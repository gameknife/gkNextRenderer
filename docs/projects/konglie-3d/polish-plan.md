# 空裂 KONG LIE 3D — 打磨计划（Polish Plan）

## Context

[`plan.md`](plan.md) 中 M1-M8 已由 codex 完成，游戏已可玩（拖拽部署、战斗 tick、技能、加时、结算、圣物 buff 全部到位），数值严格对齐 web 原版。本计划目标：**让 3D 复刻品质明显高于 web 原版**。

### 当前实现的明显短板（人工 review 结论）

| 类别 | 问题 | 影响 |
|---|---|---|
| 视觉 | 全部棋子都是同色 box+sphere，看不出职业差异 | 体感「积木堆」 |
| 视觉 | 棋盘单调（仅交替灰+中线紫，无装饰） | 缺乏沉浸感 |
| 视觉 | 单位无阴影、无阵营色调、无边光 | 飘浮感、辨识度低 |
| 特效 | 攻击/技能全部用 ImGui 2D draw list 在屏幕上画线圆 | 缺少 3D 冲击力 |
| 特效 | 死亡仅下沉 0.3m + 变暗，没有命中反馈 | 战斗无打击感 |
| 音效 | **完全静音** | 交互无反馈 |
| UI | UI 文字大量 raw UTF-8 字节字符串，但未加载中文字体 | 中文显示为方块 |
| 性能 | 移动插值/死亡动画每帧 `MarkDirty()` | 5-15ms/帧 GPU 资源重建浪费 |
| 玩法 | `synergies` 羁绊字段 web 原版有，codex JSON/Loader 都未搬 | 缺一个完整系统 |
| 代码 | `BootstrapScene = "playground.glb"` 先加载垃圾场景再清空 | hack |
| 代码 | `AttackCooldownScale = 1.2f` 战斗速度被减慢 20% | 节奏拖沓，与 web 原版不一致 |
| 玩法 | 没有暂停快捷键、ESC 取消拖拽、加速倍率、单位 tooltip | 操作不顺手 |

### 引擎已具备但 codex 未使用的能力

调研自 [src/Runtime/Subsystems/](../../../src/Runtime/Subsystems)、[src/Assets/Loaders/](../../../src/Assets/Loaders)、[assets/fonts/](../../../assets/fonts)：

| 能力 | 路径 | 用途 |
|---|---|---|
| **NextAudio**（miniaudio） | [src/Runtime/Subsystems/NextAudio.h](../../../src/Runtime/Subsystems/NextAudio.h) | `PlaySound(name, loop, volume)` 播放 SFX/BGM |
| **DroidSansFallback.ttf** | [assets/fonts/DroidSansFallback.ttf](../../../assets/fonts/DroidSansFallback.ttf) | 中文字体已存在，`ImGui::AddFontFromFileTTF` 加载 |
| **PathTracing 切换** | CVar `r.rendererType` (0/1/2/3) | F3 一键切换 PathTracing/SoftTracing/Modern，体现 3D 引擎独有优势 |
| **PhysicsComponent + 冲量** | [src/Runtime/Subsystems/NextPhysics.h](../../../src/Runtime/Subsystems/NextPhysics.h) `AddForceToBody` | 死亡时给单位刚体冲量做"被打飞"碎裂 |
| **AreaLight 动态生成** | [src/Assets/Loaders/FProcModel.h](../../../src/Assets/Loaders/FProcModel.h) `CreateAreaLight` | 大招爆发瞬间临时面光源照亮棋盘 |
| **CVar 调试开关** | [src/Runtime/Config/CVarSystem.hpp](../../../src/Runtime/Config/CVarSystem.hpp) | 暴露 `battle.speedMultiplier` 等调试 CVar |

### 引擎缺失的能力（任务设计避坑）

- **屏幕震动 / PostProcess** — 没有现成 API。震动用相机 `ModelView` 矩阵微抖模拟，闪屏用 ImGui 全屏半透明矩形覆盖
- **粒子系统** — 没有。死亡碎块用 ProcModel 临时 box + 物理刚体方案；技能光环继续用 ImGui drawlist 即可
- **ProcModel cylinder/cone/torus** — 没有。所有装饰几何只能用 box 和 sphere 组合

### 决策

- **范围**：P0 + P1 必做（10 个任务，~7-9h），P2 + P3 看进度选做（9 个任务，~9-11h）
- **顺序**：先做 P0 解决"硬伤"（中文字体、性能 bug、hack 清理），再做 P1 视觉/音效/特效升级，最后做 P2/P3 玩法亮点
- **每个任务 30min-1.5h，独立可执行**；任务间显式标注依赖

## 任务索引

### P0 基础品质（必做，~3h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [Q1](#q1-中文字体加载--清理-utf-8-字节字符串) | 中文字体加载 + 清理 UTF-8 字节字符串 | ~30m | — |
| [Q2](#q2-清理-bootstrapscene-hack--还原战斗节奏) | 清理 BootstrapScene hack + 还原战斗节奏 | ~30m | — |
| [Q3](#q3-markdirty-性能优化) | MarkDirty 性能优化 | ~45m | — |
| [Q4](#q4-接入音频系统--基础-sfx-包) | 接入音频系统 + 基础 SFX 包 | ~1h | — |

### P1 视觉/特效升级（必做，~5h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [Q5](#q5-棋盘装饰升级) | 棋盘装饰升级 | ~1h | — |
| [Q6](#q6-单位职业化外观) | 单位职业化外观 | ~1.5h | Q5 |
| [Q7](#q7-接触阴影--阵营边光) | 接触阴影 + 阵营边光 | ~45m | Q6 |
| [Q8](#q8-投射物-3d-化) | 投射物 3D 化 | ~1h | — |
| [Q9](#q9-攻击命中反馈) | 攻击命中反馈（闪白 + 震屏 + 碎屑） | ~1h | Q8 |

### P2 大招爆发感与玩法体验（选做，~4h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [Q10](#q10-大招爆发感) | 大招爆发感（屏幕变色 + 镜头推 + 大字） | ~1h | — |
| [Q11](#q11-死亡飞溅物理化) | 死亡飞溅物理化 | ~45m | Q9 |
| [Q12](#q12-暂停--加速快捷键) | 暂停 / 加速快捷键 | ~30m | — |
| [Q13](#q13-hover-tooltip--技能描述完整化) | Hover Tooltip + 技能描述完整化 | ~45m | Q1 |
| [Q14](#q14-拖拽体验改善) | 拖拽体验改善 | ~30m | — |

### P3 玩法扩展与亮点（看进度选做，~5h）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [Q15](#q15-羁绊synergies系统) | 羁绊（synergies）系统 | ~1.5h | Q1 |
| [Q16](#q16-pathtracing-切换演示) | PathTracing 切换演示 | ~30m | — |
| [Q17](#q17-部署阶段视觉引导) | 部署阶段视觉引导 | ~30m | Q1 |
| [Q18](#q18-多关卡--难度) | 多关卡 / 难度 | ~2h | Q15 |
| [Q19](#q19-大招动态光照) | 大招动态面光源 | ~30m | Q10 |

---

## Q1. 中文字体加载 + 清理 UTF-8 字节字符串

**优先级**: P0  **工时**: ~30m

### 背景
[KongLie3DUI.cpp](../../../src/Application/KongLie3D/KongLie3DUI.cpp) 中所有中文都用 raw UTF-8 字节序列（`"\xE2\x9C\x93 \xE5\xB7\xB2\xE9\x87\x8A\xE6\x94\xBE"`等等），既难读又因 ImGui 默认字体不含中文 glyph 而显示为方块。`assets/fonts/DroidSansFallback.ttf` 项目已经备好，加载它就能解决全部中文渲染问题。

### TODO
- [ ] 在 `KongLie3DGameInstance` 加 `OnInitUI()` override（参考 [src/Application/gkNextRenderer/gkNextRenderer.cpp:247](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp)）：
  ```cpp
  ImFontConfig cfg;
  cfg.MergeMode = false;
  ImGui::GetIO().Fonts->AddFontFromFileTTF(
      "assets/fonts/DroidSansFallback.ttf", 18.0f, &cfg,
      ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
  ```
- [ ] 把 [KongLie3DUI.cpp](../../../src/Application/KongLie3D/KongLie3DUI.cpp) 里所有 `"\xE2\x9C\x93..."` 这种字节字符串替换为可读的 `u8"✓ 已释放"` 形式（C++20 `u8` 字面量在源文件 UTF-8 编码下显示为可读中文）
- [ ] 同步替换 `KongLie3DGameInstance.cpp` 与 `KongLie3DSkills.cpp` 中所有相关字符串（如果有）
- [ ] 确保源文件保存为 UTF-8 with BOM（VS / clion / vscode 默认）

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`

### 验收方法
1. 编译通过（`./build.bat --preset full-windows`）
2. 启动后所有 UI 文字（"重来一局" / "回主菜单" / "战斗胜利" / "加 时" / 英雄名 "布鲁 Blue" / 技能名 "魔晶护盾"）都正常显示中文
3. 部署/战斗/结算三阶段所有面板都不再有方块
4. 关闭 ImGui MVP 调试窗口仍工作

### 注意
- **不要**用 `ImGui::PushFont`/`PopFont` 包裹每次 Text — 直接把加载的中文字体设为默认即可
- glyph range 用 `GetGlyphRangesChineseSimplifiedCommon()` 即可（覆盖常用 GB 字符 + 拉丁），如果出现少见字（如"裂"）显示为方块再考虑全集
- 如果 ImGui 在引擎层有自己的字体初始化（gkNextRenderer 用了 Roboto），KongLie3D 的字体应在它之后追加 — 需要找对 hook 点（`OnInitUI` vs `OnPreConfigUI`），先试 `OnInitUI`，不行再调

---

## Q2. 清理 BootstrapScene hack + 还原战斗节奏

**优先级**: P0  **工时**: ~30m

### 背景
当前 [KongLie3DGameInstance.cpp:137](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp) 通过 `RequestLoadScene("assets/models/playground.glb")` 先加载一个游乐场场景再被 `BeforeSceneRebuild` 清空，浪费 IO 与显存。同时 [KongLie3DBattleSystem.cpp:22](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp) 的 `AttackCooldownScale = 1.2f` 把所有单位攻击间隔放大 20%，让战斗时长偏离 web 原版的 18-20s 设计。

### TODO
- [ ] 在 `KongLie3DGameInstance::OnInit` 删除 `RequestLoadScene(BootstrapScene)`，改为通过引擎已有的"空场景启动"路径触发 `BeforeSceneRebuild`（如果引擎不支持空启动，最小改动是 `RequestLoadScene("")` 或新建一个空 placeholder scene 文件）
- [ ] 删除 `BootstrapScene` 常量
- [ ] 删除 `KongLie3DBattleSystem.cpp` 的 `AttackCooldownScale` 常量与 `ComputeAttackCooldownMs` 中的乘法。还原为：
  ```cpp
  return baseCooldownMs + TickMs;  // 不再 *1.2
  ```
- [ ] 测试一局，确认平均战斗时长回到 ~18-22s（与 web 版对齐）

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.cpp`、`KongLie3DBattleSystem.cpp`

### 验收方法
1. 编译通过
2. 启动日志不再出现 `Loading scene: assets/models/playground.glb`
3. 一局战斗在 18-25s 内分胜负（与 web 原版数值设计对齐）
4. 圣物未选时双方互打仍能在 30s 前结束

### 注意
- 如果删除 RequestLoadScene 后场景没渲染（黑屏），说明引擎要求至少一次 LoadScene 才会触发 BeforeSceneRebuild — 那就保留调用，但用空字符串或单个 cell 的占位资源
- 关于 `+ TickMs`（100ms 缓冲）：web 版没有这个加项；先保留，看测试是否仍 ~20s。如果偏短才考虑去掉

---

## Q3. MarkDirty 性能优化

**优先级**: P0  **工时**: ~45m

### 背景
`Scene::MarkDirty()` 每次会：1) 重建 GPU NodeMatrix buffer，2) 重建 CPU 加速结构（光追用），3) 禁用渐进式渲染。每帧成本 5-15ms。当前 [KongLie3DBattleSystem.cpp](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp) 的 `UpdateMovementInterpolation` 与 `UpdateDeathAnimations` 每帧都设 `sceneDirty_ = true`，浪费严重。

### TODO
- [ ] 调研 `Node::SetTranslation` 是否会内部触发 transform invalidation（看 [src/Assets/Core/Node.h](../../../src/Assets/Core/Node.h) 与 cpp 实现）。如果会，则**移动插值期间不需要 MarkDirty**
- [ ] 修改 `UpdateMovementInterpolation`：去掉 `sceneDirty_ = true`，仅 `node->SetTranslation`
- [ ] 修改 `UpdateDeathAnimations`：移动期间去掉 `sceneDirty_ = true`，**仅在动画结束时（隐藏 visibility）触发**一次
- [ ] `KillPiece`：保留 `sceneDirty_ = true`（材质从原色切到暗色，需要 GPU 同步）
- [ ] 拖拽 `OnCursorPosition` 中的 `MarkDirty` 改为延迟到 `FinishDraggingPiece`，拖动过程仅用 `SetTranslation`

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DBattleSystem.cpp`、`KongLie3DGameInstance.cpp`

### 验收方法
1. 编译通过
2. 用 F2 打开引擎自带 timing overlay（如可用），战斗中 CPU 帧时间从（猜测）8-12ms 降到 ~2ms 以内
3. 视觉无回归：单位移动平滑、死亡下沉正常、拖拽流畅
4. 重来一局后所有单位回到初始位置（说明 Reset() 仍触发 MarkDirty 一次，是对的）

### 注意
- **保留**这些位置的 MarkDirty：`Reset`（拓扑重置）、`KillPiece`（材质换）、`SnapAllPiecesToTargets`（结算定格）、`TeleportPiece`（瞬移）
- 如果 `SetTranslation` **不**自动 invalidate，则需要找一个轻量"transform-only"接口；引擎可能有 `Node::MarkTransformDirty` 一类的方法
- 不要为了优化把 visible 切换的 MarkDirty 也省掉 — visibility 是 GPU draw call 控制位，必须同步

---

## Q4. 接入音频系统 + 基础 SFX 包

**优先级**: P0  **工时**: ~1h

### 背景
游戏完全静音是体验最大的硬伤。[NextAudio](../../../src/Runtime/Subsystems/NextAudio.h) 已支持 `PlaySound(name, loop, volume)`（基于 miniaudio）。本任务接入，并放置 8 个基础 SFX 占位文件。

### TODO
- [ ] 创建 `assets/sounds/konglie/` 目录
- [ ] 准备 8 个占位音效（可以用 freesound.org / sfxr / 项目已有 sfx 包二次利用）：
  - `ui_click.wav`、`attack_hit_ad.wav`、`attack_hit_ap.wav`
  - `skill_cast.wav`、`unit_die.wav`、`battle_start.wav`
  - `victory.wav`、`defeat.wav`
- [ ] 在 `KongLie3DBattleSystem`：
  - `Attack()` 按 attackType 播 `attack_hit_ad/ap`
  - `KillPiece` 播 `unit_die`
  - `Start` 播 `battle_start`
  - `Tick` 末尾的胜负判定 → 播 `victory` 或 `defeat`
- [ ] 在 `KongLie3DSkills`：W/Ultimate 触发处播 `skill_cast`
- [ ] 在 `KongLie3DUI`：所有 ImGui::Button 按钮点击响应处加 `ui_click`
- [ ] 音量控制：通过 CVar `audio.sfxVolume`（0-1，默认 0.6）
- [ ] **BGM**（可选，本任务不强求）：如果有循环音轨资源，部署阶段播 `bgm_deployment.ogg`，战斗阶段切 `bgm_battle.ogg`

### 涉及文件
- 新建：`assets/sounds/konglie/*.wav`（占位）
- 改：`KongLie3DBattleSystem.cpp`、`KongLie3DSkills.cpp`、`KongLie3DUI.cpp`、`KongLie3DGameInstance.cpp`

### 验收方法
1. 编译通过
2. 战斗中能听到攻击命中的反馈音（AD/AP 不同）
3. 单位死亡有 die 音
4. 释放技能有 cast 音
5. 战斗结束播放胜利/失败音
6. UI 按钮点击有反馈音
7. 音量通过 CVar `audio.sfxVolume` 控制

### 注意
- 占位音效**不**强求高质量，sfxr / chiptune / 干净的 8-bit 都可以；目标是有反馈即可，后续可换正式资源
- 同一音效短时间内大量触发（如 ADC 高攻速）会叠加爆音 — 给每个 sfx key 限制最小间隔（如同名 sfx 间隔 < 50ms 跳过，加 `lastPlayMs` 字典）
- 路径用 `assets/sounds/konglie/xxx.wav` 形式调 NextAudio，确认路径分隔符与引擎其他用法一致
- 如果 `WITH_AUDIO` 编译开关关闭（minimal preset），NextAudio 调用应是空操作，不要让 KongLie3D 在 minimal 下崩溃

---

## Q5. 棋盘装饰升级

**优先级**: P1  **工时**: ~1h

### 背景
[KongLie3DBoard.cpp](../../../src/Application/KongLie3D/KongLie3DBoard.cpp) 当前只有 7×8 个交替灰色 + 中线紫的格子，体感是"一块棋盘"而不是"对战擂台"。

### TODO
- [ ] **棋盘外框**：在棋盘四周加 4 个矮 box（高 0.05，宽 0.2，长度匹配棋盘尺寸），暗灰色金属感（用 `Material::Metallic` 或者较暗 Lambertian）
- [ ] **棋盘底座**：在棋盘下方加一个大 box（覆盖 -1 到 7 的 X，-1 到 9 的 Z，y = -0.5 到 -0.05），深灰色，营造"棋台"质感
- [ ] **中线发光**：把 row=4 的 7 个格子材质改为更亮的紫蓝色 + 自发光（如果材质系统支持 `emissive`，否则纯亮色）
- [ ] **玩家区/敌方区微差**：row 0-3 的格子色调略偏暗紫（敌方），row 4-7 略偏暖灰（玩家）；保留交替深浅但 base hue 不同
- [ ] **板凳席平台**：板凳席（z=8.5，x=0,1,2 那一排）下方加一个独立小 box 平台（与主棋盘视觉区隔）

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DBoard.cpp`

### 验收方法
1. 编译通过
2. 启动后看到带边框的棋盘，下方有底座
3. 中线明显比其他格子亮
4. 玩家区与敌方区色调可分辨
5. 板凳席与主棋盘视觉区隔清晰

### 注意
- 不要引入新材质类型（Lambertian 即可），靠颜色和体量做差异
- emissive 材质如果引擎支持但参数复杂，用纯亮色（rgb 接近 1.0）+ 较高 reflectance 模拟即可

---

## Q6. 单位职业化外观

**优先级**: P1  **工时**: ~1.5h  **依赖**: Q5

### 背景
[KongLie3DGameInstance.cpp:36](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp) 中 `GetPieceDimensions` 仅按 role 给出不同 box 尺寸，但所有单位本质都是单一 box（HERO 加个顶部 sphere）。3D 化的最大优势是用几何区分职业。

### TODO
- [ ] 抽出 `BuildPieceVisual(pieceDef, models, materials, parentNode)` 函数（放 `KongLie3DGameInstance.cpp` 或新文件 `KongLie3DPieceVisual.cpp`）
- [ ] 按 role 添加装饰子节点（每个装饰是一个独立 ProcModel + 子 Node，挂在 piece node 下）：
  - **TANK**（hero_blue / tank_support）：本体 box + 顶部加宽扁 box（"盾"，1.1×0.1×0.1，高度在头顶）
  - **DEF**（shadow_tank_*）：本体 box + 前方加细窄 box（"枪管"，0.15×0.15×0.5）
  - **ADC**（adc_a/b/shadow_adc_*）：本体 box + 前方加长瘦 box（"弓/枪"，0.1×0.15×0.6，水平指向前方）
  - **SUP**（healer_support / shadow_healer）：本体 box + 顶部加垂直 box（"法杖"，0.08×0.5×0.08）+ 顶端加小 sphere（0.1）
  - **FTR/atk_tank**（hero_sydney）：本体 box + 头顶加细 box（"剑"，0.08×0.4×0.08）
- [ ] 装饰部件颜色：用主色稍亮的版本（`color * 1.3` 截断到 1.0）形成层次感
- [ ] HERO 的标志 sphere 保留（已有）

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.cpp`（或新建 `KongLie3DPieceVisual.{hpp,cpp}`）

### 验收方法
1. 编译通过
2. 启动后能一眼区分坦克（盾）、ADC（弓）、SUP（杖）、FTR（剑）、DEF（枪）
3. 玩家方与敌方同职业仍用相同造型，仅靠 base color 区分阵营
4. HERO 仍有顶部光球
5. 装饰部件不影响选中拾取（确保仍能 raycast 命中本体；装饰部件 instanceId 也注册到 lookup 表，触发选中时回到主体）

### 注意
- 所有装饰都做成 piece node 的 child（`heroSphereNode->SetParent(pieceNode)` 模式），这样移动/死亡动画自动跟随
- 装饰 node 的 instanceId 也要写进 `pieceInstanceLookup_`，让 raycast 命中装饰时能反查到所属 piece
- **不要**为每种 role 创建独立 model — 重用相同的 box 模型，只改 transform 与材质 ID

---

## Q7. 接触阴影 + 阵营边光

**优先级**: P1  **工时**: ~45m  **依赖**: Q6

### 背景
单位飘在棋盘上的"悬浮感"在 Q6 之后仍存在。需要在每个单位下方画一个柔和的接触阴影，并给单位边缘加阵营色调，让画面有"游戏感"。

### TODO
- [ ] **接触阴影**：每帧在 `RenderHUD` 里给所有 alive 单位画一个椭圆阴影
  - 用 ImGui foreground draw list `AddCircleFilled` 投影到屏幕（半径 ~ 单位 box 宽度的 0.6 倍）
  - 颜色 `IM_COL32(0, 0, 0, 90)`（半透明黑）
  - 位置 = 单位 World y=0.02 处的投影（贴棋盘）
  - 单位移动时阴影也跟着移动（基于 piece->node->WorldTranslation 投影）
- [ ] **阵营边光**：给每个单位的主材质 ID 之外，再准备一个 `glowMaterialId`（亮一点的同色），死亡时切到 `darkMaterialId` 已有；新增 alive 状态下渲染时，给主体周围画一个 ImGui circle stroke（外圈），玩家=暖橙色 `(255, 180, 100, 60)`，敌方=冷紫色 `(180, 80, 200, 60)`
- [ ] 也可以用 ProcModel 在每个单位脚下做一个稍大的扁平 box（高 0.001，比 piece 大 1.2 倍）作为光晕底盘，材质用 base color 加亮版本

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DUI.cpp`（接触阴影）、`KongLie3DGameInstance.cpp`（阵营底盘 if 选 ProcModel 方案）

### 验收方法
1. 编译通过
2. 每个 alive 单位下方都有柔和椭圆阴影，跟随移动
3. 玩家方单位有暖色光晕，敌方有冷色光晕
4. 死亡单位（visible=false）阴影/光晕也消失
5. 阴影/光晕不遮挡拖拽高亮和 HP 条

### 注意
- 接触阴影用 ImGui drawlist 是最简单的做法，不需要真 shadow map
- 如果用 ProcModel 底盘，记得在 piece 死亡时把底盘也 SetVisible(false)，避免残留

---

## Q8. 投射物 3D 化

**优先级**: P1  **工时**: ~1h

### 背景
当前 ADC 远程攻击只在屏幕上画一条 2D 线（[KongLie3DUI.cpp DrawAttackTraces](../../../src/Application/KongLie3D/KongLie3DUI.cpp:209)），完全没有 3D 飞行轨迹。3D 化的最大优势之一就是让远程子弹真的"飞"起来。

### TODO
- [ ] 在 `KongLie3DBattleSystem.hpp` 加 `struct FProjectile`：
  ```cpp
  struct FProjectile {
      glm::vec3 startPos, endPos;
      float durationMs, elapsedMs;
      glm::vec3 color;
      uint32_t modelId, materialId;
      std::shared_ptr<Assets::Node> node;
      // 命中时要触发的伤害回调
      std::function<void()> onHit;
  };
  ```
- [ ] `BeforeSceneRebuild` 预创建一个 sphere model（半径 0.08）+ N 个材质池（按 attackType 分组：橙色 AD / 蓝色 AP / 绿色 heal）；每个材质对应一个隐藏的 spawned node 池（10-20 个，复用避免运行时创建 node）
- [ ] `Attack()` 中：如果 `attacker.def.range >= 2`（远程），不再 RecordAttackTrace，而是 SpawnProjectile：从空闲池子里取一个 sphere node、SetVisible(true)、设 startPos=attacker pos、endPos=target pos、material=按 attackType 选；伤害延迟到投射物到达时执行
- [ ] `Update` 中遍历活跃投射物，每帧 lerp 位置 SetTranslation；命中（elapsedMs >= durationMs）时调 onHit 并归还 node 到池子（SetVisible(false)）
- [ ] 飞行时长按距离计算：`durationMs = 80 * chebyshev_distance` （约 80ms/格，远距离不超 400ms）
- [ ] 近战（range=1）保留原 RecordAttackTrace（屏幕线）即可，不需要投射物

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DGameInstance.cpp`（BeforeSceneRebuild 加投射物 model + node 池）

### 验收方法
1. 编译通过
2. ADC 攻击远程目标时能看到一个小 sphere（橙色 AD / 蓝色 AP）从 ADC 飞向目标
3. 命中瞬间 sphere 消失，目标掉血
4. 治疗者（SUP）治疗也飞绿色 sphere
5. 近战单位（坦克/Sydney）攻击仍是屏幕线（不变）
6. 投射物频繁飞行不卡顿（说明池子复用工作）

### 注意
- 投射物 node 池**不**新建/销毁 node — 复用 visible 切换。预先在 BeforeSceneRebuild 创建 20 个隐藏 node 即可（同一 sphere model，3 个不同色材质，每色 ~7 个 node）
- 如果池子用尽（极限场景），新攻击降级为屏幕线
- 投射物**不**走物理引擎，纯插值（避免引入复杂度）；命中检测就是 elapsedMs 到点
- 飞行时不应触发 sceneDirty（参考 Q3，纯 SetTranslation）

---

## Q9. 攻击命中反馈（闪白 + 震屏 + 碎屑）

**优先级**: P1  **工时**: ~1h  **依赖**: Q8

### 背景
当前攻击命中没有视觉反馈，掉血只能从 HP 条数字感知。需要"被打"的瞬间有强烈反馈。

### TODO
- [ ] **被击者闪白**：`FPieceRuntime` 加 `float hitFlashMs = 0.0f`；`Attack()` / `ApplyAbilityDamage` 命中时 `target.hitFlashMs = 80.0f`；`Update` 中递减；渲染时如果 `hitFlashMs > 0`，临时把材质 ID 切到一个**全亮白色材质**（在 BeforeSceneRebuild 创建一个共享白色 material），timer 到 0 切回原 materialId
- [ ] **屏幕震动**：`FBattleSystem` 加 `float screenShakeMs = 0.0f`；`Attack` 命中时累加 50ms（限制最大 200ms 避免过度抖动）；`OverrideRenderCamera` 时根据 shakeMs 给相机位置加随机 `vec3(rand-0.5, rand-0.5, rand-0.5) * 0.05 * shakeMs/200`，timer 到 0 不偏移
- [ ] **命中碎屑**：每次命中在被击者位置 spawn 2-3 个小 box（0.04m），从命中点向上+斜向飞 200ms 然后 fade（同样用 node 池复用，预创建 30 个共用 model + 共用材质）
- [ ] 大招命中也走同一逻辑（碎屑/闪白/震屏）

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DPiece.hpp`（hitFlashMs 字段）、`KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DGameInstance.cpp`（白色 material + 碎屑池）

### 验收方法
1. 编译通过
2. 任何攻击命中时被击者瞬间闪白
3. 屏幕在密集战斗中有明显（但不眩晕的）抖动
4. 每次命中冒 2-3 个小白点向上飞散
5. 高频命中（ADC 持续打）不积累过度抖动

### 注意
- 闪白材质共享一个 ID 即可（不需要 per-piece）— 受击瞬间临时切换，timer 到 0 切回各自原材质
- 屏幕震动幅度上限 0.05m，超过会让人晕；累加上限 200ms
- 碎屑用 node 池避免运行时分配；与 Q8 投射物池机制相同

---

## Q10. 大招爆发感（屏幕变色 + 镜头推 + 大字）

**优先级**: P2  **工时**: ~1h

### 背景
当前大招效果只有一个扩张圆环（[KongLie3DUI.cpp DrawSkillEffects](../../../src/Application/KongLie3D/KongLie3DUI.cpp:262)），缺乏"大招爆发"的史诗感。

### TODO
- [ ] **屏幕短暂变色**：大招命中瞬间，全屏覆盖半透明矩形 300ms（蓝色 surge 用蓝、fury 用橙），透明度从 0.4 fade 到 0
- [ ] **镜头推近**：`OverrideRenderCamera` 加一个"大招推近"模式，释放大招瞬间设 `cameraFocusPos = caster.worldPos`，`focusTimerMs = 600`；timer 内相机位置在原位与 `caster + (3, 5, 5)` 之间 lerp（lerp 因子用 sin 曲线先推近后回弹）
- [ ] **大字技能名**：在屏幕中央显示技能名（"蓝色洪流" / "西德之怒"），字号 48-60，颜色对应技能色，淡入 200ms + 持续 400ms + 淡出 400ms = 1s
- [ ] sydney_fury 期间整个棋盘节奏不变（不做慢动作 — 复杂且影响 tick），仅靠相机推近 + 大字突出

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DBattleSystem.{hpp,cpp}`（添加 cameraFocus 状态）、`KongLie3DGameInstance.cpp`（OverrideRenderCamera）、`KongLie3DUI.cpp`（屏幕变色 + 大字）

### 验收方法
1. 编译通过
2. 释放 Blue 大招：蓝色全屏闪一下 + 镜头推近 Blue + "蓝色洪流"大字弹出
3. 释放 Sydney 大招：橙色全屏闪 + 镜头推近 Sydney + "西德之怒"大字弹出
4. 1 秒后镜头自动复位
5. 视觉震撼但不破坏战斗节奏（操作不卡顿）

### 注意
- 大字用 ImGui 默认字体足够（中文字体已在 Q1 加载）
- 镜头推近时 `OverrideRenderCamera` 返回的 ModelView 要平滑切换；用 `glm::mix` lerp 即可
- **不要**做真正的慢动作（影响所有 tick / 物理同步），只是相机+视觉效果

---

## Q11. 死亡飞溅物理化

**优先级**: P2  **工时**: ~45m  **依赖**: Q9

### 背景
当前死亡只是 `node->SetVisible(false)` + 0.3m 下沉，缺乏"被击败"的爆发感。引擎已支持 PhysicsComponent + AddForceToBody，可做"被打飞"碎裂。

### TODO
- [ ] 在 `BeforeSceneRebuild` 中给每个 piece node 挂 PhysicsComponent + Kinematic 刚体（KO 前不受力影响；通常自走棋单位都是 kinematic 跟随逻辑）
- [ ] `KillPiece` 中：
  - 把刚体切到 `Dynamic`
  - 计算冲量方向：从最后一个攻击它的攻击者位置指向 piece（用 `lastAttackerPos` 字段记录，Attack 时更新）
  - `AddForceToBody(bodyId, dir * 50000)` + 一个向上分量
  - 同时 spawn 5-8 个小 box 碎块（用 Q9 的碎屑池但更大），每个施加随机方向冲量
  - 1.5 秒后 SetVisible(false) + Kinematic 复位（Reset 时复用）
- [ ] **如果给所有 piece 加 Physics 性能或开发成本太高，降级方案**：仅在死亡瞬间临时创建一个 Dynamic box 刚体覆盖 piece 位置 + 施力，piece 本体直接 SetVisible(false)；视觉上替身飞，本体消失

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.cpp`（创建物理体）、`KongLie3DBattleSystem.{hpp,cpp}`（KillPiece 升级、lastAttackerPos 记录）

### 验收方法
1. 编译通过
2. 单位死亡时 box 向被击方向被打飞 + 旋转
3. 周围有几个小碎块同时飞溅
4. 1-2 秒后干净消失（不会留尸体）
5. 重来一局后死亡过单位完整复活在原位

### 注意
- 物理体生命周期管理要小心 — 重来时把所有刚体切回 Kinematic 并复位
- 如果引擎物理 step 频率与游戏 tick 不同，可能出现"刚体抖动"，看实际效果决定是否用降级替身方案
- 别让飞溅 box 影响其他活单位的物理（用 collision filter 让碎块只与地面碰撞，不与其他刚体碰撞）

---

## Q12. 暂停 / 加速快捷键

**优先级**: P2  **工时**: ~30m

### 背景
当前只能通过 ImGui 按钮暂停，没有键盘快捷化；也没有战斗倍速（看回放/快速重玩）。

### TODO
- [ ] `OnKey` 中加：
  - **P 键**：战斗中 `battleSystem.TogglePause()`
  - **ESC 键**：拖拽中 → `CancelDraggingPiece()`；结算 modal 中 → `ResetBattle()`
  - **1/2/4 键**：设置 CVar `battle.speedMultiplier` 为 1.0/2.0/4.0
- [ ] `KongLie3DBattleSystem::Update` 顶部读 `battle.speedMultiplier` CVar，把 deltaSeconds 乘以倍率（仅作用于战斗 tick 与浮字/特效，不作用于相机/UI 动画）
- [ ] UI 右上角（`DrawSideControlPanel`）显示当前速度 `"Speed: 1x/2x/4x"`，点击循环切换

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.cpp`（OnKey）、`KongLie3DBattleSystem.cpp`（speedMultiplier）、`KongLie3DUI.cpp`（速度显示）
- 可能改：`src/Runtime/Config/EngineCVars.cpp`（注册 `battle.speedMultiplier` CVar，或者在 `KongLie3DGameInstance::ApplyDefaultCVars` 注册）

### 验收方法
1. 编译通过
2. 战斗中按 P 暂停/继续，按钮文字同步
3. 拖拽中按 ESC 单位回到原位
4. 结算时按 ESC 重来一局
5. 按 2 → 战斗速度 2 倍；按 4 → 4 倍；按 1 → 还原
6. UI 右上角显示当前倍速

### 注意
- 加速倍率上限 4x（再快人眼跟不上技能特效）
- 倍速影响 tick frequency 而不是 tick 内逻辑 — 直接 deltaSeconds 乘倍率即可（accumulator 模式下 100ms tick 在 4x 下一帧多打几次 tick，正确）
- ApplyDefaultCVars 是合适的注册位置（在 NextGameInstanceBase 已 declare）

---

## Q13. Hover Tooltip + 技能描述完整化

**优先级**: P2  **工时**: ~45m  **依赖**: Q1

### 背景
当前看不到单位详情（HP/ATK/range/skill 描述），英雄面板的 W 描述只有名字（"魔晶护盾"），缺少效果说明（"3 秒内吸收 150 伤害"）。

### TODO
- [ ] **pieces.json 加技能描述**：每个 hero 的 `skills.w.desc` 与 `skills.ultimate.desc` 字段。数据从 web 版 `pieceData.js` 完整搬运（如 magic_shield 的 desc："为自身及最近队友施加150护盾，持续3秒"）
- [ ] **DataLoader 读取 desc**：`FPieceDef` 加 `skillWDesc` / `skillUltimateDesc` 字段
- [ ] **英雄面板**：W 显示 "W: {name}\n   {desc}"（两行，desc 用 `TextWrapped`）；R 按钮 hover 时 ImGui 显示 tooltip "{name}: {desc}"
- [ ] **棋盘 hover tooltip**（异步：每 200ms 触发一次 raycast，避免每帧）：
  - 鼠标静止 200ms 时，触发 `RayCastGPU`，命中棋子 → ImGui::BeginTooltip 显示：
    ```
    {name}  [{role}]
    HP {currentHp}/{maxHp}
    ATK {atk}  ATK_SPD {atkSpeed}
    Range {range}
    {team == player ? '我方' : '敌方'}
    ```
  - hover 离开 → 不显示

### 涉及文件
- 改：`assets/configs/konglie/pieces.json`、`src/Application/KongLie3D/KongLie3DDataLoader.{hpp,cpp}`、`KongLie3DUI.cpp`、`KongLie3DGameInstance.{hpp,cpp}`

### 验收方法
1. 编译通过
2. 英雄面板 W 行显示完整描述
3. R 按钮 hover 显示大招描述 tooltip
4. 鼠标在棋子上停留 ~200ms 后弹出 tooltip 显示属性
5. 移走鼠标 tooltip 消失

### 注意
- raycast 200ms 节流避免每帧 GPU raycast 卡顿
- tooltip 不应在拖拽中显示（拖拽时 hidePieceTooltips_ = true）
- 字段缺失时（如非 hero 的 skillW desc）不显示 tooltip 中对应行

---

## Q14. 拖拽体验改善

**优先级**: P2  **工时**: ~30m

### 背景
当前拖拽到非法格无视觉反馈（仅"弹回"），且单位 y 高度不变，缺少"抓起"质感。

### TODO
- [ ] **拖拽中 piece 浮起**：`BeginDraggingPiece` 把 piece y 设为 `targetWorldPos.y + 0.4`；`OnCursorPosition` 维护这个偏移（已有逻辑只更新 x/z，y 保持上浮）
- [ ] **非法目标红色高亮**：`KongLie3DGameInstance::GetValidDragCells` 已返回合法格列表；新增 `GetInvalidDragHover()` 返回当前鼠标悬停格（如果不在合法列表里），UI 用红色 `IM_COL32(255, 64, 64, 80)` 高亮该格
- [ ] **CancelDraggingPiece 时重置 y**：现有逻辑已 `UpdatePieceDeploymentTransform` 重置位置，确保 y 也回到正常高度

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 开始拖拽时 piece 明显浮起 0.4m
3. 拖到合法格 → 该格白色高亮
4. 拖到敌方区/棋盘外 → 鼠标位置那个格子（如果是合法格之外）红色高亮
5. 释放在非法位置 → piece 回到原位（落地）

### 注意
- 浮起高度 0.4 不要太大（>0.6 看起来"飞"）
- 红色高亮不要替换白色，二者并存（合法格白，非法当前悬停红）
- 浮起时 piece 阴影位置仍在 y=0（保持视觉关联）— 与 Q7 阴影协同

---

## Q15. 羁绊（synergies）系统

**优先级**: P3  **工时**: ~1.5h  **依赖**: Q1

### 背景
web 版 [`pieceData.js`](https://github.com/biaowww/webEngine-projectBreach/blob/main/src/data/pieceData.js) 有 `synergies` 字段（如 `['远射', '联盟']`），按上场单位拥有同羁绊数量触发 buff。codex 跳过了这块，少了一个完整玩法系统。

### TODO
- [ ] **pieces.json 加 `synergies` 字段**：从 web 版 pieceData.js 搬运
- [ ] **DataLoader**：`FPieceDef` 加 `std::vector<std::string> synergies`
- [ ] **羁绊定义文件**：新建 `assets/configs/konglie/synergies.json`：
  ```json
  {
    "synergies": [
      { "id": "远射",  "name": "远射", "tiers": [{"count": 2, "atkBonus": 0.10}, {"count": 3, "atkBonus": 0.20}] },
      { "id": "铁壁",  "name": "铁壁", "tiers": [{"count": 2, "hpBonus": 0.10}, {"count": 3, "hpBonus": 0.20}] },
      { "id": "联盟",  "name": "联盟", "tiers": [{"count": 2, "spdBonus": 0.10}] },
      { "id": "魔法",  "name": "魔法", "tiers": [{"count": 2, "apBonus": 0.15}] },
      { "id": "守护",  "name": "守护", "tiers": [{"count": 2, "hpBonus": 0.15}] },
      { "id": "刃铠",  "name": "刃铠", "tiers": [{"count": 2, "atkBonus": 0.15}] },
      { "id": "战士",  "name": "战士", "tiers": [{"count": 2, "atkBonus": 0.10}] },
      { "id": "圣光",  "name": "圣光", "tiers": [{"count": 2, "spdBonus": 0.15}] }
    ]
  }
  ```
- [ ] **DataLoader**：`LoadSynergies(path)` 返回 `vector<FSynergyDef>`
- [ ] **战斗启动时计算羁绊激活**：`Start()` 中遍历玩家方上场单位，统计每个 synergy 的数量，激活满足 count 的最高 tier；与圣物 buff 一同应用到 piece.def
- [ ] **UI 显示羁绊总览**：英雄面板上方加一区，列出当前激活的羁绊：`远射 ×3 (+20% ATK)` `铁壁 ×2 (+10% HP)`，激活的金色加粗、未达条件灰色
- [ ] **战前实时预览**：拖拽阵容时实时重算（不应用，只显示），让玩家看清当前阵型激活了哪些羁绊

### 涉及文件
- 改：`assets/configs/konglie/pieces.json`、`KongLie3DDataLoader.{hpp,cpp}`、`KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DUI.cpp`、`KongLie3DGameInstance.cpp`
- 新建：`assets/configs/konglie/synergies.json`

### 验收方法
1. 编译通过
2. 启动后英雄面板上方看到羁绊预览（默认阵容："远射 ×2 已激活"等）
3. 把一个 ADC 拖到板凳 → 远射变 ×1 灰色
4. 开战后玩家方实际属性按激活 tier 加成（ADC 攻击力提升明显）
5. 重来一局羁绊重新计算

### 注意
- 羁绊 buff 与圣物 buff 都改 `piece.def`，注意叠加顺序：圣物 → 羁绊（基于已应用圣物的值二次乘）。或者两者都基于 `baseDef` 独立计算后求和
- `synergies.json` 数值不必完全照搬 web 原版（web 版可能没明确数值），用合理假设即可
- 羁绊预览 UI 不要太大，建议 100×120 区块塞下 5-8 个羁绊

---

## Q16. PathTracing 切换演示

**优先级**: P3  **工时**: ~30m

### 背景
gkNextRenderer 引擎独特卖点是支持硬件光线追踪（PathTracing），3D 化复刻最大的"超越 web 版"的视觉证据就是让玩家能切到光追看金属盾、玻璃光环的真实反射。

### TODO
- [ ] `OnKey` 中加 **F3 键**：依次切换 CVar `r.rendererType` 0 → 1 → 2 → 3 → 0
  ```cpp
  auto& cvars = NextEngine::GetInstance()->GetCVarSystem();
  int current = std::stoi(cvars.GetValueString("r.rendererType"));
  current = (current + 1) % 4;
  cvars.SetValueFromString("r.rendererType", std::to_string(current), ECVarSetBy::Console);
  ```
- [ ] UI 右下角显示当前 renderer：`"Render: PathTracing"` / `"Render: SoftTracing"` / `"Render: PureAmbient"` / `"Render: VoxelTracing"`，淡显
- [ ] 部署阶段额外提示一行 `"按 F3 切换渲染管线 (体验光追画质)"`

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DGameInstance.cpp`、`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 按 F3 渲染管线立即切换
3. 在 PathTracing 模式下，金属棋盘边框（如果 Q5 用了 Metallic 材质）有真实反射；技能扩张环（如果 Q10 屏幕变色用了透明几何）能看到玻璃感
4. 右下角实时显示当前 renderer 名
5. 战斗中切换不崩溃（场景已构建，仅渲染管线切换）

### 注意
- 切换 renderer 在战斗中可能短暂卡顿（GPU 资源重新分配），但不应崩溃
- 各 renderer 的支持情况依赖 GPU；如果用户硬件不支持光追（无 RT core），PathTracing 模式会回退或 black — 提示用户："此机器不支持 RT，回退到软光追"
- 不要默认使用 PathTracing（启动太重），用 SoftwareModern（rasterizer + 软 GI）作默认

---

## Q17. 部署阶段视觉引导

**优先级**: P3  **工时**: ~30m  **依赖**: Q1

### 背景
新玩家进入游戏不知道怎么开始（要拖拽？要按什么？）。

### TODO
- [ ] 部署阶段在屏幕中央显示一行轻提示：`"拖拽棋子调整阵型，按 SPACE 开始战斗"`
  - 字号 22，半透明白色 `(255, 255, 255, 200)`
  - 持续 5 秒后淡出（避免一直占屏）
  - 任何拖拽事件触发或按 SPACE 后立即隐藏
- [ ] **玩家区淡蓝色提示**：部署阶段给玩家区（row 4-7）每个格子画一个非常淡的蓝色叠加（IM_COL32(80, 140, 220, 30)），让玩家明确"这一片是我的部署区"
- [ ] **敌方区淡红色提示**：row 0-3 同样淡红色叠加
- [ ] 战斗开始后这些提示全部隐藏

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DUI.cpp`、`KongLie3DGameInstance.cpp`

### 验收方法
1. 编译通过
2. 启动后看到中央提示 `"拖拽棋子调整阵型，按 SPACE 开始战斗"`
3. 玩家区每格有非常淡蓝色叠加，敌方区淡红色
4. 拖拽一次或按 SPACE 后中央提示淡出
5. 战斗开始后区域色叠加消失

### 注意
- 区域色叠加要非常淡（alpha < 50），不能干扰棋子辨识
- 中央提示文字用大号字便于阅读，但不能一直占屏（5s 自动消失）

---

## Q18. 多关卡 / 难度

**优先级**: P3  **工时**: ~2h  **依赖**: Q15

### 背景
打完一局没有进度感。引入 3 个难度等级 + 3 个不同敌方阵型，让玩家有"通关"目标。

### TODO
- [ ] **改 placement.json 为 levels 数组**：
  ```json
  {
    "levels": [
      { "id": "easy",   "name": "简单",  "enemyDmgMult": 0.85, "enemy": [...], "bench": [...] },
      { "id": "normal", "name": "普通",  "enemyDmgMult": 1.00, "enemy": [...], "bench": [...] },
      { "id": "hard",   "name": "困难",  "enemyDmgMult": 1.30, "enemy": [...], "bench": [...] }
    ],
    "player": [...]
  }
  ```
- [ ] **DataLoader 加载 levels**
- [ ] **难度选择 UI**：部署阶段右上角加 `"难度: 简单/普通/困难"`（dropdown 或 3 个 button），切换重新触发 BeforeSceneRebuild
- [ ] **通关进度**：把当前难度记到 `KongLie3DGameInstance::currentLevel_`；胜利结算 modal 加 `"下一关"` 按钮（仅在简单/普通赢时显示，自动切到下一难度并 ResetBattle）
- [ ] 保存进度到磁盘（spdlog::info 或简单 txt 文件，最高难度记录）— 可选

### 涉及文件
- 改：`assets/configs/konglie/placement.json`、`KongLie3DDataLoader.{hpp,cpp}`、`KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`、`KongLie3DBattleSystem.{hpp,cpp}`（enemyDmgMult 应用）

### 验收方法
1. 编译通过
2. 部署阶段能选择 3 种难度
3. 困难难度敌方明显更强（伤害 +30%）
4. 简单难度赢一局后结算 modal 出现 "下一关" 按钮
5. 切难度时阵型重置（如果玩家做了拖拽自定义会丢失，可以接受）

### 注意
- 难度切换时如果不重建 scene 而只调 `enemyDmgMult` 系数最简单
- 困难+简单的敌方阵型可以差异化（如困难加一个 shadow_healer），但优先做难度系数即可
- "下一关"按钮在 hard 胜利时不显示（因为已经最高了），改显示 `"通关！"`

---

## Q19. 大招动态面光源

**优先级**: P3  **工时**: ~30m  **依赖**: Q10

### 背景
大招爆发瞬间，整个棋盘应该被照亮。引擎支持运行时添加 AreaLight。

### TODO
- [ ] `CastUltimate` 释放大招瞬间，临时在 caster 头顶 1m 处生成一个 AreaLight：
  ```cpp
  auto light = Assets::FProcModel::CreateAreaLight(
      "ult_light", center, normal, size, intensity * 30.0f);
  scene.Lights().push_back(light);
  scene.MarkDirty();
  ```
- [ ] 颜色：blue_surge 用蓝色，sydney_fury 用橙色
- [ ] 持续 600ms（与 Q10 大字时长匹配）后从 `scene.Lights()` 移除并 MarkDirty
- [ ] 用 `std::vector<FTempLight>` 队列管理生命周期

### 涉及文件
- 改：`src/Application/KongLie3D/KongLie3DSkills.cpp`、`KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DGameInstance.cpp`

### 验收方法
1. 编译通过
2. 释放 Blue 大招：整个棋盘在 600ms 内被蓝色面光源照亮（PathTracing 模式下尤其明显）
3. 释放 Sydney 大招：橙色补光
4. 600ms 后光源消失，恢复正常
5. 多次大招快速释放不会泄漏 Lights

### 注意
- 在非光追模式下（VoxelTracing/PureAmbient），AreaLight 可能不直接体现 — 这是引擎渲染管线决定的，不是任务问题
- AreaLight 频繁 add/remove 可能触发 sceneDirty 重建 — 接受成本（每场只放 2 次大招）
- 颜色不要过亮（intensity * 30 是估值，按视觉调整）

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target KongLie3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀 |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 平台 | 用 `PlatformCommon.h`，不直接 include 平台头 |
| 注释 | 默认不写，仅写非显然的 WHY |
| 提交 | 不要 git commit，由用户决定时机 |

**禁止**：
- 修改 `src/ThirdParty/`、`external/`
- 引入新大型依赖
- 任务卡范围之外的"顺手清理"
- 用 `// removed` / `// TODO` 留半成品

## 与 plan.md 的关系

[plan.md](plan.md) 描述了 M1-M8 的开发，本文件继承相同的命名/构建/数据格式约定，仅追加打磨任务。`plan.md` 不需要更新（它是历史记录）。

## 后续 agent 调用模板

每个 Q1-Q19 任务适合一个独立 agent 执行：

```
请执行 docs/projects/konglie-3d/polish-plan.md 中的 Q{N} 任务。
- 严格按 TODO 清单做，不扩大范围
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报
- 不要 commit，不要做"顺手清理"
- 如果发现依赖任务（注明依赖的 Q{M}）未完成，先汇报让用户决定
```

**推荐执行顺序**：

1. **第一波（基础品质，~3h）**：Q1（中文字体）→ Q2（清理 hack）→ Q3（性能）→ Q4（音效）
2. **第二波（视觉冲击，~5h）**：Q5（棋盘）→ Q6（职业外观）→ Q7（阴影边光）→ Q8（投射物）→ Q9（命中反馈）
3. **第三波（节奏体验，~4h）**：Q10（大招爆发）→ Q11（死亡飞溅）→ Q12（快捷键）→ Q13（tooltip）→ Q14（拖拽改善）
4. **第四波（亮点扩展，~5h，可选）**：Q15（羁绊）→ Q16（光追切换）→ Q17（视觉引导）→ Q18（多关卡）→ Q19（大招光源）

第一波 + 第二波完成即可交付一个明显高于 web 原版的版本。第三波/第四波看进度选做。
