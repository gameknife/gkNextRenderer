// Brotato3D 固定场景：大型公路郊外。
// 1 unit = 1 metre。800 x 560 边界内，聚落与服务区贴近真实尺度公路，南部保持开阔。

use <../lib/kit_deadly.scad>
use <../lib/kit_layout.scad>

$fn = 12;

module rural_row(n, step, seed)
{
    lay_row(n, step, 0, seed = seed)
        lay_jitter($seed, 1.0, 0.6, 4)
            lay_pick($seed)
            {
                dd_bldg_house(seed = $seed, L = 8, D = 6);
                dd_bldg_house_porch(seed = $seed, L = 9, D = 6.5);
                dd_bldg_shed(seed = $seed, L = 7, D = 5);
            }
}

module crop_field(cols, rows, seed)
{
    lay_grid(cols, rows, 15, 12, seed = seed)
        lay_jitter($seed, 0.8, 0.8, 2)
            dd_nature_crop_patch(L = 13, D = 9, seed = $seed);
}

color([0.33, 0.39, 0.22]) translate([0, 0, -0.18]) cube([800, 560, 0.35], center = true);

// 10.5 m 双向公路（车道+路肩），支路为 7 m；道路宽度不随地图边界放大。
dd_ground_road(L = 800, W = 10.5, seed = 101);
translate([-225, 42, 0]) rotate([0, 0, 90]) dd_ground_road(L = 185, W = 7, seed = 102);
translate([225, 62, 0]) rotate([0, 0, 90]) dd_ground_road(L = 150, W = 7, seed = 103);
translate([-205, 88, 0]) dd_ground_road(L = 175, W = 7, seed = 104);
translate([255, 95, 0]) dd_ground_road(L = 190, W = 6.5, seed = 105);

// 仅镇区公路段设 1.8 m 人行道。
for (y = [-6.1, 6.1]) translate([-245, y, 0]) dd_ground_sidewalk(L = 185, W = 1.8);
for (p = [[-225, 0], [225, 0], [-225, 88], [225, 95]])
    translate([p[0], p[1], 0]) dd_ground_cross(W = 10.5, seed = p[0] + p[1] + 700);

// 西部公路镇：住宅前墙距公路路缘约 2–3 m，后排围绕 88 m 支路形成次级街坊。
translate([-350, 11.5, 0]) rural_row(10, 12.5, 111);
translate([-350, -11.5, 0]) rotate([0, 0, 180]) rural_row(9, 12.5, 112);
translate([-215, 11.5, 0]) rural_row(7, 12.5, 113);
translate([-215, -11.5, 0]) rotate([0, 0, 180]) rural_row(6, 12.5, 114);
translate([-285, 77.5, 0]) rotate([0, 0, 180]) rural_row(6, 13, 115);
translate([-285, 98.5, 0]) rural_row(5, 13, 116);
translate([-315, 28, 0]) dd_bldg_church(seed = 117, L = 8, D = 14);
translate([-260, 12, 0]) dd_bldg_shop(seed = 118, L = 13, D = 9);
translate([-190, 12, 0]) dd_bldg_shop(seed = 119, L = 12, D = 8);

// 东北农田按真实田块成片布置，并由窄支路连接；农舍靠道路，不悬在空地中央。
translate([150, 135, 0]) crop_field(7, 5, 121);
translate([270, 155, 0]) crop_field(6, 4, 122);
translate([345, 185, 0]) crop_field(3, 5, 123);
translate([165, 85, 0]) dd_bldg_house_porch(seed = 124);
translate([205, 83, 0]) dd_bldg_shed(seed = 125, L = 10, D = 7);
translate([285, 84, 0]) dd_bldg_house(seed = 126);
lay_along([[80, 112], [385, 112]], step = 9, seed = 127, offset = 3) dd_prop_fence(len = 8);

// 东部服务区紧贴公路：商店前场约 8–12 m，可容纳车辆转向和停车。
translate([248, 17, 0]) dd_bldg_shop(seed = 131, L = 16, D = 10);
translate([275, 16, 0]) dd_bldg_shop(seed = 132, L = 12, D = 8);
translate([305, 17, 0]) dd_bldg_shed(seed = 133, L = 13, D = 8);
translate([335, 18, 0]) dd_bldg_house(seed = 134);
translate([218, 13, 0]) dd_prop_sign(seed = 135);
lay_scatter(22, 235, 342, 7, 29, seed = 136)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_cone(); dd_prop_dumpster(); }

// 堵车按 4–5 m 车长和安全间距排列；中央出生区保留约 25 m 视距。
translate([-395, 2.2, 0]) lay_row(13, 10.5, 0, seed = 141)
    lay_jitter($seed, 1.0, 0.7, 4)
        lay_pick($seed)
        {
            dd_veh_sedan(seed = $seed);
            dd_veh_van(seed = $seed);
            dd_veh_pickup(seed = $seed);
            dd_veh_wreck(seed = $seed);
        }
translate([92, 2.0, 0]) rotate([0, 0, 195]) dd_veh_wreck(seed = 142);
translate([102, -2.1, 0]) rotate([0, 0, 5]) dd_veh_pickup(seed = 143);
translate([113, 2.3, 0]) rotate([0, 0, 178]) dd_veh_van(seed = 144);
lay_scatter(14, 82, 122, -5, 5, seed = 145) dd_prop_cone();

// 西北林带连续而浓密，其余方向只保留零星植被。
lay_scatter(70, -390, -70, 175, 275, seed = 151)
    lay_pick($seed)
    {
        dd_nature_pine(s = 1.4, seed = $seed);
        dd_nature_pine(s = 1.1, seed = $seed);
        dd_nature_tree(s = 1.0, seed = $seed);
    }
lay_scatter(28, -390, -300, 30, 170, seed = 152)
    lay_pick($seed) { dd_nature_pine(s = 1.3, seed = $seed); dd_nature_tree(s = 1.1, seed = $seed); }
lay_scatter(13, 40, 370, -250, -145, seed = 153) dd_nature_tree(s = 1.0, seed = $seed);
lay_scatter(10, -120, 160, -210, -105, seed = 154) dd_nature_bush(s = 1.3, seed = $seed);

// 南部不再是一整片空草地：三个彼此断开的林团形成疏密变化，同时保留迁移和战斗通道。
lay_scatter(82, -390, -175, -265, -95, seed = 155)
    lay_pick($seed)
    {
        dd_nature_pine(s = 1.35, seed = $seed);
        dd_nature_tree(s = 1.1, seed = $seed);
        dd_nature_bush(s = 1.5, seed = $seed);
    }
lay_scatter(42, -105, 85, -245, -145, seed = 156)
    lay_pick($seed)
    {
        dd_nature_pine(s = 1.15, seed = $seed);
        dd_nature_tree(s = 1.0, seed = $seed);
        dd_nature_bush(s = 1.35, seed = $seed);
    }
lay_scatter(48, 285, 390, -225, -55, seed = 157)
    lay_pick($seed)
    {
        dd_nature_pine(s = 1.3, seed = $seed);
        dd_nature_tree(s = 1.05, seed = $seed);
        dd_nature_bush(s = 1.4, seed = $seed);
    }
lay_along([[75, 128], [375, 128]], step = 18, seed = 158, offset = 5)
    dd_nature_tree(s = 0.95, seed = $seed);

lay_along([[-390, -8], [-100, -8]], step = 34, seed = 161, offset = 12) dd_prop_pole(seed = $seed);
lay_along([[140, 8], [390, 8]], step = 38, seed = 162, offset = 5) dd_prop_pole(seed = $seed);
translate([-385, -12, 0]) dd_prop_sign_fallen(seed = 163);
translate([380, 12, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 164);
