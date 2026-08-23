// island_showcase.scad —— kit_island 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：建筑 / 水工与载具 / 果树与植被 / 道具 / 沙滩道具 / 小物 / 地面。
// gnb shot --scene assets/scad/source/island_showcase.scad

use <../lib/kit_island.scad>

$fn = 12;

// 展台
color([0.36, 0.47, 0.21]) translate([0, -12, -0.15]) cube([150, 132, 0.3], center = true);

// ================= 建筑 =================
translate([-44, 28, 0]) is_bldg_house(seed = 0);
translate([-33, 28, 0]) is_bldg_house(seed = 4);
translate([-22, 28, 0]) is_bldg_house(seed = 9);
translate([-6, 29, 0]) is_bldg_hall(seed = 1);
translate([13, 29, 0]) is_bldg_shop(seed = 2);
translate([29, 28, 0]) is_bldg_lighthouse(s = 0.9, seed = 1);

// ================= 水工与载具（桩脚下探为预期） =================
translate([-42, 13, 0]) is_bldg_dock(L = 14);
translate([-24, 13, 0]) is_bldg_bridge(L = 8);
translate([-10, 13, -0.1]) is_veh_boat(seed = 0);
translate([0, 13, -0.1]) is_veh_boat(seed = 1);

// ================= 果树与植被 =================
translate([-44, 0, 0]) is_nature_apple(s = 1.0, seed = 1);
translate([-37, 0, 0]) is_nature_orange(s = 1.0, seed = 2);
translate([-30, 0, 0]) is_nature_peach(s = 1.0, seed = 3);
translate([-23, 0, 0]) is_nature_coconut(s = 1.0, seed = 4);
translate([-16, 0, 0]) is_nature_tree(s = 1.0, seed = 5);
translate([-9, 0, 0]) is_nature_tree(s = 1.2, seed = 6);
translate([-3, 0, 0]) is_nature_bush(s = 1.2, seed = 7);
translate([1, 0, 0]) is_nature_bush(s = 0.9, seed = 8);
translate([5, 0, 0]) is_nature_flowerbed(seed = 1);
translate([8, 0, 0]) is_nature_flowerbed(seed = 5);
translate([12, 0, 0]) is_nature_flowerbed(seed = 9);
translate([17, 0, 0]) is_nature_rock(s = 1.1, seed = 1);
translate([21, 0, 0]) is_nature_rock(s = 0.8, seed = 2);
translate([30, 0, 0]) is_nature_hedge(len = 8, seed = 1);

// ================= 庭院道具 =================
translate([-44, -12, 0]) is_prop_fence(len = 5);
translate([-36, -12, 0]) is_prop_bench();
translate([-31, -12, 0]) is_prop_lamp();
translate([-26, -12, 0]) is_prop_mailbox();
translate([-21, -12, 0]) is_prop_flagpole(h = 7);
translate([-13, -12, 0]) is_prop_fountain(s = 1.0);
translate([-6, -12, 0]) is_prop_sign(seed = 2);
translate([0, -12, 0]) is_prop_bollard();
translate([3.5, -12, 0]) is_prop_bollard();
translate([8, -12, 0]) is_prop_crate(seed = 1);
translate([11, -12, 0]) is_prop_crate(seed = 2);
translate([15, -12, 0]) is_prop_wateringcan();

// ================= 沙滩道具 =================
translate([-42, -24, 0]) is_prop_umbrella();
translate([-37, -24, 0]) is_prop_lounger();
translate([-33, -24, 0]) rotate([0, 0, 15]) is_prop_lounger();
translate([-28, -24, 0]) is_prop_beachkit();
translate([-24, -24, 0]) is_prop_torch();
translate([-20, -24, 0]) is_prop_torch();
translate([-14, -24, 0]) is_prop_firepit();
translate([-7, -24, 0]) is_prop_scarecrow(seed = 1);

// ================= 小物 =================
translate([2, -24, 0]) is_item_fruit(kind = 0);
translate([4.5, -24, 0]) is_item_fruit(kind = 1);
translate([7, -24, 0]) is_item_fruit(kind = 2);
translate([9.5, -24, 0]) is_item_fruit(kind = 3);
translate([12.5, -24, 0]) is_item_shell(seed = 1);
translate([14.5, -24, 0]) is_item_shell(seed = 4);

// ================= 地面 =================
translate([-44, -42, 0]) is_ground_plaza(L = 18, D = 14);
translate([-27, -42, 0]) is_ground_sand(L = 12, D = 10, seed = 1);
translate([-14, -42, 0]) is_ground_grass(L = 12, D = 10, seed = 2);
translate([0, -42, 0]) is_ground_path(L = 12, W = 2.4, seed = 3);
translate([9, -42, 0]) is_ground_field(L = 10, D = 8, seed = 4, crop = 1);
translate([22, -42, 0]) is_ground_field(L = 10, D = 8, seed = 5, crop = 2);
translate([36, -42, 0]) is_ground_stream(L = 16, W = 3.2, seed = 6);
translate([-38, -58, 0]) is_ground_blob(L = 24, D = 16, t = 0.12, c = is_GRASSC(), seed = 1);
translate([-24, -58, 0]) is_ground_blob(L = 20, D = 14, t = 0.12, c = is_SANDC(), seed = 2);
translate([-10, -58, 0]) is_ground_water_blob(L = 20, D = 14, t = 0.12, c = is_SEASHAL(), seed = 3,
                                               roughness = is_WATER_ROUGH_SHALLOW());
translate([4, -58, 0]) is_ground_water_blob(L = 20, D = 14, t = 0.12, c = is_SEADEEP(), seed = 4,
                                             roughness = is_WATER_ROUGH_DEEP());
