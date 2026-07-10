// old_city.scad —— 低多边形古代城池（参考低多边形中式围城示意图）
//
// 人尺度：1 unit = 1 m，OpenSCAD Z-up，+y 北 / -y 南；带朝向 module 约定 front = -y。
// 落地件底面 z=0，布局时 translate 到所在台面高度。角色（ScadRig，身高 ~1.7m）可直接行走：
// 门洞净宽 8 净高 6、马道坡度 ~21°、墙顶步道净宽 ~5m、街道 6~10m。
//
// 总平面（城墙外皮 192 x 192：x,y∈[-96,96]；地形 340 x 340）：
//   城外  四向官道通往地图边缘；西南水塘、西北石山、东北土丘、四周散树/岩石
//   城墙  高 9 厚 7；四角角楼、四面中央城门楼（北/东/南/西）、每面 2 座马面敌台
//         北门东侧 / 南门西侧各一条登城马道（可走上墙顶巡逻道）
//   城内  十字大街（宽 10，连四门）+ 宫城环街（宽 8）+ 巷道（宽 6）+ 顺城街（宽 5）
//   宫城  中央 56x56 内城（主城正殿 + 东西配殿 + 后殿 + 南宫门），环街四角箭塔
//   街区  西北民居 | 北仓库群(圆囤) | 东北民居 | 东北角民居
//         西侧兵营/校场 + 军帐营地 | 东侧民居 + 市场(彩棚摊位)
//         西南农田 | 南农田/果园 | 南酒楼 | 东南农田/打谷场
//
// 标高：地形草地顶 z=0.30(TZ)；城基台地顶 z=0.50(CZ)；街面 CZ+0.05；墙顶步道 CZ+9.12。
//
// POI 锚点（命名约定与 airport.scad / habor_city_hd.scad 同构）：
//   锚点 = 具名 user module，加载后节点名 = module 名，WorldTranslation = 点位坐标。
//   锚点调用必须 translate(...) 在 rotate(...) 外层。锚点即建筑/设施本体或点位标石。
//   —— 攻防 / 巡逻 玩法 ——
//     gate_01..04    城门楼（北/东/南/西，含墩台+城楼+常开门扇）
//     tower_01..04   角楼（NW/NE/SE/SW，墙顶平台 + 两层攒尖楼）
//     watch_01..04   箭塔（宫城环街四角，独立哨塔）
//     ramp_01..02    马道上口界石（北门东侧 / 南门西侧，墙顶标高）
//     node_01..08    路网节点界石（01..04 环街四角 NW/NE/SE/SW，05..08 四门内广场 北/东/南/西）
//     spawn_01..04   城外官道生成点（四门外 ~22m 路面道钉，北/东/南/西）
//   —— 经营 玩法 ——
//     keep_01        主城（正殿，重檐庑殿 + 三重台基 + "主城"匾）
//     innergate_01   宫城南门（门屋 + 常开门扇）
//     barracks_01..02 兵营   warehouse_01..02 仓库   inn_01 酒楼
//     house_01..16   民居    farm_01..08 农田        market_01..08 市场摊位
//     well_01..02    水井
//   非锚点一律 ground_* / part_* / wall_* / bldg_* / prop_* / farm_* / nature_* 前缀。
//
// 构造约定（沿用 airport.scad / habor_city_hd.scad 经验）：
//   * 仅用引擎 SCADLoader 已支持特性（无 offset/projection/minkowski/import/resize）；
//   * 避免 difference：门洞用分段墩台 + 过梁拼出，屋面用 polyhedron 画家叠层；
//   * 伪随机 rnd(i,m) 整数散列，确定性（引擎与 OpenSCAD 渲染一致）；
//   * text() 只用于匾额（CJK 走 DroidSansFallback），字宽按全角 size 手动居中（不用 len() 数多字节）。

$fn = 10;

// ================= 标高与结构常量 =================
TZ = 0.30;    // 地形草地顶
CZ = 0.50;    // 城基台地顶（城内地坪）
WT = 7;       // 城墙厚
WH = 9;       // 城墙高（墙身，垛口另加）

// ================= 配色 =================
STONEC  = [0.63, 0.63, 0.60];   // 城砖灰
STONED  = [0.52, 0.52, 0.49];   // 石基深灰
STONEL  = [0.72, 0.72, 0.68];   // 垛口/台基浅灰
PAVEC   = [0.74, 0.72, 0.66];   // 石板街面
PAVED   = [0.64, 0.62, 0.56];   // 海墁/御道深石板
ROOFC   = [0.27, 0.29, 0.33];   // 青瓦
ROOFD   = [0.20, 0.22, 0.26];   // 深瓦/正脊
ROOFB   = [0.33, 0.42, 0.55];   // 仓库蓝瓦
REDW    = [0.58, 0.19, 0.14];   // 朱漆柱
REDD    = [0.45, 0.14, 0.11];   // 深朱门扇
REDC    = [0.78, 0.22, 0.17];   // 旗帜红
PLASTER = [0.89, 0.85, 0.75];   // 灰泥墙
WOODC   = [0.58, 0.42, 0.26];   // 原木
WOODD   = [0.42, 0.29, 0.17];   // 深木构架
GOLDC   = [0.85, 0.70, 0.32];   // 匾金/门钉
DARKC   = [0.12, 0.12, 0.13];   // 洞口/窗芯
DIRTC   = [0.80, 0.73, 0.56];   // 城内夯土
DIRTD   = [0.71, 0.63, 0.46];   // 土路/校场
GRASSC  = [0.58, 0.72, 0.37];   // 草地
GRASSD  = [0.50, 0.65, 0.31];   // 深草斑块
SOILC   = [0.56, 0.43, 0.28];   // 田土
SOILD   = [0.47, 0.36, 0.24];   // 田埂
CROPY   = [0.86, 0.72, 0.30];   // 熟穗黄
STRAWC  = [0.80, 0.68, 0.42];   // 茅草/干草/麻袋
WATERC  = [0.30, 0.58, 0.71];   // 水面
SANDC   = [0.90, 0.82, 0.58];   // 滩涂
ROCKC   = [0.56, 0.56, 0.53];   // 岩石
MOUNTC  = [0.45, 0.46, 0.49];   // 石山
BASEC   = [0.24, 0.21, 0.28];   // 展台底座
CANVW   = [0.90, 0.87, 0.78];   // 帐布米白
PURPC   = [0.56, 0.38, 0.68];   // 市场紫棚

// ---- 伪随机 / 调色板 ----
function rnd(seedValue, m) = (((((seedValue * 73 + 31) % 97 + 97) % 97) * 13) + ((seedValue % 7 + 7) % 7)) % m;
function house_c(i) = [[0.90, 0.86, 0.76], [0.86, 0.80, 0.68], [0.92, 0.90, 0.84], [0.81, 0.77, 0.67]][rnd(i, 4)];
function leaf_c(i)  = [[0.50, 0.72, 0.30], [0.42, 0.64, 0.26], [0.58, 0.78, 0.34], [0.88, 0.68, 0.26], [0.82, 0.55, 0.22]][rnd(i, 5)];
function canv_c(i)  = [PURPC, REDC, CANVW, [0.35, 0.52, 0.62]][rnd(i, 4)];
function goods_c(i) = [[0.85, 0.30, 0.22], [0.95, 0.75, 0.25], [0.45, 0.68, 0.30], [0.90, 0.55, 0.20], [0.75, 0.70, 0.60], [0.55, 0.35, 0.60]][rnd(i, 6)];

// ---- 基础工具 ----
module boxc(s) cube(s, center = true);
module slab(L, D, t) translate([0, 0, t / 2]) boxc([L, D, t]);   // 底面 z=0 平板

// ================= 屋面与建筑公共件 =================

// 通用坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙体外皮，h=脊高，ov=出檐，
// rin=山面内收（0=两坡悬山，0<rin<L/2=四坡，rin>=L/2=攒尖）。
// 面序为 OpenSCAD 约定（从外看顺时针）。
module part_roof(L, D, h, ov = 0.8, rin = 0, c = [0.27, 0.29, 0.33])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// 正脊 + 两端鸱吻（置于脊线标高，rl=含吻兽全长）
module part_ridge(rl, c = [0.20, 0.22, 0.26])
{
    color(c)
    {
        boxc([rl, 0.5, 0.46]);
        for (sx = [-1, 1]) translate([sx * (rl / 2 - 0.2), 0, 0.5]) boxc([0.42, 0.56, 0.7]);
    }
}

// 居中中文字（front=-y 外凸 0.06；n=字数，CJK 全角宽≈size，手动居中避免 len() 数多字节）
module part_text_cn(label, n = 2, size = 0.8, c = [0.85, 0.70, 0.32])
{
    color(c) translate([-n * size * 0.5, -0.02, -size * 0.48])
        rotate([90, 0, 0]) linear_extrude(0.06) text(label, size = size);
}

// 匾额：黑底金字金框（front=-y）
module part_plaque(label, n = 2, size = 0.9)
{
    w = n * size * 1.15 + 0.75;
    h = size + 0.65;
    color(DARKC) translate([0, 0.09, 0]) boxc([w, 0.16, h]);
    color(GOLDC)
    {
        for (sz = [-1, 1]) translate([0, 0.04, sz * (h / 2 - 0.06)]) boxc([w + 0.06, 0.10, 0.12]);
        for (sx = [-1, 1]) translate([sx * (w / 2 - 0.06), 0.04, 0]) boxc([0.12, 0.10, h + 0.06]);
    }
    part_text_cn(label, n, size, GOLDC);
}

// 垛墙：沿 x 通长矮墙 + 等距垛口（merlon 宽 1.3 净距 0.7 高 h）
module part_battlement(len, t = 0.45, h = 1.7)
{
    color(STONEL) translate([0, 0, 0.35]) boxc([len, t, 0.7]);
    n = floor((len - 1.5) / 2) + 1;
    x0 = -(n - 1);
    for (i = [0 : n - 1])
        color(STONEL) translate([x0 + i * 2.0, 0, h / 2]) boxc([1.3, t, h]);
}

// 台阶（自 y=0 向 -y 下行，n 级，级高 rise 级深 run）
module part_steps(w = 4, n = 5, rise = 0.3, run = 0.42)
{
    for (i = [0 : n - 1])
        color(STONEL) translate([0, -(i + 0.5) * run, (n - i) * rise / 2])
            boxc([w, run, (n - i) * rise]);
}

// 木棂窗（贴墙面：墙面在 y=0，窗朝 -y）
module part_lattice_win(w = 1.4, h = 1.1)
{
    color(WOODD) translate([0, -0.05, 0]) boxc([w, 0.10, h]);
    color(DARKC) translate([0, -0.09, 0]) boxc([w - 0.2, 0.05, h - 0.2]);
    color(WOODD)
    {
        translate([0, -0.115, 0]) boxc([w - 0.16, 0.03, 0.09]);
        for (sx = [-1, 0, 1]) translate([sx * (w - 0.3) / 3, -0.115, 0]) boxc([0.07, 0.03, h - 0.16]);
    }
}

// ================= 植被与地景（底面 z=0） =================

// 低多边形团状树（绿为主，seed 混入秋黄）
module nature_tree(s = 1.0, i = 0)
{
    lc = leaf_c(i);
    color([0.44, 0.31, 0.19]) cylinder(h = 1.7 * s, r = 0.15 * s, $fn = 6);
    color(lc) translate([0, 0, 2.3 * s]) sphere(r = 1.05 * s, $fn = 9);
    color(lc) translate([0.6 * s, 0.25 * s, 1.75 * s]) sphere(r = 0.68 * s, $fn = 8);
    color(leaf_c(i + 2)) translate([-0.55 * s, -0.3 * s, 1.85 * s]) sphere(r = 0.62 * s, $fn = 8);
    color(leaf_c(i + 1)) translate([0.05 * s, -0.5 * s, 2.9 * s]) sphere(r = 0.55 * s, $fn = 8);
}

// 松树（三段锥）
module nature_pine(s = 1.0)
{
    color([0.40, 0.28, 0.17]) cylinder(h = 1.1 * s, r = 0.13 * s, $fn = 6);
    color([0.30, 0.52, 0.30]) translate([0, 0, 0.9 * s]) cylinder(h = 1.6 * s, r1 = 1.05 * s, r2 = 0.55 * s, $fn = 8);
    color([0.33, 0.56, 0.32]) translate([0, 0, 2.2 * s]) cylinder(h = 1.4 * s, r1 = 0.80 * s, r2 = 0.35 * s, $fn = 8);
    color([0.37, 0.60, 0.34]) translate([0, 0, 3.3 * s]) cylinder(h = 1.3 * s, r1 = 0.55 * s, r2 = 0.05 * s, $fn = 8);
}

// 半埋岩石（双球拼合）
module nature_rock(s = 1.0, i = 0)
{
    color(ROCKC) scale([1, 0.85, 0.6]) sphere(r = s, $fn = 7);
    color([0.60, 0.60, 0.57]) translate([0.55 * s, -0.3 * s, 0]) scale([1, 0.8, 0.55]) sphere(r = 0.6 * s, $fn = 6);
}

// 绿丘（背景收边）
module nature_hill(r = 20, h = 9)
{
    color([0.52, 0.70, 0.34]) cylinder(h = h, r1 = r, r2 = r * 0.42, $fn = 8);
    color([0.57, 0.75, 0.37]) translate([r * 0.18, -r * 0.12, h * 0.85]) cylinder(h = h * 0.5, r1 = r * 0.40, r2 = r * 0.10, $fn = 7);
}

// 石山（灰岩峰，远景地标）
module nature_mount(s = 1.0)
{
    color(MOUNTC) cylinder(h = 24 * s, r1 = 17 * s, r2 = 1.5 * s, $fn = 7);
    color([0.52, 0.53, 0.56]) translate([9 * s, -4 * s, 0]) cylinder(h = 13 * s, r1 = 9 * s, r2 = 1.2 * s, $fn = 6);
    color([0.60, 0.61, 0.64]) translate([0, 0, 20 * s]) cylinder(h = 4.5 * s, r1 = 3.2 * s, r2 = 0.4 * s, $fn = 6);
}

// 区域散布（树/松/石混合；rect x0..x1, y0..y1，置于地形面）
module nature_scatter(seed = 0, n = 14, x0 = 0, x1 = 10, y0 = 0, y1 = 10)
{
    for (i = [0 : n - 1])
    {
        xx = x0 + rnd(seed * 11 + i * 7, x1 - x0);
        yy = y0 + rnd(seed * 5 + i * 13, y1 - y0);
        k = rnd(seed + i, 6);
        translate([xx, yy, TZ])
        {
            if (k < 3) nature_tree(0.85 + 0.07 * rnd(i, 5), seed + i);
            else if (k < 5) nature_pine(0.9 + 0.07 * rnd(i + 2, 4));
            else nature_rock(0.7 + 0.2 * rnd(i + 4, 4), i);
        }
    }
}

// ================= 街道小品（底面 z=0；带朝向者 front=-y） =================

// 军旗（幡面朝 +y 挑出）
module prop_flag(c = [0.78, 0.22, 0.17], h = 4.5)
{
    color(WOODD) cylinder(h = h, r = 0.06, $fn = 6);
    color(GOLDC) translate([0, 0, h]) sphere(r = 0.09, $fn = 6);
    color(c) translate([0.02, 0.55, h - 0.85]) boxc([0.05, 1.0, 1.5]);
    color(ROOFD) translate([0.02, 0.55, h - 1.66]) boxc([0.06, 1.0, 0.10]);
}

// 挂式红灯笼（挂点在原点，向下垂）
module prop_lantern()
{
    color(WOODD) translate([0, 0, -0.05]) cylinder(h = 0.1, r = 0.03, $fn = 6);
    color(GOLDC) translate([0, 0, -0.14]) cylinder(h = 0.09, r = 0.12, $fn = 8);
    color(REDC) translate([0, 0, -0.44]) scale([1, 1, 1.15]) sphere(r = 0.26, $fn = 8);
    color(GOLDC) translate([0, 0, -0.80]) cylinder(h = 0.08, r = 0.10, $fn = 8);
    color(GOLDC) translate([0, 0, -0.98]) cylinder(h = 0.18, r = 0.02, $fn = 6);
}

// 石灯（街道沿线）
module prop_stone_lamp()
{
    color(STONED) translate([0, 0, 0.1]) boxc([0.6, 0.6, 0.2]);
    color(STONEC) translate([0, 0, 0.85]) boxc([0.24, 0.24, 1.3]);
    color(CANVW) translate([0, 0, 1.66]) boxc([0.4, 0.4, 0.32]);
    color(STONED) translate([0, 0, 1.92]) boxc([0.56, 0.56, 0.2]);
    color(STONEC) translate([0, 0, 2.09]) boxc([0.16, 0.16, 0.14]);
}

// 水井（石圈 + 双柱井架 + 辘轳 + 瓦顶）
module prop_well()
{
    color(STONEC) cylinder(h = 0.85, r1 = 0.88, r2 = 0.78, $fn = 9);
    color(DARKC) translate([0, 0, 0.86]) cylinder(h = 0.02, r = 0.55, $fn = 9);
    color(WOODD) for (sx = [-1, 1]) translate([sx * 0.95, 0, 1.9]) boxc([0.12, 0.12, 1.8]);
    color(WOODC) translate([-0.85, 0, 1.55]) rotate([0, 90, 0]) cylinder(h = 1.7, r = 0.09, $fn = 6);
    translate([0, 0, 2.8]) part_roof(2.6, 1.5, 0.55, 0.2, 0, ROOFC);
    color(WOODC) translate([0.35, 0, 0.98]) boxc([0.3, 0.3, 0.25]);
}

// 陶缸
module prop_jar()
{
    color([0.36, 0.27, 0.21]) translate([0, 0, 0.42]) scale([1, 1, 1.05]) sphere(r = 0.42, $fn = 8);
    color([0.42, 0.32, 0.25]) translate([0, 0, 0.78]) cylinder(h = 0.1, r = 0.26, $fn = 8);
    color(DARKC) translate([0, 0, 0.86]) cylinder(h = 0.02, r = 0.20, $fn = 8);
}

// 木箱堆 + 麻袋
module prop_crates(seed = 0)
{
    color(WOODC) translate([0, 0, 0.4]) boxc([0.8, 0.8, 0.8]);
    color(WOODD) translate([0.85, 0.1, 0.35]) rotate([0, 0, 14]) boxc([0.7, 0.7, 0.7]);
    if (rnd(seed, 2) == 0)
        color(WOODC) translate([0.3, 0.05, 1.1]) rotate([0, 0, -10]) boxc([0.7, 0.7, 0.6]);
    color(STRAWC) translate([-0.3, 0.75, 0.30]) scale([1, 1, 0.8]) sphere(r = 0.38, $fn = 7);
}

// 木板车（车把朝 -y；seed 决定是否载干草）
module prop_cart(seed = 0)
{
    color(WOODC) translate([0, 0.1, 0.72]) boxc([1.5, 2.6, 0.12]);
    for (sx = [-1, 1])
    {
        color(WOODD) translate([sx * 0.6, 0.1, 0.88]) boxc([0.1, 2.6, 0.28]);
        color(WOODD) translate([sx * 0.55, -1.55, 0.55]) rotate([65, 0, 0]) boxc([0.08, 0.08, 1.5]);
        color(WOODD) translate([sx * 0.8, 0.3, 0.55]) rotate([0, 90, 0]) cylinder(h = 0.1, r = 0.55, $fn = 9);
    }
    color(WOODD) translate([-0.9, 0.3, 0.55]) rotate([0, 90, 0]) cylinder(h = 1.8, r = 0.05, $fn = 6);
    if (rnd(seed, 2) == 0)
        color(STRAWC) translate([0, 0.3, 1.05]) scale([1.1, 1.6, 0.75]) sphere(r = 0.55, $fn = 8);
}

// 干草垛
module prop_hay()
{
    color([0.72, 0.60, 0.36]) translate([0, 0, 0.02]) cylinder(h = 0.5, r1 = 1.15, r2 = 0.95, $fn = 9);
    color(STRAWC) cylinder(h = 1.9, r1 = 1.05, r2 = 0.12, $fn = 9);
}

// 兵器架（斜靠红缨长枪）
module prop_rack()
{
    color(WOODD)
    {
        for (sx = [-1, 1]) translate([sx * 1.1, 0, 0.75]) boxc([0.12, 0.12, 1.5]);
        translate([0, 0, 1.42]) boxc([2.5, 0.10, 0.14]);
        translate([0, 0, 0.25]) boxc([2.5, 0.35, 0.10]);
    }
    for (i = [0 : 4])
        translate([-0.85 + i * 0.42, 0.14, 0]) rotate([-14, 0, 0])
        {
            color(WOODC) cylinder(h = 2.5, r = 0.035, $fn = 5);
            color([0.75, 0.77, 0.80]) translate([0, 0, 2.5]) cylinder(h = 0.3, r1 = 0.07, r2 = 0.01, $fn = 5);
            color(REDC) translate([0, 0, 2.46]) sphere(r = 0.07, $fn = 5);
        }
}

// 箭靶（靶面朝 -y）
module prop_target()
{
    color(WOODD) for (sx = [-1, 1]) translate([sx * 0.4, 0.28, 0.9]) rotate([16, 0, 0]) boxc([0.1, 0.1, 1.9]);
    color(CANVW) translate([0, 0.06, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.62, $fn = 10);
    color(REDC) translate([0, -0.07, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.03, r = 0.38, $fn = 8);
    color(DARKC) translate([0, -0.11, 1.1]) rotate([90, 0, 0]) cylinder(h = 0.03, r = 0.15, $fn = 8);
}

// 铜香炉（殿前）
module prop_incense()
{
    color([0.34, 0.38, 0.35]) cylinder(h = 0.35, r = 0.42, $fn = 8);
    color([0.42, 0.46, 0.42]) translate([0, 0, 0.35]) cylinder(h = 0.75, r1 = 0.60, r2 = 0.52, $fn = 8);
    translate([0, 0, 1.1]) part_roof(1.35, 1.35, 0.5, 0.1, 0.67, ROOFD);
    color(GOLDC) translate([0, 0, 1.62]) sphere(r = 0.1, $fn = 6);
}

// 石牌坊（跨御道，front=-y）
module prop_paifang()
{
    for (sx = [-1, 1])
    {
        color(STONEC) translate([sx * 3.5, 0, 2.6]) boxc([0.55, 0.55, 5.2]);
        color(STONED) translate([sx * 3.5, 0, 0.5]) boxc([1.1, 1.4, 1.0]);
    }
    color(STONED) translate([0, 0, 4.9]) boxc([8.6, 0.5, 0.55]);
    color(STONEC) translate([0, 0, 5.55]) boxc([7.6, 0.45, 0.5]);
    translate([0, 0, 5.85]) part_roof(7.4, 1.2, 0.62, 0.35, 0, ROOFC);
    translate([0, 0, 6.5]) part_roof(3.2, 1.3, 0.55, 0.25, 0, ROOFC);
}

// 路网节点界石（八角石盘 + 中心钉；node 锚点用）
module prop_marker()
{
    color(STONEL) cylinder(h = 0.07, r = 0.85, $fn = 8);
    color(DARKC) translate([0, 0, 0.07]) cylinder(h = 0.02, r = 0.14, $fn = 8);
}

// 生成点道钉（三枚白石横跨路面，沿 x 排布；spawn 锚点用）
module prop_spawn_stone()
{
    for (i = [-1 : 1]) color([0.93, 0.92, 0.88]) translate([i * 0.8, 0, 0.06]) boxc([0.5, 0.2, 0.12]);
}

// ================= 城墙系统（沿 x 向 module，外侧 = +y；底面 z=0） =================

// 城墙段：基座收分 + 墙身 + 顶面海墁 + 外侧垛墙 + 内侧女墙。
// g0/g1：内侧女墙豁口区间（局部 x 坐标，g1>g0 时留口接马道；默认无口）。
module wall_run(len, g0 = 0, g1 = 0)
{
    color(STONED) translate([0, 0, 0.6]) boxc([len, WT + 1.4, 1.2]);
    color(STONEC) translate([0, 0, WH / 2]) boxc([len, WT, WH]);
    color(PAVED) translate([0, 0, WH + 0.06]) boxc([len, WT - 0.8, 0.12]);
    translate([0, WT / 2 - 0.25, WH + 0.12]) part_battlement(len);
    if (g1 > g0)
    {
        color(STONEL) translate([(-len / 2 + g0) / 2, -(WT / 2 - 0.25), WH + 0.62]) boxc([g0 + len / 2, 0.4, 1.0]);
        color(STONEL) translate([(g1 + len / 2) / 2, -(WT / 2 - 0.25), WH + 0.62]) boxc([len / 2 - g1, 0.4, 1.0]);
    }
    else
        color(STONEL) translate([0, -(WT / 2 - 0.25), WH + 0.62]) boxc([len, 0.4, 1.0]);
}

// 城楼（置于城台顶；两层歇山，front 同城门 -y 内侧）
module part_gate_tower()
{
    L = 15;
    D = 7.5;
    color(STONEL) slab(L + 1.4, D + 1.4, 0.25);
    color(REDW) for (ix = [-3 : 3], sy = [-1, 1])
        translate([ix * 2.2, sy * (D / 2 - 0.22), 0.25]) cylinder(h = 3.5, r = 0.22, $fn = 8);
    color(PLASTER) translate([0, 0, 0.25]) slab(L - 1.2, D - 1.0, 3.5);
    color(REDD) for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.42), 2.1]) boxc([L - 3.5, 0.16, 1.5]);
    color(WOODD) translate([0, 0, 3.95]) boxc([L + 0.8, D + 0.8, 0.5]);
    translate([0, 0, 4.2]) part_roof(L, D, 1.8, 1.2, 2.8, ROOFC);
    color(PLASTER) translate([0, 0, 4.9]) slab(L - 3.6, D - 2.2, 2.5);
    color(REDD) for (sy = [-1, 1]) translate([0, sy * ((D - 2.2) / 2 + 0.02), 6.3]) boxc([L - 5.5, 0.16, 1.2]);
    color(WOODD) translate([0, 0, 7.2]) boxc([L - 2.8, D - 1.4, 0.45]);
    translate([0, 0, 7.4]) part_roof(L - 3.2, D - 1.8, 2.4, 1.2, 2.6, ROOFC);
    translate([0, 0, 9.6]) part_ridge(7.2);
}

// 城门楼：墩台（洞宽 8 净高 6）+ 常开门扇 + 门枕石 + 城台垛墙 + 城楼 + 门额石匾。
// 外侧 = +y；name = 匾额两字。
module bldg_gatehouse(name = "北门")
{
    GT = 10;   // 城台进深
    for (sx = [-1, 1])
    {
        color(STONED) translate([sx * 7.5, 0, 0.6]) boxc([7.9, GT + 1.2, 1.2]);
        color(STONEC) translate([sx * 7.5, 0, WH / 2]) boxc([7, GT, WH]);
    }
    color(STONEC) translate([0, 0, 7.5]) boxc([22, GT, 3.0]);
    color(DARKC) translate([0, 0, 5.94]) boxc([8.1, GT - 0.6, 0.12]);
    for (sx = [-1, 1])
    {
        color(REDD) translate([sx * 3.82, 1.6, 3.0]) boxc([0.22, 3.4, 5.4]);
        for (iy = [0 : 3], iz = [0 : 4])
            color(GOLDC) translate([sx * 3.68, 0.35 + iy * 0.85, 0.9 + iz * 1.05]) boxc([0.08, 0.14, 0.14]);
        color(STONEL) translate([sx * 3.9, -GT / 2 + 0.9, 0.3]) boxc([0.9, 1.6, 0.6]);
    }
    color(PAVED) translate([0, 0, WH + 0.06]) boxc([22, GT + 0.4, 0.12]);
    translate([0, GT / 2 - 0.25, WH + 0.12]) part_battlement(21.6);
    for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 10.75, sy * (GT / 2 - 1.5), WH + 0.12]) rotate([0, 0, 90]) part_battlement(2.6);
    color(STONEL) translate([0, -GT / 2 + 0.25, WH + 0.62]) boxc([22, 0.4, 1.0]);
    translate([0, GT / 2 + 0.10, 7.3]) rotate([0, 0, 180]) part_plaque(name, 2, 0.85);
    translate([0, 0, WH + 0.12]) part_gate_tower();
}

// 角楼：骑墙角台 + 四面垛墙（-x/-y 两侧留豁口接墙顶步道）+ 两层攒尖楼。
// 布局时按角位旋转，使豁口朝向相接的两段城墙。
module bldg_corner_tower()
{
    color(STONED) translate([0, 0, 0.6]) boxc([16, 16, 1.2]);
    color(STONEC) translate([0, 0, WH / 2]) boxc([15, 15, WH]);
    color(PAVED) translate([0, 0, WH + 0.06]) boxc([15.2, 15.2, 0.12]);
    translate([0, 7.35, WH + 0.12]) part_battlement(14.2);
    translate([7.35, 0, WH + 0.12]) rotate([0, 0, 90]) part_battlement(14.2);
    for (s = [-1, 1])
    {
        translate([s * 5.35, -7.35, WH + 0.12]) part_battlement(3.5);
        translate([-7.35, s * 5.35, WH + 0.12]) rotate([0, 0, 90]) part_battlement(3.5);
    }
    translate([0, 0, WH + 0.12])
    {
        color(STONEL) slab(9.6, 9.6, 0.22);
        color(REDW) for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 3.9, sy * 3.9, 0.22]) cylinder(h = 3.0, r = 0.2, $fn = 8);
        color(PLASTER) translate([0, 0, 0.22]) slab(8.2, 8.2, 3.0);
        color(REDD) for (a = [0, 90]) rotate([0, 0, a]) for (sy = [-1, 1])
            translate([0, sy * 4.12, 2.0]) boxc([6.0, 0.14, 1.1]);
        color(WOODD) translate([0, 0, 3.45]) boxc([9.0, 9.0, 0.45]);
        translate([0, 0, 3.65]) part_roof(8.4, 8.4, 1.5, 1.0, 2.4, ROOFC);
        color(PLASTER) translate([0, 0, 4.1]) slab(5.6, 5.6, 2.2);
        color(WOODD) translate([0, 0, 6.1]) boxc([6.4, 6.4, 0.4]);
        translate([0, 0, 6.3]) part_roof(6.0, 6.0, 2.6, 0.9, 3.0, ROOFC);
        color(GOLDC) translate([0, 0, 8.6]) cylinder(h = 0.5, r = 0.14, $fn = 6);
        color(GOLDC) translate([0, 0, 9.05]) sphere(r = 0.38, $fn = 8);
    }
}

// 马面敌台：外凸 4.3m，三面垛墙；flag=0 立旗 / 1 建铺房哨舍。外侧 = +y。
module wall_bastion(flag = 0)
{
    color(STONED) translate([0, 3.0, 0.6]) boxc([12.8, 10.4, 1.2]);
    color(STONEC) translate([0, 3.0, WH / 2]) boxc([12, 9.6, WH]);
    color(PAVED) translate([0, 3.25, WH + 0.06]) boxc([12, 9.2, 0.12]);
    translate([0, 7.55, WH + 0.12]) part_battlement(11.6);
    for (sx = [-1, 1])
        translate([sx * 5.75, 3.4, WH + 0.12]) rotate([0, 0, 90]) part_battlement(7.6);
    if (flag == 0)
        translate([0, 4.6, WH + 0.12]) prop_flag(REDC, 4.6);
    else
    {
        color(PLASTER) translate([0, 4.2, WH + 0.12]) slab(4.6, 3.2, 2.2);
        translate([0, 4.2, WH + 2.32]) part_roof(4.6, 3.2, 1.1, 0.6, 0, ROOFC);
        translate([0, 4.2, WH + 3.3]) part_ridge(5.4);
    }
}

// 登城马道：沿 +x 上行（x∈[0,len] 从地面升至墙顶步道），靠墙侧 = +y，外缘矮栏。
// 顶端 x∈[len,len+4] 为登城平台，与墙顶海墁同高（女墙在该区间留豁口）。
module wall_ramp(len = 24, w = 3.0)
{
    up = WH + 0.12;
    ang = atan(up / len);
    rl = sqrt(len * len + up * up) + 0.4;
    color(STONEC) translate([len / 2, 0, up / 2 - 0.28]) rotate([0, -ang, 0]) boxc([rl, w, 0.6]);
    color(STONED) translate([len / 2, -w / 2 + 0.15, up / 2 + 0.32]) rotate([0, -ang, 0]) boxc([rl, 0.3, 1.1]);
    for (i = [1 : 3])
        color(STONED) translate([len * i / 4, 0, up * i / 8]) boxc([2.0, w - 0.2, up * i / 4]);
    color(STONEC) translate([len + 2.0, 0, up / 2]) boxc([4.0, w, up]);
    color(PAVED) translate([len + 2.0, 0, up + 0.04]) boxc([4.0, w, 0.08]);
    color(STONED) translate([len + 2.0, -w / 2 + 0.15, up + 0.55]) boxc([4.0, 0.3, 1.0]);
}

// ================= 建筑库（front = -y；底面 z=0） =================

// 宫墙段（沿 x，石身 + 两坡瓦压顶）
module wall_palace_run(len)
{
    color(STONEC) translate([0, 0, 2.1]) boxc([len, 1.2, 4.2]);
    translate([0, 0, 4.2]) part_roof(len - 0.4, 1.2, 0.55, 0.3, 0, ROOFC);
}

// 宫墙角柱
module wall_palace_pier()
{
    color(STONED) translate([0, 0, 2.3]) boxc([1.8, 1.8, 4.6]);
    translate([0, 0, 4.6]) part_roof(1.9, 1.9, 0.5, 0.2, 0.95, ROOFD);
}

// 宫城南门：门屋跨墙 + 常开门扇 + 门枕 + 灯笼（front=-y 朝城外）
module bldg_inner_gate()
{
    for (sx = [-1, 1])
    {
        color(STONEC) translate([sx * 4.0, 0, 2.25]) boxc([2.4, 2.0, 4.5]);
        color(REDD) translate([sx * 2.55, 0.6, 2.0]) boxc([0.2, 2.2, 4.0]);
        color(STONEL) translate([sx * 2.7, -1.3, 0.3]) boxc([0.7, 1.2, 0.6]);
    }
    color(REDW) translate([0, 0, 5.0]) boxc([10.6, 2.4, 1.0]);
    color(WOODD) translate([0, 0, 5.75]) boxc([11.2, 3.0, 0.5]);
    translate([0, 0, 6.0]) part_roof(10.8, 3.2, 1.6, 0.9, 0, ROOFC);
    translate([0, 0, 7.45]) part_ridge(11.2);
    for (sx = [-1, 1]) translate([sx * 3.6, -1.7, 3.6]) prop_lantern();
}

// 主城正殿：三重台基 + 月台三出台阶 + 前廊柱列 + 重檐庑殿顶 + "主城"匾
module bldg_keep()
{
    color(STONEL) slab(31, 21, 0.9);
    color(STONEC) translate([0, 0, 0.9]) slab(28, 18, 0.9);
    color(STONEL) translate([0, -12.5, 0]) slab(14, 6, 1.55);
    translate([0, -15.5, 0]) part_steps(5, 5, 0.31, 0.45);
    for (sx = [-1, 1]) translate([sx * 8.5, -12, 0]) rotate([0, 0, sx * 90]) part_steps(3.4, 5, 0.31, 0.45);
    PB = 1.8;
    color(REDW) for (i = [-4 : 4]) translate([i * 2.7, -7.4, PB]) cylinder(h = 5.0, r = 0.30, $fn = 8);
    color(PLASTER) translate([0, 0.8, PB]) slab(23, 12.5, 5.0);
    color(REDD) translate([0, -5.55, PB + 1.6]) boxc([16, 0.25, 3.2]);
    color(GOLDC) for (i = [-3 : 3]) translate([i * 2.25, -5.70, PB + 1.6]) boxc([0.10, 0.06, 3.0]);
    color(WOODD) translate([0, 0, PB + 5.2]) boxc([25.5, 14.5, 0.55]);
    translate([0, 0, PB + 5.45]) part_roof(24.5, 13.5, 2.4, 1.6, 4.6, ROOFC);
    color(PLASTER) translate([0, 0.5, PB + 6.9]) slab(16, 8.5, 2.7);
    color(REDD) translate([0, 0.5 - 4.27, PB + 8.5]) boxc([12, 0.14, 1.3]);
    color(WOODD) translate([0, 0.5, PB + 9.5]) boxc([17.5, 10, 0.5]);
    translate([0, 0.5, PB + 9.72]) part_roof(17, 9.5, 3.2, 1.4, 3.8, ROOFC);
    translate([0, 0.5, PB + 12.72]) part_ridge(10.1);
    translate([0, -7.78, PB + 4.35]) part_plaque("主城", 2, 1.0);
}

// 配殿 / 后殿（单檐四坡）
module bldg_side_hall(L = 11, D = 6, seed = 0)
{
    color(STONEL) slab(L + 1.2, D + 1.2, 0.5);
    color(REDW) for (i = [-2 : 2]) translate([i * (L - 0.9) / 4, -D / 2 + 0.28, 0.5]) cylinder(h = 3.0, r = 0.16, $fn = 8);
    color(PLASTER) translate([0, 0.25, 0.5]) slab(L - 0.5, D - 0.9, 3.0);
    color(REDD) translate([0, -D / 2 + 0.05, 1.9]) boxc([L - 2.8, 0.16, 1.8]);
    color(WOODD) translate([0, 0, 3.6]) boxc([L + 0.6, D + 0.6, 0.4]);
    translate([0, 0, 3.8]) part_roof(L, D, 1.9, 0.9, 2.0, ROOFC);
    translate([0, 0, 5.55]) part_ridge(L - 3.6);
}

// 民居（seed 定墙色/屋型/瓦色；L/D 可微调）
module bldg_house(seed = 0, L = 9, D = 6.5)
{
    wh = 2.7;
    color(STONED) slab(L + 0.6, D + 0.6, 0.3);
    color(house_c(seed)) translate([0, 0, 0.3]) slab(L, D, wh);
    color(WOODD)
    {
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.3), -D / 2 - 0.04, 0.3 + wh / 2]) boxc([0.24, 0.08, wh]);
        translate([0, 0, 0.3 + wh - 0.19]) boxc([L + 0.3, D + 0.3, 0.38]);
        translate([0, -D / 2 - 0.06, 1.35]) boxc([1.5, 0.12, 2.1]);
    }
    color(WOODC) translate([0, -D / 2 - 0.10, 1.3]) boxc([1.2, 0.08, 1.9]);
    for (sx = [-1, 1]) translate([sx * L * 0.30, -D / 2, 1.75]) part_lattice_win(1.5, 1.1);
    rh = 1.6 + 0.25 * rnd(seed, 3);
    ri = (rnd(seed + 3, 3) == 0) ? 2.2 : 0;
    translate([0, 0, 0.3 + wh])
    {
        part_roof(L, D, rh, 0.8, ri, (rnd(seed + 5, 4) == 0) ? ROOFD : ROOFC);
        translate([0, 0, rh - 0.12]) part_ridge((ri > 0 ? L - 2 * ri : L) + 0.5);
    }
}

// 酒楼：两层 + 挑台栏杆 + "酒楼"匾 + 酒旗 + 灯笼（front=-y）
module bldg_inn()
{
    L = 12;
    D = 9;
    color(STONED) slab(L + 0.8, D + 0.8, 0.4);
    color(PLASTER) translate([0, 0, 0.4]) slab(L, D, 3.2);
    color(REDW) for (i = [-2 : 2]) translate([i * 2.6, -D / 2 + 0.15, 0.4]) cylinder(h = 3.2, r = 0.16, $fn = 8);
    color(WOODD) translate([0, -D / 2 - 0.06, 1.5]) boxc([2.2, 0.12, 2.4]);
    color(DARKC) translate([0, -D / 2 - 0.10, 1.45]) boxc([1.9, 0.06, 2.2]);
    for (sx = [-1, 1]) translate([sx * 3.9, -D / 2, 1.95]) part_lattice_win(1.7, 1.3);
    color(WOODC) translate([0, -0.7, 3.75]) boxc([L + 0.5, D + 2.2, 0.3]);
    color(WOODD) for (i = [-5 : 5]) translate([i * 1.15, -D / 2 - 1.6, 4.3]) boxc([0.09, 0.09, 0.85]);
    color(WOODD) translate([0, -D / 2 - 1.6, 4.75]) boxc([L + 0.4, 0.12, 0.12]);
    color(house_c(7)) translate([0, 0.3, 3.9]) slab(L - 1.2, D - 1.6, 2.7);
    for (sx = [-1, 0, 1]) translate([sx * 3.4, 0.3 - (D - 1.6) / 2, 5.45]) part_lattice_win(1.8, 1.3);
    color(WOODD) translate([0, 0.3, 6.7]) boxc([L - 0.4, D - 1.0, 0.45]);
    translate([0, 0.3, 6.9]) part_roof(L - 0.9, D - 1.4, 2.2, 1.0, 2.4, ROOFC);
    translate([0, 0.3, 8.95]) part_ridge(6.8);
    translate([0, -D / 2 - 0.15, 3.0]) part_plaque("酒楼", 2, 0.62);
    color(WOODD) translate([-L / 2 - 0.9, -D / 2 - 0.6, 0]) cylinder(h = 7.6, r = 0.09, $fn = 6);
    color(WOODD) translate([-L / 2 - 0.9, -D / 2 - 0.6, 7.3]) rotate([90, 0, 0]) cylinder(h = 0.9, r = 0.05, $fn = 6);
    color(CANVW) translate([-L / 2 - 0.9, -D / 2 - 1.35, 6.1]) boxc([0.07, 0.75, 2.2]);
    translate([-L / 2 - 0.95, -D / 2 - 1.35, 6.3]) rotate([0, 0, -90]) part_text_cn("酒", 1, 0.6, REDD);
    for (sx = [-1, 1]) translate([sx * (L / 2 - 0.6), -D / 2 - 1.2, 3.7]) prop_lantern();
}

// 兵营（长屋双门 + 高窗 + 角旗）
module bldg_barracks(seed = 0)
{
    L = 18;
    D = 8;
    color(STONED) slab(L + 0.7, D + 0.7, 0.35);
    color(PLASTER) translate([0, 0, 0.35]) slab(L, D, 3.1);
    color(WOODD)
    {
        translate([0, 0, 3.26]) boxc([L + 0.4, D + 0.4, 0.4]);
        for (i = [-2 : 2]) translate([i * 4.2, -D / 2 - 0.03, 1.7]) boxc([0.25, 0.1, 3.0]);
    }
    for (sx = [-1, 1])
    {
        color(WOODD) translate([sx * 5.5, -D / 2 - 0.05, 1.35]) boxc([2.0, 0.06, 2.5]);
        color(REDD) translate([sx * 5.5, -D / 2 - 0.10, 1.3]) boxc([1.7, 0.08, 2.3]);
    }
    for (x = [-8, -2, 2, 8]) translate([x, -D / 2, 2.55]) part_lattice_win(1.1, 0.8);
    translate([0, 0, 3.46]) part_roof(L, D, 2.3, 0.85, 0, ROOFC);
    translate([0, 0, 5.62]) part_ridge(L + 0.5);
    for (sx = [-1, 1]) translate([sx * (L / 2 + 1.2), -D / 2 + 0.4, 0]) prop_flag(REDC, 5);
}

// 仓库（高台基防潮 + 板门 + 高通风窗 + 蓝瓦四坡顶）
module bldg_warehouse(seed = 0)
{
    L = 15;
    D = 10;
    color(STONED) slab(L + 0.8, D + 0.8, 0.75);
    translate([0, -(D + 0.8) / 2, 0]) part_steps(3.4, 3, 0.25, 0.4);
    color(PLASTER) translate([0, 0, 0.75]) slab(L, D, 3.6);
    color(WOODD)
    {
        translate([0, 0, 4.14]) boxc([L + 0.4, D + 0.4, 0.42]);
        for (i = [-2 : 2]) translate([i * 3.4, -D / 2 - 0.03, 2.55]) boxc([0.28, 0.1, 3.6]);
        translate([0, -D / 2 - 0.05, 2.2]) boxc([3.4, 0.09, 2.9]);
    }
    color(WOODC) for (sx = [-1, 1]) translate([sx * 0.78, -D / 2 - 0.09, 2.15]) boxc([1.5, 0.07, 2.7]);
    color(DARKC) for (x = [-5, 5]) translate([x, -D / 2 - 0.06, 3.7]) boxc([1.3, 0.05, 0.6]);
    translate([0, 0, 4.35]) part_roof(L, D, 2.5, 0.95, 2.6, ROOFB);
    translate([0, 0, 6.75]) part_ridge(10.3, [0.24, 0.30, 0.40]);
}

// 圆囤粮仓（茅草锥顶）
module bldg_granary()
{
    color(STONED) cylinder(h = 0.4, r = 3.3, $fn = 10);
    color(PLASTER) translate([0, 0, 0.4]) cylinder(h = 3.2, r = 2.8, $fn = 10);
    color(WOODD) translate([0, -2.72, 1.5]) boxc([1.1, 0.35, 2.2]);
    color(STRAWC) translate([0, 0, 3.6]) cylinder(h = 2.0, r1 = 3.5, r2 = 0.35, $fn = 10);
    color(WOODD) translate([0, 0, 5.6]) cylinder(h = 0.5, r = 0.12, $fn = 6);
}

// 市场摊位（front=-y 顾客侧；seed 定棚色/货色）
module bldg_stall(seed = 0)
{
    color(WOODD) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 1.7, sy * 1.3, 1.1]) boxc([0.14, 0.14, 2.2]);
    color(WOODC) translate([0, -1.0, 0.5]) boxc([3.6, 1.0, 1.0]);
    color(WOODC) translate([0, 0.9, 0.4]) boxc([3.2, 1.0, 0.8]);
    translate([0, 0, 2.2]) part_roof(3.6, 2.9, 0.8, 0.45, 0, canv_c(seed));
    color(CANVW) translate([0, 0, 2.96]) boxc([4.3, 0.2, 0.10]);
    for (i = [0 : 2]) color(goods_c(seed + i)) translate([-1.1 + i * 1.1, -1.0, 1.18]) boxc([0.8, 0.7, 0.36]);
    color(STRAWC) translate([-0.9, 0.9, 0.95]) scale([1, 1, 0.75]) sphere(r = 0.45, $fn = 7);
    color(STRAWC) translate([0.5, 0.9, 0.92]) scale([1, 1, 0.75]) sphere(r = 0.42, $fn = 7);
    translate([2.3, -0.9, 0]) prop_jar();
}

// 箭塔（石基收分 + 木挑台栏杆 + 哨舱 + 攒尖顶 + 红旗）
module bldg_tower_watch()
{
    color(STONED) translate([0, 0, 0.9]) boxc([5.6, 5.6, 1.8]);
    color(STONEC) translate([0, 0, 3.3]) boxc([4.6, 4.6, 3.0]);
    color(STONEC) translate([0, 0, 5.9]) boxc([3.9, 3.9, 2.2]);
    color(WOODC) translate([0, 0, 7.15]) boxc([5.4, 5.4, 0.3]);
    color(WOODD) for (a = [0, 90]) rotate([0, 0, a]) for (sy = [-1, 1])
    {
        translate([0, sy * 2.6, 7.75]) boxc([5.3, 0.09, 0.10]);
        for (i = [-2 : 2]) translate([i * 1.05, sy * 2.6, 7.55]) boxc([0.08, 0.08, 0.55]);
    }
    color(PLASTER) translate([0, 0, 8.5]) boxc([3.4, 3.4, 2.4]);
    for (a = [0, 90, 180, 270]) rotate([0, 0, a]) translate([0, -1.7, 8.95]) part_lattice_win(1.3, 0.9);
    color(WOODD) translate([0, 0, 9.87]) boxc([4.0, 4.0, 0.35]);
    translate([0, 0, 10.02]) part_roof(3.9, 3.9, 1.7, 0.75, 1.95, ROOFC);
    translate([0, 0, 11.6]) prop_flag(REDC, 2.6);
}

// 军帐（方锥帐 + 门帘 + 顶端小旗）
module bldg_tent(seed = 0)
{
    part_roof(4.0, 4.0, 2.9, 0.05, 2.0, (rnd(seed, 3) == 0) ? [0.82, 0.78, 0.66] : CANVW);
    color(ROOFD) translate([0, -1.6, 0.62]) rotate([34, 0, 0]) boxc([0.9, 0.14, 1.3]);
    color(WOODD) translate([0, 0, 2.7]) cylinder(h = 0.75, r = 0.05, $fn = 6);
    color(REDC) translate([0.02, 0.26, 3.28]) boxc([0.04, 0.48, 0.32]);
}

// 草棚（开敞柴棚/马棚）
module bldg_shed(L = 6, D = 4)
{
    color(WOODD) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (L / 2 - 0.2), sy * (D / 2 - 0.2), 1.1]) boxc([0.16, 0.16, 2.2]);
    translate([0, 0, 2.2]) part_roof(L, D, 0.9, 0.5, 0, STRAWC);
    color(STRAWC) translate([0, 0.5, 0.42]) scale([1, 1, 0.7]) sphere(r = 0.8, $fn = 7);
    color(WOODC) translate([-1.4, -0.6, 0.3]) boxc([1.6, 1.0, 0.6]);
}

// 农田（田埂 + 五垄作物；seed 三分之一概率为熟穗黄）
module farm_plot(seed = 0, L = 13, D = 8)
{
    color(SOILD) slab(L + 0.8, D + 0.8, 0.22);
    color(SOILC) translate([0, 0, 0.22]) slab(L, D, 0.10);
    ripe = rnd(seed, 3) == 0;
    for (iy = [0 : 4])
    {
        yy = -D / 2 + D / 10 + iy * D / 5;
        color(ripe ? CROPY : [0.40 + 0.04 * rnd(seed + iy, 3), 0.60 + 0.05 * rnd(seed + iy + 1, 3), 0.26])
            translate([0, yy, 0.32]) boxc([L - 0.9, D / 5 * 0.55, 0.30]);
        color(ripe ? [0.92, 0.80, 0.38] : [0.52, 0.75, 0.32])
            translate([0, yy, 0.50]) boxc([L - 1.3, D / 5 * 0.34, 0.10]);
    }
}

// ================= 地面 =================

// 展台底座 + 地形草地 + 城基台地
module ground_base()
{
    color(BASEC) translate([0, 0, -2.9]) boxc([352, 352, 5.8]);
    color(GRASSC) slab(340, 340, TZ);
    for (i = [0 : 15])
        color(GRASSD) translate([-160 + rnd(i * 7, 320), -160 + rnd(i * 13 + 3, 320), TZ])
            slab(9 + rnd(i, 12), 7 + rnd(i + 5, 10), 0.02);
    color(DIRTC) translate([0, 0, TZ]) slab(199, 199, CZ - TZ);
}

// 四向城外官道（出四门通往地图边缘，含车辙）
module ground_roads_out()
{
    for (a = [0, 90, 180, 270]) rotate([0, 0, a])
    {
        color(DIRTD) translate([0, 135, TZ]) slab(7, 71, 0.05);
        color([0.66, 0.58, 0.42]) for (sx = [-1, 1]) translate([sx * 2.4, 135, TZ + 0.05]) slab(0.5, 71, 0.012);
    }
}

// 西南水塘（滩涂 + 水面 + 浅水高光 + 芦苇 + 岸石）
module ground_pond()
{
    color(SANDC) translate([-136, -134, TZ]) slab(58, 44, 0.05);
    color(WATERC) translate([-136, -134, TZ + 0.02]) slab(48, 34, 0.02);
    color([0.38, 0.66, 0.78]) translate([-143, -138, TZ + 0.04]) slab(16, 9, 0.012);
    for (i = [0 : 5])
        color([0.42, 0.60, 0.30]) translate([-117 + rnd(i * 3, 5), -148 + i * 6, TZ + 0.04])
            cylinder(h = 1.1 + 0.2 * rnd(i, 3), r = 0.05, $fn = 5);
    translate([-158, -114, TZ]) nature_rock(1.4, 3);
    translate([-112, -152, TZ]) nature_rock(1.0, 8);
}

// 城内街面：宫城环街 + 十字大街（御道中线）+ 门内广场 + 街坊巷道
module ground_city()
{
    color(PAVEC)
    {
        for (sy = [-1, 1]) translate([0, sy * 38, CZ]) slab(84, 8, 0.05);
        for (sx = [-1, 1]) translate([sx * 38, 0, CZ]) slab(8, 68, 0.05);
        for (sy = [-1, 1]) translate([0, sy * 59, CZ]) slab(10, 34, 0.05);
        for (sx = [-1, 1]) translate([sx * 59, 0, CZ]) slab(34, 10, 0.05);
        translate([0, -31, CZ]) slab(10, 6, 0.05);
        for (sy = [-1, 1]) translate([0, sy * 82, CZ]) slab(20, 12, 0.05);
        for (sx = [-1, 1]) translate([sx * 82, 0, CZ]) slab(12, 20, 0.05);
        translate([0, 0, CZ]) slab(68, 68, 0.04);
    }
    color(PAVED)
    {
        for (sy = [-1, 1]) translate([0, sy * 59, CZ + 0.05]) slab(3, 34, 0.014);
        for (sx = [-1, 1]) translate([sx * 59, 0, CZ + 0.05]) slab(34, 3, 0.014);
    }
    color(DIRTD) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 46, sy * 63, CZ]) slab(6, 42, 0.03);
}

// ================= 功能锚点（节点名 = module 名；调用处 translate 在 rotate 外层） =================
module gate_01() bldg_gatehouse("北门");
module gate_02() bldg_gatehouse("东门");
module gate_03() bldg_gatehouse("南门");
module gate_04() bldg_gatehouse("西门");
module tower_01() bldg_corner_tower();   // 角楼 NW
module tower_02() bldg_corner_tower();   // 角楼 NE
module tower_03() bldg_corner_tower();   // 角楼 SE
module tower_04() bldg_corner_tower();   // 角楼 SW
module watch_01() bldg_tower_watch();    // 箭塔 环街NW
module watch_02() bldg_tower_watch();    // 箭塔 环街NE
module watch_03() bldg_tower_watch();    // 箭塔 环街SE
module watch_04() bldg_tower_watch();    // 箭塔 环街SW
module ramp_01() prop_marker();          // 马道上口 北门东
module ramp_02() prop_marker();          // 马道上口 南门西
module node_01() prop_marker();          // 环街 NW
module node_02() prop_marker();          // 环街 NE
module node_03() prop_marker();          // 环街 SE
module node_04() prop_marker();          // 环街 SW
module node_05() prop_marker();          // 北门内广场
module node_06() prop_marker();          // 东门内广场
module node_07() prop_marker();          // 南门内广场
module node_08() prop_marker();          // 西门内广场
module spawn_01() prop_spawn_stone();    // 北官道
module spawn_02() prop_spawn_stone();    // 东官道
module spawn_03() prop_spawn_stone();    // 南官道
module spawn_04() prop_spawn_stone();    // 西官道
module keep_01() bldg_keep();
module innergate_01() bldg_inner_gate();
module barracks_01() bldg_barracks(1);
module barracks_02() bldg_barracks(2);
module warehouse_01() bldg_warehouse(1);
module warehouse_02() bldg_warehouse(2);
module inn_01() bldg_inn();
module well_01() prop_well();
module well_02() prop_well();
module house_01() bldg_house(1);
module house_02() bldg_house(2, 8.5, 6.2);
module house_03() bldg_house(3, 9.5, 7);
module house_04() bldg_house(4);
module house_05() bldg_house(5, 8.5, 6);
module house_06() bldg_house(6);
module house_07() bldg_house(7, 8, 6.2);
module house_08() bldg_house(8, 10, 7);
module house_09() bldg_house(9);
module house_10() bldg_house(10, 9.5, 6.8);
module house_11() bldg_house(11, 8.5, 6.2);
module house_12() bldg_house(12);
module house_13() bldg_house(13, 9.5, 7);
module house_14() bldg_house(14, 8.5, 6.4);
module house_15() bldg_house(15);
module house_16() bldg_house(16, 9, 6.8);
module farm_01() farm_plot(1);
module farm_02() farm_plot(2, 13, 9);
module farm_03() farm_plot(3);
module farm_04() farm_plot(4, 12, 8);
module farm_05() farm_plot(5, 14, 9);
module farm_06() farm_plot(6);
module farm_07() farm_plot(7, 12, 8.5);
module farm_08() farm_plot(8);
module market_01() bldg_stall(1);
module market_02() bldg_stall(2);
module market_03() bldg_stall(3);
module market_04() bldg_stall(4);
module market_05() bldg_stall(5);
module market_06() bldg_stall(6);
module market_07() bldg_stall(7);
module market_08() bldg_stall(8);

// ======================== 总装 ========================

ground_base();
ground_roads_out();
ground_pond();
ground_city();

// ---- 城墙（外皮 ±96；北/南墙全宽，东/西墙嵌于其间；门洞留 8m 缺口） ----
for (sy = [-1, 1], sx = [-1, 1])
    translate([sx * 50, sy * 92.5, CZ]) rotate([0, 0, sy > 0 ? 0 : 180])
        wall_run(92, (sx * sy > 0) ? -21 : 0, (sx * sy > 0) ? -11 : 0);   // 豁口=马道登城平台
for (sx = [-1, 1], sy = [-1, 1])
    translate([sx * 92.5, sy * 50, CZ]) rotate([0, 0, (sx > 0) ? -90 : 90]) wall_run(92);

// 马面敌台（每面两座，旗/铺房交替）
for (s = [-1, 1])
{
    translate([s * 48, 92.5, CZ]) wall_bastion(s > 0 ? 1 : 0);
    translate([s * 48, -92.5, CZ]) rotate([0, 0, 180]) wall_bastion(s > 0 ? 0 : 1);
    translate([92.5, s * 48, CZ]) rotate([0, 0, -90]) wall_bastion(s > 0 ? 1 : 0);
    translate([-92.5, s * 48, CZ]) rotate([0, 0, 90]) wall_bastion(s > 0 ? 0 : 1);
}

// 城门楼（北/东/南/西）
translate([0, 92.5, CZ]) gate_01();
translate([92.5, 0, CZ]) rotate([0, 0, -90]) gate_02();
translate([0, -92.5, CZ]) rotate([0, 0, 180]) gate_03();
translate([-92.5, 0, CZ]) rotate([0, 0, 90]) gate_04();

// 角楼（旋转使垛墙豁口朝向相接城墙）
translate([-92.5, 92.5, CZ]) rotate([0, 0, 90]) tower_01();
translate([92.5, 92.5, CZ]) tower_02();
translate([92.5, -92.5, CZ]) rotate([0, 0, -90]) tower_03();
translate([-92.5, -92.5, CZ]) rotate([0, 0, 180]) tower_04();

// 登城马道（北门东 / 南门西）+ 上口界石锚点
translate([8, 87.3, CZ]) wall_ramp();
translate([-8, -87.3, CZ]) rotate([0, 0, 180]) wall_ramp();
translate([34, 87.3, CZ + WH + 0.2]) ramp_01();
translate([-34, -87.3, CZ + WH + 0.2]) ramp_02();

// ---- 宫城（内城 56x56：主城正殿 + 东西配殿 + 后殿 + 南宫门） ----
translate([0, 28, CZ]) wall_palace_run(57.2);
for (sx = [-1, 1]) translate([sx * 28, 0, CZ]) rotate([0, 0, 90]) wall_palace_run(54);
for (sx = [-1, 1]) translate([sx * 16.6, -28, CZ]) wall_palace_run(22.8);
for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 28, sy * 28, CZ]) wall_palace_pier();
translate([0, -28, CZ]) innergate_01();
translate([0, 6, CZ + 0.04]) keep_01();
translate([-19.5, 0, CZ + 0.04]) rotate([0, 0, 90]) bldg_side_hall(11, 6, 1);
translate([19.5, 0, CZ + 0.04]) rotate([0, 0, -90]) bldg_side_hall(11, 6, 2);
translate([0, 22, CZ + 0.04]) bldg_side_hall(14, 6.5, 3);
translate([0, -12.5, CZ + 0.04]) prop_incense();
for (sx = [-1, 1]) translate([sx * 8, -18.5, CZ + 0.04]) prop_flag(REDC, 7);
for (sx = [-1, 1], iy = [0 : 1]) translate([sx * 3.8, -15 - iy * 5, CZ + 0.04]) prop_stone_lamp();
for (sx = [-1, 1]) translate([sx * 23, 22, CZ + 0.04]) nature_pine(1.1);
for (sx = [-1, 1]) translate([sx * 23.5, -22, CZ + 0.04]) nature_tree(0.95, 41 + sx);

// 环街四角箭塔
translate([-45.5, 45.5, CZ]) watch_01();
translate([45.5, 45.5, CZ]) watch_02();
translate([45.5, -45.5, CZ]) watch_03();
translate([-45.5, -45.5, CZ]) watch_04();

// ---- 西北街区：民居 x4 + 水井 ----
translate([-76, 71, CZ]) house_01();
translate([-57, 71, CZ]) house_02();
translate([-76, 53, CZ]) rotate([0, 0, 180]) house_03();
translate([-57, 53, CZ]) rotate([0, 0, 180]) house_04();
translate([-66.5, 62, CZ]) well_01();
translate([-74.5, 62, CZ]) nature_tree(0.9, 3);
translate([-59, 61.5, CZ]) nature_tree(0.8, 9);
translate([-70, 65, CZ]) prop_jar();
translate([-62.5, 59, CZ]) prop_crates(2);
translate([-81, 60, CZ]) prop_cart(1);

// ---- 北街区（西）：仓库群 + 圆囤粮仓 ----
color(DIRTD) translate([-24, 63, CZ]) slab(36, 40, 0.03);
translate([-33, 56, CZ + 0.03]) warehouse_01();
translate([-15, 56, CZ + 0.03]) warehouse_02();
translate([-34, 75, CZ + 0.03]) bldg_granary();
translate([-24, 76, CZ + 0.03]) bldg_granary();
translate([-14, 75, CZ + 0.03]) bldg_granary();
translate([-37, 46.5, CZ + 0.03]) prop_crates(4);
translate([-28, 46, CZ + 0.03]) prop_cart(2);
translate([-19, 46.5, CZ + 0.03]) prop_crates(7);
translate([-10, 47, CZ + 0.03]) prop_hay();
translate([-40.5, 70, CZ + 0.03]) prop_jar();

// ---- 北街区（东）：民居 x5 ----
translate([12, 71, CZ]) house_05();
translate([25, 71, CZ]) house_06();
translate([38, 71, CZ]) house_07();
translate([17, 52, CZ]) rotate([0, 0, 180]) house_08();
translate([33, 52, CZ]) rotate([0, 0, 180]) house_09();
translate([25, 61.5, CZ]) nature_tree(0.85, 13);
translate([9, 60, CZ]) prop_jar();
translate([40, 60, CZ]) prop_crates(9);

// ---- 东北街区：民居 x3 + 园地 ----
color(GRASSD) translate([66.5, 62, CZ]) slab(22, 8, 0.03);
translate([58, 71, CZ]) house_10();
translate([75, 71, CZ]) house_11();
translate([66, 52, CZ]) rotate([0, 0, 180]) house_12();
translate([59, 62, CZ + 0.03]) nature_tree(0.9, 17);
translate([74, 62, CZ + 0.03]) nature_tree(0.85, 18);
translate([80, 55, CZ]) prop_hay();

// ---- 西街区（北）：兵营 + 校场 ----
color(DIRTD) translate([-64, 23.5, CZ]) slab(38, 35, 0.04);
translate([-64, 34, CZ + 0.04]) barracks_01();
for (i = [0 : 2]) translate([-78 + i * 6, 27, CZ + 0.04]) prop_rack();
for (i = [0 : 1]) translate([-49.5, 11 + i * 8, CZ + 0.04]) rotate([0, 0, -90]) prop_target();
for (sx = [-1, 1]) translate([-64 + sx * 16, 8.5, CZ + 0.04]) prop_flag(REDC, 5.5);
translate([-77, 12, CZ + 0.04]) prop_crates(11);

// ---- 西街区（南）：兵营 + 军帐营地 ----
color(DIRTD) translate([-64, -23.5, CZ]) slab(38, 35, 0.04);
translate([-64, -12, CZ + 0.04]) barracks_02();
translate([-76, -27, CZ + 0.04]) bldg_tent(1);
translate([-67, -30, CZ + 0.04]) bldg_tent(2);
translate([-56, -27, CZ + 0.04]) bldg_tent(3);
translate([-71, -38, CZ + 0.04]) bldg_tent(4);
translate([-58, -37, CZ + 0.04]) prop_rack();
translate([-50, -33, CZ + 0.04]) prop_hay();
translate([-79, -37, CZ + 0.04]) prop_crates(13);
translate([-48, -9, CZ + 0.04]) prop_flag(REDC, 5.5);

// ---- 东街区（北）：民居 x4 + 水井 ----
translate([55, 32, CZ]) house_13();
translate([72, 32, CZ]) house_14();
translate([55, 13, CZ]) rotate([0, 0, 180]) house_15();
translate([72, 13, CZ]) rotate([0, 0, 180]) house_16();
translate([63.5, 22.5, CZ]) well_02();
translate([47.5, 23, CZ]) nature_tree(0.85, 21);
translate([80, 22, CZ]) nature_tree(0.8, 22);
translate([76, 27, CZ]) prop_jar();

// ---- 东街区（南）：市场（双排彩棚摊位） ----
color(PAVEC) translate([64, -23.5, CZ]) slab(38, 35, 0.045);
translate([50, -14, CZ + 0.045]) market_01();
translate([59, -14, CZ + 0.045]) market_02();
translate([68, -14, CZ + 0.045]) market_03();
translate([77, -14, CZ + 0.045]) market_04();
translate([50, -33, CZ + 0.045]) rotate([0, 0, 180]) market_05();
translate([59, -33, CZ + 0.045]) rotate([0, 0, 180]) market_06();
translate([68, -33, CZ + 0.045]) rotate([0, 0, 180]) market_07();
translate([77, -33, CZ + 0.045]) rotate([0, 0, 180]) market_08();
translate([55, -23.5, CZ + 0.045]) prop_cart(5);
translate([70, -23, CZ + 0.045]) prop_crates(15);
translate([63, -24, CZ + 0.045]) prop_jar();
translate([82.5, -39, CZ]) nature_tree(0.85, 25);

// ---- 西南街区：农田 x4 + 草垛 ----
color(GRASSD) translate([-66.5, -63, CZ]) slab(33, 40, 0.03);
translate([-76, -54, CZ + 0.03]) farm_01();
translate([-57, -54, CZ + 0.03]) farm_02();
translate([-76, -71, CZ + 0.03]) farm_03();
translate([-57, -71, CZ + 0.03]) farm_04();
translate([-66, -80.5, CZ + 0.03]) prop_hay();
translate([-78, -80, CZ + 0.03]) prop_hay();
translate([-49.5, -80, CZ + 0.03]) prop_cart(7);

// ---- 南街区（西）：农田 x2 + 果园 ----
color(GRASSD) translate([-24, -63, CZ]) slab(36, 40, 0.03);
translate([-33, -54, CZ + 0.03]) farm_05();
translate([-14, -54, CZ + 0.03]) farm_06();
for (ix = [0 : 3], iy = [0 : 1])
    translate([-38 + ix * 9, -68 - iy * 9, CZ + 0.03])
        nature_tree(0.85 + 0.05 * rnd(ix + iy * 4, 3), 30 + ix + iy * 7);
translate([-9, -78, CZ + 0.03]) prop_hay();

// ---- 南街区（东）：酒楼 + 草棚后院 ----
color(DIRTD) translate([24, -63, CZ]) slab(36, 40, 0.035);
translate([13, -57, CZ + 0.035]) rotate([0, 0, -90]) inn_01();
translate([30, -55, CZ + 0.035]) bldg_shed();
translate([24, -73, CZ + 0.035]) prop_cart(9);
translate([33, -74, CZ + 0.035]) prop_crates(17);
translate([15, -75, CZ + 0.035]) prop_jar();
translate([17.5, -75, CZ + 0.035]) prop_jar();
translate([39, -70, CZ]) nature_tree(0.9, 33);

// ---- 东南街区：农田 x2 + 打谷场 ----
color(GRASSD) translate([66.5, -63, CZ]) slab(33, 40, 0.03);
translate([57, -54, CZ + 0.03]) farm_07();
translate([76, -54, CZ + 0.03]) farm_08();
color(DIRTD) translate([66, -74, CZ + 0.03]) cylinder(h = 0.04, r = 7, $fn = 12);
translate([62, -74, CZ + 0.07]) prop_hay();
translate([70, -76, CZ + 0.07]) prop_cart(11);
translate([80.5, -78, CZ]) nature_tree(0.85, 35);

// ---- 街道家具与节点 ----
translate([0, -50, CZ + 0.05]) prop_paifang();
for (sx = [-1, 1], i = [0 : 2])
{
    translate([sx * 4.3, -47 - i * 9, CZ + 0.05]) prop_stone_lamp();
    translate([sx * 4.3, 47 + i * 9, CZ + 0.05]) prop_stone_lamp();
    translate([47 + i * 9, sx * 4.3, CZ + 0.05]) prop_stone_lamp();
    translate([-47 - i * 9, sx * 4.3, CZ + 0.05]) prop_stone_lamp();
}
translate([7.5, 78, CZ + 0.05]) prop_crates(21);
translate([-7.5, 77, CZ + 0.05]) prop_cart(15);
translate([78, 7.5, CZ + 0.05]) prop_crates(23);
translate([-77, -7, CZ + 0.05]) prop_jar();
translate([7, -78, CZ + 0.05]) prop_crates(25);

translate([-38, 38, CZ + 0.05]) node_01();
translate([38, 38, CZ + 0.05]) node_02();
translate([38, -38, CZ + 0.05]) node_03();
translate([-38, -38, CZ + 0.05]) node_04();
translate([0, 82, CZ + 0.05]) node_05();
translate([82, 0, CZ + 0.05]) node_06();
translate([0, -82, CZ + 0.05]) node_07();
translate([-82, 0, CZ + 0.05]) node_08();

// ---- 城外：官道生成点 + 门外旗 + 地景 ----
translate([0, 118, TZ + 0.05]) spawn_01();
translate([118, 0, TZ + 0.05]) rotate([0, 0, 90]) spawn_02();
translate([0, -118, TZ + 0.05]) spawn_03();
translate([-118, 0, TZ + 0.05]) rotate([0, 0, 90]) spawn_04();
for (a = [0, 90, 180, 270]) rotate([0, 0, a])
{
    translate([6.5, 100.5, TZ]) prop_flag(REDC, 5);
    translate([-6.5, 100.5, TZ]) prop_flag(REDC, 5);
    translate([5.6, 128, TZ]) nature_rock(0.8, a + 1);
    translate([-5.8, 168, TZ]) nature_rock(0.7, a + 2);
}

translate([-138, 138, TZ]) nature_mount(1.1);
translate([-100, 156, TZ]) nature_mount(0.65);
translate([136, 142, TZ]) nature_hill(22, 9.5);
translate([156, 108, TZ]) nature_hill(14, 6);
translate([-152, 52, TZ]) nature_hill(13, 5.5);
nature_scatter(1, 16, 14, 162, 106, 162);
nature_scatter(2, 10, -96, -14, 106, 158);
nature_scatter(3, 16, 106, 162, 12, 95);
nature_scatter(4, 14, 106, 162, -95, -12);
nature_scatter(5, 14, -162, -106, 12, 95);
nature_scatter(6, 12, -96, -14, -162, -106);
nature_scatter(7, 14, 14, 162, -162, -106);
nature_scatter(8, 10, -162, -106, -95, -50);
