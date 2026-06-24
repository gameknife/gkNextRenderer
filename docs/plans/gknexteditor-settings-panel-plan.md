# gkNextEditor 设置面板开发计划

> 状态：待开发（设计已定，交由后续 AGENT 实现）
> 范围：`src/Application/Editor/gkNextEditor`、`src/Engine/Runtime/Config`、`assets/configs`
> 目标读者：接手实现的 AI coding agent / 工程师

## 1. 需求

给 gkNextEditor 增加一个统一的 **设置面板（Settings / Preferences）**，用于在编辑器内配置两类东西：

1. **渲染参数** —— 复用 `gkNextRenderer` 已有的那套（`UserSettings` / `ShowFlags`，通过 cvar 暴露），让用户在编辑器里也能调 renderer 类型、采样数、降噪、DLSS、SHARC、GTAO 等。
2. **编辑器行为** —— 编辑器自身的交互开关，例如「鼠标悬停物体时的 hover 预览高亮」「Outliner 自动滚动到选中项」「Gizmo 默认模式 / 吸附」「启动默认 renderer」等。

入口有两个：

- Toolbar 右侧已有的齿轮按钮（`ICON_FA_GEAR`，tooltip "Editor Settings"），目前点击无响应，需要接上。
- 主菜单（自绘标题栏的菜单条）新增入口，例如 `Edit ▸ Preferences…` 或单独的 `Settings` 菜单。

设计要求「优雅」：尽量**数据驱动**——用 `assets/configs/cvar_editor.json` 描述「面板里展示哪些配置项、分到哪个分类、用什么控件、范围/步长/选项是什么」，C++ 侧只负责按描述渲染控件并读写 cvar，而不是把每个配置项硬编码进 `.cpp`。

## 2. 现状调研结论

### 2.1 编辑器 UI 架构

- 主入口 `EditorInterface::Render()`（`EditorInterface.cpp`）按帧组织：`DockSpaceUI()` → `ToolbarUI(ctx)` → `DrawTitleBarOverlay()` → 各 docked panel。
- **每个面板**是一个自由函数 `Editor::DrawXxxPanel(EditorContext& ctx, EditorUiState& ui)`：
  - 声明在 `gkNextEditor/EditorUi.hpp`；
  - 实现放在 `gkNextEditor/Panels/*.cpp`；
  - 是否显示由 `EditorUiState`（`Core/EditorUiState.hpp`）里的一个 `bool` 字段控制；
  - 在 `EditorInterface::Render()` 里 `if (uiState_.xxx) Editor::DrawXxxPanel(ctx, uiState_);`。
- **Toolbar**（`EditorInterface::ToolbarUI`，约 232 行）已经画了齿轮按钮，但调用的是 `NextUI::Theme::ToolbarButton(...)` 且**忽略了返回值**。`ToolbarButton` 实际返回 `bool`（`Modules/DevTools/ProfessionalUI.hpp:91`），所以接上点击只需取返回值翻转一个 bool。
- **主菜单**在 `Overlays/TitleBarOverlay.cpp::DrawTitleBarOverlay` 内的 `config.DrawMenuBar` lambda 里，已有 `File / Edit / View / Tools / Build / Windows / Help` 菜单。`Edit` 菜单底部、或 `Tools`、`Windows` 菜单都是合理挂载点。
- 注意：`TitleBarOverlay.cpp` 的 `View` 菜单里有一批 `static bool showGrid/gizmoTranslate/...`，目前是**孤立的 static 变量**（没接到实际渲染），新面板应把这些真正接到 cvar / `ShowFlags` 上，顺手消除这些假开关。
- 新增源文件会被 CMake 自动收录：`src/cmake/SourceFiles.cmake:91` 用 `file(GLOB_RECURSE src_files_editor "Application/Editor/gkNextEditor/*")`。新增 `Panels/SettingsPanel.cpp` 后需要跑一次 `./gnb build gkNextEditor --reconfigure` 让 glob 重新生效。

### 2.2 CVar 系统（`src/Engine/Runtime/Config/`）

- 核心类 `NextCVar::FCVarSystem`（`CVarSystem.hpp/.cpp`）。
- 注册：`RegisterInt/RegisterUInt/RegisterFloat/RegisterBool/RegisterString(name, default, target*, flags, desc, onChanged=null)`。
  - `target` 指向真实运行时变量（`UserSettings` / `ShowFlags` / `Options` 的字段），cvar 改值即写穿到该字段。
  - `flags`：`Archive` = 会被持久化；`ReadOnly`；`StartupOnly`。
  - `onChanged`：值变化后的回调（如 `RequestSwapChainIfPossible` 触发交换链重建）。
- 读写：`SetValueFromString(name, value, setBy, &err)`、`GetValueString(name, &found)`、`ResetToDefault(name)`、`Match(query, opts)`。
- 持久化：`LoadDefaultFile("assets/configs/cvar_default.json")`、`LoadUserFile("assets/configs/cvar_user.json")`、`SaveUserFile(path)`。
  - `SaveUserFile` 只写 **带 `Archive` 且当前值 ≠ 默认值** 的 cvar，写到 `cvar_user.json`。
- 引擎接线（`Engine.cpp` ~403–407）：构造 `FCVarSystem` → `RegisterEngineCVars()` → `LoadDefaultFile` → `gameInstance_->ApplyDefaultCVars()` → `LoadUserFile` → 应用命令行 override。
- 取用：`engine.GetCVarSystem()`（`Engine.hpp:115`）。
- 引擎层所有 cvar 在 `EngineCVars.cpp::RegisterEngineCVars` 集中注册，绑定到 `UserSettings`（`UserSettings.hpp`）和 `ShowFlags`（`ShowFlags.hpp`）。命名前缀约定：`r.*`（渲染）、`sys.*`（系统/场景）、`ui.*`、`show.*` / `debug.*`（调试可视化，多为非 Archive）。

> **关键缺口**：`FCVarSystem` 目前**没有公开的「枚举 / 查询单个 cvar 元信息」API**（拿不到某个 cvar 的类型、描述、当前值是否等于默认值）。数据驱动面板需要补一个轻量只读查询接口（见 §4.1）。

### 2.3 现有 RenderSetting 面板（参考样板）

- `gkNextRenderer.cpp::DrawSettings()`（~813 行）直接编辑 `GetEngine().GetUserSettings()` 的字段，用一组本地 helper：
  - `DrawSettingSliderRow(label, dataType, value*, min, max, fmt, dragSpeed)`
  - `DrawSettingCheckboxRow(label, bool*)`
  - `DrawSettingComboRow(label, preview, body)`
  - 底层都走 `NextUI::Theme::BeginFormRow(label)` 做「左标签 + 右控件」布局。
- 它是**硬编码 + 直接改 struct 字段**的写法。新编辑器面板**不照搬这种硬编码**，而是改成 cvar + json 驱动（见 §3），但可以复用这些 row helper 的视觉风格（必要时把它们提取成共享工具）。
- renderer 选择器、ShowFlags、View Mode 等已有可复用 helper 在 `Modules/DevTools/GraphicsDebugPanel.hpp`（`DrawRendererSelector`、`DrawSectionHeader`、`ApplyViewMode` 等），面板可直接调用。

### 2.4 编辑器行为现状（需要被设置项接管的点）

- **Hover 预览**：`EditorMain.cpp::OnCursorPosition`（~266–285）每次鼠标移动都 `RayCastGPU` 并 `SetHoveredId/ClearHoveredId`。这就是「选择物体的预览 hover」，应由一个开关（如 `ed.hoverHighlight`）控制是否执行。
- **Outliner 自动滚动**：`EditorUiState::outlinerAutoScrollToSelection`（已存在，bool）。
- **默认渲染参数**：`EditorMain.cpp::ApplyDefaultCVars`（60–67）硬编码 `r.samples=4 / r.temporalFrames=16 / r.denoiser=0 / r.superResolution=2`，是编辑器进场默认。
- **Dock 布局重置**：`EditorUiState::dockResetRequested`。
- 这些「编辑器行为」目前散落在 `EditorUiState` 与代码里，计划把可配置者收敛为 `ed.*` cvar（见 §4.2）。

## 3. 总体设计

### 3.1 分层

```
assets/configs/cvar_editor.json   ← 数据：面板布局描述（分类/分组/控件/范围/选项）
        │ 读取
        ▼
Editor::SettingsPanel (Panels/SettingsPanel.cpp)
        │ 渲染控件，读写值
        ▼
NextEngine::GetCVarSystem()  ──► UserSettings / ShowFlags / Options / EditorSettings
        │ onChanged 回调（交换链重建等）
        ▼
SaveUserFile → assets/configs/cvar_user.json （持久化）
```

要点：

- 面板**不认识**任何具体配置项语义，只认识 `cvar_editor.json` 里的描述 + cvar 名字。新增一个可调项 = 注册一个 cvar + 在 json 里加一行，**无需改面板代码**。
- 渲染参数（`r.* / sys.* / show.*`）已经是 cvar，直接引用即可。
- 编辑器行为新增 `ed.*` 一组 cvar（§4.2），同样进 cvar 系统、同样持久化到 `cvar_user.json`，复用全部基础设施。

### 3.2 `cvar_editor.json` schema

放在 `assets/configs/cvar_editor.json`。建议结构：

```json
{
  "version": 1,
  "categories": [
    {
      "id": "rendering",
      "label": "Rendering",
      "icon": "",
      "groups": [
        {
          "label": "Quality",
          "items": [
            { "cvar": "r.rendererType", "label": "Renderer", "widget": "combo",
              "options": ["PathTracing","SoftwareTracing","SoftwareModern","VoxelTracing","SwModernNoAmbient"],
              "tooltip": "Active rendering path" },
            { "cvar": "r.samples", "label": "Samples", "widget": "slider_int", "min": 1, "max": 16, "step": 1 },
            { "cvar": "r.temporalFrames", "label": "Temporal Frames", "widget": "slider_int", "min": 1, "max": 64 }
          ]
        },
        {
          "label": "Denoise",
          "advanced": true,
          "items": [
            { "cvar": "r.denoiser", "label": "Enable Denoiser", "widget": "checkbox" },
            { "cvar": "r.denoiseAtrousIterations", "label": "A-trous Iterations",
              "widget": "slider_int", "min": 1, "max": 6 }
          ]
        }
      ]
    },
    {
      "id": "upscaling", "label": "Upscaling",
      "groups": [ { "label": "DLSS / FSR", "items": [
        { "cvar": "r.dlss",  "label": "DLSS",  "widget": "checkbox", "requiresRestart": false },
        { "cvar": "r.dlssg", "label": "Frame Generation", "widget": "checkbox" },
        { "cvar": "r.superResolution", "label": "Super Resolution", "widget": "combo",
          "options": ["Native","Ultra Quality","Quality","Balanced","Performance"] }
      ]}]
    },
    {
      "id": "editor", "label": "Editor",
      "groups": [ { "label": "Interaction", "items": [
        { "cvar": "ed.hoverHighlight", "label": "Hover Preview Highlight", "widget": "checkbox",
          "tooltip": "Raycast under cursor and highlight the hovered object" },
        { "cvar": "ed.outlinerAutoScroll", "label": "Auto-scroll Outliner to Selection", "widget": "checkbox" },
        { "cvar": "ed.gizmoSnap", "label": "Gizmo Snap", "widget": "checkbox" }
      ]}]
    }
  ]
}
```

**字段约定**：

| 字段 | 含义 |
|---|---|
| `widget` | `checkbox` / `slider_int` / `slider_float` / `drag_float` / `combo` / `color` / `text` |
| `min`/`max`/`step`/`format` | 数值控件参数；`format` 形如 `"%.2f"` |
| `options` | combo 选项标签数组（按整数索引映射到 cvar 值） |
| `advanced` | true → 默认折叠 / 只在「显示高级」时出现 |
| `requiresRestart` | true → 控件旁标记「需重启」 |
| `tooltip` | 悬停说明；若缺省回退到 cvar 自身 description |

**鲁棒性**：解析时若某个 `cvar` 名在系统中不存在（或类型与 widget 不匹配），跳过该项并 `SPDLOG_WARN`，不要让整个面板崩或空白。json 缺失时回退到一个最小内置默认描述（保证面板永远能开）。

### 3.3 面板交互

- 左侧分类列表（categories）+ 右侧分组/控件区（典型 settings 双栏），或顶部 Tab 条 + 内容区，二选一（推荐左侧竖直分类列表，和 VS/Unreal Preferences 一致）。
- 顶部工具条：搜索框（按 label / cvar 名过滤）、`显示高级` 开关、`Reset to Defaults`（对当前分类或全部，调 `ResetToDefault`）。
- 底部：`Apply & Save`（调 `SaveUserFile("assets/configs/cvar_user.json")`）；改值即时生效（cvar 写穿），Save 只负责落盘。
- 控件改值统一走 `SetValueFromString` / 类型化 set，触发 onChanged（如 DLSS 改动重建交换链）。

### 3.4 入口接线

- **Toolbar 齿轮**（`EditorInterface.cpp:232`）：把 `NextUI::Theme::ToolbarButton(ICON_FA_GEAR, "Editor Settings", uiState_.settingsPanel, ...)` 的返回值用于 `if (clicked) uiState_.settingsPanel = !uiState_.settingsPanel;`，并把 `active` 实参传 `uiState_.settingsPanel` 让按钮高亮。
- **主菜单**：在 `TitleBarOverlay.cpp` 的 `Edit` 菜单加 `Separator` + `MenuItem("Preferences…", "Ctrl+,", &ui.settingsPanel)`；同时在 `Windows` 菜单加 `MenuItem("Settings", nullptr, &ui.settingsPanel)`。
- **快捷键**（可选）：`Ctrl+,` 切换，挂到现有全局快捷键处理处。

## 4. 工程改动清单

### 4.1 引擎层：补 cvar 元信息查询 API（`Engine/Runtime/Config/CVarSystem.{hpp,cpp}`）

数据驱动面板需要能查到「这个 cvar 是什么类型、当前值、是否= 默认值、描述」。新增**只读**接口，不破坏现有调用：

```cpp
struct FCVarInfo
{
    std::string name;
    std::string description;
    ECVarType   type;
    ECVarFlags  flags;
    bool        isDefault;   // 当前值是否等于默认
};

bool TryGetInfo(const std::string& name, FCVarInfo& out) const;          // 单个
void ForEach(const std::function<void(const FCVarInfo&)>& fn) const;     // 枚举（搜索/调试用）
```

- 实现简单：遍历 `cvars_` map，从 `FCVarEntry` 填充。
- `ForEach` 顺带让 console / 未来「全部 cvar」视图复用。
- 面板读值仍可用现成 `GetValueString`；写值用 `SetValueFromString`（字符串路）即可满足全部 widget，无需新增类型化 setter。
- **加单测**：`src/Tests/` 下补 `[Unit][CVar]` 用例覆盖 `TryGetInfo` / `ForEach` / `isDefault` 判定。

### 4.2 编辑器层：注册 `ed.*` 编辑器行为 cvar

- 新增 `EditorSettings` 状态载体。两个选择，**推荐 A**：
  - **A（推荐）**：在 `EditorUiState` 里加字段（`bool hoverHighlight=true; bool gizmoSnap=true; ...`），由编辑器在初始化时注册 cvar 绑定到这些字段。好处：与现有 panel 共享同一个 `uiState_`，渲染/读取零摩擦。
  - B：单独 `struct EditorSettings`，独立 `editor_settings.json`。更隔离，但要再造一套加载/持久化，收益不大。
- 在 `EditorMain.cpp` 新增 `RegisterEditorCVars(FCVarSystem&, EditorUiState&)`，在编辑器初始化（`OnInit` 之后、能拿到 cvar 系统时）调用。注意时序：引擎 `ApplyDefaultCVars` 在 `LoadUserFile` 之前，但 `ed.*` 绑定的目标是 `EditorUiState`，需保证注册发生在 `LoadUserFile` 之前才能被 `cvar_user.json` 覆盖。**建议**给 `GameInstance` 增加一个在 `RegisterEngineCVars` 之后、`LoadUserFile` 之前调用的钩子（如 `virtual void RegisterGameCVars(FCVarSystem&)`），编辑器 override 它注册 `ed.*`。需要小改 `Engine.cpp` 接线顺序，改动面小且向后兼容（基类空实现）。
- 初版 `ed.*` 清单（全部 `Archive`）：
  - `ed.hoverHighlight`（bool，默认 true）→ 控 `OnCursorPosition` 是否 RayCast 设置 HoveredId。
  - `ed.outlinerAutoScroll`（bool）→ 绑 `outlinerAutoScrollToSelection`。
  - `ed.gizmoSnap`（bool）、`ed.gizmoSnapTranslate`（float）、`ed.gizmoDefaultMode`（int 0/1/2）。
  - `ed.defaultRenderer`（int）、`ed.defaultSamples`（int）—— 取代 `ApplyDefaultCVars` 里的硬编码。
- 改 `EditorMain.cpp::OnCursorPosition`：`if (uiState_.hoverHighlight) { …RayCastGPU… }`，关闭时 `ClearHoveredId()` 一次。

### 4.3 编辑器层：新面板

- 新文件 `Panels/SettingsPanel.cpp`，函数 `void Editor::DrawSettingsPanel(EditorContext& ctx, EditorUiState& ui);`，声明加到 `EditorUi.hpp`。
- `EditorUiState` 加 `bool settingsPanel = false;`。
- `EditorInterface::Render()` 末尾加 `if (uiState_.settingsPanel) Editor::DrawSettingsPanel(ctx, uiState_);`。
- 面板内部：
  1. 首帧（或文件变更时）加载并解析 `assets/configs/cvar_editor.json`（`nlohmann/json` + `Utilities::FileHelper::GetPlatformFilePath`，与 `CVarSystem.cpp` 同款）。把解析结果缓存为一个 `FSettingsLayout`（categories→groups→items）静态/成员结构，避免每帧 IO。
  2. 渲染：分类列表 + 控件区；每个 item 按 `widget` 分派到一个 `DrawItem(ctx, item)`，内部用 `GetCVarSystem()` 读当前值、画控件、改了就 `SetValueFromString`。
  3. 复用 `NextUI::Theme::BeginFormRow` / `GraphicsDebugPanel::DrawSectionHeader` 保持风格一致；renderer 选择器可直接用 `GraphicsDebugPanel::DrawRendererSelector`。
- combo 的「整数值 ↔ 选项索引」映射：选项数组顺序即对应 cvar 整数值（0..n-1）。

### 4.4 清理 View 菜单假开关

- 把 `TitleBarOverlay.cpp` `View` 菜单里的 `static bool showGrid/showBounds/gizmo*` 等改成读写对应 cvar（`show.grid`、`show.wireframe`、`ed.gizmo*` 等），或直接移除并引导到设置面板，消除「点了没反应」的孤立开关。此项可作为独立小 PR。

## 5. 实施步骤（建议顺序）

1. **引擎 cvar 查询 API**（§4.1）+ 单测。`./gnb build gkNextRenderer gkNextUnitTests` 验证。
2. **`GameInstance::RegisterGameCVars` 钩子** + `Engine.cpp` 接线顺序调整（§4.2 时序）。全量编译确认未破坏其它 program：`./gnb build gkNextRenderer gkNextUnitTests`（引擎层改动按 AGENTS.md 只需这两个目标）。
3. **编辑器 `ed.*` cvar 注册** + `OnCursorPosition` 接 `ed.hoverHighlight`（§4.2）。`./gnb build gkNextEditor`。
4. **`cvar_editor.json`** 初版（§3.2），覆盖 Rendering / Upscaling / Editor 三类常用项。
5. **`SettingsPanel.cpp`** 面板实现（§4.3）+ `EditorUiState.settingsPanel` + `EditorInterface::Render` 接入。`./gnb build gkNextEditor --reconfigure`（新文件需 reconfigure）。
6. **入口接线**：Toolbar 齿轮 + 主菜单（§3.4）。
7. **清理 View 菜单假开关**（§4.4，可选/最后）。
8. **联调 & 验证**（§6）。

每完成一步按 spec 工作流写 `.spec/journal/<id>.md`（若走 TODO 流程）。

## 6. 验证

- **编译**：引擎改动 `./gnb build gkNextRenderer gkNextUnitTests`；编辑器改动 `./gnb build gkNextEditor`（新增文件加 `--reconfigure`）。
- **单测**：cvar 查询 API 的 Catch2 用例 `./out/build/<preset>/bin/gkNextUnitTests "[Unit][CVar]"`。
- **运行**：`./gnb run gkNextEditor`，确认启动并 `uploaded scene [...] to gpu`。
- **功能自测**：
  - 齿轮按钮 / `Edit ▸ Preferences…` 都能开关面板，按钮高亮态正确。
  - 改 `r.samples` / `r.rendererType` / `r.dlss` 即时影响渲染；`r.dlss` 改动触发交换链重建无崩溃。
  - 关闭 `ed.hoverHighlight` 后鼠标移动不再产生 hover 高亮；重开恢复。
  - `Apply & Save` 后 `assets/configs/cvar_user.json` 出现非默认项；重启编辑器保持。
  - 故意把 json 里写一个不存在的 cvar / 删除 json：面板不崩，日志 WARN，回退默认。
- **视觉**：渲染相关项可用 `gnb shot --target gkNextEditor`（若该 target 支持 `--scene`/agent-validation）截图肉眼确认；否则手动跑编辑器目测。

## 7. 风险与注意

- **时序**：`ed.*` cvar 必须在 `LoadUserFile` 之前注册，否则用户保存的编辑器偏好读不回来——这是为何引入 `RegisterGameCVars` 钩子而非塞进 `OnInit`。
- **combo 值映射**：`r.rendererType` 的整数定义见 `EngineCVars.cpp:61`（0=PathTracing,1=SoftwareTracing,2=SoftwareModern,3=VoxelTracing,4=SwModernNoAmbient）；`cvar_editor.json` 的 `options` 顺序必须与之一致，且部分 renderer 在不支持硬件光追的设备上不可用（参考 `GraphicsDebugPanel::GetRendererOptionCount`），combo 渲染时应据此裁剪。
- **持久化语义**：`SaveUserFile` 只落「Archive 且 ≠ 默认」的项；非 `Archive` 的 `show.*`/`debug.*` 改动不会被保存，这是预期；若希望某编辑器项跨会话保留务必给 `Archive`。
- **不要硬编码绝对路径**；json 走 `Utilities::FileHelper::GetPlatformFilePath`。
- **第三方目录勿改**；`nlohmann/json` 已是依赖，直接用。
- **命名规范**（`.clang-tidy`）：类型/函数 PascalCase，成员 `camelCase_`，文件首个 include `Common/CoreMinimal.hpp`，Allman 大括号，4 空格缩进。

## 8. 关键文件索引

| 用途 | 路径 |
|---|---|
| 面板渲染入口 / Toolbar / DockSpace | `src/Application/Editor/gkNextEditor/EditorInterface.cpp` |
| 面板声明 | `src/Application/Editor/gkNextEditor/EditorUi.hpp` |
| 面板可见性状态 | `src/Application/Editor/gkNextEditor/Core/EditorUiState.hpp` |
| 主菜单 / 标题栏 | `src/Application/Editor/gkNextEditor/Overlays/TitleBarOverlay.cpp` |
| 编辑器 GameInstance / hover / 默认 cvar | `src/Application/Editor/gkNextEditor/EditorMain.cpp` |
| 新面板（待建） | `src/Application/Editor/gkNextEditor/Panels/SettingsPanel.cpp` |
| CVar 系统 | `src/Engine/Runtime/Config/CVarSystem.{hpp,cpp}` |
| 引擎 cvar 注册表 | `src/Engine/Runtime/Config/EngineCVars.cpp` |
| 运行时设置载体 | `src/Engine/Runtime/Config/UserSettings.hpp`、`ShowFlags.hpp` |
| 引擎接线 | `src/Engine/Runtime/Engine.cpp`（~403–415）、`GameInstance.hpp` |
| RenderSetting 样板 | `src/Application/Render/gkNextRenderer/gkNextRenderer.cpp::DrawSettings` |
| 可复用 UI helper | `src/Modules/DevTools/ProfessionalUI.hpp`、`GraphicsDebugPanel.hpp` |
| 数据描述（待建） | `assets/configs/cvar_editor.json` |
| 默认 / 用户配置 | `assets/configs/cvar_default.json`、`cvar_user.json` |
