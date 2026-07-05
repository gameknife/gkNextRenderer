// habor_city.scad —— 低多边形等距大型海滨城市（参考卡通城市示意图）
//
// 构造经验沿用 airport.scad / office.scad / modern_city：
//   * 单文件、模块化、参数复用：基础件(prop_/veh_/boat_) → 建筑(bldg_/house) → 组团(block_*) → 总装
//   * 所有带朝向的 module 约定 front = -y；布局处 rotate 调整朝向；
//     落地件底面 z=0，布局时 translate 到所在台面高度。
//   * 伪随机：rnd(i,m) 整数散列，保证确定性（引擎与 OpenSCAD 渲染一致）。
//   * 仅使用引擎 SCADLoader 已支持特性（无 offset/projection/minkowski/import）。
//
// OpenSCAD Z-up。城市朝向：+y 为北（山地森林），-y 为南（海滩与海洋）。
// 总平面 672 x 508（y∈[-164,344]），城市建成区 12 列 x 6 行街区（约初版 3 倍）：
//   y∈[274,344]  北部山地森林带（大山 + 信号塔 + 东侧次丘 + 分簇变密度树海）
//   y∈[-34,274]  城市路网（11 纵 x 7 横）：
//        Row F/E (y≈245/195) 北郊住宅带     Row D (y≈145) 公园 + 公寓带
//        Row C/B (y≈95/45)   中央 CBD 天际线（hs 参数控制各组团高度）
//        Row A   (y≈-5)      滨海商业带（超市/商街/球场/双酒店/停车广场）
//   y∈[-40,-34]  滨海步道（棕榈）   y∈[-78,-40] 沙滩（伞/躺椅/救生塔/双木栈桥）
//   y∈[-164,-78] 海洋（3 货轮/帆船/快艇/浮标/直升机/海鸥）

$fn = 12;

// ================= 配色 =================
GRASSC  = [0.55, 0.75, 0.33];   // 草地
GRASSD  = [0.46, 0.67, 0.27];   // 深草
PITCHC  = [0.38, 0.69, 0.30];   // 球场草
PAVEC   = [0.86, 0.86, 0.83];   // 人行道/台面
PLAZAC  = [0.80, 0.78, 0.74];   // 广场砖
ROADC   = [0.29, 0.30, 0.33];   // 沥青
LOTC    = [0.44, 0.46, 0.48];   // 停车场
MARKC   = [0.93, 0.93, 0.91];   // 标线
SANDC   = [0.93, 0.84, 0.58];   // 干沙
SANDW   = [0.87, 0.77, 0.54];   // 湿沙
SEAC    = [0.25, 0.60, 0.77];   // 海面
SEAD    = [0.235, 0.575, 0.745]; // 深海块
SEAL    = [0.47, 0.76, 0.84];   // 浅水带
FOAMC   = [0.95, 0.97, 0.97];   // 浪花
BASEC   = [0.24, 0.21, 0.28];   // 展台底座
WHITEC  = [0.95, 0.95, 0.93];
CREAMC  = [0.93, 0.88, 0.73];
GREYC   = [0.62, 0.64, 0.66];
DGREYC  = [0.36, 0.38, 0.41];
DARKC   = [0.16, 0.17, 0.19];
GLASSB  = [0.33, 0.56, 0.82];   // 蓝玻璃幕墙
GLASSD  = [0.20, 0.36, 0.60];   // 深蓝玻璃
GLASSL  = [0.55, 0.74, 0.90];   // 浅蓝玻璃/窗
REDC    = [0.85, 0.26, 0.20];
ORANGEC = [0.92, 0.57, 0.22];
YELLOWC = [0.95, 0.78, 0.20];
TEALC   = [0.30, 0.62, 0.58];
BROWNC  = [0.46, 0.30, 0.18];
TRUNKC  = [0.44, 0.30, 0.17];
LEAFC   = [0.44, 0.70, 0.25];
LEAFD   = [0.33, 0.58, 0.20];
PALMC   = [0.30, 0.62, 0.30];
OAKC    = [0.76, 0.56, 0.34];   // 木栈道
POOLC   = [0.35, 0.72, 0.85];   // 泳池水

// ---- 伪随机 / 调色板 ----
function rnd(i, m)    = let (k = (i * 73 + 31) % 97, kp = (k < 0) ? k + 97 : k) (kp * 13 + ((i % 7 + 7) % 7)) % m;
function house_c(i)   = [[0.93,0.88,0.73],[0.88,0.91,0.94],[0.93,0.82,0.66],[0.82,0.88,0.78],[0.93,0.93,0.90],[0.86,0.78,0.66]][rnd(i,6)];
function roof_c(i)    = [[0.85,0.26,0.20],[0.55,0.34,0.20],[0.92,0.57,0.22],[0.30,0.44,0.62]][rnd(i,4)];
function apt_c(i)     = [[0.72,0.33,0.26],[0.81,0.46,0.30],[0.92,0.75,0.38],[0.93,0.87,0.72],[0.42,0.64,0.60],[0.84,0.56,0.47]][rnd(i,6)];
function car_c(i)     = [[0.85,0.26,0.20],[0.93,0.94,0.95],[0.60,0.62,0.66],[0.24,0.32,0.48],[0.92,0.57,0.22],[0.42,0.62,0.40]][rnd(i,6)];
function ctn_c(i)     = [[0.80,0.28,0.24],[0.92,0.57,0.22],[0.28,0.50,0.76],[0.40,0.65,0.36],[0.62,0.44,0.70],[0.90,0.88,0.84]][rnd(i,6)];
function umb_c(i)     = [[0.85,0.26,0.20],[0.95,0.78,0.20],[0.28,0.50,0.76],[0.40,0.65,0.36],[0.92,0.57,0.22],[0.84,0.45,0.62]][rnd(i,6)];

// ---- 基础工具 ----
module boxc(s) cube(s, center = true);
module slab(L, D, t) translate([0, 0, t / 2]) boxc([L, D, t]);   // 底面 z=0 平板

// ================= 植被 =================
// 阔叶树（双球冠，s 整体缩放）
module nature_tree(s = 1.0, i = 0)
{
    scale([s, s, s])
    {
        color(TRUNKC) cylinder(h = 1.6, r = 0.22, $fn = 7);
        color((rnd(i, 2) == 0) ? LEAFC : LEAFD) translate([0, 0, 2.5]) sphere(r = 1.45, $fn = 8);
        color((rnd(i, 2) == 0) ? LEAFD : LEAFC) translate([0.55, 0.3, 3.4]) sphere(r = 0.95, $fn = 8);
    }
}

// 松树（双锥）
module nature_pine(s = 1.0)
{
    scale([s, s, s])
    {
        color(TRUNKC) cylinder(h = 1.0, r = 0.18, $fn = 7);
        color(LEAFD) translate([0, 0, 0.9]) cylinder(h = 2.2, r1 = 1.25, r2 = 0.35, $fn = 8);
        color(LEAFC) translate([0, 0, 2.6]) cylinder(h = 1.6, r1 = 0.85, r2 = 0.06, $fn = 8);
    }
}

// 棕榈树（沙滩/街道用，s 缩放）
module nature_palm(s = 1.0, lean = 6)
{
    scale([s, s, s]) rotate([0, lean, 0])
    {
        color([0.52, 0.40, 0.26]) cylinder(h = 4.6, r1 = 0.26, r2 = 0.16, $fn = 7);
        for (a = [0 : 60 : 300])
            color((a % 120 == 0) ? PALMC : LEAFD)
                rotate([0, 0, a]) translate([1.15, 0, 4.65]) rotate([0, 30, 0]) boxc([2.3, 0.5, 0.09]);
        color([0.40, 0.55, 0.28]) translate([0, 0, 4.6]) sphere(r = 0.28, $fn = 8);
        color([0.62, 0.42, 0.22]) translate([0.35, 0.18, 4.45]) sphere(r = 0.14, $fn = 7);
        color([0.62, 0.42, 0.22]) translate([0.10, -0.34, 4.45]) sphere(r = 0.14, $fn = 7);
    }
}

module nature_hedge(len = 4)
{
    color(LEAFD) translate([0, 0, 0.40]) boxc([len, 0.9, 0.80]);
    color(LEAFC) translate([0, 0, 0.80]) boxc([len - 0.5, 0.6, 0.22]);
}

// 低多边形大山（北侧地标，含次峰；底面 z=0）
module nature_mountain()
{
    color([0.47, 0.68, 0.30]) cylinder(h = 24, r1 = 30, r2 = 17, $fn = 7);
    color([0.42, 0.63, 0.27]) translate([2, -3, 20]) rotate([0, 0, 26]) cylinder(h = 16, r1 = 19, r2 = 9, $fn = 7);
    color([0.52, 0.72, 0.34]) translate([-2, 2, 34]) rotate([0, 0, 50]) cylinder(h = 13, r1 = 10, r2 = 2.8, $fn = 7);
    // 次峰
    color([0.44, 0.66, 0.29]) translate([26, 8, 0]) rotate([0, 0, 14]) cylinder(h = 20, r1 = 18, r2 = 6, $fn = 7);
    color([0.51, 0.71, 0.33]) translate([24, 7, 16]) rotate([0, 0, 40]) cylinder(h = 10, r1 = 7, r2 = 2.0, $fn = 7);
}

// 山顶信号塔（红白相间桅杆 + 横撑 + 小天线）
module prop_radio_tower(h = 15)
{
    for (i = [0 : 4])
        color((i % 2 == 0) ? [0.88, 0.30, 0.24] : WHITEC)
            translate([0, 0, i * h / 5]) cylinder(h = h / 5, r1 = 0.62 - i * 0.09, r2 = 0.62 - (i + 1) * 0.09, $fn = 6);
    color(DGREYC) for (zz = [h * 0.35, h * 0.7]) translate([0, 0, zz]) boxc([2.6, 0.16, 0.16]);
    color(DGREYC) for (zz = [h * 0.35, h * 0.7]) translate([0, 0, zz]) boxc([0.16, 2.6, 0.16]);
    color(WHITEC) translate([0, 0, h]) cylinder(h = 2.6, r = 0.07, $fn = 6);
    color([0.88, 0.30, 0.24]) translate([0, 0, h + 2.6]) sphere(r = 0.22, $fn = 8);
}

// ================= 街道小品 =================
module prop_lamp()
{
    color(DGREYC)
    {
        cylinder(h = 0.16, r = 0.16, $fn = 8);
        cylinder(h = 4.6, r = 0.07, $fn = 6);
        translate([0, 0.42, 4.55]) boxc([0.09, 0.95, 0.08]);
    }
    color([0.97, 0.93, 0.75]) translate([0, 0.82, 4.48]) boxc([0.24, 0.5, 0.14]);
}

module prop_bench()
{
    color(DGREYC) for (sx = [-1, 1]) translate([0.62 * sx, 0, 0.20]) boxc([0.08, 0.48, 0.40]);
    color(OAKC) translate([0, 0, 0.42]) boxc([1.55, 0.48, 0.07]);
    color(OAKC) translate([0, 0.24, 0.66]) rotate([10, 0, 0]) boxc([1.55, 0.06, 0.42]);
}

module prop_bin(c = [0.30, 0.50, 0.40])
{
    color(c) cylinder(h = 0.75, r = 0.26, $fn = 8);
    color(DARKC) translate([0, 0, 0.75]) cylinder(h = 0.07, r = 0.27, $fn = 8);
}

// 公交候车亭（front=-y）
module prop_bus_shelter()
{
    color(DGREYC) for (sx = [-1, 1]) translate([1.45 * sx, 0.40, 1.25]) boxc([0.10, 0.10, 2.50]);
    color([0.55, 0.72, 0.85, 0.45]) translate([0, 0.46, 1.35]) boxc([3.00, 0.05, 1.60]);
    color(GREYC) translate([0, 0.20, 2.56]) boxc([3.30, 1.30, 0.10]);
    color(DGREYC) translate([0, 0.30, 0.42]) boxc([2.40, 0.35, 0.08]);
    color([0.28, 0.50, 0.76]) translate([1.55, 0.40, 2.30]) boxc([0.06, 0.45, 0.45]);
}

// 圆形喷泉
module prop_fountain()
{
    color(PAVEC) cylinder(h = 0.35, r = 2.4, $fn = 10);
    color(POOLC) translate([0, 0, 0.35]) cylinder(h = 0.10, r = 2.1, $fn = 10);
    color(PAVEC) translate([0, 0, 0.35]) cylinder(h = 0.85, r = 0.42, $fn = 8);
    color(POOLC) translate([0, 0, 1.2]) sphere(r = 0.30, $fn = 8);
    color(FOAMC) translate([0, 0, 0.46]) cylinder(h = 0.02, r = 0.85, $fn = 10);
}

// 小吃亭（雪糕/饮料，front=-y）
module prop_kiosk(c = ORANGEC)
{
    color(WHITEC) translate([0, 0, 1.1]) boxc([2.6, 2.0, 2.2]);
    color(c) translate([0, 0, 2.42]) boxc([3.0, 2.4, 0.45]);
    color(c) translate([0, -1.01, 0.85]) boxc([2.2, 0.06, 0.5]);
    color(GLASSL) translate([0, -1.01, 1.6]) boxc([2.2, 0.05, 0.8]);
    color(WHITEC) translate([0, -1.35, 2.2]) rotate([18, 0, 0]) boxc([3.0, 0.85, 0.06]);
}

// 旗杆
module prop_flag(c = [0.28, 0.50, 0.76])
{
    color(GREYC) cylinder(h = 7.0, r = 0.08, $fn = 6);
    color(c) translate([0.95, 0, 6.4]) boxc([1.8, 0.05, 1.05]);
}

// 城市标识字（广场用）
module prop_city_sign(label = "HARBOR CITY")
{
    color(DGREYC) translate([0, 0, 0.35]) boxc([len(label) * 1.55 + 1.6, 1.5, 0.7]);
    color(WHITEC) translate([-len(label) * 0.72, -0.4, 0.7]) linear_extrude(2.2) text(label, size = 1.9);
}

// ================= 道路 =================
RW = 8;          // 路宽
RT = 0.15;       // 路面厚
CURB = 0.32;     // 街区台面高

module road_x(L) color(ROADC) slab(L, RW, RT);
module road_y(L) color(ROADC) slab(RW, L, RT);

// 斑马线（横跨沿 y 道路；行人沿 x 通过）
module road_crosswalk()
{
    color(MARKC) for (x = [-2.8 : 1.4 : 2.8]) translate([x, 0, RT + 0.015]) boxc([0.8, 2.4, 0.025]);
}

// 停车场（含分隔线；置于台面顶）
module road_parking(W = 24, D = 12, bays = 4)
{
    color(LOTC) slab(W, D, 0.06);
    color(MARKC) for (i = [0 : bays])
        translate([-W / 2 + i * (W / bays), -D * 0.22, 0.07]) boxc([0.25, D * 0.56, 0.02]);
}

// ================= 建筑公共件 =================
// 环绕玻璃窗带
module part_win_band(L, D, z, hh = 1.3)
{
    color(GLASSL)
    {
        translate([0, -D / 2 - 0.05, z]) boxc([L * 0.80, 0.1, hh]);
        translate([0, D / 2 + 0.05, z]) boxc([L * 0.80, 0.1, hh]);
        translate([-L / 2 - 0.05, 0, z]) boxc([0.1, D * 0.72, hh]);
        translate([L / 2 + 0.05, 0, z]) boxc([0.1, D * 0.72, hh]);
    }
}

// 单面整齐窗阵（front/back 两面；白框 + 玻璃）
module part_win_grid(L, D, floors, cols, z0 = 2.0, fh = 2.9, fc = WHITEC)
{
    pitch = (L - 2.4) / cols;
    for (f = [0 : floors - 1], c = [0 : cols - 1], sy = [-1, 1])
        translate([-L / 2 + 1.2 + pitch * (c + 0.5), sy * (D / 2 + 0.04), z0 + f * fh])
        {
            color(fc) boxc([1.5, 0.10, 1.7]);
            color(GLASSL) translate([0, sy * 0.04, 0]) boxc([1.18, 0.06, 1.38]);
        }
}

// 四条板拼接女儿墙（避免 difference，降载）
module part_parapet(L, D, c = WHITEC, h = 0.6, t = 0.45)
{
    color(c)
    {
        for (sy = [-1, 1]) translate([0, sy * (D - t) / 2, h / 2]) boxc([L, t, h]);
        for (sx = [-1, 1]) translate([sx * (L - t) / 2, 0, h / 2]) boxc([t, D - 2 * t, h]);
    }
}

// 屋顶杂项：空调 + 水箱 + 出屋面楼梯间（seed 控制取舍）
module part_roof_clutter(L, D, seed = 0)
{
    color(GREYC) translate([-L * 0.25, -D * 0.18, 0.45]) boxc([1.8, 1.2, 0.9]);
    color(DGREYC) translate([-L * 0.25, -D * 0.18, 0.95]) boxc([1.4, 0.9, 0.12]);
    if (rnd(seed, 2) == 0)
    {
        color(GREYC) translate([L * 0.24, D * 0.18, 0.8]) cylinder(h = 1.6, r = 0.62, $fn = 8);
        color(DGREYC) translate([L * 0.24, D * 0.18, 2.4]) cylinder(h = 0.22, r = 0.72, $fn = 8);
    }
    if (rnd(seed + 1, 2) == 0)
        color(WHITEC) translate([L * 0.22, -D * 0.2, 1.0]) boxc([2.4, 2.0, 2.0]);
}

// 屋顶花园（绿化 + 树篱 + 凉棚）
module part_roof_garden(L, D)
{
    color(GRASSC) slab(L - 1.6, D - 1.6, 0.16);
    translate([-L * 0.22, 0, 0.1]) nature_hedge(D * 0.45);
    color(OAKC) for (sx = [-1, 1], sy = [-1, 1])
        translate([L * 0.25 + sx * 1.1, sy * 1.0, 1.0]) boxc([0.14, 0.14, 2.0]);
    color(OAKC) translate([L * 0.25, 0, 2.0]) boxc([2.6, 2.5, 0.12]);
}

// 阳台（front=-y 面外挑）
module part_balcony(w = 2.2)
{
    color(WHITEC) translate([0, -0.55, 0]) boxc([w, 1.1, 0.14]);
    color(WHITEC) translate([0, -1.05, 0.45]) boxc([w, 0.08, 0.9]);
    color(WHITEC) for (sx = [-1, 1]) translate([sx * (w / 2 - 0.04), -0.55, 0.45]) boxc([0.08, 1.0, 0.9]);
}

// ================= 建筑库（front = -y，底面 z=0） =================
// 多层公寓：色彩立面 + 白框窗阵 + 阳台 + 屋顶（花园或杂项）
module bldg_apartment(L = 17, D = 11, F = 5, bc = [0.72, 0.33, 0.26], garden = false, seed = 0)
{
    H = F * 2.9 + 1.6;
    color(WHITEC) slab(L + 0.5, D + 0.5, 0.5);
    color(bc) translate([0, 0, 0.5]) slab(L, D, H - 0.5);
    part_win_grid(L, D, F, 4, 2.3, 2.9);
    // 侧面窗带
    for (f = [0 : F - 1])
        color(GLASSL) for (sx = [-1, 1])
            translate([sx * (L / 2 + 0.04), 0, 2.3 + f * 2.9]) boxc([0.08, D * 0.55, 1.3]);
    // 中央每层阳台
    for (f = [1 : F - 1]) translate([0, -D / 2, 1.9 + f * 2.9 - 1.45]) part_balcony(2.4);
    // 入口门厅
    color(WHITEC) translate([0, -D / 2 - 0.45, 1.25]) boxc([2.6, 0.9, 2.5]);
    color(DGREYC) translate([0, -D / 2 - 0.92, 1.05]) boxc([1.5, 0.10, 2.1]);
    translate([0, 0, H]) part_parapet(L, D, WHITEC);
    if (garden) translate([0, 0, H]) part_roof_garden(L, D);
    else        translate([0, 0, H]) part_roof_clutter(L, D, seed);
}

// 蓝玻璃板式塔楼：幕墙 + 层线 + 角柱
module bldg_tower_glass(L = 14, D = 14, H = 38, gc = GLASSB, fc = WHITEC, seed = 0)
{
    color(fc) slab(L + 0.6, D + 0.6, 0.6);
    color(gc) translate([0, 0, 0.6]) slab(L, D, H - 0.6);
    for (z = [3.4 : 3.2 : H - 1.5])
        color(GLASSD) translate([0, 0, z]) boxc([L + 0.08, D + 0.08, 0.22]);
    color(fc) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * L / 2, sy * D / 2, H / 2]) boxc([0.5, 0.5, H]);
    translate([0, 0, H]) part_parapet(L + 0.3, D + 0.3, fc, 0.7, 0.5);
    translate([0, 0, H + 0.05]) part_roof_clutter(L, D, seed);
    color(DGREYC) translate([L / 4, -D / 4, H]) cylinder(h = 2.6, r = 0.07, $fn = 6);
    color(REDC) translate([L / 4, -D / 4, H + 2.7]) sphere(r = 0.18, $fn = 8);
}

// 圆柱塔楼 + 屋顶停机坪（蓝色幕墙，可选停放直升机）
module bldg_tower_round(r = 6.8, H = 34, heli = true)
{
    color(WHITEC) cylinder(h = 0.6, r = r + 0.4, $fn = 18);
    color(GLASSB) translate([0, 0, 0.6]) cylinder(h = H - 0.6, r = r, $fn = 18);
    for (z = [3.4 : 3.2 : H - 1.5])
        color(GLASSD) translate([0, 0, z]) cylinder(h = 0.22, r = r + 0.06, $fn = 18);
    // 顶盖 + 停机坪（白圈 + H，画家叠层避免 2D 布尔）
    color(WHITEC) translate([0, 0, H]) cylinder(h = 0.55, r = r + 0.35, $fn = 18);
    color(GLASSD) translate([0, 0, H + 0.55]) cylinder(h = 0.16, r = r - 0.4, $fn = 18);
    color(MARKC) translate([0, 0, H + 0.71]) cylinder(h = 0.03, r = r * 0.72, $fn = 18);
    color(GLASSD) translate([0, 0, H + 0.72]) cylinder(h = 0.03, r = r * 0.60, $fn = 18);
    color(MARKC) translate([-1.4, -1.7, H + 0.75]) linear_extrude(0.05) text("H", size = 3.4);
    // 边缘护栏
    color(GREYC) for (a = [0 : 30 : 330])
        rotate([0, 0, a]) translate([r + 0.1, 0, H + 1.0]) boxc([0.07, 0.07, 0.9]);
    if (heli) translate([2.3, 1.8, H + 0.74]) rotate([0, 0, 35]) veh_helicopter([0.93, 0.94, 0.95], fly = false);
}

// 退台尖顶塔（CBD 最高地标）
module bldg_tower_spire(L = 13, H = 46, gc = GLASSD)
{
    color(WHITEC) slab(L + 0.6, L + 0.6, 0.6);
    color(gc) translate([0, 0, 0.6]) slab(L, L, H * 0.62);
    color(gc) translate([0, 0, H * 0.62]) slab(L * 0.78, L * 0.78, H * 0.22);
    color(gc) translate([0, 0, H * 0.84]) slab(L * 0.55, L * 0.55, H * 0.16);
    for (z = [3.4 : 3.2 : H * 0.62 - 1])
        color(GLASSL) translate([0, 0, z]) boxc([L + 0.08, L + 0.08, 0.20]);
    for (z = [H * 0.62 + 2 : 3.2 : H * 0.84 - 1])
        color(GLASSL) translate([0, 0, z]) boxc([L * 0.78 + 0.08, L * 0.78 + 0.08, 0.20]);
    color(WHITEC) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * L / 2, sy * L / 2, H * 0.31 + 0.3]) boxc([0.5, 0.5, H * 0.62]);
    translate([0, 0, H]) part_parapet(L * 0.55, L * 0.55, WHITEC, 0.5, 0.35);
    color(GREYC) translate([0, 0, H]) cylinder(h = 7.5, r1 = 0.42, r2 = 0.05, $fn = 8);
    color(REDC) translate([0, 0, H + 7.5]) sphere(r = 0.26, $fn = 8);
}

// 砖色高层（白窗阵，示意图中心的红褐塔楼）
module bldg_tower_brick(L = 14, D = 12, F = 10, bc = [0.70, 0.34, 0.27], seed = 0)
{
    H = F * 2.9 + 1.8;
    color(WHITEC) slab(L + 0.5, D + 0.5, 0.7);
    color(bc) translate([0, 0, 0.7]) slab(L, D, H - 0.7);
    part_win_grid(L, D, F, 4, 2.5, 2.9);
    for (f = [0 : F - 1])
        color(GLASSL) for (sx = [-1, 1])
            translate([sx * (L / 2 + 0.04), 0, 2.5 + f * 2.9]) boxc([0.08, D * 0.5, 1.3]);
    color(WHITEC) translate([0, -D / 2 - 0.04, H - 0.6]) boxc([L, 0.14, 1.2]);   // 顶部白檐带
    translate([0, 0, H]) part_parapet(L, D, WHITEC);
    translate([0, 0, H]) part_roof_clutter(L, D, seed);
}

// 中型写字楼（白框 + 环绕窗带）
module bldg_office(L = 18, D = 13, H = 22, c = WHITEC, seed = 0)
{
    color(c) slab(L, D, H);
    for (f = [0 : floor((H - 4) / 3.4)]) part_win_band(L, D, 2.2 + f * 3.4, 1.6);
    translate([0, 0, H]) part_parapet(L + 0.3, D + 0.3, GREYC, 0.6, 0.5);
    translate([0, 0, H]) part_roof_clutter(L, D, seed);
    color(GLASSL) translate([0, -D / 2 - 0.15, 1.6]) boxc([5.0, 0.2, 3.0]);     // 入口玻璃厅
    color(DGREYC) translate([0, -D / 2 - 0.30, 3.3]) boxc([5.6, 0.4, 0.4]);
}

// 海滨酒店：奶油色长板楼 + 满铺阳台 + HOTEL 招牌
module bldg_hotel(L = 27, D = 13, F = 6)
{
    H = F * 3.0 + 1.6;
    color(CREAMC) slab(L, D, H);
    part_win_grid(L, D, F, 7, 2.4, 3.0, CREAMC);
    for (f = [1 : F - 1])
        translate([0, -D / 2, 1.0 + f * 3.0])
        {
            color(WHITEC) translate([0, -0.55, 0]) boxc([L - 2.0, 1.1, 0.14]);
            color(WHITEC) translate([0, -1.05, 0.42]) boxc([L - 2.0, 0.08, 0.85]);
        }
    translate([0, 0, H]) part_parapet(L, D, WHITEC);
    translate([0, 0, H]) part_roof_clutter(L, D, 1);
    // 屋顶招牌
    color(DGREYC) for (sx = [-1, 1]) translate([sx * 3.4, D * 0.18, H + 0.9]) boxc([0.14, 0.14, 1.8]);
    color(REDC) translate([0, D * 0.18, H + 2.1]) boxc([8.4, 0.3, 1.7]);
    color(WHITEC) translate([-2.9, D * 0.18 - 0.2, H + 1.45]) rotate([90, 0, 0]) linear_extrude(0.06) text("HOTEL", size = 1.15);
    // 玻璃门厅 + 雨棚
    color(GLASSL) translate([0, -D / 2 - 0.12, 1.6]) boxc([5.4, 0.2, 3.0]);
    color(REDC) translate([0, -D / 2 - 1.5, 3.3]) boxc([6.4, 3.0, 0.28]);
    color(GREYC) for (sx = [-1, 1]) translate([sx * 2.6, -D / 2 - 2.7, 1.6]) cylinder(h = 3.2, r = 0.12, $fn = 6);
}

// 独栋小住宅：坡顶 + 烟囱 + 门窗（front=-y）
module house(bc = [0.93, 0.88, 0.73], rc = [0.85, 0.26, 0.20])
{
    L = 8.5; D = 6.5; H = 3.2;
    color(bc) slab(L, D, H);
    color(rc) translate([0, 0, H]) rotate([90, 0, 90])
        linear_extrude(L + 1.0, center = true) polygon([[-D / 2 - 0.5, 0], [D / 2 + 0.5, 0], [0, 2.3]]);
    color(GLASSL) for (sx = [-1, 1]) translate([sx * 2.0, -D / 2 - 0.06, 1.7]) boxc([1.5, 0.12, 1.1]);
    color(BROWNC) translate([0, -D / 2 - 0.06, 1.05]) boxc([1.2, 0.12, 2.1]);
    color(GLASSL) translate([L / 2 + 0.05, 0, 1.7]) boxc([0.10, 2.2, 1.0]);
    color(GREYC) translate([L / 2 - 1.4, 1.2, H + 1.1]) boxc([0.75, 0.75, 1.9]);
}

// 私家泳池（含池边）
module prop_pool(W = 5.0, D = 3.4)
{
    color(PAVEC) slab(W + 1.2, D + 1.2, 0.18);
    color(POOLC) translate([0, 0, 0.18]) slab(W, D, 0.08);
    color(FOAMC) translate([-W * 0.25, D * 0.2, 0.27]) boxc([0.8, 0.25, 0.02]);
}

// 三开间临街商铺（彩色遮阳棚 + 屋顶招牌）
module bldg_shop_row(L = 20, D = 10, H = 5, seed = 0)
{
    color(CREAMC) slab(L, D, H);
    translate([0, 0, H]) part_parapet(L + 0.3, D + 0.3, WHITEC, 0.5, 0.4);
    s = L / 3;
    for (i = [0 : 2])
        translate([-L / 2 + s * (i + 0.5), 0, 0])
        {
            color(GLASSL) translate([0, -D / 2 - 0.06, 1.6]) boxc([s * 0.7, 0.12, 1.9]);
            cc = umb_c(seed + i);
            color(cc) translate([0, -D / 2 - 0.7, 3.45]) rotate([24, 0, 0]) boxc([s * 0.85, 1.7, 0.1]);
            color(WHITEC) translate([0, -D / 2 - 1.42, 3.10]) boxc([s * 0.85, 0.22, 0.18]);
        }
    part_win_band(L, D, H - 1.2, 1.0);
    color(umb_c(seed + 4)) translate([-L / 2 + 1.4, -D / 2 + 0.9, H + 1.0]) boxc([0.4, 2.6, 2.0]);
    translate([0, 0, H]) part_roof_clutter(L, D, seed);
}

// 蓝色超级市场（白色fascia + MARKET 文字 + 玻璃门面）
module bldg_market(L = 26, D = 16, H = 7)
{
    color([0.25, 0.45, 0.75]) slab(L, D, H);
    color(WHITEC) translate([0, -D / 2 - 0.10, H - 1.3]) boxc([L + 0.2, 0.3, 1.8]);
    color([0.25, 0.45, 0.75]) translate([-3.4, -D / 2 - 0.35, H - 1.3]) rotate([90, 0, 0]) linear_extrude(0.07) text("MARKET", size = 1.3);
    color(GLASSL) translate([0, -D / 2 - 0.08, 1.7]) boxc([L * 0.62, 0.16, 3.0]);
    color(DGREYC) translate([0, -D / 2 - 0.16, 1.7]) boxc([1.0, 0.2, 3.0]);
    translate([0, 0, H]) part_parapet(L, D, WHITEC);
    translate([0, 0, H]) part_roof_clutter(L, D, 0);
    color(GREYC) translate([L * 0.28, -D / 2 - 0.05, H + 0.4]) boxc([2.0, 0.4, 0.6]);
}

// 加油站：黄色雨棚 + 双泵岛 + 小商店（front=-y）
module bldg_gas_station()
{
    // 商店
    translate([5.5, 3.0, 0])
    {
        color(WHITEC) slab(8, 6, 3.4);
        color(REDC) translate([0, 0, 3.4]) slab(8.4, 6.4, 0.5);
        color(GLASSL) translate([0, -3.05, 1.6]) boxc([5.4, 0.12, 2.0]);
    }
    // 雨棚
    color(GREYC) for (p = [[-4, 0], [4, 0]]) translate([p[0] - 1.5, p[1], 0]) cylinder(h = 4.4, r = 0.26, $fn = 8);
    color(YELLOWC) translate([-1.5, 0, 4.4]) boxc([14, 9.5, 0.7]);
    color(REDC) translate([-1.5, -4.78, 4.4]) boxc([14, 0.06, 0.72]);
    color(WHITEC) translate([-1.5, 0, 4.05]) boxc([14.2, 9.7, 0.1]);
    // 泵岛
    for (px = [-5, 2])
        translate([px, 0, 0])
        {
            color(PAVEC) slab(3.4, 1.6, 0.22);
            color(REDC) translate([-0.7, 0, 0.22]) slab(0.85, 0.6, 1.5);
            color(REDC) translate([0.9, 0, 0.22]) slab(0.85, 0.6, 1.5);
            color(DARKC) translate([-0.7, -0.31, 1.35]) boxc([0.5, 0.04, 0.3]);
            color(DARKC) translate([0.9, -0.31, 1.35]) boxc([0.5, 0.04, 0.3]);
        }
    // 价目牌
    color(DGREYC) translate([-9.5, -3.2, 0]) cylinder(h = 4.6, r = 0.18, $fn = 6);
    color(YELLOWC) translate([-9.5, -3.2, 4.6]) boxc([2.0, 0.3, 1.8]);
    color(DARKC) translate([-9.5, -3.0, 4.6]) boxc([1.5, 0.05, 1.2]);
}

// 足球场：草坪 + 白线 + 球门 + 橙色看台 + 灯杆
module bldg_soccer_field()
{
    color(PITCHC) slab(34, 21, 0.14);
    // 边线/中线/中圈（画家叠层）
    color(MARKC)
    {
        for (sy = [-1, 1]) translate([0, sy * 9.7, 0.15]) boxc([31.6, 0.28, 0.02]);
        for (sx = [-1, 1]) translate([sx * 15.7, 0, 0.15]) boxc([0.28, 19.7, 0.02]);
        translate([0, 0, 0.15]) boxc([0.28, 19.7, 0.02]);
        for (sx = [-1, 1]) translate([sx * 12.6, 0, 0.15]) boxc([0.24, 8.6, 0.02]);
        for (sx = [-1, 1]) translate([sx * 14.2, 4.3, 0.15]) boxc([3.2, 0.24, 0.02]);
        for (sx = [-1, 1]) translate([sx * 14.2, -4.3, 0.15]) boxc([3.2, 0.24, 0.02]);
        translate([0, 0, 0.145]) cylinder(h = 0.02, r = 2.9, $fn = 14);
    }
    color(PITCHC) translate([0, 0, 0.15]) cylinder(h = 0.02, r = 2.6, $fn = 14);
    // 球门
    color(WHITEC) for (sx = [-1, 1])
    {
        translate([sx * 15.7, 1.9, 0.7]) boxc([0.12, 0.12, 1.4]);
        translate([sx * 15.7, -1.9, 0.7]) boxc([0.12, 0.12, 1.4]);
        translate([sx * 15.7, 0, 1.4]) boxc([0.12, 3.9, 0.12]);
    }
    // 北侧四级橙色看台
    for (i = [0 : 3])
        color(ORANGEC) translate([0, 11.6 + i * 0.85, 0.3 + i * 0.42]) boxc([26, 0.85, 0.6 + i * 0.84]);
    color(GREYC) translate([0, 14.6, 0]) boxc([26, 0.4, 4.2]);
    // 灯杆
    for (sx = [-1, 1])
    {
        color(DGREYC) translate([sx * 16.8, 10.8, 0]) cylinder(h = 7.5, r = 0.18, $fn = 6);
        color([0.97, 0.93, 0.75]) translate([sx * 16.8, 10.4, 7.4]) rotate([18, 0, 0]) boxc([1.6, 0.2, 1.0]);
    }
}

// ================= 沙滩小品 =================
module beach_umbrella(c = REDC)
{
    color(WHITEC) cylinder(h = 2.0, r = 0.06, $fn = 6);
    color(c) translate([0, 0, 1.55]) cylinder(h = 0.65, r1 = 1.35, r2 = 0.06, $fn = 8);
    color(WHITEC) translate([0, 0, 2.18]) sphere(r = 0.07, $fn = 6);
}

module beach_lounger(c = [0.28, 0.50, 0.76])
{
    color(WHITEC) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.75, sy * 0.35, 0.16]) boxc([0.10, 0.10, 0.32]);
    color(c) translate([-0.25, 0, 0.36]) boxc([1.45, 0.85, 0.10]);
    color(c) translate([0.78, 0, 0.62]) rotate([0, -38, 0]) boxc([0.85, 0.85, 0.10]);
}

module beach_towel(c = YELLOWC)
{
    color(c) slab(1.0, 1.9, 0.05);
    color(WHITEC) translate([0, 0.7, 0.05]) boxc([1.0, 0.3, 0.052]);
}

// 救生塔（高脚小屋 + 坡道）
module beach_lifeguard()
{
    color(WHITEC) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 1.0, sy * 0.9, 1.0]) boxc([0.18, 0.18, 2.0]);
    color(REDC) translate([0, 0, 2.0]) slab(2.8, 2.4, 0.18);
    color(WHITEC) translate([0, 0.2, 2.18]) slab(2.4, 2.0, 1.5);
    color(REDC) translate([0, 0.2, 3.68]) rotate([90, 0, 90]) linear_extrude(2.8, center = true) polygon([[-1.3, 0], [1.3, 0], [0, 0.8]]);
    color(GLASSL) translate([0, -0.82, 2.95]) boxc([1.5, 0.08, 0.7]);
    color(OAKC) translate([0, -2.2, 1.0]) rotate([-38, 0, 0]) boxc([0.9, 3.2, 0.10]);
    color(REDC) translate([1.15, 0.2, 4.1]) cylinder(h = 0.9, r = 0.05, $fn = 6);
    color(ORANGEC) translate([1.4, 0.2, 4.75]) boxc([0.55, 0.04, 0.4]);
}

// 海滩小屋（更衣/小卖，front=-y）
module beach_hut(c = TEALC)
{
    color(c) slab(3.2, 2.6, 2.3);
    color(WHITEC) translate([0, -1.32, 1.0]) boxc([0.9, 0.08, 1.9]);
    color(WHITEC) translate([0, 0, 2.3]) rotate([90, 0, 90]) linear_extrude(3.6, center = true) polygon([[-1.5, 0], [1.5, 0], [0, 0.9]]);
    color(WHITEC) for (sx = [-1, 1]) translate([sx * 1.0, -1.32, 1.75]) boxc([0.7, 0.06, 0.7]);
}

// 木栈桥段（沿 +y 延伸 len；板面顶 z≈0.5，桩入水）
module beach_pier(len = 56, w = 6)
{
    for (yy = [0.7 : 1.4 : len])
        color(OAKC) translate([0, yy, 0.38]) boxc([w, 1.25, 0.22]);
    for (yy = [2 : 7 : len])
        color([0.55, 0.40, 0.24]) for (sx = [-1, 1])
            translate([sx * (w / 2 - 0.4), yy, -2.4]) cylinder(h = 2.9, r = 0.28, $fn = 7);
    // 系船柱
    for (yy = [10 : 14 : len])
        color(DARKC) for (sx = [-1, 1]) translate([sx * (w / 2 - 0.5), yy, 0.5]) cylinder(h = 0.55, r = 0.18, $fn = 7);
}

// ================= 车辆（车头 +x，轮底 z=0） =================
module veh_wheel(r = 0.34, w = 0.24)
{
    color(DARKC) rotate([90, 0, 0]) cylinder(h = w, r = r, center = true, $fn = 10);
}

module veh_car(c = [0.85, 0.26, 0.20])
{
    color(c) translate([0, 0, 0.62]) boxc([4.0, 1.75, 0.6]);
    color(c) translate([-0.2, 0, 1.18]) boxc([2.1, 1.66, 0.52]);
    color(GLASSL) translate([-0.2, 0, 1.18]) boxc([1.85, 1.74, 0.38]);
    color(MARKC) for (sy = [-1, 1]) translate([2.01, 0.58 * sy, 0.70]) boxc([0.05, 0.3, 0.15]);
    color([0.80, 0.15, 0.12]) for (sy = [-1, 1]) translate([-2.01, 0.58 * sy, 0.70]) boxc([0.05, 0.3, 0.15]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.25 * sx, 0.9 * sy, 0.34]) veh_wheel();
}

module veh_taxi()
{
    veh_car(YELLOWC);
    color(DARKC) translate([-0.2, 0, 1.50]) boxc([0.62, 0.30, 0.18]);
}

module veh_bus(c = [0.28, 0.50, 0.76])
{
    color(c) translate([0, 0, 1.35]) boxc([7.6, 2.25, 1.95]);
    color(GLASSL) translate([0, 0, 1.80]) boxc([7.2, 2.31, 0.72]);
    color(GLASSL) translate([3.76, 0, 1.55]) boxc([0.14, 1.85, 0.95]);
    color(MARKC) translate([0, 0, 0.62]) boxc([7.62, 2.27, 0.42]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([2.5 * sx, 1.0 * sy, 0.42]) veh_wheel(0.42, 0.3);
}

module veh_truck_box(cab = [0.30, 0.55, 0.80], box = WHITEC)
{
    color(cab) translate([2.3, 0, 1.0]) boxc([1.6, 2.1, 1.5]);
    color(GLASSL) translate([3.04, 0, 1.28]) boxc([0.16, 1.8, 0.55]);
    color(box) translate([-0.6, 0, 1.45]) boxc([4.2, 2.25, 2.1]);
    for (sx = [2.3, -1.8], sy = [-1, 1]) translate([sx, 1.0 * sy, 0.42]) veh_wheel(0.42, 0.3);
}

// 集装箱卡车（拖头 + 板车 + 彩色箱）
module veh_truck_ct(seed = 0)
{
    color(REDC) translate([3.3, 0, 1.1]) boxc([1.8, 2.2, 1.7]);
    color(GLASSL) translate([4.14, 0, 1.45]) boxc([0.16, 1.9, 0.6]);
    color(DGREYC) translate([-0.8, 0, 0.85]) boxc([7.0, 2.2, 0.35]);
    color(ctn_c(seed)) translate([-0.8, 0, 2.05]) boxc([6.4, 2.3, 2.1]);
    color(ctn_c(seed + 3)) translate([-0.81, 0, 2.05]) boxc([6.42, 2.0, 1.8]);
    for (sx = [3.3, -2.6, -4.0], sy = [-1, 1]) translate([sx, 1.0 * sy, 0.42]) veh_wheel(0.42, 0.3);
}

// ================= 船舶（船头 +x，吃水线 z≈0 放于海面） =================
// 快艇 + 尾迹
module boat_speed(c = WHITEC, wake = true)
{
    color(c) translate([0, 0, 0.35]) boxc([4.0, 1.5, 0.7]);
    color(c) hull()
    {
        translate([2.0, 0, 0.35]) boxc([0.1, 1.5, 0.7]);
        translate([3.1, 0, 0.55]) boxc([0.1, 0.4, 0.3]);
    }
    color(GLASSD) translate([0.9, 0, 0.85]) rotate([0, -18, 0]) boxc([0.10, 1.2, 0.55]);
    color(DGREYC) translate([-0.6, 0, 0.78]) boxc([1.2, 0.9, 0.20]);
    color(DARKC) translate([-2.05, 0, 0.45]) boxc([0.3, 0.5, 0.55]);
    if (wake)
    {
        color(FOAMC) translate([-3.4, 0, 0.04]) boxc([2.6, 0.9, 0.06]);
        color(FOAMC) translate([-5.6, 0, 0.03]) boxc([1.8, 1.6, 0.05]);
        color(FOAMC) translate([2.4, 0, 0.04]) boxc([1.2, 0.5, 0.05]);
    }
}

// 帆船
module boat_sail(hc = [0.24, 0.32, 0.48])
{
    color(hc) translate([0, 0, 0.30]) boxc([3.6, 1.2, 0.6]);
    color(hc) hull()
    {
        translate([1.8, 0, 0.30]) boxc([0.1, 1.2, 0.6]);
        translate([2.6, 0, 0.45]) boxc([0.1, 0.3, 0.3]);
    }
    color(OAKC) translate([0, 0, 0.6]) boxc([3.2, 0.8, 0.10]);
    color(GREYC) translate([0.3, 0, 0.6]) cylinder(h = 4.6, r = 0.06, $fn = 6);
    color(WHITEC) translate([0.42, 0.03, 0.9]) rotate([90, 0, 0]) linear_extrude(0.05) polygon([[0, 0], [1.7, 0], [0, 3.9]]);
    color(WHITEC) translate([0.18, -0.03, 1.1]) rotate([90, 0, 180]) linear_extrude(0.05) polygon([[0, 0], [1.3, 0], [0, 3.3]]);
}

// 集装箱货轮（参数化长度/配色；船头 +x）
module boat_ship(len = 46, hc = [0.62, 0.24, 0.20], seed = 0)
{
    W = len * 0.20;
    // 船体 + 船头收分
    color(hc) translate([-len * 0.06, 0, 1.1]) boxc([len * 0.82, W, 2.6]);
    color(hc) hull()
    {
        translate([len * 0.35, 0, 1.1]) boxc([0.2, W, 2.6]);
        translate([len * 0.50, 0, 1.5]) boxc([0.2, W * 0.25, 1.8]);
    }
    color(DARKC) translate([-len * 0.06, 0, -0.25]) boxc([len * 0.82 + 0.04, W + 0.04, 0.9]);
    color(WHITEC) translate([-len * 0.06, 0, 2.42]) boxc([len * 0.82, W + 0.06, 0.18]);
    // 艉楼
    color(WHITEC) translate([-len * 0.38, 0, 4.4]) boxc([len * 0.12, W * 0.85, 6.0]);
    color(GLASSD) translate([-len * 0.38, 0, 6.6]) boxc([len * 0.12 + 0.06, W * 0.75, 0.8]);
    color(YELLOWC) translate([-len * 0.33, 0, 7.6]) cylinder(h = 1.6, r = 0.5, $fn = 8);
    // 集装箱堆
    for (i = [0 : 4], sy = [-1, 1], lv = [0 : 1 + rnd(seed + i, 2)])
        color(ctn_c(seed + i * 2 + lv + sy))
            translate([len * 0.26 - i * len * 0.105, sy * W * 0.24, 2.5 + 1.05 + lv * 2.1]) boxc([len * 0.095, W * 0.42, 2.1]);
    // 船首桅
    color(GREYC) translate([len * 0.44, 0, 2.4]) cylinder(h = 2.2, r = 0.10, $fn = 6);
}

module prop_buoy(c = REDC)
{
    color(c) cylinder(h = 0.85, r1 = 0.42, r2 = 0.16, $fn = 8);
    color(WHITEC) translate([0, 0, 0.85]) sphere(r = 0.14, $fn = 6);
}

// ================= 空中 =================
// 直升机（fly=true 时旋翼为快转圆盘）
module veh_helicopter(c = REDC, fly = true)
{
    color(c) translate([0.2, 0, 0.95]) boxc([2.6, 1.3, 1.1]);
    color(GLASSD) translate([1.45, 0, 1.0]) boxc([0.3, 1.1, 0.8]);
    color(c) translate([-1.9, 0, 1.25]) boxc([2.2, 0.3, 0.3]);
    color(c) translate([-3.0, 0, 1.7]) boxc([0.25, 0.08, 0.9]);
    color(DGREYC) translate([-3.0, 0.1, 1.95]) rotate([90, 0, 0]) cylinder(h = 0.2, r = 0.42, $fn = 8);
    color(DGREYC) for (sy = [-1, 1])
    {
        translate([0.2, 0.62 * sy, 0.18]) boxc([2.4, 0.12, 0.12]);
        translate([0.2, 0.62 * sy, 0.32]) boxc([0.10, 0.10, 0.35]);
    }
    color(DGREYC) translate([0.2, 0, 1.5]) cylinder(h = 0.35, r = 0.14, $fn = 6);
    if (fly)
        color([0.55, 0.57, 0.60, 0.22]) translate([0.2, 0, 1.82]) cylinder(h = 0.05, r = 2.3, $fn = 16);
    else
    {
        color(DGREYC) translate([0.2, 0, 1.82]) boxc([5.2, 0.22, 0.06]);
        color(DGREYC) translate([0.2, 0, 1.82]) boxc([0.22, 5.2, 0.06]);
    }
}

// 海鸥（白色双翼）
module prop_gull()
{
    color(WHITEC)
    {
        translate([0.45, 0, 0.12]) rotate([0, -14, 0]) boxc([0.95, 0.1, 0.05]);
        translate([-0.45, 0, 0.12]) rotate([0, 14, 0]) boxc([0.95, 0.1, 0.05]);
        boxc([0.32, 0.30, 0.12]);
    }
}

// ================= 组团（block_*：48 x 42 街区台面 + 内容；中心原点） =================
BW = 48;    // 台面宽
BD = 42;    // 台面深

// 住宅组团：2 排 x 3 户，泳池/车道/树篱/行道树，seed 驱动配色与朝向
module block_houses(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(GRASSC) translate([0, 0, CURB]) slab(BW - 3.5, BD - 3.5, 0.10);
    for (i = [0 : 5])
    {
        lx = -15 + (i % 3) * 15;
        row = floor(i / 3);
        ly = (row == 0) ? -10.5 : 10.5;
        translate([lx, ly, CURB + 0.10])
        {
            rotate([0, 0, (row == 0) ? 0 : 180]) house(house_c(seed * 7 + i), roof_c(seed * 5 + i * 3));
            // 车道（通向所在街道）
            color(PAVEC) translate([5.6, (row == 0) ? -7.2 : 7.2, 0]) boxc([2.6, 7.5, 0.06]);
            if (rnd(seed * 11 + i, 3) == 0)
                translate([5.4, (row == 0) ? -8.2 : 8.2, 0.04]) rotate([0, 0, (row == 0) ? 90 : -90]) veh_car(car_c(seed * 13 + i));
            // 后院泳池 / 树
            if (rnd(seed * 3 + i, 3) == 0) translate([-1.5, (row == 0) ? 5.8 : -5.8, 0]) prop_pool();
            else translate([-2.5, (row == 0) ? 5.6 : -5.6, 0]) nature_tree(0.9 + 0.12 * rnd(seed + i, 3), seed + i);
        }
    }
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 20.5, sy * 17, CURB + 0.10]) nature_tree(1.05 + 0.1 * rnd(seed + sx + sy, 3), seed * 2 + sx);
    for (sx = [-1, 1]) translate([sx * 10, 0, CURB + 0.10]) nature_hedge(7);
    translate([22, 0, CURB]) prop_bin();
}

// 公寓组团：四角公寓楼（层数/配色/屋顶花园随 seed），中庭绿地 + 游乐设施
module block_apartments(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(PLAZAC) translate([0, 0, CURB]) slab(BW - 6, BD - 6, 0.05);
    for (i = [0 : 3])
    {
        sx = (i % 2 == 0) ? -1 : 1;
        sy = (i < 2) ? -1 : 1;
        translate([sx * 13, sy * 11.5, CURB + 0.05])
            rotate([0, 0, (sy < 0) ? 0 : 180])
                bldg_apartment(17, 11, 4 + rnd(seed * 5 + i, 3), apt_c(seed * 3 + i * 2),
                               rnd(seed * 7 + i, 3) == 0, seed + i);
    }
    // 中庭
    color(GRASSC) translate([0, 0, CURB + 0.05]) slab(13, 11, 0.08);
    translate([-4, 2.5, CURB + 0.13]) nature_tree(1.0, seed);
    translate([4, -2.5, CURB + 0.13]) nature_tree(0.85, seed + 1);
    // 滑梯 + 沙坑
    translate([2.5, 2.5, CURB + 0.13])
    {
        color(SANDC) slab(3.4, 2.6, 0.10);
        color(YELLOWC) translate([-0.9, 0, 0.1]) boxc([0.14, 0.14, 1.5]);
        color(YELLOWC) translate([-0.9, 0.6, 0.1]) boxc([0.14, 0.14, 1.5]);
        color(REDC) translate([0.1, 0.3, 0.85]) rotate([0, 32, 0]) boxc([2.2, 0.6, 0.08]);
    }
    for (sy = [-1, 1]) translate([0, sy * 4.2, CURB + 0.13]) prop_bench();
    translate([0, 0, CURB + 0.05])
    {
        translate([21, -14, 0]) rotate([0, 0, 90]) veh_car(car_c(seed * 17 + 1));
        translate([21, 14, 0]) rotate([0, 0, 90]) veh_car(car_c(seed * 17 + 4));
    }
    translate([-21.5, 0, CURB]) prop_bin();
}

// CBD 组团 A：圆柱停机坪塔 + 蓝玻璃塔 + 砖塔 + 广场（hs 缩放高度）
module block_downtown_a(seed = 0, hs = 1.0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(PLAZAC) translate([0, 0, CURB]) slab(BW - 5, BD - 5, 0.05);
    translate([-12, 7, CURB + 0.05]) bldg_tower_round(6.5, 34 * hs, true);
    translate([11, -8, CURB + 0.05]) bldg_tower_glass(13, 13, 38 * hs, GLASSB, WHITEC, seed);
    translate([12, 12.5, CURB + 0.05]) rotate([0, 0, 180]) bldg_tower_brick(13, 10, max(6, floor(9 * hs)), [0.70, 0.34, 0.27], seed + 1);
    translate([-14, -13, CURB + 0.05]) prop_fountain();
    for (i = [0 : 2]) translate([-4 + i * 4, -17.5, CURB + 0.05]) prop_flag(umb_c(i + 6));
    for (i = [0 : 2])
    {
        color(WHITEC) translate([-6 + i * 6, -13.5, CURB + 0.05]) slab(1.6, 1.6, 0.5);
        translate([-6 + i * 6, -13.5, CURB + 0.55]) nature_tree(0.8, seed + i);
    }
    translate([-20, -5, CURB + 0.05]) prop_bench();
    translate([-20, -8, CURB + 0.05]) prop_bin();
}

// CBD 组团 B：退台尖塔（地标）+ 写字楼 + 砖塔 + 深蓝塔 + 城市标识（hs 缩放高度）
module block_downtown_b(seed = 0, hs = 1.0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(PLAZAC) translate([0, 0, CURB]) slab(BW - 5, BD - 5, 0.05);
    translate([-11, 9, CURB + 0.05]) bldg_tower_spire(13, 46 * hs, GLASSD);
    translate([12, 9.5, CURB + 0.05]) bldg_office(16, 12, 22 * hs, WHITEC, seed);
    translate([12, -11, CURB + 0.05]) bldg_tower_brick(13, 10, max(5, floor(8 * hs)), [0.78, 0.44, 0.30], seed + 2);
    translate([-12, -12, CURB + 0.05]) bldg_tower_glass(11, 10, 26 * hs, GLASSL, DGREYC, seed + 3);
    translate([0, -18.5, CURB + 0.05]) prop_city_sign("HARBOR CITY");
    translate([0, 0, CURB + 0.05])
    {
        translate([0, -8, 0]) prop_fountain();
        translate([-2, 4, 0]) nature_tree(0.9, seed);
        translate([2.5, 1, 0]) nature_tree(0.75, seed + 1);
    }
    translate([21, 0, CURB + 0.05]) prop_lamp();
    translate([-21, 2, CURB + 0.05]) prop_lamp();
}

// 海滨酒店组团：酒店 + 泳池区 + 棕榈 + 小停车场
module block_hotel(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(GRASSC) translate([-2, 14, CURB]) slab(40, 10, 0.08);
    translate([0, 8, CURB]) bldg_hotel(27, 13, 6);
    translate([-9, -9, CURB]) prop_pool(9, 6);
    for (i = [0 : 3]) translate([-14 + i * 3.4, -15.5, CURB]) rotate([0, 0, 90]) beach_lounger(umb_c(seed + i));
    translate([-15, -12, CURB]) beach_umbrella(umb_c(seed + 1));
    translate([-2, -13, CURB]) beach_umbrella(umb_c(seed + 4));
    translate([-19, -5, CURB]) nature_palm(1.1, 4);
    translate([4, -5.5, CURB]) nature_palm(0.95, -6);
    translate([19, 3, CURB]) nature_palm(1.05, 8);
    translate([13, -12, CURB]) road_parking(14, 9, 3);
    translate([10, -12.5, CURB + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed * 7));
    translate([15, -12.5, CURB + 0.06]) rotate([0, 0, 90]) veh_taxi();
    translate([-21, -16, CURB]) prop_lamp();
}

// 超市组团：蓝色大盒子 + 门前停车场 + 购物车棚
module block_market(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    translate([-4, 9, CURB]) bldg_market(26, 16, 7);
    translate([-4, -10, CURB]) road_parking(30, 14, 5);
    translate([-13, -11, CURB + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed * 3));
    translate([-7, -11, CURB + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed * 3 + 2));
    translate([5, -11, CURB + 0.06]) rotate([0, 0, 90]) veh_truck_box([0.30, 0.55, 0.80]);
    // 购物车棚
    color(GREYC) translate([13, 1, CURB]) boxc([0.1, 4.0, 1.4]);
    color(GREYC) translate([15.5, 1, CURB + 1.45]) boxc([5.2, 4.2, 0.10]);
    for (i = [0 : 2]) color(GREYC) translate([14.5 + i * 0.8, 1, CURB + 0.4]) boxc([0.7, 1.0, 0.8]);
    color(GRASSC) translate([18, 12, CURB]) slab(10, 14, 0.08);
    translate([18, 13, CURB + 0.08]) nature_tree(1.15, seed);
    translate([15, 7, CURB + 0.08]) nature_tree(0.9, seed + 2);
    translate([-20.5, -2, CURB]) prop_lamp();
}

// 商业街组团：临街商铺排 + 写字楼 + 小吃亭广场
module block_shops(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(PLAZAC) translate([0, 2, CURB]) slab(BW - 8, 8, 0.05);
    translate([-11, -10, CURB]) bldg_shop_row(20, 10, 5, seed);
    translate([13, -10, CURB]) bldg_shop_row(16, 10, 4.6, seed + 3);
    translate([-12, 12, CURB]) rotate([0, 0, 180]) bldg_office(14, 10, 13, CREAMC, seed);
    translate([8, 12, CURB]) rotate([0, 0, 180]) bldg_shop_row(14, 9, 5.4, seed + 6);
    translate([19, 9, CURB]) prop_kiosk(umb_c(seed));
    translate([0, 2, CURB + 0.05])
    {
        translate([-6, 0, 0]) prop_bench();
        translate([6, 0, 0]) prop_bench();
        translate([0, 0, 0]) prop_bin([0.28, 0.50, 0.76]);
    }
    translate([21, -2, CURB]) nature_tree(0.95, seed + 1);
    translate([-21, 4, CURB]) nature_tree(0.85, seed + 4);
}

// 停车广场组团：大停车场 + 棕榈广场 + 喷泉
module block_plaza_parking(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    translate([-7, -5, CURB]) road_parking(26, 16, 4);
    translate([-15, -6, CURB + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed));
    translate([-9, -6, CURB + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed + 2));
    translate([-2, -6, CURB + 0.06]) rotate([0, 0, 90]) veh_taxi();
    translate([4, -6, CURB + 0.06]) rotate([0, 0, 90]) veh_car(car_c(seed + 4));
    color(PLAZAC) translate([15, 0, CURB]) slab(14, 36, 0.05);
    translate([15, 9, CURB + 0.05]) prop_fountain();
    translate([15, -8, CURB + 0.05]) prop_kiosk(REDC);
    translate([11, 1, CURB + 0.05]) prop_bench();
    translate([19, 1, CURB + 0.05]) prop_bench();
    translate([11, 16, CURB + 0.05]) nature_palm(1.0, 5);
    translate([19, 16, CURB + 0.05]) nature_palm(1.1, -4);
    translate([19, -15, CURB + 0.05]) nature_palm(0.9, 7);
    color(GRASSC) translate([-7, 13, CURB]) slab(26, 10, 0.08);
    translate([-14, 13, CURB + 0.08]) nature_tree(1.1, seed + 1);
    translate([-2, 14, CURB + 0.08]) nature_tree(0.9, seed + 5);
    translate([-21, -14, CURB]) prop_lamp();
}

// 球场组团：足球场 + 周边绿化
module block_soccer(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(GRASSC) translate([0, 0, CURB]) slab(BW - 3.5, BD - 3.5, 0.08);
    translate([0, -2, CURB + 0.08]) bldg_soccer_field();
    for (sx = [-1, 1]) translate([sx * 20.5, -16, CURB + 0.08]) nature_tree(1.0, seed + sx);
    translate([-20.5, 16, CURB + 0.08]) nature_tree(1.2, seed + 3);
    translate([20.5, 16, CURB + 0.08]) nature_tree(0.9, seed + 5);
    translate([18, -18.5, CURB]) prop_bin();
}

// 加油站组团：加油站 + 一栋公寓 + 绿地
module block_gas(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    translate([-7, -8, CURB]) bldg_gas_station();
    translate([12, 10, CURB]) rotate([0, 0, 180]) bldg_apartment(17, 11, 4, apt_c(seed * 3 + 1), false, seed);
    color(GRASSC) translate([-12, 13, CURB]) slab(20, 12, 0.08);
    translate([-16, 13, CURB + 0.08]) nature_tree(1.15, seed);
    translate([-7, 15, CURB + 0.08]) nature_tree(0.9, seed + 2);
    translate([-8, 11, CURB + 0.08]) nature_hedge(6);
    translate([18, -14, CURB + 0.04]) rotate([0, 0, 90]) veh_car(car_c(seed * 5));
    translate([21, -5, CURB]) prop_bin();
}

// 公园组团：池塘 + 喷泉 + 十字步道 + 树群 + 小卖亭
module block_park(seed = 0)
{
    color(PAVEC) slab(BW, BD, CURB);
    color(GRASSC) translate([0, 0, CURB]) slab(BW - 3.5, BD - 3.5, 0.10);
    color([0.90, 0.87, 0.78]) translate([0, 0, CURB + 0.10]) slab(3.2, BD - 3.5, 0.04);
    color([0.90, 0.87, 0.78]) translate([0, 0, CURB + 0.10]) slab(BW - 3.5, 3.2, 0.04);
    // 池塘（画家叠层）
    translate([-12, 9, CURB + 0.10])
    {
        color([0.90, 0.87, 0.78]) cylinder(h = 0.06, r = 7.2, $fn = 9);
        color(POOLC) translate([0, 0, 0.06]) cylinder(h = 0.05, r = 6.4, $fn = 9);
        color(FOAMC) translate([-2, 1.5, 0.12]) boxc([1.6, 0.3, 0.02]);
    }
    translate([12, 10, CURB + 0.14]) prop_fountain();
    for (i = [0 : 7])
    {
        tx = -18 + rnd(seed * 3 + i, 36);
        if (abs(tx) > 3)
            translate([tx, -16 + rnd(seed * 7 + i, 13), CURB + 0.14])
                nature_tree(0.9 + 0.18 * rnd(seed + i, 4), seed + i);
    }
    translate([18, -2, CURB + 0.14]) nature_pine(1.2);
    translate([-20, -3, CURB + 0.14]) nature_pine(1.0);
    for (sx = [-1, 1]) translate([sx * 4.5, -3.4, CURB + 0.14]) prop_bench();
    translate([4.5, 3.4, CURB + 0.14]) rotate([0, 0, 180]) prop_bench();
    translate([16, -15, CURB + 0.14]) prop_kiosk(TEALC);
    translate([-4.5, 3.8, CURB + 0.14]) prop_bin();
    for (sx = [-1, 1]) translate([sx * 13, 17.6, CURB + 0.10]) nature_hedge(12);
}

// ======================== 地形与海面 ========================
// 平面布局常量（12 列 x 6 行街区）
VX = [-280, -224, -168, -112, -56, 0, 56, 112, 168, 224, 280];          // 纵路中心
HY = [-30, 20, 70, 120, 170, 220, 270];                                 // 横路中心
CX = [-308, -252, -196, -140, -84, -28, 28, 84, 140, 196, 252, 308];    // 街区列中心
RY = [-5, 45, 95, 145, 195, 245];                                       // 街区行中心

module ground_all()
{
    // 展台底座
    color(BASEC) translate([0, 90, -2.6]) boxc([680, 512, 2.8]);
    // 城市基面（含滨海步道，y∈[-40,274]）
    color(PAVEC) translate([0, 117, -1.2]) translate([0, 0, 0.6]) boxc([672, 314, 1.2]);
    // 北部森林草甸 (y∈[274,344])
    color(GRASSC) translate([0, 309, 0]) slab(672, 70, 0.42);
    // 滨海步道面层
    color([0.90, 0.87, 0.78]) translate([0, -37, 0]) slab(672, 6, 0.16);
    // 沙滩（干沙 → 湿沙台阶）
    color(SANDC) translate([0, -55, -1.2]) translate([0, 0, 0.575]) boxc([672, 30, 1.15]);
    color(SANDW) translate([0, -74, -1.2]) translate([0, 0, 0.425]) boxc([672, 8, 0.85]);
    // 海面 + 浅水带 + 深水色块
    color(SEAC) translate([0, -121, -1.2]) translate([0, 0, 0.325]) boxc([672, 86, 0.65]);
    color(SEAL) translate([0, -81.5, -0.56]) boxc([672, 7, 0.07]);
    color(SEAD) translate([-230, -125, -0.555]) boxc([90, 38, 0.02]);
    color(SEAD) translate([-20, -140, -0.555]) boxc([120, 30, 0.02]);
    color(SEAD) translate([150, -118, -0.555]) boxc([70, 26, 0.02]);
    color(SEAD) translate([290, -135, -0.555]) boxc([85, 30, 0.02]);
    color(SEAD) translate([-90, -103, -0.555]) boxc([60, 16, 0.02]);
    // 岸线浪花
    color(FOAMC) translate([0, -77.6, -0.40]) boxc([672, 0.5, 0.04]);
    for (i = [0 : 24])
        color(FOAMC) translate([-320 + i * 27 + rnd(i, 9), -79.5 - rnd(i, 3), -0.50]) boxc([7 + rnd(i, 5), 0.4, 0.03]);
}

// ======================== 道路网 ========================
function nearr(x, vs) = min([for (v = vs) abs(x - v)]);

module roads_all()
{
    for (y = HY) translate([0, y, 0]) road_x(672);
    for (x = VX) translate([x, 120, 0]) road_y(292);
    // 虚线（避开路口）
    for (y = HY, x = [-330 : 6 : 330])
        if (nearr(x, VX) > 7) color(MARKC) translate([x, y, RT + 0.01]) boxc([2.8, 0.3, 0.02]);
    for (x = VX, y = [-22 : 6 : 262])
        if (nearr(y, HY) > 7) color(MARKC) translate([x, y, RT + 0.01]) boxc([0.3, 2.8, 0.02]);
    // 路口斑马线
    for (x = VX, y = HY)
    {
        translate([x, y - 5.4, 0]) road_crosswalk();
        translate([x, y + 5.4, 0]) road_crosswalk();
        translate([x - 5.4, y, 0]) rotate([0, 0, 90]) road_crosswalk();
        translate([x + 5.4, y, 0]) rotate([0, 0, 90]) road_crosswalk();
    }
    // 滨海路灯 + CBD 路灯
    for (i = [0 : 11]) translate([-308 + i * 56, -25.2, 0]) rotate([0, 0, 180]) prop_lamp();
    for (i = [0 : 11]) translate([-280 + i * 56, 24.8, 0]) rotate([0, 0, 180]) prop_lamp();
    for (i = [0 : 11]) translate([-308 + i * 56, 74.8, 0]) rotate([0, 0, 180]) prop_lamp();
}

// ======================== 车流（参数化撒布，避开路口） ========================
module veh_pick(i)
{
    t = rnd(i, 12);
    if (t <= 6) veh_car(car_c(i));
    else if (t == 7) veh_taxi();
    else if (t == 8) veh_bus((rnd(i, 2) == 0) ? TEALC : [0.28, 0.50, 0.76]);
    else if (t <= 10) veh_truck_box((rnd(i, 2) == 0) ? [0.30, 0.55, 0.80] : [0.85, 0.26, 0.20]);
    else veh_truck_ct(i);
}

module traffic_x(y, seed, n = 10)
{
    for (k = [0 : n - 1])
    {
        tx = -320 + k * (640 / n) + rnd(seed * 7 + k, 17);
        if (nearr(tx, VX) > 9)
        {
            east = (rnd(seed * 3 + k, 2) == 0);
            translate([tx, y + (east ? -2 : 2), RT]) rotate([0, 0, east ? 0 : 180]) veh_pick(seed * 13 + k);
        }
    }
}

module traffic_y(x, seed, n = 5)
{
    for (k = [0 : n - 1])
    {
        ty = -16 + k * (270 / n) + rnd(seed * 7 + k, 19);
        if (nearr(ty, HY) > 9)
        {
            north = (rnd(seed * 5 + k, 2) == 0);
            translate([x + (north ? 2 : -2), ty, RT]) rotate([0, 0, north ? 90 : -90]) veh_pick(seed * 11 + k);
        }
    }
}

// ======================== 总装 ========================
ground_all();
roads_all();

// ---- Row A 滨海休闲带 (y=-5) ----
translate([CX[0], RY[0], 0]) block_houses(1);
translate([CX[1], RY[0], 0]) block_apartments(2);
translate([CX[2], RY[0], 0]) block_market(3);
translate([CX[3], RY[0], 0]) block_shops(4);
translate([CX[4], RY[0], 0]) block_plaza_parking(5);
translate([CX[5], RY[0], 0]) block_soccer(6);
translate([CX[6], RY[0], 0]) block_hotel(7);
translate([CX[7], RY[0], 0]) block_apartments(8);
translate([CX[8], RY[0], 0]) block_shops(9);
translate([CX[9], RY[0], 0]) block_plaza_parking(10);
translate([CX[10], RY[0], 0]) block_hotel(11);
translate([CX[11], RY[0], 0]) block_houses(12);

// ---- Row B 中央 CBD 天际线 (y=45)：hs 错落 ----
translate([CX[0], RY[1], 0]) block_houses(13);
translate([CX[1], RY[1], 0]) block_apartments(14);
translate([CX[2], RY[1], 0]) block_apartments(15);
translate([CX[3], RY[1], 0]) block_downtown_a(16, 1.0);
translate([CX[4], RY[1], 0]) block_downtown_b(17, 1.12);
translate([CX[5], RY[1], 0]) rotate([0, 0, 180]) block_downtown_a(18, 1.25);
translate([CX[6], RY[1], 0]) block_downtown_b(19, 0.95);
translate([CX[7], RY[1], 0]) rotate([0, 0, 180]) block_downtown_a(20, 0.85);
translate([CX[8], RY[1], 0]) block_apartments(21);
translate([CX[9], RY[1], 0]) block_apartments(22);
translate([CX[10], RY[1], 0]) block_apartments(23);
translate([CX[11], RY[1], 0]) block_houses(24);

// ---- Row C CBD 北缘 + 公寓 (y=95) ----
translate([CX[0], RY[2], 0]) block_houses(25);
translate([CX[1], RY[2], 0]) block_houses(26);
translate([CX[2], RY[2], 0]) block_gas(27);
translate([CX[3], RY[2], 0]) block_apartments(28);
translate([CX[4], RY[2], 0]) rotate([0, 0, 180]) block_downtown_b(29, 0.80);
translate([CX[5], RY[2], 0]) rotate([0, 0, 180]) block_downtown_a(30, 0.70);
translate([CX[6], RY[2], 0]) block_apartments(31);
translate([CX[7], RY[2], 0]) block_apartments(32);
translate([CX[8], RY[2], 0]) block_gas(33);
translate([CX[9], RY[2], 0]) block_apartments(34);
translate([CX[10], RY[2], 0]) block_houses(35);
translate([CX[11], RY[2], 0]) block_houses(36);

// ---- Row D 公园 + 公寓带 (y=145) ----
translate([CX[0], RY[3], 0]) block_houses(37);
translate([CX[1], RY[3], 0]) block_apartments(38);
translate([CX[2], RY[3], 0]) block_park(39);
translate([CX[3], RY[3], 0]) block_apartments(40);
translate([CX[4], RY[3], 0]) block_apartments(41);
translate([CX[5], RY[3], 0]) block_park(42);
translate([CX[6], RY[3], 0]) block_apartments(43);
translate([CX[7], RY[3], 0]) block_apartments(44);
translate([CX[8], RY[3], 0]) block_park(45);
translate([CX[9], RY[3], 0]) block_apartments(46);
translate([CX[10], RY[3], 0]) block_houses(47);
translate([CX[11], RY[3], 0]) block_houses(48);

// ---- Row E 北住宅带 (y=195) ----
translate([CX[0], RY[4], 0]) block_houses(49);
translate([CX[1], RY[4], 0]) block_houses(50);
translate([CX[2], RY[4], 0]) block_apartments(51);
translate([CX[3], RY[4], 0]) block_houses(52);
translate([CX[4], RY[4], 0]) block_apartments(53);
translate([CX[5], RY[4], 0]) block_houses(54);
translate([CX[6], RY[4], 0]) block_apartments(55);
translate([CX[7], RY[4], 0]) block_houses(56);
translate([CX[8], RY[4], 0]) block_apartments(57);
translate([CX[9], RY[4], 0]) block_houses(58);
translate([CX[10], RY[4], 0]) block_apartments(59);
translate([CX[11], RY[4], 0]) block_houses(60);

// ---- Row F 北郊边缘 (y=245) ----
translate([CX[0], RY[5], 0]) block_apartments(61);
translate([CX[1], RY[5], 0]) block_houses(62);
translate([CX[2], RY[5], 0]) block_houses(63);
translate([CX[3], RY[5], 0]) block_apartments(64);
translate([CX[4], RY[5], 0]) block_houses(65);
translate([CX[5], RY[5], 0]) block_houses(66);
translate([CX[6], RY[5], 0]) block_apartments(67);
translate([CX[7], RY[5], 0]) block_houses(68);
translate([CX[8], RY[5], 0]) block_houses(69);
translate([CX[9], RY[5], 0]) block_apartments(70);
translate([CX[10], RY[5], 0]) block_houses(71);
translate([CX[11], RY[5], 0]) block_apartments(72);

// ---- 北部山地 + 分簇变密度树海 ----
// 主峰（信号塔）+ 东侧次丘；近山簇密、远山稀疏带空地
translate([-60, 307, 0.4]) scale([1.55, 1.08, 1.35]) nature_mountain();
translate([-63.1, 309.2, 63.8]) prop_radio_tower(16);
translate([240, 312, 0.4]) scale([0.75, 0.60, 0.65]) nature_mountain();
for (ci = [0 : 60], cj = [0 : 5])
{
    id = ci * 17 + cj * 5;
    fx = -330 + ci * 11 + rnd(id, 8);
    fy = 278 + cj * 10.5 + rnd(id * 3, 8);
    nearhill = (abs(fx + 60) < 100) || (abs(fx - 240) < 70);   // 近山带 → 密林
    n = min(7, rnd(id, 9) - (nearhill ? 0 : 1));               // 远山带保留空地起伏
    if (n > 0
        && (fx + 60) * (fx + 60) / 2310 + (fy - 307) * (fy - 307) / 1160 > 1.0
        && (fx - 240) * (fx - 240) / 560 + (fy - 312) * (fy - 312) / 360 > 1.05
        && fy < 341)
        for (k = [0 : n - 1])
        {
            tx = fx + rnd(id + k * 3, 11) - 5;
            ty = fy + rnd(id * 2 + k, 9) - 4;
            if (rnd(id + k, 4) == 0)
                translate([tx, ty, 0.42]) nature_pine(1.55 + 0.26 * rnd(id + k, 4));
            else
                translate([tx, ty, 0.42]) nature_tree(1.5 + 0.24 * rnd(id * 3 + k, 4), id + k);
        }
}

// ---- 滨海步道：棕榈 + 长椅 + 候车亭 ----
for (i = [0 : 35])
    translate([-325 + i * 18.5 + rnd(i, 6), -37, 0.16]) rotate([0, 0, rnd(i, 8) * 45]) nature_palm(1.0 + 0.12 * rnd(i, 4), 4 + rnd(i, 6));
for (i = [0 : 11]) translate([-310 + i * 56, -36.2, 0.16]) prop_bench();
translate([-280, -36, 0.16]) prop_bin();
translate([-100, -36, 0.16]) prop_bin();
translate([60, -36, 0.16]) prop_bin();
translate([240, -36, 0.16]) prop_bin();
translate([70, -25.4, CURB]) rotate([0, 0, 180]) prop_bus_shelter();
translate([-210, -25.4, CURB]) rotate([0, 0, 180]) prop_bus_shelter();

// ---- 沙滩（两排伞群 + 散布躺椅/毛巾/沙滩球，避开双栈桥） ----
for (i = [0 : 43])
{
    bx = -325 + i * 15 + rnd(i * 3, 9);
    by = ((i % 2 == 0) ? -47 : -57) - rnd(i * 7, 7);
    if (abs(bx - 150) > 14 && abs(bx + 250) > 12)
    {
        translate([bx, by, 0.05]) beach_umbrella(umb_c(i));
        translate([bx + 1.8, by + 0.4, 0.05]) rotate([0, 0, rnd(i, 7) * 50]) beach_lounger(umb_c(i + 3));
        if (rnd(i, 2) == 0) translate([bx - 1.6, by - 1.2, 0.05]) rotate([0, 0, rnd(i, 5) * 35]) beach_towel(umb_c(i + 1));
        if (rnd(i, 3) == 0) translate([bx - 2.4, by + 1.6, 0.05]) rotate([0, 0, rnd(i, 9) * 40]) beach_lounger(umb_c(i + 5));
        if (rnd(i + 4, 4) == 0) color(umb_c(i + 2)) translate([bx + 3.0, by - 1.8, 0.30]) sphere(r = 0.3, $fn = 8);
    }
}
translate([-280, -52, 0.05]) beach_lifeguard();
translate([-120, -52, 0.05]) beach_lifeguard();
translate([20, -54, 0.05]) beach_lifeguard();
translate([180, -48, 0.05]) beach_lifeguard();
translate([300, -50, 0.05]) beach_lifeguard();
translate([-180, -44, 0.05]) beach_hut([0.84, 0.45, 0.62]);
translate([-55, -44, 0.05]) beach_hut(TEALC);
translate([-50.5, -44, 0.05]) beach_hut(REDC);
translate([95, -44, 0.05]) beach_hut([0.28, 0.50, 0.76]);
translate([260, -44, 0.05]) beach_hut(ORANGEC);
// 沙滩排球 x2
for (vx = [-20, 220])
    translate([vx, -59, 0.05])
    {
        color(GREYC) for (sx = [-1, 1]) translate([sx * 2.6, 0, 0]) cylinder(h = 1.6, r = 0.07, $fn = 6);
        color(WHITEC) translate([0, 0, 1.15]) boxc([5.2, 0.05, 0.65]);
    }

// ---- 双木栈桥码头 + 系泊小船 ----
translate([150, -40, 0]) rotate([0, 0, 180]) beach_pier(56, 6);
translate([143, -83, -0.55]) rotate([0, 0, 90]) boat_sail([0.24, 0.32, 0.48]);
translate([157.5, -87, -0.55]) rotate([0, 0, -90]) boat_speed(WHITEC, false);
translate([144, -92, -0.55]) rotate([0, 0, 85]) boat_sail([0.55, 0.25, 0.22]);
translate([-250, -40, 0]) rotate([0, 0, 180]) beach_pier(44, 5);
translate([-256, -80, -0.55]) rotate([0, 0, 90]) boat_sail([0.30, 0.44, 0.34]);
translate([-244, -85, -0.55]) rotate([0, 0, -90]) boat_speed([0.92, 0.57, 0.22], false);

// ---- 海洋 ----
translate([-60, -128, -0.55]) rotate([0, 0, 6]) boat_ship(46, [0.62, 0.24, 0.20], 1);
translate([-185, -108, -0.55]) rotate([0, 0, -4]) boat_ship(32, [0.20, 0.28, 0.42], 5);
translate([250, -132, -0.55]) rotate([0, 0, -8]) boat_ship(42, [0.20, 0.42, 0.40], 7);
translate([-10, -98, -0.55]) rotate([0, 0, 18]) boat_speed(WHITEC);
translate([70, -112, -0.55]) rotate([0, 0, -35]) boat_speed([0.92, 0.57, 0.22]);
translate([120, -92, -0.55]) rotate([0, 0, 5]) boat_speed(REDC);
translate([200, -104, -0.55]) rotate([0, 0, 150]) boat_speed(WHITEC);
translate([-300, -112, -0.55]) rotate([0, 0, -8]) boat_speed([0.42, 0.62, 0.40]);
translate([10, -125, -0.55]) rotate([0, 0, 40]) boat_sail([0.24, 0.32, 0.48]);
translate([-120, -96, -0.55]) rotate([0, 0, -15]) boat_sail([0.55, 0.25, 0.22]);
translate([300, -98, -0.55]) rotate([0, 0, 70]) boat_sail([0.24, 0.32, 0.48]);
translate([-265, -122, -0.55]) rotate([0, 0, 30]) boat_sail([0.30, 0.44, 0.34]);
for (i = [0 : 10])
{
    bxx = -310 + i * 62;
    if (abs(bxx - 150) > 20 && abs(bxx + 250) > 20)
        translate([bxx, -84, -0.55]) prop_buoy((i % 2 == 0) ? REDC : YELLOWC);
}
for (i = [0 : 29])
    color(FOAMC) translate([-320 + i * 22 + rnd(i * 5, 15), -92 - rnd(i * 3, 58), -0.50]) boxc([2.4, 0.22, 0.03]);

// ---- 空中 ----
translate([60, -68, 18]) rotate([0, 0, -30]) rotate([0, 4, 0]) veh_helicopter(REDC, true);
translate([-40, -70, 9]) rotate([0, 0, 20]) prop_gull();
translate([-34, -66, 11]) rotate([0, 0, -15]) prop_gull();
translate([84, -58, 8]) rotate([0, 0, 60]) prop_gull();
translate([178, -100, 10]) rotate([0, 0, -40]) prop_gull();
translate([-2, -88, 12.5]) rotate([0, 0, 5]) prop_gull();
translate([240, -70, 10]) rotate([0, 0, 100]) prop_gull();
translate([-200, -62, 9]) rotate([0, 0, -70]) prop_gull();
translate([-310, -95, 12]) rotate([0, 0, 30]) prop_gull();

// ---- 路面车流（按道路撒布） ----
for (i = [0 : 6]) traffic_x(HY[i], i * 3 + 1, 11);
for (i = [0 : 10]) traffic_y(VX[i], i * 7 + 3, 5);
