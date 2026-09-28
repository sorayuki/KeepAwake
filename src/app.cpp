#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

namespace {

constexpr wchar_t kWindowClassName[] = L"KeepAwakeWindowClass";
constexpr wchar_t kWindowTitle[] = L"awake";
constexpr wchar_t kLabelText[] = L"awake";
constexpr int kWindowWidth = 360;
constexpr int kWindowHeight = 180;
constexpr int kLabelHeight = 32;

class ExecutionStateGuard {
public:
  ExecutionStateGuard()
      : active_(SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0) {}

  ~ExecutionStateGuard() {
    if (active_) {
      SetThreadExecutionState(ES_CONTINUOUS);
    }
  }

  ExecutionStateGuard(const ExecutionStateGuard&) = delete;
  ExecutionStateGuard& operator=(const ExecutionStateGuard&) = delete;

  [[nodiscard]] bool is_active() const { return active_; }

private:
  bool active_;
};

struct WindowState {
  HWND label = nullptr;
  HFONT label_font = nullptr;
};

void CenterLabel(HWND parent, HWND label) {
  RECT client_rect{};
  GetClientRect(parent, &client_rect);

  const int client_width = client_rect.right - client_rect.left;
  const int client_height = client_rect.bottom - client_rect.top;
  const int label_width = client_width;
  const int label_x = 0;
  const int label_y = (client_height - kLabelHeight) / 2;

  MoveWindow(label, label_x, label_y, label_width, kLabelHeight, TRUE);
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
      state->label = CreateWindowExW(
          0,
          L"STATIC",
          kLabelText,
          WS_CHILD | WS_VISIBLE | SS_CENTER,
          0,
          0,
          0,
          0,
          hwnd,
          nullptr,
          reinterpret_cast<LPCREATESTRUCTW>(lparam)->hInstance,
          nullptr);

      if (state->label == nullptr) {
        return -1;
      }

      state->label_font = CreateFontW(
          32,
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

      SendMessageW(
          state->label, WM_SETFONT, reinterpret_cast<WPARAM>(state->label_font), TRUE);
      CenterLabel(hwnd, state->label);
      return 0;
    }

    case WM_SIZE:
      if (state != nullptr && state->label != nullptr) {
        CenterLabel(hwnd, state->label);
      }
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    case WM_NCDESTROY:
      if (state != nullptr && state->label_font != nullptr) {
        DeleteObject(state->label_font);
        state->label_font = nullptr;
      }
      return DefWindowProcW(hwnd, message, wparam, lparam);

    default:
      return DefWindowProcW(hwnd, message, wparam, lparam);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  ExecutionStateGuard power_guard;
  if (!power_guard.is_active()) {
    MessageBoxW(nullptr,
                L"Failed to request the system stay awake.",
                kWindowTitle,
                MB_OK | MB_ICONERROR);
    return 1;
  }

  WindowState window_state;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
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
      kWindowWidth,
      kWindowHeight,
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
