// astro_showcase.scad —— kit_astro 零件总览（按类别排行，验收/选型用）
// 行序（自北向南）：悬浮岛与地面 / 大型平台机关 / 小型平台与地面薄板 / 结构 A / 结构 B /
//                   植被地景 / 道具机关 A / 道具机关 B / 收集物 / 角色。
// 悬浮岛一行不铺展台（岛体向下延伸，本来就是悬空的）。
// gnb shot --scene assets/scad/source/astro_showcase.scad

use <../lib/kit_astro.scad>
use <../lib/gk_camera.scad>

$fn = 12;

// 展台（岛屿行之外）
color([0.34, 0.46, 0.22]) translate([0, -18, -0.15]) cube([136, 104, 0.3], center = true);

// ================= 悬浮岛 / 地面 =================
translate([-44, 50, 0]) ab_ground_island(L = 20, D = 16, seed = 1);
translate([-20, 50, 0]) ab_ground_island(L = 14, D = 12, seed = 2, top = 1);
translate([-5, 50, 0]) ab_ground_island_round(r = 7, seed = 3);
translate([8, 50, 0]) ab_ground_island_round(r = 4.5, seed = 4, top = 2);
translate([20, 50, 0]) ab_ground_tile(L = 8, D = 8);
translate([32, 50, 0]) ab_ground_pool(L = 8, D = 6);
translate([44, 50, 0]) ab_ground_lava(L = 8, D = 6, seed = 1);
translate([56, 50, 0]) ab_ground_path(L = 8, seed = 2);

// ================= 大型平台机关 =================
translate([-54, 30, 0]) ab_plat_moving(rail = 10, t = 0.6);
translate([-40, 30, 0]) ab_plat_spin(r = 3);
translate([-30, 30, 0]) ab_plat_seesaw();
translate([-20, 30, 0]) ab_plat_pendulum();
translate([-8, 30, 0]) ab_plat_roller(L = 5);
translate([4, 30, 0]) ab_plat_bridge(L = 8);
translate([16, 30, 0]) ab_plat_conveyor(L = 6);
translate([24, 30, 2]) ab_plat_zipline(L = 12, drop = 2, t = 0.35);
translate([44, 30, 0]) ab_plat_pillar(h = 4);
translate([52, 30, 0]) ab_plat_toyblocks(seed = 2);
translate([60, 30, 0]) ab_bldg_gantry(w = 5, h = 6);
translate([60, 30, 0]) ab_prop_spike_ball_chain(h = 6);

// ================= 小型平台 / 地面薄板 =================
translate([-54, 18, 0]) ab_plat_block(L = 2, D = 2, H = 1);
translate([-50, 18, 0]) ab_plat_block(L = 2, D = 2, H = 2, c = ab_BLUE());
translate([-42, 18, 0]) ab_plat_stairs(n = 4, w = 3);
translate([-32, 18, 0]) ab_plat_float(r = 2);
translate([-25, 18, 0]) ab_plat_float(r = 1.3, c = ab_ORANGE());
translate([-19, 18, 0]) ab_plat_crumble();
translate([-13, 18, 0]) ab_plat_bounce();
translate([-7, 18, 0]) ab_plat_spring();
translate([-2, 18, 0]) ab_plat_toyblock(s = 1.5, c = ab_TEAL());
translate([6, 18, 0]) ab_ground_grass(L = 6, D = 6, seed = 1);
translate([14, 18, 0]) ab_ground_sand(L = 6, D = 6, seed = 2);
translate([22, 18, 0]) ab_ground_tile(L = 6, D = 6, cell = 0.75);
translate([32, 18, 0]) ab_prop_gate_bars(w = 4, h = 3);
translate([40, 18, 0]) ab_prop_fence(len = 5);
translate([48, 18, 0]) ab_prop_laser(L = 6);

// ================= 结构 A =================
translate([-50, 4, 0]) ab_bldg_goal();
translate([-36, 4, 0]) ab_bldg_startpad();
translate([-24, 4, 0]) ab_bldg_ship();
translate([-12, 4, 0]) ab_bldg_tower(h = 8);
translate([0, 4, 0]) ab_bldg_windmill(h = 7, seed = 1);
translate([12, 4, 0]) ab_bldg_arch(w = 5, h = 5, seed = 2);
translate([22, 4, 0]) ab_bldg_wall_break(L = 4, h = 3, seed = 1);
translate([32, 4, 0]) ab_bldg_pipe(L = 6);
translate([42, 4, 0]) ab_bldg_pipe_up();
translate([52, 4, 0]) ab_bldg_house(seed = 1);

// ================= 结构 B / 大型道具 =================
translate([-52, -10, 0]) ab_bldg_house(seed = 4);
translate([-42, -10, 0]) ab_bldg_billboard(seed = 1);
translate([-32, -10, 0]) ab_prop_hoop();
translate([-24, -10, 0]) ab_prop_fountain_jet(h = 4);
translate([-16, -10, 0]) ab_prop_fan();
translate([-8, -10, 0]) ab_prop_cage(seed = 2);
translate([0, -10, 0]) ab_prop_checkpoint();
translate([8, -10, 0]) ab_prop_balloon(seed = 1);
translate([16, -10, 0]) ab_prop_bubble();
translate([24, -10, 0]) ab_prop_lamp();
translate([32, -10, 0]) ab_nature_cloud(s = 1.2, seed = 1);
translate([44, -10, 0]) ab_nature_cloud(s = 0.8, seed = 2);

// ================= 植被地景 =================
translate([-54, -22, 0]) ab_nature_tree_ball(s = 1.0, seed = 1);
translate([-46, -22, 0]) ab_nature_tree_ball(s = 0.8, seed = 4);
translate([-39, -22, 0]) ab_nature_tree_cone(s = 1.0, seed = 2);
translate([-32, -22, 0]) ab_nature_palm(s = 1.0, seed = 3);
translate([-26, -22, 0]) ab_nature_bush(s = 1.2, seed = 1);
translate([-22, -22, 0]) ab_nature_flower(s = 1.0, seed = 2);
translate([-19, -22, 0]) ab_nature_flower(s = 0.7, seed = 5);
translate([-16, -22, 0]) ab_nature_grass_tuft(seed = 1);
translate([-12, -22, 0]) ab_nature_rock(s = 1.2, seed = 2);
translate([-6, -22, 0]) ab_nature_mushroom(s = 1.0, seed = 1);
translate([-1, -22, 0]) ab_nature_mushroom(s = 0.6, seed = 4);
translate([5, -22, 0]) ab_nature_fruit(kind = 0);
translate([10, -22, 0]) ab_nature_fruit(kind = 1);
translate([15, -22, 0]) ab_nature_fruit(kind = 2);
translate([21, -22, 0]) ab_nature_cactus(s = 1.0, seed = 1);
translate([27, -22, 0]) ab_nature_crystal(s = 1.0, seed = 1);
translate([32, -22, 0]) ab_nature_lilypad(seed = 1);
translate([40, -22, 0]) ab_nature_tree_cone(s = 1.3, seed = 7);
translate([50, -22, 0]) ab_nature_tree_ball(s = 1.2, seed = 9);

// ================= 道具机关 A =================
translate([-54, -32, 0]) ab_prop_crate();
translate([-51, -32, 0]) ab_prop_crate(s = 1.4, seed = 1);
translate([-46, -32, 0]) ab_prop_sign_arrow(seed = 0, dir = 0);
translate([-42, -32, 0]) ab_prop_sign_arrow(seed = 1, dir = 90);
translate([-38, -32, 0]) ab_prop_sign_board(seed = 2);
translate([-34, -32, 0]) ab_prop_flag(h = 4, seed = 1);
translate([-28, -32, 0]) ab_prop_spikes(len = 3);
translate([-22, -32, 0]) ab_prop_spike_ball();
translate([-16, -32, 0]) ab_prop_button();
translate([-11, -32, 0]) ab_prop_lever();
translate([-7, -32, 0]) ab_prop_bumper();
translate([-2, -32, 0]) ab_prop_chest(seed = 1);
translate([3, -32, 0]) ab_prop_pot(seed = 1);
translate([7, -32, 0]) ab_prop_capsule(seed = 1);
translate([11, -32, 0]) ab_prop_capsule(seed = 4, r = 0.35);
translate([15, -32, 0]) ab_prop_bench();
translate([20, -32, 0]) ab_prop_ladder(h = 3);
translate([24, -32, 0]) ab_prop_cone();
translate([28, -32, 0]) ab_prop_barrel(seed = 1);
translate([31, -32, 0]) ab_prop_barrel(seed = 3);

// ================= 收集物 =================
translate([-54, -42, 0]) ab_item_coin();
translate([-48, -42, 0]) ab_item_coin_row(n = 5);
translate([-38, -42, 0]) ab_item_coin_arc(n = 7, L = 6, h = 2.5);
translate([-28, -42, 0]) ab_item_coin_ring(n = 8, R = 2);
translate([-22, -42, 0]) ab_item_puzzle();
translate([-18, -42, 0]) ab_item_gem(seed = 1);
translate([-15, -42, 0]) ab_item_gem(seed = 2);
translate([-11, -42, 0]) ab_item_key();
translate([-6, -42, 0]) ab_item_star(s = 1.2);

// ================= 角色 =================
translate([-54, -52, 0]) ab_char_bot(seed = 0, pose = 0, hat = 0);
translate([-51, -52, 0]) ab_char_bot(seed = 1, pose = 1, hat = 2);
translate([-48, -52, 0]) ab_char_bot(seed = 2, pose = 2, hat = 3);
translate([-45, -52, 0]) ab_char_bot(seed = 3, pose = 3, hat = 4);
translate([-42, -52, 0]) ab_char_bot(seed = 4, pose = 0, hat = 5);
translate([-38, -52, 0]) ab_char_bot_lost(seed = 5, kind = 0);
translate([-34, -52, 0]) ab_char_bot_lost(seed = 6, kind = 1);
translate([-29, -52, 0]) ab_char_enemy_walker(seed = 0);
translate([-25, -52, 0]) ab_char_enemy_walker(seed = 3);
translate([-21, -52, 0]) ab_char_enemy_flyer(seed = 0, hover = 1.5);
translate([-16, -52, 0]) ab_char_enemy_spiky(seed = 0);

// ================= 相机机位 =================
gk_camera_lookat(eye = [40, -120, 75], target = [0, -6, 0], name = "overview", fov = 50);
gk_camera_lookat(eye = [-36, -66, 5], target = [-40, -50, 1], name = "characters", fov = 45);
gk_camera_lookat(eye = [-10, 8, 12], target = [-30, 30, 1], name = "mechanisms", fov = 50);
gk_camera_lookat(eye = [-20, 20, 14], target = [-20, 48, 0], name = "islands", fov = 50);
