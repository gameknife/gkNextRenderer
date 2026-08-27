---
title: "TruckerDemo —— SnowRunner 风格越野运输 Demo 迭代计划"
category: project
status: 计划（迭代）
owner: TruckerDemo
created: 2026-07-21
last_updated: 2026-07-21
---

# TruckerDemo — SnowRunner 风格越野运输 Demo 迭代计划

> **前提**：demo 已经能跑（`gnb.bat run TruckerDemo` 可出生、追尾相机跟车、WASD 能开、有极简 HUD 与单条运输任务）。本文**不是**从零搭建计划，而是在**现有实现**上做迭代，把它推进到"可玩的 SnowRunner 切片"。
> **本文回应用户四条要求**：① 补全类 SnowRunner 的 MVP 功能；② 车辆悬架尽量接近真实；③ 任务能跑起来并有简单界面交互；④ 有简单 HUD（车辆状态 / 档位 / 油门 / 刹车 / 油量）。
> **前置必读**：[AGENTS.md](AGENTS.md)、[AGENT_GUIDE/SCADLoader.md](AGENT_GUIDE/SCADLoader.md)、当前实现 [TruckerDemoGameInstance.cpp](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp)、载具后端 [JoltPhysicsBackend.cpp](src/Modules/NextPhysics/JoltPhysicsBackend.cpp)。
> **代号 / target**：`TruckerDemo`（目录 `src/Application/Game/TruckerDemo/`）。

---

## 0. 现状盘点（已可运行）

当前实现是**单文件** `TruckerDemoGameInstance.{hpp,cpp}`（约 260 行），已经具备：

- 加载 [assets/scad/source/overhill/overhill_mission.scad](assets/scad/source/overhill/overhill_mission.scad)（`use lib/kit_overhill` + `include gen/overhill_vignette`，含玩家卡车锚点 `player_truck_body` / `player_wheel` / `cargo_crate`）。
- Jolt `VehicleConstraint` 6 轮卡车（前轴转向、后双轴驱动 = 6×4），经 [NextPhysics](src/Engine/Runtime/Subsystems/NextPhysics.hpp:114) 抽象创建。
- 路面摩擦系统：`bodyID → 表面类型` 表 + 每轮 `SetVehicleWheelFrictionScale` + 对车身施加附加阻力（[TruckerDemoGameInstance.cpp:128](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:128)）。
- 任务状态机：`Driving → AtPickup → Loaded → AtDropoff → Complete`，F 装/卸货、R 复位。
- 轨道追尾相机（鼠标环绕 + 滚轮缩放）。
- 极简 HUD：速度 / 表面 / 任务 / 计时。
- Agent 查询与烟测脚本：`game.missionState/speed/surface/elapsed`，`assets/agentscripts/trucker_{smoke,physics_debug,camera_orbit}.agentscript.json`。

**结论**：骨架完整，问题集中在"物理手感"与"玩法闭环/表现层"两块。以下逐条对齐用户要求做差距分析。

---

## 1. 差距分析（对齐四条要求，带文件定位）

### 1.1 悬架 / 载具手感【要求 ②】

| 现状问题 | 位置 | 后果 |
| --- | --- | --- |
| 底盘是**单个 BoxShape，质心在几何中心**（偏高） | [JoltPhysicsBackend.cpp:1044](src/Modules/NextPhysics/JoltPhysicsBackend.cpp:1044) | 重心高 → 转向侧倾大、易翻，"积木感" |
| 所有轮共用 `SpringSettings(FrequencyAndDamping, 2.2f, 0.85f)`，硬编码 | [JoltPhysicsBackend.cpp:1077](src/Modules/NextPhysics/JoltPhysicsBackend.cpp:1077) | 频率太高（2.2Hz）+ 阻尼比 0.85 近临界 → 悬挂"发死"，颠簸不吸收，前后轴无法分别调 |
| **没有抗侧倾杆**（Jolt 有 `mAntiRollBars`，未使用） | 同上构造 | 过弯 / 侧坡车身横摆过大 |
| 引擎只设了 `mMaxTorque`，**无扭矩曲线 / 变速箱调校** | [JoltPhysicsBackend.cpp:1062](src/Modules/NextPhysics/JoltPhysicsBackend.cpp:1062) | 用默认 5 挡自动 + 平坦扭矩，缺柴油卡车低转高扭手感 |
| **S 键 = 反向扭矩，没有脚刹**（brake 输入恒 0） | [TruckerDemoGameInstance.cpp:107](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:107) | 无法"刹停"，只能倒车；高速下手感差 |
| 表面滚阻用**对车身施加人工水平力**模拟 | [TruckerDemoGameInstance.cpp:142](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:142) | 权宜之计，非轮胎滚阻，方向感突兀 |
| 悬挂行程 0.12–0.48，未按越野拉长 | [TruckerDemoGameInstance.cpp:89](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:89) | 越野地形贴地性一般 |

### 1.2 SnowRunner MVP 功能缺口【要求 ①】

- **无油量系统**（HUD 要求里点名要油量）。
- **无档位 / 无差速锁 / 无 AWD 切换** —— SnowRunner 的招牌越野工具（低挡 + 锁差速 + 全驱脱困）全缺；当前固定 6×4、自动挡且玩家无从感知/干预。
- 任务只有**一条硬编码**、无"接单"流程、无补给点。
- 脱困只有 R 键瞬移，无卡住/翻车检测反馈。
- （明确不做：绞盘、损伤、软泥形变、拖车——见 §4 各里程碑"不做"。）

### 1.3 任务界面交互【要求 ③】

- 取/卸货点是**硬编码坐标** `(27,-11)` / `(-38,-7)`（[TruckerDemoGameInstance.cpp:114](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:114)），**未与场景实际节点对齐**（`overhill_mission.scad` 只放了卡车，任务点是 vignette 里的建筑，坐标是猜的）。
- 世界里**没有取/卸货标记、没有路点箭头、没有目标距离**；交互仅"进圈按 F + 文字提示"。
- 没有任务面板 / 接单按钮 / 结算界面。

### 1.4 HUD【要求 ④】

- 现有 HUD 只有 速度 / 表面 / 任务 / 计时（[OnRenderUI](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:237)）。
- **缺**：档位、转速、油门条、刹车条、油量、手刹/差速锁/AWD 状态、坡度。
- 好消息：档位/转速可从 Jolt 直接读（§2.2），只差一条遥测 API 与自绘表盘。

---

## 2. 已核实的引擎 / Jolt 事实（写代码前先读）

> 下列均在动手前已在代码 / vcpkg 头文件中核实，API 引用为真实符号。

### 2.1 物理步进是固定 60Hz

[JoltPhysicsBackend.cpp:517](src/Modules/NextPhysics/JoltPhysicsBackend.cpp:517) 的 `Tick` 固定 `cDeltaTime = 1/60`，按累积时间做 1–4 个 collision step。**含义**：载具解算稳定、可确定性验证；调悬挂/变速箱不必担心变步长。游戏侧 `OnTick` 里对 `deltaSeconds` 已 `min(…,0.1)` 钳制。

### 2.2 Jolt 载具能力齐全（vcpkg joltphysics，头文件已在 `vcpkg_installed/.../Jolt/Physics/Vehicle/`）

- **悬挂弹簧**：`WheelSettingsWV::mSuspensionSpring = SpringSettings(FrequencyAndDamping, 频率Hz, 阻尼比)`；`mSuspensionMinLength/mMaxLength`、`mSuspensionPreloadForce`。
- **质心下移**：`OffsetCenterOfMassShape(shape, Vec3 offset)`（`Physics/Collision/Shape/OffsetCenterOfMassShape.h`）包住底盘 Box 即可。
- **抗侧倾杆**：`VehicleConstraintSettings::mAntiRollBars`，元素 `VehicleAntiRollBar{ mLeftWheel, mRightWheel, mStiffness(N/m) }`。
- **引擎**：`VehicleEngineSettings{ mMaxTorque, mMinRPM, mMaxRPM, mInertia, mNormalizedTorque(LinearCurve) }`。
- **变速箱**：`VehicleTransmissionSettings{ mMode(Auto/Manual), mGearRatios[], mReverseGearRatios[], mShiftUpRPM, mShiftDownRPM, mClutchStrength }`。
- **差速 / 差速锁**：`VehicleDifferentialSettings{ mLeftWheel, mRightWheel, mDifferentialRatio, mLimitedSlipRatio, mEngineTorqueRatio }`；`mLimitedSlipRatio` 越接近 1 越"锁"（`FLT_MAX` = 全开）。
- **遥测读出**（HUD 直接用）：`WheeledVehicleController::GetTransmission().GetCurrentGear()`、`GetEngine().GetCurrentRPM()`、`Wheel::HasContact()`。
- 摩擦已用 `SetCombineFriction` 回调按轮乘 `frictionScales`（[JoltPhysicsBackend.cpp:1100](src/Modules/NextPhysics/JoltPhysicsBackend.cpp:1100)），表面系统继续复用。

### 2.3 场景自动静态碰撞 + SCAD 命名

- 场景构建对每个可见静态节点自动建 mesh 碰撞并绑 `PhysicsComponent`（土路 / 桥 / 建筑加载即有碰撞）；**mobility=Dynamic 的节点被跳过**——这正是玩家卡车节点在 `BeforeSceneRebuild` 打 Dynamic 标记的原因（[TruckerDemoGameInstance.cpp:34](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:34)）。
- SCAD 里每次 user module 调用 = 一个同名场景节点；任务点可用 `oh_bldg_garage` / `oh_prop_tent` 等按名解析（AnchorMap 先例 [src/Gameplay/Sim/AnchorMap.h](src/Gameplay/Sim/AnchorMap.h)），**替换硬编码坐标**。

### 2.4 验证链路

`gnb shot --target TruckerDemo --ui`（HUD 截图）；`gnb validate --script assets/agentscripts/trucker_smoke.agentscript.json`（输入驱动 + `game.*` 断言，报告落 `out/build/<preset>/agent_reports/`）；DevTools `PhysicsDebugOverlay` 可视化悬挂段 / 接触点（[JoltPhysicsBackend.cpp:960](src/Modules/NextPhysics/JoltPhysicsBackend.cpp:960) 已画载具轮姿态）。

---

## 3. 分层与"引擎侧 / 游戏侧"边界

保持仓库取向：**引擎瘦、玩法在 Application**。

```
引擎侧（NextPhysics 抽象 + Jolt 后端）——只做"物理能力"：
  T1 悬架真实化（质心/弹簧/抗侧倾/行程 → 参数化进 API）
  T2 动力总成 + 遥测 + 差速锁/AWD + 真实脚刹（新增 vehicle API）

游戏侧（src/Application/Game/TruckerDemo/，建议拆多文件）——只做"游戏规则/表现"：
  T3 HUD 仪表盘        T4 燃料与补给
  T5 任务系统 + 界面交互   T6 场景动线打磨 + 自动验收
```

> 建议把当前单文件拆成 `VehicleController`（输入→载具+挡位/差速锁/脚刹映射）、`SurfaceSystem`、`FuelSystem`、`MissionSystem`、`TruckHud`、`TruckVisualBinder`，降低后续维护成本；拆分可在 T3 顺手做。

---

## 4. 迭代里程碑

每个里程碑独立可验、出问题好定位。顺序有依赖：T2 的遥测 API 是 T3 HUD 档位/转速的数据源；T4 油量条挂 T3 的 HUD 框架。

### T1 —— 悬架与底盘真实化【要求 ②｜引擎侧】

**目标**：卡车不再"高重心积木感"，悬挂有真实行程/回弹、过弯有抗侧倾、前后轴可分别调校。

**改动**（[NextPhysics.hpp](src/Engine/Runtime/Subsystems/NextPhysics.hpp) + [JoltPhysicsBackend.cpp](src/Modules/NextPhysics/JoltPhysicsBackend.cpp)）：

1. `FNextWheelSettings` 增 `suspensionFrequency`、`suspensionDamping`、`suspensionPreload`；后端 `SpringSettings` 从中取值，替换硬编码 `2.2f/0.85f`。
2. `FNextVehicleSettings` 增 `centerOfMassOffset`（默认下移 ~0.35m）；后端用 `OffsetCenterOfMassShape` 包底盘 Box。
3. `FNextVehicleSettings` 增 `frontAntiRollStiffness`、`rearAntiRollStiffness`；后端按左右轮 index 填 `mAntiRollBars`（前轴 1 根、后两轴各 1 根）。
4. 游戏侧 `CreateVehicle` 按 §5 参数表传入（前软后硬）；行程拉到 0.15–0.55。
5. **单测**（`src/Tests/`，无 GPU fixture，参考现有物理用例）：平地直行 N tick 后 x 位移 > 阈值 且 roll/pitch < 阈值；18° 桥坡通过不翻。

**验收**：`gnb.bat build gkNextRenderer gkNextUnitTests TruckerDemo` 过；新单测绿；`gnb shot --target TruckerDemo` 看静止车姿贴地自然；`PhysicsDebugOverlay` 看悬挂压缩合理；过弯侧倾明显小于改前。

**不做**：轮胎胎压 / 形变、非线性弹簧、每轮独立悬挂几何。

---

### T2 —— 动力总成 + 遥测 + 越野驱动工具【要求 ①②｜引擎侧】

**目标**：有档位、柴油卡车扭矩曲线、S 是脚刹、可读 gear/rpm；提供差速锁 / AWD 越野脱困工具。

**改动**（vehicle API 扩展）：

1. 新 `FNextVehicleEngineSettings`（maxTorque/min/maxRPM/inertia/归一化扭矩曲线点集）+ `FNextVehicleTransmissionSettings`（mode、gearRatios、reverseRatio、shiftUp/DownRPM、clutchStrength）；后端填 `controller->mEngine` / `mTransmission`。给低转高扭曲线（峰值扭矩落在 ~40% RPM）。
2. `SetVehicleDiffLock(id, bool)`：置各差速 `mLimitedSlipRatio`→~1.02（锁）/ 恢复默认（松）。`SetVehicleAllWheelDrive(id, bool)`：切换前轴是否 driven（6×4 ↔ 6×6）。
3. **真实脚刹**：`FNextVehicleInput.brake` 真正接线；游戏侧输入映射改为——**前进中按 S = 脚刹**（brake），车速≈0 后再按 S 才给倒挡油门。当前 [SetVehicleInput](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:107) brake 恒 0 要改。
4. 遥测：`FNextVehicleTelemetry{ int gear; float rpm; float engineTorque; float forwardSpeed; float wheelSlip[]; bool wheelContact[]; }` + `GetVehicleTelemetry(id)`（gear/rpm 见 §2.2）。
5. 全部数值 CVar 化（`trucker.drive.*`），HUD 前先用日志/临时文字核对。

**验收**：HUD 临时打印 gear/rpm 随油门变化正确；泥地开差速锁能爬出、关掉打滑（对比）；6×6 比 6×4 明显更能脱困；S 能把车刹停而非直接倒车。

**不做**：手动离合、涡轮、真实传动损耗建模。

---

### T3 —— HUD 仪表盘【要求 ④｜游戏侧】

**目标**：一屏看清 速度 / 档位 / 转速 / 油门 / 刹车 / 油量 / 手刹 / 差速锁 / AWD / 表面 / 坡度。

**改动**（重写 [OnRenderUI](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:237)，`ImGui::GetForegroundDrawList` 自绘，`ShouldRenderUiDuringScreenshot` 已开）：

- 左下：速度表盘（指针 + 数字 km/h）+ 转速表（RPM，接近 maxRPM 转红）+ 档位大字（`R/N/1..5`，数据来自 T2 遥测）。
- 右下：油门条 / 刹车条（0–100%，读输入映射后的实际值）、油量竖条（接 T4，先占位常量）。
- 状态灯：手刹、差速锁、AWD/6×6、低油告警。
- 顶部一行：任务提示（沿用现有 `AtPickup/AtDropoff/Complete` 文案）+ 表面 + 坡度(pitch°)。
- 顺手把单文件拆出 `TruckHud`。

**验收**：`gnb shot --target TruckerDemo --ui` 截图逐项核对；开动时油门/刹车/档位/转速实时变化正确。

---

### T4 —— 燃料与补给【要求 ①④｜游戏侧】

**目标**：油耗随油门/转速/负载/打滑消耗，油尽熄火，加油站补给。

**改动**（新 `FuelSystem`）：

- 油箱容量 + 当前油量；消耗率 = f(throttle, rpm, 载货, wheelSlip)；油量→0 时 `SetVehicleInput` 强制 throttle=0（熄火）。
- 加油点：锚定场景既有建筑节点（`oh_bldg_*`）或在 mission spec 加一个油站；进圈自动补给或按键补给。
- HUD 油量条接真实数据 + 低油告警灯。
- CVar：`trucker.fuel.capacity/idleRate/throttleRate/slipPenalty/…`，方便热调。

**验收**：跑一段油量单调下降；泥地打滑油耗更快；到油站回满；油尽熄火且能靠惯性滑停。

**不做**：多油种、油价经济、油管破损。

---

### T5 —— 任务系统 + 界面交互【要求 ①③｜游戏侧】

**目标**：有任务面板可"接单"，世界有取/卸货标记与路点指引，交互清晰，形成闭环。

**改动**（重写 `MissionSystem`）：

1. **锚点修复**（先做）：取/卸货点从硬编码坐标改为**按 SCAD 节点名解析**（garage=装货、camp=卸货、可加油站），坐标从加载后节点实测取（`node->WorldTranslation()`）。
2. **任务面板**（ImGui）：列 1–2 条运输任务，显示 起点 / 终点 / 距离 / 报酬；"接受"按钮激活任务。
3. **世界指引**：取/卸货处放发光标柱（3D 节点，基色压 0.1–0.3 避免 PT 发白）+ 屏幕投影路点箭头 + 到目标距离。
4. **状态机细化**：`Available → Accepted → Driving → AtPickup(停稳+F 装货) → Loaded → AtDropoff(停稳+F 卸货) → Complete(结算：报酬+用时，可接下一单)`；"停稳"= 速度<0.5m/s 且在半径内。
5. 结算小窗（完成用时 + 报酬 + "再来一单"）。

**验收**：点接单 → 世界出现标记与箭头 → 按指引开到取货 → 装 → 卸 → 弹结算；`gnb validate` 脚本断言 `game.missionState == complete`。

**不做**：任务链解锁、地图选择、货物物理掉落（留 §6 未来）。

---

### T6 —— 场景动线打磨 + 稳定性 + 自动验收【要求 ①③｜游戏侧 + 资产】

**目标**：场景布局与任务动线自洽，视觉不出错，自动化验收覆盖全流程。

**改动**：

1. **修车轮左右映射**：`OnSceneLoaded` 现在靠遍历顺序把 6 个 `player_wheel` 塞进数组（[TruckerDemoGameInstance.cpp:67](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp:67)），与物理轮 index 不保证一致 → 可能左右/前后视觉错位。改为按节点实测局部坐标匹配物理轮位。
2. **mission spec 完善**：给 `overhill_mission` 加取/卸货硬化停车坪、加油站、明确东西贯通动线（原 MVP 计划 §4.4 未落地部分）；kit 改动记得手动 `cp` 到 build assets。
3. **脱困反馈**：翻车（车身 up 与世界 up 夹角>80° 持续 2s）或长时间卡住 → HUD 提示按 R；R 复位回最近停车坪（Loaded 保货）。
4. **agentscript**：更新 `trucker_smoke` 覆盖 接单→装→卸→complete，加 `game.fuel`/`game.gear` 断言（需在 `RegisterAgentQueries` 补查询）。

**验收**：`gnb validate --script assets/agentscripts/trucker_smoke.agentscript.json` 全绿；`gnb shot --ui` 全流程关键帧截图；人肉过一遍 §8 验收清单。**到此迭代 MVP 完成。**

---

## 5. 悬架 / 动力真实化参数附录（起调值，全部 CVar 热调）

> 目标车：~3500kg 平板 6 轮越野卡车。以下是**起点**，靠 CVar 边开边调，不是定死值。

| 参数 | 现值 | 迭代目标 | 依据 |
| --- | --- | --- | --- |
| 悬挂自然频率（前 / 后） | 2.2 / 2.2 Hz | **1.3 / 1.6 Hz** | 重卡后轴载货更硬；2.2Hz 过硬发死 |
| 悬挂阻尼比 ζ（前 / 后） | 0.85 / 0.85 | **0.35 / 0.40** | 0.85 近临界→吸不了颠簸；越野要 0.3–0.45 |
| 悬挂行程 min/max | 0.12 / 0.48 | **0.15 / 0.55** | 越野拉长行程、贴地 |
| 悬挂预载 | 0 | 小预载稳住静态车高 | — |
| 质心偏移（相对底盘中心） | 0（几何中心） | **下移 ~0.35m** | 压低重心、抗翻 |
| 前 / 后抗侧倾刚度 | 无 | 起 ~10000 / ~14000 N/m | 削过弯侧倾 ~40% |
| 引擎峰值扭矩位置 | 平坦 | 曲线峰值落 ~40% RPM | 柴油低转高扭 |
| 变速箱 | 默认自动 | 保留自动、调 shiftUp≈2600 / shiftDown≈1300 RPM | 卡车早换挡 |
| 差速锁 `mLimitedSlipRatio` | 默认 1.4 | 松 1.4 / 锁 ~1.02 | 脱困工具 |
| 制动 | 无脚刹 | 前进按 S = 脚刹，停稳再倒 | 手感 |

**CVar 命名**：`trucker.susp.*`、`trucker.drive.*`、`trucker.surface.*`（沿用现有表面数值）、`trucker.fuel.*`。

---

## 6. 明确不做（本迭代范围外，别顺手加）

绞盘（Jolt 距离约束）、车辆损伤、SnowRunner 式软泥形变 / 轮胎压痕、深水熄火、拖车 / 多车型 / 载具切换、物理货物掉落、手柄 / 力反馈、`overhill_trail` 大图第二关、程序化大地图。以上都是明确的"未来扩展"，不进本迭代。

---

## 7. 风险与坑（实现前必读）

1. **Jolt 载具字段勿凭记忆**：动手前再核 `vcpkg_installed/.../Jolt/Physics/Vehicle/` 头（本文已核对 `SpringSettings` / `mAntiRollBars` / `VehicleDifferentialSettings::mLimitedSlipRatio` / `OffsetCenterOfMassShape` / `GetCurrentGear/RPM`，但版本升级后仍应复核）。
2. **SCAD Z-up → 引擎 Y-up**（绕 X −90°，`world=(x, z, −y)`）：轮位、任务点坐标一律以**加载后节点实测世界坐标**为准，别手工换算两次——这正是现在硬编码坐标可能对不上的根因。
3. **玩家节点务必在 `BeforeSceneRebuild` 标 Dynamic**（现已做），否则自动静态碰撞会给车体建碰撞、出生卡死；新增的世界标记若是静态可见节点会自动获得碰撞，作装饰要放动线外或标 Dynamic。
4. **PT 管线 albedo≈0.5 渲近白**：新增 3D 标记 / 货物基色压 0.1–0.3。
5. **kit / spec 改动后要镜像**：kit 手动 `cp` 到 `out/build/<preset>/assets/scad/lib/`；spec 重跑 `gnb scad compose`（自动镜像 gen）；忘 cp 的症状 = 改了没生效。
6. **物理是固定 60Hz 子步**（§2.1）：调悬挂/变速箱在此前提下确定；游戏侧 `deltaSeconds` 已钳制 0.1，勿去掉。
7. **既有单测基线**：改前先跑一遍 `gkNextUnitTests`，区分自己的回归与历史无关失败。
8. **差速锁语义**：`mLimitedSlipRatio` 是"最大/最小轮速比阈值"，越小越锁；设 1.0 附近即近似锁死，`FLT_MAX` 为全开——别写反。

---

## 8. 验证清单（迭代 MVP 验收）

Windows 上 `gnb.bat run TruckerDemo`：

1. 出生在西侧土路，追尾相机跟车；HUD 显示 速度/档位/转速/油门/刹车/油量/表面。
2. W 起步，干土路能到 ~40km/h 观感；**过弯侧倾明显收敛**（T1），换挡时档位/转速跳变正确（T2/T3）。
3. 驶入泥坑明显减速打滑（表面=mud）；**开差速锁 / 切 6×6 能爬出**（T2），HUD 状态灯亮。
4. S 键能**刹停**（非直接倒车）；油量随行驶下降，到油站回满，油尽熄火（T4）。
5. 打开任务面板**接单** → 世界出现取/卸货标记与路点箭头 → 开到取货区停稳按 F 装货 → 运到卸货区按 F → **弹结算（用时+报酬）**（T5）。
6. R 键随时复位；翻车/卡住有提示（T6）。
7. `gnb validate --script assets/agentscripts/trucker_smoke.agentscript.json` 全绿（报告在 `agent_reports/`）。

---

## 9. 参考文件清单

| 类别 | 路径 |
| --- | --- |
| 当前实现 | [src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp) / [.hpp](src/Application/Game/TruckerDemo/TruckerDemoGameInstance.hpp) |
| 物理抽象 / 后端 | [src/Engine/Runtime/Subsystems/NextPhysics.hpp](src/Engine/Runtime/Subsystems/NextPhysics.hpp) / [src/Modules/NextPhysics/JoltPhysicsBackend.cpp](src/Modules/NextPhysics/JoltPhysicsBackend.cpp) |
| Jolt 载具头 | `out/build/<preset>/vcpkg_installed/x64-windows-static/include/Jolt/Physics/Vehicle/*.h` |
| 场景 / 零件库 | [assets/scad/source/overhill/overhill_mission.scad](assets/scad/source/overhill/overhill_mission.scad)、`assets/scad/lib/kit_overhill.scad`、`assets/scad/specs/overhill_vignette.json` |
| 锚点按名解析先例 | [src/Gameplay/Sim/AnchorMap.h](src/Gameplay/Sim/AnchorMap.h)、`src/Application/Game/AirportSim/AirportMap.cpp` |
| 验证脚本 | `assets/agentscripts/trucker_{smoke,physics_debug,camera_orbit}.agentscript.json` |
| 验证工具 | AGENTS.md "Agent Visual Validation" / "Agent Interactive Validation" 两节 |
