# Brotato 3D — 资产占位打磨计划（Phase 3）

## Context

P1–P10 完成后（P11 可选完成或跳过），游戏在玩法、枪感、UI 框架、工程结构上已"产品化"，但视觉与听觉仍然是 ImGui 几何 + 默认字体 + 缺失的占位音频。**本计划用本地一份 Brotato 原版游戏的解包资产作为快速占位**，让作者本人和小圈子内测者**第一次看到接近真游戏成品的样子**，迅速形成"找到感觉"的反馈循环。

> **本计划的前提**：
> - MVP 计划见 [plan.md](plan.md)
> - 产品化计划见 [product-plan.md](product-plan.md)，假设 P1–P10 全部完成（P11 完成或未完成都不影响本计划）
> - 用户确认拥有 Brotato 正版（路径 `D:/SteamLibrary/steamapps/common/Brotato/Brotato_extracted`）
> - 这些资产**仅作占位**，正式发布前必须全部替换为自制 / CC0 / 授权资产

## ⚠️ 关键合规约束（每个 agent 必读）

**Brotato 是 Blobfish Games 发行的商业游戏，全部资产受版权保护。**

本计划**严格限定**为：
- 个人本地构建上的占位测试，不能进入任何公开发布产物
- **不能**进入 git（必须 `.gitignore`）
- **不能**用于公开 demo / itch.io / Steam / 网页演示 / 截图分享 / 视频录制对外
- **不能**用于商业用途

**强制实施手段**（B1 任务包含）：
- 占位资产统一放在 `assets/_placeholder/brotato/` 子目录（前缀下划线提示忽略）
- `.gitignore` 显式排除该目录
- `assets/_placeholder/README.md` 写明授权状态 + 替换计划（参见 B1）
- 在游戏启动 splash 期间日志打印 `[PLACEHOLDER ASSETS] using vendor reference assets — DO NOT DISTRIBUTE`

如果 agent 觉察到任何会让占位资产泄漏到 git / 安装包 / CI 产物 / 公开二进制的风险，**立即停下来与用户沟通**，不要静默推进。

## 当前状态盘点（Phase 3 起点）

**已完成（P1–P10）**：
- 主菜单 / 选角 / 暂停 / 设置 / 10 波 + Boss / 6 武器 / 6 敌人 / Item / 武器 tier / 暴击 / muzzle flash / trail / 临时点光源 / 屏幕震动分级 / 音频接口（[Brotato3DAudio.hpp](../../../src/Application/Brotato3D/Brotato3DAudio.hpp)）/ 系统拆分（PlayerSystem / EnemySystem / ProjectileSystem / PickupSystem / EffectSystem / CombatSystem）

**仍是占位 / 缺失的视听**（这就是 Phase 3 要解决的）：
1. SFX 路径全部存在但**文件不存在**——目前 `engine_->PlaySound` 静默失败，spdlog 一行警告
2. BGM `bgm_calm.wav / bgm_battle.wav / bgm_boss.wav` 路径声明了但文件没填
3. 字体仅 `Roboto-BoldCondensed.ttf` + 默认字体，**没有中文字形**（中文 UI 全乱码豆腐块）；HUD 大字粗细不够"游戏感"
4. HUD 元素全是 ImGui 几何画的：HP 条、XP 条、敌人血条、武器槽、商店卡、升级卡——产品级游戏看上去都该有美术贴图
5. 主菜单背景半透明黑覆盖 + 标题文本——空洞，没有"卖相"
6. 选角 3 个角色卡只有色块——没有人物立绘
7. 商店开张瞬间没有"开张感"
8. 飘字仍是默认字体——爽快感少 30%
9. 圆形阴影 / 拾取物 / Item 图标——全是文字"V/M/F"等占位字符

## 目标（Phase 3 验收线）

完成 B1–B10 后：
- 所有音效有真实声音（开火 / 命中 / 暴击 / 拾取 / 升级 / 波次 / 商店 / 死亡 / Boss / UI）
- 3 段 BGM 在 wave 1/4/10 切换有"层次推进感"
- HUD 看起来像 2024 商业 roguelite 游戏：HP/XP 条有金属边框，武器槽有类型图标
- 主菜单 splash：标题 logo + 角色立绘 + 雾气视差 + 真按钮风格
- 选角 3 张卡有真人物头像（Brotato 的 brawler / ranger / generalist）
- 商店有背景图 + 物品卡 rarity 边框
- 中文支持：所有 i18n 文本正确显示（NotoSansSC 字体）
- 飘字与大字用 Anybody-Medium，有"街机感"
- 占位资产**完全隔离**：删除 `assets/_placeholder/` 后游戏仍能编译运行（fallback 到 ImGui 几何 / 英文 / 静音）

## 资产清单（Brotato 原版可用资源摘要）

> 完整探查记录见本文档「附录 A」。以下是 Phase 3 直接使用的子集。

### 音频（共 168 wav + 8 mp3）
- **武器开火**：
  - SMG → `weapons/ranged/smg/gun_submachine_auto_shot_01..09.wav`（9 个变体随机播放）
  - Sniper → `weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04.wav`（laser_gun 文件夹里）
  - Rocket → `weapons/ranged/rocket_launcher/gun_silenced_pistol2_shot_01.wav`
  - Flamethrower → `weapons/ranged/flamethrower/gas_med_flame_ignite_01.wav`
  - Laser → `weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04v2.wav`
  - Shotgun → 用 SMG `gun_submachine_auto_shot_*.wav` 中的低频版本（占位，效果可接受）
  - 近战通用 → `weapons/melee_sounds/whoosh_swish_high_big_01..05.wav`
- **命中 / 暴击 / 受伤**：
  - Hit normal → `entities/units/unit/hurt_sounds/punch_general_body_impact_01..05.wav`
  - Hit crit → `entities/units/unit/crit_sounds/bullet_impact_metal_light_01..05.wav`
  - Player hurt → `entities/units/unit/hurt_sounds/bullet_impact_body_flesh_05..08.wav`
- **拾取 / 升级 / 波次**：
  - Pickup XP → `entities/units/player/hp_regen_sounds/Potion_Grab_01.ogg`
  - Pickup Material → `items/consumables/item_box/item_box_pickup.wav`
  - Level up → `resources/sounds/level_up.wav` ✓ 完美
  - Wave start → `ui/sounds/clock_tick_01.wav`（节奏感）
  - Wave end → `ui/sounds/end_wave.wav` ✓ 完美
  - Wave start boss → `entities/units/enemies/boss/zombie_voice_general_emote_05.wav`
- **商店 / UI**：
  - Shop open → `ui/sounds/CGM3_Bubble_Button_01_3.wav`
  - Shop buy → `ui/sounds/buy.wav` ✓
  - Shop reroll → `ui/sounds/diceroll.wav` ✓
  - UI click → `ui/sounds/button_press.wav` ✓
  - UI hover → `ui/sounds/button_focus.wav`
  - Cant buy → `ui/sounds/cant_buy.wav`
- **结算**：
  - Defeat → `entities/units/enemies/pursuer/sci-fi_code_fail_08.wav`
  - Victory → `entities/units/pet/scapegoat/scapegoat_rising.wav`（占位，整体感能接受）
  - Enemy die small → `entities/units/unit/hurt_sounds/bullet_impact_water_01.wav`
  - Enemy die boss → `entities/units/enemies/boss/zombie_voice_general_emote_05.wav`
- **BGM（6 段，Artlist 授权曲目）**：
  - Calm → `resources/music/wasteland-survivors by evgeny-bardyuzha Artlist.mp3`
  - Battle → `resources/music/power-punch by 2050 Artlist.mp3`
  - Boss → `resources/music/extreme-chaos by 2050 Artlist.mp3`
  - 备选 → `after-midnight`、`density-wave`、`time-jumper`

### 字体
- `resources/fonts/raw/Anybody-Medium.ttf` — 拉丁大字 / banner 用
- `resources/fonts/raw/NotoSansSC-Medium.otf` — 中文 UI 主字体
- `resources/fonts/raw/NotoSansJP-Medium.otf` / `NotoSansKR-Medium.otf` / `NotoSansTC-Medium.otf` — 留给后续多语言

### UI 图像（PNG，1297 张共，挑出关键的 ~30 张）
- HP 条：`ui/hud/ui_lifebar_bg.png` / `ui_lifebar_fill.png` / `ui_lifebar_frame.png`
- XP 条：`ui/hud/ui_xp_bg.png` / `ui_xp_fill.png`
- 进度条：`ui/hud/ui_progress_progress.png` / `ui_progress_under.png`（用于 wave 倒计时）
- 面板：`ui/hud/ui_panel_normal.png` / `ui_panel_flat.png` / `ui_panel_transparent.png`
- 道具栏抓取：`ui/hud/ui_grabber_normal.png` / `ui_grabber_focus.png`
- 主菜单：`ui/menus/title_screen/title_screen_background/splash_art_brotato.png`、`splash_art_mist_back.png`、`splash_art_mist_mid.png`、`splash_art_mist_front.png`、`splash_art_bg.png`、`ui_logo.png`
- 商店：`ui/menus/shop/shop_background.png`
- 角色头像：`items/characters/{generalist,brawler,ranger}/{id}_icon.png`
- 武器图标：`weapons/ranged/{smg,sniper_gun,rocket_launcher,laser_gun,flamethrower}/{id}_icon.png`、`weapons/ranged/double_barrel_shotgun/double_barrel_shotgun_icon.png`（充当 shotgun）
- 敌人图标（用于 wave intro / Boss 警告）：`entities/units/enemies/{chaser,bruiser,spitter,charger,healer,boss}/...icon.png`
- Stat 图标：`items/stats/max_hp.png`、`speed.png`、`attack_speed.png`、`crit_chance.png`、`range.png`、`armor.png`、`lifesteal.png` 等
- Item 图标（rarity）：`items/all/<item_id>/<item_id>_icon.png` —— 选 6 个映射到 Phase 2 P6 设计的 `vampire_fang / regen_charm / magnet / fury_core / shrapnel / speed_boots`：
  - vampire_fang → `items/all/blood_leech/blood_leech_icon.png`
  - regen_charm → `items/all/celery_tea/celery_tea_icon.png`（或其它 regen 类）
  - magnet → 选一个磁吸或拾取相关 item
  - fury_core → `items/all/adrenaline/adrenaline_icon.png`
  - shrapnel → `items/all/explosive_shells/explosive_shells_icon.png`
  - speed_boots → `items/all/big_arms/big_arms_icon.png`（Brotato 没有"靴子"原 item，挑形状接近的）

### 引擎复用能力
| 需求 | 复用 | 路径 |
|---|---|---|
| 加载贴图给 ImGui | `engine->GetUserInterface()->RequestImTextureByName(filename)` | 参考 [MagicaLegoUserInterface.cpp:1055](../../../src/Application/MagicaLego/MagicaLegoUserInterface.cpp) |
| 加载字体 | `ImGui::AddFontFromFileTTF` + `GlyphRangesBuilder` | 参考 [gkNextRenderer.cpp:251](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) |
| 音频引擎 | `engine->PlaySound(path, loop, volume)`（miniaudio backend，**支持 wav/mp3/ogg/flac**）| [NextAudio.cpp](../../../src/Runtime/Subsystems/NextAudio.cpp) |
| 音频节流 | `Brotato3DAudio::PlayBrotatoSfx(soundPath, volumeScale, minIntervalMs)` | [Brotato3DAudio.hpp](../../../src/Application/Brotato3D/Brotato3DAudio.hpp) |

## 任务索引（10 个核心，~12–14h）

| # | 阶段 | 标题 | 工时 | 依赖 |
|---|---|---|---|---|
| [B1](#b1-资产导入脚本--gitignore--license-护栏) | I 基建 | 资产导入脚本 + .gitignore + license 护栏 | ~1h | — |
| [B2](#b2-sfx-接入随机变体--路径映射) | I 基建 | SFX 接入（随机变体 + 路径映射） | ~1.5h | B1 |
| [B3](#b3-bgm-接入mp3--3-段切换) | I 基建 | BGM 接入（mp3 + 3 段切换） | ~0.5h | B1 |
| [B4](#b4-字体接入anybody--notosanssc) | I 基建 | 字体接入（Anybody + NotoSansSC） | ~1h | B1 |
| [B5](#b5-hud-贴图替换hp--xp--倒计时--武器槽) | II 视觉 | HUD 贴图替换（HP / XP / 倒计时 / 武器槽） | ~1.5h | B1, B4 |
| [B6](#b6-主菜单-splash-美化logo--雾气视差) | II 视觉 | 主菜单 splash 美化（logo + 雾气视差） | ~1.5h | B1 |
| [B7](#b7-选角-ui-人物立绘) | II 视觉 | 选角 UI 人物立绘 | ~1h | B1, B6 |
| [B8](#b8-敌人-icon--wave-introboss-warning) | II 视觉 | 敌人 icon → wave intro / Boss warning | ~1h | B1, B5 |
| [B9](#b9-商店-ui-风格化背景--rarity--icon) | II 视觉 | 商店 UI 风格化（背景 + rarity + icon） | ~1.5h | B1, B5 |
| [B10](#b10-arena-主题底图--zone-绿野荒地数码) | II 视觉 | Arena 主题底图（zone 绿野/荒地/数码） | ~1h | B1 |

> **执行节奏**：B1 是必前置；B2/B3/B4 可并行；B5–B10 大量并行（不同 agent 同会话各做 1–2 个）。每个 B 任务完成后游戏仍可启动到主循环，资产缺失走 fallback。

---

## B1. 资产导入脚本 + .gitignore + license 护栏

**优先级**: P0  **工时**: ~1h

### 背景

把 Brotato 解包资产从 `D:/SteamLibrary/steamapps/common/Brotato/Brotato_extracted` 拷贝到项目子目录，并锁死它**绝对不能**进 git / 不能进打包后的 binary。一个简单的 import 脚本 + gitignore + 启动期警告日志。

### TODO

**目录结构**：
- [ ] 新建目录 `assets/_placeholder/brotato/`（前缀 `_` 是为了 lexicographic 排序时排在最后 + 视觉上提示忽略）
  - 子目录：`audio/sfx/`、`audio/bgm/`、`audio/voice/`、`fonts/`、`ui/hud/`、`ui/menu/`、`ui/icons/weapons/`、`ui/icons/characters/`、`ui/icons/enemies/`、`ui/icons/items/`、`ui/icons/stats/`、`ui/arena/`

**.gitignore**：
- [ ] 在仓库根 `.gitignore` 末尾添加：
  ```
  # Phase 3 占位资产 — 受版权保护，不得入库
  assets/_placeholder/
  ```
- [ ] 验证 `git status` 后该目录不出现在 untracked

**导入脚本** `scripts/import_brotato_placeholder.py`（Python 跨平台）：
- [ ] 接受参数 `--source <brotato解包根>`（默认 `D:/SteamLibrary/steamapps/common/Brotato/Brotato_extracted`）
- [ ] 脚本顶部打印 license 警告：
  ```
  ⚠️  WARNING: Importing PROPRIETARY Brotato assets as placeholders.
      These files MUST NOT be committed, distributed, or used publicly.
      Source: <path>  Target: assets/_placeholder/brotato/
  ```
- [ ] 检测 `.gitignore` 是否包含 `assets/_placeholder/`，否则**报错退出**
- [ ] 复制清单（key=目标路径相对 `assets/_placeholder/brotato/`，value=源路径相对 brotato 根）：
  ```python
  copy_list = {
      # SFX
      "audio/sfx/fire_smg_01.wav": "weapons/ranged/smg/gun_submachine_auto_shot_01.wav",
      "audio/sfx/fire_smg_02.wav": "weapons/ranged/smg/gun_submachine_auto_shot_02.wav",
      # ... (full list in 附录 B)
      # BGM
      "audio/bgm/bgm_calm.mp3": "resources/music/wasteland-survivors by evgeny-bardyuzha Artlist.mp3",
      # Fonts
      "fonts/Anybody-Medium.ttf": "resources/fonts/raw/Anybody-Medium.ttf",
      "fonts/NotoSansSC-Medium.otf": "resources/fonts/raw/NotoSansSC-Medium.otf",
      # ... etc
  }
  ```
- [ ] 复制时校验源文件存在；不存在的项 spdlog `WARN` 但**不**中断（允许部分缺失）
- [ ] 完成后输出：成功 / 失败计数 + 总占用字节数（提示用户该目录不要 commit）

**README** `assets/_placeholder/README.md`：
- [ ] 标题：`# 占位资产说明（请勿入库）`
- [ ] 四段：
  1. 来源：本地 Brotato 正版游戏解包
  2. 状态：受 Blobfish Games 版权保护，仅供个人本地构建
  3. 替换计划：在公开发布前，全部替换为自制 / CC0 / 已授权资产
  4. 不可入库：`.gitignore` 已配置，CI / 打包脚本须排除该目录

**启动期警告**：
- [ ] 在 `Brotato3DGameInstance::OnInit` 末尾加：
  ```cpp
  if (std::filesystem::exists("assets/_placeholder/brotato/audio/sfx/fire_smg_01.wav"))
  {
      spdlog::warn("[PLACEHOLDER ASSETS] Brotato vendor reference assets detected — DO NOT DISTRIBUTE");
  }
  ```
- [ ] 在主菜单 UI 上同时显示一个**永久可见**的小角标 `"⚠ PLACEHOLDER ASSETS"`（右上角，半透明红色，10px 字号），让作者随时记得这是占位状态

**Asset path resolver**（让代码统一从 `assets/_placeholder/brotato/...` 取，而非把这个路径硬编码 N 处）：
- [ ] 新增 `src/Application/Brotato3D/Brotato3DAssetPaths.hpp`（header-only），暴露：
  ```cpp
  namespace Brotato3D::Assets
  {
      inline constexpr const char* kPlaceholderRoot = "assets/_placeholder/brotato/";

      inline std::string Sfx(const std::string& relPath)   { return std::string(kPlaceholderRoot) + "audio/sfx/" + relPath; }
      inline std::string Bgm(const std::string& relPath)   { return std::string(kPlaceholderRoot) + "audio/bgm/" + relPath; }
      inline std::string Font(const std::string& relPath)  { return std::string(kPlaceholderRoot) + "fonts/" + relPath; }
      inline std::string Hud(const std::string& relPath)   { return std::string(kPlaceholderRoot) + "ui/hud/" + relPath; }
      inline std::string Icon(const std::string& cat, const std::string& id)
      {
          return std::string(kPlaceholderRoot) + "ui/icons/" + cat + "/" + id + ".png";
      }
      // 占位资产存在性检测，UI/Audio 调用方据此走 fallback
      inline bool Exists(const std::string& fullPath) { return std::filesystem::exists(fullPath); }
  }
  ```

### 涉及文件
- 新建：`scripts/import_brotato_placeholder.py`、`assets/_placeholder/README.md`、`src/Application/Brotato3D/Brotato3DAssetPaths.hpp`
- 改：`.gitignore`、`src/Application/Brotato3D/Brotato3DGameInstance.cpp`（启动 warn）、`src/Application/Brotato3D/Brotato3DUI.cpp`（角标）

### 验收方法
1. 在干净环境下 `python3 scripts/import_brotato_placeholder.py --source D:/SteamLibrary/steamapps/common/Brotato/Brotato_extracted` 成功，目标目录有 ~150+ 文件
2. `git status` **不**显示 `assets/_placeholder/` 任何子文件
3. 启动游戏 spdlog 立即出现 `[PLACEHOLDER ASSETS]` 警告
4. 主菜单右上角看到红色 `⚠ PLACEHOLDER ASSETS` 角标
5. 删除整个 `assets/_placeholder/` 后游戏**仍可正常启动到主循环**（fallback：所有 sfx 静默 + UI 走 ImGui 几何）
6. 重新跑导入脚本恢复

### 注意
- 脚本使用 `shutil.copy2` 保留 mtime，便于增量重导
- 路径中含空格的源文件（如 `wasteland-survivors by evgeny-bardyuzha Artlist.mp3`）必须在 copy_list value 用原始字符串传入；目标改成简短无空格名（如 `bgm_calm.mp3`）
- **不要**用 git submodule 或 LFS 引入 — 这些资产不允许进任何远端
- 不要把 `.gitignore` 添加到 _placeholder 子目录里面（让顶层 ignore 规则统一管理）
- macOS / Linux agent 跑这个脚本时若 `--source` 不指向真 Brotato 根，应该 graceful 退出并提示，而不是 silent fail

---

## B2. SFX 接入（随机变体 + 路径映射）

**优先级**: P0  **工时**: ~1.5h  **依赖**: B1

### 背景

[Brotato3DAudio.hpp](../../../src/Application/Brotato3D/Brotato3DAudio.hpp) 已有 `PlayWeaponFireSfx / PlayHitSfx / ...` 接口骨架，但路径全部指向 `assets/sounds/brotato3d/fire_smg.wav` 这种**不存在的占位路径**。本任务把它们重定向到 `assets/_placeholder/brotato/audio/sfx/...`，并给多变体武器（SMG 9 个变体）增加**随机变体**支持。

### TODO

**SFX 命名重映射**（导入到 `assets/_placeholder/brotato/audio/sfx/` 后的目标文件名）：
- 武器（每把武器多变体则编号）：
  - `fire_smg_01..09.wav`（来自 9 个 `gun_submachine_auto_shot_01..09.wav`）
  - `fire_shotgun_01.wav`（来自 SMG `gun_submachine_auto_shot_07.wav` — 偏低频）
  - `fire_sniper_01.wav`（来自 `weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04.wav`）
  - `fire_rocket_01.wav`（来自 `weapons/ranged/rocket_launcher/gun_silenced_pistol2_shot_01.wav`）
  - `fire_laser_01.wav`（来自 `gun_silenced_sniper1_shot_04v2.wav`）
  - `fire_flamethrower_01..04.wav`（4 个 ignite 变体）
- 命中：
  - `hit_normal_01..05.wav`（来自 `punch_general_body_impact_01..05.wav`）
  - `hit_crit_01..04.wav`（来自 `bullet_impact_metal_light_01..03,05.wav`）
- 玩家受伤：`player_hurt_01..04.wav`（4 个 `bullet_impact_body_flesh`）
- 敌人死亡：
  - `enemy_die_small_01..02.wav`（`bullet_impact_water_01..02.wav`）
  - `enemy_die_tank_01..05.wav`（`punch_general_body_impact_06..08.wav` + 任意 hurt 变体）
  - `enemy_die_boss.wav`（`zombie_voice_general_emote_05.wav`）
- 拾取：
  - `pickup_xp_01..02.ogg`（来自 `Potion_Grab_01..02.ogg`）
  - `pickup_material.wav`（`item_box_pickup.wav`）
- 升级 / 波次：
  - `level_up.wav`（来自 `resources/sounds/level_up.wav` ✓）
  - `wave_start.wav`（`ui/sounds/clock_tick_01.wav`）
  - `wave_end.wav`（`ui/sounds/end_wave.wav` ✓）
  - `wave_start_boss.wav`（`zombie_voice_general_emote_05.wav`）
- UI：
  - `ui_click.wav`（`ui/sounds/button_press.wav`）
  - `ui_hover.wav`（`ui/sounds/button_focus.wav`）
  - `shop_open.wav`（`ui/sounds/CGM3_Bubble_Button_01_3.wav`）
  - `shop_buy.wav`（`ui/sounds/buy.wav` ✓）
  - `shop_reroll.wav`（`ui/sounds/diceroll.wav` ✓）
  - `shop_cant_buy.wav`（`ui/sounds/cant_buy.wav` ✓）
- 结算：
  - `victory.wav`（`scapegoat_rising.wav`）
  - `defeat.wav`（`sci-fi_code_fail_08.wav`）

→ 这些命名在 B1 导入脚本的 copy_list 中实现（路径映射的 single source of truth）。

**修改 [Brotato3DAudio.hpp](../../../src/Application/Brotato3D/Brotato3DAudio.hpp)**：
- [ ] 删除原硬编码路径 `"assets/sounds/brotato3d/..."`，改用 `Assets::Sfx("...")` helper
- [ ] 新增**随机变体辅助函数**：
  ```cpp
  inline std::mt19937& SfxRng()
  {
      static std::mt19937 rng{ std::random_device{}() };
      return rng;
  }

  // 在 [1, maxIndex] 中随机选一个变体编号，返回路径
  inline std::string PickVariant(const std::string& prefix, int maxIndex, const char* ext = "wav")
  {
      std::uniform_int_distribution<int> dist(1, maxIndex);
      const int n = dist(SfxRng());
      return Assets::Sfx(fmt::format("{}_{:02d}.{}", prefix, n, ext));
  }
  ```
- [ ] 改写带变体的接口：
  ```cpp
  inline void PlayWeaponFireSfx(const std::string& weaponId)
  {
      static const std::unordered_map<std::string, std::pair<std::string, int>> map = {
          {"smg",          {"fire_smg", 9}},
          {"shotgun",      {"fire_shotgun", 1}},
          {"sniper",       {"fire_sniper", 1}},
          {"flamethrower", {"fire_flamethrower", 4}},
          {"rocket",       {"fire_rocket", 1}},
          {"laser",        {"fire_laser", 1}},
      };
      auto it = map.find(weaponId);
      if (it == map.end()) { it = map.find("smg"); }
      const auto& [prefix, count] = it->second;
      PlayBrotatoSfx(PickVariant(prefix, count), 0.75f, 70);
  }

  inline void PlayHitSfx(int damage, bool isCrit)
  {
      const std::string path = isCrit ? PickVariant("hit_crit", 4) : PickVariant("hit_normal", 5);
      PlayBrotatoSfx(path, isCrit ? 0.85f : std::clamp(damage / 20.0f, 0.45f, 0.8f),
                     isCrit ? 45 : 35);
  }

  inline void PlayPlayerHurtSfx() { PlayBrotatoSfx(PickVariant("player_hurt", 4), 0.8f, 120); }
  inline void PlayPickupXpSfx()   { PlayBrotatoSfx(PickVariant("pickup_xp", 2, "ogg"), 0.6f, 45); }
  // ... 其余照样
  ```
- [ ] **不**改公共 API 签名（避免触动 GameInstance 调用点），只改实现

**敌人死亡分派优化**：
- [ ] `PlayEnemyDeathSfx(enemyId)` 加入新增敌人映射：
  ```cpp
  if (id == "boss_warden") play("enemy_die_boss");
  else if (id == "tank" || id == "Brute" || id == "bruiser") play(variant("enemy_die_tank", 5));
  else play(variant("enemy_die_small", 2));
  ```

**Crit 视觉与音效配合**（需要小改 Combat 系统）：
- [ ] `Brotato3DCombatSystem` 在 ApplyDamage 时已经识别 crit，确认走 `PlayHitSfx(dmg, isCrit=true)`
- [ ] crit hit sfx 与 normal hit sfx 错开 ≥45ms（minIntervalMs 已设）

### 涉及文件
- 改：`src/Application/Brotato3D/Brotato3DAudio.hpp`（路径 helper + 变体随机）
- 改：`scripts/import_brotato_placeholder.py`（B1 的 copy_list 末尾追加 SFX 全集）
- 不动：调用点（GameInstance / CombatSystem 等不变）

### 验收方法
1. 导入脚本跑一次，`assets/_placeholder/brotato/audio/sfx/` 出现 ~50 个 wav/ogg
2. 编译通过
3. 装备 SMG 持续开火，能听到 9 个变体随机轮播（不重复同一个）
4. 暴击命中音和普通命中音明显不同（金属 vs. 肉感）
5. 玩家受伤声音不会因为连续命中刷屏（120ms 节流）
6. Boss 死亡有低吼声，普通敌人死亡是水溅声
7. 删除 `assets/_placeholder/` 整个目录 → 游戏不崩溃，spdlog 警告但所有玩法正常

### 注意
- 变体随机用 `SfxRng()` 同一份引擎，**不要**每次开火都 `std::random_device{}()`
- 不要给 `PlayBrotatoSfx` 加路径前缀逻辑 — 接受完整路径由 `Assets::Sfx()` 拼接
- ogg 在 miniaudio 默认编译时支持但需要确认；若有问题改用 mp3/wav 重导（用 ffmpeg 在 import 脚本里转换）
- Shotgun 仅 1 个变体可接受，**不**为了变体数对称去复制相同 wav 多份
- minIntervalMs 不要设太短 — flamethrower 12Hz 下若 70ms 节流仍嫌快可调到 90ms

---

## B3. BGM 接入（mp3 + 3 段切换）

**优先级**: P0  **工时**: ~0.5h  **依赖**: B1

### 背景

P5 的 `StartBgm("calm" / "battle" / "boss")` 接口已经存在，路径目前写的是 `assets/sounds/brotato3d/bgm_{}.wav`。改用 mp3 + 占位路径，引擎本身支持 mp3（miniaudio）。

### TODO

- [ ] 在 B1 导入脚本 copy_list 加：
  ```python
  "audio/bgm/bgm_calm.mp3":   "resources/music/wasteland-survivors by evgeny-bardyuzha Artlist.mp3",
  "audio/bgm/bgm_battle.mp3": "resources/music/power-punch by 2050 Artlist.mp3",
  "audio/bgm/bgm_boss.mp3":   "resources/music/extreme-chaos by 2050 Artlist.mp3",
  ```
- [ ] 修改 [Brotato3DAudio.hpp](../../../src/Application/Brotato3D/Brotato3DAudio.hpp) 的 `StartBgm`：
  ```cpp
  inline void StartBgm(const std::string& trackName)
  {
      const std::string path = Assets::Bgm(fmt::format("bgm_{}.mp3", trackName));
      // ... 其余逻辑不变
  }
  ```
- [ ] 检查 [Brotato3DGameInstance.cpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.cpp) 内 BGM 切换时机：
  - wave 1 启动 → `StartBgm("calm")`
  - wave 4 开始 → `StartBgm("battle")`
  - wave 10 开始 → `StartBgm("boss")`
  - victory / defeat → `StopBgm()`
- [ ] 主菜单进入 → `StartBgm("calm")`（让玩家在主菜单就有音乐 → 提升整体游戏感）
  - 注意：此前 BGM 只在 `Playing` 状态启动，本任务把启动时机提前到 `MainMenu` 进入
- [ ] 暂停时（`Paused` 状态）：BGM **降低音量**到 30%，恢复时回到 100%（用 `engine_->SetSoundVolume(currentBgmPath, 0.3f * MusicVolume)`，若该 API 不存在则 spdlog 一行注释跳过 — **不**强制实现，留给 MVP+2）

### 涉及文件
- 改：`src/Application/Brotato3D/Brotato3DAudio.hpp`、`src/Application/Brotato3D/Brotato3DGameInstance.cpp`（BGM 启动时机迁到 MainMenu）
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. 启动游戏 → 进主菜单 → 听到 `wasteland-survivors`（calm BGM）
2. 选角进战斗 wave 1 → 仍是 calm
3. wave 4 开始瞬间 → 切到 `power-punch`（battle BGM），明显节奏更紧
4. wave 10 开始 → 切到 `extreme-chaos`（boss BGM）
5. 胜利 / 失败结算 → BGM 停
6. 设置面板调 MusicVolume → 即时生效

### 注意
- mp3 在 Windows / Linux / macOS 下 miniaudio 都默认支持 — 但 vcpkg 编译选项必须开启 mp3 支持（默认已开，参见 `assets/sfx/bgm.mp3` 已经在用）
- 暂停时降音量是体验加分项，不强求；若 `SetSoundVolume` API 缺失，留 TODO 注释即可
- 不要做 BGM 交叉淡入（API 不支持，硬切可接受）

---

## B4. 字体接入（Anybody + NotoSansSC）

**优先级**: P0  **工时**: ~1h  **依赖**: B1

### 背景

P9 的 `bigFont_` 当前用 `Roboto-BoldCondensed.ttf`，效果不够"街机感"。Brotato 的 `Anybody-Medium.ttf` 是带工业风格的显示字体，更适合 wave banner / 飘字 / 暴击数字。同时引入 `NotoSansSC-Medium.otf` 让 P11（i18n）的中文 UI 真正能显示。

### TODO

- [ ] 在 B1 导入脚本 copy_list 加：
  ```python
  "fonts/Anybody-Medium.ttf":      "resources/fonts/raw/Anybody-Medium.ttf",
  "fonts/NotoSansSC-Medium.otf":   "resources/fonts/raw/NotoSansSC-Medium.otf",
  ```
- [ ] 在 [Brotato3DGameInstance.cpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.cpp) 或 `Brotato3DUI.cpp` 的 ImGui 字体初始化处（参考 [gkNextRenderer.cpp:251](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp)）：
  ```cpp
  ImGuiIO& io = ImGui::GetIO();

  // 默认字体（含中文）
  if (auto p = Assets::Font("NotoSansSC-Medium.otf"); std::filesystem::exists(p))
  {
      static const ImWchar ranges[] = {
          0x0020, 0x00FF,   // basic latin + latin-1
          0x2000, 0x206F,   // 标点
          0x3000, 0x30FF,   // 中日韩标点 + 平假名
          0x4E00, 0x9FFF,   // CJK 统一表意
          0xFF00, 0xFFEF,   // 全角
          0,
      };
      io.Fonts->AddFontFromFileTTF(p.c_str(), 18.0f, nullptr, ranges);
  }

  // 大字字体（仅拉丁，工业风）
  if (auto p = Assets::Font("Anybody-Medium.ttf"); std::filesystem::exists(p))
  {
      bigFont_ = io.Fonts->AddFontFromFileTTF(p.c_str(), 48.0f);
      // 中字（飘字 / 暴击 / shop title）
      midFont_ = io.Fonts->AddFontFromFileTTF(p.c_str(), 28.0f);
  }
  ```
- [ ] `Brotato3DUI` 持有 `ImFont* bigFont_ = nullptr; ImFont* midFont_ = nullptr;`，缺失字体时仍指向 `nullptr`，渲染时检查并 fallback
- [ ] 字体应用点：
  - Wave banner（`"WAVE 6 / 10"` / `"BOSS 来袭"`）→ bigFont_
  - 暴击飘字 → midFont_（金黄色）
  - 普通飘字 → 默认字体（已 NotoSansSC）
  - 主菜单标题 `"BROTATO 3D"` → bigFont_ 放大 1.5×
  - 升级 modal 标题 → midFont_
  - 结算 `"VICTORY"` / `"DEFEATED"` → bigFont_ 放大 1.8×
- [ ] 中文渲染验证：`upgrades.json` 的 `"+8% 暴击率"` / 主菜单 `"开始游戏"` / 商店 `"重抽"` 等应该正确渲染（目前是豆腐块）

### 涉及文件
- 改：字体加载点（`Brotato3DGameInstance.cpp` 或 `Brotato3DUI.cpp`，看 P9 实际放在哪）
- 改：`Brotato3DUI.cpp`（多个 banner / 飘字 / modal 标题切到 bigFont/midFont）
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. 启动游戏看到主菜单标题 "BROTATO 3D" 用 Anybody 字体（粗体方块感）
2. 升级 modal 中文不再显示豆腐块
3. wave 6 banner "WAVE 6 / 10" 用 Anybody 大字
4. 暴击飘字字号大且金黄
5. 删除 `assets/_placeholder/` 后 fallback 到 Roboto，UI 仍正常但没有 Anybody / 中文豆腐块（acceptable degraded mode）

### 注意
- ImGui 字体加载只能在 backend 重建前调用 — 找正确的 hook 点（通常在 `OnInitUI` 或 `OnRenderUI` 第一帧守门）
- `AddFontFromFileTTF` 失败返回 nullptr 不要崩 — 用 `if (font)` 守门
- 中文 ranges 不要给 `GetGlyphRangesChineseFull()`（包 ~21000 字符，atlas 巨大）— 用上面定义的 5 个块够用
- atlas 大小可能超 1024 → ImGui 会自动扩到 2048/4096，编译时若有 `IM_ASSERT(...)` 命中先调小字号到 16/24/40

---

## B5. HUD 贴图替换（HP / XP / 倒计时 / 武器槽）

**优先级**: P0  **工时**: ~1.5h  **依赖**: B1, B4

### 背景

把 HUD 从纯 ImGui 几何升级为带贴图的"游戏感 HUD"。重点：HP 条、XP 条、wave 倒计时、武器槽 6 格。

### TODO

**贴图导入**（B1 copy_list 追加）：
- `ui/hud/lifebar_bg.png`、`lifebar_fill.png`、`lifebar_frame.png`
- `ui/hud/xp_bg.png`、`xp_fill.png`
- `ui/hud/progress_under.png`、`progress_progress.png`（倒计时）
- `ui/hud/panel_normal.png`、`panel_flat.png`、`panel_transparent.png`
- `ui/icons/weapons/{smg,shotgun,sniper,rocket,flamethrower,laser}.png`
- `ui/icons/stats/{max_hp,speed,attack_speed,crit_chance,range,armor,lifesteal,...}.png`

**贴图加载 helper**（避免每帧 `RequestImTextureByName` 调用开销）：
- [ ] 新增 `Brotato3DUI` 成员：
  ```cpp
  struct FUITextures {
      ImTextureID lifebarBg = 0, lifebarFill = 0, lifebarFrame = 0;
      ImTextureID xpBg = 0, xpFill = 0;
      ImTextureID progressUnder = 0, progressFill = 0;
      ImTextureID panelNormal = 0, panelFlat = 0, panelTransparent = 0;
      std::unordered_map<std::string, ImTextureID> weaponIcons;
      std::unordered_map<std::string, ImTextureID> statIcons;
  };
  FUITextures tex_;
  bool texLoaded_ = false;
  ```
- [ ] `LoadHudTextures()`（首次调用时初始化）：
  ```cpp
  if (texLoaded_) return;
  auto load = [&](const std::string& path) -> ImTextureID {
      if (!std::filesystem::exists(path)) return 0;
      return (ImTextureID)(intptr_t)engine_->GetUserInterface()->RequestImTextureByName(path);
  };
  tex_.lifebarBg = load(Assets::Hud("lifebar_bg.png"));
  // ...
  texLoaded_ = true;
  ```

**HP 条**：
- [ ] 替换原 ImGui `DrawList->AddRectFilled` 三层为：
  1. 背景：`AddImage(tex_.lifebarBg, p0, p1)`
  2. 填充（按 HP%）：用 `AddImage(tex_.lifebarFill, p0, p1_clipped)` + `uv1.x = hpPct`
  3. 边框：`AddImage(tex_.lifebarFrame, p0, p1)`
- [ ] tex == 0（缺失）时 fallback 到原 ImGui 几何

**XP 条**：同 HP 条逻辑，用 xpBg / xpFill

**Wave 倒计时**：
- [ ] HUD 顶部用 `progress_under` + `progress_progress`（按 timeRemaining/total），两层 AddImage
- [ ] 中央叠 wave 文本（如 `"WAVE 5 / 10  0:18"`）用 midFont_

**武器槽 6 格**：
- [ ] 每格背景用 `panel_flat.png`，tier 2 用 `panel_normal.png`
- [ ] 中央贴 `weaponIcons[weapon.weaponId]`
- [ ] 槽下方 cooldown 进度条仍用 ImGui 几何（小条）
- [ ] tex 缺失时 fallback：纯色块 + 武器名首字母

**敌人头顶 HP 条 + 屏幕投影**：
- [ ] 不替换为贴图（性能 + 投影变形复杂），保留 ImGui 几何即可

**飘字**：保留 ImGui，只切字体（B4 已做）

**Stat 升级 modal 卡片**：
- [ ] 卡片左侧加 stat icon（来自 `items/stats/*.png` 映射）：
  ```cpp
  static const std::unordered_map<std::string, std::string> statIconMap = {
      {"damagePct", "max_hp"},  // 占位 — Brotato 没单独 damagePct 图标，挑形似的
      {"atkSpeedPct", "attack_speed"},
      {"moveSpeedPct", "speed"},
      {"maxHpFlat", "max_hp"},
      {"rangePct", "range"},
      {"critChancePct", "crit_chance"},
      // ...
  };
  ```
- [ ] icon 大小 48×48，左侧居中

### 涉及文件
- 改：`src/Application/Brotato3D/Brotato3DUI.cpp`（HP 条 / XP 条 / 武器槽 / 倒计时 / 升级卡 stat 图标）、`Brotato3DUI.hpp`（FUITextures）
- 改：`scripts/import_brotato_placeholder.py`（HUD 资产 copy_list）

### 验收方法
1. HUD 顶部 wave 倒计时是带贴图的进度条
2. HP 条左下：背景灰皮 + 红色填充 + 金属边框
3. XP 条紧挨 HP 条下方，蓝色填充
4. 武器槽 6 格，每格中央真正的武器图标（SMG / Shotgun / Sniper 等）
5. tier 2 武器槽用更亮的边框（panel_normal）
6. 升级卡左侧有 stat 图标（max_hp / attack_speed 等）
7. 删除 `assets/_placeholder/ui/` 子目录后 → HUD 自动 fallback 到 ImGui 几何形状，仍能玩

### 注意
- `RequestImTextureByName` **首次**调用会触发 atlas 上传 — 把 `LoadHudTextures()` 放在第一帧 `OnRenderUI`，不要在 `OnInit`（此时 backend 可能还没 ready）
- AddImage 的 uv 默认是 (0,0)-(1,1)，HP/XP fill 切片时改 uv1.x 即可（不需要重采样贴图）
- 武器图标 PNG 大小不一（128×128 ~ 200×200）— 统一用 ImGui `AddImage` 缩放到目标 rect，不要自己写采样
- 缺失贴图（路径不存在）时 `ImTextureID = 0`，AddImage 会画黑色矩形 — 必须每个绘制前 `if (tex != 0)` 守门 fallback
- 不要同时改 P10 拆分后的 `Brotato3DUI` 和回归 P10 之前的代码 — 假定 P10 已完成，UI 集中在 `Brotato3DUI.cpp`

---

## B6. 主菜单 splash 美化（logo + 雾气视差）

**优先级**: P0  **工时**: ~1.5h  **依赖**: B1

### 背景

主菜单从"半透明黑覆盖 + 文字按钮"升级为"分层背景 + logo + 雾气视差 + 大按钮"。Brotato 自己的 splash_art 包含 5 层（bg + 雾气 back/mid/front + 主体），可以直接拼成视差背景。

### TODO

**贴图导入**（B1 copy_list 追加 `ui/menu/`）：
- `splash_bg.png`、`splash_brotato.png`、`splash_mist_back.png`、`splash_mist_mid.png`、`splash_mist_front.png`
- `splash_post.png`（来自 `splash_art_post_processing.png`，有则锦上添花）
- `logo.png`（来自 `ui_logo.png`）

**主菜单渲染重构**（`Brotato3DUI::RenderMainMenu`）：
- [ ] 全屏分层（5 层从远到近，每层 `ImGui::GetForegroundDrawList()->AddImage` 全屏 quad）：
  ```
  Layer 0: splash_bg          (静止全屏)
  Layer 1: splash_mist_back   (左漂 +0.4 px/frame)
  Layer 2: splash_brotato     (居中靠右，土豆主角)
  Layer 3: splash_mist_mid    (右漂 -0.6 px/frame)
  Layer 4: splash_mist_front  (左漂 +1.2 px/frame)
  Layer 5: splash_post        (顶层叠加 alpha=0.5)
  ```
- [ ] 雾气漂移用累计偏移变量 `float mistOffsetBack_ / Mid / Front_`，每帧 += dt * speed，对 1024 取模
- [ ] 雾气贴图 uv0.x/uv1.x 平移（uv-scroll）实现循环漂移：
  ```cpp
  float u = mistOffsetBack_ / 1024.0f;
  drawList->AddImage(tex_.mistBack, p0, p1, ImVec2(u, 0), ImVec2(u + 1, 1), IM_COL32(255,255,255,180));
  ```

**Logo + 标题**：
- [ ] 顶部居中 `splash_logo.png`（Brotato 的 ui_logo 是 "BROTATO" 字样）— 替代旧的 ImGui 文本标题
- [ ] 副标题用 bigFont_ 写 `"3D — 幸存 10 波"`（小一号）

**按钮风格化**：
- [ ] 4 个按钮用 panel_normal.png 作为 BG，hover 时切到 panel_flat.png
- [ ] 按钮用 ImGui::ImageButton 或自定义绘制 + InvisibleButton 检测 hover
- [ ] 按钮文本居中，字体 midFont_
- [ ] hover 触发 `PlayUiHoverSfx`（B2 提供），click 触发 `PlayUiClickSfx`

**fallback**：
- [ ] 任何 splash_* 贴图缺失 → 跳过该层，仅画原半透明黑（保留 P8 的实现作为 fallback 路径）

### 涉及文件
- 改：`Brotato3DUI.cpp`（RenderMainMenu）、`Brotato3DUI.hpp`（FUITextures 加 splashBg/Mist/Brotato/Post/Logo）
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. 启动游戏看到主菜单：背景有云雾飘动，土豆角色立绘居中靠右
2. 顶部 BROTATO logo 清晰
3. 4 按钮用真贴图风格，hover 切色 + 听到 hover 音
4. 雾气分 3 层不同方向漂动，**视差感**明显
5. 主菜单 BGM "calm" 在播
6. 删除 `assets/_placeholder/ui/menu/` 后 → 自动 fallback 到原半透明黑

### 注意
- 5 层 AddImage 单帧开销很小（< 1ms），不要再做"按需渲染"优化
- 雾气 uv-scroll 取模时用 `fmodf(offset, 1024.0f)`，避免 float 精度漂移
- 雾气 alpha 值（180）控制不要太透，否则视差不明显；不要太实，否则糊
- splash 主体（土豆）层用 `IM_COL32(255,255,255,255)` 全不透明
- ImageButton 在不同 ImGui 版本接口不同，先尝试 `ImageButton(id, tex, size)`，失败用 InvisibleButton + AddImage 自绘 + IsItemHovered

---

## B7. 选角 UI 人物立绘

**优先级**: P0  **工时**: ~1h  **依赖**: B1, B6

### 背景

P8 的选角是 3 张色块卡，本任务把色块换成 Brotato 角色头像（generalist / brawler / ranger），让"选角色"这个动作有"在挑队友"的感觉。

### TODO

**角色映射**（与 P8 的 `characters.json` 对齐）：
| Phase 2 角色 ID | Brotato 头像源 | 目标文件 |
|---|---|---|
| `soldier` | `items/characters/generalist/generalist_icon.png` | `ui/icons/characters/soldier.png` |
| `brawler` | `items/characters/brawler/brawler_icon.png` | `ui/icons/characters/brawler.png` |
| `marksman` | `items/characters/ranger/ranger_icon.png` | `ui/icons/characters/marksman.png` |

- [ ] B1 copy_list 加上述映射

**渲染**（`Brotato3DUI::RenderCharSelect`）：
- [ ] 每张卡顶部 200×200 的色块改为 `AddImage(charIcons[id], p0, p1, ...)`
- [ ] 头像下方仍是名字 + tagline + 起手武器图标 + stat 列表
- [ ] 当前选中卡的金色边框保留
- [ ] 头像缺失 fallback 到原色块（按 P8 的 `character.color`）

**可选锦上添花**：
- [ ] 当前选中卡放大 5%（`SetCursorPos` 调整 + size scale），增加 selected 的视觉强度
- [ ] hover 时卡背景从 panel_flat 切到 panel_normal

### 涉及文件
- 改：`Brotato3DUI.cpp`（RenderCharSelect）、`Brotato3DUI.hpp`（FUITextures 加 characterIcons map）
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. 选角界面看到 3 个角色头像（土豆基底 + 不同妆容）
2. 选中 Brawler 头像有红色光环 / 边框金色
3. hover 切换时卡背景变化
4. 删除头像 → fallback 到原色块

### 注意
- Brotato 的 character_icon 是 64×64 透明 PNG — 在 200×200 容器里居中放大 OK
- 头像周围空白多，加色块底（`character.color` 半透明）能让头像更突出
- soldier ↔ generalist 是逻辑映射（Phase 2 自定义角色名），不是改 characters.json — 仅在 B7 的图标 map 里做转换

---

## B8. 敌人 icon → wave intro / Boss warning

**优先级**: P1  **工时**: ~1h  **依赖**: B1, B5

### 背景

Wave 开始 banner 目前只有文字 "WAVE 6 / 10"。本任务在 banner 旁边显示该波**主要敌人种类的头像**，让玩家"知道这波该怕谁"。Boss 警告横幅显示 boss 头像。

### TODO

**敌人 icon 映射**（B1 copy_list 加）：
| 项目敌人 ID | Brotato 头像源 | 目标 |
|---|---|---|
| rat | `entities/units/enemies/chaser/chaser_icon.png` | `ui/icons/enemies/rat.png` |
| spitter | `entities/units/enemies/spitter/spitter_icon.png` | `ui/icons/enemies/spitter.png` |
| tank | `entities/units/enemies/bruiser/bruiser_icon.png` | `ui/icons/enemies/tank.png` |
| charger | `entities/units/enemies/charger/charger_icon.png` | `ui/icons/enemies/charger.png` |
| bomber | `entities/units/enemies/spawner/spawner_icon.png` | `ui/icons/enemies/bomber.png` |
| shaman | `entities/units/enemies/healer/healer_icon.png` | `ui/icons/enemies/shaman.png` |
| boss_warden | `ui/icons/misc/boss_icon.png` | `ui/icons/enemies/boss_warden.png` |

- [ ] FUITextures 加 `std::unordered_map<std::string, ImTextureID> enemyIcons;`
- [ ] B5 的 `LoadHudTextures` 末尾加载

**Wave intro banner 改造**（`Brotato3DUI::RenderWaveIntro` 或对应渲染点）：
- [ ] 横幅大字 `"WAVE N / 10"` 不变，下方加一行 64×64 敌人头像（最多 4 个），代表该波关键敌人
  - 敌人列表来自 `waves.json` 该波的 spawn 配置（取前 N 种）
- [ ] boss 波横幅 `"BOSS 来袭"` 红字下方放 128×128 boss 头像，红色光环

**HUD 敌人指示器（可选锦上添花）**：
- [ ] 屏幕右上角加 mini panel，每帧统计 alive 敌人种类计数：
  - 显示前 3 种敌人头像 + `xN` 数字
- [ ] **可省略** — 只做 wave intro banner 的话工时足够

### 涉及文件
- 改：`Brotato3DUI.cpp`（RenderWaveIntro / Wave banner / 可选 RenderEnemyIndicator）、`Brotato3DUI.hpp`
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. wave 6 开始 banner 下方显示 charger / bomber / spitter 三个头像（前 3 种 spawn 数量最大的敌人）
2. wave 10 banner 下方显示巨大的 boss 头像，红色光环
3. 头像缺失（test 删除）→ fallback 到 ImGui 文本（保持 banner 主体不变）

### 注意
- 敌人 icon 选择策略：从 `waves.json` 该波 spawn 列表里按 totalCount 排序取前 3
- boss 头像要明显大于普通敌人头像（128 vs 64），有视觉权重差
- spawner_icon 占位 bomber 是因为 Brotato 没有 bomber 形状 — 视觉接近即可

---

## B9. 商店 UI 风格化（背景 + rarity + icon）

**优先级**: P1  **工时**: ~1.5h  **依赖**: B1, B5

### 背景

商店模态目前是 ImGui 默认窗口 + 4 张文字卡。换成 shop_background 背景 + 物品 rarity 边框 + item/weapon 真图标，瞬间到位。

### TODO

**贴图导入**（B1 copy_list 加）：
- `ui/menu/shop_background.png`
- Item 图标 6 个（前面映射到 vampire_fang / regen_charm / ... 6 个 Brotato item PNG）
- Weapon 图标已在 B5 加载

**Shop modal 风格化**：
- [ ] modal 全屏背景画 shop_background.png（半透明 alpha=0.85）
- [ ] 4 张卡用 panel_normal.png 作 BG
- [ ] 卡边框颜色按 rarity（来自 P6 的 items.json `rarity`）：
  - common：灰 IM_COL32(180,180,180,255)
  - uncommon：绿 IM_COL32(80,200,80,255)
  - rare：紫 IM_COL32(200,80,200,255)
- [ ] 卡左上 64×64 图标：
  - stat 卡：`statIcons[stat]`（B5 已加载）
  - item 卡：`itemIcons[itemId]`（B9 加载）
  - weapon 卡：`weaponIcons[weaponId]`（B5 已加载）
- [ ] 卡中部：名字（midFont_）+ 描述（默认字体）
- [ ] 卡底部：价格 + "购买" 按钮
- [ ] reroll 按钮放在 4 卡下方居中，配 dice 图标（来自 `ui/icons/all/material_icon_data.tres` 缺失，简单用 stat icon 充当）

**商店开张特效**：
- [ ] modal 弹出时 200ms 缩放动画（scale 0.7→1.0），用 `SetNextWindowSize` + 时间插值
- [ ] PlayShopOpenSfx 在弹出瞬间触发（已在 B2）

**结算 UI 用同套风格**（顺手）：
- [ ] 死亡 / 胜利结算 modal 用 panel_transparent.png 背景
- [ ] 拥有 items 列表用真图标 32×32 排成一行

### 涉及文件
- 改：`Brotato3DUI.cpp`（RenderShop / RenderResult）
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. wave 1 结束 → 商店 modal 弹出有缩放动画 + 开张音
2. 4 张卡看到真图标 + rarity 边框（颜色区分明显）
3. weapon 卡显示武器图标
4. 死亡结算 items 列表用真图标
5. 删除 shop_background.png → modal 仍可用，背景退化为 ImGui 默认

### 注意
- ImGui modal 的"缩放动画"实现用 `SetNextWindowSizeConstraints` + 自定义 scale state，ImGui 没有内置动画 API
- 商店滚刷池过滤时（P6 的逻辑）当 item 图标缺失要 fallback 到名字首字母（保持 P6 的占位写法）
- rarity 颜色用 IM_COL32 alpha=255 而非半透明（边框需要清晰可见）

---

## B10. Arena 主题底图（zone 绿野/荒地/数码）

**优先级**: P2（可选）  **工时**: ~1h  **依赖**: B1

### 背景

P11 的多场地仅是地面/边界换色块。本任务在地面纹理 + 圆形 vignette 上叠 Brotato 的 zone 背景图，让 3 个场地有真正的视觉差异。

### TODO

**贴图导入**（B1 copy_list 加 — Brotato 的 background_data 是 .tres 不能直接用，需要找对应的 PNG）：
- 翻找 `zones/backgrounds/`、`zones/zone_1/000_all/`，挑 3 张能直接当 ground tile 的纹理
  - 若资产中无 ground tile PNG（Godot 用 SVG/Shader 居多），**跳过 B10**，留 P11 的纯色块即可
  - 若有：导入到 `ui/arena/ground_grass.png`、`ground_wasteland.png`、`ground_techgrid.png`

**Arena ground material**：
- [ ] 检查项目当前 arena 实现 [Brotato3DArena.cpp](../../../src/Application/Brotato3D/Brotato3DArena.cpp)：是否走 PBR material with diffuse texture
- [ ] 若是 → 把 `groundMaterial.albedo` 替换为 ground_*.png（按 selectedArenaId 选择）
- [ ] 若不是（仅顶点色）→ **不实现** B10，跳过

**Vignette + 边界**：
- [ ] 屏幕角落叠 `splash_post.png`（B6 的 splash_post）作为暗角，提升场景"包裹感"

### 涉及文件
- 改：`Brotato3DArena.cpp`、`Brotato3DUI.cpp`（vignette）
- 改：`scripts/import_brotato_placeholder.py`

### 验收方法
1. 选择 grassland → 地面有草地纹理
2. 选择 wasteland → 地面荒地颜色
3. 选择 tech_grid → 地面带网格
4. 屏幕四角有暗角

### 注意
- **本任务允许整体跳过**：Brotato 是 2D Godot，地面 tile 资产可能不直接可用；若找不到合适 PNG，跳到 P11 数据驱动纯色已 OK
- 不要试图把 Brotato 的 .tres 资源运行时解析 — 直接用 PNG
- vignette 用 ImGui foreground draw list AddImageQuad 即可，不要走 shader

---

## 公共约束（所有 B 任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target Brotato3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀 |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 平台 | 用 `PlatformCommon.h`，不直接 include 平台头 |
| 注释 | 默认不写注释，仅写非显然的 WHY |
| 提交 | 不要执行 git commit；只完成代码改动，由用户决定何时提交；**`assets/_placeholder/` 永远不入 git** |
| 沟通 | 与用户用中文对话 |

**禁止**：
- 修改 `src/ThirdParty/` 或 `external/`
- 引入新大型依赖
- 把任何 Brotato 资产 commit 到 git（包括误操作 add）
- 把 Brotato 资产用脚本 zip / encrypt / encode 后混入仓库（这等同 commit）
- 在公开 demo / 录屏 / itch.io 页 / Steam 页 / 朋友圈 / Twitter 截图中展示这些资产（**作者本人需自觉，agent 仅在代码层确保 gitignore 完整）

**Fallback 守门**：
- 所有 B5–B10 的贴图引用必须有 `if (tex == 0) fallback to ImGui geom` 守门
- 所有 B2–B3 的音频路径必须经过 `Brotato3DAudio` 内部存在性检查（已在 PlayBrotatoSfx 实现）
- B4 的字体加载失败必须 fallback 到默认字体（`if (font) ... else use default`）
- 验收的最后一步：**删除整个 `assets/_placeholder/` 目录，跑一遍主菜单 → wave 1 → 商店 → 结算 → 主菜单**，全程游戏可玩，仅视听降级

## 验证完整端到端

完成 B1–B10 后：

1. `python3 scripts/import_brotato_placeholder.py` 成功导入 ~150 文件
2. `git status` 不显示 `assets/_placeholder/`
3. `./build.bat --preset full-windows --reconfigure` 通过
4. `./run.bat` 启动 → 启动 spdlog 看到 PLACEHOLDER 警告
5. **主菜单**：BROTATO logo + 雾气视差 + 土豆立绘 + calm BGM + 4 大按钮 + 右上 ⚠ PLACEHOLDER 角标
6. **选角**：3 张卡 generalist / brawler / ranger 头像
7. **Wave 1**：HUD 有 HP/XP 真贴图 + 武器槽真图标 + 倒计时贴图 + 飘字用 Anybody 字体
8. **Wave 4**：BGM 切 power-punch（battle）；wave 6 banner 下方显示该波敌人头像
9. **Wave 10**：BGM 切 extreme-chaos（boss）；BOSS 来袭横幅 + 巨大 boss 头像
10. **Boss 战 → 击杀**：victory.wav 播放
11. **结算**：拥有 items 用真图标
12. **设置**：调音量即时生效，关震动有效，关 HP 条有效
13. **退出删除 `assets/_placeholder/` → 重启**：游戏仍能完整通关一局，UI 退化为 ImGui 几何 + 无音效（acceptable degraded）

## 风险与备注

| 风险 | 应对 |
|---|---|
| 误把 _placeholder commit 进 git | B1 强制 .gitignore + import 脚本前置检查；agent 在 commit 前每次 `git status` 检查 |
| BGM mp3 加载失败（miniaudio 编译开关） | 已知 `assets/sfx/bgm.mp3` 已用，验证可用；若挂掉转 wav 重导（脚本加 ffmpeg fallback） |
| 占位资产意外进入 release 包 | 后续 Phase 4 上线前**必须**先做"占位资产替换审计"任务（不在本计划内） |
| Anybody-Medium 不含中文 → 中文 banner 显示 fallback | wave banner / 飘字纯英文 + 数字（已是），中文统一走 NotoSansSC 默认字体 |
| 雾气视差性能 | 5 层 AddImage 单帧 < 1ms，1080p 下不影响帧率；Steam Deck 需测 |
| ImGui atlas 超 4096 | 中文字形限制在常用 5 个 unicode 块（B4 已限），实测 atlas 约 2048×2048 |
| 占位资产体积大（音乐 mp3 ~30MB × 3） | 不进 git 故无影响；本地存储能接受 |
| Brotato 资产风格与 ProcModel 几何不统一 | 主菜单 / HUD / UI 用 Brotato 美术，**游戏世界**仍是 ProcModel 几何 — 风格分层即可，作者实测可接受 |

## 后续 agent 调用建议

每个 B 任务（B1, B2, ...）适合用一个独立 agent 调用执行，prompt 模板：

```
请执行 docs/projects/brotato-3d/asset-polish-plan.md 中的 B{N} 任务。

前置条件：
- product-plan.md 的 P1–P10 已完成
- 假设 B1 已经跑过（资产已导入 assets/_placeholder/brotato/），除非你执行的就是 B1
- 严格按 TODO 清单做，不要扩大范围
- 完成后用 full-windows preset 编译验证
- 验收方法逐条勾选汇报，**包括"删除 _placeholder 后游戏仍可玩"那一条**
- 不要 commit
- 与用户沟通用中文
- 永远不要把 _placeholder 内文件 git add
```

**推荐执行顺序**：
1. B1（资产导入 + license 护栏，所有后续依赖）
2. B2（SFX）+ B3（BGM）+ B4（字体）— 三者独立，可并行 3 个 agent 同时跑
3. B5（HUD）— 视觉骨架
4. B6（主菜单）+ B7（选角）+ B8（敌人 icon）+ B9（商店）— 各自独立 UI 区域，可并行 4 个 agent
5. B10（arena，可选）

---

## 附录 A：完整资产清单（Brotato 解包目录）

> 已盘点。本附录为 agent 在执行 B 任务时的快速查阅表，避免重新探查目录。

**音频文件（共 168 wav + 8 mp3 + 2 ogg）**：

```
ui/sounds/                              13 wav (UI 全套：button/buy/dice/wave_end/...)
weapons/melee_sounds/                   18 wav (whoosh / glass break)
weapons/ranged/<weapon>/                每把武器 1-9 wav (开火变体)
entities/units/unit/hurt_sounds/        13 wav (玩家受伤 + 通用 impact)
entities/units/unit/crit_sounds/        4 wav (金属命中 = 暴击)
entities/units/unit/dodge_sounds/       2 wav
entities/units/player/hp_regen_sounds/  2 ogg (Potion grab)
entities/units/player/step_sounds/      9 mp3 (脚步)
entities/units/enemies/boss/            1 wav (zombie voice)
entities/units/enemies/pursuer/         2 wav (sci-fi fail)
entities/units/pet/<pet>/               每宠物 2-3 wav
entities/birth/                         2 wav (出生音效)
items/consumables/<type>/               1-2 wav (拾取)
resources/sounds/                       17 mp3+wav (level_up.wav + Madness/Fear OST)
resources/music/                        6 mp3 (Artlist 授权 BGM)
```

**字体（resources/fonts/raw/）**：
- Anybody-Medium.ttf
- NotoSansJP-Medium.otf
- NotoSansKR-Medium.otf
- NotoSansSC-Medium.otf
- NotoSansTC-Medium.otf

**UI 图像（共 1297 PNG）**：
- `ui/hud/*.png`（HP/XP/panel/grabber/progress 共 11 张）
- `ui/menus/title_screen/title_screen_background/*.png`（splash 5 张 + ui_logo）
- `ui/menus/shop/shop_background.png`
- `ui/menus/global/*.png`（按钮 / 键位图标 30+ 张）
- `ui/icons/all/*_data.tres` + `ui/icons/misc/*.png`（boss_icon, enemy_icon 等通用图标 17 张）
- `ui/splash/splash_anim_*.png`（开场动画 30+ 张，本计划不用）

**角色立绘（items/characters/）**：
共 40+ 角色文件夹，每个含 `<id>_icon.png`、`<id>_eyes.png`、`<id>_mouth.png`。本计划用 `generalist`、`brawler`、`ranger` 三个。

**敌人 icon（entities/units/enemies/<type>/<type>_icon.png）**：
chaser、bruiser、spitter、charger、healer、bomber、spawner、boss/* 等 30+ 种。

**武器图标（weapons/ranged/<weapon>/<weapon>_icon.png）**：
smg, shotgun(double_barrel_shotgun), sniper_gun, rocket_launcher, laser_gun, flamethrower 6 种本计划用。

**Stat 图标（items/stats/）**：
armor, attack_speed, crit_chance, dodge, elemental_damage, engineering, harvesting, hp_regeneration, lifesteal, luck, max_hp, melee_damage, percent_damage, range, ranged_damage, speed 共 17 张。

**Item 图标（items/all/<item_id>/<item_id>_icon.png）**：
共 200+ items，本计划用 6 个映射到 P6 设计的占位 item。

---

## 附录 B：完整 SFX 路径映射表（B1 import 脚本的 copy_list 全集）

```python
copy_list = {
    # ============ 武器开火（多变体） ============
    "audio/sfx/fire_smg_01.wav": "weapons/ranged/smg/gun_submachine_auto_shot_01.wav",
    "audio/sfx/fire_smg_02.wav": "weapons/ranged/smg/gun_submachine_auto_shot_02.wav",
    "audio/sfx/fire_smg_03.wav": "weapons/ranged/smg/gun_submachine_auto_shot_03.wav",
    "audio/sfx/fire_smg_04.wav": "weapons/ranged/smg/gun_submachine_auto_shot_04.wav",
    "audio/sfx/fire_smg_05.wav": "weapons/ranged/smg/gun_submachine_auto_shot_05.wav",
    "audio/sfx/fire_smg_06.wav": "weapons/ranged/smg/gun_submachine_auto_shot_06.wav",
    "audio/sfx/fire_smg_07.wav": "weapons/ranged/smg/gun_submachine_auto_shot_07.wav",
    "audio/sfx/fire_smg_08.wav": "weapons/ranged/smg/gun_submachine_auto_shot_08.wav",
    "audio/sfx/fire_smg_09.wav": "weapons/ranged/smg/gun_submachine_auto_shot_09.wav",
    "audio/sfx/fire_shotgun_01.wav": "weapons/ranged/smg/gun_submachine_auto_shot_07.wav",
    "audio/sfx/fire_sniper_01.wav": "weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04.wav",
    "audio/sfx/fire_rocket_01.wav": "weapons/ranged/rocket_launcher/gun_silenced_pistol2_shot_01.wav",
    "audio/sfx/fire_laser_01.wav": "weapons/ranged/laser_gun/gun_silenced_sniper1_shot_04v2.wav",
    "audio/sfx/fire_flamethrower_01.wav": "weapons/ranged/flamethrower/gas_med_flame_ignite_01.wav",
    "audio/sfx/fire_flamethrower_02.wav": "weapons/ranged/flamethrower/gas_med_flame_ignite_02.wav",
    "audio/sfx/fire_flamethrower_03.wav": "weapons/ranged/flamethrower/gas_small_flame_ignite_01.wav",
    "audio/sfx/fire_flamethrower_04.wav": "weapons/ranged/flamethrower/gas_small_flame_ignite_02.wav",

    # ============ 命中 / 暴击 ============
    "audio/sfx/hit_normal_01.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_01.wav",
    "audio/sfx/hit_normal_02.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_02.wav",
    "audio/sfx/hit_normal_03.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_03.wav",
    "audio/sfx/hit_normal_04.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_04.wav",
    "audio/sfx/hit_normal_05.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_05.wav",
    "audio/sfx/hit_crit_01.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_01.wav",
    "audio/sfx/hit_crit_02.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_02.wav",
    "audio/sfx/hit_crit_03.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_03.wav",
    "audio/sfx/hit_crit_04.wav": "entities/units/unit/crit_sounds/bullet_impact_metal_light_05.wav",

    # ============ 玩家受伤 ============
    "audio/sfx/player_hurt_01.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_05.wav",
    "audio/sfx/player_hurt_02.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_06.wav",
    "audio/sfx/player_hurt_03.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_07.wav",
    "audio/sfx/player_hurt_04.wav": "entities/units/unit/hurt_sounds/bullet_impact_body_flesh_08.wav",

    # ============ 敌人死亡 ============
    "audio/sfx/enemy_die_small_01.wav": "entities/units/unit/hurt_sounds/bullet_impact_water_01.wav",
    "audio/sfx/enemy_die_small_02.wav": "entities/units/unit/hurt_sounds/bullet_impact_water_02.wav",
    "audio/sfx/enemy_die_tank_01.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_06.wav",
    "audio/sfx/enemy_die_tank_02.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_07.wav",
    "audio/sfx/enemy_die_tank_03.wav": "entities/units/unit/hurt_sounds/punch_general_body_impact_08.wav",
    "audio/sfx/enemy_die_boss.wav": "entities/units/enemies/boss/zombie_voice_general_emote_05.wav",

    # ============ 拾取 ============
    "audio/sfx/pickup_xp_01.ogg": "entities/units/player/hp_regen_sounds/Potion_Grab_01.ogg",
    "audio/sfx/pickup_xp_02.ogg": "entities/units/player/hp_regen_sounds/Potion_Grab_02.ogg",
    "audio/sfx/pickup_material.wav": "items/consumables/item_box/item_box_pickup.wav",

    # ============ 升级 / 波次 ============
    "audio/sfx/level_up.wav": "resources/sounds/level_up.wav",
    "audio/sfx/wave_start.wav": "ui/sounds/clock_tick_01.wav",
    "audio/sfx/wave_end.wav": "ui/sounds/end_wave.wav",
    "audio/sfx/wave_start_boss.wav": "entities/units/enemies/boss/zombie_voice_general_emote_05.wav",

    # ============ UI ============
    "audio/sfx/ui_click.wav": "ui/sounds/button_press.wav",
    "audio/sfx/ui_hover.wav": "ui/sounds/button_focus.wav",
    "audio/sfx/shop_open.wav": "ui/sounds/CGM3_Bubble_Button_01_3.wav",
    "audio/sfx/shop_buy.wav": "ui/sounds/buy.wav",
    "audio/sfx/shop_reroll.wav": "ui/sounds/diceroll.wav",
    "audio/sfx/shop_cant_buy.wav": "ui/sounds/cant_buy.wav",

    # ============ 结算 ============
    "audio/sfx/victory.wav": "entities/units/pet/scapegoat/scapegoat_rising.wav",
    "audio/sfx/defeat.wav": "entities/units/enemies/pursuer/sci-fi_code_fail_08.wav",

    # ============ BGM ============
    "audio/bgm/bgm_calm.mp3":   "resources/music/wasteland-survivors by evgeny-bardyuzha Artlist.mp3",
    "audio/bgm/bgm_battle.mp3": "resources/music/power-punch by 2050 Artlist.mp3",
    "audio/bgm/bgm_boss.mp3":   "resources/music/extreme-chaos by 2050 Artlist.mp3",

    # ============ 字体 ============
    "fonts/Anybody-Medium.ttf":     "resources/fonts/raw/Anybody-Medium.ttf",
    "fonts/NotoSansSC-Medium.otf":  "resources/fonts/raw/NotoSansSC-Medium.otf",

    # ============ HUD 图像 ============
    "ui/hud/lifebar_bg.png":         "ui/hud/ui_lifebar_bg.png",
    "ui/hud/lifebar_fill.png":       "ui/hud/ui_lifebar_fill.png",
    "ui/hud/lifebar_frame.png":      "ui/hud/ui_lifebar_frame.png",
    "ui/hud/xp_bg.png":              "ui/hud/ui_xp_bg.png",
    "ui/hud/xp_fill.png":            "ui/hud/ui_xp_fill.png",
    "ui/hud/progress_under.png":     "ui/hud/ui_progress_under.png",
    "ui/hud/progress_progress.png":  "ui/hud/ui_progress_progress.png",
    "ui/hud/panel_normal.png":       "ui/hud/ui_panel_normal.png",
    "ui/hud/panel_flat.png":         "ui/hud/ui_panel_flat.png",
    "ui/hud/panel_transparent.png":  "ui/hud/ui_panel_transparent.png",

    # ============ 主菜单 ============
    "ui/menu/splash_bg.png":         "ui/menus/title_screen/title_screen_background/splash_art_bg.png",
    "ui/menu/splash_brotato.png":    "ui/menus/title_screen/title_screen_background/splash_art_brotato.png",
    "ui/menu/splash_mist_back.png":  "ui/menus/title_screen/title_screen_background/splash_art_mist_back.png",
    "ui/menu/splash_mist_mid.png":   "ui/menus/title_screen/title_screen_background/splash_art_mist_mid.png",
    "ui/menu/splash_mist_front.png": "ui/menus/title_screen/title_screen_background/splash_art_mist_front.png",
    "ui/menu/splash_post.png":       "ui/menus/title_screen/title_screen_background/splash_art_post_processing.png",
    "ui/menu/logo.png":              "ui/menus/title_screen/title_screen_background/ui_logo.png",
    "ui/menu/shop_background.png":   "ui/menus/shop/shop_background.png",

    # ============ 角色头像 ============
    "ui/icons/characters/soldier.png":  "items/characters/generalist/generalist_icon.png",
    "ui/icons/characters/brawler.png":  "items/characters/brawler/brawler_icon.png",
    "ui/icons/characters/marksman.png": "items/characters/ranger/ranger_icon.png",

    # ============ 武器图标 ============
    "ui/icons/weapons/smg.png":          "weapons/ranged/smg/smg_icon.png",
    "ui/icons/weapons/shotgun.png":      "weapons/ranged/double_barrel_shotgun/double_barrel_shotgun_icon.png",
    "ui/icons/weapons/sniper.png":       "weapons/ranged/sniper_gun/sniper_gun_icon.png",
    "ui/icons/weapons/rocket.png":       "weapons/ranged/rocket_launcher/rocket_launcher_icon.png",
    "ui/icons/weapons/laser.png":        "weapons/ranged/laser_gun/laser_gun_icon.png",
    "ui/icons/weapons/flamethrower.png": "weapons/ranged/flamethrower/flamethrower_icon.png",

    # ============ 敌人图标 ============
    "ui/icons/enemies/rat.png":         "entities/units/enemies/chaser/chaser_icon.png",
    "ui/icons/enemies/spitter.png":     "entities/units/enemies/spitter/spitter_icon.png",
    "ui/icons/enemies/tank.png":        "entities/units/enemies/bruiser/bruiser_icon.png",
    "ui/icons/enemies/charger.png":     "entities/units/enemies/charger/charger_icon.png",
    "ui/icons/enemies/bomber.png":      "entities/units/enemies/spawner/spawner_icon.png",
    "ui/icons/enemies/shaman.png":      "entities/units/enemies/healer/healer_icon.png",
    "ui/icons/enemies/boss_warden.png": "ui/icons/misc/boss_icon.png",

    # ============ Item 图标（占位映射） ============
    "ui/icons/items/vampire_fang.png": "items/all/blood_leech/blood_leech_icon.png",
    "ui/icons/items/regen_charm.png":  "items/all/celery_tea/celery_tea_icon.png",
    "ui/icons/items/magnet.png":       "items/all/coupon/coupon_icon.png",   # 占位
    "ui/icons/items/fury_core.png":    "items/all/adrenaline/adrenaline_icon.png",
    "ui/icons/items/shrapnel.png":     "items/all/explosive_shells/explosive_shells_icon.png",
    "ui/icons/items/speed_boots.png":  "items/all/big_arms/big_arms_icon.png",  # 占位

    # ============ Stat 图标 ============
    "ui/icons/stats/max_hp.png":       "items/stats/max_hp.png",
    "ui/icons/stats/speed.png":        "items/stats/speed.png",
    "ui/icons/stats/attack_speed.png": "items/stats/attack_speed.png",
    "ui/icons/stats/crit_chance.png":  "items/stats/crit_chance.png",
    "ui/icons/stats/range.png":        "items/stats/range.png",
    "ui/icons/stats/armor.png":        "items/stats/armor.png",
    "ui/icons/stats/lifesteal.png":    "items/stats/lifesteal.png",
    "ui/icons/stats/dodge.png":        "items/stats/dodge.png",
    "ui/icons/stats/percent_damage.png": "items/stats/percent_damage.png",
    "ui/icons/stats/ranged_damage.png":  "items/stats/ranged_damage.png",
}
```

> **B1 实现脚本时直接复制上述 dict** —— agent 不必自己探查路径。
> 部分映射（magnet / speed_boots）是不完美的占位，作者后期替换为自制资产时会改这两条。
