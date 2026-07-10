// habor_city_v2.scad —— 高细节低多边形海港城市
//
// 设计目标：
//   * 保留 habor_city.scad 的 672 x 508 大型城市尺度；
//   * 复用 habor_city_hd.scad 已完成的人尺度细节零件库；
//   * 以参考图的密度组织街区：功能建筑、停车、铺装、屋顶设备、街具和绿化共同构成细节；
//   * 每个街区均有明确功能，核心区以 3~7 层中低层建筑为主，不依赖重复摩天楼填充画面；
//   * 仅使用 gkNextEngine SCADLoader 支持的语法和几何特性。
//
// OpenSCAD Z-up。+y 北（山丘与风电林带），-y 南（海滩、游艇港和货运港）。
// 城市建成区仍为 12 列 x 6 行，完整范围 x∈[-336,336], y∈[-164,344]。
//
// habor_city_hd.scad 只有模块库、没有总装。use 导入其 module/function，
// 本文件在调用侧重申模块所需全局常量，确保引擎动态作用域与 OpenSCAD 均能稳定求值。

use <habor_city_hd.scad>

$fn = 12;

// ================= 标高与配色（HD 模块调用环境） =================
GZT  = 0.15;
WZT  = 0.30;
SEAZ = -0.50;
RW   = 8;

ROADC   = [0.27, 0.28, 0.31];
MARKC   = [0.94, 0.94, 0.92];
WALKC   = [0.83, 0.83, 0.80];
WALKD   = [0.74, 0.74, 0.71];
CURBC   = [0.69, 0.69, 0.66];
LOTC    = [0.44, 0.45, 0.47];
APRONC  = [0.65, 0.65, 0.62];
PLAZAC  = [0.86, 0.84, 0.79];
GRASSC  = [0.55, 0.78, 0.35];
GRASSD  = [0.47, 0.70, 0.29];
SANDC   = [0.93, 0.84, 0.58];
SANDW   = [0.87, 0.77, 0.54];
SEAC    = [0.25, 0.60, 0.77];
SEAD    = [0.21, 0.54, 0.72];
FOAMC   = [0.95, 0.97, 0.97];
BASEC   = [0.24, 0.21, 0.28];
WHITEC  = [0.94, 0.93, 0.90];
CREAMC  = [0.93, 0.88, 0.73];
GREYC   = [0.62, 0.64, 0.66];
DGREYC  = [0.36, 0.38, 0.41];
DARKC   = [0.15, 0.16, 0.18];
METALC  = [0.72, 0.74, 0.77];
GLASSL  = [0.55, 0.74, 0.90];
GLASSB  = [0.33, 0.56, 0.82];
GLASSD  = [0.20, 0.36, 0.60];
GLASSA  = [0.55, 0.72, 0.85, 0.35];
REDC    = [0.85, 0.26, 0.20];
ORANGEC = [0.92, 0.57, 0.22];
YELLOWC = [0.95, 0.78, 0.20];
BLUEC   = [0.27, 0.47, 0.75];
POLBLUE = [0.30, 0.55, 0.82];
TEALC   = [0.30, 0.62, 0.58];
BROWND  = [0.30, 0.22, 0.16];
TRUNKC  = [0.45, 0.31, 0.18];
LEAFC   = [0.49, 0.76, 0.27];
LEAFD   = [0.38, 0.64, 0.22];
PALMC   = [0.30, 0.62, 0.30];
OAKC    = [0.76, 0.56, 0.34];
COURTR  = [0.72, 0.30, 0.26];
COURTB  = [0.30, 0.45, 0.65];
YELLINE = [0.85, 0.75, 0.25];

// ================= 总平面常量 =================
V2_BW = 48;
V2_BD = 42;
V2_VX = [-280, -224, -168, -112, -56, 0, 56, 112, 168, 224, 280];
V2_HY = [-30, 20, 70, 120, 170, 220, 270];
V2_CX = [-308, -252, -196, -140, -84, -28, 28, 84, 140, 196, 252, 308];
V2_RY = [-5, 45, 95, 145, 195, 245];

// 街区类型矩阵：南→北，每行西→东。
// 0住宅 1公寓 2商街 3办公 4公园 5市场 6酒店 7医院
// 8警消 9学校球场 10加油餐饮 11物流 12滨海休闲 13市政 14交通广场 15风电林地
V2_LAYOUT = [
    [12, 6, 2, 5, 2, 14, 10, 6, 2, 11, 11, 12],
    [ 0, 1, 7, 1, 3, 13, 14, 3,  8,  1,  2,  0],
    [ 0, 1, 9, 1, 3,  3,  3, 13,  4,  1,  1,  0],
    [ 0, 0, 4, 1, 1,  4,  1,  1,  9,  4,  0,  0],
    [15, 0, 0, 1, 4,  0,  0,  1,  4,  0,  0, 15],
    [15, 0, 0, 1, 0,  0,  0,  1,  0,  0, 15, 15]
];

function v2_near(v, values) = min([for (q = values) abs(v - q)]);

// ================= 新增建筑（front = -y，底面 z=0） =================

// 中层玻璃办公楼：退台、竖向玻璃格、入口雨棚和密集屋顶设备。
module v2_bldg_office(L = 17, D = 12, F = 6, seed = 0)
{
    H = F * 3.0 + 1.0;
    color(DGREYC) slab(L + 0.4, D + 0.4, 0.55);
    color(WHITEC) translate([0, 0, 0.55]) slab(L, D, H - 0.55);

    for (f = [0 : F - 1])
    {
        zc = 2.15 + f * 3.0;
        color(GLASSB) for (sy = [-1, 1])
            translate([0, sy * (D / 2 + 0.055), zc]) boxc([L - 1.5, 0.11, 1.65]);
        color(WHITEC) for (sy = [-1, 1], i = [-3 : 3])
            translate([i * (L - 2) / 7, sy * (D / 2 + 0.09), zc]) boxc([0.24, 0.12, 1.78]);
        color(GLASSD) for (sx = [-1, 1])
            translate([sx * (L / 2 + 0.055), 0, zc]) boxc([0.11, D - 2.0, 1.65]);
    }

    color(GLASSD) translate([0, -D / 2 - 0.10, 1.45]) boxc([4.5, 0.14, 2.6]);
    translate([0, -D / 2, 0]) part_door_glass(2.4, 2.55);
    translate([0, -D / 2, 0]) part_entry_canopy(4.8, 2.3, BLUEC);
    color(BLUEC) translate([0, 0, H - 0.30]) boxc([L + 0.24, D + 0.24, 0.60]);
    translate([0, 0, H]) part_parapet(L, D, WHITEC, 0.52, 0.30);
    translate([-4.8, 2.2, H]) part_roof_ac(3);
    translate([4.5, -2.0, H]) part_roof_misc(seed);
}

// 消防站：三车库、观察塔、红色檐带与屋顶通信设备。
module v2_bldg_fire_station(L = 19, D = 12)
{
    H = 6.4;
    color(CREAMC) slab(L, D, H);
    color(REDC) translate([0, -D / 2 - 0.08, H - 0.75]) boxc([L + 0.25, 0.16, 1.25]);
    translate([0, -D / 2 - 0.18, H - 0.72]) part_sign_text("FIRE STATION", 0.62, WHITEC);
    for (i = [-1 : 1]) translate([i * 4.7, -D / 2, 0]) part_door_roller(3.6, 3.7);
    color(WHITEC) for (i = [-1 : 1])
        translate([i * 4.7, -D / 2 - 0.09, 1.8]) boxc([2.9, 0.06, 0.30]);
    color(REDC) translate([L / 2 - 2.3, 2.2, H]) slab(3.6, 3.6, 5.2);
    color(WHITEC) for (sy = [-1, 1])
        translate([L / 2 - 2.3, 2.2 + sy * 1.83, H + 3.0]) boxc([2.2, 0.08, 1.3]);
    color(GLASSL) for (sy = [-1, 1])
        translate([L / 2 - 2.3, 2.2 + sy * 1.88, H + 3.0]) boxc([1.8, 0.05, 1.0]);
    color(DGREYC) translate([L / 2 - 2.3, 2.2, H + 5.2]) cylinder(h = 3.2, r = 0.06, $fn = 6);
    color(REDC) translate([L / 2 - 2.3, 2.2, H + 8.4]) sphere(r = 0.14, $fn = 6);
    translate([-6.4, 2.6, H]) part_roof_ac(2);
}

// 学校：双翼教学楼、中央入口钟、明亮遮阳檐。
module v2_bldg_school(L = 27, D = 13)
{
    H = 7.2;
    color(CREAMC) slab(L, D, H);
    color(ORANGEC) translate([0, 0, 0.45]) boxc([L + 0.15, D + 0.15, 0.90]);
    for (f = [0 : 1])
    {
        zc = 2.3 + f * 3.0;
        translate([0, -D / 2 - 0.06, zc]) part_win_row(L - 3.0, 8, 1.55, 1.45);
        color(ORANGEC) translate([0, -D / 2 - 0.22, zc + 1.0]) boxc([L - 2.0, 0.65, 0.10]);
    }
    translate([0, -D / 2, 0]) part_door_glass(2.8, 2.55);
    translate([0, -D / 2, 0]) part_entry_canopy(5.2, 2.5, ORANGEC);
    color(WHITEC) translate([0, -D / 2 - 0.12, H - 0.60]) boxc([9.0, 0.18, 1.0]);
    translate([0, -D / 2 - 0.24, H - 0.57]) part_sign_text("HARBOR SCHOOL", 0.55, DGREYC);
    color(WHITEC) translate([0, 0, H]) slab(4.2, 4.2, 2.0);
    color(DGREYC) translate([0, -2.15, H + 1.0]) rotate([90, 0, 0]) cylinder(h = 0.08, r = 0.72, $fn = 12);
    color(WHITEC) translate([0, -2.20, H + 1.0]) rotate([90, 0, 0]) cylinder(h = 0.04, r = 0.55, $fn = 12);
    translate([-8.0, 2.0, H]) part_roof_ac(3);
    translate([8.0, 2.0, H]) part_roof_misc(4);
}

// 市政厅 / 图书馆：对称台阶、柱廊、钟楼和旗杆。
module v2_bldg_civic(L = 24, D = 14)
{
    H = 8.0;
    color(WHITEC) slab(L, D, H);
    color(GREYC) translate([0, 0, 0.35]) boxc([L + 0.25, D + 0.25, 0.70]);
    translate([0, -D / 2 - 2.0, 0])
    {
        color(WALKC) for (i = [0 : 3]) translate([0, i * 0.45, i * 0.15]) boxc([12 - i * 1.0, 1.1, 0.30]);
        color(WHITEC) for (x = [-4.2, -1.4, 1.4, 4.2])
            translate([x, 1.5, 0.55]) cylinder(h = 5.2, r = 0.30, $fn = 8);
        color(CREAMC) translate([0, 1.5, 5.75]) boxc([11.0, 2.6, 0.55]);
        color(CREAMC) translate([0, 0.2, 6.0]) rotate([90, 0, 90])
            linear_extrude(11.0, center = true) polygon([[-1.4, 0], [1.4, 0], [0, 1.5]]);
    }
    translate([0, -D / 2, 0.6]) part_door_glass(2.6, 2.7);
    for (f = [0 : 1], i = [-2 : 2])
        if (i != 0) translate([i * 4.0, -D / 2 - 0.06, 2.2 + f * 3.0]) part_window(1.6, 1.6);
    color(CREAMC) translate([0, 0, H]) slab(5.0, 5.0, 3.4);
    color(DGREYC) translate([0, -2.55, H + 1.7]) rotate([90, 0, 0]) cylinder(h = 0.08, r = 0.95, $fn = 12);
    color(WHITEC) translate([0, -2.61, H + 1.7]) rotate([90, 0, 0]) cylinder(h = 0.04, r = 0.74, $fn = 12);
    color(REDC) translate([0, 0, H + 3.4]) cylinder(h = 2.0, r1 = 1.8, r2 = 0.10, $fn = 8);
}

// 港务大楼：蓝白立面、控制塔与观景平台。
module v2_bldg_harbor_office(L = 18, D = 11)
{
    H = 7.0;
    color(WHITEC) slab(L, D, H);
    color(TEALC) translate([0, 0, 0.45]) boxc([L + 0.18, D + 0.18, 0.90]);
    for (f = [0 : 1])
    {
        color(GLASSB) translate([0, -D / 2 - 0.06, 2.2 + f * 2.8]) boxc([L - 2.0, 0.12, 1.45]);
        color(WHITEC) for (i = [-3 : 3]) translate([i * 2.0, -D / 2 - 0.10, 2.2 + f * 2.8]) boxc([0.20, 0.12, 1.55]);
    }
    translate([0, -D / 2, 0]) part_door_glass(2.2, 2.5);
    color(TEALC) translate([0, -D / 2 - 0.14, H - 0.65]) boxc([10.5, 0.22, 1.1]);
    translate([0, -D / 2 - 0.28, H - 0.62]) part_sign_text("HARBOR OFFICE", 0.54, WHITEC);
    color(DGREYC) translate([4.8, 1.4, H]) slab(5.2, 5.2, 2.8);
    color(GLASSL) for (sy = [-1, 1]) translate([4.8, 1.4 + sy * 2.62, H + 1.45]) boxc([4.3, 0.08, 1.7]);
    color(WHITEC) translate([4.8, 1.4, H + 2.8]) cylinder(h = 0.9, r = 2.3, $fn = 8);
    color(DGREYC) translate([4.8, 1.4, H + 3.7]) cylinder(h = 2.0, r = 0.06, $fn = 6);
}

// ================= 街区公共构件 =================

module v2_block_pad(seams = true)
{
    ground_block_pad(V2_BW, V2_BD, seams);
    // 四角圆形树池/防撞柱让每个台面轮廓更精细。
    for (sx = [-1, 1], sy = [-1, 1])
    {
        color(WALKD) translate([sx * 21.4, sy * 17.9, WZT]) cylinder(h = 0.07, r = 1.15, $fn = 10);
        color(GRASSC) translate([sx * 21.4, sy * 17.9, WZT + 0.07]) cylinder(h = 0.04, r = 0.92, $fn = 10);
    }
}

module v2_block_edge_props(seed = 0, trees = 2)
{
    for (i = [0 : trees - 1])
        translate([-18 + i * (36 / max(1, trees - 1)), 17.5, WZT + 0.11])
            nature_tree_street(0.82 + 0.08 * rnd(seed + i, 3), seed + i);
    translate([-21.0, -17.2, WZT]) prop_hydrant();
    translate([21.0, -17.0, WZT]) prop_bin((rnd(seed, 2) == 0) ? TEALC : DGREYC);
}

// ================= 功能街区库（48 x 42） =================

module v2_block_houses(seed = 0)
{
    v2_block_pad();
    color(GRASSC) translate([0, 0, WZT]) slab(43.0, 36.0, 0.08);
    color(WALKC) translate([0, 0, WZT + 0.08]) slab(3.0, 36.0, 0.04);
    for (i = [0 : 5])
    {
        col = i % 3;
        row = floor(i / 3);
        xx = -15 + col * 15;
        yy = (row == 0) ? -10.0 : 10.0;
        translate([xx, yy, WZT + 0.12]) rotate([0, 0, row == 0 ? 0 : 180]) bldg_house(seed * 7 + i);
        color(WALKC) translate([xx + 5.3, row == 0 ? -15.1 : 15.1, WZT + 0.08]) slab(2.5, 7.0, 0.04);
        if (rnd(seed * 11 + i, 3) == 0)
            translate([xx + 5.3, row == 0 ? -15.2 : 15.2, WZT + 0.12])
                rotate([0, 0, row == 0 ? 90 : -90]) veh_car(car_c(seed * 13 + i));
    }
    for (x = [-21, 21]) translate([x, 0, WZT + 0.12]) nature_hedge(12);
    v2_block_edge_props(seed, 3);
}

module v2_block_apartments(seed = 0)
{
    v2_block_pad();
    color(PLAZAC) translate([0, 0, WZT]) slab(43.0, 36.0, 0.05);
    for (i = [0 : 3])
    {
        sx = (i % 2 == 0) ? -1 : 1;
        sy = (i < 2) ? -1 : 1;
        translate([sx * 13.0, sy * 10.8, WZT + 0.05])
            rotate([0, 0, sy < 0 ? 0 : 180])
                bldg_apartment(12.5, 10.0, 3 + rnd(seed * 5 + i, 3), house_c(seed + i), seed + i);
    }
    color(GRASSC) translate([0, 0, WZT + 0.05]) slab(11.5, 9.0, 0.08);
    color(WALKC) translate([0, 0, WZT + 0.13]) slab(2.0, 9.0, 0.04);
    translate([-3.4, 1.7, WZT + 0.13]) nature_tree(0.82, seed);
    translate([3.2, -1.8, WZT + 0.13]) nature_tree(0.72, seed + 1);
    for (sy = [-1, 1]) translate([0, sy * 4.8, WZT + 0.13]) prop_bench();
    v2_block_edge_props(seed + 7, 2);
}

module v2_block_shops(seed = 0)
{
    v2_block_pad();
    color(PLAZAC) translate([0, 1.5, WZT]) slab(43, 7.0, 0.05);
    translate([-14.5, 11.0, WZT]) rotate([0, 0, 180]) bldg_barber(9, 8, 4.4);
    translate([-4.8, 11.0, WZT]) rotate([0, 0, 180]) bldg_shop_unit("BAKERY", ORANGEC, 9, 8, 4.2);
    translate([4.8, 11.0, WZT]) rotate([0, 0, 180]) bldg_shop_unit("BOOKS", TEALC, 9, 8, 4.2);
    translate([14.5, 11.0, WZT]) rotate([0, 0, 180]) bldg_shop_unit("GIFTS", BLUEC, 9, 8, 4.2);
    translate([-10.5, -10.5, WZT]) bldg_cafe(10, 8, 4.2);
    translate([3.0, -10.5, WZT]) bldg_burger(11, 9, 4.6);
    for (i = [0 : 2]) translate([13.0 + (i % 2) * 3.0, -13 + floor(i / 2) * 4.0, WZT]) furn_cafe_table(umb_c(seed + i));
    translate([0, 1.0, WZT + 0.05]) prop_fountain_spiral();
    for (x = [-18, 18]) translate([x, 1.0, WZT + 0.05]) prop_bench();
    v2_block_edge_props(seed, 2);
}

module v2_block_offices(seed = 0)
{
    v2_block_pad();
    color(PLAZAC) translate([0, 0, WZT]) slab(43, 36, 0.05);
    translate([-11.5, 6.0, WZT + 0.05]) rotate([0, 0, 180])
        v2_bldg_office(18, 12, 5 + rnd(seed, 3), seed);
    translate([11.5, 7.5, WZT + 0.05]) rotate([0, 0, 180])
        v2_bldg_office(16, 11, 4 + rnd(seed + 3, 3), seed + 1);
    translate([-9.5, -11.0, WZT + 0.05]) road_parking_row(4, 2.8, 5.4);
    translate([-13.5, -11.0, WZT + 0.11]) rotate([0, 0, 90]) veh_car(car_c(seed));
    translate([-5.2, -11.0, WZT + 0.11]) rotate([0, 0, 90]) veh_taxi();
    color(GRASSC) translate([13.5, -10.5, WZT + 0.05]) slab(12, 8.5, 0.08);
    for (i = [0 : 2]) translate([9.5 + i * 4.0, -10.5, WZT + 0.13]) nature_tree(0.82, seed + i);
    translate([13.5, -15.2, WZT + 0.05]) prop_board(BLUEC);
    v2_block_edge_props(seed + 2, 2);
}

module v2_block_park(seed = 0)
{
    v2_block_pad();
    color(GRASSC) translate([0, 0, WZT]) slab(43, 36, 0.10);
    color(WALKC) translate([0, 0, WZT + 0.10]) slab(3.0, 36, 0.04);
    color(WALKC) translate([0, 0, WZT + 0.10]) slab(43, 3.0, 0.04);
    translate([0, 0, WZT + 0.14]) prop_fountain_spiral();
    for (i = [0 : 13])
    {
        xx = -19 + rnd(seed * 5 + i, 38);
        yy = -15 + rnd(seed * 9 + i, 30);
        if (abs(xx) > 3.2 && abs(yy) > 3.2)
            translate([xx, yy, WZT + 0.10]) nature_tree(0.75 + 0.12 * rnd(seed + i, 4), seed + i);
    }
    for (x = [-8, 8], y = [-4, 4]) translate([x, y, WZT + 0.14]) rotate([0, 0, y > 0 ? 180 : 0]) prop_bench();
    translate([16, -13, WZT + 0.14]) prop_mailbox(REDC);
    translate([-16, 13, WZT + 0.14]) prop_board(TEALC);
}

module v2_block_market(seed = 0)
{
    v2_block_pad();
    translate([0, 9.0, WZT]) rotate([0, 0, 180]) bldg_market(25, 14, 6.2);
    translate([-5.5, -10.3, WZT]) road_parking_row(6, 2.8, 5.4);
    for (i = [0 : 3])
        translate([-12.5 + i * 5.4, -10.3, WZT + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed + i));
    translate([14.2, -10.0, WZT + 0.05]) prop_bus_shelter();
    translate([18.5, 11.0, WZT]) nature_tree_street(0.9, seed);
    translate([-19.0, 10.0, WZT]) nature_tree_street(0.85, seed + 1);
    v2_block_edge_props(seed, 2);
}

module v2_block_hotel(seed = 0)
{
    v2_block_pad();
    color(GRASSC) translate([0, 8, WZT]) slab(43, 18, 0.08);
    translate([0, 8.0, WZT + 0.08]) rotate([0, 0, 180]) bldg_hotel(18, 12, 4 + rnd(seed, 2));
    translate([-12.5, -9.0, WZT]) road_parking_row(4, 2.8, 5.4);
    translate([-16.5, -9.0, WZT + 0.06]) rotate([0, 0, 90]) veh_taxi();
    translate([-8.2, -9.0, WZT + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed));
    translate([11.5, -10.0, WZT]) bldg_cafe(10, 8, 4.2);
    for (i = [0 : 2]) translate([17.0, -15 + i * 4.0, WZT]) furn_cafe_table(umb_c(seed + i));
    for (x = [-19, 19]) translate([x, 11.5, WZT + 0.08]) nature_palm(0.9, x < 0 ? 6 : -6);
    v2_block_edge_props(seed + 2, 2);
}

module v2_block_hospital(seed = 0)
{
    v2_block_pad();
    color(GRASSC) translate([-7.5, 8.0, WZT]) slab(30, 17, 0.08);
    translate([-6.5, 8.0, WZT + 0.08]) rotate([0, 0, 180]) bldg_hospital(26, 15);
    translate([-14.0, 8.0, WZT + 11.10]) part_helipad(3.5, BLUEC);
    translate([-14.0, 8.0, WZT + 11.24]) rotate([0, 0, 18]) veh_helicopter(REDC, false);
    translate([14.5, 8.5, WZT]) road_parking_row(3, 2.8, 5.4);
    translate([14.5, 8.5, WZT + 0.06]) rotate([0, 0, -90]) veh_ambulance();
    color(LOTC) translate([0, -11.0, WZT]) slab(34, 8.0, 0.05);
    translate([-9.0, -11.0, WZT + 0.05]) rotate([0, 0, 90]) veh_ambulance();
    translate([2.0, -11.0, WZT + 0.05]) rotate([0, 0, 90]) veh_car(WHITEC);
    translate([12.0, -11.0, WZT + 0.05]) rotate([0, 0, 90]) veh_car(BLUEC);
    translate([20.0, -13.0, WZT]) prop_board(REDC);
    v2_block_edge_props(seed, 2);
}

module v2_block_police_fire(seed = 0)
{
    v2_block_pad();
    translate([-11.5, 9.0, WZT]) rotate([0, 0, 180]) bldg_police(13, 10);
    translate([-11.5, 9.0, WZT + 7.2]) part_helipad(3.0, POLBLUE);
    translate([10.0, 8.0, WZT]) rotate([0, 0, 180]) v2_bldg_fire_station(19, 12);
    color(LOTC) translate([0, -11.5, WZT]) slab(40, 7.5, 0.05);
    translate([-15.0, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_police_car();
    translate([-5.0, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_police_car();
    translate([7.0, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_ambulance();
    translate([17.0, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_truck_box(REDC, WHITEC);
    translate([-21.0, -16.5, WZT]) prop_hydrant();
    v2_block_edge_props(seed, 2);
}

module v2_block_school_court(seed = 0)
{
    v2_block_pad();
    translate([0, 11.2, WZT]) rotate([0, 0, 180]) v2_bldg_school(27, 13);
    translate([0, -9.5, WZT]) bldg_court(22, 12);
    for (x = [-19, 19]) translate([x, 8, WZT]) nature_tree_street(0.9, seed + x);
    translate([-18.0, -16.5, WZT]) prop_bench();
    translate([18.0, -16.0, WZT]) prop_bin(TEALC);
}

module v2_block_gas_food(seed = 0)
{
    v2_block_pad();
    translate([-8.0, 6.0, WZT]) rotate([0, 0, 180]) bldg_gas();
    translate([14.0, 8.5, WZT]) rotate([0, 0, 180]) bldg_burger(11, 9, 4.6);
    translate([14.0, -9.5, WZT]) bldg_cafe(10, 8, 4.2);
    for (i = [0 : 2]) translate([-15 + i * 5.5, -11.0, WZT]) rotate([0, 0, 90]) veh_car(car_c(seed + i));
    translate([20.5, -16.0, WZT]) prop_bin(REDC);
    translate([-20.5, 15.0, WZT]) prop_board(YELLOWC);
    v2_block_edge_props(seed, 2);
}

module v2_block_logistics(seed = 0)
{
    v2_block_pad(false);
    color(APRONC) translate([0, 0, WZT]) slab(46, 39, 0.05);
    translate([-8.0, 8.0, WZT + 0.05]) rotate([0, 0, 180]) bldg_warehouse(22, 13, 6.8);
    for (i = [0 : 3])
        translate([12 + (i % 2) * 5.6, 8 + floor(i / 2) * 4.1, WZT + 0.05])
            prop_container_stack(seed + i, 1 + rnd(seed + i, 3));
    translate([-9, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_truck_box(BLUEC, WHITEC);
    translate([3.5, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_truck_ct(seed);
    translate([15.0, -11.5, WZT + 0.05]) rotate([0, 0, 90]) veh_forklift();
    translate([20.5, -16.5, WZT + 0.05]) prop_crate_pile(seed);
    translate([-21.0, -16.0, WZT]) prop_load_mark();
}

module v2_block_waterfront(seed = 0)
{
    v2_block_pad();
    color(GRASSC) translate([-11, 7, WZT]) slab(21, 20, 0.08);
    translate([-10.0, 8.0, WZT + 0.08]) rotate([0, 0, 180]) v2_bldg_harbor_office(18, 11);
    translate([12.0, 8.0, WZT]) rotate([0, 0, 180]) bldg_cafe(10, 8, 4.2);
    color(PLAZAC) translate([7.0, -8.5, WZT]) slab(26, 12, 0.05);
    translate([0, -8.5, WZT + 0.05]) prop_fountain_spiral();
    for (i = [0 : 3]) translate([7 + (i % 2) * 6, -11 + floor(i / 2) * 5, WZT + 0.05]) furn_cafe_table(umb_c(seed + i));
    for (x = [-20, 20]) translate([x, -13, WZT]) nature_palm(0.9, x < 0 ? 6 : -6);
    v2_block_edge_props(seed, 2);
}

module v2_block_civic(seed = 0)
{
    v2_block_pad();
    color(PLAZAC) translate([0, 0, WZT]) slab(43, 36, 0.05);
    translate([0, 7.0, WZT + 0.05]) rotate([0, 0, 180]) v2_bldg_civic(24, 14);
    translate([0, -9.5, WZT + 0.05]) prop_fountain_spiral();
    for (x = [-16, -8, 8, 16]) translate([x, -9.5, WZT + 0.05]) nature_tree_street(0.82, seed + x);
    for (x = [-12, 12]) translate([x, -15.5, WZT + 0.05]) prop_flag(x < 0 ? REDC : BLUEC);
    translate([-20, 12, WZT + 0.05]) prop_mailbox(REDC);
    translate([20, 12, WZT + 0.05]) prop_board(BLUEC);
}

module v2_block_transit(seed = 0)
{
    v2_block_pad();
    color(LOTC) translate([-7.0, 0, WZT]) slab(30, 35, 0.05);
    for (row = [0 : 1])
    {
        translate([-7.0, row == 0 ? -9 : 9, WZT + 0.05]) road_parking_row(8, 3.0, 6.0);
        for (i = [0 : 5])
            if (rnd(seed + row * 7 + i, 3) != 0)
                translate([-17.5 + i * 4.2, row == 0 ? -9 : 9, WZT + 0.11])
                    rotate([0, 0, row == 0 ? 90 : -90]) veh_car(car_c(seed + row * 7 + i));
    }
    color(PLAZAC) translate([14.0, 0, WZT]) slab(13, 35, 0.05);
    translate([14.0, -10.5, WZT + 0.05]) prop_bus_shelter();
    translate([14.0, 5.0, WZT + 0.05]) prop_fountain_spiral();
    translate([14.0, 13.0, WZT + 0.05]) prop_board(BLUEC);
    translate([9.5, 10.0, WZT + 0.05]) nature_tree_street(0.85, seed);
    translate([18.5, 10.0, WZT + 0.05]) nature_tree_street(0.85, seed + 1);
}

module v2_block_wind_green(seed = 0)
{
    v2_block_pad(false);
    color(GRASSC) translate([0, 0, WZT]) slab(46, 39, 0.10);
    translate([0, 0, WZT + 0.10]) prop_wind_turbine(17, rnd(seed, 4) * 20);
    for (i = [0 : 9])
    {
        xx = -20 + rnd(seed * 5 + i, 40);
        yy = -17 + rnd(seed * 9 + i, 34);
        if (abs(xx) > 5 || abs(yy) > 6)
            translate([xx, yy, WZT + 0.10]) nature_pine(0.8 + 0.12 * rnd(seed + i, 4));
    }
    color(WALKC) translate([0, -14, WZT + 0.10]) slab(3, 10, 0.04);
    translate([4.0, -14, WZT + 0.14]) prop_bench();
}

module v2_block_by_type(t = 0, seed = 0)
{
    if (t == 0) v2_block_houses(seed);
    else if (t == 1) v2_block_apartments(seed);
    else if (t == 2) v2_block_shops(seed);
    else if (t == 3) v2_block_offices(seed);
    else if (t == 4) v2_block_park(seed);
    else if (t == 5) v2_block_market(seed);
    else if (t == 6) v2_block_hotel(seed);
    else if (t == 7) v2_block_hospital(seed);
    else if (t == 8) v2_block_police_fire(seed);
    else if (t == 9) v2_block_school_court(seed);
    else if (t == 10) v2_block_gas_food(seed);
    else if (t == 11) v2_block_logistics(seed);
    else if (t == 12) v2_block_waterfront(seed);
    else if (t == 13) v2_block_civic(seed);
    else if (t == 14) v2_block_transit(seed);
    else v2_block_wind_green(seed);
}

// ================= 地形、道路与城市家具 =================

module v2_ground_all()
{
    // 展台和城市结构层，范围与旧版基本一致。
    color(BASEC) translate([0, 90, -2.6]) boxc([680, 512, 2.8]);
    color([0.42, 0.40, 0.38]) translate([0, 117, -0.60]) boxc([672, 314, 1.2]);

    // 北部草地、滨海步道、沙滩和三段海色。
    color(GRASSC) translate([0, 309, 0]) slab(672, 70, 0.42);
    color(WALKC) translate([0, -37, 0]) slab(672, 6, 0.18);
    color(WALKD) for (x = [-333 : 3 : 333]) translate([x, -37, 0.18]) boxc([0.045, 6, 0.006]);
    color(SANDC) translate([-154, -57, -0.62]) boxc([364, 34, 1.16]);
    color(SANDW) translate([-154, -76, -0.78]) boxc([364, 7, 0.84]);
    color(APRONC) translate([210, -58, -0.43]) boxc([308, 38, 0.86]);
    color(YELLINE) translate([210, -78.1, 0.02]) boxc([306, 0.22, 0.04]);

    color(SEAC) translate([0, -121, -0.88]) boxc([672, 86, 0.66]);
    color([0.33, 0.67, 0.80]) translate([-154, -82, -0.54]) boxc([364, 8, 0.04]);

    // 岸线碎浪，形成参考图式的细小亮边。
    color(FOAMC) translate([-154, -79.5, -0.49]) boxc([364, 0.45, 0.04]);
    for (i = [0 : 34])
        color(FOAMC) translate([-330 + i * 19 + rnd(i * 5, 12), -86 - rnd(i * 3, 70), -0.49])
            boxc([2.0 + rnd(i, 5), 0.22, 0.03]);
}

module v2_roads_all()
{
    for (y = V2_HY) translate([0, y, 0]) road_x(672, -330, 330);
    for (x = V2_VX) translate([x, 120, 0]) road_y(292, -142, 142);

    // HD road 模块画连续虚线；路口上方以路面色短块遮盖，让斑马线更清楚。
    for (x = V2_VX, y = V2_HY)
    {
        color(ROADC) translate([x, y, GZT]) boxc([9.0, 9.0, 0.018]);
        translate([x, y - 5.35, 0]) road_crosswalk();
        translate([x, y + 5.35, 0]) road_crosswalk();
        translate([x - 5.35, y, 0]) rotate([0, 0, 90]) road_crosswalk();
        translate([x + 5.35, y, 0]) rotate([0, 0, 90]) road_crosswalk();
    }

    // 主路口信号灯。每隔一个路口设置四角灯，远景形成稳定的竖向节奏。
    for (xi = [0 : 2 : 10], yi = [0 : 2 : 6])
    {
        xx = V2_VX[xi];
        yy = V2_HY[yi];
        translate([xx - 5.1, yy - 5.1, GZT]) prop_traffic_light();
        translate([xx + 5.1, yy + 5.1, GZT]) rotate([0, 0, 180]) prop_traffic_light();
        translate([xx - 5.1, yy + 5.1, GZT]) rotate([0, 0, 90]) prop_traffic_light();
        translate([xx + 5.1, yy - 5.1, GZT]) rotate([0, 0, -90]) prop_traffic_light();
    }

    // 沿街灯、消防栓、邮箱和公交站，不与所有街区内部细节重复。
    for (i = [0 : 11])
    {
        translate([V2_CX[i], -25.1, GZT]) rotate([0, 0, 180]) prop_lamp();
        translate([V2_CX[i], 24.9, GZT]) rotate([0, 0, 180]) prop_lamp();
        translate([V2_CX[i], 124.9, GZT]) rotate([0, 0, 180]) prop_lamp();
        translate([V2_CX[i], 224.9, GZT]) rotate([0, 0, 180]) prop_lamp();
    }
    for (i = [0 : 5])
    {
        translate([V2_CX[i * 2], 74.9, GZT]) prop_hydrant();
        translate([V2_CX[i * 2 + 1], 174.9, GZT]) prop_mailbox((i % 2 == 0) ? REDC : BLUEC);
    }
    translate([-252, -25.2, GZT]) rotate([0, 0, 180]) prop_bus_shelter();
    translate([28, 24.8, GZT]) rotate([0, 0, 180]) prop_bus_shelter();
    translate([196, 124.8, GZT]) rotate([0, 0, 180]) prop_bus_shelter();
    translate([-84, 224.8, GZT]) rotate([0, 0, 180]) prop_bus_shelter();
}

// ================= 城市交通 =================

module v2_vehicle_pick(i = 0)
{
    t = rnd(i, 15);
    if (t < 8) veh_car(car_c(i));
    else if (t == 8) veh_taxi();
    else if (t == 9) veh_police_car();
    else if (t == 10) veh_ambulance();
    else if (t < 13) veh_bus((rnd(i, 2) == 0) ? BLUEC : TEALC);
    else if (t == 13) veh_truck_box((rnd(i, 2) == 0) ? BLUEC : REDC, WHITEC);
    else veh_truck_ct(i);
}

module v2_traffic_x(y = 0, seed = 0, n = 11)
{
    for (i = [0 : n - 1])
    {
        xx = -323 + i * (646 / n) + rnd(seed * 7 + i, 18);
        if (v2_near(xx, V2_VX) > 9)
        {
            east = rnd(seed * 3 + i, 2) == 0;
            translate([xx, y + (east ? -2.0 : 2.0), GZT])
                rotate([0, 0, east ? 0 : 180]) v2_vehicle_pick(seed * 17 + i);
        }
    }
}

module v2_traffic_y(x = 0, seed = 0, n = 5)
{
    for (i = [0 : n - 1])
    {
        yy = -20 + i * (282 / n) + rnd(seed * 5 + i, 16);
        if (v2_near(yy, V2_HY) > 9)
        {
            north = rnd(seed * 3 + i, 2) == 0;
            translate([x + (north ? 2.0 : -2.0), yy, GZT])
                rotate([0, 0, north ? 90 : -90]) v2_vehicle_pick(seed * 19 + i);
        }
    }
}

module v2_traffic_all()
{
    for (i = [0 : 6]) v2_traffic_x(V2_HY[i], 10 + i * 7, 11);
    for (i = [0 : 10]) v2_traffic_y(V2_VX[i], 90 + i * 5, 5);
}

// ================= 南侧海滨、游艇港与货运港 =================

module v2_promena_de()
{
    for (i = [0 : 33])
        translate([-326 + i * 19.8 + rnd(i, 5), -37.0, 0.18])
            nature_palm(0.90 + 0.08 * rnd(i, 4), 4 + rnd(i, 7));
    for (i = [0 : 11])
    {
        translate([-308 + i * 56, -36.5, 0.18]) prop_bench();
        if (i % 3 == 0) translate([-300 + i * 56, -36.2, 0.18]) prop_bin(TEALC);
    }
    for (i = [0 : 11]) translate([-326 + i * 59, -33.8, 0.18]) rotate([0, 0, 180]) prop_lamp();
}

module v2_beach_west()
{
    for (i = [0 : 27])
    {
        xx = -330 + (i % 14) * 13.0 + rnd(i * 3, 6);
        yy = -49 - floor(i / 14) * 13.0 - rnd(i * 7, 5);
        translate([xx, yy, 0.02]) beach_umbrella(umb_c(i));
        translate([xx + 2.0, yy + 0.4, 0.02]) rotate([0, 0, rnd(i, 8) * 40]) beach_lounger(umb_c(i + 2));
        if (i % 3 == 0) translate([xx - 2.0, yy - 1.3, 0.02]) rotate([0, 0, 20 * i]) beach_towel(umb_c(i + 4));
    }
    translate([-298, -57, 0.02]) beach_lifeguard();
    translate([-210, -58, 0.02]) beach_lifeguard();
    translate([-326, -45, 0.02]) beach_hut(REDC);
    translate([-317, -45, 0.02]) beach_hut(BLUEC);
    translate([-308, -45, 0.02]) beach_hut(TEALC);

    // 沙滩排球场。
    color(SANDW) translate([-132, -59, 0.02]) slab(26, 14, 0.03);
    color(MARKC)
    {
        for (sy = [-1, 1]) translate([-132, -59 + sy * 6.8, 0.06]) boxc([25.5, 0.09, 0.02]);
        for (sx = [-1, 1]) translate([-132 + sx * 12.7, -59, 0.06]) boxc([0.09, 13.5, 0.02]);
    }
    color(GREYC) for (sx = [-1, 1]) translate([-132 + sx * 0.2, -65.8, 0.05]) cylinder(h = 2.2, r = 0.08, $fn = 6);
    color(WHITEC) translate([-132, -59, 1.55]) boxc([0.08, 13.5, 0.75]);
}

module v2_marina()
{
    // 港务广场与三座木栈桥。
    color(PLAZAC) translate([18, -56, 0.02]) slab(112, 34, 0.10);
    for (px = [-22, 18, 58])
    {
        translate([px, -40, 0.12]) prop_pier(42, 4.5);
        translate([px - 4.0, -68, -0.50]) rotate([0, 0, 90]) boat_sail(roof_c(px));
        translate([px + 4.0, -74, -0.50]) rotate([0, 0, -90]) boat_speed(car_c(px), false);
        translate([px, -42, 0.12]) prop_bollard_pair();
    }
    translate([18, -51, 0.12]) prop_harbor_hut();
    translate([6, -49, 0.12]) prop_lifering_post();
    translate([30, -49, 0.12]) prop_lifering_post();
    for (i = [0 : 5]) translate([-27 + i * 18, -45, 0.12]) prop_flag(umb_c(i));
}

module v2_cargo_port()
{
    // 堆场网格、箱堆、吊机、叉车和泊位设施。
    color(WALKD) for (x = [70 : 8 : 330]) translate([x, -57, 0.02]) boxc([0.08, 34, 0.02]);
    color(WALKD) for (y = [-73 : 8 : -43]) translate([205, y, 0.02]) boxc([260, 0.08, 0.02]);

    for (i = [0 : 27])
    {
        xx = 82 + (i % 9) * 27.0;
        yy = -48 - floor(i / 9) * 9.0;
        translate([xx, yy, 0.05]) rotate([0, 0, (i % 2) * 90])
            prop_container_stack(i + 20, 1 + rnd(i, 3));
    }
    translate([105, -67, 0.05]) prop_crane(ORANGEC);
    translate([210, -67, 0.05]) prop_crane(YELLOWC);
    translate([310, -67, 0.05]) prop_crane(REDC);

    for (i = [0 : 5])
        translate([90 + i * 42, -75, 0.05]) prop_bollard_pair();
    for (i = [0 : 6])
        translate([78 + i * 40, -42.5, 0.05]) prop_lifering_post();
    translate([140, -44, 0.05]) rotate([0, 0, 90]) veh_forklift();
    translate([248, -44, 0.05]) rotate([0, 0, 90]) veh_forklift(YELLOWC);
    translate([292, -55, 0.05]) rotate([0, 0, -90]) veh_truck_ct(8);
    translate([186, -55, 0.05]) rotate([0, 0, -90]) veh_truck_box(BLUEC, WHITEC);
}

module v2_sea_traffic()
{
    translate([225, -102, -0.50]) rotate([0, 0, 3]) boat_cargo(52, [0.62, 0.24, 0.20], 1);
    translate([-105, -132, -0.50]) rotate([0, 0, -8]) boat_cargo(42, [0.20, 0.36, 0.50], 5);
    translate([-285, -115, -0.50]) rotate([0, 0, 15]) boat_sail([0.24, 0.32, 0.48]);
    translate([-205, -102, -0.50]) rotate([0, 0, -20]) boat_speed(WHITEC);
    translate([-12, -118, -0.50]) rotate([0, 0, 28]) boat_speed(YELLOWC);
    translate([82, -132, -0.50]) rotate([0, 0, 160]) boat_sail([0.55, 0.25, 0.22]);
    translate([320, -135, -0.50]) rotate([0, 0, 185]) boat_speed(REDC);

    for (i = [0 : 12])
        translate([-316 + i * 52, -91 - rnd(i, 22), -0.50]) prop_buoy((i % 2 == 0) ? REDC : YELLOWC);
    for (i = [0 : 8])
        translate([-260 + i * 65, -92 - rnd(i * 4, 35), 8 + rnd(i * 5, 8)])
            rotate([0, 0, rnd(i, 8) * 45]) prop_gull();
}

module v2_waterfront_all()
{
    v2_promena_de();
    v2_beach_west();
    v2_marina();
    v2_cargo_port();
    v2_sea_traffic();
}

// ================= 北部丘陵、树海与能源地标 =================

module v2_north_greenbelt()
{
    translate([-160, 307, 0.42]) scale([1.7, 1.0, 1.15]) nature_hill(25, 13);
    translate([70, 313, 0.42]) scale([1.15, 0.85, 0.95]) nature_hill(23, 12);
    translate([255, 310, 0.42]) scale([1.05, 0.80, 0.85]) nature_hill(21, 11);
    translate([-165, 308, 16.5]) prop_radio_tower(17);

    for (i = [0 : 65])
    {
        xx = -330 + (i % 33) * 20 + rnd(i * 3, 11);
        yy = 282 + floor(i / 33) * 29 + rnd(i * 7, 18);
        if (abs(xx + 160) > 38 && abs(xx - 70) > 30 && abs(xx - 255) > 28)
            translate([xx, yy, 0.42])
                nature_tree(1.0 + 0.15 * rnd(i, 5), i);
    }
    for (i = [0 : 30])
    {
        xx = -320 + i * 21 + rnd(i, 10);
        yy = 330 - rnd(i * 4, 20);
        translate([xx, yy, 0.42]) nature_pine(1.0 + 0.12 * rnd(i, 4));
    }

    translate([-270, 310, 0.42]) prop_wind_turbine(24, 0);
    translate([170, 322, 0.42]) prop_wind_turbine(25, 25);
    translate([310, 321, 0.42]) prop_wind_turbine(23, -18);
}

// ================= 总装 =================

v2_ground_all();
v2_roads_all();

for (r = [0 : 5], c = [0 : 11])
    translate([V2_CX[c], V2_RY[r], 0])
        v2_block_by_type(V2_LAYOUT[r][c], 1 + r * 17 + c * 3);

v2_waterfront_all();
v2_north_greenbelt();
v2_traffic_all();
