# NextTotalwar 代码导览

`NextTotalwar` 是运行在 400×400 m low-poly 战场上的军团级即时战术 Demo。当前代码已经具备
选择、定向行军、部队级寻路、阵型跟随、固定步长近战、减员、死亡和战斗特效；尚未实现会实际
变化的士气/溃逃、敌方 Commander AI、弓兵远程、胜负 session 和产品化 UI。产品化目标见
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
- 相机：`Render/BattleCamera.{h,cpp}`
- 闪白、死亡和血迹：`Render/CombatFx.{h,cpp}`
- 战场：`assets/scad/proc/nexttotalwar/greenfield_400.scad`
- 兵种 kit/rig：`assets/scad/lib/kit_tw.scad` 与 `assets/scad/characters/tw_*.scad`

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

## 当前战斗数据流

`OnTick` 的有效顺序是：相机 → regiment anchor/path → 20 Hz `CombatSystem` → soldier 位置/动画 →
`CombatFx`。AgentValidation 可启用“一渲染帧一战斗 tick”的确定性模式。

`CombatSystem` 当前实现：

1. 用军团阵型 OBB、接战距离和 broadphase 建 `engagedWith`；
2. 用 `FCombatGrid` 在士兵粒度配对，每个目标最多三名攻击者并分配攻击槽；
3. 前线士兵离开槽位接近目标，友军做局部软分离；
4. 按兵种 attack/defense/damage/interval、前侧后攻击弧和 charge timer 确定性结算；
5. 生命归零进入 Dying/Dead、军团 strength 递减，产生 Hit/Death/RegimentDestroyed 事件；
6. `CombatFx` 做攻击/受击闪白、死亡定格和固定 256 槽血迹池。

`morale` 当前只初始化和显示，低于 40 的惩罚代码没有正常输入来源；Routing/Charging/Rout/Rally
大多只是预留状态。弓兵当前按弱近战兵作战。代码中没有 Commander AI，也没有统一 BattleResult。

## 输入与 UI

- LMB：点选/框选；Shift 加选；双击同兵种全选
- RMB 拖拽：目标点 + 阵面朝向
- WASD/方向键/MMB 拖拽：平移；Q/E：旋转；滚轮：缩放
- F：跟随选中军团
- `[`/`]`：调整选中军团排数
- F1/F5：显示/隐藏战斗调试

当前世界选择和底部单位条都允许选择蓝红双方，这是技术 Demo 行为，不是产品阵营边界。左上
`NextTotalwar` 和右上 `Battle Debug` 主要展示 proxy、FPS、NavGrid、战斗 tick 等开发信息；产品 UI
尚未拆出独立文件。

## Agent 查询

主要查询包括：

- 选择/移动：`selectedRegiments`、`marchingRegiments`、`lastOrderDistance`、`routeNodeCount`、
  `finalApproachAligned`
- 场景/相机：`regimentCount`、`soldierCount`、`renderProxyCount`、`navReady`、`camera*`
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
```
