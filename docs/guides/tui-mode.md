---
title: "TUI 终端模式"
category: guide
status: 现行
owner: engine
created: 2026-06-25
last_updated: 2026-07-17
---

# TUI 终端模式

`--tui` 会让引擎使用隐藏窗口正常渲染，再把最终画面以 truecolor 半块字符持续刷新到当前终端。

## 快速开始

直接运行：

```bash
./gnb.sh tui --scene assets/models/playground.glb
```

也可以透传到底层 target：

```bash
./gnb.sh run gkNextRenderer --tui --load-scene=assets/models/playground.glb
```

常用参数：

```bash
./gnb.sh tui --scene assets/models/playground.glb --tui-fps 20
./gnb.sh tui --scene assets/models/playground.glb --tui-ssaa 2
./gnb.sh tui --scene assets/models/playground.glb --tui-max-cols 120 --tui-max-rows 40
./gnb.sh tui --scene assets/models/playground.glb --tui-no-input
```

## 键位

- `q`：退出
- `Ctrl+C`：退出
- `r`：截图到 `out/build/<preset>/screenshots/tui_capture.jpg`
- 字母、数字、常用标点、Space/Enter/Tab/Backspace 与方向键：转发为短按 SDL 键盘事件，复用 target 自己已有的输入绑定
- 支持 mouse-reporting 的终端还会转发鼠标移动、按键和滚轮，并映射到当前 TUI viewport

## 行为说明

- TUI 模式会隐含启用隐藏窗口、SDR 输出和 `VK_PRESENT_MODE_IMMEDIATE_KHR`
- 终端尺寸变化后会自动请求 swapchain recreate，并按 `列 x (行-1)*2` 重新贴合渲染分辨率
- 默认底部保留 1 行状态栏，显示 fps、frame、终端尺寸和快捷键提示
- 渲染日志不会再写回 stdout，而是重定向到 `out/build/<preset>/logs/tui.log`

## 已知限制

- 当前只面向支持 UTF-8、VT 和 truecolor 的现代终端，优先 Windows Terminal / wezterm / kitty / gnome-terminal
- TUI 画面使用 Unicode 半块 `▀`，不是终端图像协议
- 截图与终端刷新共享同一隐藏窗口渲染路径；如果场景本身还在大量刷日志，日志会进 `tui.log`，不会进终端
