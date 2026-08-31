---
title: "NextTotalwar 行军、地图与大规模表现约束"
category: project
status: 现行
owner: NextTotalwar
last_updated: 2026-08-31
---

# NextTotalwar 行军、地图与大规模表现约束

本文保留 NextTotalwar 当前仍成立的战场、编队、寻路和大规模表现契约。完整可玩战斗循环、阵营、订单、
士气、弓兵、AI 与重赛状态见[基础战斗循环产品化设计](nexttotalwar-productization-design.md)；代码入口见
[NextTotalwar 代码导览](../../AGENT_GUIDE/NextTotalwar.md)。

## 战场与坐标

- 默认战场是 400×400 m 的 `assets/scad/source/totalwar/greenfield_400.scad`，包含地形、河流与桥。
- TERR 使用 176×176 cells：约 2.27 m/cell、61,952 个三角形，保持为单个可碰撞 MeshShape。
  提高分辨率前先核对 SCAD Terrain 的 section/physics 分块边界，不能只改一个常量。
- SCAD 创作坐标为 Z-up，进入引擎后为 Y-up；游戏逻辑、NavGrid 与 render proxy 一律使用引擎世界坐标。
- 桥是道路语义与可行走表面，河流是不可行走区域。寻路不能用直线插值跨河；waypoint 必须经过可用桥面。

相关语言和资产约定见 [SCAD Terrain](../../AGENT_GUIDE/ScadTerrain.md)、
[SCAD 资产 Playbook](../../AGENT_GUIDE/ScadAssetPlaybook.md) 与 [ScadRig](../../AGENT_GUIDE/ScadRig.md)。

## 固定步长与命令

- 仿真以固定 20 Hz tick 推进；render frame rate 只影响插值，不改变战斗结果。
- 玩家和 Commander AI 都生成同一套语义 order；系统不能通过 UI/阵营特判绕过 order 队列。
- 多军团移动先做目标槽位分配，再为每个 regiment 生成路径；到达阶段恢复阵型深度和朝向。
- 路径与槽位 tie-break 必须稳定。相同 seed、scenario 与 order stream 应得到相同结果。

## 规模与表现

- 默认双方各 12 个 regiment、每队 100 人，共 2,400 个 soldier visual。
- soldier 使用共享 ScadRig 资产与 part/material 组合；不得为每名士兵复制模型或独立解析 SCAD。
- `RenderProxySystem` 负责 sim pose 到 scene node 的批量映射和帧间插值。战斗系统不直接写渲染节点。
- 可见性预算受全局 131,072 render proxy 上限保护；Visibility ID 格式见
  [双平面 Visibility Buffer](../../designs/massive-visibility-buffer-design.md)。增加规模时同时审计 scene node、
  render proxy、尸体/血迹池和 UI 兵牌预算。
- 动画状态通过 regiment/soldier 数据驱动 idle、march/run、attack、route、die；死亡对象退出作战仿真，
  尸体表现由有界池管理。

## 验证

```bash
./gnb.sh build NextTotalwar
./out/build/<preset>/bin/gkNextUnitTests "[NextTotalwar]"
./gnb.sh validate --script assets/agentscripts/nexttotalwar-march.agentscript.json
./gnb.sh validate --script assets/agentscripts/nexttotalwar-product-loop.agentscript.json
```

地图或阵型修改还应运行 camera/select/navgrid 相关 agentscript，并检查桥梁路径、2,400 人可见预算、相同
seed 重赛和固定 tick 确定性。运行成功以 `committed scene [...]` 为准。
