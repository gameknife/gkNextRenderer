# NextTotalwar 代码导览

`NextTotalwar` 是运行在 400×400 m low-poly 战场上的军团级即时战术产品切片。当前代码具备
蓝军指挥、数据驱动部署、统一订单、敌方 Commander AI、定向行军、固定步长近战、弓兵齐射、
士气/溃逃/重整、胜负与同进程重赛，以及默认隐藏诊断信息的产品 UI。设计依据见
[基础战斗循环产品化设计](../docs/projects/nexttotalwar/nexttotalwar-productization-design.md)。

## 入口与文件地图

- target：`src/Application/Game/NextTotalwar/CMakeLists.txt`
- 主编排、输入和当前 UI：`NextTotalwarGameInstance.{hpp,cpp}`
- runtime 数据与军团状态：`NextTotalwarTypes.h`
- 战斗 tuning：`NextTotalwarCombatConfig.hpp`
- 阵型纯逻辑：`Battle/FormationLayout.{h,cpp}`
- 军团/士兵接战：`Battle/CombatSystem.{h,cpp}`
- 命中和攻击弧纯函数：`Battle/CombatModel.{h,cpp}`
- 士兵空间索引：`Battle/CombatGrid.{h,cpp}`
- 战斗事件与确定性 RNG：`Battle/BattleState.h`
- 战役阶段和胜负：`Battle/BattleSession.{h,cpp}`
- 统一玩家/AI 订单：`Battle/BattleOrderSystem.{h,cpp}`
- 士气、溃逃与重整：`Battle/MoraleSystem.{h,cpp}`
- 弓兵齐射、弹药与压制：`Battle/RangedCombatSystem.{h,cpp}`
- 红军指挥 AI：`AI/CommanderAI.{h,cpp}`
- 兵种与战役配置加载：`Data/BattleData.{h,cpp}`
- 相机：`Render/BattleCamera.{h,cpp}`
- 闪白、死亡和血迹：`Render/CombatFx.{h,cpp}`
- 固定 96 槽代表箭矢弧线池：`Render/RangedVolleyFx.{h,cpp}`
- 战场：`assets/scad/proc/nexttotalwar/greenfield_400.scad`
- 兵种 kit/rig：`assets/scad/lib/kit_tw.scad` 与 `assets/scad/characters/tw_*.scad`
- 产品数据：`assets/configs/nexttotalwar/units.json` 与 `battles/greenfield.json`

## 当前规模与视觉契约

当前规模是双方各 12 个 regiment、每队 100 人，共 24 队/2,400 人。每队依次使用矛兵、剑兵、
弓兵定义。三个 rig 都是 7 骨骼、6 个 part，并带 idle/walk/march/run/attack/die 六个 clip。

每个兵种只注入 6 个共享 part model，共 18 个角色 mesh；每名士兵有独立 world/bone node 树和
Animator，部队换色通过逐节点 material id 完成。角色约贡献 14,400 个 render proxy，另外还有
地图和 256 个池化血迹节点。当前可见项编码硬预算是 32,767；不要依据旧扩军实验假定启用了更高
上限的 Massive mode。

动画按相机距离分帧：近景全量、中景三分之一、远景八分之一。静止部队不需要重算 A*，但当前
GameInstance 仍集中处理大量视觉与 UI 工作，产品化前计划做等价拆分。

## 行军、阵型与桥

NavGrid 使用 2 m cell、0.4 m agent radius，并通过 `TerrainComponent::IsWater` 屏蔽河面。
常规命令先调用 `FNavGrid::FindPath`。桥端在离散网格上可能断连；A* 失败且路线跨河时，游戏层
生成最近桥的入口、出口和目标 waypoint，不使用穿河直线。桥廊士兵高度取桥面插值，其他位置取
`TerrainComponent::SampleHeight`。

RMB 按下确定目标、拖拽确定最终阵面朝向。多选军团先生成目标阵列，再用
`Formation::MinimumTravelAssignment` 减少交叉。`Reforming` 只有在朝向到位且活士兵距槽位小于
0.12 m 后结束，因此 `game.marchingRegiments == 0` 同时表示 anchor 与阵型完成。

## 战斗数据流

`OnTick` 的有效顺序是：验证请求 → 相机 → session/time scale → regiment anchor/path → 20 Hz 固定步进
（CommanderAI 产生命令 → BattleOrderSystem 执行 → RangedCombatSystem → CombatSystem → MoraleSystem →
BattleSession 胜负）→ soldier 位置/动画 → CombatFx → 清空当帧事件。AgentValidation 保持“一渲染帧
一战斗 tick”，相同 seed、订单序列和 tick 数得到相同结果。

`CombatSystem` 当前实现：

1. 用军团阵型 OBB、接战距离和 broadphase 建 `engagedWith`；
2. 用 `FCombatGrid` 在士兵粒度配对，每个目标最多三名攻击者并分配攻击槽；
3. 前线士兵离开槽位接近目标，友军做局部软分离；
4. 按兵种 attack/defense/damage/interval、前侧后攻击弧和 charge timer 确定性结算；
5. 生命归零进入 Dying/Dead、军团 strength 递减，产生 Hit/Death/RegimentDestroyed 事件；
6. `CombatFx` 做攻击/受击闪白、死亡定格和固定 256 槽血迹池。

`MoraleSystem` 消费死亡事件、远程压制、侧后威胁、局部兵力差和友军溃逃；带迟滞地进入
Steady/Wavering/Routing/Rallying/Eliminated。溃军向本方边界撤退，可在脱离威胁后重整，离开战场
则计为消灭。弓兵按数据配置的射程、最小射程、齐射间隔、命中率和弹药作战，近战仍使用较弱的
Archer combat def。远程结算用少量地形高度采样拒绝隔山齐射；表现使用 96 槽循环箭矢池，每轮只
显示六支代表箭，不影响数值命中，也不会随齐射次数增长 scene node。

所有 Move/Attack/Charge/Halt/Withdraw/SetFormation 都先提交 `FBattleOrder`，校验阵营所有权、目标、
状态和序列，再由唯一执行口修改军团。玩家只能选择/指挥蓝军；红军 AI 只读取合法战场快照并通过
同一订单系统下令。正常难度 AI 按 Advance/Engage/Press/Recover 阶段使用战线、侧翼、远程和撤退行为。

## 输入与 UI

- LMB：点选/框选；Shift 加选；双击同兵种全选
- RMB 拖拽：目标点 + 阵面朝向
- WASD/方向键/MMB 拖拽：平移；Q/E：旋转；滚轮：缩放
- F：跟随选中军团；F6：显示/隐藏 NavGrid 可行走（绿）与不可行走（红）区域
- X/C/H/V：攻击/冲锋/停止/撤退
- `[`/`]`：调整选中军团排数
- Space/P：推进简报或暂停；1/2/3：0.5x/1x/2x；F1/F5：显示/隐藏战斗诊断

普通启动依次进入 Briefing → Deployment → Active；暂停进入 Paused，任一方无可作战军团后进入
Finished，展示 Victory/Defeat/Draw 与 Rematch。左上显示阶段、时间、速度和双方兵力，底部只显示
蓝军卡片（兵力、士气、弓兵弹药）。诊断面板默认隐藏，仅 F5 打开。

## Agent 查询

主要查询包括：

- 选择/移动：`selectedRegiments`、`marchingRegiments`、`lastOrderDistance`、`routeNodeCount`、
  `finalApproachAligned`
- 产品循环：`battlePhase`、`battleResult`、`battleSeconds`、`acceptedOrders`、`rejectedOrders`、
  `aiDecisions`、`playerAttackOrders`、`enemyMarchingRegiments`
- 士气/远程：`routingRegiments`、`waveringRegiments`、`rangedVolleys`、`remainingAmmo`、
  `activeArrows`、`arrowPoolCapacity`
- 场景/相机：`regimentCount`、`soldierCount`、`renderProxyCount`、`navReady`、`navGridVisible`、`camera*`
- 战斗：`aliveSoldiers`、`factionStrength0/1`、`engagedRegiments`、`fightingSoldiers`、
  `destroyedRegiments`、`disengagingRegiments`、`pursuingRegiments`、`totalKills`、`combatTicks`
- 表现：`corpseCount`、`flashingSoldiers`、`flashingCorpses`、`bloodStainCount`、
  `bloodPoolCapacity`

## 定向验证

```powershell
gnb.bat build NextTotalwar gkNextUnitTests
out\build\windows\bin\gkNextUnitTests.exe "[NextTotalwar]"
gnb.bat shot --target NextTotalwar --ui
gnb.bat validate --script assets\agentscripts\nexttotalwar-select.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-march.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-camera.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-battle.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-battle-c2.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-ai-idle-player.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-product-loop.agentscript.json
gnb.bat validate --script assets\agentscripts\nexttotalwar-navgrid-debug.agentscript.json
```
