---
title: "Desktop UI Foundation"
category: design
status: 现行
owner: desktop-ui
created: 2026-08-09
last_updated: 2026-08-31
---

# Desktop UI Foundation

桌面 ImGui UI 分为四层，依赖只允许向下：Application 组合产品工作流；DevTools 组合诊断功能；
Engine UI foundation 提供主题、上下文、语义控件、容器与纯 AppChrome；Renderer/Upscaler
领域目录和 mutation command 由 Engine 拥有。Engine UI 不得包含 Modules 头。

## Engine UI API

- `UiTheme`：语义颜色和每个 ImGui Context 独立安装的标准主题。
- `UiContext`：字体、metrics、surface kind、window-command capability。
- `UiScopes`：Style、ID、Disabled、Window、Child 的栈安全作用域。
- `UiWidgets`：语义 Button/IconButton、disabled tooltip、ComboOption。
- `UiContainers`：Toolbar、Overlay、Inset、Section、LabeledRow；新代码使用作用域对象。
- `AppChrome`：只绘制并返回 action/drag geometry；调用方执行窗口命令。
- `DesktopUI`：现有产品组合 API 的 Engine 归属入口；不再存在 DevTools `ProfessionalUI` facade。

Overlay 默认 `borderSize=0`，保持现代扁平样式。Padding 的 x/y 都生效，Section 缩进严格对称。
按钮尺寸未指定时由当前字体与 frame padding 决定，Toolbar 文字/图标保持居中。

## 领域选择与 mutation

Renderer 选项只来自 `RendererChoices`，具备 stable ID、固定顺序和 capability filter。
Upscaler 类型/质量只来自 `UpscalerTypes` 元数据。Settings manifest 对领域下拉只保存
`optionsProvider`，不能复制名称或假定下标等于枚举值。所有入口及 CVar 回调最终进入
`RequestRendererType` / `SetUpscalerConfiguration` 的 Engine mutation 路径。

## UI 帧与 backend

`FUiFramePolicy` 表达 capture/surface 能否绘制应用 UI，`FUiFrameResult` 只表达请求的
Statistics/Console layer。Engine 是绘制顺序唯一所有者，`FUiFrameDispatcher` 位于 backend 外。
RemoteView 的 developer mask 为 None，且不执行本地主窗口命令。

`UserInterface` 保持窄 facade，内部由 `FImGuiContextHost`、`FImGuiVulkanRenderer`、
`FUiTextureResolver` 和 `FUiFrameDispatcher` 分担 Context、Vulkan pipeline、纹理与 layer 调度。

## 稳定交互与验证基线

- 主 DockSpace ID `MyDockSpace`，以及窗口/popup 的 `###`/`##` 后缀，都是持久化协议；显示文案变化
  不能顺手改 stable ID。
- `F2` 切换 Graphics Debug，面板内 `Q` 循环 Renderer、`1..9` 切换 View Mode；grave 键切换
  Console。ImGui ini key、Settings manifest id 与 CVar 名同样视为持久化协议。
- Overlay 默认无边框；Section indent/unindent 必须对称；disabled 控件仍可显示原因；任何 collapsed、
  close 或 early-return 路径都要保持 Window/Style 栈平衡。
- MainWindow 可执行本地主窗口命令；RemoteView 与 capture surface 禁止额外窗口副作用。hidden-UI
  capture 不绘制 UI，include-UI capture 按 `FUiFrameResult` 绘制。

自动化入口：

- `assets/agentscripts/ui-foundation-renderer-{normal,compact}.agentscript.json`
- `assets/agentscripts/ui-foundation-editor-{normal,compact}.agentscript.json`

脚本验证功能和栈稳定性，不是像素基线。发布前仍需在 Windows 100%、125%、150%、200% 缩放下人工
检查 titlebar、toolbar、popup、footer、dockspace 的 hit target、裁剪与文本基线。开发环境可用
`ui.catalog true` 打开 UI Catalog。
