# 大航海 Voyage 3D 复刻 — MVP 开发计划

## Context

参考光荣《大航海时代 IV》（跑商 + 探索 + 海战 三大循环），在 gkNextRenderer 引擎中新建一个 Application（`Voyage3D`），用引擎现有能力（Vulkan 渲染、ImGui、ProcModel、Node + RenderComponent、ImGui HUD）做一个**俯视 3D 复刻 MVP**。

**大航海时代 IV 核心玩法**（保留项）：
- **航海**：俯视海图视角，玩家船自由航行，靠近港口可进港
- **跑商**：港口间买卖商品，价格随港口"高产/高需"差异化，玩家靠差价赚钱
- **海战**：遭遇敌船时进入战斗状态，**侧舷炮战**（必须把船舷对准敌人才能开火，《大航海》经典手感）
- **探索**：海上随机事件（海盗、沉船、风暴、漂流瓶），已访问港口标记
- **升级**：港口造船厂买更大的船（货舱大、HP 高、炮位多）
- **闭环**：出港 → 航海（事件/战斗）→ 入港 → 贸易/升级 → 出港

**MVP 砍掉的项**（避免范围爆炸）：
- 多艘船舰队 / 副船管理（仅 1 艘旗舰）
- 船员系统（船长 / 航海士 / 炮手 等职业）
- 风向 / 洋流 / 季节 / 气候
- 国家关系 / 战争 / 悬赏 / 爵位
- 任务委托系统（运 X 货物到 Y 港换报酬）
- 大西洋扩展（探险家路线）
- 造船定制（船体 / 风帆 / 武装可分别选择）
- 配音、剧情过场、多语言

**用户决策**（已确认）：
- 范围：MVP 核心循环（M1–M7 必做，M8 可选）
- 视觉：**ProcModel 简单几何体**（box / sphere 组合，对齐 Brotato3D / KongLie3D 风格），不依赖美术资产
- 文档：输出到 `docs/projects/voyage-3d/plan.md`
- 数据：**JSON 配置**（港口 / 商品 / 船型 / 事件），便于调参不重编译
- 命名：项目代号 `Voyage3D`（对齐 Brotato3D / KongLie3D 命名风格）
- 沙盒：地中海（闭合海域，10 个真实港口，玩家熟悉度高）

**为什么这样设计**：MVP 优先验证「俯视相机 + 大范围漫游 + 港口/海战状态机切换 + 经济模拟」在引擎里能跑通；几何体方案不依赖美术资产，把后续 agent 的开发卡点降到最小；地中海闭合海域天然给玩家"边界"，省去开放世界设计成本。每张任务卡都设计成「单 agent 单次会话能完成」的颗粒。

## 引擎可复用能力清单（不新造轮子）

| 需求 | 复用 | 文件路径 |
|---|---|---|
| Application 入口 | `NextGameInstanceBase` | [src/Runtime/Engine.hpp:41](../../../src/Runtime/Engine.hpp) |
| 输入回调 | `OnKey/OnMouseButton/OnCursorPosition` | [src/Runtime/Engine.hpp:74](../../../src/Runtime/Engine.hpp) |
| 程序化几何体 | `Assets::FProcModel::CreateBox/CreateSphere` | [src/Assets/Loaders/FProcModel.h:12](../../../src/Assets/Loaders/FProcModel.h) |
| 场景动态构建 | `BeforeSceneRebuild` + `Scene::AddNode` | [src/Application/Brotato3D/Brotato3DGameInstance.cpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.cpp) |
| 节点 + 组件 | `Node::CreateNode` + `RenderComponent` | [src/Assets/Core/Node.h](../../../src/Assets/Core/Node.h), [src/Runtime/Components/RenderComponent.h](../../../src/Runtime/Components/RenderComponent.h) |
| 摄像机覆盖 | `OverrideRenderCamera` 钩子 | [src/Application/Brotato3D/Brotato3DGameInstance.cpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.cpp) |
| ImGui HUD | `OnRenderUI/OnInitUI` 虚函数 | [src/Runtime/Engine.hpp:49](../../../src/Runtime/Engine.hpp) |
| 屏幕震动 / 飘字 / 闪光复用 | Brotato3D 同名工具函数（FFloatingText / FMuzzleFlash / screenShake） | [src/Application/Brotato3D/Brotato3DGameInstance.hpp:35-51](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp) |
| JSON 解析 | `nlohmann-json`（已在 vcpkg.json 中） | [vcpkg.json:38](../../../vcpkg.json) |
| 音频接口 | `engine->PlaySound(path, looping, volume)` | [src/Application/Brotato3D/Brotato3DAudio.hpp](../../../src/Application/Brotato3D/Brotato3DAudio.hpp) |
| CMake 注册套路 | 参考 Brotato3D | [src/cmake/SourceFiles.cmake](../../../src/cmake/SourceFiles.cmake), [src/CMakeLists.txt:131](../../../src/CMakeLists.txt) |
| 启动 | `run.bat --target Voyage3D` | [scripts/run.ps1](../../../scripts/run.ps1) |

**不需要的**：
- 物理（PhysicsComponent 不挂；船与陆地用简单 AABB 检测，炮弹用直线运动）
- QuickJS（数据走 JSON 即可）
- glTF 加载（纯 procmodel）
- 动画系统（船只移动用插值，炮口闪光用屏幕特效）
- 粒子系统（爆炸/水花用 ImGui foreground 圆环 + 临时 Node）
- 真实地理 GeoJSON（用代码硬编码 5-7 个大 box 拼地中海陆地块即可）

## 文件结构（最终态）

```
src/Application/Voyage3D/
├── Voyage3DGameInstance.hpp/cpp     # 主入口，状态机，继承 NextGameInstanceBase
├── Voyage3DCommon.hpp               # 共享类型（FShipRuntime / FCargoSlot / EAppState 等）
├── Voyage3DWorldMap.hpp/cpp         # 地中海陆地块构建 + 经纬度↔世界坐标转换
├── Voyage3DPort.hpp/cpp             # 港口运行时（图标 / 价格表 / 库存）
├── Voyage3DShip.hpp/cpp             # 玩家船 + 敌船运行时（HP / 速度 / 货舱 / 炮位）
├── Voyage3DSailing.hpp/cpp          # 航海控制（WASD / 转向 / 进港检测）
├── Voyage3DCombat.hpp/cpp           # 海战系统（炮弹 / 侧舷判定 / AI）
├── Voyage3DTrade.hpp/cpp            # 贸易系统（价格公式 / 买卖逻辑）
├── Voyage3DEvent.hpp/cpp            # 海上随机事件 (roll + 应用)
├── Voyage3DUI.hpp/cpp               # 全部 ImGui HUD（主菜单/HUD/港口/贸易/造船/海战/结算）
└── Voyage3DDataLoader.hpp/cpp       # JSON 加载 ports/goods/ships/events

assets/configs/voyage3d/
├── ports.json           # 10 个地中海港口（经纬度 / 国家 / 特产）
├── goods.json           # 6 种商品（基础价 / 高产地 / 高需地）
├── ships.json           # 3 种船型（HP / 货舱 / 炮位 / 速度 / 价格）
├── events.json          # 5 类海上事件（权重 / 效果）
└── landmass.json        # 地中海陆地块（box 列表，参考 § 视觉/坐标约定）

docs/projects/voyage-3d/
└── plan.md              # 本文档
```

CMake 修改（仅 2 处，照抄 Brotato3D 套路）：
- [src/cmake/SourceFiles.cmake](../../../src/cmake/SourceFiles.cmake) 加 `src_files_voyage3d` GLOB
- [src/CMakeLists.txt](../../../src/CMakeLists.txt) 加 `add_executable(Voyage3D ...)` + 把 `Voyage3D` 加入 `AllTargets` 列表（MINGW 与非 MINGW 两份）

## JSON Schema 设计

**通用约定**：颜色 `[r,g,b]` float 0–1；坐标用经纬度（lon / lat），运行时由 `WorldMap::GeoToWorld()` 转换；金币单位 `gold`；时间字段以 `Sec` / `Ms` 后缀显式标注。

### ports.json

```json
{
  "ports": [
    { "id": "venice",     "name": "威尼斯",       "lon": 12.34, "lat": 45.43, "nation": "Venice",    "specialty": "glass",     "color": [0.85, 0.20, 0.20] },
    { "id": "genoa",      "name": "热那亚",       "lon":  8.93, "lat": 44.40, "nation": "Genoa",     "specialty": "weapon",    "color": [0.95, 0.95, 0.95] },
    { "id": "marseille",  "name": "马赛",         "lon":  5.36, "lat": 43.29, "nation": "France",    "specialty": "wine",      "color": [0.10, 0.20, 0.70] },
    { "id": "barcelona",  "name": "巴塞罗那",     "lon":  2.17, "lat": 41.38, "nation": "Aragon",    "specialty": "wool",      "color": [0.95, 0.75, 0.10] },
    { "id": "naples",     "name": "那不勒斯",     "lon": 14.27, "lat": 40.85, "nation": "Naples",    "specialty": "wheat",     "color": [0.85, 0.55, 0.10] },
    { "id": "athens",     "name": "雅典",         "lon": 23.72, "lat": 37.98, "nation": "Byzantine", "specialty": "olive_oil", "color": [0.75, 0.80, 0.30] },
    { "id": "istanbul",   "name": "君士坦丁堡",   "lon": 28.97, "lat": 41.01, "nation": "Ottoman",   "specialty": "spice",     "color": [0.10, 0.60, 0.30] },
    { "id": "alexandria", "name": "亚历山大",     "lon": 29.92, "lat": 31.20, "nation": "Egypt",     "specialty": "silk",      "color": [0.90, 0.85, 0.40] },
    { "id": "tunis",      "name": "突尼斯",       "lon": 10.18, "lat": 36.81, "nation": "Hafsid",    "specialty": "ivory",     "color": [0.55, 0.30, 0.10] },
    { "id": "palermo",    "name": "巴勒莫",       "lon": 13.36, "lat": 38.12, "nation": "Sicily",    "specialty": "sulfur",    "color": [0.95, 0.55, 0.20] }
  ]
}
```

### goods.json

```json
{
  "goods": [
    { "id": "wine",      "name": "葡萄酒", "basePrice":  50, "supplyPorts": ["marseille","barcelona"],     "demandPorts": ["istanbul","alexandria"] },
    { "id": "spice",     "name": "香料",   "basePrice": 200, "supplyPorts": ["alexandria","istanbul"],     "demandPorts": ["venice","marseille"] },
    { "id": "silk",      "name": "丝绸",   "basePrice": 300, "supplyPorts": ["alexandria","istanbul"],     "demandPorts": ["venice","genoa"] },
    { "id": "wheat",     "name": "小麦",   "basePrice":  20, "supplyPorts": ["naples","tunis"],            "demandPorts": ["venice","genoa"] },
    { "id": "weapon",    "name": "武器",   "basePrice": 150, "supplyPorts": ["genoa","venice"],            "demandPorts": ["tunis","alexandria"] },
    { "id": "glass",     "name": "玻璃",   "basePrice": 100, "supplyPorts": ["venice"],                    "demandPorts": ["alexandria","istanbul","tunis"] }
  ]
}
```

**价格公式**（每次进港重算）：
```
finalPrice = basePrice × (1 + supplyFactor) × (1 + demandFactor) × randomNoise
supplyFactor  = -0.40 if 在 supplyPorts 中 else 0.0
demandFactor  = +0.60 if 在 demandPorts 中 else 0.0
randomNoise   = uniform[0.85, 1.15]
```

### ships.json

```json
{
  "ships": [
    { "id": "sloop",   "name": "单桅快船", "hp":  60, "cargoMax":  20, "cannonCount": 4,  "speedKnots":  9.0, "price":     0, "size": [1.4, 0.3, 0.5], "sailHeight": 1.4 },
    { "id": "carrack", "name": "卡拉克",   "hp": 120, "cargoMax":  60, "cannonCount": 12, "speedKnots":  7.0, "price":  3000, "size": [2.2, 0.4, 0.8], "sailHeight": 2.0 },
    { "id": "galleon", "name": "盖伦",     "hp": 220, "cargoMax": 120, "cannonCount": 24, "speedKnots":  6.0, "price": 12000, "size": [3.0, 0.5, 1.0], "sailHeight": 2.6 }
  ]
}
```

### events.json

```json
{
  "events": [
    { "id": "calm",    "weight": 70, "effect": "none" },
    { "id": "pirate",  "weight": 15, "effect": "combat",       "enemyShip": "sloop" },
    { "id": "bottle",  "weight":  5, "effect": "intel",        "rewardGold": 0,    "messageKey": "intel_discount" },
    { "id": "wreck",   "weight":  5, "effect": "treasure",     "rewardGold": 200,  "rewardCargoId": "spice", "rewardCargoQty": 5 },
    { "id": "storm",   "weight":  5, "effect": "storm",        "hpDamage": 10,     "speedDebuffMs": 5000 }
  ]
}
```

### landmass.json

地中海陆地用 7 个 box 简化拼接（写在配置里方便后期调形）：

```json
{
  "blocks": [
    { "name": "iberia",        "min": [-30, -1, -50], "max": [-15, 1,  20], "color": [0.55, 0.45, 0.30] },
    { "name": "north_africa",  "min": [-25, -1, -90], "max":  [60, 1, -55], "color": [0.65, 0.55, 0.35] },
    { "name": "italy",         "min": [ -3, -1, -60], "max":  [12, 1,  10], "color": [0.55, 0.45, 0.30] },
    { "name": "balkans",       "min": [ 12, -1, -45], "max":  [30, 1,  20], "color": [0.50, 0.42, 0.28] },
    { "name": "anatolia",      "min": [ 30, -1, -30], "max":  [85, 1,  10], "color": [0.55, 0.45, 0.30] },
    { "name": "levant",        "min": [ 55, -1, -90], "max":  [85, 1, -25], "color": [0.65, 0.55, 0.35] },
    { "name": "sicily",        "min": [  6, -1, -45], "max":  [18, 1, -35], "color": [0.55, 0.45, 0.30] }
  ]
}
```

> **坐标对应关系**（在 `Voyage3DWorldMap` 中实现）：经度 -6° → 世界 X = -30；经度 36° → 世界 X = +85（线性映射）。纬度 30° → 世界 Z = -90；纬度 46° → 世界 Z = +20。整个海域世界坐标范围约 `X ∈ [-30, 85] × Z ∈ [-90, 20]`，海面 `y = 0`。陆地块用上面 box 的 `min/max` 精确写好；后续可手工调整使港口"嵌入"陆地边缘。

## 视觉/坐标约定

- **海面**：一块大 box `min=(-30,-0.05,-90)`，`max=(85,0,20)`，蓝色 Lambertian `[0.10,0.30,0.60]`。`y=0` 平面以下不可见
- **陆地**：landmass.json 中的 7 个棕色 box，`y∈[-1,1]`，比海面略高出 1m
- **港口**（每个 1 组复合 box）：
  - 主塔楼：`box(-0.5, 0, -0.5)~(0.5, 1.6, 0.5)`，颜色 = `port.color`
  - 屋顶：`box(-0.6, 1.6, -0.6)~(0.6, 1.9, 0.6)`，深色 `[0.15,0.10,0.10]`
  - 锚点 sphere：`sphere(center=(0,2.4,0), r=0.25)`，金色 `[0.9,0.7,0.2]`，HERO 风格的"已访问"标记
  - 整体放在该港口的 `WorldMap::GeoToWorld(lon, lat)` 处，y=0
- **玩家船**（按当前 ship.size 和 sailHeight 缩放）：
  - 船身：`box(-sx/2, 0.0, -sz/2)~(+sx/2, +sy, +sz/2)`，棕色 `[0.50, 0.30, 0.15]`
  - 桅杆：`box(-0.06, sy, -0.06)~(+0.06, sy+sailHeight, +0.06)`，深棕 `[0.30, 0.20, 0.10]`
  - 帆：`box(-sx/3, sy+0.3, -0.04)~(+sx/3, sy+sailHeight, +0.04)`，米白 `[0.95, 0.92, 0.85]`
  - 整艘船作为一个父 Node，移动时只更新父 Node 的 transform；旋转用 yaw（绕 Y 轴）
- **敌船（海盗）**：与玩家船同结构，帆改红黑双色 `[0.6, 0.1, 0.1]`，体型固定为 `sloop`
- **炮弹**：`sphere(r=0.12)`，深灰 `[0.15, 0.15, 0.15]`，运行时实例化（池）
- **摄像机**：
  - **航海视角**：眼位 `(playerX, 18, playerZ + 14)`，看向 `(playerX, 0, playerZ)`，斜俯 50° 跟随玩家船
  - **海战视角**：眼位 `(midX, 8, midZ + 10)`，看向 `(midX, 0, midZ)`，midX/Z 是玩家+敌船中点
- **HUD**：金币 / 当前船型 / HP / 当前游戏日期 用 ImGui 顶部固定窗口；港口图标用 foreground draw list 投影标签

## 任务索引（MVP 共 7 个，~9-10 小时）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [M1](#m1-application-骨架--海面--地中海陆地块) | Application 骨架 + 海面 + 地中海陆地块 | ~1h | — |
| [M2](#m2-json-数据加载--港口与玩家船渲染) | JSON 数据加载 + 港口与玩家船渲染 | ~1.5h | M1 |
| [M3](#m3-航海控制--进港交互) | 航海控制 + 进港交互 | ~1.5h | M2 |
| [M4](#m4-贸易系统) | 贸易系统 | ~1.5h | M3 |
| [M5](#m5-海战系统侧舷炮战) | 海战系统（侧舷炮战） | ~2h | M3 |
| [M6](#m6-海上事件--探索) | 海上事件 + 探索 | ~1h | M5 |
| [M7](#m7-造船厂--hud-抛光--结算) | 造船厂 + HUD 抛光 + 结算 | ~1h | M4, M5, M6 |
| [M8](#m8-可选声誉与国籍--经济调优) | （可选）声誉与国籍 + 经济调优 | ~1h | M7 |

---

## M1. Application 骨架 + 海面 + 地中海陆地块

**优先级**: P0  **工时**: ~1h

### 背景

打通编译/启动链。新建 `Voyage3D` 子项目，参考 [Brotato3D](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp) 的最小模板。本任务后能 `run.bat --target Voyage3D` 启起来，看到地中海海面 + 7 个棕色陆地块、俯视斜角摄像机即算成功。

### TODO

- [ ] 创建目录 `src/Application/Voyage3D/`
- [ ] 写 `Voyage3DCommon.hpp`：定义 `enum class EAppState { MainMenu, NewGame, Sailing, NavalCombat, InPort, Trading, ShipUpgrade, Tavern, Paused, Result }`，声明 `FShipRuntime` 占位 struct（M2 完善）
- [ ] 写 `Voyage3DGameInstance.hpp/cpp`：继承 `NextGameInstanceBase`，声明 `OnInit/OnTick/OnDestroy/OnRenderUI/OnKey/OnMouseButton/OnCursorPosition/BeforeSceneRebuild/OverrideRenderCamera`，构造函数把窗口 title 设为 `"Voyage3D"`，1280×720
- [ ] 写 `Voyage3DWorldMap.hpp/cpp`：
  - `glm::vec3 GeoToWorld(float lon, float lat)`：经度 `-6 ~ 36` → X `-30 ~ 85`；纬度 `30 ~ 46` → Z `-90 ~ 20`（线性映射，不用墨卡托）
  - `BuildOcean(std::vector<Model>&, std::vector<FMaterial>&, std::vector<shared_ptr<Node>>&)`：用 `FProcModel::CreateBox` 生成大蓝海面
  - `BuildLandmass(...)`：从 `assets/configs/voyage3d/landmass.json` 读取 7 个 box，分别生成棕色陆地块 Node
- [ ] 在 `BeforeSceneRebuild` 钩子里调用 `BuildOcean` + `BuildLandmass`
- [ ] 在 `OverrideRenderCamera` 里返回固定俯视摄像机：眼位 `(20, 25, 0)`，看向 `(20, 0, -30)`（默认看着地中海中心，M3 改为跟随玩家船）
- [ ] `OnRenderUI`：画一个最小 ImGui 窗口 `"Voyage3D MVP"`，里面 `ImGui::Text("Phase 1: bootstrap OK")`
- [ ] CMake 注册：
  - `src/cmake/SourceFiles.cmake`：加 `file(GLOB_RECURSE src_files_voyage3d "Application/Voyage3D/*.cpp" "Application/Voyage3D/*.hpp")`
  - `src/CMakeLists.txt`：加 `add_executable(Voyage3D ${src_files_voyage3d} DesktopMain.cpp)`，把同样的 `target_link_libraries` / `set_target_properties` 复制 Brotato3D 那段；把 `Voyage3D` 加入 `AllTargets` 列表（MINGW 与非 MINGW 两份）
- [ ] 创建 `assets/configs/voyage3d/landmass.json`（内容见 § JSON Schema 设计）

### 涉及文件

- 新建：`src/Application/Voyage3D/Voyage3DCommon.hpp`、`Voyage3DGameInstance.{hpp,cpp}`、`Voyage3DWorldMap.{hpp,cpp}`、`assets/configs/voyage3d/landmass.json`
- 改：`src/cmake/SourceFiles.cmake`、`src/CMakeLists.txt`

### 验收方法

1. `./build.bat --preset full-windows --reconfigure` 通过
2. `./run.bat --preset full-windows --target Voyage3D` 成功启动
3. 日志出现 `uploaded scene [...] to gpu`
4. 屏幕上能看到一片蓝色海面 + 7 个棕色陆地块构成的"近似地中海"轮廓（南欧 / 北非 / 安纳托利亚 / 黎凡特 / 西西里）
5. ImGui 窗口显示 `"Phase 1: bootstrap OK"`

### 注意

- **不要**在 M1 加港口 / 玩家船 / 输入逻辑（M2-M3 才做）
- **不要**自己实现 ModelViewController；M1 用 `OverrideRenderCamera` 返回硬编码相机即可
- 经纬度→世界坐标用线性映射，**不要**写墨卡托投影（MVP 不需要正确性，需要简单）
- 陆地 box 的 `y` 范围 `-1 ~ 1`，让它"露出海面 1m"，海面 `y` 顶面是 0
- landmass.json 加载用项目已有的 nlohmann/json（[vcpkg.json:38](../../../vcpkg.json) 已配）

---

## M2. JSON 数据加载 + 港口与玩家船渲染

**优先级**: P0  **工时**: ~1.5h  **依赖**: M1

### 背景

让港口和玩家船从 JSON 数据驱动地渲染到海图上。完成后能看到 10 个有色港口塔楼按经纬度精确摆放在陆地边缘，以及一艘玩家船浮在海面上（不动、不被控制）。

### TODO

- [ ] 写 `assets/configs/voyage3d/ports.json`、`goods.json`、`ships.json`（内容见 § JSON Schema 设计），逐字搬完
- [ ] 写 `Voyage3DDataLoader.hpp/cpp`：
  - 定义 `struct FPortDef { id, name, lon, lat, nation, specialtyId, color }`
  - 定义 `struct FGoodsDef { id, name, basePrice, supplyPorts, demandPorts }`
  - 定义 `struct FShipDef { id, name, hp, cargoMax, cannonCount, speedKnots, price, size, sailHeight }`
  - 定义 `struct FLandmassBlock { name, min, max, color }`
  - 函数 `LoadPorts(path) -> std::vector<FPortDef>`
  - 函数 `LoadGoods(path) -> std::vector<FGoodsDef>`
  - 函数 `LoadShips(path) -> std::vector<FShipDef>`
  - 函数 `LoadLandmass(path) -> std::vector<FLandmassBlock>`
  - **JSON 缺字段时 spdlog ERROR 然后 abort，不要默默给默认值**
- [ ] 写 `Voyage3DPort.hpp`：
  - `struct FPortRuntime { FPortDef def; std::shared_ptr<Node> node; std::map<std::string,int> currentPrices; std::map<std::string,int> stock; bool visited; }`
- [ ] 写 `Voyage3DShip.hpp`：
  - `struct FShipRuntime { FShipDef def; int currentHp; std::map<std::string,int> cargo; int cargoUsed; glm::vec3 worldPos; float yaw; std::shared_ptr<Node> hullNode; std::shared_ptr<Node> mastNode; std::shared_ptr<Node> sailNode; }`
- [ ] 在 `Voyage3DGameInstance::OnInit` 里：
  - 调 `LoadPorts/LoadGoods/LoadShips`，结果存到 GameInstance 成员
  - 玩家初始：`gold = 1000`，`currentShipId = "sloop"`，构造 `playerShip_`，初始位置 = 威尼斯港的世界坐标 + (0, 0, +5)（外海）
- [ ] 在 `BeforeSceneRebuild` 里：
  - 海面 / 陆地（M1 已有）
  - 每个 port → 用 `FProcModel::CreateBox`/`CreateSphere` 拼出主塔楼 + 屋顶 + 锚点 sphere（共 3 个 procmodel），3 个 Node 作为同一父 Node 的 children；父 Node 放在 `GeoToWorld(port.lon, port.lat)`
  - 玩家船：船身 + 桅杆 + 帆共 3 个 procmodel，3 个 Node 作为 `playerShip_` 父 Node 的 children；父 Node 放到 `playerShip_.worldPos`
- [ ] 玩家船的"船头朝向"：默认 yaw=0 时船头朝 +X，让 `boxsize.x` 是船的"长度"

### 涉及文件

- 新建：`assets/configs/voyage3d/ports.json`、`goods.json`、`ships.json`、`src/Application/Voyage3D/Voyage3DDataLoader.{hpp,cpp}`、`Voyage3DPort.hpp`、`Voyage3DShip.hpp`
- 改：`Voyage3DGameInstance.cpp`、`Voyage3DCommon.hpp`（完善 FShipRuntime forward decl 用法）

### 验收方法

1. 编译通过
2. 启动后地中海上能看到 10 个港口塔楼按经纬度精确分布（威尼斯靠北、亚历山大靠南、君士坦丁堡靠东、巴塞罗那靠西、塞浦路斯/克里特位置可被任一港口替代）
3. 港口颜色按各国 color 区分（威尼斯红、马赛蓝、君士坦丁堡绿…）
4. 一艘棕色船身 + 米白帆的玩家船浮在威尼斯外海（暂时不动）
5. 关掉 ImGui 窗口看场景纯净度

### 注意

- 港口的复合 box 要小心 child node 的相对坐标（child 应在父节点局部空间内，父节点统一摆到世界坐标）
- 港口塔楼的 y 起始位置 = 0（顶在海面上），不是 -1（避免半埋海里看不见）
- **不要**在 M2 加港口图标 ImGui 标签（M3 抛光时再加）
- 颜色 JSON 里写 `[r,g,b]` float 0-1，**不**写 hex
- 玩家船的初始位置应该在威尼斯港**外海**（北面 +Z 方向 5m），避免船重叠在港口塔楼里

---

## M3. 航海控制 + 进港交互

**优先级**: P0  **工时**: ~1.5h  **依赖**: M2

### 背景

让玩家船动起来。完成后能用 WASD 控制船在海面上自由航行，靠近港口时按 Space 进港、弹出港口主菜单（5 个按钮，仅"出港"可用），按 Esc 出港回到航海视图。

### TODO

- [ ] 写 `Voyage3DSailing.hpp/cpp`：
  - 函数 `UpdatePlayerShip(FShipRuntime& ship, double deltaSec, const InputState& input)`
  - 输入字段：`bool keyW, keyA, keyS, keyD`，玩家船 `yaw` 由 A/D 控制（旋转速度 1.5 rad/sec），`speed` 由 W/S 控制（W 加速到 ship.speedKnots，S 减速到 0；不允许倒退）
  - 每帧 `worldPos += vec3(cos(yaw)*speed*dt, 0, sin(yaw)*speed*dt)`
  - 每帧应用陆地碰撞：遍历 7 个陆地 box，若 ship.worldPos 落在 box.min ~ box.max 的 XZ 投影内，则把 worldPos 推回上一帧位置（最简反弹，足够 MVP）
  - 每帧应用海域边界：worldPos.x clamp 到 [-30, 85]，worldPos.z clamp 到 [-90, 20]
- [ ] 在 `Voyage3DGameInstance::OnKey` 里维护 `keyW_/keyA_/keyS_/keyD_` 标志
- [ ] 在 `Voyage3DGameInstance::OnTick` 里：
  - 仅当 `appState_ == EAppState::Sailing` 调 `UpdatePlayerShip`
  - 更新 playerShip_ 的父 Node `SetTransform`（位置 + Y 轴旋转 yaw）
- [ ] 摄像机跟随：在 `OverrideRenderCamera` 里返回 `eye = playerShip.worldPos + (0, 18, 14)`，`target = playerShip.worldPos`
- [ ] 进港检测：每帧若 `appState_ == Sailing`，遍历 ports，若 `distance(playerShip.worldPos, port.worldPos) < 4.0`，记录 `nearestPort_ = &port`；否则 `nearestPort_ = nullptr`
- [ ] HUD：当 `nearestPort_ != nullptr` 时屏幕底部显示 `"按 SPACE 进入 {portName}"`
- [ ] 按 Space + `nearestPort_` 非空：切换 `appState_ = InPort`，记录 `currentPort_ = nearestPort_`，`port.visited = true`
- [ ] 港口主菜单 UI（在 `Voyage3DUI.hpp/cpp` 写 `RenderPortMenu(GameInstance&)`，被 OnRenderUI 在 `appState == InPort` 时调用）：
  - ImGui 居中模态窗口（`SetNextWindowPos` 居中 + `Begin` flags 含 `NoMove` `NoResize`）
  - 标题：`"{portName} - {nation}"`
  - 5 个按钮：`市场（暂禁用）` / `酒馆（暂禁用）` / `造船厂（暂禁用）` / `出港` / `保存（暂禁用）`
  - "出港" 点击 → 切回 `appState_ = Sailing`，`currentPort_ = nullptr`，玩家船保留当前位置和朝向
- [ ] 主菜单（`appState == MainMenu`）：写 `RenderMainMenu`，标题 + "新游戏" / "退出"两个按钮；"新游戏" → `appState_ = Sailing`

### 涉及文件

- 新建：`src/Application/Voyage3D/Voyage3DSailing.{hpp,cpp}`、`Voyage3DUI.{hpp,cpp}`
- 改：`Voyage3DGameInstance.{hpp,cpp}`

### 验收方法

1. 编译通过
2. 启动 → 主菜单 → 点击"新游戏" → 进入航海视图
3. 用 WASD 能控制船航行，转向自然（A/D 转身、W 加速、S 减速）
4. 摄像机平滑跟随玩家船
5. 撞陆地块时被弹回（不能穿过）
6. 接近威尼斯港 → 屏幕底部显示提示 → 按 Space → 弹出港口主菜单
7. 港口主菜单"出港"按钮可用 → 点击后回航海视图、玩家船在原位置
8. 至少能从威尼斯航行到那不勒斯并进港，再返回威尼斯

### 注意

- 速度 1 单位 ≈ 1m/s（地中海宽 110 单位 = 110 公里？不必精确，玩家感受为准）
- 陆地碰撞用 AABB 即可（不需要 OBB），船当成质点检测
- yaw 旋转：在 GLM 中用 `glm::angleAxis(yaw, vec3(0,1,0))` 或直接用 `glm::rotate(mat4(1), yaw, vec3(0,1,0))`
- **不要**在 M3 加陀螺式相机（按住右键转视角等）；固定俯视即可
- **不要**在 M3 实现"市场""酒馆""造船厂"逻辑（M4 / M7 才做）
- 主菜单文字可以英文（避免字体问题），M7 抛光阶段再考虑中文字体

---

## M4. 贸易系统

**优先级**: P0  **工时**: ~1.5h  **依赖**: M3

### 背景

闭环跑商核心。完成后玩家可在港口买卖 6 种商品，价格按高产/高需地差异化，玩家能从威尼斯买玻璃 → 跑到亚历山大卖出获利 → 完成第一次盈利。

### TODO

- [ ] 写 `Voyage3DTrade.hpp/cpp`：
  - 函数 `RefreshPortPrices(FPortRuntime& port, const std::vector<FGoodsDef>& goods, std::mt19937& rng)`：按 § 价格公式 计算 `port.currentPrices`；每次进港调用一次
  - 函数 `BuyGood(FPortRuntime& port, FShipRuntime& ship, int& playerGold, const std::string& goodId, int qty) -> bool`：扣金币、加货物，校验：金币足够、货舱不超载（`ship.cargoUsed + qty <= ship.def.cargoMax`），返回是否成功
  - 函数 `SellGood(FPortRuntime& port, FShipRuntime& ship, int& playerGold, const std::string& goodId, int qty) -> bool`：扣货物、加金币，校验：玩家持有足够数量
  - 价格 RNG 用 GameInstance 持有的全局 `std::mt19937`（参考 [Brotato3DGameInstance.hpp:288](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp)）
- [ ] 在 `Voyage3DGameInstance::EnterPort()`（M3 中按 SPACE 进港的处理函数）调 `RefreshPortPrices`
- [ ] 港口主菜单"市场"按钮启用 → 点击 → 切换 `appState_ = Trading`
- [ ] 写 `RenderTradePanel(GameInstance&)`（在 `Voyage3DUI.cpp`），ImGui 模态窗口：
  - 顶部：`"威尼斯 - 1499 年 4 月 - 金币 {gold}"`、`"货舱 {cargoUsed}/{cargoMax}"`
  - 主体：双栏 `BeginTable` 2 列：
    - 左列（港口）：`商品名 | 当前价 | 卖给我`（每行有"买 1"按钮 + 数量 InputInt + "买 N"按钮）
    - 右列（玩家货舱）：`商品名 | 持有 | 卖出`（每行有"卖 1"按钮 + 数量 InputInt + "卖 N"按钮）
  - 底部："返回港口"按钮 → 切回 `appState_ = InPort`
- [ ] 实现"买 1" / "买 N" / "卖 1" / "卖 N" 的回调：调 `BuyGood` / `SellGood`，失败时短暂红字提示（用 `tradeMessage_` + `tradeMessageMs_` 复用 Brotato3D 的浮字模式）

### 涉及文件

- 新建：`src/Application/Voyage3D/Voyage3DTrade.{hpp,cpp}`
- 改：`Voyage3DGameInstance.{hpp,cpp}`、`Voyage3DUI.{hpp,cpp}`

### 验收方法

1. 编译通过
2. 进入威尼斯港 → 点"市场" → 看到 6 种商品价格表
3. 玻璃在威尼斯（高产）显示 ~60 金币（base 100 × 0.6 × noise）
4. 葡萄酒在威尼斯（非高产非高需）显示 ~50 金币（base 50 × noise）
5. 在威尼斯买 5 玻璃 → 货舱显示 `5/20` → 出港 → 航行到亚历山大 → 进港 → 玻璃显示 ~160 金币（base 100 × 1.6 × noise，高需地）
6. 在亚历山大卖出全部 5 玻璃 → 净赚 ~500 金币，金币从 1000 → ~1500
7. 货舱满时再点买入 → 红字 `"货舱已满"`
8. 金币不足时再点买入 → 红字 `"金币不足"`

### 注意

- 价格 noise 每次进港**重新 roll**，玩家不能"读档反复刷低价"（MVP 不做存档，但保持机制正确）
- "买 N" / "卖 N" 的 N 默认 = 货舱剩余空间 / 玩家持有量，由 `InputInt` 让玩家覆盖
- 价格表格用 `ImGui::BeginTable("trade", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)`
- 港口库存（FPortRuntime::stock）字段先保留接口但不实现衰减（M8 才做）
- **不要**做信用 / 借贷 / 抵押系统（不在 MVP 范围）
- **不要**做"特产 +20% 售价"等复杂修正（仅靠 supplyPorts/demandPorts 已足够）

---

## M5. 海战系统（侧舷炮战）

**优先级**: P0  **工时**: ~2h  **依赖**: M3

### 背景

实现《大航海》经典手感的侧舷炮战。完成后航行中能随机遭遇海盗船 → 选择开战 → 进入海战视图 → 用 Q/E 左右舷开火 → 击沉敌船获奖励。

### TODO

- [ ] 在 `Voyage3DGameInstance` 增加敌船容器 `std::vector<FShipRuntime> enemyShips_`、炮弹池 `std::vector<FProjectileRuntime> projectiles_`
- [ ] 写 `Voyage3DCombat.hpp/cpp`：
  - `struct FProjectileRuntime { glm::vec3 worldPos; glm::vec3 velocity; float lifetimeMs; bool fromPlayer; int damage; std::shared_ptr<Node> node; }`
  - 函数 `SpawnEnemyShip(GameInstance&, const std::string& shipId, const glm::vec3& spawnPos)`：创建敌船 procmodel（红黑帆变体），加入 `enemyShips_`
  - 函数 `UpdateCombat(GameInstance&, double deltaSec)`：被 OnTick 在 NavalCombat 状态下调用
    - 玩家船按 WASD 控制（同 Sailing）
    - 敌船 AI：朝玩家船方向移动；当距离 < 8 时尝试侧舷瞄准（计算敌船到玩家船的方向，使敌船的左/右舷方向与该方向夹角小 → 转向调整）；当夹角 < 15° 且距离 < 6 → 开火
    - 玩家开火：按下 Q → 左舷一组炮位齐射（cannonCount/2 颗炮弹，沿玩家船 yaw + 90° 方向飞）；E → 右舷（yaw - 90°）；按键有 cooldown 1500ms
    - 炮弹 spawn 在船舷边缘（worldPos + side_dir * sx/2），velocity 长度 = 18 m/s，lifetimeMs = 800
    - 炮弹更新：每帧 `worldPos += velocity * dt`，`lifetimeMs -= dt*1000`，过期销毁
    - 命中检测：玩家炮弹检测所有敌船（distance(projectile.worldPos, ship.worldPos) < 1.0），命中扣 ship.currentHp（damage = 8）；敌船炮弹同理打玩家；命中销毁炮弹
    - HP < 30%：船速降为 50%
    - HP <= 0：船爆炸（在 worldPos 推一次 explosionRing 视觉，复用 Brotato3D 的 explosionRings_ 模式），从 enemyShips_ 移除节点
- [ ] 状态机：
  - Sailing 中遭遇事件触发 pirate（M6 串接）→ 切换 `appState_ = NavalCombat` 前先弹一个 modal："海盗来袭！[开战] [逃跑]"
  - 开战 → spawn 敌船在玩家船 +Z 方向 12m 外，随机 yaw
  - 逃跑 → 25% 概率失败仍然开战、75% 直接回 Sailing
  - 战斗中按 Shift（连按 3 秒）尝试脱战 → 50% 成功回 Sailing；失败继续战斗
- [ ] 海战相机：在 NavalCombat 状态下 `OverrideRenderCamera` 改为眼位 `(midX, 8, midZ + 10)`，target = `(midX, 0, midZ)`，midX/Z = (player + enemy) / 2
- [ ] 战斗结束：玩家胜 → 加奖励金币 `randInt(150, 400)` + 50% 概率掉落 `randInt(2,5)` 单位敌船特产货物（按敌船所在海域随机选 1 种 good）→ 切回 Sailing
- [ ] 战斗结束：玩家败（HP <= 0）→ 切到 `appState_ = Result`，显示"全军覆没"结算（M7 完善）
- [ ] HUD（NavalCombat 中）：
  - 顶部：玩家 HP 条（绿/黄/红）、敌船 HP 条
  - 底部：`"Q: 左舷齐射  E: 右舷齐射  Shift+长按 3s: 撤退"`
  - 复用 Brotato3D 的 muzzleFlash / floatingText / screenShake：开炮时船舷推 muzzleFlash、命中时推 floatingText `"-{damage}"`、击沉时推 screenShake

### 涉及文件

- 新建：`src/Application/Voyage3D/Voyage3DCombat.{hpp,cpp}`
- 改：`Voyage3DGameInstance.{hpp,cpp}`、`Voyage3DUI.{hpp,cpp}`

### 验收方法

1. 编译通过
2. 在调试期可加一个键（如 F5）强制触发海盗战斗（M6 接事件后改为随机）
3. 进入海战视图：相机切换、玩家船与敌船同时可见
4. 按 Q → 左舷一组炮弹飞出 + 炮口闪光
5. 玩家船左舷对着敌船开火 → 命中 → 飘字 `-8` + 屏幕震动
6. 敌船 AI 主动靠近玩家船 + 转向对舷 + 开火，玩家不动也会被打中
7. 击沉敌船 → 爆炸圆环 + 奖励 200~400 金币 + 可能掉落货物
8. 玩家被击沉（HP=0）→ 进入 Result 状态显示失败

### 注意

- 侧舷判定：敌船的 yaw + 90° 是其左舷法线方向，向量 `vec3(cos(yaw+pi/2), 0, sin(yaw+pi/2))`；与"敌船指向玩家"向量做点积，绝对值越大越正对侧舷
- 炮弹**不需要**抛物线；MVP 是水平直线运动，命中检测纯距离
- "开炮 cooldown" 在 FShipRuntime 加 `leftBroadsideCooldownMs / rightBroadsideCooldownMs`，每帧 -= dt*1000
- 炮口闪光、命中飘字、屏幕震动、爆炸环全部**直接照搬** Brotato3D 同名工具函数（`PushMuzzleFlash` `PushFloatingText` `StartScreenShake` `PushExplosionRing`），把 GameInstance 写成同样接口
- AI 不做转向预判 / 拦截算法（MVP 难度友好）
- **不要**做白刃战 / 登船战
- **不要**为每发炮弹做物理刚体；纯运动学
- **不要**让多个敌船同时出现（MVP 期 1v1）

---

## M6. 海上事件 + 探索

**优先级**: P1  **工时**: ~1h  **依赖**: M5

### 背景

让航行不单调。完成后持续航行 5 分钟内会随机触发 ≥ 3 种事件，已访问港口在视觉上能与未访问区分。

### TODO

- [ ] 写 `Voyage3DEvent.hpp/cpp`：
  - `struct FEventDef { id, weight, effect, ... }`，加载 `events.json`
  - 函数 `RollEvent(GameInstance&)`：按权重随机选一个事件应用
- [ ] 在 `Voyage3DGameInstance` 加 `eventCheckTimerSec_`，Sailing 状态下每帧累加 deltaSec；当 `> 8.0` 时调 `RollEvent` 并清零（也即每 8 秒一次事件 roll）
- [ ] 实现 5 种 effect：
  - **calm**：什么都不做（70% 权重，玩家大部分时间不会被打扰）
  - **pirate**：弹出"海盗来袭"modal（沿用 M5 的开战流程）
  - **bottle**（intel）：弹出 toast 浮字 `"漂流瓶情报：{随机港口名} 的 {随机商品名} 价格便宜！"`，并在该港口下次 `RefreshPortPrices` 时强制把该商品 supplyFactor 再 -0.2（一次性 buff，下次进港后清除）
  - **wreck**（treasure）：直接结算 `gold += 200`、`cargo[spice] += 5`（货舱满则忽略 cargo），弹出 toast `"发现沉船！获得 200 金币 + 5 香料"`
  - **storm**：`playerShip.currentHp -= 10`，speed buff -50% 持续 5000ms（用 `stormDebuffMs_` 字段），弹出 toast `"暴风雨！HP -10 速度 -50%"`
- [ ] 弹出 toast：复用 Brotato3D `PushFloatingText` 模式或在 UI 顶部画一个固定窗口短暂显示文字
- [ ] 已访问标记：在 `RenderHUD` 时为每个港口投影世界坐标到屏幕，用 foreground draw list 在港口图标上方画小标签（已访问：金色 ✓；未访问：灰色 ?）；同时锚点 sphere 颜色：未访问 → 灰色 `[0.5,0.5,0.5]`，已访问 → 金色（`SetNodeMaterial` 切换）
- [ ] 航海日志：按 J 打开 ImGui 窗口"航海日志"，显示最近 10 条事件文本（在 `eventLog_` vector 中维护）

### 涉及文件

- 新建：`src/Application/Voyage3D/Voyage3DEvent.{hpp,cpp}`、`assets/configs/voyage3d/events.json`
- 改：`Voyage3DGameInstance.{hpp,cpp}`、`Voyage3DUI.{hpp,cpp}`

### 验收方法

1. 编译通过
2. 持续航行 1 分钟（不进港）至少触发 1-2 次非 calm 事件
3. 持续航行 5 分钟内触发 ≥ 3 种不同事件类别
4. 海盗事件 → 进入战斗 → 胜利后回 Sailing
5. 漂流瓶 → 顶部 toast → 该港口下次进港某商品价格明显低于平均
6. 沉船 → 金币立刻增加、香料货物立刻增加
7. 风暴 → HP 立刻 -10、5 秒内航速肉眼可见变慢
8. 已访问港口的锚点 sphere 变金色，未访问保持灰色
9. 按 J 打开航海日志，显示历史事件列表

### 注意

- 事件 roll 间隔 8 秒可调，但**不要**做成"刚触发完一种就能 1 秒后再触发"，防止刷屏
- 风暴的 speed debuff 在 Sailing.cpp 应用：`effectiveSpeed = ship.def.speedKnots * (stormDebuffMs > 0 ? 0.5 : 1.0)`
- 漂流瓶的"指定港口商品价格 buff"实现简单点：在 FPortRuntime 加 `std::map<string, float> nextVisitDiscountFactor`，下次 RefreshPortPrices 时乘进去然后清空
- 沉船货物超过货舱容量时**只补到满，不溢出**（MVP 不做"丢哪个货"选择）
- 航海日志最多保留 10 条，新增推到末尾、超过则丢最早

---

## M7. 造船厂 + HUD 抛光 + 结算

**优先级**: P1  **工时**: ~1h  **依赖**: M4, M5, M6

### 背景

完成完整一局闭环（出威尼斯 → 跑商 → 战斗 → 升级船 → 继续）+ 结算 UI + 整体抛光。

### TODO

- [ ] 港口主菜单"造船厂"按钮启用 → 切换 `appState_ = ShipUpgrade`
- [ ] 写 `RenderShipUpgradePanel(GameInstance&)`（在 Voyage3DUI.cpp）：
  - ImGui 模态窗口
  - 列出 3 种船型，每种一行：船型名 | HP | 货舱 | 炮位 | 速度 | 价格 | "购买"按钮
  - 当前船型行高亮（蓝色背景）+ "购买"按钮置灰
  - 价格 > 玩家金币 → 按钮置灰
  - 点击购买：扣金币、把 playerShip 的 def 切换为新 ship、currentHp 满血、cargo 保留（货舱不够时按超载量丢弃最多份的商品）；视觉上重建船的 procmodel size（M7 必须重建船的 box / mast / sail child node）
- [ ] HUD 完善（顶部固定窗口）：
  - 左：金币 / 当前船型 / HP `{cur}/{max}`
  - 中：当前游戏日期（每秒前进游戏内 0.5 天，格式 `"1499 年 4 月"`）
  - 右：当前船世界坐标（debug 用，可隐藏）
- [ ] 主菜单视觉：标题字体放大、添加副标题 `"地中海跑商冒险"`
- [ ] 港口主菜单：每个港口配一段简短的 ImGui::TextWrapped 介绍，从 `assets/configs/voyage3d/port_lore.json` 读取（M7 新增）；如临时来不及写所有港口，至少威尼斯和亚历山大要有
- [ ] 结算 UI（`appState == Result`）：
  - 居中模态窗口
  - 标题：`"全军覆没"` / `"破产"`（gold < 0 且无货物时也算输）
  - 副信息：`"航行天数：{N}  港口足迹：{N}/10  战斗胜场：{N}"`
  - 两个按钮："重新开始"（reset 所有 runtime 字段，appState_ = MainMenu）/ "退出"（engine_->RequestExit()）
- [ ] BGM 切换：在 OnInit 加载 3 首占位 BGM（仅当 audio 系统可用时）：`sailing_bgm.ogg / port_bgm.ogg / battle_bgm.ogg`，根据 appState 切换；找不到文件不报错（spdlog warn 即可）
- [ ] 平衡调试（用 dev 控制台或硬编码常量）：
  - 玩家初始金币：1000
  - 卡拉克价格 3000：跑 3-4 个高利润航线（威→亚→威 + 威→君→威）能凑齐
  - 海盗船 HP 60，玩家 sloop HP 60，伤害比 1:1，新手能赢

### 涉及文件

- 改：`Voyage3DGameInstance.{hpp,cpp}`、`Voyage3DUI.{hpp,cpp}`、`Voyage3DShip.hpp`（购买切换船型逻辑）
- 新建（可选）：`assets/configs/voyage3d/port_lore.json`

### 验收方法

1. 编译通过
2. 完整新手流程：新游戏 → 出威尼斯 → 航行到亚历山大 → 卖玻璃买香料 → 回威尼斯卖香料 → 利润 ≥ 500 金币 → 重复 2-3 次凑够 3000 金币 → 进威尼斯造船厂 → 购买卡拉克 → 视觉上船变大 → 货舱从 20 升到 60 → 继续跑商
3. 全程 15 分钟内可顺畅完成
4. 战斗胜负后状态正确切换；玩家被击沉显示结算 UI
5. HUD 顶部信息全部可读（金币 / 船型 / HP / 游戏日期）
6. 至少威尼斯和亚历山大有港口介绍文字
7. 无明显 UI bug（按钮失灵 / 文字裁剪 / 模态窗口卡死）

### 注意

- 重建船的 procmodel：购买新船时调一个 `RebuildPlayerShipNodes()` 函数，先 hide/destroy 旧 child node，再按新 def.size 重新 CreateBox 三次
- 货物超载丢货：按 `cargoUsed` 倒序遍历，丢到刚好放下为止；spdlog warn 一行
- 游戏日期推进：每秒 +0.5 天（即 60 秒 = 30 天）；维护 `int gameDayCounter_`，转日期 `year = 1499 + days/365`，`month = (days/30 % 12) + 1`
- 结算条件：玩家船 HP <= 0 → 全军覆没；金币 < 0 且 cargoUsed = 0 → 破产
- BGM 切换避免每帧 PlaySound；维护 `currentBgmId_` 字段，仅在变化时调
- **不要**做存档（MVP 不在范围）
- **不要**做"船舱里水手谈话"等 RPG 内容

---

## M8.（可选）声誉与国籍 + 经济调优

**优先级**: P2  **工时**: ~1h  **依赖**: M7

### 背景

锦上添花。让国家关系影响商品交易和事件，提升重玩价值。

### TODO

- [ ] 玩家初始选 1 个国籍（新游戏时弹选择对话框）：威尼斯 / 阿拉贡 / 奥斯曼 / 法国，影响初始港口
- [ ] 玩家在每个国家有 `reputation`（声誉 -100 ~ +100）
  - 击沉海盗 + 5（向附近最大势力国家声誉）
  - 在某国港口贸易 + 1 / 100 金币交易额
  - 击沉非海盗船 - 20（MVP 不做对手国军舰，预留接口）
- [ ] 港口对玩家的态度：
  - reputation < -50：拒绝进港（按 SPACE 时显示"敌国，禁止入港"）
  - reputation > +50：商品价格 -10%（友好国折扣）
- [ ] 港口库存衰减（M4 留的接口）：
  - FPortRuntime::stock 初始值随机 50-200（每商品）
  - 玩家买入 → stock -= qty；卖出 → stock += qty
  - stock < 50 时该商品 supplyFactor 失效（缺货价不便宜了）
  - stock > 200 时 demandFactor 失效（积压不再加价）
  - 每次 RefreshPortPrices 时 stock 向"中位数 100"回归 5（自然恢复）
- [ ] HUD 增加声誉条（顶部，4 国家 4 个小条）

### 涉及文件

- 改：`Voyage3DGameInstance.{hpp,cpp}`、`Voyage3DTrade.{hpp,cpp}`、`Voyage3DUI.{hpp,cpp}`、`Voyage3DPort.hpp`

### 验收方法

1. 编译通过
2. 新游戏弹国籍选择，选完后初始港口对应（威尼斯人从威尼斯起、阿拉贡人从巴塞罗那起）
3. 持续在威尼斯贸易 → 威尼斯声誉条上升
4. 击沉海盗 → 附近某国声誉 + 5（spdlog 输出哪个国）
5. 在威尼斯反复买入玻璃 → 库存下降 → 单价上升（不再是高产折扣价）
6. 友好国港口（reputation > 50）的商品价格全部 -10%

### 注意

- 国家划分：港口 nation 字段已存在（M2），声誉用 `std::map<std::string, int> reputations_`
- 库存回归 / 自然衰减只在进港 RefreshPortPrices 时算，**不要**每帧算
- **不要**做战争 / 联盟 / 外交事件树
- 国籍只影响初始港口和起始声誉，**不要**做种族贸易差异等

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target Voyage3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀 |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 平台 | 用 `PlatformCommon.h`，不直接 include 平台头 |
| Vulkan | 所有 VkResult 用 `VK_CHECK_RESULT`；RAII 资源管理 |
| 注释 | 默认不写注释，仅写非显然的 WHY |
| 提交 | 不要执行 git commit；只完成代码改动，由用户决定何时提交 |

**禁止**：
- 修改 `src/ThirdParty/` 或 `external/`
- 修改 Brotato3D / KongLie3D / MagicaLego 等其他 Application 子项目代码
- 引入新大型依赖（Bullet、Recast、海洋 FFT 库等）；MVP 期间所有逻辑用 stdlib + glm + nlohmann/json
- 在任务卡范围之外做"顺手清理"
- 把代码写到注释里 — 删掉的代码就是删掉，不留 `// removed`
- 拷贝 Brotato3D 整个目录（按本计划新建文件，仅复用其工具函数模式）

## 验证完整端到端

完成 M1-M7 后，端到端跑一遍：

1. `./build.bat --preset full-windows --reconfigure` 通过，无 warning regression
2. `./run.bat --preset full-windows --target Voyage3D` 启动
3. 主菜单 → "新游戏" → 进入航海视图
4. 看到地中海陆地块 + 10 个港口塔楼 + 玩家 sloop 在威尼斯外海
5. WASD 航行到亚历山大（约 60-90 秒），中间触发至少 1 次随机事件
6. 进入亚历山大 → 市场 → 买香料 → 卖玻璃 → 看到价格符合"高产/高需"差异
7. 出港 → 继续航行 → 遭遇海盗 → 选择开战 → 进入海战视图 → 用 Q/E 侧舷开炮 → 击沉敌船 → 获奖励
8. 回到威尼斯 → 卖香料 → 累计金币达到 3000+
9. 进入威尼斯造船厂 → 购买卡拉克 → 船视觉变大、货舱从 20 → 60
10. 继续航行至少 1 圈，验证升级后的体验仍然顺畅
11. （故意作死）让玩家船 HP 归零 → 显示"全军覆没"结算 → 点重新开始 → 状态完全重置

**不需要**：单元测试（这是游戏 demo，行为通过手玩验证更直接）、视觉测试（Voyage3D 不进 visual_test.json，因为它是交互式的）。

## 风险与备注

| 风险 | 应对 |
|---|---|
| 陆地 box 拼出来不像地中海 | landmass.json 是数据驱动的，先做 M1 看视觉效果再迭代 box 数值；最差情况下 box 数量加到 10-15 个 |
| 港口数量 10 个太多或太少 | M7 平衡调试时若发现"太分散没耐心跑"，可减到 6 个核心港口；JSON 改动即可 |
| 侧舷判定让玩家觉得手感差 | M5 抛光阶段加屏幕指示器（在玩家船左/右舷方向画虚线，敌船在线上时高亮）|
| 海盗 AI 太蠢 / 太强 | AI 状态机超简，调 cooldown 和命中率即可平衡；M7 调参 |
| 经济过快暴富 / 死锁 | 平衡通过调 ports.json 的 supplyPorts/demandPorts 设置，不改代码 |
| 中文字体 | M1-M6 用英文文本（船型 / 商品 / 港口名也可保留拼音）；M7 抛光阶段若有空再考虑 ImGui::AddFontFromFile + 字符范围 |
| 重建船 procmodel 重复造 | M7 写 `RebuildPlayerShipNodes()` 工具函数，造船厂购买时调用一次；保留旧 child node 引用以便清除 |
| Scene 重建开销 | 重新开始（结算 → 重玩）**不**重建 Scene；玩家船 / 敌船 / 炮弹的 visible/transform/material color 字段直接 reset，hp/cargo/gold 重置 |

## 后续 agent 调用建议

每个任务（M1, M2, ...）适合用一个独立 agent 调用执行，prompt 模板：

```
请执行 docs/projects/voyage-3d/plan.md 中的 M{N} 任务。
- 严格按 TODO 清单做，不要扩大范围
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报
- 不要 commit
```

完成 M7 后整体复盘是否需要 M8。
