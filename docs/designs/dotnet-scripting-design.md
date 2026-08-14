---
title: ".NET 脚本运行时架构"
category: design
status: 提案，未实现
owner: engine/scripting
created: 2026-08-14
last_updated: 2026-08-14
plan: ../plans/dotnet-scripting-plan.md
---

# .NET 脚本运行时架构

用 C# 作为引擎唯一的脚本层，替换 QuickJS/TypeScript。核心约束是**同一份托管代码必须能在 CoreCLR
与 NativeAOT 两种后端下无差别运行**：开发期用 CoreCLR 换热重载与调试，release 与移动端用
NativeAOT 换体积与启动速度，切换只改一个 CMake option，托管代码一行不动。

本文记录边界、契约与取舍。实施顺序见 [.NET 脚本运行时开发计划](../plans/dotnet-scripting-plan.md)。

## 决策摘要

| 决策 | 结论 | 理由 |
|---|---|---|
| 脚本语言 | C#，唯一 | 静态类型、性能、可规模化；AOT 后无运行时依赖 |
| 后端 | CoreCLR + NativeAOT 双后端，同一份托管代码 | dev 要热重载，release/移动端要体积与启动 |
| 跨界 ABI | 双向函数指针表 + 单一 `GkNext_Bootstrap` 入口 | 这是两种后端能共用同一份 C# 的技术前提 |
| QuickJS | 完全删除，包括 Editor eval | 见下节 |
| 语言中立绑定层 | **不做** | 只有一个宿主时是纯负担 |
| 组件/属性访问 | 复用现有 `entt::meta` 反射 | 该层本来就语言中立 |

## 1. 目标与非目标

**目标**

- `Runtime::IScriptRuntime` 的唯一实现是 `Modules/NextDotNet`。
- 同一份 C# 源码，CoreCLR 与 NativeAOT 两种后端产出**行为一致**的运行结果。
- 绑定面的声明只有一处，C# 侧的调用封装由代码生成产出。
- 组件与节点访问继续走 `entt::meta`，与 Editor PropertyPanel 共享同一份反射事实。

**非目标（本设计不覆盖，但不能被本设计挡死）**

- 把 `src/Gameplay/` 整层迁移到 C#。这是更大的战略选择，但 `FEngineApi` 的设计不得窄化到"只够
  FlappyCSharp 用"，否则未来迁移要推倒重来。
- 脚本内的交互式 eval / REPL。NativeAOT 下无 JIT，能力不存在；不要设计成 CoreCLR 独占能力，
  那会破坏"两种后端行为一致"这条根本约束。
- Android/iOS 的托管运行时落地。AOT 是唯一可行路径，但设备验证不在首版范围。

## 2. 为什么用 C# 完全替换 QuickJS

QuickJS 的实际依赖面只有两个 target：`FlappyJs` 与 `gkNextEditor`。

- `FlappyJs` 是绑定演示与 parity 回归载体，被 `FlappyCSharp` 等价取代。
- `gkNextEditor` 用 [EditorScriptExecutor.cpp:882](../../src/Application/Editor/gkNextEditor/Automation/EditorScriptExecutor.cpp)
  的 `EvalJavaScript` 提供 AI 生成片段的执行沙盒。该文件前 ~880 行是自有的 editorscript 行命令
  DSL，**不依赖 QuickJS**，删除 JS 路径后 Script Console 仍然可用。

`assets/scripts/studiosim_entry.js` 是死文件——StudioSim 没有链接 `NextQuickJS`。

**已确认放弃的能力**：Editor 的 JS eval。NativeAOT 下 C# 没有等价物（无 JIT、无
`Reflection.Emit`），CoreCLR + Roslyn Scripting 虽可实现但会引入 ~15MB 依赖与秒级首次编译延迟，
且会造出一个"只有 CoreCLR 后端才有"的能力，直接违反本设计的根本约束。取舍已在 2026-08-14 拍板：
放弃 eval，Script Console 只保留 editorscript DSL。

替换后同时消失的还有：TypeScript 工具链（`tools/tsc`、tsc fetcher、TS 编译与热重载约 90 行）、
`Engine.d.ts` 的字符串拼接生成器（[QuickJSBindings.TsDefs.cpp:278](../../src/Modules/NextQuickJS/QuickJSBindings.TsDefs.cpp)
的 108 行手写字符串）、以及 vendored 的 `src/ThirdParty/quickjs-ng`（2.9MB）。

顺带修掉一个现存漂移：[Engine.cpp:71](../../src/Engine/Runtime/Engine.cpp) 的
`NextEngine::RegisterReflection()` 只注册了 `GetTotalFrames`，而
[QuickJSEngine.cpp:154](../../src/Modules/NextQuickJS/QuickJSEngine.cpp) 的 `class_<NextEngine>`
注册了 4 个方法——反射与绑定当前已经不是同一份事实。

## 3. 双后端：CoreCLR 与 NativeAOT

### 3.1 唯一跨界入口

native 与托管之间只有一个函数签名穿越边界，两种后端完全一致：

```cpp
// src/Modules/NextDotNet/Interop.h —— native 与 managed 共享的唯一 ABI
struct FEngineApi
{
    uint32_t version;
    void     (*UI_DrawText)(GkStr text, float x, float y, float scale, uint32_t rgba);
    bool     (*Input_IsKeyDown)(GkStr name);
    uint32_t (*Scene_AddRenderNode)(const FRenderNodeSpec* spec);
    void     (*Node_SetVec3)(uint32_t nodeId, uint32_t propId, const float* value);
    /* ... 由 EngineApi.def.h 展开 ... */
};

struct FManagedApi
{
    uint32_t version;
    void (*Tick)(double deltaSeconds);
    void (*Lifecycle)(EScriptHook hook, double deltaSeconds);
    void (*InputEvent)(const FInputEventBlob* event);
};

extern "C" int GkNext_Bootstrap(const FEngineApi* engineApi, FManagedApi* outManagedApi);
```

托管侧：

```csharp
[UnmanagedCallersOnly(EntryPoint = "GkNext_Bootstrap")]
public static unsafe int Bootstrap(FEngineApi* engineApi, FManagedApi* outManagedApi)
{
    Api.Table = engineApi;                  // 存为 static，全托管侧共用
    outManagedApi->Tick      = &Dispatch.Tick;
    outManagedApi->Lifecycle = &Dispatch.Lifecycle;
    outManagedApi->InputEvent = &Dispatch.InputEvent;
    return 0;
}
```

`[UnmanagedCallersOnly]` 在两种后端下产出的调用约定是同一个东西，这是双后端能共用同一份托管
代码的根本原因。

### 3.2 两种后端的差异只在 native 侧

差异被完全隔离在"如何拿到 `GkNext_Bootstrap` 指针"这一步：

| | NativeAOT | CoreCLR |
|---|---|---|
| 获取方式 | 链接 `.lib`/`.so`/`.a`，直接调符号 | nethost `get_hostfxr_path` → `hostfxr_initialize_for_runtime_config` → `hostfxr_get_runtime_delegate(hdt_load_assembly_and_get_function_pointer)` → `load_assembly_and_get_function_pointer(..., UNMANAGEDCALLERSONLY_METHOD, ...)` |
| native 实现 | `FAotHost : IManagedHost` | `FCoreClrHost : IManagedHost` |
| C# 代码 | **完全相同** | **完全相同** |

选择由 CMake option `GK_DOTNET_BACKEND=CoreCLR|AOT` 决定，默认 `CoreCLR`；release preset 与
移动端 preset 强制 `AOT`。

### 3.3 程序集必须拆成两个（热重载的硬约束）

`[UnmanagedCallersOnly]` 方法**不能位于 collectible `AssemblyLoadContext` 中**。因此托管侧固定
拆成两个程序集：

```text
GkNext.Bootstrap.dll   默认 ALC，永不卸载，持有 [UnmanagedCallersOnly] 入口与 FEngineApi 表
GkNext.Game.dll        CoreCLR 下加载进 collectible ALC，可热重载
```

热重载时：卸载 collectible ALC → 新建 → 加载新 `GkNext.Game.dll` → 重新绑定委托。
`GkNext_Bootstrap` 的函数指针始终不变，**native 侧对热重载完全无感知**，语义与
[QuickJSEngine.cpp:52](../../src/Modules/NextQuickJS/QuickJSEngine.cpp) `ResetContextAndLoadScript()`
的整体重建一致。

AOT 下没有 collectible ALC，`GkNext.Game` 静态链接进来直接调用。这是**托管侧唯一允许出现
`#if` 的位置**，且只在 `GkNext.Bootstrap` 的一个文件里：

```csharp
#if GK_AOT
    GkNext.Game.Entry.Initialize();
#else
    LoadGameAssemblyIntoCollectibleAlc();
#endif
```

游戏代码与框架层不得出现任何 `#if`。

### 3.4 托管代码的硬约束

以下是 AOT 兼容性的硬边界，违反会在 AOT 构建时才暴露，必须进 coding standard：

1. **零 `DllImport`**。所有 native 调用走注入的 `FEngineApi` 函数指针
   （`delegate* unmanaged<...>`）。CoreCLR 下宿主符号位于主 exe，DllImport 解析路径很脏；统一
   走指针表两种后端都干净，还省掉 marshalling stub。
2. **禁止 `Assembly.Load` / `Reflection.Emit` / `dynamic` / `Activator.CreateInstance(Type)`**。
3. **游戏入口注册必须编译期确定**。不得用 `AppDomain.GetAssemblies()` 扫描
   `IGameInstance` 实现；用 Source Generator 扫 `[GameInstance]` attribute 生成注册表。这是唯一
   使用 Source Generator 的场景（其余代码生成走 build-time 的 `gnb`，见 4.1）。
4. **跨界结构体全部 blittable**。`float`/`int`/固定长数组；字符串统一 `GkStr`
   （UTF-8 `byte*` + 长度），不用 `MarshalAs`。
5. **CI 必须两种后端都构建**。丝滑切换不是设计出来的，是持续验证出来的——只要 CI 不跑 AOT
   构建，某天有人加了行反射就会悄悄腐烂，且要到发版才发现。

### 3.5 显式承认的能力差异

| 能力 | CoreCLR | NativeAOT |
|---|---|---|
| 热重载 | 秒级（collectible ALC） | 无 |
| 启动时间 | ~150ms | ~5ms |
| 分发体积 | +70MB（self-contained） | +5MB/程序 |
| 移动端 | 不可用 | 可用（iOS 需 `--staticlib` 静态链接） |
| 调试 | mixed-mode（VS 可同时调 C++/C#） | native only |

**允许存在的差异只有迭代速度，不允许存在语义差异。** 任何"CoreCLR 下能用、AOT 下不能用"的
游戏可见能力，都是设计缺陷而不是可接受的取舍。

## 4. 绑定层

### 4.1 EngineApi 声明表

只有一个宿主语言，因此绑定直接使用真实签名的 C 函数——零装箱、零变体、零字符串查表。但仍需
避免"C++ 表 + C# 声明"两处手写漂移，用 X-macro 收敛：

```cpp
// src/Modules/NextDotNet/EngineApi.def.h —— 绑定面的唯一事实来源
GK_API(UI,    DrawText,      void,     (GkStr text, float x, float y, float scale, uint32_t rgba))
GK_API(UI,    DrawRect,      void,     (float x, float y, float w, float h, uint32_t rgba, float rounding))
GK_API(Input, IsKeyDown,     bool,     (GkStr name))
GK_API(Scene, AddRenderNode, uint32_t, (const FRenderNodeSpec* spec))
```

一份 `.def.h` 展开出三个产物：`FEngineApi` 的结构体字段、表填充代码、以及由
`gnb csharpgen` 生成的 `Engine.g.cs`（含默认参数、`string` → UTF-8 编码、`Vector3` 包装等友好层）。
**新增一个绑定 = 改一行 def + 写一个实现函数**，对比当前 QuickJS 的三步人肉同步流程
（注册、TS 定义字符串、文档）是本次重构的主要收益之一。

### 4.2 为什么不做语言中立变体层

早期方案曾设计 `FScriptValue` tagged union + `IScriptCallContext`，让动态类型的 JS 与静态类型的
C# 共用一份绑定实现。**该方案随 QuickJS 退场一并作废**：只有一个宿主时，变体层是纯粹的
运行时开销与代码负担。如果未来要接入第二种动态语言，应重新评估，不要从本设计里恢复该层。

### 4.3 反射复用：组件与节点访问

`src/Engine/Runtime/Reflection/` 本来就语言中立（`entt::meta_type` + `void*`），继续作为组件与
节点访问的唯一事实来源，与 Editor PropertyPanel 共享。C# 侧可以做得比 JS 时代好一个量级：
build-time codegen 从反射清单生成强类型 wrapper，propId 是编译期常量。

```csharp
node.GetComponent<RenderComponent>().Visible = false;
// 展开为 Api.ComponentSetBool(nodeId, typeId, 0x1A2B3C4D, false)
// 编译期类型安全 + 运行时零字符串查找
```

对比 QuickJS 时代的 `node.GetComponent("RenderComponent").Visible = false`——后者每次访问都要
字符串查表 + 反射（[QuickJSBindings.Scene.cpp:285](../../src/Modules/NextQuickJS/QuickJSBindings.Scene.cpp)
`CreateComponentObject`）。

生成流程用 build-time codegen（引擎 `--dump-reflection` 导出 JSON → `gnb csharpgen`），不用
Source Generator：后者复杂度高、难调试，在这里没有额外收益。

`PropertyFlags::JSExposed`（[PropertyMeta.hpp:13](../../src/Engine/Runtime/Reflection/PropertyMeta.hpp)）
改名为 `ScriptExposed`，保留旧名作为 deprecated 别名一个版本周期。

## 5. 目录结构与所有权

```text
src/Modules/NextDotNet/            引擎侧宿主（可选模块，非 Android/iOS 首版）
  NextDotNetModule.hpp/.cpp        Install/Get，对齐现有 NextQuickJSModule
  DotNetRuntime.cpp                实现 Runtime::IScriptRuntime
  Interop.h                        FEngineApi / FManagedApi / GkStr —— 唯一跨界 ABI
  EngineApi.def.h                  ★绑定面唯一事实来源
  EngineApi.cpp                    绑定实现 + 表填充
  Host/IManagedHost.hpp
  Host/CoreClrHost.cpp             hostfxr 装载
  Host/AotHost.cpp                 直接链接符号

assets/csharp/                     托管侧，对标已删除的 assets/typescript
  GkNext.Bootstrap/                默认 ALC，[UnmanagedCallersOnly] 入口
  GkNext.Engine/
    Engine.g.cs                    ★生成物，勿手改
    Components.g.cs                ★生成物，勿手改
    NextGameInstance.cs            游戏基类
  GkNext.SourceGen/                [GameInstance] 注册表生成器
  Flappy/FlappyCSharp/             验收载体

external/dotnet/                   gnb dotnet setup 拉取的 pinned SDK/runtime
```

**依赖方向**：`Engine` 不感知 `NextDotNet`；`NextDotNet` 依赖 `Engine`；托管侧只依赖注入的
`FEngineApi`，不依赖任何具体宿主形态。

## 6. 运行时生命周期

`DotNetRuntime` 实现 [`Runtime::IScriptRuntime`](../../src/Engine/Runtime/Interface/ScriptRuntime.hpp)
的 `Initialize` / `Tick` / `HandleEvent`，生命周期钩子集合与当前 QuickJS 一致：
`OnInit`、`OnDestroy`、`BeforeSceneRebuild`、`OnSceneLoaded`、`OnRenderUI`、`OnInputEvent`、
`OverrideRenderCamera`。

`BeforeSceneRebuild` 期间的 `SceneBuild.*` 仍然只在该钩子执行窗口内有效，与
[QuickJSEngine.cpp:356](../../src/Modules/NextQuickJS/QuickJSEngine.cpp) `CallBeforeSceneRebuild`
的上下文进出语义相同。

## 7. 工具链获取与编译

沿用既有的 external 依赖模式（[fetcher.go:190](../../tools/gnb/internal/fetcher/fetcher.go)
`ensureTSC` 与 `gnb llm setup` 下载到 `external/llm/`）：

```bash
gnb dotnet setup       # pinned SDK/runtime → external/dotnet/，gnb.toml 加 [external.dotnet]
```

**不要求用户全局安装 .NET SDK**，与项目"bundled tsc、不依赖 Node"的一贯做法一致。

编译流程与已删除的 `CompileTypeScriptSources()` 同构：时间戳扫描 + stamp 文件跳过，
`NextRenderer::OSProcess` 调用 `external/dotnet/dotnet build`。stamp 从 `.tsc.stamp` 换成
`.dotnet.stamp`。

## 8. QuickJS 退场清单

删除时逐项核对，避免留下悬空引用：

| 类别 | 路径 |
|---|---|
| 模块 | `src/Modules/NextQuickJS/` 全部（3724 行） |
| 第三方 | `src/ThirdParty/quickjs-ng/`（2.9MB）；`src/CMakeLists.txt:15,48-49` |
| CMake | `src/Modules/CMakeLists.txt:93`；`src/cmake/SourceFiles.cmake:73-75` |
| 应用 | `src/Application/Game/Flappy/FlappyJs/` 全部 |
| Editor | `EditorMain.cpp:27,47`；`ScriptConsole.cpp:6,7,30,140`；`EditorScriptExecutor` 的 `EvalJavaScript` / `RegisterEditorBindings` 与 882–1485 行；`gkNextEditor/CMakeLists.txt:18` |
| 资产 | `assets/typescript/` 全部；`assets/scripts/studiosim_entry.js`（死文件） |
| 工具链 | `tools/tsc/`；`fetcher.go` 的 `ensureTSC`/`tscMatchesHostArch`；`config.go` 的 `TSCConfig`；`main.go:312` 的 fetch 子命令帮助文本 |
| 文档 | `docs/guides/typescript-integration.md`；`docs/AGENT_GUIDE/QuickJSBindings.md`；`docs/projects/flappy-bird-parity/introduction.md` 需改写 |

`EditorScriptExecutor` 的 editorscript 行命令 DSL（前 ~880 行）**保留**，Script Console 继续可用。

## 9. 已知取舍与风险

**GC 是比后端切换更现实的风险。** 引擎有 SoftwareTracing 等帧时间敏感路径，托管层每帧分配会
造成 GC spike。规矩必须在框架层就定死：`struct` 优先、对象池、`Span<T>`、gameplay 热路径禁用
LINQ 与闭包。`NextGameInstance` 基类应内置 dev-only 的 per-frame 分配量断言，让违规在开发期
就暴露而不是等到掉帧。GC 模式在 `runtimeconfig.json` 与 AOT MSBuild 属性中保持一致配置。

**AOT 兼容性会静默腐烂。** 唯一的防线是 CI 双后端构建（3.4 第 5 条）。

**iOS 需要静态链接。** `--staticlib` 路径与动态库路径在符号可见性上有差异，首版只要求编译通过，
不做设备验证。

**首版不覆盖 Android/iOS 运行。** 有先例：QuickJS 在 Android 上本来就被排除
（`src/cmake/SourceFiles.cmake:73-75`）。

**Flappy parity 按放宽标准执行。** C# 原生有 `float`，不做 JS 时代的 `Math.fround` 对齐
（[FlappyJsGameInstance.ts:9](../../assets/typescript/flappy/FlappyJs/FlappyJsGameInstance.ts)
的 `const f32 = Math.fround`）。`score`/`deathFrame`/`frameCount`/每帧 `state` 严格相等，
`birdY`/`birdVelocityY` 容差 1e-3。这保留了"绑定层无 bug"的回归价值——调用次数、顺序与生命周期
时序都会体现在 score 与死亡帧上——同时去掉浮点纠缠。

## 10. 验证

- **绑定面**：`gnb csharpgen` 输出与 `EngineApi.def.h` 一致性单测。
- **双后端等价**：FlappyCSharp 在 `GK_DOTNET_BACKEND=CoreCLR` 与 `=AOT` 下分别跑
  `--flappy-replay`，两份 trace 必须与 FlappyCpp 同时通过放宽后的 parity 门槛。这是双后端语义
  一致性的主验收手段。
- **热重载**：CoreCLR 下修改 C# 源码，确认 collectible ALC 卸载重载后行为更新且无泄漏。
- **反射链路**：改动反射属性后，Editor PropertyPanel、undo/redo 与 C# wrapper 三条链同时验证；
  只确认 `entt::resolve<T>()` 非空不足以证明可用。
