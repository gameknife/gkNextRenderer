// dune_relay.scad —— AstroBot 风格第二关「落日沙洲」
// 真实世界比例：主角机器人 1.6 m，单跳 ≈ 2 m，悬浮跨越 ≈ 5 m，弹跳垫弹起 ≈ 6 m。
// 出生点 = ab_bldg_startpad 顶面中心 (-54, 0, 0.25)，朝向 +x（主角由 NextAstrobot 运行时放置）。
// 设计意图：第一关教平台，这一关教**非 ★ 活动件**——风扇顺风、激光节奏、喷泉托举、拉杆开门、
//   摆动刺球。所以机关密度更高、平台跨度更短，通关约 3~4 分钟。
// 关卡流程（自西向东，三座沙洲）：
//   洲 A 出生沙洲（z=0）  ：降落台 → 金币引导 → 木箱/宝箱 → 沙坑被困机器人 →
//                            拳击拉杆开栅栏门 → 风扇塔（顺风跨峡谷，两块悬浮圆盘）
//   洲 B 机关沙洲（z=3）  ：检查点 1 → 双激光走廊（数节奏冲过去）→ 宝石
//                            → 喷泉托举上高台拿拼图 → 跷跷板 → 弹跳垫上立柱牢笼救援
//                            → 摆动刺球门架 → 滑索速降到洲 C
//   洲 C 终点沙洲（z=1）  ：检查点 2 → 岩浆池滚筒 → 逆行传送带拼图支线 →
//                            终点门 + 金星 + 金币环 + 欢呼机器人
// gnb shot --scene assets/scad/source/astro/dune_relay.scad

use <../../lib/kit_astro.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

// ================= 洲 A：出生沙洲（z=0，28×22，x -60..-32，y -11..11） =================
translate([-46, 0, 0]) ab_ground_island(L = 28, D = 22, seed = 4, top = 1);
translate([-54, 0, 0]) ab_bldg_startpad(r = 3.0);
translate([-54, 6, 0]) ab_bldg_ship(seed = 3);
translate([-49, -3, 0]) ab_prop_sign_arrow(seed = 2, dir = 0);
translate([-49.5, 0, 0]) ab_item_coin_row(n = 5, dx = 1.2);
// 木箱堆 + 宝箱（拳击掀盖）
translate([-45, 6, 0]) ab_prop_crate();
translate([-43.9, 6.2, 0]) ab_prop_crate(seed = 3);
translate([-44.4, 6.1, 1.0]) ab_prop_crate(s = 0.8, seed = 5);
translate([-41, 7.5, 0]) ab_prop_chest(seed = 7);
// 积木台阶 → 蓝色高台（拼图）
translate([-47.5, 3, 0]) ab_plat_stairs(n = 4, w = 2.6, seed = 2);
translate([-43.6, 3, 0]) ab_plat_block(L = 3, D = 3, H = 2, c = ab_BLUE());
translate([-43.6, 3, 2]) ab_item_puzzle();
// 沙坑半埋机器人
translate([-50, -7, 0]) ab_ground_sand(L = 5, D = 4, seed = 3);
translate([-50, -7, 0.08]) ab_char_bot_lost(seed = 2, kind = 0);
// 拉杆 → 栅栏门：门封住东侧出口，拳击拉杆才放行
translate([-42, -3, 0]) ab_plat_block(L = 2, D = 2, H = 0.8, c = ab_ORANGE());
translate([-42, -3, 0.8]) ab_prop_lever(idx = 1);
translate([-36, 0, 0]) rotate([0, 0, 90]) ab_prop_gate_bars(w = 5, h = 3, idx = 1);
translate([-36, 4, 0]) rotate([0, 0, 90]) ab_prop_fence(len = 4);
translate([-36, -4, 0]) rotate([0, 0, 90]) ab_prop_fence(len = 4);
// 巡逻敌人
translate([-45, -4, 0]) rotate([0, 0, 90]) ab_char_enemy_walker(seed = 6);
// 装饰
translate([-58, -8, 0]) ab_nature_palm(s = 1.1, seed = 7);
translate([-58, 8, 0]) ab_nature_cactus(s = 1.0, seed = 3);
translate([-34, 8.5, 0]) ab_nature_palm(s = 0.9, seed = 8);
translate([-52, 9.5, 0]) ab_nature_rock(s = 1.0, seed = 4);
translate([-40, -9.5, 0]) ab_nature_rock(s = 0.8, seed = 9);
translate([-56, -4, 0]) ab_prop_lamp();
lay_scatter(n = 8, x0 = -59, x1 = -33, y0 = -10, y1 = 10, seed = 41)
    lay_pick($seed) { ab_nature_grass_tuft(seed = $seed); ab_nature_cactus(s = 0.5, seed = $seed); ab_nature_rock(s = 0.5, seed = $seed); }

// ================= 峡谷 1：风扇塔顺风 + 三块递升悬浮圆盘（顶面 0.9 / 1.8 / 2.7） =================
// 风扇装在洲 A 东缘的塔上，沿 +x 吹 20 m，整条峡谷都在风区里：每一跳 4.5~5.5 m，
// 顺风时轻松，逆着走回去则明显吃力——这就是这一关教风扇的地方。
translate([-33, 0, 0]) ab_plat_block(L = 3.6, D = 3.2, H = 1.0, c = ab_TEAL());
translate([-34.2, 0, 1.0]) rotate([0, 0, 90]) ab_prop_fan(s = 1.2, speed = 540, power = 6, range = 20);
translate([-28.5, 0.5, 0.4]) ab_plat_float(r = 2.0, c = ab_ORANGE());
translate([-24, -0.5, 1.3]) ab_plat_float(r = 2.0, c = ab_PURPLE());
translate([-19.5, 0.5, 2.2]) ab_plat_float(r = 2.0, c = ab_YELLOW());
translate([-30.8, 0.3, 1.0]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);
translate([-26.2, 0, 1.9]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);
translate([-21.8, 0, 2.8]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);
translate([-16.8, 0.4, 3.4]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);

// ================= 洲 B：机关沙洲（z=3，32×24，x -14..18，y -12..12） =================
translate([2, 0, 3]) ab_ground_island(L = 32, D = 24, seed = 5, top = 1);
translate([-11, 0, 3]) ab_prop_checkpoint(idx = 1);
translate([-9, -3, 3]) ab_prop_sign_board(seed = 4);
// 南线：双激光走廊 → 宝石（两盏相位错开，要数着节奏冲）
translate([-8, -7, 3]) ab_prop_laser(L = 6, period = 2.4, duty = 0.45, phase = 0);
translate([-1, -7, 3]) ab_prop_laser(L = 6, period = 2.4, duty = 0.45, phase = 0.5);
translate([-8, -10.5, 3]) rotate([0, 0, 90]) ab_prop_fence(len = 3);
translate([6.5, -7, 3]) ab_item_gem(seed = 6);
translate([4.5, -7, 3]) ab_item_coin_row(n = 3, dx = 0.9, hover = 0.5);
translate([9, -9.5, 3]) ab_char_enemy_spiky(seed = 4);
// 中线：喷泉托举 → 高台拼图
translate([-4, 1, 3]) ab_prop_fountain_jet(h = 6, r = 0.5, period = 3.0, lift = 7.5);
translate([-1, 3.4, 8.2]) ab_plat_float(r = 1.5, c = ab_TEAL());
translate([-1, 3.4, 8.7]) ab_item_puzzle();
translate([-4, 1, 3]) ab_item_coin_ring(n = 6, R = 1.6, hover = 2.2);
// 北线：跷跷板 → 弹跳垫 → 立柱顶牢笼救援
translate([4, 8, 3]) ab_plat_seesaw(L = 6, W = 2, amp = 12, speed = 25);
translate([10, 8, 3]) ab_plat_bounce(r = 1.1, launch = 6);
translate([14, 9, 3]) ab_plat_pillar(h = 4, r = 1.3, c = ab_PINK());
translate([14, 9, 7]) ab_prop_cage(seed = 4);
translate([10, 8, 3]) ab_item_coin_arc(n = 5, L = 3.6, h = 2.6, hover = 1.2);
// 东端：摆动刺球门架 → 滑索速降到洲 C
translate([14, 0, 3]) ab_bldg_gantry(w = 6, h = 6);
translate([14, 0, 4.2]) ab_prop_spike_ball_chain(h = 4.8, r = 0.6, ang = 40, period = 2.6);
translate([16.5, 3, 3]) ab_plat_pillar(h = 5, r = 1.4, c = ab_TEAL());
translate([16.5, 3, 8]) ab_plat_zipline(L = 20, drop = 7, t = 0.35, speed = 8);
translate([12, -3, 3]) ab_prop_sign_arrow(seed = 5, dir = 0);
translate([-6, 6, 3]) ab_char_enemy_flyer(seed = 5, hover = 2.4);
translate([-2, -2, 3]) ab_char_bot(seed = 9, pose = 1);
// 装饰
translate([-12, 9, 3]) ab_nature_palm(s = 1.0, seed = 10);
translate([-12, -9, 3]) ab_nature_cactus(s = 1.1, seed = 5);
translate([16, -9, 3]) ab_nature_rock(s = 1.1, seed = 11);
translate([2, 11, 3]) ab_prop_balloon(seed = 6, n = 3);
translate([8, -11, 3]) ab_prop_bench();
lay_scatter(n = 10, x0 = -13, x1 = 17, y0 = -11, y1 = 11, seed = 43)
    lay_pick($seed) { ab_nature_grass_tuft(seed = $seed); ab_nature_cactus(s = 0.5, seed = $seed); ab_nature_rock(s = 0.5, seed = $seed); }

// ================= 洲 C：终点沙洲（z=1，26×20，x 34..60，y -10..10） =================
translate([47, 0, 1]) ab_ground_island(L = 26, D = 20, seed = 6, top = 1);
translate([37, 3, 1]) ab_prop_checkpoint(idx = 2);
// 岩浆池 + 滚筒（踩着滚筒过去，脚下会被往南带）
translate([44, 0, 1]) ab_ground_lava(L = 6, D = 6, seed = 4);
translate([44, 0, 1]) ab_plat_roller(L = 6, r = 1.0, speed = 2.5);
translate([44, 0, 3.6]) ab_item_coin_row(n = 4, dx = 1.2, hover = 0.5);
// 北支线：逆行传送带 → 拼图
translate([42, 7, 1]) rotate([0, 0, 180]) ab_plat_conveyor(L = 6, W = 2, speed = 2);
translate([42, 7, 1.5]) ab_item_coin_row(n = 4, dx = 1.3, hover = 0.6);
translate([47, 7, 1.5]) ab_item_puzzle();
// 南支线：被困机器人 + 风车
translate([42, -8, 1]) ab_char_bot_lost(seed = 12, kind = 1);
translate([56, -7.5, 1]) ab_bldg_windmill(h = 6, seed = 5, speed = 24);
// 终点门（front 朝 -x）+ 金星 + 金币环 + 欢呼机器人 + 气球
translate([55, 0, 1]) rotate([0, 0, -90]) ab_bldg_goal(r = 2.6);
translate([55, 0, 1]) ab_item_star(hover = 2.7, s = 1.2);
translate([55, 0, 1]) ab_item_coin_ring(n = 10, R = 4.2, hover = 1.4);
translate([51, 4.5, 1]) rotate([0, 0, -40]) ab_char_bot(seed = 13, pose = 3);
translate([51, -4.5, 1]) rotate([0, 0, 40]) ab_char_bot(seed = 14, pose = 3);
translate([58, 6, 1]) ab_prop_balloon(seed = 7, n = 4);
// 装饰
translate([36, 8.5, 1]) ab_nature_palm(s = 1.0, seed = 12);
translate([36, -8.5, 1]) ab_nature_palm(s = 0.9, seed = 13);
translate([49, 9, 1]) ab_nature_cactus(s = 1.0, seed = 6);
translate([40, -4, 1]) ab_nature_rock(s = 1.0, seed = 14);
lay_scatter(n = 8, x0 = 35, x1 = 59, y0 = -9, y1 = 9, seed = 47)
    lay_pick($seed) { ab_nature_grass_tuft(seed = $seed); ab_nature_cactus(s = 0.5, seed = $seed); ab_nature_rock(s = 0.5, seed = $seed); }

// ================= 天空装饰 =================
for (p = [[-40, -26, -8, 2.2], [-10, 24, -11, 2.4], [24, -28, -9, 2.0], [52, 26, -7, 2.6],
          [-58, 20, 5, 1.6], [20, 14, 10, 1.2], [4, -20, 7, 1.4]])
    translate([p[0], p[1], p[2]]) ab_nature_cloud(s = p[3], seed = p[0] + p[1]);

// ================= 相机机位（第一个为默认入场机位） =================
gk_camera_lookat(eye = [6, -72, 42], target = [0, 0, 2], name = "overview", fov = 50);
gk_camera_lookat(eye = [-64, -14, 5], target = [-46, 0, 1.5], name = "start-island", fov = 55);
gk_camera_lookat(eye = [-14, -22, 12], target = [4, 0, 4], name = "mechanism-island", fov = 50);
gk_camera_lookat(eye = [36, -15, 8], target = [55, 0, 3], name = "goal-island", fov = 50);
// 路径相机：沿关卡流程自西向东飞越
gk_camera_lookat_key(eye = [-68, -20, 11], target = [-48, 0, 1], path = "level-flythrough", t = 0, fov = 52);
gk_camera_lookat_key(eye = [-30, -18, 9], target = [-22, 0, 2], path = "level-flythrough", t = 6, fov = 52);
gk_camera_lookat_key(eye = [0, -22, 12], target = [4, 0, 4], path = "level-flythrough", t = 12, fov = 52);
gk_camera_lookat_key(eye = [34, -18, 10], target = [46, 0, 3], path = "level-flythrough", t = 18, fov = 52);
gk_camera_lookat_key(eye = [48, -14, 7], target = [55, 0, 3], path = "level-flythrough", t = 23, fov = 52);
