# StudioSim —— 产出与进度系统改进方案（打磨前结构性调整）

> **状态**：设计草案（待评审 → 交后续 agent 实现）
> **目标读者**：负责打磨 StudioSim 的后续 AI agent / 开发者
> **前置必读**：[`StudioSim-MVP-Plan.md`](StudioSim-MVP-Plan.md)（原始 MVP 计划）、本仓 `src/Application/Game/StudioSim/` 全部源码
> **设计北极星**：开罗游戏《游戏发展国 / Game Dev Story》的"研发点数 + 工序阶段 + 可见浮字"产出循环
> **一句话**：把"员工围着目标漫游、不停说话、天黑给个幻觉评分"改成"项目有可量化的产出仪表，员工的工作真正推动仪表、并据此减少废话、增加连续性"。

---

## 0. TL;DR（给赶时间的实现者）

当前 demo 的三个病根本质上是**同一个缺失**：**整个模拟没有"项目产出状态"这个对象**。员工的"工作"不产生任何可观测的东西，于是：

1. 员工**对进度无感知** → 因为根本没有进度可感知，决策 prompt 里也没有进度字段（`DecisionScheduler.cpp` 的 `BuildPrompt`）。
2. 员工**话太多、不连续** → 因为每次决策都被强制要求输出 `dialogue`，且 `FEmployee` 没有任何记忆字段，每句话都是无上下文的独立产物。
3. **没有可量化指标** → `FDailyGoal` 只有 `title/description/set`，原计划里的 `progress` 字段被丢掉了；唯一的数字是结算时 LLM 凭员工情绪"编"出来的 0–100 分（`GoalSystem::Summarize`）。

**解法主线**：引入 `FProjectState`（项目产出状态）。员工 `WORK` 动作按职位真实地往项目仪表加点 → 仪表驱动 HUD 进度条与头顶 `+N` 浮字（量化，解决问题 3）→ 把仪表与"剩余工作"喂回决策 prompt，让员工围绕"补哪块短板"决策（感知，解决问题 1）→ 大部分工作是**静默加点**，对话改为"里程碑/事件/被搭话"触发，并加一个短记忆环让发言成串（节流 + 连续，解决问题 2）。

**第四块（用户追加，与上面同等重要）**：把"开不起来"的会议/茶水间升级为 **GatheringSystem（群体聚集与决策系统）**——可并发的小范围聚集（不再强制全员）、由项目状态**自然触发**（仪表落后/bug 堆积/阶段切换/进度停滞 → 开会；情绪低落/里程碑 → 茶水间），一次聚集 = 一次 LLM 生成的**多人轮流对白**，会议还产出**结构化群体决策建议（经玩家【采纳/否决】确认后回灌）**（限时仪表加成 / 改 todayTask / 进全员短记忆，不改玩家选定的总目标）。这让"开发过程中有多人交谈和决策"真正发生，且每场聚集都有后果（详见 §3.7）。

---

## 1. 现状分析（代码级根因）

> 下面每条都定位到具体文件，方便实现者直接改。

### 1.1 问题一：员工对项目整体进度不清楚，工作只是"表面附和"

**根因：模拟里不存在"项目"这个实体。**

- `FDailyGoal`（`StudioSimTypes.h:162`）只有 `title / description / source / set`。原 MVP 计划 §7 里设计过的 `float progress`、`roleTasks`、`focusRoles` 全部没落地。目标只是一句文字，没有任何状态。
- `FEmployee`（`EmployeeSystem.h:30`）有一个 `todayTask`（目标分解出的一句话），但它**只在晨会写一次，全天不更新**，也不和任何产出挂钩。
- "工作"的实现就是站到工位 POI：`EmployeeSystem::Tick`（`EmployeeSystem.cpp:188`）→ `ScheduledPoi` 返回 `homeDeskPoi`（`DaySchedule.h:11`）。**站在工位上不产生任何东西**——没有点数、没有完成的子任务、没有交付物。
- 决策 prompt（`DecisionScheduler.cpp:21` `BuildPrompt`）喂给 LLM 的信息只有：职位、性格、时间、目标标题/描述、`todayTask`、事件、邻座同事、可去点位。**完全没有"项目做到哪了 / 还差什么"**。LLM 被问的永远是"选一个下一步动作"，它无从判断进度，只能产出"对齐目标"的表面动作——这正是用户说的"表面附和"。

**结论**：不是 prompt 调教问题，是**缺一个产出状态机**。没有"做了多少、还差多少"，再怎么调 prompt 员工也只能附和。

### 1.2 问题二：员工说话太多，且不连续

**根因：每次决策都强制产出对白 + 零记忆。**

- `BuildPrompt`（`DecisionScheduler.cpp:76`）的 JSON schema 把 `dialogue` 设成**必填**字段（"一句不超过15字的话"），没有"可以沉默"的语义。于是每次决策必冒一句话。
- 决策频率：`kDecisionIntervalMinutes = 60`（`DecisionScheduler.cpp:19`）。每个员工每 60 游戏分钟决策一次。默认 `timeScale=5`（`StudioSimTypes.h:96`，UI slider 最高到 240，见 `StudioSimGameInstance.cpp:561`）：60 游戏分钟 ≈ **12 真实秒**就轮一圈，6 人轮流冒泡 → 屏幕几乎一直在弹气泡。
- **没有任何记忆**：`FEmployee` 没有 `shortMemory`（原计划 §8.4 设计过、实现时丢了）。每次决策是**完全无状态**的，LLM 看不到自己上一句说了什么、最近在干嘛、别人说了什么（除了 `pendingFrom/pendingText` 这一次性的 TALK 回应字段）。所以第 N 句和第 N-1 句毫无关系 → 散、不连续。
- 气泡只在"下次决策"或"会议结束"时才被覆盖/清空（`DrawWorldOverlay` `StudioSimGameInstance.cpp:523`），导致旧话长期悬浮。

### 1.3 问题三：整体项目进度没有可量化指标

**根因：全程没有任何数字在累积。**

- 白天没有任何进度量。`FDailyGoal` 没有 `progress`。
- 唯一的数字是结算分：`GoalSystem::Summarize`（`GoalSystem.cpp:234`）把员工**情绪字符串**（"Alice（engineer，focused）"）拼进 prompt，让 LLM 给 0–100 分。**这个分和员工实际做了什么毫无因果关系，是纯幻觉**，且只在天黑出现一次。
- HUD（`OnRenderUI` `StudioSimGameInstance.cpp:541`）显示时间/阶段/目标标题/情绪/事件，**没有进度条、没有点数、没有完成度**。

### 1.4 问题四（用户追加）：多人会议 / 茶水间闲聊开不起来，开发途中没有群体交谈与决策

**根因：聚集既缺触发源，又是全员单例，还没有后果。**

- **常态下根本不触发**：会议只在两种情况发起——目标标题命中关键词 `GoalNeedsMeeting`（`StudioSimGameInstance.cpp:76`）或注入特定事件 `EventNeedsMeeting`（`StudioSimGameInstance.cpp:83`）。开发途中（仪表落后、bug 堆积、进度停滞）**没有任何触发源**，所以会议几乎不开。
- **"茶水间多人闲聊"其实不存在**：`ScheduledPoi`（`DaySchedule.h:14`）只是在 12:00–13:00 / 15:30–16:00 把所有人**送到** pantry 站着，**没有任何对话或分组逻辑**——是"一起站着"，不是"一起聊"。
- **全局单例 + 强制全员**：`meeting_` 是单数（`StudioSimGameInstance.hpp:66`），`StartMeeting` 遍历**所有员工**占座（`StudioSimGameInstance.cpp:360`）。无法"3 个人小范围碰一下、其他人继续干活"——要么全员开会，要么没人开会。
- **没有任何决策/后果**：`TickMeeting` 把 `meeting_.lines` 逐句播完后，所有人清 `overrideTargetPoi` 各自散开（`StudioSimGameInstance.cpp:440`）。**台词是纯演出，不改变任何项目状态**——所以"群体决策"从未真正发生。
- **对白与正事脱节**：会议台词由一次性 LLM 调用生成，散会后员工的个人决策完全看不到会上说过什么（没有记忆回灌），决策与执行断裂。

**结论**：要让"开发过程中有多人交谈和决策"，需要把单例 meeting 升级为**可并发、由项目状态自然触发、产出真实决策并回灌**的聚集系统（§3.7）。

---

## 2. 借鉴《游戏发展国》的机制映射

游戏发展国之所以"有进度感"，靠的是三件套，正好对应我们的三个病：

| 游戏发展国机制 | 作用 | 映射到 StudioSim |
| --- | --- | --- |
| **研发点数**（乐趣/创新/画面/技术 四类，员工边工作边产出，按职位侧重不同类） | 把"工作"变成可累积的数字 | `FProjectState` 的 4 个产出仪表；员工 `WORK` 按职位加点（问题 1+3） |
| **头顶 `+N` 浮字**（每次出点都飘一个数字/图标） | 让产出**肉眼可见**、即时反馈、且**不需要说话**就能表达"我在推进" | 头顶浮动 `+N 技术` 数字，替代大部分对白（问题 2+3） |
| **工序/阶段**（企划→编程→美术→音乐→**调试除虫**→上线；每阶段一根进度条） | 把"一天"切成有结构的推进、有"还剩多少"的明确信号 | `EProjectStage` 阶段机 + 每阶段进度条 + **bug 计数**（"剩余工作"的具象化）（问题 1+3） |
| **Bug（小虫图标，调试阶段冒出来、必须清掉才能发布）** | 具象的"未完成工作"，制造张力 | `bugCount`：QA/测试动作发现 bug、工程师修 bug；上线前必须清零（问题 1） |
| **deadline / 一鼓作气 等节奏事件** | 周期性的戏剧节奏 | 复用现有 `EventSystem`；事件改为对仪表/bug 的真实冲击 |
| **员工组合 / 士气 / 团队加成时刻**（多名特定职位同台产出额外效果、休息回血再"一鼓作气"） | 团队层面的戏剧高潮与张弛节奏 | **GatheringSystem**：会议产出群体决策给仪表"专注加成"，茶水间闲聊回 mood→提产能（§3.7） |

**关键转译**：游戏发展国里玩家是"上帝视角点菜单"，而 StudioSim 里是"LLM 员工自己决策"。所以我们不是把玩家操作搬过来，而是把**点数系统作为员工决策的输入与产出**——员工看着仪表决定补哪块短板，工作产出点数，点数推动阶段，阶段与短板回灌进 prompt。形成闭环。

---

## 3. 设计方案

### 3.1 新增核心：`FProjectState`（项目产出状态）

这是整个改进的地基。放 `StudioSimTypes.h`，由 GameInstance 持有一份（每天/每项目一份）。

```cpp
namespace StudioSim
{
    enum class EProjectStage
    {
        Planning,   // 企划：定方向（晨会目标即此阶段产物）
        Production, // 生产：编程/美术/设计/音效并行产出
        Polish,     // 打磨/调试：清 bug、提质量
        Done        // 完成（达到目标阈值或 18:00 结算）
    };

    // 四类产出仪表（沿用游戏发展国的"点数"心智）。
    struct FProjectMeters
    {
        float tech    = 0.0f; // 技术/工程（engineer 主产）
        float design  = 0.0f; // 玩法/设计（designer 主产）
        float art      = 0.0f; // 美术/画面（artist 主产）
        float polish  = 0.0f; // 品质/稳定（qa 主产、pm 协调加成）
    };

    struct FProjectState
    {
        EProjectStage stage = EProjectStage::Planning;
        FProjectMeters meters;        // 已累积点数
        FProjectMeters targetMeters;  // 本目标达成所需阈值（晨会按目标类别设定）
        int   bugCount = 0;            // 待修 bug（Polish 阶段的"剩余工作"）
        int   bugsFixed = 0;
        float overallProgress = 0.0f; // 0..1，由 meters/targetMeters + bug 综合算出
        bool  shipped = false;        // 是否已"交付"（达成）
    };

    // 单次 WORK 产出（员工每个工作 tick 产生，用于浮字 + 累积）。
    struct FWorkOutput
    {
        std::string meter;  // "tech"/"design"/"art"/"polish"
        float       amount = 0.0f;
        bool        foundBug = false; // QA 可能"发现 bug"而非加点
        bool        fixedBug = false; // engineer 在 Polish 阶段修 bug
    };
}
```

**`overallProgress` 计算**（建议实现，可调）：

```
metersDone   = clamp( Σ min(meters.x, target.x) / Σ target.x , 0, 1 )   // 四类点数完成度
bugPenalty   = (stage==Polish 或 Done) ? bugCount / (bugCount+bugsFixed+1) : 0
overall      = metersDone * (1 - 0.3*bugPenalty)
```

即：点数攒够 + bug 清掉 = 进度满。这给玩家一个**真实、单调递增、可解释**的百分比。

### 3.2 员工产出模型（让"工作"真正产生东西）

把"站在工位"升级为"站在工位 → 周期性产出点数"。

- **职位 → 主产仪表映射**（放配置，见 §6）：
  `engineer→tech`、`designer→design`、`artist→art`、`qa→polish(+发现bug)`、`pm→给全员加 10% 协调系数（不直接产点，产"协调点"折算到最短板）`、`boss→士气加成`。
- **产出节拍**：员工处于 `WORK` 动作且站在自己/合适工位上时，每 N 游戏分钟（建议 15）产出一次 `FWorkOutput`。产量 = `基础值 × 情绪系数 × pm协调系数 × 随机抖动`。情绪 `focused/excited` 增产，`stressed/bored/panicked` 减产——**让情绪有了机械意义**，而不只是显示。
- **阶段推进**：
  - `Planning → Production`：晨会目标确定即切换（已有逻辑，`GoalSystem` Active 时）。
  - `Production → Polish`：四类 meters 都达到 `targetMeters` 的 ~80% 时自动切；切入时按已累积量**生成一批 bug**（`bugCount += f(总点数)`）。
  - `Polish → Done`：`bugCount==0` 且 meters 达标 → `shipped=true`。
  - 18:00 强制结算：无论到哪阶段，用当前 `overallProgress` 出分。
- **Bug 循环**（Polish 阶段才活跃）：QA 的 `WORK` 有概率 `foundBug=true`（bugCount++）；engineer 的 `WORK` 在 Polish 阶段变成"修 bug"（bugCount--，bugsFixed++）。这把"还差多少"变成屏幕上能数的东西。

> **fallback 纪律**（沿用 MVP §5.3）：产出系统是**纯确定性的本地计算**，不依赖 LLM。即使拔掉 llama-server，员工走脚本日程站到工位上，仪表照样涨、进度条照样动、结算照样有真实分。LLM 只决定"去哪/补哪块/说不说话"，不决定点数。

### 3.3 员工进度感知（把仪表喂回决策）

改 `DecisionScheduler::BuildPrompt`，注入项目状态，让决策从"附和目标"升级为"针对短板推进"。

新增 prompt 段（控制在 ~80 token 内，符合 MVP §5.4 预算）：

```
[项目进度]
阶段：生产期。总进度 42%。
还缺：技术 60/100、美术 30/100 偏低；玩法 85/100、品质 50/100。
（Polish 阶段时追加）待修 Bug：7 个。
你最近的产出：技术 +12（今天累计）。
```

- 数据来源：`FProjectState` + 每员工累计产出（在 `FEmployee` 加 `float contributedTech` 等，或一个 `FProjectMeters myContribution`）。
- **决策语义升级**：prompt 明确要求"优先补**最低的那块仪表**，或在 Polish 阶段优先修 bug"。这样工程师在技术落后时会去工位猛干、在 bug 多时去救火；美术落后时美术加班——**行为随项目状态变化**，而不是无脑站工位。
- 这一步直接解决"表面附和"：员工现在有了**可推理的世界状态**和**明确的短板信号**。

### 3.4 对话节流 + 连续性

**目标**：把"每次决策必说话"改成"大部分时间静默产出、关键时刻才发言、且发言成串"。

**(a) 静默产出替代闲聊**
- WORK 的常规产出**不出对白，只出头顶 `+N` 浮字**（§3.5）。`dialogue` 在 prompt 里从必填改为**可选**，并显式给"沉默"语义：`"dialogue":"<可留空；只在有要紧事时说>"`。
- 在 `ApplyResult`（`DecisionScheduler.cpp:169`）里加**发言闸门**：只有满足触发条件才把 `dialogue` 显示成气泡，否则丢弃。

**(b) 发言触发条件（whitelist）**——只有这些时刻允许冒对白：
1. 跨过里程碑（某仪表达标 / 进度过 25/50/75% / 阶段切换 / `shipped`）。
2. Polish 阶段发现或修掉 bug。
3. 被玩家事件波及后的第一次决策（已有事件插队逻辑）。
4. 被同事 TALK 搭话（已有 `pendingFrom` 逻辑）。
5. 会议中（已有 `TickMeeting`）。
6. 低频"性格闲聊"配额：每员工每游戏小时**至多 1 句**随机闲聊（用 `nextChatterAt` 计时器限流），且话痨性格配额高、`沉默寡言` 配额接近 0。

**(c) 短记忆环（连续性）**
- 给 `FEmployee` 加 `std::vector<std::string> shortMemory`（环形，保留最近 3–4 条，如 `"10:30 在工位补技术 +12"`、`"竞品发布，去会议室讨论"`、`"Bob 说先修 bug"`）。
- 每次决策**把 shortMemory 拼进 prompt 末尾**（原计划 §8.4 的设计，补回来）。每次 apply 后**往 shortMemory push 一条本次摘要**。
- 效果：LLM 看得到自己刚说过/做过什么、别人刚说了什么 → 发言能接上文，形成"线程"而非散点。

**(d) 气泡生命周期**
- 气泡显示 N 秒后自动淡出清空（加 `bubbleClearAt` 计时），不再长期悬浮。

### 3.5 量化表现层（UI）

游戏发展国的"游戏感"九成来自可见的数字反馈。要加：

- **HUD 进度区**（`OnRenderUI`）：
  - 一根**总进度条** `overallProgress`（带百分比）。
  - 四根**仪表小条**：技术/玩法/美术/品质（当前 / 阈值）。
  - 阶段徽章：企划 / 生产 / 打磨 / 完成。
  - Polish 阶段显示 **🐛 Bug: N** 计数。
- **头顶 `+N` 浮字**（`DrawWorldOverlay`，复用现有 world→screen 投影）：员工每次产出时，在头顶生成一个**向上飘 + 淡出**的 `+12 技术`（按仪表着色）。这是替代对白的主要"我在干活"信号。需要一个轻量浮字粒子列表（`{worldPos, text, color, ageSeconds}`，每帧上飘+衰减）。
- **每员工行**（已有列表 `StudioSimGameInstance.cpp:599`）补一列"今日产出"（如 `tech+34`）。
- **结算面板**：展示真实达成度（各仪表完成比、bug 清理情况）+ 总分，分数由 §3.6 计算而非 LLM 幻觉。

### 3.6 结算改造（真实分数）

改 `GoalSystem::Summarize`：

- **分数 = 本地计算**：`score = round(100 * project.overallProgress)`，再叠加少量修正（如按时 shipped +bonus、bug 残留 -penalty）。**不再让 LLM 决定分数。**
- LLM **只负责生成一句有人味的点评文字**（输入真实数字：进度 X%、技术达标、美术欠缺、剩 N 个 bug），可选；失败就用模板文字。这样结算既准确又仍有"叙事"。
- 多天钩子：把今日 meters/bug 残留写进"项目累计状态"，作为次日 Briefing 输入（原计划 §10.5 的扩展钩子，现在有了真实数据可传）。

### 3.7 群体聚集与决策系统（GatheringSystem）★

**目标**：让"多人会议"和"茶水间多人闲聊"在开发途中**自然地开起来**，且会议能产出**真实的群体决策**回灌项目状态。把现有的全局单例 `meeting_` 替换为可并发的"聚集"列表。

#### 3.7.1 核心数据结构

放 `StudioSimTypes.h`，由新增的 `GatheringSystem`（`GatheringSystem.{h,cpp}`）管理。

```cpp
enum class EGatheringKind  { Meeting, Pantry };                       // 决策会议 / 茶水间闲聊
enum class EGatheringState { Forming, Talking, Deciding, Dispersing }; // 集合中→发言中→出决策→散会

struct FGroupDecision   // 会议产出的结构化决策（LLM 给，落到项目状态）
{
    std::string summary;                 // 一句话决议，如"砍美术全力修bug"
    std::string focusMeter;              // 团队接下来集中补哪块 "tech/design/art/polish"
    std::vector<std::pair<std::string,std::string>> reassign; // 可选：员工名→新的 todayTask
    bool        valid = false;
};

struct FGathering
{
    EGatheringKind  kind = EGatheringKind::Meeting;
    EGatheringState state = EGatheringState::Forming;
    std::string     topic;                 // 议题/话题
    bool            awaitingConfirm = false; // 会议出决策后等玩家确认（见 §3.7.4 / Q8）
    std::string     anchorCategory;        // "meet" / "pantry"
    std::vector<size_t> participants;      // 参与员工索引（2–5 人，非全员）
    double          startGameMinutes = 0.0;
    double          endGameMinutes = 0.0;
    std::vector<FMeetingLine> lines;       // 多人轮流对白（复用现有 FMeetingLine）
    size_t          nextLineIndex = 0;
    double          nextLineRealSeconds = 0.0;
    FGroupDecision  decision;              // 仅 Meeting 用
    uint64_t        generation = 0;        // 异步回调防过期（同现有 meetingGeneration_）
};
```

`FEmployee` 增 `int gatheringId = -1;`（>=0 表示正被某场聚集占用，生产/决策系统跳过它）。

#### 3.7.2 组织触发（让聚集自然发生）

`GatheringSystem::Tick` 每隔一段（建议每 30 游戏分钟）评估一次，依据 `FProjectState` 与员工情绪决定是否发起聚集。**这是解决"开不起来"的关键**——触发源来自项目状态，而非仅关键词/事件。

**会议（决策型，PM 当召集人，拉相关 2–4 人）**，满足其一即发起：

| 触发 | 参与者 | 议题 |
| --- | --- | --- |
| 某仪表 < 目标 40% 且当日过半 | 该仪表主产职位 + PM | "补短板：xx 进度落后怎么办" |
| Polish 阶段 `bugCount` 超阈值 | engineer + QA + PM | "救火分工：bug 太多" |
| 阶段切换 Production→Polish | PM + 各职位 1 人 | "转入打磨，定收尾范围" |
| `overallProgress` 一段时间几乎没涨（停滞） | PM + 进度最慢职位 | "复盘：为什么卡住了" |
| 玩家事件（复用 `EventNeedsMeeting`） | 受影响职位 + PM | 现有逻辑 |

**茶水间闲聊（社交型，自发）**，满足其一即发起：

| 触发 | 参与者 |
| --- | --- |
| ≥2 名员工 mood 为 bored/stressed | 这些低落的员工（2–4 人） |
| 跨过里程碑（进度 25/50/75% 或 shipped） | 随机 2–3 人（庆祝） |
| 午休 12:00 / 下午茶 15:30 时间窗 | 当时空闲的人（保留，但变成"真的聊"） |

**并发与预算**：同一时刻最多 **1 场会议 + 1 场茶水间**（避免全员被抽走、也避免聚集的 LLM 调用把决策串行通道排太长）。被抽进聚集的人才占座并 `gatheringId>=0`，**其余人继续正常生产/决策**——彻底解决"全员强制"的问题。

#### 3.7.3 多人对白（连续、成串）

一场聚集 = **一次 LLM 调用**生成整段多人轮流发言（复用现有 `ParseMeetingLines` 解析、`TickMeeting` 的逐句节拍冒泡）。prompt 升级为带上下文：

```
议题：{topic}。参会者：{name(role) ×N}。
项目进度：{阶段}，总进度 {x}%，最短板：{meter} {v}/{target}，待修Bug：{n}。
今日目标：{goal}。当日事件：{events}。
（会议）生成 6–10 句多人对话，每句由一名参会者发言，围绕"是否调整方向/谁负责什么"，
最后必须给一个群体决策。只输出JSON：
{"lines":[{"speaker":"Alice","line":"≤16字"}...],
 "decision":{"summary":"...","focus_meter":"tech|design|art|polish","reassign":[{"who":"Carol","task":"..."}]}}
（茶水间）生成 4–6 句轻松对话（吐槽/打气/八卦），不需要 decision。
```

因为整段对话**一次生成、上下文一致**，所以天然连续；把原来散落各处的单人闲聊**集中成一场有来有回的对话**，正面呼应问题二的"连续性"。

#### 3.7.4 群体决策回灌（让决策有后果）★ —— 玩家确认制（Q8 拍板）

会议进入 `Deciding` 时**不直接改状态**，而是把 `FGroupDecision` 作为**建议**弹给玩家确认（`awaitingConfirm=true`）。这保留了玩家对总方向的掌控，AI 只提议、不擅自推翻玩家意图：

1. **弹出确认卡**（HUD）：显示会议一句话决议 `summary` + 拟议动作（"集中补 {focusMeter}"、"{who} 改做 {task}"）+ 【采纳】/【否决】两个按钮。会议台词照常播完，决议卡停留等待（建议给一个默认超时：玩家不点则视为否决，避免卡流程）。
2. **玩家采纳** → 才把决策落到状态：
   - `focusMeter` → 给该仪表一个限时**团队专注加成**（产量 ×`focusBoost`，持续到当日结束 / 下次会议），并把方向写进相关员工 `todayTask`。
   - `reassign` → 改对应员工 `todayTask` 与主产仪表（如让美术 Carol 暂停美术去帮 QA 复现 bug → 她的 WORK 改产 polish）。
   - `summary` → 进事件日志 + **全体参会者 shortMemory**（"10:30 决定：砍美术全力修bug"）。散会后个人决策 prompt 立即带上这条 → 行为与对白都接住会议结论，形成"开会→执行"的连贯叙事（接 §3.3 感知、§3.4 连续）。
3. **玩家否决** → 不改仪表/分工，但仍把"提议被否决"写进 shortMemory（员工可据此另寻方案），事件日志记一行。

> **边界**：`focusMeter`/`reassign` 只在玩家定的**当前目标内部**做局部调度（补短板、临时换岗），**不修改玩家选定的总目标与 `targetMeters` 总框架**。即"怎么打"可由会议提议+玩家确认，"打什么"始终是玩家说了算。

**茶水间闲聊的后果**：把参与者 mood 往 calm/focused 回拨。因为 §3.2 里 mood 直接影响产量，所以"摸鱼闲聊"有了机械意义——短期不产点，但回血后产能更高，制造**张弛节奏**（对应游戏发展国"休息后一鼓作气"）。

#### 3.7.5 与现有系统的衔接 / 改造

- **替换** `StudioSimGameInstance` 里的单例 `meeting_` / `FMeetingRuntime` / `StartMeeting` / `TickMeeting` 为 `GatheringSystem` 持有的 `std::vector<FGathering>`；现有的"事件触发会议"（`RaiseEventAndMaybeStartMeeting`）改为向 GatheringSystem 投递一个会议请求。
- **与 DecisionScheduler 协调**：聚集进行时，参与者 `gatheringId>=0`，scheduler 跳过他们（类似现在 `meeting_.active` 时不跑 scheduler，但现在是**按人**跳过而非全局停摆——非参与者照常被调度）。
- **与 ProductionSystem 协调**：参与者在聚集期间不产生 WORK 点数（在聊天/开会）；茶水间结束回 mood、会议结束施加 focus 加成。

#### 3.7.6 Fallback（LLM 不可用也能开起来）

- 会议：台词用 `BuildFallbackMeetingLines`（已有）；决策用**确定性规则**——`focusMeter = 当前最低的仪表`（Polish 阶段则 = 修 bug），`summary` 用模板。
- 茶水间：台词从 `studio_sim.json` 的预置闲聊池随机取；照常回 mood。
- 聚集照常发生、照常有后果，只是文字是模板。符合 MVP §5.3"拔掉 LLM 仍完整演示"。

---

## 4. 数据结构改动清单

| 文件 | 改动 |
| --- | --- |
| `StudioSimTypes.h` | 新增 `EProjectStage`、`FProjectMeters`、`FProjectState`、`FWorkOutput`；`FDailyGoal` 补回 `FProjectMeters targetMeters` 与 `std::string category`（目标类别，决定阈值/分解模板） |
| `EmployeeSystem.h` `FEmployee` | 新增：`FProjectMeters myContribution`；`std::vector<std::string> shortMemory`；`double nextChatterAt`；`double bubbleClearAt`；`double nextWorkOutputAt`（产出节拍计时器） |
| `DecisionScheduler.*` | `BuildPrompt` 注入 `[项目进度]` 段 + `shortMemory`；`dialogue` 改可选；`ApplyResult` 加发言闸门 + 写 shortMemory |
| 新增 `ProductionSystem.{h,cpp}` | 持有/更新 `FProjectState`；`Tick` 里给在 WORK 的员工按节拍产 `FWorkOutput`、累积仪表、推进阶段、生成/消除 bug；产出浮字事件 |
| `StudioSimTypes.h` | 新增 `EGatheringKind`、`EGatheringState`、`FGroupDecision`、`FGathering`（§3.7.1） |
| `EmployeeSystem.h` `FEmployee` | 再加 `int gatheringId = -1`（>=0 表示正被某场聚集占用） |
| 新增 `GatheringSystem.{h,cpp}` | §3.7：评估触发→组建聚集（会议/茶水间，2–5 人，最多 1+1 并发）→一次 LLM 调用生成多人对白+群体决策→逐句冒泡→`Deciding` 时把 `FGroupDecision` 回灌项目状态/`todayTask`/shortMemory；茶水间结束回 mood；fallback 模板 |
| `GoalSystem.*` | 晨会确定目标时**设定 `targetMeters` + category**；`Summarize` 改为本地算分 + LLM 仅点评 |
| `StudioSimGameInstance.*` | 持有 `ProductionSystem` + `GatheringSystem`；**移除**单例 `meeting_`/`StartMeeting`/`TickMeeting`，改委托 `GatheringSystem`；`RaiseEventAndMaybeStartMeeting` 改为向其投递会议请求；`OnTick` 调其 `Tick`；`OnRenderUI` 加进度条/仪表/bug + 当前聚集列表 + **会议决策确认卡【采纳/否决】**；`DrawWorldOverlay` 加 `+N` 浮字 |
| `assets/configs/studio_sim.json` | 新增 `roleOutput`（职位→仪表+基础产量）、`goalTemplates`（目标类别→targetMeters + 各职位 todayTask 静态映射）、`moodFactor`（情绪→产量系数）、`chatterBudget`（性格→闲聊配额）、`gathering`（触发阈值 + 茶水间闲聊池 + 评估间隔/并发上限） |

> **建议**：把产出/进度逻辑单独放 `ProductionSystem`，与 `DecisionScheduler`（决定"做什么/说什么"）解耦——一个管"机械产出"（确定性），一个管"意图与表达"（LLM）。这条边界让 fallback 永远稳。

---

## 5. 配置扩展示例（`studio_sim.json`）

```jsonc
{
  "employees": [ /* 现有不变 */ ],

  "roleOutput": {
    "engineer": {"meter": "tech",    "base": 8,  "polishRole": "fixBug"},
    "designer": {"meter": "design",  "base": 7},
    "artist":   {"meter": "art",      "base": 7},
    "qa":       {"meter": "polish",  "base": 5,  "polishRole": "findBug"},
    "pm":       {"meter": "coord",   "base": 0,  "coordBoost": 0.10},
    "boss":     {"meter": "morale",  "base": 0}
  },

  "moodFactor": {
    "focused": 1.3, "excited": 1.2, "calm": 1.0,
    "bored": 0.7,   "stressed": 0.8, "panicked": 0.5
  },

  "goalTemplates": {
    "ship_demo": {
      "match": ["demo", "发布", "上线", "试玩"],
      "targetMeters": {"tech": 100, "design": 70, "art": 80, "polish": 90},
      "tasks": {"engineer":"打通demo核心循环","artist":"出首屏美术","designer":"定引导流程","pm":"协调进度与对外口径","qa":"准备冒烟测试"}
    },
    "fix_crash": {
      "match": ["崩溃", "救火", "修复", "稳定"],
      "targetMeters": {"tech": 60, "design": 20, "art": 10, "polish": 120},
      "tasks": {"engineer":"定位崩溃根因","qa":"复现并缩小范围","pm":"对外安抚与排期","designer":"评估影响面","artist":"待命"}
    },
    "brainstorm": {
      "match": ["头脑风暴", "脑暴", "创意", "玩法"],
      "targetMeters": {"tech": 20, "design": 110, "art": 50, "polish": 20},
      "tasks": {"designer":"主导发散与收敛","artist":"出概念图","pm":"记录与排优先级","engineer":"评估可行性","qa":"提风险点"}
    }
  },

  "chatterBudget": { "话痨": 2, "话密": 2, "默认": 1, "沉默寡言": 0 },

  "gathering": {
    "evalIntervalMinutes": 30,         // 多久评估一次是否发起聚集（游戏分钟）
    "maxConcurrentMeetings": 1,
    "maxConcurrentPantry": 1,
    "meetingMinParticipants": 2,
    "meetingMaxParticipants": 4,
    "laggingMeterRatio": 0.40,         // 仪表 < 目标的此比例且过半 → 触发补短板会
    "polishBugThreshold": 6,           // Polish 阶段 bug 超此数 → 触发救火会
    "stallMinutes": 90,                // 进度停滞此久 → 触发复盘会
    "lowMoodCountForPantry": 2,        // ≥此人数情绪低落 → 触发茶水间
    "focusBoost": 1.3,                 // 群体决策对 focus_meter 的产量加成
    "pantryMoodRestore": "focused",    // 茶水间结束后参与者 mood 回拨到
    "chatterPool": [
      "这bug真不是人改的", "中午吃啥", "竞品那个手感是真不错",
      "我这块快收尾了", "周末加班吗", "刚那版本又崩了哈哈"
    ]
  }
}
```

---

## 6. 里程碑拆解（R1–R6，可登记进 `.spec/TODO.md`）

> 每个独立可验证。**R1 是地基，必须先做**；R4 表现层可并行；R5 群体聚集依赖 R1（项目状态）+ 受益于 R3/R4；R6 结算最后。

### R1 — 产出状态机（确定性，不碰 LLM）
- 新增 `FProjectState` + `ProductionSystem`。员工在 WORK 时按职位/节拍累积仪表；阶段自动推进；Polish 生成/消除 bug；`overallProgress` 实时算。
- **验证**：拔掉 llama-server，`gnb run StudioSim` 跑完一天，日志/HUD 能看到四类仪表单调上涨、进度条到达某百分比、Polish 阶段 bug 数先升后降。

### R2 — 量化 UI + 头顶浮字
- HUD 加总进度条 + 四仪表 + 阶段徽章 + bug 计数；`DrawWorldOverlay` 加 `+N` 浮字粒子。
- **验证**：肉眼看到员工头顶不断飘 `+N`，HUD 进度条随之走；`gnb shot` 截图核对。

### R3 — 进度感知决策
- `BuildPrompt` 注入 `[项目进度]` + 短板提示 + `shortMemory`；决策要求优先补最低仪表 / Polish 修 bug。
- **验证**：起 LLM，人为把某仪表调低，观察对应职位员工去工位猛补；不同阶段行为不同。

### R4 — 对话节流 + 连续性
- `dialogue` 改可选 + 发言闸门（里程碑/bug/事件/搭话/会议/闲聊配额）；`shortMemory` 环；气泡自动淡出；`chatterBudget` 限流。
- **验证**：对比改前后单位时间气泡数量显著下降；出现的对白能接上文（A 提 bug → B 回应修 bug），而非散句。

### R5 — 群体聚集与决策系统（GatheringSystem）★
- 用 `GatheringSystem` 替换单例 `meeting_`：支持并发的会议/茶水间聚集（2–5 人、最多 1+1）。
- **组织触发**：按 §3.7.2 从 `FProjectState` + 情绪自然发起（仪表落后/bug 超标/阶段切换/进度停滞→会议；情绪低落/里程碑/午茶→茶水间）。
- **多人对白**：一次 LLM 调用生成整段轮流发言（复用 `ParseMeetingLines`/逐句节拍）。
- **群体决策（玩家确认制）**：会议产出 `FGroupDecision` → 弹确认卡【采纳/否决】→ 采纳才改 focusMeter 加成 + `todayTask` + 全员 shortMemory（只在当前目标内做局部调度，不改总目标）；茶水间结束回 mood。
- fallback：台词模板 + 确定性决策建议（补最低仪表）。
- **验证**：不靠关键词/事件，正常跑一天能自动开起会议（如美术落后→美术+PM 碰头）和茶水间闲聊；会议弹出决议卡，采纳后相关员工真的转去补对应仪表、对白能引用会议结论，否决则不变；拔掉 LLM 用模板仍能开起来并弹卡；非参与者全程继续干活（非全局停摆）。

### R6 — 真实结算 + 多天钩子
- `Summarize` 本地算分（`overallProgress` + bonus/penalty），LLM 仅点评；结算面板展示各仪表达成；残留状态写入次日 Briefing 输入。
- **验证**：同一目标下，让员工多干活的那次结算分明显更高（分数与产出有因果）；连跑两天，次日晨会能引用昨日残留。

---

## 7. 验证与调试（沿用 MVP §15）

- 构建：`gnb build StudioSim`。
- 运行：`gnb run StudioSim`；产出/进度日志打 `StudioSim/Prod:` 前缀，便于过滤。
- 场景快验：`gnb shot --target StudioSim --scene assets/scad/office.scad`。
- 本地 LLM：`gnb llm serve` / `gnb llm status`。
- **关键回归**：每个里程碑都要验"拔掉 LLM 仍能完整跑通且仪表/进度/结算正常"——产出系统的确定性是 demo 不卡死的底线。

---

## 8. 决议（已拍板 ✅）

> 评审已逐条拍板，实现者**按下表执行**，无需再询。

| # | 问题 | **决议** |
| --- | --- | --- |
| Q1 | 仪表每日重置 vs 跨天累积 | ✅ **每日重置**：一天一目标一份 meters，天黑结算；`FProjectState` **预留跨天累积字段**，多天演化留到 R6。 |
| Q2 | 四类仪表命名/数量 | ✅ **4 类：技术 / 玩法 / 美术 / 品质**（tech/design/art/polish），对齐游戏发展国心智；数量写死 4，命名可在配置微调。 |
| Q3 | 浮字/对白/HUD 语言 | ✅ **中文**（与本地模型 + 现有 UI 一致）。 |
| Q4 | 闲聊配额默认值 | ✅ **每人每游戏小时 ≤1 句**闲聊，按 `chatterBudget` 性格区分，运行时 slider 可调。 |
| Q5 | pm/boss 是否产点 | ✅ **只做系数加成，不产点**：PM 协调 +10%、Boss 士气加成；避免 6 人平均加点，分工有层次。 |
| Q6 | bug 系统是否纳入 MVP | ✅ **纳入 MVP**：Polish 阶段 bug 循环（QA 找 / 工程师修），作为"剩余工作"最强可视化信号。 |
| Q7 | 聚集并发上限 | ✅ **1 会议 + 1 茶水间**并发（保护 LLM 串行通道 + 避免全员被抽走）。 |
| Q8 | 群体决策的方向权限 | ✅ **会议建议需玩家确认才生效**：决策弹【采纳/否决】卡，采纳才落地，且只在当前目标内做局部调度（focusMeter 加成 / reassign），**不改玩家选定的总目标与 targetMeters**（见 §3.7.4）。 |
| Q9 | 茶水间回 mood 力度 / 冷却 | ✅ 回 mood 到 `focused`，但给个人**社交冷却** + 占用工时（少产点），自然制约"全员泡茶水间"。 |
| Q10 | 聚集时长与发言节拍 | ✅ **沿用现有 ~3s/句**，运行时可调，按可读性定。 |

---

## 9. 与原 MVP 计划的关系

本文是对 [`StudioSim-MVP-Plan.md`](StudioSim-MVP-Plan.md) 的**增量打磨设计**，不推翻原架构：

- 复用：阶段机 `EDayPhase`、`DecisionScheduler` 串行预算、`GoalSystem` 晨会、`EventSystem`、`EmployeeSystem` 移动、会议台词生成/解析/逐句节拍（`ParseMeetingLines`/`TickMeeting` 思路）。
- 补回原计划设计过但实现时遗漏的：`FDailyGoal.progress`（→ 升级为 `FProjectState`）、`FEmployee.shortMemory`、目标分解的静态映射表（→ `goalTemplates`）。
- 新增原计划没有的核心：**产出/点数/工序/bug 系统**（`ProductionSystem`）——把"漫游模拟"变成"有进度的经营 demo"的关键缺失件。
- **重构会议为 `GatheringSystem`**：把全局单例、强制全员、无后果的 `meeting_`，升级为可并发、按项目状态自然触发、产出真实群体决策并回灌的聚集系统——让"开发过程中的多人交谈与决策"真正发生（§3.7，用户追加的重点）。
