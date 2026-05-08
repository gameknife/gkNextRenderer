# Brotato 3D — 地图扩展 / 地表材质 / 跟随相机改造计划

## Context

当前 Brotato3D 的竞技场是 24m × 16m 的固定尺寸（`ArenaHalfWidth=12.0f` / `ArenaHalfDepth=8.0f`，写死在 [Brotato3DCommon.hpp:16](../../../src/Application/Brotato3D/Brotato3DCommon.hpp:16)），地表是单一 `Lambertian` 材质（[Brotato3DArena.cpp:21](../../../src/Application/Brotato3D/Brotato3DArena.cpp:21)），边框是 0.4m 高的纯视觉装饰盒（[Brotato3DArena.cpp:38](../../../src/Application/Brotato3D/Brotato3DArena.cpp:38)），相机在 `OverrideRenderCamera` 里固定瞄准 `(0, 0, 0)`（[Brotato3DEffectSystem.cpp:213](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:213)）。

本计划要把这套改造成：

1. **地图明显变大**（约 3× 面积起步），尺寸从 `arenas.json` 数据驱动，每个 arena 可自定义大小
2. **地表分区材质**：同一张地图里能分区切换材质模型 —— Lambertian 粘土感（已有）、Metallic 金属、Mixture/Dielectric 光滑塑料感
3. **物理空气墙**：边界做成高的静态物理墙体（>= 4m 高、足够厚），保证 debris / Material box / kinematic 角色的 dynamic body 不会被冲量打出地图
4. **跟随相机**：相机锁定主角 XZ 位置 + 平滑 lerp，地图边缘做相机框限制（避免镜头超出地表露出空白），保留现有屏幕震动叠加

> **本计划的前提**：MVP / Feel 阶段（[plan.md](plan.md) / [feel-polish-plan.md](feel-polish-plan.md)）已落地。改造不破坏：升级 / 商店 / hit-stop / 屏幕震动 / 暴击 / 拾取磁吸 / 战利品计数 / debris 物理。

## 引擎可复用能力（不新造轮子）

| 需求 | 复用 | 文件路径 |
|---|---|---|
| 多种材质模型 | `Assets::Material::Lambertian/Metallic/Mixture/Dielectric` | [src/Assets/Data/Material.hpp:10-37](../../../src/Assets/Data/Material.hpp:10) |
| 材质注册到场景 | `SceneBuilder::AddLambertianMaterial` 已有；其他 model 在 Arena 里直接 `materials.push_back({Material::Metallic(...), name})` | [src/Runtime/Scene/SceneBuilder.cpp:10](../../../src/Runtime/Scene/SceneBuilder.cpp:10) |
| 静态物理 box | `NextPhysics::CreateBoxBody(pos, extent, NextMotionType::Static)` | [src/Runtime/Subsystems/NextPhysics.h:72](../../../src/Runtime/Subsystems/NextPhysics.h:72) |
| 物理代理 Node 范式 | Brotato3D 的 `attachPhysicsProxyNode` workaround | [src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:253](../../../src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:253) |
| 相机覆盖钩子 | `OverrideRenderCamera`（`const` 接口） | [src/Application/Brotato3D/Brotato3DEffectSystem.cpp:211](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:211) |
| ProcModel | `Assets::FProcModel::CreateBox` | [src/Assets/Loaders/FProcModel.h](../../../src/Assets/Loaders/FProcModel.h) |
| 数据驱动 arenas | `Brotato3DDataLoader::LoadArenas` + `arenas.json` | [src/Application/Brotato3D/Brotato3DDataLoader.cpp](../../../src/Application/Brotato3D/Brotato3DDataLoader.cpp), [assets/configs/brotato3d/arenas.json](../../../assets/configs/brotato3d/arenas.json) |

## 影响面盘点（改 `ArenaHalfWidth/Depth` 时必须同步的位置）

`grep` 出来需要联动改的地方：

- [Brotato3DCommon.hpp:16-31](../../../src/Application/Brotato3D/Brotato3DCommon.hpp:16) — `ArenaHalfWidth/Depth` 常量 + `ClampToArena` 内联
- [Brotato3DArena.cpp:27-66](../../../src/Application/Brotato3D/Brotato3DArena.cpp:27) — 地表 box / 边界 box 的 min/max
- [Brotato3DWaveSystem.cpp:152-153](../../../src/Application/Brotato3D/Brotato3DWaveSystem.cpp:152) — 调试 spawn 范围 `xDist / zDist`
- 任意调用 `Brotato3DUtil::ClampToArena` 的 player / pickup / debris 逻辑（grep 全工程）

> **重要**：`ClampToArena` 现在是 `inline constexpr` 常量驱动的；改造后必须改为运行时参数（GameInstance 持有 `arenaHalfWidth_ / arenaHalfDepth_`，把 helper 改成接受参数的自由函数或 GameInstance 成员方法）。**不允许保留旧常量做 fallback**，否则会出现"游戏拿大地图，spawn 用旧 12/8"的隐 bug。

## 改造后的目标行为

完成 A1–A5 后，端到端体验：

1. **默认 grassland 地图**：地表 60m × 40m（>= 现状 6×），中心仍在 (0,0,0)，玩家初始位置不变
2. **地表分 4–8 个区域**：每个区域是地表平面上的一个 axis-aligned 矩形 quad，材质模型（lambertian / metallic / mixture）由 `arenas.json` 配置；不同区域之间不留接缝，整张地图视觉上是一块拼接地面
3. **每个 arena 视觉差异**：grassland（粘土主导，少量金属补丁）、wasteland（粘土 + 沙地塑料）、tech_grid（大面积金属 + 塑料网格），通过 JSON 参数化
4. **物理墙**：4 面边界各一个静态 box body（高度 5m、厚度 0.5m），位于地表边缘外侧 0.05m 处；视觉墙体同步高到 4m（让玩家能看见边界），墙体 material 用对应 arena 的 borderMaterial（保持 lambertian）
5. **debris / material box / 玩家与敌人 kinematic body 不会飞出地图**：射击 wave 持续 30s 后，所有 dynamic body 都还在地图内（可视化测试）
6. **相机跟随**：主角移动时相机以 `lerp(currentTarget, playerXZ, k * dt)` 平滑跟随；相机 target 在地图四周做 clamp（保证视野内不出现地表外的虚空）；屏幕震动 jitter 仍叠加在跟随后的 target 上
7. **死亡 / Result / Pause / MainMenu / CharacterSelect**：相机锁定在最后一次跟随位置，**不**回归原点（保留沉浸感）；MainMenu 走老的固定全景视角
8. **不破坏既有功能**：现有 unit test + visual test 全过；feel-polish 阶段的 debris 物理推力 / 拾取磁吸 / 击杀冲量手感不变

## 数据结构与配置变更

### `FArenaDef` 扩展（[Brotato3DDataLoader.hpp:146](../../../src/Application/Brotato3D/Brotato3DDataLoader.hpp:146)）

```cpp
namespace Brotato3D
{
    enum class EGroundMaterialKind : uint8_t
    {
        Lambertian = 0,   // 现有粘土感
        Metallic = 1,     // 金属（fuzziness 0.05–0.4）
        Mixture = 2,      // 光滑塑料（介电+漫反射混合，fuzziness 0–0.1）
    };

    struct FGroundTileDef
    {
        glm::vec2 minXZ = glm::vec2(0.0f);     // 相对地图中心的 XZ
        glm::vec2 maxXZ = glm::vec2(0.0f);
        glm::vec3 color = glm::vec3(0.5f);
        EGroundMaterialKind kind = EGroundMaterialKind::Lambertian;
        float fuzziness = 0.1f;                // metallic / mixture 用
        float refractionIndex = 1.45f;         // mixture 用
    };

    struct FArenaDef
    {
        std::string id;
        std::string name;
        glm::vec2 halfExtent = glm::vec2(12.0f, 8.0f);  // 新字段，缺省 = MVP 老尺寸
        glm::vec3 baseGroundColor = glm::vec3(0.32f, 0.40f, 0.28f); // 用于"未被 tile 覆盖"区域的 fallback
        glm::vec3 borderColor = glm::vec3(0.45f, 0.55f, 0.35f);
        std::vector<FGroundTileDef> groundTiles;        // 新字段，可空（空 = 整片 baseGroundColor lambertian）
    };
}
```

### `arenas.json` 新 schema（向后兼容 — 缺字段用旧默认）

```json
{
  "arenas": [
    {
      "id": "grassland",
      "name": "绿野",
      "halfExtent": [30.0, 20.0],
      "baseGroundColor": [0.32, 0.40, 0.28],
      "borderColor": [0.45, 0.55, 0.35],
      "groundTiles": [
        { "min": [-30, -20], "max": [-10,  20], "kind": "lambertian", "color": [0.32, 0.40, 0.28] },
        { "min": [-10, -20], "max": [ 10,  20], "kind": "mixture",    "color": [0.55, 0.55, 0.50], "fuzziness": 0.05, "ior": 1.45 },
        { "min": [ 10, -20], "max": [ 30,  20], "kind": "metallic",   "color": [0.65, 0.62, 0.55], "fuzziness": 0.20 }
      ]
    },
    {
      "id": "tech_grid",
      "name": "数码",
      "halfExtent": [40.0, 28.0],
      "baseGroundColor": [0.18, 0.22, 0.30],
      "borderColor": [0.35, 0.55, 0.85],
      "groundTiles": [
        { "min": [-40, -28], "max": [40, 28], "kind": "metallic",  "color": [0.55, 0.65, 0.85], "fuzziness": 0.10 },
        { "min": [-12, -10], "max": [12, 10], "kind": "mixture",   "color": [0.85, 0.85, 0.90], "fuzziness": 0.02, "ior": 1.50 }
      ]
    }
  ]
}
```

> tile 顺序定义绘制顺序（后画的盖前面），缺省顺序：先 baseGroundColor 大底，再依次叠 tile。

## 任务拆分（每张卡可由独立 agent 单次会话完成）

### A1 · 数据层 + 配置层扩容（不动渲染 / 物理 / 相机）

**目标**：拓宽 `FArenaDef` 与 `arenas.json` schema，把地图尺寸 / tile 列表从硬编码迁到数据驱动；运行后游戏视觉 / 玩法**完全不变**（兼容旧字段 + 新字段 fallback 到旧默认）。

**改动**：
1. [Brotato3DDataLoader.hpp](../../../src/Application/Brotato3D/Brotato3DDataLoader.hpp): 新增 `EGroundMaterialKind` / `FGroundTileDef`，`FArenaDef` 加 `halfExtent / baseGroundColor / groundTiles` 字段（保留 `groundColor` alias 兼容旧 JSON）
2. [Brotato3DDataLoader.cpp](../../../src/Application/Brotato3D/Brotato3DDataLoader.cpp) `LoadArenas`: 解析新字段；缺字段用旧默认值（`halfExtent = [12, 8]`、`groundTiles = []`）；解析失败用 spdlog warn 不退出
3. [Brotato3DGameInstance.hpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp): 加 `glm::vec2 arenaHalfExtent_ = glm::vec2(12.0f, 8.0f)` 成员；`ApplySelectedArena()` 内根据当前 arena 写入；提供 `GetArenaHalfExtent() const` getter
4. [Brotato3DCommon.hpp:16-31](../../../src/Application/Brotato3D/Brotato3DCommon.hpp:16): **删除** `ArenaHalfWidth/Depth` 常量；`ClampToArena` 改成 `ClampToArena(const glm::vec3& pos, float radius, const glm::vec2& halfExtent)`；编译错误处全部回到 GameInstance 拿 runtime 值
5. [Brotato3DWaveSystem.cpp:152-153](../../../src/Application/Brotato3D/Brotato3DWaveSystem.cpp:152): spawn 范围用 `arenaHalfExtent_`；如果 WaveSystem 拿不到 GameInstance，加构造参数或在 spawn 调用点传入

**验收**：
- 编译过 `full-windows`
- 启动 Brotato3D，地图与之前完全一致（24×16），无视觉变化
- 切换 arena（grassland / wasteland / tech_grid）行为同 MVP

**不做**：物理墙、新材质、相机改动 —— 都是后续任务

### A2 · 地表分区材质渲染（不动物理 / 相机）

**目标**：让 `BuildArena` 根据 `arenaDef.groundTiles` 生成多个地表 quad，每个 quad 用自己的 material（lambertian / metallic / mixture）；运行后 grassland / tech_grid 视觉差异可见（金属反射、塑料半光泽）。

**前置依赖**：A1 已合入。

**改动**：
1. [Brotato3DArena.hpp](../../../src/Application/Brotato3D/Brotato3DArena.hpp) `FArenaResources`:
   - 把单一 `groundModelId / groundMaterialId / groundNode` 改为 `std::vector<std::shared_ptr<Assets::Node>> groundNodes` + 每节点的 model/material id 内嵌于 node
   - 保留 `groundMaterialIds[arenaId]` 给 fallback `baseGroundColor` 用
2. [Brotato3DArena.cpp](../../../src/Application/Brotato3D/Brotato3DArena.cpp) `BuildArena`:
   - 第一步：用 `arenaDef.halfExtent` 生成"大底"地表 box（厚度 0.05，颜色 = `baseGroundColor`，lambertian）
   - 第二步：遍历 `arenaDef.groundTiles`，每个 tile 生成一个紧贴大底**之上 0.001m**的 box（避免 z-fighting），按 `kind` 创建 material：
     - `Lambertian`：`Material::Lambertian(color)`
     - `Metallic`：`Material::Metallic(color, fuzziness)`
     - `Mixture`：`Material::Mixture(color, fuzziness)`（注意 Mixture 走介电+漫反射混合，更接近"光滑塑料"）
   - material 直接 `materials.push_back({mat, name})`，不走 `SceneBuilder::AddLambertianMaterial`（它只支持 lambertian）
   - 每个 tile 对应一个 RenderNode（`SceneBuilder::CreateRenderNode`），`rayCastVisible = true`
3. ID 命名：`Brotato3D_GroundBase`、`Brotato3D_GroundTile_{i}_{kind}`，便于调试

**注意**：
- 不要把地表 z-stack 高度堆得过高，所有 tile 都在 y∈[-0.05, 0.001] 微薄层；玩家 y=0.5 的 worldPos 不会与之穿插
- `Material::Mixture` 的 `fuzziness` 越小越光滑；建议 `0.0–0.1` 段（光滑塑料）；`Material::Metallic` 建议 `0.1–0.4`（轻微磨砂金属）；过低会让 path tracing 噪点爆炸
- 给 grassland 配 1–2 个 metallic 补丁，给 tech_grid 配大面积 metallic + 中心 mixture，让差异肉眼可分辨

**验收**：
- 编译过 `full-windows`
- 启动 Brotato3D，进入战斗后地表能看到反射 / 半光泽差异（在路径追踪管线下尤其明显）
- 跑 `gkNextVisualTest` 截图比对：grassland 截图能看到金属反射 sky / 周围 debris

**不做**：物理墙、相机、新增 SceneBuilder helper（Lambertian 之外的可以后续补）

### A3 · 静态物理空气墙（不动相机）

**目标**：四面边界做 5m 高、0.5m 厚的静态物理墙，材质沿用 arena `borderColor`（lambertian），dynamic body（debris / material box / kinematic player & enemy）撞墙后被弹回，不会飞出地图。

**前置依赖**：A1 已合入；A2 可独立并行（不冲突）。

**改动**：
1. [Brotato3DArena.cpp](../../../src/Application/Brotato3D/Brotato3DArena.cpp) `BuildArena`：
   - 视觉墙：把现有 4 面边界 box 的 Y 范围从 `[0, 0.4]` 改为 `[0, 4.0]`（让玩家肉眼可见有"墙"），厚度仍 0.2m；位置在 arena 边缘（`±halfExtent.x ± wallThickness/2`）
   - 视觉墙 material 沿用 `borderMaterialId`
2. [Brotato3DGameInstance.hpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp): 加 `std::array<NextBodyID, 4> arenaWallBodyIds_{}` 成员
3. 新增方法 `Brotato3DGameInstance::BuildArenaWallBodies()`（推荐放 [Brotato3DDebrisSystem.cpp](../../../src/Application/Brotato3D/Brotato3DDebrisSystem.cpp)，与 `BuildKinematicCollisionBodies` 同区）：
   - 在 `BuildKinematicCollisionBodies` 之前 / 之后调用一次
   - 用 `physics->CreateBoxBody(center, extent, NextMotionType::Static)` 创建 4 面墙
     - 北墙 / 南墙：center = `(0, 2.5, ±halfDepth + 0.25)`，extent = `(halfWidth + 0.5, 2.5, 0.25)`
     - 东墙 / 西墙：center = `(±halfWidth + 0.25, 2.5, 0)`，extent = `(0.25, 2.5, halfDepth + 0.5)`
   - **不**给静态墙挂代理 Node（静态 body 不会被 RebuildMeshBuffer 替换；feel-polish 计划里的代理 Node workaround 只适用 kinematic）
4. 重建 arena 时（切换地图、重开局）：先 `physics->RemoveBody` 旧墙，再创建新墙
5. 在 `OnDestroy` 里清理 `arenaWallBodyIds_`

**验收**：
- 编译过 `full-windows`
- 战斗中开高频武器（机枪 / 霰弹），让大量 debris / material box 朝边界飞溅，30s 后所有 body 都还在地图内（可肉眼 / 物理 debug overlay 验证 — feel-polish 已有 `b841e31a Polish Brotato3D physics feedback` 的 overlay 工具可用）
- 玩家 kinematic body 推墙 / 敌人推墙也不会被弹出地图

**注意**：
- 墙 + 玩家 kinematic body 之间会产生**碰撞**（kinematic 推 static），这正是我们想要的 — 玩家被堵在地图内
- 但**敌人 AI 寻路是几何距离驱动**，不依赖物理碰撞 — 现有的 `ClampToArena(enemy.worldPos, enemy.radius)`（A1 中已改成数据驱动）继续生效，墙只是兜底

### A4 · 跟随相机 + 边缘 clamp

**目标**：相机锁定主角 XZ + 平滑 lerp + 边缘 clamp（地图够大时镜头不出地表边界），死亡 / 暂停 / 结算时定格，MainMenu 走全景。

**前置依赖**：A1 已合入。A2/A3 不影响相机但都做完后视觉验证更直观。

**改动**：
1. [Brotato3DGameInstance.hpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp): 加成员
   ```cpp
   mutable glm::vec3 cameraSmoothedTarget_ = glm::vec3(0.0f);
   bool cameraInitialized_ = false;
   ```
   `mutable` 是因为 `OverrideRenderCamera` 是 `const`；或者把 lerp 移到 `OnTick` 里，`OverrideRenderCamera` 只读 — **优先方案 2**（更干净）
2. [Brotato3DGameInstance.cpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.cpp) `OnTick`：游戏世界推进时（`Playing` / `Hitstop` 等），更新 `cameraSmoothedTarget_`：
   ```cpp
   const glm::vec3 desired(player_.worldPos.x, 0.0f, player_.worldPos.z);
   const float lerpK = 1.0f - std::exp(-8.0f * static_cast<float>(deltaSeconds)); // ~125ms 半衰
   cameraSmoothedTarget_ = glm::mix(cameraSmoothedTarget_, desired, lerpK);
   // 边缘 clamp：以相机视野半径估算，让 target 不靠太近边界
   const float marginX = std::max(0.0f, arenaHalfExtent_.x - kCameraHalfViewX);
   const float marginZ = std::max(0.0f, arenaHalfExtent_.y - kCameraHalfViewZ);
   cameraSmoothedTarget_.x = std::clamp(cameraSmoothedTarget_.x, -marginX, marginX);
   cameraSmoothedTarget_.z = std::clamp(cameraSmoothedTarget_.z, -marginZ, marginZ);
   ```
   `kCameraHalfViewX/Z` 由相机高度 + FOV 推导（高 18m、FOV 50°、aspect 16:9 → 半视野 ≈ X 12m / Z 7m），可在 cpp 文件顶部定 `constexpr`
3. [Brotato3DEffectSystem.cpp:211-226](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:211) `OverrideRenderCamera`：
   ```cpp
   glm::vec3 cameraTarget = cameraSmoothedTarget_;
   glm::vec3 cameraPosition = cameraTarget + glm::vec3(0.0f, 18.0f, 11.0f);
   // 屏幕震动 jitter 不变（保留现有逻辑）
   ```
4. **状态机分支**：
   - `MainMenu`：强制 `cameraSmoothedTarget_ = (0,0,0)` + 锁定不更新（让菜单全景图保持）
   - `CharacterSelect`：同 MainMenu
   - `Playing / Hitstop / LevelUpPicking / Shopping / Paused`：跟随
   - `Result`：定格（不更新 lerp）
   - `Result → MainMenu`：在 `GoToMainMenu()` 里把 `cameraSmoothedTarget_` 重置回 `(0,0,0)`
5. **初始化**：`StartNewRun` 时 `cameraSmoothedTarget_ = player_.worldPos * vec3(1,0,1)`（无 lerp 跳过去），避免开局相机从原点滑到角色位置

**验收**：
- 编译过 `full-windows`
- 30m × 20m 地图下，主角往边角走，相机跟随且**不会越过**地表边界
- 屏幕震动叠加正常（被打中时仍能看到 jitter）
- 死亡定格、暂停定格、Result 定格、回主菜单回到全景

**注意**：
- 不要在 lerp 里乘 `globalTimeScale_` —— hit-stop 期间相机也应该保持响应（否则 hit-stop 几十毫秒视觉会顿）
- `cameraSmoothedTarget_` 用 `vec3(0)` 初始化，第一次 `OnTick` 用「整数化跳过 lerp」避免开局拉镜
- 路径追踪 / 软件追踪管线对相机移动敏感（temporal accumulation reset），但引擎已处理 camera change，无需特殊处理

### A5 · 端到端联调 + 视觉测试

**目标**：A1–A4 合入后跑一遍完整流程，修补遗留问题，更新视觉测试基线。

**前置依赖**：A1–A4 全部合入。

**改动**：
1. 跑 5 波完整局，验证：
   - 武器击杀 / debris 物理 / material box 拾取 / XP 磁吸 / 升级卡 / 商店全部正常
   - 不同 arena 切换正常（material 切换不漏 / 不重复注册）
   - 相机不卡死、不出图、震动正常
2. 跑 `gkNextUnitTests`（保 RenderComponent / Component 单测过）
3. 跑 `gkNextVisualTest`（[assets/configs/visual_test.json](../../../assets/configs/visual_test.json)）：
   - 如果 visual_test 包含 Brotato3D 场景，更新基线截图
   - 如果不包含，**不**强制添加（避免范围扩散）
4. 调参：根据视觉效果微调 tile 配色 / fuzziness 数值，让 grassland 偏自然（少量金属补丁），tech_grid 偏机械（大面积金属 + 塑料中心）
5. 把 `arenas.json` 三个内置 arena 都按新 schema 填好（不留缺省 fallback 路径，让数据看起来"完整设计"）

**验收**：
- 端到端 5 波通关无 crash / 无渲染异常
- 单测全过
- visual test 基线（如果有 Brotato3D 项）已更新并 commit

## 风险与权衡

1. **Material::Mixture 噪点**：在 path tracing 管线下，介电+漫反射混合材质 fuzziness 过低会产生 fireflies。本计划要求 `fuzziness >= 0.02`，并在 A2 验收时肉眼检查。如果噪点过强，回退到 `Lambertian` + 加深颜色。
2. **静态墙与 kinematic 玩家的 push 互动**：feel-polish 阶段 `MoveKinematicBody` 用固定 `1/60s` step；玩家高速贴墙时可能触发 Jolt 的 `kKinematicVsStatic` 解算，理论上 Jolt 会处理穿透，但建议 A3 验收时录一段贴墙跑动视频确认。
3. **相机 clamp 在小地图上可能完全锁死**：当 `arenaHalfExtent < kCameraHalfView` 时（例如旧 12×8），`marginX/Z` 算成 0 → 相机始终在原点。这正是兼容老 MVP 行为的预期。但需要在 A4 验收时切到老 grassland（如果想保留小地图选项）确认无回归。
4. **JSON schema 兼容**：A1 的 fallback（缺字段→旧默认）必须做对，否则旧的 `arenas.json` 解析失败会让游戏卡在 character select。建议 A1 PR 先**保留旧 JSON 不动**，新增字段时用 `value("halfExtent", ...)` 范式取值。
5. **不引新依赖**：本计划全部使用引擎已有能力（Material::Metallic/Mixture、CreateBoxBody Static、glm::mix），不动 vcpkg.json。

## 任务执行顺序与并行度

```
A1 (数据/配置 — 必做先) ───┬─→ A2 (地表材质渲染) ─┐
                          ├─→ A3 (物理空气墙)    ├─→ A5 (联调 + 视觉测试)
                          └─→ A4 (跟随相机)      ┘
```

A2 / A3 / A4 在 A1 完成后可由不同 agent **并行**领取（互不冲突的文件改动）；A5 串行兜底。

## 验收清单（每个 PR 自查）

- [ ] 编译 `full-windows` 通过（CLAUDE.md / AGENTS.md 强制）
- [ ] `Brotato3D` 启动后日志含 `uploaded scene [...] to gpu`
- [ ] 不引入新的 third-party / vcpkg 依赖
- [ ] 不修改 `ThirdParty/` / `external/`
- [ ] 不留 `// TODO` 跨任务（每个任务必须自包含完成）
- [ ] 无 magic number 直接散落在新代码里 —— 命名常量或来自 JSON
- [ ] 单测 `gkNextUnitTests` 全过
- [ ] 不破坏现有 feel-polish 阶段的 debris / 拾取 / 击杀手感

## 不在范围内

- 不改 wave / 升级 / 商店 / 物品 / 角色逻辑
- 不引入地形高度 / 多层楼 / 楼梯 / 斜坡（地表始终 y=0 平面）
- 不做地形破坏 / 弹孔贴花 / decal
- 不引入新材质模型（Material::Enum 不扩展，沿用 Lambertian/Metallic/Mixture/Dielectric）
- 不做地图编辑器 / 关卡编辑器 UI
- 不改主菜单 / 角色选择 UI（A4 状态机分支只读不写）
