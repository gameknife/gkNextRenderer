// terrain_layout_demo.scad —— 地形贴地组合子 demo(SCAD Terrain M1 验收场景)
//
// 验收点:
//   * kit 件全部贴合地表(ter_place / ter_along / ter_scatter,无悬空/穿地)
//   * pad 上建筑共面(村庄)
//   * ter_scatter 过滤生效:松树只长在缓坡草地(河里、陡壁、雪线上无树),
//     岩石只出现在岩石生物群系
//   * 桥横跨河道,桥面高于水面
// 运行:gnb shot --scene assets/scad/proc/terrain_layout_demo.scad

use <../lib/kit_layout.scad>
use <../lib/kit_terrain.scad>
use <../lib/kit_overhill.scad>

TERR = ["gkterr1", [240, 200], [120, 100], 11, [0, 1.4, 0.55], undef, "temperate",
    [
        ["mountain", [-70, 62], 46, 24, 0.6],
        ["mountain", [10, 70], 40, 19, 0.5],
        ["ridge", [[-95, 45], [-40, 74], [18, 60]], 36, 13],
        ["plateau", [78, -48], 30, 7],
        ["lake", [-78, -20], 18, 2.2],
        ["river", [[-52, 52], [-28, 20], [-6, -6], [2, -52], [-6, -96]], 7, 1.8],
        ["road", [[-108, -46], [-40, -38], [16, -30], [38, -26]], 5],
        ["pad", [58, -24], [34, 24], 8]
    ]];

gk_terrain(TERR);

// ---- 桥:横跨河道。锚点取道路路面(不是河床);桥长要盖过整个河岸下切带
// (bank 带 = 2.2 x 半宽),引桥落脚点才在坡外,下桥台阶不超过角色步高 ----
translate([2, -30.5, gk_terrain_height(TERR, -8.5, -31)])
    rotate([0, 0, 8]) oh_prop_bridge(L = 18);

// ---- 村庄:pad 上共面建筑群 ----
ter_place(TERR, 50, -20) rotate([0, 0, 172]) oh_bldg_cabin(seed = 1);
ter_place(TERR, 64, -18) rotate([0, 0, 188]) oh_bldg_cabin(seed = 4, L = 5.5, D = 4.5);
ter_place(TERR, 68, -30) rotate([0, 0, 262]) oh_bldg_tower(seed = 2);
ter_place(TERR, 56, -29) oh_prop_campfire(seed = 3);
ter_place(TERR, 49, -28) rotate([0, 0, 40]) oh_prop_log_pile(seed = 5);
ter_place(TERR, 44, -26) rotate([0, 0, 100]) oh_prop_signpost(seed = 6);

// ---- 营地:湖东岸缓坡 ----
ter_place(TERR, -52, -14) rotate([0, 0, 250]) oh_prop_tent(seed = 7);
ter_place(TERR, -48, -19) oh_prop_campfire(seed = 8);

// ---- 沿路栅栏段(北侧) ----
ter_along(TERR, [[-100, -42], [-44, -34.5]], step = 4.6, seed = 9)
    translate([0, 3.6, 0]) oh_prop_fence_log(len = 4.2);

// ---- 松树:只长在缓坡草地(避水 3,坡度 ≤26°,草生物群系) ----
ter_scatter(TERR, 21, 90, [0, 0, 115], [0.3, 13, 26, 3, ["grass", "grass_dark"]])
    lay_pick($seed)
    {
        oh_nature_pine(s = lay_randr($seed, 5, 0.8, 1.4), seed = $seed);
        oh_nature_pine(s = lay_randr($seed, 6, 0.9, 1.5), seed = $seed + 1);
        oh_nature_autumn(s = lay_randr($seed, 7, 0.8, 1.2), seed = $seed);
        oh_nature_bush(s = lay_randr($seed, 8, 0.9, 1.3), seed = $seed);
    }

// ---- 岩块:随坡倾斜,只出现在岩石生物群系 ----
ter_scatter(TERR, 33, 22, [-115, -95, 115, 95], [0.5, 30, 60, 0, ["rock", "rock_high"]], rot = true)
    oh_rock_boulder(s = lay_randr($seed, 4, 0.7, 1.5), seed = $seed);

// ---- 干草簇:高地枯草带 ----
ter_scatter(TERR, 47, 40, [-115, -95, 115, 95], [0.3, 20, 24, 1, ["dry_grass"]])
    oh_nature_grass(seed = $seed);
