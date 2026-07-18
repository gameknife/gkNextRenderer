// Initially extracted from old_city.scad by the one-time tools/scadkit migration; this checked-in file is now authoritative.
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "oc_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y。调用方自设 $fn（建议 12）。



// ================= 标高与结构常量 =================
function oc_TZ() = 0.30;    // 地形草地顶
function oc_CZ() = 0.50;    // 城基台地顶（城内地坪）
function oc_WT() = 7;       // 城墙厚
function oc_WH() = 9;       // 城墙高（墙身，垛口另加）

// ================= 配色 =================
function oc_STONEC() = [0.63, 0.63, 0.60];   // 城砖灰
function oc_STONED() = [0.52, 0.52, 0.49];   // 石基深灰
function oc_STONEL() = [0.72, 0.72, 0.68];   // 垛口/台基浅灰
function oc_PAVEC() = [0.74, 0.72, 0.66];   // 石板街面
function oc_PAVED() = [0.64, 0.62, 0.56];   // 海墁/御道深石板
function oc_ROOFC() = [0.27, 0.29, 0.33];   // 青瓦
function oc_ROOFD() = [0.20, 0.22, 0.26];   // 深瓦/正脊
function oc_ROOFB() = [0.33, 0.42, 0.55];   // 仓库蓝瓦
function oc_REDW() = [0.58, 0.19, 0.14];   // 朱漆柱
function oc_REDD() = [0.45, 0.14, 0.11];   // 深朱门扇
function oc_REDC() = [0.78, 0.22, 0.17];   // 旗帜红
function oc_PLASTER() = [0.89, 0.85, 0.75];   // 灰泥墙
function oc_WOODC() = [0.58, 0.42, 0.26];   // 原木
function oc_WOODD() = [0.42, 0.29, 0.17];   // 深木构架
function oc_GOLDC() = [0.85, 0.70, 0.32];   // 匾金/门钉
function oc_DARKC() = [0.12, 0.12, 0.13];   // 洞口/窗芯
function oc_DIRTC() = [0.80, 0.73, 0.56];   // 城内夯土
function oc_DIRTD() = [0.71, 0.63, 0.46];   // 土路/校场
function oc_GRASSC() = [0.58, 0.72, 0.37];   // 草地
function oc_GRASSD() = [0.50, 0.65, 0.31];   // 深草斑块
function oc_SOILC() = [0.56, 0.43, 0.28];   // 田土
function oc_SOILD() = [0.47, 0.36, 0.24];   // 田埂
function oc_CROPY() = [0.86, 0.72, 0.30];   // 熟穗黄
function oc_STRAWC() = [0.80, 0.68, 0.42];   // 茅草/干草/麻袋
function oc_WATERC() = [0.30, 0.58, 0.71];   // 水面
function oc_SANDC() = [0.90, 0.82, 0.58];   // 滩涂
function oc_ROCKC() = [0.56, 0.56, 0.53];   // 岩石
function oc_MOUNTC() = [0.45, 0.46, 0.49];   // 石山
function oc_BASEC() = [0.24, 0.21, 0.28];   // 展台底座
function oc_CANVW() = [0.90, 0.87, 0.78];   // 帐布米白
function oc_PURPC() = [0.56, 0.38, 0.68];   // 市场紫棚

// ---- 伪随机 / 调色板 ----
function oc_rnd(seedValue, m) = (((((seedValue * 73 + 31) % 97 + 97) % 97) * 13) + ((seedValue % 7 + 7) % 7)) % m;
function oc_house_c(i) = [[0.90, 0.86, 0.76], [0.86, 0.80, 0.68], [0.92, 0.90, 0.84], [0.81, 0.77, 0.67]][oc_rnd(i, 4)];
function oc_leaf_c(i)  = [[0.50, 0.72, 0.30], [0.42, 0.64, 0.26], [0.58, 0.78, 0.34], [0.88, 0.68, 0.26], [0.82, 0.55, 0.22]][oc_rnd(i, 5)];
function oc_canv_c(i)  = [oc_PURPC(), oc_REDC(), oc_CANVW(), [0.35, 0.52, 0.62]][oc_rnd(i, 4)];
function oc_goods_c(i) = [[0.85, 0.30, 0.22], [0.95, 0.75, 0.25], [0.45, 0.68, 0.30], [0.90, 0.55, 0.20], [0.75, 0.70, 0.60], [0.55, 0.35, 0.60]][oc_rnd(i, 6)];

// ---- 基础工具 ----
module oc_boxc(s) cube(s, center = true);
module oc_slab(L, D, t) translate([0, 0, t / 2]) oc_boxc([L, D, t]);   // 底面 z=0 平板

// ================= 屋面与建筑公共件 =================

// 通用坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙体外皮，h=脊高，ov=出檐，
// rin=山面内收（0=两坡悬山，0<rin<L/2=四坡，rin>=L/2=攒尖）。
// 面序为 OpenSCAD 约定（从外看顺时针）。
module oc_part_roof(L, D, h, ov = 0.8, rin = 0, c = [0.27, 0.29, 0.33])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// 正脊 + 两端鸱吻（置于脊线标高，rl=含吻兽全长）
module oc_part_ridge(rl, c = [0.20, 0.22, 0.26])
{
    color(c)
    {
        oc_boxc([rl, 0.5, 0.46]);
        for (sx = [-1, 1]) translate([sx * (rl / 2 - 0.2), 0, 0.5]) oc_boxc([0.42, 0.56, 0.7]);
    }
}

// 居中中文字（front=-y 外凸 0.06；n=字数，CJK 全角宽≈size，手动居中避免 len() 数多字节）
module oc_part_text_cn(label, n = 2, size = 0.8, c = [0.85, 0.70, 0.32])
{
    color(c) translate([-n * size * 0.5, -0.02, -size * 0.48])
        rotate([90, 0, 0]) linear_extrude(0.06) text(label, size = size);
}

// 匾额：黑底金字金框（front=-y）
module oc_part_plaque(label, n = 2, size = 0.9)
{
    w = n * size * 1.15 + 0.75;
    h = size + 0.65;
    color(oc_DARKC()) translate([0, 0.09, 0]) oc_boxc([w, 0.16, h]);
    color(oc_GOLDC())
    {
        for (sz = [-1, 1]) translate([0, 0.04, sz * (h / 2 - 0.06)]) oc_boxc([w + 0.06, 0.10, 0.12]);
        for (sx = [-1, 1]) translate([sx * (w / 2 - 0.06), 0.04, 0]) oc_boxc([0.12, 0.10, h + 0.06]);
    }
    oc_part_text_cn(label, n, size, oc_GOLDC());
}

// 垛墙：沿 x 通长矮墙 + 等距垛口（merlon 宽 1.3 净距 0.7 高 h）
module oc_part_battlement(len, t = 0.45, h = 1.7)
{
    color(oc_STONEL()) translate([0, 0, 0.35]) oc_boxc([len, t, 0.7]);
    n = floor((len - 1.5) / 2) + 1;
    x0 = -(n - 1);
    for (i = [0 : n - 1])
        color(oc_STONEL()) translate([x0 + i * 2.0, 0, h / 2]) oc_boxc([1.3, t, h]);
}

// 台阶（自 y=0 向 -y 下行，n 级，级高 rise 级深 run）
module oc_part_steps(w = 4, n = 5, rise = 0.3, run = 0.42)
{
    for (i = [0 : n - 1])
        color(oc_STONEL()) translate([0, -(i + 0.5) * run, (n - i) * rise / 2])
            oc_boxc([w, run, (n - i) * rise]);
}

// 木棂窗（贴墙面：墙面在 y=0，窗朝 -y）
module oc_part_lattice_win(w = 1.4, h = 1.1)
{
    color(oc_WOODD()) translate([0, -0.05, 0]) oc_boxc([w, 0.10, h]);
    color(oc_DARKC()) translate([0, -0.09, 0]) oc_boxc([w - 0.2, 0.05, h - 0.2]);
    color(oc_WOODD())
    {
        translate([0, -0.115, 0]) oc_boxc([w - 0.16, 0.03, 0.09]);
        for (sx = [-1, 0, 1]) translate([sx * (w - 0.3) / 3, -0.115, 0]) oc_boxc([0.07, 0.03, h - 0.16]);
    }
}

// ================= 植被与地景（底面 z=0） =================

// 低多边形团状树（绿为主，seed 混入秋黄）
module oc_nature_tree(s = 1.0, i = 0)
{
    lc = oc_leaf_c(i);
    color([0.44, 0.31, 0.19]) cylinder(h = 1.7 * s, r = 0.15 * s, $fn = 6);
    color(lc) translate([0, 0, 2.3 * s]) sphere(r = 1.05 * s, $fn = 9);
    color(lc) translate([0.6 * s, 0.25 * s, 1.75 * s]) sphere(r = 0.68 * s, $fn = 8);
    color(oc_leaf_c(i + 2)) translate([-0.55 * s, -0.3 * s, 1.85 * s]) sphere(r = 0.62 * s, $fn = 8);
    color(oc_leaf_c(i + 1)) translate([0.05 * s, -0.5 * s, 2.9 * s]) sphere(r = 0.55 * s, $fn = 8);
}

// 松树（三段锥）
module oc_nature_pine(s = 1.0)
{
    color([0.40, 0.28, 0.17]) cylinder(h = 1.1 * s, r = 0.13 * s, $fn = 6);
    color([0.30, 0.52, 0.30]) translate([0, 0, 0.9 * s]) cylinder(h = 1.6 * s, r1 = 1.05 * s, r2 = 0.55 * s, $fn = 8);
    color([0.33, 0.56, 0.32]) translate([0, 0, 2.2 * s]) cylinder(h = 1.4 * s, r1 = 0.80 * s, r2 = 0.35 * s, $fn = 8);
    color([0.37, 0.60, 0.34]) translate([0, 0, 3.3 * s]) cylinder(h = 1.3 * s, r1 = 0.55 * s, r2 = 0.05 * s, $fn = 8);
}

// 半埋岩石（双球拼合）
module oc_nature_rock(s = 1.0, i = 0)
{
    color(oc_ROCKC()) scale([1, 0.85, 0.6]) sphere(r = s, $fn = 7);
    color([0.60, 0.60, 0.57]) translate([0.55 * s, -0.3 * s, 0]) scale([1, 0.8, 0.55]) sphere(r = 0.6 * s, $fn = 6);
}

// 绿丘（背景收边）
module oc_nature_hill(r = 20, h = 9)
{
    color([0.52, 0.70, 0.34]) cylinder(h = h, r1 = r, r2 = r * 0.42, $fn = 8);
    color([0.57, 0.75, 0.37]) translate([r * 0.18, -r * 0.12, h * 0.85]) cylinder(h = h * 0.5, r1 = r * 0.40, r2 = r * 0.10, $fn = 7);
}

// 石山（灰岩峰，远景地标）
module oc_nature_mount(s = 1.0)
{
    color(oc_MOUNTC()) cylinder(h = 24 * s, r1 = 17 * s, r2 = 1.5 * s, $fn = 7);
    color([0.52, 0.53, 0.56]) translate([9 * s, -4 * s, 0]) cylinder(h = 13 * s, r1 = 9 * s, r2 = 1.2 * s, $fn = 6);
    color([0.60, 0.61, 0.64]) translate([0, 0, 20 * s]) cylinder(h = 4.5 * s, r1 = 3.2 * s, r2 = 0.4 * s, $fn = 6);
}

// 区域散布（树/松/石混合；rect x0..x1, y0..y1，置于地形面）
module oc_nature_scatter(seed = 0, n = 14, x0 = 0, x1 = 10, y0 = 0, y1 = 10)
{
    for (i = [0 : n - 1])
    {
        xx = x0 + oc_rnd(seed * 11 + i * 7, x1 - x0);
        yy = y0 + oc_rnd(seed * 5 + i * 13, y1 - y0);
        k = oc_rnd(seed + i, 6);
        translate([xx, yy, oc_TZ()])
        {
            if (k < 3) oc_nature_tree(0.85 + 0.07 * oc_rnd(i, 5), seed + i);
            else if (k < 5) oc_nature_pine(0.9 + 0.07 * oc_rnd(i + 2, 4));
            else oc_nature_rock(0.7 + 0.2 * oc_rnd(i + 4, 4), i);
        }
    }
}

// ================= 街道小品（底面 z=0；带朝向者 front=-y） =================

// 军旗（幡面朝 +y 挑出）
module oc_prop_flag(c = [0.78, 0.22, 0.17], h = 4.5)
{
    color(oc_WOODD()) cylinder(h = h, r = 0.06, $fn = 6);
    color(oc_GOLDC()) translate([0, 0, h]) sphere(r = 0.09, $fn = 6);
    color(c) translate([0.02, 0.55, h - 0.85]) oc_boxc([0.05, 1.0, 1.5]);
    color(oc_ROOFD()) translate([0.02, 0.55, h - 1.66]) oc_boxc([0.06, 1.0, 0.10]);
}

// 挂式红灯笼（挂点在原点，向下垂）
module oc_prop_lantern()
{
    color(oc_WOODD()) translate([0, 0, -0.05]) cylinder(h = 0.1, r = 0.03, $fn = 6);
    color(oc_GOLDC()) translate([0, 0, -0.14]) cylinder(h = 0.09, r = 0.12, $fn = 8);
    color(oc_REDC()) translate([0, 0, -0.44]) scale([1, 1, 1.15]) sphere(r = 0.26, $fn = 8);
    color(oc_GOLDC()) translate([0, 0, -0.80]) cylinder(h = 0.08, r = 0.10, $fn = 8);
    color(oc_GOLDC()) translate([0, 0, -0.98]) cylinder(h = 0.18, r = 0.02, $fn = 6);
}

// 石灯（街道沿线）
module oc_prop_stone_lamp()
{
    color(oc_STONED()) translate([0, 0, 0.1]) oc_boxc([0.6, 0.6, 0.2]);
    color(oc_STONEC()) translate([0, 0, 0.85]) oc_boxc([0.24, 0.24, 1.3]);
    color(oc_CANVW()) translate([0, 0, 1.66]) oc_boxc([0.4, 0.4, 0.32]);
    color(oc_STONED()) translate([0, 0, 1.92]) oc_boxc([0.56, 0.56, 0.2]);
    color(oc_STONEC()) translate([0, 0, 2.09]) oc_boxc([0.16, 0.16, 0.14]);
}

// 水井（石圈 + 双柱井架 + 辘轳 + 瓦顶）
module oc_prop_well()
{
    color(oc_STONEC()) cylinder(h = 0.85, r1 = 0.88, r2 = 0.78, $fn = 9);
    color(oc_DARKC()) translate([0, 0, 0.86]) cylinder(h = 0.02, r = 0.55, $fn = 9);
    color(oc_WOODD()) for (sx = [-1, 1]) translate([sx * 0.95, 0, 1.9]) oc_boxc([0.12, 0.12, 1.8]);
    color(oc_WOODC()) translate([-0.85, 0, 1.55]) rotate([0, 90, 0]) cylinder(h = 1.7, r = 0.09, $fn = 6);
    translate([0, 0, 2.8]) oc_part_roof(2.6, 1.5, 0.55, 0.2, 0, oc_ROOFC());
    color(oc_WOODC()) translate([0.35, 0, 0.98]) oc_boxc([0.3, 0.3, 0.25]);
}

// 陶缸
module oc_prop_jar()
{
    color([0.36, 0.27, 0.21]) translate([0, 0, 0.42]) scale([1, 1, 1.05]) sphere(r = 0.42, $fn = 8);
    color([0.42, 0.32, 0.25]) translate([0, 0, 0.78]) cylinder(h = 0.1, r = 0.26, $fn = 8);
    color(oc_DARKC()) translate([0, 0, 0.86]) cylinder(h = 0.02, r = 0.20, $fn = 8);
}

// 木箱堆 + 麻袋
module oc_prop_crates(seed = 0)
{
    color(oc_WOODC()) translate([0, 0, 0.4]) oc_boxc([0.8, 0.8, 0.8]);
    color(oc_WOODD()) translate([0.85, 0.1, 0.35]) rotate([0, 0, 14]) oc_boxc([0.7, 0.7, 0.7]);
    if (oc_rnd(seed, 2) == 0)
        color(oc_WOODC()) translate([0.3, 0.05, 1.1]) rotate([0, 0, -10]) oc_boxc([0.7, 0.7, 0.6]);
    color(oc_STRAWC()) translate([-0.3, 0.75, 0.30]) scale([1, 1, 0.8]) sphere(r = 0.38, $fn = 7);
}

// 木板车（车把朝 -y；seed 决定是否载干草）
module oc_prop_cart(seed = 0)
{
    color(oc_WOODC()) translate([0, 0.1, 0.72]) oc_boxc([1.5, 2.6, 0.12]);
    for (sx = [-1, 1])
    {
        color(oc_WOODD()) translate([sx * 0.6, 0.1, 0.88]) oc_boxc([0.1, 2.6, 0.28]);
        color(oc_WOODD()) translate([sx * 0.55, -1.55, 0.55]) rotate([65, 0, 0]) oc_boxc([0.08, 0.08, 1.5]);
        color(oc_WOODD()) translate([sx * 0.8, 0.3, 0.55]) rotate([0, 90, 0]) cylinder(h = 0.1, r = 0.55, $fn = 9);
    }
    color(oc_WOODD()) translate([-0.9, 0.3, 0.55]) rotate([0, 90, 0]) cylinder(h = 1.8, r = 0.05, $fn = 6);
    if (oc_rnd(seed, 2) == 0)
        color(oc_STRAWC()) translate([0, 0.3, 1.05]) scale([1.1, 1.6, 0.75]) sphere(r = 0.55, $fn = 8);
}

// 干草垛
module oc_prop_hay()
{
    color([0.72, 0.60, 0.36]) translate([0, 0, 0.02]) cylinder(h = 0.5, r1 = 1.15, r2 = 0.95, $fn = 9);
    color(oc_STRAWC()) cylinder(h = 1.9, r1 = 1.05, r2 = 0.12, $fn = 9);
}

// 兵器架（斜靠红缨长枪）
module oc_prop_rack()
{
    color(oc_WOODD())
    {
        for (sx = [-1, 1]) translate([sx * 1.1, 0, 0.75]) oc_boxc([0.12, 0.12, 1.5]);
        translate([0, 0, 1.42]) oc_boxc([2.5, 0.10, 0.14]);
        translate([0, 0, 0.25]) oc_boxc([2.5, 0.35, 0.10]);
    }
    for (i = [0 : 4])
        translate([-0.85 + i * 0.42, 0.14, 0]) rotate([-14, 0, 0])
        {
            color(oc_WOODC()) cylinder(h = 2.5, r = 0.035, $fn = 5);
            color([0.75, 0.77, 0.80]) translate([0, 0, 2.5]) cylinder(h = 0.3, r1 = 0.07, r2 = 0.01, $fn = 5);
            color(oc_REDC()) translate([0, 0, 2.46]) sphere(r = 0.07, $fn = 5);
        }
}

// 箭靶（靶面朝 -y）
module oc_prop_target()
{
    color(oc_WOODD()) for (sx = [-1, 1]) translate([sx * 0.4, 0.28, 0.9]) rotate([16, 0, 0]) oc_boxc([0.1, 0.1, 1.9]);
    color(oc_CANVW()) translate([0, 0.06, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.62, $fn = 10);
    color(oc_REDC()) translate([0, -0.07, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.03, r = 0.38, $fn = 8);
    color(oc_DARKC()) translate([0, -0.11, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.03, r = 0.15, $fn = 8);
}

// 铜香炉（殿前）
module oc_prop_incense()
{
    color([0.34, 0.38, 0.35]) cylinder(h = 0.35, r = 0.42, $fn = 8);
    color([0.42, 0.46, 0.42]) translate([0, 0, 0.35]) cylinder(h = 0.75, r1 = 0.60, r2 = 0.52, $fn = 8);
    translate([0, 0, 1.1]) oc_part_roof(1.35, 1.35, 0.5, 0.1, 0.67, oc_ROOFD());
    color(oc_GOLDC()) translate([0, 0, 1.62]) sphere(r = 0.1, $fn = 6);
}

// 石牌坊（跨御道，front=-y）
module oc_prop_paifang()
{
    for (sx = [-1, 1])
    {
        color(oc_STONEC()) translate([sx * 3.5, 0, 2.6]) oc_boxc([0.55, 0.55, 5.2]);
        color(oc_STONED()) translate([sx * 3.5, 0, 0.5]) oc_boxc([1.1, 1.4, 1.0]);
    }
    color(oc_STONED()) translate([0, 0, 4.9]) oc_boxc([8.6, 0.5, 0.55]);
    color(oc_STONEC()) translate([0, 0, 5.55]) oc_boxc([7.6, 0.45, 0.5]);
    translate([0, 0, 5.85]) oc_part_roof(7.4, 1.2, 0.62, 0.35, 0, oc_ROOFC());
    translate([0, 0, 6.5]) oc_part_roof(3.2, 1.3, 0.55, 0.25, 0, oc_ROOFC());
}

// 路网节点界石（八角石盘 + 中心钉；node 锚点用）
module oc_prop_marker()
{
    color(oc_STONEL()) cylinder(h = 0.07, r = 0.85, $fn = 8);
    color(oc_DARKC()) translate([0, 0, 0.07]) cylinder(h = 0.02, r = 0.14, $fn = 8);
}

// 生成点道钉（三枚白石横跨路面，沿 x 排布；spawn 锚点用）
module oc_prop_spawn_stone()
{
    for (i = [-1 : 1]) color([0.93, 0.92, 0.88]) translate([i * 0.8, 0, 0.06]) oc_boxc([0.5, 0.2, 0.12]);
}

// ================= 城墙系统（沿 x 向 module，外侧 = +y；底面 z=0） =================

// 城墙段：基座收分 + 墙身 + 顶面海墁 + 外侧垛墙 + 内侧女墙。
// g0/g1：内侧女墙豁口区间（局部 x 坐标，g1>g0 时留口接马道；默认无口）。
module oc_wall_run(len, g0 = 0, g1 = 0)
{
    color(oc_STONED()) translate([0, 0, 0.6]) oc_boxc([len, oc_WT() + 1.4, 1.2]);
    color(oc_STONEC()) translate([0, 0, oc_WH() / 2]) oc_boxc([len, oc_WT(), oc_WH()]);
    color(oc_PAVED()) translate([0, 0, oc_WH() + 0.06]) oc_boxc([len, oc_WT() - 0.8, 0.12]);
    translate([0, oc_WT() / 2 - 0.25, oc_WH() + 0.12]) oc_part_battlement(len);
    if (g1 > g0)
    {
        color(oc_STONEL()) translate([(-len / 2 + g0) / 2, -(oc_WT() / 2 - 0.25), oc_WH() + 0.62]) oc_boxc([g0 + len / 2, 0.4, 1.0]);
        color(oc_STONEL()) translate([(g1 + len / 2) / 2, -(oc_WT() / 2 - 0.25), oc_WH() + 0.62]) oc_boxc([len / 2 - g1, 0.4, 1.0]);
    }
    else
        color(oc_STONEL()) translate([0, -(oc_WT() / 2 - 0.25), oc_WH() + 0.62]) oc_boxc([len, 0.4, 1.0]);
}

// 城楼（置于城台顶；两层歇山，front 同城门 -y 内侧）
module oc_part_gate_tower()
{
    L = 15;
    D = 7.5;
    color(oc_STONEL()) oc_slab(L + 1.4, D + 1.4, 0.25);
    color(oc_REDW()) for (ix = [-3 : 3], sy = [-1, 1])
        translate([ix * 2.2, sy * (D / 2 - 0.22), 0.25]) cylinder(h = 3.5, r = 0.22, $fn = 8);
    color(oc_PLASTER()) translate([0, 0, 0.25]) oc_slab(L - 1.2, D - 1.0, 3.5);
    color(oc_REDD()) for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.42), 2.1]) oc_boxc([L - 3.5, 0.16, 1.5]);
    color(oc_WOODD()) translate([0, 0, 3.95]) oc_boxc([L + 0.8, D + 0.8, 0.5]);
    translate([0, 0, 4.2]) oc_part_roof(L, D, 1.8, 1.2, 2.8, oc_ROOFC());
    color(oc_PLASTER()) translate([0, 0, 4.9]) oc_slab(L - 3.6, D - 2.2, 2.5);
    color(oc_REDD()) for (sy = [-1, 1]) translate([0, sy * ((D - 2.2) / 2 + 0.02), 6.3]) oc_boxc([L - 5.5, 0.16, 1.2]);
    color(oc_WOODD()) translate([0, 0, 7.2]) oc_boxc([L - 2.8, D - 1.4, 0.45]);
    translate([0, 0, 7.4]) oc_part_roof(L - 3.2, D - 1.8, 2.4, 1.2, 2.6, oc_ROOFC());
    translate([0, 0, 9.6]) oc_part_ridge(7.2);
}

// 城门楼：墩台（洞宽 8 净高 6）+ 常开门扇 + 门枕石 + 城台垛墙 + 城楼 + 门额石匾。
// 外侧 = +y；name = 匾额两字。
module oc_bldg_gatehouse(name = "北门")
{
    GT = 10;   // 城台进深
    for (sx = [-1, 1])
    {
        color(oc_STONED()) translate([sx * 7.5, 0, 0.6]) oc_boxc([7.9, GT + 1.2, 1.2]);
        color(oc_STONEC()) translate([sx * 7.5, 0, oc_WH() / 2]) oc_boxc([7, GT, oc_WH()]);
    }
    color(oc_STONEC()) translate([0, 0, 7.5]) oc_boxc([22, GT, 3.0]);
    color(oc_DARKC()) translate([0, 0, 5.94]) oc_boxc([8.1, GT - 0.6, 0.12]);
    for (sx = [-1, 1])
    {
        color(oc_REDD()) translate([sx * 3.82, 1.6, 3.0]) oc_boxc([0.22, 3.4, 5.4]);
        for (iy = [0 : 3], iz = [0 : 4])
            color(oc_GOLDC()) translate([sx * 3.68, 0.35 + iy * 0.85, 0.9 + iz * 1.05]) oc_boxc([0.08, 0.14, 0.14]);
        color(oc_STONEL()) translate([sx * 3.9, -GT / 2 + 0.9, 0.3]) oc_boxc([0.9, 1.6, 0.6]);
    }
    color(oc_PAVED()) translate([0, 0, oc_WH() + 0.06]) oc_boxc([22, GT + 0.4, 0.12]);
    translate([0, GT / 2 - 0.25, oc_WH() + 0.12]) oc_part_battlement(21.6);
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 10.75, sy * (GT / 2 - 1.5), oc_WH() + 0.12]) rotate([0, 0, 90]) oc_part_battlement(2.6);
    color(oc_STONEL()) translate([0, -GT / 2 + 0.25, oc_WH() + 0.62]) oc_boxc([22, 0.4, 1.0]);
    translate([0, GT / 2 + 0.10, 7.3]) rotate([0, 0, 180]) oc_part_plaque(name, 2, 0.85);
    translate([0, 0, oc_WH() + 0.12]) oc_part_gate_tower();
}

// 角楼：骑墙角台 + 四面垛墙（-x/-y 两侧留豁口接墙顶步道）+ 两层攒尖楼。
// 布局时按角位旋转，使豁口朝向相接的两段城墙。
module oc_bldg_corner_tower()
{
    color(oc_STONED()) translate([0, 0, 0.6]) oc_boxc([16, 16, 1.2]);
    color(oc_STONEC()) translate([0, 0, oc_WH() / 2]) oc_boxc([15, 15, oc_WH()]);
    color(oc_PAVED()) translate([0, 0, oc_WH() + 0.06]) oc_boxc([15.2, 15.2, 0.12]);
    translate([0, 7.35, oc_WH() + 0.12]) oc_part_battlement(14.2);
    translate([7.35, 0, oc_WH() + 0.12]) rotate([0, 0, 90]) oc_part_battlement(14.2);
    for (s = [-1, 1])
    {
        translate([s * 5.35, -7.35, oc_WH() + 0.12]) oc_part_battlement(3.5);
        translate([-7.35, s * 5.35, oc_WH() + 0.12]) rotate([0, 0, 90]) oc_part_battlement(3.5);
    }
    translate([0, 0, oc_WH() + 0.12])
    {
        color(oc_STONEL()) oc_slab(9.6, 9.6, 0.22);
        color(oc_REDW()) for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 3.9, sy * 3.9, 0.22]) cylinder(h = 3.0, r = 0.2, $fn = 8);
        color(oc_PLASTER()) translate([0, 0, 0.22]) oc_slab(8.2, 8.2, 3.0);
        color(oc_REDD()) for (a = [0, 90]) rotate([0, 0, a]) for (sy = [-1, 1])
            translate([0, sy * 4.12, 2.0]) oc_boxc([6.0, 0.14, 1.1]);
        color(oc_WOODD()) translate([0, 0, 3.45]) oc_boxc([9.0, 9.0, 0.45]);
        translate([0, 0, 3.65]) oc_part_roof(8.4, 8.4, 1.5, 1.0, 2.4, oc_ROOFC());
        color(oc_PLASTER()) translate([0, 0, 4.1]) oc_slab(5.6, 5.6, 2.2);
        color(oc_WOODD()) translate([0, 0, 6.1]) oc_boxc([6.4, 6.4, 0.4]);
        translate([0, 0, 6.3]) oc_part_roof(6.0, 6.0, 2.6, 0.9, 3.0, oc_ROOFC());
        color(oc_GOLDC()) translate([0, 0, 8.6]) cylinder(h = 0.5, r = 0.14, $fn = 6);
        color(oc_GOLDC()) translate([0, 0, 9.05]) sphere(r = 0.38, $fn = 8);
    }
}

// 马面敌台：外凸 4.3m，三面垛墙；flag=0 立旗 / 1 建铺房哨舍。外侧 = +y。
module oc_wall_bastion(flag = 0)
{
    color(oc_STONED()) translate([0, 3.0, 0.6]) oc_boxc([12.8, 10.4, 1.2]);
    color(oc_STONEC()) translate([0, 3.0, oc_WH() / 2]) oc_boxc([12, 9.6, oc_WH()]);
    color(oc_PAVED()) translate([0, 3.25, oc_WH() + 0.06]) oc_boxc([12, 9.2, 0.12]);
    translate([0, 7.55, oc_WH() + 0.12]) oc_part_battlement(11.6);
    for (sx = [-1, 1])
        translate([sx * 5.75, 3.4, oc_WH() + 0.12]) rotate([0, 0, 90]) oc_part_battlement(7.6);
    if (flag == 0)
        translate([0, 4.6, oc_WH() + 0.12]) oc_prop_flag(oc_REDC(), 4.6);
    else
    {
        color(oc_PLASTER()) translate([0, 4.2, oc_WH() + 0.12]) oc_slab(4.6, 3.2, 2.2);
        translate([0, 4.2, oc_WH() + 2.32]) oc_part_roof(4.6, 3.2, 1.1, 0.6, 0, oc_ROOFC());
        translate([0, 4.2, oc_WH() + 3.3]) oc_part_ridge(5.4);
    }
}

// 登城马道：沿 +x 上行（x∈[0,len] 从地面升至墙顶步道），靠墙侧 = +y，外缘矮栏。
// 顶端 x∈[len,len+4] 为登城平台，与墙顶海墁同高（女墙在该区间留豁口）。
module oc_wall_ramp(len = 24, w = 3.0)
{
    up = oc_WH() + 0.12;
    ang = atan(up / len);
    rl = sqrt(len * len + up * up) + 0.4;
    color(oc_STONEC()) translate([len / 2, 0, up / 2 - 0.28]) rotate([0, -ang, 0]) oc_boxc([rl, w, 0.6]);
    color(oc_STONED()) translate([len / 2, -w / 2 + 0.15, up / 2 + 0.32]) rotate([0, -ang, 0]) oc_boxc([rl, 0.3, 1.1]);
    for (i = [1 : 3])
        color(oc_STONED()) translate([len * i / 4, 0, up * i / 8]) oc_boxc([2.0, w - 0.2, up * i / 4]);
    color(oc_STONEC()) translate([len + 2.0, 0, up / 2]) oc_boxc([4.0, w, up]);
    color(oc_PAVED()) translate([len + 2.0, 0, up + 0.04]) oc_boxc([4.0, w, 0.08]);
    color(oc_STONED()) translate([len + 2.0, -w / 2 + 0.15, up + 0.55]) oc_boxc([4.0, 0.3, 1.0]);
}

// ================= 建筑库（front = -y；底面 z=0） =================

// 宫墙段（沿 x，石身 + 两坡瓦压顶）
module oc_wall_palace_run(len)
{
    color(oc_STONEC()) translate([0, 0, 2.1]) oc_boxc([len, 1.2, 4.2]);
    translate([0, 0, 4.2]) oc_part_roof(len - 0.4, 1.2, 0.55, 0.3, 0, oc_ROOFC());
}

// 宫墙角柱
module oc_wall_palace_pier()
{
    color(oc_STONED()) translate([0, 0, 2.3]) oc_boxc([1.8, 1.8, 4.6]);
    translate([0, 0, 4.6]) oc_part_roof(1.9, 1.9, 0.5, 0.2, 0.95, oc_ROOFD());
}

// 宫城南门：门屋跨墙 + 常开门扇 + 门枕 + 灯笼（front=-y 朝城外）
module oc_bldg_inner_gate()
{
    for (sx = [-1, 1])
    {
        color(oc_STONEC()) translate([sx * 4.0, 0, 2.25]) oc_boxc([2.4, 2.0, 4.5]);
        color(oc_REDD()) translate([sx * 2.55, 0.6, 2.0]) oc_boxc([0.2, 2.2, 4.0]);
        color(oc_STONEL()) translate([sx * 2.7, -1.3, 0.3]) oc_boxc([0.7, 1.2, 0.6]);
    }
    color(oc_REDW()) translate([0, 0, 5.0]) oc_boxc([10.6, 2.4, 1.0]);
    color(oc_WOODD()) translate([0, 0, 5.75]) oc_boxc([11.2, 3.0, 0.5]);
    translate([0, 0, 6.0]) oc_part_roof(10.8, 3.2, 1.6, 0.9, 0, oc_ROOFC());
    translate([0, 0, 7.45]) oc_part_ridge(11.2);
    for (sx = [-1, 1]) translate([sx * 3.6, -1.7, 3.6]) oc_prop_lantern();
}

// 主城正殿：三重台基 + 月台三出台阶 + 前廊柱列 + 重檐庑殿顶 + "主城"匾
module oc_bldg_keep()
{
    color(oc_STONEL()) oc_slab(31, 21, 0.9);
    color(oc_STONEC()) translate([0, 0, 0.9]) oc_slab(28, 18, 0.9);
    color(oc_STONEL()) translate([0, -12.5, 0]) oc_slab(14, 6, 1.55);
    translate([0, -15.5, 0]) oc_part_steps(5, 5, 0.31, 0.45);
    for (sx = [-1, 1]) translate([sx * 8.5, -12, 0]) rotate([0, 0, sx * 90]) oc_part_steps(3.4, 5, 0.31, 0.45);
    PB = 1.8;
    color(oc_REDW()) for (i = [-4 : 4]) translate([i * 2.7, -7.4, PB]) cylinder(h = 5.0, r = 0.30, $fn = 8);
    color(oc_PLASTER()) translate([0, 0.8, PB]) oc_slab(23, 12.5, 5.0);
    color(oc_REDD()) translate([0, -5.55, PB + 1.6]) oc_boxc([16, 0.25, 3.2]);
    color(oc_GOLDC()) for (i = [-3 : 3]) translate([i * 2.25, -5.70, PB + 1.6]) oc_boxc([0.10, 0.06, 3.0]);
    color(oc_WOODD()) translate([0, 0, PB + 5.2]) oc_boxc([25.5, 14.5, 0.55]);
    translate([0, 0, PB + 5.45]) oc_part_roof(24.5, 13.5, 2.4, 1.6, 4.6, oc_ROOFC());
    color(oc_PLASTER()) translate([0, 0.5, PB + 6.9]) oc_slab(16, 8.5, 2.7);
    color(oc_REDD()) translate([0, 0.5 - 4.27, PB + 8.5]) oc_boxc([12, 0.14, 1.3]);
    color(oc_WOODD()) translate([0, 0.5, PB + 9.5]) oc_boxc([17.5, 10, 0.5]);
    translate([0, 0.5, PB + 9.72]) oc_part_roof(17, 9.5, 3.2, 1.4, 3.8, oc_ROOFC());
    translate([0, 0.5, PB + 12.72]) oc_part_ridge(10.1);
    translate([0, -7.78, PB + 4.35]) oc_part_plaque("主城", 2, 1.0);
}

// 配殿 / 后殿（单檐四坡）
module oc_bldg_side_hall(L = 11, D = 6, seed = 0)
{
    color(oc_STONEL()) oc_slab(L + 1.2, D + 1.2, 0.5);
    color(oc_REDW()) for (i = [-2 : 2]) translate([i * (L - 0.9) / 4, -D / 2 + 0.28, 0.5]) cylinder(h = 3.0, r = 0.16, $fn = 8);
    color(oc_PLASTER()) translate([0, 0.25, 0.5]) oc_slab(L - 0.5, D - 0.9, 3.0);
    color(oc_REDD()) translate([0, -D / 2 + 0.05, 1.9]) oc_boxc([L - 2.8, 0.16, 1.8]);
    color(oc_WOODD()) translate([0, 0, 3.6]) oc_boxc([L + 0.6, D + 0.6, 0.4]);
    translate([0, 0, 3.8]) oc_part_roof(L, D, 1.9, 0.9, 2.0, oc_ROOFC());
    translate([0, 0, 5.55]) oc_part_ridge(L - 3.6);
}

// 民居（seed 定墙色/屋型/瓦色；L/D 可微调）
module oc_bldg_house(seed = 0, L = 9, D = 6.5)
{
    wh = 2.7;
    color(oc_STONED()) oc_slab(L + 0.6, D + 0.6, 0.3);
    color(oc_house_c(seed)) translate([0, 0, 0.3]) oc_slab(L, D, wh);
    color(oc_WOODD())
    {
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.3), -D / 2 - 0.04, 0.3 + wh / 2]) oc_boxc([0.24, 0.08, wh]);
        translate([0, 0, 0.3 + wh - 0.19]) oc_boxc([L + 0.3, D + 0.3, 0.38]);
        translate([0, -D / 2 - 0.06, 1.35]) oc_boxc([1.5, 0.12, 2.1]);
    }
    color(oc_WOODC()) translate([0, -D / 2 - 0.10, 1.3]) oc_boxc([1.2, 0.08, 1.9]);
    for (sx = [-1, 1]) translate([sx * L * 0.30, -D / 2, 1.75]) oc_part_lattice_win(1.5, 1.1);
    rh = 1.6 + 0.25 * oc_rnd(seed, 3);
    ri = (oc_rnd(seed + 3, 3) == 0) ? 2.2 : 0;
    translate([0, 0, 0.3 + wh])
    {
        oc_part_roof(L, D, rh, 0.8, ri, (oc_rnd(seed + 5, 4) == 0) ? oc_ROOFD() : oc_ROOFC());
        translate([0, 0, rh - 0.12]) oc_part_ridge((ri > 0 ? L - 2 * ri : L) + 0.5);
    }
}

// 酒楼：两层 + 挑台栏杆 + "酒楼"匾 + 酒旗 + 灯笼（front=-y）
module oc_bldg_inn()
{
    L = 12;
    D = 9;
    color(oc_STONED()) oc_slab(L + 0.8, D + 0.8, 0.4);
    color(oc_PLASTER()) translate([0, 0, 0.4]) oc_slab(L, D, 3.2);
    color(oc_REDW()) for (i = [-2 : 2]) translate([i * 2.6, -D / 2 + 0.15, 0.4]) cylinder(h = 3.2, r = 0.16, $fn = 8);
    color(oc_WOODD()) translate([0, -D / 2 - 0.06, 1.5]) oc_boxc([2.2, 0.12, 2.4]);
    color(oc_DARKC()) translate([0, -D / 2 - 0.10, 1.45]) oc_boxc([1.9, 0.06, 2.2]);
    for (sx = [-1, 1]) translate([sx * 3.9, -D / 2, 1.95]) oc_part_lattice_win(1.7, 1.3);
    color(oc_WOODC()) translate([0, -0.7, 3.75]) oc_boxc([L + 0.5, D + 2.2, 0.3]);
    color(oc_WOODD()) for (i = [-5 : 5]) translate([i * 1.15, -D / 2 - 1.6, 4.3]) oc_boxc([0.09, 0.09, 0.85]);
    color(oc_WOODD()) translate([0, -D / 2 - 1.6, 4.75]) oc_boxc([L + 0.4, 0.12, 0.12]);
    color(oc_house_c(7)) translate([0, 0.3, 3.9]) oc_slab(L - 1.2, D - 1.6, 2.7);
    for (sx = [-1, 0, 1]) translate([sx * 3.4, 0.3 - (D - 1.6) / 2, 5.45]) oc_part_lattice_win(1.8, 1.3);
    color(oc_WOODD()) translate([0, 0.3, 6.7]) oc_boxc([L - 0.4, D - 1.0, 0.45]);
    translate([0, 0.3, 6.9]) oc_part_roof(L - 0.9, D - 1.4, 2.2, 1.0, 2.4, oc_ROOFC());
    translate([0, 0.3, 8.95]) oc_part_ridge(6.8);
    translate([0, -D / 2 - 0.15, 3.0]) oc_part_plaque("酒楼", 2, 0.62);
    color(oc_WOODD()) translate([-L / 2 - 0.9, -D / 2 - 0.6, 0]) cylinder(h = 7.6, r = 0.09, $fn = 6);
    color(oc_WOODD()) translate([-L / 2 - 0.9, -D / 2 - 0.6, 7.3]) rotate([90, 0, 0]) cylinder(h = 0.9, r = 0.05, $fn = 6);
    color(oc_CANVW()) translate([-L / 2 - 0.9, -D / 2 - 1.35, 6.1]) oc_boxc([0.07, 0.75, 2.2]);
    translate([-L / 2 - 0.95, -D / 2 - 1.35, 6.3]) rotate([0, 0, -90]) oc_part_text_cn("酒", 1, 0.6, oc_REDD());
    for (sx = [-1, 1]) translate([sx * (L / 2 - 0.6), -D / 2 - 1.2, 3.7]) oc_prop_lantern();
}

// 兵营（长屋双门 + 高窗 + 角旗）
module oc_bldg_barracks(seed = 0)
{
    L = 18;
    D = 8;
    color(oc_STONED()) oc_slab(L + 0.7, D + 0.7, 0.35);
    color(oc_PLASTER()) translate([0, 0, 0.35]) oc_slab(L, D, 3.1);
    color(oc_WOODD())
    {
        translate([0, 0, 3.26]) oc_boxc([L + 0.4, D + 0.4, 0.4]);
        for (i = [-2 : 2]) translate([i * 4.2, -D / 2 - 0.03, 1.7]) oc_boxc([0.25, 0.1, 3.0]);
    }
    for (sx = [-1, 1])
    {
        color(oc_WOODD()) translate([sx * 5.5, -D / 2 - 0.05, 1.35]) oc_boxc([2.0, 0.06, 2.5]);
        color(oc_REDD()) translate([sx * 5.5, -D / 2 - 0.10, 1.3]) oc_boxc([1.7, 0.08, 2.3]);
    }
    for (x = [-8, -2, 2, 8]) translate([x, -D / 2, 2.55]) oc_part_lattice_win(1.1, 0.8);
    translate([0, 0, 3.46]) oc_part_roof(L, D, 2.3, 0.85, 0, oc_ROOFC());
    translate([0, 0, 5.62]) oc_part_ridge(L + 0.5);
    for (sx = [-1, 1]) translate([sx * (L / 2 + 1.2), -D / 2 + 0.4, 0]) oc_prop_flag(oc_REDC(), 5);
}

// 仓库（高台基防潮 + 板门 + 高通风窗 + 蓝瓦四坡顶）
module oc_bldg_warehouse(seed = 0)
{
    L = 15;
    D = 10;
    color(oc_STONED()) oc_slab(L + 0.8, D + 0.8, 0.75);
    translate([0, -(D + 0.8) / 2, 0]) oc_part_steps(3.4, 3, 0.25, 0.4);
    color(oc_PLASTER()) translate([0, 0, 0.75]) oc_slab(L, D, 3.6);
    color(oc_WOODD())
    {
        translate([0, 0, 4.14]) oc_boxc([L + 0.4, D + 0.4, 0.42]);
        for (i = [-2 : 2]) translate([i * 3.4, -D / 2 - 0.03, 2.55]) oc_boxc([0.28, 0.1, 3.6]);
        translate([0, -D / 2 - 0.05, 2.2]) oc_boxc([3.4, 0.09, 2.9]);
    }
    color(oc_WOODC()) for (sx = [-1, 1]) translate([sx * 0.78, -D / 2 - 0.09, 2.15]) oc_boxc([1.5, 0.07, 2.7]);
    color(oc_DARKC()) for (x = [-5, 5]) translate([x, -D / 2 - 0.06, 3.7]) oc_boxc([1.3, 0.05, 0.6]);
    translate([0, 0, 4.35]) oc_part_roof(L, D, 2.5, 0.95, 2.6, oc_ROOFB());
    translate([0, 0, 6.75]) oc_part_ridge(10.3, [0.24, 0.30, 0.40]);
}

// 圆囤粮仓（茅草锥顶）
module oc_bldg_granary()
{
    color(oc_STONED()) cylinder(h = 0.4, r = 3.3, $fn = 10);
    color(oc_PLASTER()) translate([0, 0, 0.4]) cylinder(h = 3.2, r = 2.8, $fn = 10);
    color(oc_WOODD()) translate([0, -2.72, 1.5]) oc_boxc([1.1, 0.35, 2.2]);
    color(oc_STRAWC()) translate([0, 0, 3.6]) cylinder(h = 2.0, r1 = 3.5, r2 = 0.35, $fn = 10);
    color(oc_WOODD()) translate([0, 0, 5.6]) cylinder(h = 0.5, r = 0.12, $fn = 6);
}

// 市场摊位（front=-y 顾客侧；seed 定棚色/货色）
module oc_bldg_stall(seed = 0)
{
    color(oc_WOODD()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 1.7, sy * 1.3, 1.1]) oc_boxc([0.14, 0.14, 2.2]);
    color(oc_WOODC()) translate([0, -1.0, 0.5]) oc_boxc([3.6, 1.0, 1.0]);
    color(oc_WOODC()) translate([0, 0.9, 0.4]) oc_boxc([3.2, 1.0, 0.8]);
    translate([0, 0, 2.2]) oc_part_roof(3.6, 2.9, 0.8, 0.45, 0, oc_canv_c(seed));
    color(oc_CANVW()) translate([0, 0, 2.96]) oc_boxc([4.3, 0.2, 0.10]);
    for (i = [0 : 2]) color(oc_goods_c(seed + i)) translate([-1.1 + i * 1.1, -1.0, 1.18]) oc_boxc([0.8, 0.7, 0.36]);
    color(oc_STRAWC()) translate([-0.9, 0.9, 0.95]) scale([1, 1, 0.75]) sphere(r = 0.45, $fn = 7);
    color(oc_STRAWC()) translate([0.5, 0.9, 0.92]) scale([1, 1, 0.75]) sphere(r = 0.42, $fn = 7);
    translate([2.3, -0.9, 0]) oc_prop_jar();
}

// 箭塔（石基收分 + 木挑台栏杆 + 哨舱 + 攒尖顶 + 红旗）
module oc_bldg_tower_watch()
{
    color(oc_STONED()) translate([0, 0, 0.9]) oc_boxc([5.6, 5.6, 1.8]);
    color(oc_STONEC()) translate([0, 0, 3.3]) oc_boxc([4.6, 4.6, 3.0]);
    color(oc_STONEC()) translate([0, 0, 5.9]) oc_boxc([3.9, 3.9, 2.2]);
    color(oc_WOODC()) translate([0, 0, 7.15]) oc_boxc([5.4, 5.4, 0.3]);
    color(oc_WOODD()) for (a = [0, 90]) rotate([0, 0, a]) for (sy = [-1, 1])
    {
        translate([0, sy * 2.6, 7.75]) oc_boxc([5.3, 0.09, 0.10]);
        for (i = [-2 : 2]) translate([i * 1.05, sy * 2.6, 7.55]) oc_boxc([0.08, 0.08, 0.55]);
    }
    color(oc_PLASTER()) translate([0, 0, 8.5]) oc_boxc([3.4, 3.4, 2.4]);
    for (a = [0, 90, 180, 270]) rotate([0, 0, a]) translate([0, -1.7, 8.95]) oc_part_lattice_win(1.3, 0.9);
    color(oc_WOODD()) translate([0, 0, 9.87]) oc_boxc([4.0, 4.0, 0.35]);
    translate([0, 0, 10.02]) oc_part_roof(3.9, 3.9, 1.7, 0.75, 1.95, oc_ROOFC());
    translate([0, 0, 11.6]) oc_prop_flag(oc_REDC(), 2.6);
}

// 军帐（方锥帐 + 门帘 + 顶端小旗）
module oc_bldg_tent(seed = 0)
{
    oc_part_roof(4.0, 4.0, 2.9, 0.05, 2.0, (oc_rnd(seed, 3) == 0) ? [0.82, 0.78, 0.66] : oc_CANVW());
    color(oc_ROOFD()) translate([0, -1.6, 0.62]) rotate([34, 0, 0]) oc_boxc([0.9, 0.14, 1.3]);
    color(oc_WOODD()) translate([0, 0, 2.7]) cylinder(h = 0.75, r = 0.05, $fn = 6);
    color(oc_REDC()) translate([0.02, 0.26, 3.28]) oc_boxc([0.04, 0.48, 0.32]);
}

// 草棚（开敞柴棚/马棚）
module oc_bldg_shed(L = 6, D = 4)
{
    color(oc_WOODD()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (L / 2 - 0.2), sy * (D / 2 - 0.2), 1.1]) oc_boxc([0.16, 0.16, 2.2]);
    translate([0, 0, 2.2]) oc_part_roof(L, D, 0.9, 0.5, 0, oc_STRAWC());
    color(oc_STRAWC()) translate([0, 0.5, 0.42]) scale([1, 1, 0.7]) sphere(r = 0.8, $fn = 7);
    color(oc_WOODC()) translate([-1.4, -0.6, 0.3]) oc_boxc([1.6, 1.0, 0.6]);
}

// 农田（田埂 + 五垄作物；seed 三分之一概率为熟穗黄）
module oc_farm_plot(seed = 0, L = 13, D = 8)
{
    color(oc_SOILD()) oc_slab(L + 0.8, D + 0.8, 0.22);
    color(oc_SOILC()) translate([0, 0, 0.22]) oc_slab(L, D, 0.10);
    ripe = oc_rnd(seed, 3) == 0;
    for (iy = [0 : 4])
    {
        yy = -D / 2 + D / 10 + iy * D / 5;
        color(ripe ? oc_CROPY() : [0.40 + 0.04 * oc_rnd(seed + iy, 3), 0.60 + 0.05 * oc_rnd(seed + iy + 1, 3), 0.26])
            translate([0, yy, 0.32]) oc_boxc([L - 0.9, D / 5 * 0.55, 0.30]);
        color(ripe ? [0.92, 0.80, 0.38] : [0.52, 0.75, 0.32])
            translate([0, yy, 0.50]) oc_boxc([L - 1.3, D / 5 * 0.34, 0.10]);
    }
}

// ================= 地面 =================

// 展台底座 + 地形草地 + 城基台地
module oc_ground_base()
{
    color(oc_BASEC()) translate([0, 0, -2.9]) oc_boxc([352, 352, 5.8]);
    color(oc_GRASSC()) oc_slab(340, 340, oc_TZ());
    for (i = [0 : 15])
        color(oc_GRASSD()) translate([-160 + oc_rnd(i * 7, 320), -160 + oc_rnd(i * 13 + 3, 320), oc_TZ()])
            oc_slab(9 + oc_rnd(i, 12), 7 + oc_rnd(i + 5, 10), 0.02);
    color(oc_DIRTC()) translate([0, 0, oc_TZ()]) oc_slab(199, 199, oc_CZ() - oc_TZ());
}

// 四向城外官道（出四门通往地图边缘，含车辙）
module oc_ground_roads_out()
{
    for (a = [0, 90, 180, 270]) rotate([0, 0, a])
    {
        color(oc_DIRTD()) translate([0, 135, oc_TZ()]) oc_slab(7, 71, 0.05);
        color([0.66, 0.58, 0.42]) for (sx = [-1, 1]) translate([sx * 2.4, 135, oc_TZ() + 0.05]) oc_slab(0.5, 71, 0.012);
    }
}

// 西南水塘（滩涂 + 水面 + 浅水高光 + 芦苇 + 岸石）
module oc_ground_pond()
{
    color(oc_SANDC()) translate([-136, -134, oc_TZ()]) oc_slab(58, 44, 0.05);
    color(oc_WATERC()) translate([-136, -134, oc_TZ() + 0.02]) oc_slab(48, 34, 0.02);
    color([0.38, 0.66, 0.78]) translate([-143, -138, oc_TZ() + 0.04]) oc_slab(16, 9, 0.012);
    for (i = [0 : 5])
        color([0.42, 0.60, 0.30]) translate([-117 + oc_rnd(i * 3, 5), -148 + i * 6, oc_TZ() + 0.04])
            cylinder(h = 1.1 + 0.2 * oc_rnd(i, 3), r = 0.05, $fn = 5);
    translate([-158, -114, oc_TZ()]) oc_nature_rock(1.4, 3);
    translate([-112, -152, oc_TZ()]) oc_nature_rock(1.0, 8);
}

// 城内街面：宫城环街 + 十字大街（御道中线）+ 门内广场 + 街坊巷道
module oc_ground_city()
{
    color(oc_PAVEC())
    {
        for (sy = [-1, 1]) translate([0, sy * 38, oc_CZ()]) oc_slab(84, 8, 0.05);
        for (sx = [-1, 1]) translate([sx * 38, 0, oc_CZ()]) oc_slab(8, 68, 0.05);
        for (sy = [-1, 1]) translate([0, sy * 59, oc_CZ()]) oc_slab(10, 34, 0.05);
        for (sx = [-1, 1]) translate([sx * 59, 0, oc_CZ()]) oc_slab(34, 10, 0.05);
        translate([0, -31, oc_CZ()]) oc_slab(10, 6, 0.05);
        for (sy = [-1, 1]) translate([0, sy * 82, oc_CZ()]) oc_slab(20, 12, 0.05);
        for (sx = [-1, 1]) translate([sx * 82, 0, oc_CZ()]) oc_slab(12, 20, 0.05);
        translate([0, 0, oc_CZ()]) oc_slab(68, 68, 0.04);
    }
    color(oc_PAVED())
    {
        for (sy = [-1, 1]) translate([0, sy * 59, oc_CZ() + 0.05]) oc_slab(3, 34, 0.014);
        for (sx = [-1, 1]) translate([sx * 59, 0, oc_CZ() + 0.05]) oc_slab(34, 3, 0.014);
    }
    color(oc_DIRTD()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 46, sy * 63, oc_CZ()]) oc_slab(6, 42, 0.03);
}
