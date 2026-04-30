# 空裂 KONG LIE 3D 复刻 — 开发计划

## Context

参考 web 原型 [biaowww/webEngine-projectBreach](https://github.com/biaowww/webEngine-projectBreach)（Phaser 3 自走棋"空裂 KONG LIE"），在 gkNextRenderer 引擎中新建一个 Application（`KongLie3D`），用引擎现有能力（Vulkan 渲染、ImGui、ECS、ProcModel、GPU Raycast）做一个 3D 化复刻。

**Web 原型核心机制**（从源码逐字提取，非猜测）：
- 棋盘 7 列 × 8 行；敌方占 row 0-3，玩家占 row 4-7
- 战斗采用 100ms 固定 tick（`BattleSystem.js` 的 `TICK_MS = 100`）
- AI 选目标用切比雪夫距离 `max(|Δcol|, |Δrow|)`，范围内打、否则走一步
- 伤害公式：`atk × dmgMult × furyMult`，加时每秒 dmgMult +0.1（封顶 1.7）
- 单位类型：ADC（远程脆皮）/ DEF/TANK（坦克）/ SUP（治疗）/ FTR（战士） / HERO（主角 Blue + 副角 Sydney）
- 普攻积蓝（`gainMana`），满蓝自动 W；玩家点击 R 释放大招（每场 1 次）
- 加时赛：30 秒触发（`BATTLE_LIMIT_MS`），45 秒判平局
- 共 6 个玩家上场 + 3 板凳席，6 个敌方暗影单位

**用户决策**（Phase 1 已确认）：
- 范围：**MVP 核心战斗**（M1-M7 必做，M8 可选）
- 视觉：**ProcModel 简单几何体**（box / cylinder / sphere 组合，复用 MagicaLego 风格）
- 文档：输出到 `docs/projects/konglie-3d/plan.md`
- 数据：**JSON** 配置（`assets/configs/konglie/pieces.json` + `placement.json`）

**为什么选这个方向**：MVP 优先验证「3D 棋盘 + 战斗 tick + ImGui HUD」三大块在引擎里能跑通；几何体方案不依赖美术资产，把后续 agent 的开发卡点降到最小（重编译就能见效，不需要找模型）。

## 引擎可复用能力清单（不新造轮子）

| 需求 | 复用 | 文件路径 |
|---|---|---|
| Application 入口 | `NextGameInstanceBase` | [src/Runtime/Engine.hpp:41](../../../src/Runtime/Engine.hpp) |
| 输入回调 | `OnKey/OnMouseButton/OnCursorPosition` 虚函数 | [src/Runtime/Engine.hpp:74](../../../src/Runtime/Engine.hpp) |
| 程序化几何体 | `Assets::FProcModel::CreateBox/CreateSphere` | [src/Application/gkNextRenderer/gkNextRenderer.cpp:154](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) |
| 场景动态构建 | `BeforeSceneRebuild` + `Scene::AddNode` | [src/Application/gkNextRenderer/gkNextRenderer.cpp:150](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) |
| 节点+组件 | `Node::CreateNode` + `RenderComponent` | [src/Assets/Core/Node.h:18](../../../src/Assets/Core/Node.h), [src/Runtime/Components/RenderComponent.h](../../../src/Runtime/Components/RenderComponent.h) |
| 鼠标拾取 | `RayCastGPU` | [src/Runtime/Engine.hpp:260](../../../src/Runtime/Engine.hpp) |
| ImGui HUD | `OnRenderUI/OnInitUI` 虚函数 | [src/Runtime/Engine.hpp:49](../../../src/Runtime/Engine.hpp) |
| CMake 注册套路 | 参考 MagicaLego | [src/cmake/SourceFiles.cmake:74](../../../src/cmake/SourceFiles.cmake), [src/CMakeLists.txt:123](../../../src/CMakeLists.txt) |
| 启动 | `run.bat --target KongLie3D.exe` | [scripts/run.ps1:12](../../../scripts/run.ps1) |

**不需要的**：物理（PhysicsComponent 不挂；棋子是网格驱动逻辑动）、QuickJS（数据走 JSON 即可）、glTF 加载（纯 procmodel）。

## 文件结构（最终态）

```
src/Application/KongLie3D/
├── KongLie3DGameInstance.hpp/cpp     # 主入口，继承 NextGameInstanceBase
├── KongLie3DBoard.hpp/cpp            # 棋盘网格 + 单元格高亮
├── KongLie3DPiece.hpp/cpp            # 棋子运行时数据（HP/MP/team/role/cooldowns）
├── KongLie3DBattleSystem.hpp/cpp     # 100ms tick / AI / 伤害 / 寻路
├── KongLie3DSkills.hpp/cpp           # W/R 技能效果（M6）
├── KongLie3DUI.hpp/cpp               # ImGui HUD（英雄面板/计时器/统计）
└── KongLie3DDataLoader.hpp/cpp       # JSON 加载 PieceDef/Placement

assets/configs/konglie/
├── pieces.json          # 全部单位定义（HP/ATK/role/skills 等）
└── placement.json       # 初始阵型 + 板凳席

docs/projects/konglie-3d/
└── plan.md              # 本文档
```

CMake 修改（仅 2 处）：
- [src/cmake/SourceFiles.cmake](../../../src/cmake/SourceFiles.cmake) 加 `src_files_konglie3d` GLOB
- [src/CMakeLists.txt](../../../src/CMakeLists.txt) 加 `add_executable(KongLie3D ...)`

## JSON Schema 设计

### pieces.json（精简，仅保留 MVP 必需字段）

```json
{
  "pieces": {
    "hero_blue": {
      "name": "布鲁 Blue",
      "team": "player",
      "isHero": true,
      "role": "tank",
      "attackType": "ap",
      "hp": 1520, "atk": 24, "atkSpeed": 1.60, "range": 2,
      "moveSpeed": 0.65,
      "maxMana": 90, "manaPerAtk": 22,
      "color": [0.13, 0.27, 0.73],
      "skills": {
        "w": { "name": "魔晶护盾", "effect": "magic_shield", "cooldown": 9000 },
        "ultimate": { "name": "蓝色洪流", "effect": "blue_surge" }
      }
    },
    "hero_sydney": { ... },
    "adc_a": { ... }, "adc_b": { ... },
    "tank_support": { ... }, "healer_support": { ... },
    "shadow_adc_1": { ... }, "shadow_adc_2": { ... }, "shadow_adc_3": { ... },
    "shadow_tank_1": { ... }, "shadow_tank_2": { ... }, "shadow_healer": { ... },
    "bench_tank_1": { ... }, "bench_tank_2": { ... }, "bench_tank_3": { ... }
  }
}
```

数值**直接抄自** [`pieceData.js`](https://github.com/biaowww/webEngine-projectBreach/blob/main/src/data/pieceData.js)（已在 Phase 1 完整提取）。

### placement.json

```json
{
  "player": [
    { "pieceId": "hero_blue", "col": 2, "row": 4 },
    { "pieceId": "tank_support", "col": 4, "row": 4 },
    { "pieceId": "hero_sydney", "col": 3, "row": 5 },
    { "pieceId": "healer_support", "col": 3, "row": 6 },
    { "pieceId": "adc_a", "col": 0, "row": 7 },
    { "pieceId": "adc_b", "col": 6, "row": 7 }
  ],
  "enemy": [ ... 同 web 版 ... ],
  "bench": ["bench_tank_1", "bench_tank_2", "bench_tank_3"]
}
```

JSON 库使用项目已有的 nlohmann/json 或 rapidjson（M2 任务首先在 `vcpkg.json` 确认，必要时复用 `gkNextVisualTest` 的解析路径）。

## 视觉/坐标约定

- 棋盘原点：世界坐标 `(0, 0, 0)`，X 轴 = 列（0-6），Z 轴 = 行（0-7），Y 轴向上
- 每格 1m × 1m，棋盘平面 `y = 0`
- 棋子尺寸（按 role）：
  - TANK / DEF：宽矮 box `0.7 × 0.5 × 0.7`
  - ADC：高瘦 box `0.4 × 0.9 × 0.4`
  - FTR：方正 box `0.55 × 0.7 × 0.55`
  - SUP：圆柱（用 box 近似）`0.5 × 0.6 × 0.5`
  - HERO：在原 role 基础上 +0.2 高度，顶部加一个小 sphere 装饰
- 颜色：直接读 JSON `color`，转 `vec3`，做 Lambertian 材质
- 摄像机：固定俯视斜角，眼位约 `(3.5, 8, 11)`，看向 `(3.5, 0, 3.5)`，对应 web 版 `perspective 1400px / rotateX 6deg` 的 3D 等价
- HP 条：ImGui foreground draw list，把世界坐标投影到屏幕坐标，画细矩形

## 任务索引（MVP 共 7 个，~7-9 小时）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [M1](#m1-application-骨架--棋盘渲染) | Application 骨架 + 棋盘渲染 | ~1h | — |
| [M2](#m2-json-数据加载--单位渲染) | JSON 数据加载 + 单位渲染 | ~1.5h | M1 |
| [M3](#m3-战斗-tick--ai--伤害结算) | 战斗 tick + AI + 伤害结算 | ~1.5h | M2 |
| [M4](#m4-imgui-hud--英雄面板) | ImGui HUD + 英雄面板 | ~1h | M3 |
| [M5](#m5-部署阶段拖拽阵型) | 部署阶段（拖拽阵型） | ~1.5h | M4 |
| [M6](#m6-技能-w-自动--r-手动大招) | 技能 W 自动 + R 手动大招 | ~1h | M4 |
| [M7](#m7-加时赛--结算--抛光) | 加时赛 + 结算 + 抛光 | ~1h | M3, M4 |
| [M8](#m8-可选圣物系统--调优) | （可选）圣物系统 + 调优 | ~1h | M7 |

---

## M1. Application 骨架 + 棋盘渲染

**优先级**: P0  **工时**: ~1h

### 背景
打通编译/启动链。新建 `KongLie3D` 子项目，参考 `gkNextRenderer` 的最小模板（不要参考 MagicaLego，那个太复杂）。本任务后能 `run.bat --target KongLie3D.exe` 启起来，看到一个 7×8 的 3D 棋盘和俯视摄像机即算成功。

### TODO
- [ ] 创建目录 `src/Application/KongLie3D/`
- [ ] 写 `KongLie3DGameInstance.hpp`：继承 `Engine::NextGameInstanceBase`，声明 `OnInit/OnTick/OnDestroy/OnRenderUI/OnKey/OnMouseButton`，构造函数把窗口 title 设为 `"KongLie3D"`，1280×720
- [ ] 写 `KongLie3DGameInstance.cpp`：
  - 实现 `CreateGameInstance`（参考 [`gkNextRenderer.cpp:119`](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp)）
  - `OnInit` 留空（暂不加载场景）
  - `OnTick` 留空
  - `OnRenderUI` 画一个最小 ImGui 窗口 `"KongLie3D MVP"`，里面 `ImGui::Text("Phase 1: bootstrap OK")`
- [ ] 写 `KongLie3DBoard.hpp/cpp`：暴露 `BuildBoard(std::vector<Model>&, std::vector<FMaterial>&, std::vector<shared_ptr<Node>>&)` 函数，用 `FProcModel::CreateBox` 生成 7×8 个地面格子（每格 0.95×0.05×0.95），交替深浅灰色；中线 `row=4` 处用蓝紫色高亮
- [ ] 在 `BeforeSceneRebuild` 钩子里调用 `BuildBoard`
- [ ] 在 `OverrideRenderCamera` 里返回固定俯视摄像机（参考 [`gkNextRenderer.cpp`](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) 中 `Assets::Camera` 的构造方式）
- [ ] CMake 注册：
  - `src/cmake/SourceFiles.cmake`：加 `file(GLOB_RECURSE src_files_konglie3d "Application/KongLie3D/*.cpp" "Application/KongLie3D/*.hpp")`
  - `src/CMakeLists.txt`：加 `add_executable(KongLie3D ${src_files_konglie3d} DesktopMain.cpp)`，并把同样的 `target_link_libraries` / `set_target_properties` 复制 MagicaLego 那段
- [ ] 创建空 `assets/configs/konglie/.gitkeep`（M2 用）

### 涉及文件
- 新建：`src/Application/KongLie3D/KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DBoard.{hpp,cpp}`
- 改：`src/cmake/SourceFiles.cmake`、`src/CMakeLists.txt`

### 验收方法
1. `./build.bat --preset full-windows --reconfigure` 通过
2. `./run.bat --preset full-windows --target KongLie3D` 成功启动
3. 日志出现 `uploaded scene [...] to gpu`
4. 屏幕上能看到 7×8 灰白棋盘，row=4 中线蓝紫色，俯视斜角
5. ImGui 窗口右上角显示 `"Phase 1: bootstrap OK"`

### 注意
- **不要**复制 MagicaLego 整个目录 — 它有 AI/Script/Pak 等大量本任务用不上的代码
- **不要**在 M1 加 RayCast / 单位 / UI 复杂度，所有这些放后续任务
- 摄像机用 `OverrideRenderCamera` 覆盖，**不要**自己实现 ModelViewController（太重）

---

## M2. JSON 数据加载 + 单位渲染

**优先级**: P0  **工时**: ~1.5h  **依赖**: M1

### 背景
让棋子从 JSON 数据驱动地渲染到棋盘上。完成后能看到双方 12 个单位按 web 原型阵型摆好（不动、不打）。

### TODO
- [ ] 写 `assets/configs/konglie/pieces.json`：把 `pieceData.js` 里 13 个单位（hero_blue, hero_sydney, adc_a, adc_b, tank_support, healer_support, 3 个 bench, 3 个 shadow_adc, 2 个 shadow_tank, shadow_healer）逐字搬过来，HP/ATK/atkSpeed/range/moveSpeed/color 一一对应。**不要篡改数值**（数值是平衡好的）
- [ ] 写 `assets/configs/konglie/placement.json`：照搬 `INITIAL_PLACEMENT`
- [ ] 写 `KongLie3DDataLoader.hpp/cpp`：
  - 定义 `struct FPieceDef { name, team, role, attackType, hp, atk, atkSpeed, range, moveSpeed, maxMana, manaPerAtk, color, isHero, skillW, skillUltimate (string id) }`
  - 定义 `struct FPlacementEntry { pieceId, col, row }`
  - 函数 `LoadPieces(path) -> map<string, FPieceDef>`
  - 函数 `LoadPlacement(path) -> { player[], enemy[], bench[] }`
  - 用项目已有 JSON 库；如果没有，引入 nlohmann/json（更新 vcpkg.json）— **先 grep 现有代码看是否已用 nlohmann 或 rapidjson**
- [ ] 写 `KongLie3DPiece.hpp`：
  - `struct FPieceRuntime { FPieceDef def; int currentHp; int currentMana; int col; int row; bool alive; bool onBench; std::shared_ptr<Node> node; uint32_t modelId; }`
- [ ] 在 `KongLie3DGameInstance::OnInit` 里：
  - 调 `LoadPieces` + `LoadPlacement`
  - 在 `BeforeSceneRebuild` 里：每个 placement entry → 用 `FProcModel::CreateBox` 生成对应 role 尺寸的 box → 创建 Material（Lambertian + JSON color）→ `Node::CreateNode` 放到对应世界坐标 `(col, height/2, row)` → AddNode
  - 板凳席摆在 `row=8` 后面一排（`z = 8.5`，`x = 0,1,2`），缩放 0.82
- [ ] HERO 单位顶部加一个小 sphere（额外 ProcModel + 子 Node 或 child component）

### 涉及文件
- 新建：`assets/configs/konglie/pieces.json`、`placement.json`、`src/Application/KongLie3D/KongLie3DDataLoader.{hpp,cpp}`、`KongLie3DPiece.hpp`
- 改：`KongLie3DGameInstance.cpp`
- 可能改：`vcpkg.json`（若需引入 nlohmann/json）

### 验收方法
1. 编译通过
2. 启动后看到棋盘上 12 个有色 box：玩家方下半 6 个，敌方上半 6 个，板凳 3 个在棋盘外
3. 颜色与 web 原型基本一致（蓝色 Blue、红色 Sydney、黄色 ADC、紫色 Shadow 系列）
4. HERO 头顶有 sphere
5. 关掉 ImGui 窗口看场景纯净度

### 注意
- JSON 缺字段时**不要**默默给默认值；spdlog ERROR 然后 abort，避免数据错误掩盖
- 颜色 JSON 里写 `[r,g,b]` float 0-1，**不**写 hex（程序员调试时直观）
- 不要在 M2 加 HP/MP 条 UI，留给 M4

---

## M3. 战斗 tick + AI + 伤害结算

**优先级**: P0  **工时**: ~1.5h  **依赖**: M2

### 背景
让单位动起来打起来。完成后按某个键（如 SPACE）开战，看到双方互相靠近、攻击、掉血、死亡，最后一方全灭分胜负。本任务是项目最核心的逻辑层。

### TODO
- [ ] 写 `KongLie3DBattleSystem.hpp/cpp`：
  - 状态：`enum EBattleState { Deployment, Battle, Ended }`
  - 累加器 `tickAccumulatorMs_`，每帧 `OnTick(deltaSeconds)` 累加，超过 100ms 触发一次 `Tick()`
  - `Tick()` 内：
    - 对每个 alive 单位：调 `_findNearestEnemy(piece)` 用切比雪夫距离
    - 若距离 ≤ `range`：发动普攻 `_attack(piece, target)`：扣 HP（考虑 `dmgMult`），加 `manaPerAtk`，记录 stat
    - 否则：朝目标方向走一步（col/row 各靠近 1，移动消耗时间 = `100ms / moveSpeed`，简化版直接每 tick 走一格）
    - 单位死亡：`alive=false`，`node->SetVisible(false)`（不用真删除，避免 Scene 重建）
  - 攻击 cooldown：每个单位维护 `attackCooldownMs_`，攻击后置 `1000 / atkSpeed`，每 tick 减 100
  - 治疗者（role=support）：每 `healInterval` ms 给最低血队友加 `healAmount` HP
  - 胜负判定：每 tick 末尾，一方 alive 数 = 0 → 切到 `Ended`，记录胜方
- [ ] 在 `KongLie3DGameInstance::OnKey` 里：按 SPACE 时若 `state == Deployment`，调 `BattleSystem::Start()` → 切到 `Battle`
- [ ] 在 `OnTick` 里把 deltaSeconds 喂给 BattleSystem
- [ ] 单位移动：每 tick 重算世界坐标，写到 `node->SetTransform(...)`；用线性插值（在 `OnTick` 内基于 `lerpProgress = elapsedMsSinceMove / moveDurationMs` 在 `prevWorldPos` 与 `targetWorldPos` 之间插）让视觉不抖动

### 涉及文件
- 新建：`src/Application/KongLie3D/KongLie3DBattleSystem.{hpp,cpp}`
- 改：`KongLie3DGameInstance.cpp`、`KongLie3DPiece.hpp`（加运行时字段）

### 验收方法
1. 编译通过
2. 启动后按 SPACE 开战
3. 双方单位向中间靠拢，ADC 站住打远程，坦克近战
4. 单位 HP 归零后消失（不可见）
5. 一方全灭后战斗停止，spdlog 输出胜方
6. 整局战斗约 18-25 秒结束（与 web 版基本对齐）

### 注意
- `_findNearestEnemy` 用简单 O(n²) 即可，单位数 < 20，不需要空间索引
- 移动时**不要**碰撞检测（web 版也没有），允许重叠走过
- 攻击伤害公式 MVP 期：`damage = atk * dmgMult`，`dmgMult` 默认 1.0（加时赛在 M7 才改）
- 不实现 stun / shield 字段，留给 M6（W 技能里用）
- **不要**在 M3 加投射物视觉，先纯逻辑+变色闪一下表示攻击即可（用 ImGui draw list 画攻击线）

---

## M4. ImGui HUD + 英雄面板

**优先级**: P0  **工时**: ~1h  **依赖**: M3

### 背景
让玩家看到比分。完成后屏幕上有：左侧两位英雄面板（HP/MP 条 + W/R 状态）、顶部计时器、右侧"开始战斗/暂停"按钮、底部数据统计。

### TODO
- [ ] 写 `KongLie3DUI.hpp/cpp`，函数 `RenderHUD(GameInstance&)`，被 `OnRenderUI` 调用
- [ ] HP 条：每个 alive 单位上方画 ImGui foreground draw list 矩形（绿/黄/红渐变），世界坐标投到屏幕：复用引擎现有相机的 ViewProj 矩阵
- [ ] 左侧英雄面板（窗口固定 `pos=(8, 60), size=(180, 420)`）：
  - 每位英雄一个 group：彩色头像方框 + 名字 + HP 条 + MP 条 + W 技能描述 + R 按钮
  - R 按钮三态：
    - 就绪（mana 满 + cooldown=0 + alive 上场）：黄色背景，可点击
    - 已释放（`ultimateUsed=true`）：灰色 `"✓ 已释放"`
    - 阵亡（`alive=false`）：暗红 `"✗ 英雄阵亡"`
  - R 按钮 OnClick：`battleSystem.RequestUltimate(heroId)`（M6 实现真正效果，M4 只 spdlog 一行）
- [ ] 顶部计时器（窗口固定 `pos=((W-200)/2, 8), size=(200, 36)`）：
  - 战斗中：`elapsed = battleSystem.elapsedMs / 1000`，显示 `"剩余 {N} 秒"`（30 - elapsed），低于 5 秒红色
- [ ] 右侧（窗口固定 `pos=(W-180, 60), size=(170, 100)`）：
  - 计数 `"我方: {N}/6 敌方: {N}/6"`
  - "开始战斗"按钮（`Deployment` 时显示）/"暂停/继续"按钮（`Battle` 时显示）
- [ ] 底部数据统计（窗口固定 `pos=(8, H-160), size=(W-16, 150)`）：
  - 两栏 tab："我方"/"敌方"
  - 每行：name | AD伤 | AP伤 | 承伤 | 治疗（按总伤害降序）
  - 每 ~600ms 刷新一次（不需要每帧）
  - 阵亡单位灰显
- [ ] `BattleSystem` 添加统计字段：`statDmgAD`, `statDmgAP`, `statDmgTaken`, `statHeal`，在 `_attack` / 治疗时累加

### 涉及文件
- 新建：`src/Application/KongLie3D/KongLie3DUI.{hpp,cpp}`
- 改：`KongLie3DGameInstance.cpp`、`KongLie3DBattleSystem.{hpp,cpp}`

### 验收方法
1. 编译通过
2. 启动后看到左侧两个英雄面板，名字/血条/魔法条都正常显示
3. 顶部计时器开战后倒数
4. 战斗中实时看到双方剩余单位数
5. 数据统计面板战斗中持续更新
6. R 按钮点击有 spdlog 日志（实际效果 M6 实现）
7. 单位 HP 条跟随世界位置浮动

### 注意
- HP 条用 foreground draw list（ImGui::GetForegroundDrawList()），不要用 Window — 窗口太多会卡且遮挡场景
- 屏幕坐标投影：用 `engine_->GetCamera().GetViewProjection()` × `vec4(world, 1)`，再 `/w` 拿 NDC，再映射到像素
- 数据统计面板用 ImGui Table API，**不要**手撸 columns
- 字体：暂时用默认字体（中文渲染受限可保留英文 fallback；M7 抛光阶段再考虑加字体）

---

## M5. 部署阶段（拖拽阵型）

**优先级**: P1  **工时**: ~1.5h  **依赖**: M4

### 背景
让玩家在战前调整阵型。完成后能用鼠标拖拽己方单位到任意未占用的玩家区格子，或交换两个己方单位，板凳席单位也能拖上场。开战后拖拽锁定。

### TODO
- [ ] 拾取：`OnMouseButton` 左键按下 → `RayCastGPU` → 命中的 `instanceId` 反查 `FPieceRuntime`
- [ ] 拖拽状态：在 `KongLie3DGameInstance` 加 `draggingPiece_`、`dragStartCol_/Row_`，`OnCursorPosition` 时把 piece node 跟着鼠标射线投到 `y=0` 平面
- [ ] 释放：左键抬起时 → 找鼠标射线在 `y=0` 的格子坐标 (`col, row`) → 校验：
  - 必须在 row 4-7（玩家区）或板凳区
  - 若该格已有玩家单位 → 交换两者位置
  - 若空格 → 单位放过去
  - 若是从板凳席拖到棋盘：检查上场数量 ≤ 敌方上场数（参考 web 版 `_canAddToBoard`），超出则弹回
- [ ] 高亮：拖拽中，所有合法目标格用半透明白色矩形高亮（在 `OnRenderUI` 用 foreground draw list 投影绘制）
- [ ] 状态锁：`battleSystem.state != Deployment` 时禁用拖拽

### 涉及文件
- 改：`KongLie3DGameInstance.{hpp,cpp}`、`KongLie3DUI.cpp`（高亮绘制）

### 验收方法
1. 编译通过
2. 部署阶段（开战前）能拖拽任一己方单位到玩家区任意空格
3. 拖到已有己方单位的格子 → 两个交换
4. 拖板凳上场：若已经 6 个上场满 → 不允许；否则上场，板凳留空
5. 拖到敌方区 / 棋盘外 → 弹回原位
6. 按 SPACE 开战后再点单位 → 不进入拖拽
7. 拖拽中目标格子有视觉高亮

### 注意
- `RayCastGPU` 是异步回调，要小心拖拽逻辑的时序：建议第一次按下时立即 raycast 拿到 piece，后续用屏幕→y=0 平面解析的简单方式更新位置（不重复 raycast）
- 屏幕到 y=0 平面：射线方程 `O + t*D`，求 `O.y + t*D.y = 0` → `t = -O.y / D.y`
- 板凳席的世界坐标在棋盘外（`row=8, x=0..2`），写一个统一的 `IsInPlayerZone(col,row)` 谓词处理两类
- **不要**让玩家拖动敌方单位（虽然原型也禁用，但要明确）
- **不要**使用 ImGui 的 drag-drop API — 它是 UI 层面的，与世界 3D 拖动不匹配

---

## M6. 技能 W 自动 + R 手动大招

**优先级**: P1  **工时**: ~1h  **依赖**: M4

### 背景
完成英雄技能闭环。本任务做 4 个技能（每位英雄各 1 W + 1 R），加上简单视觉特效。

### TODO
- [ ] 写 `KongLie3DSkills.hpp/cpp`，函数 `TryCastW(piece, battleSys)`、`CastUltimate(piece, battleSys)`，在 `BattleSystem::Tick` 末尾对每个 hero 检查 mana 满 + cooldown=0 → 调 W
- [ ] **W: magic_shield**（Blue）：
  - 给自己和最近队友加 `shield += 150`
  - 在 piece 受伤时优先扣 shield（修改 `KongLie3DBattleSystem::_attack` 的伤害结算，用 `int FPieceRuntime::ApplyDamage(int damage)`，先扣 shield 再扣 hp）
  - 持续 3000ms（用 `shieldTimerMs_`，到时清零）
  - 视觉：piece 周围画蓝色光环（foreground draw list 投影后画圆环）
- [ ] **W: rapid_slash**（Sydney）：
  - 找最远敌人（最大切比雪夫距离）
  - 移动到敌人相邻空格（参考 web 版 `_adjacentTo`）
  - 立即对该敌人造成 150 伤害 + 晕眩 500ms（`stunTimerMs_`，stun 期间不攻击不移动不积蓝）
  - 视觉：在出发点到目标点画一条橙色快速移动的 box（用临时 procmodel 或 ImGui 屏幕线）
- [ ] **R: blue_surge**（Blue）：
  - 自身 3 格内（切比雪夫）所有敌人受 160 AP 伤害
  - `ultimateUsed = true`
  - 视觉：以 Blue 为中心扩张的蓝色环（用 ImGui circle stroke，半径从 0 到 3 个格子距离的屏幕值，0.4s）
- [ ] **R: sydney_fury**（Sydney）：
  - 自身进入 fury 模式 4000ms：`atkSpeed *= 3`、`dmgMult *= 1.5`
  - 视觉：橙色脉冲光环（与 W 类似但颜色和动画不同）
- [ ] R 大招触发口：M4 已加 R 按钮 OnClick → 调 `BattleSystem::RequestUltimate(heroId)` → 推到一个 pending queue，下个 tick 调 `CastUltimate`
- [ ] mana 满后**自动** W（无需玩家点击），cooldown 内即使再满也不触发

### 涉及文件
- 新建：`src/Application/KongLie3D/KongLie3DSkills.{hpp,cpp}`
- 改：`KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DPiece.hpp`（加 shield/stun/fury timer）、`KongLie3DUI.cpp`（特效绘制）

### 验收方法
1. 编译通过
2. 战斗开始约 5-10 秒后 Blue 满蓝自动放 W，最近队友头上短暂蓝光环
3. Blue 受伤前 3 秒内 shield 抵消伤害，可在 spdlog 看到 `[shield absorbed N]`
4. Sydney 满蓝自动放急袭斩，瞬间冲到最远敌人旁边并造成 150 伤害
5. 点击 Blue R 按钮：3 格内敌人血量明显下降（每个 -160）+ 蓝色扩张环
6. 点击 Sydney R 按钮：之后 4 秒内 Sydney 攻击频率明显加快
7. R 释放过一次后按钮变 `"✓ 已释放"`，再点不响应

### 注意
- shield/stun/fury 字段要在 M3 的 piece runtime 加好；M3 任务没加的话本任务先补
- 视觉特效**不要**做粒子系统 — 用 ImGui foreground draw list 画几何形状即可（圆环/线/扩张圆），项目要求"用最少代码做最大效果"
- **不要**为每个技能新建 Node，所有特效是 UI 层叠加
- 4 个技能写在同一个文件里 OK，每个一个 case 函数；数值**严格**对齐 web 版（150 / 160 / 0.5s / 4s / 3 cells）

---

## M7. 加时赛 + 结算 + 抛光

**优先级**: P1  **工时**: ~1h  **依赖**: M3, M4

### 背景
完成完整一局闭环（开战 → 加时 → 结算 → 重来）。

### TODO
- [ ] **加时赛**（在 BattleSystem 加）：
  - 30 秒（`BATTLE_LIMIT_MS = 30000`）触发 `overtimeActive_ = true`，记录 `overtimeStartMs_`
  - `dmgMult = clamp(1.0 + (overtimeElapsedSec * 0.1), 1.0, 1.7)`
  - `healMult = clamp(1.0 - (overtimeElapsedSec * 0.1), 0.3, 1.0)`（治疗削弱）
  - 45 秒（`OVERTIME_DRAW_MS = 45000`）后强制 `state = Ended`，结果 `Draw`
- [ ] 视觉：加时期间屏幕边缘红色脉冲边框（ImGui foreground draw list 画粗线条带正弦透明度）+ 屏幕中央动画文本 `"⚡ 加 时 ⚡"`（淡入淡出 2 秒后消失）
- [ ] **结算 UI**（`state == Ended` 时显示）：
  - ImGui 居中半透明 modal（不要新场景，复用现有窗口）
  - 卡片色：胜=金边深蓝、负=红边深红、平=黄边深黄
  - 标题：`"战斗胜利"` / `"战斗失败"` / `"超时平局"`
  - 副信息：`"全歼敌方 · 存活 N/6 · 耗时 M:SS"` / `"加时赛结束未分胜负 · 耗时 M:SS"` / `"全军覆没 · 耗时 M:SS"`
  - 两个按钮：`"重来一局"` / `"回主菜单"`（MVP 期两个都 reload，等同 web 版）
- [ ] **重来**：把 `BattleSystem` 重置（HP/MP 满血、回原位、stat 清零、ultimateUsed=false、overtime=false、state=Deployment），不重启进程。**不要重建 Scene**（开销大），只 reset 各 piece 字段并 `node->SetVisible(true)`
- [ ] **抛光**：
  - 攻击时给目标方向画一条简短闪线（ImGui 屏幕线，0.1 秒）— ADC=橙色，AP=蓝色
  - 伤害浮字：在受伤位置上方显示 `-N` 红色文本，0.6 秒内向上飘升+淡出
  - 死亡时单位 box 颜色变暗（material color * 0.4）+ y 下沉 0.3，0.5 秒后 SetVisible(false)

### 涉及文件
- 改：`KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DUI.{hpp,cpp}`、`KongLie3DGameInstance.cpp`

### 验收方法
1. 编译通过
2. 一局战斗在 30 秒前结束 → 显示胜利或失败结算
3. 一局战斗持续到 30 秒 → 屏幕红色边框脉冲，飘字 `"加时"`，伤害明显增加
4. 持续到 45 秒 → 显示平局结算
5. 点击"重来一局" → 所有单位回到初始位置满血，state 切回 Deployment
6. 攻击时能看到方向线和飘字
7. 单位死亡有下沉动画

### 注意
- 加时倍率每秒重算一次即可（不要每 tick 算）
- 浮字用 `std::vector<FFloatingText>` 队列管理，每个有 `worldPos / text / color / lifeMs`，UI 层每帧遍历投影绘制
- 重来时 piece 的 col/row 要从 placement.json 重读，**不要**只缓存上次开战前的位置（玩家可能拖拽过）— 应缓存 `lastDeploymentSnapshot`
- **不要**把结算 UI 做成新 ImGui Window — 用 BeginPopupModal 或 SetNextWindowPos 居中绘制即可

---

## M8.（可选）圣物系统 + 调优

**优先级**: P2  **工时**: ~1h  **依赖**: M7

### 背景
锦上添花。5 种圣物 buff 让玩家在战前选 1，应用全队属性加成。

### TODO
- [ ] 把 `RELICS` 数组从 `pieceData.js` 搬到 `assets/configs/konglie/relics.json`
- [ ] Deployment 阶段右下角显示 5 个圣物按钮（图标 + 名字 + buff 描述）
- [ ] 选中后：
  - `atkBonus`/`hpBonus`/`apBonus`/`spdBonus`/`cdBonus` 应用到所有玩家单位（开战时一次性应用）
  - 当前选中圣物名字显示在英雄面板上方
- [ ] 结算 UI 显示 `"携带圣物：{name}"`
- [ ] 重来时保留圣物选择（不重置）

### 涉及文件
- 新建：`assets/configs/konglie/relics.json`
- 改：`KongLie3DDataLoader.{hpp,cpp}`、`KongLie3DBattleSystem.{hpp,cpp}`、`KongLie3DUI.cpp`

### 验收方法
1. 编译通过
2. 部署阶段右下角看到 5 个圣物
3. 点击其中一个 → 高亮，重启战斗后玩家方属性按 buff 加成（如选破军之戒，AD 单位攻击力 +22%，战斗时间明显缩短）
4. 结算 UI 显示圣物名

### 注意
- 圣物 buff **只**作用于玩家方
- 5 种 buff 的字段对应：`statKey="atkBonus"` → 加在所有 attackType=ad 的 atk 上；`apBonus` → 加在 ap 单位 atk 上；`spdBonus` → 加 atkSpeed；`hpBonus` → 加 maxHp（按比例）；`cdBonus` → 减 W cooldown
- 一次只能选一个圣物

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target KongLie3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀 |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 平台 | 用 `PlatformCommon.h`，不直接 include 平台头 |
| Vulkan | 所有 VkResult 用 `VK_CHECK_RESULT`；RAII 资源管理 |
| 注释 | 默认不写注释，仅写非显然的 WHY |
| 提交 | 不要执行 git commit；只完成代码改动，由用户决定何时提交 |

**禁止**：
- 修改 `src/ThirdParty/` 或 `external/`
- 引入新大型依赖（Bullet、Recast 等）；MVP 期间所有逻辑用 stdlib
- 在任务卡范围之外做"顺手清理"
- 把代码写到注释里 — 删掉的代码就是删掉，不留 `// removed`

## 验证完整端到端

完成 M1-M7 后，端到端跑一遍：

1. `./build.bat --preset full-windows --reconfigure` 通过，无 warning regression
2. `./run.bat --preset full-windows --target KongLie3D` 启动
3. 进入部署阶段：12 个单位摆好阵型，左侧英雄面板显示
4. 拖拽 ADC 到中路 → 位置改变
5. 按 SPACE 开战 → 单位移动、攻击、加蓝、HP 下降
6. 满蓝自动 W：Blue 给队友护盾、Sydney 冲锋
7. 点击 R 按钮：Blue 蓝色洪流 / Sydney 狂暴
8. 战斗在 ~20 秒内结束 → 结算 UI 显示
9. 点重来 → 状态完全重置，可再来一局
10. （故意送掉单位拖到 30s 后）→ 加时赛红边脉冲 → 45s 平局结算

**不需要**：单元测试（这是游戏 demo，行为通过手玩验证更直接）、视觉测试（KongLie3D 不进 visual_test.json，因为它是交互式的）。

## 风险与备注

| 风险 | 应对 |
|---|---|
| JSON 库未引入 | 第一步 grep `vcpkg.json` 与 `Tests/`，看是否已用 nlohmann/rapidjson；都没的话引入 nlohmann/json（header-only，最小成本） |
| 摄像机 API 不熟 | 先看 [`gkNextRenderer.cpp`](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) 中 `OverrideRenderCamera` 的实例；不行则用 ModelViewController 的 setter 强制位置 |
| Scene 重建开销大 | 重来一局**不**重建 Scene；piece node 的 visible/transform/material color 字段直接 reset |
| GPU Raycast 异步 | 拖拽改用屏幕→y=0 平面解析（同步），仅按下选中那一刻用 raycast |
| 中文字体 | M1-M6 用英文文本；M7 抛光阶段若有空再考虑 ImGui::AddFontFromFile + 字符范围 |

## 后续 agent 调用建议

每个任务（M1, M2, ...）适合用一个独立 agent 调用执行，prompt 模板：

```
请执行 docs/projects/konglie-3d/plan.md 中的 M{N} 任务。
- 严格按 TODO 清单做，不要扩大范围
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报
- 不要 commit
```

完成 M7 后整体复盘是否需要 M8。
