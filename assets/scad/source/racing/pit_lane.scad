// racing/pit_lane.scad —— 赛车场维修区：六车队 P 房排 + 维修通道 + 赛道段 + 看台 + 控制塔 + 围场后勤区
// 1 unit = 1 m。gnb shot --scene assets/scad/source/racing/pit_lane.scad
use <../../lib/kit_pitlane.scad>
use <../../lib/kit_layout.scad>

$fn = 12;

// ================= 基底（草地） =================
color([0.36, 0.41, 0.24]) translate([0, -2, -0.15]) cube([128, 96, 0.3], center = true);

// ================= 赛道（北侧，沿 x；含发车格） =================
translate([-32, 20, 0]) rp_ground_track(L = 64, W = 14, seed = 1);
translate([32, 20, 0]) rp_ground_track(L = 64, W = 14, seed = 2, start = true);
// 北侧砂石缓冲区 + 护栏 + 胎墙 + 捕捉网
translate([0, 30.6, 0]) rp_ground_gravel(L = 96, D = 7, seed = 1);
translate([-44, 24, 0]) rp_ground_grass(L = 16, D = 22, seed = 3);
translate([46, 24, 0]) rp_ground_grass(L = 20, D = 22, seed = 4);
for (x = [-42 : 6 : 42])
    translate([x, 27.4, 0.12]) rp_prop_guardrail(len = 6);
for (x = [-30 : 4 : -10])
    translate([x, 28.5, 0.12]) rp_prop_tire_wall(len = 4, seed = x);
for (x = [14 : 4 : 34])
    translate([x, 28.5, 0.12]) rp_prop_tire_wall(len = 4, seed = x + 1);
for (x = [-51 : 6 : 51])
    translate([x, 34.2, 0]) rp_prop_fence_catch(len = 6, seed = x);

// 起跑灯架（跨越赛道，灯组朝 -x 来车方向）
translate([31, 20, 0.14]) rp_prop_gantry(L = 16, seed = 1);

// ================= 看台（最北侧，面向 -y 赛道） =================
translate([0, 40, 0]) rp_bldg_grandstand(L = 34, rows = 8, seed = 2);

// ================= 维修墙 + 数据屏 =================
for (x = [-24 : 8 : 24])
    translate([x, 11.8, 0]) rp_prop_pitwall(len = 8, seed = x);
translate([-16, 10.2, 0]) rp_prop_monitor(seed = 0);
translate([0, 10.2, 0]) rp_prop_monitor(seed = 2);
translate([16, 10.2, 0]) rp_prop_monitor(seed = 4);

// ================= 维修通道（车库侧 -y 带 pit box） =================
translate([0, 5, 0]) rp_ground_pitlane(L = 64, W = 12, seed = 0);
// 锥桶引导线
for (x = [-30 : 6 : 30])
    translate([x, 9.8, 0.14]) rp_prop_cone(seed = x);

// ================= P 房排（六开间，front 旋转朝 +y 维修通道） =================
// 1 号 P 房：车停在门前 pit box，做进站练习（胎堆/工具车/气瓶围车）
translate([-20, -6, 0]) rotate([0, 0, 180]) rp_bldg_garage(seed = 0, car = -1);
translate([-20.5, 1.0, 0.14]) rp_veh_gt3(seed = 0);
translate([-23.2, 0.2, 0.14]) rp_prop_tire_stack(seed = 1, n = 4);
translate([-17.6, -0.2, 0.14]) rp_prop_tire_stack(seed = 2, n = 3);
translate([-22.8, 2.6, 0.14]) rp_prop_toolcart(seed = 1);
translate([-17.2, 2.4, 0.14]) rp_prop_bottle(seed = 1);
translate([-19.6, 3.4, 0.14]) rp_prop_cone(seed = 1);
// 2 号 P 房：赛车入库
translate([-12, -6, 0]) rotate([0, 0, 180]) rp_bldg_garage(seed = 2, car = 3);
// 3 号 P 房：举升机上架车检修（叙事焦点：卸胎检修）
translate([-4, -6, 0]) rotate([0, 0, 180]) rp_bldg_garage(seed = 4, car = -1);
translate([-4, -8.6, 0.14]) rotate([0, 0, 90]) rp_prop_lift(seed = 0);
translate([-4, -8.6, 0.82]) rotate([0, 0, -90]) rp_veh_gt3(seed = 4);
translate([-6.2, -6.4, 0.14]) rp_part_tire(0.33, 0.3);
translate([-1.8, -6.8, 0.14]) rp_prop_toolcart(seed = 5);
// 4 号 P 房：赛车入库
translate([4, -6, 0]) rotate([0, 0, 180]) rp_bldg_garage(seed = 5, car = 6);
// 5 号 P 房：车队未到，卷帘门关闭
translate([12, -6, 0]) rotate([0, 0, 180]) rp_bldg_garage(seed = 6, car = -1, closed = true);
// 6 号 P 房：赛车入库
translate([20, -6, 0]) rotate([0, 0, 180]) rp_bldg_garage(seed = 7, car = 2);
// 门前备用胎堆/装备
translate([-24.6, -0.4, 0.14]) rp_prop_tire_stack(seed = 3);
translate([16.4, -0.2, 0.14]) rp_prop_tire_stack(seed = 4, n = 4);
translate([24.6, 0.4, 0.14]) rp_prop_generator(seed = 0);

// ================= 控制塔（东侧，起跑线旁） =================
translate([44, 6, 0]) rp_bldg_control_tower(seed = 1, h = 13);
// 记分塔（西侧，pit 入口旁）
translate([-42, 10, 0]) rp_prop_pylon(seed = 0);

// ================= 围场（P 房后方混凝土坪） =================
translate([-13, -24, 0]) rp_ground_paddock(L = 34, D = 20, seed = 1);
translate([21, -24, 0]) rp_ground_paddock(L = 34, D = 20, seed = 2);
translate([-41, -20, 0]) rp_ground_paddock(L = 12, D = 12, seed = 3);
// 运输卡车排
translate([-16, -28.5, 0.14]) rp_veh_hauler(seed = 0);
translate([-2, -28.5, 0.14]) rp_veh_hauler(seed = 3);
translate([12, -28.5, 0.14]) rp_veh_hauler(seed = 5);
translate([26, -29, 0.14]) rp_veh_van(seed = 2);
translate([32, -29, 0.14]) rp_veh_van(seed = 6);
// 车队帐篷区（后勤补给）
for (i = [0 : 3])
{
    translate([-18 + i * 6.5, -16, 0.14]) rp_prop_canopy(seed = i);
    translate([-18 + i * 6.5, -16, 0.14]) rp_prop_tire_stack(seed = i + 7, n = 3);
}
translate([-20, -19.5, 0.14]) rp_prop_generator(seed = 2);
translate([-11, -19.5, 0.14]) rp_prop_fuel_rig(seed = 0);
translate([-4, -19.5, 0.14]) rp_prop_toolcart(seed = 3);
// 招待所 ×2（东侧朝西、西侧朝北）
translate([36, -16, 0.14]) rotate([0, 0, 90]) rp_bldg_hospitality(seed = 1);
translate([-36, -20, 0.14]) rotate([0, 0, 180]) rp_bldg_hospitality(seed = 6);
// 领奖台（东南角，背板朝南）
translate([42, -26, 0.14]) rotate([0, 0, 180]) rp_prop_podium(seed = 0);
// 安全车待命位（pit 通道东端）
translate([34, 7.5, 0.14]) rp_veh_safety_car(seed = 0);
// 牵引小车
translate([26, -12.5, 0.14]) rotate([0, 0, 30]) rp_veh_cart(seed = 0);

// ================= 灯光 / 围网 / 标识 =================
for (p = [[-52, 30], [52, 30], [-52, -30], [52, -30]])
    translate([p[0], p[1], 0]) rp_prop_floodlight(seed = 0);
// 围场南侧围网 + 广告布
for (x = [-42 : 6 : 42])
    translate([x, -38, 0]) rp_prop_fence_catch(len = 6, h = 2.6, seed = x);
for (x = [-39 : 6 : 39])
    translate([x, -37.8, 0.3]) rp_prop_banner(len = 5.6, seed = x);
// 绿篱 + 旗帜线
for (x = [-33 : 6 : 33])
    translate([x, -36.2, 0]) rp_nature_hedge(L = 4.5, seed = x);
for (i = [0 : 5])
    translate([-25 + i * 10, -41.5, 0]) rp_prop_flag(seed = i, h = 7);

// ================= 植被与边角 =================
lay_scatter(n = 10, x0 = -60, x1 = -46, y0 = -30, y1 = 11, seed = 61)
    lay_pick($seed) { rp_nature_tree(s = 1.3, seed = $seed); rp_nature_tree(s = 1.0, seed = $seed + 1); rp_nature_bush(s = 1.3, seed = $seed); }
lay_scatter(n = 10, x0 = 48, x1 = 60, y0 = -20, y1 = 11, seed = 62)
    lay_pick($seed) { rp_nature_tree(s = 1.2, seed = $seed); rp_nature_tree(s = 0.9, seed = $seed + 2); rp_nature_bush(s = 1.2, seed = $seed); }
lay_scatter(n = 8, x0 = -56, x1 = 56, y0 = -34, y1 = -14, seed = 63)
    lay_pick($seed) { rp_nature_bush(s = 1.1, seed = $seed); rp_nature_bush(s = 0.9, seed = $seed + 1); }
lay_scatter(n = 6, x0 = -50, x1 = 50, y0 = 36, y1 = 44, seed = 64)
    lay_pick($seed) { rp_nature_tree(s = 1.1, seed = $seed); rp_nature_bush(s = 1.2, seed = $seed); }
