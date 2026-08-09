---
title: "gkNextEditor / gkNextRenderer 桌面 UI 基础层重构计划"
category: plan
status: 待实施
owner: desktop-ui
created: 2026-08-09
last_updated: 2026-08-09
---

# gkNextEditor / gkNextRenderer 桌面 UI 基础层重构计划

本计划是发布期 UI 功能收口之后的架构治理，不重新设计 gkNextEditor 或
gkNextRenderer 的产品形态。默认要求是视觉、快捷键、状态持久化和运行时副作用保持不变；
只有先被特征化验证覆盖的明确缺陷，才允许在重构过程中一并修正。

> 审计基线：2026-08-09，提交 1e838daf。盘点范围包括 gkNextEditor、
> gkNextRenderer、Engine/Runtime/Editor、Engine/Runtime/Interface、
> Engine/Utilities、Modules/DevTools，以及它们直接使用的渲染器、Upscaler 和 CVar 接口。

---

## 1. 结论摘要

当前问题不是单纯“函数太多”，而是缺少稳定的分层和语义控件，导致样式、容器、
领域选择和运行时副作用在不同位置各自实现：

1. ProfessionalUI 放在 DevTools 模块，却被发布应用作为通用 UI 基础层使用。
   它同时负责主题、按钮、Panel、应用标题栏、显存/帧率状态栏、热重载、截图和
   DevTools 面板开关。通用视觉层因此反向依赖可选开发模块。
2. gkNextEditor、gkNextRenderer、Gizmo、Sequencer、Material Editor 各自维护一套
   Button、Toolbar、Combo、Tooltip、表单行和窗口样式；ProfessionalUI 内部也有多套
   位置命名的按钮实现。
3. “可选项”和“改值后的行为”没有统一所有者。Renderer 类型、Upscaler 类型和质量档位
   在 Viewport、Settings、GraphicsDebugPanel 和 JSON 中重复列举，部分入口只写
   UserSettings，部分入口立即切 Renderer 或重建 SwapChain。
4. UI 生命周期用 OnRenderUI() 的 bool 隐式表达“是否还要绘制默认 DevTools UI”。
   Editor 的返回值恒为 true，同时 EditorInterface 直接调用 FUiDevPanels 单例补画 Console；
   Renderer 返回 false 依赖 Engine 再画。这是跨层的隐藏协议。
5. DPI、字体和控件尺寸存在多套计算方法，且通用绘制函数直接读取 NextEngine 单例。
   Remote ImGui 又拥有独立 Context，因此全局可变指标和隐式主窗口假设会成为后续缺陷源。
6. ProfessionalUI 的手工 Begin/End、Push/Pop 已出现可确认的契约问题：
   BeginOverlayPanel 忽略 Padding.y，BorderAlpha 在边框宽度为 0 时无效；
   BeginSection 缩进 5px，而 EndSection 只退回 4px。

建议用“Engine 通用 UI 基础层 + DevTools 诊断组合层 + Application 产品组合层 +
Engine 领域命令/选项目录”四层收口，并通过兼容 facade 分阶段迁移，避免一次性改写全部 UI。

---

## 2. 审计快照

### 2.1 静态规模

下表只是发现重复热点的快照，不是要求消灭所有原生 ImGui 调用。自定义图表、复杂交互和
领域控件继续直接使用 ImGui 是合理的。

| 项目 | 审计范围内数量 |
| --- | ---: |
| ImGui Button / SmallButton / ArrowButton / ImageButton | 62 |
| InvisibleButton | 6 |
| BeginCombo / Combo | 24 |
| Selectable | 17 |
| 原生 SetTooltip / BeginTooltip | 34 |
| PushStyleColor / PushStyleVar | 208 |
| ProfessionalUI 的按钮调用 | 19 |
| ProfessionalUI 的 Panel/Section Begin 调用 | 26 |

主要热点：

| 文件 | 行数快照 | 当前职责 |
| --- | ---: | --- |
| src/Modules/DevTools/ProfessionalUI.cpp | 1282 | 主题、控件、应用 chrome、状态栏、Panel、诊断小部件 |
| src/Application/Render/gkNextRenderer/gkNextRenderer.cpp | 2630 | 游戏逻辑、UI 状态、所有 Renderer 界面和运行时副作用 |
| src/Application/Editor/gkNextEditor/Panels/PropertyWidgets.cpp | 1265 | 属性布局、领域编辑器、undo/reset、字体获取 |
| src/Application/Editor/gkNextEditor/Panels/SequencerPanel.cpp | 1295 | Sequencer 状态、工具栏、轨道和时间轴绘制 |
| src/Engine/Runtime/Editor/UserInterface.cpp | 1169 | ImGui 生命周期、Vulkan backend、字体、纹理、输入、DevTools 分发 |
| src/Modules/DevTools/GraphicsDebugPanel.hpp | 436 | Renderer 目录、快捷键、状态切换、完整面板绘制，几乎全部 inline |
| src/Modules/DevTools/UiDevPanels.cpp | 1106 | Console、统计、浮动面板和诊断绘制 |

### 2.2 依赖现状

- gkNextEngine 先于 Modules 构建；每个 Module 单向链接 gkNextEngine。
- DevTools 是标准桌面 runtime module，但仍然是可选模块，不应成为 Engine 通用 UI 的所有者。
- ProfessionalUI.hpp 当前被 gkNextEditor、gkNextRenderer、ScadStudio、ScadLibrary 和
  DevTools 自身直接包含。
- UserInterface 初始化样式时调用 IDebugUiProvider::ApplyUiStyle()；
  DevTools provider 再调用 ApplyProfessionalTheme()。RemoteImGuiSession 也重复这条路径。

这意味着“是否安装标准主题”目前取决于“是否安装 DevTools provider”，依赖方向与职责均不合理。

---

## 3. 详细问题清单

### A. ProfessionalUI 同时跨越四层

ProfessionalUI.cpp 直接包含 UiDevPanels、NextEngine、ScreenShotService 和
UserInterface。其职责可拆成四类：

| 当前职责 | 正确所有者 |
| --- | --- |
| 颜色、字体角色、间距、圆角、Button/Tooltip/Panel 外观 | Engine UI 基础层 |
| 标题栏和底栏的几何骨架 | Engine UI 的 app chrome |
| Console、Stats、VRAM、热重载、截图按钮 | DevTools 组合层 |
| 切 Renderer、重建 SwapChain、请求窗口最小化/关闭 | Engine 领域服务或 Application |

DrawStandardBottomBar 一次性读取内存统计、帧率和热重载状态，切换 Console/Stats，
并触发截图、Shader/C++ reload。它不是通用底栏。

DrawAppTitleBar 接收大量回调，但内部仍直接调用
NextEngine::ConfigureCustomTitleBarDrag()，并通过 Engine/UserInterface 获取品牌纹理。
Remote UI 虽然把窗口控制回调置空，函数仍会绘制主窗口 chrome，并配置本地主窗口拖拽区。
标题栏需要明确 surface 能力，且只能返回 action；窗口副作用由调用者执行。

### B. Button、Tooltip 和 Toolbar 是“按位置复制”，不是“按语义复用”

ProfessionalUI 内已有：

- DrawWindowControlButton
- IconButton
- GhostButton
- ToolbarButton
- DrawFlatViewportButton
- ModeRailButton
- FloatingPanel 内另一份手写 Close Button

Editor 主 Toolbar 又手工 PushStyle 绘制绿色 Play，Sequencer 和 Material Editor 使用原生
FontAwesome Button，GizmoController 自建窗口、背景、RadioButton 和 Tooltip。
Renderer Viewport 和 Editor Viewport 都维护自己的按钮宽度、Combo 宽度、间距和 compact
判断；Editor 代码甚至明确写着“匹配 gkNextRenderer 的 toolbar metrics”。

问题根源是现有 API 以位置命名，而不是表达 Primary、Ghost、Toolbar、Danger、Active、
IconOnly 等视觉语义。Toolbar 也没有统一容器，只统一了其中一颗按钮。

Tooltip 同时存在 ProfessionalUI::DrawTooltip、原生 SetTooltip/BeginTooltip，以及
Engine/Utilities/ImGui.hpp 的 BUTTON_TOOLTIP 宏。禁用项 hover、换行宽度和延迟行为因入口不同
而不一致。

### C. Panel、Section 和样式栈契约脆弱

- BeginOverlayPanel、BeginInsetPanel、BeginSection 要求调用者手工配对 End。
- BeginFloatingPanel 在 ImGui::Begin 返回 false 时会自行 End 并 Pop；
  返回 true 时才要求调用者 EndFloatingPanel。其注释称“匹配 ImGui::Begin 语义”，
  但平衡规则实际上不同。
- PushToolWindowStyle 等接口依赖调用者记住 7 个 StyleVar、8 个 StyleColor 的准确数量。
- BeginOverlayPanel 把 Padding.y 写死为 8，而配置默认值和调用者设置均无法生效；
  同时 WindowBorderSize 为 0，公开的 BorderAlpha 不会产生边框。
- BeginSection 使用 Indent(5)，EndSection 使用 Unindent(4)，同一窗口内连续 Section 会漂移。
- BeginFormRow 通过 GetContentRegionAvail + CursorPosX 推导绝对 SameLine 位置；
  PropertyWidgets、MaterialEditor 和 Renderer Settings 又各有不同的 label/value 对齐算法。

应使用作用域对象保证样式与 Begin/End 成对，并统一容器契约；不能只把现有 Push/Pop
改名后继续暴露。

### D. Combo 的重复分成“外观重复”和“领域目录重复”

外观层重复包括 popup padding、Selectable 高度、默认焦点、箭头、预览文字和 tooltip。
领域层重复更危险：

- GraphicsDebugPanel 有一套 RendererOptions 和 RT 过滤。
- gkNextEditor ViewportOverlay 有 BuildSupportedRendererList，同时考虑 RT 和
  AmbientCube budget，排列顺序又不同。
- gkNextRenderer Viewport 复用 GraphicsDebugPanel 的数组。
- SettingsPanel 的 fallback manifest 和 assets/configs/ui/settings_panel.json
  分别硬编码 Renderer、Upscaler、质量档位名称。
- Renderer Settings 又有本地 quality 数组；Viewport 则使用 UpscalerTypes.hpp 的现有元数据。

Renderer 枚举数值顺序、GraphicsDebugPanel 显示顺序、Editor Viewport 显示顺序并不一致。
更改行为也不一致：有的入口只写 UserSettings，等待 Engine 下一帧切换；有的立即调用
SwitchLogicRenderer；Upscaler 入口则直接 RequestRecreateSwapChain。

通用 Combo 只能统一外观和 item 交互，不能拥有 Renderer/Upscaler 逻辑。
Renderer/Upscaler 的可选项目录、能力过滤、fallback 和 mutation side effect 必须由
Engine 领域接口统一。

### E. UI 帧生命周期是隐藏的 bool 协议

Engine 当前执行：

1. 调用 GameInstance::OnRenderUI()，得到 uiHandled。
2. uiHandled 为 false 时，UserInterface::Render() 再调用 DevTools provider 绘制默认面板。

Editor 始终返回 true，因此 Engine 不再绘制默认 DevTools UI；EditorInterface 随后直接调用
FUiDevPanels::Get().RenderConsoleOverlay() 补画 Console。Renderer 正常帧返回 false，
依赖 Engine 补画；截图/录像期间又返回 true 来抑制后续 UI。

同一个 bool 同时表达“应用画完了”“不要画默认 DevTools”“截图时抑制 UI”等多种语义，
RemoteImGuiSession 甚至忽略返回值。应替换为显式 FUiFrameResult layer mask，并由 Engine
在唯一的 dispatcher 中排序 application、developer overlay、console、profile 等层。

### F. DPI、字体和 Context 责任不统一

当前至少存在：

- UserInterface::UiScale() 和 framebuffer 输入坐标转换；
- ImGuiScaling::GetViewportUiScale()；
- gkNextRenderer::UpdateUiScaledMetrics() 的 SwapChain content scale + font ratio；
- gkNextEditor 的固定 title/footer/toolbar/control 像素；
- 各 Panel 自己计算的 7px padding、3px 间隙和 frame height。

物理坐标变换、视觉密度和响应式布局被混在一起。Renderer 还有只被初始化/缩放、没有实际消费的
IconSize、PaletteSize、ButtonSize、BuildBarWidth、SideBarWidth、ShortcutSize 候选状态，
FRendererUiState::bigFont 也未进入实际绘制路径。

UserInterface 同时负责默认字体、三段 FontAwesome merge、中文 fallback 和 title font；
Editor、Renderer 又各自加载额外字号。通用 UI 函数通过 NextEngine 单例取 title/default font，
而 Remote ImGui 使用共享 FontAtlas 的独立 Context。后续基础层必须 context-local，
不能依赖进程级可变布局状态。

### G. UserInterface 把 backend、资源和开发面板调度混在一起

UserInterface 的公开头同时暴露 ImGui internal、Vulkan pipeline/render pass、字体、bindless
texture ID、输入、multi-viewport 和 debug statistics。构造函数需要 Engine、CommandPool、
SwapChain、DepthBuffer、UserSettings 和两个 UI callback，导致只测试 theme/context 也会接触
GPU backend 生命周期。

其中 Render(statistics, profiler, scene, suppressStatisticsOverlay) 名为 Render，实际只调用
IDebugUiProvider::DrawUiPanels；真正的 ImGui::Render、draw-data prepare 和 Vulkan submit
在另外的方法中。Scene 参数在当前实现中也没有参与该调用。Backend 因此知道
Statistics、FrameProfiler 和 DevTools 调度，职责与命名都不清晰。

重构后保留 UserInterface 作为 Engine 的窄 facade，但内部至少分开：

- ImGuiContextHost：Context、font atlas、input、multi-viewport、frame begin/end；
- ImGuiVulkanRenderer：surface、pipeline、framebuffer、render buffer、draw-data submit；
- UiTextureResolver：bindless texture ID、异步请求与尺寸 cache；
- UiFrameDispatcher：application/engine/dev layer 顺序，位于 backend 之外。

公开头应通过 forward declaration/PImpl 隔离 imgui_internal 和 Vulkan 具体实现。这个拆分以
职责和可测试性为目标，不要求把每个十几行 helper 都变成新类。

### H. 大文件、诊断小部件和遗留工具重复

- ProfileDebugOverlay.cpp 与 GraphicsDebugPanel.hpp 各有 DrawSectionHeader、
  DrawValueRow 和相同硬编码颜色；ProfessionalUI 也有类似 PanelHeader/LabelValue/Badge。
- FormatBytes 在 Renderer 和 ProfileDebugOverlay 重复。
- GraphicsDebugPanel 把领域目录、快捷键和 400 多行 UI 放在 header inline 实现。
- EditorUtils 混合 modal、style/resources/about 辅助窗、Dear ImGui demo 派生代码和多个
  仅声明/定义、没有产品调用点的候选 helper。
- EditorInterface::DrawIndicator 与 Engine UserInterface::DrawIndicator 重复，且当前没有调用点。
- ProfessionalUI 暴露的 LabelOver、DrawLabelValue、DrawMetricCard、部分 Font/Brand/Status
  helper 没有外部消费者或只有内部消费者。

这些项目适合先做低风险清理，但必须以全仓调用搜索和编译为删除依据，不能仅凭本次快照。

---

## 4. 目标与非目标

### 4.1 目标

1. Engine 提供不依赖 DevTools、Application 或 NextEngine 单例的通用 ImGui UI 基础层。
2. gkNextEditor、gkNextRenderer、Remote UI 使用同一主题、字体角色、尺寸 token 和语义控件。
3. Button、Tooltip、Toolbar、Panel、Labeled Row 等常见外观只有一个规范实现。
4. Renderer/Upscaler 选择的目录、能力过滤、fallback 和副作用只有一个领域所有者。
5. Application 只组合工作流；DevTools 只组合 Console、Stats、Profiler、CVar 等开发功能。
6. 每个迁移阶段可单独构建、截图、回退，不要求大爆炸式改写。

### 4.2 非目标

- 不改变已经确认的发布版视觉语言、菜单信息架构或产品功能。
- 不引入 XML、反射式 UI schema 或一套新的 declarative UI framework。
- 不追求“业务代码零原生 ImGui 调用”；特殊时间轴、曲线、网格、图表继续直接绘制。
- 不把 DevTools、截图、热重载、Renderer 业务逻辑搬进 Engine UI 基础控件。
- 不创建一个包含所有领域参数的万能 Toolbar 或万能 Property Editor。
- 本轮首批迁移聚焦 gkNextEditor 和 gkNextRenderer；ScadStudio、ScadLibrary 只在删除兼容
  facade 前做必要迁移。
- Settings 继续遵守 [Editor Settings 与 CVar 架构](../designs/editor-settings-and-cvars.md)：
  manifest 只描述呈现，值、校验、持久化和 onChanged 仍由 CVarSystem 拥有。

---

## 5. 目标分层

推荐目录是实现起点，不是必须逐字采用的 API 名；依赖边界是硬约束。

~~~text
src/Engine/Runtime/Editor/UI/
├── UiTheme.*           # semantic colors、typography、FUiMetrics、ApplyTheme
├── UiContext.*         # 当前 ImGui context 的 fonts、scale、texture/surface capabilities
├── UiScopes.*          # style、ID、disabled、window/child 的 RAII scopes
├── UiWidgets.*         # Button、IconButton、Tooltip、ComboOption、LabeledRow
├── UiContainers.*      # Toolbar、OverlayPanel、ToolWindow、FloatingPanel、Section
└── AppChrome.*         # 纯标题栏/底栏骨架，只返回 action，不执行 Engine/OS 副作用

src/Engine/Runtime/Editor/
├── UserInterface.*     # 对 Engine 保持窄 facade
├── ImGuiContextHost.*  # context/font/input/multi-viewport
├── ImGuiVulkanRenderer.* # surface/pipeline/draw-data submit
└── UiTextureResolver.* # bindless texture 与尺寸 cache

src/Modules/DevTools/UI/
├── DeveloperStatusBar.*
├── DiagnosticWidgets.*
└── UiDevPanels.*

src/Engine/Rendering/
└── RendererChoices.*   # renderer/upscaler 目录、能力过滤、fallback

src/Engine/Runtime/Engine.*      # selection commands：统一校验、设置写入、
                                # renderer switch / swapchain recreate
~~~

依赖只能是：

~~~text
gkNextEditor / gkNextRenderer ──> Engine UI foundation
             │                 └> Engine renderer/config services
             └──────────────────> DevTools composites（可选）

DevTools ────────────────────────> Engine UI foundation
Engine UI foundation ────────────> ImGui + 窄接口服务
Engine ──────────────────────────X DevTools
~~~

### 5.1 基础类型

- FUiTheme：语义颜色和字体角色；不读取 Engine，不绘制业务内容。
- FUiMetrics：spacing、radius、control height、toolbar height、title/footer height、
  overlay alpha 等不可变 token，由当前 Context 的 scale/density 生成。
- FUiContext：显式传递 Font roles、metrics、texture resolver、surface kind、
  window-command capability。MainWindow 和 RemoteView 分别构造，不共享可变尺寸状态。
- ScopedStyle/ScopedWindow/ScopedChild/ScopedId/ScopedDisabled：
  析构时恢复栈，消除调用者记忆 Pop 数量的要求。

### 5.2 语义控件

统一 Button primitive 使用语义选项，而不是按位置复制实现：

- variant：Primary、Secondary、Ghost、Toolbar、Danger。
- tone/state：Default、Accent、Success、Warning；active、selected、disabled。
- content：Text、Icon、IconAndText。
- size policy：Content、Square、Fixed、Fill。
- tooltip：统一支持 disabled item hover、wrapped text 和可选延迟。

IconButton 是薄封装。ModeRail 和窗口控制可保留专门 composition，因为它们具有额外几何
（active strip、系统按钮 hit target），但其底层颜色、状态和 tooltip 仍复用同一 primitive。

Toolbar 统一的是容器：surface、padding、spacing、separator、compact/overflow 策略；
每个产品仍在调用点按 immediate-mode 顺序组合按钮和 Combo。

Combo 统一 popup chrome、option row、默认焦点和 tooltip；选项迭代与 mutation 留在领域调用方，
避免把 std::function 和任意业务状态塞进通用控件。

LabeledRow/PropertyGrid 显式接收 label width policy 和 trailing action slot。
不要再用 GetCurrentTable() 隐式猜测当前布局环境；PropertyWidgets 的 undo/reset 和
Material Editor 的领域编辑仍留在各自模块。

### 5.3 Chrome 与开发工具

AppChrome 只绘制品牌、菜单槽位、右侧槽位、窗口按钮和底栏区域，返回类似
minimize/maximize/close 的 action 以及 titlebar hit geometry。Application 决定是否执行
窗口命令，Engine 平台层只消费 geometry。

DeveloperStatusBar 由 DevTools 拥有，组合 FPS、VRAM、Console、Stats、Shader/C++ reload
和截图入口。通用 AppBottomBar 不认识这些概念。

### 5.4 UI 帧协议

用显式结果替代 bool uiHandled。推荐由 Engine 固定 application、engine overlay、profile、
developer stats、console 的绘制顺序。Engine 在绘制前构造 FUiFramePolicy，表达 surface、
capture 和是否允许 application UI；GameInstance 绘制后返回默认后置 developer layer 的
显式 mask，Engine 再用 policy 与该 mask 求交集。FUiFrameResult 至少区分：

- DeveloperStatistics
- Console

Application、engine overlay 和 profile 的调用由 Engine dispatcher 及其既有 show flag 管理；
截图/录像是否跳过 application UI 则是独立的 frame policy。FUiFrameResult 不再兼任
“应用有没有绘制”或“正在截图”。
Engine dispatcher 必须：

1. Engine 是顺序的唯一所有者；
2. Editor 不再直接访问 FUiDevPanels 单例来补偿 bool；
3. MainWindow、RemoteView、普通截图和 include-ui 截图的语义可单独表达；
4. 返回值不能再被 RemoteImGuiSession 静默忽略。

---

## 6. 现有 API 迁移表

| 当前入口 | 目标 | 处置 |
| --- | --- | --- |
| EColor / Color / ApplyProfessionalTheme | Engine UiTheme | 搬入 Engine，按 ImGui Context 安装 |
| GetDefaultFont / GetTitleFont | FUiContext font roles | 消除 NextEngine 单例读取 |
| IconButton / GhostButton / ToolbarButton / DrawFlatViewportButton | Button + FUiButtonOptions | 合并状态与样式，保留薄 IconButton |
| ModeRailButton | RailItem composition | 复用 Button 状态，保留 active strip |
| DrawWindowControlButton / floating close button | WindowControl composition | 统一 hit target、tooltip、hover tone |
| Push/PopViewportToolbarStyle、PopupStyle、ToolWindowStyle | scoped style/container | 兼容 facade 只临时转发 |
| DrawViewportComboOption | ComboOption | 只统一 option chrome |
| Begin/EndOverlayPanel、InsetPanel、FloatingPanel、Section | scoped containers | 统一 Begin/End 规则，修复 padding/indent 契约 |
| BeginFormRow + 各地 BeginPropertyRow/DrawSettingRow | LabeledRow / PropertyGrid | 统一 label/value/trailing slot |
| DrawAppTitleBar / DrawBottomBar | Engine AppChrome | 纯绘制并返回 action |
| DrawStandardBottomBar | DevTools DeveloperStatusBar | 移出通用 Theme |
| Sparkline、diagnostic badge/card | DevTools DiagnosticWidgets | 与产品通用控件分离 |
| GraphicsDebugPanel::RendererOptions | Engine RendererChoices | 单一 capability-filtered catalog |
| Settings JSON 中领域 options | 命名 options provider，读取 Engine domain catalog | JSON 只保留 provider ID，不复制 Renderer/Upscaler 字符串 |
| BUTTON_TOOLTIP | Tooltip compatibility shim | 首批应用迁移后弃用；全仓迁移前不强删 |

---

## 7. 分阶段执行计划

每一阶段都应是独立提交或一组小提交。跨阶段同时修改 ProfessionalUI、UI 生命周期和
Renderer 选择行为，会让视觉回归与功能回归无法归因。

### Stage 0：固化行为基线与契约

- [ ] UI-0.1 在 assets/agentscripts/ 增加可重复运行的四个脚本：
  ui-foundation-editor-normal.agentscript.json、ui-foundation-editor-compact.agentscript.json、
  ui-foundation-renderer-normal.agentscript.json、ui-foundation-renderer-compact.agentscript.json；
  各自覆盖 Settings、Renderer/Upscaler popup、Console/Stats 等对应状态。
- [ ] UI-0.2 在 Windows 100%、125%、150%、200% 显示缩放下记录 titlebar、toolbar、
  popup、footer 和 dockspace 的 hit/clip 情况。不能自动化的 DPI 项明确标为人工矩阵。
- [ ] UI-0.3 记录 MainWindow、RemoteView、截图隐藏 UI、截图包含 UI 四种 surface 行为。
- [ ] UI-0.4 为 Overlay padding/border、Section indent、disabled tooltip、FloatingPanel
  close/Begin-End 行为增加最小特征化测试或专用 UI catalog case。
- [ ] UI-0.5 在 docs/notes/ui-foundation-manual-baseline.md 记录 DPI 人工矩阵，以及必须保持
  稳定的 ImGui window ID、dockspace ID、popup ID、快捷键和 ini/state persistence key。

验收：

- agent script 可重生成 out/ 下的基线截图；手工矩阵可被后续 agent 找到，
  且区分“现状基线”与“期望修复”。不提交一次性 out/ 产物。
- 对 Padding.y、BorderAlpha、Section 1px 漂移给出明确期望；未定义前不得顺手改视觉。

### Stage 1：低风险清理与拆文件，不改 UI API

- [ ] UI-1.1 全仓确认并删除 EditorInterface::DrawIndicator、EditorUtils 无调用 helper、
  Renderer 无消费的 scaled metrics 和 bigFont 等候选 dead code。
- [ ] UI-1.2 将 ProfessionalUI 只有内部使用的 helper 私有化；删除确无调用者的公开 API。
- [ ] UI-1.3 把 GraphicsDebugPanel 的实现从 header 移到 cpp；先不改变目录顺序和快捷键行为。
- [ ] UI-1.4 把 FormatBytes/FormatCount 等纯格式化逻辑移到非 UI utility，
  ProfileDebugOverlay 和 Renderer 共用。
- [ ] UI-1.5 将诊断用 SectionHeader/ValueRow/Badge 收口到 DevTools/DiagnosticWidgets；
  不把诊断卡片误归为 Engine 产品通用控件。

验收：

- 截图与 Stage 0 基线无有意差异。
- 删除项均有 rg 无调用结果、受影响目标编译和运行证据。
- GraphicsDebugPanel.hpp 不再承载完整面板实现。

### Stage 2：建立 Engine UI foundation，保留 ProfessionalUI facade

- [ ] UI-2.1 新增 UiTheme、FUiMetrics、FUiContext 和基础 RAII scopes。
- [ ] UI-2.2 把 ApplyProfessionalTheme、语义颜色和 font role 查询搬到 Engine。
  UserInterface 自己安装标准主题，不再通过 IDebugUiProvider::ApplyUiStyle。
- [ ] UI-2.3 RemoteImGuiSession 为自己的 ImGui Context 安装同一主题，并构造独立 UiContext。
- [ ] UI-2.4 搬迁通用 Tooltip、Button 状态、Overlay/Inset/Section 容器；
  使用 scope 统一栈平衡。
- [ ] UI-2.5 按 Stage 0 契约修复 Overlay Padding.y、有效 border 配置和 Section indent。
- [ ] UI-2.6 保留 Modules/DevTools/ProfessionalUI.hpp 作为转发 facade，
  建立迁移清单；此阶段不让所有调用点同时改名。
- [ ] UI-2.7 增加 metrics/token 与 style stack 平衡的单元测试或 ImGui debug-context 测试。

验收：

- src/Engine 新文件不包含 Modules/DevTools 头。
- 没有 DevTools provider 时仍安装标准主题。
- Main 和 Remote Context 的主题一致，但 scale、surface capability 和临时状态彼此隔离。
- facade 前后的调用点截图一致，已声明的三处样式契约缺陷除外。

### Stage 3：按垂直切片迁移 Button、Toolbar、Tooltip 和表单

建议迁移顺序：

1. gkNextEditor 顶部 Toolbar + Play/Settings；
2. Editor ViewportOverlay；
3. gkNextRenderer Viewport top bar + Mode rail；
4. Sequencer、MaterialEditor、Outliner、ContentBrowser；
5. GizmoController；
6. PropertyWidgets、Renderer Settings 和通用 tool windows。

每个切片执行：

- [ ] UI-3.x.1 用 Button/IconButton/options 替换同类手工 style cluster。
- [ ] UI-3.x.2 用 Toolbar/Overlay/ToolWindow 容器接管 padding、spacing、surface 和 compact。
- [ ] UI-3.x.3 用统一 Tooltip 替换 raw SetTooltip/BeginTooltip；验证 disabled item。
- [ ] UI-3.x.4 用 LabeledRow/PropertyGrid 统一 label/value 对齐和 trailing reset slot。
- [ ] UI-3.x.5 对比该切片的基线截图、hit target、快捷键和 ImGui ID。

约束：

- 不为每个 Toolbar 建立继承层次或配置 DSL。
- 时间轴、网格、曲线、缩略图、drag/drop 等领域绘制不强行套入通用 widget。
- 新增特殊 style cluster 时必须说明为何现有 semantic variant 无法表达。

### Stage 4：统一 Renderer/Upscaler 选项目录和 mutation

- [ ] UI-4.1 在 Engine 建立 RendererChoices：稳定 ID、显示名、排序、requirements、
  Supports/Available、fallback reason。
- [ ] UI-4.2 复用 UpscalerTypes.hpp 的元数据，补足 capability 过滤和统一的 mode catalog；
  删除各 UI 的本地名称数组。
- [ ] UI-4.3 在 NextEngine 提供 RequestRendererType / SetUpscalerConfiguration 领域命令，
  在一个位置完成校验、UserSettings/CVar 更新、SwitchLogicRenderer 或
  RequestRecreateSwapChain。
- [ ] UI-4.4 依次迁移 GraphicsDebugPanel、Renderer Settings、Renderer Viewport、
  Editor Viewport 和 Editor Settings。
- [ ] UI-4.5 为 capability-sensitive Settings combo 注册命名 options provider，
  由 provider 读取 RendererChoices/Upscaler catalog；manifest 只保存 provider ID。
  静态枚举只有在出现多个非 UI 消费者后，才另行扩展通用 CVar choice metadata。
  同步更新 [Editor Settings 与 CVar 架构](../designs/editor-settings-and-cvars.md) 和 manifest schema；
  不允许 JSON 再成为领域值的第二事实源。
- [ ] UI-4.6 增加无 RT、AmbientCube budget 不足、不支持指定 Upscaler、非法持久化值、
  fallback 和切换副作用测试。

验收：

- Renderer/Upscaler 名称、排序、可用性和 fallback 只有一个代码事实源。
- 所有入口选择同一项产生相同设置值与运行时副作用。
- Settings 仍通过 CVar API 改值，不直接绕过校验和持久化。
- UI Combo wrapper 不包含 Renderer/Upscaler 领域逻辑。

### Stage 5：拆分 AppChrome、DeveloperStatusBar 和 UI 帧协议

- [ ] UI-5.1 将通用 title/bottom bar 几何移到 Engine AppChrome。
- [ ] UI-5.2 AppChrome 使用 UiContext 获取 brand texture/font/surface capability，
  返回 action 和 drag geometry，不直接调用 NextEngine 或 OS/window command。
- [ ] UI-5.3 将 DrawStandardBottomBar 拆为 DevTools DeveloperStatusBar；
  Console、Stats、VRAM、reload、capture 都留在 DevTools/Application。
- [ ] UI-5.4 Engine 在绘制前从 surface/capture 状态构造 FUiFramePolicy，并通过
  FGameUiFrameContext 传入 Application；用 FUiFrameResult developer-layer mask 替换
  bool uiHandled，让 MainWindow 与 RemoteView dispatcher 都消费该结果。
- [ ] UI-5.5 删除 EditorInterface 对 FUiDevPanels::Get().RenderConsoleOverlay() 的补偿调用。
- [ ] UI-5.6 将 UserInterface::Render(statistics, profiler, scene, ...) 中的 DevTools
  调度移到 UiFrameDispatcher；backend API 只保留 frame 和 draw-data 职责。
- [ ] UI-5.7 逐一验证 MainWindow、RemoteView、普通截图、include-ui 截图、录像抑制 UI。
- [ ] UI-5.8 AboutDialog 和 PropertyWidgets 改为接收 presentation/context，
  不从绘制 helper 内部读取 NextEngine 单例。

验收：

- UI layer 顺序只有 Engine 一处定义。
- RemoteView 不绘制或执行本地主窗口控制，不修改本地 titlebar drag region。
- Application UI 是否存在不再决定标准主题是否存在。
- capture policy 不再通过一个含义不明的 bool 传播。

### Stage 6：拆分巨型 UI、迁移余下消费者并删除 facade

- [ ] UI-6.1 保留 UserInterface 公共 facade，将 Context/font/input、Vulkan submit、
  texture resolver 拆为内部职责对象；从公开头移除 imgui_internal 和不必要的 Vulkan 具体头。
- [ ] UI-6.2 将 gkNextRenderer.cpp 的 UI 拆为 RendererChrome、
  RendererViewportToolbar、RendererSettingsPanel、RendererMemoryPanel 等文件，
  将 state/model、presentation、mutation 分开。
- [ ] UI-6.3 按职责拆分 EditorUtils 和大型 panel；保留领域内聚，不以行数为唯一标准。
- [ ] UI-6.4 迁移 ScadStudio、ScadLibrary 和 DevTools 自身的 ProfessionalUI 调用。
- [ ] UI-6.5 删除 ProfessionalUI facade、IDebugUiProvider::ApplyUiStyle 和已废弃兼容入口。
- [ ] UI-6.6 增加仅在开发环境可打开的 UI Catalog，集中展示 Button variants、
  Toolbar、Combo、Tooltip、Panel、font roles 和 scale cases。
- [ ] UI-6.7 把稳定边界写入 docs/designs/desktop-ui-foundation.md，
  更新相关 guide/design；计划全部完成后按 docs 生命周期移除此 plan。

验收：

- gkNextEditor、gkNextRenderer、Engine 不再包含 Modules/DevTools/ProfessionalUI.hpp。
- Engine 不依赖 DevTools；DevTools 只向下依赖 Engine UI foundation。
- 发布应用中不再有第二套 Renderer/Upscaler 目录。
- ProfessionalUI 和含混的 uiHandled 协议均已删除。

---

## 8. 验证矩阵

### 8.1 构建

构建必须串行执行。普通阶段只构建受影响目标，不默认跑全量：

~~~powershell
gnb.bat build gkNextRenderer gkNextUnitTests
gnb.bat build gkNextEditor
~~~

改动 DevTools/ProfessionalUI 或共享 Engine UI 后，两套发布应用和 unit tests 都要跑。
Stage 6 删除 facade 时还要构建仍使用过它的消费者：

~~~powershell
gnb.bat build gkNextRenderer gkNextEditor ScadStudio ScadLibrary gkNextUnitTests
~~~

新增文件需要 CMake 重新发现时加 --reconfigure。只有出现广泛 header/ABI 影响或无法确认消费面，
才按 AGENTS.md 使用全量 build --all --reconfigure。

### 8.2 视觉与交互

基础截图命令：

~~~powershell
gnb.bat shot --target gkNextRenderer --scene assets/models/playground.glb --frames 60 --ui
gnb.bat shot --target gkNextEditor --scene assets/models/playground.glb --frames 60 --ui
~~~

Stage 0 脚本落地后，用交互验证重放 popup、panel 和紧凑布局：

~~~powershell
gnb.bat validate --script assets/agentscripts/ui-foundation-renderer-normal.agentscript.json
gnb.bat validate --script assets/agentscripts/ui-foundation-renderer-compact.agentscript.json
gnb.bat validate --script assets/agentscripts/ui-foundation-editor-normal.agentscript.json
gnb.bat validate --script assets/agentscripts/ui-foundation-editor-compact.agentscript.json
~~~

每个垂直切片至少检查：

- normal/hover/active/disabled/selected；
- Popup 打开、当前项 focus、超长 label、紧凑宽度；
- titlebar drag 与 min/max/close hit target；
- dockspace、窗口 ID 和用户布局恢复；
- Console、Stats、Settings、Memory panel 的层级与遮挡；
- MainWindow 与 RemoteView；
- include-ui 和 hidden-ui 截图。

### 8.3 自动化测试重点

- FUiMetrics 在 1.0/1.25/1.5/2.0 scale 的确定性；
- scope 提前 return、collapsed window、disabled branch 时的栈平衡；
- RendererChoices 的排序、能力过滤和 fallback；
- Upscaler type/mode compatibility 与 SwapChain recreate 次数；
- CVar invalid/default/user override 后的 UI 选择；
- FUiFrameResult 在 main/remote surface 与 capture policy 下的层组合；
- 稳定 ID，避免按钮文案变化破坏 popup/dock/selection state。

---

## 9. 风险与迁移门槛

| 风险 | 典型后果 | 门槛 |
| --- | --- | --- |
| ImGui ID 改变 | dock 布局、popup、selection 状态丢失 | 先记录 ID；显示文案与隐藏 ID 分离 |
| Begin/End 语义改变 | assert、样式泄漏、后续窗口错位 | RAII + collapsed/early-return 测试 |
| DPI token 与 framebuffer scale 混用 | 双重缩放、点击偏移、裁切 | 分开坐标变换、视觉 density、responsive layout |
| Renderer mutation 时机变化 | 一帧状态不一致、重复 rebuild | 领域命令单点执行并测试调用次数 |
| Remote Context 读取全局状态 | 会话互相污染、控制本地窗口 | context-local metrics/state + surface capability |
| facade 过早删除 | ScadStudio/ScadLibrary 或 DevTools 编译断裂 | 维护消费者清单，最后阶段删除 |
| 过度抽象 | callback/配置比原生 ImGui 更难读 | 只抽取至少两个稳定消费者共有的视觉契约 |
| 大文件机械拆分 | 依赖更隐蔽、行为仍耦合 | 按 state/presentation/mutation 责任切分 |

Stage 4 和 Stage 5 是行为敏感阶段，不能与大规模文件移动或命名清理放在同一提交。
任何视觉差异都必须归类为“已批准修复”或“回归”；不能用“重构后更好看”替代验收。

---

## 10. 完成定义

全部满足后才算完成：

- [ ] Engine 拥有标准主题、context、metrics、scopes 和通用 widgets/containers。
- [ ] UserInterface 是窄 facade，Context、Vulkan submit、texture resolution 和 layer dispatch
  不再堆叠在同一实现职责中。
- [ ] DevTools 不再拥有发布应用必须依赖的 UI 基础层。
- [ ] gkNextEditor 与 gkNextRenderer 使用同一 Button、Tooltip、Toolbar、Panel 和 row 规范。
- [ ] Renderer/Upscaler 目录、能力过滤、fallback、mutation 均为单一事实源。
- [ ] bool uiHandled 与 Editor 的 Console 补偿调用已被显式 UI layer 协议替代。
- [ ] Main、Remote、DPI、capture、dock/ID 行为通过矩阵验证。
- [ ] ProfessionalUI facade 删除，剩余 raw style 仅用于有说明的特殊领域绘制。
- [ ] 目标构建、unit tests、UI screenshots 均通过。
- [ ] 当前架构已提炼到 design 文档，本 plan 按文档生命周期退出现行索引。
