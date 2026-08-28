# .NET Bindings

C# 侧能调用的引擎能力有两个来源，两者都不手写生成代码：

| 想暴露的东西 | 来源 | 加东西的动作 |
| --- | --- | --- |
| **函数**（引擎服务：日志、输入、UI、场景操作、资源 I/O） | `src/Modules/NextDotNet/EngineApi.def.h` | 加一行 + 写一个实现函数 |
| **属性**（component / node 的反射字段） | `entt::meta` 注册（`REFLECT_COMPONENT`） | 什么都不用加，刷新清单后重新生成 |

这条分界是设计 4.4 的决议：反射拥有*属性*，绑定表拥有*函数*。两边都注册同一个东西，正是上一代
QuickJS 绑定和反射层漂移的原因。

## 加一个绑定函数

**一步。** 在 `EngineApi.def.h` 里加一行，然后在 `EngineApi.cpp` 里写同名实现：

```cpp
// EngineApi.def.h
GK_API(Audio, PlaySfx, void, (GkStr path, float volume))   //= volume:1.0f
```

```cpp
// EngineApi.cpp（匿名 namespace 内）
void Audio_PlaySfx(GkStr path, float volume) { /* ... */ }
```

然后 `gnb csharpgen`。托管侧 `Audio.PlaySfx("x.wav")` 立即可用。

**不需要**改 `Interop.h`、`Engine.g.cs`、CMake 或任何注册表。`FEngineApi` 结构体字段、表填充、
生成的 C# 包装全部由同一个 def 文件展开——声明了却没实现是**编译错误**，不是运行时空指针。

### 跨界类型规则

违反这些规则不会立刻报错，而是在 NativeAOT 下静默出问题：

- **不要 `bool`**，用 `GkBool`（int32，0/1）。解析器会直接拒绝。
- 字符串**入参**用 `GkStr`（调用期间由调用方持有的 UTF-8 区间）。
- 字符串**出参**用 `(char* buffer, int32_t capacity)`，返回"需要/已写入"的长度，传 `nullptr`
  探测所需大小。生成器把这一对折叠成 C# 的 `string` 返回值。
- 结构体按指针跨界，定长、无可选字段、**不含字符串**——结构体里的字符串需要调用方看不见的 arena
  生命周期管理，所以名字一律作为独立的 `GkStr` 参数传。
- 函数确实需要领域 enum 时，在 `Interop.h` 定义显式底层类型为 `int32_t` 的 ABI enum，并在 C# 侧
  镜像为 `enum : int`；native 实现必须逐值校验 / 转换，不要把引擎内部 enum 直接穿过边界。
  这不等于支持反射属性的通用 Enum codegen，后者仍需要清单携带 enumerator。
- 颜色用 `GkColor32`（IM_COL32 布局），C# 侧是 `Color`，在调用点打包。
- 非 const 指针必须是名为 `out*` 的出参；生成器会把单个出参变成 C# 的返回值。
- 行尾 `//= name:value` 声明生成的 C# 默认实参，只能是编译期常量，且必须是尾部参数。

### 失败语义

跨这条 ABI 没有异常。约定是**记一次日志，返回无害值**：node id 返回 `GK_INVALID_NODE_ID`，
physics handle 返回 `PhysicsBodyIds.Invalid`，带 `Valid` 字段的状态返回无效值，普通数值返回 0，
指针参数为空就直接 return。静默无操作是脚本“看起来对但屏幕上什么都不动”的主要来源，
所以未知 node id 会 warn 一次（`FindNodeOrWarn`）。

### 绑定背后需要一个子系统时

有些能力不能直接在 `EngineApi.cpp` 里实现：`NextDotNet` 不允许依赖 `NextGameplay` 或某个内容模块。
`Rig.*`（ScadRig 角色）是现成的样板，和 `NextPhysics` 走的是同一条路：

1. 在 `src/Engine/Runtime/Subsystems/` 声明一个纯虚接口（`NextRig.hpp`），只用引擎类型和不透明句柄；
2. 引擎持有它（`SetRigFactory` / `GetRig()`），并负责 tick 与场景生命周期；
3. 真正的实现放在有资格依赖领域库的地方（`src/Gameplay/Rig/RigSubsystem.cpp`），由应用一行
   `NextGameplay::Rig::Install(engine)` 装上；
4. 绑定函数只做 `engine->GetRig()`，拿不到就记一次日志返回无害值，并提供一个
   `Rig.IsAvailable()` 让脚本能提前问。

判据是"这个能力属于引擎的哪一层"，不是"实现起来方便不方便"：绑定层放不下的东西，说明它需要一个
子系统，而不是一条 `#include`。宿主没装时，游戏应在 manifest 的 `requiredModules` 里声明依赖，
菜单会直接标灰而不是加载到一半才失败。

## 加一个属性

不用碰 `EngineApi.def.h`。属性走反射：

1. 在 component 的 `RegisterReflection()` 里加 `.data<&Set, &Get>("Name").custom<PropertyMeta>(...)`
2. `gnb csharpgen --refresh`（跑 `gkNextRenderer --dump-reflection` 刷新清单，然后重新生成）
3. C# 侧 `node.Render.Visible = false` 之类立即可用

生成的包装在 `assets/csharp/GkNext.Engine/Components.g.cs`：每个 component 一个 `readonly struct`，
只装一个 node id，属性 id 是**编译期常量**——每帧写属性不应该做字符串比较。

```csharp
NodeRef node = NodeRef.WithComponent<EnvironmentComponent>();
node.Environment.SunIntensity = 600.0f;          // 简写
node.GetComponent<RenderComponent>().Visible = false;  // 泛型形式
node.Translation = new Vector3(0, 1, 0);          // node 自身的反射属性
```

`node.Render.Visible = false` 对返回的结构体副本赋值也是有效的：赋值直接走到引擎，不是改结构体。

### 反射清单

`src/Modules/NextDotNet/ReflectionManifest.json` 是**提交进仓库的快照**，记录引擎向 `entt::meta`
注册了什么。生成器读这份快照，而不是跑引擎——否则 `gnb csharpgen --check` 就必须先有一个能跑的
二进制，而它的职责恰恰是守住那次构建之前的状态。

快照过期的代价是生成的 C# 指向一个已经不存在的属性 id，表现为一条 warning 加一个永远不变的值。
`Test_ReflectionManifest.cpp` 就是这道闸门：它拿提交的清单和实时反射逐项比对，过期就测试失败，
修复方式是 `gnb csharpgen --refresh`。

### 还没绑定的属性类型

生成的文件会在每个 component 末尾列出跳过了什么和为什么，而不是悄悄漏掉：

- `Array` —— 需要元素级访问器，不是整值拷贝
- `Enum` —— 需要生成对应的 C# enum 类型，清单目前只带 type id
- `Mat4` / `AssetRef` —— 托管侧还没有对应类型
- 没有 `ScriptExposed` flag 的属性

## 校验

```bash
gnb csharpgen --check   # 生成文件与两个来源一致（CI 用；不需要构建产物）
gnb dotnet ci           # 上面这条 + 双后端探针 + CoreCLR/AOT 两次引擎构建
```

改了 ABI、宿主或托管层就跑 `gnb dotnet ci`。改了反射注册就跑 `gnb csharpgen --refresh` 并提交
清单和生成文件。

## 相关文档

- [用 C# 开发应用](CSharpGameDevelopment.md) —— 反过来的视角：怎么**用**这些绑定写游戏
- [反射系统](ReflectionSystem.md) —— entt::meta 注册方式与编辑器侧消费者
- [.NET 脚本运行时架构](../designs/dotnet-scripting-design.md) —— 为什么是这个形状（双后端、
  单一 ABI 入口、类型规则的由来）
- [Flappy Bird Parity](../projects/flappy-bird-parity/introduction.md) —— 绑定层的回归验收
