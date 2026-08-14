---
title: ".NET 脚本运行时开发计划"
category: plan
status: 实施中（P0–P4 已完成，下一步 P5）
owner: engine/scripting
created: 2026-08-14
last_updated: 2026-08-14
design: ../designs/dotnet-scripting-design.md
---

# .NET 脚本运行时开发计划

按 [.NET 脚本运行时架构](../designs/dotnet-scripting-design.md) 落地 `NextDotNet`，以
`FlappyCSharp` 在 CoreCLR 与 NativeAOT 双后端下同时通过 parity 为主验收。

工期为单人全职粗估，总计 **17–19 天**。

## 1. 阶段依赖

```mermaid
flowchart LR
    P0["P0 双后端钉子"] --> P1["P1 QuickJS 退场"]
    P1 --> P2["P2 EngineApi 表 + codegen"]
    P1 --> P3["P3 NextDotNet 双后端宿主"]
    P2 --> P3
    P3 --> P4["P4 托管框架层 + FlappyCSharp"]
    P4 --> P5["P5 反射 wrapper 与文档收尾"]
```

P2 与 P3 的 native 骨架可并行，但 P3 的表填充依赖 P2 的 `.def.h` 定稿。

## 1.1 两条排序原则（先读这里）

**为什么退场排在 P1 而不是最后。** QuickJS 退场不依赖 .NET 的任何进展，是完全自包含的清理
任务；parity 验收的 golden reference 是 `FlappyCpp` 而非 `FlappyJs`，删除后 P4 验收不受影响。
提前退场让 P2 之后的全部开发在干净地基上进行，且文档一次改到位而不是先写共存版再改。

**为什么仍然保留 P0 在退场之前。** P0 只有 2 天，买的是一个确定性：万一双后端路线不成立，
QuickJS 还在，损失 2 天而不是一次没有退路的迁移。这是全程唯一的回退窗口。

**双后端不会拖慢日常开发。** 两种后端的差异完全隔离在 native 侧的两个 `IManagedHost` 实现
（`FCoreClrHost` ~200 行 / `FAotHost` ~60 行），托管代码零差异，唯一的 `#if` 只在
`GkNext.Bootstrap` 一个文件里。P2/P4/P5 写的代码不感知后端。**日常开发只面对 CoreCLR**，
AOT 在 P0 立起来后由 CI 守着，只有 P4 主验收会显式双跑一次。

反过来，AOT 不能后置：托管侧的 5 条硬约束（设计 3.4）如果没有 AOT 构建在跑就没有任何强制
机制，等到后期再补，要回头改的是已验收的框架层、Source Generator 与组件 wrapper，且会波及
托管侧公开 API 形态。AOT 兼容性是贯穿写法的约束，不是可以事后加开关的特性。

## 2. Phase 0 — 双后端钉子（2 天）

**这一阶段不通过就必须重新选型，不要往下走。**

最小 C++ 程序 + 最小 C# 类库，一次验证两条路：

1. `GkNext_Bootstrap` 双向调用跑通（native → managed 的 `Tick`，managed → native 的一个绑定）。
2. **同一份 C# 源码**分别用 CoreCLR（hostfxr）与 NativeAOT 跑通，托管代码零改动。
3. CoreCLR 下 collectible ALC 卸载重载，确认 `[UnmanagedCallersOnly]` 入口位于默认 ALC 时
   热重载可行（设计 3.3 的两程序集拆分）。
4. iOS `--staticlib` 至少验证能编译（不要求上设备）。
5. `gnb dotnet setup` 拉 pinned SDK 到 `external/dotnet/`，`gnb.toml` 加 `[external.dotnet]`。

**验收**：一个脚本能在两种后端下打印相同结果；CoreCLR 下改一行 C# 后热重载生效。

### 2.1 P0 结果（2026-08-14，已通过）

一条命令复现全部验收：

```bash
gnb dotnet probe
```

它发布托管程序集、编译两个后端的原生探针、各跑一次并 diff 两份 transcript。产出：

| 验收项 | 结果 |
|---|---|
| `GkNext_Bootstrap` 双向调用 | 通过。native→managed 的 `Tick`/`Lifecycle`/`LoadGame`，managed→native 的 `Log_Info`/`Engine_GetTotalFrames`/`Engine_GetTime` 全部跑通，`GkStr` 按值跨界正确 |
| 同一份 C# 跑两种后端 | 通过。7 行 CORE transcript 逐字节相同，托管代码零改动 |
| collectible ALC 热重载 | 通过。加载 variant A → reload 到 variant B，输出变化证明换的是真代码；旧 ALC 在 GC 后被回收（否则报 `StatusUnloadPending`） |
| iOS `--staticlib` | **部分**。`NativeLib=Static` 发布路径在 win-x64 上验证可产出 `.lib`；`-r ios-arm64` 需要 macOS + iOS workload，本机无法验证，另立任务 |
| `gnb dotnet setup` | 通过。`[external.dotnet]` 已加入 gnb.toml，pinned SDK 10.0.300，5 个平台的下载 URL 均实测可达 |

实测数据：NativeAOT 产物 833 KB（设计 3.5 预算为 +5MB/程序）。

与设计的两处偏差，均已在设计文档中记录：

1. **CoreCLR 不用 libnethost**。`libnethost.lib` 是 /MT 编译的，链进 /MD 的引擎会 `LNK2038`；动态 `nethost.dll` 则要多发一个文件只为读一个我们已经知道的路径。改为直接在 .NET root 下找 `host/fxr/<最高版本>/hostfxr`，仅使用 host pack 的两个头文件，不链接任何东西。
2. **`gnb dotnet setup` 接受已安装的 SDK**。pinned version 作为下限而非精确匹配：已装 SDK 达标就直接用，只有找不到才下载 300MB 到 `external/dotnet/`。`--force` 可强制安装 pinned 版本。

一处对 `assets/csharp/` 之外的改动：`platform.EnsureMSVCEnvironment()` 现在会把 VS Installer 目录加进 PATH——NativeAOT 的 ILC 会裸调 `vswhere` 找 link.exe，找不到时会把 shell 的报错文本当成链接器路径传下去。

## 3. Phase 1 — QuickJS 退场（2 天）

P0 通过后立即执行，让后续开发在干净地基上进行。按设计第 8 节的退场清单逐项删除并核对悬空
引用。要点：

- `EditorScriptExecutor` 只删 `EvalJavaScript` / `RegisterEditorBindings` 与 882–1485 行，
  **保留前 ~880 行的 editorscript 行命令 DSL**，Script Console 继续可用。
- `gnb fetch` 的 tsc 分支与 `TSCConfig` 一并删除，注意 `main.go:312` 的帮助文本。
- **先抄走再删**：`EngineApi.def.h` 的绑定面范围以当前 QuickJS 注册表为基线（P2 需要），
  删除前先把 `QuickJSEngine.cpp:94-161` 的注册清单和 `BuildTypeScriptDefinitions()` 的签名
  誊出来，不要等删完再从 Git 历史里翻。
- 文档：删 `docs/guides/typescript-integration.md` 与 `docs/AGENT_GUIDE/QuickJSBindings.md`；
  更新 `AGENTS.md` 的技术栈、Modules 列表与 Subprojects 段落（如实写成"脚本层迁移中"）；
  `docs/projects/flappy-bird-parity/introduction.md` 暂标为"仅 C++ 基线，C# 对照待 P4"。
  C# 侧的新文档等 P4/P5 有实物后再补，不要提前写不存在的东西。

**本阶段结束后到 P4 之间引擎没有脚本能力**，真空期约 9–12 天。实际影响为零：脚本层当前的
唯一用户是 `FlappyJs`（演示）与 Editor eval（已决定放弃）。

**验收**：`gnb build --all --reconfigure` 全绿；`gkNextEditor` 启动后 Script Console 的
editorscript 命令仍可用；全仓 `rg -i quickjs` 只剩 Git 历史。

### 3.1 P1 结果（2026-08-14，已完成）

- 绑定面誊本落在 [脚本绑定面基线](../designs/script-binding-surface-baseline.md)，是 P2 的
  `EngineApi.def.h` 范围基线。誊写时发现 4 处需要 P2 判断而非照抄的问题（颜色参数形式、
  `RenderNodeSpec`/`ProceduralModelSpec` 不是 blittable、`NextEngine` 反射与绑定不一致、
  `WriteFile` 无路径约束），已在该文末尾列出。
- 删除范围与设计第 8 节清单一致，另外补上了清单未列出的三处：
  `cmake/SetupExternalLibs.cmake` 的 tsc 存在性检查（`FATAL_ERROR`，不删则无法 configure）、
  `assets/cmake/RuntimeAssets.cmake` 的 typescript 资产目录与 tsc 拷贝、以及
  `gnb.toml` 的 `targets.all` 中的 `FlappyJs`。
- `EditorScriptExecutor` 删掉 605 行（`EvalJavaScript` + `Editor.*` JS 绑定），保留 880 行的
  editorscript DSL。同时删掉只为 JS 回调存在的 `activeInstance_` 静态，`Log()` 收回 private。
- `tools/flappy/diff_traces.py` 的比较逻辑未动，只把指向已删除 target 的文案改成指向 P4。

**验收结果**：`gnb build --all --reconfigure` 65 个目标全绿；`gkNextUnitTests` 314 test cases /
59357 assertions 全通过；`gnb validate --script ui-foundation-editor-normal` 通过（Editor 启动、
渲染、截图、退出均正常）。

## 4. Phase 2 — EngineApi 表 + codegen（3 天）

**前置已完成**：P1 誊本标出的 4 处"不能照抄"已定稿，见
[设计 4.4](../designs/dotnet-scripting-design.md)——颜色打包 `GkColor32`、`ProceduralModelSpec`
拆成两个函数且 `FRenderNodeSpec` 去可选、`NextEngine`/`Scene` 的 meta func 注册删除（已实施）、
`WriteFile` 不进 ABI 改为 `Paths_*`。`GkBool` / `GkColor32` 已落进
[Interop.h](../../src/Modules/NextDotNet/Interop.h)。

- `EngineApi.def.h` 收纳全部绑定面。范围以 P1 誊出的 QuickJS 注册清单为基线（Global/Input/
  UI/Audio/SceneBuild 与顶层函数，约 40 项），**但不得窄化到只够 Flappy 用**（设计 1 的非目标）。
- `GkStr`、`FRenderNodeSpec` 等跨界结构体定稿，全部 blittable。
- `gnb csharpgen` 子命令：读 `.def.h` 生成 `Engine.g.cs`，含默认参数、UTF-8 编码、`Vector3` 包装。
- ~~补全 `NextEngine` / `Assets::Scene` 的 `entt::meta` 注册~~ → 改为**删除**这两处 func 注册
  （已于解决 4 项定稿问题时实施）。函数面归 `EngineApi.def.h`，反射只管 component/node 属性；
  理由见设计 4.4 第 3 条。P2 需确认 `Scene` 的 `FindNodeIdWithComponent` / `GetNodeById` 等
  能力在 def 表中有等价项。

**验收**：`gkNextUnitTests` 新增一致性单测（def 项数 = 表字段数 = 生成的 C# 方法数）；
生成物无手工修改痕迹。

### 4.1 P2 结果（2026-08-15，已完成）

绑定面 **43 项**，覆盖誊本的全部命名空间（Log/Engine/Input/Audio/UI/Scene/SceneBuild/Paths/Assets）。
与誊本的四处差异都是 4.4 的定稿决议，不是缩水：颜色打包、图元拆函数、`WriteFile` 换成
`Paths_*` + `Assets_ReadFile`、相机改为 `FManagedApi::OverrideCamera` 拉取（因此 `Camera_*` 两项
绑定被删除，43 而非 45）。

一致性由三道闸门守：

- `Test_DotNetEngineApi.cpp`：表字段数 == `GK_ENGINE_API_COUNT`；**每一项都非空**（表填充与结构体
  由同一份 def 展开，声明了却没实现是编译错误）；另测 SceneBuild 窗口外拒绝执行、字符串探测长度
  约定、输入"任意键"语义。
- `gnb csharpgen --check`：`Engine.g.cs` 与 def 不同步即失败。
- `csharpgen` 的 Go 单测：拒绝 `bool` 跨界、拒绝未知类型、拒绝非 out 的可写指针、拒绝非末尾默认值。

最后一条在写 def 时立刻发挥了作用——`UI_DrawText` 原本把 `scale` 的默认值放在 `color` 之前，
生成器直接报错，参数顺序因此在定稿前就修正了。

## 5. Phase 3 — NextDotNet 双后端宿主（4–5 天）

- `IManagedHost` 接口 + `FCoreClrHost`（hostfxr）与 `FAotHost`（直接链接）两个实现。
- `DotNetRuntime` 实现 [`Runtime::IScriptRuntime`](../../src/Engine/Runtime/Interface/ScriptRuntime.hpp)，
  生命周期钩子集合沿用 P1 誊出的 QuickJS 钩子集。
- 两程序集拆分：`GkNext.Bootstrap`（默认 ALC）+ `GkNext.Game`（collectible ALC）。
- `dotnet build` + `.dotnet.stamp` 时间戳跳过，与已删除的 `CompileTypeScriptSources()` 同构。
- CMake option `GK_DOTNET_BACKEND=CoreCLR|AOT`，默认 CoreCLR；release/移动端 preset 强制 AOT。
- `NextDotNet` 加入 `src/cmake/SourceFiles.cmake` 的 `GK_MODULE_NAMES`，Android/iOS 首版排除。
- **CI 双后端构建接进 gnb**（设计 3.4 第 5 条）。这一项不能推迟到后面阶段。

**验收**：空脚本工程在两种后端下都能启动到 `committed scene [...]`；CI 两条构建线都绿。

### 5.1 P3 结果（2026-08-15，已完成）

`DotNetSandbox` 是"空脚本工程"：约 90 行 host，装 `NextDotNet`、加载 `Empty.proc`、转发全部钩子。
两种后端下都跑到 `committed scene [Empty.proc]`，C# 侧 `OnInit` / `Tick` / `OnRenderUI` /
`BeforeSceneRebuild` / `OnSceneLoaded` 全部命中，且 AOT 正确报告 hot reload 不可用。

CI 闸门是 `gnb dotnet ci`：生成物检查 → 双后端探针 → 引擎在两种后端下各构建一次 → 恢复 CoreCLR。

两个只有真机集成才会暴露的问题：

1. **CoreCLR 因混合路径分隔符拒绝启动**。`GK_DOTNET_ROOT` 经 CMake `TO_CMAKE_PATH` 变成正斜杠，
   hostpolicy 把它拼进 TPA 列表后用字符串比较去重，`System.Private.CoreLib.dll` 因两种写法各出现
   一次，`coreclr_initialize` 直接 `E_INVALIDARG`。`FCoreClrHost` 现在对交给 hostfxr 的所有路径做
   `make_preferred()`。诊断靠 `COREHOST_TRACE=1`——错误码本身（0x80008089）不指向根因。
2. **AOT 产物落位**。NativeAOT 产出的是 native DLL，必须与 exe 同目录由 OS loader 找到，
   放进 `bin/csharp/` 会让进程静默启动失败。AOT 分支现在直接发布到 `bin/`。

另外 `IManagedHost` 新增 `LoadsGameFromDisk()`：AOT 下日志不再谎称从某个路径加载了程序集。

## 6. Phase 4 — 托管框架层 + FlappyCSharp（4 天）

- `NextGameInstance` 基类（对标已删除的 `NextGameInstanceBase.ts`）+ `[GameInstance]`
  Source Generator 注册表。
- 基类内置 dev-only 的 per-frame 分配量断言（设计 9 的 GC 风险对策）。
- `FlappyCSharp`：C++ 侧约 30 行，照 `FlappyJsGameInstance.cpp` 的结构；C# 侧翻译
  Bird / Pipes / Rng / GameInstance；复用同一份 `assets/configs/flappy/*.json`。
- `tools/flappy/diff_traces.py` 扩展为 cpp/cs 双向对比，并按设计 9 放宽门槛：
  `score`/`deathFrame`/`frameCount`/每帧 `state` 严格相等，`birdY`/`birdVelocityY` 容差 1e-3。

**验收（主验收）**：

```bash
gnb run FlappyCpp --flappy-replay
gnb run FlappyCSharp --flappy-replay          # GK_DOTNET_BACKEND=CoreCLR
gnb run FlappyCSharp --flappy-replay          # GK_DOTNET_BACKEND=AOT
python3 tools/flappy/diff_traces.py
```

三份 trace 必须同时通过门槛。**两种后端结果不一致即视为阶段失败**，这是双后端语义一致性的
唯一硬证据。

### 6.1 P4 结果（2026-08-15，已完成）

主验收通过，而且比门槛要求的更强：

| 对比 | 结果 |
|---|---|
| FlappyCSharp(CoreCLR) vs FlappyCpp | PASS，0 violations |
| FlappyCSharp(AOT) vs FlappyCpp | PASS，0 violations |
| CoreCLR trace vs AOT trace | **720/720 帧逐字段完全相同** |

`deathFrame` 两侧都是 126，`frameCount` 都是 720，最大 `birdY` 偏差 2.35e-7、最大速度偏差
2.37e-7——比 1e-3 的放宽门槛小四个数量级。放宽标准实际上没有被用到，但仍然保留：它买的是
不必用 `Math.fround` 那种手段去对齐浮点。

比较器唯一需要的调整是把 `fixedDeltaSeconds` 按 float32 比较——nlohmann 提升到 double 打印
`0.01666666753590107`，`Utf8JsonWriter` 打印 float 的最短往返 `0.016666668`，两者是同一个
float32，差异只在写出的位数。

框架层：`NextGameInstance` 把 ABI 的扁平钩子翻译成具名虚方法，游戏代码见不到 hook id；
`GkNext.SourceGen` 从 `[GameInstance]` 生成 `GkNext.Generated.GameEntry`，缺少入口/有两个入口/
入口不实现 `IGameModule`/无无参构造都是编译期诊断（GKNEXT002–004）。分配量断言由
`GK_DOTNET_ALLOC_GUARD=1` 打开，超预算只报一次。

四个只有真正接上两个 app 才会暴露的构建问题（都已修复，理由写在对应代码注释里）：

1. **`-p:AssemblyName=` 是全局属性**，会传播到被引用项目并让 restore 报 ambiguous project name。
   改用只被 bootstrap 读取的 `GkNativeName`。
2. **并发 `dotnet publish` 互相踩踏**。多个 app 发布同一个 bootstrap 项目，共享 `obj/` 与
   `GkNext.SourceGen.dll`，症状是文件被占用或 ILC 直接崩。改为用 CMake 依赖链把全部托管发布
   串行化——重定向 `BaseIntermediateOutputPath` 反而会让默认 `obj/` 脱离默认排除、产生重复
   程序集特性。
3. **AOT 发布把托管 PDB 泄进 `bin/`**。bootstrap 引用游戏项目，publish 会把
   `FlappyCSharp.pdb`（portable PDB）拷到 `bin/`，与原生 `FlappyCSharp.exe` 要写的 PDB 撞名，
   链接器报 LNK1207。改为发布到暂存目录、只拷 `.dll`/`.lib`。
4. **链接 NextDotNet 但不托管 C# 的目标**（`gkNextUnitTests`）在 AOT 下缺 `GkNext_Bootstrap`
   符号。新增 `gk_dotnet_stub_game()` 提供一个明确返回失败的桩。

三个只有**交互运行**才会暴露的问题（replay 只比对模拟状态，全部躲过了 trace 验收）：

5. **节点 id 0 被当成"无节点"哨兵**。`GenerateBuildInstanceId` 在节点表为空时返回 0，而 0 是
   合法节点，于是场景里第一个建出来的节点对持有它的代码永远不可见。ABI 新增
   `GK_INVALID_NODE_ID`（0xFFFFFFFF），所有产出节点 id 的绑定改用它表示失败。
6. **在 `BeforeSceneRebuild` 里调用了 live `Scene.*`**。那一刻节点只存在于正在构建的向量里，
   按 id 找不到。节点的初始变换必须写进 `RenderNodeSpec`。为了让这类误用不再静默，
   `Scene.SetNode*` 现在会对未知 node id 每个 id 警告一次——这条日志正是定位问题的关键。
7. **忘了 `Scene.MarkTransformDirty()`**。移动节点只更新场景图，不会让渲染器重传 instance
   transform，画面上一切静止。FlappyCpp 与旧 TS 版本都每帧调一次。

**视觉还原**：最初只移植了 parity 需要的部分（小鸟 + 管道），理由是"其余不影响 trace"。这对
trace 成立，但 `FlappyCSharp` 的定位是 `FlappyCpp` 的 C# 对照实现，并排看却是两个游戏。已补齐
到逐项对应：球形小鸟、7 种材质、背景板、地面/天花板、山/植被/云三层视差、以及 HUD 的记分板与
Ready/GameOver 面板。视差在 `FixedStep` 内推进（与 C++ 同位置），不含 RNG 与碰撞，parity 重跑
仍是 0 violations。

**仍存在的唯一视觉差异：光照。** `FlappyCpp` 在 `OnSceneLoaded` 里通过
`scene.GetEnvSettings()` 设置 `HasSky` / `SkyIdx` / `SkyIntensity` / `HasSun` / `SunIntensity` /
`SunElevation`；C# 侧没有对应能力，因为那是 component 属性访问，属于 **P5**。旧 TS 版本走的是
`EnvironmentComponent` 反射代理，正是 P5 要生成的那条链路。**不为它单开绑定**——那会造出 P5
生成的 wrapper 之后要重复的第二条通路（设计 4.4 决议 3 反对的正是这个）。

## 7. Phase 5 — 反射组件 wrapper 与文档收尾（2–3 天）

- 引擎新增 `--dump-reflection` 导出反射清单 JSON。
- `gnb csharpgen` 消费清单生成 `Components.g.cs`，propId 为编译期常量。
- `PropertyFlags::JSExposed` → `ScriptExposed`，保留 deprecated 别名一个版本周期。
- 文档收尾（P1 刻意推迟到此处的部分）：新增 `docs/AGENT_GUIDE/DotNetBindings.md`
  （"加一个绑定"从三步降为一步）；`docs/projects/flappy-bird-parity/introduction.md`
  改写为 C++/C# parity；`AGENTS.md` 从"脚本层迁移中"改为 C# 现状；更新 `docs/README.md`
  索引并把本 plan 移入已完成段。

**验收**：`node.GetComponent<RenderComponent>().Visible = false` 可用；改动反射属性后
Editor PropertyPanel、undo/redo 与 C# wrapper 三条链同时验证通过。

## 8. 风险与回退

| 风险 | 触发点 | 对策 |
|---|---|---|
| 双后端路线不成立 | P0 | **唯一的回退窗口**：QuickJS 尚未删除，止损为 2 天 |
| CoreCLR/AOT 行为不一致 | P4 验收 | 双后端 parity 是硬门槛；不一致则停在 P4 排查，不进 P5 |
| AOT 兼容性静默腐烂 | 任意阶段之后 | P3 就接入 CI 双后端构建，不推迟 |
| GC spike 影响帧时间 | P4 之后 | 框架层分配断言 + 热路径规矩；必要时收紧到对象池强制 |
| `[UnmanagedCallersOnly]` 与 collectible ALC 冲突 | P0 | 已知约束，两程序集拆分是既定方案；P0 必须实证 |
| 绑定面在退场时丢失 | P1 | P1 要求先誊出 QuickJS 注册清单再删，不依赖事后翻 Git 历史 |
| iOS 静态链接符号问题 | P0 / 未来 | 首版只要求编译通过，设备验证另立任务 |

**回退边界**：**P0 是全程唯一的回退窗口**。P1 执行后 QuickJS 即不可逆地退场（仅存于 Git
历史），引擎在 P4 之前没有脚本能力。因此 P1 的前置条件是 P0 的四项验证全部实证通过——
尤其是"同一份 C# 在两种后端下跑通"与"collectible ALC 热重载可行"这两项，它们决定了整条
路线是否成立。P1 之后各阶段失败只影响进度，不影响引擎其余功能。
