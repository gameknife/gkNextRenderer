# AirportSim —— Jumbo Airport Story 风格机场生态观察 Demo（MVP 设计与开发计划）

> **状态**：规划草案（已与需求方确认三项关键取向，见 §1.3）
> **目标读者**：负责实现本原型的后续 AI agent / 开发者
> **代号**：`AirportSim`（target 名）
> **前置必读**：[`AGENT_GUIDE/CharacterDemo.md`](../AGENT_GUIDE/CharacterDemo.md)（NavGrid/CharacterActor 模板）、[`docs/StudioSim-MVP-Plan.md`](StudioSim-MVP-Plan.md) 与 `src/Application/Game/StudioSim/`（**最接近的实现先例**：SCAD 锚点解析、LLM 决策调度、气泡 UI 全部有现成代码可参考）、[`AGENT_GUIDE/SCADLoader.md`](../AGENT_GUIDE/SCADLoader.md)、[`AGENTS.md`](../AGENTS.md)
> **本文写作前已核实的真实引擎设施**：`assets/scad/airport.scad` 全部 POI 锚点（§2 坐标表摘自源文件）、`NextGameplay::FNavGrid`/`FPathFollower`/`CharacterActor`、`StudioSim::OfficeMap`/`DecisionScheduler`、`NextAI::FAIService::GenerateTextAsync`、`Assets::EnvironmentSetting`（`SunRotation`/`SunIntensity`/`SkyIntensity`，见 `src/Engine/Assets/Core/Model.hpp`）、`Scene::GetEnvSettings()`。下文 API 引用均为代码中已存在的符号。

---

## 1. 愿景与 MVP 边界

### 1.1 一句话定位

用 `airport.scad` 程序化机场航站楼，搭一个 **Jumbo Airport Story 风格的"活机场"观察盒**：一天之内（带日夜光照变换），各职业员工通勤上岗、换班下班，旅客按航班表从陆侧入口涌入，走完 **值机 → 安检 → 空侧消费/候机 → 登机** 的完整旅程；角色由**本地 LLM 在决策时刻输出动作**（去哪、做什么、对周边人说什么、什么心情），对相遇的其他角色作出合理反应。玩家是**观察者**：自由相机 + 跟踪视角 + 调试面板，不含经营玩法。

### 1.2 MVP 做什么 / 不做什么

| 维度 | MVP 内（In Scope） | MVP 外（Out of Scope，留作扩展） |
| --- | --- | --- |
| 场景 | 1 个 `assets/scad/airport.scad`（已完成，含全部 POI 锚点，**不改 scad 布局**，允许微调锚点坐标修走线） | 多航站楼、室内顶棚/二层、动态改建 |
| 角色视觉 | 简单几何体（胶囊/盒组装）+ 职业配色 + 头顶气泡/状态图标；**架构上预留骨骼模型换装接口**（§3.3） | 第一阶段就上 Mannequin/KayKit 骨骼动画（第二阶段做） |
| 员工 | ~15 名、9 种职业，按班次通勤上岗/下班（§3.1） | 排班编辑、雇佣/工资、技能成长 |
| 旅客 | 并发上限 ~24，由航班表驱动生成，走完旅程后登机消失（§5.2） | 接送机人群、到港旅客提取行李、误机剧情 |
| 移动 | `FNavGrid` A* + `FPathFollower` + 轻量 kinematic mover + 简单分离避让（§7.2） | 真人群仿真、physics 胶囊互推、坐下 IK |
| 决策 | **LLM 驱动**：决策时刻输出单个 JSON 动作（对齐 StudioSim 的 `DecisionScheduler` 模式）；**确定性旅程状态机兜底**，LLM 不可用时全程可演示（§5） | 多步工具调用 agent、长期记忆、角色关系网 |
| 互动 | 相遇寒暄气泡、同店/同椅区闲聊、对长队/夜晚/航班临近的情绪反应 | 语音、多人群聊、玩家与 NPC 对话 |
| 时间 | 连续 24h 日夜循环（太阳方位+强度+天光联动），班次与航班表挂钟（§4） | 跨天存档、天气、季节 |
| 玩家 | 观察相机（自由飞行 + 锁定跟踪某角色）+ ImGui 调试面板（调速/决策日志/NavGrid 覆盖层） | 经营建造、任务系统、UI 美术 |
| 平台 | Windows 桌面（与 localllm 同机） | 移动端 |

### 1.3 已确认的三项关键取向（需求方拍板）

1. **实现路径**：新建 C++ target `src/Application/Game/AirportSim/`，链接 `NextGameplay`，与 CharacterDemo/StudioSim 同构。
2. **角色表现**：几何体起步、**预留换装**——视觉层走接口（§3.3），第二阶段无痛替换骨骼模型。
3. **行为决策**：**LLM 驱动**（本地 llama-server，`NextAI::FAIService`），但底层旅程/排队/寻路由确定性状态机保证正确性，LLM 只在"决策时刻"上层选择（§5.1 分层）。

### 1.4 MVP 成功标准（Demo 验收）

Windows 上 `./gnb.bat run AirportSim` →
清晨 05:30 天蒙蒙亮，早班员工从停车场/公交站走人行道进入 entrance、各自走到岗位（值机柜台后、安检通道旁、店铺收银后）→ 07:00 第一班航班的旅客陆续生成，排队值机（部分走自助 kiosk）→ 安检南进北出 → 空侧有人买咖啡、有人逛礼品店、有人上厕所、多数去候机椅坐下 → 登机口广播后排队登机、走进 gate 消失 → 全天 8~12 班循环 → 角色相遇时头顶冒出 LLM 生成的寒暄/抱怨/闲聊气泡，安检排长队时队尾旅客出现不耐烦情绪 → 13:30 早晚班在岗位交接 → 日落后航站楼人流稀疏、太阳光转为夜色 → 21:30 晚班下班离场，夜间只剩保安巡逻 + 保洁拖地 → 次日清晨循环。**断网/LLM 不可用时，气泡换预制台词库，其余流程不受影响。**

---

## 2. 场景资产：airport.scad 与 POI 锚点

### 2.1 场景结构速览（源文件已核实）

- OpenSCAD **Z-up**，地面顶面 z=0.15；场地 84×80。加载后锚点节点名 = module 名，世界坐标取节点 `WorldTranslation`（与 office.scad/OfficeMap 同一套约定，加载链路已被 StudioSim 验证可用）。
- **航站楼** 60×40：x∈[-30,30]，y∈[-12,28]，玻璃幕墙、无顶棚（俯视可观察）。
- **功能分区**（scad y 轴，南→北）：
  - 陆侧室外 y<-12：人行道（y≈-13.5）→ 马路（y≈-17.7，两处斑马线 x≈-18 / x≈0）→ 停车场（中心约 (-19,-25.7)）与东南草地。**员工/旅客的城市侧生成与消失区。**
  - 陆侧大厅 y∈[-12, ~4]：值机柜台×6（西）、自助 kiosk×4、问询台、ATM。
  - 安检带 y≈4.6~7.7：4 条通道，**南进北出**（通道局部纵深 3.1m，金属探测门在通道东侧偏移 +1.05）。
  - 空侧 y∈[~8, 28]：西餐饮区（BURGER + 咖啡岛）、东零售街（SHOP/BOOKS/GIFTS）、两簇候机排椅、东北卫生间、东侧员工办公室、北墙 6 个登机口。
  - 停机坪 y>28：3 架客机 + 地勤车队（MVP 仅作背景，不参与模拟）。

### 2.2 POI 锚点表（坐标摘自 scad 布局段，加载后以运行时 dump 为准）

| 类别前缀 | 数量 | 位置要点（scad 坐标） | 玩法角色 |
| --- | --- | --- | --- |
| `entrance_01..03` | 3 | (-18,-12) / (0,-12) / (14,-12) 南墙自动门 | 所有角色进出航站楼的必经点 |
| `checkin_01..06` | 6 | x=-27.0→-14.0 步距 2.6，y=1.2，面南 | 值机员岗位 + 旅客值机服务点（前方排队） |
| `kiosk_01..04` | 4 | x=-10.5，y=0.6→-3.6，面东 | 旅客自助值机（短停留，无队列或短队列） |
| `security_01..04` | 4 | x=-9.3→-2.7 步距 2.2，y=4.6 | 安检员岗位 + 旅客南进北出通过点 |
| `gate_01..06` | 6 | x=-20→20 步距 8，y=27.6 北墙 | 登机口职员岗位 + 旅客登机消失点 |
| `wait_01..12` | 12 | 西簇 x∈[-22,-15.2]、东簇 x∈[9,15.8]，y=16.5/19.5，4 联座 | 候机坐席（每锚点 4 个 seat slot） |
| `cafe_01` / `food_01` | 2 | (-7,12.5) 咖啡岛 / (-26.8,10.2) BURGER | 店员岗位 + 旅客消费点 |
| `shop_01` / `book_01` / `gift_01` | 3 | (4.4,9.4) / (13.4,9.4) / (21.4,9.4) 收银台 | 店员岗位 + 旅客消费点 |
| `toilet_01..02` | 2 | (24,17.4) 男 / (24,20.6) 女，东北角 | 旅客/员工如厕（进门消失数十秒再出现） |
| `staff_01..02` | 2 | (26.7,-2.4) / (28.7,-2.4) 东侧办公室 | 主管/休息中的员工落脚点 |
| `info_01` | 1 | (-2.5,-7.5) 大厅胶囊问询台 | 问询员岗位 + 旅客随机问询 |
| `atm_01` | 1 | (-29.55,-7.0) 西墙 | 旅客短停留 |
| `vending_01..05` | 5 | 东西墙沿（空侧×3、陆侧×2） | 旅客/员工买饮料短停留 |

**朝向约定**：所有带朝向的 module front = 局部 -y，布局处用 `rotate` 调整。运行时从节点世界旋转恢复 front 方向；**服务点 = 锚点 + front×偏移，队列从服务点沿 front 继续向外延伸**（§7.3）。

### 2.3 AirportMap（仿 StudioSim::OfficeMap）

新建 `AirportMap` 类：`BuildFromScene(scene)` 扫描节点名，按前缀归类为 `FPointOfInterest{name, category, position, frontDir, occupiedBy}`；提供 `PointsOfCategory()` / `FindByName()` / `ClaimSeat()`（wait 类按 4 slot 细分）。直接抄 OfficeMap 的解析骨架，前缀表换成 §2.2。M0 里程碑要求把解析结果 dump 到日志逐点核对。

---

## 3. 角色阵容与视觉表现

### 3.1 员工 roster（默认配置，全部进 `AirportSimConfig.hpp` 可调）

| 职业 | 人数 | 岗位 POI | 班次 | 在岗行为要点 |
| --- | --- | --- | --- | --- |
| 值机员 | 4 | checkin_01..04（柜台 5/6 默认关闭） | 早/晚各 2 | 站柜台后，逐个服务队首旅客（计时 8~15 游戏分钟/人） |
| 安检员 | 4 | security_01..04 | 早/晚各 2（各开 2 条通道） | 站通道旁，旅客逐个通过；偶发"复检"延迟 |
| 登机口职员 | 2 | 按航班动态调度到对应 gate | 早/晚各 1 | 航班登机前 30 分钟到 gate 开检票，登机结束回 staff 办公室 |
| 问询员 | 1 | info_01 | 早班 | 坐台，被旅客随机问询时冒对话气泡 |
| 店员 | 5 | cafe/food/shop/book/gift 各 1 | 全天单班（简化） | 站收银后，服务到店旅客；没客人时整理货架/发呆 |
| 保洁 | 1 | 全场巡回 | 夜班为主+白天补位 | 在 POI 间巡回，每处停留拖地 1~2 分钟 |
| 保安 | 1 | 全场巡逻路线 | 夜班 | 沿大厅→安检→空侧→登机口环线巡逻 |

合计在场员工峰值约 12~15。早班 05:30–13:30、晚班 13:30–21:30，交接时新班次员工**先走到岗位**、旧员工再离岗（可观察到"交接"瞬间）。员工通勤：从停车场/公交站生成 → 斑马线 → entrance → 岗位；下班反向走出场景边缘消失。

### 3.2 旅客

- 由航班表（§4.3）驱动生成：每班 6~10 人，在起飞前 90~40 游戏分钟内陆续从马路侧（出租车/公交/停车场三选一出生点）生成。
- 个体随机：行走速度 ±15%、性格标签（急躁/悠闲/健谈，进 LLM prompt）、随机配色与体型微缩放、是否走自助 kiosk（30%）、空侧自由时间消费倾向。
- 并发上限 24（`maxConcurrentPassengers`），超限的航班旅客延后生成，保性能与画面密度。

### 3.3 视觉层接口（换装预留，本 MVP 的关键架构约束）

> 状态：**已落地 ScadRig 换装**（见 `docs/ScadRig-Design.md`、`AGENT_GUIDE/ScadRig.md`）。
> 默认 `Config::kUseScadRigVisual = true` 走 `ScadRigVisual`（刚体骨骼角色 + idle/walk/sit/work clip + 职业换色）；
> rig 加载失败或开关关闭时回退 `GeometryVisual` 直立 box。

```
IAgentVisual（纯虚）
 ├── GeometryVisual   // 回退：直立 box + 职业色材质（坐下=压矮 hack）
 └── ScadRigVisual    // 默认：assets/scad/characters/agent_basic.scad 的 ScadRig 实例
                      //   FRigAnimator 播 4 个 clip；tint section 按池位换职业/调色板色
接口面：SetWorldTransform(pos, yaw) / SetAnimHint(EAgentAnimHint) / SetVisible(bool)
        / SetMoveSpeed(m/s)（走路动画相位匹配）/ Tick(dt)（动画推进）
EAgentAnimHint: Idle / Walk / Sit / Work
```

游戏逻辑**只**通过 `IAgentVisual` 与外观交互、只发 `AnimHint`，绝不直接摸 mesh 节点——这是后续换骨骼模型不动玩法代码的保证。头顶气泡/名牌不属于 visual 层，由 UI 层用世界坐标投影绘制（§7.5）。

---

## 4. 时间系统：时钟、日夜、班次与航班表

### 4.1 世界时钟

- 连续循环的游戏内 24h；默认 **1 游戏日 = 12 真实分钟**（1 真实秒 = 2 游戏分钟），可调 0.25×~8×、可暂停（调试面板滑条 + 快捷键）。
- 所有系统（班次、航班、服务计时、决策冷却）一律用**游戏分钟**为单位，杜绝真实秒/游戏秒混用。

### 4.2 日夜光照（已核实的引擎接口）

- `scene.GetEnvSettings()` 暴露 `SunRotation`（方位，0..2 映射一圈）、`SunIntensity`、`SkyIntensity`、`HasSun`。每帧由 TimeSystem 写入：
  - `SunRotation = f(hour)`：06:00→18:00 扫过半圈（东升西落的方位变化）。
  - 强度曲线：白天 `SunIntensity` 全值；黄昏/黎明（05:00–07:00、17:00–19:00）平滑 lerp；夜间 `HasSun=false` 或强度趋零，`SkyIntensity` 降至 ~15% 并可叠加冷色调。
- **已知引擎限制**：`EnvironmentSetting::SunDirection()` 的仰角固定 0.75（只有方位可变）。MVP 用"方位+强度+天光"组合已足够表达日夜；若想要真实日出日落仰角，需小改 `Model.hpp` 增加 `SunElevation` 字段并接入 CSM/PathTracing 取向（engine 层改动，按 AGENTS.md 须连带 `gnb build gkNextRenderer gkNextUnitTests` 验证）——列为 M6 可选项，不阻塞主线。
- 夜间氛围加分项（可选）：夜里调低 carpet 区域曝光、给 prop_light_mast/路灯位置挂少量点光或 emissive 材质提亮。

### 4.3 航班表（FlightBoard）

- 每天 05:00 生成当日 8~12 班**离港**航班：随机分配 `gate_01..06`、起飞时刻（07:00–21:00 均布防扎堆，同 gate 间隔 ≥90 游戏分钟）、旅客数 6~10、航司色。
- 航班状态机：`Scheduled → CheckinOpen(-120min) → Boarding(-30min) → Final(-10min) → Departed(0)`。状态变化向全场广播事件（旅客旅程状态机订阅；也作为 LLM 决策时刻的触发源——"你的航班开始登机了"）。
- Boarding 时该 gate 的登机口职员到岗、gate 排队点激活；Departed 时未登机旅客直接传送登机（MVP 不做误机剧情，日志记一条即可）。
- 可选（M3 末）：每天 2~3 班**到港**航班——一批旅客从 gate 走出、穿过空侧→安检旁出口→entrance 离场，制造双向人流。

---

## 5. 行为架构：旅程状态机（Layer 0）+ LLM 决策（Layer 1）

这是本项目最重要的设计决策。机场角色多（峰值 ~35 个）、流程刚性强（值机→安检→登机不能乱序），如果全部决策都问 LLM，既不可靠也喂不起。因此**分两层**：

```
┌─ Layer 1：LLM 决策层（"做什么有意思的事/说什么"）────────────────┐
│  触发：决策时刻（空侧自由时间开始、相遇、感知事件、员工空闲）        │
│  输出：单个 JSON 动作 {action, target, say, mood}                  │
│  调度：DecisionScheduler 串行 budget（同时在途 1 个请求）           │
│  失败/超时/不可用 → 规则化 fallback（加权随机 + 预制台词库）        │
└──────────────────────────┬─────────────────────────────────────┘
                           ▼ 只能在白名单动作集内选择
┌─ Layer 0：确定性执行层（"怎么把事做对"）───────────────────────────┐
│  旅客旅程状态机 / 员工日程状态机（刚性流程，永不交给 LLM）           │
│  QueueSystem 排队、FNavGrid+FPathFollower 寻路、服务计时、班次       │
└────────────────────────────────────────────────────────────────┘
```

**原则**：Layer 0 保证"机场永远在正确运转"；Layer 1 只决定弹性部分——空侧这 40 分钟先喝咖啡还是先逛书店、遇到同事打不打招呼、排长队时抱怨什么。LLM 挂了，Demo 退化为"规则驱动但依然完整"的版本。

### 5.1 旅客旅程状态机（Layer 0 主线）

```
Spawn(陆侧出生点) → WalkToEntrance → [30%] UseKiosk / [70%] QueueCheckin → CheckinService
  → WalkToSecurity → QueueSecurity → PassSecurity(沿通道局部 y 0→3.1 南进北出)
  → AirsideFree(自由时间 = 距登机的富余时间，进入 Layer 1 决策循环：
       消费(cafe/food/shop/book/gift/vending/atm) ⇄ 如厕 ⇄ 候机椅坐下 ⇄ 闲逛/看 FIDS)
  → [航班 Boarding 广播] WalkToGate → QueueGate → 检票 → 进 gate 门消失(Despawn)
```

- 每个状态固定"到点→排队→服务计时→离开"骨架；AirsideFree 内每完成一个活动回到决策点，剩余时间 <15 游戏分钟时强制收敛到 gate。
- 问询/ATM/售货机是低概率随机插入的短停留，丰富画面。

### 5.2 员工日程状态机（Layer 0）

```
Spawn(停车场/公交站, 班次开始前20min) → 过斑马线 → Entrance → WalkToPost → OnDuty
  OnDuty 内循环：服务队首客人(计时) / 无客人时 Idle(进入 Layer 1：整理货架、和邻柜同事闲聊、喝水)
  → [偶发] 工间休息：去 staff 办公室 / vending 5~10 分钟再回岗
  → 换班时刻：接班者到岗后 OffDuty → Entrance → 走出场景 Despawn
保洁/保安：无固定岗，沿巡逻/巡回路线在 POI 间移动，每点停留作业
```

### 5.3 LLM 决策（Layer 1，复用 StudioSim 模式）

- **服务**：`engine.GetAIService()` → `SwitchProvider(LocalLlama)` → `GenerateTextAsync(prompt, callback)`；配置走 `assets/configs/ai_config.json` 的 `localllm` 段（自动发现 `external/llm/run/server.pid`）。启动：`gnb llm serve`。
- **调度**：照搬 `StudioSim::DecisionScheduler`（串行、同时在途 1 个请求、回调线程→主线程队列回灌）。请求来源两类：
  - **空闲决策**：AirsideFree 的旅客 / Idle 的员工进入待决策池，每角色冷却 ≥20 游戏分钟；
  - **感知事件**（优先级更高）：相遇熟人/同事、队列长度 >6、航班 Boarding/Final、换班交接、夜幕降临等，事件带 3 秒去抖。
- **Prompt 模板**（中文，单次 <600 token）：角色卡（职业/性格/当前状态/剩余时间）+ 世界快照（时刻/天色/所在区域）+ 周边感知列表（≤5 个邻居：谁、职业、在干嘛）+ **白名单动作集**（由 Layer 0 按当前状态给出合法选项）→ 要求只输出一个 JSON。
- **动作 schema**：

```json
{ "action": "goto | use_poi | say_to | emote | idle",
  "target": "<POI 名 或 agent id，可空>",
  "say":    "<≤20 字气泡台词，可空>",
  "mood":   "neutral | happy | tired | annoyed | excited | anxious" }
```

- **校验与 fallback**：JSON 解析失败 / action 不在白名单 / target 非法 → 丢弃并走规则 fallback（按性格加权随机选动作 + 从预制台词库抽 say）。mood 映射头顶表情图标与少量行为参数（annoyed 时走路加速、idle 缩短）。
- **预算预期**：1 game day = 12 real min，串行在途 1 个、单次推理 1~3s，全天约可消化 300~500 次决策，对 ~35 个角色足够（刚性流程不耗预算）。

### 5.4 感知与规则反应（不走 LLM 的即时反应）

- PerceptionSystem 每 0.5s 更新：半径 3m 邻居、同 POI 共处、所在队列长度。
- 纯规则即时反应（零成本、保证帧帧成立）：相向行人 1m 内侧移让路（分离力）、进队列自动站到队尾 slot、被服务时面向服务台、坐下时朝向椅子 front。
- 规则反应负责"physically 合理"，LLM 负责"socially 有趣"，两者不抢活。

---

## 6. 系统架构与模块清单

```
┌────────────────────────────────────────────────────────────────────┐
│ AirportSimGameInstance (NextGameInstanceBase 子类，编排层)           │
│  OnInit / BeforeSceneRebuild / OnSceneLoaded / OnTick / OnRenderUI   │
└──────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────┘
       ▼          ▼          ▼          ▼          ▼          ▼
 ┌─────────┐┌─────────┐┌──────────┐┌──────────┐┌──────────┐┌─────────┐
 │Airport  ││Time     ││Flight    ││Agent     ││Journey   ││Queue    │
 │Map      ││System   ││Board     ││System    ││System    ││System   │
 │POI 解析  ││时钟/日夜 ││航班表/   ││角色池/   ││旅客+员工 ││队列 slot │
 │/占用     ││/班次    ││广播      ││mover/视觉││状态机    ││/服务计时 │
 └─────────┘└─────────┘└──────────┘└────┬─────┘└────┬─────┘└─────────┘
                                        │           │
 ┌──────────────┐  ┌──────────────┐     │           │
 │Perception    │  │Decision      │◄────┴───────────┘
 │System 感知/事件│→│Scheduler LLM │   （决策时刻入队）
 └──────────────┘  └──────┬───────┘
                          ▼
        NextAI::FAIService (GenerateTextAsync, localllm)
 ┌────────────────────────────────────────────────────────────────────┐
 │ 复用层：NextGameplay (FNavGrid/FPathFollower) + SCAD loader          │
 │        + Scene::GetEnvSettings() 日夜 + ImGui (气泡/HUD/调试面板)    │
 └────────────────────────────────────────────────────────────────────┘
```

### 6.1 文件结构（`src/Application/Game/AirportSim/`）

| 文件 | 职责 |
| --- | --- |
| `AirportSimGameInstance.{hpp,cpp}` | 入口编排：加载 scad、建 NavGrid、tick 各系统、相机控制 |
| `AirportSimTypes.h` | 枚举（EAgentRole/EPassengerState/EStaffState/EFlightState/EAgentAnimHint/EMood）+ POD 结构 + `GetXxxName()` |
| `AirportSimConfig.hpp` | 全部数值配置（纯 C++ 结构体，照 CharacterDemoConfig 惯例）：roster、时钟、航班、决策预算 |
| `AirportMap.{h,cpp}` | SCAD 锚点解析（仿 OfficeMap，§2.3） |
| `TimeSystem.{h,cpp}` | 世界时钟 + 日夜光照写 EnvSettings + 班次表 |
| `FlightBoard.{h,cpp}` | 航班生成/状态机/广播 |
| `AgentSystem.{h,cpp}` | 角色池（spawn/despawn）、kinematic mover、`IAgentVisual` + `GeometryVisual` |
| `JourneySystem.{h,cpp}` | 旅客旅程状态机 + 员工日程状态机（Layer 0） |
| `QueueSystem.{h,cpp}` | 队列 slot 链生成/占用/推进、服务计时 |
| `PerceptionSystem.{h,cpp}` | 邻居/事件检测、去抖、决策时刻入队 |
| `DecisionScheduler.{h,cpp}` | LLM 串行调度 + prompt 组装 + JSON 校验 + fallback（参考 StudioSim 同名类） |
| `AirportSimUI.{h,cpp}` | 头顶气泡/名牌投影绘制、HUD（时钟/航班表）、调试面板 |

CMake：在 `src/CMakeLists.txt` 按 CharacterDemo/StudioSim 的样子注册 `AirportSim`，链接 `NextGameplay`。验证只需 `./gnb.bat build AirportSim`（engine 层若动了 EnvironmentSetting 才连带 `gkNextRenderer gkNextUnitTests`）。

---

## 7. 关键技术设计细节

### 7.1 SCAD 场景加载与坐标

- `OnInit` 里 `GetEngine().RequestLoadScene({.filename = "assets/scad/airport.scad"})`（StudioSim 同款入口）。
- scad 为 Z-up，引擎玩法在 XZ 平面（yaw = `atan2(dir.x, dir.z)`，CharacterDemo 惯例）。**不要手写坐标换算**：锚点世界坐标一律取加载后节点的 WorldTranslation/旋转（OfficeMap 已验证这条链路），M0 用 dump+截图核对。
- 锚点 front 方向：从节点世界旋转变换局部 -y 得到；每类 POI 的服务点偏移/队列方向/坐席 slot 偏移写成 C++ 常量表（`AirportMap` 内）。

### 7.2 导航与移动

- `FNavGrid::Build()` 覆盖全场 84×80（含陆侧室外），cell 建议 0.4~0.5m；场景静态，**一次构建**，无需脏区域重建。
- **不给群体角色上物理胶囊**（35 个 NextCharacterController 不值当）：kinematic mover = A* 路径 + `FPathFollower` 跟随 + 邻居分离力（半径 0.6m，简单 boids-separation）+ NavGrid 地面高度采样贴地。每帧直接写节点 transform + yaw。
- 安检"南进北出"的单向性不靠 NavGrid 表达：PassSecurity 状态是脚本化走点（通道入口→X 光带旁→金属门→北出口），其余路径规划把安检带视为不可走区、只能经由状态机进入（NavGrid 在通道格子上打"仅脚本通行"掩码，或简单地把通道两侧家具自然形成的窄口交给 A*——M2 实测后择一）。
- 门（entrance/gate/toilet）：穿过门线即触发 spawn/despawn/隐身，不做开门动画。

### 7.3 排队系统

- 每个服务 POI 生成队列 slot 链：`slot[i] = 服务点 + frontDir × (0.9 + 0.8×i)`，默认 8 个 slot；checkin/security 的队列方向按现场 stanchion 走向（scad 里已有隔离柱）可在常量表里给每 POI 单独覆写折线。
- agent 申请队列 → 占据最后空 slot → 前方释放时整队前移（带 0.3~0.8s 随机延迟，避免机器人感）→ 队首进入服务计时 → 完成后释放。
- 队列长度暴露给 PerceptionSystem（触发"抱怨"决策时刻）与值机逻辑（旅客选最短队）。

### 7.4 性能与规模

- 峰值 ~35 agent × 几何体（每个 3~5 个 primitive 节点），对引擎是小负载；分离力 O(n²) 在 n=35 可接受，PerceptionSystem 复用同一份邻居查询结果（每 0.5s 一次，网格分桶可后补）。
- LLM 回调在内部线程触发（StudioSim 已踩过）：**回调里只往无锁/加锁队列塞结果，主线程 OnTick 消费**，绝不在回调里碰 Scene。

### 7.5 气泡与调试 UI（全 ImGui，无美术依赖）

- 头顶气泡：世界坐标 → 屏幕投影，ImGui DrawList 画圆角矩形 + 文字 + mood 表情符号；台词显示 4~6s 后淡出；同屏气泡上限 ~8（按距相机排序裁剪）。
- HUD：左上世界时钟 + 时间倍速；右上迷你航班表（FIDS 同步：航班号/gate/状态色）。
- 调试面板（F8，沿 CharacterDemo 惯例）：时间调速/跳时段、agent 列表（点击→相机跟踪 + 显示其状态机/最近一次 LLM prompt&response）、决策日志滚动窗、NavGrid/路径覆盖层开关、LLM 开关（强制 fallback 模式做对照演示）。
- 观察相机：自由飞 + 等距俯视预设（贴近 Jumbo Airport Story 视角）+ 跟踪锁定。

---

## 8. 一天的演示时间线（验收叙事基准）

| 游戏时刻 | 画面 |
| --- | --- |
| 05:00 | 夜色，保安在空侧巡逻，保洁在大厅拖地；FlightBoard 生成今日航班 |
| 05:30 | 天际转亮；早班员工从停车场/公交站陆续过斑马线进 entrance，各自走向岗位 |
| 07:00 | 第一班旅客生成：排队值机、kiosk 自助、安检通过；阳光斜照进玻璃幕墙 |
| 09:00–12:00 | 高峰：4 条安检队伍、空侧咖啡/零售热闹、候机椅大半坐满、气泡此起彼伏 |
| 13:30 | 换班：晚班员工到岗、早班走人，柜台前可见交接 |
| 17:30–19:00 | 日落：光照转暖再转暗、SkyIntensity 下降；晚高峰登机 |
| 21:00 | 末班机 Departed；旅客清空 |
| 21:30 | 晚班下班离场；夜灯氛围（可选项）；保安/保洁接管 |
| 24:00→05:00 | 静谧空场快进感（观察者可调 8× 跳过）→ 循环 |

---

## 9. 开发里程碑（M0–M6，每步可独立验收）

> 建议接手 agent 把每个里程碑拆成 `.spec/TODO.md` 任务走 Spec Workflow；每步完成都要过"验收"栏再进下一步。验证命令统一见 §11。

| # | 里程碑 | 内容 | 验收标准 |
| --- | --- | --- | --- |
| **M0** | 工程骨架 | 新建 target + CMake 注册；加载 airport.scad；`AirportMap` 解析全部锚点并 dump 日志；自由相机 | `gnb run AirportSim` 出场景且日志列出 §2.2 全部锚点（数量/坐标核对）；`gnb shot --target AirportSim` 截图正确 |
| **M1** | 时间与日夜 | TimeSystem 时钟 + 调速/暂停；SunRotation/SunIntensity/SkyIntensity 按 §4.2 曲线驱动；HUD 时钟 | 8× 速度看一整天，光照连续平滑、无跳变；截图对比 06:00/12:00/18:00/24:00 四时段 |
| **M2** | 导航与移动 | NavGrid 构建 + 调试覆盖层；AgentSystem + GeometryVisual + kinematic mover + 分离力；测试指令"spawn 10 个随机游走 agent" | 10 个 agent 在航站楼内任意 POI 间穿梭 5 分钟：不穿墙/不穿家具/不重叠卡死 |
| **M3** | 旅客旅程 | FlightBoard + QueueSystem + 旅客状态机全链（§5.1）+ 登机消失 | 完整一天 fallback 模式（无 LLM）：所有旅客走完值机→安检→候机→登机，无人卡死/丢失；队列推进自然 |
| **M4** | 员工生态 | 员工 roster + 班次通勤 + 各岗位 OnDuty 行为 + 保洁保安巡逻 + 换班 | 完整一天：员工准时上岗/交接/下班，值机安检登机口被员工真实"服务驱动"（无员工在岗时柜台不工作） |
| **M5** | LLM 决策层 | DecisionScheduler + prompt 模板 + JSON 校验 + fallback + 气泡 UI + PerceptionSystem 事件 | `gnb llm serve` 开启后：相遇寒暄/长队抱怨/空侧消费选择可观察且台词贴角色；杀掉 llama-server，演示无缝退化到预制台词 |
| **M6** | 打磨与验收 | 观察相机预设/跟踪、调试面板补全、夜灯氛围（可选）、SunElevation 引擎扩展（可选）、性能过查、§1.4 全流程验收录屏 | §1.4 成功标准逐条打钩；8× 连跑 3 个游戏日无崩溃无泄漏迹象 |

里程碑依赖：M0→M1/M2 可并行→M3→M4→M5→M6。M5 之前全部系统**必须**在 fallback 模式下完整可演示——这是"LLM 是点缀不是地基"的硬性体现。

---

## 10. 风险与备选方案

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| LLM 输出不合法/慢/server 未启动 | 气泡哑火、决策停滞 | 白名单校验 + 超时 10s 弃单 + 规则 fallback 永远在线（M3/M4 先于 LLM 交付） |
| 决策预算不够分（角色多） | 角色"呆滞" | 刚性流程不耗预算；冷却+优先级队列；fallback 填缝；必要时调小 roster |
| NavGrid 在家具密集区/玻璃幕墙采样出碎格 | 卡路径 | cell 0.4 实测调参；对已知走廊画"强制可走"矩形白名单；M2 验收专测 |
| 安检单向流被 A* 绕过 | 旅客逆行穿安检 | 通道格子打脚本通行掩码（§7.2），状态机控制唯一入口 |
| 几何体角色表现力不足 | 观感单调 | mood 表情图标 + 气泡 + AnimHint 微动作（坐下变矮、工作时小幅摆动）；第二阶段换骨骼模型（接口已预留） |
| SunDirection 仰角固定 | 日落不够"日落" | 强度+天光曲线先顶上；M6 可选小改 engine（带全量验证） |
| 与 StudioSim 代码重复（DecisionScheduler 等） | 维护两份 | MVP 先复制改造（避免牵连 StudioSim）；跑通后再评估沉到 NextGameplay/NextAI 共享层 |

---

## 11. 验证方式

```bash
# 构建与运行（Windows；macOS/Linux 去掉 .bat）
./gnb.bat build AirportSim
./gnb.bat run AirportSim          # 日志见 "uploaded scene [...] to gpu" 即初始化通过

# 视觉验收（不弹窗、自动退出、读 agent_validation.jpg）
gnb shot --target AirportSim --scene assets/scad/airport.scad --frames 90

# LLM 在线/离线两态都要验
gnb llm serve && ./gnb.bat run AirportSim     # 在线
gnb llm stop  && ./gnb.bat run AirportSim     # fallback

# 若动了 Engine 层（如 SunElevation）
./gnb.bat build gkNextRenderer gkNextUnitTests
```

纯逻辑模块（FlightBoard 排程、QueueSystem 推进、旅程状态机迁移、JSON 动作校验）建议配 Catch2 单测挂进 `gkNextUnitTests`（tag `[AirportSim]`），它们不依赖 Vulkan，测试成本低、对 agent 自验最有用。

## 12. 参考

- `docs/StudioSim-MVP-Plan.md` + `src/Application/Game/StudioSim/`：OfficeMap/DecisionScheduler/气泡 UI/LLM fallback 的直接先例
- `AGENT_GUIDE/CharacterDemo.md`：NavGrid/PathFollower/CharacterActor 用法、F8 调试面板惯例
- `AGENT_GUIDE/SCADLoader.md`：scad 解析与节点命名语义
- `assets/scad/airport.scad` 头部注释：锚点命名约定权威来源
- `AGENTS.md`：构建/命名/验证全局规范（targeted build，勿全量）

