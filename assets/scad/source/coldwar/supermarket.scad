// coldwar/supermarket.scad —— 城市超市街区：大盒子超市 + 停车场 + 洗劫现场
// 1 unit = 1 m。gnb shot --scene assets/scad/source/coldwar/supermarket.scad
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.37, 0.39, 0.30]) translate([0, 0, -0.15]) cube([88, 68, 0.3], center = true);
translate([-28, 22, 0]) cw_ground_grass(L = 24, D = 16, seed = 1);
translate([34, -12, 0]) cw_ground_grass(L = 16, D = 22, seed = 2);
translate([22, 24, 0]) cw_ground_grass(L = 24, D = 14, seed = 9);
translate([36, 6, 0]) cw_ground_dirt(L = 12, D = 9, seed = 3);
translate([-38, -6, 0]) cw_ground_dirt(L = 10, D = 8, seed = 4);

// ================= 街道（南侧主街沿 x） =================
translate([-22, -22, 0]) cw_ground_road(L = 44, seed = 3);
translate([22, -22, 0]) cw_ground_road(L = 44, seed = 4);
translate([-20, -17.6, 0]) cw_ground_sidewalk(L = 40, seed = 5);
translate([20, -17.6, 0]) cw_ground_sidewalk(L = 40, seed = 6);
translate([-20, -26.4, 0]) cw_ground_sidewalk(L = 40, seed = 7);
translate([20, -26.4, 0]) cw_ground_sidewalk(L = 40, seed = 8);

// ================= 超市主体 + 停车场 =================
translate([2, 8, 0]) cw_bldg_supermarket(seed = 0, L = 22, D = 13);
translate([0, -8, 0]) cw_ground_parking(L = 30, D = 11, seed = 1);
// 停车场车辆与购物车
translate([-8, -10, 0]) rotate([0, 0, 92]) cw_veh_lada(seed = 3);
translate([-2, -10.5, 0]) rotate([0, 0, 88]) cw_veh_wreck(seed = 2);
translate([7, -10, 0]) rotate([0, 0, 94]) cw_veh_uaz_van(seed = 6);
translate([12, -9.5, 0]) rotate([0, 0, 90]) cw_veh_lada(seed = 7);
translate([-12, -6, 0]) cw_prop_cart_shop(seed = 0);
translate([3, -4.5, 0]) cw_prop_cart_shop(seed = 3);
translate([9, -6.2, 0]) cw_prop_cart_shop(seed = 5);
translate([-5, -3.6, 0]) cw_prop_cart_shop(seed = 7);
// 入口洗劫现场（货架拖到门外 + 散货）
translate([-6, 0.5, 0]) rotate([0, 0, 165]) cw_prop_shelf_market(seed = 1);
translate([0, 1, 0]) rotate([0, 0, 12]) cw_prop_shelf_market(seed = 4);
translate([6, 0.2, 0]) rotate([0, 0, 195]) cw_prop_shelf_market(seed = 6);
translate([-2.5, 1.8, 0]) cw_item_can(seed = 1);
translate([2.2, 0.4, 0]) cw_item_can(seed = 6);
translate([4.4, 1.9, 0]) cw_item_medkit();
translate([-8.6, 1.2, 0]) cw_item_backpack(seed = 4);
translate([8.8, 2.2, 0]) cw_wpn_pistol(seed = 1);
// 装卸区（超市背面）
translate([8, 17.5, 0]) cw_prop_pallet(seed = 2);
translate([11.5, 17.2, 0]) cw_prop_pallet(seed = 6);
translate([14.5, 17.8, 0]) cw_prop_dumpster();
translate([4.5, 18.2, 0]) cw_prop_crate_ammo(seed = 4);
translate([18, 16, 0]) rotate([0, 0, 24]) cw_veh_truck_canvas(seed = 4);

// ================= 街区配楼 =================
translate([-32, 8, 0]) cw_bldg_panel_flat(seed = 2, floors = 4, L = 14, D = 10);
translate([32, 10, 0]) rotate([0, 0, -90]) cw_bldg_shop_row(seed = 5, L = 11, D = 6);
translate([32, -4, 0]) rotate([0, 0, -90]) cw_bldg_ruin(seed = 4, L = 8, D = 6);
translate([-33, -12, 0]) cw_bldg_shop_row(seed = 8, L = 10, D = 6);

// ================= 街道家具 =================
translate([-14, -16.8, 0]) cw_bldg_bus_stop(seed = 0);
translate([-24, -16.5, 0]) cw_prop_kiosk(seed = 1);
translate([-19.5, -16.6, 0]) cw_prop_phone_booth(seed = 0);
for (x = [-36, -8, 16, 38])
    translate([x, -17.2, 0]) cw_prop_lamp();
for (x = [-28, 2, 30])
    translate([x, -27, 0]) rotate([0, 0, 180]) cw_prop_lamp();
for (x = [-40, -16, 8, 32])
    translate([x, -28.5, 0]) cw_prop_pole_concrete(seed = x);
translate([22, -16.9, 0]) cw_prop_sign_road(seed = 0);
translate([-42, -16.9, 0]) cw_prop_sign_town(seed = 0);
translate([26, -30, 0]) cw_prop_billboard(seed = 1);
translate([-6, -30.5, 0]) cw_prop_monument(seed = 2);
// 主街弃车
translate([-30, -20.4, 0]) rotate([0, 0, 3]) cw_veh_bus(seed = 3);
translate([12, -23.8, 0]) rotate([0, 0, 182]) cw_veh_lada(seed = 10);
translate([34, -20.2, 0]) rotate([0, 0, -5]) cw_veh_wreck(seed = 11);

// ================= 植被 =================
lay_scatter(n = 6, x0 = -36, x1 = -20, y0 = 18, y1 = 27, seed = 71)
    lay_pick($seed) { cw_nature_birch(s = 1.0, seed = $seed); cw_nature_pine(s = 1.0, seed = $seed); cw_nature_bush(s = 1.1, seed = $seed); }
lay_scatter(n = 5, x0 = 28, x1 = 40, y0 = -24, y1 = -16, seed = 72, rot = false)
    lay_pick($seed) { cw_nature_tree_dead(s = 1.0, seed = $seed); cw_nature_bush(s = 1.0, seed = $seed); }
lay_scatter(n = 8, x0 = -40, x1 = 40, y0 = 14, y1 = 30, seed = 73)
    lay_pick($seed) { cw_nature_grass_tuft(seed = $seed); cw_nature_bush(s = 0.8, seed = $seed); cw_nature_grass_tuft(seed = $seed + 1); }
lay_scatter(n = 6, x0 = 28, x1 = 42, y0 = 16, y1 = 30, seed = 74)
    lay_pick($seed) { cw_nature_birch(s = 1.0, seed = $seed); cw_nature_pine(s = 1.1, seed = $seed); cw_nature_bush(s = 1.0, seed = $seed); }
translate([-40, 28, 0]) cw_nature_pine(s = 1.2, seed = 8);
translate([40, 24, 0]) cw_nature_birch(s = 1.1, seed = 9);
// 东侧荒地补充
translate([36, -2, 0]) cw_prop_debris(seed = 6);
translate([40, -8, 0]) cw_prop_barrel(seed = 8);
translate([38.5, -9.2, 0]) cw_prop_tires(seed = 4);
translate([33, 2, 0]) cw_nature_tree_dead(s = 1.1, seed = 10);
translate([40, -14, 0]) cw_nature_bush(s = 1.2, seed = 11);
translate([-38, -2, 0]) cw_prop_woodpile(seed = 5);
