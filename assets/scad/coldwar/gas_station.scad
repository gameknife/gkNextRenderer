// coldwar/gas_station.scad —— 公路加油站：雨棚油泵 + 小卖部 + 弃车长龙 + 松林
// 1 unit = 1 m。gnb shot --scene assets/scad/coldwar/gas_station.scad
use <../lib/kit_coldwar.scad>
use <../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.36, 0.41, 0.24]) translate([0, 0, -0.15]) cube([96, 64, 0.3], center = true);
translate([0, 8, 0]) cw_ground_dirt(L = 20, D = 10, seed = 1);
translate([-30, -18, 0]) cw_nature_field(L = 18, D = 12, seed = 2);

// ================= 公路（沿 x 贯穿） =================
translate([-24, -4, 0]) cw_ground_road(L = 48, W = 8, seed = 3);
translate([24, -4, 0]) cw_ground_road(L = 48, W = 8, seed = 4);
// 站前场坪
color([0.33, 0.33, 0.35]) translate([2, 5.5, 0]) cube([26, 11, 0.22], center = true);

// ================= 加油站主体 =================
translate([2, 8, 0]) cw_bldg_gas_canopy(seed = 0);
translate([-1.5, 7, 0]) cw_prop_pump_gas(seed = 0);
translate([2, 7, 0]) cw_prop_pump_gas(seed = 1);
translate([5.5, 7, 0]) cw_prop_pump_gas(seed = 2);
translate([13, 4.5, 0]) cw_prop_sign_pylon(seed = 0);
translate([-12, 12, 0]) cw_bldg_shop_row(seed = 3, L = 9, D = 6);
translate([16, 14, 0]) cw_prop_fueltank(seed = 0);
translate([16, 19, 0]) cw_prop_fence_chain(len = 9);
translate([11.5, 16.5, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 6);
translate([21.5, 16.5, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 6);
// 站区杂物
translate([-6, 4, 0]) cw_prop_barrel(seed = 1);
translate([-7.5, 4.6, 0]) cw_prop_barrel(seed = 3);
translate([-16, 8, 0]) cw_prop_tires(seed = 2);
translate([8.5, 10.5, 0]) cw_prop_debris(seed = 3);
translate([-14, 5, 0]) cw_prop_dumpster();
translate([0.5, 4.2, 0]) cw_item_jerrycan(seed = 1);
translate([4.2, 4.6, 0]) cw_item_jerrycan(seed = 4);
translate([6.3, 10.8, 0]) cw_item_can(seed = 2);
translate([-10.5, 8.6, 0]) cw_prop_cart_shop(seed = 2);

// ================= 公路路标 =================
translate([-40, -9.4, 0]) cw_prop_sign_town(seed = 1);
translate([-28, 0.6, 0]) cw_prop_sign_road(seed = 0);
translate([36, 0.6, 0]) rotate([0, 0, 180]) cw_prop_sign_town(seed = 2);
translate([26, 8, 0]) cw_prop_billboard(seed = 0);
for (x = [-42, -26, -10, 10, 30, 44])
    translate([x, -9.2, 0]) rotate([0, 0, 180]) cw_prop_pole_concrete(seed = x);
for (x = [-34, -2, 38])
    translate([x, 0.8, 0]) cw_prop_lamp();

// ================= 弃车长龙（向东逃难方向） =================
translate([-38, -6, 0]) rotate([0, 0, 2]) cw_veh_lada(seed = 1);
translate([-30, -5.8, 0]) rotate([0, 0, -3]) cw_veh_uaz_van(seed = 3);
translate([-21, -6.2, 0]) cw_veh_wreck(seed = 2);
translate([-12, -5.9, 0]) rotate([0, 0, 4]) cw_veh_lada(seed = 6);
translate([-2, -6.1, 0]) rotate([0, 0, -2]) cw_veh_bus(seed = 0);
translate([9, -5.8, 0]) cw_veh_lada(seed = 9);
translate([18, -6.3, 0]) rotate([0, 0, 6]) cw_veh_wreck(seed = 7);
translate([30, -6, 0]) rotate([0, 0, -4]) cw_veh_truck_canvas(seed = 2);
translate([42, -5.7, 0]) cw_veh_lada(seed = 12);
// 对向车道零星
translate([12, -1.6, 0]) rotate([0, 0, 178]) cw_veh_uaz_van(seed = 5);
translate([-18, -1.8, 0]) rotate([0, 0, 183]) cw_veh_wreck(seed = 9);

// ================= 松林背景 + 植被 =================
lay_scatter(n = 12, x0 = -46, x1 = 46, y0 = 20, y1 = 30, seed = 41)
    lay_pick($seed) { cw_nature_pine(s = 1.3, seed = $seed); cw_nature_pine(s = 1.0, seed = $seed + 1); cw_nature_birch(s = 1.0, seed = $seed); }
lay_scatter(n = 8, x0 = -46, x1 = 46, y0 = -28, y1 = -14, seed = 42)
    lay_pick($seed) { cw_nature_pine(s = 1.1, seed = $seed); cw_nature_tree_dead(s = 1.0, seed = $seed); cw_nature_bush(s = 1.2, seed = $seed); }
lay_scatter(n = 10, x0 = -44, x1 = 44, y0 = -12, y1 = 18, seed = 43)
    lay_pick($seed) { cw_nature_grass_tuft(seed = $seed); cw_nature_bush(s = 0.8, seed = $seed); cw_nature_grass_tuft(seed = $seed + 3); }
translate([-44, 14, 0]) cw_nature_rock(s = 1.2, seed = 3);
translate([40, -22, 0]) cw_nature_log(seed = 5);
translate([34, 22, 0]) cw_nature_stump(s = 1.1, seed = 6);
