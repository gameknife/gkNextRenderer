---
title: "SCAD 刚体骨骼角色（ScadRig）设计与开发计划"
category: design
status: 已完成
owner: engine
created: 2026-06-12
last_updated: 2026-06-13
---

# SCAD 刚体骨骼角色（ScadRig）设计与开发计划

> 目标：给引擎增加一种**基于文本（OpenSCAD DSL）的角色建模 + 动作机制**，对标 Blockbench 制作 Minecraft entity 的工作流——刚体部件挂在骨骼层级上，**无蒙皮**。建模复用现有 SCAD loader，骨骼层级通过**模块命名标注**实现，动画 clip 以**同文件顶层 `anim_*` 变量**定义。首个落地场景：把 AirportSim 的角色代理表现从直立 box（`GeometryVisual`）换成 ScadRig 角色。
>
> 前置阅读：`AGENT_GUIDE/SCADLoader.md`、`docs/designs/scad-loader-design.md`、`docs/plans/airport-sim-mvp-plan.md`（§3.3 视觉层接口）。
>
> 状态：**已实施（Phase 0–4 完成）**。使用手册与实现纪要见 `AGENT_GUIDE/ScadRig.md`。已确认决策：动画用 in-scad `anim_*` 变量（非独立 JSON）；AirportSim 用单一角色模型 + 职业/个体换色。

---

## 1. 目标

1. **纯文本建模**：角色模型是一个合法的 `.scad` 文件，可在 ScadStudio / gkNextRenderer / 原版 OpenSCAD 中直接打开查看绑定姿态（bind pose）。骨骼标注不引入任何新语法，只是命名约定。
2. **标注式骨骼**：`bone_` 前缀的 user module = 骨骼；模块嵌套调用 = 骨骼父子层级；调用点外层 `translate/rotate` = 骨骼 pivot（绑定局部 TRS）；模块体内直属几何 = 刚性绑定该骨骼的部件。**不做蒙皮**，部件随骨骼节点整体变换（Minecraft entity 同语义）。
3. **文本动画**：动作 clip 定义在同一 `.scad` 的顶层 `anim_<name>` 变量里（纯数据 list），可用 OpenSCAD 函数 / list comprehension 程序化生成关键帧。
4. **引擎运行时**：每实例独立的 clip 播放（循环、变速、crossfade），驱动引擎 Node TRS——复用现有 Node 层级 + RenderComponent 渲染路径，零渲染层改动。
5. **AirportSim 落地**：实现 `IAgentVisual` 的新实现 `ScadRigVisual`，替换 `GeometryVisual`，28 个池位角色走 ScadRig 表现（Idle/Walk/Sit/Work 四个 clip + 职业换色）。

非目标（v1 不做）：蒙皮/ozz 骨骼动画、IK、动画事件、动画混合树（仅两 clip crossfade）、Blockbench 文件导入。

---

## 2. 现有可复用基础（已盘点确认）

| 设施 | 位置 | 复用方式 |
| --- | --- | --- |
| SCAD scene graph 求值 | `src/Modules/ScadLoader/FScadEvaluator.{h,cpp}` `EvaluateScene()` | **核心复用点**。每个 user module 调用已生成一个 `scad::SceneNode{ name=模块名, instanceId, localTransform=调用点外层累计变换, meshes(按色分桶), children }`（见 `CallUserModule`，L921：`CreateSceneNode(def.name, xform)`）。骨骼识别 = 在这棵树上按名字前缀打标 |
| SCAD→引擎 TRS 转换 | `src/Modules/ScadLoader/FScadLoader.cpp` `ScadToWorldBasis` / `ScadLocalToEngineTRS`（约 L238–L274） | Z-up→Y-up 基变换 B 共轭 + `glm::decompose`，骨骼 pivot 与动画 rot/pos 通道用**同一套转换**，必须提为可复用函数 |
| 网格组装（焊接/平滑/分桶→Model） | `FScadLoader.cpp` `AttachSceneMeshesToNode` 及其上游 | 部件三角汤→`Assets::Model` 完整复用 |
| 关键帧容器与采样 | `src/Engine/Assets/Core/Model.hpp` `AnimationKey<T>` / `AnimationChannel<T>::Sample` | clip 通道直接复用这两个模板。**注意**：不复用 `Scene::tracks_` 全局 track 系统（按名字全场景查找、全局 ping-pong 播放、递归推物理，见 `Scene.Update.cpp` L64–L130，不适合 28 个独立实例） |
| Node 层级 | `src/Engine/Assets/Core/Node.h/.cpp` | `SetParent/AddChild`；`RecalcTransform(true)` 递归子树（已确认 `Node.cpp` L88–L110）。`Translation()/Rotation()/Scale()` 返回可变引用——animator 批量写完所有骨骼 TRS 后，root 一次 `RecalcTransform(true)`，避免每骨骼 `Set*` 触发三次子树递归 |
| 运行时注入模型/节点 | `src/Application/Game/AirportSim/AgentSystem.cpp` `InjectAssets`（BeforeSceneRebuild 注入 models/materials）+ `OnSceneLoaded`（`SceneBuilder::CreateRenderNode` + `scene.AddNode`） | 实例化流程沿用此模式 |
| 视觉层接口 | `src/Application/Game/AirportSim/AgentSystem.h` `IAgentVisual`（SetWorldTransform / SetAnimHint / SetVisible） | 游戏逻辑零改动的替换面；`EAgentAnimHint`: Idle/Walk/Sit/Work（`AirportSimTypes.h` L105） |
| 锚点=具名模块惯例 | `assets/scad/airport.scad` 头注释 | "节点名=module 名、外层 translate=点位" 已是仓库惯例，骨骼标注与其同构，作者无新概念负担 |
| 模块注册 | `src/Modules/ScadLoader/ScadModule.{hpp,cpp}` | AirportSim 已调用 `Modules::Scad::Register()` 且已链接 `ScadLoader`（`src/CMakeLists.txt` L628–636） |

依赖关系约束：`ScadLoader`（Modules 静态库）只依赖 `gkNextEngine`；`NextGameplay` 同样只依赖 `gkNextEngine`。因此**与格式无关的 rig 资产类型必须放 Engine 层**（`src/Engine/Assets/Data/`，已有 `Skeleton.hpp` 先例），ScadLoader 负责生产、NextGameplay 负责消费，两者互不依赖。

---

## 3. DSL 规范（作者视角）

### 3.1 骨骼标注

```scad
// agent_basic.scad —— 合法 OpenSCAD，原版/ScadStudio 直接渲染绑定姿态
ROLECOLOR = [1, 0, 1];            // 换色占位色（§3.4）

module part_arm() {               // 普通 helper（非 bone_ 前缀）：几何折叠进所属骨骼
    color(ROLECOLOR) translate([0, 0, -0.26]) cube([0.10, 0.10, 0.26], center = true);
}

module bone_head() {
    color([0.92, 0.76, 0.62]) translate([0, 0, 0.10]) cube(0.22, center = true);
}
module bone_arm_l() { part_arm(); }
module bone_arm_r() { mirror([1, 0, 0]) part_arm(); }   // mirror 在骨骼体内：烘进网格，安全
module bone_leg_l() { color([0.25, 0.25, 0.30]) translate([0, 0, -0.38]) cube([0.12, 0.12, 0.76], center = true); }
module bone_leg_r() { bone_leg_l_geom(); /* 同上，略 */ }

module bone_torso() {
    color(ROLECOLOR) translate([0, 0, 0.26]) cube([0.34, 0.20, 0.52], center = true);
    translate([0, 0, 0.55]) bone_head();                 // 子骨骼：外层 translate = pivot
    translate([-0.22, 0, 0.50]) bone_arm_l();
    translate([ 0.22, 0, 0.50]) bone_arm_r();
}

module bone_root() {                                     // 根骨骼（盆骨），原点=两脚间地面投影
    translate([0, 0, 0.84]) bone_torso();
    translate([-0.10, 0, 0.76]) bone_leg_l();
    translate([ 0.10, 0, 0.76]) bone_leg_r();
}

bone_root();                                             // 顶层恰好调用一次根骨骼
```

规则（rig loader 按此校验，违反给 `SCADRIG:` 前缀 warning）：

1. 骨骼 = 名字以 `bone_` 开头的 user module。**骨骼名 = 完整模块名**（含前缀），动画通道按此引用。
2. 每个骨骼模块在 rig 内**只调用一次**（左右肢体写两个模块，共享几何走普通 helper 模块 + `mirror`，mirror 烘进网格，不上节点）。重复调用 → warning，取首个。
3. 父子关系 = 模块体内调用；顶层有且仅有一个 `bone_*` 调用作为根。顶层散落的非骨骼几何 → warning + 忽略。
4. pivot：调用点**外层只允许 `translate` / `rotate`**（绑定局部 TRS）。`scale/mirror/multmatrix` 出现在 bone 调用外层 → warning（`glm::decompose` 对负 scale/shear 不可靠）。
5. 骨骼体内直属几何（含非 bone helper 模块展开的几何）刚性绑定该骨骼，坐标相对 pivot。实现上：rig loader 把 SceneNode 树中**非 `bone_` 节点折叠**——其 localTransform 烘进三角形、meshes 合入最近的 bone 祖先。
6. 角色尺度：1 SCAD unit = 1 m（与 airport.scad 一致），根骨骼原点落地（z=0）。

### 3.2 动画 clip：顶层 `anim_*` 变量

```scad
// clip 名 = 变量名去掉 anim_ 前缀；时间单位秒；duration = 最大 key 时间；默认 loop
anim_walk = [
    ["bone_leg_l", "rot", [[0, [ 35, 0, 0]], [0.4, [-35, 0, 0]], [0.8, [ 35, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-35, 0, 0]], [0.4, [ 35, 0, 0]], [0.8, [-35, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-30, 0, 0]], [0.4, [ 30, 0, 0]], [0.8, [-30, 0, 0]]]],
    ["bone_arm_r", "rot", [[0, [ 30, 0, 0]], [0.4, [-30, 0, 0]], [0.8, [ 30, 0, 0]]]],
    ["bone_root",  "pos", [[0, [0, 0, 0]], [0.2, [0, 0, 0.03]], [0.4, [0, 0, 0]],
                           [0.6, [0, 0, 0.03]], [0.8, [0, 0, 0]]]],
];

anim_sit = [
    ["loop", false],                                    // 可选元数据行
    ["bone_root",  "pos", [[0, [0, 0, -0.42]]]],        // 单帧 = 姿态
    ["bone_leg_l", "rot", [[0, [-90, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-90, 0, 0]]]],
];

// 程序化关键帧：复用 OpenSCAD 函数能力（这正是 in-scad 格式的核心收益）
anim_idle = [
    ["bone_torso", "rot", [for (t = [0 : 0.25 : 2]) [t, [2 * sin(180 * t), 0, 0]]]],
];
```

格式规范：

- 通道行 = `[boneName, channel, keys]`；`channel ∈ "rot" | "pos" | "scale"`。
- `"rot"`：度，OpenSCAD `rotate([x,y,z])` 语义（X→Y→Z 顺序），SCAD Z-up 空间；`"pos"`：SCAD 单位、骨骼 pivot 局部空间；`"scale"`：无量纲。
- key = `[time, [x,y,z]]`，time 单调递增；线性插值（rot 加载期转 quat 后 slerp）。
- 元数据行 = `["loop", bool]`（默认 true）。duration = 全 clip 最大 key 时间。
- **合成语义（Blockbench 同款，绕 pivot）**：`L_final = L_bind · T(pos) · R(rot) · S(scale)`，即动画是绑定姿态之上的局部偏移，空 clip = 绑定姿态。
- 引用不存在的骨骼 → warning + 丢弃该通道（不 fail，便于迭代）。

### 3.3 坐标转换

骨骼 pivot、`"rot"`、`"pos"` 与几何走同一基变换 `B = ScadToWorldBasis(scale)`（Z-up→Y-up，纯旋转）：pivot/旋转用共轭 `M_engine = B · M_scad · B⁻¹`（现成的 `ScadLocalToEngineTRS`）；`"pos"` 向量直接 `(x, z, -y) * scale`。**加载期全部转完**，运行时数据已在引擎空间。

### 3.4 换色占位（tint）

约定占位色 `ROLECOLOR = [1, 0, 1]`（纯品红）。rig loader 对 quantized color 命中该值的颜色桶打 `tintable` 标记；运行时按实例把 tintable section 的材质替换为职业/个体色。占位色可经 `ScadRigLoadOptions::tintPlaceholder` 覆盖。

---

## 4. 数据结构与文件结构

### 4.1 新增文件

```
src/Engine/Assets/Data/RigAsset.hpp          # 格式无关 rig 资产（Engine 层，仅数据）
src/Modules/ScadLoader/FScadRig.h/.cpp       # FScadRigLoader：.scad → FRigAsset
src/Gameplay/Rig/RigInstance.h/.cpp   # 实例化（建 Node 树）+ FRigAnimator（clip 播放）
src/Application/Game/AirportSim/ScadRigVisual.h/.cpp  # IAgentVisual 适配层
assets/scad/characters/agent_basic.scad      # 角色资产（模型 + 4 clips）
src/Tests/Test_ScadRig.cpp                   # 单测
```

模块归属已被 CMake glob 自动收录（`src/cmake/SourceFiles.cmake` 的 `GK_MODULE_NAMES` 含 ScadLoader；NextGameplay 库已有 `add_library`），新文件无需改 CMake，但新增 `assets/scad/characters/` 目录需确认 `assets/CMakeLists.txt` 的 scad 拷贝是否递归。

### 4.2 RigAsset.hpp（Engine 层）

```cpp
namespace Assets
{
    struct FRigBone
    {
        std::string name;                 // "bone_xxx"
        int32_t parent = -1;              // 索引父骨骼，root = -1
        glm::vec3 bindT{0.0f};            // 绑定局部 TRS（引擎空间）
        glm::quat bindR{1, 0, 0, 0};
        glm::vec3 bindS{1.0f};
        std::vector<int32_t> children;
    };

    struct FRigPart                       // 一个骨骼的一段可渲染几何
    {
        int32_t bone = -1;
        int32_t modelIndex = -1;          // 指向 FRigAsset::partModels
        std::vector<glm::vec4> sectionColors;   // 每 section 烘焙色
        std::vector<bool> sectionTintable;      // 命中占位色的 section
    };

    struct FRigChannel
    {
        int32_t bone = -1;
        AnimationChannel<glm::vec3> position;   // 复用 Model.hpp 模板
        AnimationChannel<glm::quat> rotation;   // 加载期 euler→quat
        AnimationChannel<glm::vec3> scale;
    };

    struct FRigClip
    {
        std::string name;
        float duration = 0.0f;
        bool loop = true;
        std::vector<FRigChannel> channels;
    };

    struct FRigAsset
    {
        std::vector<FRigBone> bones;      // bones[0] = root，父先序
        std::vector<Model> partModels;    // 注入场景前的暂存
        std::vector<FRigPart> parts;
        std::vector<FRigClip> clips;
        int32_t FindBone(std::string_view name) const;
        const FRigClip* FindClip(std::string_view name) const;
    };
}
```

### 4.3 FScadRigLoader（ScadLoader 模块）

```cpp
struct ScadRigLoadOptions
{
    float scadToWorldScale = 1.0f;
    float smoothAngleDegrees = 35.0f;
    glm::vec4 tintPlaceholder { 1.0f, 0.0f, 1.0f, 1.0f };
    std::string bonePrefix = "bone_";
    std::string animPrefix = "anim_";
};

class FScadRigLoader
{
public:
    static bool LoadRig(const std::string& filename, const ScadRigLoadOptions& options,
                        Assets::FRigAsset& outAsset, std::string& outError);
};
```

流程：`ExtractDirectives → Lexer → Parser`（全复用）→ `EvaluateScene`（复用，需新增**顶层变量导出**，见下）→ 在 `SceneEvalResult.roots` 上：识别 `bone_` 节点 → 折叠非骨骼节点（变换烘进三角形，meshes 按色合并入最近 bone 祖先）→ 每骨骼 meshes 走现有焊接/平滑管线生成 `Model`（直接生成引擎空间顶点，部件局部 = pivot 局部）→ pivot 经 `ScadLocalToEngineTRS` → 解析 `anim_*` 变量为 clips。

**Evaluator 唯一改动**：`SceneEvalResult` 增加 `std::map<std::string, scad::Value> topLevelVariables;`，`EvaluateScene` 在顶层 scope 求值完成后捕获最终绑定（实现提示：顶层 `Context` 帧在 `EvalScope(mainTopLevel)` 返回前 snapshot 即可；只在 scene 模式开启，对现有路径零影响）。

### 4.4 RigInstance + FRigAnimator（NextGameplay）

```cpp
namespace NextGameplay
{
    struct FRigInstanceDesc
    {
        std::string namePrefix;                      // "agent_03"
        std::vector<uint32_t> partModelIds;          // 注入后的全局 model id（与 asset.parts 对位）
        std::vector<std::array<uint32_t, 16>> partMaterialIds; // 每 part 的 section→材质映射（含 tint 替换）
    };

    class FRigInstance
    {
    public:
        // 为资产建一棵 Node 树挂到 scene：每骨骼一个 Node（绑定 TRS），part 经
        // SceneBuilder::CreateRenderNode 挂到对应骨骼 Node 下。返回 root。
        static std::shared_ptr<Assets::Node> Instantiate(
            Assets::Scene& scene, const Assets::FRigAsset& asset, const FRigInstanceDesc& desc,
            std::vector<Assets::Node*>& outBoneNodes);   // 与 asset.bones 对位
    };

    class FRigAnimator
    {
    public:
        void Bind(const Assets::FRigAsset* asset, std::vector<Assets::Node*> boneNodes,
                  Assets::Node* root);
        void Play(std::string_view clip, float fadeSeconds = 0.15f);  // 同名重入 = no-op
        void SetPlaySpeed(float speed);
        void SetPhaseOffset(float seconds);          // 实例去同步
        void Update(float deltaSeconds);             // 采样 → 写骨骼 TRS（可变引用）→ root->RecalcTransform(true) 一次
    };
}
```

播放语义：当前 clip + 上一 clip 线性 crossfade（pos/scale lerp、rot slerp，权重 smoothstep）；loop clip wrap，非 loop 停在末帧；未覆盖通道的骨骼回绑定姿态（即混合目标缺省 = bind）。

---

## 5. AirportSim 集成方案

### 5.1 替换点（游戏逻辑零改动）

```
IAgentVisual
 ├── GeometryVisual   // 保留为 fallback（AirportSimConfig.hpp: kUseScadRigVisual=true 可关）
 └── ScadRigVisual    // 新：FRigAnimator + root Node
```

- `IAgentVisual` 增加 `virtual void Tick(float deltaSeconds) {}`（GeometryVisual 不实现）。`AgentSystem::Tick` 末尾对 active agent 调 `visual->Tick(dt)`（`scene.MarkDirty()` 已有）。
- `SetAnimHint` 映射：Idle→`idle`、Walk→`walk`、Sit→`sit`、Work→`work`；缺 clip 回退 `idle`；Walk 时 `SetPlaySpeed(agent.speed / Config::kBaseWalkSpeed)`。Sit 不再用压扁 hack。
- `SetWorldTransform`：写 root 节点 pos + yaw（`angleAxis(yaw, Y)`），与现实现一致；`SetVisible(false)` 沿用挪到 `kParkedPos` 地下。
- 相位去同步：spawn 时 `SetPhaseOffset(hash(agent.id))`，避免 28 人步伐整齐划一。

### 5.2 AgentSystem 改造

- `InjectAssets`（BeforeSceneRebuild）：`FScadRigLoader::LoadRig("assets/scad/characters/agent_basic.scad")`（CPU 解析，预算 <100ms）→ `asset.partModels` 全部 push 进 `models` 记录全局 id（**所有池位共享 part Model**）；材质：非 tint section 共享一份，tint section 每池位 `AddLambertianMaterial(职业/调色板色)`（沿用现有 `kStaffRoster[i].color` / `kPassengerPalette` 逻辑）。
- `OnSceneLoaded`：每池位 `FRigInstance::Instantiate`（namePrefix=`agent_%02d`，传入该池位的 partMaterialIds）→ root 挂 `PhysicsComponent(Dynamic)`（与现 box 一致，骨骼子节点不挂）→ `ScadRigVisual` 持 root + animator。
- 体型微缩放（§3.2 旅客个体差异）：root 节点 `SetScale(uniform 0.95~1.05)`。

### 5.3 共享 Model + per-node 材质的风险验证（Phase 4 第一步）

`AgentSystem.cpp` L61 注释称"共享 model 改 per-node 材质不可靠"，但 `RenderComponent` 有 per-node `materialIdx_[16]`（`RenderComponent.h` L28）且 FScadLoader 已按节点 `SetMaterials`。实施时**先做最小实验**：两个节点共享同一 box model、`SetMaterials` 指向不同材质，`gnb shot` 确认双色。若不可靠（GPU-driven 路径按 model 绑材质），回退 Plan B：**tintable part 的 Model 按池位复制**（部件均为数十~百级三角形，28 份开销可忽略），非 tint part 仍共享。结论写回本文档。

> **实施结论（2026-06）**：材质维度 Plan A 成立——per-node `materialIdx` 可靠（MagicaLego 即此机制），非 tint section 材质全池共享、tint section 每池位一份。**model 维度采用 Plan B 的变体**：GPU-driven primitive buffer 按注入 model 总三角数定容，为保证容量覆盖全部实例，每池位注入独立 part model 拷贝（成本 ~250 tris × 42 池位，可忽略）。另一实施纪要：引擎回调顺序是 `BeforeSceneRebuild` → `OnSceneUnloaded` → `OnSceneLoaded`，`AgentSystem::Clear()`（挂在 unload）不可清空注入产物，详见 `AGENT_GUIDE/ScadRig.md`。

---

## 6. 开发计划

> 构建验证按 AGENTS.md targeted-build 规则；每 phase 完成需单测绿 + 注明的视觉验收。预估总量 5~6d。

### Phase 0 — 资产类型 + evaluator 变量导出（0.5d）
- `RigAsset.hpp`；`SceneEvalResult.topLevelVariables` 捕获 + `[Scad]` 单测（数值/向量/嵌套 list、函数生成的 list）。
- 验证：`./gnb build gkNextRenderer gkNextUnitTests`。

### Phase 1 — FScadRigLoader（1.5d）
- bone 节点识别、非骨骼节点折叠、pivot TRS 转换、part→Model（复用焊接/平滑）、tint 标记、`anim_*` 解析（rot/pos/scale、loop 行、euler→quat、坐标转换）、全部 warning 路径。
- `Test_ScadRig.cpp`：内联 scad 字符串覆盖——骨骼树形/pivot 数值（含 rotate 外层）、helper 折叠、重复骨骼/顶层散几何/未知骨骼通道 warning、clip duration/loop、程序化关键帧（list comprehension）。
- 验证：`gkNextUnitTests "[ScadRig]"`。

### Phase 2 — RigInstance + FRigAnimator（1d）
- 实例化 + 播放（loop/非 loop、crossfade、playSpeed、phase offset、缺省回 bind）。
- 单测：固定 asset 采样确定性、wrap 边界、fade 权重；改 NextGameplay 连带 `./gnb build CharacterDemo gkNextUnitTests`。

### Phase 3 — agent_basic.scad 资产（1d）
- 7 骨骼（root/torso/head/arm_l/arm_r/leg_l/leg_r），身高 ~1.7m、<500 tris、ROLECOLOR 占位躯干+四肢上段。
- 4 clips：`idle`（2s 呼吸+微摆臂）、`walk`（0.8s 腿±35° 臂反相 + root bob）、`sit`（单帧非 loop：root 下沉 0.42、大腿 -90°）、`work`（1.5s 手臂小幅动作）。
- 验收：`gnb shot --target ScadStudio --scene assets/scad/characters/agent_basic.scad` 绑定姿态目检 0 warning；原版 OpenSCAD 可打开（如有环境）。

### Phase 4 — AirportSim 接入（1d）
- §5.3 材质共享实验 → 定 Plan A/B；`ScadRigVisual` + `IAgentVisual::Tick` + AgentSystem 改造 + `kUseScadRigVisual` 开关。
- 验收：`./gnb build AirportSim` + `gnb shot --target AirportSim --frames 300` 截图见多色角色行走/坐姿；日志 `uploaded scene`；帧耗时与 box 版相比无明显回退（28 实例 × 7 骨骼采样应 «1ms）。
- 收尾：更新 `docs/plans/airport-sim-mvp-plan.md` §3.3 状态、新建 `AGENT_GUIDE/ScadRig.md`（使用手册 + 实现纪要，含 §5.3 结论）。

### Phase 5 — 可选增强（按需，不阻塞）
- ScadStudio：大纲显示骨骼树/clip 列表、clip 预览播放条。
- QuickJS/TS 绑定（脚本游戏复用 rig）。
- StudioSim 员工几何体迁移；动画事件 / 简单两层混合（上下半身）。

---

## 7. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| bone 调用外层含 scale/mirror → decompose 错误 | DSL 规则 4 禁止 + loader warning；mirror 引导进骨骼体内烘网格 |
| 共享 Model per-node 材质不可靠（历史注释） | §5.3 先实验后定案，Plan B 成本可忽略 |
| Euler 关键帧插值万向锁 | 加载期逐 key 转 quat，运行时只 slerp |
| 多实例 Node 名重复（`Scene::GetNode` 取首个） | animator 持 `Node*` 直接驱动，不走名字查找；节点名仅调试用，加 `agent_%02d/` 前缀 |
| `anim_*` 变量被模块体内同名变量干扰 | 只捕获**顶层 scope** 最终绑定，动态作用域不影响顶层快照 |
| 角色 scad 与场景 scad 的 scale 混淆 | rig 用独立 `ScadRigLoadOptions::scadToWorldScale`（默认 1），与 CVar `sys.scadToWorldScale` 解耦 |

## 8. 关键参考速查

- 求值器 scene graph：`FScadEvaluator.cpp` `CallUserModule` / `CreateSceneNode` / `EmitSceneGeometry`
- TRS 转换：`FScadLoader.cpp` `ScadToWorldBasis` / `ScadLocalToEngineTRS`
- 关键帧模板：`Model.hpp` `AnimationKey/AnimationChannel`
- 替换面：`AgentSystem.h` `IAgentVisual`；注入/实例化范式：`AgentSystem.cpp` `InjectAssets/OnSceneLoaded`
- 节点递归刷新：`Node.cpp` `RecalcTransform(bool full)`
- 既有文档：`AGENT_GUIDE/SCADLoader.md`、`docs/plans/airport-sim-mvp-plan.md` §3.3、`AGENT_GUIDE/CharacterDemo.md`（动画状态机命名参考，本方案不复用其蒙皮路径）
