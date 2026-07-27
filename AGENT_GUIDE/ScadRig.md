# ScadRig — SCAD 刚体骨骼角色

基于 OpenSCAD DSL 的角色建模 + 动作机制（Blockbench/Minecraft-entity 同语义：刚体部件挂骨骼层级，**无蒙皮**）。本文件是现行约定；运行时复用入口是 `NextGameplay::Sim::FScadRigVisual` / `FCharacterPool`。

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

**ScadLibrary 角色台**（顶栏应用模式，`CharacterDesigner.*`）：按分类选件 + 调
肤色/发色/主色 → 生成角色 scad → `FRigPreview` 实机预览（rig 加载 → `BeforeSceneRebuild`
注入部件模型/材质 → `OnSceneLoaded` RigInstance 实例化 + FRigAnimator 播 clip，可切动画/
绑定姿态）→ 导出 `assets/scad/characters/<名>.scad`（`use <../lib/kit_char.scad>`，游戏直接
加载）。**生命周期**：引擎顺序 BeforeSceneRebuild → OnSceneUnloaded → OnSceneLoaded，
`FRigPreview::OnSceneUnloaded` 只清 animator/节点指针，不可清注入产物（同 AirportSim 教训）。
回归脚本：`gnb validate --script assets/agentscripts/scadlibrary-designer.agentscript.json`。

## ScadLibrary 角色工作室（动作 + 装备）

ScadLibrary 顶栏把作者工具组织成三个应用级模式，每个模式拥有独立布局：

- **场景组装（Ctrl+1）**：左侧浏览 `assets/scad` 组装场景与 Kit，中间预览，右侧提供
  对象化摆放和完整 SCAD 源码编辑；
- **角色台（Ctrl+2）**：隐藏通用 kit 浏览器，扩大角色预览，右侧集中造型与颜色；
- **角色工作室（Ctrl+3）**：最大化动作预览区，并提供更宽的动作、骨架与装备编辑器。

场景组装会递归发现所有引用 `kit_*.scad` 的场景。复杂手写场景通过 ScadLoader 求值
变量、循环、条件和 module 引用，将最终 Kit 实例展开成可编辑对象；展开编辑必须另存副本，
原文件仍保留完整源码。ScadLibrary 生成的固定变换链平铺场景可无损往返对象列表。未保存
源码通过工作区副本预览，保存限制在 `assets/scad` 且禁止覆盖 `lib/`。新建通用场景写入 `assets/scad/scenes/`；
`gen/` 仍视为可被规格重新生成覆盖的产物。完整目录约定见 `assets/scad/README.md`，
回归脚本为 `assets/agentscripts/scadlibrary-assembly.agentscript.json`。

角色工作室面向已有 ScadRig 资产，默认打开
`assets/scad/characters/nextdayz_survivor.scad`，也可输入其他 `.scad` 路径：

- **动作预览**：动作选择、播放/暂停、正反向速度、时间拖动和绑定姿态预览；
- **角色工作室布局**：顶部以居中的大页签切换三个应用级模式；左侧是全高骨骼层级与
  骨骼信息，中间是带变换工具条的角色视口，右侧是动作/装备属性，动画时间轴独立贴在
  中央视口底边，切到装备页时自动收起；
- **贴底时间轴动作修改**：骨骼层级按 rig 父子关系显示并标注该骨骼的轨道数，时间轴只
  显示所选骨骼的 `pos` / `rot` / `scale` 轨道。关键帧显示为可拖动菱形；标尺和轨道空白可定位播放头，双击轨道按当前
  插值创建关键帧，选中后可精调时间与 XYZ 数值，也可删除关键帧或整条轨道。编辑值始终
  使用 SCAD 作者空间（Z-up、XYZ 角度），预览时转换为引擎空间。选择骨骼后，视口在该
  骨骼枢轴显示移动/旋转/缩放 Gizmo；拖动时暂停播放，并在当前时间自动创建或更新对应的
  `pos` / `rot` / `scale` 关键帧；
- **安全回写**：保存时把加载器求值后的全部 clip 展开到原文件末尾
  `SCADLIBRARY_RIG_EDITOR_BEGIN/END` 标记区。后续保存只替换该区域，不改骨架、几何和
  helper function；因此 `nd_stand_cycle()` 一类函数生成的动作也可编辑；
- **装备预览/修改**：任意 catalog kit 模块可挂到任意骨架，支持模块参数、启用状态及
  SCAD 本地位置/旋转/缩放的实时调整。预览仍走真实的 model/material 注入和 bone parent，
  不是独立摆在角色旁边的静态模型；
- **装备数据**：保存到角色同名 `.equipment.json`。`kit` 写相对路径，记录稳定的 `id`、
  显示名、目标骨架、模块、参数和 TRS；这是后续换装系统可直接消费/扩展的作者数据，
  当前游戏运行时是否读取它仍由各产品决定。

NextDayz 初始配置为 `characters/nextdayz_survivor.equipment.json`（军帽、背包、主武器）。
交互回归：

```bash
gnb validate --script assets/agentscripts/scadlibrary-workbench.agentscript.json
```

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

复杂角色可选择 `src/Gameplay/Rig/RigLayeredAnimator.*` 的 `FRigLayeredAnimator`；旧消费端无需迁移：

- `FRigBoneMask::FullBody` / `FromSubtree` 创建逐骨骼权重遮罩，`SetBoneWeight` 可微调 torso/head 等过渡。
- layer 按创建顺序求值，支持 `Override` 与相对绑定姿态的 `Additive`。
- `SetLoopBlend` 接收多个 `FRigClipBlendSample`；同一 `syncGroup` 在方向或 gait clip 集切换时保留
  normalized phase。
- `SetStaticBlend` 用于 aim pose，`SetManualBlend` 用权威 gameplay 时间采样交互动作。
- `PlayOneShot(..., restartIfSame=true)` 用于逐发 recoil；`IsOneShotComplete` 可查询收尾。
- 采样只覆盖 clip 实际 authored 的 position/rotation/scale 分量；未 authored 分量不会错误覆盖底层 pose。

NextDayz 是完整参考：Locomotion FullBody Override → Aim UpperBody Override → Recoil UpperBody
Additive → Action FullBody Override。资产和运行时契约见
`docs/projects/nextdayz/nextdayz-3c-scadrig-design.md`。

- 关键帧容器复用 `Model.hpp` 的 `AnimationChannel<T>`（单 key 直接返回该 key；`Sample` 为 const）。
- evaluator 侧唯一改动：`SceneEvalResult.topLevelVariables` 顶层变量快照（`anim_*` 由此读取）。
- 坐标转换与场景加载共用 `FScadShared.h`：`ScadToWorldBasis/ScadLocalToEngineTRS/ScadToWorldPos/ScadRotateXYZ`。
- 多实例去同步：`SetPhaseOffset`；行走速度匹配：`SetPlaySpeed(speed / baseSpeed)`。
- 单测：`gkNextUnitTests "[ScadRig]"`（loader）、`"[Rig]"`（animator/实例化链路，含真实资产站姿数值验证）。

## Sim Kit 与 AirportSim 接入结论

- `src/Gameplay/Sim/ScadRigVisual.*` 实现 `FScadRigVisual`：世界 Node（位置/yaw/体型微缩放/PhysicsComponent）+ rig 子树 + animator；`FCharacterPool` 统一负责注入、实例化、动画提示与 box fallback。AirportSim、StudioSim 和 CitySolSim 都走这条共享路径。
- 非 tint section 材质全池共享，tint section 每池位一份；为满足 GPU-driven primitive buffer 的定容语义，每池位注入独立 part model 拷贝。容量和开销必须以当前消费端配置及 profiler 为准。
- 生命周期顺序仍是 `BeforeSceneRebuild` 注入 → `OnSceneUnloaded` 清运行时指针 → `OnSceneLoaded` 实例化。清理时不能提前销毁已注入的 model/material 数据；`FCharacterPool::Clear()` 只重置 visual、导航、scene 指针和重新注入标志。
- AirportSim 开关是 `AirportSimConfig.hpp` 的 `kUseScadRigVisual`（默认 true），rig 路径为 `kAgentRigPath`；当前池容量由 18 名员工 + 24 名旅客组成，共 42 个 slot。
- 验收使用 `./gnb.sh shot --target AirportSim --frames 300` 检查多色角色排队、行走与在岗；性能结论必须重新采样，不沿用旧日志中的固定毫秒数。
