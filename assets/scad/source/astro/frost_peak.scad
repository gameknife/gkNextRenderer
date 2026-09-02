// frost_peak.scad —— AstroBot 风格第三关「极光冰峰」
// 真实世界比例：主角机器人 1.6 m，单跳 ≈ 2 m，悬浮跨越 ≈ 5 m，弹跳垫弹起 ≈ 6 m。
// 出生点 = ab_bldg_startpad 顶面中心 (-54, 0, 0.25)，朝向 +x（主角由 NextAstrobot 运行时放置）。
// 设计意图：第三关为大结局关（Finale），采用全新的冰川雪山与极光水晶主题（Snow & Crystal Peak）。
//   地形为覆雪浮岛（top = 2），搭配冰蓝与淡紫琉璃高光晶簇、苍翠雪松、冰川深岩与高空云海。
//   玩法整合全部核心机关：顺风峡谷助跳、冰泉托举、双激光走廊、旋转冰盘、摇摆刺球门架、
//   高空跷跷板登顶牢笼救援、超长高空滑索俯冲、冰川滚筒与逆向传送带。
// 关卡流程（自西向东，三座雪峰浮岛）：
//   峰 A 冰川基地（z=0）  ：降落台 → 金币引导 → 积木台阶拼图 1 → 冰坑被困机器人 1 →
//                            拉杆开栅栏门 → 顺风风扇助跳跨越悬浮冰盘
//   峰 B 霜晶险峰（z=4）  ：检查点 1 → 冰泉托举高空拼图 2 → 双激光走廊与尖刺怪 →
//                            旋转盘金币环 → 摇摆刺球门架 → 跷跷板 + 弹跳垫登顶牢笼救援 2 →
//                            高空超长滑索俯冲飞降峰 C
//   峰 C 极光圣殿（z=2）  ：检查点 2 → 冰渊滚筒 → 逆向传送带拼图 3 → 冰窟被困机器人 3 + 宝箱 →
//                            终点神殿门 + 金星 + 胜利金币环 + 欢呼机器人小队
// gnb shot --scene assets/scad/source/astro/frost_peak.scad

use <../../lib/kit_astro.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

// ================= 峰 A：冰川基地（z=0，28×22，x -60..-32，y -11..11，雪地 top=2） =================
translate([-46, 0, 0]) ab_ground_island(L = 28, D = 22, seed = 12, top = 2);
translate([-54, 0, 0]) ab_bldg_startpad(r = 3.0);
translate([-54, 6, 0]) ab_bldg_ship(seed = 5);
translate([-49, -3, 0]) ab_prop_sign_arrow(seed = 1, dir = 0);
translate([-49.5, 0, 0]) ab_item_coin_row(n = 5, dx = 1.2);

// 木箱堆 + 宝箱（拳击掀盖）
translate([-45, 6, 0]) ab_prop_crate();
translate([-43.9, 6.2, 0]) ab_prop_crate(seed = 2);
translate([-44.4, 6.1, 1.0]) ab_prop_crate(s = 0.8, seed = 4);
translate([-41, 7.5, 0]) ab_prop_chest(seed = 8);

// 积木台阶 → 冰晶蓝高台（拼图 1）
translate([-47.5, 3, 0]) ab_plat_stairs(n = 4, w = 2.6, seed = 3);
translate([-43.6, 3, 0]) ab_plat_block(L = 3, D = 3, H = 2, c = ab_BLUEL());
translate([-43.6, 3, 2]) ab_item_puzzle();

// 冰坑半埋被困机器人 1
translate([-50, -7, 0]) ab_ground_pool(L = 5, D = 4, depth = 0.4);
translate([-50, -7, 0.08]) ab_char_bot_lost(seed = 15, kind = 0);

// 拉杆 → 栅栏门：门封住东侧出口，拳击拉杆方可通过
translate([-42, -3, 0]) ab_plat_block(L = 2, D = 2, H = 0.8, c = ab_TEAL());
translate([-42, -3, 0.8]) ab_prop_lever(idx = 1);
translate([-36, 0, 0]) rotate([0, 0, 90]) ab_prop_gate_bars(w = 5, h = 3, idx = 1);
translate([-36, 4, 0]) rotate([0, 0, 90]) ab_prop_fence(len = 4, c = ab_WHITE());
translate([-36, -4, 0]) rotate([0, 0, 90]) ab_prop_fence(len = 4, c = ab_WHITE());

// 巡逻敌人
translate([-45, -4, 0]) rotate([0, 0, 90]) ab_char_enemy_walker(seed = 11);

// 自然与晶簇装饰
translate([-58, -8, 0]) ab_nature_tree_cone(s = 1.2, seed = 8);
translate([-58, 8, 0]) ab_nature_tree_cone(s = 1.0, seed = 9);
translate([-34, 8.5, 0]) ab_nature_crystal(s = 1.2, seed = 1);
translate([-52, 9.5, 0]) ab_nature_crystal(s = 1.0, seed = 2);
translate([-40, -9.5, 0]) ab_nature_rock(s = 1.1, seed = 15);
translate([-56, -4, 0]) ab_prop_lamp();
lay_scatter(n = 8, x0 = -59, x1 = -33, y0 = -10, y1 = 10, seed = 61)
    lay_pick($seed) { ab_nature_crystal(s = 0.6, seed = $seed); ab_nature_tree_cone(s = 0.6, seed = $seed); ab_nature_rock(s = 0.6, seed = $seed); }

// ================= 峡谷 1：风扇塔顺风 + 三块递升悬浮冰盘（顶面 1.0 / 2.2 / 3.4） =================
translate([-33, 0, 0]) ab_plat_block(L = 3.6, D = 3.2, H = 1.0, c = ab_WHITE());
translate([-34.2, 0, 1.0]) rotate([0, 0, 90]) ab_prop_fan(s = 1.2, speed = 540, power = 6, range = 22);
translate([-28.5, 0.5, 0.5]) ab_plat_float(r = 2.0, c = ab_BLUEL());
translate([-24, -0.5, 1.6]) ab_plat_float(r = 2.0, c = ab_WHITE());
translate([-19.5, 0.5, 2.7]) ab_plat_float(r = 2.0, c = ab_TEAL());
translate([-30.8, 0.3, 1.1]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);
translate([-26.2, 0, 2.1]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);
translate([-21.8, 0, 3.1]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);
translate([-16.8, 0.4, 3.8]) ab_item_coin_arc(n = 5, L = 3.4, h = 1.0, hover = 0.5);

// ================= 峰 B：霜晶险峰（z=4，32×24，x -14..18，y -12..12，雪地 top=2） =================
translate([2, 0, 4]) ab_ground_island(L = 32, D = 24, seed = 21, top = 2);
translate([-11, 0, 4]) ab_prop_checkpoint(idx = 1);
translate([-9, -3, 4]) ab_prop_sign_board(seed = 7);

// 北线高空：冰泉托举 → 高空浮台拼图 2
translate([-4, 6, 4]) ab_prop_fountain_jet(h = 6, r = 0.5, period = 3.0, lift = 7.5);
translate([-1, 8.4, 9.5]) ab_plat_float(r = 1.6, c = ab_WHITE());
translate([-1, 8.4, 10.0]) ab_item_puzzle();
translate([-4, 6, 4]) ab_item_coin_ring(n = 6, R = 1.6, hover = 2.2);

// 南线：双激光走廊 → 宝石（冰晶长廊）
translate([-8, -7, 4]) ab_prop_laser(L = 6, period = 2.4, duty = 0.45, phase = 0);
translate([-1, -7, 4]) ab_prop_laser(L = 6, period = 2.4, duty = 0.45, phase = 0.5);
translate([-8, -10.5, 4]) rotate([0, 0, 90]) ab_prop_fence(len = 3, c = ab_WHITE());
translate([6.5, -7, 4]) ab_item_gem(seed = 9);
translate([4.5, -7, 4]) ab_item_coin_row(n = 3, dx = 0.9, hover = 0.5);
translate([9, -9.5, 4]) ab_char_enemy_spiky(seed = 7);

// 中央线：旋转冰盘（金币环） + 摇摆刺球门架
translate([4, 0, 4]) ab_plat_spin(r = 3, speed = 30);
translate([4, 0, 4]) ab_item_coin_ring(n = 8, R = 2.2, hover = 1.2);
translate([10, 0, 4]) ab_bldg_gantry(w = 6, h = 6);
translate([10, 0, 5.2]) ab_prop_spike_ball_chain(h = 4.8, r = 0.6, ang = 40, period = 2.6);
translate([-6, 0, 4]) ab_char_enemy_flyer(seed = 9, hover = 2.6);

// 东北高台：跷跷板 → 弹跳垫 → 冰柱牢笼救援 2
translate([5, 8, 4]) ab_plat_seesaw(L = 6, W = 2, amp = 12, speed = 25);
translate([11, 8, 4]) ab_plat_bounce(r = 1.1, launch = 6);
translate([15, 9, 4]) ab_plat_pillar(h = 4, r = 1.3, c = ab_BLUEL());
translate([15, 9, 8]) ab_prop_cage(seed = 8);
translate([11, 8, 4]) ab_item_coin_arc(n = 5, L = 3.6, h = 2.6, hover = 1.2);

// 东南滑索站：滑索速降到峰 C（长 20m，落差 7m，z: 9 -> 2）
translate([16.5, -3, 4]) ab_plat_pillar(h = 5, r = 1.4, c = ab_WHITE());
translate([16.5, -3, 9]) ab_plat_zipline(L = 20, drop = 7, t = 0.35, speed = 8);
translate([12, -3, 4]) ab_prop_sign_arrow(seed = 6, dir = 0);
translate([-2, -2, 4]) ab_char_bot(seed = 17, pose = 1);

// 装饰
translate([-12, 9, 4]) ab_nature_tree_cone(s = 1.2, seed = 14);
translate([-12, -9, 4]) ab_nature_crystal(s = 1.3, seed = 12);
translate([16, -9, 4]) ab_nature_crystal(s = 1.1, seed = 13);
translate([2, 11, 4]) ab_prop_balloon(seed = 9, n = 3);
translate([8, -11, 4]) ab_prop_bench();
lay_scatter(n = 10, x0 = -13, x1 = 17, y0 = -11, y1 = 11, seed = 73)
    lay_pick($seed) { ab_nature_crystal(s = 0.7, seed = $seed); ab_nature_tree_cone(s = 0.6, seed = $seed); ab_nature_rock(s = 0.6, seed = $seed); }

// ================= 峰 C：极光圣殿（z=2，26×20，x 34..60，y -10..10，雪地 top=2） =================
translate([47, 0, 2]) ab_ground_island(L = 26, D = 20, seed = 33, top = 2);
translate([37, 3, 2]) ab_prop_checkpoint(idx = 2);

// 冰渊 + 冰滚筒（踩着滚筒通过寒潭）
translate([44, 0, 2]) ab_ground_pool(L = 6, D = 6, depth = 0.5);
translate([44, 0, 2]) ab_plat_roller(L = 6, r = 1.0, speed = 2.5);
translate([44, 0, 4.6]) ab_item_coin_row(n = 4, dx = 1.2, hover = 0.5);

// 北支线：逆向传送带 → 拼图 3
translate([42, 7, 2]) rotate([0, 0, 180]) ab_plat_conveyor(L = 6, W = 2, speed = 2);
translate([42, 7, 2.5]) ab_item_coin_row(n = 4, dx = 1.3, hover = 0.6);
translate([47, 7, 2.5]) ab_item_puzzle();

// 南支线：冰窟被困机器人 3 + 冰原风车
translate([42, -8, 2]) ab_char_bot_lost(seed = 18, kind = 1);
translate([56, -7.5, 2]) ab_bldg_windmill(h = 6, seed = 12, speed = 24);

// 终点神殿门（front 朝 -x）+ 金星 + 胜利金币环 + 欢呼机器人 + 气球
translate([55, 0, 2]) rotate([0, 0, -90]) ab_bldg_goal(r = 2.6);
translate([55, 0, 2]) ab_item_star(hover = 2.7, s = 1.2);
translate([55, 0, 2]) ab_item_coin_ring(n = 10, R = 4.2, hover = 1.4);
translate([51, 4.5, 2]) rotate([0, 0, -40]) ab_char_bot(seed = 22, pose = 3);
translate([51, -4.5, 2]) rotate([0, 0, 40]) ab_char_bot(seed = 23, pose = 3);
translate([58, 6, 2]) ab_prop_balloon(seed = 15, n = 4);

// 装饰
translate([36, 8.5, 2]) ab_nature_tree_cone(s = 1.1, seed = 16);
translate([36, -8.5, 2]) ab_nature_tree_cone(s = 1.0, seed = 17);
translate([49, 9, 2]) ab_nature_crystal(s = 1.2, seed = 18);
translate([40, -4, 2]) ab_nature_rock(s = 1.0, seed = 19);
lay_scatter(n = 8, x0 = 35, x1 = 59, y0 = -9, y1 = 9, seed = 81)
    lay_pick($seed) { ab_nature_crystal(s = 0.6, seed = $seed); ab_nature_tree_cone(s = 0.5, seed = $seed); ab_nature_rock(s = 0.5, seed = $seed); }

// ================= 天空装饰 =================
for (p = [[-40, -26, -8, 2.2], [-10, 24, -11, 2.4], [24, -28, -9, 2.0], [52, 26, -7, 2.6],
          [-58, 20, 5, 1.6], [20, 14, 10, 1.2], [4, -20, 7, 1.4]])
    translate([p[0], p[1], p[2]]) ab_nature_cloud(s = p[3], seed = p[0] + p[1]);

// ================= 相机机位（第一个为默认入场机位） =================
gk_camera_lookat(eye = [6, -72, 42], target = [0, 0, 3], name = "overview", fov = 50);
gk_camera_lookat(eye = [-64, -14, 5], target = [-46, 0, 1.5], name = "start-island", fov = 55);
gk_camera_lookat(eye = [-14, -22, 13], target = [4, 0, 5], name = "crystal-summit", fov = 50);
gk_camera_lookat(eye = [36, -15, 8], target = [55, 0, 3], name = "goal-sanctuary", fov = 50);

// 路径相机：沿关卡流程自西向东飞越雪山浮岛与晶峰
gk_camera_lookat_key(eye = [-68, -20, 11], target = [-48, 0, 1], path = "level-flythrough", t = 0, fov = 52);
gk_camera_lookat_key(eye = [-30, -18, 10], target = [-22, 0, 2], path = "level-flythrough", t = 6, fov = 52);
gk_camera_lookat_key(eye = [0, -22, 13], target = [4, 0, 5], path = "level-flythrough", t = 12, fov = 52);
gk_camera_lookat_key(eye = [34, -18, 11], target = [46, 0, 3], path = "level-flythrough", t = 18, fov = 52);
gk_camera_lookat_key(eye = [48, -14, 8], target = [55, 0, 3], path = "level-flythrough", t = 23, fov = 52);
