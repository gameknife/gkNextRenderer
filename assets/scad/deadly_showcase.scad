// deadly_showcase.scad —— kit_deadly 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：建筑 / 载具 / 植被 / 道具 / 地面 / 农场细节 / 大件 /
//                    v2 成片地面 / v2 线性件与营地 / v2 建筑 / v2 地标。
// gnb shot --scene assets/scad/deadly_showcase.scad

use <lib/kit_deadly.scad>

$fn = 12;

// 展台
color([0.42, 0.47, 0.29]) translate([2, -58, -0.15]) cube([120, 190, 0.3], center = true);

// ================= 建筑 =================
translate([-24, 18, 0]) dd_bldg_house(seed = 3);
translate([-10, 18, 0]) dd_bldg_house_porch(seed = 7);
translate([3, 18, 0]) dd_bldg_shop(seed = 1);
translate([14, 21, 0]) dd_bldg_church(seed = 0);
translate([24, 18, 0]) dd_bldg_shed(seed = 2);
translate([37, 20, 0]) dd_bldg_barn(seed = 6);

// ================= 载具 =================
translate([-20, 8, 0]) dd_veh_sedan(seed = 4);
translate([-10, 8, 0]) dd_veh_van(seed = 8);
translate([0, 8, 0]) dd_veh_pickup(seed = 5);
translate([10, 8, 0]) dd_veh_wreck(seed = 7);
translate([20, 8, 0]) dd_veh_sedan(seed = 23);
translate([30, 8, 0]) dd_veh_flipped(seed = 9);
translate([40, 8, 0]) dd_veh_harvester(seed = 3);

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
translate([35, -16, 0]) dd_ground_dirt(L = 9, D = 7, seed = 4);
translate([43, -16, 0]) dd_ground_puddle(s = 1.2, seed = 5);

// ================= 农场细节 =================
translate([-30, -27, 0]) dd_nature_field_rows(seed = 2);
translate([-15, -27, 0]) dd_nature_pumpkin_patch(seed = 3);
translate([-6, -27, 0]) dd_nature_stump(s = 1.1, seed = 1);
translate([-2, -27, 0]) dd_nature_log(seed = 2);
translate([3, -27, 0]) dd_prop_haybale(seed = 3);
translate([7, -27, 0]) dd_prop_haybale(seed = 6);
translate([10, -27, 0]) dd_prop_gascan();
translate([13, -27, 0]) dd_prop_pallet(seed = 1);
translate([17, -27, 0]) dd_prop_tires(seed = 2);
translate([22, -27, 0]) dd_prop_debris(seed = 4);
translate([28, -27, 0]) dd_prop_debris(seed = 9);

// ================= 大件 =================
translate([-34, -39, 0]) dd_prop_windturbine_fallen(seed = 1, s = 1.0);

// ================= v2：成片地面 =================
translate([-34, -52, 0]) dd_ground_lot(L = 26, D = 18, seed = 3);
translate([-8, -52, 0]) dd_ground_gravel(L = 18, D = 14, seed = 4);
translate([12, -52, 0]) dd_ground_concrete(L = 18, D = 14, seed = 5);
translate([36, -52, 0]) dd_ground_track(L = 22, W = 4.5, seed = 6);

// ================= v2：线性界定件 =================
translate([-34, -64, 0]) dd_prop_hedge(len = 8, seed = 1);
translate([-24, -64, 0]) dd_prop_chainlink(len = 8, seed = 2);
translate([-14, -64, 0]) dd_prop_jersey(len = 3, seed = 3);
translate([-8, -64, 0]) dd_prop_sandbags(len = 4, seed = 4);
translate([0, -64, 0]) dd_prop_guardrail(len = 8, seed = 5);
translate([12, -64, 0]) dd_prop_tent(seed = 2);
translate([18, -64, 0]) dd_prop_campfire(seed = 3);
translate([24, -64, 0]) dd_prop_container(seed = 4, stack = 1);
translate([34, -64, 0]) dd_prop_container(seed = 7, stack = 2);

// ================= v2：建筑 =================
translate([-40, -80, 0]) dd_bldg_block(seed = 2, L = 16, D = 11, floors = 2);
translate([-18, -80, 0]) dd_bldg_block(seed = 5, L = 12, D = 10, floors = 3);
translate([5, -80, 0]) dd_bldg_diner(seed = 1);
translate([28, -80, 0]) dd_bldg_trailer(seed = 3);
translate([44, -80, 0]) dd_bldg_ruin(seed = 6);

translate([-38, -102, 0]) dd_bldg_warehouse(seed = 1, L = 26, D = 16);
translate([-2, -102, 0]) dd_bldg_gasstation(seed = 2);
translate([36, -102, 0]) dd_bldg_motel(seed = 4, units = 7);

// ================= v2：地标与大件 =================
translate([-36, -124, 0]) dd_bldg_watertower(s = 1.0, seed = 1);
translate([-18, -124, 0]) dd_bldg_silo(seed = 2, s = 1.0);
translate([-6, -124, 0]) dd_prop_tank(seed = 3, s = 1.0);
translate([8, -124, 0]) dd_prop_radiomast(s = 1.0, seed = 4);
translate([24, -124, 0]) dd_prop_billboard(seed = 5);
translate([40, -124, 0]) dd_veh_bus(seed = 1);
translate([40, -132, 0]) dd_veh_truck(seed = 2);
