---
title: "用 C# 绑定实现 Brotato3D 的能力缺口与可行性"
category: design
status: 实施中；首个 C# 可玩纵切已落地，原 C++ application 保留
owner: engine/scripting
created: 2026-08-22
last_updated: 2026-08-24
design: dotnet-scripting-design.md
related: ../AGENT_GUIDE/DotNetBindings.md, ../AGENT_GUIDE/Brotato3D.md
---

# 用 C# 绑定实现 Brotato3D 的能力缺口与可行性

本文回答两个问题：

1. **当前 C# 绑定面到底提供了什么**（以 `EngineApi.def.h` + 反射清单为准，不是以文档为准）；
2. **要把 Brotato3D 从 C++ 换成 C#，还缺哪些能力，代价多大，值不值得**。

结论先行：**架构上可行，没有一项缺口是 ABI 类型规则挡住的；但"补几行 def"这个印象是错的——
按现在的形状全量迁移需要新增约 50–100 个绑定项、1–2 次 ABI version bump，以及 ~9k 行 C# 重写。
真正的成本集中在 UI 与物理两块，而其中 UI 有一条能把成本砍到 1/3 的替代设计（见 §4.2）。
推荐做法不是"迁移 Brotato3D"，而是把 Brotato3D 当成绑定面演进的验收目标，分档落地（见 §7）。**

---

## 1. 分析启动时绑定面的实际形状

本节保留 2026-08-22 做可行性判断时的 68 项基线，便于核对“为 Brotato3D 实际增加了什么”；
实施后的当前数字与取舍见 §8。

绑定能力有且只有两个来源，这条分界是设计 4.4 的决议（反射拥有*属性*，绑定表拥有*函数*）：

| 来源 | 文件 | 当前规模 |
|---|---|---|
| 函数 | `src/Modules/NextDotNet/EngineApi.def.h` | **68** 个 `GK_API` 条目（分析基线） |
| 属性 | `entt::meta` → `ReflectionManifest.json` → `Components.g.cs` | Node + 7 个 component |

68 个条目的分布：

| 命名空间 | 条数 | 覆盖 |
|---|---:|---|
| `Component` | 21 | 反射属性的类型化通道（Bool/Int32/UInt32/Float/Double/Vec2/Vec3/Vec4/Quat/String 各一对 + `Has`） |
| `UI` | 11 | `Begin/End/Text/SetCursorPos/GetWindowSize/SetWindowFontScale/GetScreenSize/CalcTextSize/DrawText/DrawRectFilled/DrawRect` |
| `Scene` | 10 | `GetIndicesCount/FindNodeIdWithComponent/AddRenderNode/RemoveNodeById/MarkTransformDirty/RecalcNodeTransform/SetNodeTranslation/SetNodeScale/SetNodeVisible/GetEnvironmentNodeId` |
| `Engine` | 7 | 帧计数、时间、delta、`RequestLoadScene`、`RequestClose`、`IsReplayMode` |
| `SceneBuild` | 5 | `AddBoxModel/AddSphereModel/AddLambertianMaterial/AddDiffuseLightMaterial/AddRenderNode` |
| `Input` | 5 | 键盘 / 鼠标按钮 / 手柄按钮的 down 与 pressed |
| `Log` | 3 | Info / Warn / Error |
| `Audio` | 3 | `PlaySfx/PlayMusic/StopMusic` |
| `Paths` | 2 | `GetProjectRoot/GetOutputDir` |
| `Assets` | 1 | `ReadFile`（透过 pak 文件系统读） |

生命周期是 **7 个固定钩子**（`EScriptHook` 的 5 个 + `InputEvent` + `OverrideCamera`），
形状写死在 `FManagedApi` 里。**没有通用的 C++ → C# 调用通道**——这一点对混合方案很关键（§6）。

组件属性侧，生成器明确跳过四类，并在生成文件里写出跳过原因：
`Array`（需要元素级访问器）、`Enum`（清单只带 type id，没有 enumerator 名字）、
`Mat4` / `AssetRef`（托管侧无对应类型）。这直接影响 `RenderComponent.Materials` 和
`PhysicsComponent.Mobility`——两个都是 Brotato3D 的必需项。

### 1.1 参照系：FlappyCSharp 的规模

| | C++ | C# |
|---|---:|---:|
| FlappyCpp / FlappyCSharp | 1163 行 | 1215 行 + 94 行 C++ 壳 |
| Brotato3D | 9640 行（UI 占 2149） | 预计 9000–10000 行 + ~90 行壳 |

C++/C# 行数基本 1:1，所以"C# 侧要写多少"不是风险点，风险点全在绑定面。

---

## 2. Brotato3D 实际依赖的引擎能力

统计自 `src/Application/Game/Brotato3D/` 的 include 与调用点：

| 依赖 | 用在哪 | 强度 |
|---|---|---|
| `NextPhysics`（body 生命周期 / transform / velocity / active） | 碎块池、kinematic 推挤体、竞技场墙 | **重**（15 处调用点，1360 个 body） |
| `Assets::NodeUtils`（`SetPrimaryMaterial` / `SetOutlineFlags` / `SetVisible`） | 敌人受击闪白、碎块着色、投射物、临时灯 | **重**（45 处） |
| `Node::SetParent` | 玩家武器与朝向节点、敌人身体方块、撤离车、rig | **重**（6 处，每处带子树） |
| `Node::SetTranslation/SetRotation/SetScale` | 每帧刷所有活动实体 | **重**（50 处调用点） |
| ImGui + `NextUI::Painter` + `FontLoader` + `RequestUiTexture` | 主菜单 / HUD / 升级 / 商店 / 暂停 / 结算 / 设置 | **重**（2149 行，87 处图标） |
| `SceneBuilder::CreateRenderNode` / `AddLambertianMaterial`（**运行时**） | `EnsureLightMaterial(color)` 按色缓存新建材质 | 中 |
| `FProcModel::CreateBox/CreateSphere/CreateAreaLight` | 全部程序化几何 + "灯光即材质" | 中 |
| `NextAudio::PlaySfx/PlaySfxVariant/PlayMusic/SetMusicVolume` | `Brotato3DAudio.hpp` 单入口 | 中 |
| `FScadRigLoader` + `FRigInstance` + 每帧 bone 姿势 | 玩家角色视觉 | 中（272 行，结构性） |
| `OnGamepadInput(leftStickX, ...)` | 手柄摇杆移动 | 中 |
| `NextLocalization` | 66 处 `Tr()` / `TrFormat()` | 轻（C# 可自理） |
| `Engine::RequestLoadScene`（`.scad` 竞技场） | 三张固定地图 | 轻（已可用） |
| `UserPaths::EnsureUserFile` | `best.json` 存档 | 轻 |
| `nlohmann::json`（9 个配置文件，600 行） | Def 原型加载 | 轻（C# 可自理，但受 AOT 约束） |

---

## 3. 已经够用、不需要补的部分

先划掉这些，免得高估工作量：

- **日志、帧时间、`RequestClose`、`IsReplayMode`**——原样可用。
- **竞技场加载**：`Engine.RequestLoadScene("assets/scad/source/brotato3d/deadly_town.scad")` 已经能吃
  `.scad` 路径，三张地图零成本。
- **相机**：`OnOverrideCamera` 钩子存在。Brotato3D 的相机平滑跟随、屏幕震动、clamp 全是纯数学，
  搬到 C# 侧算完填 `CameraOverride` 即可。
- **天空 / 太阳过渡**：`Scene.GetEnvironmentNodeId()` + `EnvironmentComponent` 反射属性，
  FlappyCSharp 已在用。
- **键盘与手柄*按钮*输入**：`Input.IsKeyDown/IsKeyPressed/IsGamepadButtonDown` 加上带 SDL raw code 的
  `OnInputEvent`，足以覆盖 `OnKey` 的全部逻辑。摇杆*轴*不行，见 §4.3。
- **配置与本地化**：C# 有 BCL，9 个 JSON 直接自己读；`i18n.json` 同理，**不需要 localization 绑定**。
  唯一约束是 AOT 下不能用反射序列化（见 §5.3）。
- **运行时增删节点**：`Scene.AddRenderNode` / `RemoveNodeById` 可用（但材质必须是建场景时创建的，见 §4.1）。
- **`best.json` 存档**：`Paths.GetOutputDir()` + BCL `File`。想沿用引擎的用户目录约定则补一条
  `Paths_GetUserFile`，1 行。

---

## 4. 缺口清单

按"补一行 def 就行" → "要动 ABI" → "结构性"三档排列。

### 4.1 补绑定即可（形态清楚，风险低）

| 缺口 | 建议绑定 | 条数 | 说明 |
|---|---|---:|---|
| 节点旋转的快路径 | `Scene_SetNodeRotation(nodeId, FVec4*)` | 1 | 反射路径 `node.Rotation` 能用，但每帧刷 1000+ 实体不该走属性查找 |
| **合并变换写入** | `Scene_SetNodeTransform(nodeId, pos, rot, scale)` | 1 | 现在分开设会做 **3 次** `RecalcTransform(true)` + 3 次 node 查找，见 §5.1 |
| **批量变换写入** | `Scene_SetNodeTransforms(const uint32_t* ids, const FTransform* xf, int32_t count)` | 1 | 碎块池 / 子弹池的正解，把数千次调用压成个位数 |
| 父子关系 | `Scene_SetNodeParent(childId, parentId)` | 1 | 6 处，每处带整棵子树 |
| 材质切换 | `Scene_SetNodePrimaryMaterial` / `Scene_SetNodeMaterialRecursive` | 2 | 直接绑 `NodeUtils`，绕开未绑定的 `Materials` 数组属性 |
| 描边 | `Scene_SetNodeOutlineFlags(nodeId, flags)` | 1 | `NodeUtils::SetOutlineFlags`，不是反射属性 |
| 递归可见性 | `Scene_SetNodeVisibleRecursive` | 1 | 敌人身体方块整体隐藏 |
| **运行时创建材质** | `Scene_AddLambertianMaterial` / `Scene_AddDiffuseLightMaterial` | 2 | `EnsureLightMaterial(color)` 在 tick 里按色缓存新建材质。**这是相对 QuickJS 基线的回退**：旧的 `BindScenePrototype` 本来就把这两个挂在 live `Scene` 上 |
| AreaLight 模型 | `SceneBuild_AddAreaLightModel` | 1 | "灯光即材质"是 Brotato3D 唯一的照明方案 |
| 音频补齐 | `Audio_PlaySfxEx(path, volume, minIntervalMs)`、`Audio_SetMusicVolume`、（可选）`Audio_PlaySfxVariant` | 2–3 | `minIntervalMs` 是防同帧叠音的关键参数，当前绑定丢了；variant 可退化为 C# 侧自己随机选路径 |
| 用户目录 | `Paths_GetUserFile(app, name, buf, cap)` | 1 | 可选 |
| 物理世界暂停 | `Physics_SetWorldPaused(GkBool)` | 1 | 每次状态切换都调 |
| **物理 body** | `Create{Box,Sphere}Body` / `RemoveBody` / `Set{Transform,Velocity,Active}` / `Get{Transform,Velocity}` / `AddForceToBody` | ~10 | `NextBodyID` 以 blittable handle（uint32/uint64）跨界；native 侧 API 已经足够扁平，逐条翻译即可 |

小计 **~26 项**，全部落在既有类型规则内（scalar + 指针传定长结构 + handle）。

### 4.2 UI —— 工作量最大的一块，但有一条便宜得多的路

Brotato3D 用到的 ImGui 面（统计自 `Brotato3DUI.cpp`）：

- **绘制图元**：`AddRectFilled`(30) `AddText`(22) `AddLine`(16) `AddRect`(14) `AddCircle`(10)
  `AddCircleFilled`(8) `AddPolyline`(2) `AddConvexPolyFilled`(2)，以及前景 / 背景 drawlist
- **控件**：`Button`(19) `InvisibleButton`(3) `SliderFloat`(3) `Checkbox`(2) `IsItemHovered`(6) `BeginDisabled`(3)
- **布局**：`SameLine`(13) `Dummy`(12) `Separator`(8) `PushTextWrapPos`(5) `BeginChild`(3) `BeginGroup`(1)
  `SetNextWindowSize`(10) `SetNextWindowPos`(5)
- **弹窗**：`OpenPopup`(6) `BeginPopupModal`(5) `CloseCurrentPopup`(8) `BeginTooltip`(3)
- **样式**：`PushStyleColor`(22) `PushStyleVar`(2)
- **纹理与字体**：`RequestUiTexture` + `NextUI::Painter::{DrawImageContain,DrawImageCover,DrawBar,DrawTexturedBar,DrawFullscreenDim}`，
  3 档自定义字体（`FontLoader::Load`），`DrawOutlinedBoldText` 靠按字号 / 字体绘文本

**路线 A —— 直译 ImGui**：把上面这些一根一根穿过 C ABI，估 **45–60 项**。
代价不只是条数：ImGui 是即时模式，控件状态留在 native 侧，绑定面一旦定死很难演进，
最终得到的是一个"更难用的 ImGui"。**不推荐。**

**路线 B —— 只绑 drawlist + 输入状态，控件在 C# 侧实现**（推荐）：

即时模式控件的本质就是"画一个矩形 + 对鼠标做命中测试"。如果 C# 拿到

- 完整 drawlist 图元（含 `AddLine/AddCircle/AddCircleFilled/AddPolyline/AddConvexPolyFilled` + 前景 / 背景层）
- 图片：`UI_RequestTexture(path) -> {handle, pixelSize}` + `UI_DrawImage(handle, rect, uv, tint)`
- 字体：`UI_LoadFont(desc) -> fontId` + `UI_DrawTextFont(fontId, size, pos, color, text)` + `UI_CalcTextSizeFont`
- 输入状态：`Input_GetMousePosition`（当前**完全没有**）+ 已有的鼠标按钮 down / pressed

那么 `Button` / `Checkbox` / `SliderFloat` / `BeginDisabled` / tooltip / 模态遮罩全部可以在 C# 侧
用矩形命中测试自己写——Brotato3D 的按钮本来就有相当一部分是 `InvisibleButton` + 自绘。
绑定面从 45–60 降到 **~15**，而且 C# 侧得到的是一个可以持续演进的 UI 库，而不是 ImGui 的 C ABI 转写。

路线 B 的代价（必须明说）：

- 失去 ImGui 的键盘 / 手柄导航。Brotato3D **开了** `ImGuiConfigFlags_NavEnableGamepad`，
  手柄操作菜单要自己实现焦点环。
- 失去 popup 的模态输入拦截，要自己做输入吞掉。
- 与 C++ 侧 Editor / DevTools 的 UI 风格分家，两套代码不再共享控件。

### 4.3 需要动 ABI（version bump）

| 缺口 | 影响 | 说明 |
|---|---|---|
| `FRenderNodeSpec` 缺 **Rotation** | 结构体布局变更 → `GK_DOTNET_ABI_VERSION` bump | Brotato3D 在 `BeforeSceneRebuild` 里一口气建 2000+ 节点，其中不少需要初始旋转；该窗口内 live `Scene_Set*` 不可用，旋转只能来自 spec |
| **手柄摇杆轴** | 二选一 | C++ 有 `OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, ...)`，`IGameModule` 完全没有对应钩子。**推荐加 poll 式绑定 `Input_GetGamepadAxis(axis)`**（只加表项，不动 ABI 形状），而不是给 `FManagedApi` 加第 8 个钩子 |
| **显式挂接 physics body** | 新绑定 + 固定宽度 enum | Brotato3D 的 kinematic 推挤体要求代理节点挂一个标 `Dynamic` 的 `PhysicsComponent` 以跳过 mesh 自动晋升（见 `Brotato3D.md` §5.4）。实施时采用 `Scene{Build}_BindPhysicsBody(nodeId, bodyId, mobility)`，一次完成 component 建立、旧 body 替换和所有权转移 |
| **Enum 属性绑定** | 本阶段不需要 | `ENodeMobility` 的通用反射 enum codegen 仍需要清单携带 enumerator；M2 没有为一个属性扩大整套清单，而是在 ABI 定义稳定的 `GkNodeMobility : int32_t`，由专用绑定显式转换 |

### 4.4 结构性缺口

**ScadRig 角色与骨骼动画。** `FScadRigLoader::LoadRig` → `FRigInstance::Instantiate(scene, asset, desc, boneNodes)`
→ 每帧驱动 bone 节点姿势。把它的内部对象模型（rig asset、part 列表与 `sectionTintable`、bone 索引、
clip 采样）整个搬过 ABI 不合理。两条路：

- **门面式绑定**（推荐）：`Rig_Load(path) -> rigId`、`Rig_Instantiate(rigId, namePrefix) -> rootNodeId`、
  `Rig_GetBoneNodeId(instanceId, boneName)`、`Rig_PlayClip(instanceId, clip, timeSeconds)`、
  `Rig_SetTintMaterial(instanceId, materialId)`。约 6–8 项，把 rig 当黑盒。
  风险：门面若同时在绑定表和反射里各注册一份，就会成为第二个 QuickJS 漂移点——只能有一份事实。
- **留在 C++**：作为混合方案的一部分（§6）。

**热重载与对象池状态。** Brotato3D 的全部运行时状态（1360 碎块槽、768 子弹槽、敌人池、波次状态机）
会落在 C# 侧。CoreCLR 热重载换的是 assembly，托管状态随之丢失；如果强行保留热重载，就要在
`OnDestroy` / `OnInit` 做状态序列化往返，或接受“重载后重开一局”。本次实施将热重载视为可选开发能力，
不是玩法架构要求：`Brotato3DCSharp` 直接关闭它，修改后正常构建并重启，不为热重载引入状态恢复协议。

---

## 5. 性能分析

### 5.1 每帧跨界调用量与成本

按 Brotato3D 的池容量估算最坏情况（活跃实体数，非池容量上限）：

| 来源 | 估算调用数 / 帧 |
|---|---:|
| 碎块（~400 活跃 × 读 body transform + 写 node transform） | ~1200 |
| 敌人（~150 × 自身 transform + ~5 身体方块） | ~1000 |
| 投射物（~200 活跃 × 2） | ~400 |
| UI（图标 + drawlist 图元） | 300–800 |
| **合计** | **3000–8000** |

成本拆开看，两部分差一个数量级：

- **P/Invoke 本身可以忽略**。全部参数 blittable、无 marshalling、通过函数指针 `calli`，
  约 1–3 ns/次。8000 次 ≈ **16 µs**。
- **成本在 native 侧的 per-call 开销**。每个 `Scene_Set*` 都要
  `FindNodeOrWarn` → `unordered_map::find` + `shared_ptr` 拷贝（原子引用计数），
  然后 `RecalcTransform(true)`。C++ 版本直接持 `Node*`，零查找。
  按 30–80 ns/次估，3000 次 ≈ **0.15 ms**，8000 次 ≈ **0.4 ms**。
  在 16.6 ms 预算里是 1–2.5%——可接受，但不是零，**且这是绑定形状造成的，不是 C# 造成的**。

两条优化直接把它压回噪声级，而且对所有 C# 游戏都有价值：

1. `Scene_SetNodeTransform(pos, rot, scale)` 合并写入——立省 2/3 的查找与 recalc。
2. `Scene_SetNodeTransforms(ids, transforms, count)` 批量写入——碎块池一次调用刷完，
   数千次降到个位数。

### 5.2 GC 与分配

`FrameAllocationGuard` 默认预算 4KB/帧，**只包住 `OnTick`，不覆盖 `OnRenderUI`**。
Brotato3D 的风险点：

- 66 处 `Tr()` / `TrFormat()` 每帧返回新 string——必须缓存成字段，不能每帧格式化。
- 伤害漂浮文字、HUD 数字：`$"{value}"` 插值在 `OnRenderUI` 里守卫看不见，但 GC 压力真实存在。
- 碎块 / 子弹 / 敌人池必须是 struct 数组或预分配对象池，不能每帧 new。

这些在 C++ 里是免费的，在 C# 里是需要主动管的纪律。参照值：FlappyCSharp 连跑 200 帧不触发守卫。

### 5.3 NativeAOT 约束

发布走 AOT 后端，下面几条**只在 AOT 下暴露**：

- 9 个配置文件（600 行 JSON）**不能**用 `JsonSerializer.Deserialize<T>`（反射序列化）。
  要么用 source-generated `JsonSerializerContext`，要么照 `FlappyConfig.cs` 手写解析。
  Brotato3D 的 Def 结构比 Flappy 复杂一个量级，这是一笔实打实的额外工作量。
- `Enum.ToString()` 裁剪后可能拿不到名字——Brotato3D 有 `EAppState` / `ELanceState` / `EDebrisKind`
  等多个枚举，凡是要显示名字的地方都得自己写 `switch`。
- 不能 `DllImport`：所有 native 调用必须走绑定表，没有逃生舱。

---

## 6. 混合方案为什么也不便宜

直觉上的折中——"C++ 留 UI + rig + 场景构建，C# 只写玩法逻辑"——正好对应 Brotato3D 里
已经是纯逻辑、零渲染依赖的那两块（`FWaveSystem`、`FShop`）。听起来很自然。

但**当前 ABI 没有 C++ → C# 的通用调用通道**：`FManagedApi` 只有 7 个固定入口
（`LoadGame` / `UnloadGame` / `ReloadGame` / `Tick` / `Lifecycle` / `InputEvent` / `OverrideCamera`），
游戏特定的调用无处可去。要做混合就得二选一：

- 给 `FManagedApi` 加一个通用 `Game_Invoke(id, argsPtr, argsSize, outPtr, outSize)`——ABI bump，
  而且这是一个类型不安全的口子，正是设计 3.4 想避免的形状；
- 或者让 C# 侧**完全拥有**玩法状态，C++ 侧只在 `OnTick` 之后通过绑定反查——但那样 C++ 就没有
  "调用 C# 逻辑"的需求了，退化成全量迁移。

所以混合方案的第一步和全量迁移的第一步是同一步。这不是反对混合，
而是说**它不能作为绕开绑定面工作的捷径**。

---

## 7. 可行性结论与推荐路线

### 汇总

| 维度 | 估算 |
|---|---|
| 新增绑定项（UI 走路线 B） | **~50–60**（当前 68 → ~125） |
| 新增绑定项（UI 走路线 A 直译） | ~90–100（当前 68 → ~165） |
| ABI version bump | 1–2 次（`FRenderNodeSpec` 加 Rotation；若给 `FManagedApi` 加钩子则再一次） |
| codegen 变更 | Enum 属性支持（清单需带 enumerator 名字） |
| C# 侧代码量 | ~9000–10000 行 + ~90 行 C++ 壳 |
| 每帧跨界成本 | 优化前 0.15–0.4 ms；加合并 / 批量 transform 后降到噪声级 |

**判断：技术上完全可行，没有一项缺口撞到 ABI 的类型规则或双后端约束。**
但"缺的能力大多是加一行 def 的距离"这句话对 Brotato3D 不成立——它成立于 §4.1 的 26 项，
不成立于 UI、rig 和 enum codegen。

**同时：为了迁移 Brotato3D 而迁移 Brotato3D，收益不清楚。** 它已经是能跑的 C++ 代码，
迁移换来的是热重载和 C# 生态，代价是上面整张表；而 §4.4 指出热重载在重状态游戏上恰恰最难兑现。

### 推荐做法：把 Brotato3D 当验收目标，不当迁移目标

按下面的顺序补绑定，每一档都有独立于 Brotato3D 的价值和可验收产物：

| 档 | 内容 | 验收 |
|---|---|---|
| **M1 场景与变换** | §4.1 的 node / 材质 / parent / outline / live 材质 + 合并与批量 transform + spec 加 Rotation | C# demo：1000 个方块的"碎块雨"，60fps，`gnb shot` 出图 |
| **M2 物理** | body 生命周期 + kinematic + world paused + 显式 body/node 绑定 + 稳定 ABI enum | C# demo：1000 体物理碎块池，可推挤，帧时间与 C++ 版对齐 |
| **M3 输入与音频** | `Input_GetGamepadAxis` / `Input_GetMousePosition`、`Audio_PlaySfxEx`(minIntervalMs) / `Audio_SetMusicVolume` | 手柄双摇杆驱动的 C# demo |
| **M4 UI（路线 B）** | drawlist 图元 + 纹理 + 字体 + 鼠标位置；控件在 C# 侧实现 | C# 复刻 Brotato3D 的 HUD 与升级抽卡面板 |
| **M5 Rig 门面** | `Rig_Load/Instantiate/GetBoneNodeId/PlayClip/SetTintMaterial` | C# 驱动 ScadRig 走路 |
| **M6 全量移植** | 视 M1–M5 的实际成本再决定做不做 | Brotato3D C# 版与 C++ 版行为对照，比照 Flappy parity |

M1–M3 是任何 C# 游戏都会撞到的墙（`CSharpGameDevelopment.md` §10 已经把其中大半列为
"现在还没有的能力"），**即使 M6 永远不做也是净收益**。M4 的路线选择应该单独决策，
因为它决定了 C# UI 层未来的形状。

### 风险登记

- 每次 ABI bump 都要跑 `gnb dotnet ci`（双后端探针 + 两次引擎构建），并重跑 Flappy parity 回归。
- UI 绑定面一旦定死很难改。路线 A 与路线 B 是**单向门**，先想清楚再落第一根桩。
- Rig 门面若同时进绑定表和反射，会重演 QuickJS 时代的漂移。只能有一份事实。
- 热重载不保 C# 侧对象池状态；当前目标有意关闭热重载。未来若重新开启，必须先定义显式重载语义，
  不能让这项可选能力反向复杂化玩法和对象池设计。

---

## 8. 实施进展（2026-08-24）

当前实现已经按本文的路线 B 落地为独立目标 `Brotato3DCSharp`，并继续完成了 M2 物理纵切；原有
`src/Application/Game/Brotato3D/` 未被替换或删改。目标可独立启动，当前包含：

- C# 持有完整玩法状态：角色移动 / 冲刺、自动武器、敌人生成与追击、投射物与数学碰撞、
  掉落 / 经验 / 升级、波次、商店、胜负状态和跟随相机；配置直接读取现有 Brotato3D JSON，
  使用 `JsonDocument` 手写解析以保持 NativeAOT 安全。
- 场景在 `BeforeSceneRebuild` 一次性建立托管对象池，每帧把活动实体写入一个
  `ReadOnlySpan<NodeTransform>`，通过一次 `Scene.SetNodeTransforms` 跨界提交；没有逐实体的
  三次位置 / 旋转 / 缩放调用。
- UI 控件和命中测试完全位于 `ManagedImGui.cs`。C# 将矩形、圆、折线、凸多边形、文本和图片
  累积为三个可复用缓冲区，每帧只调用一次 `UI.SubmitDrawList`；native 只把命令翻译为
  `ImDrawList`，没有新增 Button、Checkbox、Slider、popup、style 等 ImGui API 绑定。
- M2 使用通用的 primitive-body 门面，而不是 Brotato3D 专用 API：球 / 盒 body 创建、删除、激活、
  transform / kinematic move、速度、施力、状态读取和 world pause；`Scene.BindPhysicsBody` 与
  `SceneBuild.BindPhysicsBody` 将显式 body 交给节点的 `PhysicsComponent`，替换可能存在的隐式 mesh body。
- C# 在场景构建期一次性建立 **1360** 个动态死亡碎块体（800 tiny + 480 chunk + 80 boss）、
  **257** 个 kinematic 推挤体（玩家 + 256 敌人槽）和 4 面静态边界墙。死亡时复用固定池，普通敌人
  发射 8 块、boss 发射 24 块；动态 body 的节点同步由引擎现有 `PhysicsComponent` 路径完成，
  C# 不需要每帧逐碎块读取 body transform 再写回 node。
- `FRenderNodeSpec` 加 Rotation 时 ABI 从 3 升到 4；物理阶段加入固定布局的
  `FPhysicsBodyState`、`GkPhysicsMotionType`、`GkNodeMobility`，ABI 再升到 **5**。绑定表从分析时的
  68 项增至 **98** 项（第一阶段 84 + 物理阶段 14），仍只铺开实际使用的通用能力。
- 该 target 显式设置 `enableHotReload = false`、`compileManagedSources = false`。修改 C# 后由正常
  构建发布并重启应用，不为保留对象池状态引入序列化、恢复协议或半重建世界。
- 角色选择补了 C# 自持有的键盘 / 手柄焦点环；交互烟测可稳定用方向键选中 Marksman，在首波完成
  击杀并触发动态碎块路径。该烟测同时验证场景提交、物理池、drawlist UI、移动和自动武器。

当前仍不声称“全量 parity”：玩家暂用程序化球体、敌人暂用方块，伤害判定仍是确定性的 C# 数学碰撞，
物理负责推挤代理、边界和死亡表现；敌人代理形状也暂为统一盒体。ScadRig 骨骼表现、完整本地化 / 存档、
全套菜单的手柄焦点和更细的表现对齐仍留给后续阶段。至此 M1 + M2 + M3 + M4 的 API 形状已经由真实
玩法验证，下一项高价值结构工作是 M5 Rig 门面，而不是继续为了“迁移完成”扩张无调用者的绑定面。

---

## 相关文档

- [.NET Bindings](../AGENT_GUIDE/DotNetBindings.md) —— 加一个绑定的实际动作与类型规则
- [用 C# 开发应用](../AGENT_GUIDE/CSharpGameDevelopment.md) —— §10 列了当前已知缺口
- [.NET 脚本运行时架构](dotnet-scripting-design.md) —— 双后端、单一 ABI 入口、类型规则的由来
- [脚本绑定面基线（QuickJS 誊本）](script-binding-surface-baseline.md) —— 判断某项是"从未有过"还是"退化"
- [Brotato3D 代码结构梳理](../AGENT_GUIDE/Brotato3D.md) —— 对象池、场景重建窗口、kinematic hack
- [Flappy Bird Parity](../projects/flappy-bird-parity/introduction.md) —— 绑定层回归验收的既有形状
