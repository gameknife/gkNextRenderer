---
title: "Desktop UI Foundation"
category: design
status: 现行
owner: desktop-ui
created: 2026-08-09
last_updated: 2026-08-09
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

功能基线见 [ui-foundation-manual-baseline.md](../notes/ui-foundation-manual-baseline.md)。本轮以编译、
单元测试和 agent script 为主，不做像素比较。开发环境可用 `ui.catalog true` 打开 UI Catalog。
