# 真实地理数据 → OpenSCAD 城市关卡

从公开高程数据（SRTM）与 OpenStreetMap 矢量数据，生成可渲染、可行走的 `.scad` 城市关卡。
**零 LLM**：`geo` 包只依赖 Go 标准库，换地点只改命令行参数。

```bash
gnb geo make --name <tile> --at <lat>,<lon> --size 1000 [--profile default|china|hongkong]
gnb shot --scene assets/scad/proc/generated/<tile>.scad
```

已验证 5 个 tile（沿海 3 / 内陆 2，含西经与南半球），见 §5.2c。单个 tile 端到端 10~25 秒，
重跑逐字节一致。

未完成的工作见 [../plans/geo-city-generation-plan.md](../plans/geo-city-generation-plan.md)。
语言面以 [SCADLoader](../AGENT_GUIDE/SCADLoader.md) 为准，地形以 [ScadTerrain](../AGENT_GUIDE/ScadTerrain.md) 为准。

## 1. 边界与非目标

- **是**：一条确定性的离线管线，输入经纬 bbox，输出 `.scad` + `.hmap`，产物入库后无需网络即可加载。
- **不是**：运行时流式地图、全球无缝地图、城市规划精度的 GIS 工具。
- **不是**：香港专用。所有数据源都是全球通用的；HK 政府高精度数据是后续可选 provider（P6）。

## 2. 数据源（P0 实测结论）

| 类别 | 选定 | 实测 |
|---|---|---|
| 高程 | AWS `elevation-tiles-prod/skadi/<N/S><lat>/<tile>.hgt.gz`（SRTM 1 弧秒，大端 int16） | HTTP 200，6.6MB/1°瓦片，中环 bbox **0 void**，维港 2~9m、半山 119m，剖面正确 |
| 矢量 | Overpass API `out body geom;` | HTTP 200，1km² 578KB / 535 栋建筑 |

**被否决的 DEM 源**：`elevation-tiles-prod/terrarium/{z}/{x}/{y}.png`。解码约定
`h = (R·256 + G + B/256) − 32768`，但实测该瓦片约 **1% 像素 red 通道跑飞**（正常 R=127~129，
坏点 R=96~121 → 解出 −1600 ~ −8600m），且散布而非成条，无法用简单规则区分真实海底。
skadi 原始 int16 没有这个问题，且一个 1°瓦片覆盖整个城市群，缓存命中率更高。

### 许可与入库政策（已确认）

- OSM 数据为 **ODbL**。原始 Overpass 响应与归一化 IR 属"衍生数据库"，**留在 gitignore 的
  `external/geocache/`，不入库**。
- 生成的 `.scad` / `.hmap` 属"产出作品"，入库，文件头强制携带署名：
  `© OpenStreetMap contributors (ODbL)` + `SRTM / NASA-USGS (public domain)`。
- SRTM 为公有领域，`.hmap` 无传染性风险。

## 3. 管线

```
A. fetch      bbox → SRTM .hgt.gz + Overpass JSON        → external/geocache/<tile>/
B. normalize  投影到本地 ENU 米制 + 拼环 → IR(tile.json)  → external/geocache/<tile>/
C. terrain    重采样 → DSM→DTM → 水位规划 → terrain.hmap  → assets/scad/geo/<tile>/
D. layout     高度推断 / 简化 / 街区分组                   → 内存
E. emit       TERR + hmap 引用 + 分组建筑 module → .scad  → assets/scad/proc/generated/
```

**IR 是解耦点**：C/D/E 只读 `tile.json`，可离线迭代任意次不碰网络，也允许手工注入自制数据。
每段独立可重跑，`gnb geo fetch|build|scad|make` 对应。C 段算出的 tile 基准面与水位回写进 IR
的 `terrain` 段（`.hmap` 只带采样值、不带语义），E 段据此写 TERR。

### 目录

```
external/geocache/<tile>/       # gitignored
  meta.json      bbox / 源 URL / 抓取时间 / sha256
  dem/N22E114.hgt.gz
  osm/overpass.json
  tile.json      归一化 IR（米制）
assets/scad/geo/<tile>/         # 入库
  terrain.hmap   二进制高度场
  ATTRIBUTION.md
assets/scad/proc/generated/<tile>.scad
```

### 投影

tile 中心 (lat0, lon0) 处的本地 ENU 切平面，SCAD +x = 东、+y = 北：

```
x = (lon − lon0) · 111319.49 · cos(lat0)
y = (lat − lat0) · 110574.0
```

1km 尺度误差 < 1cm。引擎侧 `(x, z) = (x, −y)`，world Y = scad z。

## 4. 引擎改动：TERR `hmap` 有序算子（唯一一处）

```scad
// 成都望江楼的实际产物：基准面 483.46m，锦江水位同高。
TERR = ["gkterr1", [1000, 1000], [176, 176], 7,
        [483.46, 0, 0], 483.46, "urban",
        [ ["hmap", "assets/scad/geo/chengdu_wangjianglou/terrain.hmap", "set", 1, 0],
          ["road", [[...]], 9] ]];
```

- **作为 features 首位的有序算子**，而不是替换 base：后续 `road`/`pad` 压平语义完全不变，
  `ter_place` / `ter_scatter` / `gk_terrain_height` 全套组合子零改动。
- 参数：`["hmap", path|[cols,rows], mode, zScale, zBias]`，`mode = "set" | "add"`。
  内联字面量形式 `["hmap", [cols,rows], [...值...], mode, zScale, zBias]` 保留为无文件依赖的逃生舱。
- 采样：双线性，域外 clamp 到边界值。
- **路径是 runtime-root 相对路径**（含 `assets/` 前缀），与 loader 归一化后的主场景路径同一语义。
  读取复用 `FScadShared` 的 `ScadReadAsset`，已走 `FPackageFileSystem` + loose 回退，打包进 pak 天然可用。
- 确定性：`SpecCacheKey` 纳入 path + 文件内容 FNV hash。
- 网格数据按 path 进程级缓存，同一场景多次引用只读一次盘。

**为什么是文件而不是内联数组**：`Scad::Value` 内部 `std::vector<Value>` 按值存储，30k 元素的
TERR 字面量会让每次 `gk_terrain_height(TERR, x, y)` 传参都可能深拷贝数 MB。建筑贴地要做数千次
高度查询——内联会变成性能灾难。路径形式让 TERR 保持 ~2KB。

### `.hmap` 二进制格式

```
magic   "GKHM"  4B
version u32 = 1
cols    u32
rows    u32
originX f32   // SCAD 空间左下角（米）
originY f32
cellX   f32   // 格距（米）
cellY   f32
scale   f32   // h = raw · scale + bias
bias    f32
data    int16 × cols × rows   // 行主序，row 0 = originY 一侧（+y 递增）
```

176×176 int16 = 62KB。小端。`scale = 0.01` 时高程分辨率 1cm、量程 ±327m，足够；
超过量程的 tile 由生成器自动放大 scale 并写进头。

## 5. 生成规则

### 5.1 地形：DSM → DTM

SRTM C 波段回波贴近屋顶/树冠，是 **DSM**。实测中环岸边一格 65m 夹在 5m/9m 邻格之间——就是一栋楼。
直接当地面用会得到"高台上再站一栋楼"。实际步骤（`BuildTerrain`，**顺序有讲究**）：

1. **去毛刺**：只替换与全部邻格都相差极大的孤立点（源数据损坏的特征），保留真实陡坎。
2. **渐进形态学滤波**（`progressiveGroundFilter`，Zhang 等人方法的简化版）：结构元按
   1/2/4/8/12 格递增开运算，每一级把高出开运算面超过坡度阈值（`1.2m + 0.32 × 窗口`，上限 22m）
   的样本压下去。**这是主力**，中环参考点 52.6m → 24.7m。
3. **footprint 掩膜 + IDW 修补**：OSM 建筑轮廓内的样本作废，从邻近地面插值。
4. **两遍 radius 2 平滑**：见下。

**滤波必须在掩膜之前**。反过来 inpaint 会把邻近的屋顶回波插值*进*footprint，滤波再去收拾
自己的输入。footprint 掩膜单独也远远不够：30m 采样把屋顶抹到轮廓之外，轮廓外的格子照样是屋顶。

高程数据源通过 `ElevationSource` 接口接入（`terrain.go`），这是 P6 高精度 DTM 的接缝；
目前只有 SRTM 一个实现，**`--dem-provider` 开关尚未实现**。

生成器还把非水样本 clamp 在**水位 + 0.4m** 以上：SRTM 近岸略微低于水面，滤波又会再压低，
不 clamp 的话码头一带会被水面淹成一条蓝带。

**残留偏差与其边界（重要）**：渐进形态学滤波把参考 tile 上一个中环参考点从 52.6m 压到 24.7m，
但**密集 CBD 的绝对高程仍然偏高，且无法在缺少独立 DTM 的情况下验证**。根因是物理性的：30m
采样的 DSM 在中环这种密度下**根本不含裸地回波**，任何后处理都恢复不出不存在的数据。
`TerrainReport.CoastalGradient`（近岸 20~300m 陆地的中位数"高程/离岸距"）把这个残留暴露出来
（参考 tile 为 10%），超过 15% 时 `gnb geo build` 会告警。这是提示不是判据——它区分不了
"真实陡峭海岸"和"残留屋顶偏差"。**要真正解决只有换真 DTM，即 P6。**

平滑强度也由可行走性倒逼：源数据 30m 间距上采样到 5.7m，比源间距更锐的结构都是插值伪影 +
残留噪声。不平滑掉，相邻 1m 采样有 28% 落差超过 0.6m，NavGrid 会被切成互不连通的碎片
（两遍 radius 2 的 box blur 后降到 6.8%）。

### 5.1b 地形着色：`urban` 调色板

现有三套调色板都是自然地貌的（雪线 = `0.75 × span`），套在城市 tile 上会给 120m 的丘陵盖雪、
把低平的市中心染成草地。新增 `urban`（`FScadTerrain.cpp` `FindPalette`）：

- 低平地 → 混凝土/沥青灰；`dryFrac = 0.55` 以上 → 绿坡；陡面 → 挡土墙灰 / 林地绿。
- **雪线关死**（`minReliefForSnow = 3000`），城市 tile 永远不出雪。
- `dryFrac` 不能取小：`span` 由 tile 里最高的那个角决定，0.25 会把整个 downtown 变成草坪。

这个"低处灰、高处绿"的反向色阶对沿海城市恰好是对的——填海的平地是建成区，山地是郊野公园。

### 5.2 水面：tile 本地水位，不是海平面

**绝对地面高程从 0m（曼哈顿）到 ~500m（成都）都有，所以任何"离地多高"的判据都必须锚在
tile 自己的基准面上，不能锚在 z=0。** 这条踩过一次：巴黎地面 43~64m，原来 `seaMaxElevation=20m`
的绝对闸门把塞纳河整条否决（31329 个采样只有 18 个判成水），调色板又按绝对高程把市中心染成草地。

`WaterPlan`（`terrain.go`）决定 tile 唯一的水面 —— TERR 只有一个全局 `waterLevel`：

- **有 `natural=coastline`** → 触海，水位 = 0（SRTM 是正高，海面真的在 0），下切 2~12m。
- **否则**取最大的水体多边形，水位 = 其内部 DEM 采样的**中位数**（DEM 在开阔水面读到的就是
  水面），下切 1~4m（内河不是航道）。
- **面积 < 6000 m² 的水体一律跳过**。喷泉、纪念水池、酒店池塘被当成"海"挖坑会毁掉地面：
  实测下曼哈顿两块 2944 m² 的 911 纪念池被挖成了 5m 深坑。
- 无水体 → TERR `waterLevel` 写 `undef`。

淹没判据 `elevation <= waterLevel + 20m` 是**相对水面**的。陆地下限是 `waterLevel + 0.4m`。
`BaseElevation`（有水取水位，无水取全 tile 高程的 5 分位）作为 TERR `baseHeight` 写进场景，
调色板的生物群系带、相机高度都从它起算。

水面桶 rayCast 不可见、无碰撞体；水上物体用 `"snap": "none"`。

### 5.2b OSM multipolygon 必须拼环

relation 的边界是**拆成若干条开放 way** 的，只有首尾相接才成环。塞纳河是 8 条开放 outer 成员；
把最长的那条当成闭合多边形会得到一个和水毫无关系的形状，实测把巴黎南岸整个淹了。
`assembleRings`（`normalize.go`）按端点吻合（0.5m 容差）拼环，拼不出闭环的链**直接丢弃**——
补全它等于凭空造几何。建筑取最大的 outer 环，水体/用地则**每个闭合 outer 环各生成一个 Area**
（一个 relation 可以合法地覆盖多块互不相连的水面）。

### 5.2c 已验证的 tile（generality 证据）

同一条命令、零代码改动，只换 `--at` 与 `--profile`：

| tile | 中心 | 地面高程 | 水 | 建筑 | 真实高度占比 | 自动识别的最高建筑 |
|---|---|---|---|---|---|---|
| hk_victoria | 22.2855,114.1580 | 0.4~81m | 海 | 952 | 40% | 國際金融中心二期 415.8m |
| nyc_lower_manhattan | 40.7075,−74.0113 | 0.4~13.7m | 海 | 641 | **72%** | One World Trade Center 417m |
| rio_botafogo | −22.95,−43.183 | 1.5~43m | 海 | 2606 | — | — |
| paris_cite | 48.8556,2.3475 | 41~64m | **内河** | 2236 | **77%** | Sainte-Chapelle 75m |
| chengdu_wangjianglou | 30.6293,104.0897 | 484~497m | **内河** | 365 | **0%** | 锦江区妇幼保健院 30m |

西经（`N40W075`）、南半球+西经（`S23W044`）的 SRTM 瓦片命名自动正确。**成都的 OSM 建筑
高度覆盖率是 0%**，整个天际线由 `china` profile 的 fallback 撑起来——这是数据可用性问题，
不是管线问题，`gnb geo build` 会把 tagged/levels/inferred 三个数字打出来。

### 5.3 建筑高度推断

按优先级取，**每栋记录 provenance**，`gnb geo build` 输出覆盖率报告：

1. `assets/scad/geo/landmarks.json` 人工覆盖表（按 OSM id；**机制已就位，文件尚未创建**）
2. OSM `height`（解析 `"415.8"` / `"96 m"`）
3. `building:levels` × profile 层高 + 屋顶
4. profile 的 `building=*` 类型默认值
5. 同街区（120m 内）已知高度的中位数，按 footprint 面积缩放后与默认值各半混合

`--profile` 提供三套 fallback（`normalize.go` `Profiles`）：`default`（欧美通用）、
`china`（板楼为主，`yes` 默认 21m）、`hongkong`（住宅塔 92m，比写字楼还高）。
**profile 只影响没有真实数据的建筑**，覆盖率越高它越无关紧要。

实测覆盖率差异极大：巴黎 77%、纽约 72%、香港 40%、**成都 0%**。香港的 IFC 二期（415.8m）、
怡和大厦（178.54m）等地标全部已 tag，所以天际线辨识度主要由那 40% 决定；成都则完全靠 profile 撑，
那一版的建筑高度**是按类型猜的、不是真实高度**。

### 5.4 建筑几何

```scad
color(c) translate([0, 0, z0]) linear_extrude(height = h + skirt)
    polygon(points = [[...]], paths = [[0,1,...]]);
```

- `z0` 必须由 `gk_terrain_height(TERR, x, y)` 在**求值期**取（四角 + 中心取 min，经生成的
  `gz()` 函数），不能由 Go 预烘焙：地形三角化网格是高度的唯一真源，预烘焙必然漂移。
- `skirt = 1.5m` 防坡地露底。
- footprint 用 Douglas-Peucker 简化到 0.5m 容差、顶点上限 24；面积 < 12m² 丢弃。
- multipolygon relation 的 inner ring → `polygon` 的 `paths` 洞。
- **整个 footprint bbox 必须在 tile 内**才发射。只判断质心会让边缘建筑悬挑到地形之外，
  画面上是一块浮在虚空里的板。

### 5.5 街区分组（必须）

2000 栋 × ~40 tri ≈ 80k 三角。若全部作为顶层裸几何按颜色合并成单个 Model，索引会逼近
**65535×3 上限 → 引擎跳过 MeshShape，建筑全部没碰撞**（ScadTerrain.md 记录的限制）。

生成器按 100m × 100m 把建筑切成 `blk_r{i}c{j}()` module（每组 20~60 栋），每个 Model 远低于
限制，同时给出 BLAS / culling 友好的空间层级。

### 5.6 道路：两层

1. **TERR `road` 算子**（trunk / primary / secondary / tertiary，上限 80 条）：压平地形 + road
   biome 染色。这一层做的是"干道会切削山坡"这件真事，代价是每条算子对每个地形格一次查询。
2. **街面几何**（`SurfaceClasses`，含 residential / service，上限 600 条）：5.7m 的地形格画不出
   街道网格，所以整个可行车网络另发一层薄板。

街面用**每段一块平板 + 每个内折点一个圆盘**，而不是整条 mitre 出来的 ribbon：

- 单一平面 ribbon 跟不了坡；逐顶点取 `gk_terrain_height` 会往文件里塞几万次调用。
- 圆盘比平板**高 2cm**，保证重叠处没有共面 —— PT 下共面重叠会 alias 出脏面。
- 整块街网是**一个 module 吃一个数据列表**，不是一条街一个 module：每次 user module 调用都会
  变成一个场景 Node，600 条街 600 个 Node 毫无意义（实测节点数 1542 → 90）。

### 5.7 码头与绿地

- `man_made=pier`：闭合 way 当甲板轮廓挤出，开放 way 沿线摆实心板。它们立在疏浚过的水里，
  按 **tile 水位**定位而不是贴地形。
- `leisure=park|garden` / `landuse=grass|forest`：Go 侧多边形内拒绝采样出确定性点位，SCAD 侧
  `gk_terrain_height` 贴地。树是**生成器自带几何**，不用 `kit_city_hd` 的 `hc_nature_tree`
  —— 那套树叶色是 0.76 绿，城市尺度下 PT 直接过曝成霓虹点。

## 6. 预算与限制

实测（5 个 tile）：**85k ~ 200k 三角、90 ~ 116 节点、0 warning、加载 280~360ms**。

已知限制，按影响排序：

- **CBD 绝对高程偏高且无法验证**（§5.1）。源数据是 DSM，密集城区不含裸地回波。唯一真解是 P6。
- **单 tile 1km**。`cells ≤ 176`（超过 180² 单 Model 三角数越过 65535，引擎跳过物理网格），
  所以 2km tile 只能 11.4m/格。做更大的地图需要先决定：接受更粗的地形，还是改引擎支持地形
  跨多个 Model —— 这是引擎改动，生成器绕不过去。tile 边缘的建筑会被丢弃（香港 483 栋）。
- **TERR 只有一个全局水位**。一个 tile 里同时有不同高程的河与湖时只取最大的那个，其余跳过。
- **30m DEM 上采样到 5.7m** = 多数是插值：宏观坡度对，微地形平滑。
- **iOS 不可用**：`GK_WITH_EARCUT` 在 iOS 关闭，凹多边形建筑出不来。
- **Overpass 限流**：首次请求常返回 HTTP 200 + HTML 错误页。当前会报错退出（不写缓存），
  重跑即可；尚无自动退避重试。
- 生成场景含 `gk_*`，不兼容 OpenSCAD 本体；地形不得参与场景级 CSG。
- 建筑是单个挤出棱柱，没有 `building:part`：IFC 二期、中银大厦这类有削切造型的塔楼是光棱柱。

## 6b. 可行走闭环（NavGrid 契约，血泪经验）

`src/Tests/Test_GeoCityWalkable.cpp` 是这条管线"产出的是关卡不是模型"的判据：街道连通、
挤出的 footprint 对 NavGrid 与物理都是实心障碍、维港不可走。踩过的四个坑，照抄：

- **`sampleCeiling` 必须高于区域内最高屋顶**。它是绝对世界 Y，默认 50；从 50 往下打射线会
  打在 150m 高楼的内部。
- **平屋顶是"可走"格**。法线朝上、净空足够，NavGrid 就标可走 —— 密集城区里绝大多数格子是
  屋顶。判断"楼挡住了"要用**可达性**（`IsCellReachable` / `BuildReachabilityMask`）而不是
  `IsCellWalkable`。
- **`floorHeightTolerance` 是绕查询参考高度的绝对带宽，不会沿路径传播**。它的用途是把楼层
  分开；放到有坡的地形上必须覆盖整个区域的起伏，否则上坡一米就算"另一层楼"。
- **NavGrid 区域要宽到装得下一条街级路线**。在中环这种密度下，紧贴两个端点画的框会被中间的
  街区整个封死，寻路正确地找不到路——这不是 bug。

## 7. 验证

```bash
gkNextUnitTests "[ScadTerrain]"                     # hmap 解码/双线性/确定性/域外 clamp
gkNextUnitTests "[Geo]"                             # 可行走闭环（需 GPU，加载参考 tile）
cd tools/gnb && go test ./internal/geo/             # fixture 驱动，不打网络
gnb shot --scene assets/scad/proc/generated/hk_victoria.scad
```

`gnb geo build` 输出的三组数字就是数据质量的看门指标：建筑高度的 tagged / levels / inferred
覆盖率、地形的 building-masked / ground-filtered 数量、以及近岸坡度。

工具层的一个坑：`console.Info` 本身是 Printf 风格，日志函数里再 `fmt.Sprintf` 预格式化，
报告里的 `%` 会二次进入格式化器（`10%!(NOVERB)`）。透传 `format, args...` 即可。
