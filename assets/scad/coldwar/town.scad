// coldwar/town.scad —— 废弃小镇：预制板楼主街 + 南侧村屋 + 教堂广场
// 1 unit = 1 m。gnb shot --scene assets/scad/coldwar/town.scad
use <../lib/kit_coldwar.scad>
use <../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.36, 0.41, 0.24]) translate([0, 0, -0.15]) cube([104, 84, 0.3], center = true);
translate([-30, -26, 0]) cw_ground_dirt(L = 14, D = 10, seed = 1);
translate([26, 24, 0]) cw_ground_dirt(L = 12, D = 9, seed = 2);
translate([38, -20, 0]) cw_nature_field(L = 16, D = 12, seed = 3);

// ================= 路网（主街沿 x，北支路沿 y） =================
translate([0, 0, 0]) cw_ground_cross(W = 7, seed = 1);
translate([-27.5, 0, 0]) cw_ground_road(L = 48, seed = 2);
translate([27.5, 0, 0]) cw_ground_road(L = 48, seed = 3);
translate([0, 21.5, 0]) rotate([0, 0, 90]) cw_ground_road(L = 36, seed = 4);
for (sx = [-1, 1])
    translate([sx * 26, 4.4, 0]) cw_ground_sidewalk(L = 44, seed = sx + 2);
translate([-26, -4.4, 0]) cw_ground_sidewalk(L = 44, seed = 5);
translate([26, -4.4, 0]) cw_ground_sidewalk(L = 44, seed = 6);

// ================= 北侧：预制板楼区 =================
translate([-33, 12, 0]) cw_bldg_panel_flat(seed = 1, floors = 4, L = 16, D = 10);
translate([-13, 12, 0]) cw_bldg_panel_flat(seed = 5, floors = 3, L = 14, D = 10);
translate([13, 12, 0]) cw_bldg_shop_row(seed = 2, L = 12, D = 6);
translate([27, 12, 0]) cw_bldg_shop_row(seed = 7, L = 10, D = 6);
// 楼间院落
translate([-23, 24, 0]) cw_ground_grass(L = 22, D = 14, seed = 8);
translate([-28, 24, 0]) cw_prop_playground(seed = 1);
translate([-16, 26, 0]) cw_prop_woodpile(seed = 2);
translate([-19, 21, 0]) cw_prop_dumpster();
translate([-23, 27, 0]) rotate([0, 0, 40]) cw_prop_bench();
translate([-13, 23, 0]) cw_prop_campfire(seed = 2);
translate([-14.5, 24.5, 0]) cw_item_bedroll(seed = 1);
translate([-12, 21.8, 0]) cw_item_can(seed = 3);

// ================= 东北：教堂广场 =================
translate([16, 26, 0]) cw_ground_grass(L = 18, D = 14, seed = 9);
translate([14, 28, 0]) cw_bldg_chapel(seed = 0);
translate([9, 22, 0]) cw_prop_monument(seed = 0);
translate([20, 22, 0]) rotate([0, 0, 180]) cw_prop_bench();
translate([22, 30, 0]) cw_nature_birch(s = 1.1, seed = 4);
translate([8, 31, 0]) cw_nature_pine(s = 1.0, seed = 5);

// ================= 南侧：村屋带 =================
translate([-36, -12, 0]) rotate([0, 0, 180]) cw_bldg_house_rural(seed = 1);
translate([-24, -13, 0]) rotate([0, 0, 172]) cw_bldg_house_rural(seed = 4, L = 8, D = 6);
translate([-11, -12, 0]) rotate([0, 0, 180]) cw_bldg_house_rural(seed = 6);
translate([9, -13, 0]) rotate([0, 0, 188]) cw_bldg_house_rural(seed = 9, L = 7.5, D = 5.5);
translate([22, -12, 0]) rotate([0, 0, 180]) cw_bldg_ruin(seed = 2, L = 8, D = 6);
translate([34, -13, 0]) rotate([0, 0, 180]) cw_bldg_house_rural(seed = 11);
// 院落栅栏与生活道具
for (x = [-30, -17, -3])
    translate([x, -7.5, 0]) cw_prop_fence_barbed(len = 8);
translate([-18, -17, 0]) cw_prop_well(seed = 0);
translate([-27, -18, 0]) cw_prop_woodpile(seed = 3);
translate([12, -17, 0]) cw_prop_barrel(seed = 4);
translate([-8, -16, 0]) cw_prop_debris(seed = 5);
translate([23, -17, 0]) cw_item_jerrycan(seed = 2);

// ================= 街道家具（沿主街） =================
translate([6, 5.5, 0]) cw_bldg_bus_stop(seed = 1);
translate([-8, 5.2, 0]) cw_prop_kiosk(seed = 0);
translate([-3.4, 5.0, 0]) cw_prop_phone_booth(seed = 0);
for (x = [-40, -22, 16, 34])
    translate([x, 4.9, 0]) cw_prop_lamp();
for (x = [-31, -13, 25, 43])
    translate([x, -4.9, 0]) rotate([0, 0, 180]) cw_prop_lamp();
for (x = [-44, -26, -8, 10, 28])
    translate([x, -6.5, 0]) cw_prop_pole_concrete(seed = x);
translate([-47, -5.4, 0]) cw_prop_sign_town(seed = 1);
translate([47, 5.4, 0]) rotate([0, 0, 180]) cw_prop_sign_town(seed = 0);
translate([-14, 5.4, 0]) cw_prop_sign_road(seed = 0);
translate([3.6, -8, 0]) rotate([0, 0, 90]) cw_prop_sign_road(seed = 1);
translate([40, 6.5, 0]) cw_prop_billboard(seed = 0);

// ================= 弃车 =================
translate([-18, 1.6, 0]) rotate([0, 0, 4]) cw_veh_lada(seed = 2);
translate([9, -1.8, 0]) rotate([0, 0, 184]) cw_veh_wreck(seed = 3);
translate([30, 1.7, 0]) rotate([0, 0, -8]) cw_veh_uaz_van(seed = 2);
translate([-2, 14, 0]) rotate([0, 0, 96]) cw_veh_bus(seed = 1);
translate([-38, -2, 0]) rotate([0, 0, 178]) cw_veh_lada(seed = 8);

// ================= 植被散布 =================
lay_scatter(n = 7, x0 = -50, x1 = -8, y0 = 30, y1 = 40, seed = 21)
    lay_pick($seed) { cw_nature_pine(s = 1.1, seed = $seed); cw_nature_birch(s = 1.0, seed = $seed); cw_nature_pine(s = 0.8, seed = $seed); }
lay_scatter(n = 6, x0 = -50, x1 = 50, y0 = -38, y1 = -24, seed = 22)
    lay_pick($seed) { cw_nature_pine(s = 1.2, seed = $seed); cw_nature_birch(s = 0.9, seed = $seed); cw_nature_tree_dead(s = 1.0, seed = $seed); }
lay_scatter(n = 10, x0 = -48, x1 = 48, y0 = -20, y1 = 32, seed = 23)
    lay_pick($seed) { cw_nature_bush(s = 1.0, seed = $seed); cw_nature_grass_tuft(seed = $seed); cw_nature_grass_tuft(seed = $seed + 1); }
translate([44, 26, 0]) cw_nature_rock(s = 1.3, seed = 2);
translate([-44, -22, 0]) cw_nature_stump(s = 1.0, seed = 3);
translate([46, -8, 0]) cw_nature_log(seed = 4);
