// pitlane_showcase.scad —— kit_pitlane 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：建筑 / 载具 / 赛道设施 / 车库内饰件 / 地面 / 植被。
// gnb shot --scene assets/scad/source/pitlane_showcase.scad

use <../lib/kit_pitlane.scad>

$fn = 12;

// 展台
color([0.38, 0.42, 0.27]) translate([0, 0, -0.15]) cube([120, 92, 0.3], center = true);

// ================= 建筑 =================
translate([-38, 26, 0]) rp_bldg_garage(seed = 0, car = 0);
translate([-26, 26, 0]) rp_bldg_garage(seed = 2, car = -1, closed = true);
translate([-10, 24, 0]) rp_bldg_control_tower(seed = 0);
translate([12, 26, 0]) rp_bldg_hospitality(seed = 3);
translate([38, 26, 0]) rp_bldg_grandstand(L = 24, seed = 1);

// ================= 载具 =================
translate([-40, 6, 0]) rp_veh_gt3(seed = 0);
translate([-32, 6, 0]) rp_veh_gt3(seed = 3);
translate([-24, 6, 0]) rp_veh_gt3(seed = 5);
translate([-15, 6, 0]) rp_veh_safety_car(seed = 0);
translate([-6, 6, 0]) rp_veh_van(seed = 1);
translate([6, 6, 0]) rp_veh_hauler(seed = 2);
translate([22, 6, 0]) rp_veh_cart(seed = 0);

// ================= 赛道设施 =================
translate([-40, -6, 0]) rp_prop_gantry(L = 14, seed = 0);
translate([-24, -6, 0]) rp_prop_pitwall(len = 8, seed = 0);
translate([-14, -6, 0]) rp_prop_pitwall(len = 8, seed = 1);
translate([-2, -6, 0]) rp_prop_tire_wall(len = 4, seed = 0);
translate([3, -6, 0]) rp_prop_tire_wall(len = 4, seed = 1);
translate([9, -6, 0]) rp_prop_guardrail(len = 6);
translate([17, -6, 0]) rp_prop_fence_catch(len = 6, seed = 0);
translate([25, -6, 0]) rp_prop_banner(len = 6, seed = 2);
translate([33, -6, 0]) rp_prop_floodlight(seed = 0);
translate([40, -6, 0]) rp_prop_pylon(seed = 0);
translate([44, -6, 0]) rp_prop_flag(seed = 1);

// ================= 车库内饰件 / 围场道具 =================
translate([-40, -16, 0]) rp_prop_workbench(seed = 0);
translate([-36, -16, 0]) rp_prop_tire_rack(seed = 0);
translate([-32, -16, 0]) rp_prop_locker(seed = 1);
translate([-28, -16, 0]) rp_prop_toolcart(seed = 0);
translate([-25, -16, 0]) rp_prop_bottle(seed = 0);
translate([-21, -16, 0]) rp_prop_lift(seed = 0);
translate([-16, -16, 0]) rp_prop_fuel_rig(seed = 0);
translate([-12, -16, 0]) rp_prop_generator(seed = 0);
translate([-7, -16, 0]) rp_prop_canopy(seed = 4);
translate([-1, -16, 0]) rp_prop_podium(seed = 0);
translate([8, -16, 0]) rp_prop_monitor(seed = 0);
translate([12, -16, 0]) rp_prop_tire_stack(seed = 0);
translate([14, -16, 0]) rp_prop_tire_stack(seed = 1, n = 4);
for (i = [0 : 4])
    translate([17 + i * 1.2, -16, 0]) rp_prop_cone(seed = i);

// ================= 地面 =================
translate([-38, -30, 0]) rp_ground_track(L = 24, seed = 0);
translate([-10, -30, 0]) rp_ground_track(L = 24, seed = 1, start = true);
translate([22, -30, 0]) rp_ground_pitlane(L = 24, seed = 0);
translate([-38, -44, 0]) rp_ground_paddock(L = 12, D = 10, seed = 0);
translate([-24, -44, 0]) rp_ground_grass(L = 10, D = 8, seed = 0);
translate([-12, -44, 0]) rp_ground_gravel(L = 10, D = 8, seed = 0);

// ================= 植被 =================
translate([6, -44, 0]) rp_nature_tree(s = 1.2, seed = 0);
translate([12, -44, 0]) rp_nature_tree(s = 0.9, seed = 3);
translate([17, -44, 0]) rp_nature_bush(s = 1.2, seed = 0);
translate([20, -44, 0]) rp_nature_bush(s = 1.0, seed = 1);
translate([26, -44, 0]) rp_nature_hedge(L = 6, seed = 0);
