// coldwar/prison.scad —— 监狱：高墙方城 + 双囚楼 + 放风场 + 角楼探照
// 1 unit = 1 m。gnb shot --scene assets/scad/coldwar/prison.scad
use <../lib/kit_coldwar.scad>
use <../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.36, 0.40, 0.25]) translate([0, 0, -0.15]) cube([92, 76, 0.3], center = true);
color([0.42, 0.42, 0.40]) translate([0, 2, 0]) cube([56, 48, 0.24], center = true);   // 监区混凝土场坪
translate([-34, -24, 0]) cw_ground_dirt(L = 14, D = 10, seed = 1);

// ================= 高墙方城（内圈 56x48，门开在南墙中） =================
for (x = [-21.5, -10.75, 10.75, 21.5])
    translate([x, -22, 0]) cw_bldg_prison_wall(len = x > -15 && x < 15 ? 10.5 : 13);
translate([0, -22, 0]) cw_bldg_prison_gate(seed = 0);
for (x = [-21, -7, 7, 21])
    translate([x, 26, 0]) cw_bldg_prison_wall(len = 14);
for (y = [-10, 2, 14])
    for (sx = [-1, 1])
        translate([sx * 28, y, 0]) rotate([0, 0, 90]) cw_bldg_prison_wall(len = 12.5);
for (sx = [-1, 1])
{
    translate([sx * 28, -17.5, 0]) rotate([0, 0, 90]) cw_bldg_prison_wall(len = 9);
    translate([sx * 28, 21.5, 0]) rotate([0, 0, 90]) cw_bldg_prison_wall(len = 9);
}
// 四角岗楼
for (sx = [-1, 1], sy = [-1, 1])
    translate([sx * 27, sy * (sy > 0 ? 25 : 21), 0]) cw_bldg_guard_tower(seed = sx * 2 + sy);

// ================= 囚楼 + 行政 =================
translate([-12, 12, 0]) cw_bldg_prison_block(seed = 0, floors = 2, L = 16, D = 7);
translate([12, 12, 0]) cw_bldg_prison_block(seed = 3, floors = 3, L = 14, D = 7);
translate([-16, -14, 0]) cw_bldg_hq(seed = 4, L = 11, D = 7);   // 行政楼
translate([18, -12, 0]) cw_bldg_warehouse(seed = 1, L = 10, D = 7);

// ================= 放风场（铁网隔离） =================
translate([0, 0, 0]) cw_prop_fence_chain(len = 20);
translate([-10, -3.5, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 7);
translate([10, -3.5, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 7);
translate([0, -7, 0]) cw_prop_fence_chain(len = 20);
translate([-5, -3, 0]) cw_prop_bench();
translate([4, -4.5, 0]) rotate([0, 0, 180]) cw_prop_bench();
translate([7.5, -2.5, 0]) cw_prop_debris(seed = 2);
translate([-7.6, -5.4, 0]) cw_item_can(seed = 4);

// ================= 监区道具 =================
translate([-22, 4, 0]) cw_prop_searchlight(seed = 0);
translate([22, 4, 0]) rotate([0, 0, 180]) cw_prop_searchlight(seed = 1);
translate([-4, 20, 0]) cw_prop_lamp();
translate([6, 20, 0]) rotate([0, 0, 180]) cw_prop_lamp();
translate([-20, 20, 0]) cw_prop_dumpster();
translate([16, 20.5, 0]) cw_prop_barrel(seed = 3);
translate([18.4, 20.2, 0]) cw_prop_barrel(seed = 7);
translate([22, -18.5, 0]) cw_prop_pallet(seed = 1);
translate([-2, -16, 0]) rotate([0, 0, 24]) cw_veh_uaz_van(seed = 4);   // 押运车弃于门内
translate([-24, 17, 0]) cw_prop_debris(seed = 5);
// 越狱缺口叙事：北墙内侧堆物 + 绳降杂物
translate([12, 23, 0]) cw_prop_crate_ammo(seed = 6);
translate([14, 22, 0]) cw_prop_tires(seed = 3);
translate([13, 20, 0]) cw_item_backpack(seed = 3);

// ================= 墙外 =================
translate([0, -33, 0]) rotate([0, 0, 90]) cw_ground_road(L = 14, W = 6, seed = 6);
translate([-4, -30, 0]) cw_prop_barrier_gate(seed = 1);
translate([6, -31, 0]) cw_prop_hedgehog();
translate([-9, -34, 0]) cw_prop_sign_road(seed = 1);
translate([12, -32, 0]) rotate([0, 0, 8]) cw_veh_bus(seed = 2);   // 囚车
translate([-18, -32, 0]) rotate([0, 0, 168]) cw_veh_wreck(seed = 8);
translate([36, -20, 0]) cw_prop_billboard(seed = 2);
translate([-38, 8, 0]) cw_prop_pole_concrete(seed = 2);
translate([38, 12, 0]) cw_prop_pole_concrete(seed = 5);

// ================= 植被（墙外荒林） =================
lay_scatter(n = 10, x0 = -44, x1 = 44, y0 = 31, y1 = 36, seed = 61)
    lay_pick($seed) { cw_nature_pine(s = 1.2, seed = $seed); cw_nature_birch(s = 1.0, seed = $seed); cw_nature_tree_dead(s = 1.1, seed = $seed); }
for (sx = [-1, 1])
    lay_scatter(n = 5, x0 = sx * 44 - 2, x1 = sx * 34 - 2, y0 = -28, y1 = 26, seed = 62 + sx)
        lay_pick($seed) { cw_nature_pine(s = 1.1, seed = $seed); cw_nature_bush(s = 1.2, seed = $seed); cw_nature_grass_tuft(seed = $seed); }
lay_scatter(n = 6, x0 = -40, x1 = 40, y0 = -36, y1 = -28, seed = 64)
    lay_pick($seed) { cw_nature_grass_tuft(seed = $seed); cw_nature_bush(s = 0.9, seed = $seed); cw_nature_stump(s = 1.0, seed = $seed); }
translate([40, 28, 0]) cw_nature_rock(s = 1.2, seed = 3);
