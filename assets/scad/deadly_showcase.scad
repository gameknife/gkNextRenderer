// deadly_showcase.scad —— kit_deadly 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：建筑 / 载具 / 植被 / 道具 / 地面。
// gnb shot --scene assets/scad/deadly_showcase.scad

use <lib/kit_deadly.scad>

$fn = 12;

// 展台
color([0.42, 0.47, 0.29]) translate([0, 2, -0.15]) cube([64, 52, 0.3], center = true);

// ================= 建筑 =================
translate([-24, 18, 0]) dd_bldg_house(seed = 3);
translate([-10, 18, 0]) dd_bldg_house_porch(seed = 7);
translate([3, 18, 0]) dd_bldg_shop(seed = 1);
translate([14, 21, 0]) dd_bldg_church(seed = 0);
translate([24, 18, 0]) dd_bldg_shed(seed = 2);

// ================= 载具 =================
translate([-20, 8, 0]) dd_veh_sedan(seed = 4);
translate([-10, 8, 0]) dd_veh_van(seed = 8);
translate([0, 8, 0]) dd_veh_pickup(seed = 5);
translate([10, 8, 0]) dd_veh_wreck(seed = 7);
translate([20, 8, 0]) dd_veh_sedan(seed = 23);

// ================= 植被 =================
translate([-24, 0, 0]) dd_nature_pine(s = 1.3, seed = 1);
translate([-18, 0, 0]) dd_nature_pine(s = 1.0, seed = 2);
translate([-12, 0, 0]) dd_nature_tree(s = 1.0, seed = 3);
translate([-6, 0, 0]) dd_nature_bush(s = 1.2, seed = 4);
translate([2, 0, 0]) dd_nature_crop_patch(seed = 1);
translate([8, 0, 0]) dd_nature_grass(seed = 2);
translate([14, 0, 0]) dd_nature_tree(s = 1.2, seed = 9);
translate([21, 0, 0]) dd_nature_pine(s = 1.5, seed = 5);

// ================= 道具 =================
translate([-27, -7, 0]) dd_prop_fence(len = 5);
translate([-21, -7, 0]) dd_prop_lamp();
translate([-18, -7, 0]) dd_prop_pole(seed = 0);
translate([-14, -7, 0]) dd_prop_sign(seed = 2);
translate([-8, -7, 0]) dd_prop_sign_fallen(seed = 5);
translate([-3, -7, 0]) dd_prop_hydrant();
translate([-1, -7, 0]) dd_prop_mailbox();
translate([2, -7, 0]) dd_prop_trash(seed = 1);
translate([4, -7, 0]) dd_prop_trash(seed = 3);
translate([6, -7, 0]) dd_prop_barrel(seed = 2);
translate([8, -7, 0]) dd_prop_barrel(seed = 1);
translate([11, -7, 0]) dd_prop_crate();
translate([15, -7, 0]) dd_prop_dumpster();
translate([18, -7, 0]) dd_prop_cone();
translate([21, -7, 0]) dd_prop_bench();
translate([26, -7, 0]) dd_prop_barricade();

// ================= 地面 =================
translate([-20, -16, 0]) dd_ground_road(L = 16, seed = 1);
translate([-4, -16, 0]) dd_ground_cross(seed = 2);
translate([8, -16, 0]) dd_ground_sidewalk(L = 12);
translate([22, -16, 0]) dd_ground_grass(L = 10, D = 8, seed = 3);
