# Desktop UI foundation baseline

本文记录桌面 UI 基础层重构必须保持的交互契约。自动脚本只做功能与稳定性验证；截图用于人工抽查，
不作为像素级或严格视觉回归基线。

## 自动化入口

- `ui-foundation-renderer-normal.agentscript.json`：1280×720，Renderer/Upscaler CVar、Graphics Debug、Console。
- `ui-foundation-renderer-compact.agentscript.json`：800×600，紧凑布局与 Graphics Debug。
- `ui-foundation-editor-normal.agentscript.json`：1440×900，Editor、Graphics Debug、Console。
- `ui-foundation-editor-compact.agentscript.json`：900×640，Editor 紧凑布局与 Upscaler CVar。

## 稳定标识与快捷键

- 主 DockSpace ID：`MyDockSpace`；应用窗口、DockSpace、popup 的 `###`/`##` 后缀不得因显示文案变化而改变。
- `F2` 切换 Graphics Debug；面板可见时 `Q` 循环 Renderer，数字键 `1..9` 切换 View Mode。
- `` ` ``（grave）切换 Console；`F1`、`F3`、`F4` 的既有 Engine 快捷键语义保持不变。
- ImGui ini/state key 和 Settings manifest 中的 `id`、CVar 名称视为持久化协议。

## 容器契约

- Overlay 默认无边框；只有显式 `BorderSize > 0` 才显示边框，`Padding.x/y` 均生效。
- Section 的 indent/unindent 数值严格对称，连续绘制不得发生 1px 横向漂移。
- disabled 控件仍可显示说明原因的 tooltip。
- FloatingPanel 无论 collapsed、关闭或提前返回，Window/Style 栈都必须平衡。

## Surface 行为

| Surface | Application UI | Developer layer | 本地主窗口命令 |
| --- | --- | --- | --- |
| MainWindow | 按应用策略绘制 | 按 `FUiFrameResult` | 允许 |
| RemoteView | 绘制远端内容 | 显式 layer mask | 禁止 |
| hidden-UI capture | 禁止 | 禁止 | 禁止 |
| include-UI capture | 允许 | 按 frame result | 禁止额外副作用 |

## DPI 人工矩阵

Windows 100%、125%、150%、200% 为发布前人工抽查项，检查 titlebar、toolbar、popup、footer、
dockspace 的 hit target、裁剪和文本基线。当前开发机不是 Windows，因此本轮不伪造通过结论；
该矩阵明确保留为人工验证项，不阻塞功能重构。视觉差异只要求维持现代扁平风格、无意外边框、
按钮居中，不做像素级判定。
