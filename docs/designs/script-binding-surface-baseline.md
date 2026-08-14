---
title: "脚本绑定面基线（QuickJS 誊本）"
category: design
status: 现行；P2 完成后由 EngineApi.def.h 取代
owner: engine/scripting
created: 2026-08-14
last_updated: 2026-08-14
design: dotnet-scripting-design.md
plan: ../plans/dotnet-scripting-plan.md
---

# 脚本绑定面基线（QuickJS 誊本）

这份文件是 QuickJS 退场前对**绑定面**的完整誊本，取自删除时的
`QuickJSEngine.cpp` 注册表与 `QuickJSBindings.TsDefs.cpp::BuildTypeScriptDefinitions()`。

它存在的唯一理由：[.NET 脚本运行时开发计划](../plans/dotnet-scripting-plan.md) 的 P2 要以此
为 `EngineApi.def.h` 的范围基线，而 P1 会先把源文件删掉。计划明确要求"先抄走再删"，不要等
删完再从 Git 历史里翻——历史里翻得到的是实现，翻不到的是"哪些是有意暴露的公开面、哪些只是
内部辅助"。

**P2 的两条约束**（来自设计第 1 节非目标）：

- 这份清单是**下界不是上界**。`FEngineApi` 不得窄化到只够 FlappyCSharp 用。
- 逐项翻译时要重新判断是否仍然合理，而不是照抄。标注 ⚠️ 的项目见文末"已知问题"。

## 1. 命名空间函数

### Global

| 签名 | 说明 |
|---|---|
| `spdlog(level: string, ...args: any[]): void` | 日志 |
| `GetEngine(): NextEngine` | 引擎单例 |
| `GetScene(): Scene` | 当前场景 |

### Input

| 签名 |
|---|
| `IsKeyDown(name: string): boolean` |
| `IsKeyPressed(name: string): boolean` |
| `IsMouseButtonDown(button: number): boolean` |
| `IsMouseButtonPressed(button: number): boolean` |
| `GetGamepadButton(name: string): boolean` |

`IsKeyPressed` / `IsMouseButtonPressed` 是"本帧刚按下"语义，由 `Tick` 末尾的
`ClearPressed()` 维持；C# 侧必须保留同样的清帧时序，否则输入手感会静默改变。

### Audio

| 签名 |
|---|
| `PlaySfx(path: string, volume?: number): void` |
| `PlayMusic(path: string, volume?: number): void` |
| `StopMusic(): void` |

### UI

| 签名 |
|---|
| `Begin(name: string, flags?: number): boolean` |
| `End(): void` |
| `Text(text: string): void` |
| `SetCursorPos(x: number, y: number): void` |
| `GetWindowSize(): Vec2` |
| `SetWindowFontScale(scale: number): void` |
| `GetScreenSize(): Vec2` |
| `CalcTextSize(text: string, scale?: number): Vec2` |
| `DrawText(text, x, y, scale?, r?, g?, b?, a?): void` |
| `DrawRectFilled(x, y, width, height, r, g, b, a, rounding?): void` |
| `DrawRect(x, y, width, height, r, g, b, a, rounding?, thickness?): void` |

颜色以 4 个独立 number 传递，取值区间是 **0..1**（缺省全 1.0），native 侧 `clamp(v,0,1)*255+0.5`
后进 `IM_COL32`。P2 改为打包 `GkColor32`，见文末决议 1。

### SceneBuild（仅在 `BeforeSceneRebuild` 钩子窗口内有效）

| 签名 |
|---|
| `AddProceduralModel(spec: ProceduralModelSpec): number` |
| `AddLambertianMaterial(color: Vec3): number` |
| `AddDiffuseLightMaterial(color: Vec3, intensity?: number): number` |
| `AddRenderNode(spec: RenderNodeSpec): number` |

## 2. 顶层函数

| 签名 |
|---|
| `RegisterLifecycleHooks(hooks: LifecycleHooks): void` |
| `RegisterTickCallback(callback: (deltaSeconds: number) => void): void` |
| `LoadJson(path: string): any` |
| `RequestLoadScene(filename: string): void` |
| `RequestClose(): void` |
| `GetScreenSize(): Vec2` |
| `SetOverrideCamera(camera: CameraOverride): void` |
| `IsReplayMode(): boolean` |
| `WriteFile(path: string, content: string): void` |

`RegisterLifecycleHooks` / `RegisterTickCallback` 是 JS 的动态注册模式，在 C# 里由
`IGameModule` 的虚方法取代，不需要对应的绑定项。

## 3. 类方法

### NextEngine

`GetTotalFrames()` · `GetTime()` · `GetDeltaSeconds()` · `GetSmoothDeltaSeconds()`

⚠️ 设计第 2 节记录的漂移：`NextEngine::RegisterReflection()` 当时只注册了 `GetTotalFrames`，
而 QuickJS 绑定注册了 4 个。P2 要求补齐反射注册，让两者重新成为同一份事实。

### Scene

`GetIndicesCount()` · `FindNodeIdWithComponent()`

原型上另外挂了（`BindScenePrototype`）：

`GetNodeById(nodeId): Node` · `AddLambertianMaterial(color)` · `AddDiffuseLightMaterial(color, intensity?)`
· `AddRenderNode(spec)` · `RemoveNodeById(nodeId)` · `MarkTransformDirty()`

### Node

`RecalcTransform(full?: boolean): void`，其余属性来自反射。

## 4. 反射暴露的类型

由 `QuickJSReflectionBridge::GenerateTypeScriptDef<T>()` 生成，属性集合来自 `entt::meta` 且带
`PropertyFlags::JSExposed`（设计 4.3 要求改名为 `ScriptExposed`）：

- `Assets::Node`
- `Assets::Scene`
- `Runtime::RenderComponent`
- `Runtime::PhysicsComponent`
- `Runtime::SkinnedMeshComponent`
- `Runtime::ENodeMobility`（枚举）

这一层是 P5 `Components.g.cs` 的输入，不属于 `EngineApi.def.h` 的范围。

## 5. 跨界数据结构

```ts
Vec2 { x, y }
Vec3 { x, y, z }
Vec4 { x, y, z, w }
Quat { x, y, z, w }

ProceduralModelSpec =
  | { type: "box";    min: Vec3; max: Vec3 }
  | { type: "sphere"; center?: Vec3; radius: number }

RenderNodeSpec {
  name: string; modelId: number; materialId: number;
  translation?: Vec3; scale?: Vec3; visible?: boolean;
}

CameraOverride { position: Vec3; target: Vec3; up: Vec3; fov: number }

InputEventType = "keyDown" | "keyUp" | "mouseButtonDown" | "mouseButtonUp"
               | "gamepadButtonDown" | "gamepadButtonUp"
InputEvent { type: InputEventType; key?: string; mouseButton?: number;
             gamepadButton?: string; repeated?: boolean }
```

⚠️ `ProceduralModelSpec` 是 tagged union，`RenderNodeSpec` 有可选字段——两者都不是 blittable。
设计 3.4 第 4 条要求跨界结构体全部 blittable，P2 定稿时需要显式设计（例如 tag 字段 + 固定
布局，可选字段用哨兵值或独立的 presence 位）。

## 6. 生命周期钩子

`onInit` · `onDestroy` · `onBeforeSceneRebuild` · `onSceneLoaded` · `onRenderUI` · `onInputEvent`

加上 `SetOverrideCamera` 支撑的 `OverrideRenderCamera`，即设计第 6 节列出的 7 个钩子，已在
[Interop.h](../../src/Modules/NextDotNet/Interop.h) 的 `EScriptHook` 中一一对应。

`onRenderUI` / `onInputEvent` 的返回值有语义：返回 `true` 表示脚本消费了该事件/绘制，宿主不再
继续默认处理。C# 侧的 `IGameModule` 必须保留这个返回通道。

## 已决议（2026-08-14，P2 按此实施）

誊写时标出的 4 处问题都已定稿，理由见
[.NET 脚本运行时架构](dotnet-scripting-design.md) 4.4。这里只记结论：

1. **颜色**：ABI 用打包 `GkColor32`（IM_COL32 布局），C# 侧 `Color` 结构体保留 0..1 float 手感
   并在调用点打包。UI 绘制函数的参数个数因此从 11 个降到 6 个。
2. **`ProceduralModelSpec`**：tagged union 取消，拆成 `SceneBuild_AddBoxModel(min, max)` 与
   `SceneBuild_AddSphereModel(center, radius)` 两个函数。**`FRenderNodeSpec`** 保留结构体但
   去掉可选性：全部字段必填且 blittable，缺省值由生成的 C# 包装层提供。
3. **`NextEngine` / `Scene` 的 meta func 注册已删除**（本次改动）。函数面归 `EngineApi.def.h`，
   反射只负责 component/node 属性。漂移的解法是去掉重复的那一份，不是把两份都补齐。
4. **`WriteFile` 不进 ABI**。C# 有 BCL，`File.WriteAllText` 直接可用；引擎只需提供路径解析
   （`Paths_GetProjectRoot` / `Paths_GetOutputDir`），这是宿主才知道的信息。
