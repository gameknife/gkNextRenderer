# Low-Poly PCG 竞技场系统 — 开发计划

## Context

### 目标

为 Brotato3D 这类俯视竞技场射击游戏，在 gkNextEngine 上构建一套 **Low-Poly 风格的过程化竞技场系统**。系统需同时满足：

- **视觉**：地表具有 Low-Poly 几何感（Flat Shading、轻微高低起伏、不规则碎片化色块、有断裂感的围墙），完全规避「纯平大草地」的呆板。
- **逻辑**：所有战斗判定（子弹飞行、角色位移、碰撞、AI 寻路）必须基于一个**平整的逻辑平面 $y=0$**。视觉起伏只对顶点位置生效，绝不影响 logical collision/raycast。
- **可复现**：同一 Seed 输入产生完全相同的场景，便于 bug 复现和 visual test 基线。

### 与已有系统的关系

当前 Brotato3D Arena（[Brotato3DArena.cpp](../../../src/Application/Brotato3D/Brotato3DArena.cpp)）已经实现了：

- 4 邻洪水填充的「伪 Voronoi」分区（[BuildFloodShapeProfile](../../../src/Application/Brotato3D/Brotato3DArena.cpp:239)）
- 多边形 → 挤出 mesh 的地表 tile（[FProcModel::CreateExtrudedConvexPolygon](../../../src/Assets/Loaders/FProcModel.cpp:223)）
- 静态物理墙（[BuildArenaWallBodies](../../../src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:272)）
- Session seed（[GetArenaSessionSeed](../../../src/Application/Brotato3D/Brotato3DArena.cpp:169)）

本计划是 Arena 的**演进**，不是推倒重写：洪水填充 → 真 Voronoi、平地表 → 顶点位移、无障碍物 → Poisson 散布、整段直墙 → 几何断裂围墙。所有既有玩法（升级 / 商店 / debris / 拾取磁吸 / Wave）的 API 不变，只替换 Arena 内部的几何生成路径。

### 范围边界

**做**：地形 mesh / 障碍物坐标 / 边界围墙 / 调试 gizmos / 配置数据。

**不做**：动态地形（不会运行时再生）、地表破坏（不挖洞）、生物群落转场、GPU 加速 Voronoi（CPU 端 ≤512 cells 完全够用）、**任何形式的导航 / 避障决策层**——敌人保留现有的"直线追击"行为，撞到 prop 由物理静态 BoxBody 挡住，不引入 NavGrid / NavMesh / avoidance steering。

## 引擎可复用能力清单

| 需求 | 复用 | 文件 |
|---|---|---|
| 多边形挤出（Voronoi cell 视觉 mesh） | `FProcModel::CreateExtrudedConvexPolygon` | [src/Assets/Loaders/FProcModel.h:14](../../../src/Assets/Loaders/FProcModel.h:14) |
| 任意 `Model` 构造 | `Assets::Model` 私有构造 + `friend FProcModel` | [src/Assets/Core/Model.hpp:154](../../../src/Assets/Core/Model.hpp:154) |
| 顶点格式（含 normal） | `Assets::Vertex`，无需 tangent space 重算可设 `needGenTSpace=false` | [src/Assets/Data/Vertex.hpp](../../../src/Assets/Data/Vertex.hpp) |
| 材质 | `Assets::Material::Lambertian/Metallic/Mixture` | [src/Assets/Data/Material.hpp](../../../src/Assets/Data/Material.hpp) |
| Scene rebuild 钩子 | `NextGameInstanceBase::BeforeSceneRebuild` | [src/Runtime/Engine.hpp](../../../src/Runtime/Engine.hpp) |
| 静态物理 mesh body | `NextPhysics::CreateMeshShape` + `CreateMeshBody` | [src/Runtime/Subsystems/NextPhysics.h:74-76](../../../src/Runtime/Subsystems/NextPhysics.h:74) |
| 静态物理 box body（围墙、矮 prop） | `NextPhysics::CreateBoxBody(pos, extent, Static)` | [src/Runtime/Subsystems/NextPhysics.h:72](../../../src/Runtime/Subsystems/NextPhysics.h:72) |
| ECS 渲染 | `RenderComponent`（modelId + 16 槽材质） | [src/Runtime/Components/RenderComponent.h](../../../src/Runtime/Components/RenderComponent.h) |
| ECS 物理 | `PhysicsComponent`（绑 NextBodyID） | [src/Runtime/Components/PhysicsComponent.h](../../../src/Runtime/Components/PhysicsComponent.h) |
| 反射 / 编辑器 PropertyPanel | `REFLECT_COMPONENT` + entt::meta | [src/Runtime/Reflection/ReflectionMacros.h](../../../src/Runtime/Reflection/ReflectionMacros.h), [AGENT_GUIDE/ReflectionSystem.md](../../../AGENT_GUIDE/ReflectionSystem.md) |
| JSON 解析 | `nlohmann-json`（已在 vcpkg） | [vcpkg.json](../../../vcpkg.json) |
| 调试线段绘制 | `PhysicsDebugOverlay` 同源的 ImGui foreground draw list 路径 | [src/Runtime/Utilities/PhysicsDebugOverlay.cpp](../../../src/Runtime/Utilities/PhysicsDebugOverlay.cpp) |
| 已有的 session seed 与 cell hash | `GetArenaSessionSeed` / `HashCell` 复用 | [Brotato3DArena.cpp:169-186](../../../src/Application/Brotato3D/Brotato3DArena.cpp:169) |

**第三方依赖决策**：vcpkg 没有 Boost.Polygon。Voronoi 实现路线 → **用 header-only 单文件库 [`jc_voronoi`](https://github.com/JCash/voronoi)**（MIT，~1k 行，纯 C99 Fortune sweepline）。引入方式：单文件放到 `src/ThirdParty/jc_voronoi/jc_voronoi.h`，按既有 `ThirdParty/` 目录约定（[AGENTS.md](../../../AGENTS.md) 不允许修改 ThirdParty，但允许新增），在一个 .cpp 里 `#define JC_VORONOI_IMPLEMENTATION` 后 include，**不**走 vcpkg。本计划不留备选路径——若编译出问题在本计划范围内解决（patch ThirdParty 文件 + 提交 patch.diff，参考既有 ThirdParty 子目录习惯），不切换库。

## 设计原则

### 1. 逻辑层与视觉层完全分离

```
                  ┌─ Visual Mesh (顶点 y ∈ [-0.15, +0.15] 微起伏，Flat Shading)
   MapGraph ──────┤
                  └─ Logical Plane (恒定 y=0，子弹/AI/碰撞唯一参与方)
```

- 视觉 mesh 由 Voronoi cell triangulation 产物 + Perlin/Simplex 顶点位移构成，**只**喂给 `RenderComponent`。
- 逻辑层：玩家/敌人/子弹 y 坐标恒为 0（或固定 0.5），所有 raycast / 距离判定都在 XZ 平面。**不**为地表 mesh 创建任何 PhysicsBody。
- 障碍物（岩石、掩体）才创建 PhysicsBody，且 body 形状是**沿 y 轴的简单 box / cylinder**（不是 mesh collider），保证子弹击中是干脆的、可预测的圆柱判定。
- 围墙（procedural borders）的物理体仍是 4 面 axis-aligned box（沿用现有 `BuildArenaWallBodies` 路径），只有**视觉 mesh** 被替换为断裂围墙。

> **关键约束**：任何顶点 y 位移产生的"视觉鼓包"不允许超过 0.15m，否则子弹（高度恒定 0.5m）会出现"明明视觉上挨着山包但子弹穿过去"的强割裂感，违反 Brotato 类游戏的精确射击体感。

### 2. 确定性 Seed

- 顶层只暴露**一个** `uint32_t arenaSeed_`，所有子系统（Voronoi、Perlin、Poisson disc、border fracture、material rolls）都从此派生子 seed（用 splitmix64 或简单的 `seed ^ stageHash`）。
- 任何运行时 `std::random_device` 调用都**只能**在最顶层"决定 session seed"的位置出现一次（沿用 [GetArenaSessionSeed](../../../src/Application/Brotato3D/Brotato3DArena.cpp:169)），其余生成函数全部用 `std::mt19937` 显式传 seed。
- 失败时的可复现：日志统一打 `[Brotato3DPcg] seed=0x{:08x} arena={} cells={} props={}`，重放只需把日志里的 seed 写回 config。

### 3. Flat Shading 顶点策略

- 不共享顶点。Voronoi cell 三角化后，每个三角形产生 **3 个独立顶点**，三个顶点的 normal 全部填**面法线**。
- 这会让顶点数翻 ~3 倍（512 cell × 平均 5 三角 × 3 = 7680 顶点），相对引擎现有 ProcModel 量级完全可接受，路径追踪 BLAS 构建也无压力。
- 复用 [FProcModel::CreateExtrudedConvexPolygon](../../../src/Assets/Loaders/FProcModel.cpp:223) 已有的"每面独立顶点"模式（看 sideOffset 那段），证明这种内存布局在引擎里是 first-class。

### 4. 不打破 Brotato3D 现有契约

- `BuildArena` 函数签名不变，内部分支即可：旧 path（`groundTiles` 显式给点）保留，新增 `pcg` 模式由 `arenas.json` 的字段 `pcg: { ... }` 触发。
- `FArenaResources::groundNodes / borderNodes` 字段语义不变，PCG 路径多产出一份 `propsNodes` 即可。
- 现有玩法系统（敌人 AI / 子弹 / 拾取磁吸）**不感知** PCG 的存在，只读 `arenaHalfExtent_` 来 clamp 位置（这是已经在做的事）。

## 数据契约

### `FArenaPcgConfig`（新增字段，挂在 `FArenaDef` 内）

```cpp
namespace Brotato3D
{
    enum class EBorderFracturePattern : uint8_t
    {
        Uniform = 0,    // 均匀分块
        Cluster = 1,    // 局部簇状破碎
        Spike   = 2,    // 偶发高低尖刺
    };

    struct FPropDef
    {
        std::string id;                  // "rock_small" / "crate" / "bunker"
        glm::vec2  footprintXZ{0.8f};    // 占据 footprint（用于 Poisson disc 间距）
        float      visualHeight = 1.0f;
        float      colliderHeight = 0.6f; // <= visualHeight：让子弹打到视觉上"凸出来"的尖只是擦边
        glm::vec3  baseColor{0.55f};
        float      weight = 1.0f;        // 散布抽样权重
    };

    struct FArenaPcgConfig
    {
        bool      enabled = false;
        uint32_t  seedOverride = 0;       // 0 = 用 session seed
        int       targetCells = 256;       // Voronoi cell 数量，128–512 合理
        int       lloydRelaxIterations = 2;// 0 = 完全随机，2–3 让 cell 大小更均匀
        float     vertexJitterAmplitude = 0.12f;  // y 位移幅度上限（米），硬上限 0.15
        float     vertexJitterFrequency = 0.18f;  // Perlin 频率（1/m）
        float     borderHeight = 4.0f;
        int       borderSegments = 48;             // 围墙断块数量
        EBorderFracturePattern borderPattern = EBorderFracturePattern::Cluster;
        float     borderHeightJitter = 0.6f;       // 围墙顶面 y 抖动幅度
        float     propPoissonRadius = 1.8f;        // Poisson 最小间距（米）
        int       propPoissonMaxAttempts = 30;     // Bridson k 参数
        float     spawnSafeRadius = 4.0f;          // 出生点周围禁放 prop
        float     edgeKeepout = 1.2f;              // 距离围墙的最小留白
        std::vector<FPropDef> props;
        std::vector<glm::vec3> palette;            // 地表色卡（按 cell hash 抽样）
    };
}
```

敌人 AI 故意**不出现**在数据契约里 —— PCG 不向 AI 系统暴露任何 prop / cell 查询接口，敌人撞到 prop 完全由物理 BoxBody 兜底。

`arenas.json` 增量 schema（不动旧字段）：

```json
{
  "id": "grassland_pcg",
  "halfExtent": [30, 20],
  "pcg": {
    "enabled": true,
    "targetCells": 320,
    "lloydRelaxIterations": 2,
    "vertexJitterAmplitude": 0.12,
    "borderPattern": "cluster",
    "borderSegments": 56,
    "props": [
      { "id": "rock_small", "footprintXZ": [0.8, 0.8], "visualHeight": 0.7,
        "colliderHeight": 0.5, "baseColor": [0.45, 0.45, 0.48], "weight": 3.0 },
      { "id": "crate",      "footprintXZ": [1.2, 1.2], "visualHeight": 1.2,
        "colliderHeight": 1.2, "baseColor": [0.62, 0.45, 0.25], "weight": 2.0 }
    ],
    "palette": [[0.36,0.55,0.30],[0.32,0.50,0.28],[0.42,0.58,0.32]]
  }
}
```

### `FMapGraph`（中间层，PCG 内部使用，不外泄）

```cpp
namespace Brotato3D::Pcg
{
    struct FVoronoiSite
    {
        glm::vec2 positionXZ;       // 站点位置
        int       paletteIndex = 0; // 抽样得到的色卡索引
    };

    struct FVoronoiCell
    {
        int                 siteIndex = -1;
        std::vector<glm::vec2> polygonXZ;  // 已 clip 到 arena 矩形内、按 CCW 排序
        glm::vec2           centroidXZ{0.0f};
        std::vector<int>    neighborCellIds;
    };

    struct FCellVertexDisplacement
    {
        glm::vec2 xz;       // 顶点 XZ
        float     yVisual;  // 视觉 y，由 Perlin 决定；逻辑 y 永远是 0
    };

    struct FPropPlacement
    {
        std::string id;                    // FPropDef.id
        glm::vec2   positionXZ;
        float       rotationYRadians;
    };

    struct FBorderSegment
    {
        glm::vec2 baseStartXZ;
        glm::vec2 baseEndXZ;
        float     topY;        // 顶面 y（含 jitter）
        float     thickness;
        glm::vec3 color;
    };

    struct FMapGraph
    {
        glm::vec2                            arenaHalfExtent;
        uint32_t                             rootSeed = 0;
        std::vector<FVoronoiSite>            sites;
        std::vector<FVoronoiCell>            cells;
        std::vector<FCellVertexDisplacement> vertexBuffer;   // 全局顶点表
        std::vector<uint32_t>                indexBuffer;    // 三角形索引（按材质段切分）
        std::vector<uint32_t>                sectionMaterialOffsets; // 每段材质对应索引起点
        std::vector<FPropPlacement>          props;
        std::vector<FBorderSegment>          borderSegments;
    };
}
```

> 选择「**全局顶点表 + 按材质段索引**」而不是「每 cell 一个 Model」，是为了让最终 mesh 走单一 BLAS 单一 drawcall —— 这是 Vulkan 后端节省 instance 开销最直接的方式，[Brotato3DArena.cpp:96](../../../src/Application/Brotato3D/Brotato3DArena.cpp:96) 当前是「每 tile 一个 Model」，PCG 阶段顺手做这次合并是合理顺路收益。

---

## 阶段一：基础骨架与数据结构设计

**目标**：把"PCG 配置 → MapGraph → 老 Arena BuildArena 入口"这条数据通路搭通，**先不**做任何 Voronoi/Perlin/Poisson 算法，用「单 cell = 整张地表矩形」作为占位返回，保证编译 + 启动 + 玩法回归不出问题。

| # | 任务 | 工时 | 依赖 |
|---|---|---|---|
| P1-1 | 扩展 `FArenaDef` / JSON loader，新增 `pcg` 字段 | ~0.5h | — |
| P1-2 | 新建 `Brotato3DPcgConfig.hpp`、`Brotato3DPcgTypes.hpp`，定义 `FArenaPcgConfig` / `FMapGraph` / `FPropDef` | ~0.5h | P1-1 |
| P1-3 | 新建 `Brotato3DPcgGenerator.hpp/cpp`，骨架函数 `BuildMapGraph(config, halfExtent, outGraph)`，stub 返回单 cell | ~0.5h | P1-2 |
| P1-4 | 在 `BuildArena` 内根据 `arenaDef.pcg.enabled` 分支，PCG 路径调 `BuildMapGraph` 后用 stub graph 走旧 `CreateExtrudedConvexPolygon` 出图 | ~0.5h | P1-3 |
| P1-5 | 注册 `FArenaPcgConfig` 到反射系统（REFLECT_COMPONENT 不适用，用 `entt::meta_factory` 直接挂），让 PropertyPanel 能调 seed / amplitude / cells 实时预览 | ~0.5h | P1-2 |

**验收**：
- `gnb.bat build --reconfigure` 通过。
- `arenas.json` 里把 `grassland` 加上 `"pcg": { "enabled": true, "targetCells": 1 }` 后启动 Brotato3D，地表还是一整块矩形（因为 stub），游戏可正常玩 5 波。
- `arenas.json` 不动时，旧 arena（`pcg.enabled=false`）行为完全一致 —— 这点必须验证，因为 Brotato3D 已有 visual test 覆盖。

**风险 / 反模式**：
- ❌ 不要把 PCG 配置塞回 `Brotato3DCommon.hpp` 的全局常量。Seed / amplitude 必须随 arena 实例走。
- ❌ 不要在 `OnTick` 里调 PCG 任何东西。所有生成都在 `BeforeSceneRebuild` 一次性完成。

---

## 阶段二：几何生成管线

**目标**：把 P1 stub 替换为真 Voronoi + Lloyd 松弛 + 顶点位移 + Flat Shading mesh 构建，落到 `FMapGraph` 的 `vertexBuffer / indexBuffer` 上。

### P2 任务表

| # | 任务 | 工时 | 依赖 |
|---|---|---|---|
| P2-1 | 引入 `jc_voronoi` header，放 `src/ThirdParty/jc_voronoi/`，写引擎层薄封装 `Pcg::ComputeVoronoi(sites, bbox, outCells)` | ~1h | P1-3 |
| P2-2 | 实现 Site 采样：bbox 内均匀随机 N 个种子 → 用本地 `std::mt19937(rootSeed ^ 'SITE')` 抽样 | ~0.5h | P2-1 |
| P2-3 | Lloyd relaxation：每轮把 site 移到 cell 质心，重新跑 Voronoi，迭代 `lloydRelaxIterations` 次 | ~0.5h | P2-2 |
| P2-4 | Cell polygon clip 到 arena bbox（库本身就支持，关键是 CCW 校正 + 去退化边 < 0.01m） | ~0.5h | P2-3 |
| P2-5 | 写 Perlin/Simplex（**自实现**最简 2D Perlin，~120 LOC；或用 `stb_perlin.h` —— stb 已在 vcpkg，stb_perlin 是免费头文件） | ~0.5h | P2-4 |
| P2-6 | `ComputeVertexDisplacement(cell, perlin, amplitude)`：cell 每个顶点 (x,z) → y = `clamp(perlin.sample(x*freq,z*freq) * amplitude, -0.15, 0.15)`；**关键**：相邻 cell 共享同一 (x,z) 时必须出同一个 y（缝合一致性，否则会出现 z-fighting / 撕裂） — 用 spatial hash 去重 | ~1h | P2-5 |
| P2-7 | Fan triangulation：每个 cell 用质心作辅助点，与每条边构成扇形三角，**质心顶点的 y = cell 顶点 y 的平均值 + 一个独立 Perlin sample**（让 cell 中心有点凸起，避免完全平板） | ~0.5h | P2-6 |
| P2-8 | Flat-shading 顶点输出：每个三角形 3 个独立 `Assets::Vertex`，normal = cross(v1-v0, v2-v0)；填 `FMapGraph.vertexBuffer / indexBuffer` | ~0.5h | P2-7 |
| P2-9 | 按 cell.paletteIndex 分段：sort triangles by material → 填 `sectionMaterialOffsets`；把所有段合成**单个** `Assets::Model`（用 `friend FProcModel` 直接构造），挂 16 个材质槽到 `RenderComponent` | ~1h | P2-8 |

### 关键算法细节

**Perlin 频率与振幅校准**：
- amplitude ≤ 0.15m 是硬约束（见[设计原则 §1](#1-逻辑层与视觉层完全分离)），JSON loader 加 clamp。
- frequency 默认 0.18/m → 波长 ~5.5m，比 cell 平均尺寸（halfExtent=30×20、targetCells=320 → 平均 cell ≈ 3.7m²，宽度 ~2m）略大一点，让起伏跨越多个 cell 形成"丘陵"而不是"皱褶"。
- 用 2 octaves 叠加（amplitude × 1.0 + amplitude × 0.4 在 2× 频率），低频出大丘、高频出 Low-Poly 碎片感。

**逻辑高度对齐**：
- `FCellVertexDisplacement.yVisual` 字段名故意带 "Visual"，提醒后续 reader 这只是渲染用。
- 任何在 `OnTick` 里把 player / enemy / projectile 位置往 `mapGraph` 上 raycast 的尝试都要在 code review 阶段拒绝。
- 单元测试用例：随机抽 64 个 (x,z)，确认 `mapGraph.SampleVisualY(x,z) ∈ [-0.15, 0.15]`，且 `LogicalY(x,z) == 0`。这条测试放进 `gkNextUnitTests` 的 `[Brotato3D][Pcg]` tag 下。

**Voronoi 库已锁定 `jc_voronoi`**：
- 单文件、MIT、~1000 LOC，输入 sites + bbox，输出含邻接信息的 cell 数组，可直接喂 P2-3。
- 落点 `src/ThirdParty/jc_voronoi/jc_voronoi.h`；一个 .cpp 里 `#define JC_VORONOI_IMPLEMENTATION` 后 include。
- iOS arm64 / macOS arm64 / Windows / Linux 必须各跑一次 build；遇到编译问题在 ThirdParty 目录写 patch.diff 修，**不**切换库。

**顶点缝合（P2-6 的关键点）**：
- Voronoi 的相邻 cell 共享边 → 共享顶点；如果两次 perlin sample 用浮点 (x,z) 直接算，IEEE 754 不能保证 bit-equal。
- 方案：先把所有 cell 顶点 (x,z) 量化到 1mm 网格（`int gx = round(x*1000)`），用 `unordered_map<pair<int,int>, float>` 缓存 `yVisual`，第二次访问直接读缓存。这样既保证缝合一致性，又允许后续 hot reload 调 amplitude 时全表重算。

### P2 验收

1. 启动 Brotato3D，PCG 地表呈现明显的 Voronoi 碎片化 + Low-Poly Flat Shading 风格。
2. 用 `ortho` 顶视截图，相邻 cell 边界**无缝、无 z-fighting**。
3. 玩家在地图上跑一圈，所有移动/射击行为与平地表无区别（验证逻辑层未受污染）。
4. 单元测试 `[Brotato3D][Pcg][LogicalPlane]` 全过。
5. 同一 seed 启动 3 次，生成 mesh 的顶点数完全相同（`spdlog::info` 打印 vertex/index count 用于人工对照）。

---

## 阶段三：分布与连接系统

**目标**：障碍物 Poisson 散布（含静态物理体）+ 几何断裂围墙。敌人 AI 完全不感知。

### P3 任务表

| # | 任务 | 工时 | 依赖 |
|---|---|---|---|
| P3-1 | Poisson disc sampling（Bridson 算法）：函数 `Pcg::SamplePoissonDisc(bbox, minDistance, k, rng) -> vector<vec2>` | ~1h | P2-9 |
| P3-2 | 避障策略层：过滤 spawnSafeRadius、edgeKeepout、prop-vs-prop 冲突（用 KD-tree 或简单 grid） | ~0.5h | P3-1 |
| P3-3 | 多 prop 类型加权抽样：按 `FPropDef.weight` 在合法位置上选 prop type；旋转随机 `[0, 2π)` | ~0.5h | P3-2 |
| P3-4 | Prop 静态 mesh 批处理：所有同 prop type 的 instance 用**同一个** `Assets::Model`，每个 instance 一个 `Node` + transform；Node 挂 `RenderComponent`（共用 modelId） + `PhysicsComponent`（每个独立 BoxBody，便于击退/移除单个 prop） | ~1h | P3-3 |
| P3-5 | 围墙断裂生成：沿 arena 周长均匀切 `borderSegments` 段，每段 base 用直边，**顶面 y 加 Perlin jitter**（`borderHeightJitter`），段间偶尔做 ±10° 倾斜（Spike pattern）；每段一个挤出 prism mesh，全部合到一个 Model（同地表合并思路） | ~1h | P2-9 |
| P3-6 | 围墙的物理体保持不变：仍然走 [BuildArenaWallBodies](../../../src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:272) 的 4 个 axis-aligned BoxBody，视觉断裂不影响物理 | ~0.25h | P3-5 |

### 关键细节

**Poisson disc 与 Voronoi 的耦合**：
- 不要在 cell 内部均匀放 prop —— 那会让 prop 看起来"网格感"。
- 在整个 arena bbox 跑一次 Poisson，让 prop 跨 cell 分布；prop 落在哪个 cell 只是用来决定**视觉地基偏色**（让 prop 底部材质和地面色卡稍微对齐）。

**Spawn 安全区**：
- 玩家初始位置（默认 `(0, 0.5, 0)`）周围 `spawnSafeRadius=4m` 强制禁放 prop。
- 这条规则比 Poisson minDistance 优先级高，过滤时先 cull spawn 圆。
- 敌人 spawn 沿用现有 [Brotato3DWaveSystem](../../../src/Application/Brotato3D/Brotato3DWaveSystem.cpp) 的"bbox 边缘外侧 random 抽点"逻辑 —— 因为 spawn 点在地图**外**，几何上不可能落在 prop（prop 在 `edgeKeepout` 留白区域之内），spawn 系统**不需要**任何修改。

**Prop 碰撞体高度**：
- `FPropDef.colliderHeight ≤ visualHeight`。比如视觉上 1.0m 高的岩石，碰撞体只 0.6m 高，让子弹（恒定 y=0.5）会**穿过岩石视觉尖**但被岩石主体挡住 —— 这是 Brotato 类游戏避免"明明能瞄到却打不到"挫败感的标准做法。

**Prop 批处理的渲染效益**：
- 假设 80 个岩石 instance，传统每 instance 一个 Model 会有 80 个 BLAS / 80 个 drawcall。
- 用「同 modelId、不同 Node transform」后，BLAS 只建一次（引擎已经为相同 modelId 自动 instanceing），drawcall 数变成 prop_type 数（通常 ≤ 5）。
- 这正是 [Brotato3DDebrisSystem.cpp:240](../../../src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:240) 那段 `attachPhysicsProxyNode` 已经在用的模式，沿用即可。

### P3 验收

1. 启动后地图上有 ~60–120 个 prop，分布看起来"自然散布"而非网格化（人工目视）。
2. 玩家出生点周围 4m 内**无任何** prop。
3. 子弹射击：能打到岩石主体，但若瞄准岩石视觉尖端则**穿过去** —— 这是预期行为。
4. 敌人直线追击：朝玩家走、撞到 prop 后被 BoxBody 挡住停在原地（kinematic push proxy 行为不变），**不**绕路 —— 这是当前接受的玩法。
5. 围墙视觉上明显碎片化，顶面有高低起伏，但 debris 的 dynamic body 仍然被 4 面墙挡住（沿用 [arena-expansion-plan.md](arena-expansion-plan.md) 的物理回归测试）。

---

## 阶段四：调试与优化工具

**目标**：让生成结果可见、可复现、可被回归测试发现退化。

### P4 任务表

| # | 任务 | 工时 | 依赖 |
|---|---|---|---|
| P4-1 | ImGui Debug Panel：固定窗口 `Brotato3D PCG Debug`，显示当前 seed / cell count / vertex count / prop count / 「Regenerate with new seed」按钮 / 「Save current seed to clipboard」按钮 | ~0.5h | P3-8 |
| P4-2 | Gizmo 1 — 逻辑平面校验：用 ImGui foreground draw list 在世界 y=0 平面上画一个稀疏网格线（每 2m 一条），证明逻辑面是平的 | ~0.5h | P4-1 |
| P4-3 | Gizmo 2 — Voronoi cell 边界：toggle 显示所有 cell polygon edge（细黄线），按 cell index 显示 site 标号 | ~0.5h | P4-1 |
| P4-4 | Gizmo 3 — Prop collider 与 visual 偏差：toggle 高亮 collider AABB vs 视觉 mesh AABB 不一致区域（红黄对比），帮助校验"视觉尖端凸出 collider"的体感设计 | ~0.5h | P3-4 |
| P4-5 | 重现系统：CLI 参数 `--brotato3d-pcg-seed=<hex>`，启动时若给定则覆盖 session seed；arena hot reload 时把 seed 写进 `assets/configs/brotato3d/.pcg_replay_seed` 文件，崩溃后下次启动自动读回 | ~0.5h | P4-1 |
| P4-6 | 可视化测试：扩展 [gkNextVisualTest](../../../src/Application/gkNextVisualTest/) 的 `visual_test.json`，加 3 个固定 seed 的 PCG arena 场景，渲染 1 帧顶视截图，作为视觉回归基线 | ~0.5h | P4-5 |
| P4-7 | 性能预算检查：`Brotato3DPcgGenerator::BuildMapGraph` 的 wall-clock 限制 ≤ 50ms（scene rebuild 时一次性开销，超时会让玩家感到卡顿）；超阈值打 spdlog warn | ~0.25h | P3-6 |

### 关键调试场景

**「逻辑判定面校准」专用 toggle**：
- 三个 gizmo（逻辑平面网格 / Voronoi 边界 / Prop collider 偏差）叠加显示时，一眼能看出"视觉起伏在哪" vs "子弹判定面在哪" vs "玩家会被哪个 prop 挡"。
- 推荐快捷键：`F3` 切换 Gizmo 总开关，子开关在 ImGui Debug Panel 里点。

**Seed 回放工作流**：
- bug report 上 spdlog 输出的 `[Brotato3DPcg] seed=0x1234ABCD` 一行能完整复现一张地图。
- QA 报"地图上有个 prop 卡死敌人"时，开发者只需 `Brotato3D.exe --brotato3d-pcg-seed=0x1234ABCD --arena=grassland_pcg`。
- 这条 CLI 路径要走 [DesktopMain.cpp](../../../src/Application/DesktopMain.cpp) 的 cxxopts 解析（cxxopts 已在 vcpkg）。

---

## 引擎集成要点

### 1. Mesh 数据流（顶点缓冲优化）

**核心结论**：把整张地表合并成 **1 个 `Assets::Model`**，把同 type 的 props 各合并成 **1 个 modelId**，围墙合并成 **1 个 Model**。

- gkNextEngine 的 [Vulkan/RayTracing](../../../src/Vulkan/RayTracing/) 后端会为每个 unique `modelId` 建 1 个 BLAS；Node 通过 transform 复用 modelId 不会重复 BLAS。
- 当前 [Brotato3DArena.cpp:96](../../../src/Application/Brotato3D/Brotato3DArena.cpp:96) 是「每 tile 一个 Model」—— 这是 PCG 阶段顺手要修掉的；测出 BLAS 数量降低（用 `physics-debug-overlay` 边上加个 mesh-debug-overlay 输出 model/instance 数）。
- Vertex 构造时把 `needGenTSpace` 设 `false`（Flat shading 不需要 tangent space，[Model.hpp:155](../../../src/Assets/Core/Model.hpp:155) 的私有构造接受这个参数），可省一段 tangent 重算与磁盘缓存 IO（[SaveTangentCache](../../../src/Assets/Core/Model.hpp:157)）。
- 因为 `Assets::Model` 的构造是 private + `friend FProcModel`，PCG 必须**从 FProcModel 内部**暴露一个 `static Model CreateFromBuffers(name, vertices, indices, false)` 静态工厂（最小侵入），不要试图破封装。

### 2. 静态障碍物 → 物理同步（无导航层）

**物理（唯一的 AI–障碍物耦合通路）**：
- Prop 节点同时挂 `RenderComponent`（共享 modelId）和 `PhysicsComponent`。
- `PhysicsComponent.SetMobility(ENodeMobility::Static)`，物理体选 BoxBody（不是 mesh collider）—— 简单、快、子弹 raycast 干脆。
- 创建走 `physics->CreateBoxBody(propPos + vec3(0, colliderHeight/2, 0), vec3(footprint/2, colliderHeight/2, footprint/2), NextMotionType::Static)`，把返回的 `NextBodyID` `BindPhysicsBody` 到 PhysicsComponent。
- **不**用 mesh collider —— [AGENT_GUIDE/PrefabSceneWorkflow.md:78](../../../AGENT_GUIDE/PrefabSceneWorkflow.md:78) 那条「静态走 mesh collider」是针对 KayKit 不规则斜坡的；规整 prop 用 box 更合适。

**敌人行为约定（明确不做避障）**：
- 敌人继续走现有的"直接修改 transform 直线追玩家"路径（[Brotato3DEnemySystem.cpp](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp)），**不改 AI 代码**。
- 敌人的 kinematic push proxy body（已存在于 [Brotato3DDebrisSystem.cpp:260](../../../src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:260)）会撞上 prop 的静态 BoxBody，物理引擎自动阻挡 —— 敌人就停在 prop 边沿。Brotato 类游戏可接受"敌人卡在岩石后面"，那是玩家利用地形的乐趣。
- 因此 PCG 系统**不向外暴露任何 prop 位置查询 API**（无 `IsBlocked` / `FindNearestFreeCell`），`FMapGraph` 对玩法层完全不可见。
- Spawn 系统也无需改动：敌人从 bbox **外侧**抽点入场，prop 受 `edgeKeepout` 留白保护不会出现在边缘，几何上无 spawn-into-prop 的可能。
- 若日后想加避障/寻路，再开 `docs/projects/brotato-3d/pcg-navmesh-plan.md` 单独计划，**不**在本计划范围内偷偷塞进来。

### 3. ECS / 反射 / Hot Reload 接入

- 在 [Brotato3DGameInstance.hpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp) 加 `Brotato3D::Pcg::FMapGraph mapGraph_` 成员；`BuildArena` 完成后存进去。
- 通过 [REFLECT_COMPONENT](../../../src/Runtime/Reflection/ReflectionMacros.h) 把 `FArenaPcgConfig` 注册到 entt::meta：seed / amplitude / cell count 等数值字段都在 [PropertiesPanel](../../../src/Editor/Panels/) 里成为可编辑滑块，运行时改完触发 scene rebuild → 直接看到新地形。
- QuickJS 暴露（可选 P5）：`Global.GetArena().GetSeed()` / `Global.GetArena().Regenerate(seed)` 通过反射桥（[QuickJSReflectionBridge.h](../../../src/Runtime/Reflection/QuickJSReflectionBridge.h)）自动可用 —— 这给后续 game tweaking 脚本（[AGENT_GUIDE/QuickJSBindings.md](../../../AGENT_GUIDE/QuickJSBindings.md)）留了口子。

### 4. Scene Rebuild 时机

- 所有 PCG 工作都在 `BeforeSceneRebuild(models, materials, nodes, lights)` 钩子内完成。
- **不要**在 `OnInit` 里建 Model（那时 scene 尚未存在）。
- **不要**在 `OnTick` 里 mutate `FMapGraph` —— 任何"换一张地图"的需求都走 `engine_->RequestLoadScene("")` 触发新一轮 rebuild。
- 性能预算：BuildMapGraph 整体 ≤ 50ms（见 P4-8）。一帧 60Hz 是 16ms，但 scene rebuild 是停帧操作，加载界面盖住即可，50ms 体验上是"无感"的。

---

## 验收总体路线

1. **构建**：`./gnb build --reconfigure`（无错无新增 warning）+ `./gnb build Brotato3D` 通过。
2. **运行**：`./gnb run Brotato3D --arena=grassland_pcg`，日志出现 `uploaded scene [...] to gpu` + `[Brotato3DPcg] seed=0x... cells=320 props=87 verts=12480`。
3. **回归**：
   - 旧 arena（`pcg.enabled=false`）行为完全不变；
   - 新 arena 跑完 5 波 + 商店 + 升级，无崩溃、无敌人卡死、无子弹穿墙；
   - debris 物理推力 / 拾取磁吸 / hit-stop / 屏幕震动 / 暴击全部 OK（沿用 [feel-polish-plan.md](feel-polish-plan.md) 的回归项）。
4. **单元测试**：`gkNextUnitTests "[Brotato3D][Pcg]"` 全过。覆盖：
   - SampleVisualY 范围（视觉 y ∈ [-0.15, 0.15]，逻辑 y == 0）
   - Seed 决定性（同 seed → 同 vertex count、同 prop count）
   - Poisson 最小距离不被破坏
   - Spawn safe radius 内无 prop
5. **视觉测试**：`gkNextVisualTest` 跑 P4-7 加入的 3 个 PCG 场景，与基线 PNG 像素差 ≤ 1%（沿用 visual_test.json 的现有阈值机制）。

---

## 风险与回滚

| 风险 | 触发 | 回滚 |
|---|---|---|
| jc_voronoi iOS arm64 编译失败 | P2-1 跑不通 | 在 ThirdParty 目录写 patch.diff 修编译问题；本计划不留切换库的备选路径 |
| Perlin 缝合露缝 (z-fighting) | P2-6 验收看到撕裂 | 把 quantize 网格从 1mm 收紧到 0.5mm；如仍有问题，把"共享顶点"策略改成"沿每条 Voronoi 边显式焊接" |
| 敌人被 prop 群完全堵死 | 玩家测试反馈 wave 中后期敌人到不了玩家身边 | 调大 `propPoissonRadius` / 调小 `targetCells`；不引入避障 —— 这是设计选择，不是 bug |
| 视觉起伏太"波涛" | 玩家测试反馈不像 Low-Poly 像"地震" | 把 amplitude 缺省从 0.12 降到 0.08；增加 cell 数让单 cell 更小、起伏更碎片化 |
| Scene rebuild > 50ms | P4-7 警告 | targetCells 上限收紧到 256；Voronoi/Poisson 之间 profiled 找瓶颈，必要时把 Perlin sample 表格化（512×512 LUT） |

---

## 不在本计划范围（明确划清边界）

- 任何形式的导航 / 避障 / 寻路（NavMesh、NavGrid、A\*、Theta\*、boid avoidance）—— 敌人保持直线追击 + 物理体兜底
- 运行时地形破坏 / 挖洞
- 生物群落 / 主题切换的过渡带（biome blending）
- GPU 加速 Voronoi（Jump Flooding）
- LOD（Brotato 是固定俯视相机，地表完全可见，无 LOD 必要）
- 多人联机时 seed 同步 —— 单机游戏不考虑
- Voronoi 库的备选实现 —— 锁定 jc_voronoi，遇到平台问题就 patch 它

如后续要做这些，开新计划：`docs/projects/brotato-3d/pcg-navmesh-plan.md`、`pcg-biome-blending-plan.md` 等。

---

## 参考

- [docs/projects/brotato-3d/plan.md](plan.md) — Brotato3D MVP 主计划
- [docs/projects/brotato-3d/arena-expansion-plan.md](arena-expansion-plan.md) — 地图扩展 / 多材质 / 跟随相机
- [AGENT_GUIDE/PrefabSceneWorkflow.md](../../../AGENT_GUIDE/PrefabSceneWorkflow.md) — 静态碰撞 / mesh collider 选择规则
- [AGENT_GUIDE/ReflectionSystem.md](../../../AGENT_GUIDE/ReflectionSystem.md) — 反射注册详解
- [AGENT_GUIDE/QuickJSBindings.md](../../../AGENT_GUIDE/QuickJSBindings.md) — 反射 → JS 暴露
- Bridson, R. (2007). *Fast Poisson Disk Sampling in Arbitrary Dimensions.*
- Lloyd, S. P. (1982). *Least Squares Quantization in PCM* — Lloyd relaxation
- [`jc_voronoi`](https://github.com/JCash/voronoi) — 单文件 Fortune sweepline 实现
