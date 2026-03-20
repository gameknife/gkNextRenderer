# BrickPlayer / LDraw 技术总结（2026-03）

## 当前阶段结论

`gkNextEngine` 现阶段唯一明确重点，是基于 **LDraw** 的乐高搭建体验，也就是 `BrickPlayer` 方向。最近开发不是在横向铺更多引擎能力，而是在把一条可用的搭建链路逐步打通：

- LDraw 资源可稳定加载
- 零件可被选中、拖拽、吸附、放置
- 自由搭建（FreeBuild）模式开始成立
- 交互反馈、物理表现、调试可视化逐步补齐

从近期提交来看，工作主线已经很清晰：**围绕 BrickPlayer 做玩法原型的可玩性闭环。**

---

## 一、当前技术主轴

### 1. LDraw 资源与数据链路

当前 BrickPlayer 玩法建立在 LDraw 生态之上，核心意义是：

- 以 `.ldr / .mpd` 为内容输入
- 用 LDraw 库提供乐高零件与装配结构
- 用引擎运行时把这些静态装配数据转成可交互对象

近期相关点包括：

- `FLDrawLoader`
- `FLDrawParser`
- `FLDrawConfig`
- `BrickPlayerLDrawShadow`
- LDraw library pak workflow

这说明现阶段并不只是“能导入模型”，而是在逐步建设一条完整链路：

1. **LDraw 文件解析**
2. **零件 / 子文件引用解析**
3. **运行时场景实例化**
4. **吸附信息提取与组织**
5. **资源打包与挂载（pak workflow）**

换句话说，LDraw 在这里已经不只是资源格式，而是整个 BrickPlayer 系统的数据基础。

---

### 2. Snap（吸附）系统是当前交互核心

近期最密集的改动集中在 `BrickPlayerSnapLogic`、`BrickPlayerGameInstance` 和 `BrickPlayerGameInstanceDebug`，说明目前的主要技术难点不是渲染，而是 **搭建交互判定**。

当前 Snap 方向已经包含：

- 拖拽中的候选连接点查找
- 连接器兼容性判断
- Hover 过滤
- 目标位置求解
- 吸附确认反馈
- 调试绘制与状态可视化

从代码结构和提交信息看，吸附系统已经开始从“写在 GameInstance 里的流程代码”往“独立逻辑模块”演进：

- `BrickPlayerSnapLogic.cpp/.hpp`
- `refactor brickplayer snap logic and state handling`
- `split debug rendering from game instance`

这一步很关键，因为它意味着系统正在从“原型期堆逻辑”进入“可维护的玩法模块化”。

---

### 3. FreeBuild 模式开始成为独立玩法模式

最近新增和修复非常集中地指向 `FreeBuild`：

- `Add FreeBuild mode to BrickPlayer`
- `Fix Bricks unable to snap onto baseplate in FreeBuild mode`
- `BuildFreeBuildInventory()`
- UI 中新增 `FreeBuild` 入口与工具栏
- `assets/omr/freebuild.ldr`

这表明当前目标已经从“回放/浏览 LDraw 场景”进一步推进到：

**让玩家直接进入自由搭建模式进行拼搭。**

FreeBuild 不是一个简单开关，而是涉及一整套运行时状态：

- 初始场景与地基（baseplate）
- 库存/部件来源
- 拖拽行为
- 可放置判定
- 吸附约束
- UI 工具入口

也就是说，BrickPlayer 现在已经不只是“LDraw 模型查看器”，而是在向 **可编辑、可搭建的乐高玩法容器** 演化。

---

## 二、目前已经成型的关键技术点

### 1. 拖拽稳定性

近期多次提交都在修正拖拽和物理同步问题：

- `Add stable drag interaction for BrickPlayer parts`
- `Fix BrickPlayer dynamic body physics offset`
- `Fix BrickPlayer physics body sync and add OBB debug draw`
- `Improve BrickPlayer part physics feel`

这说明当前交互层最核心的问题之一，是**视觉变换、物理刚体、拖拽状态**三者之间的一致性。

如果这三者不同步，用户会直接感知到：

- 拖拽漂移
- 部件抖动
- 放下后位置不对
- 吸附点与显示位置不一致

所以这些提交虽然看起来是“修 bug”，实际是在补 BrickPlayer 可玩性的地基。

---

### 2. Shadow-based snapping

`feat(brickplayer): add shadow-based free snapping` 是一个很值得注意的点。

它说明当前并不只是拿零件几何去做粗暴碰撞判定，而是在构造一层面向搭建逻辑的 **shadow / connector abstraction**。

从 `BrickPlayerLDrawShadow.*` 的结构判断，这套机制大致承担：

- 从 LDraw 数据中提取/映射可吸附连接器
- 建立原始零件与 shadow 数据之间的解析关系
- 在运行时为吸附判断提供比渲染网格更轻、更稳定的数据表示

这类做法的好处很明显：

- 吸附逻辑不必依赖复杂三角网格碰撞
- 更容易表达 stud / tube / cylinder 这类乐高连接语义
- 更方便做兼容性规则判断
- 更容易调试与可视化

如果这个方向继续深化，它会成为整个 BrickPlayer 搭建系统最有价值的基础设施之一。

---

### 3. 反馈系统开始补全

近期关于反馈的提交包括：

- `refine snap feedback and audio`
- 各类 snap candidate / confirm debug draw
- OBB debug draw
- 交互状态文案与调试信息渲染

这意味着当前开发不再停留在“算法正确就行”，而是开始重视：

- 玩家是否知道当前能不能吸附
- 玩家是否感知到吸附目标
- 玩家是否理解当前状态（free drag / candidate / invalid）
- 开发者是否能看清问题出在哪

这一步很重要，因为乐高搭建体验的核心不只是规则正确，更是**交互反馈是否让人安心**。

---

## 三、目前的系统分层轮廓

从最近代码演进看，BrickPlayer 相关技术已经出现比较清晰的分层：

### 1. 数据与资源层

- `FLDrawLoader`
- `FLDrawParser`
- `FLDrawConfig`
- LDraw pak workflow

负责：LDraw 文件、库资源、解析、挂载和运行时导入。

### 2. 搭建语义层

- `BrickPlayerLDrawShadow`
- Snap connectors / compatibility / hover filter

负责：把 LDraw 资源映射成“可搭建逻辑”能理解的数据结构。

### 3. 玩法状态层

- `BrickPlayerGameInstance`
- FreeBuild state
- drag state / hover state / active candidate

负责：玩家当前在做什么、拖着哪个零件、是否命中候选吸附点、是否处于自由搭建等。

### 4. 交互规则层

- `BrickPlayerSnapLogic`

负责：候选筛选、连接规则、吸附判定与结果计算。

### 5. 表现与调试层

- `BrickPlayerUserInterface`
- `BrickPlayerGameInstanceDebug`
- 辅助线、点、包围盒、状态文本、音效反馈

负责：让玩家看见、让开发者调试、让交互变得可理解。

这个分层还在演进中，但方向是对的。

---

## 四、当前阶段最有价值的成果

如果只看“最近这轮开发最重要的技术产出”，我认为有四个：

### 1. BrickPlayer 已从展示走向交互

系统不再只是加载 LDraw 然后展示，而是已经能围绕零件进行拖拽、吸附、放置和调试。

### 2. FreeBuild 已经立起来了

虽然还在打磨，但它已经是一个明确模式，而不是零散实验功能。

### 3. Snap 系统开始模块化

这是从原型走向长期维护的必要步骤。

### 4. 物理与交互的一致性开始被系统性修复

这是所有“手感”相关问题的基础，没有它，后面做再多 UI 和反馈都不稳。

---

## 五、当前阶段仍然暴露的技术风险

基于最近提交方向，可以推测出当前仍需持续关注的风险点：

### 1. 规则复杂度会快速膨胀

乐高连接规则一旦扩展到更多零件类型、姿态和特殊件，snap 兼容性逻辑会迅速复杂化。

需要尽早思考：

- 连接器类型系统是否足够稳定
- 兼容规则是散落在代码里，还是可数据化
- 是否能把“几何计算”和“连接语义”分层

### 2. FreeBuild 会把运行时状态管理推到更复杂的级别

一旦进入自由搭建，系统要长期面对：

- 取件/放件
- 撤销/重做
- 已拼装结构重组
- 父子层级变更
- 物理与逻辑状态同步

这要求 `GameInstance` 里的状态不能无限膨胀，否则后面会越来越难维护。

### 3. 调试能力必须持续建设

当前已经开始加 debug draw，这是正确的。Brick 搭建这类系统如果没有强调试能力，很难排查：

- 为什么这个点能吸附 / 不能吸附
- 为什么目标位置漂了
- 为什么放下后物理状态错了
- 为什么 baseplate 或 inventory 中的砖块行为不一致

这一块不是临时工具，而应该被视为核心开发基础设施。

---

## 六、我对当前技术路线的判断

目前 `gkNextEngine` 在 BrickPlayer / LDraw 方向上的路线可以概括为：

> 先建立基于 LDraw 的乐高零件与装配数据基础，
> 再通过 shadow / connector / snap logic 构建可搭建语义层，
> 最终把它变成一个支持自由拼搭的互动玩法系统。

这条路线是成立的，而且最近提交已经证明：

- 不是停留在概念层
- 不是只做资产导入
- 而是在一步步补齐“可玩”的必要技术细节

如果继续沿这个方向推进，接下来最值得做深的，不是再分散开新主题，而是继续把以下三件事打透：

1. **连接语义 / 吸附规则**
2. **拖拽-放置-物理同步的一致性**
3. **FreeBuild 模式的完整操作闭环**

---

## 七、一句话总结

当前 `gkNextEngine` 的核心开发主题非常明确：

**以 LDraw 为内容基础，以 Snap/Shadow 为搭建语义核心，把 BrickPlayer 推进成一个真正可交互、可自由搭建的乐高玩法系统。**
