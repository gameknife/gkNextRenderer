# Brotato 3D 复刻 — MVP 开发计划

## Context

参考 Blobfish 出品的《Brotato》（吸血鬼幸存者 like + 多武器自动战斗 + 波次商店循环），在 gkNextRenderer 引擎中新建一个 Application（`Brotato3D`），用引擎现有能力（Vulkan 渲染、ImGui、ProcModel、Node + RenderComponent、ImGui HUD）做一个**俯视 3D 复刻 MVP**。

**Brotato 核心玩法**（保留项）：
- 俯视固定竞技场，WASD 控制玩家**移动**（不主动瞄准）
- 武器**自动瞄准 + 自动开火**（核心爽快点）
- 多武器槽位（MVP 上限 2 个），同时开火，伤害/射程/攻速独立
- 波次制（MVP 5 波，每波 30s 倒计时）
- 击杀掉**经验（XP）+ 材料（Materials）**，玩家半径内自动磁吸
- 升级：满 XP 暂停游戏，3 选 1 属性卡
- 商店：每波结束有 10s 商店阶段，4 张属性卡用 Materials 购买
- 死亡 / 全 5 波存活 → 结算

**MVP 砍掉的项**（避免范围爆炸）：
- 角色选择系统（直接固定 1 个角色、起始数值）
- 武器商店与武器升级 / 合成（仅卖属性卡）
- 物品（passive items）
- 危险敌人 / 精英 / Boss（只 3 种基础敌人）
- 木箱、地图破坏、特殊掉落
- 暂停 / 设置菜单 / 存档 / 多语言

**用户决策**（已确认）：
- 范围：MVP 核心循环（M1–M8 必做，M9 可选）
- 视觉：**ProcModel 简单几何体**（box / sphere 组合，复用 MagicaLego/KongLie3D 风格），不依赖美术资产
- 文档：输出到 `docs/projects/brotato-3d/plan.md`
- 数据：**JSON 配置**（武器/敌人/波次/升级卡），便于调参不重编译
- 命名：项目代号 `Brotato3D`（对齐 KongLie3D 命名风格）

**为什么这样设计**：MVP 优先验证「俯视相机 + 玩家移动 + 大量动态实体（敌人/子弹/拾取物）+ 波次商店循环」在引擎里能跑通；几何体方案不依赖美术资产，把后续 agent 的开发卡点降到最小。每张任务卡都设计成「单 agent 单次会话能完成」的颗粒。

## 引擎可复用能力清单（不新造轮子）

| 需求 | 复用 | 文件路径 |
|---|---|---|
| Application 入口 | `NextGameInstanceBase` | [src/Runtime/Engine.hpp:41](../../../src/Runtime/Engine.hpp) |
| 输入回调 | `OnKey/OnMouseButton/OnCursorPosition` | [src/Runtime/Engine.hpp:74](../../../src/Runtime/Engine.hpp) |
| 程序化几何体 | `Assets::FProcModel::CreateBox/CreateSphere` | [src/Assets/Loaders/FProcModel.h:12](../../../src/Assets/Loaders/FProcModel.h) |
| 场景动态构建 | `BeforeSceneRebuild` + `Scene::AddNode` | [src/Application/gkNextRenderer/gkNextRenderer.cpp:150](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) |
| 节点 + 组件 | `Node::CreateNode` + `RenderComponent` | [src/Assets/Core/Node.h](../../../src/Assets/Core/Node.h), [src/Runtime/Components/RenderComponent.h](../../../src/Runtime/Components/RenderComponent.h) |
| 摄像机覆盖 | `OverrideRenderCamera` 钩子 | [src/Application/KongLie3D/KongLie3DGameInstance.cpp](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp) |
| ImGui HUD | `OnRenderUI/OnInitUI` 虚函数 | [src/Runtime/Engine.hpp:49](../../../src/Runtime/Engine.hpp) |
| JSON 解析 | `nlohmann-json`（已在 vcpkg.json 中） | [vcpkg.json:38](../../../vcpkg.json) |
| CMake 注册套路 | 参考 KongLie3D | [src/cmake/SourceFiles.cmake:95](../../../src/cmake/SourceFiles.cmake), [src/CMakeLists.txt:127](../../../src/CMakeLists.txt) |
| 启动 | `run.bat --target Brotato3D` | [scripts/run.ps1](../../../scripts/run.ps1) |

**不需要的**：
- 物理（PhysicsComponent 不挂 — 玩家/敌人/子弹用简单圆形碰撞；不做刚体动力学）
- QuickJS（数据走 JSON 即可）
- glTF 加载（纯 procmodel）
- 动画系统（移动用插值，攻击用闪光 + 飘字）
- 粒子系统（受击/死亡用 ImGui foreground 屏幕特效 + 简单临时 Node）

## 文件结构（最终态）

```
src/Application/Brotato3D/
├── Brotato3DGameInstance.hpp/cpp     # 主入口，继承 NextGameInstanceBase
├── Brotato3DArena.hpp/cpp            # 地面 + 边界 + 摄像机参数
├── Brotato3DPlayer.hpp/cpp           # 玩家移动 / stat / 经验 / 武器槽
├── Brotato3DEnemy.hpp/cpp            # 敌人运行时（HP / 速度 / 类型 / 尺寸）
├── Brotato3DWeapon.hpp/cpp           # 武器定义 / 自动瞄准 / 攻击节奏
├── Brotato3DProjectile.hpp/cpp       # 子弹运行时 + 池
├── Brotato3DPickup.hpp/cpp           # XP / Material 拾取物
├── Brotato3DWaveSystem.hpp/cpp       # 波次状态机 / 难度曲线 / spawn 调度
├── Brotato3DShop.hpp/cpp             # 商店 / 升级卡逻辑
├── Brotato3DUI.hpp/cpp               # 全部 ImGui HUD（HP/XP/Wave/Material/Cards/Result）
└── Brotato3DDataLoader.hpp/cpp       # JSON 加载 weapons/enemies/waves/upgrades

assets/configs/brotato3d/
├── weapons.json          # 武器定义（伤害 / 攻速 / 射程 / 子弹速度 / 颜色）
├── enemies.json          # 敌人定义（HP / 速度 / 接触伤害 / 尺寸 / XP掉落 / 颜色）
├── waves.json            # 5 波 spawn 表（敌人种类 + 数量 + 间隔 + 难度系数）
├── upgrades.json         # 升级卡定义（stat / 数值 / 权重）
└── shop_items.json       # 商店属性卡（stat / 数值 / 价格 / 权重）

docs/projects/brotato-3d/
└── plan.md               # 本文档
```

CMake 修改（仅 2 处，照抄 KongLie3D 套路）：
- [src/cmake/SourceFiles.cmake](../../../src/cmake/SourceFiles.cmake) 加 `src_files_brotato3d` GLOB
- [src/CMakeLists.txt](../../../src/CMakeLists.txt) 加 `add_executable(Brotato3D ...)` + 把 `Brotato3D` 加入 `AllTargets` 列表（MINGW 与非 MINGW 两份）

## JSON Schema 设计

**通用约定**：颜色 `[r,g,b]` float 0–1；尺寸单位米；时间字段以 `ms` 后缀显式标注。

### weapons.json

```json
{
  "weapons": {
    "smg": {
      "name": "SMG",
      "damage": 6,
      "atkSpeedHz": 4.0,
      "rangeMeters": 7.0,
      "projectileSpeed": 18.0,
      "projectileLifetimeMs": 800,
      "projectileColor": [1.0, 0.85, 0.2],
      "projectileSize": 0.12,
      "spreadDeg": 4.0
    },
    "shotgun": {
      "name": "Shotgun",
      "damage": 4,
      "atkSpeedHz": 1.0,
      "rangeMeters": 5.5,
      "projectileSpeed": 16.0,
      "projectileLifetimeMs": 500,
      "projectileColor": [1.0, 0.4, 0.2],
      "projectileSize": 0.14,
      "pellets": 5,
      "spreadDeg": 18.0
    }
  }
}
```

### enemies.json

```json
{
  "enemies": {
    "rat": {
      "name": "Rat",
      "hp": 14,
      "moveSpeed": 3.6,
      "contactDamage": 4,
      "size": [0.45, 0.45, 0.45],
      "color": [0.65, 0.45, 0.25],
      "xpDrop": 1,
      "materialDrop": 1
    },
    "tank": {
      "name": "Brute",
      "hp": 60,
      "moveSpeed": 1.6,
      "contactDamage": 12,
      "size": [0.85, 0.9, 0.85],
      "color": [0.55, 0.15, 0.20],
      "xpDrop": 5,
      "materialDrop": 4
    },
    "spitter": {
      "name": "Spitter",
      "hp": 18,
      "moveSpeed": 2.2,
      "contactDamage": 6,
      "size": [0.55, 0.55, 0.55],
      "color": [0.30, 0.65, 0.45],
      "xpDrop": 2,
      "materialDrop": 2,
      "kitingDistance": 4.5
    }
  }
}
```

> MVP 期 spitter 的远程攻击**先不做**（接触伤害即可），把 `kitingDistance` 字段保留为 M9 占位。

### waves.json

```json
{
  "waves": [
    { "durationSec": 30, "spawns": [ {"enemyId":"rat","count":18,"intervalMs":1100} ] },
    { "durationSec": 30, "spawns": [ {"enemyId":"rat","count":24,"intervalMs":900},
                                     {"enemyId":"spitter","count":4,"intervalMs":4500} ] },
    { "durationSec": 30, "spawns": [ {"enemyId":"rat","count":28,"intervalMs":750},
                                     {"enemyId":"spitter","count":8,"intervalMs":2800},
                                     {"enemyId":"tank","count":2,"intervalMs":12000} ] },
    { "durationSec": 30, "spawns": [ {"enemyId":"rat","count":36,"intervalMs":600},
                                     {"enemyId":"spitter","count":12,"intervalMs":2200},
                                     {"enemyId":"tank","count":4,"intervalMs":7500} ] },
    { "durationSec": 30, "spawns": [ {"enemyId":"rat","count":50,"intervalMs":450},
                                     {"enemyId":"spitter","count":16,"intervalMs":1700},
                                     {"enemyId":"tank","count":7,"intervalMs":4500} ] }
  ]
}
```

### upgrades.json（升级 3 选 1）

```json
{
  "cards": [
    {"id":"dmg_pct",   "name":"+15% 伤害",      "stat":"damagePct",      "delta":0.15, "weight":3},
    {"id":"atkspd_pct","name":"+12% 攻速",      "stat":"atkSpeedPct",    "delta":0.12, "weight":3},
    {"id":"speed_pct", "name":"+10% 移速",      "stat":"moveSpeedPct",   "delta":0.10, "weight":2},
    {"id":"hp_flat",   "name":"+15 最大生命",   "stat":"maxHpFlat",      "delta":15,   "weight":3},
    {"id":"range_pct", "name":"+15% 射程",      "stat":"rangePct",       "delta":0.15, "weight":2},
    {"id":"pickup",    "name":"+25% 拾取半径",  "stat":"pickupRadiusPct","delta":0.25, "weight":2}
  ]
}
```

### shop_items.json（每波结束 4 选 N）

```json
{
  "items": [
    {"id":"buy_dmg",   "name":"+5 固定伤害",  "stat":"damageFlat",  "delta":5,   "cost":12, "weight":3},
    {"id":"buy_hp",    "name":"+20 最大生命","stat":"maxHpFlat",   "delta":20,  "cost":10, "weight":3},
    {"id":"buy_speed", "name":"+8% 移速",    "stat":"moveSpeedPct","delta":0.08,"cost":8,  "weight":2},
    {"id":"buy_atkspd","name":"+10% 攻速",   "stat":"atkSpeedPct", "delta":0.10,"cost":15, "weight":2},
    {"id":"buy_heal",  "name":"恢复 30% HP", "stat":"healPct",     "delta":0.30,"cost":8,  "weight":2}
  ]
}
```

## 视觉 / 坐标约定

- 竞技场原点：世界坐标 `(0, 0, 0)`，X 轴 = 横向，Z 轴 = 纵向，Y 轴向上
- 竞技场尺寸：**24m × 16m**（X ∈ [-12, 12]，Z ∈ [-8, 8]），地面 `y = 0`
- 玩家初始位置：`(0, 0.5, 0)`
- 玩家几何：球 `radius=0.4` + 顶部小立方体表示朝向（朝向跟随当前自动瞄准目标方向）
- 敌人几何：按 `enemies.json[size]` 建 box，`y = size.y/2`
- 子弹几何：球 `radius=projectileSize`
- 拾取物：XP 球（绿色 `radius=0.12`）、Material 球（黄色 `radius=0.10`），y = 0.15
- 摄像机：固定俯视斜角，眼位 `(0, 18, 11)`，看向 `(0, 0, 0)`，FOV 50°，**不**跟随玩家（竞技场全可见）
- 颜色：直接读 JSON `color`，转 `vec3` → `Material::Lambertian`
- HP 条：ImGui foreground draw list，世界坐标投屏，敌人头顶绘 `2px` 高细矩形（HP > 70% 不画，避免视觉杂乱）

## 玩家属性模型

```cpp
struct FPlayerStats {
    float maxHpFlat       = 50.0f;
    float damagePct       = 0.0f;   // 武器最终伤害 = baseDamage * (1 + damagePct) + damageFlat
    float damageFlat      = 0.0f;
    float atkSpeedPct     = 0.0f;   // 武器最终 hz = baseHz * (1 + atkSpeedPct)
    float rangePct        = 0.0f;
    float moveSpeedPct    = 0.0f;
    float pickupRadiusPct = 0.0f;
    int   maxHpFlatBonus  = 0;
};
```

- 基础移速 `5.0 m/s`
- 基础拾取半径 `1.6 m`
- 起手武器：`smg`，槽位 1
- 第二个武器槽位 M9 阶段开放（MVP 期保持 1 个武器即可玩）

## 任务索引（MVP 共 8 个，~9–11 小时）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [M1](#m1-application-骨架--俯视相机--地面竞技场) | Application 骨架 + 俯视相机 + 地面竞技场 | ~1h | — |
| [M2](#m2-玩家移动--几何体--边界限制) | 玩家移动 + 几何体 + 边界限制 | ~1h | M1 |
| [M3](#m3-敌人-spawn--追击-ai--接触伤害) | 敌人 spawn + 追击 AI + 接触伤害 | ~1.5h | M2 |
| [M4](#m4-武器系统--自动瞄准--子弹--命中判定) | 武器系统 + 自动瞄准 + 子弹 + 命中判定 | ~1.5h | M3 |
| [M5](#m5-xp--material-掉落--磁吸--升级卡) | XP / Material 掉落 + 磁吸 + 升级卡 | ~1h | M4 |
| [M6](#m6-波次系统--难度曲线--波间过渡) | 波次系统 + 难度曲线 + 波间过渡 | ~1h | M3 |
| [M7](#m7-hud--飘字伤害) | HUD + 飘字伤害 | ~1h | M5, M6 |
| [M8](#m8-商店--死亡胜利结算--抛光) | 商店 + 死亡/胜利结算 + 抛光 | ~1.5h | M5, M6, M7 |
| [M9](#m9-可选第二把武器--特效池--音效) | （可选）第二把武器 + 特效池 + 音效 | ~1h | M8 |

---

## M1. Application 骨架 + 俯视相机 + 地面竞技场

**优先级**: P0  **工时**: ~1h

### 背景

打通编译/启动链。新建 `Brotato3D` 子项目，参考 `KongLie3D` 的最小入口（`KongLie3DGameInstance.cpp` 头部 + `OverrideRenderCamera`，**不要**参考 MagicaLego，那个有 AI/Pak/Script 太重）。本任务后能 `run.bat --target Brotato3D` 启起来，看到一个 24×16 的灰色矩形地面 + 俯视斜角相机 + 一个空 ImGui 窗口写 `"Brotato3D MVP - bootstrap OK"`。

### TODO
- [ ] 创建目录 `src/Application/Brotato3D/`
- [ ] 写 `Brotato3DGameInstance.hpp`：继承 `NextGameInstanceBase`，声明 `OnInit/OnTick/OnDestroy/OnRenderUI/OnInitUI/OnKey/BeforeSceneRebuild/OverrideRenderCamera`，构造函数把窗口 title 设为 `"Brotato3D"`，1280×720
- [ ] 写 `Brotato3DGameInstance.cpp`：
  - 实现全局 `CreateGameInstance`（参考 [`KongLie3DGameInstance.cpp`](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp) 顶部）
  - `OnInit` 调用 `engine_->RequestLoadScene("")` 触发空场景重建（让 `BeforeSceneRebuild` 跑起来）
  - `OnTick` 留空
  - `OnRenderUI` 画一个最小窗口 `ImGui::Text("Brotato3D MVP - bootstrap OK")`
- [ ] 写 `Brotato3DArena.hpp/cpp`：
  - 暴露 `void BuildArena(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials, std::vector<std::shared_ptr<Assets::Node>>& nodes, FArenaResources& outRes)`
  - 用 `FProcModel::CreateBox(vec3(-12,-0.05,-8), vec3(12,0,8))` 创建地面（厚度 0.05），Lambertian 灰色 `(0.32,0.32,0.34)`
  - 在 4 条边上各放一个长条 box 作为视觉边界（高度 0.4，颜色 `(0.5,0.5,0.55)`）
  - 输出地面 `modelId / materialId` 给 `FArenaResources` 备用（M5 拾取地面投射用）
- [ ] 在 `BeforeSceneRebuild` 钩子里调用 `BuildArena`
- [ ] 在 `OverrideRenderCamera` 里返回固定俯视摄像机：
  - `camera.ModelView = glm::lookAt(vec3(0,18,11), vec3(0,0,0), vec3(0,1,0))`
  - `camera.FieldOfView = glm::radians(50.0f)`
- [ ] CMake 注册：
  - `src/cmake/SourceFiles.cmake`：加
    ```cmake
    file(GLOB_RECURSE src_files_brotato3d
        "Application/Brotato3D/*.cpp"
        "Application/Brotato3D/*.hpp"
    )
    ```
  - `src/CMakeLists.txt`：加 `add_executable(Brotato3D ${src_files_brotato3d} DesktopMain.cpp)`，并把 `Brotato3D` 同时加入两份 `AllTargets`（MINGW 分支与默认分支）
- [ ] 创建空 `assets/configs/brotato3d/.gitkeep`（M2 起逐步填充）

### 涉及文件
- 新建：`src/Application/Brotato3D/Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DArena.{hpp,cpp}`
- 改：`src/cmake/SourceFiles.cmake`、`src/CMakeLists.txt`

### 验收方法
1. `./build.bat --preset full-windows --reconfigure` 通过
2. `./run.bat --preset full-windows --target Brotato3D` 成功启动
3. 日志出现 `uploaded scene [...] to gpu`
4. 屏幕上能看到 24×16 灰色地面 + 4 边界条，俯视斜角
5. ImGui 窗口显示 `"Brotato3D MVP - bootstrap OK"`

### 注意
- **不要**复制 MagicaLego 整个目录 — 它有 AI/Script/Pak 等大量本任务用不上的代码；首选模仿 KongLie3D 的最小骨架
- **不要**做 ModelViewController（玩家用 WASD 直接驱动 Player Node，不靠相机）
- 头文件首行必须 `#include "Common/CoreMinimal.hpp"`
- 不在 M1 加任何动态实体（玩家/敌人/子弹），全部留给后续

---

## M2. 玩家移动 + 几何体 + 边界限制

**优先级**: P0  **工时**: ~1h  **依赖**: M1

### 背景

让玩家用 WASD 在竞技场内移动。完成后玩家是个绿色球（顶部小立方体表示朝向），WASD 8 方向移动，速度恒定 5 m/s，撞到边界停下。这是后续敌人/武器/碰撞的基准点。

### TODO
- [ ] 写 `Brotato3DPlayer.hpp`：
  ```cpp
  struct FPlayerRuntime {
      glm::vec3 worldPos = glm::vec3(0, 0.5f, 0);
      glm::vec3 facingDir = glm::vec3(0, 0, -1);   // 朝向（M4 自动瞄准会改）
      float radius = 0.4f;
      int currentHp = 50;
      int maxHp = 50;
      FPlayerStats stats;                            // 见上文「玩家属性模型」
      std::shared_ptr<Assets::Node> bodyNode;
      std::shared_ptr<Assets::Node> facingNode;
  };
  ```
- [ ] 在 `Brotato3DGameInstance` 加成员 `FPlayerRuntime player_;`
- [ ] `BeforeSceneRebuild` 内：
  - 创建球 model `FProcModel::CreateSphere(vec3(0), 0.4f)` → 绿色 `(0.20, 0.75, 0.30)` Lambertian → 创建 Node，`translation = player.worldPos`
  - 创建小立方体 model（朝向指示器）`CreateBox(vec3(-0.08,-0.08,-0.08), vec3(0.08,0.08,0.08))` → 白色 → Node，本地位置 `(0, 0.45, -0.25)`，作为玩家 Node 的子节点（暂时直接平铺也行，M4 再考虑朝向旋转的实现方式）
- [ ] `OnKey`：处理 WASD 按下/抬起，维护 `bool keyW_, keyA_, keyS_, keyD_`（不要在 OnKey 里直接动玩家，按状态机模式由 `OnTick` 消费）
- [ ] `OnTick(deltaSeconds)`：
  - `inputDir = vec3(keyD-keyA, 0, keyS-keyW)`，归一化（避免对角线 √2 倍速）
  - `speed = 5.0f * (1 + stats.moveSpeedPct)`
  - `player.worldPos += inputDir * speed * dt`
  - 边界 clamp：`x ∈ [-12 + radius, 12 - radius]`，`z ∈ [-8 + radius, 8 - radius]`
  - 把 `player.worldPos` 写到 `bodyNode->SetTransform(...)`（参考 KongLie3D 移动写 transform 的方式）
  - 朝向 indicator：`facingDir = inputDir != 0 ? inputDir : facingDir`（保留上一帧朝向）；child node 本地偏移用 `facingDir * 0.45f` 表示

### 涉及文件
- 新建：`src/Application/Brotato3D/Brotato3DPlayer.{hpp,cpp}`
- 改：`Brotato3DGameInstance.{hpp,cpp}`

### 验收方法
1. 编译通过
2. 启动后看到中心绿球 + 白色朝向小方块
3. 按 WASD 球流畅移动，对角线不超速
4. 球撞到边界条不穿出
5. 移动方向变化时朝向小方块跟着指向运动方向
6. 松开按键球立即停下（无惯性）

### 注意
- 不要在 M2 加重力 / 跳跃 / 冲刺；俯视游戏只有 X/Z 平面位移
- `Node::SetTransform` 接受 `(translation, rotation, scale)`；保持 rotation 为 identity，只改 translation
- **不要**用 PhysicsComponent — 边界用简单 clamp，碰撞 M3/M4 用距离判断
- 朝向指示器 MVP 期可以**直接**用 child node 局部位置偏移（不需要旋转矩阵），简单清晰

---

## M3. 敌人 spawn + 追击 AI + 接触伤害

**优先级**: P0  **工时**: ~1.5h  **依赖**: M2

### 背景

让敌人出现并追玩家。完成后能在没有 wave 系统的情况下手动按 `K` 键 spawn 一只 rat，敌人会从场地外向玩家直奔，碰到玩家扣 HP，自身被玩家「碰撞」时先暂不死亡（M4 才有武器）。本任务专注**敌人生命周期**和**接触伤害**逻辑。

### TODO
- [ ] 写 `assets/configs/brotato3d/enemies.json`（按上文 schema 填 3 种敌人）
- [ ] 写 `Brotato3DDataLoader.hpp/cpp`：
  - `struct FEnemyDef { name, hp, moveSpeed, contactDamage, size, color, xpDrop, materialDrop, kitingDistance }`
  - 函数 `bool LoadEnemies(path, std::map<std::string, FEnemyDef>& out)`
  - 用 `nlohmann::json`（已在 vcpkg.json）；缺字段用 `value("xxx", default)` 容错读取，但**关键字段缺失** spdlog ERROR 后 abort
- [ ] 写 `Brotato3DEnemy.hpp`：
  ```cpp
  struct FEnemyRuntime {
      const FEnemyDef* def = nullptr;
      glm::vec3 worldPos = glm::vec3(0);
      float radius = 0.3f;             // 用于碰撞判定：max(size.x,size.z)*0.5
      int currentHp = 0;
      bool alive = true;
      float hitFlashRemainingMs = 0.0f;
      float contactCooldownMs = 0.0f;  // 同一个敌人持续接触玩家时不连续扣血，每 600ms 触发一次
      uint32_t modelId = 0;
      uint32_t materialId = 0;
      std::shared_ptr<Assets::Node> node;
  };
  ```
- [ ] 写 `Brotato3DEnemy.cpp`：
  - `void SpawnEnemy(const FEnemyDef& def, const glm::vec3& worldPos, ...)`：在 runtime 容器里 emplace_back，**不**重建 Scene；Node 通过 `engine_->GetScene().AddNode(...)` 动态加入
- [ ] 在 `Brotato3DGameInstance` 加成员：
  - `std::map<std::string, FEnemyDef> enemyDefs_;`
  - `std::vector<FEnemyRuntime> enemies_;`
  - 每个敌人类型预创建 model（1 个 model 多个 instance 共享）和 material
- [ ] `OnInit` 调用 `LoadEnemies("assets/configs/brotato3d/enemies.json", enemyDefs_)`
- [ ] `BeforeSceneRebuild`：为 3 种敌人各创建 1 个 Box model（按 `def.size`）+ 1 个 Lambertian material，缓存 modelId/matId
- [ ] `OnKey`：按 `K` 时，从竞技场边缘随机选点（4 边各 25% 概率），`SpawnEnemy(enemyDefs_["rat"], spawnPos)`
- [ ] `OnTick` 内对每个 alive 敌人：
  - `dirToPlayer = normalize(player.worldPos - enemy.worldPos)`
  - `enemy.worldPos += dirToPlayer * def.moveSpeed * dt`
  - 写 `enemy.node->SetTransform(...)`
  - 接触判定：`distance(enemy.worldPos, player.worldPos) < enemy.radius + player.radius`
    - 若 `enemy.contactCooldownMs <= 0`：`player.currentHp -= def.contactDamage`，`enemy.contactCooldownMs = 600`
  - `contactCooldownMs -= dt * 1000`，`hitFlashRemainingMs -= dt * 1000`
- [ ] 玩家死亡判定（`player.currentHp <= 0`）→ spdlog 输出 `[player dead]`，**不**做结算 UI（留给 M8），暂时停在原地不动

### 涉及文件
- 新建：`assets/configs/brotato3d/enemies.json`、`src/Application/Brotato3D/Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DEnemy.{hpp,cpp}`
- 改：`Brotato3DGameInstance.{hpp,cpp}`

### 验收方法
1. 编译通过
2. 启动后按 `K` 一次 → 场地边缘出现一只褐色小 box，向玩家直奔
3. 玩家被撞到时 HP 数值（spdlog 打日志）按 4 点下降；同一只敌人持续接触每 600ms 扣一次
4. 多次按 `K` 可同时 spawn 多只敌人
5. 玩家移动时敌人轨迹会重新瞄准玩家
6. 敌人不会穿过场地边界（用同样的 clamp 逻辑）

### 注意
- **不要**在 M3 加敌人间相互避让 / boid（性能开销大且 Brotato 本身也允许重叠堆怪）
- **不要**在 M3 加 HP 条 UI（留给 M7）；用 spdlog 打 HP 即可
- `enemies_` 用 `std::vector` 即可，单帧最大 ~150 个敌人，O(n) 遍历可接受；若性能瓶颈出现再考虑空间索引（MVP 期不应出现）
- 死亡的敌人 `alive=false` + `node->SetVisible(false)`，**不要**真删除 Node（避免 Scene 重建抖动）；M5 阶段做对象池复用
- spawn 逻辑暂时手动 `K`，**不要**和 wave 系统耦合（M6 接管）

---

## M4. 武器系统 + 自动瞄准 + 子弹 + 命中判定

**优先级**: P0  **工时**: ~1.5h  **依赖**: M3

### 背景

让玩家自动开火打死敌人。完成后玩家手持 SMG 武器，自动瞄准最近敌人（射程内），到达 cooldown 自动发射黄色小球，命中敌人扣 HP，敌人死亡变暗下沉消失。这是 Brotato 「爽快感」的核心 — 屏幕上要有大量敌人 + 大量子弹同时存在。

### TODO
- [ ] 写 `assets/configs/brotato3d/weapons.json`（按上文 schema 填 SMG，shotgun 留给 M9）
- [ ] 在 `Brotato3DDataLoader` 加 `LoadWeapons` + `struct FWeaponDef`
- [ ] 写 `Brotato3DWeapon.hpp`：
  ```cpp
  struct FWeaponRuntime {
      const FWeaponDef* def = nullptr;
      float cooldownMs = 0.0f;
      // M9 阶段：每把武器独立 model/material 用于子弹外观；MVP 共用 SMG 的池
  };
  ```
- [ ] 写 `Brotato3DProjectile.hpp/cpp`：
  ```cpp
  struct FProjectileRuntime {
      glm::vec3 worldPos;
      glm::vec3 velocity;       // m/s
      float remainingLifetimeMs;
      int damage;
      uint32_t modelId, materialId;
      std::shared_ptr<Assets::Node> node;
      bool active = false;      // 池化复用
  };
  ```
  - 维护 `std::vector<FProjectileRuntime> projectilePool_;`，初始预创建 256 个（够用），通过 `active` 标志复用 — 不活跃时把 Node visibility 关掉并把位置移到 `(0, -100, 0)`
- [ ] 在 `Brotato3DGameInstance` 加：
  - `std::map<std::string, FWeaponDef> weaponDefs_;`
  - `std::vector<FWeaponRuntime> equippedWeapons_;`（MVP 期初始化为 1 个 SMG）
  - `std::vector<FProjectileRuntime> projectilePool_;`
  - 子弹 model：`CreateSphere(vec3(0), 0.12f)`，材质 = SMG 颜色（M9 才按武器分流）
- [ ] `BeforeSceneRebuild` 内为 256 个 projectile 创建 Node（all hidden），存入 pool
- [ ] `OnTick`：
  - **自动瞄准**：对每个 weapon，`cooldownMs -= dt*1000`；当 `cooldownMs <= 0`：
    - 找射程内最近 alive 敌人 `target`（O(n) 遍历 enemies_，距离 ≤ `weapon.range * (1 + stats.rangePct)`）
    - 若找到：
      - `dir = normalize(target.worldPos - player.worldPos)`，加 `±spreadDeg/2` 随机偏角（绕 Y 轴）
      - 从 pool 找 `!active` 的 projectile slot，初始化：`worldPos = player + dir * 0.5`、`velocity = dir * projectileSpeed`、`damage = weapon.damage * (1+damagePct) + damageFlat`、`remainingLifetimeMs = weapon.projectileLifetimeMs`、`active = true`、`node->SetVisible(true)`
      - `weapon.cooldownMs = 1000.0 / (atkSpeedHz * (1 + stats.atkSpeedPct))`
    - 若射程内无敌人：不开火，但 `cooldownMs` 不重置（攻速不浪费）
  - **子弹更新**：对每个 active projectile：
    - `worldPos += velocity * dt`，`remainingLifetimeMs -= dt*1000`
    - 边界外 / 寿命到：`active = false`，`node->SetVisible(false)`
    - **命中判定**：遍历 alive 敌人，`distance(projectile, enemy) < enemy.radius + 0.12f` → 命中：
      - `enemy.currentHp -= projectile.damage`
      - `enemy.hitFlashRemainingMs = 80`（M7 才用，M4 先打 spdlog）
      - 子弹失活
      - 若 `enemy.currentHp <= 0`：`enemy.alive = false`，`node->SetTransform` 让 box 颜色变暗 + 下沉 0.3 → 0.5s 后 SetVisible(false)（用一个 `deathFadeMs` 字段在 OnTick 推进）
- [ ] **玩家朝向**：把 player.facingDir 从 M2 的「输入方向」改为「自动瞄准方向」（瞄到目标时朝目标，无目标时朝最后输入方向）

### 涉及文件
- 新建：`assets/configs/brotato3d/weapons.json`、`src/Application/Brotato3D/Brotato3DWeapon.{hpp,cpp}`、`Brotato3DProjectile.{hpp,cpp}`
- 改：`Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DEnemy.{hpp,cpp}`（加 deathFadeMs 字段）、`Brotato3DPlayer.{hpp,cpp}`、`Brotato3DGameInstance.cpp`

### 验收方法
1. 编译通过
2. 启动后按 `K` spawn 几只 rat → 玩家自动朝最近 rat 开火，黄色小球飞出
3. 子弹命中 rat：rat HP 下降，14HP 的 rat 在 SMG 4Hz×6dmg 下约 0.6s 内死亡
4. rat 死亡：变暗下沉 0.5s 后消失
5. 移动玩家拉远到 SMG 射程外（7m）→ 不再开火
6. 同时 spawn 20+ 只敌人也不卡顿（子弹 + 敌人总数 < 300 不应有性能问题）
7. 玩家朝向小方块跟随当前自动瞄准目标方向

### 注意
- **不要**在 M4 做投射物贯穿 / 反弹 / AOE — 都是单发单命中（命中即失活）
- **不要**给每发子弹都重建 Scene — 用对象池
- 命中检测 O(projectiles × enemies) 在 256×100 = 2.5 万次/帧，60fps = 150 万次/秒，stdlib 实现完全够用，**不用**空间索引
- 子弹的 Node 在池里复用：失活时把 visible 关掉 + worldPos 设 `(0,-100,0)`，**不要**从 Scene 删除（删除会触发 Scene rebuild）
- 死亡淡出动画在 `enemy.deathFadeMs` 内推进，到时设 `node->SetVisible(false)`；下一波 spawn 同种敌人时**重置**该 Node 复用（M5 做对象池池化敌人 Node）
- 若敌人 Node 池化逻辑过早做导致 M4 复杂度爆炸：MVP 期可暂时**不复用**敌人 Node（每次 spawn 真的 AddNode），等 M6 大量 spawn 时再优化

---

## M5. XP / Material 掉落 + 磁吸 + 升级卡

**优先级**: P0  **工时**: ~1h  **依赖**: M4

### 背景

让击杀有反馈。完成后敌人死亡掉两种球：绿色 XP 球和黄色 Material 球，玩家靠近自动磁吸吸入；XP 满后游戏暂停弹出 3 选 1 升级卡，选择后属性立即应用，关闭弹窗游戏继续。

### TODO
- [ ] 写 `Brotato3DPickup.hpp`：
  ```cpp
  enum class EPickupKind : uint8_t { XP, Material };
  struct FPickupRuntime {
      EPickupKind kind;
      glm::vec3 worldPos;
      int value;                       // XP 量或 Material 量
      bool active = false;
      bool magnetized = false;
      std::shared_ptr<Assets::Node> node;
  };
  ```
  维护 `std::vector<FPickupRuntime> pickupPool_;` 预创建 256 个（128 XP + 128 Material）
- [ ] 子弹/敌人死亡处理（M4 死亡分支里）：调 `SpawnPickup(enemy.def->xpDrop, EPickupKind::XP, enemy.worldPos)` 和 `SpawnPickup(enemy.def->materialDrop, EPickupKind::Material, enemy.worldPos)`
- [ ] `OnTick` 拾取更新：
  - 对每个 active pickup：
    - 距离玩家 < `pickupRadius * (1 + stats.pickupRadiusPct)` → `magnetized = true`
    - 若 magnetized：`worldPos = lerp(worldPos, player.worldPos, 12.0f * dt)`（指数追逐）
    - 距离 < `0.4` → 拾取：
      - XP：`player.currentXp += value`；若 `currentXp >= xpToNextLevel` → 触发升级流程
      - Material：`player.materials += value`
      - `pickup.active = false`，Node 隐藏
- [ ] **升级流程**：
  - 在 GameInstance 加 `EAppState { Playing, LevelUpPicking, Shopping, Result }`
  - 触发升级：`appState_ = LevelUpPicking`，`pendingLevelUps_ += 1`，从 `upgrades.json` 按权重抽 3 张 + 弹出 ImGui modal（M7 实现 UI；M5 期先用 `ImGui::Begin("Choose Upgrade")` + 3 个按钮临时占位）
  - 玩家点卡片 → 应用 stat → `pendingLevelUps_ -= 1`
  - 如果 `pendingLevelUps_ > 0` 继续抽下一组；否则 `appState_ = Playing`
  - **暂停语义**：`appState != Playing` 时跳过 OnTick 内所有逻辑更新（玩家移动、敌人 AI、子弹、cooldown），`elapsedMs` 不增长
- [ ] 写 `assets/configs/brotato3d/upgrades.json`（按上文 schema）
- [ ] 在 `Brotato3DDataLoader` 加 `LoadUpgrades`
- [ ] `xpToNextLevel` 公式：`5 + currentLevel * 4`（level 1→2 需要 5XP，2→3 需要 9XP，3→4 需要 13XP …）

### 涉及文件
- 新建：`src/Application/Brotato3D/Brotato3DPickup.{hpp,cpp}`、`assets/configs/brotato3d/upgrades.json`
- 改：`Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DPlayer.hpp`（加 currentXp / materials / level / pendingLevelUps）

### 验收方法
1. 编译通过
2. 击杀 rat 掉 1 绿球 + 1 黄球
3. 玩家走近 1.6m 内球被吸过来，碰到瞬间消失
4. 累计 XP 到阈值 → 游戏暂停，ImGui 弹窗显示 3 张卡
5. 点卡 → 数值生效（如选「+15% 伤害」，下一发子弹伤害约从 6→7）
6. 同一帧多次升级（连吃多个 XP 球）会连续弹 3 次卡
7. Material 数量 spdlog 打印（HUD 在 M7）

### 注意
- 临时升级 UI 在 M5 用最简 `Begin/Button/End`，不抠样式；M7 抛光阶段做卡片视觉
- **不要**在 M5 加 stat 上限封顶（如 atkSpeedPct 不超 200%），MVP 期允许玩家 stack 出离谱数值（爽快！）
- **不要**让升级时玩家位置/敌人位置发生跳变 — 暂停期间所有 worldPos 保持上一帧值
- 升级卡的「+15% 伤害」应用：把 `stats.damagePct += 0.15`，下一发子弹生成时**重新算** `damage = baseDamage * (1+damagePct) + damageFlat`，**不要**改已经飞出去的子弹
- pickupPool_ 大小 256 一般够，超了就**最远的一个先消失**（直接覆盖）；记得在覆盖前把旧 Node visible 关掉

---

## M6. 波次系统 + 难度曲线 + 波间过渡

**优先级**: P0  **工时**: ~1h  **依赖**: M3（敌人 spawn 接口）

### 背景

让游戏有节奏。完成后游戏自动按 5 波推进：每波倒计时 30s，敌人按 `waves.json` 调度自动 spawn；倒计时到 0 → 清场（剩余敌人立即移除）→ 进入「商店阶段」（M8 实现 UI；M6 期 5s 自动跳过 + spdlog 打日志）→ 下一波。

### TODO
- [ ] 写 `assets/configs/brotato3d/waves.json`（按上文 schema 填 5 波）
- [ ] 在 `Brotato3DDataLoader` 加 `LoadWaves` + `struct FWaveDef { int durationSec; vector<FSpawnEntry> spawns; }`，`FSpawnEntry { string enemyId; int count; float intervalMs; }`
- [ ] 写 `Brotato3DWaveSystem.hpp/cpp`：
  ```cpp
  enum class EWaveState : uint8_t { Idle, Active, Intermission, AllCleared };
  class FWaveSystem {
  public:
      void LoadWaves(std::vector<FWaveDef> waves);
      void StartGame();          // 切到 wave 0 active
      void Update(double dt, std::function<void(const std::string& enemyId, glm::vec3 pos)> spawnCallback);
      EWaveState GetState() const;
      int GetCurrentWaveIndex() const;
      float GetWaveTimeRemainingSec() const;
      float GetIntermissionTimeRemainingSec() const;
      void EnterShop();          // M8 商店 UI 接入；M6 期内部直接进 Intermission
      void EndIntermissionAndAdvance();
  private:
      // 每个 spawn entry 维护 spawnedCount / nextSpawnTimerMs
  };
  ```
- [ ] spawn 位置：从 4 边随机选边 + 沿边随机 X/Z 取点；偏移 1m 避免出现在边界条上
- [ ] 在 `Brotato3DGameInstance::OnInit` 调 `waveSystem_.LoadWaves(...)` 后 `waveSystem_.StartGame()`
- [ ] `OnTick` 内（仅 `appState_ == Playing` 时）调 `waveSystem_.Update(dt, [this](id, pos){ SpawnEnemy(...); })`
- [ ] 波次切换：当 `waveSystem_` Active 状态计时到 0：
  - 强制清场：把所有 alive 敌人 `KillPiece`（**不**触发掉落，**不**给 XP），动画淡出
  - state → Intermission，倒计时 5s（M6 期占位；M8 替换为商店 UI 决定的等待时长）
  - Intermission 完成 → 下一波，state = Active，重置 spawn 表
  - 5 波全过 → state = AllCleared，spdlog 输出 `[victory]`
- [ ] 玩家死亡（M3 已检测）：state → AllCleared 同分支但记录 `playerDead = true`（M8 区分胜负）
- [ ] **难度曲线（MVP 期由 waves.json 直接配死，不用代码动态调）**：M6 不写动态难度系数；只读静态表

### 涉及文件
- 新建：`assets/configs/brotato3d/waves.json`、`src/Application/Brotato3D/Brotato3DWaveSystem.{hpp,cpp}`
- 改：`Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DEnemy.cpp`（暴露清场 API）

### 验收方法
1. 编译通过
2. 启动后立即进入 wave 1，30s 内不断有 rat 从场地外出现
3. 30s 到 → 场上所有敌人淡出 → spdlog 出现 `[Wave 1 ended] [Intermission start]`
4. 5s 后进入 wave 2，难度提升（rat 间隔变 900ms + 出现 spitter 绿球）
5. 一直存活到 wave 5 结束 → spdlog 输出 `[victory]`
6. 中途玩家死亡（M3 已实现 HP<=0 检测）→ spdlog 输出 `[defeat]`，state 切到 AllCleared
7. 升级 modal 弹出时（M5）波次倒计时和 spawn 调度暂停

### 注意
- 敌人 spawn 完全由 wave system 驱动 — **去掉 M3 的 `K` 键 spawn 逻辑**（或保留但仅在 debug build 启用）
- spawn callback 拿到 enemyId 后调 GameInstance 的 `SpawnEnemy(enemyDefs_[id], pos)`，不要在 wave system 内直接持有 enemy 容器引用（解耦）
- 暂停语义对 wave system 同样适用：`appState_ != Playing` 时**不调** `waveSystem_.Update`
- 清场动画用 0.4s 淡出（颜色×0.4，y 下沉 0.3），不要瞬移消失（视觉太硬）
- **不要**在 M6 实现多波次间的属性继承（玩家 stat 自动跨波保留，已经是默认行为）

---

## M7. HUD + 飘字伤害

**优先级**: P0  **工时**: ~1h  **依赖**: M5, M6

### 背景

让玩家看到关键信息。完成后屏幕上有：左上 HP 条 + XP 条、右上当前 wave + 倒计时 + 材料数、左下武器图标列、敌人头顶 HP 条（残血时）、伤害飘字、升级卡 modal 美化。

### TODO
- [ ] 写 `Brotato3DUI.hpp/cpp`，函数 `RenderHUD(GameInstance&)`、`RenderUpgradeModal(GameInstance&)`、`RenderResultModal(GameInstance&)`，被 `OnRenderUI` 按 state 调用
- [ ] **左上玩家面板**（pos=(8,8), size=(280,90)）：
  - HP 条：红→黄→绿渐变，文本 `"HP {cur} / {max}"`
  - XP 条：蓝色，文本 `"Lv {N}  XP {cur}/{next}"`
  - **不**画头像（MVP 省）
- [ ] **顶部中央波次面板**（pos=((W-260)/2, 8), size=(260, 50)）：
  - 文本 `"Wave {N+1} / 5"`，字体放大
  - 倒计时 `"剩余 {s} 秒"`，<5s 红色脉冲
  - Intermission 期间显示 `"商店阶段 (M8 接入)"`
- [ ] **右上资源**（pos=(W-160, 8), size=(150, 50)）：
  - `"💰 材料 {N}"`（不用 emoji，用纯文本 `"材料"`，字体支持有限时英文 fallback `"Materials: N"`）
- [ ] **左下武器槽**（pos=(8, H-90), size=(140, 80)）：
  - 每个装备的武器一个彩色方框 + 名字 + 当前 cooldown 百分比条（剩余 cooldown / total）
- [ ] **敌人 HP 条**（foreground draw list 投影）：
  - 仅 `currentHp < maxHp * 0.7` 时绘制
  - 在 enemy 头顶（`worldPos + vec3(0, size.y+0.2, 0)`）画 30px×3px 的红色矩形 + 当前血条
  - 投影：用 `engine_->GetScene().GetRenderCamera()` 拿 `ViewProj`，世界坐标 × ViewProj → NDC → 像素
- [ ] **飘字伤害**：
  - 在 GameInstance 维护 `std::vector<FFloatingText> floatingTexts_;`，结构 `{ glm::vec3 worldPos; std::string text; glm::vec4 color; float lifeMs; float remainingMs; }`
  - 子弹命中（M4）时 push 一个 `{enemy.worldPos + vec3(0,0.8,0), "-{damage}", red, 600ms, 600ms}`
  - 玩家受伤（M3）时 push `{player.worldPos + vec3(0,1,0), "-{damage}", purple, 700ms, 700ms}`
  - 拾取（M5）时：XP 球 push `{player.worldPos, "+{value} XP", green, 500ms, 500ms}`，Material 同理黄色
  - `OnTick` 内 `remainingMs -= dt*1000`，到时移除；UI 渲染时把 worldPos 投到屏幕，y 在生命周期内向上漂 30px，alpha = `remainingMs / lifeMs`
- [ ] **升级卡 modal 美化**（M5 临时占位替换）：
  - `ImGui::OpenPopupModal("升级")` 居中
  - 3 张卡横排：每张 `BeginChild` 200×260，标题 + 描述 + 「选择」按钮，hover 高亮
- [ ] HUD 在所有 `appState_` 都画（包括暂停期间）；倒计时只在 `Playing` 推进

### 涉及文件
- 新建：`src/Application/Brotato3D/Brotato3DUI.{hpp,cpp}`
- 改：`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DEnemy.hpp`（受击时 push 飘字）

### 验收方法
1. 编译通过
2. 启动后看到左上 HP+XP 条、顶部 wave 倒计时、右上材料数、左下 SMG 武器图标 + cooldown 条
3. 击杀敌人有红色飘字 `-6`，吃 XP 球有绿色飘字 `+1 XP`
4. 残血敌人头顶有 HP 条
5. 玩家受伤有紫色飘字
6. 升级 modal 视觉清晰，3 张卡居中
7. 倒计时 < 5s 红色脉冲

### 注意
- 飘字不要为每条都创建 ImGui Window — 用 `ImGui::GetForegroundDrawList()->AddText(...)` 一次性绘制
- 投影函数封装为 `bool WorldToScreen(const glm::vec3& world, ImVec2& outScreen)`，被多处复用
- 中文显示若默认字体不支持，**MVP 期所有 HUD 文本用英文 fallback**（如 `"Wave 1/5"`、`"Materials: 8"`）；M9 抛光阶段再考虑加字体（参考 [`gkNextRenderer.cpp:251`](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) 的 `AddFontFromFileTTF` 调用）
- 武器 cooldown 条用 `(weapon.cooldownMs / weaponMaxCooldown) → 1.0 - x`（满即攻击）
- HP 条颜色渐变：`hpRatio > 0.6` 绿、`> 0.3` 黄、否则红，用 `ImLerp` 平滑过渡

---

## M8. 商店 + 死亡/胜利结算 + 抛光

**优先级**: P0  **工时**: ~1.5h  **依赖**: M5, M6, M7

### 背景

完成完整一局闭环（开始 → 5 波 → 商店 → 升级 → 死亡或通关 → 结算 → 重开）。

### TODO
- [ ] 写 `assets/configs/brotato3d/shop_items.json`（按上文 schema）
- [ ] 在 `Brotato3DDataLoader` 加 `LoadShopItems`
- [ ] 写 `Brotato3DShop.hpp/cpp`：
  - `class FShop { void Roll(int count, std::vector<FShopItemDef>& outOffer); void Buy(int slotIndex); void Reroll(); }`
  - 滚刷规则：从 `shop_items.json` 按 weight 抽 4 张（不重复）
  - Reroll 价格 = `2 + waveIndex`（上调 reroll 经济）
- [ ] **波间商店流程**（替换 M6 的 5s 占位）：
  - `EWaveState::Intermission` 进入时调 `shop.Roll(4)`，`appState_ = Shopping`
  - UI（在 `Brotato3DUI.cpp` 加 `RenderShopModal`）：
    - 居中商店窗口 600×400
    - 顶部 `"商店阶段 - 波 {N}/5 结束"` + 当前材料数
    - 4 张卡横排：名字 + 描述 + 价格 + 「购买」按钮（材料不足时灰显禁用）
    - 底部「重抽」按钮（消耗材料）+「下一波」按钮
    - 点「下一波」→ `appState_ = Playing`，调 `waveSystem_.EndIntermissionAndAdvance()`
  - 治疗类商品（`buy_heal`）：购买后 `currentHp = min(maxHp, currentHp + maxHp * delta)`
- [ ] **结算 UI**：
  - `appState_ = Result` 触发条件：`waveSystem_.state == AllCleared`
  - 居中半透明 modal（500×300）：
    - 胜利：金边深蓝，标题 `"VICTORY"`，副标题 `"幸存 5 波"`
    - 失败：红边深红，标题 `"DEFEAT"`，副标题 `"死亡于 Wave {N+1}"`
  - 显示统计：本局耗时（分钟:秒）/ 最高等级 / 总击杀（每次敌人死亡 +1 计数）/ 总材料获得
  - 两个按钮：`"再来一局"` / `"退出"`
- [ ] **再来一局**：完整重置（不重启进程）：
  - 玩家：`currentHp = maxHp = 50`、`currentXp = 0`、`level = 1`、`materials = 0`、`stats = FPlayerStats{}`、武器槽重置为 SMG
  - 敌人池：所有 alive 设 false，Node visible 关
  - 子弹池：所有 active 设 false
  - 拾取池：清空
  - 飘字队列：清空
  - WaveSystem.Reset()
  - `appState_ = Playing`
  - **不**重建 Scene（开销大），只复用现有 Node
- [ ] **抛光**：
  - 屏幕震动：玩家受伤时 `screenShakeMs = 150`，OnTick 内随机偏移 ImGui 主 viewport（或手动加 camera offset）
  - 击杀计数：`int killCount` 每次敌人死亡 +1，结算 UI 显示
  - 升级 modal 弹出时屏幕加暗（ImGui foreground 全屏半透明黑矩形 alpha=0.4）
  - 击中红闪：M4 占位的 `hitFlashRemainingMs` 实际生效 — 受击瞬间 enemy material 变白色（`color * 1.0` 加权 `vec3(1)` 80ms 后还原）；做法：每个敌人额外维护一个 hitFlashMaterialId，受击时 `node->SetMaterial({hitFlashMaterialId})`，80ms 后切回原色

### 涉及文件
- 新建：`assets/configs/brotato3d/shop_items.json`、`src/Application/Brotato3D/Brotato3DShop.{hpp,cpp}`
- 改：`Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DUI.{hpp,cpp}`、`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DEnemy.{hpp,cpp}`

### 验收方法
1. 编译通过
2. wave 1 结束 → 商店 modal 弹出，4 张卡 + 当前材料数显示
3. 点「+5 伤害」（成本 12）：若材料 ≥ 12 → 扣 12 + stat 生效（下一波伤害提升）；不足 → 按钮灰
4. 点「重抽」消耗材料 + 4 张卡刷新
5. 点「下一波」→ 关 modal + 进 wave 2
6. 死亡 → 结算 UI 显红色 `"DEFEAT"` + 统计
7. 通关 5 波 → 结算 UI 显金色 `"VICTORY"` + 统计
8. 点「再来一局」→ 完全重置，可立即再来
9. 受伤有屏幕震动 + 击中敌人有红闪
10. 升级时屏幕变暗

### 注意
- 商店打开时**不**强制 Reroll 一次（保留 wave 进入时的 4 张卡，玩家自己判断要不要 reroll）
- Reroll 价格指数级增长太狠 — MVP 期固定线性 `2 + waveIndex`
- 屏幕震动**不要**做大幅度（位移 ≤ 4 像素），否则晕；振幅 = `min(4, screenShakeMs/30)`
- 重置时**不要**重置 wave 数据 / 升级卡库 / 商店物品库（这些是数据，整局不变）
- 击中红闪用预创建的 `hitFlashMaterialId`（白色 Lambertian），不要每次受击新建材料
- 结算 modal 用 ImGui Style Push/Pop 改背景色 + 边框；**不要**画到 foreground draw list（modal 需要拦截输入）

---

## M9.（可选）第二把武器 + 特效池 + 音效

**优先级**: P2  **工时**: ~1h  **依赖**: M8

### 背景

锦上添花。让武器槽用满 2 个、子弹有差异化外观、击杀有简单 hit-stop 反馈。

### TODO
- [ ] 起手装备扩展：第二个武器槽位 SMG 复制一把（双 SMG 火力翻倍）；或 wave 3 商店出现「武器升级」固定卡，购买后第二槽换成 shotgun
- [ ] shotgun 实现：单发开火打 5 发 pellet，每发独立子弹、独立伤害、扇形发散
- [ ] 子弹外观差异：每把武器对应自己的 projectile pool（不同 model size + color）
- [ ] 命中粒子：在击中位置 spawn 3 个小立方体碎片（用 KongLie3D 的 `FImpactDebrisPoolEntry` 套路），随机 velocity，0.4s 后失活
- [ ] 简单 SFX：用 `NextAudio`（若已可用），开火/击中/拾取/升级各一个 wav；音效缺失就 spdlog 占位（**不**要为此引入 SDL_mixer / OpenAL 新依赖）
- [ ] Hit-stop：玩家被精英怪（tank）击中时，全游戏暂停 80ms（`appState_` 临时切到 `Playing` 之外的 `Hitstop` 子态，OnTick 跳过更新）

### 涉及文件
- 改：`weapons.json`（shotgun 启用）、`Brotato3DWeapon.{hpp,cpp}`、`Brotato3DProjectile.{hpp,cpp}`、`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DUI.cpp`（左下显示 2 个武器槽）

### 验收方法
1. 编译通过
2. 装备双武器后左下武器槽显示 2 个图标
3. shotgun 单发产生 5 道扇形子弹
4. 子弹外观随武器变化（SMG 黄、shotgun 橙）
5. 击中敌人有小碎片飞溅
6. 被 tank 击中时画面短暂卡顿（hit-stop）

### 注意
- 不要因为做这个任务回头去改 M1–M8 的核心结构 — M9 是叠加项
- 音效可选：若 NextAudio 集成成本高，**直接跳过**音效部分

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target Brotato3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀；常量 camelCase |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 平台 | 用 `PlatformCommon.h`，不直接 include 平台头 |
| Vulkan | 所有 VkResult 用 `VK_CHECK_RESULT`；RAII 资源管理 |
| 注释 | 默认不写注释，仅写非显然的 WHY |
| 提交 | 不要执行 git commit；只完成代码改动，由用户决定何时提交 |
| 沟通 | 与用户用中文对话 |

**禁止**：
- 修改 `src/ThirdParty/` 或 `external/`
- 引入新大型依赖（Bullet、SDL_mixer 等）；MVP 期所有逻辑用 stdlib + 已有 vcpkg 包
- 在任务卡范围之外做"顺手清理"
- 把代码写到注释里 — 删掉的代码就是删掉，不留 `// removed`
- 在升级/商店/结算 modal 里做长动画 — modal 是 functional UI，不是 cinematic

## 验证完整端到端

完成 M1–M8 后，端到端跑一遍：

1. `./build.bat --preset full-windows --reconfigure` 通过，无 warning regression
2. `./run.bat --preset full-windows --target Brotato3D` 启动
3. 进入 wave 1：3 秒内开始 spawn rat
4. WASD 移动玩家，自动开火击杀 rat → 掉 XP/材料 → 磁吸 → 满 XP 升级 → 选 1 张属性卡
5. 30s 倒计时到 → 清场 → 商店 modal 4 张卡 + 重抽 + 下一波按钮
6. 点击「下一波」进入 wave 2 → spitter 出现
7. 持续到 wave 5：tank 出现，难度明显提升
8. 通关 5 波 → 金色「VICTORY」结算 + 统计数据
9. 中途送死复现：黄色「DEFEAT」结算 + 死亡波次
10. 「再来一局」→ 完全重置，立即可再来

**不需要**：单元测试（这是游戏 demo，行为通过手玩验证更直接）；视觉测试（Brotato3D 不进 visual_test.json，因为它高度交互式）。

## 风险与备注

| 风险 | 应对 |
|---|---|
| 大量 Node 动态创建拖慢 Scene rebuild | 全部用对象池（projectile/pickup/enemy）；Node 只在 `BeforeSceneRebuild` 阶段一次性创建 + visible 切换；M3 暂可允许真删，M4 之后改池化 |
| 自动瞄准 O(n²) 性能 | n < 200，每帧 4 万次足以；若 wave 5 满屏 80+ 敌人 + 256 子弹仍有问题，考虑给 enemies 按 X 坐标排序后 binary search 范围 |
| 摄像机 FOV / 视角不舒服 | 在 M1 验收阶段如果觉得太斜或太垂直，可以调 `eye.y` 与 `eye.z`；保持 `lookAt = origin` 不动 |
| 中文字体 | M1–M7 用英文 HUD；M8 抛光阶段若有空再加 ImGui::AddFontFromFile + 字符范围 |
| 同帧多次升级（连吃多球） | `pendingLevelUps_` 计数，每次关 modal 后检查是否 > 0，是则继续抽下一组 |
| 暂停期间继续画 HUD 但卡住 wave | `if (appState_ != Playing) return;` 加在 OnTick 顶部，UI 部分不受影响 |
| Wave 清场时还有飞行子弹 | 清场只杀 enemies，**保留** projectiles（让它们自然 lifetime 到期），避免视觉断裂 |
| Material 数值 vs upgrades.json 平衡 | 若发现某 wave 难度跳变太硬：调 `waves.json` 的 intervalMs 或 enemies.json 的 hp / contactDamage —— **数值是配置层**，不动代码 |

## 后续 agent 调用建议

每个任务（M1, M2, ...）适合用一个独立 agent 调用执行，prompt 模板：

```
请执行 docs/projects/brotato-3d/plan.md 中的 M{N} 任务。
- 严格按 TODO 清单做，不要扩大范围
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报
- 不要 commit
- 与用户沟通用中文
```

完成 M8 后整体复盘是否需要 M9。如果某个任务涉及多个文件且 TODO 较长（如 M4 / M8），可以拆成两个 sub-agent 调用先后跑（如 M4a = 武器系统 + 自动瞄准，M4b = 子弹池 + 命中判定 + 死亡动画）。

## 后续扩展方向（不在 MVP 内，后续迭代时可参考）

- **角色系统**：JSON 配 5 个角色，起始 stat 偏置（如「Soldier」起手 SMG +20% 攻速；「Brawler」起手近战武器 +50% 伤害但 -30% 射程）
- **武器升级树**：3 把同类武器合 1 把高级版（仿 Brotato 的 tier 系统）
- **物品系统**：被动 item，影响 stat（不是即时消耗）
- **危险敌人**：每波尾段 spawn 1–2 只精英（HP/伤害 ×2，掉率 ×3）
- **Boss wave**：wave 5 改为单只大 boss
- **多关卡 / 主菜单**：选角色 + 选难度
- **存档**：本地通关记录
