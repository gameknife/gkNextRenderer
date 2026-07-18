// Brotato3D 固定场景：大型末日城镇。
// 统一采用 1 unit = 1 metre。720 x 520 是地图边界，城镇本体保持真实的小镇街区尺度。

use <../lib/kit_deadly.scad>
use <../lib/kit_layout.scad>

$fn = 12;

module street_houses(n, step, seed)
{
    lay_row(n, step, 0, seed = seed)
        lay_jitter($seed, 0.7, 0.35, 3)
            lay_pick($seed)
            {
                dd_bldg_house(seed = $seed, L = 8, D = 6);
                dd_bldg_house_porch(seed = $seed, L = 9, D = 6.5);
                dd_bldg_shop(seed = $seed, L = 8, D = 6);
            }
}

module workshop_row(n, step, seed)
{
    lay_row(n, step, 0, seed = seed)
        lay_jitter($seed, 1.0, 0.5, 4)
            lay_pick($seed)
            {
                dd_bldg_shed(seed = $seed, L = 10, D = 7);
                dd_bldg_shed(seed = $seed, L = 13, D = 8);
                dd_bldg_shop(seed = $seed, L = 11, D = 8);
            }
}

color([0.27, 0.31, 0.19]) translate([0, 0, -0.18]) cube([720, 520, 0.35], center = true);

// 米制道路：镇内双车道 7.2–8 m；相邻道路约 50 m，形成真实街坊而非十倍拉伸的空场。
dd_ground_road(L = 650, W = 8, seed = 11);
translate([-55, 50, 0]) dd_ground_road(L = 430, W = 7.2, seed = 12);
translate([-75, -52, 0]) dd_ground_road(L = 390, W = 7.2, seed = 13);
translate([-135, 102, 0]) dd_ground_road(L = 230, W = 7.2, seed = 14);
translate([-210, 25, 0]) rotate([0, 0, 90]) dd_ground_road(L = 175, W = 7.2, seed = 15);
translate([-95, 25, 0]) rotate([0, 0, 90]) dd_ground_road(L = 175, W = 7.2, seed = 16);
translate([25, 0, 0]) rotate([0, 0, 90]) dd_ground_road(L = 110, W = 7.2, seed = 17);
translate([150, -27, 0]) rotate([0, 0, 90]) dd_ground_road(L = 82, W = 7.2, seed = 18);

// 1.8 m 人行道直接贴路缘，房屋前墙再退约 1–2 m。
for (y = [-4.9, 4.9]) translate([-70, y, 0]) dd_ground_sidewalk(L = 390, W = 1.8);
for (y = [45.5, 54.5]) translate([-85, y, 0]) dd_ground_sidewalk(L = 345, W = 1.8);
for (y = [-56.5, -47.5]) translate([-90, y, 0]) dd_ground_sidewalk(L = 300, W = 1.8);
for (x = [-214.5, -205.5]) translate([x, 24, 0]) rotate([0, 0, 90]) dd_ground_sidewalk(L = 145, W = 1.8);
for (x = [-99.5, -90.5]) translate([x, 24, 0]) rotate([0, 0, 90]) dd_ground_sidewalk(L = 145, W = 1.8);

for (p = [[-210, 0], [-95, 0], [25, 0], [150, 0], [-210, 50], [-95, 50], [25, 50],
          [-210, -52], [-95, -52], [25, -52], [-210, 102], [-95, 102]])
    translate([p[0], p[1], 0]) dd_ground_cross(W = 8, seed = p[0] + p[1] + 500);

// 西部住宅区：建筑沿街成排，前门距路缘约 2 m；街坊内部留下 18–24 m 后院。
translate([-320, 10, 0]) street_houses(8, 12.5, 21);
translate([-320, -10, 0]) rotate([0, 0, 180]) street_houses(7, 12.5, 22);
translate([-197, 10, 0]) street_houses(8, 12.5, 23);
translate([-197, -10, 0]) rotate([0, 0, 180]) street_houses(7, 12.5, 24);
translate([-197, 40, 0]) rotate([0, 0, 180]) street_houses(8, 12.5, 25);
translate([-197, 60, 0]) street_houses(7, 12.5, 26);
translate([-197, -42, 0]) street_houses(7, 12.5, 27);
translate([-197, -62, 0]) rotate([0, 0, 180]) street_houses(6, 12.5, 28);

// 老城区是密度焦点，混入商店和教堂；东侧逐步疏落。
translate([-82, 10, 0]) street_houses(8, 12, 31);
translate([-82, -10, 0]) rotate([0, 0, 180]) street_houses(7, 12, 32);
translate([-82, 40, 0]) rotate([0, 0, 180]) street_houses(8, 12, 33);
translate([-82, 60, 0]) street_houses(7, 12, 34);
translate([-82, -42, 0]) street_houses(7, 12, 35);
translate([-82, -62, 0]) rotate([0, 0, 180]) street_houses(6, 12, 36);
translate([-185, 92, 0]) rotate([0, 0, 180]) street_houses(7, 12.5, 37);
translate([-185, 112, 0]) street_houses(6, 12.5, 38);
translate([-95, 82, 0]) dd_bldg_church(seed = 39, L = 9, D = 15);

// 东南工业区紧贴支路，建筑尺度仍为真实米制，外围留作荒废厂坪。
translate([162, -42, 0]) workshop_row(5, 17, 41);
translate([162, -65, 0]) rotate([0, 0, 180]) workshop_row(4, 18, 42);
translate([230, -18, 0]) dd_bldg_shop(seed = 43, L = 14, D = 9);
lay_scatter(25, 145, 245, -82, -24, seed = 44)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_dumpster(); dd_prop_trash(seed = $seed); }

// 东北公园/疏散营地是一处独立密度团块。
translate([105, 82, 0])
    lay_scatter(30, -45, 45, -35, 35, seed = 51)
        lay_pick($seed)
        {
            dd_nature_pine(s = 1.2, seed = $seed);
            dd_nature_tree(s = 1.0, seed = $seed);
            dd_nature_bush(s = 1.3, seed = $seed);
        }
translate([100, 72, 0]) dd_bldg_church(seed = 52);
translate([80, 57, 0]) dd_prop_bench();
translate([94, 58, 0]) dd_prop_bench();

// 堵车集中在西侧；车辆宽约 1.8 m，与 8 m 双车道匹配。
translate([-310, 2.0, 0]) lay_row(10, 10.5, 0, seed = 61)
    lay_jitter($seed, 1.1, 0.6, 4)
        lay_pick($seed)
        {
            dd_veh_sedan(seed = $seed);
            dd_veh_van(seed = $seed);
            dd_veh_pickup(seed = $seed);
            dd_veh_wreck(seed = $seed);
        }
translate([67, 1.7, 0]) rotate([0, 0, 205]) dd_veh_wreck(seed = 62);
translate([76, -1.8, 0]) rotate([0, 0, 7]) dd_veh_van(seed = 63);
lay_scatter(12, 58, 88, -4, 4, seed = 64) dd_prop_cone();

// 细节只集中在老城与事故点，边缘保持荒凉。
lay_scatter(38, -205, 45, 7, 43, seed = 71)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_trash(seed = $seed); dd_prop_mailbox(); }
lay_scatter(22, -205, 35, -40, -7, seed = 72)
    lay_pick($seed) { dd_prop_dumpster(); dd_prop_mailbox(); dd_prop_hydrant(); }
lay_along([[-330, 6], [-225, 6], [-110, 6]], step = 26, seed = 73, offset = 8) dd_prop_lamp();
lay_along([[-198, 57], [5, 57]], step = 32, seed = 74, offset = 10) dd_prop_pole(seed = $seed);

lay_scatter(28, -350, -245, 180, 250, seed = 81)
    lay_pick($seed) { dd_nature_pine(s = 1.4, seed = $seed); dd_nature_tree(s = 1.1, seed = $seed); }
lay_scatter(16, 240, 350, 185, 250, seed = 82) dd_nature_pine(s = 1.3, seed = $seed);
lay_scatter(12, 280, 350, -245, -185, seed = 83) dd_nature_bush(s = 1.2, seed = $seed);

// 新增树林仍按团块组织：南部老林、东缘防风林和东南孤立树岛之间保留大片草地。
lay_scatter(72, -350, -185, -245, -115, seed = 86)
    lay_pick($seed)
    {
        dd_nature_pine(s = 1.1, seed = $seed);
        dd_nature_pine(s = 1.5, seed = $seed);
        dd_nature_tree(s = 1.15, seed = $seed);
        dd_nature_bush(s = 1.4, seed = $seed);
    }
lay_scatter(54, 190, 350, 55, 170, seed = 87)
    lay_pick($seed)
    {
        dd_nature_pine(s = 1.25, seed = $seed);
        dd_nature_tree(s = 1.0, seed = $seed);
        dd_nature_bush(s = 1.35, seed = $seed);
    }
lay_scatter(30, 55, 145, -215, -135, seed = 88)
    lay_pick($seed) { dd_nature_pine(s = 1.2, seed = $seed); dd_nature_tree(s = 1.05, seed = $seed); }
translate([-345, -18, 0]) dd_prop_sign_fallen(seed = 84);
translate([340, 18, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 85);
