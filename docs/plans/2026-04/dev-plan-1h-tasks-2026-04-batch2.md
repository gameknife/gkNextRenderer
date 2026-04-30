# 1 小时任务开发计划 第二批 (2026-04, Editor UX 收尾 + Showcase 场景扩展)

## Context

第一批 9 项任务(`docs/plans/2026-04/dev-plan-1h-tasks-2026-04.md`)中 **8 项已完成**(A2/A3/B1/B2/B3/B4/C1/D1),仅 **A1 (Save Scene As 文件对话框)** 未实现。本批继续以「<1h 自包含」为口径,聚焦两个方向:

1. **Editor UX 收尾**: 当前编辑器还残留若干「画了但没接线」的 UI 元素(`Static Mode` 复选、重复的 `Edit > Reset`、footer Home/Pen 按钮),File 菜单也只有 `Save Scene As / Exit` 两项可用,缺 `Open Scene`、`Recent Scenes`,选中为空时 Properties 面板会瞬空白。这些会持续给用户「不完整」的观感,且都属于小幅可闭环改动。
2. **Showcase 场景扩展**: 第一批新增了 `MaterialShowcase.proc`,验证了 procedural 场景的回归价值。`SceneList.cpp` 仍只有 2 个 showcase(`GIBootcamp`、`MaterialShowcase`)。再补 `LightingShowcase` / `CameraShowcase` 与对应的 visual_test 配置,可让光照与相机参数变化在视觉测试里直接被发现。

A1 暂作 known issue 列在文末,等待用户单独安排处理。

## 使用方法

1. **挑任务**: 任选 1 项,逐项完成 TODO。
2. **遵循公共约束**: 命名/构建/平台规则全部沿用 [`AGENTS.md`](../../AGENTS.md)。
3. **构建 preset**: 验证一律使用 `full-*` preset,与上批一致。
4. **报告**: 完成后简述「改了哪些文件、测了什么、看到的输出」,**不**总结代码意图。

## 公共上下文(快速参考)

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows` / `./build.sh --preset full-macos-arm64` |
| 运行编辑器 | `./run.bat --preset full-windows --target gkNextEditor.exe` |
| 运行渲染器 | `./run.bat --preset full-windows --target gkNextRenderer.exe` |
| 运行 visual test | `./out/build/full-windows/bin/gkNextVisualTest.exe` |
| 运行成功标志 | 日志出现 `uploaded scene [...] to gpu` |
| 命名规范 | 类型/函数 PascalCase;变量/参数 camelCase;私有成员 trailing `_`;详见 `.clang-tidy` |
| 头文件首选 | 新文件首先 `#include "Common/CoreMinimal.hpp"` |

> 提示:**禁止**修改 `src/ThirdParty/` 与 `external/`。

---

## 任务索引

| # | 标题 | 工时 | 优先级 |
|---|---|---|---|
| [E1](#e1-清理-titlebaroverlay-的死按钮和重复菜单) | 清理 TitleBarOverlay 的死按钮和重复菜单 | ~30m | P0 |
| [E2](#e2-file--open-scene-接入-sdl_showopenfiledialog) | File > Open Scene 接入 SDL_ShowOpenFileDialog | ~45m | P0 |
| [E3](#e3-file--recent-scenes-子菜单与持久化) | File > Recent Scenes 子菜单与持久化 | ~1h | P1 |
| [E4](#e4-properties-面板空选状态友好提示) | Properties 面板空选状态友好提示 | ~20m | P1 |
| [E5](#e5-outliner-节点右键复制节点路径) | Outliner 节点右键「复制节点路径」 | ~30m | P2 |
| [S1](#s1-lightingshowcase-procedural-场景) | LightingShowcase procedural 场景 | ~1h | P1 |
| [S2](#s2-camerashowcase-procedural-场景) | CameraShowcase procedural 场景 | ~45m | P2 |
| [S3](#s3-visual_testjson-接入新场景--per-scene-timeout) | visual_test.json 接入新场景 + per-scene timeout | ~30m | P2 |
| [Known Issue: A1 carry-over](#known-issue-a1-save-scene-as-文件对话框) | Save Scene As 文件对话框(carry-over) | ~45m | (deferred) |

---

## E1. 清理 TitleBarOverlay 的死按钮和重复菜单

**优先级**: P0  **工时**: ~30m  **风险**: 极低

### 背景
[`TitleBarOverlay.cpp`](../../src/Editor/Overlays/TitleBarOverlay.cpp) 当前同时存在 3 处「画了但没接线」的 UI:

- L116 `ImGui::MenuItem("Static Mode", nullptr);` — 第二参数为 `nullptr`(无快捷键),且未传 `bool*` 状态,纯装饰。HelpMarker 描述的「static/linear vs fixed/manual layout」也并无对应实现。
- L121-123 `if (ImGui::MenuItem("Reset")) { }` — 在 `Edit` 菜单内但**位于 `Layout` 子菜单之外**,与 L108-113 的 `Layout > Reset`(已在第一批接入 `dockResetRequested`)语义重复且空操作。
- L246-252 footer 的 `ICON_FA_HOUSE` 与 `ICON_FA_PEN` 按钮,handler 为空 `{}`。

这三处都给用户造成「点了没反应」的 UX 噪声。本任务只做删除,不引入新功能。

### TODO

- [ ] 删除 [`TitleBarOverlay.cpp:114-119`](../../src/Editor/Overlays/TitleBarOverlay.cpp) 的 `if (ImGui::BeginMenu("Behavior")) { ImGui::MenuItem("Static Mode", ... ); }` 整段(连同 HelpMarker)
- [ ] 删除 [`TitleBarOverlay.cpp:121-123`](../../src/Editor/Overlays/TitleBarOverlay.cpp) 重复的 `if (ImGui::MenuItem("Reset")) { }` 整段
- [ ] 删除 [`TitleBarOverlay.cpp:246-253`](../../src/Editor/Overlays/TitleBarOverlay.cpp) footer 的两个 `ICON_FA_HOUSE` / `ICON_FA_PEN` 空 lambda 按钮(以及紧随其后多余的 `ImGui::SameLine()`)
- [ ] 检查 `EditorUtils.h` 的 `utils::HelpMarker` 是否还有其他调用方;若无,**不**做额外清理(超范围)
- [ ] 重新编译,目视确认 `Edit` 菜单只剩 Undo/Redo/Layout 三项,footer 第一行仅剩 CVar 输入框

### 涉及文件
- `src/Editor/Overlays/TitleBarOverlay.cpp` (主改,纯删减)

### 验收方法
1. 构建: `./build.bat --preset full-windows`,无新增警告
2. 启动 `gkNextEditor`:
   - `Edit` 菜单展开后,只能看到 `Undo`、`Redo`、分隔、`Layout > Reset`
   - footer 不再有「房子」和「笔」图标,直接是 CVar 输入框
3. 鼠标悬停以前 `Static Mode` 处,**不应**再有 tooltip 出现
4. 反复打开关闭 `Edit` 菜单 5 次,无 ImGui 警告或断言

### 注意
- 这一改动**不应**改任何状态字段或 action enum,纯 UI 删减
- 若觉得后续仍想用 footer Home/Pen 做模式切换,把它们留到独立任务,不要在本任务里夹带
- `EditorUtils.h` 的 `HelpMarker` 是公用工具,**不要**因为本任务删了一处调用就把它一并删除

---

## E2. File > Open Scene 接入 SDL_ShowOpenFileDialog

**优先级**: P0  **工时**: ~45m  **风险**: 低

### 背景
`File` 菜单目前只有 `Save Scene As...` 与 `Exit` 两项。打开场景需要走「内容浏览器双击 .glb」或拖拽,缺一个标准 `File > Open Scene...` 入口。

引擎层的接口已经齐备:`NextEngine::RequestLoadScene(std::string)` ([`Engine.cpp:1467`](../../src/Runtime/Engine.cpp))、action `EEditorAction::IO_LoadScene` ([`EditorMain.cpp:73-78`](../../src/Editor/EditorMain.cpp))。SDL3 文件对话框也有现成范式:[`BrickPlayerGameInstance.cpp:2264-2283`](../../src/Application/BrickPlayer/BrickPlayerGameInstance.cpp)。

### TODO

- [ ] 在 [`TitleBarOverlay.cpp`](../../src/Editor/Overlays/TitleBarOverlay.cpp) `File` 菜单 `Save Scene As...` **之前**插入一个 `Open Scene...`(快捷键 `Ctrl+O`)
- [ ] 点击后调用 `SDL_ShowOpenFileDialog`:
  - filter 至少 `{ "Scenes", "glb;gltf;ldr;mpd" }` 与 `{ "All Files", "*" }`
  - `userdata` 传 `&ctx.engine`(或封装一个简单结构体把 `EditorContext*` 带过去)
  - callback 内:`filelist[0]` 非空 → `engine->RequestLoadScene(filelist[0])`
  - 取消(`filelist == nullptr` 或 `filelist[0] == nullptr`)→ 仅 `SPDLOG_DEBUG`,不报错
- [ ] 复用 `ctx.engine.GetWindow().Handle()` 作为父窗口
- [ ] 由于 `TitleBarOverlay` 是 namespace 函数,直接捕获 `ctx` 不便;首选**把 `EditorContext*` 通过 `userdata` 传递**(单例式静态全局也行,但 userdata 更干净)。可参考 BrickPlayer 的写法

### 涉及文件
- `src/Editor/Overlays/TitleBarOverlay.cpp` (主改)

### 验收方法
1. 构建通过
2. `gkNextEditor` 中 `File > Open Scene...` 弹出原生对话框
3. 选 `assets/models/kitchen.glb` 或任一 `.glb`,点确定 → 视口加载新场景,日志出现 `uploaded scene [...] to gpu`
4. 取消对话框,无错误日志、无崩溃
5. `Ctrl+O` 也能触发(若 ImGui 已绑定;否则只在菜单上显示快捷键提示也合格)

### 注意
- SDL 文件对话框为**异步**回调,callback 在 SDL 事件泵驱动下执行,引擎主循环不会阻塞
- **不要**把 `RequestLoadScene` 改成同步阻塞的 `LoadScene`,会卡 UI 线程
- 若 `userdata` 用 `EditorContext*`,callback 中**不要**长时间持有 `ctx.scene` 引用 — 仅在该回调内使用,然后就交给引擎管线
- 不要新增依赖;SDL3 已是核心依赖

---

## E3. File > Recent Scenes 子菜单与持久化

**优先级**: P1  **工时**: ~1h  **风险**: 中(IO 与序列化)

### 背景
开发回归经常需要在几个固定场景之间切换。E2 完成后,Open Scene 需要重新选目录,体验仍不流畅。`File > Recent Scenes` 维持 5-10 项 MRU 列表,本地持久化即可解决。

`EditorUiState` ([`EditorUiState.hpp`](../../src/Editor/Core/EditorUiState.hpp)) 是天然落点;持久化文件可放在用户配置目录(参考引擎现有的写文件路径,例如 `imgui.ini` 同目录)。

### TODO

- [ ] 在 [`EditorUiState.hpp`](../../src/Editor/Core/EditorUiState.hpp) 增加 `std::vector<std::string> recentScenes;`(上限常量 `kRecentScenesCap = 10`)
- [ ] 写两个工具函数(`EditorUiState.cpp` 或新文件 `Editor/Core/RecentScenes.{hpp,cpp}`):
  - [ ] `void LoadRecentScenes(EditorUiState& ui)`:启动时读 `recent_scenes.txt`(或简单 JSON),每行一个绝对路径
  - [ ] `void SaveRecentScenes(const EditorUiState& ui)`:落盘
  - [ ] `void PushRecentScene(EditorUiState& ui, std::string path)`:已存在则提到队首,否则插入并裁剪到 `kRecentScenesCap`,**最后调用 SaveRecentScenes**
- [ ] 在 [`EditorMain.cpp`](../../src/Editor/EditorMain.cpp) 的 `IO_LoadScene` action 入口、E2 的 Open Scene callback 内,调用 `PushRecentScene`
- [ ] 在 [`TitleBarOverlay.cpp`](../../src/Editor/Overlays/TitleBarOverlay.cpp) `File` 菜单内 `Open Scene...` 之后插入 `BeginMenu("Recent Scenes")`:
  - [ ] 遍历 `ui.recentScenes`,每项为 `MenuItem(displayName)`,点击 → `engine.RequestLoadScene(path)`
  - [ ] `displayName` 取 `std::filesystem::path(p).filename().string()`,tooltip 显示完整路径
  - [ ] 列表为空时显示 disabled `MenuItem("(empty)")`
  - [ ] 末尾分隔 + `Clear` 项,点了清空并落盘
- [ ] 引擎启动主路径(`EditorMain.cpp` 初始化处)调用 `LoadRecentScenes`

### 涉及文件
- `src/Editor/Core/EditorUiState.hpp` (字段 + 常量)
- `src/Editor/Core/RecentScenes.{hpp,cpp}` (新增,可选;也可直接放到 `EditorUiState.cpp`)
- `src/Editor/EditorMain.cpp` (启动加载 + action 入口 push)
- `src/Editor/Overlays/TitleBarOverlay.cpp` (UI)

### 验收方法
1. 构建通过
2. 启动 → `File > Recent Scenes` 显示 `(empty)`
3. 用 E2 的 Open Scene 打开两个不同 `.glb`
4. `File > Recent Scenes` 出现这两条,**最近的在最上**
5. 点击其中一条,场景被加载;它会被提到队首
6. 关闭并重新启动 `gkNextEditor`,Recent Scenes 列表保留(同样顺序)
7. `Clear` 后立刻为 `(empty)`,重启依然为空

### 注意
- 持久化文件路径选与 `imgui.ini` 同目录,或者用 `Utilities::FileHelper` 现有的可写路径(参考 `gkNextVisualTest` 的输出目录策略)。**不要**写到 `assets/`(那是只读)
- 路径写入前先 `std::filesystem::weakly_canonical`,避免相对路径在不同 cwd 下失效
- 不要使用第三方 JSON 库 + 新依赖;一行一路径的 plain text 即可
- 多人协作场景下不应该把这个文件加入 git;若放仓库内目录,记得加 `.gitignore`

---

## E4. Properties 面板空选状态友好提示

**优先级**: P1  **工时**: ~20m  **风险**: 极低

### 背景
[`PropertiesPanel.cpp:32-36`](../../src/Editor/Panels/PropertiesPanel.cpp) 当 `selectedIds` 为空时,直接 `ImGui::End(); return;`,Properties 面板呈现纯空白。新用户/agent 容易误以为面板坏了。给一个居中提示("Select an object in the Outliner")可大幅改善体感。

### TODO

- [ ] 在 [`PropertiesPanel.cpp:32`](../../src/Editor/Panels/PropertiesPanel.cpp) 的 `if (selectedIds.empty())` 分支,在 `ImGui::End()` 之前绘制居中提示:
  - [ ] 使用 `ICON_FA_CIRCLE_INFO` + 一行说明文字(灰色,`ImGuiCol_TextDisabled`)
  - [ ] 文案:"No object selected" + "Select an object from the Outliner or Viewport"(两行,小一号)
  - [ ] 居中实现:用 `ImGui::GetContentRegionAvail()` 计算,`SetCursorPos` 把光标移到中部
- [ ] 为 `size > 1` 但找不到 active 节点的分支(L53-57)同样加一个简短 disabled 提示,避免空白
- [ ] **不要**调整任何选中逻辑,只动渲染分支

### 涉及文件
- `src/Editor/Panels/PropertiesPanel.cpp`

### 验收方法
1. 构建通过
2. 打开 `gkNextEditor`,任意场景加载后**先不选中任何节点**,Properties 面板显示居中提示
3. 在 Outliner 选中一个节点,提示消失,正常属性出现
4. 取消选中(在 Outliner 空白处点击),提示再次出现
5. 切换面板大小,提示始终居中

### 注意
- **不要**用 `ImGui::Text` 后做 `SameLine` 拼接,易在窄面板下错位
- `ICON_FA_CIRCLE_INFO` 已存在于 `IconsFontAwesome6.h`,无需新增依赖
- 不要给该面板加 `ImGuiWindowFlags_NoBackground` 之类副作用 flag

---

## E5. Outliner 节点右键「复制节点路径」

**优先级**: P2  **工时**: ~30m  **风险**: 低

### 背景
调试场景树、写脚本(QuickJS `Scene.FindNodeIdWithComponent`)、向他人/agent 描述节点位置时,需要一个准确的节点引用。当前 Outliner 没有「拷贝节点信息」入口。每次都要手动定位很啰嗦。

### TODO

- [ ] 在 [`OutlinerPanel.cpp`](../../src/Editor/Panels/OutlinerPanel.cpp) `DrawNode` 现有的右键菜单(若已存在)或新增的 `BeginPopupContextItem` 中加两项:
  - [ ] `Copy Node Name` → `ImGui::SetClipboardText(node.GetName().c_str())`
  - [ ] `Copy Node Path` → 复制完整层级路径,如 `Root/Building/Door01`
- [ ] 实现一个本地静态 `std::string MakeNodePath(const Assets::Node& node)`,从根递归拼接 `parent->name / .../ self->name`(分隔符 `/`)
- [ ] 复制成功后,可选地用 `SPDLOG_INFO` 输出,便于确认
- [ ] **不要**触发选中/反选;`BeginPopupContextItem` 默认不会改变选中状态

### 涉及文件
- `src/Editor/Panels/OutlinerPanel.cpp`

### 验收方法
1. 构建通过
2. 加载场景,在 Outliner 右键任意节点 → 菜单出现 `Copy Node Name`、`Copy Node Path`
3. 点 `Copy Node Path`,粘贴到记事本/聊天框,字符串与可视层级一致
4. 嵌套较深的节点(>3 级)路径正确,无截断
5. 在子节点上点 `Copy Node Name` 仅获得叶子名,无父级前缀

### 注意
- `ImGui::PushID` 必须配合 popup 一起设置,避免多个节点的 popup ID 冲突
- 节点名含 `/` 的极端情况:**不**做转义处理,直接原样拼接(超范围),但加一个 `SPDLOG_DEBUG` 提醒可能歧义即可
- 不要在右键 popup 里塞一堆功能;本任务只加这两条复制项

---

## S1. LightingShowcase procedural 场景

**优先级**: P1  **工时**: ~1h  **风险**: 低

### 背景
`MaterialShowcase` 验证了 procedural showcase 模板的回归价值。光照管线(直接光、面光、不同 shape、强度范围、阴影)同样需要一个稳定基线。`LightingShowcase` 应在固定材质下,枚举常见灯光设置,让 visual test 一眼能看出阴影/光强度回归。

### TODO

- [ ] 在 [`SceneList.cpp`](../../src/Runtime/Scene/SceneList.cpp) 仿 `MaterialShowcase`(L631-741)新增 `LightingShowcase` 函数:
  - [ ] 1 个地面 (Lambertian gray) + 一面墙(同材质,作为阴影投射对象)
  - [ ] 4 个相同的中性灰球作为「receiver」并排
  - [ ] 4 种不同灯光(各对应一个球):
    - [ ] 点光(强度中等,1 个)
    - [ ] 面光(矩形,DiffuseLight)
    - [ ] 平行光/远距离 spot(模拟太阳)
    - [ ] 弱填充光(低强度,体现阴影柔化)
  - [ ] 中性 HDRI(`HasSky = true; SkyIdx = 0`,与 `MaterialShowcase` 保持一致)
  - [ ] 相机正面对着球阵列,稍俯视,使阴影对地面可见
- [ ] 在 [`SceneList.cpp:1293`](../../src/Runtime/Scene/SceneList.cpp) 之后追加 `AllScenes.push_back("LightingShowcase.proc");`
- [ ] 在 [`SceneList.cpp:1370-1373`](../../src/Runtime/Scene/SceneList.cpp) 附近添加 `if (filename == "LightingShowcase.proc")` 分支并调用新函数
- [ ] **本任务不修改 visual_test.json**,该文件由 S3 任务统一更新

### 涉及文件
- `src/Runtime/Scene/SceneList.cpp` (主改)

### 验收方法
1. 构建通过
2. `./run.bat --preset full-windows --target gkNextRenderer.exe`,场景下拉看到 `LightingShowcase.proc`
3. 选中后视口出现 4 球 + 地面 + 墙,**4 种不同光影特征清晰可辨**
4. 日志出现 `uploaded scene [LightingShowcase.proc] to gpu`
5. 切换至 RT/PathTracing 管线,光强度量级合理(无全黑/全白)

### 注意
- 不要新增贴图依赖,完全 procedural
- 球间距 ≥ 2.5 倍球半径
- 灯光强度参考 `GIBootcamp` 范围(数百~上千),**不要**用 1.0 这种导致过暗的值
- `LightingShowcase` 不是 path tracer 优劣比较场,**只**关注「光照参数变化能被一眼看出」

---

## S2. CameraShowcase procedural 场景

**优先级**: P2  **工时**: ~45m  **风险**: 低

### 背景
相机参数(FOV、近远裁、aspect)在 visual test 里目前没有覆盖。一个简单的 `CameraShowcase`:把若干结构清晰的几何放在已知距离上,固定灯光,**靠相机参数变化来制造视觉差异**,适合作为 FOV/projection 改动的回归基准。

### TODO

- [ ] 在 [`SceneList.cpp`](../../src/Runtime/Scene/SceneList.cpp) 仿 `MaterialShowcase` 新增 `CameraShowcase`:
  - [ ] 一字排开 5 个不同尺寸的 cube(从近到远),用于体现透视
  - [ ] 旁边放置一个 grid(地面 plane + 棋盘格 procedural 材质或简单条纹色块)体现近远清晰度
  - [ ] 中性 HDRI + 1 盏方向光,关闭面光以避免分散注意力
  - [ ] 默认相机 FOV 写一个**有特征**的值(例如 50°),让回归时易辨别 FOV 漂移
- [ ] `AllScenes.push_back("CameraShowcase.proc");` + 加载分支
- [ ] **本任务不修改 visual_test.json**

### 涉及文件
- `src/Runtime/Scene/SceneList.cpp`

### 验收方法
1. 构建通过
2. 渲染器场景下拉出现 `CameraShowcase.proc`
3. 加载后,5 个 cube 透视感清晰,地面 grid 可读
4. 日志正常 `uploaded scene [...] to gpu`
5. 在 ImGui 调相机 FOV slider,视觉变化即时响应(不必把交互 FOV 持久化)

### 注意
- 不要在 procedural 里塞太多几何,5 个 cube 足够
- grid 用现成 procedural 材质即可,**不要**新增贴图
- `cameraInit.fov` 字段名以代码现状为准;参考 `GIBootcamp` 设置方式

---

## S3. visual_test.json 接入新场景 + per-scene timeout

**优先级**: P2  **工时**: ~30m  **风险**: 低

### 背景
[`visual_test.json`](../../assets/configs/visual_test.json) 当前只有 `MaterialShowcase.proc` 一项,且全局 `loadTimeoutSeconds: 20.0` 一刀切。S1/S2 完成后,新增的 showcase 需要纳入测试;同时 procedural 场景与 LDraw `.mpd` 加载耗时差距显著(后者经常 >20s),需要 per-scene timeout 覆盖。

### TODO

- [ ] 修改 [`visual_test.json`](../../assets/configs/visual_test.json) `scenes` 数组,加入 `"LightingShowcase.proc"`、`"CameraShowcase.proc"`(顺序无关)
- [ ] 在 [`gkNextVisualTest.cpp/.hpp`](../../src/Application/gkNextVisualTest/gkNextVisualTest.cpp) 解析逻辑增加新字段 `sceneTimeouts` (`std::unordered_map<std::string, double>`):
  - [ ] JSON 形如 `"sceneTimeouts": { "MagicaCity.mpd": 60.0 }`,key 为 scene 文件名(与 `scenes` 一致)
  - [ ] 读取后,在 wait/timeout 处先查 `sceneTimeouts`,缺失则 fallback 到全局 `loadTimeoutSeconds`
- [ ] **不**改任何已通过的场景的等待逻辑(只在缺省路径上 fallback)
- [ ] 在 `visual_test.json` 顶部加 `"sceneTimeouts": {}` 占位(空对象)便于后续扩展

### 涉及文件
- `assets/configs/visual_test.json`
- `src/Application/gkNextVisualTest/gkNextVisualTest.cpp`
- `src/Application/gkNextVisualTest/gkNextVisualTest.hpp`(若需暴露字段)

### 验收方法
1. 构建通过
2. `./out/build/full-windows/bin/gkNextVisualTest.exe` 跑完,**3 个 procedural showcase 都生成截图**
3. HTML 报告新增两行,RMSE/diff 列正常显示(初次运行无 baseline 时显示 "no baseline")
4. 在 `visual_test.json` 给某场景设置异常小的 timeout(例如 0.1)→ 该场景 timeout 失败,其他场景照常通过 → 验证 per-scene 覆盖生效
5. 删除 `sceneTimeouts` 字段或留空,行为退回纯全局 timeout

### 注意
- 解析失败要 `SPDLOG_WARN` 降级到全局 timeout,**不要**直接 abort
- 不要把 baseline 截图提交进 git
- 测试前如果改了 procedural 场景内容,记得先 `--update-baseline`

---

## Known Issue: A1 Save Scene As 文件对话框

**carry-over from previous batch**, 工时 ~45m。

[`TitleBarOverlay.cpp:54-67`](../../src/Editor/Overlays/TitleBarOverlay.cpp) 仍硬编码 `"saved_scene.glb"` 并保留 TODO 注释。本批暂不处理,待用户后续单独安排。E2 的 Open Scene 实现完成后,Save Scene As 可直接复用同一 `userdata` 模式,届时 30m 内即可闭环。

---

## 完成后的常规收尾

- 不要在代码里留 `// TODO` / `// FIXME`,除非明确标注下一项任务
- 不要新增 `.md` 文档,除非用户/计划文档明确要求
- `SPDLOG_INFO` 给最终成功路径,`SPDLOG_WARN` 给可恢复异常,`SPDLOG_ERROR` 给真正失败
- 提交前 `git status` 不应有未跟踪的中间文件(`out/`、临时截图等)
- 严格遵循 `AGENTS.md` 的「执行前确认」原则:**不要**做任务卡范围之外的「顺手清理」

## 任务挑选建议

按时间块从 P0 开始:
- **第 1 块(30m)**: E1 (清理死按钮) — 体感反馈最直接
- **第 2 块(45m)**: E2 (Open Scene 对话框)
- **第 3 块(20m)**: E4 (Properties 空状态)
- **第 4 块(1h)**: E3 (Recent Scenes) 或 S1 (LightingShowcase)
- 之后视精力挑剩余 P1/P2

S3 必须在 S1+S2 至少完成一个之后再做(否则没有 scene 可加进 visual_test)。

