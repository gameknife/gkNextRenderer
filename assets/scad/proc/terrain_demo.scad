// terrain_demo.scad —— gk_terrain() 原生低模地形 demo(M0 验收场景)
//
// TERR 编码(engine 扩展,非 OpenSCAD 标准;版本标签 "gkterr1"):
//   ["gkterr1", [sizeX,sizeY], [cellsX,cellsY], seed,
//    [baseHeight, relief, roughness], waterLevel|undef, "palette", [features]]
// features 按序作用于高度场:
//   ["mountain", [x,y], radius, height, rugged]
//   ["ridge",    [[x,y],...], width, height]
//   ["plateau",  [x,y], radius, height]
//   ["lake",     [x,y], radius, depth]
//   ["river",    [[x,y],...], width, depth]     // 上游→下游
//   ["road",     [[x,y],...], width]
//   ["pad",      [x,y], [w,d], rotDeg]          // 建筑基座(局部压平)
// 配套查询函数:
//   gk_terrain_height(TERR, x, y)  → 地表高度(与渲染网格逐三角形一致)
//   gk_terrain_info(TERR, x, y)    → [height, slopeDeg, water, biomeId]

TERR = ["gkterr1", [240, 200], [120, 100], 11, [0, 1.4, 0.55], undef, "temperate",
    [
        // 北面山脉:两座主峰 + 连接山脊
        ["mountain", [-70, 62], 46, 24, 0.6],
        ["mountain", [10, 70], 40, 19, 0.5],
        ["ridge", [[-95, 45], [-40, 74], [18, 60]], 36, 13],
        // 东南台地 + 西侧湖
        ["plateau", [78, -48], 30, 7],
        ["lake", [-78, -20], 18, 2.2],
        // 河:发源北山山谷,穿过中部流向南缘
        ["river", [[-52, 52], [-28, 20], [-6, -6], [2, -52], [-6, -96]], 7, 1.8],
        // 东西向道路,止于村庄基座西缘(road 在 pad 之前/之后都应止于基座边缘,
        // 避免路面算子把基座重新抬回路面高度)
        ["road", [[-108, -46], [-40, -38], [16, -30], [38, -26]], 5],
        // 村庄基座(pad):道路东端
        ["pad", [58, -24], [34, 24], 8]
    ]];

gk_terrain(TERR);

// 贴地摆放冒烟:村庄基座上两座"房子"、桥位一个标记块。
// (正式贴地组合子 ter_place 属 M1 的 kit_terrain.scad,此处直接调用查询函数。)
color([0.55, 0.30, 0.20])
    translate([52, -26, gk_terrain_height(TERR, 52, -26)])
        cube([6, 5, 4]);
color([0.60, 0.55, 0.45])
    translate([64, -20, gk_terrain_height(TERR, 64, -20)])
        cube([5, 5, 3]);
color([0.35, 0.25, 0.18])
    translate([-1, -30, gk_terrain_height(TERR, 2, -30) + 0.6])
        cube([8, 3, 0.7], center = true);
