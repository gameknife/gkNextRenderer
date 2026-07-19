// coldwar_showcase.scad —— kit_coldwar 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：大型建筑 / 工业建筑 / 军事监狱建筑 / 小型建筑 / 载具 /
//                   植被 / 围栏路标 / 工业道具 / 街道道具 / 地面 / 武器物资。
// gnb shot --scene assets/scad/coldwar_showcase.scad

use <lib/kit_coldwar.scad>

$fn = 12;

// 展台
color([0.38, 0.42, 0.27]) translate([0, -6, -0.15]) cube([110, 128, 0.3], center = true);

// ================= 大型建筑 =================
translate([-32, 46, 0]) cw_bldg_panel_flat(seed = 3, floors = 4);
translate([-6, 46, 0]) cw_bldg_hospital(seed = 1, floors = 3);
translate([22, 46, 0]) cw_bldg_supermarket(seed = 2);
translate([42, 46, 0]) cw_bldg_water_tower(seed = 1);

// ================= 工业建筑 =================
translate([-36, 30, 0]) cw_bldg_factory_hall(seed = 1);
translate([-16, 30, 0]) cw_bldg_factory_saw(seed = 2);
translate([2, 30, 0]) cw_bldg_warehouse(seed = 3);
translate([18, 30, 0]) cw_bldg_hangar(seed = 0);
translate([32, 30, 0]) cw_bldg_smokestack(seed = 0);
translate([42, 30, 0]) cw_bldg_gas_canopy(seed = 0);

// ================= 军事 / 监狱建筑 =================
translate([-38, 14, 0]) cw_bldg_hq(seed = 0);
translate([-20, 14, 0]) cw_bldg_barracks(seed = 1);
translate([-2, 14, 0]) cw_bldg_prison_block(seed = 2);
translate([12, 14, 0]) cw_bldg_prison_gate(seed = 0);
translate([24, 14, 0]) cw_bldg_prison_wall(len = 8);
translate([33, 14, 0]) cw_bldg_guard_tower(seed = 0);
translate([41, 14, 0]) cw_bldg_bunker(seed = 0);

// ================= 小型建筑 =================
translate([-38, 2, 0]) cw_bldg_house_rural(seed = 4);
translate([-26, 2, 0]) cw_bldg_shop_row(seed = 1);
translate([-14, 2, 0]) cw_bldg_chapel(seed = 0);
translate([-4, 2, 0]) cw_bldg_checkpoint(seed = 0);
translate([6, 2, 0]) cw_bldg_bus_stop(seed = 1);
translate([20, 2, 0]) cw_bldg_ruin(seed = 3);
translate([34, 2, 0]) cw_bldg_ruin(seed = 8, L = 6, D = 5);

// ================= 载具 =================
translate([-40, -9, 0]) cw_veh_lada(seed = 2);
translate([-31, -9, 0]) cw_veh_uaz_van(seed = 1);
translate([-21, -9, 0]) cw_veh_truck_canvas(seed = 0);
translate([-9, -9, 0]) cw_veh_bus(seed = 1);
translate([1, -9, 0]) cw_veh_tractor(seed = 0);
translate([11, -9, 0]) cw_veh_btr(seed = 0);
translate([22, -9, 0]) cw_veh_tank(seed = 0);
translate([31, -9, 0]) cw_veh_wreck(seed = 5);
translate([42, -9, 0]) cw_veh_heli_wreck(seed = 1);

// ================= 植被 =================
translate([-40, -19, 0]) cw_nature_pine(s = 1.2, seed = 1);
translate([-34, -19, 0]) cw_nature_pine(s = 0.9, seed = 4);
translate([-28, -19, 0]) cw_nature_birch(s = 1.1, seed = 2);
translate([-22, -19, 0]) cw_nature_birch(s = 0.9, seed = 7);
translate([-16, -19, 0]) cw_nature_tree_dead(s = 1.1, seed = 3);
translate([-11, -19, 0]) cw_nature_bush(s = 1.2, seed = 1);
translate([-7, -19, 0]) cw_nature_grass_tuft(seed = 2);
translate([-3, -19, 0]) cw_nature_reeds(seed = 1);
translate([2, -19, 0]) cw_nature_rock(s = 1.2, seed = 2);
translate([7, -19, 0]) cw_nature_stump(s = 1.0, seed = 1);
translate([12, -19, 0]) cw_nature_log(seed = 2);
translate([24, -19, 0]) cw_nature_field(L = 12, D = 8, seed = 1);

// ================= 围栏 / 路标 =================
translate([-40, -28, 0]) cw_prop_fence_concrete(len = 5);
translate([-33, -28, 0]) cw_prop_fence_chain(len = 5);
translate([-26, -28, 0]) cw_prop_fence_barbed(len = 5);
translate([-19, -28, 0]) cw_prop_wall_sandbag(len = 3);
translate([-14, -28, 0]) cw_prop_hedgehog();
translate([-10, -28, 0]) cw_prop_lamp();
translate([-6, -28, 0]) cw_prop_pole_concrete(seed = 0);
translate([-2, -28, 0]) cw_prop_sign_town(seed = 0);
translate([2, -28, 0]) cw_prop_sign_town(seed = 1);
translate([6, -28, 0]) cw_prop_sign_road(seed = 0);
translate([12, -28, 0]) cw_prop_billboard(seed = 0);
translate([20, -28, 0]) cw_prop_monument(seed = 0);
translate([26, -28, 0]) cw_prop_statue(seed = 0);
translate([32, -28, 0]) cw_prop_barrier_gate(seed = 0);
translate([40, -28, 0]) cw_prop_barrier_gate(seed = 1);

// ================= 工业 / 军事道具 =================
translate([-40, -37, 0]) cw_prop_barrel(seed = 1);
translate([-38, -37, 0]) cw_prop_barrel(seed = 2);
translate([-34, -37, 0]) cw_prop_crate_ammo(seed = 1);
translate([-30, -37, 0]) cw_prop_pallet(seed = 1);
translate([-26, -37, 0]) cw_prop_tires(seed = 2);
translate([-22, -37, 0]) cw_prop_dumpster();
translate([-17, -37, 0]) cw_prop_pump_gas(seed = 0);
translate([-14, -37, 0]) cw_prop_pump_gas(seed = 1);
translate([-8, -37, 0]) cw_prop_fueltank(seed = 0);
translate([0, -37, 0]) cw_prop_sign_pylon(seed = 0);
translate([6, -37, 0]) cw_prop_antenna(seed = 0);
translate([12, -37, 0]) cw_prop_searchlight(seed = 0);
translate([18, -37, 0]) cw_prop_tent_military(seed = 0);
translate([26, -37, 0]) cw_prop_campfire(seed = 0);
translate([32, -37, 0]) cw_wpn_crate(seed = 0);

// ================= 街道 / 室内道具 =================
translate([-40, -45, 0]) cw_prop_cart_shop(seed = 0);
translate([-36, -45, 0]) cw_prop_cart_shop(seed = 3);
translate([-31, -45, 0]) cw_prop_shelf_market(seed = 1);
translate([-26, -45, 0]) cw_prop_shelf_market(seed = 4);
translate([-20, -45, 0]) cw_prop_kiosk(seed = 0);
translate([-15, -45, 0]) cw_prop_phone_booth(seed = 0);
translate([-11, -45, 0]) cw_prop_well(seed = 0);
translate([-6, -45, 0]) cw_prop_woodpile(seed = 1);
translate([-2, -45, 0]) cw_prop_bench();
translate([6, -45, 0]) cw_prop_playground(seed = 0);
translate([14, -45, 0]) cw_prop_bed_hospital(seed = 1);
translate([18, -45, 0]) cw_prop_bed_hospital(seed = 4);
translate([24, -45, 0]) cw_prop_debris(seed = 2);

// ================= 大件 =================
translate([36, -45, 0]) cw_prop_bridge(L = 20, W = 8);

// ================= 地面 =================
translate([-32, -54, 0]) cw_ground_road(L = 16, seed = 1);
translate([-18, -54, 0]) cw_ground_cross(seed = 2);
translate([-8, -54, 0]) cw_ground_sidewalk(L = 10, seed = 1);
translate([3, -54, 0]) cw_ground_grass(L = 8, D = 7, seed = 3);
translate([12, -54, 0]) cw_ground_dirt(L = 8, D = 6, seed = 4);
translate([22, -54, 0]) cw_ground_parking(L = 10, D = 7, seed = 1);
translate([33, -54, 0]) cw_ground_helipad(S = 9);
translate([44, -54, 0]) cw_ground_rail(L = 10, seed = 0);

// ================= 武器 / 物资（灰台上展示） =================
color([0.46, 0.46, 0.44]) translate([-14, -62, 0]) cube([60, 4, 0.12], center = true);
translate([-40, -62, 0.12]) cw_wpn_ak(seed = 0);
translate([-36, -62, 0.12]) cw_wpn_svd(seed = 0);
translate([-32, -62, 0.12]) cw_wpn_mosin(seed = 0);
translate([-28, -62, 0.12]) cw_wpn_shotgun(seed = 0);
translate([-25, -62, 0.12]) cw_wpn_pistol(seed = 0);
translate([-22, -62, 0.12]) cw_wpn_rpg(seed = 0);
translate([-18, -62, 0.12]) cw_item_can(seed = 1);
translate([-16.5, -62, 0.12]) cw_item_jerrycan(seed = 0);
translate([-14.5, -62, 0.12]) cw_item_medkit();
translate([-12.5, -62, 0.12]) cw_item_ammobox(seed = 0);
translate([-10.5, -62, 0.12]) cw_item_backpack(seed = 0);
translate([-8, -62, 0.12]) cw_item_radio(seed = 0);
translate([-6, -62, 0.12]) cw_item_lantern(seed = 0);
translate([-4, -62, 0.12]) cw_item_bedroll(seed = 0);
translate([-1.5, -62, 0.12]) cw_item_helmet(seed = 0);
translate([1, -62, 0.12]) cw_item_crate_supply(seed = 0);
