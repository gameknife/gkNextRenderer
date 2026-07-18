// Brotato3D 固定场景：大型沙漠荒野。
// 1 unit = 1 metre。840 x 600 边界内，以真实公路、车辆、前哨和地貌尺度组织少数热点。

use <../lib/kit_deadly.scad>
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
