// coldwar/factory.scad —— 废弃工厂：围墙厂区 + 车间群 + 烟囱 + 铁路专用线
// 1 unit = 1 m。gnb shot --scene assets/scad/source/coldwar/factory.scad
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.38, 0.37, 0.31]) translate([0, 0, -0.15]) cube([100, 76, 0.3], center = true);
translate([0, 2, 0]) cw_ground_dirt(L = 30, D = 20, seed = 1);
translate([-28, -12, 0]) cw_ground_dirt(L = 16, D = 12, seed = 2);
translate([30, 18, 0]) cw_ground_grass(L = 24, D = 16, seed = 3);

// ================= 厂区道路（大门在南，主路沿 y 进厂） =================
translate([0, -26, 0]) rotate([0, 0, 90]) cw_ground_road(L = 20, W = 6, seed = 4);
translate([0, -8, 0]) cw_ground_cross(W = 6, seed = 5);
translate([-19, -8, 0]) cw_ground_road(L = 32, W = 6, seed = 6);
translate([19, -8, 0]) cw_ground_road(L = 32, W = 6, seed = 7);

// ================= 围墙 + 大门 =================
for (x = [-43.75, -31.25, -18.75])
    translate([x, -33.5, 0]) cw_prop_fence_concrete(len = 12.5);
for (x = [18.75, 31.25, 43.75])
    translate([x, -33.5, 0]) cw_prop_fence_concrete(len = 12.5);
for (x = [-43.75, -31.25, -18.75, -6.25, 6.25, 18.75, 31.25, 43.75])
    translate([x, 33.5, 0]) cw_prop_fence_concrete(len = 12.5);
for (y = [-27.3, -14.8, -2.3, 10.2, 22.7])
    for (sx = [-1, 1])
        translate([sx * 49.5, y, 0]) rotate([0, 0, 90]) cw_prop_fence_concrete(len = 12.5);
// 大门岗
translate([-6, -33.5, 0]) cw_bldg_checkpoint(seed = 0);
translate([-3.2, -33, 0]) cw_prop_barrier_gate(seed = 0);
translate([8, -34.6, 0]) cw_prop_billboard(seed = 1);
translate([-10, -30, 0]) cw_prop_sign_road(seed = 2);

// ================= 车间群 =================
translate([-24, 12, 0]) cw_bldg_factory_hall(seed = 1, L = 18, D = 11);
translate([-2, 12, 0]) cw_bldg_factory_saw(seed = 2, L = 16, D = 11);
translate([-30, -22, 0]) cw_bldg_warehouse(seed = 3, L = 14, D = 9);
translate([24, -20, 0]) rotate([0, 0, 90]) cw_bldg_factory_hall(seed = 4, L = 14, D = 9);
translate([-42, 2, 0]) rotate([0, 0, -90]) cw_bldg_hq(seed = 2, L = 12, D = 8);   // 厂办
translate([16, 12, 0]) cw_bldg_smokestack(seed = 0, h = 16);
translate([21, 8, 0]) cw_bldg_smokestack(seed = 1, h = 11);
translate([30, 28, 0]) cw_bldg_water_tower(seed = 0);
translate([42, -6, 0]) cw_bldg_ruin(seed = 5, L = 9, D = 7);

// ================= 铁路专用线（东西向穿北区） =================
for (x = [-40, -20, 0, 20, 40])
    translate([x, 26, 0]) cw_ground_rail(L = 20, seed = x);
translate([-12, 22.5, 0]) cw_prop_crate_ammo(seed = 3);
translate([-6, 22.8, 0]) cw_prop_pallet(seed = 2);
translate([-2, 22.4, 0]) cw_prop_pallet(seed = 5);
translate([4, 22.6, 0]) cw_prop_barrel(seed = 1);
translate([6, 22.6, 0]) cw_prop_barrel(seed = 6);
translate([-44, 22, 0]) cw_prop_debris(seed = 7);

// ================= 油罐区 =================
translate([36, 6, 0]) cw_prop_fueltank(seed = 0);
translate([36, 1, 0]) cw_prop_fueltank(seed = 1);
translate([30, 3, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 10);
translate([41, 9.5, 0]) cw_prop_barrel(seed = 2);
translate([43, 8.8, 0]) cw_prop_barrel(seed = 9);

// ================= 场院道具 =================
translate([-14, -2, 0]) cw_prop_tires(seed = 1);
translate([-10, -3, 0]) cw_prop_debris(seed = 2);
translate([8, -2, 0]) cw_prop_pallet(seed = 7);
translate([11, -1, 0]) cw_prop_crate_ammo(seed = 8);
translate([-25, -14, 0]) cw_prop_dumpster();
translate([5, -14, 0]) cw_veh_truck_canvas(seed = 1);
translate([-12, -25, 0]) rotate([0, 0, 12]) cw_veh_tractor(seed = 1);
translate([14, -27, 0]) rotate([0, 0, 176]) cw_veh_wreck(seed = 4);
translate([-38, -28, 0]) cw_prop_woodpile(seed = 4);
for (x = [-16, 4, 26])
    translate([x, -5, 0]) cw_prop_lamp();
translate([34, -14, 0]) cw_prop_pole_concrete(seed = 3);
translate([-34, -6, 0]) cw_prop_pole_concrete(seed = 4);
// 幸存者角落（车间背后）
translate([-24, 24, 0]) cw_prop_campfire(seed = 3);
translate([-26, 25.5, 0]) cw_item_bedroll(seed = 2);
translate([-22.3, 23, 0]) cw_item_can(seed = 5);
translate([-27, 22.5, 0]) cw_item_lantern(seed = 1);
translate([-21, 25.8, 0]) cw_wpn_shotgun(seed = 1);

// ================= 植被（厂区野草化） =================
lay_scatter(n = 8, x0 = -46, x1 = 46, y0 = 29, y1 = 32, seed = 31)
    lay_pick($seed) { cw_nature_tree_dead(s = 1.0, seed = $seed); cw_nature_bush(s = 1.1, seed = $seed); cw_nature_birch(s = 0.9, seed = $seed); }
lay_scatter(n = 12, x0 = -46, x1 = 46, y0 = -30, y1 = 20, seed = 32, rot = true)
    lay_pick($seed) { cw_nature_grass_tuft(seed = $seed); cw_nature_bush(s = 0.8, seed = $seed); cw_nature_grass_tuft(seed = $seed + 2); }
translate([44, 24, 0]) cw_nature_pine(s = 1.1, seed = 6);
translate([-46, 28, 0]) cw_nature_pine(s = 1.3, seed = 7);
translate([-46, -18, 0]) cw_nature_tree_dead(s = 1.2, seed = 8);
