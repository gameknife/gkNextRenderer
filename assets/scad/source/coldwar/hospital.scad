// coldwar/hospital.scad —— 城市医院：主楼 + 隔离检疫区 + 分诊帐篷 + 停机坪
// 疫情崩溃叙事：军方封锁的医院。1 unit = 1 m。
// gnb shot --scene assets/scad/source/coldwar/hospital.scad
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.36, 0.40, 0.27]) translate([0, 0, -0.15]) cube([92, 72, 0.3], center = true);
translate([-30, 16, 0]) cw_ground_grass(L = 24, D = 26, seed = 1);
translate([32, 16, 0]) cw_ground_grass(L = 22, D = 24, seed = 2);
translate([2, 30, 0]) cw_ground_grass(L = 34, D = 10, seed = 7);
translate([-34, -12, 0]) cw_ground_dirt(L = 12, D = 9, seed = 8);
translate([38, -10, 0]) cw_ground_dirt(L = 9, D = 8, seed = 9);

// ================= 街道（南侧沿 x）+ 医院前庭 =================
translate([-22, -26, 0]) cw_ground_road(L = 44, seed = 3);
translate([22, -26, 0]) cw_ground_road(L = 44, seed = 4);
translate([-20, -21.6, 0]) cw_ground_sidewalk(L = 40, seed = 5);
translate([20, -21.6, 0]) cw_ground_sidewalk(L = 40, seed = 6);
color([0.44, 0.44, 0.42]) translate([0, -10, 0]) cube([34, 20, 0.22], center = true);   // 前庭场坪

// ================= 医院主楼 + 附属 =================
translate([0, 8, 0]) cw_bldg_hospital(seed = 0, floors = 4, L = 22, D = 10);
translate([-26, 6, 0]) rotate([0, 0, -90]) cw_bldg_hospital(seed = 3, floors = 2, L = 12, D = 8);   // 门诊翼
translate([26, 8, 0]) cw_bldg_warehouse(seed = 2, L = 10, D = 7);   // 太平间/库房
translate([32, 30, 0]) cw_bldg_smokestack(seed = 2, h = 9);         // 锅炉房烟囱
translate([24, 27, 0]) cw_bldg_warehouse(seed = 5, L = 9, D = 6);

// ================= 停机坪（急救转运） =================
translate([-6, 26, 0]) cw_ground_helipad(S = 11);
translate([4, 30, 0]) cw_item_crate_supply(seed = 0);
translate([2, 27.5, 0]) cw_item_medkit();
translate([6, 28.6, 0]) cw_prop_bed_hospital(seed = 2);

// ================= 隔离检疫区（军方封锁前庭） =================
translate([0, -19.5, 0]) cw_prop_fence_chain(len = 26);
translate([-13, -14, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 11);
translate([13, -14, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 11);
translate([0, -19.5, 0]) cw_prop_barrier_gate(seed = 1);
translate([-6, -21, 0]) cw_prop_wall_sandbag(len = 4);
translate([7, -21, 0]) cw_prop_wall_sandbag(len = 4);
translate([-11, -22.5, 0]) cw_prop_hedgehog();
translate([12, -23, 0]) rotate([0, 0, 40]) cw_prop_hedgehog();
translate([-16, -18, 0]) cw_bldg_checkpoint(seed = 2);
// 分诊帐篷排
translate([-7, -12, 0]) cw_prop_tent_military(seed = 0);
translate([-1, -12, 0]) cw_prop_tent_military(seed = 1);
translate([5, -12, 0]) rotate([0, 0, 6]) cw_prop_tent_military(seed = 2);
// 外置病床与医疗物资
translate([-10, -6, 0]) cw_prop_bed_hospital(seed = 0);
translate([-7, -5.5, 0]) rotate([0, 0, 14]) cw_prop_bed_hospital(seed = 1);
translate([9, -6, 0]) rotate([0, 0, -8]) cw_prop_bed_hospital(seed = 3);
translate([12, -5, 0]) rotate([0, 0, 78]) cw_prop_bed_hospital(seed = 4);
translate([-3.5, -7.2, 0]) cw_item_medkit();
translate([-1.8, -6.8, 0]) cw_item_medkit();
translate([1.5, -7.5, 0]) cw_item_crate_supply(seed = 2);
translate([6, -8.4, 0]) cw_item_can(seed = 2);
translate([-12.5, -9, 0]) cw_prop_barrel(seed = 3);
translate([13.5, -9.5, 0]) cw_prop_debris(seed = 3);
translate([-5.5, -3.8, 0]) cw_wpn_ak(seed = 4);
translate([3, -3.5, 0]) cw_item_helmet(seed = 2);

// ================= 救援车辆 =================
translate([-8, -16, 0]) rotate([0, 0, 78]) cw_veh_uaz_van(seed = 1);
translate([6, -16.5, 0]) rotate([0, 0, 96]) cw_veh_truck_canvas(seed = 1);
translate([20, -13, 0]) rotate([0, 0, 40]) cw_veh_btr(seed = 1);
translate([-28, -24.5, 0]) rotate([0, 0, 4]) cw_veh_bus(seed = 2);
translate([14, -28, 0]) rotate([0, 0, 184]) cw_veh_wreck(seed = 5);
translate([34, -24, 0]) rotate([0, 0, -6]) cw_veh_lada(seed = 6);

// ================= 街道家具 =================
for (x = [-34, -10, 14, 36])
    translate([x, -21.2, 0]) cw_prop_lamp();
for (x = [-38, -14, 10, 34])
    translate([x, -30.5, 0]) cw_prop_pole_concrete(seed = x);
translate([-20, -20.8, 0]) cw_prop_sign_road(seed = 0);
translate([24, -20.8, 0]) cw_prop_sign_town(seed = 1);
translate([-34, -31, 0]) cw_prop_billboard(seed = 2);
translate([40, -19, 0]) cw_prop_phone_booth(seed = 1);
translate([-38, 2, 0]) cw_prop_dumpster();
translate([-36, 14, 0]) cw_prop_bench();

// ================= 植被 =================
lay_scatter(n = 7, x0 = -38, x1 = -22, y0 = 16, y1 = 28, seed = 81)
    lay_pick($seed) { cw_nature_birch(s = 1.1, seed = $seed); cw_nature_pine(s = 1.0, seed = $seed); cw_nature_tree_dead(s = 1.0, seed = $seed); }
lay_scatter(n = 5, x0 = 28, x1 = 42, y0 = 10, y1 = 22, seed = 82)
    lay_pick($seed) { cw_nature_pine(s = 1.1, seed = $seed); cw_nature_bush(s = 1.1, seed = $seed); }
lay_scatter(n = 8, x0 = -40, x1 = 40, y0 = -2, y1 = 30, seed = 83)
    lay_pick($seed) { cw_nature_grass_tuft(seed = $seed); cw_nature_bush(s = 0.8, seed = $seed); cw_nature_grass_tuft(seed = $seed + 4); }
translate([-42, 26, 0]) cw_nature_pine(s = 1.3, seed = 4);
translate([42, 8, 0]) cw_nature_birch(s = 1.0, seed = 5);
translate([-42, -12, 0]) cw_nature_tree_dead(s = 1.1, seed = 6);
