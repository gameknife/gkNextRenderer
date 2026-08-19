# GeoWalk — 真实地点漫游器

加载 `gnb geo` 生成的真实城市 tile，把 OpenStreetMap 标注的地点显示成世界空间标签，并在**可达
的街面**上放一个 ScadRig 角色（AI 自动漫游 / 玩家 WASD 操控，按键切换）。

管线与地形契约见 [geo-city-generation-design](../designs/geo-city-generation-design.md)，
角色资产格式见 [ScadRig](ScadRig.md)，复用的仿真层见 [SimKit](SimKit.md)。

```bash
./gnb.sh build GeoWalk
./gnb.sh run GeoWalk                                                   # 默认加载第一个 tile
./gnb.sh shot --target GeoWalk --ui --scene assets/scad/proc/generated/paris_cite.scad
./gnb.sh validate --script assets/agentscripts/geowalk-smoke.agentscript.json
./out/build/<preset>/bin/gkNextUnitTests "[POI]"                       # poi.json 数据契约，无需 GPU
```

## 操作

| 键 | 作用 |
|---|---|
| `F` | 在 **Roam（AI 自动漫游）** 与 **Player（WASD）** 之间切换 |
| `W/A/S/D` | 玩家模式移动角色；自由相机模式移动相机 |
| `Q/E` | 自由相机降/升 |
| `Shift` | 跑步 / 相机加速 |
| `C` | 跟随相机 ↔ 自由相机 |
| `V` | 吸附到整块 tile 的俯瞰视角 |
| `L` | 开关地点标签 |
| `Tab` | 开关 HUD |
| 右键拖动 | 转视角；滚轮调跟随距离 |

HUD 里可切 tile、按分类过滤标签、搜索地点列表；双击列表项或按 "Walk here" 让角色走过去
（切回 Roam 模式并寻路），"Look at" 只转相机。

## 数据来源：`poi.json` sidecar

地点名字**不在 `.scad` 里**（那里只有注释），也不能从 `external/geocache/` 读（那是 gitignore
的 ODbL 衍生数据库）。`gnb geo build` 额外产出一个入库的 sidecar：

```
assets/scad/geo/<tile>/poi.json     # 与 terrain.hmap 同级，同属"产出作品"，带署名
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

两者站在**同一套导航面高度**上（`FGeoWalker::GroundHeight`：窗口内取 NavGrid 采样值，窗口外
回退到地形高度场）。NavGrid 的采样跟着路面/码头/桥面走，地形高度场不会——所以角色走在路面上
而不是陷进路基里。

## 相机

第三人称吊臂在 CBD 里默认是穿墙的。`ResolveFollowDistance` 从角色沿吊臂方向对 CPU BVH 打一条
射线，命中就把吊臂缩到命中点前 0.35m；**射线起点要推出 0.9m**（`kCameraCollisionStart`），
否则第一帧就打在角色自己的 rig 上，吊臂每帧塌到最小值。遇障瞬间收、离开缓慢放。

出生时只取最显著地点的**朝向**、俯仰用固定的 `kSpawnPitch`。直接 look-at 塔楼屋顶会把吊臂
甩进地面。想看塔楼全景用 `V`。

## 已知限制

- **tile 发现走的是 loose 目录扫描**（`assets/scad/geo/*/`），pak 内没有目录列表。tile 目前是
  未打包的入库资产；真进了 pak 需要改成读一份清单。读 `poi.json` 本身已经走
  `ScadReadAsset`（package + loose 回退），所以只有"发现"这一步有这个限制。
- **滑动窗口重建是同步的**，372×372 格约 1 秒，跨窗口时会卡一帧。要平滑需要异步重建 NavGrid。
- **标签没有遮挡剔除**，楼后面的地点标签照样画在楼上。做正确需要每标签一次射线或深度查询。
- **玩家模式没有跳跃动画**，rig 的 clip 只有 idle/walk/sit/work；空格会驱动物理跳但姿态还是走路。
- iOS 不可用（`GK_WITH_EARCUT` 关闭，凹多边形建筑出不来），同 geo 管线本身的限制。
