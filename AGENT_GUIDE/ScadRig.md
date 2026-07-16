# ScadRig — SCAD 刚体骨骼角色

基于 OpenSCAD DSL 的角色建模 + 动作机制（Blockbench/Minecraft-entity 同语义：刚体部件挂骨骼层级，**无蒙皮**）。设计文档：`docs/ScadRig-Design.md`。首个落地：AirportSim 的 `ScadRigVisual`。

## 资产格式速查（作者视角）

一个角色 = 一个合法 `.scad` 文件（原版 OpenSCAD / ScadStudio 可直接打开看绑定姿态）：

```scad
ROLECOLOR = [1, 0, 1];                      // 换色占位（纯品红），运行时按实例替换
module part_arm() { ... }                   // 非 bone_ 前缀 = helper，几何折叠进所属骨骼
module bone_arm_l() { part_arm(); }
module bone_arm_r() { mirror([1,0,0]) part_arm(); }   // mirror 必须在骨骼体内（烘进网格）
module bone_torso() {
    color(ROLECOLOR) cube(...);             // 体内直属几何 = 刚性绑定该骨骼
    translate([0,0,0.54]) bone_head();      // 调用点外层 translate/rotate = 子骨骼 pivot
}
module bone_root() { translate([0,0,0.84]) bone_torso(); ... }
bone_root();                                // 顶层恰好一个 bone_* 调用

anim_walk = [                               // clip = 顶层 anim_<name> 变量（纯数据 list）
    ["bone_leg_l", "rot", [[0,[35,0,0]], [0.4,[-35,0,0]], [0.8,[35,0,0]]]],
    ["bone_root",  "pos", [[0,[0,0,0]], [0.2,[0,0,0.03]], [0.4,[0,0,0]]]],
];
anim_sit = [ ["loop", false], ["bone_root","pos",[[0,[0,0,-0.42]]]] ];   // 单帧 = 姿态
```

规则（违反 → `SCADRIG:` 前缀 warning，加载不失败）：

1. 骨骼 = `bone_` 前缀 user module；骨骼名 = 完整模块名；每骨骼只调用一次（重复取首个）。
2. bone 调用外层只允许 `translate`/`rotate`（scale/mirror → warning）。
3. 顶层只有一个 `bone_*` 根调用；散落几何被忽略。
4. 通道 `rot`（度，OpenSCAD rotate 语义）/ `pos`（SCAD 单位）/ `scale`；key 时间单调递增，线性插值（rot 加载期转 quat 后 slerp）。`["loop", bool]` 元数据行，默认 true；duration = 最大 key 时间。
5. 合成语义：`L_final = L_bind · T(pos) · R(rot) · S(scale)`，空 clip = 绑定姿态。
6. 1 unit = 1 m，Z-up，根骨骼原点落地；引用未知骨骼的通道 → warning + 丢弃。
7. 引擎面朝 +Z = SCAD 的 −Y（鼻子/鞋尖朝 −Y 建模）。

示例资产：`assets/scad/characters/agent_basic.scad`（7 骨骼、~250 tris、idle/walk/sit/work）。

## 角色件库 kit_char（造型组装，非只换色）

`assets/scad/lib/kit_char.scad`（prefix `ch_`，scaleClass human）把 rig 角色拆成可组合部件，
新角色 = 薄 `.scad` 文件选件拼装：

- **部件**（分类 = 名字第二段，进 catalog / ScadLibrary 浏览器）：`ch_head_*` / `ch_hair_*` /
  `ch_hat_*` / `ch_torso_*` / `ch_arm_*` / `ch_leg_*` / `ch_acc_*`；整装预设 `ch_char_*`。
  部件原点 = 所属骨骼 pivot；手臂/腿以左侧建模，右侧骨骼体内 `mirror([1,0,0])`。
- **骨架标准**：`ch_pivot_torso/head/arm_l/arm_r/leg_l/leg_r()` 返回固定 pivot（与 agent_basic
  相同），因此 **clip 跨角色复用**：`anim_walk = ch_clip_walk();`（idle/walk/sit/work/wave）。
- **use 语义约束**：kit 顶层赋值会被丢弃，常量一律零参函数（`ch_TINT()` 品红换色占位、
  `ch_SKIN(i)` 等）。loader 侧零改动：`use <>` 闭包 + 函数导入本来就支持。
- **范例角色**：`characters/worker.scad`（安全帽+反光背心+背包+工具腰带+靴）、
  `characters/citizen.scad`(马尾+连衣裙)；单测 `gkNextUnitTests "[KitChar]"`。

**ScadLibrary 角色设计台**（右侧"角色台"tab，`CharacterDesigner.*`）：按分类选件 + 调
肤色/发色/主色 → 生成角色 scad → `FRigPreview` 实机预览（rig 加载 → `BeforeSceneRebuild`
注入部件模型/材质 → `OnSceneLoaded` RigInstance 实例化 + FRigAnimator 播 clip，可切动画/
绑定姿态）→ 导出 `assets/scad/characters/<名>.scad`（`use <../lib/kit_char.scad>`，游戏直接
加载）。**生命周期**：引擎顺序 BeforeSceneRebuild → OnSceneUnloaded → OnSceneLoaded，
`FRigPreview::OnSceneUnloaded` 只清 animator/节点指针，不可清注入产物（同 AirportSim 教训）。
回归脚本：`gnb validate --script assets/agentscripts/scadlibrary-designer.agentscript.json`。

## 运行时管线

```
FScadRigLoader::LoadRig(.scad)            // ScadLoader 模块（src/Modules/ScadLoader/FScadRig.*）
   → Assets::FRigAsset                    // Engine 层纯数据（src/Engine/Assets/Data/RigAsset.hpp）
      bones[]（父先序 bind TRS，引擎空间） parts[]（每骨骼分 section 的 Model + tint 标记） clips[]
FRigInstance::Instantiate(scene, asset, desc)   // NextGameplay（src/Gameplay/Rig/RigInstance.*）
   → 每骨骼一个 Node（bind TRS）+ 每 part 一个 RenderNode 挂骨骼下；返回根骨骼 Node
FRigAnimator                              // 每实例：Bind / Play(clip, fade) / SetPlaySpeed
   → Update(dt) 采样 →（可选 crossfade）→ 写骨骼 TRS（可变引用）→ root 一次 RecalcTransform(true)
```

- 关键帧容器复用 `Model.hpp` 的 `AnimationChannel<T>`（单 key 直接返回该 key；`Sample` 为 const）。
- evaluator 侧唯一改动：`SceneEvalResult.topLevelVariables` 顶层变量快照（`anim_*` 由此读取）。
- 坐标转换与场景加载共用 `FScadShared.h`：`ScadToWorldBasis/ScadLocalToEngineTRS/ScadToWorldPos/ScadRotateXYZ`。
- 多实例去同步：`SetPhaseOffset`；行走速度匹配：`SetPlaySpeed(speed / baseSpeed)`。
- 单测：`gkNextUnitTests "[ScadRig]"`（loader）、`"[Rig]"`（animator/实例化链路，含真实资产站姿数值验证）。

## AirportSim 接入纪要（§5 实施结论）

- `ScadRigVisual`（`src/Application/Game/AirportSim/ScadRigVisual.*`）：世界 Node（位置/yaw/体型微缩放/PhysicsComponent）+ rig 子树 + animator。`IAgentVisual` 增加 `SetMoveSpeed` 与 `Tick(dt)` 缺省空实现，游戏逻辑不变。
- **§5.3 材质结论**：per-node 材质可靠（MagicaLego 同机制）——非 tint section 材质全池共享、tint section 每池位一份。**model 维度走 Plan B**：GPU-driven primitive buffer 按注入 model 总三角数定容，因此每池位注入独立 part model 拷贝（42 池位 × ~250 tris，开销可忽略）。
- **生命周期陷阱**：引擎回调顺序为 `BeforeSceneRebuild`（注入）→ `OnSceneUnloaded`（→`AgentSystem::Clear`）→ `OnSceneLoaded`（实例化）。`Clear()` 只能复位 `assetsInjected_` 与玩法状态，**不可清空注入产物**（rig 资产、model/材质 id 表），否则实例化回退 box 分支且 modelId 兜底成 0（场景首个网格 → "巨大拍扁片" 显示 bug 的根因）。
- 开关：`AirportSimConfig.hpp` 的 `kUseScadRigVisual`（默认 true）；rig 路径 `kAgentRigPath`。
- 验收：`gnb shot --target AirportSim --frames 300` 可见多色角色排队/行走/在岗，28 实例 × 7 骨骼采样开销远低于 1ms。
