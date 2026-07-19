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
// 厂坪是碾压过的泥地，堆着托盘/轮胎/油壶，还有一辆翻在场里的车。
translate([195, -55, 0]) dd_ground_dirt(L = 85, D = 45, seed = 45);
lay_scatter(12, 150, 240, -80, -28, seed = 46)
    lay_pick($seed) { dd_prop_pallet(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_gascan(); }
translate([215, -70, 0]) rotate([0, 0, 145]) dd_veh_flipped(seed = 47);
translate([170, -35, 0]) dd_ground_puddle(s = 1.5, seed = 48);
translate([238, -62, 0]) dd_ground_puddle(s = 1.0, seed = 49);

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
translate([88, 1.9, 0]) rotate([0, 0, 195]) dd_veh_flipped(seed = 65);
translate([62, -4.6, 0]) dd_prop_gascan();

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

// ================= 镇郊农业角：东南空地补一处小农场，与工业区呼应 =================
translate([262, -138, 0]) rotate([0, 0, 12]) dd_bldg_barn(seed = 91, L = 10, D = 12);
translate([232, -162, 0]) rotate([0, 0, 8]) dd_nature_field_rows(L = 15, D = 11, seed = 92);
translate([288, -168, 0]) rotate([0, 0, 12]) dd_nature_pumpkin_patch(L = 12, D = 9, seed = 93);
translate([258, -155, 0]) dd_ground_dirt(L = 30, D = 20, seed = 94);
lay_scatter(5, 240, 285, -150, -132, seed = 95, rot = false) dd_prop_haybale(seed = $seed);
translate([300, -145, 0]) rotate([0, 0, -35]) dd_veh_harvester(seed = 96);
lay_along([[222, -128], [305, -122]], step = 9, seed = 97, offset = 2) dd_prop_fence(len = 8);
// 东侧草场上的倒塌风机（远景地标）。
translate([305, -95, 0]) rotate([0, 0, -155]) dd_prop_windturbine_fallen(seed = 98, s = 1.0);

// ================= 地表细节层：后院泥地/水洼/杂物/树桩，消掉整片纯色平地 =================

// 住宅后院带（主路南北两侧街坊内部）。
lay_scatter(9, -330, -40, 14, 38, seed = 101)
    dd_ground_dirt(L = lay_randr($seed, 5, 5, 11), D = lay_randr($seed, 6, 4, 8), seed = $seed);
lay_scatter(9, -330, -40, -38, -14, seed = 102)
    dd_ground_dirt(L = lay_randr($seed, 5, 5, 11), D = lay_randr($seed, 6, 4, 8), seed = $seed);
lay_scatter(16, -330, -30, 12, 40, seed = 103) dd_prop_debris(seed = $seed);
lay_scatter(14, -330, -30, -40, -12, seed = 104) dd_prop_debris(seed = $seed);
lay_scatter(6, -320, -50, -36, 36, seed = 105) dd_ground_puddle(s = lay_randr($seed, 5, 0.7, 1.4), seed = $seed);

// 主街与人行道：散落杂物集中在老城段。
lay_scatter(16, -220, 60, -8, 8, seed = 106) dd_prop_debris(seed = $seed);
lay_scatter(8, -220, 40, 42, 58, seed = 107) dd_prop_debris(seed = $seed);

// 公园/营地地表：踩秃的土斑、树桩和倒木。
lay_scatter(5, 68, 140, 52, 112, seed = 108)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 12), D = lay_randr($seed, 6, 5, 9), seed = $seed);
lay_scatter(6, 65, 145, 50, 115, seed = 109)
    lay_pick($seed) { dd_nature_stump(s = 1.1, seed = $seed); dd_nature_log(seed = $seed); dd_prop_debris(seed = $seed); }

// 开阔草地的疏密纹理：泥斑 + 草簇 + 林缘树桩。
lay_scatter(10, -350, -60, -240, -120, seed = 111)
    dd_ground_dirt(L = lay_randr($seed, 5, 7, 16), D = lay_randr($seed, 6, 5, 11), seed = $seed);
lay_scatter(8, 60, 340, 60, 200, seed = 112)
    dd_ground_dirt(L = lay_randr($seed, 5, 7, 15), D = lay_randr($seed, 6, 5, 10), seed = $seed);
lay_scatter(40, -350, 350, -240, -80, seed = 113) dd_nature_grass(seed = $seed);
lay_scatter(35, -350, 350, 70, 240, seed = 114) dd_nature_grass(seed = $seed);
lay_scatter(8, -340, -180, -120, -95, seed = 115)
    lay_pick($seed) { dd_nature_stump(s = lay_randr($seed, 5, 0.9, 1.3), seed = $seed); dd_nature_log(seed = $seed); }
lay_scatter(5, 185, 350, 45, 60, seed = 116) dd_nature_stump(s = 1.0, seed = $seed);
lay_scatter(6, -60, 180, -230, -150, seed = 117) dd_ground_puddle(s = lay_randr($seed, 5, 0.9, 1.7), seed = $seed);

// ================= 全图兜底密度层（俯视一屏约 50 m，保证屏屏有物） =================

// 低密度疏林：外围草地铺开，团块林之间不再有整屏空档。
lay_scatter(24, -350, 350, -240, -78, seed = 121)
    lay_pick($seed)
    {
        dd_nature_tree(s = lay_randr($seed, 5, 0.85, 1.2), seed = $seed);
        dd_nature_pine(s = lay_randr($seed, 6, 0.9, 1.3), seed = $seed);
        dd_nature_bush(s = lay_randr($seed, 7, 1.1, 1.5), seed = $seed);
    }
lay_scatter(20, -350, 350, 122, 250, seed = 122)
    lay_pick($seed) { dd_nature_pine(s = 1.2, seed = $seed); dd_nature_tree(s = 1.0, seed = $seed); }
lay_scatter(10, 185, 350, -20, 45, seed = 123)
    lay_pick($seed) { dd_nature_tree(s = 1.0, seed = $seed); dd_nature_bush(s = 1.3, seed = $seed); }
lay_scatter(8, 250, 350, -170, -85, seed = 124)
    lay_pick($seed) { dd_nature_tree(s = 0.95, seed = $seed); dd_nature_pine(s = 1.1, seed = $seed); }
// 住宅后院行道树（矮一档，避开 y=±10/±40 的建筑排）。
lay_scatter(10, -330, -45, 16, 34, seed = 125) dd_nature_tree(s = lay_randr($seed, 5, 0.75, 1.0), seed = $seed);
lay_scatter(8, -330, -45, -34, -16, seed = 126) dd_nature_tree(s = lay_randr($seed, 5, 0.75, 1.0), seed = $seed);

// 次级街道的弃车与街道小品。
lay_scatter(4, -260, 150, 47, 53, seed = 131)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(4, -265, 110, -55, -49, seed = 132)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_van(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(3, -213, -207, -50, 100, seed = 133)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); }
lay_along([[-320, 6.5], [-120, 6.5]], step = 15, seed = 134, offset = 4)
    lay_pick($seed) { dd_prop_mailbox(); dd_prop_hydrant(); dd_prop_trash(seed = $seed); }
lay_along([[-320, -6.5], [-125, -6.5]], step = 17, seed = 135, offset = 5)
    lay_pick($seed) { dd_prop_trash(seed = $seed); dd_prop_mailbox(); dd_prop_barrel(seed = $seed); }
lay_along([[-260, 43.5], [140, 43.5]], step = 21, seed = 136, offset = 6)
    lay_pick($seed) { dd_prop_mailbox(); dd_prop_trash(seed = $seed); dd_prop_hydrant(); }
// 教堂前广场的长椅与杂物。
translate([-108, 74, 0]) rotate([0, 0, 15]) dd_prop_bench();
translate([-84, 73, 0]) rotate([0, 0, -10]) dd_prop_bench();
lay_scatter(5, -110, -80, 66, 78, seed = 137) dd_prop_debris(seed = $seed);

// 散落补给与草簇/杂物加密。
lay_scatter(9, -350, 350, -230, -75, seed = 141)
    lay_pick($seed) { dd_prop_crate(s = 0.9); dd_prop_barrel(seed = $seed); dd_prop_gascan(); }
lay_scatter(7, -350, 350, 120, 240, seed = 142)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(s = 0.85); dd_prop_tires(seed = $seed); }
lay_scatter(80, -350, 350, -240, -70, seed = 143) dd_nature_grass(seed = $seed);
lay_scatter(70, -350, 350, 68, 245, seed = 144) dd_nature_grass(seed = $seed);
lay_scatter(30, 160, 350, -80, 55, seed = 145)
    lay_pick($seed) { dd_nature_grass(seed = $seed); dd_prop_debris(seed = $seed); }
lay_scatter(20, -350, 350, -235, -70, seed = 146) dd_prop_debris(seed = $seed);
lay_scatter(14, -350, 350, 118, 240, seed = 147) dd_prop_debris(seed = $seed);
lay_scatter(8, -350, 350, 120, 245, seed = 148)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 13), D = lay_randr($seed, 6, 4, 9), seed = $seed);
lay_scatter(5, -340, 340, 125, 240, seed = 149) dd_ground_puddle(s = lay_randr($seed, 5, 0.8, 1.5), seed = $seed);
// 镇南近郊口袋（工业区与南林之间）：单独补一组，消掉整屏空档。
lay_scatter(10, -40, 190, -150, -85, seed = 151)
    lay_pick($seed)
    {
        dd_nature_tree(s = lay_randr($seed, 5, 0.9, 1.2), seed = $seed);
        dd_nature_pine(s = 1.1, seed = $seed);
        dd_nature_bush(s = 1.3, seed = $seed);
    }
lay_scatter(12, -40, 190, -150, -80, seed = 152)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_nature_grass(seed = $seed); }
lay_scatter(4, -30, 180, -145, -90, seed = 153)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 12), D = lay_randr($seed, 6, 4, 8), seed = $seed);
