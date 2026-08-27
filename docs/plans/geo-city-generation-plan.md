# 真实地理数据 → OpenSCAD 城市关卡：剩余计划

架构、契约与全部踩坑记录见 [../designs/geo-city-generation-design.md](../designs/geo-city-generation-design.md)。
本文只保留**尚未完成**的工作。

## 现状（一句话）

`gnb geo make --name X --at lat,lon --size 1000` 一条命令、10~25 秒产出可渲染可行走的 1km 城市
tile，沿海与内陆均已验证（香港 / 纽约 / 里约 / 巴黎 / 成都），重跑逐字节一致。P0~P5 与 R3 已完成，
耐久知识已全部并入 design。每个 tile 另出 `poi.json` 命名地点 sidecar，消费者是
[NextWorldTravel](../AGENT_GUIDE/NextWorldTravel.md)（漫游器：地点标签 + ScadRig 角色在可行走区域上行走）。

## 冷启动指引

```bash
# 代码
tools/gnb/internal/geo/          # 管线全部逻辑，只依赖 Go 标准库
tools/gnb/cmd/gnb/geo.go         # CLI
src/Modules/ScadLoader/FScadTerrain.{h,cpp}   # TERR hmap 算子 + urban 调色板
src/Tests/Test_GeoCityWalkable.cpp            # 可行走闭环
src/Tests/Test_ScadTerrain.cpp                # [Hmap] 用例

# 回归
cd tools/gnb && go test ./internal/geo/       # fixture 驱动，不打网络
./out/build/<preset>/bin/gkNextUnitTests "[ScadTerrain]"
./out/build/<preset>/bin/gkNextUnitTests "[Geo]"        # 需 GPU，加载 hk_victoria tile
./gnb.sh geo make --name hk_victoria --at 22.2855,114.1580 --size 1000 --profile hongkong
./gnb.sh shot --scene assets/geo/hk_victoria/hk_victoria.scad
```

`[Geo]` 测试硬编码了 hk_victoria tile 的坐标（恒生总行 footprint、干諾道中、维港），
**改动生成器后必须重新生成该 tile 再跑这个测试**。

## 待办

### R1. 需要用户先决策的事

1. ~~**入库范围**~~ —— **已决（2026-08-20）：tile 不入库，走 pak**。四件产物合并到
   `assets/geo/<tile>/`（`<tile>.scad` + `terrain.hmap` + `poi.json` + `ATTRIBUTION.md`），
   目录 gitignore，由 `gnb geo pak` 打成 `assets/paks/geo.pak` 分发；`assets/geo/landmarks.json`
   是输入不是产出，仍然入库。引擎随 `runtime.pak`/`optional.pak` 一起自动挂载 geo.pak，
   pak entry 名与散文件路径逐字相同。tile 发现取"散文件 ∪ pak 目录枚举"的并集
   （`FPackageFileSystem::ListMountedEntries`），因此不需要额外的 tile 清单文件。
2. **地图尺寸**。1km 对关卡偏小，但 2km 会撞上 `cells ≤ 176` 的物理网格上限（见 design §6）。
   选项：(a) 接受 11.4m/格的粗地形；(b) 改引擎让地形跨多个 Model / 支持多 tile 拼接。
   (b) 是引擎改动，生成器绕不过去。

### R2. P6 高精度 DTM provider —— CBD 高程问题的唯一真解

`ElevationSource` 接口（`terrain.go`）已经是接缝，`BuildTerrain` 只依赖它。要做的是：

- 加 `--dem-provider` 开关与 provider 注册表（目前只有 SRTM 一个实现，开关未实现）。
- 接入香港政府 CSDI 2m DTM。**验收判据明确**：干諾道中一带的高程应从当前的 ~25m 降到个位数，
  `TerrainReport.CoastalGradient` 应从 10% 降到个位数。
- 顺带调研其他国家的开放 DTM（法国 IGN RGE ALTI、美国 3DEP、英国 LIDAR），能复用同一个接口。

注意 P6 只解决高程，**不解决建筑高度**（成都 0% 覆盖率那个问题）。

### R3. Overpass 健壮性 —— 已完成（离线输入除外）

已做：限流页识别（不再回显 HTML）、15s 起指数退避重试 5 次、`--overpass-endpoint` 换镜像、
按查询指纹失效缓存（改了 QL 不会静默命中旧缓存）。

剩下：离线 `.osm.json` 输入还没做。

### R4. P5-B 细节层

路面部分的落点已经确定：`assets/scad/lib/kit_road.scad`。它是数据驱动的规则库，
加修饰＝加一个 `rd_*` 模块再挂进 `rd_network`，不必碰生成器。已有中线虚线作为样板。

- **路面修饰**（在 kit_road 里做）：路缘石、斑马线、停止线、井盖、路口转角圆弧。
- `kit_city_hd` 街具（路灯 / 红绿灯 / 公交站）—— 沿路网折线撒，同样归 kit_road。
- `building:part` 支持：IFC 二期、中银大厦现在是光棱柱。要处理 part 与父 building 的覆盖关系。
- 填 `assets/geo/landmarks.json`：机制已就位、文件还没建。香港/纽约补十几栋地标是廉价收益。

已知的路面遗留：
- **桥梁/高架仍贴地形**（`layer` / `bridge` tag 没用上），中環灣仔繞道会沿地面爬坡而不是架空。
  这是目前路网最明显的失真。
- **路面被抬高 0.35m** 才能不被地形啃穿，根因是地形网格顶点的 XY 抖动（见 design §5.6）。
  想降到真实的路缘高度，得让 `gk_terrain` 支持在道路/pad 处抑制抖动，或提供一个"网格面精确
  查询"而不是解析场查询。属于引擎侧改动。
- **路口面片是进口断面的凸包**，不是真正的交叉口造型：没有转角圆弧、没有渠化岛。
- 人行道、路缘石、斑马线、停止线都还没有 —— 但落点已经确定（kit_road 的修饰扩展点）。

### R5. 数据质量诊断成体系

目前只有近岸坡度一项。可加：高度覆盖率低于阈值时告警（成都 0% 应该刺眼）、建筑密度合理性、
把 `gnb geo build` 的报告写成 JSON 便于横向比较多个 tile。

### R6. iOS

`GK_WITH_EARCUT` 在 iOS 关闭 → 凹多边形建筑出不来，整套场景在 iOS 不可用。
要么在 iOS 打开 earcut，要么生成器输出凸分解后的 footprint。优先级取决于 iOS 是否是目标平台。
