// coldwar/military_base.scad —— 军事基地：铁网营区 + 指挥部/营房/机库 + 停机坪坠机
// 1 unit = 1 m。gnb shot --scene assets/scad/source/coldwar/military_base.scad
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_layout.scad>

$fn = 12;

// ================= 基底 =================
color([0.35, 0.38, 0.24]) translate([0, 0, -0.15]) cube([108, 84, 0.3], center = true);
translate([6, 0, 0]) cw_ground_dirt(L = 34, D = 22, seed = 1);
translate([-34, 20, 0]) cw_ground_dirt(L = 14, D = 10, seed = 2);

// ================= 周界（铁丝网 + 岗塔 + 拒马） =================
for (x = [-45, -33, -21, -9, 15, 27, 39])
    translate([x, -36, 0]) cw_prop_fence_chain(len = 12);
for (x = [-45, -33, -21, -9, 3, 15, 27, 39])
    translate([x, 36, 0]) cw_prop_fence_chain(len = 12);
for (y = [-30, -18, -6, 6, 18, 30])
    for (sx = [-1, 1])
        translate([sx * 51, y, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 12);
for (sx = [-1, 1], sy = [-1, 1])
    translate([sx * 48, sy * 33, 0]) cw_bldg_guard_tower(seed = sx + sy);
// 南大门
translate([3, -36, 0]) cw_bldg_checkpoint(seed = 1);
translate([6.2, -35.4, 0]) cw_prop_barrier_gate(seed = 0);
translate([1, -40, 0]) cw_prop_hedgehog();
translate([10, -41, 0]) rotate([0, 0, 30]) cw_prop_hedgehog();
translate([-4, -38.5, 0]) cw_prop_wall_sandbag(len = 4);
translate([13, -38, 0]) rotate([0, 0, 90]) cw_prop_wall_sandbag(len = 3);
translate([-8, -39, 0]) cw_prop_sign_town(seed = 3);

// ================= 进场路（沿 y）+ 场内环路 =================
translate([6, -26, 0]) rotate([0, 0, 90]) cw_ground_road(L = 20, W = 6, seed = 3);
translate([6, -13, 0]) cw_ground_cross(W = 6, seed = 4);
translate([-16, -13, 0]) cw_ground_road(L = 38, W = 6, seed = 5);
translate([27, -13, 0]) cw_ground_road(L = 36, W = 6, seed = 6);

// ================= 指挥区（西） =================
translate([-30, -2, 0]) cw_bldg_hq(seed = 0, L = 13, D = 8);
translate([-44, -4, 0]) cw_prop_antenna(seed = 0);
translate([-40, -8, 0]) cw_prop_searchlight(seed = 0);
translate([-24, -8.5, 0]) cw_prop_monument(seed = 1);
translate([-36, -20, 0]) cw_bldg_barracks(seed = 1, L = 15, D = 6);
translate([-36, -29, 0]) cw_bldg_barracks(seed = 3, L = 15, D = 6);
translate([-14, -22, 0]) rotate([0, 0, 90]) cw_bldg_barracks(seed = 5, L = 12, D = 6);
translate([-45, -26, 0]) cw_bldg_bunker(seed = 0);

// ================= 机库区（东） =================
translate([28, 2, 0]) cw_bldg_hangar(seed = 0, L = 13, D = 15);
translate([44, 2, 0]) cw_bldg_hangar(seed = 1, L = 13, D = 15);
translate([38, -20, 0]) cw_bldg_warehouse(seed = 2, L = 13, D = 8);
translate([22, -24, 0]) cw_prop_fueltank(seed = 1);
translate([24, -29, 0]) cw_prop_barrel(seed = 2);
translate([26.5, -28.4, 0]) cw_prop_barrel(seed = 5);
translate([47, -28, 0]) cw_bldg_water_tower(seed = 1);

// ================= 停机坪 + 坠机现场（北） =================
translate([6, 22, 0]) cw_ground_helipad(S = 13);
translate([7, 22, 0]) cw_veh_heli_wreck(seed = 0);
translate([-3, 15, 0]) rotate([0, 0, 20]) cw_prop_debris(seed = 4);
translate([16, 27, 0]) cw_item_crate_supply(seed = 1);
translate([14.5, 24.5, 0]) cw_item_medkit();

// ================= 装备停放 =================
translate([-4, -1, 0]) rotate([0, 0, 8]) cw_veh_btr(seed = 0);
translate([8, 3, 0]) rotate([0, 0, -6]) cw_veh_tank(seed = 0);
translate([16, -7, 0]) rotate([0, 0, 92]) cw_veh_truck_canvas(seed = 0);
translate([-8, -18, 0]) rotate([0, 0, 90]) cw_veh_truck_canvas(seed = 3);
translate([0, -22, 0]) rotate([0, 0, 86]) cw_veh_uaz_van(seed = 0);
translate([26, 14, 0]) rotate([0, 0, 40]) cw_veh_wreck(seed = 6);

// ================= 帐篷营地 + 物资（东北内侧） =================
translate([-24, 24, 0]) cw_prop_tent_military(seed = 0);
translate([-30, 27, 0]) rotate([0, 0, 14]) cw_prop_tent_military(seed = 1);
translate([-17, 27, 0]) rotate([0, 0, -10]) cw_prop_tent_military(seed = 2);
translate([-23, 30.5, 0]) cw_prop_campfire(seed = 1);
translate([-20, 21, 0]) cw_wpn_crate(seed = 0);
translate([-27, 20.5, 0]) cw_prop_crate_ammo(seed = 2);
translate([-25.2, 31.8, 0]) cw_item_bedroll(seed = 3);
translate([-21.5, 32, 0]) cw_item_radio(seed = 0);
translate([-18.6, 20.6, 0]) cw_item_ammobox(seed = 1);
translate([-16, 22.5, 0]) cw_item_helmet(seed = 1);
translate([-14.8, 24.8, 0]) cw_wpn_ak(seed = 2);
translate([-31, 22, 0]) cw_item_backpack(seed = 2);
// 沙袋机枪位
translate([-6, 32, 0]) cw_prop_wall_sandbag(len = 4);
translate([-4, 30.5, 0]) cw_wpn_mosin(seed = 1);
translate([16, 33, 0]) cw_prop_searchlight(seed = 1);

// ================= 植被（营区外野生） =================
lay_scatter(n = 8, x0 = -50, x1 = 50, y0 = 38.5, y1 = 41, seed = 51)
    lay_pick($seed) { cw_nature_pine(s = 1.2, seed = $seed); cw_nature_birch(s = 1.0, seed = $seed); cw_nature_pine(s = 0.9, seed = $seed + 1); }
lay_scatter(n = 6, x0 = -52, x1 = 52, y0 = -41, y1 = -38.5, seed = 52)
    lay_pick($seed) { cw_nature_pine(s = 1.1, seed = $seed); cw_nature_tree_dead(s = 1.0, seed = $seed); cw_nature_bush(s = 1.1, seed = $seed); }
lay_scatter(n = 9, x0 = -48, x1 = 48, y0 = -32, y1 = 32, seed = 53)
    lay_pick($seed) { cw_nature_grass_tuft(seed = $seed); cw_nature_bush(s = 0.7, seed = $seed); cw_nature_grass_tuft(seed = $seed + 2); }
translate([-48, 12, 0]) cw_nature_rock(s = 1.4, seed = 4);
translate([46, 24, 0]) cw_nature_tree_dead(s = 1.1, seed = 5);
