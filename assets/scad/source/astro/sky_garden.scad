// sky_garden.scad —— AstroBot 风格 3D 平台跳跃解密 demo 关卡「星尘花园」
// 真实世界比例：主角机器人 1.6 m，单跳 ≈ 2 m，悬浮跨越 ≈ 5 m，弹跳垫弹起 ≈ 6 m。
// 关卡流程（自西向东，三座悬浮岛 + 两处秘境）：
//   岛 A 出生岛（z=0）  ：降落台 + 飞船 → 金币引导 → 积木台阶/高台拼图 → 沙坑/倒栽葱被困机器人 →
//                          弹跳垫秘密云朵金币环 → 三块递升悬浮圆盘跨越峡谷
//   岛 B 机关花园（z=3）：检查点 → 北线：水池易碎石板 → 旋转盘 → 弹跳垫 → 立柱顶牢笼救援
//                          南线：踩按钮开栅栏门 → 跷跷板 → 拳击积木墙露出金币/宝石
//                          东端：弹簧上立柱 → 滑索速降 / 轨道移动平台稳妥过桥
//   岛 C 终点岛（z=3）  ：摆动刺球门架 → 逆行传送带拼图支线 / 激光宝石支线 → 岩浆池滚筒 →
//                          糖果条纹终点门 + 欢呼机器人 + 气球
//   秘境：北面水晶小岛（弹跳垫可达）、南面沙岛（跳下悬浮圆盘可达）
// gnb shot --scene assets/scad/source/astro/sky_garden.scad

use <../../lib/kit_astro.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

// ================= 岛 A：出生岛（z=0，30×24，x -67..-37，y -12..12） =================
translate([-52, 0, 0]) ab_ground_island(L = 30, D = 24, seed = 1);
translate([-60, -1, 0]) ab_bldg_startpad(r = 3.2);
translate([-60, 7.5, 0]) ab_bldg_ship();
translate([-57, -1, 0.25]) ab_char_bot(seed = 0, pose = 0, hat = 0);          // 主角
translate([-53, -4, 0]) ab_prop_sign_arrow(seed = 0, dir = 0);
translate([-49.5, 0, 0]) ab_item_coin_row(n = 5, dx = 1.2);
// 积木台阶 → 蓝色高台（拼图）
translate([-44, 3, 0]) ab_plat_stairs(n = 4, w = 3);
translate([-39.5, 3, 0]) ab_plat_block(L = 4, D = 4, H = 2, c = ab_BLUE());
translate([-39.5, 3, 2]) ab_item_puzzle();
// 木箱堆 + 宝箱
translate([-48, 8, 0]) ab_prop_crate();
translate([-46.8, 8.2, 0]) ab_prop_crate(seed = 1);
translate([-47.4, 8.1, 1.0]) ab_prop_crate(s = 0.8, seed = 2);
translate([-44, 9.5, 0]) ab_prop_chest(seed = 1);
// 沙坑半埋机器人 / 倒栽葱机器人
translate([-58, -8, 0]) ab_ground_sand(L = 5, D = 4, seed = 1);
translate([-58, -8, 0.08]) ab_char_bot_lost(seed = 5, kind = 0);
translate([-46, -8, 0]) ab_char_bot_lost(seed = 6, kind = 1);
// 弹跳垫 → 上方云朵金币环（秘密）
translate([-40, -7, 0]) ab_plat_bounce();
translate([-40, -7, 6.5]) ab_nature_cloud(s = 1.6, seed = 3);
translate([-40, -7, 8.2]) ab_item_coin_ring(n = 8, R = 1.6, hover = 0.4);
// 巡逻敌人
translate([-47, -3, 0]) rotate([0, 0, 90]) ab_char_enemy_walker(seed = 0);
// 植被 / 小景
translate([-65, -9, 0]) ab_nature_tree_ball(s = 1.1, seed = 1);
translate([-65, 10, 0]) ab_nature_tree_cone(s = 1.0, seed = 2);
translate([-39, 10, 0]) ab_nature_tree_ball(s = 0.9, seed = 4);
translate([-52, 9.5, 0]) ab_nature_mushroom(s = 1.0, seed = 1);
translate([-54, 10.5, 0]) ab_nature_mushroom(s = 0.6, seed = 4);
translate([-63, 2, 0]) ab_nature_flower(s = 1.0, seed = 2);
translate([-64, -3, 0]) ab_nature_flower(s = 0.8, seed = 5);
translate([-42, -10, 0]) ab_nature_rock(s = 1.0, seed = 2);
translate([-55, -10.5, 0]) ab_prop_fence(len = 5);
translate([-49, -10.5, 0]) ab_prop_fence(len = 5);
translate([-63.5, 4.5, 0]) ab_prop_lamp();
lay_scatter(n = 10, x0 = -66, x1 = -38, y0 = -11, y1 = 11, seed = 11)
    lay_pick($seed) { ab_nature_grass_tuft(seed = $seed); ab_nature_flower(s = 0.5, seed = $seed); ab_nature_grass_tuft(seed = $seed + 3); }

// ================= 峡谷：三块递升悬浮圆盘（顶面 1.0 / 2.0 / 3.0） =================
translate([-33.5, 2, 0.5]) ab_plat_float(r = 1.6);
translate([-28.5, 0.5, 1.5]) ab_plat_float(r = 1.5, c = ab_ORANGE());
translate([-23, 2.5, 2.5]) ab_plat_float(r = 1.6, c = ab_PURPLE());
translate([-36, 2.5, 1.0]) ab_item_coin_arc(n = 5, L = 4, h = 1.2, hover = 0.6);
translate([-31, 1.2, 1.6]) ab_item_coin_arc(n = 5, L = 4, h = 1.2, hover = 0.6);
translate([-25.8, 1.5, 2.6]) ab_item_coin_arc(n = 5, L = 4, h = 1.2, hover = 0.6);
translate([-20.5, 2.5, 3.2]) ab_item_coin_arc(n = 5, L = 4, h = 1.2, hover = 0.6);

// ================= 岛 B：机关花园（z=3，36×26，x -18..18，y -13..13） =================
translate([0, 0, 3]) ab_ground_island(L = 36, D = 26, seed = 2);
translate([-15, 2, 3]) ab_prop_checkpoint();
translate([-14, -3, 3]) ab_prop_sign_board(seed = 2);
// 北线：水池 + 易碎石板 + 睡莲 → 旋转盘（金币环）→ 弹跳垫 → 立柱顶牢笼
translate([-6, 7, 3]) ab_ground_pool(L = 10, D = 6);
for (x = [-9, -7, -5, -3]) translate([x, 7, 3.45]) ab_plat_crumble(L = 1.6, D = 1.6, seed = x + 20);
translate([-9.5, 5, 3.62]) ab_nature_lilypad(r = 0.7, seed = 1);
translate([-4, 9, 3.62]) ab_nature_lilypad(r = 0.6, seed = 3);
translate([-6, 7, 3]) ab_char_enemy_flyer(seed = 0, hover = 2.6);
translate([4, 8, 3]) ab_plat_spin(r = 3);
translate([4, 8, 3]) ab_item_coin_ring(n = 8, R = 2.2, hover = 1.2);
translate([9.5, 8, 3]) ab_plat_bounce(r = 1.1);
translate([13, 9, 3]) ab_plat_pillar(h = 4, r = 1.3, c = ab_PINK());
translate([13, 9, 7]) ab_prop_cage(seed = 2);
// 南线：按钮 → 栅栏门（两侧围栏封路）→ 跷跷板 → 积木墙 → 金币/宝石/宝箱
translate([-9, -7, 3]) ab_prop_button(r = 0.9);
translate([-3, -7, 3]) rotate([0, 0, 90]) ab_prop_gate_bars(w = 4, h = 3);
translate([-3, -11, 3]) rotate([0, 0, 90]) ab_prop_fence(len = 4);
translate([-3, -4, 3]) rotate([0, 0, 90]) ab_prop_fence(len = 2);
translate([3, -7, 3]) ab_plat_seesaw(L = 6, W = 2);
translate([9, -7, 3]) ab_item_coin_arc(n = 5, L = 4, h = 1.5, hover = 0.8);
translate([12, -7, 3]) rotate([0, 0, -90]) ab_bldg_wall_break(L = 4, h = 2.5, seed = 1);
translate([14.5, -7, 3]) rotate([0, 0, 90]) ab_item_coin_row(n = 3, dx = 0.9, hover = 0.5);
translate([16, -10, 3]) ab_prop_chest(seed = 2);
translate([15, -4, 3]) ab_item_gem(seed = 1);
// 中央：玩具屋 + 门前小景 + 已救援的机器人
translate([2, 0, 3]) ab_bldg_house(seed = 1);
translate([0.5, -4, 3]) ab_prop_pot(seed = 1);
translate([3.5, -4, 3]) ab_prop_pot(seed = 3);
translate([-2, -2, 3]) ab_prop_bench();
translate([6.5, 0, 3]) ab_prop_lamp();
translate([7, -3, 3]) rotate([0, 0, -30]) ab_char_enemy_walker(seed = 3);
translate([-3, -0.5, 3]) ab_char_bot(seed = 7, pose = 1);
// 东端：弹簧 → 立柱 → 滑索速降；轨道移动平台桥接 B→C
translate([10, 4, 3]) ab_plat_spring(r = 0.8, h = 1.2);
translate([14, 4, 3]) ab_plat_pillar(h = 5, r = 1.5, c = ab_TEAL());
translate([14, 4, 8]) ab_plat_zipline(L = 19, drop = 5, t = 0.35);
translate([25, -1, 3]) ab_plat_moving(rail = 16, L = 3, W = 2, t = 0.3);
translate([25, -1, 3]) ab_item_coin_row(n = 7, dx = 2.0, hover = 1.2);
translate([16.5, -3, 3]) ab_prop_sign_arrow(seed = 1, dir = 0);
// 北秘境弹跳垫
translate([-8, 11.5, 3]) ab_plat_bounce(r = 1.0);
// 装饰
translate([-15, 10, 3]) ab_nature_tree_ball(s = 1.2, seed = 6);
translate([-16, -10, 3]) ab_nature_fruit(kind = 0, s = 1.0);
translate([16, 10.5, 3]) ab_nature_fruit(kind = 2, s = 0.9);
translate([8, 11.5, 3]) ab_nature_tree_cone(s = 0.9, seed = 8);
translate([-11, -11, 3]) ab_nature_mushroom(s = 0.8, seed = 2);
translate([-12, 11.5, 3]) ab_nature_palm(s = 0.9, seed = 3);
translate([-16.5, 6, 3]) ab_nature_rock(s = 1.1, seed = 5);
translate([3, 11.5, 3]) ab_prop_balloon(seed = 2);
lay_scatter(n = 12, x0 = -17, x1 = 17, y0 = -12, y1 = 12, seed = 23)
    lay_pick($seed) { ab_nature_grass_tuft(seed = $seed); ab_nature_flower(s = 0.5, seed = $seed); ab_nature_grass_tuft(seed = $seed + 5); }
translate([0, 0, 3]) lay_scatter(n = 5, x0 = -17, x1 = 17, y0 = -12, y1 = 12, seed = 24) ab_nature_grass_tuft(seed = $seed);

// ================= 岛 C：终点岛（z=3，30×22，x 32..62，y -11..11） =================
translate([47, 0, 3]) ab_ground_island(L = 30, D = 22, seed = 3);
translate([35, 7.5, 3]) ab_prop_checkpoint();
// 摆动刺球门架
translate([38, 0, 3]) ab_bldg_gantry(w = 6, h = 6);
translate([38, 0, 9]) rotate([0, 35, 0]) translate([0, 0, -4.8]) ab_prop_spike_ball_chain(h = 4.8, r = 0.6);
// 北支线：逆行传送带 → 拼图
translate([40, 7, 3]) rotate([0, 0, 180]) ab_plat_conveyor(L = 6, W = 2);
translate([40, 7, 3.5]) ab_item_coin_row(n = 4, dx = 1.3, hover = 0.6);
translate([45, 7, 3.5]) ab_item_puzzle();
// 南支线：激光 → 宝石（刺背敌人巡逻）
translate([36, -7, 3]) ab_prop_laser(L = 7);
translate([44.5, -7, 3]) ab_item_gem(seed = 3);
translate([39.5, -9.5, 3]) ab_char_enemy_spiky(seed = 1);
// 岩浆池 + 滚筒 + 上空金币
translate([46, 0, 3]) ab_ground_lava(L = 6, D = 6, seed = 2);
translate([46, 0, 3]) ab_plat_roller(L = 6, r = 1.0);
translate([46, 0, 5.6]) ab_item_coin_row(n = 4, dx = 1.2, hover = 0.5);
translate([46, 0, 3]) ab_char_enemy_flyer(seed = 2, hover = 4.0);
// 终点门（front 朝 -x）+ 金星 + 金币环 + 欢呼机器人 + 气球
translate([56, 0, 3]) rotate([0, 0, -90]) ab_bldg_goal(r = 2.6);
translate([56, 0, 3]) ab_item_star(hover = 2.7, s = 1.2);
translate([56, 0, 3]) ab_item_coin_ring(n = 12, R = 4.6, hover = 1.4);
translate([51, 5, 3]) rotate([0, 0, -40]) ab_char_bot(seed = 11, pose = 3);
translate([51, -5, 3]) rotate([0, 0, 40]) ab_char_bot(seed = 12, pose = 3);
translate([59, 7, 3]) ab_prop_balloon(seed = 3, n = 4);
translate([59, -7, 3]) ab_prop_balloon(seed = 5, n = 4);
// 装饰
translate([34, 9.5, 3]) ab_nature_palm(s = 1.0, seed = 4);
translate([34, -9.5, 3]) ab_nature_palm(s = 0.9, seed = 6);
translate([56, -8.5, 3]) ab_bldg_windmill(h = 6, seed = 2);
translate([59.5, 8, 3]) ab_nature_tree_ball(s = 1.0, seed = 12);
translate([47, 9, 3]) ab_nature_fruit(kind = 1, s = 1.0);
translate([50, -8, 3]) ab_nature_cactus(s = 0.9, seed = 2);
translate([43, -10, 3]) ab_nature_rock(s = 1.0, seed = 7);
translate([44, 10.2, 3]) ab_prop_fence(len = 6);
lay_scatter(n = 10, x0 = 33, x1 = 61, y0 = -10, y1 = 10, seed = 31)
    lay_pick($seed) { ab_nature_grass_tuft(seed = $seed); ab_nature_flower(s = 0.5, seed = $seed); ab_nature_grass_tuft(seed = $seed + 7); }

// ================= 秘境 =================
// 北：水晶小岛（岛 B 北缘弹跳垫 → 踏云 → 岛）
translate([-8, 23, 6]) ab_ground_island_round(r = 4.5, seed = 5);
translate([-8, 17, 4.2]) ab_nature_cloud(s = 1.2, seed = 5);
translate([-7, 24, 6]) ab_nature_crystal(s = 1.2, seed = 2);
translate([-9, 21, 6]) ab_item_puzzle();
translate([-10, 24.5, 6]) ab_item_coin_ring(n = 6, R = 1.4, hover = 1.0);
// 南：沙岛（岛 B 南缘跳下悬浮圆盘 → 沙岛）
translate([18, -14, 0.5]) ab_plat_float(r = 1.4, c = ab_YELLOW());
translate([24, -20, -1]) ab_ground_island_round(r = 5, seed = 6, top = 1);
translate([24, -20, -1]) ab_prop_chest(seed = 4);
translate([26.5, -18, -1]) ab_char_bot_lost(seed = 8, kind = 0);
translate([21.5, -22.5, -1]) ab_nature_palm(s = 0.8, seed = 9);

// ================= 天空装饰：云朵 / 泡泡 =================
for (p = [[-45, -30, -9, 2.2], [-20, 25, -12, 2.6], [10, -32, -10, 2.0], [35, 28, -8, 2.4],
          [60, -25, -14, 2.8], [-70, 20, -6, 1.8], [0, 4, -16, 3.0], [75, 10, -4, 2.0],
          [-30, -12, 9, 1.0], [30, 15, 12, 1.2], [-58, 22, 4, 1.4], [50, -18, 8, 1.1]])
    translate([p[0], p[1], p[2]]) ab_nature_cloud(s = p[3], seed = p[0] + p[1]);
translate([-25, 12, 6]) ab_prop_bubble(r = 0.8);
translate([28, -8, 9]) ab_prop_bubble(r = 1.0);

// ================= 相机机位（第一个为默认入场机位） =================
gk_camera_lookat(eye = [10, -78, 46], target = [0, 0, 2], name = "overview", fov = 50);
gk_camera_lookat(eye = [-70, -16, 5], target = [-48, 1, 1.5], name = "start-island", fov = 55);
gk_camera_lookat(eye = [-22, -24, 13], target = [2, 0, 4], name = "mechanism-garden", fov = 50);
gk_camera_lookat(eye = [38, -16, 9], target = [56, 0, 5], name = "goal-island", fov = 50);
gk_camera_lookat(eye = [-4, 8, 4.5], target = [13, 9, 8], name = "cage-rescue", fov = 45);
// 路径相机：沿关卡流程自西向东飞越
gk_camera_lookat_key(eye = [-74, -22, 12], target = [-52, 0, 1], path = "level-flythrough", t = 0, fov = 52);
gk_camera_lookat_key(eye = [-34, -20, 10], target = [-22, 2, 3], path = "level-flythrough", t = 7, fov = 52);
gk_camera_lookat_key(eye = [-2, -26, 13], target = [2, 0, 4], path = "level-flythrough", t = 14, fov = 52);
gk_camera_lookat_key(eye = [28, -22, 12], target = [40, 0, 4], path = "level-flythrough", t = 21, fov = 52);
gk_camera_lookat_key(eye = [50, -20, 10], target = [56, 0, 5], path = "level-flythrough", t = 28, fov = 52);
