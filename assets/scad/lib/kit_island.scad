// kit_island.scad —— 动物森友会风格度假岛零件库（明亮低模、圆润可爱）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "is_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具/船头朝 +x）。
// 尺度：mid（小屋 ~7x6、果树高 ~4、码头长 ~14、艇长 ~4）。
// 主题元素：果园三宝（苹果/橙/桃）+ 椰子树/礁石、圆润小屋（彩色坡顶+圆窗+拱门）、
//           服务处/果品店、石板广场/喷泉/旗杆/邮筒、原木篱笆/庭院灯/长椅、
//           农田垄作/稻草人/洒水壶、沙滩（遮阳伞/躺椅/沙桶/火炬/篝火/贝壳）、
//           木码头/木拱桥/红白灯塔、小汽艇、水果堆。
// 水工件（dock/bridge）桩脚下探，zMin 为负是预期（同桥契约）。

// ================= 配色（低饱和度假岛；PT 强日光下会整体提亮，故基色偏深） =================
function is_GRASSC() = [0.36, 0.47, 0.21];   // 草地基底（黄绿，勿超 ~0.4 灰度否则发白）
function is_GRASSD() = [0.30, 0.41, 0.18];   // 深草斑
function is_GRASSL() = [0.44, 0.54, 0.26];   // 浅草斑（小面积点缀）
function is_SANDC()  = [0.56, 0.50, 0.36];   // 沙滩
function is_SANDD()  = [0.47, 0.42, 0.30];   // 湿沙/沙纹
function is_SOILC()  = [0.40, 0.30, 0.20];   // 农田土
function is_SOILD()  = [0.33, 0.24, 0.16];   // 垄土
function is_PATHC()  = [0.54, 0.48, 0.34];   // 沙土小径
function is_STONEC() = [0.54, 0.53, 0.49];   // 石材（基座/喷泉/广场）
function is_STONED() = [0.45, 0.44, 0.40];   // 石缝/深石
function is_PLAZC()  = [0.50, 0.49, 0.45];   // 广场地砖
function is_PLAZD()  = [0.42, 0.41, 0.37];   // 地砖缝
function is_PLAZL()  = [0.60, 0.58, 0.52];   // 浅色镶边石
function is_SEADEEP()= [0.07, 0.29, 0.41];   // 深海
function is_SEASHAL()= [0.15, 0.43, 0.48];   // 浅海
function is_SEAFOAM()= [0.62, 0.78, 0.77];   // 浪花线（细小面积可用亮色）
function is_WOODC()  = [0.50, 0.37, 0.23];   // 原木（篱笆/桥/码头甲板）
function is_WOODD()  = [0.36, 0.26, 0.16];   // 深木（桩/梁）
function is_WOODL()  = [0.60, 0.46, 0.28];   // 浅木（家具/果箱）
function is_TRUNKC() = [0.36, 0.26, 0.17];   // 树干
function is_LEAFC()  = [0.34, 0.51, 0.23];   // 树冠亮绿
function is_LEAFD()  = [0.27, 0.42, 0.19];   // 树冠深绿
function is_PALMC()  = [0.24, 0.44, 0.22];   // 椰叶深绿
function is_TRIMW()  = [0.83, 0.81, 0.74];   // 白饰边/白框
function is_WALLC()  = [0.78, 0.74, 0.64];   // 奶油墙
function is_DARKC()  = [0.12, 0.11, 0.11];   // 窗洞
function is_REDC()   = [0.58, 0.20, 0.13];   // 邮筒红/旗红
function is_BLUEC()  = [0.24, 0.42, 0.55];   // 度假蓝
function is_FRUITR() = [0.62, 0.19, 0.11];   // 苹果红
function is_FRUITO() = [0.76, 0.43, 0.10];   // 橙
function is_FRUITP() = [0.87, 0.54, 0.53];   // 桃粉
function is_FRUITC() = [0.53, 0.40, 0.24];   // 椰棕
function is_GOLDC()  = [0.82, 0.68, 0.28];   // 金属亮件（把手/球饰）

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function is_sq(x) = (x * x + x * 419 + 71) % 65521;
function is_rnd(s, m) = is_sq(is_sq(((s % 65521) + 65521) % 65521) + 17) % m;
function is_rndf(s) = is_rnd(s, 1000) / 999;                       // [0, 1]
function is_rndr(s, a, b) = a + (b - a) * is_rndf(s);              // [a, b]

// ---- 变体调色板（列表字面量直接下标；勿对函数调用结果直接下标） ----
function is_roof_c(i) = [[0.58, 0.26, 0.18], [0.26, 0.42, 0.55], [0.28, 0.46, 0.26],
                         [0.66, 0.50, 0.18], [0.42, 0.30, 0.44]][is_rnd(i, 5)];      // 小屋屋顶
function is_wall_c(i) = [[0.78, 0.74, 0.64], [0.76, 0.70, 0.48], [0.62, 0.72, 0.74],
                         [0.78, 0.62, 0.60], [0.58, 0.44, 0.30]][is_rnd(i, 5)];      // 小屋墙
function is_flower_c(i) = [[0.68, 0.20, 0.16], [0.80, 0.62, 0.16], [0.86, 0.84, 0.80],
                           [0.82, 0.48, 0.56], [0.55, 0.38, 0.60]][is_rnd(i, 5)];    // 花色
function is_leaf_c(i) = [is_LEAFC(), is_LEAFD(), [0.46, 0.55, 0.24]][is_rnd(i, 3)];  // 树冠变体

// ---- 基础工具 ----
module is_boxc(s) cube(s, center = true);
module is_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) is_boxc([L, D, t]);   // 底面 z=0 平板

// ================= 通用构件 =================

// 坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙皮，h=脊高，ov=出檐，
// rin=山面内收（0=双坡，rin>=L/2=攒尖）。面序为 OpenSCAD 约定（从外看顺时针）。
module is_part_roof(L = 7, D = 6, h = 1.8, ov = 0.8, rin = 0, c = [0.28, 0.42, 0.30])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// 拱顶木门带白框（front=-y，底面 z=0）：矩形段 + 顶部半圆拱
module is_part_archdoor(w = 1.05, h = 1.9, c = [0.38, 0.27, 0.17])
{
    r = w / 2;
    hh = max(0.05, h - r);
    color(is_TRIMW())
    {
        translate([0, 0, hh / 2]) is_boxc([w + 0.26, 0.12, hh]);
        translate([0, 0, hh]) rotate([90, 0, 0]) cylinder(h = 0.12, r = r + 0.13, center = true, $fn = 10);
    }
    color(c)
    {
        translate([0, -0.04, hh / 2]) is_boxc([w, 0.12, hh]);
        translate([0, -0.04, hh]) rotate([90, 0, 0]) cylinder(h = 0.12, r = r, center = true, $fn = 10);
    }
    color(is_GOLDC()) translate([w * 0.26, -0.115, h * 0.42]) is_boxc([0.09, 0.05, 0.09]);
}

// 白框圆窗（贴墙面用，锚点=窗中心；圆窗无方向性）
module is_part_roundwin(r = 0.32)
{
    color(is_TRIMW()) rotate([90, 0, 0]) cylinder(h = 0.14, r = r + 0.07, center = true, $fn = 10);
    color(is_DARKC()) rotate([90, 0, 0]) cylinder(h = 0.15, r = r, center = true, $fn = 10);
    color(is_TRIMW())
    {
        is_boxc([r * 1.9, 0.16, 0.06]);
        rotate([0, 0, 90]) is_boxc([r * 1.9, 0.16, 0.06]);
    }
}

// 白框方窗（贴墙面用，front=-y，锚点=窗中心）
module is_part_window(w = 0.9, h = 1.0)
{
    color(is_TRIMW()) is_boxc([w, 0.14, h]);
    color(is_DARKC()) translate([0, -0.03, 0]) is_boxc([w - 0.2, 0.14, h - 0.2]);
    color(is_TRIMW()) translate([0, -0.06, 0]) is_boxc([0.07, 0.12, h - 0.2]);
}

// ================= 地面（底面 z=0） =================

// 有机形薄板：低模圆边多边形，岛体层叠（海/浅滩/沙滩/草地）的基础形状件。
// 多块不同 seed 的 blob 交错叠放即出海湾与海角；注意逐层抬高避免共面 z-fight。
module is_ground_blob(L = 40, D = 30, t = 0.12, c = [0.36, 0.47, 0.21], seed = 0, fn = 10)
{
    color(c) rotate([0, 0, is_rnd(seed, 180)]) scale([L * 0.5, D * 0.5, 1]) cylinder(h = t, r = 1, $fn = fn);
}

// 沙滩地（矩形区用）：沙底 + 沙纹 + 湿沙斑 + 零星小石
module is_ground_sand(L = 10, D = 8, seed = 0)
{
    color(is_SANDC()) is_slab(L, D, 0.12);
    for (i = [0 : 4])
        color(is_SANDD())
            translate([is_rndr(seed * 7 + i * 13, -(L - 2) / 2, (L - 2) / 2),
                       is_rndr(seed * 11 + i * 17 + 3, -(D - 2) / 2, (D - 2) / 2), 0.12])
                rotate([0, 0, is_rnd(seed + i, 180)]) is_slab(is_rndr(seed + i + 5, 1.4, 3.2), 0.16, 0.012);
    for (i = [0 : 3])
        color([0.58, 0.56, 0.52])
            translate([is_rndr(seed * 3 + i * 29, -(L - 1.5) / 2, (L - 1.5) / 2),
                       is_rndr(seed * 5 + i * 23 + 7, -(D - 1.5) / 2, (D - 1.5) / 2), 0.12])
                rotate([0, 0, is_rnd(seed + i * 3, 180)]) is_boxc([0.22, 0.16, 0.08]);
}

// 草地（矩形区用）：草底 + 深浅草斑 + 草簇点缀
module is_ground_grass(L = 12, D = 10, seed = 0)
{
    color(is_GRASSC()) is_slab(L, D, 0.10);
    for (i = [0 : 5])
        color(i % 2 == 0 ? is_GRASSD() : is_GRASSL())
            translate([is_rndr(seed * 13 + i * 31, -(L - 3) / 2, (L - 3) / 2),
                       is_rndr(seed * 17 + i * 41 + 3, -(D - 3) / 2, (D - 3) / 2), 0.10])
                rotate([0, 0, is_rnd(seed + i, 180)]) is_slab(is_rndr(seed + i, 1.2, 2.8), is_rndr(seed + i + 5, 0.9, 2.0), 0.012);
}

// 广场石板：地砖网格 + 四边浅色镶边 + 中心圆盘（服务处前广场）
module is_ground_plaza(L = 18, D = 14)
{
    color(is_PLAZC()) is_slab(L, D, 0.12);
    nx = max(1, floor(L / 2.8));
    ny = max(1, floor(D / 2.8));
    color(is_PLAZD())
    {
        if (nx > 1) for (i = [1 : nx - 1]) translate([-L / 2 + i * L / nx, 0, 0.12]) is_slab(0.08, D, 0.014);
        if (ny > 1) for (i = [1 : ny - 1]) translate([0, -D / 2 + i * D / ny, 0.12]) is_slab(L, 0.08, 0.014);
    }
    color(is_PLAZL())
    {
        for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.3), 0.12]) is_slab(L - 0.4, 0.42, 0.02);
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.3), 0, 0.12]) is_slab(0.42, D - 0.4, 0.02);
        translate([0, 0, 0.12]) cylinder(h = 0.02, r = min(L, D) * 0.24, $fn = 12);
    }
}

// 沙土小径（沿 x）：踏面 + 两侧收边石交错 + 中间浅色踩痕
module is_ground_path(L = 10, W = 2.4, seed = 0)
{
    color(is_PATHC()) is_slab(L, W, 0.09);
    color([0.60, 0.54, 0.40]) translate([0, 0, 0.09]) is_slab(L * 0.92, W * 0.4, 0.012);
    ns = max(2, floor(L / 1.6));
    color(is_STONEC()) for (i = [0 : ns - 1], sy = [-1, 1])
        translate([-L / 2 + 0.8 + i * (L - 1.6) / max(1, ns - 1) + (sy > 0 ? 0.7 : 0),
                   sy * (W / 2 - 0.18), 0.09])
            rotate([0, 0, is_rnd(seed + i * 7 + sy * 3, 40) - 20]) is_boxc([0.34, 0.22, 0.1]);
}

// 农田垄作（沿 x 起垄）：crop=0 空垄 / 1 萝卜（白球+绿缨）/ 2 白菜（绿球）
module is_ground_field(L = 10, D = 8, seed = 0, crop = 1)
{
    color(is_SOILC()) is_slab(L, D, 0.12);
    nr = max(2, floor(D / 1.4));
    for (r = [0 : nr - 1])
    {
        py = -D / 2 + 0.8 + r * (D - 1.6) / (nr - 1);
        color(is_SOILD()) translate([0, py, 0.12]) is_slab(L - 0.7, 0.52, 0.13);
        if (crop > 0)
        {
            nc = max(2, floor((L - 1.2) / 0.9));
            for (c = [0 : nc - 1])
                if (is_rnd(seed + r * 31 + c * 7, 6) != 0)
                {
                    px = -L / 2 + 0.7 + c * (L - 1.4) / (nc - 1);
                    if (crop == 1)
                    {
                        color([0.86, 0.82, 0.74]) translate([px, py, 0.44]) sphere(r = 0.17, $fn = 7);
                        color(is_LEAFD()) translate([px, py, 0.56]) cylinder(h = 0.22, r1 = 0.04, r2 = 0.10, $fn = 6);
                    }
                    else
                    {
                        color(is_LEAFC()) translate([px, py, 0.44]) sphere(r = 0.21, $fn = 7);
                        color(is_GRASSL()) translate([px, py, 0.56]) sphere(r = 0.10, $fn = 6);
                    }
                }
        }
    }
}

// 溪流（沿 x）：浅水面 + 两侧深色水缘 + 中央白浪虚线。叠在草地上时底面抬高 1~2 cm。
module is_ground_stream(L = 16, W = 3.2, seed = 0)
{
    color(is_SEASHAL()) is_slab(L, W, 0.06);
    color([0.10, 0.35, 0.44]) for (sy = [-1, 1])
        translate([0, sy * (W / 2 - 0.18), 0.06]) is_slab(L, 0.3, 0.012);
    nw = max(2, floor(L / 2.2));
    color(is_SEAFOAM()) for (i = [0 : nw - 1])
        translate([-L / 2 + 1.0 + i * (L - 2.0) / max(1, nw - 1), is_rndr(seed + i, -W * 0.12, W * 0.12), 0.06])
            is_slab(0.7, 0.09, 0.012);
}

// ================= 建筑（front = -y） =================

// 伙伴小屋：石基 + 彩墙 + 大出檐双坡彩顶 + 圆窗 + 拱门 + 门阶 + 烟囱（seed 偶选）
module is_bldg_house(seed = 0, L = 7, D = 6)
{
    wh = 2.5;
    wc = is_wall_c(seed);
    rc = is_roof_c(seed + 3);
    color(is_STONEC()) is_slab(L + 0.4, D + 0.4, 0.18);
    color(wc) translate([0, 0, 0.18]) is_slab(L, D, wh - 0.18);
    color(is_TRIMW()) translate([0, 0, wh]) is_slab(L + 0.14, D + 0.14, 0.12);
    translate([0, 0, wh + 0.12]) is_part_roof(L, D, 1.9, 0.85, 0, rc);
    color(rc) translate([0, 0, wh + 2.02]) is_boxc([L * 0.72, 0.34, 0.18]);   // 正脊盖
    if (is_rnd(seed, 2) == 0)
    {
        color(is_TRIMW()) translate([-L * 0.28, D * 0.16, wh + 1.1]) is_boxc([0.56, 0.56, 1.7]);
        color(is_STONED()) translate([-L * 0.28, D * 0.16, wh + 1.98]) is_boxc([0.68, 0.68, 0.12]);
    }
    translate([0, -D / 2 - 0.04, 0.18]) is_part_archdoor();
    color(is_STONEC()) translate([0, -D / 2 - 0.62, 0]) is_slab(1.9, 0.9, 0.18);
    translate([-L * 0.28, -D / 2 - 0.05, 1.72]) is_part_roundwin();
    translate([L * 0.28, -D / 2 - 0.05, 1.72]) is_part_roundwin();
    translate([L / 2 + 0.05, 0.9, 1.72]) is_part_roundwin(0.26);
    translate([-L / 2 - 0.05, -0.9, 1.72]) is_part_roundwin(0.26);
}

// 服务处（广场主建筑）：石基座 + 奶油楼身 + 陡彩顶 + 前廊双柱雨棚 + 匾额 + 圆窗排
module is_bldg_hall(seed = 0, L = 12, D = 9)
{
    wh = 3.4;
    rc = is_roof_c(seed + 2);
    color(is_STONEC()) is_slab(L + 0.5, D + 0.5, 0.35);
    color(is_WALLC()) translate([0, 0, 0.35]) is_slab(L, D, wh - 0.35);
    color(is_TRIMW()) translate([0, 0, wh]) is_slab(L + 0.16, D + 0.16, 0.14);
    translate([0, 0, wh + 0.14]) is_part_roof(L, D, 2.4, 1.0, 0, rc);
    color(rc) translate([0, 0, wh + 2.54]) is_boxc([L * 0.7, 0.38, 0.2]);
    color(is_GOLDC()) translate([0, 0, wh + 2.75]) sphere(r = 0.22, $fn = 8);   // 脊顶球饰
    // 双拱门 + 门阶
    for (sx = [-1, 1])
        translate([sx * 0.85, -D / 2 - 0.05, 0.35]) is_part_archdoor(0.95, 2.0, [0.34, 0.24, 0.15]);
    color(is_STONEC()) translate([0, -D / 2 - 0.75, 0]) is_slab(4.2, 1.1, 0.35);
    // 圆窗排 + 侧方窗
    for (i = [-1 : 1])
        translate([i * L * 0.32, -D / 2 - 0.06, 2.3]) is_part_roundwin(0.3);
    for (sy = [-0.7, 0.7])
        translate([L / 2 + 0.05, sy * D * 0.3, 2.2]) is_part_roundwin(0.28);
    // 前廊双柱雨棚
    color(is_TRIMW()) for (sx = [-1, 1])
        translate([sx * 2.2, -D / 2 - 1.1, 0]) cylinder(h = 2.95, r = 0.14, $fn = 8);
    color(rc) translate([0, -D / 2 - 0.85, 2.95]) rotate([8, 0, 0]) is_boxc([5.6, 2.2, 0.14]);
    // 雨棚前檐吊匾（彩底 + 亮字块示意）
    color([0.24, 0.42, 0.55]) translate([0, -D / 2 - 1.95, 2.7]) is_boxc([4.2, 0.18, 0.5]);
    color(is_TRIMW()) translate([0, -D / 2 - 2.05, 2.7]) is_boxc([3.4, 0.04, 0.3]);
}

// 果品商店：木墙 + 大绿顶 + 屋顶大叶子招牌 + 拱门 + 橱窗 + 门前果箱
module is_bldg_shop(seed = 0, L = 8, D = 7)
{
    wh = 2.7;
    rc = [0.26, 0.45, 0.24];
    color(is_STONEC()) is_slab(L + 0.4, D + 0.4, 0.2);
    color([0.58, 0.44, 0.30]) translate([0, 0, 0.2]) is_slab(L, D, wh - 0.2);
    color(is_TRIMW()) translate([0, 0, wh]) is_slab(L + 0.14, D + 0.14, 0.12);
    translate([0, 0, wh + 0.12]) is_part_roof(L, D, 2.0, 0.8, 0, rc);
    color(rc) translate([0, 0, wh + 2.12]) is_boxc([L * 0.7, 0.32, 0.18]);
    // 屋顶大叶子招牌：短杆 + 两片交叉压扁球
    color(is_WOODD()) translate([0, 0, wh + 2.2]) cylinder(h = 0.55, r = 0.07, $fn = 6);
    color(is_LEAFC()) for (a = [0, 90])
        translate([0, 0, wh + 2.85]) rotate([0, 0, a + 20]) scale([1.1, 0.62, 0.22]) sphere(r = 0.75, $fn = 9);
    // 门 + 橱窗
    translate([-L * 0.24, -D / 2 - 0.05, 0.2]) is_part_archdoor(1.0, 1.95, [0.32, 0.22, 0.13]);
    color(is_DARKC()) translate([L * 0.22, -D / 2 - 0.04, 1.55]) is_boxc([L * 0.4, 0.12, 1.3]);
    color(is_TRIMW()) translate([L * 0.22, -D / 2 - 0.09, 1.55]) is_boxc([L * 0.42, 0.05, 1.44]);
    color(is_STONEC()) translate([-L * 0.24, -D / 2 - 0.6, 0]) is_slab(1.7, 0.85, 0.2);
    // 门前果箱
    for (sx = [-1, 1])
        translate([sx * 1.9, -D / 2 - 1.2, 0.2]) is_prop_crate(seed + sx + 2);
}

// 木码头（沿 +x 伸入海；锚点=岸端甲板面高度 z=0，桩脚下探为预期）
module is_bldg_dock(L = 14, W = 3)
{
    nx = max(4, floor(L / 0.62));
    color(is_WOODC()) for (i = [0 : nx - 1])
        translate([-L / 2 + (i + 0.5) * L / nx, 0, -0.07]) is_slab(L / nx - 0.035, W, 0.07);
    color(is_WOODD())
    {
        for (sy = [-1, 1]) translate([0, sy * (W / 2 - 0.09), -0.2]) is_boxc([L - 0.3, 0.16, 0.2]);
        for (x = [-L / 2 + 0.5 : 2.6 : L / 2 - 0.6], sy = [-1, 1])
            translate([x, sy * (W / 2 - 0.2), -0.98]) cylinder(h = 0.94, r = 0.12, $fn = 7);
    }
    // 末端系柱
    color(is_WOODL()) for (sy = [-1, 1])
        translate([L / 2 - 0.2, sy * (W / 2 - 0.2), 0]) cylinder(h = 0.45, r = 0.14, $fn = 7);
    color(is_WOODD()) for (sy = [-1, 1])
        translate([L / 2 - 0.2, sy * (W / 2 - 0.2), 0.45]) cylinder(h = 0.06, r = 0.17, $fn = 7);
}

// 木拱桥（沿 x 跨越；锚点=引桥端路面高度 z=0，桥墩下探）：五段折线拱面 + 栏杆
module is_bldg_bridge(L = 8, W = 2.6)
{
    xs = [-L / 2, -L * 0.19, 0, L * 0.19, L / 2];
    zs = [0, 0.34, 0.52, 0.34, 0];
    for (s = [0 : 3])
    {
        dx = xs[s + 1] - xs[s];
        dz = zs[s + 1] - zs[s];
        ln = sqrt(dx * dx + dz * dz) + 0.24;
        ang = atan2(dz, dx);
        color(is_WOODC()) translate([(xs[s] + xs[s + 1]) / 2, 0, (zs[s] + zs[s + 1]) / 2 + 0.06])
            rotate([0, -ang, 0]) is_boxc([ln, W, 0.1]);
        for (sy = [-1, 1])
        {
            color(is_WOODD()) translate([(xs[s] + xs[s + 1]) / 2, sy * (W / 2 - 0.07), (zs[s] + zs[s + 1]) / 2 + 0.42])
                rotate([0, -ang, 0]) is_boxc([ln, 0.07, 0.07]);
            color(is_WOODC()) translate([xs[s], sy * (W / 2 - 0.07), zs[s] + 0.11]) cylinder(h = 0.4, r = 0.05, $fn = 6);
        }
    }
    color(is_WOODC()) translate([xs[4], W / 2 - 0.07, zs[4] + 0.11]) cylinder(h = 0.4, r = 0.05, $fn = 6);
    color(is_WOODC()) translate([xs[4], -(W / 2 - 0.07), zs[4] + 0.11]) cylinder(h = 0.4, r = 0.05, $fn = 6);
    color(is_WOODD()) for (sx = [-1, 1])
        translate([sx * L * 0.22, 0, -0.4]) is_boxc([0.34, W - 0.3, 1.4]);
}

// 红白灯塔（岛角地标）：石基 + 四段红白锥筒 + 灯室 + 红锥顶 + 底部小拱门
module is_bldg_lighthouse(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(is_STONEC()) cylinder(h = 0.35, r = 1.5, $fn = 10);
        color(is_STONED()) translate([0, 0, 0.35]) cylinder(h = 0.1, r = 1.3, $fn = 10);
        for (i = [0 : 3])
        {
            r1 = 1.12 - i * 0.09;
            r2 = 1.03 - i * 0.09;
            color(i % 2 == 0 ? [0.82, 0.79, 0.74] : [0.60, 0.21, 0.14])
                translate([0, 0, 0.45 + i * 1.5]) cylinder(h = 1.5, r1 = r1, r2 = r2, $fn = 10);
        }
        color(is_STONED()) translate([0, 0, 6.45]) cylinder(h = 0.22, r = 0.92, $fn = 10);   // 檐盘
        color(is_DARKC()) translate([0, 0, 6.67]) cylinder(h = 0.95, r = 0.72, $fn = 9);      // 灯室
        color([0.92, 0.87, 0.55]) translate([0, 0, 6.72]) cylinder(h = 0.85, r = 0.5, $fn = 8);
        color(is_TRIMW()) translate([0, 0, 7.62]) cylinder(h = 0.1, r = 0.85, $fn = 9);
        color([0.60, 0.21, 0.14]) translate([0, 0, 7.72]) cylinder(h = 0.75, r1 = 0.8, r2 = 0.05, $fn = 9);
        color(is_GOLDC()) translate([0, 0, 8.55]) sphere(r = 0.14, $fn = 7);
        // 底部小拱门（朝 -y）
        translate([0, -1.18, 0.45]) is_part_archdoor(0.8, 1.6, [0.38, 0.28, 0.18]);
    }
}

// ================= 植被（底面 z=0） =================

// 果树通用体：干 + 三球团冠 + 冠缘五果（fc=果色，lc=冠色）
module is_nature_fruit_tree(s = 1.0, seed = 0, fc = [0.62, 0.19, 0.11], lc = [0.34, 0.51, 0.23])
{
    scale([s, s, s])
    {
        color(is_TRUNKC()) cylinder(h = 1.5, r = 0.2, $fn = 6);
        color(lc) translate([0, 0, 2.45]) sphere(r = 1.45, $fn = 8);
        color(is_LEAFD()) translate([0.72, 0.34, 3.25]) sphere(r = 0.82, $fn = 7);
        color(lc) translate([-0.68, -0.3, 3.2]) sphere(r = 0.78, $fn = 7);
        for (a = [0 : 72 : 288])
            color(fc) translate([cos(a) * 1.22, sin(a) * 1.22, 1.72]) sphere(r = 0.17, $fn = 7);
    }
}

module is_nature_apple(s = 1.0, seed = 0) is_nature_fruit_tree(s, seed, is_FRUITR(), is_leaf_c(seed));
module is_nature_orange(s = 1.0, seed = 0) is_nature_fruit_tree(s, seed, is_FRUITO(), is_leaf_c(seed + 3));
module is_nature_peach(s = 1.0, seed = 0) is_nature_fruit_tree(s, seed, is_FRUITP(), is_leaf_c(seed + 7));

// 阔叶树（无果，村屋旁/路边）
module is_nature_tree(s = 1.0, seed = 0)
{
    lc = is_leaf_c(seed);
    scale([s, s, s])
    {
        color(is_TRUNKC()) cylinder(h = 1.6, r = 0.19, $fn = 6);
        color(lc) translate([0, 0, 2.6]) sphere(r = 1.4, $fn = 8);
        color(is_LEAFD()) translate([0.6, 0.3, 3.3]) sphere(r = 0.75, $fn = 7);
        color(lc) translate([-0.6, -0.28, 3.25]) sphere(r = 0.7, $fn = 7);
    }
}

// 椰子树：弯干（球链 hull 保证逐段相连）+ 顶生放射下垂棕叶 + 挂椰（沙滩标志树）
module is_nature_coconut(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        tp = [[0, 0, 0], [0.18, 0, 1.0], [0.55, 0, 1.95], [1.05, 0, 2.8], [1.55, 0, 3.5]];
        for (i = [0 : 3])
            color(is_TRUNKC()) hull()
            {
                translate(tp[i]) sphere(r = 0.21 - i * 0.015, $fn = 6);
                translate(tp[i + 1]) sphere(r = 0.21 - (i + 1) * 0.015, $fn = 6);
            }
        color(is_PALMC())
        {
            for (a = [0 : 60 : 300])
                rotate([0, 0, a])
                {
                    translate([2.25, 0, 3.5]) rotate([0, -8, 0]) is_boxc([1.4, 0.42, 0.05]);
                    translate([2.95, 0, 3.38]) rotate([0, -26, 0]) is_boxc([1.1, 0.34, 0.05]);
                }
            translate([2.0, 0, 3.62]) is_boxc([0.9, 0.4, 0.05]);
        }
        color(is_FRUITC()) for (p = [[1.7, 0.17, 3.28], [1.7, -0.17, 3.28], [1.42, 0, 3.22]])
            translate(p) sphere(r = 0.16, $fn = 7);
    }
}

// 灌木球丛
module is_nature_bush(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(is_leaf_c(seed)) translate([0, 0, 0.55]) sphere(r = 0.55, $fn = 7);
        color(is_LEAFD()) translate([0.4, 0.14, 0.36]) sphere(r = 0.36, $fn = 6);
        color(is_leaf_c(seed + 1)) translate([-0.35, -0.18, 0.32]) sphere(r = 0.32, $fn = 6);
    }
}

// 圆润绿篱（沿 x 通长）：鼓包串 + 顶小球
module is_nature_hedge(len = 6, seed = 0)
{
    n = max(2, floor(len / 1.0));
    for (i = [0 : n - 1])
    {
        px = -len / 2 + (i + 0.5) * len / n;
        color(is_rnd(seed + i * 7, 2) == 0 ? is_LEAFD() : is_LEAFC())
        {
            translate([px, 0, 0.56]) scale([1.25, 1, 1]) sphere(r = 0.55, $fn = 7);
            translate([px, is_rndr(seed + i * 3, -0.12, 0.12), 0.94]) sphere(r = 0.3, $fn = 6);
        }
    }
}

// 花丛：五株混色小花（seed 选色）
module is_nature_flowerbed(seed = 0)
{
    for (i = [0 : 4])
    {
        px = is_rndr(seed * 7 + i * 13, -0.5, 0.5);
        py = is_rndr(seed * 11 + i * 17 + 3, -0.4, 0.4);
        color(is_LEAFD()) translate([px, py, 0.16]) cylinder(h = 0.32, r = 0.03, $fn = 5);
        color(is_leaf_c(seed + i)) translate([px, py, 0.06]) rotate([0, 0, is_rnd(seed + i, 180)]) is_boxc([0.3, 0.1, 0.03]);
        color(is_flower_c(seed + i)) translate([px, py, 0.52]) sphere(r = 0.1, $fn = 6);
        color([0.88, 0.86, 0.78]) translate([px, py, 0.57]) sphere(r = 0.045, $fn = 5);
    }
}

// 礁石（灰球组，海岸/溪口点缀）
module is_nature_rock(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color([0.47, 0.47, 0.44]) translate([0, 0, 0.28]) scale([1.3, 1.0, 0.75]) sphere(r = 0.5, $fn = 7);
        color([0.54, 0.53, 0.50]) translate([0.5, 0.24, 0.2]) scale([0.8, 0.7, 0.6]) sphere(r = 0.4, $fn = 6);
        color([0.42, 0.41, 0.39]) translate([-0.42, -0.2, 0.18]) sphere(r = 0.3, $fn = 6);
    }
}

// ================= 道具（底面 z=0；带朝向者 front=-y） =================

// 原木尖桩篱笆（沿 x 通长，圆杆双横档）
module is_prop_fence(len = 4, seed = 0)
{
    n = max(2, floor(len / 0.44));
    color(is_WOODC())
    {
        for (i = [0 : n - 1])
            translate([-len / 2 + (i + 0.5) * len / n, 0, 0.55]) cylinder(h = 1.1, r = 0.075, center = true, $fn = 6);
        for (z = [0.42, 0.85])
            translate([0, 0, z]) rotate([0, 90, 0]) cylinder(h = len, r = 0.055, center = true, $fn = 6);
    }
    color(is_TRUNKC()) for (i = [0 : n - 1])
        translate([-len / 2 + (i + 0.5) * len / n, 0, 1.1]) cylinder(h = 0.1, r = 0.045, $fn = 5);
}

// 白木长椅（广场/海边，front=-y）：侧板腿 + 座板 + 靠背条（靠背向 +y 后倾）
module is_prop_bench()
{
    color(is_TRIMW())
    {
        for (sx = [-1, 1]) translate([sx * 0.72, 0, 0.23]) is_boxc([0.08, 0.5, 0.46]);
        translate([0, 0, 0.48]) is_boxc([1.8, 0.56, 0.07]);
        translate([0, 0.25, 0.62]) rotate([-12, 0, 0]) is_boxc([1.8, 0.09, 0.5]);
        translate([0, 0.25, 0.92]) rotate([-12, 0, 0]) is_boxc([1.8, 0.07, 0.07]);
        for (sx = [-1, 1]) translate([sx * 0.72, 0.25, 0.9]) sphere(r = 0.06, $fn = 5);
    }
}

// 庭院路灯：黑杆 + 悬臂 + 白圆灯罩 + 小黑顶
module is_prop_lamp()
{
    color([0.16, 0.16, 0.17])
    {
        cylinder(h = 0.08, r = 0.17, $fn = 7);
        cylinder(h = 2.9, r = 0.055, $fn = 6);
        translate([0, -0.3, 2.82]) rotate([0, 0, 0]) is_boxc([0.09, 0.68, 0.08]);
    }
    color([0.95, 0.92, 0.78]) translate([0, -0.58, 2.6]) sphere(r = 0.2, $fn = 8);
    color([0.16, 0.16, 0.17]) translate([0, -0.58, 2.82]) cylinder(h = 0.1, r1 = 0.22, r2 = 0.05, $fn = 7);
}

// 红邮筒（front=-y）：白座 + 方柱 + 圆顶 + 投信口
module is_prop_mailbox()
{
    color(is_TRIMW()) translate([0, 0, 0.07]) is_boxc([0.44, 0.44, 0.14]);
    color(is_REDC())
    {
        translate([0, 0, 0.75]) is_boxc([0.36, 0.36, 1.0]);
        translate([0, 0, 1.25]) rotate([90, 0, 0]) cylinder(h = 0.36, r = 0.18, center = true, $fn = 9);
    }
    color(is_DARKC()) translate([0, -0.19, 1.0]) is_boxc([0.26, 0.03, 0.07]);
    color(is_GOLDC()) translate([0, -0.2, 0.72]) is_boxc([0.05, 0.02, 0.07]);
}

// 旗杆：白杆 + 金球 + 悬挂三角旗 + 白徽
module is_prop_flagpole(h = 7)
{
    color(is_TRIMW())
    {
        cylinder(h = 0.15, r = 0.24, $fn = 8);
        cylinder(h = h, r = 0.06, $fn = 7);
    }
    color(is_GOLDC()) translate([0, 0, h + 0.1]) sphere(r = 0.13, $fn = 7);
    color([0.24, 0.42, 0.55]) translate([0.06, 0, h - 0.28])
        polyhedron(points = [[0, -0.03, 0.3], [1.15, -0.03, 0], [0, -0.03, -0.3],
                             [0, 0.03, 0.3], [1.15, 0.03, 0], [0, 0.03, -0.3]],
                   faces = [[0, 1, 2], [3, 5, 4], [0, 3, 4, 1], [1, 4, 5, 2], [2, 5, 3, 0]]);
    color(is_TRIMW()) translate([0.48, 0, h - 0.28]) rotate([90, 0, 0]) cylinder(h = 0.075, r = 0.09, center = true, $fn = 7);
}

// 广场双层喷泉：石盆 + 水面 + 中柱 + 上盆 + 顶球 + 边饰小球
module is_prop_fountain(s = 1.0)
{
    scale([s, s, s])
    {
        color(is_STONEC()) cylinder(h = 0.5, r = 1.75, $fn = 12);
        color(is_PLAZL()) translate([0, 0, 0.5]) cylinder(h = 0.06, r = 1.68, $fn = 12);
        color(is_SEASHAL()) translate([0, 0, 0.56]) cylinder(h = 0.03, r = 1.5, $fn = 12);
        color(is_STONEC())
        {
            translate([0, 0, 0.5]) cylinder(h = 0.9, r = 0.24, $fn = 8);
            translate([0, 0, 1.4]) cylinder(h = 0.2, r = 0.62, $fn = 10);
        }
        color(is_SEASHAL()) translate([0, 0, 1.6]) cylinder(h = 0.03, r = 0.54, $fn = 10);
        color(is_PLAZL()) translate([0, 0, 1.7]) cylinder(h = 0.3, r1 = 0.14, r2 = 0.02, $fn = 7);
        color(is_GOLDC()) translate([0, 0, 2.05]) sphere(r = 0.11, $fn = 7);
        for (a = [0 : 90 : 270])
            color(is_STONED()) translate([cos(a) * 1.62, sin(a) * 1.62, 0.58]) sphere(r = 0.14, $fn = 6);
    }
}

// 沙滩遮阳伞：斜杆 + 大伞面 + 白边环 + 顶尖
module is_prop_umbrella()
{
    rotate([8, 0, 0])
    {
        color(is_WOODL()) cylinder(h = 2.25, r = 0.045, $fn = 6);
        translate([0, 0, 2.0])
        {
            color([0.62, 0.22, 0.15]) cylinder(h = 0.42, r1 = 1.3, r2 = 0.13, $fn = 10);
            color(is_TRIMW()) translate([0, 0, -0.02]) cylinder(h = 0.09, r1 = 1.31, r2 = 1.24, $fn = 10);
            color(is_TRIMW()) translate([0, 0, 0.44]) cylinder(h = 0.16, r1 = 0.13, r2 = 0.03, $fn = 6);
        }
    }
}

// 沙滩躺椅（躺向沿 x，头端 -x）：木框腿 + 白坐垫 + 头端后仰靠背（绕 y 后倾）
module is_prop_lounger()
{
    color(is_WOODC())
    {
        translate([0, -0.24, 0.16]) is_boxc([1.9, 0.07, 0.05]);
        translate([0, 0.24, 0.16]) is_boxc([1.9, 0.07, 0.05]);
        for (sx = [-0.8, 0.8]) translate([sx, 0, 0.07]) is_boxc([0.07, 0.62, 0.14]);
    }
    color([0.88, 0.86, 0.80]) translate([0.15, 0, 0.26]) is_boxc([1.5, 0.62, 0.09]);
    color([0.88, 0.86, 0.80]) translate([-0.78, 0, 0.46]) rotate([0, -16, 0]) is_boxc([0.08, 0.62, 0.6]);
    color([0.62, 0.22, 0.15]) translate([0.15, 0, 0.315]) is_boxc([0.5, 0.64, 0.03]);
}

// 沙滩玩具：红沙桶 + 黄小铲
module is_prop_beachkit()
{
    color([0.55, 0.20, 0.14]) cylinder(h = 0.34, r1 = 0.16, r2 = 0.21, $fn = 8);
    color([0.75, 0.65, 0.40]) translate([0, 0, 0.38]) is_boxc([0.34, 0.04, 0.04]);
    color([0.80, 0.62, 0.18]) translate([0.5, 0, 0.12]) rotate([0, -58, 0])
    {
        translate([0, 0, 0.35]) is_boxc([0.05, 0.05, 0.72]);
        translate([0, 0, -0.02]) is_boxc([0.2, 0.24, 0.14]);
    }
}

// 稻草人：柱 + 横臂 + 红衫 + 裤 + 麻头 + 草帽
module is_prop_scarecrow(seed = 0)
{
    color(is_WOODD())
    {
        cylinder(h = 1.9, r = 0.06, $fn = 6);
        translate([0, 0, 1.38]) is_boxc([1.35, 0.06, 0.06]);
    }
    color([0.60, 0.24, 0.17]) translate([0, 0, 1.15]) is_boxc([0.62, 0.32, 0.75]);
    color([0.36, 0.32, 0.28]) translate([0, 0, 0.68]) is_boxc([0.36, 0.28, 0.42]);
    color([0.78, 0.66, 0.42]) translate([0, 0, 1.66]) sphere(r = 0.2, $fn = 7);
    color([0.80, 0.68, 0.26])
    {
        translate([0, 0, 1.82]) cylinder(h = 0.16, r1 = 0.12, r2 = 0.2, $fn = 7);
        translate([0, 0, 1.8]) cylinder(h = 0.05, r = 0.42, $fn = 9);
    }
    color(is_LEAFD()) translate([0.16, 0.06, 1.66]) sphere(r = 0.06, $fn = 5);
    if (is_rnd(seed, 2) == 0) color([0.70, 0.60, 0.30]) translate([0.3, -0.16, 1.1]) is_boxc([0.26, 0.2, 0.2]);
}

// 洒水壶（浅蓝，田边）
module is_prop_wateringcan()
{
    color([0.36, 0.56, 0.62]) cylinder(h = 0.42, r = 0.21, $fn = 8);
    color([0.30, 0.48, 0.54]) translate([0, 0, 0.45]) is_boxc([0.42, 0.05, 0.05]);
    color([0.36, 0.56, 0.62]) translate([0.24, 0, 0.26]) rotate([0, 38, 0]) cylinder(h = 0.55, r1 = 0.045, r2 = 0.075, $fn = 6);
    color([0.36, 0.56, 0.62]) translate([-0.02, 0, 0.44]) is_boxc([0.05, 0.34, 0.05]);
}

// 木告示牌（front=-y）：双柱 + 板面 + 顶盖条 + 白箭头
module is_prop_sign(seed = 0)
{
    color(is_WOODD()) for (sx = [-1, 1])
        translate([sx * 0.55, 0, 0]) cylinder(h = 2.0, r = 0.06, $fn = 6);
    color(is_WOODC()) translate([0, -0.03, 1.45]) is_boxc([1.7, 0.08, 1.0]);
    color(is_WOODD()) translate([0, -0.03, 2.02]) is_boxc([1.9, 0.13, 0.13]);
    color(is_TRIMW())
    {
        translate([-0.15, -0.09, 1.55]) is_boxc([0.7, 0.03, 0.12]);
        translate([0.32, -0.09, 1.55]) rotate([0, 0, 45]) is_boxc([0.17, 0.17, 0.12]);
    }
    color(is_WOODC()) translate([is_rndr(seed, -0.4, 0.4), -0.09, 1.2]) is_boxc([0.5, 0.02, 0.06]);
}

// 码头系柱：矮木桩 + 盖 + 系绳环（码头/船埠头）
module is_prop_bollard()
{
    color(is_WOODL()) cylinder(h = 0.5, r = 0.14, $fn = 7);
    color(is_WOODD())
    {
        translate([0, 0, 0.5]) cylinder(h = 0.07, r = 0.18, $fn = 7);
        translate([0, -0.17, 0.3]) is_boxc([0.06, 0.08, 0.12]);
    }
}

// 竹火炬（沙滩晚会/步道点缀）
module is_prop_torch()
{
    color(is_WOODD()) cylinder(h = 1.5, r = 0.07, $fn = 6);
    color([0.55, 0.40, 0.26]) translate([0, 0, 1.5]) cylinder(h = 0.16, r1 = 0.1, r2 = 0.17, $fn = 7);
    color([0.82, 0.36, 0.10]) translate([0, 0, 1.66]) cylinder(h = 0.42, r1 = 0.14, r2 = 0.015, $fn = 6);
    color([0.92, 0.78, 0.30]) translate([0, 0, 1.66]) cylinder(h = 0.26, r1 = 0.08, r2 = 0.01, $fn = 6);
}

// 篝火：石圈 + 交叉木柴 + 双层火焰（离地检查：柴心 z0.16、r0.09，最低点 0.07）
module is_prop_firepit()
{
    for (a = [0 : 45 : 315])
    {
        ca = cos(a);
        sa = sin(a);
        color(is_rnd(a, 2) == 0 ? [0.50, 0.49, 0.46] : [0.42, 0.41, 0.38])
            translate([ca * 0.78, sa * 0.78, 0.12]) scale([1.15, 0.95, 0.8]) sphere(r = 0.18, $fn = 6);
    }
    color(is_WOODD()) for (a = [0, 60, 120])
        translate([0, 0, 0.18]) rotate([90, 0, a]) cylinder(h = 1.0, r = 0.09, center = true, $fn = 6);
    color([0.82, 0.32, 0.08]) translate([0, 0, 0.22]) cylinder(h = 0.55, r1 = 0.26, r2 = 0.02, $fn = 6);
    color([0.93, 0.72, 0.22]) translate([0, 0, 0.22]) cylinder(h = 0.34, r1 = 0.15, r2 = 0.015, $fn = 6);
}

// 浅木果箱（seed 偶带顶部水果堆）
module is_prop_crate(seed = 0)
{
    color(is_WOODL()) translate([0, 0, 0.18]) is_boxc([0.66, 0.5, 0.36]);
    color(is_WOODD())
    {
        translate([0, 0, 0.36]) is_boxc([0.7, 0.54, 0.06]);
        for (sx = [-1, 1]) translate([sx * 0.3, 0, 0.18]) is_boxc([0.05, 0.54, 0.4]);
    }
    if (is_rnd(seed, 2) == 0)
    {
        fc = is_rnd(seed, 2) == 0 ? is_FRUITO() : is_FRUITR();
        color(fc) for (p = [[-0.14, 0, 0.44], [0.14, 0.03, 0.44], [0, -0.09, 0.46]])
            translate(p) sphere(r = 0.1, $fn = 6);
    }
}

// ================= 载具（船头朝 +x，底面 z=0；浮于水面由调用方定 z） =================

// 度假小汽艇：白船身圆头 + 蓝舷带 + 座舱 + 挡风玻璃 + 尾挂机
module is_veh_boat(seed = 0)
{
    bc = is_rnd(seed, 2) == 0 ? [0.24, 0.42, 0.55] : [0.60, 0.32, 0.30];
    color([0.88, 0.87, 0.82]) hull()
    {
        translate([-0.5, 0, 0.42]) is_boxc([2.6, 1.42, 0.5]);
        translate([1.6, 0, 0.42]) cylinder(h = 0.5, r = 0.71, center = true, $fn = 8);
    }
    color(bc) translate([0, 0, 0.62]) is_boxc([3.6, 1.48, 0.16]);
    color(bc) translate([-0.6, 0, 0.98]) is_boxc([1.25, 1.15, 0.6]);
    color([0.88, 0.87, 0.82]) translate([-0.6, 0, 1.3]) is_boxc([1.45, 1.25, 0.1]);
    color([0.36, 0.30, 0.16]) translate([-0.6, 0, 1.42]) is_boxc([0.5, 1.3, 0.14]);
    color([0.14, 0.14, 0.15])
    {
        translate([-2.05, 0, 0.55]) is_boxc([0.35, 0.34, 0.55]);
        translate([-2.28, 0, 0.3]) rotate([0, 90, 0]) cylinder(h = 0.2, r = 0.1, $fn = 6);
    }
    color([0.14, 0.14, 0.15]) translate([-1.95, 0, 0.2]) is_boxc([3.9, 1.3, 0.14]);   // 底盘压水线
    color(bc) translate([1.7, 0, 0.72]) is_boxc([0.5, 1.0, 0.14]);                    // 船头色块
}

// ================= 小物（掉落物/拾取物，底面 z=0） =================

// 水果堆：kind 0=苹果 1=橙 2=桃 3=椰子（两底一顶）
module is_item_fruit(kind = 0, seed = 0)
{
    fc = [[0.62, 0.19, 0.11], [0.76, 0.43, 0.10], [0.87, 0.54, 0.53], [0.53, 0.40, 0.24]][kind % 4];
    color(fc) for (p = [[-0.14, 0, 0.14], [0.14, 0.02, 0.14], [0, -0.05, 0.31]])
        translate(p) sphere(r = 0.15, $fn = 7);
    if (kind % 4 == 0) color(is_TRUNKC()) translate([0, -0.05, 0.44]) cylinder(h = 0.1, r = 0.02, $fn = 5);
    if (kind % 4 == 3) color(is_WOODD()) translate([0.14, 0.02, 0.26]) cylinder(h = 0.12, r = 0.025, $fn = 5);
}

// 扇贝贝壳 + 小卵石（沙滩散布）
module is_item_shell(seed = 0)
{
    color([0.88, 0.80, 0.76]) translate([0, 0, 0.075]) rotate([0, 0, is_rnd(seed, 180)]) scale([1.15, 1, 0.42]) sphere(r = 0.17, $fn = 8);
    color([0.74, 0.62, 0.58]) for (i = [0 : 2])
        translate([cos(i * 120) * 0.13, sin(i * 120) * 0.13, 0.0]) is_boxc([0.03, 0.03, 0.024]);
    color([0.6, 0.59, 0.56]) translate([0.3, 0.1, 0.055]) sphere(r = 0.06, $fn = 5);
}
