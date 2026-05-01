# 空裂 KONG LIE 3D — UI 打磨计划（UI Polish Plan）

## Context

[`plan.md`](plan.md)（M1-M8 MVP）与 [`polish-plan.md`](polish-plan.md)（Q1-Q19 视觉/特效/玩法打磨）已分别完成两轮长程开发。当前游戏的**逻辑、战斗手感、视觉特效**已达到初版交付水准，但 **UI 层（HUD 窗口排布、视觉风格、阶段感）仍处于"功能堆砌"状态**，需要单独一轮抛光，让 UI 整体观感与游戏品质对齐。

> 本计划只聚焦 ImGui HUD 与界面信息层级，**不动**战斗/技能/特效逻辑，也**不替换** ImGui 渲染管线。

### 现状盘点（基于 `KongLie3DUI.cpp` 的 review 结论）

`RenderHUD` 当前依次绘制 **17 个 draw 块**（[KongLie3DUI.cpp:1264](../../../src/Application/KongLie3D/KongLie3DUI.cpp)）：
1. `DrawDeploymentZoneGuidance`（敌/我区半透明色块）
2. `DrawDragHighlights`
3. `DrawAttackTraces` / `DrawSkillEffects` / `DrawDamagePopups`
4. `DrawUnitHealthBars`（38px 固定宽 HP 条）
5. `DrawHeroPanel`（左侧浮窗 220×500）
6. `DrawBattleTimer`（顶部居中 200×36）
7. `DrawSideControlPanel`（右上 220×235，标题"Battle"）
8. `DrawRelicPanel`（右下 284×320）
9. `DrawStatsPanel`（底部全宽 ×130）
10. `DrawOvertimeOverlay` / `DrawUltimatePresentation`
11. `DrawDeploymentHintOverlay`（中上方两行文字）
12. `DrawRendererIndicator`（右下角小窗）
13. `DrawHoveredPieceTooltip`
14. `DrawResultModal`

**核心硬伤**

| 类别 | 问题 | 证据 |
|---|---|---|
| 风格 | 中英文混杂：标题 `Heroes/Battle/Stats/Relics`、按钮 `Start Battle/Pause/Resume/Speed: 1x/Battle Ended`、stat 列 `Name/AD/AP/Taken/Heal`、`Player: %d/6 Enemy: %d/6`、`Remaining %d s`、英雄卡 `R Ready/R Charging/X Fallen/Relic: None/HP/MP` | [KongLie3DUI.cpp:605](../../../src/Application/KongLie3D/KongLie3DUI.cpp) 起整段 |
| 风格 | 默认 ImGui 深灰皮肤，与游戏冷色棋盘 + 暖色玩家氛围完全不搭，无品牌色 | 无 `ImGui::StyleColors*` 自定义 |
| 布局 | 7 个独立浮窗各占角落，遮挡棋盘可视区；右下角 `Relics(320×284)` + `RendererIndicator` + `Ultimate大字` 三者重叠 | 见上 |
| 布局 | 战斗中 `Relics` 自动消失但 `RendererIndicator + DeploymentHint` 也仅在 Deployment 才有意义；多块 UI 阶段感缺失 | DrawRelicPanel 仅在 Deployment 展示，但 Indicator/Hint 文案"按 F3 切换"在战斗中也仍有意义却被隐藏 |
| 布局 | `Stats(底部全宽×130)` 在部署阶段就常驻，但此时还没有任何数据 → 大片空白干扰视觉 | DrawStatsPanel 无 state 判断 |
| 布局 | `HeroPanel(220×500)` 占据左侧 70% 高度，但内容（relic banner + synergies + 2 hero cards）太挤，Hero card 在低分辨率下被裁 | hero card 无 min size，relic + synergies 占了 ~200px |
| 调试残留 | `showMvpWindow_ = true` 默认显示 "KongLie3D MVP / Phase X" 调试窗口，玩家看到一脸懵 | [KongLie3DGameInstance.hpp:70](../../../src/Application/KongLie3D/KongLie3DGameInstance.hpp) |
| 信息密度 | HP bar 一律 38px，远处单位过大、近处单位过小；不区分敌我 | DrawUnitHealthBars 写死 width=38 |
| 反馈 | `Result Modal(420×220)` 朴素，没有视觉冲击；胜负/平局区分仅靠 borderColor，没有 icon、动画、统计速览 | DrawResultModal |
| 反馈 | 关键事件（开战 / 圣物激活 / 难度切换 / 加时开始）无统一通知/横幅；玩家容易错过 | 仅 OvertimeOverlay 有专门带 banner |
| 操作 | 没有键位提示页（F3/SPACE/P/ESC/1/2/4 散落在 Hint overlay + 各 tooltip 里） | 仅 Deployment hint 提到 SPACE/F3 |
| 设置 | 没有音量/速度/全局选项面板，玩家想调音量只能改 CVar | DrawSideControlPanel 仅有速度按钮 |
| 阶段切换 | Deployment → Battle → Ended 三个阶段 UI 没有"过场"感（只是窗口直接出现/消失） | 无任何过渡动画 |

### 引擎/项目已具备但本轮可复用的能力

| 能力 | 路径 | 用途 |
|---|---|---|
| 中文字体（Q1 已加载） | [assets/fonts/DroidSansFallback.ttf](../../../assets/fonts/DroidSansFallback.ttf) + [KongLie3DGameInstance.cpp:118](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp) | 所有英文 UI 可换中文，字符已在 atlas |
| ImGui 自定义样式 API | `ImGui::GetStyle()` / `PushStyleColor` / `StyleColorsDark` | 全局换肤 |
| `KongLie3D::U8Text(u8"…")` 工具函数 | 已在 [KongLie3DAudio.hpp / Skills](../../../src/Application/KongLie3D/) 使用 | 直接写中文 UTF-8 字面量 |
| `ButtonWithClick` | [KongLie3DUI.cpp:211](../../../src/Application/KongLie3D/KongLie3DUI.cpp) | 所有按钮自带音效 |
| ImGui 多字号字体 | 同一 ttf 可加载多 size | 标题大字 / 正文小字 / HUD 中字 三档 |

### 决策

- **范围**：U1-U10 必做（~6-8h），U11-U13 看进度选做（~3-4h）。完成 U1-U7 后 UI 已经整体协调，可交付一个观感升级版。
- **顺序**：先全局风格（U1 字体/配色 → U2 中文化）→ HUD 重排（U3-U6）→ 强化结算/通知（U7-U8）→ 抛光（U9-U13）。
- **每个任务 30min-1.5h**，独立可执行，依赖关系显式标注。
- **禁止**：动战斗/技能/特效逻辑、改变摄像机、引入新 ImGui 插件 / docking、改 vcpkg 依赖。

## 任务索引

### 第一波 — 风格统一（必做，~2h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [U1](#u1-imgui-全局风格与多字号字体) | ImGui 全局风格与多字号字体 | ~1h | — |
| [U2](#u2-中文化与文案统一) | 中文化与文案统一 | ~45m | U1 |

### 第二波 — HUD 布局重排（必做，~3-4h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [U3](#u3-stats-面板阶段化与折叠) | Stats 面板阶段化与折叠 | ~45m | U2 |
| [U4](#u4-右侧控制面板拆分与瘦身) | 右侧控制面板拆分与瘦身 | ~45m | U2 |
| [U5](#u5-hero-面板分区重排) | Hero 面板分区重排 | ~1h | U2 |
| [U6](#u6-顶部计时器卡片化与状态化) | 顶部计时器卡片化与状态化 | ~30m | U1 |

### 第三波 — 反馈与结算（必做，~2h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [U7](#u7-结算-modal-视觉重构) | 结算 Modal 视觉重构 | ~1.5h | U1, U5 |
| [U8](#u8-阶段过渡横幅与事件通知) | 阶段过渡横幅与事件通知 | ~1h | U1 |

### 第四波 — 抛光与可选项（选做，~3-4h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [U9](#u9-mvp-调试窗口收纳与帮助页) | MVP 调试窗口收纳与帮助页 | ~45m | U2 |
| [U10](#u10-单位-hp-条距离自适应与阵营色) | 单位 HP 条距离自适应与阵营色 | ~45m | — |
| [U11](#u11-设置面板音量--渲染管线--速度) | 设置面板（音量 / 渲染管线 / 速度） | ~1h | U9 |
| [U12](#u12-渲染指示器与按键提示融合) | 渲染指示器与按键提示融合 | ~30m | U11 |
| [U13](#u13-rendererhud-渲染顺序整理与通用-z-层) | RenderHUD 渲染顺序整理与通用 z 层 | ~30m | U3-U8 |

---

## U1. ImGui 全局风格与多字号字体

**优先级**: P0  **工时**: ~1h

### 背景
当前 ImGui 使用默认 `StyleColorsDark`，窗口圆角、边框、内边距均为通用值，按钮/标题/正文都用同一字号（Q1 加载的 18px DroidSansFallback）。"统一品牌色 + 三档字号" 是后续所有任务的视觉基础。

### TODO
- [ ] 在 `KongLie3DGameInstance::OnInitUI`（[KongLie3DGameInstance.cpp:118](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp) 附近）追加 3 个字号的字体加载：
  ```cpp
  auto& fonts = ImGui::GetIO().Fonts;
  const ImWchar* ranges = GetKongLieGlyphRanges(*fonts);
  // 主字（HUD/正文/按钮）：18 — 已存在，作为默认字
  // 标题字（结算/横幅）：32
  // 大字（大招名/胜负标语）：56
  KongLieFonts::Title  = fonts->AddFontFromFileTTF("assets/fonts/DroidSansFallback.ttf", 32.0f, &cfg, ranges);
  KongLieFonts::Display= fonts->AddFontFromFileTTF("assets/fonts/DroidSansFallback.ttf", 56.0f, &cfg, ranges);
  ```
  把三个 `ImFont*` 暴露在 `namespace KongLie3D::KongLieFonts`，由 UI 模块按需 `PushFont/PopFont`。
- [ ] 新建 `KongLie3DStyle.hpp/cpp`：定义 `ApplyKongLieImGuiStyle()`，在 `OnInitUI` 字体加载后调用：
  - 主色板（const）：
    - `Accent`     = `(0.32, 0.62, 0.95, 1.0)`（玩家蓝）
    - `Hostile`    = `(0.85, 0.32, 0.32, 1.0)`（敌方红）
    - `Highlight`  = `(0.97, 0.83, 0.33, 1.0)`（金，激活/胜利）
    - `Surface`    = `(0.06, 0.08, 0.12, 0.92)`（窗口底）
    - `SurfaceAlt` = `(0.10, 0.13, 0.18, 0.95)`（卡片底）
    - `Border`     = `(1.0, 1.0, 1.0, 0.10)`
    - `TextDim`    = `(0.74, 0.78, 0.86, 1.0)`
  - `ImGuiStyle&` 字段：`WindowRounding=8 / FrameRounding=6 / GrabRounding=6 / WindowPadding=(12,10) / FramePadding=(10,6) / ItemSpacing=(8,6) / WindowBorderSize=1 / FrameBorderSize=0 / ScrollbarRounding=8`
  - 用 `style.Colors[ImGuiCol_*]` 把 `WindowBg / TitleBg / TitleBgActive / Button / ButtonHovered / ButtonActive / Header / Tab / TabActive / Separator / FrameBg / TableRowBg / TableRowBgAlt` 换成上述色板的合理映射
- [ ] 新增辅助：`KongLie3D::PushPanelStyle()` / `PopPanelStyle()` — 包裹一组 `PushStyleColor(ImGuiCol_WindowBg, Surface)` + `PushStyleVar(ImGuiStyleVar_WindowRounding, 10)`，让所有 HUD 面板调用统一外观，避免散落写法
- [ ] 把 [KongLie3DUI.cpp](../../../src/Application/KongLie3D/KongLie3DUI.cpp) 中所有 `ImGui::Begin("Heroes/Battle/Stats/Relics…")` 包一层 `PushPanelStyle/PopPanelStyle`

### 涉及文件
- 改：`KongLie3DGameInstance.cpp`（OnInitUI 加多字号）、`KongLie3DUI.cpp`
- 新建：`src/Application/KongLie3D/KongLie3DStyle.{hpp,cpp}`

### 验收方法
1. 编译通过（`./build.bat --preset full-windows`）
2. 启动后 ImGui 主题一眼可看出"自定义"：圆角窗口、深蓝灰底、金色高亮按钮
3. 全局字号默认 18，标题/横幅可被对应 PushFont 切到 32/56
4. 所有面板（Hero/Battle/Stats/Relics/Timer）外观一致，无任何"白底蓝边"残留
5. ImGui 控件 hover 状态颜色变化使用 Accent，不再是默认蓝

### 注意
- 不要 `ImGui::StyleColorsLight()`，棋盘是冷色调，需要深色底
- 字号 18/32/56 是经验值，必要时按视觉调整 ±2，不要超过 64（atlas 体积）
- `Accent` 色和玩家阵营底盘色（Q7）应保持视觉关联，但不必完全一致

---

## U2. 中文化与文案统一

**优先级**: P0  **工时**: ~45m  **依赖**: U1

### 背景
Q1 已加载中文字体并把英雄面板里的中文 UTF-8 字节字符串清理为 `u8"…"`，但仍有大量按钮/标题/列名是英文（review 段落已列举）。中英文混杂在玩家眼里非常廉价。

### TODO
- [ ] 把 [KongLie3DUI.cpp](../../../src/Application/KongLie3D/KongLie3DUI.cpp) 中以下英文全部改为 `u8"中文"`（用 `KongLie3D::U8Text(u8"…")` 包裹保持一致）：
  | 当前 | 改为 |
  |---|---|
  | 窗口标题 `Heroes###…` | `英雄###…`（`###` 后的 ID 不动） |
  | 窗口标题 `Battle###…` | `战斗###…` |
  | 窗口标题 `Stats###…` | `数据###…` |
  | 窗口标题 `Relics###…` | `圣物###…` |
  | `Player: %d/6` / `Enemy: %d/6` | `我方 %d/6` / `敌方 %d/6` |
  | `Start Battle` | `开始战斗` |
  | `Pause` / `Resume` | `暂停` / `继续` |
  | `Battle Ended` | `战斗结束` |
  | `Speed: 1x/2x/4x` | `节奏 1x/2x/4x` |
  | `Remaining %d s` | `剩余 %d 秒` |
  | `Overtime +%.1f s` | `加时 +%.1f 秒` |
  | `Waiting to Start` | `准备就绪` |
  | `Choose one relic` | `选择一件圣物` |
  | `Selected` | `已携带` |
  | `Relic: None` | `未携带圣物` |
  | `R Ready` / `R Charging` / `X Fallen` | `R 大招就绪` / `R 蓄力中` / `已阵亡` |
  | `HP` / `MP` 标签 | `生命` / `魔力` |
  | tab `Player` / `Enemy` | `我方` / `敌方` |
  | stat 列 `Name/AD/AP/Taken/Heal` | `名称/物伤/法伤/承伤/治疗` |
  | renderer indicator `F3 切换渲染管线` | 保持不变（已是中文） |
- [ ] [KongLie3DUI.cpp:851](../../../src/Application/KongLie3D/KongLie3DUI.cpp) `Enemy DMG x{:.2f}` → `敌方伤害 x{:.2f}`
- [ ] 检查 `KongLie3DGameInstance.cpp` / `KongLie3DBattleSystem.cpp` / `KongLie3DSkills.cpp` 是否还有英文 UI 字面量（spdlog 日志可保留英文）
- [ ] 文案审一遍，避免半生硬翻译（"Battle Ended" 不要直译"战斗已结束"，用"战斗结束"更顺）

### 涉及文件
- 改：`KongLie3DUI.cpp`（主要）、`KongLie3DGameInstance.cpp`、`KongLie3DBattleSystem.cpp`、`KongLie3DSkills.cpp`（如有命中）

### 验收方法
1. 编译通过
2. 启动后从部署 → 战斗 → 加时 → 结算 → 重来全流程，UI 文字 100% 中文（不含 spdlog）
3. 中文字符无方块/缺字（U1 字体已覆盖常用中文）
4. 列宽/按钮宽度未因中文文案变长而错位（必要时加 `ImVec2(-1, 0)` 让按钮拉满）

### 注意
- `###` 后的稳定 ID **不要改**（影响 ImGui 状态保留）
- "节奏" 比"速度"更贴自走棋语境（参考 LoL/TFT）
- 大招按钮 `R 大招就绪` 比 `R 已就绪` 更明确"按 R"的指引
- 列名 `物伤/法伤/承伤/治疗` 与 web 原版保持一致

---

## U3. Stats 面板阶段化与折叠

**优先级**: P0  **工时**: ~45m  **依赖**: U2

### 背景
[`DrawStatsPanel`](../../../src/Application/KongLie3D/KongLie3DUI.cpp:950) 始终绘制 `(W-16) × 130` 的全宽底栏。部署阶段没有数据，但占据屏幕底部 1/4，且空 Tab 视觉很差。战斗中又过宽，玩家不需要同时看 6 个单位详情。

### TODO
- [ ] 在 `DrawStatsPanel` 顶部加阶段守卫：
  ```cpp
  const auto state = gameInstance.GetBattleSystem().GetState();
  if (state == EBattleState::Deployment) return;  // 部署完全不显示
  ```
- [ ] 收窄面板：宽度从 `viewport.x - 16` 改为固定 `560`，居中底部对齐 `pos = ((W-560)/2, H-148)`
- [ ] 加可折叠：`ImGui::Begin` 改用 `ImGuiWindowFlags_AlwaysAutoResize` + 顶部一行 `[▼ 战况详情] / [▲ 战况详情]` 自定义按钮（用 `gameInstance` 的 `mutable bool statsCollapsed_` 或本地 `static bool`），折叠状态只显示 1 行汇总：`我方总伤 1234 / 承伤 567 · 敌方总伤 ... / 承伤 ...`
- [ ] 战斗结束（`Ended`）时**强制展开**并加金/红边框（按胜负），让玩家自然过渡到结算 modal
- [ ] 表格内列 `物伤/法伤/承伤/治疗` 加缩写列头（"物/法/承/治"），让 560 宽够放
- [ ] 行高从默认改为 22（更紧凑）

### 涉及文件
- 改：`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 部署阶段 Stats 面板完全不出现，棋盘下方干净
3. 战斗开始后底部居中出现 560 宽折叠面板，默认展开
4. 点击折叠按钮 → 收为单行汇总；再点 → 恢复
5. 战斗结束面板自动展开 + 描金/红边
6. 不会与 Relic/Renderer Indicator 视觉重叠（Relic 此时已隐藏）

### 注意
- 折叠状态用 `static bool` 即可，不一定挂到 game instance（无需持久化）
- 不使用 ImGui 自带的 `Collapsed` window flag — 它强制带 title bar，与统一风格不符
- 战斗结束→展开的强制只触发一次（用 `static bool forcedExpandedThisRound` + state edge 检测）

---

## U4. 右侧控制面板拆分与瘦身

**优先级**: P0  **工时**: ~45m  **依赖**: U2

### 背景
[`DrawSideControlPanel`](../../../src/Application/KongLie3D/KongLie3DUI.cpp:810) 在 220×235 的小窗里塞了：
- 我方/敌方计数（2 行）
- 难度名/数值/3 个难度按钮（部署阶段）
- 开战/暂停按钮
- 节奏切换按钮

部署阶段 5-6 个交互项挤在一个面板里，对玩家是"哪个能点哪个不能点"的视觉噪音。战斗阶段大半内容（难度按钮）应隐藏。

### TODO
- [ ] 拆为两块：
  - **顶部状态条（常驻）**：`pos=(W-200, 8), size=(190, 36)`，两栏文字 `我方 X/6 ｜ 敌方 X/6`，加双方"小棋子"图标块（用 `AddRectFilled` Accent/Hostile 色块各 12×12 在数字前），无窗口标题
  - **下方控制卡（阶段感知）**：`pos=(W-200, 52), size=(190, auto)`
    - Deployment：`[难度] {当前难度}` 大字 + 3 难度小按钮一行（用 `ImGui::SameLine` 摆排，按钮宽 (190-2*spacing)/3 = ~58）+ Separator + 大按钮 `开始战斗`(高 36)
    - Battle：大按钮 `暂停` / `继续`(高 36) + 三个 `1x / 2x / 4x` 节奏小按钮一排（不再循环切换，直接选）
    - Ended：禁用按钮 `战斗结束`
- [ ] 节奏切换从单按钮"循环"改为"3 个并排互斥按钮"，当前节奏按钮金色高亮（参考 U1 Highlight 色）
- [ ] 难度按钮当前选中也用 Highlight 描边，不仅仅是底色

### 涉及文件
- 改：`KongLie3DUI.cpp`（拆 DrawSideControlPanel，拆出 `DrawTopStatusStrip` + `DrawSidePhaseControls`）

### 验收方法
1. 编译通过
2. 右上一直显示双方剩余数 + 阵营色块
3. 部署阶段右侧只看到难度+开战，没有"暂停"残影
4. 战斗阶段只看到暂停+节奏，没有"难度"
5. 节奏切换 1 → 2 → 4 直接点击对应按钮，且当前节奏明显金色高亮
6. 整体面板宽度收窄到 190，给棋盘视野让位

### 注意
- 拆面板后总宽度从 220 → 190，视野占用减少
- `AddRectFilled` 阵营小色块在数字前更直觉（视觉锚点）
- 节奏 1/2/4 三按钮共用 `ButtonWithClick` 保留音效

---

## U5. Hero 面板分区重排

**优先级**: P0  **工时**: ~1h  **依赖**: U2

### 背景
当前 Hero 面板（[DrawHeroPanel](../../../src/Application/KongLie3D/KongLie3DUI.cpp:597)）220×500 内部由上到下塞：
- 圣物 banner（1-2 行）
- 羁绊预览（1-8 行不等）
- 2 个英雄卡（每个 ~5-6 行）

中间 `BeginChild("HeroRoster")` 滚动让英雄卡常常被裁。羁绊预览在战斗中已无意义却仍占空间。结构重排能让信息层级更清晰。

### TODO
- [ ] 重排为 3 个独立子区，**总宽度** 240（比当前 220 大 20，给 hero 卡里 HP/MP 进度条让位）：
  - **顶部小卡：当前圣物**（高度 auto，~60px）
    - 一行：`[圣物图标] 名字`（彩色背景），下方一行 desc 缩略
    - 未选时：`未携带圣物` 灰字 + 一行轻提示 `部署阶段右下角选择`
  - **中部小卡：羁绊**（仅 Deployment 显示完整 6-8 项 + 状态；Battle/Ended 折叠为"已激活：远射×3、铁壁×2"一行汇总）
    - 折叠状态用 `if state != Deployment` 切换，不需要按钮
  - **底部主卡：英雄列表**（剩余空间 fill）
    - 每个英雄独占一个 `ImGui::BeginChild`(高 ~180)，内部固定 5 行：头像+名字 / HP / MP / W desc / R 按钮
    - 死亡时整个 child 半透明 0.5
    - HP/MP 进度条文字叠加（如 `1280 / 1520`）— 用 `ImGui::ProgressBar(ratio, size, fmt::format("…").c_str())`
    - R 按钮按状态切配色（U1 已定义 Accent/Highlight）
- [ ] 把 `pos=(8, 60), size=(240, ...)` 改为高度自适应 `ImGuiCond_Always` + `ImGuiWindowFlags_AlwaysAutoResize`，让面板高度跟随英雄数（避免下方留空）
- [ ] 圣物 banner 和羁绊行之间用 `ImGui::Separator` + 4px Spacing 分隔

### 涉及文件
- 改：`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 部署阶段：圣物卡 + 羁绊预览（完整列表）+ 英雄主卡，三段视觉清楚
3. 战斗阶段：羁绊折叠为单行汇总；圣物卡保留；英雄主卡内部信息流不变
4. 英雄卡 HP/MP 进度条上叠加数字（更易读）
5. 阵亡英雄整张子卡半透明
6. 面板宽度 240，左侧总占用 < 250px，棋盘可视区基本不被遮

### 注意
- AlwaysAutoResize + min height 避免空英雄列表时面板坍缩
- HP 文字 `1280 / 1520` 用浅色，避免抢眼
- 不要把英雄卡变成可折叠 — 玩家就两位英雄，全展开最直接

---

## U6. 顶部计时器卡片化与状态化

**优先级**: P0  **工时**: ~30m  **依赖**: U1

### 背景
当前计时器（[DrawBattleTimer](../../../src/Application/KongLie3D/KongLie3DUI.cpp:762)）只是一个 200×36 的 ImGui 窗口配 `Text("Remaining %d s")`，平淡如调试输出。它是战斗最重要的信息源之一，应该有"卡片"存在感。

### TODO
- [ ] 重写为自定义 draw（用 `ImGui::GetForegroundDrawList()`，不再 `ImGui::Begin`）：
  - **位置**：屏幕顶部居中，`y=12`
  - **造型**：280×52 的圆角矩形（`AddRectFilled` + `AddRect`），底色 SurfaceAlt(0.95)，边色 Border
  - **内容**：左半段 icon（用 ⏱ 文字字符或自绘小三角），右半段大字 `00:18` 倒计时（用 KongLieFonts::Title 32）
  - 加时阶段：底色变 `Hostile * 0.3 + 0.1`（暗红）+ 边框脉冲红 + 文字 `加时 +12.3s`，倒数变正数
  - 准备阶段：内容 `准备就绪 · 按 SPACE 开战`（小字 18），按钮高亮 SPACE
  - 战斗结束：隐藏（结果由 modal 接管）
- [ ] 阴影：在主矩形下方 4px 处画半透明黑矩形 + 稍大尺寸做投影效果
- [ ] 倒数 ≤ 5s 时数字闪烁（按 0.5Hz `sin` 调整 alpha 0.6→1.0）

### 涉及文件
- 改：`KongLie3DUI.cpp`（重写 DrawBattleTimer，移除 Begin/End）

### 验收方法
1. 编译通过
2. 部署阶段顶部居中卡片显示 `准备就绪 · 按 SPACE 开战`
3. 战斗中 `00:25 → 00:00` 倒数，最后 5 秒数字闪
4. 进入加时：卡片转暗红、文字 `加时 +0.5s` 不断累加
5. 战斗结束卡片隐藏
6. 阴影/圆角/字号与 U1 整体风格一致

### 注意
- 用 foreground draw list 自绘可以避免 ImGui Window 的 padding/border 限制
- 倒数格式 `mm:ss` 比 `25 s` 更专业；加时用 `+m.ms` 区分
- 不需要为它单独加 PushFont — 在自绘里 `drawList->AddText(font, fontSize, ...)` 直接传 `KongLieFonts::Title`

---

## U7. 结算 Modal 视觉重构

**优先级**: P1  **工时**: ~1.5h  **依赖**: U1, U5

### 背景
当前 [DrawResultModal](../../../src/Application/KongLie3D/KongLie3DUI.cpp:1086) 是 420×220 的 ImGui Popup：标题色字 + summary 文本 + 圣物/难度行 + 2-3 个按钮平铺。胜利/失败/平局只靠 `borderColor` 区分，没有图标、没有动画、没有数据速览。是玩家最直接感受到 "游戏品质" 的地方之一。

### TODO
- [ ] **整体尺寸**：560×360（更大，给统计卡留位置）
- [ ] **顶部 Hero Banner**：高 90，按胜负填底色：
  - 胜利：金线性渐变（`AddRectFilledMultiColor` 上深下浅 `(0.2, 0.16, 0.06) → (0.45, 0.36, 0.10)`）+ 居中大字 `胜利`（KongLieFonts::Display 56，金色）
  - 失败：红渐变 + `失败`
  - 平局：黄渐变 + `平局`
  - 大字两侧加装饰元素：`AddTriangleFilled`（左右各一个三角箭头，胜利金/失败红/平局黄）
- [ ] **中部内容**（高 ~210）分两栏：
  - 左栏（宽 ~280）：
    - `本局耗时 1:23`
    - `存活单位 4/6`（仅胜利/平局）/ `全军覆没`（失败）
    - `携带圣物：破军之戒` / `未携带`
    - `当前难度：困难`
    - 加时是否触发：`本局未触发加时` / `加时 8.5 秒`
  - 右栏（宽 ~240）：mini stats — 双方总伤/承伤/治疗（4 行 6 数字），用紧凑表格
- [ ] **底部按钮区**（高 ~60）：
  - 横向均分 2-3 个按钮，按钮高 44
  - 按钮顺序：左 `重来当前关`（次要灰）、中 `下一关`（高亮金，仅胜利+可进） / `通关！`（金禁用，仅胜利+末关）、右 `回主菜单`（暂时同 `重来`，次要灰，预留 hook）
- [ ] **过渡动画**：modal 第一次出现时透明度从 0 → 1.0 在 200ms 内插值（在 game instance 里加 `resultModalElapsedMs_` 计时器，state 切到 Ended 时归零）
- [ ] 用 `KongLie3D::PushPanelStyle` 包裹，确保配色统一

### 涉及文件
- 改：`KongLie3DUI.cpp`（重写 DrawResultModal）、`KongLie3DGameInstance.{hpp,cpp}`（追加 `float resultModalAppearMs_` 字段及 reset/累加逻辑）

### 验收方法
1. 编译通过
2. 战斗结束 → modal 渐入（200ms 淡入）
3. 胜利：金渐变 banner + 大字 `胜利` + 左侧统计 + 右侧 mini stats + 底部 `重来 / 下一关 / 回主菜单`
4. 失败：红渐变 + `失败` + 底部 `重来 / 回主菜单`（无下一关）
5. 平局：黄渐变 + `平局`
6. 末关胜利：`下一关` 替换为禁用 `通关！`
7. 整体观感"够分量"，玩家想截图分享

### 注意
- mini stats 数据来源 `BattleSystem::GetPieceRuntimes()` 直接 sum，不需要新接口
- `回主菜单` 按钮 MVP 期与重来一样调 `ResetBattle()`，注释里写明 `// TODO: 主菜单 stub`（这是少有可保留的注释）
- modal 在 ImGui 中无法 fade WindowBg —— 用 `PushStyleColor` 改 alpha 即可
- 不要做声音：胜利/失败音 Q4 已实现，模 modal 出现是同步触发

---

## U8. 阶段过渡横幅与事件通知

**优先级**: P1  **工时**: ~1h  **依赖**: U1

### 背景
关键事件目前缺乏统一通知：
- 开战瞬间无视觉强反馈（玩家按 SPACE 后只是计时器开始走）
- 圣物切换 / 难度切换 / 节奏切换无 toast
- 羁绊激活/失效在拖拽时只有 hero panel 数字变化
- 加时已有 banner（保留）

### TODO
- [ ] 新增 `KongLie3DNotifications.hpp/cpp`：
  ```cpp
  enum class ENotificationKind { Info, Success, Warning, Critical };
  struct FToast { std::string text; ENotificationKind kind; float lifeMs; float durationMs; };

  class FNotificationCenter {
  public:
      void Push(std::string text, ENotificationKind kind, float durationMs = 2500.0f);
      void Update(float deltaMs);
      void Render();  // 自绘到 foreground drawlist
  private:
      std::deque<FToast> toasts_;
  };
  ```
- [ ] `FNotificationCenter` 实例放 `KongLie3DGameInstance` 成员，`OnTick` 喂时间，`RenderHUD` 末尾调 `Render()`
- [ ] 渲染样式：屏幕**右下角**（避开 stats/relic 折叠后区域）从下往上堆叠 toast 卡片：
  - 宽 280、高 44、圆角 8
  - 左侧 6px 实色 bar 表示 kind 颜色（Info=Accent、Success=Highlight、Warning=橙、Critical=Hostile）
  - 文本居中
  - 进入：x 从 +60 滑入 0（200ms）；离开：alpha 1→0（300ms）
- [ ] **接入触发点**：
  - `StartBattle()` → Push `战斗开始`（Success）
  - 圣物 `SelectRelic` → Push `已携带圣物：{name}`（Info）
  - `SelectLevel` → Push `难度切换：{name}`（Info）
  - `CycleSpeedMultiplier`（U4 改为直接选后改 setter） → Push `节奏 {x}x`（Info）
  - 羁绊状态变化（在 `BattleSystem::Start` 应用羁绊时）→ Push `羁绊激活：远射×3 等 {N} 项`（Success）
  - 加时触发 → Push `加时开始！伤害递增`（Critical）
- [ ] **开战横幅**（额外一次性大字）：`StartBattle` 时除了 toast 外，屏幕中央叠一个 800ms 大字 `开战！`（KongLieFonts::Display），fade in 150ms / hold 300ms / fade out 350ms — 复用 `BattleSystem::GetUltimatePresentation` 的渲染逻辑作为参考

### 涉及文件
- 新建：`src/Application/KongLie3D/KongLie3DNotifications.{hpp,cpp}`
- 改：`KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`、`KongLie3DBattleSystem.cpp`（少数触发点）

### 验收方法
1. 编译通过
2. 部署阶段切换难度/圣物 → 右下角弹 toast，2.5 秒后消失
3. 按 SPACE 开战 → 屏幕中央 `开战！` 大字闪过 + 右下 `战斗开始` toast
4. 羁绊激活有 toast 提示
5. 加时触发 toast `加时开始！伤害递增` + 已有红边脉冲并存（不冲突）
6. 多个 toast 同时出现会向上堆叠，最多保留 4 条，更老的提前淡出

### 注意
- toast 用 deque，`Update` 中 `lifeMs <= 0` 弹出
- 不要在每帧重复 Push 同一文案（事件触发处就是边沿，不会重复）
- 开战大字与 Q10 大招大字共用 `KongLieFonts::Display`，颜色用 Accent
- 不接 BGM 切换提示（避免噪音）

---

## U9. MVP 调试窗口收纳与帮助页

**优先级**: P2  **工时**: ~45m  **依赖**: U2

### 背景
[KongLie3DGameInstance.hpp:70](../../../src/Application/KongLie3D/KongLie3DGameInstance.hpp) 的 `showMvpWindow_ = true` 让玩家进游戏先看到一个 `"KongLie3D MVP / Phase X: bootstrap OK"` 调试窗口，体验割裂。同时游戏的快捷键（SPACE/P/ESC/F3/1/2/4 + 拖拽）散落在 deployment hint 和 README，玩家很难记。

### TODO
- [ ] 把 `showMvpWindow_` 默认改 `false`；保留 `F1` 切换显示（在 `OnKey` 加 case `SDLK_F1: showMvpWindow_ = !showMvpWindow_`）
- [ ] 当 `showMvpWindow_` 时不再显示 "Phase X bootstrap OK" 这种字符串，改为**帮助页**面板（用 PushPanelStyle，标题 `帮助 (F1)###KongLie3DHelp`），居中 480×420，内容：
  ```
  操作指南
  ----------
  鼠标拖拽       调整己方阵型 / 上下场板凳
  SPACE         开始战斗
  P             暂停/继续战斗
  ESC           取消当前拖拽 / 关闭结算 modal
  1 / 2 / 4     战斗节奏切换
  F1            显示/隐藏本帮助
  F3            切换渲染管线（光追演示）

  战斗规则
  ----------
  • 自动战斗，每 100ms 一次结算
  • 30 秒内分胜负，超时进入加时（伤害每秒 +10%）
  • 45 秒强制平局
  • 英雄满魔自动放 W；R 由玩家手动释放（每场 1 次）

  关于
  ----------
  KongLie3D · 基于 gkNextRenderer 的 3D 复刻
  原型来源：webEngine-projectBreach
  ```
- [ ] 部署阶段额外在 hint overlay 下方加一行小字 `按 F1 查看帮助`
- [ ] 删掉 KongLie3DGameInstance 里所有形如 `"Phase 1: bootstrap OK"` 的调试 Text 残留（如有）

### 涉及文件
- 改：`KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 启动后没有 "MVP" / "bootstrap" 调试窗
3. 按 F1 弹出帮助页，再按收起
4. 帮助页内容完整、字号符合 U1 标准
5. 部署阶段 hint overlay 多一行 `按 F1 查看帮助`

### 注意
- 帮助页用 `PushPanelStyle`，标题中文化
- 不要把帮助页做成 modal（不要 BeginPopup），让玩家可以同时看棋盘
- 内容里不要列具体数值（HP/ATK），那是 hover tooltip 的范围

---

## U10. 单位 HP 条距离自适应与阵营色

**优先级**: P2  **工时**: ~45m

### 背景
[DrawUnitHealthBars](../../../src/Application/KongLie3D/KongLie3DUI.cpp:266) 写死 `width=38, height=5`。摄像机 fov 60° + 距离差让远端棋子（敌方 row 0-3）的 38px HP 条相对单位本身过大，近端过小；且敌我视觉上没区分。

### TODO
- [ ] 计算单位与摄像机距离 `dist = length(camera.Position - piece.world)`（Camera 取自 `OverrideRenderCamera`）
- [ ] 距离基准 `BaselineDist = 10`，宽度按比例：`width = clamp(38 * BaselineDist / dist, 24, 50)`
- [ ] 高度按宽度比例 `height = max(3, width * 0.13)`
- [ ] 加阵营色细节：HP bar 上方 1px 加细线，玩家暖橙 `IM_COL32(255, 180, 100, 200)`、敌方冷紫 `IM_COL32(180, 80, 200, 200)`
- [ ] 阵亡瞬间动画 - 当前已经 `!alive` 直接跳过；保留
- [ ] 头顶护盾叠加（如果 piece.shieldTimerMs > 0）：HP 条上方 2px 处加蓝色 `shield` 条（宽度按 shield 量比例，简单线性映射 `shield/150`）

### 涉及文件
- 改：`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 远端敌方单位 HP 条不会"撑满"棋子；近端单位 HP 条不再过细
3. 玩家方头顶有暖橙细线、敌方有冷紫细线，一眼分敌我
4. Blue 放 W 后队友头顶蓝色护盾条出现，3 秒后消失
5. 阵亡时 HP 条立刻消失

### 注意
- HP bar 自适应**不**用屏幕高度，用世界距离（更稳定）
- shield 条简单线性 `shield/150` 即可，不需要实时取 max shield
- 别让 HP bar 太花哨，主要是清晰

---

## U11. 设置面板（音量 / 渲染管线 / 速度）

**优先级**: P2  **工时**: ~1h  **依赖**: U9

### 背景
玩家想调音量/换渲染管线/改默认节奏只能改 CVar 或熟记快捷键。引入一个简单的 Settings 面板（`Esc + S` 或 `O` 键召唤）符合"成品游戏"基本预期。

### TODO
- [ ] `OnKey` 中加 `SDLK_o`：toggle `showSettingsPanel_`
- [ ] 新增 `DrawSettingsPanel`：360×260 居中，标题 `设置###KongLie3DSettings`，PushPanelStyle 风格
- [ ] 内容：
  - **音效音量** Slider 0.0-1.0：读 / 写 CVar `audio.sfxVolume`
  - **背景音乐音量** Slider 0.0-1.0：读 / 写 CVar `audio.bgmVolume`（如未注册先在 `ApplyDefaultCVars` 加上）
  - **默认渲染管线** 4 个 RadioButton：PathTracing / SoftTracing / PureAmbient / VoxelTracing，写 CVar `r.rendererType`
  - **默认战斗节奏** 3 个 RadioButton：1x / 2x / 4x，写 `battle.speedMultiplier`
  - 底部按钮 `关闭`
- [ ] 帮助页（U9）补一行 `O — 打开设置`

### 涉及文件
- 改：`KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 按 O 弹出设置面板，再按 O 收起
3. 拖音量 slider，立即生效（攻击/UI 音变小）
4. 切渲染管线，棋盘视效立即变化
5. 切节奏，下一秒战斗速度变化
6. 关闭按钮正确关闭面板

### 注意
- CVar 写入用 `cvars.SetValueFromString("...", "...", ECVarSetBy::Console)`
- 设置面板**不**做磁盘持久化（重启回默认），简化实现
- 不要影响战斗 tick — 仅修改 CVar 数据，BattleSystem 在下一帧自然采用

---

## U12. 渲染指示器与按键提示融合

**优先级**: P2  **工时**: ~30m  **依赖**: U11

### 背景
[DrawRendererIndicator](../../../src/Application/KongLie3D/KongLie3DUI.cpp:568) 永久占据右下角小窗，与 `DeploymentHintOverlay` 重复（hint 也提"按 F3 切换"）。U11 设置面板能改默认管线后，运行时切换快捷键的常驻提示就更没必要常驻。

### TODO
- [ ] 渲染指示器改为**仅切换瞬间显示 toast**：调用 U8 的 `NotificationCenter.Push`，文案 `渲染管线：{name}`，Info 级
- [ ] 删除 `DrawRendererIndicator` 函数及其在 `RenderHUD` 的调用
- [ ] 保留 `gameInstance.GetRendererLabel()` 接口（U7 结算 modal 可能用到）
- [ ] U9 帮助页 / U11 设置面板补"光追演示"说明

### 涉及文件
- 改：`KongLie3DUI.cpp`、`KongLie3DGameInstance.cpp`（如有调用）

### 验收方法
1. 编译通过
2. 启动后右下角不再永驻 `Render: PathTracing` 小窗
3. 按 F3 切换 → 右下角 toast 弹 `渲染管线：PathTracing`，2.5s 后消失
4. 设置面板里仍能改默认管线
5. 屏幕右下角更干净（让位给 toast 区）

### 注意
- 如果 U8 `NotificationCenter` 未做（被砍），保留 indicator 函数但仅在 `Deployment` 状态显示

---

## U13. RenderHUD 渲染顺序整理与通用 z 层

**优先级**: P2  **工时**: ~30m  **依赖**: U3-U8

### 背景
目前 `RenderHUD`（[KongLie3DUI.cpp:1264](../../../src/Application/KongLie3D/KongLie3DUI.cpp)）的 17 个 draw 块顺序是历史叠加的结果，部分 z 层不正确：
- toast / overtime banner / 大招大字 / result modal 同处于 foreground drawlist，谁先谁覆盖
- HP bar / 阵营色 / 拖拽高亮谁先画影响半透明叠加
- result modal 出现时下方 stats 折叠卡片已强制展开，应该在 modal 下层

### TODO
- [ ] 重新归类 `RenderHUD` 调用顺序为 4 段，加注释说明：
  ```
  // 1) Ground overlays (棋盘上的世界叠加)
  DrawDeploymentZoneGuidance
  DrawDragHighlights
  DrawSkillEffects(地面环 / shield aura)

  // 2) Above-unit overlays (单位附近)
  DrawAttackTraces
  DrawDamagePopups
  DrawUnitHealthBars

  // 3) HUD windows (固定面板)
  DrawTopStatusStrip
  DrawSidePhaseControls
  DrawHeroPanel
  DrawRelicPanel
  DrawStatsPanel
  DrawBattleTimer  // 自绘卡片

  // 4) Overlay layer (最上层、临时)
  DrawDeploymentHintOverlay
  DrawHoveredPieceTooltip
  DrawOvertimeOverlay
  DrawUltimatePresentation
  notificationCenter.Render()
  DrawResultModal  // 始终在最顶
  ```
- [ ] modal 出现时 toast 自动暂停（在 `NotificationCenter::Render` 前判断 `state == Ended` 跳过新渲染，已加入的 toast 保留淡出）
- [ ] 用 `// section` 注释清晰分段，方便后续 agent 理解

### 涉及文件
- 改：`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 全流程跑一遍，无视觉穿帮（modal 在最顶 / toast 不被 modal 覆盖中文残影）
3. 拖拽时合法格高亮在棋子下方（地面层），HP 条在棋子上方（above-unit）
4. 大招大字在所有 HUD 窗口之上但被 modal 覆盖（合理）

### 注意
- ImGui foreground drawlist 后画的覆盖先画的，Window 在 PushFocus 后置顶
- 不要拆 RenderHUD 文件，保持单文件可读

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target KongLie3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀 |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 中文 | UI 文案一律 `KongLie3D::U8Text(u8"…")`，源文件 UTF-8 with BOM |
| 注释 | 默认不写，仅写非显然的 WHY；分段注释 `// section` 例外 |
| 字体 | 使用 `KongLieFonts::Title` / `Display`，不要在 UI 里 `AddFontFromFileTTF` |
| 配色 | 一律走 `KongLie3DStyle.hpp` 暴露的 const 色板，不要硬编码 `IM_COL32` |
| 提交 | 不要 git commit，由用户决定 |

**禁止**：
- 改 `src/ThirdParty/`、`external/`
- 引入新依赖（如 ImGui Docking branch、ImNodes、ImGuiZmo）
- 改战斗逻辑/技能/特效（U7 mini stats 仅是读数据）
- 改摄像机参数 / OverrideRenderCamera
- 用 `// removed` / `// TODO` 留半成品代码

## 与 plan.md / polish-plan.md 的关系

- [plan.md](plan.md)：M1-M8 MVP（已完成，历史记录）
- [polish-plan.md](polish-plan.md)：Q1-Q19 视觉/特效/玩法打磨（已完成）
- 本文件：U1-U13 UI 抛光，**只动 ImGui HUD 与样式**，与前两份计划正交

`Q1`（中文字体加载）已经完成，本计划复用该字体并扩展多字号。`Q13`（hover tooltip）和 `Q17`（部署引导）已完成，本计划在 U5/U9 中保留并整合到统一风格里。

## 后续 agent 调用模板

```
请执行 docs/projects/konglie-3d/ui-polish-plan.md 中的 U{N} 任务。
- 严格按 TODO 清单做，不扩大范围（不动战斗/技能/特效逻辑）
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报
- 不要 commit，不要做"顺手清理"
- 如果发现依赖任务（注明依赖的 U{M}）未完成，先汇报让用户决定
- UI 配色一律使用 KongLie3DStyle 暴露的色板，不硬编码 IM_COL32
```

**推荐执行顺序**：

1. **第一波（风格统一，~2h）**：U1（字体+配色）→ U2（中文化）
2. **第二波（HUD 重排，~3-4h）**：U3（Stats）→ U4（控制面板）→ U5（Hero 面板）→ U6（计时器）
3. **第三波（反馈强化，~2h）**：U7（结算 modal）→ U8（toast + 阶段横幅）
4. **第四波（抛光，~3-4h，可选）**：U9（帮助页）→ U10（HP 条）→ U11（设置）→ U12（渲染提示融合）→ U13（z 层整理）

第一波 + 第二波完成即可解决 90% 的"UI 杂乱"观感问题；第三波让 UI 有反馈感；第四波是锦上添花。
