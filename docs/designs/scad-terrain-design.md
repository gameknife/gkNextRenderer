---
title: "SCAD Terrain：语言描述的低模可行走地形（设计方案）"
category: design
status: ✅ M0–M4 已落地
owner: engine
created: 2026-07-17
last_updated: 2026-07-17
---

# SCAD Terrain：语言描述的低模可行走地形

> 目标：用**语言/数据描述**（"北面一列雪山，一条河从山谷流向南边平原，河上有桥，西侧平原上一个村庄"）
> 生成**连续、可行走**的 low-poly 地形——有山脉、河流、湖泊、平原、道路——并能在其上**贴地布置
> scad kit 的 module**（房屋、树木、桥、路灯…），完成完整游戏场景搭建。
>
> 前置阅读：`AGENT_GUIDE/SCADLoader.md`（loader 全貌）、`docs/designs/scad-scene-compose-design.md`
> （Kit/catalog/layout 组合子/spec+compose/LLM generate 三层管线，本方案是它的第四块拼图）。
> 开发计划见 `docs/plans/scad-terrain-plan.md`。

---

## 1. 背景与目标

### 1.1 现状缺口

- `kit_overhill.scad` 已有 `oh_terrain_hill / oh_terrain_peak / oh_terrain_mesa`，但它们是**平地上摆放的
  "山丘道具"**（叠锥体，`assets/scad/lib/kit_overhill.scad:187`）：不连续、走不上去、河（`oh_ground_river`）
  只是平铺色块。整张图仍是一块平板 `ground`。
- Scene Spec（`tools/gnb/internal/scadcompose/`）的 `ground` 字段只支持单块平地板。
- 游戏（CharacterDemo / Brotato3D / 未来的开放地形玩法）没有任何"地面高度查询"能力，角色只能在
  z=0 平面活动。

### 1.2 已有地基（本方案直接复用，不重建）

| 能力 | 位置 | 与本方案的关系 |
|------|------|----------------|
| 寻路可走性 | `src/Gameplay/AI/NavGrid.h`（`FNavGrid::Build` 用场景 CPU BVH 向下射线采样地面高度/净空/可走性） | 地形只要进场景 BVH，NavGrid **零改动**即可在山坡上寻路 |
| 角色物理 | `src/Modules/NextPhysics/JoltPhysicsBackend.cpp:748`（`CreateMeshShape(Model)` → Jolt MeshShape；`CharacterVirtual` 胶囊控制器） | 地形 mesh 走既有 MeshShape 路径即可承载角色行走 |
| builtin module 先例 | `src/Modules/ScadLoader/FScadEvaluator.Geometry.cpp:51`（cube/sphere/…按名分发）、`FScadText.cpp`（text() 原生后端） | `gk_terrain()` 按同样方式接入 |
| builtin function 分发 | `src/Modules/ScadLoader/FScadEvaluator.Expr.cpp:322`（`EvalBuiltinFunction`） | `gk_terrain_height()` 纯函数接入点 |
| 组合子库 | `assets/scad/lib/kit_layout.scad`（lay_grid/scatter/along/pick + 平方项 PRNG） | 新增 `kit_terrain.scad` 贴地组合子，风格一致 |
| spec → scad | `gnb scad compose`（校验 + 薄模板展开，确定性输出） | spec 新增 `terrain` 段，展开逻辑同样保持"薄" |
| LLM 生成 | `gnb scad generate`（catalog 菜单 + 自修复回路） | prompt/schema 扩展 terrain 词汇 |
| 材质约定 | loader：`alpha < 0.99 → Dielectric(ior=1.45)` | 河/湖水面直接用半透明色桶，免新增材质通路 |

### 1.3 目标与非目标

**目标（v1）**
1. 一份**数据化的地形描述**（spec JSON `terrain` 段）可以确定性生成连续 low-poly 高度场地形：
   山脉/山脊、台地、盆地/湖、河流（下切 + 水面）、道路（压平 + 变色）、建筑基座（局部压平）。
2. 地形与既有 kit module **贴地共存**：spec 的 placements/scatters/alongs 可声明"吸附地形"，
   散布可按高度带/坡度/水域过滤（树只长在缓坡草地，岩石只出现在陡坡）。
3. 引擎侧提供 **TerrainComponent**：`SampleHeight/SampleNormal/IsWater/IsWalkable` 查询，
   NavGrid + Jolt 上角色真实可行走，河流不可走、桥面可走。
4. `gnb scad generate` 一句话生成含地形的完整场景，0 warning 出图。

**非目标（v1 明确不做）**
- 真实水文侵蚀模拟、流向搜索（河流走 spec 给的折线，不自动找谷）。
- 地形 LOD / 流式 / 分块加载（单 mesh 全量，规模上限见 §5.7）。
- 运行时地形编辑/形变（生成后静态；重生成 = 重载场景）。
- 通用 `surface()`（OpenSCAD 的文件高度图图元）——与本方案正交，维持"未实现"状态。

---

## 2. 方案空间

### 方向 A：纯 SCAD 实现（函数式噪声 + polyhedron）

用 scad function 写 value noise，list comprehension 生成 polyhedron 顶点/面。

- ✅ 零引擎改动。
- ❌ 树遍历解释器跑不动：128×128 网格 ≈ 1.6 万顶点 × fbm 3 octave × hash ≈ **百万级解释器求值**，
  秒级起步且随规模平方增长；`lay_scatter` 级别的 PRNG 已经踩过精度坑，噪声更甚。
- ❌ 引擎侧拿不到语义（水域掩码、坡度、生物群系）——只有三角汤，`IsWater` 之类无从谈起。
- ❌ LLM 直接写这种 scad 的可靠性远低于写 spec。

### 方向 B：引擎独立地形系统（Heightmap 资产 + 专用渲染路径）

传统引擎做法：独立 terrain asset + 专用管线（clipmap/quadtree）。

- ❌ 与 <50k LOC 目标相悖，v1 规模（≤30 万三角形）完全不需要专用管线。
- ❌ 场景创作被劈成两条互不相通的路径（scad 场景 vs terrain 资产），compose/LLM 管线无法覆盖。

### 方向 C：compose（Go 侧）生成地形，输出 polyhedron 字面量

Go 里跑噪声，把顶点/面写进 gen/*.scad。

- ❌ gen 文件爆炸（128×128 ≈ 3–5 MB 文本），git 不友好，违背"compose 是薄模板展开"的既有原则。
- ❌ 引擎侧要做高度查询就必须**在 C++ 再实现一遍同样的算法并保证逐位一致**——双实现漂移是长期税。
- ❌ scad 层拿不到 `terrain_height()`，贴地摆放只能在 Go 侧算 z，布局组合子（scatter 的候选点
  在 scad 内生成）无法贴地。

### 方向 D：evaluator 原生 builtin（选定）

在 ScadLoader 模块新增 `FScadTerrain`，向 scad 语言注入两个原生符号：

- **module `gk_terrain(TERR)`**：从声明式 spec（scad 嵌套 list 常量）生成低模高度场网格 + 水面网格，
  走既有颜色分桶/scene graph 管线；
- **function `gk_terrain_height(TERR, x, y)`**：纯函数，返回该点地表高度，scad 层任何地方可调——
  贴地摆放、坡度过滤全部在 scad 内完成，**算法单一事实来源在 C++**。

- ✅ 先例成熟：`text()`（FreeType）就是"重活在原生后端、语言面只留声明"的同款结构。
- ✅ 性能：C++ 生成 256×256 高度场毫秒级；`gk_terrain_height` 每次调用是一次 C++ 采样，
  scad 层几千次摆放调用无压力。
- ✅ 引擎语义免费：evaluator 生成的高度场/水域/生物群系数据直接交给 loader 挂 TerrainComponent，
  **与渲染 mesh 逐三角形一致**（同一份数据），物理/寻路/查询三方永不漂移。
- ✅ spec → compose → gen scad 全链路保持"薄"，LLM 只需学会 terrain 段的 JSON 词汇。
- ⚠️ 代价：gen 出的场景 .scad 不再能在 OpenSCAD 本体里完整打开（`gk_` 符号未知）。接受此
  trade-off——terrain 场景本就是 engine-first 资产；`gk_` 前缀明确标注了非标扩展边界。

**结论：方向 D + spec/compose/LLM 三层各自小幅扩展。**

---

## 3. 总体架构

```
 L2  Scene Spec JSON  + "terrain" 段 + 放置规则的 snap/where 扩展   ← 人写 / gnb scad generate (LLM)
      │  gnb scad compose（校验 terrain 段 + 展开为 TERR 常量与组合子调用）
      ▼
 L1  gen/<name>.scad：
        TERR = [ ...规范化地形spec... ];
        gk_terrain(TERR);                                  ← 地形网格 + 水面
        function __th(x,y) = gk_terrain_height(TERR, x, y);
        ter_place(TERR, x, y) rotate(...) oc_bldg_house(); ← 贴地摆放（kit_terrain.scad 组合子）
        ter_scatter(TERR, seed, n, region, filt) { ... }   ← 带地形过滤的散布
      │  现有 SCAD loader + 新增 FScadTerrain builtin
      ▼
 L0  Evaluator：FScadTerrain 生成
        · 颜色分桶三角汤（地表按生物群系着色 + 半透明水面桶）→ 既有 scene graph / Model 管线
        · FTerrainData（高度场 + 水域/生物群系掩码）→ 随 scene graph 附带
      ▼
     引擎场景：
        · 地形 Node（faceted 平直着色，多 section Model）+ 水面 Node（Dielectric，rayCast 不可见）
        · TerrainComponent（SampleHeight/Normal/IsWater/IsWalkable）
        · NavGrid（BVH 射线采样，零改动）+ Jolt MeshShape（角色行走）
```

四条硬原则（延续 compose 方案）：

1. **随机全部 seed 驱动**：同 spec → 同 .scad → 同网格逐字节一致，可 baseline 截图回归。
2. **单一事实来源**：高度函数只在 C++ 实现一份；scad 层经 `gk_terrain_height` 用它，
   引擎层经 TerrainComponent 用它（同一份求值结果，见 §5.6 缓存）。
3. **compose 保持薄**：Go 侧只做校验 + 把 JSON 规范化成 scad list 字面量，不做任何数值计算。
4. **CSG 局部化**：地形网格是闭合实体但**禁止**参与场景级 difference/intersection
   （Manifold 在 10 万+ 三角形上做布尔是性能悬崖）；河道下切等一切"雕刻"都在高度场域内完成。

---

## 4. 地形描述模型（spec `terrain` 段）

设计原则：**特征列表（features）= 对高度场的有序算子**，不暴露噪声内部参数细节给 LLM，
词汇贴近自然语言（mountain/river/plateau/lake/road/pad）。

```jsonc
{
  "name": "overhill_valley",
  "seed": 11,
  "kits": ["overhill", "old_city"],
  "terrain": {
    "size": [240, 200],          // XY 尺寸（scad 单位，1≈1m），中心在原点
    "cells": [120, 100],         // 网格分辨率（默认 size/2，上限见 §5.7）
    "seed": 7,                   // 缺省继承顶层 seed
    "base": {                    // 基底：平原
      "height": 0.0,             // 平原基准标高
      "relief": 1.2,             // 起伏幅度（fbm 噪声振幅，0=纯平）
      "roughness": 0.5           // 0~1，映射 fbm 频率/octave 预设
    },
    "waterLevel": null,          // 可选全局水位（海/大湖）；null=只有 river/lake 局部水体
    "palette": "temperate",      // 命名调色板（temperate/arid/alpine）或显式色带数组
    "features": [
      { "type": "mountain", "at": [-70, 60], "radius": 45, "height": 22, "rugged": 0.6 },
      { "type": "ridge",    "pts": [[-90,40],[-40,72],[15,58]], "width": 34, "height": 16 },
      { "type": "plateau",  "at": [60, -40], "radius": 28, "height": 6 },
      { "type": "lake",     "at": [30, 28], "radius": 16, "depth": 2.0 },
      { "type": "river",    "pts": [[-52,52],[-30,18],[-6,-8],[4,-96]], "width": 6, "depth": 1.6 },
      { "type": "road",     "pts": [[-80,-30],[-20,-24],[18,-20]], "width": 4 },
      { "type": "pad",      "at": [-46,-28], "size": [30, 22], "rot": 0 }   // 村庄基座：局部压平
    ]
  },
  "placements": [
    { "module": "oh_prop_bridge", "args": "L = 9", "at": [-6, -8], "snap": "terrain" },
    { "module": "oc_bldg_house",  "args": "seed = 3", "at": [-50, -30], "rot": 15, "snap": "terrain" }
  ],
  "scatters": [
    { "module": "oh_nature_pine", "n": 120, "seed": 5, "region": [-110,-90,110,90],
      "snap": "terrain",
      "where": { "hMin": 0.5, "hMax": 14, "slopeMax": 28, "avoidWater": 2.0, "biome": ["grass"] } }
  ]
}
```

要点：

- **features 有序求值**：后写的算子作用在前面的结果上（山先隆起、河再下切、pad 最后压平），
  语义直观且与 LLM 的叙述顺序天然对齐。
- **pad（建筑基座）**是"在山地上放村庄"的关键：局部压平后 `gk_terrain_height` 在 pad 内返回常数，
  贴地摆放的多件建筑自然共面，无需额外机制。
- **`snap: "terrain"`**：placements/grids/rows/rings/scatters/alongs 通用；有 terrain 段时
  compose 默认给非 ground 类模块加 snap（可显式 `"snap": "none"` 关闭，如水面船只）。
- **`where` 过滤**（scatter 专用）：高度带 / 坡度上限 / 离水距离 / 生物群系白名单，
  由 scad 层组合子拒绝采样实现（§6.2），保证同 seed 确定性。
- **兼容性**：无 `terrain` 段的 spec 行为完全不变（`ground` 平板路径保留）；`terrain` 与 `ground`
  互斥，compose 校验报错。

---

## 5. 地形求值核心：`FScadTerrain`（C++，ScadLoader 模块内）

新文件 `src/Modules/ScadLoader/FScadTerrain.{h,cpp}`，纯 CPU、无第三方依赖（不需要排除 unity build）。

### 5.1 高度函数 h(x, y)

```
h(x,y) = base.height + relief · fbm(seed, x·f, y·f)          // 基底
       ⊕ mountain: smoothmax(h, 圆锥/穹顶轮廓 · (1 + rugged·噪声扰动))
       ⊕ ridge:    smoothmax(h, 折线距离场轮廓（Chaikin 平滑 2 轮后的折线))
       ⊕ plateau:  smoothmax(h, 平顶轮廓（陡峭裙边 smoothstep 落差))
       ⊕ lake:     smoothmin(h, 盆地下切)，记录水面 = h_原 - depth·k
       ⊕ river:    沿折线走廊 smoothmin 下切河床；水面标高沿路径下游单调不增（§5.3）
       ⊕ road:     沿折线走廊向"中心线采样高度"压平（宽度内 smoothstep 过渡），标记 road 掩码
       ⊕ pad:      矩形/圆域压平到域内平均高度（裙边 smoothstep），标记 pad 掩码
```

**road 最大填方规则（实现中补充的关键语义）**：道路可挖可填，但单点填方深度超过
0.9 时跳过（保持原地形）——路穿过已下切的河道时**不会把河填成浅滩**，深沟自动断开留给桥；
浅沟（<0.9）则被路"涉水铺过"。没有该规则时"路过河"会形成可行走的旱滩，破坏"桥是唯一通路"。

- 噪声：整数格点 hash（**含平方项**，沿用 `kit_layout` 的 PRNG 教训——线性组合会出格线伪影）
  + 双线性插值 value noise，fbm 2~4 octaves。全部 double，跨平台确定性（不用 std::rand / 不依赖库）。
- 所有特征用 smoothmax/smoothmin（多项式 smooth k 可调）混合，避免硬接缝。
- 折线特征（ridge/river/road）先做 2 轮 Chaikin 细分（确定性），点到折线距离场驱动横截面轮廓。

### 5.2 生物群系与着色（low-poly 关键）

每面颜色在**面质心**处按规则求值，量化进小调色板（≤12 色 → 颜色分桶不爆炸）：

1. 特征掩码优先：road → 土路色；pad → 基座色（可与 road 同色系）。
2. 水下（低于邻近水面）→ 河床/湖床色（深褐）。
3. 坡度 > 岩石阈值（默认 40°）→ 岩石色（按海拔分深浅两档）。
4. 海拔 > 雪线（palette 定义，temperate 默认 = 最大山高的 78%）→ 雪色。
5. 其余按海拔带 + 低频噪声斑块：沙（近水）/ 草亮 / 草暗 / 高地枯草。

调色板注意（沿用 kit_deadly 教训）：PT 强日光 + tonemap 下 albedo ≈0.5 即近白，
草地基色压在 0.15–0.35 区间。

### 5.3 河流与水面

- 河床：沿折线走廊下切 `depth`，横截面为 smoothstep 抛物线。
- 水面标高：在 Chaikin 细分后的路径点上采样**下切前**地形高度，减 `depth·0.45`，
  然后沿下游方向做**单调不增钳制** + 一轮盒滤波——保证"河往低处流"的观感，避免上坡河。
  v1 不做真实流向搜索（非目标）。
- 水面网格：河/湖各自生成条带/圆盘网格（顶点贴水面标高），单独色桶
  `[r,g,b,0.55]` → loader 既有规则自动成 Dielectric 玻璃水；湖与全局 waterLevel 同理。
- 水域掩码：网格单元中心低于局部水面 → water=true，进 FTerrainData。

### 5.4 低模网格化

- 顶点 = (cells.x+1)×(cells.y+1) 网格点，内部顶点做**确定性 XY 抖动**（≤0.35·cell，seed 派生；
  边界顶点与 pad/road 掩码内顶点不抖动，保证边缘整齐、路面/基座平整）。
- 每 cell 两三角形，对角线方向按 seed 交替翻转——与抖动共同构成经典 low-poly 不规则三角划分。
- **faceted 平直着色**：地形三角汤在 GeomList 上带 `faceted` 标志，loader 对带标志的桶**跳过法线
  平滑**（现有 `smoothAngleDegrees=35°` 会把缓坡地形抹成光滑渐变，杀死低模颗粒感）。
  这是对既有管线唯一的侵入式小改动（标志透传，见开发计划 M0）。
- 封闭实体：四周裙边下延 + 底盖（`base.height - 裙深`），观感干净、bbox/阴影稳定。

### 5.5 builtin 接口（语言面）

```scad
// 生成地形几何（module）。TERR 为 compose 产出的规范化嵌套 list。
gk_terrain(TERR);

// 地表高度纯函数：任意表达式位置可用（摆放、过滤、桥高计算…）
z = gk_terrain_height(TERR, x, y);

// 扩展查询（v1 一并实现，成本极低）：返回 [height, slopeDeg, water(0/1), biomeId]
info = gk_terrain_info(TERR, x, y);
```

- TERR 的 list 编码由 compose 生成（人也可手写），首元素为版本标签 `"gkterr1"`，
  evaluator 解码失败时 warning + 跳过（与 unknown module 降级路径一致）。
- 参数校验失败走既有 `Warn()` 通道，保持"0 warning = 干净场景"的验收口径。

### 5.6 求值缓存与三方一致性（关键设计点）

`gk_terrain_height` 若每次调用都重跑特征混合，摆放几千件会有浪费，且**解析式高度 ≠ 抖动后
三角网格的插值高度**（顶点 XY 抖动引入偏差）。解决：

- Evaluator 持有 `spec 规范化哈希 → FTerrainData` 缓存（一次生成，module 与 function 共用）。
- `FTerrainData` 保存抖动后顶点坐标 + 三角划分；`gk_terrain_height/info` 在**三角化后的网格**上
  做点定位 + 重心插值——返回值与渲染 mesh、Jolt 碰撞 mesh、TerrainComponent **逐三角形一致**。
  pad/road 区域因不抖动 + 压平，摆放高度精确。
- 同一场景理论上允许多个 `gk_terrain`（不同 TERR 哈希各自缓存），v1 不主动支持重叠混合。

### 5.7 规模与性能预算

| 项 | 预算 | 依据 |
|----|------|------|
| cells 上限 | 256×256（≈13 万地表三角形 + 水面/裙边） | 首版安全上限；实际预算必须用新增 terrain demo 在当前硬件重新测量，不沿用已删除场景的旧数据 |
| 生成耗时 | < 100ms（含特征混合 + 网格化 + 掩码） | 纯 CPU 双精度、O(cells·features) |
| `gk_terrain_height` 单次 | < 1µs（网格定位 + 重心插值） | 均匀网格直接索引 |
| 调色板 | ≤12 色 + 水面 1~2 色 | 单 Node 16 材质槽内，不触发 `__render` 分块 |
| C++ 新增 LOC | ≈ 900（FScadTerrain）+ ≈ 250（TerrainComponent） | <50k 引擎预算内 |

---

## 6. scad 层：`lib/kit_terrain.scad` 贴地组合子

与 `kit_layout.scad` 同风格（只做放置不产几何、seed 确定性、`$` 变量穿透 children），
catalog 扫描器跳过（同 kit_layout 白名单）。

### 6.1 基础组合子

```scad
// 贴地放置：children 平移到 (x, y, 地表高度 + dz)
module ter_place(t, x, y, dz = 0)
    { translate([x, y, gk_terrain_height(t, x, y) + dz]) children(); }

// 贴地 + 随坡向法线倾斜（岩石/倒木用；建筑不用）
module ter_place_tilt(t, x, y, maxTilt = 12) { ... }   // 有限差分估法线 → rotate

// 折线贴地撒点（路灯沿路、栅栏沿坡）：lay_along 的贴地版
module ter_along(t, pts, step, offset = 0) { ... }
```

### 6.2 过滤散布 `ter_scatter`（拒绝采样）

```scad
// region=[x0,y0,x1,y1]，filt=[hMin, hMax, slopeMax, avoidWater, biomes]
module ter_scatter(t, seed, n, region, filt) { ... }
```

- 候选点流由 seed 派生（`lay_rand` 同族 PRNG），逐点用 `gk_terrain_info` 查
  [h, slope, water, biome]，不满足过滤即跳过，取满 n 个或到候选上限（4n）为止。
- 确定性：同 (t, seed, n, region, filt) → 同点集。`$idx/$seed/$t` 照常穿透 children 供选型。
- 坡度直接来自 `gk_terrain_info`（C++ 里按所在三角形面法线算），不在 scad 里做有限差分。

### 6.3 compose 展开示例（gen/*.scad 形态）

```scad
// generated from specs/overhill_valley.json (sha256:xxxxxxxxxxxx)
$fn = 12;
use <../lib/kit_overhill.scad>
use <../lib/kit_old_city.scad>
use <../lib/kit_layout.scad>
use <../lib/kit_terrain.scad>

TERR = ["gkterr1", [240,200], [120,100], 7, /* base */ [0,1.2,0.5], /* waterLevel */ undef,
        /* palette */ "temperate",
        [ ["mountain", [-70,60], 45, 22, 0.6],
          ["river", [[-52,52],[-30,18],[-6,-8],[4,-96]], 6, 1.6],
          ["pad", [-46,-28], [30,22], 0] ]];

gk_terrain(TERR);

ter_place(TERR, -6, -8) oh_prop_bridge(L = 9);
ter_place(TERR, -50, -30) rotate([0, 0, 15]) oc_bldg_house(seed = 3);
ter_scatter(TERR, 5, 120, [-110,-90,110,90], [0.5, 14, 28, 2.0, ["grass"]])
    oh_nature_pine(s = ter_rndr(0.8, 1.3), seed = $seed);
```

产物仍是一等资产：可提交、可手改、`gnb shot --scene assets/scad/gen/overhill_valley.scad` 直接验收。

---

## 7. 引擎侧：TerrainComponent 与可行走闭环

### 7.1 数据流

Evaluator 生成 `FTerrainData` 后挂到 scene graph 的地形逻辑节点（与颜色桶并列的 payload）；
`FScadLoader` 组装 Node 时发现该 payload → 在地形 Node 上 `AddComponent(TerrainComponent)`，
并完成 Z-up→Y-up + scale 的坐标基变换（组件内数据存**引擎世界空间**）。

### 7.2 TerrainComponent（`src/Engine/Runtime/Components/TerrainComponent.{hpp,cpp}`）

Engine 层不依赖 Modules（数据由 loader 推入，组件本身零 SCAD 依赖）：

```cpp
class TerrainComponent : public Assets::Component
{
public:
    float SampleHeight(float worldX, float worldZ) const;   // 三角形精确（重心插值）
    glm::vec3 SampleNormal(float worldX, float worldZ) const;
    bool  IsWater(float worldX, float worldZ) const;
    bool  IsWalkable(float worldX, float worldZ, float maxSlopeDeg = 45.f) const; // 坡度+水域
    uint8_t BiomeId(float worldX, float worldZ) const;
    // 数据：抖动后顶点网格 + 高度 + 水域/生物群系/road/pad 掩码（按 cell）
};
```

`REFLECT_COMPONENT` 注册（尺寸/seed 等只读展示）→ 编辑器可视、QuickJS 自动可查
（TS 游戏也能 `SampleHeight` 贴地）。

### 7.3 寻路 / 物理 / 水面语义

- **NavGrid**（实施修正：原设想"零改动"不成立）：BVH 射线向下采样得到坡面高度；
  **水面节点标记 rayCast 不可见**（loader 对水面 Node 设既有的 RayCastVisibility），射线穿水面
  打到**干河床**——而缓坡河岸的逐格落差通常小于 maxStepHeight，纯几何判定挡不住涉水。
  因此给 `FNavGrid` 增加了通用语义钩子 **`MaskUnwalkable(predicate)`**（`src/Gameplay/AI/NavGrid.h`）：
  Build 后由游戏侧传谓词把"水下格"否决（`IsWater && cellY < WaterSurface`，桥面高于水线不受影响），
  内部重跑 erosion。集成测试 `src/Tests/Test_TerrainWalkable.cpp` 验证"河挡路、桥连通"。
  注意 `RebuildDirtyRegion` 只重采几何，区域内的 mask 需要重新应用。
- **Jolt**：零改动。场景构建只为 rayCast 可见节点建静态 MeshBody（`Scene.Build.cpp`），
  水面节点因 rayCast 不可见**自动没有碰撞体**——落水物体穿过水面停在河床（集成测试已断言）。
  限制：单 Model 索引数 ≥ 65535×3 时引擎跳过 MeshShape——地形超过 ~6.5 万三角形（约 180×180 cells）
  将没有物理碰撞体，需要分块（§9 演进）。
- **桥的布置契约**（实施经验）：河岸下切带宽 = 2.2×半河宽，桥长必须 ≥ 2.5×河宽让引桥落在
  下切带之外，且锚点（`snapAt`）取岸上路面而非河中心——否则下桥台阶超过 maxStepHeight，
  桥两端在 NavGrid 上断连。
- **TerrainComponent 的定位**：游戏逻辑的**快速语义查询**（刷怪点选择、载具贴地、AI 涉水判断、
  水花特效、NavGrid 水域 mask 的数据源），不替代物理；物理/寻路继续以 mesh 为准，三方由 §5.6
  保证一致。

---

## 8. L2 扩展：compose 校验与 LLM generate

### 8.1 compose（`tools/gnb/internal/scadcompose/`）

新增 terrain 段解析 + 校验（延续"校验是 spec 核心价值"）：

- 结构校验：size/cells 范围（cells ≤ 256）、features 类型/必填字段、折线 ≥2 点、
  river/road 点在 size 域内、`terrain` 与 `ground` 互斥。
- 语义告警：pad 与 river 走廊重叠（村庄压平会截断河道）、mountain radius 超出半图、
  scatter `where.hMax` 低于全域最低点（Go 侧不算高度，仅做"必然为空"的显式冲突检查）。
- 展开：TERR 常量（数值 canonical 格式化，保证字节稳定）+ `gk_terrain(TERR)` +
  各规则的 `ter_*` 组合子调用；`snap:"terrain"` 缺省规则见 §4。
- **Go 侧零数值计算**：不实现噪声、不采样高度（单一事实来源原则）。

### 8.2 generate（`tools/gnb/internal/scadgen/`）

- system prompt 增补 terrain 段完整合法示例（无省略号——schema 适配教训 1）+ 硬规则
  （"河流从高处流向低处描述时，pts 按上游→下游顺序给出"、"村庄先放 pad 再放建筑"）。
- few-shot 加一例山谷河流村庄场景。
- 校验错误回喂沿用自修复回路；terrain 校验错误信息按"怎么改"措辞（教训 4）。
- 验收题：**"北面一列雪山，一条河从山谷流向南边平原，河上有一座桥，西侧平原上有个小村庄，
  松树散布在缓坡上"** —— 要素齐备、0 warning、截图人审。

---

## 9. 风险与开放问题

| 风险 | 影响 | 缓解 |
|------|------|------|
| faceted 标志透传改动既有管线 | GeomList→scene graph→loader 三处小改，回归面大 | 标志默认 false，非地形路径行为逐字节不变；`[Scad]` 全量单测 + 既有场景三角形数回归 |
| 法线平滑跳过后地形与贴地件接缝处光照跳变 | 观感 | low-poly 美学本身接受硬边；必要时贴地件底部略沉 dz=-0.05 |
| 水面 Dielectric 在 SwModern 路径的表现 | 非 PT 管线水面可能偏怪 | v1 以 PathTracing 验收；SwModern 下可用 palette 提供不透明水色回退（spec `waterOpaque` 开关，后续按需） |
| 河流水面单调钳制在支流/环形路径下失效 | 上坡河观感 | v1 限单折线河；spec 校验禁止自交 |
| 颜色桶与既有 kit 调色板并集变大 | section 增多、上传变慢 | 地形 ≤12 色硬上限；compose 报告颜色桶预估（沿用 compose 方案缓解项） |
| OpenSCAD 本体兼容性丢失 | gen 场景不能在 OpenSCAD 打开 | 接受（§2 方向 D trade-off）；`gk_` 前缀显式标注扩展边界 |
| LLM 小模型对折线坐标的空间推理弱 | 河流/道路走线穿山、pts 顺序错 | few-shot 强化 + compose 域内校验回喂；必要时 generate 后 `gnb shot` 人审 |
| 大地图（>256²）需求出现 | 超出 v1 预算 | 演进：分块多 `gk_terrain` + SceneReference 引用机制（compose 方案 §7 同路线），spec 层透明 |

开放问题（不阻塞 v1，开发中决策）：

1. `gk_terrain_info` 返回 biomeId 的枚举表放哪（TERR palette 段内自描述 vs 全局约定）——倾向前者。
2. pad 是否要在 catalog/compose 里与建筑 footprint 自动关联（放 `oc_bldg_house` 自动生成 pad）——
   v1 手写 pad，观察 LLM 实际错误率再决定。
3. TS/QuickJS 侧是否需要 `Scene.GetTerrain()` 便捷入口——待 TerrainComponent 反射落地后看使用体感。

---

## 10. 里程碑概览

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| **M0 地形核心** | FScadTerrain（高度场 + 网格化 + 着色 + 水面）+ `gk_terrain/gk_terrain_height/gk_terrain_info` builtin + faceted 透传 + `[ScadTerrain]` 单测 + 手写 demo 场景出图 | ✅ 2026-07-17 |
| **M1 贴地组合子** | `lib/kit_terrain.scad`（ter_place/tilt/along/scatter/snap）+ overhill 件贴地 demo | ✅ 2026-07-17 |
| **M2 spec/compose** | terrain 段 schema + 校验 + 展开 + `specs/overhill_valley.json` 样例 | ✅ 2026-07-17 |
| **M3 可行走闭环** | TerrainComponent + loader 挂接 + 水面 rayCast 语义 + NavGrid MaskUnwalkable + Jolt 集成测试 | ✅ 2026-07-17 |
| **M4 LLM 生成** | generate prompt/schema 扩展 + 一句话验收题（Gemma-4-E4B 3 轮通过） | ✅ 2026-07-17 |
| M5（后续） | 编辑器集成 / 不透明水回退 / 分块大地图（>180² cells 物理分块）/ 侵蚀风格化 | 非本期 |

执行细节、验收记录与偏差说明见 `docs/plans/scad-terrain-plan.md`；使用速查见
`AGENT_GUIDE/ScadTerrain.md`。
