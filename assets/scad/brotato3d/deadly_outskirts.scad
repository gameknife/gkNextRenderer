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

// 农场主体：红谷仓面向支路，收割机撂在田里，草捆散在场院与田埂之间。
translate([240, 96, 0]) rotate([0, 0, 180]) dd_bldg_barn(seed = 128, L = 11, D = 13);
translate([190, 140, 0]) rotate([0, 0, -28]) dd_veh_harvester(seed = 129);
lay_scatter(9, 100, 370, 90, 104, seed = 1210, rot = false) dd_prop_haybale(seed = $seed);
translate([258, 92, 0]) dd_prop_gascan();
translate([225, 90, 0]) dd_prop_pallet(seed = 1211);
translate([310, 90, 0]) dd_prop_tires(seed = 1212);
// 田块之间补垄沟菜田与南瓜田，填掉网格田块四周的秃斑。
translate([85, 138, 0]) dd_nature_pumpkin_patch(L = 15, D = 11, seed = 1213);
translate([225, 192, 0]) dd_nature_pumpkin_patch(L = 13, D = 10, seed = 1214);
translate([118, 186, 0]) dd_nature_field_rows(L = 16, D = 11, seed = 1215);
translate([300, 195, 0]) dd_nature_field_rows(L = 14, D = 10, seed = 1216);
// 农场场院是压实的泥地，不再是纯草皮。
translate([240, 110, 0]) dd_ground_dirt(L = 34, D = 24, seed = 1217);
translate([175, 95, 0]) dd_ground_dirt(L = 22, D = 16, seed = 1218);
translate([252, 118, 0]) dd_ground_puddle(s = 1.6, seed = 1219);
translate([182, 104, 0]) dd_ground_puddle(s = 1.1, seed = 1220);

// 北部空地立两台倒塌风机，构成远景地标带。
translate([-15, 205, 0]) rotate([0, 0, 155]) dd_prop_windturbine_fallen(seed = 1221, s = 1.1);
translate([55, 240, 0]) rotate([0, 0, -75]) dd_prop_windturbine_fallen(seed = 1222, s = 0.9);

// 南部林团间隙塞一处废弃农点，把"两片树林夹一块平地"变成场景。
translate([-140, -148, 0]) rotate([0, 0, 18]) dd_bldg_barn(seed = 1223, L = 9, D = 11);
translate([-150, -182, 0]) rotate([0, 0, 12]) dd_nature_field_rows(L = 14, D = 10, seed = 1224);
translate([-120, -200, 0]) rotate([0, 0, 8]) dd_nature_pumpkin_patch(L = 11, D = 9, seed = 1225);
translate([-138, -165, 0]) dd_ground_dirt(L = 26, D = 18, seed = 1226);
lay_scatter(4, -158, -118, -172, -152, seed = 1227, rot = false) dd_prop_haybale(seed = $seed);
translate([-118, -152, 0]) rotate([0, 0, 30]) dd_veh_flipped(seed = 1228);
translate([-152, -158, 0]) dd_ground_puddle(s = 1.3, seed = 1229);

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
// 事故现场补一辆翻覆车与散落物。
translate([126, -1.6, 0]) rotate([0, 0, 8]) dd_veh_flipped(seed = 146);
translate([97, 4.2, 0]) dd_prop_gascan();
translate([-262, 2.0, 0]) rotate([0, 0, 172]) dd_veh_flipped(seed = 147);

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

// ================= 地表细节层：泥地斑/水洼/杂物/树桩，打散剩余的大平地 =================

// 镇区院落：裸土斑 + 生活垃圾。
lay_scatter(8, -370, -165, 16, 74, seed = 171)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 13), D = lay_randr($seed, 6, 4, 9), seed = $seed);
lay_scatter(14, -370, -160, 12, 78, seed = 172) dd_prop_debris(seed = $seed);
lay_scatter(4, -360, -180, 18, 70, seed = 173) dd_ground_puddle(s = lay_randr($seed, 5, 0.8, 1.5), seed = $seed);
lay_scatter(7, -370, -170, -75, -16, seed = 174)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 12), D = lay_randr($seed, 6, 4, 8), seed = $seed);
lay_scatter(10, -370, -170, -70, -16, seed = 175) dd_prop_debris(seed = $seed);

// 公路走廊：路肩杂物与散落轮胎，事故带更密。
lay_scatter(18, -380, 380, -11, 11, seed = 176) dd_prop_debris(seed = $seed);
lay_scatter(5, -260, 200, 7, 12, seed = 177) dd_prop_tires(seed = $seed);
lay_scatter(4, 230, 350, 4, 30, seed = 178)
    lay_pick($seed) { dd_prop_pallet(seed = $seed); dd_prop_gascan(); }

// 南部与东部开阔地：泥土斑 + 草簇 + 零星水洼，让草地出现疏密纹理。
lay_scatter(12, -100, 280, -250, -100, seed = 181)
    dd_ground_dirt(L = lay_randr($seed, 5, 8, 18), D = lay_randr($seed, 6, 6, 12), seed = $seed);
lay_scatter(9, -390, -180, -260, -100, seed = 182)
    dd_ground_dirt(L = lay_randr($seed, 5, 7, 15), D = lay_randr($seed, 6, 5, 10), seed = $seed);
lay_scatter(6, -120, 240, -230, -120, seed = 183) dd_ground_puddle(s = lay_randr($seed, 5, 0.9, 1.8), seed = $seed);
lay_scatter(45, -390, 390, -260, -20, seed = 184) dd_nature_grass(seed = $seed);
lay_scatter(35, -390, 390, 20, 260, seed = 185) dd_nature_grass(seed = $seed);
lay_scatter(16, -100, 390, -250, -110, seed = 186) dd_prop_debris(seed = $seed);

// 公路两侧中段带（出生区外围）：低密度泥斑/灌木/杂物，保持可跑但不再是纯平色。
lay_scatter(9, -160, 260, -95, -25, seed = 191)
    dd_ground_dirt(L = lay_randr($seed, 5, 7, 14), D = lay_randr($seed, 6, 5, 9), seed = $seed);
lay_scatter(12, -160, 260, -95, -25, seed = 192)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_nature_bush(s = 1.1, seed = $seed); }
lay_scatter(7, -60, 140, 25, 95, seed = 193)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 12), D = lay_randr($seed, 6, 4, 8), seed = $seed);
lay_scatter(10, -60, 140, 25, 100, seed = 194)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_nature_bush(s = 1.2, seed = $seed); dd_nature_grass(seed = $seed); }

// 林缘：树桩与倒木交代"树林被砍过/塌过"的边界。
lay_scatter(9, -380, -90, 158, 182, seed = 187)
    lay_pick($seed) { dd_nature_stump(s = lay_randr($seed, 5, 0.9, 1.4), seed = $seed); dd_nature_log(seed = $seed); }
lay_scatter(7, -170, 80, -145, -90, seed = 188)
    lay_pick($seed) { dd_nature_stump(s = 1.1, seed = $seed); dd_nature_log(seed = $seed); }
lay_scatter(5, 280, 390, -60, -30, seed = 189) dd_nature_stump(s = 1.0, seed = $seed);

// ================= 全图兜底密度层（俯视一屏约 50 m，保证屏屏有物） =================

// 低密度疏林：每 400–600 m² 一棵，跳过主路走廊/农田/镇区建筑排。
lay_scatter(30, -390, 390, -262, -35, seed = 301)
    lay_pick($seed)
    {
        dd_nature_tree(s = lay_randr($seed, 5, 0.85, 1.25), seed = $seed);
        dd_nature_pine(s = lay_randr($seed, 6, 0.9, 1.35), seed = $seed);
        dd_nature_bush(s = lay_randr($seed, 7, 1.1, 1.5), seed = $seed);
    }
lay_scatter(12, -120, 70, 30, 150, seed = 302)
    lay_pick($seed) { dd_nature_tree(s = 1.0, seed = $seed); dd_nature_pine(s = 1.15, seed = $seed); }
lay_scatter(8, -390, -240, 118, 168, seed = 303)
    lay_pick($seed) { dd_nature_pine(s = 1.2, seed = $seed); dd_nature_tree(s = 0.95, seed = $seed); }
lay_scatter(8, 85, 390, 225, 268, seed = 304)
    lay_pick($seed) { dd_nature_pine(s = 1.25, seed = $seed); dd_nature_tree(s = 1.0, seed = $seed); }
lay_scatter(9, 85, 390, 22, 78, seed = 305)
    lay_pick($seed) { dd_nature_tree(s = 1.0, seed = $seed); dd_nature_bush(s = 1.3, seed = $seed); }

// 路肩弃车：逃难时抛下的车散在公路两侧草肩上。
lay_scatter(4, -380, -70, 9, 17, seed = 306)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(4, 60, 380, -17, -9, seed = 307)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); dd_veh_van(seed = $seed); }

// 镇段人行道小品：信箱/消防栓/垃圾桶按节奏排布。
lay_along([[-330, 8.6], [-160, 8.6]], step = 16, seed = 308, offset = 4)
    lay_pick($seed) { dd_prop_mailbox(); dd_prop_hydrant(); dd_prop_trash(seed = $seed); }
lay_along([[-330, -8.6], [-165, -8.6]], step = 18, seed = 309, offset = 5)
    lay_pick($seed) { dd_prop_trash(seed = $seed); dd_prop_mailbox(); dd_prop_barrel(seed = $seed); }

// 散落补给：翻倒的板条箱/油桶/草捆点缀在全图。
lay_scatter(10, -390, 390, -240, -30, seed = 315)
    lay_pick($seed) { dd_prop_crate(s = 0.9); dd_prop_barrel(seed = $seed); dd_prop_gascan(); }
lay_scatter(8, -390, 390, 30, 240, seed = 316)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(s = 0.85); dd_prop_haybale(seed = $seed); }

// 草簇/杂物/水洼加密到屏屏可见。
lay_scatter(95, -390, 390, -262, -16, seed = 311) dd_nature_grass(seed = $seed);
lay_scatter(85, -390, 390, 16, 262, seed = 312) dd_nature_grass(seed = $seed);
lay_scatter(26, -390, 390, -260, -20, seed = 313) dd_prop_debris(seed = $seed);
lay_scatter(22, -390, 390, 20, 260, seed = 314) dd_prop_debris(seed = $seed);
lay_scatter(6, -350, 350, 30, 250, seed = 317) dd_ground_puddle(s = lay_randr($seed, 5, 0.8, 1.6), seed = $seed);
lay_scatter(10, -390, 390, -255, -30, seed = 318)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 14), D = lay_randr($seed, 6, 4, 9), seed = $seed);
lay_scatter(9, -390, 390, 30, 255, seed = 319)
    dd_ground_dirt(L = lay_randr($seed, 5, 6, 13), D = lay_randr($seed, 6, 4, 9), seed = $seed);
