---
title: "NextRA 当前状态与后续方向"
category: project
status: 现行
owner: NextRA
created: 2026-07-03
last_updated: 2026-07-17
---

# NextRA 当前状态与后续方向

NextRA 已是可运行的本地 RTS/lockstep 原型，不再处于旧 MVP 设计阶段。实现位于 `src/Application/Game/NextRA/`；以下是代码现状，不是对线上网络或完整经济系统的承诺。

固定 tick、Order gate、sync hash 以及 sim/render 隔离的长期约束见 [架构不变量](architecture.md)。

## 已实现

- 20 Hz fixed-step deterministic simulation，定点坐标/角度与确定性随机数。
- 7 种定义：Infantry、Tank、Barracks、Base、Rocketeer、Turret、Wall；含 weapon/armor 伤害矩阵、射程、冷却、生产时间和 footprint。
- 选择/框选、编队移动、Shift+右键 attack-move、对敌右键 attack、Barracks 生产。
- occupancy/path grid、目标获取、移动、炮塔朝向、攻击、死亡与 render proxy 同步。
- order 编解码、按 tick bucket、input delay、sync hash、replay codec。
- `LoopbackTransport` 及 UI 中的 delay/drop/reorder 注入，用于本机测试时序和确定性。
- 简单 AI 对手、命令/同步状态调试面板和程序化战场。

构建运行：

```bash
./gnb.sh build NextRA
./gnb.sh run NextRA
```

## 明确尚未实现

- 真实 socket/P2P/client-server transport、lobby、断线恢复或权威服务器。
- 完整资源采集、建造放置、科技树、战争迷雾、地图编辑和正式关卡内容。
- 面向发布的 replay 文件 UX、网络作弊防护、跨版本兼容和长局压力验证。

因此不要把 `LoopbackTransport` 描述为“联机完成”，也不要根据已删除的 combat-depth 计划假定经济/更多兵种已经排期。

## 可选后续方向

后续迭代应先选一个可验收目标再立 spec，例如：

1. 网络纵切：实现一个真实 transport，双进程跑相同 tick，并用 sync hash/replay 验证失步诊断。
2. 玩法纵切：资源 → 建造 → 生产闭环，保持命令数据和 sim 状态确定性。
3. 战斗深度：投射物、范围伤害或视野，但一次只引入一个可测机制。
4. 工程化：为 codec、order manager、path/occupancy、replay round-trip 和跨帧 sync hash 补系统测试。

这些是候选方向，不是活动 TODO；真正任务以 `.spec/TODO.md` 和新建、经确认的 spec 为准。
