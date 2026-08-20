# NextWorldTravel — 真实地点浏览器

加载 `gnb geo` 生成的真实城市 tile，用**三种视图**看同一块地：Walk 把一个 ScadRig 角色放在
**可达的街面**上（AI 自动漫游 / 玩家 WASD），Aerial 从空中把整块 tile 变成一张标着
OpenStreetMap 地点的地图，Focus 绕着其中一个地点转。走路是其中一种视图，不是这个程序的全部。

管线与地形契约见 [geo-city-generation-design](../designs/geo-city-generation-design.md)，
角色资产格式见 [ScadRig](ScadRig.md)，复用的仿真层见 [SimKit](SimKit.md)。

## tile 从哪来（先做这一步）

tile 是**生成物，不入库**：`assets/geo/` 被 gitignore，一个干净 clone 里一个 tile 都没有，
程序会打 `no geo tiles under 'assets/geo'` 然后什么都不显示。两条获取路径：

```bash
./gnb.sh paks fetch geo          # 已发布的 tile：解到 assets/paks/geo.pak，引擎自动挂载
./gnb.sh geo make --name hk_victoria --at 22.2855,114.1580 --size 1000 --profile hongkong
```

`--profile` 同时决定建筑高度回退和**立面/屋顶的地域风格**（`default` / `europe` /
`china` / `hongkong`）：欧洲旧城要用 `europe`，否则奥斯曼式街区会全是灰平顶。
生成的楼有窗格、勒脚、女儿墙和屋面杂物，街道有人行道、路灯、行道树、斑马线和信号灯 ——
Walk 视图看到的细节都来自那一层，详见
[geo-city-generation-design §5.4b](../designs/geo-city-generation-design.md)。
排查细节层本身不要重跑管线，用探针场景：
`gnb shot --scene assets/scad/source/geo_city_probe.scad`。

自己生成的 tile 以散文件落在 `assets/geo/<tile>/`（`<tile>.scad` + `terrain.hmap` +
`poi.json` + `ATTRIBUTION.md`），要分发给别人再打包：

```bash
./gnb.sh geo pak                 # assets/geo/** -> assets/paks/geo.pak
./gnb.sh paks publish geo        # 需要 GITHUB_TOKEN
```

tile 发现同时看散文件和挂载的 pak，两者取并集，所以本地新生成的 tile 会和 pak 里的一起出现
在列表里。

```bash
./gnb.sh build NextWorldTravel
./gnb.sh run NextWorldTravel                                                   # 默认加载第一个 tile
./gnb.sh shot --target NextWorldTravel --ui --scene assets/geo/hk_victoria/hk_victoria.scad
./gnb.sh validate --script assets/agentscripts/next-world-travel-smoke.agentscript.json   # 走路
./gnb.sh validate --script assets/agentscripts/next-world-travel-browse.agentscript.json  # 浏览（鸟瞰 + 绕物）
./out/build/<preset>/bin/gkNextUnitTests "[POI]"                       # poi.json 数据契约，无需 GPU
```

开发时可用 `geo.watchGeneratedScenes` 控制 geo 目录热加载（默认开启）：

```text
set geo.watchGeneratedScenes false
set geo.watchGeneratedScenes true
```

开启后，NextWorldTravel 每 0.5 秒检查 `assets/geo/<tile>/` 的 `.scad`、`terrain.hmap` 和
`poi.json`。同一组文件稳定两个轮询周期、且 tile 完整可读后，程序会自动加载该 tile 并切到
Aerial 鸟瞰模式；半写入目录不会触发加载。

**注意**：POI 标签和 marker 走 ImGui draw list，`gnb shot` / 脚本 `screenshot` 默认不含 UI；
验证浏览功能的截图必须带 `--ui`（脚本里写 `"ui": true`），否则截出来的图上一个点都没有。

## 三种视图

| 键 | 作用 |
|---|---|
| `1` | **Walk** — 第三人称跟随角色（原来的漫游器） |
| `2` / `V` | **Aerial** — 整块 tile 的鸟瞰地图，每个地点画成 marker |
| `3` / `G` | **Focus** — 绕当前地点的环绕相机；没选过地点时开在最显著的那个 |
| `N` / `B` | Focus：下一个 / 上一个地点（按显著度排序） |
| `T` | 开关 **Tour**：每停留 N 秒自动跳到下一个地点（默认 11 s，HUD 可调） |
| `O` | 开关自动环绕 |
| `F` | Walk：**Roam（AI 自动漫游）** ↔ **Player（WASD）** |
| `W/A/S/D` | Walk 玩家模式移动角色；自由相机移动相机；Aerial 平移地图 |
| `Q/E` | 自由相机降/升；Aerial 拉近/拉远 |
| `Shift` | 跑步 / 相机加速 / Aerial 快速平移 |
| `C` | Walk：跟随相机 ↔ 自由相机 |
| `L` | 开关地点标签与 marker |
| `F9` | 截图（顶部工具栏也提供同一操作） |
| `Tab` | 开关 HUD |
| 右键拖动 | 转视角（三种视图各转自己的那套角度） |
| 滚轮 | Walk 调吊臂长度；Aerial 调高度；Focus 调取景距离 |
| 左键 | Aerial：点 marker 直接 focus 该地点 |

HUD 顶部是和 `gkNextRenderer` 同风格的 viewport 工具栏：左侧选 tile，并一键切换 Walk / Aerial /
Focus；右侧从左到右是标签、Walk 跟随/自由镜头、Tour、重置当前取景、截图、Explorer 面板和快捷键
说明。Explorer 把当前视图状态、标签分类过滤和地点列表收在可关闭的浮层里。列表项单击在 Walk 里是
"看过去"、在浏览视图里是 focus，双击或 "Walk here" 让角色走过去（**会切回 Walk 视图**，因为走过去
是走路的事）。

## 相机：一个导演，三套取景

`FGeoCameraDirector`（`GeoCameraDirector.h`）拥有全部相机状态和它们之间的过渡；应用只喂给它
角色位置、地形和一条**射线探针**（场景在应用手里，导演不直接碰引擎）。

- 每个模式**精确**算自己的 pose，不做稳态平滑——鼠标转视角一帧都不能延迟。
- 切模式 / 换 focus 目标时才启动一次 **blend**（0.9 s / 1.4 s），从切换瞬间屏幕上的 pose 飞到
  新 pose。从人行道直接切到 620 m 高空会让人完全失去方位，这个飞行是必要的。
- **验证脚本里等 blend 要用 `wait-ms` 而不是 `wait-frames`**：隐藏窗口跑到 250+ fps，150 帧只有
  0.6 秒，截出来全是过渡中间态。

### Focus：怎么给一个地点取景

1. **取景距离**从地点自己的体量算：`height × 1.45 + √area × 1.25`，钳在 34…900 m。相机看的是
   **体量中腰**（`height × 0.55`），不是地基（塔会掉到画面下三分之一）也不是屋顶（会对着天）。
2. **邻域天际线抬升**：一条清晰的视线不等于一个能看的画面。55 m 的香港站周围全是 300 m 的塔，
   从它自己的高度看过去，视线恰好从楼缝里穿过去、画面里全是墙。所以还要沿 8 个方位在
   `0.7 × 取景距离` 处各打一条**朝下**的射线量屋顶高度，取最高的那个 + 26 m 作为相机最低高度。
   这个测量**每个目标只做一次**（绕圈时屋顶不会动，每帧测会变成高度和可见性的反馈震荡）。
3. 抬升超过 55°（`kFocusMaxLiftSine`）时改为**拉远**而不是继续抬：再抬下去地点就从"一栋楼"
   变成"一张平面图"了。香港站因此从 227 m 拉到 480 m，看到的是它和 IFC、交易广场一起的全景。
4. 剩下的单点遮挡由**射线抬 pitch**处理（最多 5 档，每档 0.13 rad），**不缩短吊臂**——缩短会
   破坏取景。射线从相机**朝目标**打，用地点自己的 `halfExtent` 区分"打到了要看的东西"和
   "打到了挡在前面的东西"；反过来从中心往外打没用，因为建筑的环绕中心就在建筑里面。
5. pitch 收敛故意很慢（`kFocusPitchSharpness = 1.6`）：每次有塔扫过视线就弹一下，比被挡住更难看。

### Focus 里角色是暂停的

`FNextWorldTraveler::SetPaused` 只让 rig 站住（idle clip 继续播），仿真状态、路线、导航窗口全部原样保留，
恢复时接着走。原因是 NavGrid 滑动窗口重建要 ~1 秒，落在环绕镜头中间是这个程序最明显的一次卡顿。
Aerial 不暂停——鸟瞰图里有个人在走反而给了尺度感。

## 标签与 marker

一套数据两种呈现（`FGeoPoiLayer::ELabelStyle`）：

- **Street**（Walk / Focus）：沿用原来的规则——rank ≥ 6 的 700 m 内可见，其余只在 130 m 内出现，
  最多 28 个候选，带一根指向锚点的引线。
- **Aerial**：整块 tile 是一张地图，**每个 anchored 地点都画一个点**（大小按 rank，颜色按分类），
  但只有最显著的 44 个给名字——牌子铺满会把它们描述的城市盖掉。距离不再淡出，否则 tile 的另一半
  会没有地图。

**去重叠**：牌子按显著度顺序放置，撞到已放的牌子就**整行往上抬**，最多抬 5 行，5 行都占满才丢弃。
从人行道上看，几乎所有屋顶都投影到同一条天际线带上，不抬行的话三十个标签只活得下来一个。当前
focus 的地点**永远不被剔除**，也不参与分类过滤。

因此有两个不同的 agent query：`geo.labelsVisible` 是"这一帧有多少地点值得给名字"（预算内的候选数），
`geo.labelsDrawn` 是"其中多少真的画上了屏幕"。相机对着一面墙时后者合法地接近 0，别拿它写死断言。

Aerial 里还会画角色自己的 marker（"walker"），点击 marker 的命中测试用的是**上一帧**缓存的投影
位置——那正是用户看着点下去的位置。ImGui 抓住鼠标时不参与拾取。

## 数据来源：`poi.json` sidecar

地点名字**不在 `.scad` 里**（那里只有注释），也不能从 `external/geocache/` 读（那是 gitignore
的 ODbL 衍生数据库）。`gnb geo build` 额外产出一个入库的 sidecar：

```
assets/geo/<tile>/poi.json          # 与 terrain.hmap、<tile>.scad 同级，同属"产出作品"，带署名
```

```json
{ "format": "gkgeopoi1", "tile": "paris_cite", "center": [48.8556, 2.3475], "sizeM": 1000,
  "attribution": ["© OpenStreetMap contributors, ODbL 1.0", "..."],
  "pois": [ { "id": 123, "name": "Sainte-Chapelle", "tag": "building=church",
              "category": "worship", "source": "building",
              "pos": [-181.7, -23.6], "height": 75.0, "areaM2": 900.0, "rank": 9.6 } ] }
```

**`pos` 是 SCAD 米制**（+x 东、+y 北），与 `.scad`/`.hmap` 同一坐标系；运行时转成引擎的
`(x, z) = (x, −y)`。**文件里没有 Y**：地面高度由运行时对加载后的 `TerrainComponent` 采样得到，
因为高度场才是唯一真源（同 §5.4 建筑贴地的理由）。

三个来源，按作为标签锚点的优劣排序：`building`（有高度，标签能浮在屋顶）> `area`（公园/广场，
锚在质心）> `node`（地铁口、雕像、观景点等只有点没有轮廓的 POI）。同名且 60m 内的 node 会被
去重掉，保留 footprint。

`rank` 是显著度评分（高度 + 面积 × 分类权重），文件按它降序排列。运行时**只画排名最高的
28 个**——站在小巷里，真正告诉你身在何处的是两个街区外那栋塔楼，而不是脚边的便利店。

分类集合（`landmark / transport / culture / education / health / worship / civic / commerce /
lodging / park / place / other`）在三处必须一致：`tools/gnb/internal/geo/poi.go` 的
`POICategories`、`GeoPoiLayer.h` 的 `PoiCategory::kAll`、`Test_GeoPoiSidecar.cpp` 的
`kCategories`。`building=yes` 这类没有类型信息的命名建筑落到 `other`——这是诚实的"不知道"，
不是漏配；香港 345 个地点里有 147 个是它。

各 tile 的地点数：香港 345、纽约 290、巴黎 252、里约 114、**成都 30**。成都稀疏和它建筑高度
0% 覆盖率是同一件事：OSM 在那一块数据就是薄的，不是管线问题。

## 可行走性：三个必须照抄的约束

`Test_GeoCityWalkable.cpp` 记录的坑在这里全部会踩到，实现对应如下：

1. **NavGrid 是跟随角色的滑动窗口，不是整块 tile。** 1km × 1km 按行走精度铺格是百万级射线柱。
   窗口半径 130m（`kNavWindowHalfSize`），角色离边界 45m 时重建并重新选目标。
   `FCharacterPool` 为此新增了 `navWorldMin/Max` + `RebuildNavGrid()`（见 [SimKit](SimKit.md)）。
2. **平屋顶是"可走"格，所以出生点要用可达性选。** 密集城区里绝大多数 walkable 格是屋顶。
   `FindStreetSpawn` 先用**导航面高度 vs 地形高度差 ≤ 2m**（`IsStreetLevel`）筛掉屋顶——路面
   抬高 0.35m，所以 2m 的带宽能留下路面而挡住任何一层楼——再对候选点跑 `BuildReachabilityMask`
   洪水填充，**取连通域最大的那个**。取"第一个够大的"会让角色在停车场里踱一辈子。
3. **`floorHeightTolerance` 是绝对带宽，不沿路径传播。** 每次重建窗口时按窗口内 9×9 采样的
   地形起伏 + 12m 余量现算（`FloorToleranceFor`）。给固定值会让上坡一米就算"另一层楼"。

实测各 tile 出生点的可达连通域：纽约 64k 格、巴黎 83k、里约 63k、成都 126k、**香港 13k**。
香港最小是真实的——中环被高架行人天桥和地块切得很碎。

## 两个控制器共用一个视觉

角色只有一个 ScadRig visual（`assets/scad/characters/citizen.scad`），底下挂两套控制：

- **Roam**：`NextGameplay::Sim::FCharacterPool` 的 NavGrid A* + path follower。
- **Player**：`NextCharacterController` 物理胶囊，每帧把它的脚点写回 `FSimCharacter::position`，
  再手动驱动 visual（pool 的 `Tick` 在这个模式下不跑，位置由物理拥有）。

两者站在**同一套导航面高度**上（`FNextWorldTraveler::GroundHeight`：窗口内取 NavGrid 采样值，窗口外
回退到地形高度场）。NavGrid 的采样跟着路面/码头/桥面走，地形高度场不会——所以角色走在路面上
而不是陷进路基里。

## Walk 相机

第三人称吊臂在 CBD 里默认是穿墙的。Walk 模式从角色沿吊臂方向对 CPU BVH 打一条射线，命中就把
吊臂缩到命中点前 0.35m；**射线起点要推出 0.9m**（`kCameraCollisionStart`），否则第一帧就打在
角色自己的 rig 上，吊臂每帧塌到最小值。遇障瞬间收、离开缓慢放。

出生时只取最显著地点的**朝向**（`SetHeading`）、俯仰用固定的 `kSpawnPitch`。直接 look-at 塔楼
屋顶会把吊臂甩进地面；想看塔楼全景按 `3`/`G` 进 Focus。从浏览视图回到 Walk 一律落回跟随相机，
恢复一个停在两条街外的自由相机看起来像 bug。

## 已知限制

- **滑动窗口重建是同步的**，372×372 格约 1 秒，跨窗口时会卡一帧。要平滑需要异步重建 NavGrid。
- **标签没有遮挡剔除**，楼后面的地点标签照样画在楼上（Aerial 的 marker 同理）。做正确需要每标签
  一次射线或深度查询。
- **Focus 的遮挡判断只有一条射线**，加上邻域天际线抬升已经够用，但一根正好卡在视线上的细柱子
  仍可能挡住画面而不被检测到。
- **Tour 的顺序就是 sidecar 的 rank 顺序**，不考虑地理上的邻近；连着两站可能在 tile 的两端。
- **玩家模式没有跳跃动画**，rig 的 clip 只有 idle/walk/sit/work；空格会驱动物理跳但姿态还是走路。
- iOS 不可用（`GK_WITH_EARCUT` 关闭，凹多边形建筑出不来），同 geo 管线本身的限制。
