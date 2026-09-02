# NextAstrobot — AstroBot 风格 3D 平台跳跃

`NextAstrobot` 是原生 C++ 子应用（目标 `NextAstrobot`，源码 `src/Application/Game/NextAstrobot/`），
把 `kit_astro` 零件库做成一趟可完整游玩的流程：机器人从出生岛出发，跑、跳、悬浮、拳击，踩着
移动平台、摆锤、跷跷板、旋转盘、传送带、滑索穿过悬浮岛，吃金币、捡拼图、救出被困机器人，抵达
终点门结算，然后接着打下一关。

现有三关，顺序由 `assets/configs/nextastrobot/levels.json` 决定：

| # | id | 名字 | 场景 | 教什么 |
| --- | --- | --- | --- | --- |
| 1 | `sky_garden` | 星尘花园 | `assets/scad/source/astro/sky_garden.scad` | 平台机关：移动平台、摆锤、跷跷板、易碎石板、旋转盘、弹跳垫、滑索 |
| 2 | `dune_relay` | 落日沙洲 | `assets/scad/source/astro/dune_relay.scad` | 非 ★ 活动件：风扇顺风、双激光节奏、喷泉托举、拉杆开门、摆动刺球 |
| 3 | `frost_peak` | 极光冰峰 | `assets/scad/source/astro/frost_peak.scad` | 冰雪全要素与大结局：雪原浮岛、冰晶簇、雪松、冰泉托举、高塔牢笼救援与高空超长滑索 |

本文是**现行契约**。改机关、改关卡、加关卡之前先读这里。

## 一句话架构

**`.scad` 就是关卡，场景节点就是运行时对象。** 没有第二份 JSON 关卡表：金币、机关、敌人、
危险、检查点都是 kit module 调用，引擎把每个调用变成一个具名 `Assets::Node`，游戏在
`OnSceneLoaded` 扫一遍 `scene.Nodes()` 建索引，然后直接驱动这些节点的 transform 与可见性。

```text
sky_garden.scad ──ScadLoader──> Node 树（+ metadata + 隐式静态碰撞）
                                   │
                              FLevelIndex（按模块名分桶）
        ┌──────────────┬───────────┼────────────┬─────────────┐
   MechanismSystem  Collectible  Hazard      Enemy      Interactable
   （活动件 TRS +    （金币/拼图/  （岩浆/水/  （巡逻/悬停/  （木箱/积木墙/
     kinematic box）  宝石/金星）   激光/刺球）  踩踏/拳击）    牢笼/被困机器人/
                                                            检查点/终点）
                                   │
          FPlayerController（3C）─ FPlayerRigVisual ─ FFollowCamera ─ FLevelFlow ─ AstroHud
```

## 目录

```text
src/Application/Game/NextAstrobot/
├── NextAstrobotGameInstance.{hpp,cpp}   生命周期、帧顺序、输入、相机覆盖、CVar、agent queries
├── NextAstrobotConfig.{hpp,cpp}         gameplay.json / levels.json → FConfig
├── AstroAudio.hpp                       音效单入口（当前借用 flappy 占位音）
├── Level/LevelIndex.{hpp,cpp}           FLevelIndex：扫场景，按模块名分桶
├── Level/LevelFlow.{hpp,cpp}            Title/Intro/Playing/Dead/Goal/Result/Paused（纯逻辑，可单测）
├── Mechanisms/MechanismCurves.hpp       PingPong01 / Swing / Approach / Damp（纯函数，可单测）
├── Mechanisms/MechanismSystem.{hpp,cpp} 活动件驱动 + kinematic body + 表面速度 / 发射 / 滑索
├── Player/PlayerController.{hpp,cpp}    跑/跳/悬浮/拳击/踩踏/滑索/死亡复活
├── Player/PlayerRigVisual.{hpp,cpp}     astro_bot.scad rig 注入 / 实例化 / clip 选择
├── Player/FollowCamera.{hpp,cpp}        跟随相机 + FLevelCameras（标题机位与入场飞越）
├── World/{Collectible,Hazard,Enemy,Interactable}System.{hpp,cpp}
├── World/RescueRigVisual.{hpp,cpp}      被救机器人的 rig 实例池（复用主角 rig 资产）
└── UI/AstroHud.{hpp,cpp}                HUD 与标题 / 暂停 / 结算画面
assets/configs/nextastrobot/{gameplay.json, levels.json}
assets/scad/source/astro/{sky_garden,dune_relay,frost_peak}.scad   三关
assets/scad/characters/astro_bot.scad    主角 rig（8 骨骼，11 个 clip）
assets/agentscripts/nextastrobot-*.agentscript.json
```

## kit_astro 活动件契约

写在 `assets/scad/lib/kit_astro.scad` 文件头，和放置契约并列。要点：

1. 可动机关 = `gk_flatten() { 静态壳 }` + 若干 `ab_part_<机关>_<件>()` 活动件调用。
   **活动件调用不得放进 `gk_flatten`**，否则会折进父节点，运行时就找不到它了。
2. 活动件模块自身的几何整体包在 `gk_flatten()` 里：一件 = 一个 Node = 一个 Model。
3. 活动件局部原点 = 运动基准（转轴 / 行程基准 / 落位）。父模块用 `translate/rotate` 摆到绑定
   姿态；运行时**只改活动件节点的局部 TRS**，绑定姿态就是 t=0 的姿态。
4. 玩法参数是模块的具名参数（`period` 秒 / `phase` 0..1 / `speed` m/s 或 °/s / `amp` ° /
   `idx` 序号 / `locked` 布尔），即使几何不用也要声明——ScadLoader 会把它们写进
   `Node::metadata`，运行时从那里读。
5. 旧签名向后兼容：`ab_plat_moving` 的 `t`、`ab_plat_pendulum` 的 `ang`、`ab_plat_seesaw` 的
   `tilt` 仍是绑定姿态；新增参数一律带默认值。
6. `ab_part_roof` / `ab_part_hazard` 是**几何 helper**，不是活动件。运行时按具体件名索引
   （`LevelIndex.cpp` 的 `kMechanismSpecs` 表），不靠 `ab_part_*` 前缀通配。

当前 21 个机关及其活动件（前 13 个是玩法机关，后 8 个是「非 ★ 活动件」：
不是平台，但同样是 `ab_part_*` + 运行时驱动）：

| 机关 | 活动件 | 运行时 | 碰撞 |
| --- | --- | --- | --- |
| `ab_plat_moving` | `ab_part_moving_car` | 局部 x 在轨道内往返，端点缓动 | kinematic box |
| `ab_plat_pendulum` | `ab_part_pendulum_arm` | 绕 SCAD y 轴 `ang·sin` 摆动 | kinematic box（臂端） |
| `ab_plat_seesaw` | `ab_part_seesaw_plank` | 按玩家在板上的 x 侧倾斜，无人回 0 | kinematic box |
| `ab_plat_crumble` | `ab_part_crumble_slab` | 踩 `warn` 秒后抖动下坠隐藏，`respawn` 秒复位 | kinematic box（塌落后 deactivate） |
| `ab_prop_gate_bars` | `ab_part_gate_grid` | 同 `idx` 按钮触发后升起 | kinematic box |
| `ab_prop_cage` | `ab_part_cage_dome` | 拳击后穹顶升 1.5 m | 无（栏杆是静态壳） |
| `ab_prop_button` | `ab_part_button_cap` | 踩下压 0.15 m 并锁存 | 无（底座是静态壳） |
| `ab_plat_spring` | `ab_part_spring_cap` | 触发后压缩再弹回（纯视觉） | 无 |
| `ab_plat_zipline` | `ab_part_zipline_car` | 滑车跟随骑乘者 | 无 |
| `ab_plat_roller` | `ab_part_roller_drum` | 绕 x 自转，脚下给 SCAD −y 表面速度 | 隐式静态（圆筒对称） |
| `ab_plat_spin` | 无 | 盘节点绕 z 转，脚下给 ω×r 表面速度 | 隐式静态 |
| `ab_plat_conveyor` | 无 | 脚下给 +x 表面速度 | 隐式静态 |
| `ab_plat_bounce` | 无 | 脚底在半径内且着地 → 竖直发射 `launch` m | 隐式静态 |
| `ab_prop_spike_ball_chain` | `ab_part_spike_ball` | 绕吊钩 `ang·sin` 摆动；**致命判定跟着这个节点走**，不是模块原点 | 无 |
| `ab_prop_laser` | `ab_part_laser_beam` | 按 `period`/`duty`/`phase` 整根隐藏／显示；**不可见时不致命** | 无 |
| `ab_prop_fan` | `ab_part_fan_blades` | 叶片绕 SCAD y 自转；沿 front 方向 `range` 米的圆柱风区给 `power` m/s 风速 | 无（护圈是静态壳） |
| `ab_prop_fountain_jet` | `ab_part_fountain_column` | 水柱按 `period` 沿 z 缩放涨落；柱内玩家上升 `lift` m/s | 无（池座是静态壳） |
| `ab_bldg_windmill` | `ab_part_windmill_blades` | 绕 SCAD y 自转（`speed`），纯装饰 | 无 |
| `ab_prop_chest` | `ab_part_chest_lid` | 拳击后盖子绕 SCAD x 掀开 −55°，**箱子不消失** | 无 |
| `ab_prop_lever` | `ab_part_lever_arm` | 拳击后绕 SCAD y 扳过去，并触发同 `idx` 的栅栏门 | 无 |
| `ab_prop_checkpoint` | `ab_part_checkpoint_flag` | 踩到后旗子从杆脚升到 `h−0.1` | 无 |

**一个模块可以同时是好几样东西。** `FLevelIndex::Build` 的机关表先跑、并且**故意不 continue**：
牢笼既是机关又是救援点，宝箱既是机关又是可打碎物，激光和刺球既是机关又是危险物，检查点既是
机关又是复活点。加新件时如果它也属于别的桶，就照这个模式来，不要在机关分支里 `continue`。

风区与水柱这两条**改的是玩家的速度，不是位置**：`FMechanismEffects::wind`（m/s，折进
`PlayerController` 的水平目标速度里，所以玩家能顶着风走但走不赢比自己跑速快的风）和
`liftSpeed`（m/s，给竖直速度设下限）。风若按力（m/s²）实现会被跑步阻尼当帧吃掉，形同没有。

### 一个 kit 对象 = 一棵子树

ScadLoader 把**每一次 user module 调用**都变成一个 Node，而 kit 的材质包装（`ab_gold` /
`ab_plastic` / `ab_gloss` / `ab_metal` …）本身就是 module。于是 `LevelIndex` 按模块名索引到的
那个节点，**经常自己一个三角形都不画**：

| 模块 | 几何在哪 |
| --- | --- |
| `ab_prop_crate`、`ab_bldg_wall_break`、`ab_item_puzzle` / `_key` / `_star`、`ab_char_enemy_*`、`ab_char_bot_lost` | 全在子节点，根节点没有 RenderComponent |
| `ab_item_coin`、`ab_item_gem` | 拆开：builtin `gk_material` 的部分留在根，`ab_gold()` 的部分在子节点 |
| 所有 `ab_part_*`（自身包了 `gk_flatten`） | 就在根节点 |

所以隐藏、显示或测量一个 kit 对象的代码一律走 `Assets::NodeUtils::SetVisibleRecursive` /
`SetRayCastVisibleRecursive` / `GetSubtreeWorldBounds`，不要自己写
`node->GetComponent<RenderComponent>()->SetVisible()`。只碰根节点会得到三类症状：打碎的箱子
还立在原地并继续挡路、吃掉的收集物留下静止残影、拾取判定落在锚点上（比看到的东西低约一米，
`hover` 参数造成）。`Test_NextAstrobot.cpp` 的 "Astro kit props keep their geometry in child
nodes" 钉住了这个形状，加新 kit 件时它会告诉你几何落在哪一层。

### 碰撞策略

| 类别 | 碰撞来源 | 谁负责 |
| --- | --- | --- |
| 岛屿、平台、建筑、静态道具 | 加载时隐式静态 mesh body（`Scene::EnsureNodePhysicsBody`） | 引擎 |
| 位移类活动件 | 游戏建 kinematic box → `Scene::BindPhysicsBody`（自动移除隐式体）→ 每帧 `MoveKinematicBody` | MechanismSystem |
| 旋转对称件 | 隐式静态体 + 按脚底位置附加表面速度 | MechanismSystem → PlayerController |
| 收集物、敌人、危险视觉、装饰、无碰撞活动件 | 无：`BeforeSceneRebuild` 里 `SetRayCastVisibleRecursive(false)` | GameInstance |
| 主角 | Jolt `CharacterVirtual`（高 1.5 m、半径 0.35 m、台阶 0.55 m、坡 50°） | PlayerController |

**无碰撞名单**在 `NextAstrobotGameInstance.cpp` 顶部（`kNoCollisionModules` /
`kNoCollisionParts` / `kNoCollisionDecor`）。加了新的收集物或装饰件就往那里加一行。

## 相机弹簧臂

`FFollowCamera::Update` 收一个可选的 `Assets::Scene*`；给了它，就从注视点沿吊臂方向做一次
`RayCastInCPU`，把吊臂缩短到障碍物前 `SpringArmRadius` 米。收拢是**瞬时**的（看穿柱子一帧比
硬切更糟），伸回按 `SpringArmReturnRate` 缓动。三条来之不易的规则：

1. **障碍永远赢，不设最小臂长。** 「最小距离」听起来更安全，实际相反：玩家背贴比最小值还近的
   柱子时，它会把镜头顶穿墙进去，画面全黑。宁可贴到角色肩上。
2. **臂长是对阻尼结果的钳位，不是缩放。** 每帧对已阻尼的偏移再乘一次比例会复利收敛，吊臂塌到
   注视点上，`glm::lookAt` 退化成奇异矩阵，画面同样全黑。
3. 吊臂短于 `SpringArmHideRigDistance` 时把主角 rig 隐掉，否则镜头里只剩一面白塑料。

收集物、敌人、装饰和主角 rig 本来就在 no-raycast 名单上，所以只有玩家真能站上去的实体会拉近
镜头。查询 `game.springArm`（1.0 = 全长）与 `game.cam{X,Y,Z}`。

## 被救机器人

`FRescueRigVisual` 在 `OnSceneLoaded` 按 `index_.RescueTotal()` 预建同样多个 astro_bot rig
实例（共用主角注入的 model / material，只换 tint），停在地下并隐藏。预建是为了救援当帧不建
节点树。

救出一个，`InteractableSystem` 当帧把 kit 的静态几何（`ab_char_bot_lost` 本体，或牢笼里那个
`ab_char_bot` 子节点）藏掉，rig 实例**接在静态件原来站的位置上**，然后自己走完一段演出：

| 阶段 | 做什么 | 时长 |
| --- | --- | --- |
| `Emerge` | 沿远离玩家的方向走开 0.8 m，播 `run`（0.55 倍速），朝向 = 行进方向 | 距离 / 1.6 m/s，钳在 0.4~1.1 s |
| `Cheer` | 站定，0.35 s 内转回来面向玩家，播一次性的 `cheer` | `cheer` clip 时长（1.6 s） |
| `Wave` | 切到循环的 `wave` 常驻 | — |

两个要点：

1. **起点是静态件的位置，不是让开之后的位置。** 从原地走出去才没有瞬移；直接摆到终点就是
   之前那个"啪一下被拉过去然后开始发呆"。让开 0.8 m 本身是必须的——站在笼子正中央开笼，
   不让开会把机器人生成在玩家身体里。
2. **`Place` 返回这段演出的总时长**，游戏拿它去定特写镜头的持续时间，镜头正好在机器人演完
   的那一刻还回去。

### 救援特写镜头

`FFollowCamera::BeginFocus(subject, viewer, holdSeconds)` 借走镜头去拍被救的机器人：

- **跟随相机在底下照常模拟，特写只在 `Fill()` 里按权重混进去**（`Smoothstep01`）。所以进出
  都是连续的，不是硬切；而且 `Forward()` / `Right()` 仍然用底下那个 yaw，
  **玩家的移动方向不会在特写期间被镜头带跑**。玩法全程不暂停。
- 机位取"玩家 → 机器人"这条线**偏 `FocusOffsetDegrees` 54°** 的三分之四视角，并以
  `FocusOrbitRate` 缓慢绕行。正对着这条线拍只会拍到玩家的后脑勺贴着镜头——第一版就是这样。
- `Snap()`（换关、复活、跳过开场）会取消特写，不会把半混完的镜头留在新画面上。
- 查询 `game.camFocus`（0 = 纯跟随，1 = 纯特写）。

## 帧顺序（`TickWorld`）

刻意的先后关系，改动前先读这一段：

1. **MechanismSystem** 先动：角色才能踩到*本帧*的平台位置。它读的是**上一帧**的玩家脚点
   （`previousFoot_`），因为跷跷板和表面速度判定要的是玩家真正站过的地方。
2. **PlayerController**：合成水平速度（相机相对输入 + 表面速度）与竖直速度（重力 20、
   跳 8.9、悬浮钳 −1、发射 `√(2gh)`），交给 `NextCharacterController::UpdateWithVelocity`。
3. CollectibleSystem → HazardSystem → EnemySystem → InteractableSystem（其 `freed` 事件驱动
   `FRescueRigVisual::Place`）。
4. `PlayerRigVisual` / `FollowCamera` / HUD，最后 `Scene::MarkTransformDirty()` 一次。

`FLevelFlow::WorldRunning()` 为假时整个 1–4 跳过，并 `physics->SetPaused(true)`。

## 引擎侧依赖（本项目引入的三项）

- **`Assets::Node::GetMetadata()`**：ScadLoader 把 user-module 的具名标量参数序列化成
  `"k=v;k=v"` 写进节点。读取用 `Assets::Scad::ParseScadMetadata` +
  `MetadataNumber/Bool/String`（`Modules/ScadLoader/FScadShared.h`）。向量与 range 会被跳过。
- **`NextCharacterController::UpdateWithVelocity(v, inheritGround, dt)`**：游戏自己积分重力/
  跳跃/悬浮/发射，控制器只负责推进与接触。`inheritGround` 为真且着地时叠加地面刚体速度——
  这就是角色能站住移动平台和旋转盘的原因。配套 `GetGroundVelocity/GetGroundNormal/
  GetGroundBodyID`。旧的 `Update(dir, speed, jump, dt)` 保持原样，NextDayz / CharacterDemo 不迁移。
- **击杀平面要夹在场景地板之上**：`Scene::RebuildMeshBuffer` 会在场景 AABB 最低点建一个无限
  平面，所以「一直往下掉」并不存在。`OnSceneLoaded` 里取
  `max(最低岛面 + killPlaneOffset, sceneAABBMin.y + 2)`。

## 主角 rig：`assets/scad/characters/astro_bot.scad`

9 骨骼（`bone_root` / `bone_torso` / `bone_head` / `bone_arm_l` / `bone_arm_r` / `bone_leg_l` /
`bone_leg_r` / `bone_jet_l` / `bone_jet_r`），外观复用 `kit_astro` 的配色与材质包装，所以和关卡
里的 NPC 机器人一致。`ROLECOLOR`（纯品红）占位腰带 / 手 / 脚 / 眼灯，运行时染成 `ab_BLUE()`。

必需 clip（`Test_NextAstrobot.cpp` 硬性检查）：`idle` `run` `skid` `jump` `fall` `hover` `land`
`punch` `punch2` `kick` `hurt` `win` `zip` `wave` `cheer`；其中
`jump/land/skid/punch/punch2/kick/hurt/cheer` 非循环。`run` 周期 0.5 s 对应 6 m/s，
`PlayerRigVisual` 按实际水平速度缩放播放速度。

**喷焰是两道，挂在脚下。** `bone_jet_l` / `bone_jet_r` 是 `bone_leg_*` 的子骨骼，各是一根
2.2 m（约 1.4 个身高）的细长光束，`hover` 用 `scale` 的 z 分量在 1.8~3.5 m 之间脉动，两只脚
错开半拍。这是照 PS5《宇宙机器人》改的：那边不是躯干下面一个短胖的锥，而是脚下两道又细又长
的光柱，这也是"悬浮"最强的视觉信号。三条配套约束：

1. **`hover` 里腿必须基本垂直**（±9° 以内）。喷焰挂在脚上，腿一收光束就朝两边斜出去。
2. **谁都不能默认亮着。** `SetVisibleRecursive` 是递归的，所以 `FPlayerRigVisual::SetVisible`
   显示 rig 之后要立刻把喷焰关掉（下一帧 `Update` 会按状态重开），
   `FRescueRigVisual::Place` 显示实例之后同理——被救机器人永远不喷。少了这两处，画面上就是
   每个机器人脚下插着两根 2 m 的钉子。
3. 只有 `ELocomotion::Hover` 亮。落地那几帧也必须灭，否则光束会戳穿地板。

**通道选择是这个 rig 唯一的硬坑。** 手臂和腿是绕自身局部 Z 轴对称的圆柱 + 球，所以
`rot` 的 **Z 分量转了也看不见**：

| 想要 | 用哪个分量 |
| --- | --- |
| 四肢前后摆动 | `X`，负 = 向前 |
| 四肢外张 / 上举 | `Y`，左肢正 = 向外，右肢负 = 向外 |
| 躯干 / 头 / 根骨骼的俯仰、侧倾、扭身 | `X` / `Y` / `Z` 都有效（这三个骨骼不轴对称） |

初版的 `hover` / `win` / `wave` / `zip` 把"张开手臂"写成了 Z 分量，结果是这四个动作在画面上
几乎等于绑定姿态——悬浮看不出在悬浮、被救机器人看着像在发呆。`Test_NextAstrobot.cpp` 现在
直接采样这几个 clip 的手臂四元数，要求手至少离开身侧 60°，写回 Z 通道会立刻红。

动作要点：

- `run` 有三个特征，缺一个就不像那个机器人（都是从参考视频逐帧量的）：
  **小碎步高步频**（周期 0.34 s ≈ 6 步/秒，视频里脚每 0.167 s 交错一次；腿只摆 ±26° 收在
  身体下面，摆大步反而不像）、**上身后仰**（根骨骼 −6° + 躯干 −12°，头顶落在脚跟后面）、
  **手臂摊开**（Y 外张 50~75° 是主角度，两臂交替一高一低地扇，X 前后摆只留 ±16° 点缀）。
  注意后仰这一条：直觉会往田径式前倾写，这个角色恰恰相反，是大头往后坐。
- `skid` 是 `run` 的反面：上身甩成后仰、前脚蹬出去撑住、双臂张开找平衡，收在 0.34 s 内。
- `hover` 是"这不是在下落"的全部依据：双臂平举 76~88°、腿垂直、上身后仰、0.14 m 的上下起伏
  加左右飘移，外加脚下两道脉动的长光束。`fall` 则刻意写成没控制住的样子，两者不会看混。
- `jump` 的重点是**俯仰要摆一下**：蹬地帧上身前倾 16°，收腿的同时甩成后仰 20°，收在后仰 12°
  接 `fall`。原来全程只后仰 6~10°，起跳和站着看着是同一个姿势。
- `punch` / `punch2` / `kick` 是连招三段，见下一节。
- `cheer` 是被救机器人的一次性欢呼（蓄力下蹲 → 跳起双臂高举 → 落地 → 两下挥拳），播完才
  切到循环的 `wave`。

生命周期铁律（同 ScadRig 指南）：`OnInit` LoadRig → `BeforeSceneRebuild` InjectAssets →
`OnSceneLoaded` Instantiate；`OnSceneUnloaded` **只清运行时指针**，不能清注入产物。

## 三段连招

拳击不是一个动作而是一条链：**左直拳 → 右重拳 → 回旋踢**，由 `FPlayerController` 的
`punchStage_`（0 = 没出招，1/2/3 = 三段）驱动，rig 按它选 `punch` / `punch2` / `kick`。

| | 左直拳 | 右重拳 | 回旋踢 |
| --- | --- | --- | --- |
| 时长 | `PunchSeconds` 0.30 | `PunchSeconds2` 0.32 | `KickSeconds` 0.55 |
| 判定 | `PunchRange` 1.2 / `PunchArcDegrees` 90° | 同左 | `KickRange` 1.9 / `KickArcDegrees` **360°** |
| 前冲 | `PunchLungeSpeed` 3.4 m/s | 同左 | `KickLungeSpeed` 1.8 m/s |

三条规则各有原因：

1. **接招窗口 (`ComboWindowSeconds`) 和输入缓冲 (`ComboBufferSeconds`) 是两回事。** 窗口是
   一段结束后还能接下一段的时间；缓冲是**出招过程中**按下的那一下不被丢掉，等这一段打完
   立刻兑现。缺了缓冲就必须卡着恢复帧按，玩家一连打只会掉招。
2. **出招期间脚下生根但仍然前冲。** 玩家自己的输入被压到 25%，方向由 `lunge_`（按 `Damp`
   衰减）沿朝向给，转身速率降到 35%。三段原地播放看着像三个动画，带前冲才像打中了。
3. **判定半径跟着段数走。** `player_.PunchRange()` / `PunchArcDegrees()` 是查询，敌人与
   可交互物都用它，不再读 config——回旋踢因此能扫到身后的东西。

`PunchStarted()` 每段各触发一次，所以一次连招能依次打碎三个箱子。查询 `game.punchStage`。

## 急停变向

全速反向输入会先插一段刹车，而不是让跑步加速度直接把速度翻过来：

- 触发条件：着地、水平速度 ≥ `SkidMinSpeed` 3.4、且输入方向与当前速度夹角超过约 110°
  （`SkidReverseDot` -0.35）。出招会取消 skid（刹车姿势和出招姿势抢同一批骨骼）。
- 生效 `SkidSeconds` 0.34 s：目标速度归零、用 `SkidDecel` 26 刹车、转身速率降到
  `SkidTurnScale` 30%——**身体还朝着原来的方向、脚在打滑**，这个滞后就是"急停"的全部读法。
- 状态是 `ELocomotion::Skid`，clip 是 `skid`。查询 `game.locomotion == "skid"`。

不做这一段的话，反向输入是 12 m/s 的速度差除以 50 的加速度 = 0.24 s 的倒着滑行，身上还播着
前倾的跑步循环，看着像动画错帧而不是像转身。

## 关卡流转

`levels.json` 的顺序就是流程。`levelCursor_` 指向当前关，结算画面按它决定说什么：

- 还有下一关 → 「LEVEL COMPLETE」+ `[Space / (X)] Next: <下一关名字>`；`[R]` 重打本关。
- 已是最后一关 → 「RUN COMPLETE」+ 整轮 campaign 合计（金币 / 拼图 / 救援 / 死亡 / 用时）+
  `[Space / (X)] Start over`（回到第一关并把 campaign 清零）。

手柄控制映射：
- 左摇杆：玩家移动（D-Pad 方向键亦可移动）
- 右摇杆：相机水平旋转
- 右侧功能键下（PS ✕ / Xbox A）：开始菜单确定、片头跳过、结算进入下一关、局内跳跃；起跳后空中再次按住进入悬浮（初期 1 秒内上浮 1.6 米的高升空冲刺，随后平稳滑翔）
- 右侧功能键左（PS ◻ / Xbox X）：地面直拳/摆拳/旋风腿三段攻击；空中直接触发 360° 回旋踢
- Start 键：暂停 / 恢复游戏（暂停时 PS ○ / Xbox B 亦可返回）

Campaign 合计在 flow 第一次进入 `Result` 时累加一次（`levelTallied_` 防重复），换关不清零，
只有回到第一关才清零。`AdvanceLevel()` 是唯一的推进入口，`LoadLevel(index, restartCampaign)`
是唯一的加载入口。

**`game.levelId` 报的是真正加载完的那一关**，不是刚请求的那一关：`levelCursor_` 在
`RequestLoadScene` 的那一刻就变了，脚本若等 cursor 会拿着旧场景继续跑下一步。等 `levelId`
才安全。

## 加一关

1. 写 `assets/scad/source/astro/<name>.scad`，用 kit_astro 摆件，末尾放
   `gk_camera_lookat(name = "overview", ...)` 和一条 `gk_camera_lookat_key(path = "level-flythrough", ...)`。
2. 关卡里必须有 `ab_bldg_startpad`（出生点 = 顶面中心，朝向 +x）与 `ab_bldg_goal`（终点）；
   检查点用 `ab_prop_checkpoint(idx = N)`，按钮 / 拉杆与栅栏用同一个 `idx` 配对。
3. 在 `assets/configs/nextastrobot/levels.json` 追加一条。

**零 C++ 改动**。索引、机关、危险、敌人、收集全部按模块名工作；`game.index.warnings` 会告诉你
缺了 startpad / goal / 岛屿。摆完之后用 `astro.level <id>` + 一串 `astro.teleport` 把路线上的
落脚点走一遍（断言 `game.onGround`），比截图更快能发现「这一跳过不去」。

## 调试与验收

调试 CVar：

| CVar | 作用 |
| --- | --- |
| `astro.god` | 免疫危险、敌人与击杀平面 |
| `astro.timescale` | 玩法 delta 倍率（0.05~4） |
| `astro.teleport "x,y,z"` | 传送到世界坐标 |
| `astro.ride "<机关名>"` | 传送到某个机关的站立点或身前（`moving` / `spin` / `conveyor` / `seesaw` / `bounce` / `crumble` / `button_1` / `cage` / `chest` / `lever_2` / `fountain` / `fan` / `laser` / `spikeball` …）。落点在道具**身前**时还会把玩家转过去面对它，脚本可以直接拳击，不必先猜朝向 |
| `astro.state "title\|playing\|result"` | 强制流程状态 |
| `astro.level "<id 或 1-based 序号>"` | 直接加载某一关，并把 campaign 合计清零 |

F5 切换调试面板。Agent queries 前缀 `game.`：`state` `locomotion` `clip` `player{X,Y,Z}`
`onGround` `punching` `yaw` `coins(Total)` `puzzles(Total)` `rescued(Total)` `deaths`
`checkpoint` `enemiesAlive` `killPlaneY` `springArm` `punchStage` `camFocus` `cam{X,Y,Z}`
`rescueRigs`
`level` `levelId` `levelCount` `campaign{Coins,Deaths}`
`index.{coins,puzzles,mechanisms,enemies,hazards,warnings}` `mech.<name>.t`（机关归一化相位，
名字见上面的机关表：`spikeball` `laser` `fan` `fountain` `windmill` `chest` `lever_2`
`flag_1` `flag_2` …）。

验收脚本：`smoke` / `locomotion` / `collect` / `mechanisms` / `goal` / `props` / `levels` /
`combat`。`props` 覆盖非 ★ 活动件、弹簧臂与被救机器人（靠 `astro.ride` 的自动转向完成拳击类
断言）；`levels` 覆盖两关流转：结算→下一关→campaign 累加→最后一关→重开清零，顺带验第二关
自己的拉杆门、风区与喷泉；`combat` 覆盖三段连招（逐段断言 `punchStage` 与 `clip`）、急停变向、
以及救援特写从升起到自己还回去的整条曲线，并截下 kick / skid / run / jump / 救援五张图。

写 `combat` 时踩到的一条：**截图会卡住时钟**。原来把跑步截图插在"跑 → 反向"中间，截图那一下
的停顿足够让角色跑出小岛，反向时已经不在地面上，skid 永远不触发。需要摆姿势的截图要放在自己
的片段里，不要插在有速度前提的两步之间。

```bash
gnb.bat build NextAstrobot gkNextUnitTests
out/build/windows/bin/gkNextUnitTests "[NextAstrobot],[AstroBotRig]"
gnb.bat validate --script assets/agentscripts/nextastrobot-smoke.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-locomotion.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-collect.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-mechanisms.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-goal.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-combat.agentscript.json
gnb.bat shot --target NextAstrobot --ui --frames 120
```

改了 `kit_astro.scad` 之后还要 `gnb.bat scad catalog`（0 bad / 0 warning），并对
`astro_showcase.scad` 与 `sky_garden.scad` 各截一张图比对。

## 已知边界

- 一关（`sky_garden`）。`levels.json` 支持多关顺序，但通关后没有「下一关」流转，结算按 R 重玩。
- 非 ★ 活动件（刺球摆动、风扇风区、喷泉托举、风车叶片、宝箱盖、拉杆、检查点旗帜）还没做：
  刺球与激光按绑定姿态作为静止危险体参与判定，其余是纯装饰。
- 相机没有弹簧臂：贴着立柱背面时会被遮挡。
- rig 是 Lambertian：`FScadRig` 按颜色分桶时丢掉了 `gk_material` 的 roughness/metalness，
  所以主角是哑光而非亮面塑料。玩法不依赖这一项。
- 敌人沿固定巡逻段往返，没有寻路，也没有自己的 rig。
