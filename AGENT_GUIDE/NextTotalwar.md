# NextTotalwar MVP 代码导览

`NextTotalwar` 是 400×400 m low-poly 阵型行军 RTS。MVP 边界是选择、定向移动、
部队级寻路、阵型槽位跟随与地形贴合；没有战斗、伤害、士气或敌方 AI。
战斗层的设计方案（尚未实现）见
[docs/projects/nexttotalwar/nexttotalwar-battle-mvp-design.md](../docs/projects/nexttotalwar/nexttotalwar-battle-mvp-design.md)。

## 入口与数据流

- target：`src/Application/Game/NextTotalwar/CMakeLists.txt`
- 主编排：`NextTotalwarGameInstance.{hpp,cpp}`
- 纯阵型逻辑：`Battle/FormationLayout.{h,cpp}`
- 相机：`Render/BattleCamera.{h,cpp}`
- runtime 数据：`NextTotalwarTypes.h`
- 战场：`assets/scad/proc/nexttotalwar/greenfield_400.scad`
- 兵种 kit / rig：`assets/scad/lib/kit_tw.scad` 与
  `assets/scad/characters/tw_*.scad`

当前规模是双方各 12 个 regiment、每队 100 人，共 24 队 / 2,400 人
（`NextTotalwarGameInstance.cpp` 顶部的 `regimentCountPerFaction` /
`soldiersPerRegiment` 是唯一事实来源）。每帧先推进
regiment anchor 与朝向，再让行军中的士兵 seek 对应阵型槽位，最后分帧更新共享
ScadRig 实例。寻路、选择和状态机都以 regiment 为粒度；士兵不做 A*、碰撞或 AI。

## Mesh 复用契约

扩军版固定启用 `ERenderCapacityMode::Massive`，并保持“每兵种共享 part model”：

- 3 个兵种各有 6 个 ScadRig part model，共 18 个 mesh，被同兵种的全部士兵共享；
- 每名士兵有独立 world/bone node 树和 Animator，但不复制 mesh；
- 24 个 regiment 的换色通过逐节点 material id 完成；
- 不使用 `FCharacterPool`，也不为每个士兵复制 model；
- 角色贡献约 6 个 render proxy/兵（全场 ≈ 14k）；HUD 使用运行时 capacity 显示占用，
  Massive 硬上限为 262,140。

远景动画按八分之一分帧、中景按三分之一分帧、近景全量更新；跳帧时给 Animator
补偿累计 delta。静止部队不再每帧重算槽位或刷新 world transform。

## 行军与桥

NavGrid 使用 2m cell、0.4m agent radius，并通过 `TerrainComponent::IsWater`
屏蔽河面。常规命令首先调用 `FNavGrid::FindPath`。SCAD 桥是独立 mesh，桥端在
2m 网格上偶尔会出现单格离散断连；A* 失败且路线跨河时，游戏层生成最近桥的
语义 waypoint（入口、出口、目标），绝不使用穿河直线。桥廊内士兵高度取桥面
插值，其他位置取 `TerrainComponent::SampleHeight`。

`Reforming` 只有在朝向到位且所有士兵距槽位小于 0.12m 后才结束，因此
`game.marchingRegiments == 0` 同时表示 anchor 与阵型都完成。

## 输入与 Agent 查询

- LMB：点选 / 框选；Shift 加选；双击同兵种全选
- RMB 拖拽：目标点 + 阵面朝向
- WASD / 方向键：平移；Q/E：旋转；滚轮：缩放
- `[` / `]`：调整选中部队排数

查询：`game.selectedRegiments`、`game.regimentCount`、
`game.marchingRegiments`、`game.soldierCount`、`game.renderProxyCount`、
`game.massiveMode`、`game.navReady`、`game.lastOrderDistance`。

## 定向验证

```powershell
gnb.bat build NextTotalwar gkNextUnitTests
out\build\windows\bin\gkNextUnitTests.exe "[NextTotalwar]"
gnb.bat scad catalog
gnb.bat shot --target NextTotalwar --ui
gnb.bat validate --script assets\agentscripts\nexttotalwar-select.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-march.agentscript.json
```
