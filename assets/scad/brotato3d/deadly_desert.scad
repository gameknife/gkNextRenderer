// Brotato3D 固定场景：大型沙漠荒野。
// 1 unit = 1 metre。840 x 600 边界内，以真实公路、车辆、前哨和地貌尺度组织少数热点。

use <../lib/kit_deadly.scad>
use <../lib/kit_overhill.scad>   // 沙漠植被：oh_nature_cactus / oh_nature_deadtree
use <../lib/kit_layout.scad>

$fn = 12;

module desert_dune(x, y, sx, sy, seed)
{
    color([0.55 + (seed % 4) * 0.018, 0.39, 0.21])
        translate([x, y, -0.28]) scale([sx, sy, 0.55]) sphere(r = 1, $fn = 16);
}

module desert_mesa(x, y, s, seed)
{
    color([0.39 + (seed % 3) * 0.025, 0.25, 0.14])
    {
        translate([x, y, 1.4 * s]) cylinder(h = 2.8 * s, r1 = 3.5 * s, r2 = 2.7 * s, center = true, $fn = 7);
        translate([x + 0.5 * s, y - 0.3 * s, 3.1 * s]) cylinder(h = 1.0 * s, r1 = 2.4 * s, r2 = 2.0 * s, center = true, $fn = 7);
    }
}

module desert_rock(s = 1.0, seed = 0)
{
    rock_color = [0.34 + (seed % 4) * 0.025, 0.25 + (seed % 3) * 0.018, 0.17];
    color(rock_color)
        rotate([0, 0, seed % 180])
            scale([1.35 * s, 0.9 * s, 0.65 * s]) sphere(r = 1, $fn = 7);
    if (seed % 3 == 0)
        color([rock_color[0] * 0.86, rock_color[1] * 0.86, rock_color[2] * 0.86])
            translate([0.9 * s, -0.35 * s, 0])
                scale([0.55 * s, 0.42 * s, 0.34 * s]) sphere(r = 1, $fn = 6);
}

module desert_rock_cluster(n, x0, x1, y0, y1, seed, min_s = 0.8, max_s = 2.2)
{
    lay_scatter(n, x0, x1, y0, y1, seed = seed)
        desert_rock(s = lay_randr($seed, 4, min_s, max_s), seed = $seed);
}

color([0.47, 0.32, 0.17]) translate([0, 0, -0.18]) cube([840, 600, 0.35], center = true);

// 9 m 荒漠公路含两条 3.5 m 车道和窄路肩；废车场支路为 6.5 m。
rotate([0, 0, -8]) dd_ground_road(L = 900, W = 9, seed = 201);
translate([-235, -82, 0]) rotate([0, 0, 8]) dd_ground_road(L = 260, W = 6.5, seed = 202);
translate([-350, -58, 0]) rotate([0, 0, 98]) dd_ground_road(L = 90, W = 6.5, seed = 203);

// 沙丘以 20–60 m 长的风积带出现，盆地中心仍保持大面积空旷。
for (d = [[-390, 230, 22, 8, 1], [-350, 205, 28, 10, 2], [-305, 235, 20, 7, 3],
          [-270, 200, 25, 9, 4], [-225, 235, 18, 7, 5], [-180, 205, 24, 8, 6],
          [110, 245, 24, 8, 7], [160, 222, 30, 10, 8], [220, 250, 22, 8, 9],
          [275, 215, 27, 9, 10], [330, 245, 20, 7, 11], [375, 205, 25, 9, 12],
          [-385, -245, 28, 10, 13], [-330, -220, 22, 8, 14], [-275, -255, 30, 11, 15],
          [-205, -225, 24, 9, 16], [235, -255, 24, 8, 17], [310, -230, 30, 10, 18], [380, -260, 22, 8, 19]])
    desert_dune(d[0], d[1], d[2], d[3], d[4]);

// 西部废车场收紧到约 130 x 95 m，建筑直接服务于场内道路。
translate([-305, -75, 0]) dd_bldg_shed(seed = 211, L = 16, D = 11);
translate([-350, -98, 0]) dd_bldg_shed(seed = 212, L = 12, D = 9);
translate([-255, -98, 0]) dd_bldg_shop(seed = 213, L = 14, D = 10);
lay_scatter(36, -395, -250, -145, -48, seed = 214)
    lay_pick($seed)
    {
        dd_veh_wreck(seed = $seed);
        dd_veh_sedan(seed = $seed);
        dd_veh_pickup(seed = $seed);
        dd_veh_van(seed = $seed);
    }
lay_scatter(42, -395, -245, -150, -42, seed = 215)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_trash(seed = $seed); dd_prop_cone(); }
lay_along([[-405, -155], [-240, -155], [-240, -35], [-405, -35], [-405, -155]], step = 9, seed = 216)
    dd_prop_fence(len = 8);
// 废车场地面细节：轮胎堆/托盘/油壶成堆，两辆翻覆车，压出油渍与杂物。
lay_scatter(14, -390, -250, -145, -50, seed = 217)
    lay_pick($seed) { dd_prop_tires(seed = $seed); dd_prop_pallet(seed = $seed); dd_prop_gascan(); }
translate([-322, -118, 0]) rotate([0, 0, 40]) dd_veh_flipped(seed = 218);
translate([-268, -68, 0]) rotate([0, 0, 205]) dd_veh_flipped(seed = 219);
lay_scatter(12, -395, -245, -148, -45, seed = 220) dd_prop_debris(seed = $seed);

// 公路车队按真实车长与间距排列，原点附近约 24 m 保持通畅。
translate([35, -3.5, 0]) rotate([0, 0, -8]) lay_row(15, 11, 0, seed = 221)
    lay_jitter($seed, 1.1, 0.7, 5)
        lay_pick($seed)
        {
            dd_veh_wreck(seed = $seed);
            dd_veh_van(seed = $seed);
            dd_veh_pickup(seed = $seed);
            dd_veh_sedan(seed = $seed);
        }
lay_scatter(18, 28, 205, -34, 14, seed = 222) dd_prop_cone();
translate([28, -7, 0]) dd_prop_barricade();
translate([205, 22, 0]) rotate([0, 0, 180]) dd_prop_barricade();
// 车队里一辆翻下路基的车 + 公路走廊散落物。
translate([150, -24, 0]) rotate([0, 0, 168]) dd_veh_flipped(seed = 223);
lay_scatter(16, -200, 220, -30, 20, seed = 224) dd_prop_debris(seed = $seed);
lay_scatter(5, 40, 200, -28, 12, seed = 225)
    lay_pick($seed) { dd_prop_gascan(); dd_prop_tires(seed = $seed); }

// 东部前哨贴着公路：建筑前场距路缘 6–12 m，而非散落在几十米外。
translate([255, -48, 0]) rotate([0, 0, -8]) dd_bldg_shop(seed = 231, L = 16, D = 11);
translate([285, -55, 0]) rotate([0, 0, -8]) dd_bldg_shed(seed = 232, L = 12, D = 9);
translate([230, -62, 0]) rotate([0, 0, -8]) dd_bldg_shed(seed = 233, L = 10, D = 8);
translate([315, -62, 0]) rotate([0, 0, -8]) dd_bldg_house(seed = 234);
lay_scatter(30, 215, 335, -88, -38, seed = 235)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_dumpster(); dd_prop_trash(seed = $seed); }
translate([205, -42, 0]) dd_prop_sign_fallen(seed = 236);
translate([345, -36, 0]) rotate([0, 0, 172]) dd_prop_sign(seed = 237);

// 15–30 m 高的岩丘组成远景焦点，尺度足以与 4–5 m 建筑区分。
for (m = [[120, 145, 4, 1], [160, 175, 6, 2], [205, 140, 5, 3], [245, 185, 7, 4],
          [295, 150, 5, 5], [345, 195, 6, 6], [390, 155, 4, 7]])
    desert_mesa(m[0], m[1], m[2], m[3]);
lay_scatter(22, 85, 410, 105, 230, seed = 241) dd_nature_bush(s = 1.1, seed = $seed);

// 岩石集中成数个地质带，并以孤石连接；不做全图均匀噪点式撒布。
desert_rock_cluster(38, -205, -45, -255, -125, 242, 0.8, 2.4);
desert_rock_cluster(34, -120, 65, 95, 215, 243, 0.7, 2.0);
desert_rock_cluster(36, 55, 215, -240, -105, 244, 0.9, 2.6);
desert_rock_cluster(24, 65, 390, 82, 225, 245, 0.8, 1.8);
for (r = [[-220, 18, 2.2, 246], [-155, -35, 1.4, 247], [-72, 42, 2.8, 248],
          [78, 58, 1.8, 249], [185, 70, 2.4, 250], [355, -175, 3.0, 255]])
    translate([r[0], r[1], 0]) desert_rock(s = r[2], seed = r[3]);

// 西北避难点也靠近公路，三件主体形成孤立但可信的小组。
translate([-285, 52, 0]) rotate([0, 0, -8]) dd_bldg_church(seed = 251, L = 8, D = 14);
translate([-260, 45, 0]) rotate([0, 0, -8]) dd_bldg_shed(seed = 252, L = 8, D = 6);
translate([-310, 38, 0]) rotate([0, 0, -8]) dd_veh_wreck(seed = 253);
lay_scatter(12, -325, -245, 28, 72, seed = 254)
    lay_pick($seed) { dd_nature_bush(s = 0.9, seed = $seed); dd_prop_barrel(seed = $seed); }

lay_along([[-400, 48], [-120, 9]], step = 38, seed = 261, offset = 12) dd_prop_pole(seed = $seed);
lay_along([[170, -25], [405, -58]], step = 42, seed = 262, offset = 6) dd_prop_pole(seed = $seed);

// ================= 地表细节层：风蚀沙斑/干涸洼地/杂物，让盆地不再是一整块纯色 =================

// 深浅沙斑（覆盖 c1/c2 为沙色）分布全盆地；比地基色深半档，模拟风蚀与板结。
lay_scatter(26, -400, 400, -270, 270, seed = 271)
    dd_ground_dirt(L = lay_randr($seed, 5, 10, 24), D = lay_randr($seed, 6, 7, 16), seed = $seed,
                   c1 = [0.42, 0.28, 0.15], c2 = [0.35, 0.23, 0.12]);
lay_scatter(9, -200, 250, -100, 100, seed = 272)
    dd_ground_dirt(L = lay_randr($seed, 5, 7, 14), D = lay_randr($seed, 6, 5, 10), seed = $seed,
                   c1 = [0.52, 0.37, 0.20], c2 = [0.44, 0.30, 0.16]);
// 前哨与避难点周边的生活痕迹。
lay_scatter(10, 215, 340, -92, -35, seed = 273) dd_prop_debris(seed = $seed);
lay_scatter(6, -330, -240, 25, 75, seed = 274)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_gascan(); dd_prop_tires(seed = $seed); }
// 岩带间散落的白骨杂物。
lay_scatter(10, -180, 180, -240, -120, seed = 275) dd_prop_debris(seed = $seed);
lay_scatter(8, 80, 380, 90, 220, seed = 276) dd_prop_debris(seed = $seed);

// 西北荒滩上的倒塌风机：沙漠风电场残骸，公路可见的大地标。
translate([-95, 165, 0]) rotate([0, 0, -50]) dd_prop_windturbine_fallen(seed = 277, s = 1.2);
translate([-190, 205, 0]) rotate([0, 0, 120]) dd_prop_windturbine_fallen(seed = 278, s = 0.9);

// ================= 全图兜底密度层（俯视一屏约 50 m，保证屏屏有物） =================
// 公路沿 -8° 斜穿（y ≈ -0.14x），散布矩形避开路走廊。

// 沙漠疏林：仙人掌与枯树混布，南北两大区 + 路侧两条窄带。
lay_scatter(48, -410, 410, 75, 290, seed = 281)
    lay_pick($seed)
    {
        oh_nature_cactus(s = lay_randr($seed, 5, 0.9, 1.5), seed = $seed);
        oh_nature_deadtree(s = lay_randr($seed, 6, 0.9, 1.3), seed = $seed);
        dd_nature_bush(s = lay_randr($seed, 7, 0.9, 1.2), seed = $seed);
    }
lay_scatter(48, -410, 410, -290, -75, seed = 282)
    lay_pick($seed)
    {
        oh_nature_cactus(s = lay_randr($seed, 5, 0.9, 1.5), seed = $seed);
        oh_nature_deadtree(s = lay_randr($seed, 6, 0.9, 1.3), seed = $seed);
        dd_nature_bush(s = lay_randr($seed, 7, 0.9, 1.2), seed = $seed);
    }
lay_scatter(6, -410, -260, -35, 25, seed = 283)
    lay_pick($seed) { oh_nature_cactus(s = 1.1, seed = $seed); dd_nature_bush(s = 1.0, seed = $seed); }
lay_scatter(6, 260, 410, -18, 70, seed = 284)
    lay_pick($seed) { oh_nature_cactus(s = 1.2, seed = $seed); oh_nature_deadtree(s = 1.0, seed = $seed); }

// 孤石与小风积丘补进两大区，接续既有地质带。
lay_scatter(28, -400, 400, 80, 285, seed = 285) desert_rock(s = lay_randr($seed, 4, 0.8, 2.0), seed = $seed);
lay_scatter(28, -400, 400, -285, -80, seed = 286) desert_rock(s = lay_randr($seed, 4, 0.8, 2.2), seed = $seed);
for (d = [[-120, 120, 16, 6, 21], [40, 95, 20, 7, 22], [-40, -110, 18, 6, 23],
          [150, -140, 22, 8, 24], [-260, 150, 20, 7, 25], [330, -150, 18, 6, 26]])
    desert_dune(d[0], d[1], d[2], d[3], d[4]);

// 白骨/垃圾/补给残留加密。
lay_scatter(30, -400, 400, 78, 285, seed = 287) dd_prop_debris(seed = $seed);
lay_scatter(30, -400, 400, -285, -78, seed = 288) dd_prop_debris(seed = $seed);
lay_scatter(8, -400, 400, -280, -80, seed = 289)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_gascan(); dd_prop_tires(seed = $seed); }
lay_scatter(6, -400, 400, 80, 280, seed = 290)
    lay_pick($seed) { dd_prop_crate(s = 0.85); dd_prop_barrel(seed = $seed); }
// 干草色草簇（沙地稀疏植被）。
lay_scatter(110, -400, 400, 75, 290, seed = 291) dd_nature_grass(seed = $seed);
lay_scatter(110, -400, 400, -290, -75, seed = 292) dd_nature_grass(seed = $seed);
lay_scatter(20, -250, 250, -60, 60, seed = 293) dd_nature_grass(seed = $seed);
