# 托管游戏 Launcher / 进程内启动 C# 游戏

状态：**Phase 1–5 已实施**（通用宿主 + manifest、运行时装卸与世界重置、`gkNextLauncher`、表现层与
launcher 内重建、gkNextEditor 的 play-in-editor）。PIE 的边界见 §7。

`Brotato3DCSharp`、`FlappyCSharp`、`DotNetSandbox` 不再各自携带一份原生 shell；它们和
`gkNextLauncher` 共用同一个宿主，差异全部由 `assets/configs/games/*.game.json` 描述。Launcher 在
**同一个进程内**选择、加载、运行、卸载任意托管游戏；`gkNextEditor` 用同一套机制实现 Play/Stop，
并且可以在游戏运行时"弹出"（eject）回编辑器去检视和编辑它的场景——即 Unity Editor 的 Play 模型。

## 1. 边界：为什么 CoreCLR 可以而 NativeAOT 不行

1. **CoreCLR 下可行，机制早就存在。** `FManagedApi` 带 `LoadGame(path)` / `UnloadGame()` /
   `ReloadGame(path)`（`src/Modules/NextDotNet/Interop.h`），CoreCLR 侧用可回收
   `AssemblyLoadContext` 实现（`assets/csharp/GkNext.Bootstrap/GameHost.cs`）。热重载每天做的就是
   "卸载一个游戏程序集、加载另一个"；换成加载**另一个路径**的程序集是同一条代码路径。
2. **NativeAOT 下不可行，且不应该尝试。** AOT 把托管代码编成 `GkNext.Bootstrap.<target>` 静态库链进
   exe，导出符号固定为 `GkNext_Bootstrap`，两个游戏的库无法链进同一个 exe；`GameHost` 的 AOT 分支也是
   直接静态调用 `Generated.GameEntry.Create()`。因此 `gkNextLauncher` 只在
   `GK_DOTNET_ENABLED AND NOT GK_DOTNET_USE_AOT` 下构建。
   这对应 Unity 的分工：**开发用 Launcher（CoreCLR，可加载、可热重载），发布用 per-game AOT exe**。
   桌面 release 仍只发 `gkNextRenderer` / `gkNextEditor` / `gkNextMotionBenchmark`，不受影响。
3. **native 模块只能是编译期并集。** `ScadLoader`、`NextPhysics` 等是静态库，宿主必须提前链接。
   manifest 的 `requiredModules` 因此是**校验**而非动态加载：launcher 启动时把自己链接的模块登记成一个
   集合，声明了集合外模块的条目在菜单里标灰并说明原因，而不是让它加载到一半才发现没有 loader。

## 2. 组成

```
gkNextLauncher          Brotato3DCSharp / FlappyCSharp     gkNextEditor
  └─ LauncherGameInstance  └─ (直接用 ManagedGameHostInstance)  └─ EditorGameInstance
       菜单 UI + 会话控制                                            └─ Editor::FPlaySession
            │                          │                                   │ Play/Stop/Eject
            └──────────┬───────────────┘                                   │
                       ▼                                                   │
       ManagedGameHostInstance      ← Modules/NextDotNet                    │
       8 个引擎 hook 的唯一一份转发实现                                        │
                       │                                                   │
                       └───────────────────┬───────────────────────────────┘
                                           ▼
                              ManagedGameSession        ← Modules/NextDotNet
                              Load / Unload / 状态机 / 世界重置编排
                                           │
                       ┌───────────────────┴───────────────┐
                       ▼                                   ▼
                 DotNetRuntime                        NextEngine
                 LoadGameAssembly / UnloadGame /       场景重建 / physics / cvar /
                 SetInputEnabled                        showflags / 窗口标题
```

`gkNextEditor` 走的是 session 而不是 `ManagedGameHostInstance`：它已经有自己的 `EditorGameInstance`，
只把 hook 转发到 `Editor::FPlaySession`。这正是当初把 session 独立出来、而不是让它躲在宿主 GameInstance
里的原因。

共享层在 `src/Modules/NextDotNet/`：`ManagedGameManifest.{hpp,cpp}`、
`ManagedGameSession.{hpp,cpp}`、`ManagedGameHostInstance.{hpp,cpp}`；编辑器侧是
`src/Application/Editor/gkNextEditor/Core/EditorPlaySession.{hpp,cpp}`。

**放在 Modules 而不是 `Application/Common/` 是有意的**：所有宿主目标都已经链接 `NextDotNet`，不需要
额外的 CMake 改动；而 `ManagedGameSession` 不派生 `NextGameInstanceBase`，所以 `gkNextEditor` 能在自己的
`EditorGameInstance` 里直接持有一个 session，不必继承 launcher 的宿主——PIE 落地时这一条兑现了。

收敛掉的重复：三个 per-game shell 原本共 375 行，其中 8 个 hook 的转发逐字相同，只有 5 处差异，且
**全部是数据**。现在每个 per-game 目标只剩一个 15 行的 `CreateGameInstance`。这份重复本身就是 bug 的
温床：`FlappyCSharp` 漏转发了手柄输入，而 `Brotato3DCSharp` 没漏——通用宿主无条件转发，这类漏转发不
可能再发生。

## 3. manifest 契约

`assets/configs/games/<id>.game.json`，是一个托管游戏**唯一**的声明来源：per-game exe 和 launcher 读
同一份文件，所以一个游戏不会因为启动方式不同而行为不同。

```json
{
  "id": "flappy",
  "displayName": "Flappy (C#)",
  "assembly": "flappy/FlappyCSharp.dll",
  "project": "Flappy/FlappyCSharp/FlappyCSharp.csproj",
  "window":     { "title": "FlappyCSharp", "width": 1280, "height": 720, "forceSDR": true },
  "requiredModules": ["NextAudio"],
  "initialScene": "Empty.proc",
  "showFlags":  { "debugGraphicsPanel": false, "debugPhysicsOverlay": false, "overlay": false },
  "hotReload": true,
  "compileManagedSources": false
}
```

- `assembly` 相对 `<bin>/csharp`，与 `gk_dotnet_managed_game(... DIR ...)` 的发布位置一致。
  publish 子目录由它的第一段推导（`flappy/...` → `csharp/flappy`），manifest 不重复描述。
- `project` 相对 `assets/csharp`，可选，只在源码树里有意义：它让宿主能重建这个游戏（§6）。
- `initialScene` 为空表示游戏自己在 `OnInit` 里请求场景（Brotato3D 的做法）。
- `showFlags` 用 optional 语义：只覆盖 manifest 写了的项，未提及的保持引擎默认。
- 以 `_comment` 开头的键是注释，解析时忽略。

## 4. 会话生命周期

```
Idle ──Load──> Loading ──ok──> Playing ──Stop/RequestClose──> Unloading ──> Idle
                 └──fail───> Idle（世界已回到 baseline，菜单可用）
```

- **状态切换只在帧边界发生**：`RequestLoad` / `RequestUnload` 把工作排进 `AddTickedTask`，绝不在托管代码
  的调用栈里卸载它自己的 ALC。launcher 的控制 cvar 回调同样只记录请求，由 `OnTick` 消费。
- **`UnloadPending` 是泄漏信号**：一次未回收是 GC 还没跑，连续三次就是泄漏，session 拒绝再加载并要求
  重启，而不是慢慢吃满内存。`game.unloadPending` 暴露这个计数供回归脚本断言。
- **加载失败必须回到干净的 Idle**：失败路径同样走 `RestoreBaseline()`。

### 4.1 世界重置契约

托管侧的清理是可信的（收集式 ALC + `IGameModule.Shutdown()`）。native 侧由 session 显式负责，
下表是**实测确认**后的结果（验证方式见 §5）：

| 子系统 | 处理 |
|---|---|
| Scene / 节点 | 卸载后请求 `Empty.proc`，走正常场景加载路径 |
| Physics | 场景加载序列已有 `OnSceneDestroyed()` / `OnSceneStarted()`，随上一条一并生效 |
| Audio | `NextAudio::StopMusic()`——游戏代码没了就没人停它的音乐 |
| CVar | 全量快照/恢复（跳过 ReadOnly）。**记全量而不是 diff**：游戏可能改了一个本来就非默认的 cvar，"重置为默认"会恢复成错的值 |
| ShowFlags / UserSettings | 整体结构快照/恢复 |
| 窗口标题 | 快照/恢复；加载时设为 manifest 的标题 |
| GlobalTexturePool / 材质 | 随场景重建释放，无需额外处理——A→B→A 截图一致且内存不增长即为证据 |
| RenderView / Upscaler | 配置存在 `UserSettings` 里，随其恢复 |

**baseline 只捕获一次**（首次加载前）。游戏跑完并恢复后世界已经回到 baseline，再次捕获只会有把
半恢复状态记成新真相的风险。

**宿主自己的控制 cvar 必须排除在 baseline 之外**（`SetBaselineExcludedCVars`）。launcher 用
`game.select` 决定跑哪个游戏，编辑器用 `ed.play` / `ed.playEject`——这些描述的是宿主，不是游戏能扰动的
世界。把它们纳入 baseline 会在 unload 时把**上一个游戏的 id 写回去**并再次触发宿主自己的回调，于是
"停止"变成"停一帧然后自动重启"。这条不是理论风险：两个宿主最初都踩了，launcher 那次还因为脚本紧接着
就设了新的 id 而被掩盖过去，所以两条回归脚本现在都在 stop 之后多等 120 帧再断言状态没有变回去。

### 4.2 `Engine.RequestClose` 的语义

`NextGameInstanceBase::OnGameRequestedClose()` 默认返回 `false`（照旧关进程），launcher 覆写为
"结束会话、回菜单、返回 `true`"。`Engine_RequestClose` 绑定先问 GameInstance 再决定。

**托管侧 API 完全没变**：Brotato 和 Flappy 的 C# 代码一行都没改，在 per-game exe 里点退出仍然关进程，
在 launcher 里点退出回到菜单。这是判断这套分层是否正确的试金石。

刻意**不**走 `NextEngine::RequestClose()`：窗口关闭按钮和 Alt+F4 必须继续关进程。

### 4.3 窗口

`Vulkan::Window` 新增 `GetTitle()` / `SetTitle()`——标题是宿主唯一需要在创建后修改的窗口属性。
`Config().Title` 保持创建时的值。窗口**尺寸**在 launcher 下不跟随 manifest：resize + swapchain 重建
的收益有限、风险不小，留到尺寸确实成为问题再说。

顺带修正了一处窗口初始化顺序：`windowConfig.HiddenWindow` 原本在 `CreateGameInstance` **之后**才解析，
而 GameInstance 在构造函数里就要决定窗口尺寸。现在提前到创建之前，于是 `gkNextEditor` 可以在"有人看"时
按显示器比例开窗、在隐藏窗口（agent validation）时尊重命令行尺寸。这不是锦上添花：编辑器的 PIE 回归脚本
要按归一化坐标点 Outliner，而在此之前编辑器窗口大小取决于跑测试那台机器的显示器。

## 5. 验证

七条 agent 脚本，都是长期回归网：

| 脚本 | 覆盖 |
|---|---|
| `flappy-csharp-smoke` / `brotato3d-csharp-smoke` | per-game exe 行为不变（Phase 1 的验收） |
| `launcher-game-switch` | 菜单 → flappy → 菜单 → brotato3d → 菜单 → flappy，逐步断言 |
| `launcher-swap-stress` | 7 轮切换 + 键盘路径（Down/Enter/Esc），每次卸载后断言 `game.unloadPending == 0` |
| `launcher-rebuild` | 在 launcher 里点 Rebuild 重新发布 C#，然后加载运行 |
| `editor-play-in-editor` | 编辑器里 Play brotato3d → eject → 点 Outliner 选中运行中游戏的节点 → resume → Stop 回到原场景 → 再 Play/Stop 一轮 flappy |
| `launcher-new-project` / `editor-new-project` | 新建项目对话框在两个宿主里都能打开、能列出模板、Esc 能关掉（§8）。**不**脚本化"创建"这一步——它要往源码树里写文件，validation run 不该做这种事，那一半由 `Test_ManagedGameTemplate` 覆盖 |

`Test_ManagedGameTemplate`（`[Unit][DotNet][Template]`）盯的是会写磁盘的那一半：id 推导、模板可读性
（每个模板都得有 csproj 和游戏类）、校验必须在建目录**之前**拒绝坏名字，以及一次真实 scaffold 的
产物——token 全部替换掉、assembly 前缀等于 manifest id（否则 rebuild 会发布到下次加载不去找的地方）。
它把 `GK_DOTNET_MANAGED_SOURCES` 指向临时目录，所以跑测试不会在仓库里留下工程。

实测结果：

- **Flappy replay parity**：`violations: 0`，720 帧，`frame`/`score`/`state`/`deathFrame` 严格相等，
  最大 birdY 偏差 2.3e-07。这是 Phase 1 最重要的回归证据——通用宿主没有改变任何一个 hook 的时序。
- **A→B→A 截图一致**：`launcher_flappy_first` 与 `launcher_flappy_second` 在鸟的位置、管道、地形上
  一致（视差层因实际经过时间不同有微小相位差）。brotato3d 的 SCAD 城市、物理体、材质没有任何残留。
- **内存不单调增长**：7 轮切换期间 WorkingSet 采样
  `7 → 95.7 → 387.8 → 534.9 → 533.4 → 546.5 → 550.3 → 551.4 → 551.6 → 551.9 → 548.9 → 549.2 → 549.1` MB,
  首次加载后进入平台并保持。
- **单元测试无回归**：345 用例 338 通过 / 7 失败，与改动前的 baseline（`git stash` 后重建实测）逐位相同。
  失败项为 `Test_PhysicsSync`(4)、`Test_TerrainWalkable`、`Test_GeoCityWalkable`、`Test_GeoPoiSidecar`，
  均与本设计无关。

ImGui 点击的注意事项（写进脚本注释了）：`mouse-move` 与 `mouse-button` 必须分步、中间隔几帧。ImGui 用
上一帧看到的鼠标位置做 hit test，移动和按下在同一帧送达会点在空处。

## 6. Launcher 内重建 C#

菜单每个条目旁边有 Rebuild 按钮（仅当 manifest 有 `project` 时出现），执行与 CMake 同样的
`dotnet publish` 到同样的输出目录，完成后重新扫描条目。这就是这套设计存在的意义所在的循环：
**改 C# → 点一下 → 玩**，不需要 C++ 构建，不需要重启进程。若被重建的正是当前正在跑且开了热重载的游戏，
新程序集会在轮询间隔内被热重载接手。

publish 是同步的、要几秒，所以点击只记录请求，让菜单先画一帧 "rebuilding ..." 再执行。

**顺带修掉的既有 bug**：`compileManagedSources` 从来没真正生效过。它用资产路径解析 `assets/csharp`，
而资产路径指向运行时根（`out/build/<preset>/`），那里只有发布产物、没有 C# 源码，于是"源码变了就重建"
这段逻辑在正常构建树里永远静默 no-op。现在改用编译期烘焙的 `GK_DOTNET_MANAGED_SOURCE_ROOT`（可用
环境变量 `GK_DOTNET_MANAGED_SOURCES` 覆盖）；installed build 没有源码，正确地拿到空路径。

## 7. Play-in-editor

`gkNextEditor` 的工具栏有一个游戏下拉 + Play/Stop，快捷键 **F5**（Play/Stop）与 **F8**（Eject/Resume）。
实现是 `Editor::FPlaySession`：一个薄状态机，把 `ManagedGameSession` 包起来并决定输入与相机归谁。

三个状态：

| | Stopped | Playing | Ejected |
|---|---|---|---|
| 游戏 tick | 否 | 是 | **是**（世界继续活着） |
| 游戏收到输入 | 否 | 是 | 否（`DotNetRuntime::SetInputEnabled(false)`） |
| 编辑器相机 / 快捷键 | 是 | 否 | 是 |
| 渲染相机 | 编辑器 | 游戏 `OverrideCamera` | 编辑器 |
| 游戏自绘 UI | — | 画 | 不画（免得盖住要看的面板） |
| Outliner / Properties | 编辑场景 | 游戏场景 | **游戏场景，可选可改** |

Eject 是让 PIE 有用而不只是"能看"的那一半：游戏照常跑，但 WASD 回到编辑器相机，Outliner 里点一个节点
就能在 Properties 里读写它的 Transform 和组件。切断输入时会顺手清空托管侧的按键状态，否则一个在半步中
被 eject 的游戏会永远保持"正在往前走"——它再也收不到那个 key-up。

### 7.1 游戏 UI 画在哪：UI canvas

一个游戏的 HUD 是按 `UI.GetScreenSize()` 布局、用绝对坐标画到 ImGui foreground drawlist 的。游戏自己
拥有窗口时这没问题；在编辑器里就不对了——HUD 会按整个编辑器窗口布局，并从窗口左上角开始画，盖在面板上。

`FUiCanvas`（`EngineApi.hpp`）是这件事的解法：一个矩形，宿主设了它之后，

- `UI.GetScreenSize()` 返回矩形的尺寸——游戏看到的"屏幕"就是那个面板；
- `UI_DrawText` / `UI_DrawRect*` / `UI_SubmitDrawList` 的坐标平移进矩形；
- `Input.GetMousePosition()` 相对矩形返回，游戏自己的 UI 命中测试才对得上它画的位置；
- 绘制被裁剪到矩形，于是游戏的全屏遮罩不会糊住编辑器面板。

**不做缩放。** 游戏适应面板尺寸，和它适应一个被拖小的窗口是同一件事——这样文字不会因为缩放而糊，也
不必替游戏猜一个"设计分辨率"。

canvas 未设置（尺寸为 0）时每个绑定的行为与从前逐字节相同，所以 per-game exe 和 launcher 不受影响；
编辑器只在画游戏 UI 的那一刻设置它，画完立即清除，免得同一帧里别的 ImGui 使用者也被平移。

已知边界：`UI_Begin` 开出来的是真正的 ImGui 窗口，位置由 ImGui 和 dock 管理，canvas 管不到它。目前
所有 C# 游戏的 HUD 都走 drawlist 路径（`ManagedImGui` 与 Flappy/Brotato 都是），所以还没有必要处理。

**这个 PIE 是刻意窄的。** Stop **不保留** Play 之前的编辑状态：它按路径重新加载 Play 前打开的那个场景，
选择集、undo 历史、相机都从头开始，Play 期间对场景做的任何修改都会丢失。要跨 Play 会话保留编辑，需要
一套世界快照，那是比"把游戏跑起来"大一个量级的问题，而把游戏跑起来才是这里的目标。Play 也总是走游戏
自己的完整 GameInstance 流程和它自己的场景，不存在"用当前编辑器场景开始游戏"。

Stop 之后就可以改 C# 并用工具栏的 **Rebuild C#** 就地重新发布，下一次 Play 用的就是新代码——同 §6 的
循环，只是入口在编辑器里。Rebuild 按钮只在 Stopped 时可用：它会覆盖下一次 Play 要加载的程序集，而在
运行中替换程序集是热重载那条路，不是这条。

**AOT 下 PIE 报告自己不可用**而不是消失：`gk_dotnet_stub_game(gkNextEditor)` 提供 bootstrap 符号让链接
通过，`FPlaySession::IsAvailable()` 在 `DotNetRuntime::SupportsRuntimeGameSwitching()` 为假时返回 false，
工具栏把按钮置灰并说明原因。没有 .NET SDK 的构建也一样：`EditorPlaySession.cpp` 里有一份 no-op 实现，
所以 `gkNextEditor` 不需要为此写任何条件编译。

## 8. 从模板新建游戏项目

launcher 的网格末尾有一张 **New Project** 卡片（标题栏也有同名按钮），编辑器在 **File > New Game
Project...** 和 play 工具栏的游戏下拉里各有一个入口。三处打开的是同一个对话框
`Modules::NextDotNet::FNewGameProjectDialog`——scaffold 这件事在两个宿主里是同一件事，写两份的话
先漂移的一定是校验，而校验正是决定"要不要往磁盘上写目录"的那一半。

**模板是内容，不是代码。** 一个模板就是 `assets/templates/games/<id>/` 下的一个目录：

```
assets/templates/games/blank/
├── template.json          # 元数据：显示名、描述、要点、窗口、requiredModules、initialScene
└── files/                 # 原样拷贝的文件树
    ├── __ProjectName__.csproj
    ├── __ProjectName__Game.cs
    └── README.md
```

文件名和文件内容里的 `__Token__` / `{{Token}}` 会被替换：`ProjectName`、`Namespace`、
`AssemblyName`、`DisplayName`、`GameId`、`TemplateId`。引擎、launcher、编辑器里没有任何地方枚举
模板 id，所以新增一个模板只要建目录，不需要改代码、不需要重新编译。现有五个：`blank`（全生命周期
钩子 + 旋转方块）、`arcade2d`（固定步长 + 对象池 + 状态机）、`topdown3d`（WASD + 敌人池 + 跟随
相机）、`firstperson`（yaw/pitch 相机 + 程序化街区）、`tps`（ScadRig 角色 + 越肩相机 + 命中判定，
见 [ScadRig](../AGENT_GUIDE/ScadRig.md#从-c-驱动rig-绑定)）。

`tps` 是唯一一个需要宿主具备额外能力的模板：它的 manifest 要求 `ScadLoader` 和 `NextGameplay`，
launcher 和编辑器都链接了这两者并安装 `NextRig` 子系统，所以两边都能跑。别的宿主没有，会在菜单里
直接标灰说明原因——这正是 `requiredModules` 存在的意义。

**创建一个游戏 = 写一个工程 + 写一份 manifest。** 不需要 CMake target：manifest 就是声明来源，
launcher 和编辑器都从它加载。想要独立 exe 时再补 `src/Application/Game/<Name>/`（§2 的表格）。

写到哪里，为什么：

| 产物 | 位置 | 理由 |
|---|---|---|
| C# 工程 | `<源码树>/assets/csharp/<ProjectName>/` | `dotnet publish` 只能从源码树构建；`DotNetRuntime::ManagedSourceRoot()` 是唯一知道它在哪的东西 |
| manifest | `<源码树>/assets/configs/games/<id>.game.json` | 它要能被提交进版本库 |
| manifest 副本 | `<运行时根>/assets/configs/games/` | 构建树里的 assets 是**拷贝**，而运行中的宿主扫的是那份拷贝。只写源码树的话，新游戏要等下次构建才出现；只写运行时的话，下次 configure 就丢了 |

因此 installed build 里这个功能整体不可用（没有 C# 源码可写），对话框会直接说明原因而不是画一个
点不动的表单。

创建**不**顺带构建：工程写出来是否正确与本机有没有 .NET SDK 无关，为了 `dotnet` 缺失而回滚一个好
工程是错的。对话框上的勾选项调用的是 §6 那条已有的 `RebuildGame` 路径，失败时如实说"工程建好了，
但没编译"。创建与发布都是阻塞几秒的操作，所以和 §6 的 Rebuild 一样：点击只记录请求，先画一帧说明
在做什么，下一帧再真正动手。

校验在写任何文件之前完成，逐帧运行而不是等到提交时才报错。工程名当作 C# 标识符校验（它同时是目录名、
程序集名、命名空间和类名），id 必须小写，`GkNext` 前缀被拒（那是共享程序集的命名空间），已存在的目录
或 id 也被拒。中途失败会把已建的目录整个删掉——半个工程比没有工程更糟，因为它挡住重试。

生成的工程**不会**自动进 `assets/csharp/GkNextManaged.sln`：那个文件由 `gnb dotnet sln` 生成，
CI 用 `--check` 守着。对话框完成后会把这条提示写在结果面板上。

## 9. 已决定的取舍

- **Launcher 放 `Application/Game/`**，不是 `Util/`：它是玩家入口，不是工具。
- **不做"最近游玩/收藏"持久化**：没有需求支撑复杂度。
- **`GkNext.Game`（ProbeGame）登记为 `sandbox` manifest**：作为绑定层的最小可运行样本，
  既是 `DotNetSandbox` 的游戏，也是 launcher 菜单里最小的那条。
- **控制通道用 cvar 而不是新命令机制**：launcher 的 `game.select`（设为 id 即运行，设为空即回菜单）
  和 `game.newProject`，编辑器的 `ed.play` / `ed.playEject` / `ed.newProject`。它们同时是控制台
  入口和 agent 脚本入口，不需要第二套机制。代价是必须把它们排除在世界 baseline 之外，见 §4.1。
- **模板用文件树 + token 替换，不用代码生成器**：模板的价值在于它是一份**读得懂、跑得起来**的样例
  代码，而能被直接打开和修改的文件树本身就是那份样例；生成器会让"模板长什么样"这个问题只能靠运行
  它来回答。
- **新建不写 CMake target**：数据驱动的 manifest 已经足够让 launcher 和编辑器跑起来，独立 exe 是
  后来才需要的东西。让"新建项目"去改 CMakeLists 会把一个几秒的操作变成一次需要 C++ 构建的操作。

## 10. 已知缺陷（不属于本设计，但会在这里撞上）

**SCAD 场景里的节点 InstanceId 不唯一。** 在编辑器里加载 `deadly_town.scad`（直接打开或通过 PIE 跑
brotato3d 都一样）后点击 Outliner，ImGui 会报 "4 visible items with conflicting ID"：被标红的四行
`dd_ground_road` 的 `GetInstanceId()` 全是 `2`，而其他行是 1356 / 2710 / 4064 等各不相同的值。
`FScadLoader.cpp` 创建节点时混用了两个互不相干的 id 序列——主节点用 `sceneNode.instanceId`，
`__render` / `__water` 子节点用 `nodes.size()`——两者必然碰撞。

这是 pre-existing 的引擎缺陷，与 PIE 无关（只是需要点一下 Outliner 才会暴露，所以之前没人撞到）。
影响面比那个弹窗大：instanceId 是场景主键，选择集、锁定、可见性和 undo 都按它索引，所以在这类场景里
操作一个节点会静默影响另一个。修它要动 scene 的核心契约，因此单独立项，不在本次范围内。
