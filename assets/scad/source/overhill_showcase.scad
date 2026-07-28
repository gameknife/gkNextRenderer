// overhill_showcase.scad —— kit_overhill 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：地形 / 建筑 / 载具 / 植被 / 道具 / 地面。
// gnb shot --scene assets/scad/source/overhill_showcase.scad

use <../lib/kit_overhill.scad>

$fn = 12;

// 展台
color([0.34, 0.40, 0.21]) translate([0, 4, -0.15]) cube([96, 78, 0.3], center = true);

// ================= 地形（缩小样张） =================
translate([-36, 30, 0]) oh_terrain_hill(s = 0.7, seed = 1);
translate([-16, 31, 0]) oh_terrain_peak(s = 0.8, seed = 2);
translate([4, 31, 0]) oh_terrain_mesa(s = 0.7, seed = 3);
translate([20, 27, 0]) oh_rock_cluster(s = 1.2, seed = 4);
translate([28, 27, 0]) oh_rock_cluster(s = 1.0, seed = 7, red = 1);
translate([36, 27, 0]) oh_rock_boulder(s = 1.6, seed = 9);

// ================= 建筑 =================
translate([-32, 12, 0]) oh_bldg_cabin(seed = 1);
translate([-18, 12, 0]) oh_bldg_garage(seed = 2);
translate([-5, 12, 0]) oh_bldg_fuel(seed = 3);
translate([6, 12, 0]) oh_bldg_tower(seed = 4);
translate([16, 12, 0]) oh_prop_bridge(L = 9);
translate([30, 12, 0]) oh_prop_gate_flags(W = 7);

// ================= 载具 =================
translate([-30, 2, 0]) oh_veh_offroader(seed = 0);
translate([-18, 2, 0]) oh_veh_offroader(seed = 3);
translate([-6, 2, 0]) oh_veh_van(seed = 2);
translate([8, 2, 0]) oh_veh_truck(seed = 1);
translate([20, 2, 0]) oh_veh_trailer(seed = 5);
translate([30, 2, 0]) oh_veh_van(seed = 9);

// ================= 植被 =================
translate([-34, -7, 0]) oh_nature_pine(s = 1.2, seed = 1);
translate([-28, -7, 0]) oh_nature_pine(s = 0.9, seed = 4);
translate([-22, -7, 0]) oh_nature_autumn(s = 1.1, seed = 2);
translate([-15, -7, 0]) oh_nature_autumn(s = 0.9, seed = 6);
translate([-9, -7, 0]) oh_nature_palm(s = 1.0, seed = 3);
translate([-3, -7, 0]) oh_nature_palm(s = 1.2, seed = 8);
translate([2, -7, 0]) oh_nature_cactus(s = 1.0, seed = 1);
translate([6, -7, 0]) oh_nature_cactus(s = 1.2, seed = 5);
translate([11, -7, 0]) oh_nature_deadtree(s = 1.0, seed = 2);
translate([16, -7, 0]) oh_nature_bush(s = 1.2, seed = 3);
translate([20, -7, 0]) oh_nature_bush(s = 1.0, seed = 7);
translate([24, -7, 0]) oh_nature_grass(seed = 1);
translate([30, -7, 0]) oh_nature_pine(s = 1.5, seed = 8);

// ================= 道具 =================
translate([-34, -15, 0]) oh_prop_tent(seed = 0);
translate([-29, -15, 0]) oh_prop_tent(seed = 2);
translate([-24, -15, 0]) oh_prop_campfire(seed = 1);
translate([-19, -15, 0]) oh_prop_log_pile(seed = 0);
translate([-13, -15, 0]) oh_prop_signpost(seed = 1);
translate([-8, -15, 0]) oh_prop_fence_log(len = 5);
translate([-1, -15, 0]) oh_prop_barrel(seed = 1);
translate([1.5, -15, 0]) oh_prop_barrel(seed = 2);
translate([4, -15, 0]) oh_prop_jerrycan(seed = 0);
translate([6, -15, 0]) oh_prop_jerrycan(seed = 1);
translate([9, -15, 0]) oh_prop_tirestack(seed = 1);

// ================= 地面 =================
translate([-30, -25, 0]) oh_ground_trail(L = 22, W = 6, seed = 1);
translate([-16, -25, 0]) oh_ground_trail_bend(W = 6, seed = 2);
translate([-4, -25, 0]) oh_ground_mud(L = 9, W = 6, seed = 3);
translate([8, -25, 0]) oh_ground_river(L = 14, W = 7, seed = 4);
translate([22, -25, 0]) oh_ground_sand(L = 12, D = 10, seed = 5);
translate([36, -25, 0]) oh_ground_grass(L = 12, D = 10, seed = 6);
