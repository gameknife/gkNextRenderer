# SCAD Terrain（gk_terrain 低模可行走地形）

语言/数据描述 → 连续 low-poly 高度场地形（山/河/湖/路/建筑基座）→ 可渲染、可寻路、可物理行走。
设计：`docs/designs/scad-terrain-design.md`；执行记录：`docs/plans/scad-terrain-plan.md`。

## 三种用法（从高到低）

```bash
# 1. 一句话生成（LLM）：spec + gen scad + 出图
gnb scad generate "北面雪山，一条河流经桥入南边平原，西侧小村庄"

# 2. 手写 spec JSON → compose（推荐的创作路径）
gnb scad compose --spec assets/scad/specs/overhill_valley.json
gnb shot --scene assets/scad/gen/overhill_valley.scad

# 3. 手写 scad（直接用 builtin + 组合子）
gnb shot --scene assets/scad/terrain_demo.scad          # 纯地形 7 特征 demo
gnb shot --scene assets/scad/terrain_layout_demo.scad   # 地形 + kit 贴地混摆 demo
```

## 语言面（engine 扩展，`gk_` 前缀，OpenSCAD 本体不认识）

```scad
gk_terrain(TERR);                        // module：生成地形网格 + 半透明水面
h    = gk_terrain_height(TERR, x, y);    // 纯函数：地表高度（与渲染网格逐三角形一致）
info = gk_terrain_info(TERR, x, y);      // [height, slopeDeg, water(0/1), biomeId]
```

TERR 编码（compose 自动生成，手写参考 `assets/scad/terrain_demo.scad` 头注释）：

```scad
TERR = ["gkterr1", [sizeX, sizeY], [cellsX, cellsY], seed,
        [baseHeight, relief, roughness], waterLevel /*或 undef*/, "temperate",
        [ ["mountain", [x,y], radius, height, rugged],
          ["ridge",    [[x,y],...], width, height],
          ["plateau",  [x,y], radius, height],
          ["lake",     [x,y], radius, depth],
          ["river",    [[x,y],...], width, depth],     // 上游→下游
          ["road",     [[x,y],...], width],            // 填方>0.9 的深沟留空给桥
          ["pad",      [x,y], [w,d], rotDeg] ]];       // 建筑基座压平
```

- features **按序作用**：山→河（下切+水面）→路（压平）→pad（最后压平）。
- 全部 seed 确定性；同 TERR 逐字节同网格。cells 上限 256（评估器 clamp + warn）。
- 调色板：`temperate` / `arid` / `alpine`；biomeId 枚举见 `FScadTerrain.h` `ETerrainBiome`
  （grass=0, grass_dark, dry_grass, sand, rock, rock_high, snow, bed, road, pad）。

## 贴地组合子（`assets/scad/lib/kit_terrain.scad`，catalog 不收录）

```scad
ter_place(t, x, y, dz = 0) { ... }                        // 贴地平移（pad/路面上高度精确）
ter_place_tilt(t, x, y, dz = 0, maxTilt = 12) { ... }     // 贴地 + 随坡倾斜（岩石/倒木；建筑别用）
ter_snap(t, at = [0, 0], dz = 0) { ... }                  // 每个 child 按各自 XY 贴地（配 lay_grid）
ter_along(t, pts, step = 6, seed = 0, offset = 0) { ... } // 折线贴地撒点（$idx/$t/$seed 穿透）
ter_scatter(t, seed, n, region, filt) { ... }             // 拒绝采样散布；filt=[hMin,hMax,slopeMax,avoidWater,biomes]
ter_ok(t, x, y, filt) / ter_biome(name)                   // 过滤谓词 / biome 名→id（供自定义规则）
```
实例内随机用 `lay_randr($seed, i, lo, hi)`（kit_layout 的 PRNG 族）。

## Spec 层（`gnb scad compose`）

`terrain` 段字段见 `docs/designs/scad-terrain-design.md` §4 或样例
`assets/scad/specs/overhill_valley.json`。放置规则扩展：

- 有 `terrain` 时 placements/grids/rows/rings/alongs/scatters **默认贴地**；
  `"snap": "none"` 关闭（水面船只）；`"snap": "terrain"` 无地形时报错。
- `"snapAt": [x, y]`（placements）：取高点与摆放点分离——**桥必用**（锚在岸上路面）。
- scatters 加 `"where": {"hMin","hMax","slopeMax","avoidWater","biome":[...]}` 过滤。
- 校验：`terrain`/`ground` 互斥、折线 ≥2 点且在域内、cells 4..256、biome 枚举、
  hMin≤hMax、pad 压河告警、blockGrids 不贴地告警。

## ScadLibrary 过程编辑

`ScadLibrary --scene assets/scad/terrain_layout_demo.scad` 会自动打开 Terrain“过程”页：

- TERR 画布参数和 7 类有序 feature 均为类型化控件；折线 feature 可增删折点，feature 可调整顺序。
- 中央视口会把 feature 直接画在地形表面：山峰/台地/湖泊显示半径轮廓，山峰/台地显示高度标尺，
  ridge/river/road 显示中心折线、宽度边界和控制点，pad 显示旋转后的占地框。
- 右侧列表与视口选择双向同步；拖动中心点或折线控制点可直接改 XY，选中圆形 feature 后还可拖动
  半径手柄。山峰/台地/山脊的竖直手柄编辑高度，湖泊/河流的竖直手柄编辑深度，
  ridge/river/road 的横向手柄编辑宽度。覆盖层使用当前 TERR 的网格高度缓存，拖动时不重建，
  松手后一次性刷新预览。
- Terrain 自动刷新、手动预览和保存后的场景重建会保留当前相机位置、方向与缩放；只有真正打开
  另一个 SCAD 场景时才重新按场景边界取景。
- 过程规则也显示在地形表面：`ter_place/place_tilt/snap` 为落点（tilt 另带 probe 圈），
  `ter_along` 为折线和方形折点，`ter_scatter` 为区域轮廓；高度锚点同时连接摆放点与取样点。
  方形手柄可拖动。按 `offset/step` 展开的 along 实例点，以及经过高度/坡度/水域/biome
  过滤后的 scatter 确定性点集，仅在对应规则被选中时显示，避免过程结果遮挡场景。选中 scatter
  后还会显示 `count` 竖直手柄，上下拖动可直接调整散布数量。
- 视口采用两阶段编辑：首次点击未选中手柄只选择并定位右侧面板，再次按住已选中手柄并拖动才
  修改参数；Feature 与规则共享单一选中态，右侧只展开当前项。
- 顶层 `ter_place/place_tilt/snap/along/scatter` 的语义参数可视化编辑，child SCAD 原样保留；
  桥梁用的 `gk_terrain_height(TERR, sampleX, sampleY)` 高度锚点会拆分显示摆放点与岸上取样点。
- 从左侧 Kit 浏览器添加模块时，过程场景会生成 `ter_place(TERR, x, y)`，而不是平地对象。
- 保存采用源码增量改写：未识别的桥高表达式、自由 SCAD 和注释不会因过程编辑而丢失。
- 非字面量组合子参数不强行结构化；面板提示后交给“源码”页维护。

回归：

```bash
gkNextUnitTests "[TerrainProcess]"  # demo 解析、编辑、再解析、真实 Kit 求值 0 warning
gnb validate --script assets/agentscripts/scadlibrary-terrain-process.agentscript.json
```
`ter_scatter` 的 region 推荐 `[cx,cy,r]` 圆形区域；旧 `[x0,y0,x1,y1]` AABB 仍兼容。

## 引擎侧（可行走闭环）

- **TerrainComponent**（`src/Engine/Runtime/Components/TerrainComponent.hpp`，loader 自动挂在
  地形节点）：`SampleHeight/SampleNormal/SampleSlopeDegrees/IsWater/WaterSurface/IsWalkable/BiomeId`
  （引擎世界空间 XZ）。数据与渲染网格/物理网格来自同一份三角化结果，永不漂移。
- **物理**：地形自动获得静态 MeshBody；水面节点 rayCast 不可见 → **无碰撞体**（落水物沉底）。
  限制：单 Model 索引 ≥65535×3（约 6.5 万三角形 / 180² cells）时引擎跳过 MeshShape。
- **寻路**（`src/Gameplay/AI/NavGrid.h`）：Build 后必须补水域语义否决（缓坡河岸挡不住涉水）：

```cpp
navGrid.Build(scene.GetCPUAccelerationStructure(), settings);
navGrid.MaskUnwalkable([&](const glm::vec3& cellPos) {
    return terrain->IsWater(cellPos.x, cellPos.z) &&
           cellPos.y < terrain->WaterSurface(cellPos.x, cellPos.z) + 0.05f;
});  // RebuildDirtyRegion 后需要重新应用
```

- 集成测试范例：`src/Tests/Test_TerrainWalkable.cpp`（河挡路/桥连通/落球物理）。

## 桥的布置契约（血泪经验）

河岸下切带宽 = 2.2×半河宽。**桥长 ≥ 2.5×河宽**（引桥落在下切带外），`snapAt` 锚岸上路面。
否则引桥落在坡里，下桥台阶超过 maxStepHeight，NavGrid 上桥两端断连。

## 调试

- 单测：`gkNextUnitTests "[ScadTerrain]"`（解码/确定性/单调水线/pad/着色/查询一致性）、
  `"[Integration][Terrain]"`（NavGrid+物理，需 GPU）。
- Go 侧：`cd tools/gnb && go test ./internal/scadcompose/ ./internal/scadgen/`
  （few-shot 有"必须能对真实 catalog compose"守卫）。
- 地形不对劲先 `gnb shot`；高度查询与画面不一致 = 不可能（同一份数据），先查坐标系
  （engine (x,z) = scad (x,−y)，world Y = scad z）。
- 已知限制：地形不得参与场景级 CSG；不支持倾斜地形查询（local up 必须对齐 world up）；
  gen 场景含 `gk_*` 后不再兼容 OpenSCAD 本体。
