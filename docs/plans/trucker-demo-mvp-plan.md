---
title: "TruckerDemo —— SnowRunner 风格越野运输 Demo（MVP 设计与开发计划）"
category: plan
status: 规划完成，待实现
owner: engine
created: 2026-07-11
last_updated: 2026-07-11
---

# TruckerDemo —— SnowRunner 风格越野运输 Demo（MVP 设计与开发计划）

> **状态**：规划草案（场景资产 kit_overhill 已就绪并截图验收）
> **目标读者**：负责实现本原型的后续 AI agent / 开发者
> **代号**：`TruckerDemo`（target 名，目录 `src/Application/Game/TruckerDemo/`）
> **前置必读**：[`AGENTS.md`](../../AGENTS.md)、[`AGENT_GUIDE/SCADLoader.md`](../../AGENT_GUIDE/SCADLoader.md)、[`docs/designs/scad-scene-compose-design.md`](../designs/scad-scene-compose-design.md)、`src/Application/Game/BrickPlayer/`（**最接近的物理玩法先例**：动态 box body + 节点同步）、`src/Application/Game/AirportSim/AirportMap.cpp`（SCAD 锚点按节点名解析的先例）
> **本文写作前已核实的真实引擎设施**（§3，下文 API 引用均为代码中已存在的符号）：场景自动静态 mesh 碰撞（`Scene.Build.cpp`）、`NextPhysics` 抽象面、`NextGameInstanceBase` 钩子、SCAD 节点命名规则、kit_overhill 40 模块 + 两个 spec、`gnb shot` / `gnb validate` 验证链路。

---

## 1. 愿景与 MVP 边界

### 1.1 一句话定位

基于 `assets/scad/lib/kit_overhill.scad`（"峠 Over the Hill" 低模越野山地零件库）搭一个 **SnowRunner 风格的越野运输切片**：玩家驾驶一辆平板货卡，在摩擦力/阻力各异的路面（干土路、草地、泥坑、沙地、浅河、木桥）上行驶，从修车棚装上货物，穿过泥坑与木桥，运到河对岸的营地即完成任务。

### 1.2 MVP 做什么 / 不做什么

| 维度 | MVP 内（In Scope） | MVP 外（Out of Scope，留作扩展） |
| --- | --- | --- |
| 场景 | 1 张任务图（基于 `specs/overhill_vignette.json` 派生的 mission spec，110×64，平坦地面 + 木桥坡道的天然起伏） | 高度场地形起伏、坡道/沟壑 kit 零件、程序化大地图 |
| 载具 | 1 辆平板货卡（`oh_veh_truck` 拆分出车体+车轮节点），Jolt `VehicleConstraint` 轮式载具 | 多车型、越野车/拖车可驾驶、载具切换 |
| 路面 | 5~6 种表面：干土/草/泥/沙/浅水/木桥，按**牵引力系数 + 滚动阻力 + 车身拖拽力**区分（§4.3 数值表） | SnowRunner 式软泥形变、轮胎压痕、深水熄火 |
| 任务 | 单条运输任务：修车棚装货 → 营地卸货 → 完成提示 + 计时 + R 键重置 | 任务链、经济系统、地图解锁 |
| 货物 | 货斗上的视觉货物节点（跟随车体，不参与物理） | 物理货物（可颠掉）、绞盘、吊装 |
| 相机 | 追尾相机（`OverrideRenderCamera`），鼠标不参与 | 自由相机切换、驾驶舱视角 |
| 输入 | 键盘 W/S 油门/倒车、A/D 转向、Space 手刹、R 复位、F 装/卸货 | 手柄、力反馈、键位配置 |
| UI | 极简 ImGui HUD：速度、当前表面、任务状态、计时 | 正式 UI 美术、小地图 |
| 失败态 | 翻车后 R 键原地复位（SnowRunner 的 recover） | 损伤、油量、维修 |
| 平台 | Windows 桌面优先（其余平台随 Jolt/引擎自然覆盖，不做专项验证） | 移动端触控 |

### 1.3 MVP 成功标准（Demo 验收）

Windows 上 `./gnb.bat run TruckerDemo` →
出生在西侧土路上，追尾相机跟车 → W 起步，干土路上能到 ~40km/h 观感 → 驶入泥坑段明显减速、轮子打滑（HUD 表面显示 mud）→ 出泥坑过木桥（上下坡道不卡、不飞车）→ 到东岸修车棚前的装货区停稳按 F，货斗上出现板条箱 → 原路返回穿过泥坑到西侧营地卸货区按 F → HUD 弹出"任务完成 + 用时" → R 键随时复位车辆。全程 60fps 量级（PT 或 SwModern 管线均可跑）。
**自动验收**：`gnb validate --script assets/agentscripts/trucker_smoke.agentscript.json` 通过（§6 M4）。

---

## 2. 场景资产（已就绪）

kit 与示例场景在本计划撰写时已完成并截图验收（`out/build/windows/screenshots/overhill_{showcase,trail,vignette}.jpg`）：

- **零件库** `assets/scad/lib/kit_overhill.scad`：前缀 `oh_`，40 模块，已入库 `assets/scad/lib/catalog.json`（0 warning）。与本 demo 直接相关的：
  - 地面件（沿 x 铺、底面 z=0）：`oh_ground_trail`（土路，双车辙）、`oh_ground_trail_bend`（弯道）、`oh_ground_mud`（泥坑）、`oh_ground_sand`、`oh_ground_grass`、`oh_ground_river`（河床+半透明水面）
  - `oh_prop_bridge`（木桥：桥面 z≈0.55，两端 18° 坡道——**MVP 里唯一的立体行驶挑战**）
  - `oh_veh_truck`（平板货卡，长 ~6.8，轮半径 0.5，前单轴后双轴）、`oh_veh_wheel(r, w)`
  - 任务点视觉：`oh_bldg_garage`（修车棚=装货点）、`oh_prop_tent`/`oh_prop_campfire`（营地=卸货点）、`oh_prop_gate_flags`（可作起点/检查点标记）
- **近景任务图底稿** `assets/scad/specs/overhill_vignette.json` → `gen/overhill_vignette.scad`（110×64）：西侧营地(-38,7 附近)、中央河流(x=0)+木桥(0,0)、桥西泥坑(-16,0)、东侧加油点/修车棚(16~27, 10~11)、贯穿东西的土路(y=0)。**布局天然就是任务动线**。
- 整图 `specs/overhill_trail.json`（180×140，含 S 形路线/雪峰/峡谷角）留作第二关卡或宣传图。

> 尺度：mid（约等于米）。卡车 6.8m、路宽 6m、桥宽 5.6m——数值直接按米制调物理即可。
> 坑：kit 更新后要手动 `cp assets/scad/lib/kit_overhill.scad out/build/<preset>/assets/scad/lib/`（`gnb scad catalog` 只镜像 catalog.json、`compose` 只镜像 gen 产物）。

---

## 3. 已核实的引擎设施（写代码前先读这节）

1. **场景自动静态碰撞**（`src/Engine/Assets/Core/Scene.Build.cpp` ~L292）：场景构建时对每个带 `RenderComponent` 且 `GetRayCastVisible()` 的节点，用其 Model 自动 `CreateMeshShape` + `CreateMeshBody`（Static/Kinematic），并把 bodyID 绑进节点的 `PhysicsComponent`。**含义**：SCAD 任务图加载后，土路/桥面/坡道/建筑/树全部自动有精确 mesh 碰撞，MVP 不需要手搭碰撞层。限制：Model 索引数 ≥65535×3 的不建 shape（本图单节点远小于该值）；**mobility 为 Dynamic 的节点会被跳过**——这是玩家卡车视觉节点不被误建静态碰撞的正确开关（§4.4）。
2. **NextPhysics 抽象**（`src/Engine/Runtime/Subsystems/NextPhysics.h`，后端 `src/Modules/NextPhysics/JoltPhysicsBackend.cpp`，Jolt 来自 vcpkg `joltphysics`）：现有 Box/Sphere/Mesh/Plane body、AddForce、Kinematic move、角色控制器。**缺**：载具约束、raycast、力矩/定点施力——本计划新增载具 API（§4.2）。摩擦当前是各 shape 硬编码（0.5 上下），`PhysicsComponent::PhysicsMaterial` 字符串字段已反射但**未接到 Jolt**。
3. **GameInstance 钩子**（`src/Engine/Runtime/GameInstance.hpp`）：`OnInit/OnTick/OnKey(SDL_Event&)/OnSceneLoaded/BeforeSceneRebuild(nodes)/OverrideRenderCamera(Camera&)/ConfigureCVars/RegisterAgentQueries/DrawAdditionalPhysicsDebugOverlay`。追尾相机、键盘输入、给玩家节点打 Dynamic 标记、注册 `game.*` agent 查询全部有现成入口。
4. **SCAD 节点命名**：user module 调用实例 = 逻辑节点，`node.name = module 名`（`FScadEvaluator.cpp` FinalizeSceneNode）。在场景 scad 顶层定义 wrapper module（如 `module player_truck_body() ...`）即可获得**唯一命名锚点**——StudioSim/AirportSim 的 POI 就是这么做的（`AirportMap.cpp` 按名查 `AnchorMap`，`src/Gameplay/Sim/AnchorMap.h`）。
5. **bodyID→节点反查**：场景自动建碰撞时已把 bodyID 写入各节点 `PhysicsComponent`（`BindPhysicsBody`）。游戏侧遍历一次场景节点即可建 `bodyID → 节点名前缀 → 表面类型` 查找表（§4.3）。
6. **新 target 注册**：`src/cmake/SourceFiles.cmake` 加 `file(GLOB_RECURSE src_files_truckerdemo Application/Game/TruckerDemo/*.cpp ...)`（照抄 characterdemo 条目），`src/CMakeLists.txt` 加 `add_executable(TruckerDemo ${src_files_truckerdemo} ${DESKTOP_MAIN_SOURCES})` + include dir。新增文件后首次构建要 `./gnb.bat build TruckerDemo --reconfigure`。
7. **验证链路**：`gnb shot --target TruckerDemo --scene ... --ui`（截图验收 HUD）；`gnb validate --script ...`（输入驱动 + `game.*` 查询断言，报告落 `out/build/<preset>/agent_reports/`）；`PhysicsDebugOverlay`（`src/Modules/DevTools/`）可视化碰撞体。
8. **入口样板**：每个 game target 提供 `CreateGameInstance(WindowConfig&, Options&, NextEngine*)` 工厂（见 `ScadCatalogMain.cpp` / 各 GameInstance.cpp），场景加载用 `GetEngine().RequestLoadScene({.filename = "assets/scad/..."})`（CharacterDemo 先例，支持 `.append`——但 **append 对 .scad 未验证**，本计划不依赖它，§4.4）。

---

## 4. 核心设计

### 4.1 分层

```
TruckerDemoGameInstance (装配/输入/相机/HUD)
├── VehicleSystem      —— NextPhysics 载具 API 的薄封装：创建卡车、喂输入、读轮姿态
├── SurfaceSystem      —— bodyID→表面类型表 + 每轮牵引/阻力修正（游戏侧数值，全 CVar 化）
├── MissionSystem      —— 装货/卸货 zone 状态机 + 计时 + 复位
└── TruckVisualBinder  —— 物理姿态 → player_* 场景节点世界变换（含轮转/转向角）
```

引擎侧唯一改动：**NextPhysics 载具 API**（§4.2）。其余全部在游戏目录内，符合"引擎瘦、玩法在 Application"的仓库取向。

### 4.2 载具：Jolt VehicleConstraint 经 NextPhysics 暴露（M1）

选型：**用 Jolt 自带的 `WheeledVehicleController` + `VehicleConstraint`**（vcpkg joltphysics 自带，无新依赖），不自研 raycast car。理由：悬挂/引擎/差速/轮胎摩擦曲线全部现成且稳定，"SnowRunner 感"主要靠表面数值层（§4.3）调出来；自研方案还得先给抽象层补 raycast/定点施力/力矩三件套，工作量更大且手感风险高。

`NextPhysics.h` 新增（后端实现在 `JoltPhysicsBackend`，其他后端可先 stub）：

```cpp
struct FNextWheelSettings  { glm::vec3 position; float radius, width, suspensionMin, suspensionMax; bool steered, driven; };
struct FNextVehicleSettings{ glm::vec3 chassisHalfExtent; float mass; float maxEngineTorque; float maxSteerAngleDeg;
                             std::vector<FNextWheelSettings> wheels; glm::vec3 initialPosition; glm::quat initialRotation; };
struct FNextVehicleInput   { float throttle; float steer; float brake; float handbrake; }; // 均 [-1,1]/[0,1]

virtual NextVehicleID CreateWheeledVehicle(const FNextVehicleSettings&) = 0;
virtual void RemoveVehicle(NextVehicleID) = 0;
virtual void SetVehicleInput(NextVehicleID, const FNextVehicleInput&) = 0;
virtual void GetVehicleBodyTransform(NextVehicleID, glm::vec3&, glm::quat&) = 0;
virtual void GetVehicleWheelLocalTransform(NextVehicleID, int wheel, glm::vec3&, glm::quat&) = 0;
virtual NextBodyID GetVehicleWheelContactBody(NextVehicleID, int wheel) = 0;   // 无接触返回无效 ID
virtual void SetVehicleWheelFrictionScale(NextVehicleID, int wheel, float longScale, float latScale) = 0;
virtual void SetVehicleBodyTransform(NextVehicleID, const glm::vec3&, const glm::quat&) = 0; // R 键复位
```

实现要点（给后端实现者）：
- 底盘 = Box body（Dynamic，质量 ~3500kg），`VehicleCollisionTesterRay`（或 CastCylinder）自然命中 §3.1 自动生成的静态 mesh 场景。
- 卡车 6 轮：前轴 2 轮转向、后双轴 4 轮驱动（坐标照抄 `oh_veh_truck`：前轴 x=2.5、后轴 x=-0.6/-1.75、半径 0.5——注意 SCAD Z-up 转引擎 Y-up 后是 z=2.5 等，实现时以加载后节点实测坐标为准）。
- 表面摩擦的接入点：优先用 Jolt 载具的摩擦合成回调（按接触 body 查表）；若所用 Jolt 版本回调形态不合，退化为每 tick `GetVehicleWheelContactBody` + `SetVehicleWheelFrictionScale`（本抽象两条路都留了）。**实现前先查 vcpkg 拉到的 Jolt 头文件确认回调签名，不要凭记忆写**。
- 物理 tick 时序：载具输入在 `NextPhysics::Tick` 前喂入；R 键复位用 `SetVehicleBodyTransform`（保留 Jolt body 睡眠唤醒处理）。
- 单测：`src/Tests/` 加一条"平地直行 N tick 后 x 位移 > 阈值、翻转角 < 阈值"的确定性用例（Catch2，参考 `Test_PhysicsSync.cpp` 的无 GPU fixture）。

### 4.3 表面系统（M3，本 demo 的灵魂）

**数据流**：`OnSceneLoaded` 时遍历场景节点 → 有 `PhysicsComponent` 绑定 bodyID 且名字命中前缀表的，记入 `unordered_map<bodyID, ESurface>`；每 tick 对每个车轮取 `GetVehicleWheelContactBody` → 查表得表面 → 应用三类修正：

| 表面 | 节点名前缀 | 纵向牵引 scale | 侧向 scale | 附加阻力(对车身，每个接触轮) | 观感目标 |
| --- | --- | --- | --- | --- | --- |
| 干土路 | `oh_ground_trail` | 1.0 | 1.0 | 0 | 基准，跑得最快 |
| 木桥 | `oh_prop_bridge` | 0.9 | 0.9 | 0 | 略滑，过桥要慢 |
| 草地 | 兜底（含 spec ground 平板） | 0.8 | 0.75 | 小 | 可走但费劲 |
| 沙地 | `oh_ground_sand` | 0.65 | 0.6 | 中 | 起步肉、易陷 |
| 泥坑 | `oh_ground_mud` | 0.45 | 0.4 | 大 | 明显打滑+减速，需带速度冲 |
| 浅水 | `oh_ground_river` | 0.55 | 0.5 | 很大 | 涉水强阻力（MVP 不做熄火） |

- 附加阻力 = 对底盘 body 施加 `-v * k_surface * 接触轮数/总轮数` 的水平力（现有 `AddForceToBody` 够用）。
- 全部数值挂 CVar（`ConfigureCVars`，前缀 `trucker.surface.*`），HUD 显示每轮当前表面——调手感靠改 CVar 热调，不重编。
- 注意河水节点是"河床+水面"同一 module 实例（`oh_ground_river`），水面半透明件也在同一节点下，直接按前缀归 water 即可；桥面在水面上方 z≈0.55，轮子接触的是桥就是桥。

### 4.4 玩家卡车的视觉与场景（M2）

任务图 = **专用 spec** `assets/scad/specs/overhill_mission.json`（从 vignette 复制后微调：装/卸货区各留一块 `oh_ground_trail` 硬化停车坪，清掉动线上的静态摆车）→ compose 到 `gen/overhill_mission.scad`；再写薄壳场景文件 `assets/scad/overhill_mission.scad`：

```scad
use <lib/kit_overhill.scad>
$fn = 12;
include <gen/overhill_mission.scad>          // 相对路径解析若有问题，退化为直接粘贴 gen 内容
// —— 玩家载具锚点（wrapper module ⇒ 唯一节点名；出生点 = 西侧土路） ——
module player_truck_body() oh_veh_truck_body(seed = 1);   // 见下：kit 需拆分该模块
module player_wheel(r = 0.5, w = 0.36) oh_veh_wheel(r, w);
module cargo_crate() { color([0.42,0.32,0.20]) cube([1.05,1.05,0.75], center=true); }
translate([-30, 0, 0.2]) player_truck_body();
translate([-27.5, 1.0, 0.5]) player_wheel();   // FL …共 6 只，位置≈物理轮位即可，运行时每帧覆写
// …
translate([-30, 0, -5]) cargo_crate();          // 藏在地下，装货时由游戏挪到货斗
```

- **kit 改动**：把 `oh_veh_truck` 拆出 `oh_veh_truck_body(seed)`（车体、无轮），原 `oh_veh_truck` = body + 6 轮组合，行为不变；改完跑 `gnb scad catalog` 重新入库并手动 cp kit 到 build assets。
- `BeforeSceneRebuild` 中给 `player_*`/`cargo_crate` 节点挂 `PhysicsComponent(mobility=Dynamic)` → §3.1 的自动静态碰撞会跳过它们（否则出生点会杵一个卡车形静态碰撞体）。
- `TruckVisualBinder` 每帧：车体节点 ← `GetVehicleBodyTransform`；6 只轮节点 ← 车体姿态 × `GetVehicleWheelLocalTransform`（含悬挂行程/滚转/转向角）；已装货时 `cargo_crate` ← 车体姿态 × 货斗偏移。
- 装/卸货 zone 不放场景锚点，直接用游戏配置常量（`TruckerDemoConfig.hpp`：pickup=修车棚门前 (27,11) 附近半径 4m、dropoff=营地 (-38,7) 附近半径 4m，加载后按实际节点坐标校一次）。AirportSim 的坐标表模式，简单可靠。

### 4.5 任务状态机（M4）

`Idle → Driving → AtPickup(停稳+F) → Loaded → AtDropoff(停稳+F) → Complete(计时定格, R 重开)`。
"停稳" = 速度 < 0.5 m/s 且在 zone 半径内。翻车判定：车体 up 与世界 up 夹角 > 80° 持续 2s → HUD 提示按 R。R 复位 = 回最近停车坪姿态（Loaded 状态保货）。

### 4.6 相机与输入

- 追尾相机：`OverrideRenderCamera` 输出——锚点 = 车体后上方（距离 9m、俯角 ~18°），位置对车体 yaw 低通滤波（防泥地抖动），前瞻 = 车速 × 0.3s。
- 输入：`OnKey` 维护按键位集，`OnTick` 转成 `FNextVehicleInput`（油门/转向带 attack/release 斜率，全键盘也能开出细腻感）。

---

## 5. 里程碑（每步独立可验，出问题好定位）

### M0 —— target 脚手架 + 场景进引擎（0.5 天级）
SourceFiles.cmake + CMakeLists 注册 `TruckerDemo`；GameInstance 骨架加载 `assets/scad/overhill_mission.scad`（此刻可先直接用 `gen/overhill_vignette.scad`）；自由观察相机。
**验收**：`./gnb.bat build TruckerDemo --reconfigure` 过编译；run 后日志 `uploaded scene [...] to gpu`；`gnb shot --target TruckerDemo` 出图；PhysicsDebugOverlay 里可见地面/桥自动碰撞体。

### M1 —— NextPhysics 载具 API + 盒子代理车能开（1~2 天级，唯一的引擎侧改动）
§4.2 API 全量 + Jolt 后端实现；游戏侧先用调试盒子当车身渲染（不接 SCAD 视觉）；W/S/A/D/Space 可开，R 复位。
**验收**：`./gnb.bat build gkNextRenderer gkNextUnitTests TruckerDemo` 过（引擎层改动的标准目标组合）；新增载具单测绿；demo 里平地起步、转向、手刹漂移不翻车；上下木桥坡道不卡不弹飞（这是 M1 的隐藏验收重点——悬挂参数不合适会在 18° 坡道暴露）。

### M2 —— 卡车视觉绑定 + 任务图（1 天级）
kit 拆分 `oh_veh_truck_body` + `gnb scad catalog`；mission spec + 薄壳场景文件；`BeforeSceneRebuild` Dynamic 标记；TruckVisualBinder 六轮姿态。
**验收**：`gnb shot --target TruckerDemo --ui` 卡车完整、轮贴地；开动时轮滚转/前轮转向肉眼正确；出生点无隐形碰撞（倒车绕一圈验证）。

### M3 —— 表面系统（1 天级）
bodyID→表面表 + 每轮 friction scale + 附加阻力 + CVar + HUD 每轮表面显示。
**验收**：同一段直路上分别铺 trail/mud 对比：泥地极速明显更低、油门全开轮速>车速（打滑）；涉水段几乎推不动、桥上正常；`gnb validate` 脚本用 `game.speed` 断言"泥中极速 < 干土极速 × 0.6"。

### M4 —— 任务闭环 + 自动验收（1 天级）
MissionSystem + HUD + `RegisterAgentQueries`（`game.missionState/game.speed/game.surface/game.elapsed`）+ `assets/agentscripts/trucker_smoke.agentscript.json`（脚本驱动：起步→过泥→过桥→装货→返程→卸货，断言 missionState==complete）。
**验收**：§1.3 全流程人肉过一遍 + validate 脚本绿（报告在 `agent_reports/`）。**到此 MVP 完成。**

### M5+（明确不做，列给后续）
物理货物（Dynamic box 放货斗，可颠掉——引擎能力已具备）、绞盘（Jolt 距离约束）、燃料/损伤、`overhill_trail` 大图第二关、坡地 kit 零件（`oh_ground_slope`，让路线有真正爬升）、手柄。

---

## 6. 风险与已知坑（实现 agent 必读）

1. **Jolt 载具回调签名勿凭记忆写**：先读 vcpkg 安装的 Jolt 头（`out/build/<preset>/vcpkg_installed/.../Jolt/Physics/Vehicle/`）再定摩擦接入方式；抽象层已同时留了"回调"与"每 tick 改 friction scale"两条路（§4.2）。
2. **SCAD Z-up → 引擎 Y-up**（绕 X −90°，`world=(x, z, −y)`）：spec 里的 (x,y) 平面坐标到引擎里是 (x,−z)；轮位、zone 坐标一律以**加载后节点实测世界坐标**为准，别手工换算两次。
3. **`include <gen/...>` 相对路径**：`use <lib/...>` 从 assets/scad 根文件出发已被 showcase 验证；`include` gen 产物若解析失败（gen 内部还有 `use <../lib/...>`），直接把 gen 内容粘进薄壳文件（gen 头注释带 spec sha256，可追溯）。
4. **玩家节点务必在 `BeforeSceneRebuild` 标 Dynamic**，否则 §3.1 自动给车体建静态 mesh 碰撞，出生即卡死。同理货物 crate。
5. **PT 管线 albedo≈0.5 渲近白**：新增视觉件（HUD 外的 3D 标记等）基色压 0.1~0.3。
6. **既有单测有 1 条无关失败**（"Scad loader: include executes a file even if use imported it first"），改动前先跑一遍基线，别把它当自己的回归。
7. **kit/spec 改动后的镜像**：kit 手动 cp；spec 重跑 `gnb scad compose`（自动镜像 gen）；catalog 重跑 `gnb scad catalog`（自动镜像）。忘 cp 的症状 = 改了没生效。
8. **物理与渲染帧率解耦**：`NextPhysics::Tick(deltaSeconds)` 的步进策略实现前确认（固定步/变步），载具对大 dt 敏感；必要时在载具 API 内部做子步。
9. 场景里静态摆设的 `oh_veh_*`（营地越野车等）会自动获得静态 mesh 碰撞——是特性不是 bug（可当障碍物），但任务动线上别摆。

---

## 7. 参考文件清单

| 类别 | 路径 |
| --- | --- |
| 零件库 / catalog | `assets/scad/lib/kit_overhill.scad`、`assets/scad/lib/catalog.json` |
| 场景 spec / 产物 | `assets/scad/specs/overhill_vignette.json`、`specs/overhill_trail.json`、`assets/scad/gen/overhill_*.scad` |
| 验收截图 | `out/build/windows/screenshots/overhill_{showcase,trail,vignette}.jpg` |
| 物理抽象/后端 | `src/Engine/Runtime/Subsystems/NextPhysics.h`、`src/Modules/NextPhysics/JoltPhysicsBackend.*` |
| 自动场景碰撞 | `src/Engine/Assets/Core/Scene.Build.cpp`（L285 起） |
| GameInstance 钩子 | `src/Engine/Runtime/GameInstance.hpp` |
| 锚点按名解析先例 | `src/Application/Game/AirportSim/AirportMap.cpp`、`src/Gameplay/Sim/AnchorMap.h` |
| 动态 body + 节点同步先例 | `src/Application/Game/BrickPlayer/BrickPlayerGameInstance.cpp`（L226 附近） |
| target 注册 | `src/cmake/SourceFiles.cmake`、`src/CMakeLists.txt`（各 add_executable 块） |
| 验证工具 | AGENTS.md "Agent Visual Validation" / "Agent Interactive Validation" 两节 |
