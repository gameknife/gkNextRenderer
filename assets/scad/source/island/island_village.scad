// island/island_village.scad —— 动森风格度假岛村庄：广场+服务处 / 伙伴小屋 / 果树林 /
// 农田 / 西侧小溪木桥 / 南岸沙滩篝火 / 木码头与汽艇 / 东北灯塔。
// 1 unit = 1 m。层叠岛体每层抬高 2~4 cm 防共面；顶面高度：
//   海 0.10 / 浅滩 0.16 / 沙滩 0.20 / 草地 0.26 / 广场板 0.40。
// gnb shot --scene assets/scad/source/island/island_village.scad

use <../../lib/kit_island.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

// ================= 岛体层叠（blob 有机形交错出海湾与海角） =================
translate([0, -2, 0])    is_ground_water_blob(L = 150, D = 132, t = 0.10, c = is_SEADEEP(), seed = 1, fn = 12,
                                               roughness = is_WATER_ROUGH_DEEP());                         // 深海 → 顶 0.10
translate([0, 2, 0.04])
{
    is_ground_water_blob(L = 100, D = 78, t = 0.12, c = is_SEASHAL(), seed = 2, fn = 11,
                         roughness = is_WATER_ROUGH_SHALLOW());                                           // 浅滩 → 顶 0.16
    translate([-25, -21, 0]) is_ground_water_blob(L = 50, D = 42, t = 0.12, c = is_SEASHAL(), seed = 3, fn = 9,
                                                   roughness = is_WATER_ROUGH_SHALLOW());
    translate([27, -19, 0]) is_ground_water_blob(L = 42, D = 38, t = 0.12, c = is_SEASHAL(), seed = 4, fn = 9,
                                                  roughness = is_WATER_ROUGH_SHALLOW());
}
translate([0, 2, 0.08])
{
    is_ground_blob(L = 88, D = 68, t = 0.12, c = is_SANDC(), seed = 5, fn = 12);                              // 沙滩环 → 顶 0.20
    translate([-24, -20, 0]) is_ground_blob(L = 42, D = 34, t = 0.12, c = is_SANDC(), seed = 6, fn = 10);
    translate([26, -18, 0]) is_ground_blob(L = 34, D = 30, t = 0.12, c = is_SANDC(), seed = 7, fn = 9);
}
translate([0, 4, 0.12])
{
    is_ground_blob(L = 76, D = 58, t = 0.14, c = is_GRASSC(), seed = 8, fn = 12);                             // 岛草主体 → 顶 0.26
    translate([-22, -18, 0]) is_ground_blob(L = 34, D = 26, t = 0.14, c = is_GRASSC(), seed = 9, fn = 10);
    translate([24, -16, 0]) is_ground_blob(L = 26, D = 22, t = 0.14, c = is_GRASSC(), seed = 10, fn = 9);
}
// 浪花线（浅滩南缘）
translate([0, 0.14]) for (i = [0 : 7])
    color(is_SEAFOAM()) translate([-34 + i * 10, -36 - is_rnd(i * 7, 3) * 0.6, 0.16])
        rotate([0, 0, is_rnd(i * 13, 40) - 20]) is_slab(is_rndr(i * 11 + 3, 1.6, 3.4), 0.16, 0.012);

// ================= 北侧：广场与服务处（板顶 0.40） =================
translate([0, 20, 0.26]) is_ground_plaza(L = 20, D = 15);
translate([0, 20, 0.40])
{
    translate([0, 16, 0]) is_prop_fountain(s = 1.0);                       // 喷泉在广场南半
    translate([-6, 24, 0]) is_prop_flagpole(h = 7.5);                      // 旗杆
    translate([3.5, 12.5, 0]) rotate([0, 0, 180]) is_prop_bench();
    translate([-4, 12.5, 0]) rotate([0, 0, 180]) is_prop_bench();
    translate([8, 21.5, 0]) is_prop_mailbox();
    translate([-8.5, 14, 0]) is_prop_lamp();
    translate([8.5, 14, 0]) is_prop_lamp();
    translate([-8.5, 26.5, 0]) is_nature_flowerbed(seed = 3);
    translate([8.5, 26.5, 0]) is_nature_flowerbed(seed = 8);
    translate([-8.5, 18, 0]) is_nature_bush(s = 0.9, seed = 4);
    translate([8.5, 18, 0]) is_nature_bush(s = 0.9, seed = 6);
}
translate([0, 29.5, 0.26]) is_bldg_hall(seed = 1);                         // 服务处压广场北缘，门朝南

// ================= 主路与支路（叠草顶 0.262） =================
translate([0, -6, 0.262]) rotate([0, 0, 90]) is_ground_path(L = 36, W = 2.6, seed = 1);      // 主路：广场→南岸
translate([11, 5, 0.262]) rotate([0, 0, -8]) is_ground_path(L = 22, W = 2.2, seed = 2);      // 东支：主路→农田
translate([-7, -14, 0.262]) rotate([0, 0, 35]) is_ground_path(L = 10, W = 2.0, seed = 3);    // 西南支：主路→小屋C
for (y = [2, -12, -22])
    translate([1.8, y, 0.26]) is_prop_lamp();

// ================= 伙伴小屋区（主路两侧；门面向主路） =================
translate([-6.0001, -5.7551, 0.1057]) rotate([0.0000, -0.0000, 90.0000]) scale([1.00000, 1.00000, 1.00000]) is_bldg_house(seed = 0);     // A：门朝东
translate([14, -7, 0.26]) rotate([0, 0, -90]) is_bldg_house(seed = 4);     // B：门朝西
translate([-10.6056, -17.7541, 0.2600]) rotate([0.0000, -0.0000, 100.0000]) scale([1.00000, 1.00000, 1.00000]) is_bldg_house(seed = 9);   // C：门朝东北
translate([17, -21, 0.26]) rotate([0, 0, -75]) is_bldg_house(seed = 13);   // D：门朝西南
// 院子与生活道具
translate([-16, 2.5, 0.26]) rotate([0, 0, 0]) is_prop_fence(len = 7, seed = 1);
translate([-20, -6, 0.26]) is_nature_flowerbed(seed = 12);
translate([-11.5, 2, 0.26]) is_nature_bush(s = 1.0, seed = 5);
translate([11.5, -1.5, 0.26]) is_prop_fence(len = 6, seed = 2);
translate([18.5, -3, 0.26]) is_nature_flowerbed(seed = 15);
translate([18.5, -12, 0.26]) is_prop_crate(seed = 3);
translate([-19, -13, 0.26]) is_prop_mailbox();
translate([-6.5, -18, 0.26]) is_nature_bush(s = 1.1, seed = 7);
translate([-17.5, -22.5, 0.26]) is_prop_sign(seed = 4);
translate([24.5, -14, 0.26]) is_nature_tree(s = 1.1, seed = 11);

// ================= 西北：果树林（网格 + 抖动 + 混果树） =================
translate([-24.5, 12, 0.26]) lay_grid(cols = 4, rows = 3, cw = 5, ch = 5, seed = 41)
    lay_jitter($seed, 1.2, 1.2, 20)
        lay_pick($seed) { is_nature_apple(s = 1.05, seed = $seed); is_nature_orange(s = 1.0, seed = $seed); is_nature_peach(s = 1.1, seed = $seed); }
translate([-30, 6, 0.26]) is_item_fruit(kind = 0, seed = 1);
translate([-21, 17, 0.26]) is_item_fruit(kind = 1, seed = 2);
translate([-28.5, 20, 0.26]) is_prop_crate(seed = 5);
translate([-16.5, 8, 0.26]) is_prop_crate(seed = 8);
lay_scatter(n = 6, x0 = -36, x1 = -18, y0 = 2, y1 = 22, seed = 42)
    translate([0, 0, 0.26]) lay_pick($seed) { is_nature_bush(s = 0.9, seed = $seed); is_nature_flowerbed(seed = $seed); is_nature_bush(s = 0.7, seed = $seed + 1); }

// ================= 东部：农田 =================
translate([28, 14, 0.265]) is_ground_field(L = 12, D = 8, seed = 21, crop = 1);   // 萝卜田
translate([28.0000, 1.7479, 0.2650]) rotate([0.0000, -0.0000, 0.0000]) scale([1.00000, 1.00000, 1.00000]) is_ground_field(L = 10, D = 7, seed = 22, crop = 2);    // 白菜田
translate([31.5, 19.5, 0.265]) is_prop_scarecrow(seed = 1);
translate([22.5, 9.5, 0.265]) is_prop_wateringcan();
translate([34, 9.5, 0.265]) is_prop_crate(seed = 6);
translate([33.5, 1.5, 0.26]) is_nature_bush(s = 0.9, seed = 9);

// ================= 西侧：小溪 + 木桥（溪面叠草顶 0.265） =================
translate([-33.5, 17, 0.265]) rotate([0, 0, atan2(-18, -3)]) is_ground_stream(L = 19.5, W = 3.2, seed = 31);
translate([-33.5, -1, 0.265]) rotate([0, 0, atan2(-18, 3)]) is_ground_stream(L = 19.5, W = 3.2, seed = 32);
translate([-29, -17, 0.265]) rotate([0, 0, atan2(-14, 6)]) is_ground_stream(L = 17, W = 3.0, seed = 33);
// 溪口入海（浅滩小湾 + 礁石）
translate([-25, -27, 0.05]) is_ground_water_blob(L = 12, D = 10, t = 0.10, c = is_SEASHAL(), seed = 34, fn = 8,
                                                  roughness = is_WATER_ROUGH_SHALLOW());
translate([-24, -24, 0.20]) is_nature_rock(s = 1.0, seed = 3);
translate([-27.5, -27, 0.16]) is_nature_rock(s = 0.8, seed = 5);
// 木桥跨溪（垂直溪流方向；桥锚点=引桥端路面高）
translate([-33.5, -1, 0.26]) rotate([0, 0, atan2(-18, 3) + 90]) is_bldg_bridge(L = 8, W = 2.6);
translate([-30, -4.5, 0.26]) is_prop_bollard();
translate([-37, 2.5, 0.26]) is_prop_bollard();

// ================= 东北：灯塔 =================
translate([36, 26, 0.26]) is_bldg_lighthouse(s = 1.0, seed = 2);
translate([33, 23, 0.26]) is_nature_rock(s = 1.2, seed = 7);
translate([39, 22.5, 0.20]) is_nature_rock(s = 0.9, seed = 8);
translate([32.5, 29.5, 0.26]) is_nature_bush(s = 1.0, seed = 10);

// ================= 南岸：沙滩活动区（沙环顶 0.20） =================
translate([-8, -33, 0.20]) is_ground_sand(L = 15, D = 10, seed = 41);    // 活动区沙地纹理
translate([-4.5, -36.5, 0.20]) is_prop_umbrella();
translate([1.5, -37.5, 0.20]) is_prop_umbrella();
translate([-6.5, -34.5, 0.20]) rotate([0, 0, 190]) is_prop_lounger();
translate([0.5, -35, 0.20]) rotate([0, 0, 175]) is_prop_lounger();
translate([4, -33.5, 0.20]) rotate([0, 0, 160]) is_prop_lounger();
translate([-1, -32, 0.20]) is_prop_beachkit();
translate([-13, -28.5, 0.20]) is_prop_firepit();                          // 篝火晚会
translate([-9.5, -30.5, 0.20]) is_prop_torch();
translate([-16.5, -27, 0.20]) is_prop_torch();
translate([-13.5, -25.5, 0.20]) is_item_fruit(kind = 3, seed = 4);         // 椰子堆
// 沙滩贝壳散布
lay_scatter(n = 9, x0 = -20, x1 = 8, y0 = -38, y1 = -30, seed = 43)
    translate([0, 0, 0.20]) lay_pick($seed) { is_item_shell(seed = $seed); is_nature_rock(s = 0.5, seed = $seed); is_item_shell(seed = $seed + 3); }
// 椰子树（沙滩环西南/东南）
translate([-34, -27, 0.20]) is_nature_coconut(s = 1.1, seed = 1);
translate([-38, -21, 0.20]) is_nature_coconut(s = 0.95, seed = 2);
translate([31, -27, 0.20]) is_nature_coconut(s = 1.15, seed = 3);
translate([35, -22, 0.20]) is_nature_coconut(s = 0.9, seed = 4);
translate([38, -32, 0.16]) is_nature_coconut(s = 1.0, seed = 5);

// ================= 南岸东段：木码头与汽艇（甲板锚点 z=0.36） =================
translate([10, -30, 0.36]) rotate([0, 0, -90]) is_bldg_dock(L = 14, W = 3.2);
translate([8, -28.5, 0.20]) is_prop_bollard();
translate([12, -28.5, 0.20]) is_prop_bollard();
translate([10, -44.5, 0.21]) rotate([0, 0, 14]) is_veh_boat(seed = 0);    // 系在码头末端旁
translate([6.5, -43, 0.21]) rotate([0, 0, -22]) is_veh_boat(seed = 1);
translate([13.2, -30.5, 0.36]) is_prop_crate(seed = 7);                   // 卸货果箱
translate([16.5, -29, 0.20]) is_prop_sign(seed = 5);                      // 码头指引牌

// ================= 散布植被（草地空隙） =================
lay_scatter(n = 8, x0 = -14, x1 = 8, y0 = 24, y1 = 32, seed = 44)         // 广场南侧开阔带
    translate([0, 0, 0.26]) lay_pick($seed) { is_nature_bush(s = 1.0, seed = $seed); is_nature_flowerbed(seed = $seed); is_nature_flowerbed(seed = $seed + 2); }
lay_scatter(n = 7, x0 = 18, x1 = 36, y0 = 20, y1 = 32, seed = 45)         // 东北林地
    translate([0, 0, 0.26]) lay_pick($seed) { is_nature_tree(s = 1.0, seed = $seed); is_nature_bush(s = 1.1, seed = $seed); is_nature_tree(s = 0.85, seed = $seed + 1); }
lay_scatter(n = 6, x0 = 30, x1 = 40, y0 = -14, y1 = 6, seed = 46)         // 东南边缘
    translate([0, 0, 0.26]) lay_pick($seed) { is_nature_tree(s = 0.9, seed = $seed); is_nature_bush(s = 0.9, seed = $seed + 3); }
lay_scatter(n = 5, x0 = -38, x1 = -28, y0 = -12, y1 = 2, seed = 47)       // 溪西林带
    translate([0, 0, 0.26]) lay_pick($seed) { is_nature_tree(s = 1.1, seed = $seed); is_nature_bush(s = 1.0, seed = $seed + 1); }
lay_scatter(n = 6, x0 = -40, x1 = -30, y0 = 24, y1 = 32, seed = 48)       // 西北角林带
    translate([0, 0, 0.26]) lay_pick($seed) { is_nature_tree(s = 1.0, seed = $seed); is_nature_tree(s = 0.8, seed = $seed + 4); is_nature_bush(s = 1.0, seed = $seed); }
lay_scatter(n = 5, x0 = -8, x1 = 14, y0 = -30, y1 = -24, seed = 49)       // 南岸草沙带
    translate([0, 0, 0.24]) lay_pick($seed) { is_nature_bush(s = 0.8, seed = $seed); is_nature_flowerbed(seed = $seed + 1); }

// ================= 相机机位（第一个为默认入场） =================
gk_camera_lookat(eye = [38, -52, 26], target = [0, 6, 0], name = "overview", fov = 48);
gk_camera_lookat(eye = [10, 7, 4], target = [0, 22, 2], name = "plaza", fov = 55);
gk_camera_lookat(eye = [-14, 25, 7], target = [-29, 11, 2], name = "orchard", fov = 50);
gk_camera_lookat(eye = [13, -1, 6], target = [30, 12, 1], name = "farm", fov = 50);
gk_camera_lookat(eye = [-3, -45, 6], target = [10, -31, 1], name = "beach-dock", fov = 55);
gk_camera_lookat(eye = [-25, -9, 4.5], target = [-34, -1, 1.2], name = "bridge", fov = 55);

// 路径相机：绕岛巡航（20s ping-pong 由引擎回放）
gk_camera_lookat_key(eye = [55, -20, 18], target = [10, 5, 0], path = "island-cruise", t = 0, fov = 50);
gk_camera_lookat_key(eye = [20, 55, 22], target = [0, 8, 0], path = "island-cruise", t = 5, fov = 50);
gk_camera_lookat_key(eye = [-45, 40, 20], target = [-24, 8, 0], path = "island-cruise", t = 10, fov = 50);
gk_camera_lookat_key(eye = [-45, -35, 16], target = [-10, -20, 0], path = "island-cruise", t = 15, fov = 50);
gk_camera_lookat_key(eye = [55, -20, 18], target = [10, 5, 0], path = "island-cruise", t = 20, fov = 50);
