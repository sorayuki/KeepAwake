# keepawake

一个简单的 Windows 防休眠小工具。/ A simple Windows keep-awake utility.

## 功能 / Features

**中文**

- 窗口内一个 `awake` 复选框：勾选后阻止系统进入睡眠/休眠，取消勾选则恢复正常休眠。
- 双击窗口空白处或标题栏、最大化按钮可进入全屏（黑色背景）；按 `Esc` 或还原窗口退出全屏。
- 全屏状态下鼠标静止约 2 秒后自动隐藏光标，移动鼠标即可重新显示。
- 关闭程序时自动恢复系统的正常休眠行为。

**English**

- A single `awake` checkbox in the window: check it to prevent the system from sleeping/hibernating, uncheck it to restore normal sleep behavior.
- Double-click the window background or title bar, or use the maximize button, to enter fullscreen (black background); press `Esc` or restore the window to exit.
- In fullscreen, the cursor is hidden automatically after about 2 seconds of inactivity and reappears when the mouse moves.
- Normal system sleep behavior is restored automatically when the program exits.

## 构建 / Build

```sh
cmake -B build
cmake --build build --config Release
```

需要 Windows 和 C++17 编译器。/ Requires Windows and a C++17 compiler.
