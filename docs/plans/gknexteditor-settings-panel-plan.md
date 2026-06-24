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

设计要求「优雅」：尽量**数据驱动**——用一个**视图 manifest**（`assets/configs/ui/settings_panel.json`，刻意不带 `cvar_` 前缀以区别于值文件）描述「面板里展示哪些配置项、分到哪个分类、用什么控件、范围/步长/选项是什么」，C++ 侧只负责按描述渲染控件并读写 cvar，而不是把每个配置项硬编码进 `.cpp`。

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
assets/configs/ui/settings_panel.json   ← 视图 manifest：面板布局（分类/分组/控件/范围/选项）
        │ 读取（只读，随包发布，非值文件）
        ▼
Editor::SettingsPanel (Panels/SettingsPanel.cpp)
        │ 渲染控件，按 cvar 名读写值
        ▼
NextEngine::GetCVarSystem()  ──► UserSettings / ShowFlags / Options（引擎） + EditorSettings（编辑器）
        │ onChanged 回调（交换链重建等）
        ▼
SaveUserFiles()  ──► 按 namespace 路由：
                     r./sys./show.* → assets/configs/cvar_user.json       （共享）
                     ed.*           → assets/configs/cvar_user.editor.json （编辑器专属）
```

要点：

- 面板**不认识**任何具体配置项语义，只认识 `settings_panel.json` 里的描述 + cvar 名字。新增一个可调项 = 注册一个 cvar + 在 manifest 里加一行，**无需改面板代码**。
- 渲染参数（`r.* / sys.* / show.*`）已经是 cvar，直接引用即可，持久化仍走共享的 `cvar_user.json`。
- 编辑器行为新增 `ed.*` 一组 cvar（§4.2），进同一套 cvar 系统，但**持久化到编辑器专属的 `cvar_user.editor.json`**，与共享值文件物理分离（见 §4.1 的 namespace→文件路由）。
- **三类文件三种角色，不要混淆**：`settings_panel.json` = 视图（manifest，不存值）；`cvar_default.json` = 默认值；`cvar_user.json` / `cvar_user.editor.json` = 用户覆盖值（按 namespace 分文件）。

### 3.2 `settings_panel.json` schema

这是**视图 manifest**（描述面板长什么样），不是值文件——名字刻意不带 `cvar_` 前缀，避免被误认成第三个值存储。放在 `assets/configs/ui/settings_panel.json`。它本质 app-neutral（将来 gkNextRenderer 想数据驱动设置面板也能复用同格式）；若需多 app 各自布局，再分 `settings_panel.editor.json` / `settings_panel.renderer.json`。建议结构：

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
- 底部：`Apply & Save`（调 `SaveUserFiles()`，按 namespace 自动把 `r./sys.*` 落 `cvar_user.json`、`ed.*` 落 `cvar_user.editor.json`）；改值即时生效（cvar 写穿），Save 只负责落盘。
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

#### 4.1.b 持久化按 namespace 路由到不同文件（让 `ed.*` 落到编辑器专属文件）

目前持久化是单文件、单方法：`SaveUserFile(path)` 把**所有** Archive 且 ≠ 默认的 cvar 重写进一个 json；`LoadUserFile(path)` 从一个 json 读。这导致两个问题：(1) 编辑器若把 `ed.*` 存进共享的 `cvar_user.json`，会和引擎/渲染值混在一起；(2) `SaveUserFile` 是全量重建文件，**会覆盖丢掉本进程未注册的 key**（console 的 `cvar.save` 命令走的就是这条，见 `CVarSystem.cpp:372`）。

引入「**namespace → 用户文件**」路由，让分离成为系统内在能力（所有保存路径都正确，不依赖调用方传对参数）：

```cpp
// 注册一个 namespace 前缀 → 专属用户文件；未匹配任何前缀的 cvar 落到默认共享文件。
// 默认：全部 → "assets/configs/cvar_user.json"
void RegisterUserFileChannel(const std::string& prefix, const std::string& path);

// 取代「单文件」读写：按已注册的 channel 把每个 Archive!=默认 的 cvar 写到它所属文件；
// 加载时依次读共享文件 + 每个 channel 文件。
bool LoadUserFiles();
bool SaveUserFiles() const;
```

- 现有 `LoadUserFile(path)` / `SaveUserFile(path)` 保留（显式单文件操作 / 向后兼容），但**默认流程改用 `LoadUserFiles` / `SaveUserFiles`**。
- `SaveUserFiles` 对每个 channel 仍沿用现有「只写 Archive 且 ≠ 默认」的逻辑，按前缀分桶后分别 `dump` 到各自文件——天然避免跨文件覆盖。
- **引擎接线**（`Engine.cpp` ~406-407）：把 `LoadUserFile("cvar_user.json")` 换成 `LoadUserFiles()`，且必须在 `gameInstance_->ConfigureCVars()` **之后**调用（这样编辑器已经注册了 `ed.` channel）。
- **console**：`cvar.save` 改调 `SaveUserFiles()`，保证从控制台保存也按 channel 正确分流。
- **只拆 editor**：游戏（Brotato3D 等）不注册任何 channel，照旧全部进 `cvar_user.json`，零影响。唯一注册 channel 的是编辑器（§4.2）。

### 4.2 统一 per-app 钩子 `ConfigureCVars` + 编辑器注册 `ed.*`

**不引入 engine/editor/game 三套机制**。实际只有两层：引擎固定的 `RegisterEngineCVars`，加上每个 app（`GameInstance` 子类）共用的**一个**钩子。editor 和所有 game 都是 `GameInstance` 子类，走同一钩子；`r./sys./ed./game.` 只是命名约定，不是独立机制。

**把现有 `ApplyDefaultCVars` 提升/改名为 `ConfigureCVars(FCVarSystem&)`**（`GameInstance.hpp` 基类 + `gkNextRenderer.cpp`、`ScadStudioMain.cpp`、`EditorMain.cpp` 三个 override 同步改名，调用点 `Engine.cpp:406` 同步改）。它的调用时机（`RegisterEngineCVars` + `LoadDefaultFile` 之后、`LoadUserFiles` 之前，见 `Engine.cpp:404-407`）**正好**是注册 app 自己 cvar 的窗口；`OnInit`（`Engine.cpp:574`）太晚，那时用户值已加载完。契约：

> `ConfigureCVars` 里 app 可以 (1) 改 engine cvar 的默认值（原 `ApplyDefaultCVars` 的活）、(2) 注册自己的 cvar、(3) 注册自己的持久化 channel。

渲染器 / ScadStudio 的 override 只是改名，内容不变（继续只调 `SetDefaultFromString`）。

**编辑器状态载体——用直接成员，规避生命周期坑**。`EditorGameInstance::editorUserInterface_`（连同它持有的 `EditorUiState uiState_`）是 `std::unique_ptr`，在 `OnInitUI` 才惰性构造，**在 `ConfigureCVars`（line 406）时还不存在**。所以 `ed.*` cvar **不能绑 `uiState_`**，应绑到 `EditorGameInstance` 的一个直接成员 `EditorSettings settings_;`（随 GameInstance 在 line 401 构造即存在，指针稳定）。面板与各处行为统一从 `settings_` 读（通过 `EditorContext` 暴露，或 `GetEditorSettings()` 访问器），`EditorUiState` 里相关的临时开关（如 `outlinerAutoScrollToSelection`）迁移/改为读 `settings_`。

`EditorGameInstance::ConfigureCVars` 实现：

```cpp
void EditorGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string e;
    // (1) 编辑器进场默认（沿用旧 ApplyDefaultCVars 内容）
    cvars.SetDefaultFromString("r.samples", "4", &e);
    cvars.SetDefaultFromString("r.temporalFrames", "16", &e);
    cvars.SetDefaultFromString("r.denoiser", "0", &e);
    cvars.SetDefaultFromString("r.superResolution", "2", &e);
    // (2) 注册编辑器自己的 cvar（绑定到 settings_，全部 Archive）
    RegisterEditorCVars(cvars, settings_);
    // (3) ed.* 持久化到编辑器专属文件（§4.1.b）
    cvars.RegisterUserFileChannel("ed.", "assets/configs/cvar_user.editor.json");
}
```

初版 `ed.*` 清单（全部 `Archive`，绑 `settings_` 字段）：

- `ed.hoverHighlight`（bool，默认 true）→ 控 `OnCursorPosition` 是否 RayCast 设置 HoveredId。
- `ed.outlinerAutoScroll`（bool）→ Outliner 自动滚动到选中项。
- `ed.gizmoSnap`（bool）、`ed.gizmoSnapTranslate`（float）、`ed.gizmoDefaultMode`（int 0/1/2）。
- `ed.defaultRenderer`（int）、`ed.defaultSamples`（int）—— 可选，进一步把上面 (1) 的硬编码也变成可配置项。

改 `EditorMain.cpp::OnCursorPosition`：`if (settings_.hoverHighlight) { …RayCastGPU… }`，关闭时 `ClearHoveredId()` 一次。

### 4.3 编辑器层：新面板

- 新文件 `Panels/SettingsPanel.cpp`，函数 `void Editor::DrawSettingsPanel(EditorContext& ctx, EditorUiState& ui);`，声明加到 `EditorUi.hpp`。
- `EditorUiState` 加 `bool settingsPanel = false;`。
- `EditorInterface::Render()` 末尾加 `if (uiState_.settingsPanel) Editor::DrawSettingsPanel(ctx, uiState_);`。
- 面板内部：
  1. 首帧（或文件变更时）加载并解析 `assets/configs/ui/settings_panel.json`（`nlohmann/json` + `Utilities::FileHelper::GetPlatformFilePath`，与 `CVarSystem.cpp` 同款）。把解析结果缓存为一个 `FSettingsLayout`（categories→groups→items）静态/成员结构，避免每帧 IO。
  2. 渲染：分类列表 + 控件区；每个 item 按 `widget` 分派到一个 `DrawItem(ctx, item)`，内部用 `GetCVarSystem()` 读当前值、画控件、改了就 `SetValueFromString`。
  3. 复用 `NextUI::Theme::BeginFormRow` / `GraphicsDebugPanel::DrawSectionHeader` 保持风格一致；renderer 选择器可直接用 `GraphicsDebugPanel::DrawRendererSelector`。
- combo 的「整数值 ↔ 选项索引」映射：选项数组顺序即对应 cvar 整数值（0..n-1）。

### 4.4 清理 View 菜单假开关

- 把 `TitleBarOverlay.cpp` `View` 菜单里的 `static bool showGrid/showBounds/gizmo*` 等改成读写对应 cvar（`show.grid`、`show.wireframe`、`ed.gizmo*` 等），或直接移除并引导到设置面板，消除「点了没反应」的孤立开关。此项可作为独立小 PR。

## 5. 实施步骤（建议顺序）

1. **引擎 cvar 查询 API**（§4.1）+ namespace→文件路由 `RegisterUserFileChannel` / `LoadUserFiles` / `SaveUserFiles`（§4.1.b），含 `Engine.cpp` 改用 `LoadUserFiles()`、console `cvar.save` 改 `SaveUserFiles()` + 单测。`./gnb build gkNextRenderer gkNextUnitTests` 验证。
2. **`ApplyDefaultCVars` → `ConfigureCVars` 改名**（基类 + 3 个 override + `Engine.cpp:406` 调用点，§4.2）。`./gnb build gkNextRenderer gkNextUnitTests` 确认引擎与各 program 编译不破。
3. **编辑器 `EditorSettings settings_` + `RegisterEditorCVars`** + `ConfigureCVars` 注册 `ed.*` 与 `ed.` channel + `OnCursorPosition` 接 `settings_.hoverHighlight`（§4.2）。`./gnb build gkNextEditor`。
4. **`assets/configs/ui/settings_panel.json`** 初版（§3.2），覆盖 Rendering / Upscaling / Editor 三类常用项。
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
  - `Apply & Save` 后：`r.*` 等非默认项落到 `cvar_user.json`，`ed.*` 落到 **`cvar_user.editor.json`**，两文件互不混入；重启编辑器后两类偏好都保持。
  - **跨 app 不互相破坏**：编辑器保存后再跑一个 game（如 `gnb run MagicaLego`）并触发其保存，`cvar_user.json` 里**不**应出现 `ed.*`，且不丢 game 的值；console `cvar.save` 与面板 `Apply & Save` 行为一致。
  - 故意把 manifest 里写一个不存在的 cvar / 删除 `settings_panel.json`：面板不崩，日志 WARN，回退内置默认。
- **视觉**：渲染相关项可用 `gnb shot --target gkNextEditor`（若该 target 支持 `--scene`/agent-validation）截图肉眼确认；否则手动跑编辑器目测。

## 7. 风险与注意

- **时序**：`ed.*` cvar 与 `ed.` channel 必须在 `LoadUserFiles` 之前注册，否则用户保存的编辑器偏好读不回来——这正是用 `ConfigureCVars`（line 406，`LoadUserFiles` 之前）而非 `OnInit`（line 574，太晚）的原因。
- **生命周期**：`ed.*` 绑的目标必须是 `ConfigureCVars` 时已存在且地址稳定的对象——用 `EditorGameInstance` 直接成员 `settings_`，**不要绑** `editorUserInterface_->uiState_`（那时还没构造）。
- **combo 值映射**：`r.rendererType` 的整数定义见 `EngineCVars.cpp:61`（0=PathTracing,1=SoftwareTracing,2=SoftwareModern,3=VoxelTracing,4=SwModernNoAmbient）；`settings_panel.json` 的 `options` 顺序必须与之一致，且部分 renderer 在不支持硬件光追的设备上不可用（参考 `GraphicsDebugPanel::GetRendererOptionCount`），combo 渲染时应据此裁剪。
- **持久化语义**：`SaveUserFiles` 只落「Archive 且 ≠ 默认」的项，并按 namespace 分文件；非 `Archive` 的 `show.*`/`debug.*` 改动不会被保存，这是预期；若希望某编辑器项跨会话保留务必给 `Archive`（它会进 `cvar_user.editor.json`）。
- **只拆 editor 的边界**：当前仅编辑器注册 channel。若以后某 game 也想要专属文件，照样在它的 `ConfigureCVars` 里 `RegisterUserFileChannel` 即可，机制已通用，无需再改引擎。
- **不要硬编码绝对路径**；json 走 `Utilities::FileHelper::GetPlatformFilePath`。
- **第三方目录勿改**；`nlohmann/json` 已是依赖，直接用。
- **命名规范**（`.clang-tidy`）：类型/函数 PascalCase，成员 `camelCase_`，文件首个 include `Common/CoreMinimal.hpp`，Allman 大括号，4 空格缩进。

## 8. CVarSystem 重构评估

整体结论：**`FCVarSystem` 设计是站得住的**（值/默认/绑定目标/flags/onChanged 都齐，console 命令、Match、持久化都已具备），**不需要重写**。但有几处和本次「数据驱动面板」直接相关、值得顺手做的改进，按价值排序：

**高价值（直接改善面板/console 体验，建议本次一起做）：**

1. **`onChanged` 只在值真变时触发**。`SetEntryValue`（CVarSystem.cpp:592）目前任何一次 set 都调 `onChanged()`，不比较新旧值。面板里拖 slider / 反复点选会**每帧狂触发** `RequestRecreateSwapChain` 这类重回调，卡顿甚至闪烁。改：set 前先 `GetEntryValue(entry)` 比较，值未变则跳过回调（且可跳过 `setBy` 覆盖判断的副作用）。

2. **给 cvar 增加可选 min/max（和 isUnsigned）元信息**。当前 UInt 和 Int 都存成 `ECVarType::Int`，只有 `SetEntryValue` 里 `val<0→0` 的隐式钳制，元信息层**拿不到取值范围**。而 §9 的「通用全 app 面板」是从注册表自动生成的、**没有 manifest** 可读范围，必须靠注册时带的 range 才能渲染合理的 slider 并 clamp。改：`FCVarEntry` 加可选 `min/max`（variant 同类型）+ `isUnsigned`；`RegisterInt/UInt/Float` 增加可选 range 重载；`§4.1` 的 `FCVarInfo` 顺带带出 `min/max/isUnsigned`。curated 的 `settings_panel.json` 仍可覆盖范围，但注册表自带范围让 console 与通用面板都受益。

**中价值（清理，降重复，可单独 PR）：**

3. **`SetEntryValue` / `GetEntryValue` 的 variant 分派**用 `std::visit` + overloaded lambda 收敛。现在是两段冗长的 `holds_alternative` if-else（~60 + ~25 行），逻辑重复、易漏分支。

4. **类型映射集中化**。`ECVarType`→字符串在 `cvar.help`（327-328 inline 三元）、`ToString`、`ParseValue`、`FlagsToString` 各写一遍；`ECVarType`↔stored alternative 的对应散在 `RegisterTyped` 的 `if constexpr`。集中成一张 trait/表，新增类型时只改一处。

5. **`ExecuteCommand` 的大 if-else 命令链**（`cvar.list/complete/help/reset/save/...`）可换成 `name→handler` 表，新增命令更干净；也方便 §9 加 `cvar.editor` 开关命令。

**低价值（按需，本次可不做）：**

6. **注册顺序**：`cvars_` 是 `unordered_map`，`Match` 每次结果都要 `std::sort`。若希望面板/`ForEach` 稳定有序展示，额外维护一个注册序 `vector<string>`（保留 map 做查找）。

7. **持久化 float 比较**用绝对阈值 `1e-4`（SaveUserFile:185），对极大/极小值不稳；可改相对+绝对混合阈值。

> 建议：本次只做 1、2（与面板强相关），3-5 视精力作为独立清理 PR，6-7 记 backlog。1、2 都应补 `[Unit][CVar]` 用例。

## 9. 通用 CVar 配置面板（所有 application 可打开）

除了编辑器那个 curated、`settings_panel.json` 驱动的设置面板，再做一个**通用、自动从 cvar 注册表生成**的「CVar 编辑器」面板，覆盖 `cvar_default` / `cvar_user` 的全部 cvar，**在所有 application（renderer、editor、各 game）都能打开编辑**。

**两个面板的分工（共用同一套读写/持久化管线）：**

| | 通用 CVar 编辑器（本节） | 编辑器设置面板（§3-4） |
|---|---|---|
| 来源 | 自动遍历注册表，全部 cvar | `settings_panel.json` 手工策展 |
| 受众 | 开发者 / 调参 | 终端用户 |
| 范围 | 所有 app | 仅 gkNextEditor |
| 形态 | 按 namespace 分组的搜索表格 | 分类化、精修控件 |

**挂载点——`IDebugUiProvider`（已是全 app 中央机制）**。引擎在渲染路径里统一调 `debugUiProvider_->DrawGraphicsPanel(...)`（`Engine.cpp:1260`），由 `DevTools::DefaultDebugUiProvider`（`Modules/DevTools/DevToolsDebugUiProvider`）实现，各 app 在入口 `engine.SetDebugUiProvider(...)` 注册。把通用面板加进这条链，**零 per-app 代码**即可全 app 可用：

1. **接口**：`Runtime/DebugUiProvider.hpp` 的 `IDebugUiProvider` 加 `virtual void DrawCVarEditor(NextEngine& engine, bool& panelVisible) = 0;`（或并入既有 `DrawUiPanels`）。
2. **实现**：`DevToolsDebugUiProvider` 实现之，内容全部由 cvar 系统驱动：
   - `engine.GetCVarSystem().ForEach(...)`（§4.1 新 API）拿到每个 `FCVarInfo`，按 namespace 前缀（`r./sys./show./debug./ed.…`）分组、可折叠；
   - 顶部搜索框复用 `Match`；
   - 每行按 `type` 渲染内联编辑控件（bool→checkbox，int/float→drag，带 §8.2 的 min/max 时→slider，string→input），改动走 `SetValueFromString`；
   - 每行显示 `description`（tooltip）、是否 `isDefault`（「modified」标记 + 高亮）、`Reset` 按钮（`ResetToDefault`）；
   - 顶部 `Save`→`SaveUserFiles()`（§4.1.b，自动按 channel 分流到 `cvar_user.json` / `cvar_user.editor.json`）。
3. **开关**：加 `ShowFlags.DebugCVarPanel` + 一个快捷键（与 `DebugGraphicsPanel` 的 F2 同风格，见 `Engine.Input.cpp`）+ console 命令 `cvar.editor`（在 `ExecuteCommand` 注册，翻转该 flag）。引擎在 `Engine.cpp` 紧挨 `DrawGraphicsPanel` 处加 `if (showFlags.DebugCVarPanel) debugUiProvider_->DrawCVarEditor(*this, showFlags.DebugCVarPanel);`。
4. **MagicaLego 特例**：它有自绘 console（`MagicaLegoUserInterface::DrawConsoleWindow`），不影响——通用面板走的是引擎中央 `debugUiProvider_` 路径，只要它注册了 provider 就能用。

**与编辑器面板的衔接**：编辑器设置面板可加一个「Advanced / All CVars」入口，直接调用同一个 `DrawCVarEditor`，避免两套实现。`gkNextRenderer` 现有 `DrawSettings()`（硬编码）后续也可逐步让位于这个通用面板。

**实施顺序**：依赖 §4.1 的 `ForEach`/`FCVarInfo` 和 §4.1.b 的 `SaveUserFiles`；建议在 §5 的第 1 步之后、独立于编辑器面板推进（它不依赖 `settings_panel.json`，可先于 §4.3 落地，作为最小可用的全 app 调参入口）。验证：在 `gkNextRenderer` 和某个 game 里按快捷键 / `cvar.editor` 打开，改 `r.*` 即时生效、`Save` 后 `cvar_user.json` 更新。

## 10. 关键文件索引

| 用途 | 路径 |
|---|---|
| 面板渲染入口 / Toolbar / DockSpace | `src/Application/Editor/gkNextEditor/EditorInterface.cpp` |
| 面板声明 | `src/Application/Editor/gkNextEditor/EditorUi.hpp` |
| 面板可见性状态 | `src/Application/Editor/gkNextEditor/Core/EditorUiState.hpp` |
| 主菜单 / 标题栏 | `src/Application/Editor/gkNextEditor/Overlays/TitleBarOverlay.cpp` |
| 编辑器 GameInstance / hover / `ConfigureCVars` / `EditorSettings settings_` | `src/Application/Editor/gkNextEditor/EditorMain.{h,cpp}` |
| 新面板（待建） | `src/Application/Editor/gkNextEditor/Panels/SettingsPanel.cpp` |
| CVar 系统（查询 API + namespace→文件路由 待加） | `src/Engine/Runtime/Config/CVarSystem.{hpp,cpp}` |
| 引擎 cvar 注册表 | `src/Engine/Runtime/Config/EngineCVars.cpp` |
| 运行时设置载体 | `src/Engine/Runtime/Config/UserSettings.hpp`、`ShowFlags.hpp` |
| 引擎接线 / `ConfigureCVars` 钩子 + `LoadUserFiles()` | `src/Engine/Runtime/Engine.cpp`（~404–407）、`GameInstance.hpp` |
| RenderSetting 样板 | `src/Application/Render/gkNextRenderer/gkNextRenderer.cpp::DrawSettings` |
| 可复用 UI helper | `src/Modules/DevTools/ProfessionalUI.hpp`、`GraphicsDebugPanel.hpp` |
| 全 app 中央 dev UI 接口（加 `DrawCVarEditor`，§9） | `src/Engine/Runtime/DebugUiProvider.hpp` |
| 全 app dev UI 实现 / 通用 cvar 面板（待建，§9） | `src/Modules/DevTools/DevToolsDebugUiProvider.{hpp,cpp}` |
| 中央调用点（DrawGraphicsPanel 旁加 DrawCVarEditor） | `src/Engine/Runtime/Engine.cpp`（~1260）、`Engine.Input.cpp`（快捷键） |
| 跨 app console（cvar 命令 / Match 复用） | `src/Modules/DevTools/UiDevPanels.{hpp,cpp}` |
| 视图 manifest（待建，非值文件） | `assets/configs/ui/settings_panel.json` |
| 值文件：默认 / 共享用户 / 编辑器用户 | `assets/configs/cvar_default.json`、`cvar_user.json`、`cvar_user.editor.json`（待建） |
