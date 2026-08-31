---
title: "TruckerDemo 架构与验证"
category: project
status: 现行
owner: TruckerDemo/NextPhysics
last_updated: 2026-08-31
---

# TruckerDemo 架构与验证

TruckerDemo 是一条可完整游玩的越野运输纵切：六轮 6×4 卡车从任务面板接单，前往装货点，装载后驶往
卸货点完成任务；燃油、补给、路面附着、悬挂、变速箱和 HUD 同步参与游戏循环。历史迭代计划已完成，
本文只保留当前实现边界和修改护栏。

## 运行时分层

- [`TruckerDemoGameInstance`](../../../src/Application/Game/TruckerDemo/TruckerDemoGameInstance.cpp)：场景锚点、输入、
  车辆控制、燃油与任务状态机、相机和 agent query 编排。
- [`TruckHud`](../../../src/Application/Game/TruckerDemo/TruckHud.cpp)：速度、挡位、油门/刹车、燃油、任务与交互提示；
  UI action 通过 snapshot/action 结构返回 GameInstance。
- [`NextPhysics`](../../../src/Engine/Runtime/Subsystems/NextPhysics.hpp)：引擎侧车辆设置与遥测契约。
- [`JoltPhysicsBackend`](../../../src/Modules/NextPhysics/JoltPhysicsBackend.cpp)：Jolt `VehicleConstraint`、前后轴悬挂、
  引擎/变速箱/差速器、轮胎摩擦和 debug drawing。
- [`overhill_mission.scad`](../../../assets/scad/source/overhill/overhill_mission.scad)：默认任务场景；卡车、装卸区和
  加油点通过节点语义被运行时解析，不以猜测坐标替代资产契约。

## 车辆与任务契约

- 物理固定步长为 60 Hz；游戏层只提交 throttle/steer/brake，不自行积分刚体。
- 前进中按反向键先形成脚刹，接近静止后才切换倒挡油门。
- 前后轴的 natural frequency、damping、preload 与动力/燃油参数均通过 `trucker.*` CVar 调整；不要把
  调参重新写死进 Jolt 后端。
- 路面类型通过每轮 friction scale 和车辆附加阻力表现；视觉车轮必须按空间位置匹配物理 wheel index，
  不能依赖场景遍历顺序。
- 任务状态为 `Available → Accepted → Driving → AtPickup → Loaded → AtDropoff → Complete`。
  装卸只在低速且位于对应区域时接受交互，完成后可在同一进程重开。
- 燃油由 idle、throttle、wheel slip 与 cargo load 共同消耗；加油点附近低速时补给，油量为零时切断油门。

## 验证

```bash
./gnb.sh build TruckerDemo
./gnb.sh validate --script assets/agentscripts/trucker_smoke.agentscript.json
./gnb.sh validate --script assets/agentscripts/trucker_physics_debug.agentscript.json
./gnb.sh validate --script assets/agentscripts/trucker_camera_orbit.agentscript.json
```

`trucker_smoke` 覆盖车辆创建、遥测、燃油和任务推进；另外两条脚本覆盖悬挂/接触 debug 与相机轨道。
人工检查 HUD 时使用 `gnb shot --target TruckerDemo --ui`。运行成功以日志中的
`committed scene [...]` 为准。

当前不包含绞盘、车损、轮胎损坏、多人协作和开放世界合同系统；这些不是隐含 TODO，扩展前需要重新
定义可验证范围。
