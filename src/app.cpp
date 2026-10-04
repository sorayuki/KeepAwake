#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"KeepAwakeWindowClass";
constexpr wchar_t kWindowTitle[] = L"awake";
constexpr wchar_t kCheckboxText[] = L"awake";
constexpr int kWindowWidth = 360;
constexpr int kWindowHeight = 180;
constexpr int kCheckboxFontHeight = 32;
constexpr int kCheckboxControlId = 101;
// 文字两侧留白，以及勾选框与文字之间的间距。
constexpr int kCheckboxPadding = 12;
constexpr int kCheckboxTextSpacing = 8;
// 分开使用两个定时器，避免移动确认影响闲置隐藏计时。
constexpr UINT_PTR kCursorHideTimerId = 1;
constexpr UINT_PTR kCursorDebounceTimerId = 2;
constexpr UINT kCursorHideDelayMs = 2000;
constexpr UINT kCursorDebounceIntervalMs = 50;
constexpr LONG kCursorMovementThreshold = 8;
constexpr int kCursorMovementConfirmations = 2;

// 进程使用物理像素坐标时，按 DPI 缩放以保持界面逻辑尺寸不变。
int ScaleForDpi(int value, UINT dpi) {
  return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
}

HFONT CreateCheckboxFont(UINT dpi) {
  return CreateFontW(
      ScaleForDpi(kCheckboxFontHeight, dpi),
      0,
      0,
      0,
      FW_BOLD,
      FALSE,
      FALSE,
      FALSE,
      DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_SWISS,
      L"Segoe UI");
}

struct WindowState {
  HFONT checkbox_font = nullptr;
  HWND checkbox = nullptr;
  bool prevent_sleep = false;
  LONG_PTR window_style = 0;
  LONG_PTR window_ex_style = 0;
  WINDOWPLACEMENT placement{};
  bool is_fullscreen = false;
  bool cursor_hidden = false;
  UINT_PTR cursor_timer_id = 0;
  POINT cursor_anchor{};
  int cursor_movement_confirmations = 0;
  bool cursor_debounce_active = false;
};

bool EnableSleepBlocking(WindowState& state) {
  if (SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) == 0) {
    return false;
  }
  state.prevent_sleep = true;
  return true;
}

void DisableSleepBlocking(WindowState& state) {
  if (!state.prevent_sleep) {
    return;
  }
  SetThreadExecutionState(ES_CONTINUOUS);
  state.prevent_sleep = false;
}

// 按当前字体测量勾选框尺寸，并将其移动到客户区正中。
void LayoutCheckbox(HWND hwnd, WindowState& state, UINT dpi) {
  if (state.checkbox == nullptr || state.checkbox_font == nullptr) {
    return;
  }

  HDC dc = GetDC(hwnd);
  HFONT old_font = static_cast<HFONT>(SelectObject(dc, state.checkbox_font));
  RECT text_rect{0, 0, 0, 0};
  DrawTextW(dc, kCheckboxText, -1, &text_rect, DT_CALCRECT | DT_SINGLELINE);
  SelectObject(dc, old_font);
  ReleaseDC(hwnd, dc);

  const int text_height = text_rect.bottom - text_rect.top;
  const int width = (text_rect.right - text_rect.left) + text_height +
                    ScaleForDpi(kCheckboxTextSpacing, dpi) +
                    ScaleForDpi(kCheckboxPadding, dpi);
  const int height = text_height + ScaleForDpi(kCheckboxPadding, dpi);

  RECT client_rect{};
  GetClientRect(hwnd, &client_rect);
  MoveWindow(state.checkbox,
             (client_rect.right - width) / 2,
             (client_rect.bottom - height) / 2,
             width,
             height,
             TRUE);
}

// 重置闲置计时，并恢复本窗口的光标显示状态。
void ResetCursorHideTimer(HWND hwnd, WindowState& state) {
  if (state.cursor_timer_id != 0) {
    KillTimer(hwnd, state.cursor_timer_id);
    state.cursor_timer_id = 0;
  }
  state.cursor_hidden = false;
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void StartCursorHideTimer(HWND hwnd, WindowState& state) {
  state.cursor_timer_id = SetTimer(hwnd, kCursorHideTimerId, kCursorHideDelayMs, nullptr);
}

void CancelCursorDebounce(HWND hwnd, WindowState& state) {
  if (state.cursor_debounce_active) {
    KillTimer(hwnd, kCursorDebounceTimerId);
    state.cursor_debounce_active = false;
  }
  state.cursor_movement_confirmations = 0;
}

void ShowCursorAfterMovement(HWND hwnd, WindowState& state) {
  CancelCursorDebounce(hwnd, state);
  ResetCursorHideTimer(hwnd, state);
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
  if (state.is_fullscreen) {
    StartCursorHideTimer(hwnd, state);
  }
}

bool EnterFullscreen(HWND hwnd, WindowState& state) {
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) {
    return false;
  }

  WINDOWPLACEMENT placement{};
  placement.length = sizeof(placement);
  if (!GetWindowPlacement(hwnd, &placement)) {
    return false;
  }

  // 保存普通窗口的样式和位置，以便退出全屏时准确还原。
  const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  state.window_style = style;
  state.window_ex_style = ex_style;
  state.placement = placement;

  SetWindowLongPtrW(hwnd, GWL_STYLE, (style & WS_VISIBLE) | WS_POPUP);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style &
      ~(WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME));

  if (!SetWindowPos(hwnd,
                    HWND_TOP,
                    monitor_info.rcMonitor.left,
                    monitor_info.rcMonitor.top,
                    monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                    monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                    SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOACTIVATE)) {
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style);
    SetWindowPlacement(hwnd, &placement);
    return false;
  }

  state.is_fullscreen = true;
  ShowWindow(state.checkbox, SW_HIDE);
  SetFocus(hwnd);
  StartCursorHideTimer(hwnd, state);
  InvalidateRect(hwnd, nullptr, TRUE);
  return true;
}

void ExitFullscreen(HWND hwnd, WindowState& state) {
  if (!state.is_fullscreen) {
    return;
  }

  ResetCursorHideTimer(hwnd, state);
  SetWindowLongPtrW(hwnd, GWL_STYLE, state.window_style);
  SetWindowLongPtrW(hwnd, GWL_EXSTYLE, state.window_ex_style);
  SetWindowPos(hwnd,
               nullptr,
               0,
               0,
               0,
               0,
               SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                   SWP_NOOWNERZORDER | SWP_NOACTIVATE);
  SetWindowPlacement(hwnd, &state.placement);
  state.is_fullscreen = false;
  ShowWindow(state.checkbox, SW_SHOW);
  LayoutCheckbox(hwnd, state, GetDpiForWindow(hwnd));
  InvalidateRect(hwnd, nullptr, TRUE);
}

void ToggleFullscreen(HWND hwnd, WindowState& state) {
  if (state.is_fullscreen) {
    ExitFullscreen(hwnd, state);
  } else {
    EnterFullscreen(hwnd, state);
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (message) {
    case WM_NCCREATE: {
      auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
      auto* window_state = reinterpret_cast<WindowState*>(create_struct->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window_state));
      return TRUE;
    }

    case WM_CREATE: {
      state->checkbox_font = CreateCheckboxFont(GetDpiForWindow(hwnd));
      state->checkbox = CreateWindowExW(
          0,
          L"BUTTON",
          kCheckboxText,
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
          0,
          0,
          0,
          0,
          hwnd,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCheckboxControlId)),
          reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
          nullptr);
      if (state->checkbox != nullptr) {
        SendMessageW(state->checkbox,
                     WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->checkbox_font),
                     TRUE);
        SetFocus(state->checkbox);
        LayoutCheckbox(hwnd, *state, GetDpiForWindow(hwnd));
      }
      return 0;
    }

    case WM_DPICHANGED: {
      const UINT dpi = LOWORD(wparam);
      HFONT new_font = CreateCheckboxFont(dpi);
      if (new_font != nullptr) {
        if (state->checkbox_font != nullptr) {
          DeleteObject(state->checkbox_font);
        }
        state->checkbox_font = new_font;
        SendMessageW(state->checkbox,
                     WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->checkbox_font),
                     TRUE);
      }

      const auto* suggested_rect = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd,
                   nullptr,
                   suggested_rect->left,
                   suggested_rect->top,
                   suggested_rect->right - suggested_rect->left,
                   suggested_rect->bottom - suggested_rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      LayoutCheckbox(hwnd, *state, dpi);
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;
    }

    case WM_COMMAND:
      if (LOWORD(wparam) == kCheckboxControlId && HIWORD(wparam) == BN_CLICKED) {
        const bool checked =
            SendMessageW(state->checkbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (checked) {
          if (!EnableSleepBlocking(*state)) {
            SendMessageW(state->checkbox, BM_SETCHECK, BST_UNCHECKED, 0);
            MessageBoxW(hwnd,
                        L"Failed to request the system stay awake.",
                        kWindowTitle,
                        MB_OK | MB_ICONERROR);
          }
        } else {
          DisableSleepBlocking(*state);
        }
        return 0;
      }
      break;

    case WM_LBUTTONDBLCLK:
      ToggleFullscreen(hwnd, *state);
      return 0;

    case WM_NCLBUTTONDBLCLK:
      if (wparam == HTCAPTION) {
        ToggleFullscreen(hwnd, *state);
        return 0;
      }
      break;

    case WM_SYSCOMMAND:
      switch (wparam & 0xFFF0) {
        case SC_MAXIMIZE:
          if (!state->is_fullscreen) {
            EnterFullscreen(hwnd, *state);
          }
          return 0;
        case SC_RESTORE:
          if (state->is_fullscreen) {
            ExitFullscreen(hwnd, *state);
            return 0;
          }
          break;
      }
      break;

    case WM_KEYDOWN:
      if (state->is_fullscreen && wparam == VK_ESCAPE) {
        ExitFullscreen(hwnd, *state);
        return 0;
      }
      break;

    case WM_MOUSEMOVE:
      if (state->is_fullscreen) {
        if (!state->cursor_hidden) {
          ShowCursorAfterMovement(hwnd, *state);
        } else {
          // 忽略小幅抖动；超出隐藏位置的死区后才开始确认移动。
          POINT cursor_position{};
          if (GetCursorPos(&cursor_position)) {
            const LONG dx = cursor_position.x - state->cursor_anchor.x;
            const LONG dy = cursor_position.y - state->cursor_anchor.y;
            if (dx * dx + dy * dy >=
                    kCursorMovementThreshold * kCursorMovementThreshold &&
                !state->cursor_debounce_active) {
              state->cursor_movement_confirmations = 0;
              state->cursor_debounce_active =
                  SetTimer(hwnd,
                           kCursorDebounceTimerId,
                           kCursorDebounceIntervalMs,
                           nullptr) != 0;
            }
          }
        }
        return 0;
      }
      break;

    case WM_TIMER:
      if (state->is_fullscreen && wparam == state->cursor_timer_id &&
          state->cursor_timer_id != 0) {
        KillTimer(hwnd, state->cursor_timer_id);
        state->cursor_timer_id = 0;
        state->cursor_hidden = true;
        state->cursor_movement_confirmations = 0;
        GetCursorPos(&state->cursor_anchor);
        SetCursor(nullptr);
        return 0;
      }
      if (state->is_fullscreen && wparam == kCursorDebounceTimerId) {
        // 连续多次采样都超出死区才显示光标，过滤短暂的碰撞抖动。
        POINT cursor_position{};
        if (!GetCursorPos(&cursor_position)) {
          CancelCursorDebounce(hwnd, *state);
          return 0;
        }

        const LONG dx = cursor_position.x - state->cursor_anchor.x;
        const LONG dy = cursor_position.y - state->cursor_anchor.y;
        if (dx * dx + dy * dy <
            kCursorMovementThreshold * kCursorMovementThreshold) {
          CancelCursorDebounce(hwnd, *state);
        } else if (++state->cursor_movement_confirmations >=
                   kCursorMovementConfirmations) {
          ShowCursorAfterMovement(hwnd, *state);
        } else {
          state->cursor_debounce_active =
              SetTimer(hwnd,
                       kCursorDebounceTimerId,
                       kCursorDebounceIntervalMs,
                       nullptr) != 0;
          if (!state->cursor_debounce_active) {
            state->cursor_movement_confirmations = 0;
          }
        }
        return 0;
      }
      break;

    case WM_SETCURSOR:
      if (state->is_fullscreen && state->cursor_hidden && LOWORD(lparam) == HTCLIENT) {
        SetCursor(nullptr);
        return TRUE;
      }
      break;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);

      RECT client_rect{};
      GetClientRect(hwnd, &client_rect);
      if (state->is_fullscreen) {
        FillRect(dc, &client_rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
      } else {
        FillRect(dc, &client_rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
      }

      EndPaint(hwnd, &paint);
      return 0;
    }

    case WM_SIZE:
      LayoutCheckbox(hwnd, *state, GetDpiForWindow(hwnd));
      InvalidateRect(hwnd, nullptr, TRUE);
      return 0;

    case WM_DESTROY:
      DisableSleepBlocking(*state);
      ResetCursorHideTimer(hwnd, *state);
      PostQuitMessage(0);
      return 0;

    case WM_NCDESTROY:
      if (state != nullptr && state->checkbox_font != nullptr) {
        DeleteObject(state->checkbox_font);
        state->checkbox_font = nullptr;
      }
      return DefWindowProcW(hwnd, message, wparam, lparam);

    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }

  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WindowState window_state;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_DBLCLKS;
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kWindowClassName;

  if (RegisterClassExW(&window_class) == 0) {
    MessageBoxW(nullptr,
                L"Failed to register the application window.",
                kWindowTitle,
                MB_OK | MB_ICONERROR);
    return 1;
  }

  HWND window = CreateWindowExW(
      0,
      kWindowClassName,
      kWindowTitle,
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      ScaleForDpi(kWindowWidth, GetDpiForSystem()),
      ScaleForDpi(kWindowHeight, GetDpiForSystem()),
      nullptr,
      nullptr,
      instance,
      &window_state);

  if (window == nullptr) {
    MessageBoxW(nullptr,
                L"Failed to create the application window.",
                kWindowTitle,
                MB_OK | MB_ICONERROR);
    return 1;
  }

  SetWindowTextW(window, kWindowTitle);
  ShowWindow(window, show_command);
  UpdateWindow(window);

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  return static_cast<int>(message.wParam);
}
