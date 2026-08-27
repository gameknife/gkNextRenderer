---
title: ".NET 脚本运行时架构"
category: design
status: 现行；P0–P5 全部落地
owner: engine/scripting
created: 2026-08-14
last_updated: 2026-08-15
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

**已于 P1 执行完毕**（2026-08-14）。以下是当时的判断依据，保留以解释为什么这次替换代价这么小。

QuickJS 的实际依赖面只有两个 target：`FlappyJs` 与 `gkNextEditor`。

- `FlappyJs` 是绑定演示与 parity 回归载体，被 `FlappyCSharp` 等价取代。
- `gkNextEditor` 用 `EditorScriptExecutor` 的 `EvalJavaScript` 提供 AI 生成片段的执行沙盒。该文件
  前 ~880 行是自有的 editorscript 行命令 DSL，**不依赖 QuickJS**，删除 JS 路径后 Script Console
  仍然可用（实际删除 605 行，DSL 完整保留）。

`assets/scripts/studiosim_entry.js` 是死文件——StudioSim 没有链接 `NextQuickJS`。

**已确认放弃的能力**：Editor 的 JS eval。NativeAOT 下 C# 没有等价物（无 JIT、无
`Reflection.Emit`），CoreCLR + Roslyn Scripting 虽可实现但会引入 ~15MB 依赖与秒级首次编译延迟，
且会造出一个"只有 CoreCLR 后端才有"的能力，直接违反本设计的根本约束。取舍已在 2026-08-14 拍板：
放弃 eval，Script Console 只保留 editorscript DSL。

替换后同时消失的还有：TypeScript 工具链（`tools/tsc`、tsc fetcher、TS 编译与热重载约 90 行）、
`Engine.d.ts` 的字符串拼接生成器（108 行手写字符串）、以及 vendored 的
`src/ThirdParty/quickjs-ng`（2.9MB）。退场前的绑定面誊本见
[脚本绑定面基线](script-binding-surface-baseline.md)。

顺带修掉一个现存漂移：`NextEngine::RegisterReflection()` 只注册了 `GetTotalFrames`，而 QuickJS 的
`class_<NextEngine>` 注册了 4 个方法——反射与绑定不是同一份事实。**修法是删掉重复的那一份而不是
补齐两份**，理由见 4.4 第 3 条。

## 3. 双后端：CoreCLR 与 NativeAOT

### 3.1 唯一跨界入口

native 与托管之间只有一个函数签名穿越边界，两种后端完全一致：

```cpp
// src/Modules/NextDotNet/Interop.h —— native 与 managed 共享的唯一 ABI
struct FEngineApi
{
    uint32_t version;
    void     (*UI_DrawText)(GkStr text, float x, float y, float scale, GkColor32 color);
    GkBool   (*Input_IsKeyDown)(GkStr name);
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
| 获取方式 | 链接 `.lib`/`.so`/`.a`，直接调符号 | 定位 hostfxr → `hostfxr_initialize_for_runtime_config` → `hostfxr_get_runtime_delegate(hdt_load_assembly_and_get_function_pointer)` → `load_assembly_and_get_function_pointer(..., UNMANAGEDCALLERSONLY_METHOD, ...)` |
| native 实现 | `FAotHost : IManagedHost` | `FCoreClrHost : IManagedHost` |
| C# 代码 | **完全相同** | **完全相同** |

选择由 CMake option `GK_DOTNET_BACKEND=CoreCLR|AOT` 决定，默认 `CoreCLR`；release preset 与
移动端 preset 强制 `AOT`。

**两条只有真机集成才暴露的约束**（P3 实测）：

- **交给 hostfxr 的路径必须是平台原生分隔符。** hostpolicy 把 .NET root 拼进 TPA 列表后靠字符串
  比较去重；混合分隔符会让 `System.Private.CoreLib.dll` 出现两次，`coreclr_initialize` 返回
  `E_INVALIDARG`。`FCoreClrHost` 因此对所有入参做 `make_preferred()`。
- **AOT 产物是 native DLL，必须与可执行文件同目录**，由 OS loader 解析；放进托管程序集目录会让
  进程在 main 之前静默失败。

**不用 libnethost 定位 hostfxr**（P0 实测后确定）。官方的 `get_hostfxr_path` 需要链接
`libnethost`，而它是 /MT 编译的静态库，链进 /MD 的引擎必然 `LNK2038`；改用动态 `nethost.dll`
则要多发一个文件，只为读一个我们本来就知道的路径——.NET root 要么是 `gnb dotnet setup` 装到
`external/dotnet/` 的那份，要么在平台固定位置。因此 `FCoreClrHost` 自己按
`<root>/host/fxr/<最高版本>/hostfxr` 解析，host pack 只提供 `hostfxr.h` 与
`coreclr_delegates.h` 两个头文件，不链接任何东西。`hostfxr_initialize_parameters.dotnet_root`
钉住 framework 解析，避免机器上的其他安装抢答。

### 3.3 程序集必须拆成两个（热重载的硬约束）

`[UnmanagedCallersOnly]` 方法**不能位于 collectible `AssemblyLoadContext` 中**。因此托管侧固定
拆成两个程序集：

```text
GkNext.Bootstrap.dll   默认 ALC，永不卸载，持有 [UnmanagedCallersOnly] 入口与 FEngineApi 表
GkNext.Game.dll        CoreCLR 下加载进 collectible ALC，可热重载
```

热重载时：卸载 collectible ALC → 新建 → 加载新 `GkNext.Game.dll` → 重新绑定委托。
`GkNext_Bootstrap` 的函数指针始终不变，**native 侧对热重载完全无感知**，语义与
QuickJS 的 `ResetContextAndLoadScript()` 整体重建一致。

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
GK_API(UI,    DrawText,      void,     (GkStr text, float x, float y, float scale, GkColor32 color))
GK_API(UI,    DrawRect,      void,     (float x, float y, float w, float h, GkColor32 color, float rounding))
GK_API(Input, IsKeyDown,     GkBool,   (GkStr name))
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
字符串查表 + 反射（QuickJS 的 `CreateComponentObject`）。

生成流程用 build-time codegen（引擎 `--dump-reflection` 导出 JSON → `gnb csharpgen`），不用
Source Generator：后者复杂度高、难调试，在这里没有额外收益。

`PropertyFlags::JSExposed`（[PropertyMeta.hpp](../../src/Engine/Runtime/Reflection/PropertyMeta.hpp)）
改名为 `ScriptExposed`，保留旧名作为 deprecated 别名一个版本周期。

**P5 落地结果。** 实现见 [.NET Bindings](../AGENT_GUIDE/DotNetBindings.md)。两处与上述设计不同：

- 清单**提交进仓库**（`src/Modules/NextDotNet/ReflectionManifest.json`），生成器读快照而不是每次
  跑引擎。原设计隐含"构建时导出"，但那会让 `gnb csharpgen --check` 依赖一个已构建的二进制，而
  这个检查的职责恰恰是守住构建之前的状态。代价是快照会过期，所以补了
  `Test_ReflectionManifest.cpp` 做闸门：提交的清单与实时反射不一致就测试失败。
- 节点自身的反射属性直接长在 `NodeRef` 上（`node.Translation`），不是 `node.GetComponent<Node>()`
  ——node handle 本身就是那个 node，多一层间接没有意义。component 则同时有简写（`node.Render`）
  和泛型形式（`node.GetComponent<RenderComponent>()`）。

### 4.4 跨界类型规约与四项定稿取舍

[脚本绑定面基线](script-binding-surface-baseline.md) 誊写 QuickJS 绑定面时暴露了四处不能照抄的
地方。结论如下，P2 按此实施。

**1. 颜色用打包 `GkColor32`，不用 4 个 float。**

QuickJS 时代 `UI.DrawText` 收 `r,g,b,a` 四个 0..1 float，native 侧 `clamp*255` 后进 `IM_COL32`；
`DrawRect` 因此有 11 个参数。打包成一个 `uint32_t` 后 native 侧直接透传给 ImGui，零转换，参数
从 11 降到 6。**布局钉死为 IM_COL32**（内存字节序 R,G,B,A，字面量 `0xAABBGGRR`）——不是更"自然"
的 `0xRRGGBBAA`，因为选后者只是把一次转换从 C# 挪到 native，还多一个会被写错的约定。

script 侧的 0..1 float 手感不丢：生成的 C# `Color` 结构体保留 float 分量，在调用点打包。这正是
4.1 说的"友好层"该干的事——ABI 追求窄和无歧义，好用交给生成的包装层。

**2. 跨界结构体全部去可选、去 union。**

- `ProceduralModelSpec` 是 `{type:"box"|"sphere"}` 的 tagged union。它存在的理由是 JS 没有重载，
  一个动态对象比两个绑定便宜。C# 有重载，所以**拆成两个函数**：`SceneBuild_AddBoxModel(min, max)`
  与 `SceneBuild_AddSphereModel(center, radius)`。参数全是标量与 `Vec3`，blittable 是结构上成立
  的而不是靠约束维持的；将来加第三种图元是加一行 `GK_API`，不是往 payload 里塞字段。
- `FRenderNodeSpec` 保留结构体（字段会继续长），但**去掉全部可选性**：所有字段必填、定长、
  blittable。缺省值（`name="ScriptNode"`、`translation=0`、`scale=1`、`visible=true`）由生成的
  C# 包装层以默认参数提供。可选字段是 JS 对象的产物，不该出现在线格式里。

配套的两条通用规约（已落进 [Interop.h](../../src/Modules/NextDotNet/Interop.h)）：

- **`bool` 不跨界**，一律 `GkBool`（`int32_t`，0/1）。C++ 的 `bool` 大小实现定义，C# 的 `bool`
  不是 blittable，要么加 `MarshalAs`（3.4 第 4 条禁止），要么换类型。
- 颜色用 `GkColor32`，见上。

**3. `NextEngine` / `Scene` 的 meta func 注册删除，不是补齐。**

第 2 节记录的漂移是"反射注册了 1 个方法、QuickJS 绑定注册了 4 个"。直觉解法是把反射补到 4 个，
但那会**固化两份需要手工同步的事实**——正是本次重构要消灭的东西。而且 QuickJS 退场后这些注册
已经没有任何消费者（`entt::resolve<NextEngine>()` 全仓无人调用），P5 的 `Components.g.cs` 生成器
若消费到它们，反而会同时产出 `scene.GetIndicesCount()`（反射路径）与 `Api.SceneGetIndicesCount()`
（表路径）两条等价通路，漂移原样复现。

因此划清所有权：

| 面 | 唯一事实来源 |
|---|---|
| 引擎级、场景级**函数** | `EngineApi.def.h` |
| component / node **属性** | `entt::meta` |

`NextEngine::RegisterReflection()` 与 `Scene::RegisterReflection()` 已在 P1 收尾时删除。
`Scene::FindNodeIdWithComponent` / `GetNodeById` 等仍然是脚本要用的能力，它们进 `EngineApi.def.h`
的 `Scene` 命名空间。

**4. `WriteFile` 不进 ABI。**

QuickJS 需要这个绑定是因为 JS 引擎自身没有文件 I/O；它按项目根解析相对路径、建目录、截断写入，
是脚本层唯一的任意写入口。C# 不缺这个——`File.WriteAllText` 在两种后端下都能用，再造一个引擎版
只是多一份要维护、要审计的表面。

引擎该提供的是**它才知道的信息**：`Paths_GetProjectRoot()` 与 `Paths_GetOutputDir()`。P4 的
FlappyCSharp 写 replay trace 走"引擎给路径 + BCL 写文件"，与 QuickJS 时代等价。

"沙箱"不是这里的取舍点：托管游戏代码与 C++ gameplay 代码是同一信任级别的一方代码，不是用户投喂
的脚本文本。真要做沙箱，该管的是加载什么程序集，不是拦一个 `WriteFile`。

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
  Probe/                           P0 双后端验收探针（独立 CMake，不进引擎构建）

assets/csharp/                     托管侧，对标已删除的 assets/typescript
  GkNext.Bootstrap/                默认 ALC，[UnmanagedCallersOnly] 入口 + collectible ALC 管理
  GkNext.Engine/                   契约与绑定，始终从默认 ALC 解析（跨 ALC 类型同一性的前提）
    Engine.g.cs                    ★生成物，勿手改
    Components.g.cs                ★生成物，勿手改
    NextGameInstance.cs            游戏基类
  GkNext.Game/                     可热重载的游戏程序集（AOT 下静态链接进 Bootstrap）
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
QuickJS 的 `CallBeforeSceneRebuild` 上下文进出语义相同。

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

## 8. QuickJS 退场清单（P1 已执行）

下表是 P1 执行时的核对清单，全部已删除。实际执行还补上了三处本表遗漏的引用：
`cmake/SetupExternalLibs.cmake` 的 tsc 存在性检查（`FATAL_ERROR`，不删无法 configure）、
`assets/cmake/RuntimeAssets.cmake` 的 typescript 资产目录与 tsc 拷贝、`gnb.toml` 的
`targets.all` 中的 `FlappyJs`。

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

**Flappy parity 实测结果（P4）：两种后端的 trace 逐字段完全相同，与 C++ 的最大偏差 2.4e-7。**
放宽门槛没有被用到，但保留——它买的是不必用 `Math.fround` 手段对齐浮点。

**Flappy parity 按放宽标准执行。** C# 原生有 `float`，不做 JS 时代的 `Math.fround` 对齐
（`FlappyJsGameInstance.ts` 里的 `const f32 = Math.fround`）。`score`/`deathFrame`/`frameCount`/每帧 `state` 严格相等，
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
