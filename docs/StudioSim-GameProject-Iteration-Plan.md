# StudioSim —— 游戏项目化迭代计划（向《游戏发展国》再进一步）

> **状态**：设计草案（待评审 → 交后续 agent 实现）
> **目标读者**：负责把 StudioSim 推进到"经营 demo"的后续 AI agent / 开发者
> **前置必读**：[`StudioSim-MVP-Plan.md`](StudioSim-MVP-Plan.md)、[`StudioSim-Production-Model-Refinement.md`](StudioSim-Production-Model-Refinement.md)、本仓 `src/Application/Game/StudioSim/` 全部源码
> **设计北极星**：开罗游戏《游戏发展国 / Game Dev Story》—— **选类型×题材立项 → 多天连续研发 → 团队围绕这款游戏的特性协作 → 上线评分、卖出销量、入账资金 → 拿钱开下一个项目**
> **一句话**：把现在"每天换一个抽象目标、天黑给个分"升级成"**一个有名字、有类型/题材/体验要点的具体游戏项目，用若干天连续开发，团队的交流与决策都围绕这款游戏的特性，做完上线赚钱**"。

---

## 0. TL;DR（给赶时间的实现者）

前两份文档已经把 StudioSim 从"漫游聊天"做到了"有产出仪表的单日经营"。但跟《游戏发展国》比，还差**最核心的一层**：**模拟里没有"一款具体的游戏"这个对象**。现在玩家每天在晨会选的是一句抽象目标（"赶 demo / 修崩溃 / 头脑风暴"），仪表**每日重置**（决议 Q1），天黑结算的分数**不卖钱、不留存、不影响下一天**。于是：

1. **项目不具体** → 没有类型/题材/体验要点，员工的交流只能泛泛对齐一句目标文字，无法"针对这款游戏的特性"讨论。
2. **开发不连续** → `dayIndex`/`StartNextDay` 骨架在，但每天是独立的一次性目标，不存在"这个项目还要做 5 天"的跨天推进感。
3. **劳动无回报** → `overallProgress` 算出来只进了结算面板，**没有评分、没有销量、没有资金**，玩不出"做得好 → 赚得多 → 再投入"的经营闭环。

**解法主线**：引入 `FGameProject`（一款具体游戏：名字 + 类型 + 题材 + 体验要点 + 工期/预算 + 由类型×题材决定的仪表权重）。立项流程升级 `GoalSystem` 的晨会：玩家**选类型+题材**（首日立项），LLM 生成**游戏名 + 体验要点**并据此设定 `targetMeters` 与**工期天数**。`FProjectState` 的仪表**跨天累积**（决议 Q1 预留的字段现在启用），项目跨越 N 天连续推进 Planning→Production→Polish→Done。所有 LLM 交流（决策/会议/茶水间/结算）的 prompt 都注入**项目类型/题材/体验要点**，让"交流内容和项目特性有关"。项目 `Done`（上线）后：用 `overallProgress` + 体验要点契合度算**质量分 → 媒体评分 → 销量 → 资金入账**；成本 = 团队工资 × 工期。资金留存进**公司层状态**，作为开下一个项目的本钱（轻量经济，决议见 §8）。

**范围边界（本轮已与用户拍板）**：
- 分类维度 = **类型 Genre + 题材 Theme + 体验要点 Highlights**（**不含平台**，平台/主机授权金留作扩展 §10）。
- 经济深度 = **轻量**：上线→评分→销量→资金；成本 = 工资×工期。不做市场趋势/粉丝数/续作加成/办公室升级（留扩展）。
- 工期 = **立项时按项目规模设定**：类型×题材×体验要点数 → 预估工期天数与 `targetMeters`，玩家在预算/工期间权衡，逼近 deadline 有压力。

---

## 1. 现状与差距（相对《游戏发展国》）

> 每条都对照当前代码，标出"已有 / 缺失"。

### 1.1 已经有的地基（复用，别重造）

| 设施 | 位置 | 本轮如何复用 |
| --- | --- | --- |
| 四仪表产出状态机 `FProjectState` / `FProjectMeters` | `StudioSimTypes.h` + `ProductionSystem.{h,cpp}` | **直接复用**；仪表从"每日重置"改为"项目级跨天累积" |
| 阶段机 `EProjectStage`（Planning/Production/Polish/Done） | `StudioSimTypes.h` | **直接复用**；从"一天走完"拉长为"跨多天推进" |
| 一天的阶段机 `EDayPhase`（Briefing/Working/Review） | `StudioSimTypes.h` `FWorldState` | **复用**；Briefing 在首日=立项、后续日=今日攻坚；Review 在项目最后一天=上线结算 |
| 多天骨架 `dayIndex` / `StartNextDay()` | `StudioSimTypes.h:140` / `StudioSimGameInstance.cpp:491` | **复用并填实**：跨天保留项目状态，不再重置 |
| 晨会目标系统 `GoalSystem`（3 选项+自定义+分解+结算） | `GoalSystem.{h,cpp}` | **升级为立项系统**：候选项从"抽象目标"变"游戏立项方案"；`Summarize` 接经济结算 |
| 群体聚集 `GatheringSystem`（会议/茶水间+决策回灌） | `GatheringSystem.{h,cpp}` | **复用**；议题/对白/决策注入项目类型题材 |
| 决策调度 `DecisionScheduler`（串行预算+短记忆+发言闸门） | `DecisionScheduler.{h,cpp}` | **复用**；`BuildPrompt` 增注项目特性段 |
| 事件系统 `EventSystem` | `EventSystem.{h,cpp}` | **复用**；事件冲击改为对项目工期/仪表的真实压力 |

### 1.2 缺失的核心件（本轮要补）

| 缺口 | 现状根因 | 本轮新增 |
| --- | --- | --- |
| **没有"一款具体游戏"** | `FDailyGoal` 只有 `title/description/source/category/targetMeters/set`（`StudioSimTypes.h`），没有类型/题材/体验要点 | 新增 `FGameProject`（§3.1） |
| **没有类型×题材组合** | `category` 是 `ship_demo/fix_crash/brainstorm` 三个抽象档 | `EGameGenre` × `EGameTheme` 矩阵，决定仪表权重与组合加成（§3.1/§5） |
| **没有体验要点** | 无 | `highlights`（游戏卖点列表），贯穿交流与评分（§3.1/§3.4/§3.5） |
| **开发不跨天连续** | 仪表每日重置（决议 Q1）；每天重新立目标 | 项目级 `FProjectState` 跨天累积；工期 N 天（§3.3） |
| **交流不挂钩项目特性** | `BuildPrompt`/会议 prompt 只带目标标题 | 所有 prompt 注入 类型/题材/体验要点（§3.4） |
| **劳动无经济回报** | `Summarize` 只出分，不卖钱 | 上线→评分→销量→资金→公司状态（§3.5/§3.6） |
| **没有公司层状态** | 无资金/无项目历史 | `FCompanyState`（资金/已完成项目/解锁）（§3.6） |

---

## 2. 《游戏发展国》机制映射

| 游戏发展国机制 | 作用 | 映射到 StudioSim |
| --- | --- | --- |
| **选"类型 × 题材"组合立项**（如 赛车×恐怖=冷门，RPG×奇幻=王道；组合好坏有加成） | 让每个项目有**身份**与策略空间 | `EGameGenre × EGameTheme`，查 `comboTable` 得契合系数，影响 `targetMeters` 与最终评分（§3.1/§5） |
| **多周连续开发**（一个项目跨多个游戏内周，研发点数持续累积） | "连续推进"的核心节奏 | 项目工期 N 天，`FProjectState` 跨天累积；每天一段 Working（§3.3） |
| **四类研发点数攒够 → 进入 debug → 上线** | 工序推进 | 复用 `EProjectStage` + bug 循环（已实现），拉到项目尺度 |
| **上线后媒体打分（4 评委×10 分）+ 销量曲线** | 把"做得好"变成可量化回报 | 质量分 → 媒体评分 → 销量数（§3.5） |
| **销量 × 单价 = 营收，减去开发成本 = 利润，进公司资金** | 经营闭环 | 轻量经济：`revenue - cost = profit → company.funds`（§3.5/§3.6） |
| **拿钱招人/升级/做下一个更大的项目** | 长期成长 | 资金留存 → 下个项目本钱（轻量；招人/升级留扩展 §10） |
| **体验要点 / 加分要素**（特定组合或要素提升评价） | 差异化卖点 | `highlights` 体验要点：贯穿交流，命中则评分加成（§3.4/§3.5） |

**关键转译**（沿用前文纪律）：玩家只在**立项**时做高层决策（选类型/题材/工期）；**开发过程仍由 LLM 员工自主决策**，但现在他们的决策、会议、对白都**看得到这是一款什么游戏**，从而"针对项目特性"协作。经济结算是**纯本地确定性计算**，LLM 只负责"有人味的点评"，拔掉 llama-server 整条闭环照跑（沿用 MVP §5.3）。

---

## 3. 设计方案

### 3.1 新增核心：`FGameProject`（一款具体游戏）

放 `StudioSimTypes.h`，由 GameInstance 持有一份（一个项目一份，跨天存活）。它**包住**现有的 `FProjectState`（产出仪表/阶段/bug），并补上"这是什么游戏"的身份信息。

```cpp
namespace StudioSim
{
    // 游戏类型（决定四仪表权重；可在配置增减）。
    enum class EGameGenre
    {
        RPG, Action, Simulation, Puzzle, Shooter, Adventure, Unknown
    };

    // 游戏题材（决定美术/叙事方向与组合加成）。
    enum class EGameTheme
    {
        Fantasy, SciFi, Sports, Romance, Horror, Daily, Unknown
    };

    // 一条"体验要点 / 卖点"。贯穿交流与评分（§3.4/§3.5）。
    struct FHighlight
    {
        std::string text;      // "丝滑手感" / "硬核策略" / "催泪剧情"
        std::string meter;     // 主要支撑仪表 "tech"/"design"/"art"/"polish"，空=综合
        bool        achieved = false; // 开发中是否被"做实"（命中相关里程碑/决策）
    };

    // 一款具体游戏项目（跨天存活；包住 FProjectState）。
    struct FGameProject
    {
        std::string   name;                 // LLM/玩家给的游戏名，如"星海奇谭"
        EGameGenre    genre = EGameGenre::Unknown;
        EGameTheme    theme = EGameTheme::Unknown;
        std::vector<FHighlight> highlights;  // 2–3 个体验要点
        float         comboFit = 1.0f;       // 类型×题材契合系数（查 comboTable，§5）

        int           plannedDays = 7;       // 立项设定的工期（天）
        int           elapsedDays = 0;       // 已开发天数
        int64_t       budget = 0;            // 立项预算（= 工资×plannedDays 的预估，用于显示压力）

        FProjectState production;            // 复用：四仪表/阶段/bug/overallProgress

        // 上线结算产物（Done 后写，§3.5）
        int           reviewScore = 0;       // 0–40（4 评委×10）
        int64_t       unitsSold = 0;
        int64_t       revenue = 0;
        int64_t       cost = 0;
        int64_t       profit = 0;
        bool          launched = false;
    };
}
```

> **与 `FDailyGoal` 的关系**：`FDailyGoal` **降级为"今日攻坚重点"**——不再承载"项目目标"，而是项目进行中每天的局部聚焦（晨会可由 LLM/规则按当前最短板生成，见 §3.3）。`FProjectState.targetMeters` 的来源从"目标类别"改为"**类型×题材×工期**"（§5 配置）。

### 3.2 立项流程（升级 `GoalSystem` 的 Briefing）

把首日晨会从"选一句抽象目标"升级为"**给一款游戏立项**"。复用 `GoalSystem` 的异步 LLM + 玩家选择 + fallback 纪律，只换内容与产物。

**Planning 阶段（首日 Briefing，时钟暂停）：**

1. **玩家选类型 + 题材**：UI 弹「立项」面板（升级现有 `DrawGoalChoiceModal`）——两个下拉/网格选 `EGameGenre` 与 `EGameTheme`，外加一个**工期/规模**选择（小品/标准/大作 → 不同 `plannedDays` 与 `targetMeters` 倍率）。
2. **LLM 生成游戏名 + 体验要点**：`GoalSystem` 调一次 LLM（**立项 prompt**），输入 类型/题材/规模，要求输出 JSON：
   ```json
   {
     "name": "星海奇谭",
     "highlights": [
       {"text":"宏大的太空歌剧叙事","meter":"design"},
       {"text":"精致的星舰美术","meter":"art"},
       {"text":"扎实的战斗手感","meter":"tech"}
     ]
   }
   ```
3. **本地设定 `targetMeters` + 工期 + 预算**（确定性）：查 §5 的 `genreWeights`（类型→四仪表权重）× 规模倍率 → `production.targetMeters`；`comboTable[genre][theme]` → `comboFit`；`plannedDays` 由规模档决定；`budget = 团队日工资 × plannedDays`（显示给玩家"这个项目大概要花这么多"）。
4. **写入 `FGameProject`**，阶段 `Planning → Production`，进入逐日 Working。
5. **Fallback**（LLM 不可用）：游戏名从 `studio_sim.json` 的 `nameFallback` 池按题材取；体验要点用 `genreTheme` 默认要点模板。玩家选类型/题材始终可用，与 LLM 无关。

> **一次 O(1)/项目 的 LLM 调用**，发生在时钟暂停的 Planning，不与逐员工决策抢串行通道（沿用 MVP §5.1）。

### 3.3 多天开发迭代（跨天连续，工期 N 天）

这是"为期 n 天的开发迭代"的落点。**项目状态跨天存活、仪表累积**（启用决议 Q1 预留的跨天字段）。

**每天的循环（复用 `EDayPhase` + `StartNextDay`）：**

```
Day k Briefing(晨会, 暂停)
   ├─ 首日(k=0)：立项（§3.2）
   └─ 后续日：不再立项。生成"今日攻坚重点"FDailyGoal（按当前最短仪表/最多 bug）
              + 显示"工期第 k/N 天，距 deadline 还剩 N-k 天"
        │
        ▼
Day k Working(09:00→18:00, 加速时钟)
   员工围绕 FGameProject 持续产出 → production.meters 累积（不重置）
   GatheringSystem 按项目状态自然开会/茶水间
        │
        ▼
Day k Review(收盘)
   ├─ 未到工期 & 未 shipped：日小结（今日产出/剩余）→ StartNextDay()，elapsedDays++
   └─ 到工期(elapsedDays>=plannedDays) 或 production.shipped：进入【上线结算】（§3.5）
```

- **跨天累积**：`StartNextDay()` 现在**只重置"日内"状态**（时钟回 09:00、清当日事件、清今日攻坚 `FDailyGoal`、员工日产出计数），**保留** `FGameProject` / `production.meters` / `bugCount` / `elapsedDays`。改 `StudioSimGameInstance.cpp:491` 的 `StartNextDay`：从"全量重置"改为"日内重置 + 项目保留"。
- **阶段跨天推进**：`Production→Polish→Done` 由 `ProductionSystem` 按累积仪表推进（已实现的逻辑，不改判据），只是现在跨越多天而非一天内。
- **deadline 压力**：当 `plannedDays - elapsedDays` 小且仪表/进度落后 → 触发"赶工"信号（注入 prompt + 更易触发救火会 §3.4）。到工期未达标 → 仍强制上线，但质量分按实际 `overallProgress` 打折（§3.5）——**做不完也得发，戏剧张力来自工期**。
- **工期与规模**（决议：立项时设定）：规模档 → `plannedDays`（如 小品 5 / 标准 8 / 大作 12）与 `targetMeters` 倍率。天数越多上限越高，但 `cost = 日工资 × 实际开发天数` 越高，逼玩家权衡。

### 3.4 交流内容与项目特性挂钩（核心诉求）

让"开发迭代时的各种交流内容**和项目的特性有关**"。做法：把 `FGameProject` 的 类型/题材/体验要点 注入**所有** LLM 交流的 prompt，并在 fallback 文案里也带上。

**(a) 员工决策 prompt（`DecisionScheduler::BuildPrompt`）** 在现有 `[项目进度]` 段（Refinement §3.3）之上，**新增 `[项目]` 段**（控制在 ~50 token）：

```
[项目]
《星海奇谭》——类型：RPG，题材：科幻。
体验要点：宏大太空歌剧叙事 / 精致星舰美术 / 扎实战斗手感。
工期：第 4/8 天（距上线 4 天）。
```

效果：工程师在"扎实战斗手感"是体验要点时，决策/对白自然偏向"打磨手感"；美术围绕"科幻星舰"出图；不同 类型×题材 → 不同行为分布与话题（验收 §6 G4 的肉眼信号）。

**(b) 会议/茶水间 prompt（`GatheringSystem`）** 议题与对白生成注入项目身份。会议议题模板升级，例：
- 补短板会："《星海奇谭》的**战斗手感**(tech)进度落后，距上线 4 天，怎么办？"
- 茶水间闲聊池按题材取（科幻项目聊"这星舰建模真酷"、恐怖项目聊"这音效吓到我了"）。
- 群体决策 `FGroupDecision` 的 `focusMeter`/`reassign` 仍只在项目内做局部调度（沿用 Refinement §3.7.4 玩家确认制，不改类型/题材/总 `targetMeters`）。

**(c) 体验要点的"做实"**：当某体验要点对应的仪表跨过里程碑、或一次群体决策聚焦它，则 `FHighlight.achieved = true`，并允许一次相关对白（"战斗手感终于调顺了！"）。`achieved` 的体验要点在上线评分时加成（§3.5）——**让"卖点"既是叙事钩子又是机械变量**。

**(d) 立项/结算文案**：立项 LLM 给名字+要点已带题材；上线结算 LLM 点评（§3.5）输入真实数字 + 类型/题材/命中的体验要点，产出"一款科幻 RPG，叙事亮眼但手感打磨不足"这类**针对这款游戏**的评语。

> 所有注入都遵守 token 预算（MVP §5.4）：项目段精简到一行类型题材 + 体验要点缩写 + 工期进度，不塞历史。

### 3.5 上线与经济（轻量：评分→销量→资金）

项目到 `Done`（仪表达标且 bug 清零）或工期耗尽强制上线 → 进入**上线结算**（升级 `GoalSystem::Summarize` + Review 面板）。**全部本地确定性计算**，LLM 仅点评。

**(1) 质量分 `quality`（0..1）：**
```
base       = production.overallProgress                  // 已实现：仪表完成度×(1-bug惩罚)
comboBonus = (comboFit - 1.0)                            // 类型×题材契合，±0.15 量级
hlBonus    = 0.05 × (命中的体验要点数)                    // 每个 achieved 的 highlight +0.05
deadline   = elapsedDays<=plannedDays ? +0.05 : -0.10×超期天数比   // 按时/超期
quality    = clamp(base + comboBonus + hlBonus + deadline, 0, 1)
```

**(2) 媒体评分 `reviewScore`（0–40，4 评委×10，仿游戏发展国）：**
```
mean = round(quality × 10)                  // 每位评委基准分
四位评委 = mean ± 小随机抖动(-1..+1)，clamp 1..10
reviewScore = Σ 四位                        // 满分 40；>=32 为"神作"档
```
LLM **只生成四句短评**（每位评委一句，输入真实数字+类型题材+命中要点），失败用模板。

**(3) 销量 `unitsSold`：**
```
demandBase = genreThemeDemand[genre][theme]   // 配置：该组合的市场基数（轻量，固定表，非动态趋势）
unitsSold  = round( demandBase × qualityCurve(quality) × reviewMultiplier(reviewScore) )
            // qualityCurve: quality 的凸函数（高分指数放大）；reviewMultiplier: 神作档额外乘数
```

**(4) 资金结算：**
```
revenue = unitsSold × unitPrice            // unitPrice 固定（配置）
cost    = teamDailyWage × elapsedDays       // 团队日工资 × 实际开发天数
profit  = revenue - cost
company.funds += profit                     // 入账（可负，亏损）
```

**(5) 结算面板**（升级 `DrawReviewModal`）：游戏名 + 类型题材 + 四仪表达成 + 命中的体验要点 + 四评委短评与总分 + 销量数字滚动 + 营收/成本/利润 + 资金余额变化。这是经营 demo 的"高光时刻"，沿用 `+N` 浮字的反馈哲学（Refinement §3.5）。

### 3.6 多项目循环 + 公司层状态

上线后不结束游戏，而是回到"立项"，形成经营循环。

```cpp
struct FCompanyState
{
    int64_t funds = 50000;                 // 启动资金（配置）
    std::vector<FGameProject> shipped;     // 历史已上线项目（名字/评分/销量/利润）
    int     projectIndex = 0;              // 第几个项目
    // 扩展钩子（本轮不实现）：解锁的 genre/theme、声望、粉丝数
};
```

- **上线结算后**：把 `FGameProject` 推入 `company.shipped`，`projectIndex++`，弹"开始下一个项目？"→ 回到立项（§3.2）。`dayIndex` 继续累加（全局天），`elapsedDays` 对新项目归零。
- **资金的意义（轻量）**：HUD 常驻显示公司资金；立项时显示"预算 ≈ 工资×工期"，让玩家感到"做大项目要烧钱、卖不好会亏"。**本轮资金暂不强约束**（不做"资金<0 破产"硬性失败，避免 demo 卡死）；破产/招人/升级留扩展（§10）。
- **多天钩子兑现**：Refinement §3.6 预留的"今日残留写入次日 Briefing"现在升级为"**项目残留跨天**"——次日攻坚重点直接读 `production.meters` 的当前短板，天然连续。

### 3.7 与现有系统的衔接 / 改造摘要

- **`GoalSystem`**：`BeginDay` 分叉——首日/无在研项目 → 立项流程（§3.2）；项目进行中 → 生成"今日攻坚重点"。`Summarize` 接 §3.5 经济结算（本地算分，LLM 点评）。
- **`ProductionSystem`**：判据不变，`StartProject` 改为接 `FGameProject`（读 `targetMeters`）；不再每天 `Reset`，改为项目结束才 `Reset`。
- **`DecisionScheduler::BuildPrompt`**：新增 `[项目]` 段（§3.4a）。
- **`GatheringSystem`**：议题/对白/闲聊池注入项目身份（§3.4b）。
- **`StudioSimGameInstance`**：持有 `FGameProject` + `FCompanyState`；`StartNextDay` 改"日内重置+项目保留"（§3.3）；Review 分叉日小结 vs 上线结算；新增立项面板与上线结算面板；HUD 加项目卡（名字/类型题材/工期进度/资金）。
- **`EventSystem`**：事件改为对**项目工期/仪表**的真实冲击（如"竞品提前发布"→ 缩短有效 deadline 压力 / 给市场基数打折），与项目特性叠加。

---

## 4. 数据结构改动清单

| 文件 | 改动 |
| --- | --- |
| `StudioSimTypes.h` | 新增 `EGameGenre`、`EGameTheme`、`FHighlight`、`FGameProject`、`FCompanyState`；`FDailyGoal` 语义降级为"今日攻坚重点"（字段可不变） |
| `GoalSystem.{h,cpp}` | `BeginDay` 分叉（立项 / 今日攻坚）；新增立项 prompt + 解析（name/highlights）；`Summarize` 改为 §3.5 经济结算（本地算分 + LLM 点评）；按 类型×题材×规模 设 `targetMeters`/`plannedDays`/`comboFit` |
| `ProductionSystem.{h,cpp}` | `StartProject` 接 `FGameProject`；跨天不 `Reset`（仅项目结束重置）；体验要点 `achieved` 置位钩子 |
| `DecisionScheduler.{h,cpp}` | `BuildPrompt` 新增 `[项目]` 段（类型/题材/体验要点/工期进度） |
| `GatheringSystem.{h,cpp}` | 议题模板、对白 prompt、闲聊池注入项目类型题材；`FGroupDecision` 仍项目内局部调度 |
| `StudioSimGameInstance.{hpp,cpp}` | 持有 `FGameProject_` + `FCompanyState_`；`StartNextDay` 改日内重置+项目保留；Review 分叉（日小结/上线结算）；立项面板 `DrawProjectPitchModal`（升级 `DrawGoalChoiceModal`）；上线结算面板（升级 `DrawReviewModal`）；HUD 加项目卡 + 资金 |
| `EventSystem.{h,cpp}` | 事件影响改为对项目工期/市场基数/仪表的真实冲击 |
| `assets/configs/studio_sim.json` | 新增 `genreWeights`（类型→四仪表权重）、`comboTable`（类型×题材→契合系数）、`genreThemeDemand`（→市场基数+默认体验要点+题材闲聊池）、`sizeTiers`（规模→工期/倍率）、`economy`（启动资金/单价/日工资）、`nameFallback`（题材→游戏名池） |

> **解耦纪律**（沿用 Refinement §4）：`ProductionSystem` 管确定性产出，`GoalSystem` 管立项/结算，经济计算放 `GoalSystem`/GameInstance 的本地函数，**全部不依赖 LLM**；LLM 只产文字（名字/体验要点/点评/对白）。拔掉 server，整条"立项→开发→上线→资金"闭环用 fallback 跑通。

---

## 5. 配置扩展示例（`studio_sim.json`）

```jsonc
{
  "employees": [ /* 现有不变 */ ],
  "roleOutput": { /* Refinement 已有 */ },
  "moodFactor":  { /* Refinement 已有 */ },
  "gathering":   { /* Refinement 已有 */ },

  // 类型 → 四仪表权重（决定 targetMeters 分布；和为 ~1）
  "genreWeights": {
    "rpg":        {"tech":0.25, "design":0.35, "art":0.30, "polish":0.10},
    "action":     {"tech":0.40, "design":0.20, "art":0.25, "polish":0.15},
    "simulation": {"tech":0.30, "design":0.40, "art":0.15, "polish":0.15},
    "puzzle":     {"tech":0.20, "design":0.45, "art":0.15, "polish":0.20},
    "shooter":    {"tech":0.45, "design":0.15, "art":0.25, "polish":0.15},
    "adventure":  {"tech":0.20, "design":0.30, "art":0.40, "polish":0.10}
  },

  // 类型 × 题材 契合系数（王道组合>1，冷门<1）
  "comboTable": {
    "rpg":    {"fantasy":1.15, "scifi":1.10, "romance":1.05, "horror":0.95, "sports":0.80, "daily":0.90},
    "action": {"scifi":1.15, "fantasy":1.05, "sports":1.10, "horror":1.00, "romance":0.85, "daily":0.90},
    "puzzle": {"daily":1.15, "fantasy":1.05, "scifi":1.00, "romance":1.00, "sports":0.95, "horror":0.90}
    // ...其余类型补全
  },

  // 类型×题材 → 市场基数 + 默认体验要点 + 题材闲聊池
  "genreThemeDemand": {
    "rpg_fantasy":  {"demand": 12000, "highlights":["史诗剧情","奇幻世界观","角色养成"], "chatter":["这世界观真带感","boss战要够燃"]},
    "action_scifi": {"demand": 15000, "highlights":["丝滑手感","炫酷特效","快节奏战斗"], "chatter":["这打击感绝了","星舰建模真酷"]}
    // ...
  },

  // 规模档 → 工期天数 + targetMeters 总量倍率
  "sizeTiers": {
    "small":    {"days": 5,  "scale": 0.7},
    "standard": {"days": 8,  "scale": 1.0},
    "big":      {"days": 12, "scale": 1.5}
  },

  // 轻量经济参数
  "economy": {
    "startingFunds": 50000,
    "unitPrice":     6,
    "teamDailyWage": 1800,   // 全队每游戏日工资（cost = wage × elapsedDays）
    "masterpieceReviewScore": 32  // 媒体总分阈值，达到给销量额外乘数
  },

  // 题材 → 游戏名 fallback 池（LLM 不可用时取）
  "nameFallback": {
    "fantasy": ["龙之纪元","秘境传说","魔导之书"],
    "scifi":   ["星海奇谭","曲速边境","量子黎明"]
  }
}
```

---

## 6. 里程碑拆解（G1–G6，可登记进 `.spec/TODO.md`）

> 每个独立可验证。**G1 是地基**（项目对象 + 跨天累积），后续依赖它。每个里程碑都要验"拔掉 llama-server 仍完整跑通"。

### G1 — `FGameProject` + 跨天累积（确定性地基）
- 新增 `FGameProject`/`EGameGenre`/`EGameTheme`/`FHighlight`/`FCompanyState`。`StartNextDay` 改"日内重置 + 项目保留"；`ProductionSystem` 跨天不 Reset。
- 先**写死一个项目**（如 RPG×奇幻、工期 8 天），让仪表跨多天累积、阶段跨天推进。
- **验证**：拔掉 LLM，`gnb run StudioSim` 连跑多个游戏日，HUD/日志看到 `meters` 跨天单调累积、`elapsedDays` 递增、第 N 天阶段推进到 Polish/Done（而非每天清零）。

### G2 — 立项流程（类型×题材×规模 → 项目）
- 升级 `GoalSystem::BeginDay` 首日分支 + `DrawProjectPitchModal`（选类型/题材/规模）。LLM 出 name+highlights（fallback 名字池+默认要点）。本地按 §5 配置算 `targetMeters`/`plannedDays`/`comboFit`/`budget`。
- **验证**：启动 → 立项面板选 RPG×奇幻×标准 → 生成游戏名+体验要点 → 进入开发，`targetMeters` 分布符合 `genreWeights`；停 LLM 用 fallback 仍能立项。

### G3 — 多天开发迭代 + 工期压力
- 后续日 Briefing 生成"今日攻坚重点"（最短板/最多 bug）；HUD 显示"第 k/N 天，距上线 N-k 天"；到工期强制上线、超期质量打折；deadline 临近更易触发救火会（接 `GatheringSystem`）。
- **验证**：一个 8 天项目连跑，能看到逐日攻坚重点变化、工期倒计时；故意拖慢产出 → 到第 8 天强制上线且质量分被超期惩罚。

### G4 — 交流挂钩项目特性
- `DecisionScheduler::BuildPrompt` 注入 `[项目]` 段；`GatheringSystem` 议题/对白/闲聊注入类型题材；体验要点 `achieved` 钩子 + 相关对白。
- **验证**：起 LLM，做"科幻动作"vs"奇幻 RPG"两个项目，员工对白/会议议题明显不同（手感/特效 vs 剧情/养成）；命中体验要点时有对应庆祝对白。

### G5 — 上线评分 + 销量 + 资金（经济闭环）
- 升级 `Summarize`/`DrawReviewModal`：本地算 `quality→reviewScore(4评委)→unitsSold→revenue/cost/profit→company.funds`；LLM 仅四句短评（fallback 模板）。HUD 常驻资金。
- **验证**：同一项目，让员工多干活的那次 → 评分/销量/利润明显更高（经济与产出有因果）；亏损项目 funds 减少；停 LLM 用模板短评仍出完整结算。

### G6 — 多项目循环 + 公司状态
- 上线后入 `company.shipped`、`projectIndex++`、弹"开下一个项目"→ 回立项；HUD 显示已发行作品列表（名字/评分/销量）；次日攻坚读项目残留。
- **验证**：连做两个项目，资金随利润累积/回撤；第二个项目立项面板能看到公司资金与上一作战绩。

**横切**：确定性 fallback（每个 G 都验拔 LLM）、token 预算（G4）、配置驱动（G2 起）、`StudioSim/Prod:` 日志前缀。

---

## 7. 验证与调试（沿用前两份 §15/§7）

- 构建：`gnb build StudioSim`（只消费不改 NextGameplay，无需带 `gkNextUnitTests`）。
- 运行：`gnb run StudioSim`；项目/经济日志打 `StudioSim/Proj:` 前缀，产出打 `StudioSim/Prod:`。
- 场景快验：`gnb shot --target StudioSim --scene assets/scad/office.scad`。
- 本地 LLM：`gnb llm serve` / `gnb llm status`。
- **关键回归**：每个里程碑都验"拔掉 llama-server 仍能完整跑通**立项→多天开发→上线→资金**闭环且数字正常"——经济与产出的确定性是 demo 不卡死的底线。

---

## 8. 决议（本轮已与用户拍板 ✅）

| # | 问题 | **决议** |
| --- | --- | --- |
| P1 | 项目分类维度 | ✅ **类型 Genre + 题材 Theme + 体验要点 Highlights**；**不含平台**（平台/主机授权金留扩展） |
| P2 | 经济深度 | ✅ **轻量**：上线→媒体评分→销量→资金；`cost=工资×工期`；**不做**市场趋势/粉丝/续作/办公室升级 |
| P3 | 多天工期来源 | ✅ **立项时按项目规模设定**：类型×题材×规模 → `plannedDays` + `targetMeters`；玩家在预算/工期间权衡 |
| P4 | 仪表跨天 | ✅ **项目级跨天累积**（启用 Refinement Q1 预留字段）；一个项目一份 `FProjectState`，项目结束才重置 |
| P5 | 资金硬约束 | ✅ **本轮不做破产硬失败**：资金可负、只显示，避免 demo 卡死；破产/招人/升级留扩展 |
| P6 | 结算分数来源 | ✅ **本地确定性计算**（quality→评分→销量→资金）；**LLM 仅点评**，沿用 Refinement §3.6 纪律 |
| P7 | 交流挂钩方式 | ✅ 所有 LLM prompt（决策/会议/茶水间/立项/结算）注入**类型/题材/体验要点**；fallback 文案也带题材 |

**待后续评审的开放项**：

| # | 开放问题 | 倾向 |
| --- | --- | --- |
| Q1 | `comboTable`/`genreThemeDemand` 全矩阵数值如何调平 | 先给王道/冷门粗值，G5 后按手感调 |
| Q2 | 体验要点 `achieved` 的判据（里程碑 vs 决策聚焦 vs 二者） | 默认"对应仪表达标 或 被一次群体决策聚焦"任一即置位 |
| Q3 | 工期耗尽但仪表严重不足时是否允许"延期"（花钱续命） | 本轮**强制上线**（更有张力）；延期留扩展 |
| Q4 | 立项是否允许玩家手填游戏名/体验要点（不靠 LLM） | 默认 LLM 生成 + 玩家可编辑文本框（与现有自定义目标一致） |

---

## 9. 与前两份文档的关系

本文是 [`StudioSim-MVP-Plan.md`](StudioSim-MVP-Plan.md)、[`StudioSim-Production-Model-Refinement.md`](StudioSim-Production-Model-Refinement.md) 的**第三段增量**，不推翻任何已有架构：

- **复用**：阶段机 `EDayPhase`/`EProjectStage`、`ProductionSystem` 四仪表与 bug 循环、`GoalSystem` 晨会异步、`GatheringSystem` 聚集与玩家确认制、`DecisionScheduler` 串行预算与短记忆、`EventSystem`、多天骨架 `dayIndex`/`StartNextDay`。
- **兑现前文预留**：Refinement Q1 的"跨天累积字段"、§3.6 的"残留写入次日 Briefing"，在本文升级为**项目级跨天**与**经济结算**。
- **新增核心**：`FGameProject`（一款具体游戏：类型/题材/体验要点/工期/经济）+ `FCompanyState`（公司资金/项目历史）——把"有进度的单日经营 demo"升级为"**选题立项 → 多天连续研发 → 上线赚钱 → 再开新项目**"的《游戏发展国》式经营循环。这是用户本轮强调的方向：**项目变具体、开发跨多天、交流挂钩项目特性、做完赚钱**。

---

## 10. 超出本轮的后续扩展

- **目标平台 / 主机授权金**：选 PC/掌机/主机，平台决定受众上限、授权成本、销量曲线（本轮砍掉的第四维度）。
- **市场趋势 / 竞品 / 粉丝数**：动态 `demand`、流行类型轮动、粉丝复购加成、续作系列加成。
- **公司经营层**：招聘/解雇、员工成长与升级、办公室扩建、研发等级解锁更高 `genre/theme`。
- **资金硬约束**：资金<0 破产失败、贷款、发行商预付。
- **延期机制**：工期耗尽花钱续命（Q3）。
- **跨项目人物成长**：员工因参与高分项目获得专长加成、关系网。
