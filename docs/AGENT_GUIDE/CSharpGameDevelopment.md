# 用 C# 开发 gkNextEngine 应用

面向有 Unity 经验的开发者。示例贯穿 `FlappyCSharp`（`assets/csharp/Flappy/FlappyCSharp/`），它是
`FlappyCpp` 的逐行 C# 对照实现，两者通过确定性 replay 逐帧比对，所以它里面的每种写法都是被验证过的。

想给引擎**加**新的 C# 能力（新绑定函数、新暴露属性）看 [.NET Bindings](DotNetBindings.md)；
本文是**用**已有能力写游戏。

## 1. 先改掉的三个 Unity 直觉

这一节比后面所有 API 都重要。带着 Unity 的心智模型去看这套 API 会处处别扭，换过来之后其实很小。

**没有 MonoBehaviour，没有"每个物体一个脚本"。** 整个应用只有**一个** C# 类，继承
`NextGameInstance`，标 `[GameInstance]`。它自己拿着所有游戏状态，自己在 `OnTick` 里推进所有东西。
`FlappyCSharp` 就是一个类 + 几个纯 C# 数据类（`FlappyBird`、`FlappyPipes`、`ParallaxLayer`），
后者不继承任何引擎类型，只是普通对象。

你**不能**定义自己的 component 挂到节点上。C# 侧的 "component" 指的是引擎自带的那几个
（`RenderComponent`、`EnvironmentComponent`、`PhysicsComponent`…），是**引擎的数据**，不是你的行为。
行为一律写在那唯一的游戏类里。

```csharp
// Unity：行为分散在多个 MonoBehaviour 上，各自 Update
class Bird  : MonoBehaviour { void Update() { ... } }
class Pipes : MonoBehaviour { void Update() { ... } }
```

```csharp
// gkNextEngine：一个游戏类驱动全部；bird/pipes 是普通 C# 对象，不继承任何引擎类型
[GameInstance]
public sealed class MyGame : NextGameInstance
{
    private readonly FlappyBird bird = new();
    private readonly FlappyPipes pipes = new();

    protected override void OnTick(double deltaSeconds)
    {
        bird.Update(...);
        pipes.Update(...);
    }
}
```

**没有 Editor 拖场景，没有 prefab。** 场景要么在 `BeforeSceneRebuild` 里**用代码建**，要么加载一个
glTF 文件。没有"把 prefab 拖到 Hierarchy"这一步，也没有 Inspector 上填的字段——所有可调参数走
JSON 配置文件（`FlappyCSharp` 用 `assets/configs/flappy/gameplay.json`）。

**节点用 id 引用，不是对象引用。** `NodeRef` 是一个只装 `uint` 的 `readonly struct`，不是 GameObject。
它不持有节点、不保证节点还活着、也没有 `Destroy` 之后变 null 的魔法。你自己记住这些 id。

## 2. 一个 C# 应用由四个文件组成

**C# 游戏不是纯 C#**，但 C++ 的部分已经收敛到不能再少：所有 C# 游戏共用同一个原生壳
`Modules::NextDotNet::ManagedGameHostInstance`，它负责建窗口、装 NextDotNet、把**每一个**生命周期钩子
转发给托管侧。你要写的 C++ 只有一个约 15 行的 `CreateGameInstance`，其中不含任何游戏逻辑。

| 文件 | 作用 | 抄谁 |
|---|---|---|
| `assets/configs/games/<id>.game.json` | **游戏清单**：窗口、程序集、模块、初始场景、热重载 | `flappy.game.json` |
| `src/Application/Game/<Name>/CMakeLists.txt` | 声明目标，绑定 csproj | `Flappy/FlappyCSharp/CMakeLists.txt` |
| `src/Application/Game/<Name>/<Name>Main.cpp` | 15 行：注册 loader + 指向 manifest | `FlappyCSharpMain.cpp` |
| `assets/csharp/<Name>/<Name>.csproj` | 托管工程 | `Flappy/FlappyCSharp/FlappyCSharp.csproj` |
| `assets/csharp/<Name>/*.cs` | **你的游戏** | `FlappyCSharpGameInstance.cs` |

manifest 是这个游戏唯一的声明来源——它自己的 exe 和 `gkNextLauncher` 读同一份：

```json
{
  "id": "mygame",
  "displayName": "My Game",
  "assembly": "mygame/MyGame.dll",
  "project": "MyGame/MyGame.csproj",
  "window": { "title": "My Game", "width": 1280, "height": 720, "forceSDR": true },
  "requiredModules": ["NextAudio"],
  "initialScene": "Empty.proc",
  "showFlags": { "debugGraphicsPanel": false, "overlay": false },
  "hotReload": true
}
```

`initialScene` 留空表示你自己在 `OnInit` 里调 `Engine.RequestLoadScene`。`requiredModules` 是
**校验**：原生模块是链接期决定的，宿主没有的模块会让这个游戏在菜单里直接标灰并说明原因，而不是加载
到一半才发现没有 loader。

CMake 侧只有两处是你要填的：

```cmake
gk_configure_application(MyGame MODULES ${GK_STANDARD_RUNTIME_MODULES} NextDotNet)

gk_dotnet_managed_game(MyGame
    PROJECT "${GK_DOTNET_MANAGED_ROOT}/MyGame/MyGame.csproj"
    DIR mygame)          # 托管产物落到 bin/csharp/mygame/，与 manifest 的 assembly 前缀一致
```

C++ 全文：

```cpp
#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    return std::make_unique<Modules::NextDotNet::ManagedGameHostInstance>(
        config, options, engine,
        Modules::NextDotNet::FManagedGameHostOptions{
            .manifestPath = "assets/configs/games/mygame.game.json",
            .linkedModules = {"NextAudio", "NextPhysics", "GltfLoader"},
        });
}
```

需要额外的 native loader（比如 SCAD 资产）就在这里 `Modules::Scad::Register();`，并把模块名加进
`linkedModules`。

> 早期每个应用各自抄一份约 90 行的钩子转发壳。那样漏转发一个钩子就是静默失效——`FlappyCSharp` 曾经
> 因此收不到手柄输入。现在转发只有一份实现，这类问题不会再出现；**不要**再手写钩子转发。

csproj 里只有一处不能改：对 `GkNext.Engine` 的引用必须带 `<Private>false</Private>` 和
`<ExcludeAssets>runtime</ExcludeAssets>`。它必须由宿主的加载上下文解析；跟着游戏程序集复制一份会
产生第二个副本，类型标识对不上，热重载直接崩。

## 3. 生命周期

```csharp
[GameInstance]
public sealed class MyGame : NextGameInstance
{
    protected override void OnInit() { }                      // 加载配置、初始化状态
    protected override void BeforeSceneRebuild() { }          // 建场景内容
    protected override void OnSceneLoaded() { }               // 场景已提交，节点可寻址
    protected override void OnTick(double deltaSeconds) { }   // 每帧
    protected override bool OnRenderUI() => false;            // 每帧画 UI
    protected override bool OnInputEvent(in InputEvent e) => false;
    protected override bool OnOverrideCamera(ref CameraOverride c) => false;
    protected override void OnDestroy() { }
}
```

| Unity | gkNextEngine | 说明 |
|---|---|---|
| `Awake` / `Start` | `OnInit` | 程序集加载后立刻调用，早于场景。**热重载会再调一次**。 |
| 拖 prefab 进场景 | `BeforeSceneRebuild` | 唯一能建场景内容的时机 |
| `Start`（拿场景引用） | `OnSceneLoaded` | 场景提交完成，节点 id 从这里开始可用 |
| `Update` | `OnTick(double)` | 参数是秒；没有 `FixedUpdate`，定步长自己累加（见下） |
| `OnGUI` | `OnRenderUI` | 立即模式，每帧重画 |
| `Input.GetKeyDown` | `OnInputEvent` | 事件推送；也有轮询式 `Input.*` |
| `Camera.main` | `OnOverrideCamera` | 引擎每帧来问你，返回 `true` 才生效 |
| `OnDestroy` | `OnDestroy` | 卸载前，热重载也会触发 |

`OnInit` 与 `OnDestroy` **保证只调用一次**（基类做了收敛：宿主的加载/卸载和 C++ 壳的钩子都会到达，
基类只放行第一次）。热重载会创建**新实例**，所以新实例上会重新触发一次 `OnInit`。

Unity 的 `FixedUpdate` 没有对应物，`FlappyCSharp` 的做法是标准累加器，可以直接抄：

```csharp
protected override void OnTick(double deltaSeconds)
{
    fixedAccumulator += Math.Min((float)deltaSeconds, 0.25f);   // 上限防止卡顿后追帧爆炸
    while (fixedAccumulator >= config.FixedDeltaSeconds)
    {
        fixedAccumulator -= config.FixedDeltaSeconds;
        FixedStep();
    }
    Scene.MarkTransformDirty();   // 见下，别忘
}
```

## 4. 场景与节点

### 建场景：`BeforeSceneRebuild`

只有在这个钩子里 `SceneBuild.*` 才有效，钩子外调用会被引擎拒绝并记日志。流程是
**建模型 → 建材质 → 建节点**，都返回 `uint` id：

```csharp
protected override void BeforeSceneRebuild()
{
    Vector3 center = Vector3.Zero;
    uint sphereModel = SceneBuild.AddSphereModel(in center, 0.4f);
    uint boxModel = SceneBuild.AddBoxModel(in min, in max);

    Vector3 yellow = new(1.0f, 0.82f, 0.12f);
    uint material = SceneBuild.AddLambertianMaterial(in yellow);
    // 也有 AddDiffuseLightMaterial(color, intensity) —— 自发光

    RenderNodeSpec spec = new RenderNodeSpec(sphereModel, material)
        .WithTranslation(new Vector3(-3, 0, 0))
        .WithScale(Vector3.One)
        .WithVisible(true);
    birdNodeId = SceneBuild.AddRenderNode("Bird", in spec);   // 记住 id
}
```

**这里的反直觉之处**：节点这时只存在于正在构建的数组里，还没有 id 可寻址，所以
`Scene.SetNodeTranslation(id, ...)` 在这个钩子里**无效**。节点的初始位置、缩放、可见性必须写进
`RenderNodeSpec`。`FlappyCSharp` 的管道就是这样"建出来就已经停在屏幕外且不可见"的。

想加载 glTF 而不是程序化建场景，用 `Engine.RequestLoadScene("assets/models/x.glb")`，
然后在 `OnSceneLoaded` 里找节点。

### 操作节点：`NodeRef`

场景提交之后（`OnSceneLoaded` 及之后的每一帧）节点才可寻址：

```csharp
NodeRef node = new(birdNodeId);

node.Translation = new Vector3(x, y, z);       // 反射属性，等价于下面那行
Scene.SetNodeTranslation(birdNodeId, in pos);  // 直通绑定，更快，适合每帧调用

node.Render.Visible = false;                   // 组件属性简写
node.GetComponent<RenderComponent>().Visible = false;   // 泛型形式，等价
node.Tag = "enemy";
node.Rotation = quaternionAsVector4;           // (x, y, z, w)，注意顺序

if (node.Has<PhysicsComponent>()) { ... }
```

`node.Render` 返回的是结构体副本，但对它的属性赋值**直接写进引擎**，不是改副本——不用担心值类型陷阱。

拿到 `NodeRef` 的方式目前只有两种：**自己记住** `SceneBuild.AddRenderNode` 返回的 id，或者
`NodeRef.WithComponent<T>()`（找场景里第一个带该组件的节点）。**没有 `GameObject.Find(name)`**。

环境光照是特例，走一个专门的绑定按需创建：

```csharp
EnvironmentComponent env = new NodeRef(Scene.GetEnvironmentNodeId()).Environment;
env.HasSun = true;
env.SunIntensity = 600.0f;
env.SunElevation = 0.65f;
env.HasSky = true;
env.SkyIntensity = 150.0f;
```

### 一定要记住 `Scene.MarkTransformDirty()`

移动节点只更新场景图，**不会**告诉渲染器重新上传 instance transform。忘了这行，画面上一切静止，
而且没有任何报错——这是这套 API 最容易浪费时间的一个坑。每帧调一次即可，不用每个节点调。

### 显式物理体

`Physics` 提供跨 CoreCLR / NativeAOT 一致的 primitive-body 门面。body 是不透明 `uint` handle；
建场景时先创建 body，再把它绑定到 render node：

```csharp
Vector3 position = new(0.0f, 2.0f, 0.0f);
Vector3 extent = new(0.25f, 0.25f, 0.25f);
Vector4 rotation = new(0.0f, 0.0f, 0.0f, 1.0f);

uint bodyId = Physics.CreateBoxBody(in position, in rotation, in extent,
                                    PhysicsMotionType.Dynamic);
if (PhysicsBodyIds.IsValid(bodyId) &&
    !SceneBuild.BindPhysicsBody(nodeId, bodyId, NodeMobility.Dynamic))
{
    Physics.RemoveBody(bodyId); // 绑定失败时仍由调用者清理
}
```

`BeforeSceneRebuild` 内用 `SceneBuild.BindPhysicsBody`，场景提交后用 `Scene.BindPhysicsBody`。
绑定成功后 body 由节点 / 场景拥有，节点删除或场景重建时会一起清理。`PhysicsMotionType` 决定 body
是 static / kinematic / dynamic；`NodeMobility` 决定场景侧同步和 mesh 自动烘焙策略。手工创建的
primitive body 通常传 `NodeMobility.Dynamic`，即使 body 本身是 kinematic，也可防止提交场景时被
render mesh 的隐式 body 替换。

运行时可用 `SetBodyActive`、`SetBodyTransform`、`MoveKinematicBody`、`SetBodyVelocity`、
`AddForceToBody` 和 `GetBodyState`；非 Playing 状态可用 `Physics.SetWorldPaused(true)` 冻结整个世界。
当前没有 raycast / shape cast、接触查询或碰撞回调，玩法命中仍需自行实现或后续扩绑定。

## 5. 输入

两种方式，`FlappyCSharp` 用事件式：

```csharp
protected override bool OnInputEvent(in InputEvent e)
{
    if (e.Type != InputEventType.KeyDown || e.IsRepeat) return false;
    if (e.KeyCode == KeyCodes.Escape) { Engine.RequestClose(); return true; }
    pendingFlap = true;
    return true;      // 返回 true = 事件被消费，宿主不再处理
}
```

轮询式（更接近 Unity 的 `Input` 类）：

```csharp
Input.IsKeyDown("space");            // 按住
Input.IsKeyPressed("space");         // 本帧按下
Input.IsMouseButtonDown(0);
Input.IsGamepadButtonDown("a");
```

`InputEvent.KeyCode` 是**原始 SDL 键码**，不是字符串——把它映射成名字需要每个事件一次字符串分配，
在每帧路径上不划算。`KeyCodes` 里只有 `Escape` / `Space` / `Return` 三个常量，其余自己写数值。
轮询式 `Input.IsKeyDown(name)` 接受 SDL 键名字符串，空字符串表示"任意键"。

## 6. UI

立即模式，等价于 Unity 的 `OnGUI` 而不是 uGUI——每帧重画，没有控件树，没有布局系统。
坐标是像素，原点左上。

```csharp
protected override bool OnRenderUI()
{
    Vector2 screen = UI.GetScreenSize();
    UI.DrawRectFilled(x, y, w, h, Color.FromBytes(24, 40, 52, 210), rounding: 16.0f);
    UI.DrawRect(x, y, w, h, Color.White, rounding: 16.0f, thickness: 1.0f);

    Vector2 size = UI.CalcTextSize("SCORE", scale: 2.0f);
    UI.DrawText("SCORE", (screen.X - size.X) * 0.5f, 24.0f, Color.White, scale: 2.0f);
    return false;    // true = 本帧 UI 已被消费
}
```

`UI.GetScreenSize()` 返回的是 **ImGui 坐标系**尺寸，和 `DrawText` / `DrawRect` 用的是同一套坐标，
高 DPI 屏上也对得上。也有 `UI.Begin/End/Text/SetCursorPos` 一组窗口式调用，用于调试面板。

`Color` 是 0..1 的 float，`Color.FromBytes(r, g, b, a)` 接受 0..255。

## 7. 相机、音频、配置、文件

```csharp
protected override bool OnOverrideCamera(ref CameraOverride camera)
{
    camera.Position = new Vector3(0, 0, 12);
    camera.Target = Vector3.Zero;
    camera.Up = Vector3.Up;
    camera.FieldOfView = 50.0f;
    return true;     // 返回 false 则用场景自带相机
}

Audio.PlaySfx("assets/sounds/flap.wav");        // volume 默认 1.0
Audio.PlayMusic("assets/music/bgm.ogg", 0.6f);
Audio.StopMusic();
```

**读资源用 `Assets.ReadFile(path)`**（返回 `byte[]`），它走引擎的包文件系统，所以 `.pak` 里的资源也读得到；
直接用 `File.ReadAllBytes` 打包后会失效。**写文件用 BCL**（`File.WriteAllBytes` 等）——引擎不提供写接口，
只提供它独有的信息：`Paths.GetProjectRoot()` 和 `Paths.GetOutputDir()`。

配置解析要用 `JsonDocument` 手写读取，**不要用 `JsonSerializer.Deserialize<T>()`**：后者基于反射，
NativeAOT 下会失效。`FlappyConfig.cs` 是可以直接抄的模板，包含默认值 + 逐字段覆盖的写法。

## 8. 开发循环

```bash
gnb build FlappyCSharp        # 构建（C++ 壳 + 托管发布一起做）
gnb run FlappyCSharp          # 运行它自己的 exe
gnb run gkNextLauncher        # 或：一个进程里选任意 C# 游戏
```

**Launcher 是更快的循环。** `gkNextLauncher` 读 `assets/configs/games/*.game.json`，菜单里列出所有
C# 游戏（Up/Down 选，Enter 开，游戏里 Esc 回菜单）。每个条目旁边的 **Rebuild** 会就地重新发布那个游戏的
C#——**改 C# → 点一下 → 玩**，不需要 C++ 构建，也不需要重启进程。切换游戏时引擎会把世界（场景、物理、
cvar、ShowFlags、音频、窗口标题）恢复到中性状态，所以上一个游戏不会污染下一个。

**编辑器里也能跑（play-in-editor）。** `gnb editor` 的工具栏有游戏下拉 + Play（**F5**）。游戏跑起来后按
**F8** eject：游戏继续跑，但相机和输入回到编辑器，于是可以在 Outliner 里选中它的节点、在 Properties 里
读写 Transform 和组件——对着活着的游戏世界调参。再按 F8 回到游戏，Stop 回到 Play 之前打开的场景。

PIE 是刻意窄的：**Stop 不保留 Play 期间的编辑**，它只是把 Play 前那个场景按路径重新加载一遍，选择集和
undo 历史都从头开始。Play 也总是走游戏自己的完整流程和场景，没有"用当前编辑器场景开始游戏"这回事。
Stop 之后可以点 **Rebuild C#**，下一次 Play 就用新代码。

Launcher 只在 CoreCLR 后端下构建。NativeAOT 把游戏静态链进 exe，一个 exe 只能有一个游戏——所以发布仍然
是 per-game exe，就像 Unity 的 Editor 与 Player。

**热重载**（仅 CoreCLR 后端，即默认后端）：引擎每 0.5 秒检查游戏程序集的时间戳，变了就卸载重载，
不用重启应用。触发方式是在应用运行着的时候重新发布托管工程：

```bash
dotnet publish assets/csharp/Flappy/FlappyCSharp/FlappyCSharp.csproj -c Release -o out/build/windows/bin/csharp/flappy
```

程序集是**读进内存**加载的（`File.ReadAllBytes` + `LoadFromStream`），文件不被进程占用，所以直接
覆盖发布是安全的。成功时日志会出现 `[dotnet] hot reloaded ...`。

重载会创建新的游戏实例：`OnDestroy` → 新实例 → `OnInit`。**实例字段全部丢失**，状态要么能从
`OnInit` 重建，要么就得忍受重置。场景不会重建（`BeforeSceneRebuild` 不会重新触发），所以改建场景的
代码需要重启。

热重载不是应用必须满足的架构要求。大量持有节点 / body handle 和对象池状态的游戏，可以像
`Brotato3DCSharp` 一样在 manifest 里写 `"hotReload": false`，使用"构建后重启"的简单开发循环；
不要仅为了保住热重载而给玩法层加入状态序列化和半重建世界协议。在 launcher 下这条路径同样顺畅：
Esc 回菜单、Rebuild、再进游戏，全程不重启进程。

> 注意：引擎启动时的"C# 源码变了就自动重编"（manifest 的 `compileManagedSources`）只对 sandbox 工程
> `GkNext.Game` 生效，它硬编码了那个 csproj。改 `FlappyCSharp` 的 `.cs` 会让它误以为需要重编并去编
> `GkNext.Game`，你的改动不会生效。用上面的 `dotnet publish`、`gnb build <目标>`，或 launcher 里那个
> 游戏自己的 Rebuild 按钮。

**截图验证**（不弹窗、自动退出，适合快速看一眼）：

```bash
gnb shot --target FlappyCSharp --frames 90 --ui
```

**调试**：CoreCLR 后端下 Visual Studio 可以混合模式同时调 C++ 和 C#。

## 9. 性能与 AOT 约束

发布用 NativeAOT 后端（`-DGK_DOTNET_BACKEND=AOT`），托管代码必须两种后端行为一致。下面几条违反了
**只会在 AOT 构建/运行时暴露**：

- 不要 `Assembly.Load` / `Reflection.Emit` / `dynamic` / `Activator.CreateInstance(Type)`
- 不要依赖反射序列化（`JsonSerializer.Deserialize<T>` 等）
- 不要依赖 enum 名字元数据：`Enum.ToString()` 在裁剪后可能拿不到名字，需要名字就自己写 `switch`
- 不要 `DllImport`，所有 native 调用都走已有绑定

每帧分配是这一层拖慢帧时间的现实方式。开发时打开分配守卫：

```bash
GK_DOTNET_ALLOC_GUARD=1        # 超预算（默认 4KB/帧）会 warn 一次
GK_DOTNET_ALLOC_BUDGET=8192    # 可调
```

热路径避开 LINQ、闭包、装箱和字符串插值；用结构体、`Span`、对象池。守卫只包住 `OnTick`，
**不覆盖 `OnRenderUI`**——`FlappyCSharp` 的 HUD 里那些 `$"{score}"` 插值就在守卫范围之外。
参照值：`FlappyCSharp` 连跑 200 帧不触发守卫。

## 10. 现在还没有的能力

写之前先确认你要的东西在不在这个列表里：

- **自定义 component**：不能从 C# 定义组件类型挂到节点上
- **按名字找节点**：只有 `NodeRef.WithComponent<T>()` 和你自己记的 id
- **通用加/删组件**：仍没有任意 component 的增删 API；显式 body 可通过
  `Scene{Build}.BindPhysicsBody` 安装 / 复用 `PhysicsComponent`
- **物理查询与事件**：已有 primitive body 生命周期、kinematic、速度 / 施力、状态和 world pause，
  但没有 raycast / shape cast、接触查询或碰撞回调
- **数组 / 反射 Enum / Mat4 / AssetRef 属性**：反射里有，但还没通用绑定（生成的
  `Components.g.cs` 会在每个组件末尾列出跳过了哪些、为什么）
- **协程**：没有；用状态机或计时器字段
- **动画播放控制**：没有绑定

缺的能力大多是"加一行 def + 一个实现函数"的距离，见 [.NET Bindings](DotNetBindings.md)。

## 11. 从哪抄

| 想干的事 | 看哪 |
|---|---|
| 完整的应用骨架 | `assets/csharp/Flappy/FlappyCSharp/FlappyCSharpGameInstance.cs` |
| 完整 C# 玩法纵切 | `assets/csharp/Brotato3D/Brotato3DCSharp/` |
| 程序化建场景 | 同上，`BeforeSceneRebuild` |
| HUD | 同上，`OnRenderUI` |
| C# drawlist 控件 | `assets/csharp/GkNext.Engine/UI/ManagedImGui.cs` |
| 固定物理池 / kinematic 推挤 | `assets/csharp/Brotato3D/Brotato3DCSharp/BrotatoPhysicsSystem.cs` |
| 定步长模拟 | 同上，`OnTick` + `FixedStep` |
| AOT 安全的 JSON 配置 | `assets/csharp/Flappy/FlappyCSharp/FlappyConfig.cs` |
| 确定性随机 | `assets/csharp/Flappy/FlappyCSharp/FlappyRng.cs` |
| 纯逻辑对象 | `FlappyBird.cs` / `FlappyPipes.cs` / `FlappyParallax.cs` |
| C++ 壳（15 行的全部） | `src/Application/Game/Flappy/FlappyCSharp/FlappyCSharpMain.cpp` |
| 游戏清单 | `assets/configs/games/flappy.game.json` |
| 钩子转发的唯一实现 | `src/Modules/NextDotNet/ManagedGameHostInstance.cpp` |
| 最小 ABI 探针 | `assets/csharp/GkNext.Game/ProbeGame.cs` |
| 可调用的全部 API | `assets/csharp/GkNext.Engine/Engine.g.cs`（生成，别手改） |
| 可读写的全部组件属性 | `assets/csharp/GkNext.Engine/Components.g.cs`（生成，别手改） |

## 相关文档

- [.NET Bindings](DotNetBindings.md) —— 给引擎加新的 C# 能力
- [.NET 脚本运行时架构](../designs/dotnet-scripting-design.md) —— 双后端、ABI 形状与取舍理由
- [Flappy Bird Parity](../projects/flappy-bird-parity/introduction.md) —— C++/C# 对照验收怎么跑
- [托管游戏 Launcher](../designs/managed-game-launcher-design.md) —— manifest 契约、进程内切换游戏、
  play-in-editor、世界重置边界
