# NextAstrobot — AstroBot 风格 3D 平台跳跃

`NextAstrobot` 是原生 C++ 子应用（目标 `NextAstrobot`，源码 `src/Application/Game/NextAstrobot/`），
把 `kit_astro` 零件库和 `sky_garden.scad`「星尘花园」做成可完整游玩的一关：机器人从出生岛出发，
跑、跳、悬浮、拳击，踩着移动平台、摆锤、跷跷板、旋转盘、传送带、滑索穿过三座悬浮岛，吃金币、
捡拼图、救出被困机器人，最后抵达糖果条纹终点门并结算。

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
└── UI/AstroHud.{hpp,cpp}                HUD 与标题 / 暂停 / 结算画面
assets/configs/nextastrobot/{gameplay.json, levels.json}
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

当前 13 个机关及其活动件：

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

## 帧顺序（`TickWorld`）

刻意的先后关系，改动前先读这一段：

1. **MechanismSystem** 先动：角色才能踩到*本帧*的平台位置。它读的是**上一帧**的玩家脚点
   （`previousFoot_`），因为跷跷板和表面速度判定要的是玩家真正站过的地方。
2. **PlayerController**：合成水平速度（相机相对输入 + 表面速度）与竖直速度（重力 20、
   跳 8.9、悬浮钳 −1、发射 `√(2gh)`），交给 `NextCharacterController::UpdateWithVelocity`。
3. CollectibleSystem → HazardSystem → EnemySystem → InteractableSystem。
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

8 骨骼（`bone_root` / `bone_torso` / `bone_head` / `bone_arm_l` / `bone_arm_r` / `bone_leg_l` /
`bone_leg_r` / `bone_jet`），外观复用 `kit_astro` 的配色与材质包装，所以和关卡里的 NPC 机器人
一致。`ROLECOLOR`（纯品红）占位腰带 / 手 / 脚 / 眼灯，运行时染成 `ab_BLUE()`。

必需 clip（`Test_NextAstrobot.cpp` 硬性检查）：`idle` `run` `jump` `fall` `hover` `land`
`punch` `hurt` `win` `zip` `wave`；其中 `jump/land/punch/hurt` 非循环。`run` 周期 0.5 s 对应
6 m/s，`PlayerRigVisual` 按实际水平速度缩放播放速度。`bone_jet` 默认隐藏，只在悬浮时显形。

生命周期铁律（同 ScadRig 指南）：`OnInit` LoadRig → `BeforeSceneRebuild` InjectAssets →
`OnSceneLoaded` Instantiate；`OnSceneUnloaded` **只清运行时指针**，不能清注入产物。

## 加一关

1. 写 `assets/scad/source/astro/levelNN_<name>.scad`，用 kit_astro 摆件，末尾放
   `gk_camera_lookat(name = "overview", ...)` 和一条 `gk_camera_lookat_key(path = "level-flythrough", ...)`。
2. 关卡里必须有 `ab_bldg_startpad`（出生点 = 顶面中心）与 `ab_bldg_goal`（终点）；
   检查点用 `ab_prop_checkpoint(idx = N)`，按钮与栅栏用同一个 `idx` 配对。
3. 在 `assets/configs/nextastrobot/levels.json` 追加一条。

**零 C++ 改动**。索引、机关、危险、敌人、收集全部按模块名工作；`game.index.warnings` 会告诉你
缺了 startpad / goal / 岛屿。

## 调试与验收

调试 CVar：

| CVar | 作用 |
| --- | --- |
| `astro.god` | 免疫危险、敌人与击杀平面 |
| `astro.timescale` | 玩法 delta 倍率（0.05~4） |
| `astro.teleport "x,y,z"` | 传送到世界坐标 |
| `astro.ride "<机关名>"` | 传送到某个机关的站立点（`moving` / `spin` / `conveyor` / `seesaw` / `bounce` / `crumble` / `button_1` / `cage` …） |
| `astro.state "title\|playing\|result"` | 强制流程状态 |

F4 切换调试面板。Agent queries 前缀 `game.`：`state` `locomotion` `clip` `player{X,Y,Z}`
`onGround` `punching` `yaw` `coins(Total)` `puzzles(Total)` `rescued(Total)` `deaths`
`checkpoint` `enemiesAlive` `killPlaneY` `index.{coins,puzzles,mechanisms,enemies,hazards,warnings}`
`mech.<name>.t`（机关归一化相位）。

```bash
gnb.bat build NextAstrobot gkNextUnitTests
out/build/windows/bin/gkNextUnitTests "[NextAstrobot],[AstroBotRig]"
gnb.bat validate --script assets/agentscripts/nextastrobot-smoke.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-locomotion.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-collect.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-mechanisms.agentscript.json
gnb.bat validate --script assets/agentscripts/nextastrobot-goal.agentscript.json
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
