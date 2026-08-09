// kit_overhill.scad —— "峠 Over the Hill" 风格越野山地零件库（SnowRunner-like 场景元素）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "oh_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具车头朝 +x）。调用方自设 $fn（建议 12）。
// 尺度：mid（与 kit_deadly 同级；越野车长 ~4.5、土路宽 ~6）。
// 主题元素：土路/车辙/泥坑/河流、低模山丘/雪峰/红岩台地、松林/秋叶树/棕榈/仙人掌、
//           木桥/营地(帐篷/篝火)/原木栅栏/路标/加油站/木屋/瞭望塔、越野车/房车/货卡/原木拖车。

// ================= 配色（高原秋季 + 荒漠峡谷；PT 强日光下会整体提亮，故基色偏深偏饱和） =================
function oh_DIRTC()  = [0.42, 0.32, 0.21];   // 干土路
function oh_DIRTD()  = [0.31, 0.22, 0.14];   // 车辙/湿土
function oh_MUDC()   = [0.22, 0.16, 0.11];   // 泥坑
function oh_GRASSC() = [0.36, 0.42, 0.22];   // 高地草
function oh_GRASSD() = [0.28, 0.34, 0.18];   // 深草斑
function oh_AUTUC()  = [0.56, 0.28, 0.09];   // 秋叶橙
function oh_AUTUY()  = [0.60, 0.46, 0.13];   // 秋叶黄
function oh_PINED()  = [0.11, 0.22, 0.13];   // 松针深
function oh_PINEC()  = [0.16, 0.29, 0.16];   // 松针中
function oh_PALMC()  = [0.22, 0.38, 0.17];   // 棕榈叶
function oh_CACTC()  = [0.21, 0.37, 0.20];   // 仙人掌
function oh_TRUNKC() = [0.30, 0.22, 0.15];   // 树干
function oh_ROCKC()  = [0.38, 0.36, 0.32];   // 花岗岩
function oh_ROCKD()  = [0.32, 0.30, 0.27];   // 深岩
function oh_REDRK()  = [0.50, 0.25, 0.14];   // 峡谷红岩
function oh_REDRD()  = [0.39, 0.18, 0.11];   // 红岩深层
function oh_SANDC()  = [0.55, 0.43, 0.27];   // 荒漠沙地
function oh_SNOWC()  = [0.74, 0.77, 0.80];   // 山顶雪帽
function oh_WATERC() = [0.20, 0.36, 0.40];   // 河水（配 alpha）
function oh_WOODC()  = [0.34, 0.25, 0.16];   // 原木
function oh_WOODD()  = [0.23, 0.16, 0.11];   // 深木
function oh_PLANKC() = [0.44, 0.33, 0.20];   // 桥板/货台
function oh_METALC() = [0.45, 0.47, 0.49];   // 镀锌金属
function oh_METALD() = [0.27, 0.29, 0.31];   // 深金属
function oh_RUSTC()  = [0.38, 0.20, 0.13];   // 锈
function oh_REDC()   = [0.58, 0.15, 0.11];   // 信号红（油罐/雨棚/旗）
function oh_TRIMW()  = [0.74, 0.72, 0.66];   // 米白饰边/旗布
function oh_DARKC()  = [0.10, 0.10, 0.10];   // 洞口/轮胎
function oh_GLASSC() = [0.32, 0.42, 0.48];   // 车窗玻璃

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function oh_sq(x) = (x * x + x * 599 + 43) % 65521;
function oh_rnd(s, m) = oh_sq(oh_sq(((s % 65521) + 65521) % 65521) + 11) % m;
function oh_rndf(s) = oh_rnd(s, 1000) / 999;                       // [0, 1]
function oh_rndr(s, a, b) = a + (b - a) * oh_rndf(s);              // [a, b]

// ---- 变体调色板 ----
function oh_car_c(i)  = [[0.58, 0.14, 0.10], [0.18, 0.40, 0.38], [0.62, 0.46, 0.12],
                         [0.30, 0.36, 0.22], [0.50, 0.28, 0.14], [0.58, 0.58, 0.54]][oh_rnd(i, 6)];
function oh_leaf_c(i) = [oh_AUTUC(), oh_AUTUY(), [0.40, 0.42, 0.18], oh_AUTUC()][oh_rnd(i, 4)];
function oh_tent_c(i) = [[0.55, 0.33, 0.13], [0.30, 0.36, 0.20], [0.19, 0.37, 0.35]][oh_rnd(i, 3)];
function oh_drum_c(i) = [oh_RUSTC(), oh_REDC(), [0.32, 0.38, 0.26], oh_METALD()][oh_rnd(i, 4)];

// ---- 基础工具 ----
module oh_boxc(s) cube(s, center = true);
module oh_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) oh_boxc([L, D, t]);   // 底面 z=0 平板

// ================= 通用构件 =================

// 坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙皮，h=脊高，ov=出檐，
// rin=山面内收（0=双坡，rin>=L/2=攒尖）。面序为 OpenSCAD 约定（从外看顺时针）。
module oh_part_roof(L = 6, D = 5, h = 1.6, ov = 0.5, rin = 0, c = [0.24, 0.19, 0.15])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// ================= 地面（沿 x 铺设，底面 z=0） =================

// 土路段：双车辙 + 中脊草带 + 湿土斑 + 路缘碎石
module oh_ground_trail(L = 24, W = 6, seed = 0)
{
    color(oh_DIRTC()) oh_slab(L, W, 0.12);
    color(oh_DIRTD()) for (i = [0, 1])
        translate([0, (i * 2 - 1) * 0.85, 0.12]) oh_slab(L, 0.55, 0.015);
    ng = max(1, floor(L / 6));
    for (i = [0 : ng - 1])
        color(oh_GRASSD())
            translate([-L / 2 + (i + 0.5) * L / ng + oh_rndr(seed + i * 7, -0.8, 0.8), 0, 0.12])
                oh_slab(oh_rndr(seed + i, 1.6, 2.6), 0.3, 0.018);
    for (i = [0 : 2])
        color(oh_MUDC())
            translate([oh_rndr(seed * 5 + i * 31, -(L - 3) / 2, (L - 3) / 2),
                       oh_rndr(seed * 11 + i * 17 + 3, -(W - 2.2) / 2, (W - 2.2) / 2), 0.12])
                oh_slab(oh_rndr(seed + i + 5, 0.9, 1.8), oh_rndr(seed + i + 9, 0.5, 1.0), 0.01);
    ns = max(2, floor(L / 4));
    for (i = [0 : ns - 1], sy = [-1, 1])
        color(oh_rnd(seed + i * 13 + sy, 2) == 0 ? oh_ROCKC() : oh_ROCKD())
            translate([-L / 2 + (i + 0.5) * L / ns + oh_rndr(seed + i * 3 + sy * 7, -1.2, 1.2),
                       sy * (W / 2 - 0.25), 0.12])
                rotate([0, 0, oh_rnd(seed + i + sy, 90)])
                    oh_boxc([oh_rndr(seed + i + sy + 4, 0.18, 0.4), 0.2, 0.16]);
}

// 土路弯道块：W x W 补丁，车辙走 1/4 圆弧（进 -x 边，出 +y 边；其他朝向由调用方 rot）
module oh_ground_trail_bend(W = 6, seed = 0)
{
    color(oh_DIRTC()) oh_slab(W, W, 0.12);
    for (j = [0, 1])
    {
        rr = W / 2 + (j * 2 - 1) * 0.85;
        color(oh_DIRTD()) for (k = [0 : 3])
        {
            th = (k + 0.5) * 22.5;
            translate([-W / 2 + rr * sin(th), W / 2 - rr * cos(th), 0.12])
                rotate([0, 0, th]) oh_slab(rr * 0.42 + 0.18, 0.55, 0.015);
        }
    }
    color(oh_GRASSD()) translate([W / 4 - 0.3, -W / 4 + 0.3, 0.12])
        oh_slab(oh_rndr(seed, 1.2, 2.0), oh_rndr(seed + 3, 1.0, 1.6), 0.018);
    color(oh_MUDC()) translate([-W / 4, W / 4, 0.12]) oh_slab(1.2, 0.8, 0.01);
}

// 泥坑段：铺在土路上（厚度略高于路面），深泥浆 + 溅泥车辙 + 半沉石
module oh_ground_mud(L = 9, W = 6, seed = 0)
{
    color(oh_DIRTD()) oh_slab(L, W - 0.4, 0.14);
    color(oh_MUDC()) translate([0, 0, 0.14]) oh_slab(L - 1.2, W - 1.6, 0.02);
    for (i = [0 : 2])
        color([0.17, 0.13, 0.09])
            translate([oh_rndr(seed * 7 + i * 29, -(L - 3) / 2, (L - 3) / 2),
                       oh_rndr(seed * 3 + i * 41 + 7, -(W - 3) / 2, (W - 3) / 2), 0.14])
                oh_slab(oh_rndr(seed + i, 1.2, 2.4), oh_rndr(seed + i + 4, 0.8, 1.6), 0.03);
    color(oh_DIRTD()) for (i = [0, 1])
        translate([0, (i * 2 - 1) * 0.85, 0.16]) oh_slab(L - 0.6, 0.5, 0.012);
    for (i = [0 : 3])
        color(oh_ROCKD())
            translate([oh_rndr(seed + i * 13, -(L - 2) / 2, (L - 2) / 2),
                       oh_rndr(seed + i * 23 + 5, -(W - 2) / 2, (W - 2) / 2), 0.1])
                rotate([0, 0, oh_rnd(seed + i, 90)]) oh_boxc([0.4, 0.3, 0.2]);
}

// 草地块（深草斑 + 少量秋色斑，覆盖 spec ground 之上）
module oh_ground_grass(L = 20, D = 20, seed = 0)
{
    color(oh_GRASSC()) oh_slab(L, D, 0.1);
    for (i = [0 : 5])
        color(oh_rnd(seed + i, 4) == 0 ? oh_AUTUY() : oh_GRASSD())
            translate([oh_rndr(seed * 31 + i * 47, -(L - 4) / 2, (L - 4) / 2),
                       oh_rndr(seed * 17 + i * 71 + 3, -(D - 4) / 2, (D - 4) / 2), 0.1])
                oh_slab(oh_rndr(seed + i, 1.6, 3.4), oh_rndr(seed + i + 5, 1.2, 2.6), 0.012);
}

// 荒漠沙地块（峡谷角落用）：沙面 + 风纹 + 碎石
module oh_ground_sand(L = 20, D = 20, seed = 0)
{
    color(oh_SANDC()) oh_slab(L, D, 0.1);
    nr = max(2, floor(D / 4));
    for (i = [0 : nr - 1])
        color([0.50, 0.39, 0.24])
            translate([oh_rndr(seed + i * 19, -(L - 8) / 2, (L - 8) / 2),
                       -D / 2 + (i + 0.5) * D / nr, 0.1])
                rotate([0, 0, oh_rndr(seed + i * 7, -14, 14)])
                    oh_slab(oh_rndr(seed + i, L * 0.3, L * 0.55), 0.35, 0.012);
    for (i = [0 : 4])
        color(oh_rnd(seed + i, 2) == 0 ? oh_REDRD() : oh_ROCKD())
            translate([oh_rndr(seed * 13 + i * 37, -(L - 3) / 2, (L - 3) / 2),
                       oh_rndr(seed * 7 + i * 53 + 9, -(D - 3) / 2, (D - 3) / 2), 0.1])
                rotate([0, 0, oh_rnd(seed + i, 90)])
                    oh_boxc([oh_rndr(seed + i + 2, 0.25, 0.6), 0.3, 0.22]);
}

// 河流段（沿 x）：深色河床 + 半透明水面 + 两岸沙线与卵石
module oh_ground_river(L = 20, W = 7, seed = 0)
{
    wc = oh_WATERC();
    color([0.16, 0.30, 0.32]) oh_slab(L, W, 0.05);
    color([wc[0] + 0.1, wc[1] + 0.14, wc[2] + 0.15, 0.65])
        translate([0, 0, 0.05]) oh_slab(L, W - 0.9, 0.07);
    color(oh_SANDC()) for (sy = [-1, 1])
        translate([0, sy * (W / 2 - 0.25), 0]) oh_slab(L, 0.5, 0.1);
    ns = max(2, floor(L / 3));
    for (i = [0 : ns - 1], sy = [-1, 1])
        color(oh_rnd(seed + i * 11 + sy, 2) == 0 ? oh_ROCKC() : oh_ROCKD())
            translate([-L / 2 + (i + 0.5) * L / ns + oh_rndr(seed + i * 5 + sy * 3, -1.0, 1.0),
                       sy * (W / 2 - 0.55 - oh_rndf(seed + i + sy) * 0.4), 0.1])
                rotate([0, 0, oh_rnd(seed + i + sy, 90)])
                    scale([1, 0.75, 0.6]) sphere(r = oh_rndr(seed + i + sy + 6, 0.2, 0.42), $fn = 5);
}

// ================= 地形（底面 z=0，低模刻面） =================

// 草坡山丘：3 层低边数锥台 + 岩石露头（路侧障碍/边界丘陵）
module oh_terrain_hill(s = 1.0, seed = 0)
{
    r0 = 9 * s;
    color(oh_GRASSC()) rotate([0, 0, oh_rnd(seed, 45)])
        cylinder(h = 2.6 * s, r1 = r0, r2 = r0 * 0.6, $fn = 8);
    color(oh_GRASSD()) translate([0, 0, 2.3 * s]) rotate([0, 0, oh_rnd(seed + 3, 45) + 22])
        cylinder(h = 2.2 * s, r1 = r0 * 0.64, r2 = r0 * 0.32, $fn = 7);
    color(oh_GRASSC()) translate([0, 0, 4.2 * s]) rotate([0, 0, oh_rnd(seed + 7, 45)])
        cylinder(h = 1.7 * s, r1 = r0 * 0.34, r2 = r0 * 0.1, $fn = 6);
    for (i = [0 : 2])
        color(i % 2 == 0 ? oh_ROCKD() : oh_ROCKC())
            rotate([0, 0, oh_rnd(seed + i * 17, 360)])
                translate([r0 * oh_rndr(seed + i * 5, 0.45, 0.75), 0, oh_rndr(seed + i * 9, 0.4, 1.6) * s])
                    rotate([oh_rndr(seed + i, -20, 20), oh_rndr(seed + i + 2, -20, 20), 0])
                        oh_boxc([1.3 * s, 1.0 * s, 0.8 * s]);
}

// 岩石雪峰：陡峭双锥 + 雪帽（远景边界山，s=2~3 作背景）
module oh_terrain_peak(s = 1.0, seed = 0)
{
    h1 = 7 * s;
    color(oh_ROCKC()) rotate([0, 0, oh_rnd(seed, 60)])
        cylinder(h = h1, r1 = 6.5 * s, r2 = 2.1 * s, $fn = 6);
    color(oh_ROCKD()) translate([0, 0, h1 - 0.1 * s]) rotate([0, 0, oh_rnd(seed + 5, 60) + 25])
        cylinder(h = 3.4 * s, r1 = 2.3 * s, r2 = 0.65 * s, $fn = 5);
    color(oh_SNOWC()) translate([0, 0, h1 + 2.2 * s]) rotate([0, 0, oh_rnd(seed + 5, 60) + 25])
        cylinder(h = 2.0 * s, r1 = 1.25 * s, r2 = 0.06 * s, $fn = 5);
}

// 红岩台地（mesa）：沙裙 + 分层阶地红岩（峡谷角落地标）
module oh_terrain_mesa(s = 1.0, seed = 0)
{
    color(oh_SANDC()) cylinder(h = 0.9 * s, r1 = 7.6 * s, r2 = 6.1 * s, $fn = 8);
    for (i = [0 : 3])
        color(i % 2 == 0 ? oh_REDRD() : oh_REDRK())
            translate([0, 0, (0.8 + i * 1.55) * s])
                rotate([0, 0, oh_rnd(seed + i * 7, 24) - 12])
                    cylinder(h = 1.65 * s,
                             r1 = (6.0 - i * 0.55) * s, r2 = (5.5 - i * 0.55) * s, $fn = 8);
    color(oh_REDRK()) translate([0, 0, 6.9 * s])
        cylinder(h = 0.5 * s, r1 = 3.9 * s, r2 = 3.6 * s, $fn = 8);
}

// 低模巨石（red=1 时用峡谷红岩配色）
module oh_rock_boulder(s = 1.0, seed = 0, red = 0)
{
    c = red == 1 ? (oh_rnd(seed, 2) == 0 ? oh_REDRK() : oh_REDRD())
                 : (oh_rnd(seed, 2) == 0 ? oh_ROCKC() : oh_ROCKD());
    color(c) translate([0, 0, 0.3 * s])
        rotate([oh_rndr(seed + 1, -14, 14), oh_rndr(seed + 2, -14, 14), oh_rnd(seed + 3, 360)])
            scale([1, oh_rndr(seed + 4, 0.7, 0.95), oh_rndr(seed + 5, 0.55, 0.8)])
                sphere(r = 0.55 * s, $fn = 5);
}

// 巨石群（3 块大小递减）
module oh_rock_cluster(s = 1.0, seed = 0, red = 0)
{
    oh_rock_boulder(s = 1.3 * s, seed = seed, red = red);
    translate([0.95 * s, 0.55 * s, 0]) oh_rock_boulder(s = 0.85 * s, seed = seed + 7, red = red);
    translate([-0.75 * s, -0.6 * s, 0]) oh_rock_boulder(s = 0.55 * s, seed = seed + 13, red = red);
}

// ================= 植被（底面 z=0） =================

// 高山松（5 层锥叠，比郊区松更高瘦）
module oh_nature_pine(s = 1.0, seed = 0)
{
    c1 = oh_rnd(seed, 2) == 0 ? oh_PINED() : oh_PINEC();
    c2 = oh_rnd(seed, 2) == 0 ? oh_PINEC() : oh_PINED();
    scale([s, s, s])
    {
        color(oh_TRUNKC()) cylinder(h = 1.3, r = 0.24, $fn = 6);
        color(c1) translate([0, 0, 0.9]) cylinder(h = 2.0, r1 = 1.95, r2 = 1.15, $fn = 7);
        color(c2) translate([0, 0, 2.5]) cylinder(h = 1.9, r1 = 1.6, r2 = 0.85, $fn = 7);
        color(c1) translate([0, 0, 4.0]) cylinder(h = 1.7, r1 = 1.25, r2 = 0.55, $fn = 7);
        color(c2) translate([0, 0, 5.3]) cylinder(h = 1.4, r1 = 0.9, r2 = 0.28, $fn = 7);
        color(c1) translate([0, 0, 6.4]) cylinder(h = 1.1, r1 = 0.55, r2 = 0.03, $fn = 7);
    }
}

// 秋叶树（橙黄团状阔叶）
module oh_nature_autumn(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(oh_TRUNKC()) cylinder(h = 1.7, r = 0.22, $fn = 6);
        color(oh_leaf_c(seed)) translate([0, 0, 2.7]) sphere(r = 1.5, $fn = 6);
        color(oh_leaf_c(seed + 3)) translate([0.75, 0.4, 3.4]) sphere(r = 0.9, $fn = 6);
        color(oh_leaf_c(seed + 5)) translate([-0.6, -0.4, 3.5]) sphere(r = 0.8, $fn = 6);
        color(oh_leaf_c(seed + 8)) translate([-0.2, 0.65, 2.2]) sphere(r = 0.7, $fn = 6);
    }
}

// 秋色灌木
module oh_nature_bush(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(oh_leaf_c(seed)) translate([0, 0, 0.42]) sphere(r = 0.55, $fn = 6);
        color(oh_leaf_c(seed + 2)) translate([0.42, 0.15, 0.34]) sphere(r = 0.36, $fn = 6);
        color(oh_GRASSD()) translate([-0.35, -0.2, 0.32]) sphere(r = 0.32, $fn = 6);
    }
}

// 棕榈（弯干 + 6 片下垂叶 + 椰果，峡谷绿洲用）
module oh_nature_palm(s = 1.0, seed = 0)
{
    lean = oh_rndr(seed, 0.1, 0.2);
    scale([s, s, s]) rotate([0, 0, oh_rnd(seed, 360)])
    {
        for (i = [0 : 3])
            color(i % 2 == 0 ? oh_TRUNKC() : [0.35, 0.27, 0.18])
                translate([i * lean, 0, i * 0.78]) cylinder(h = 0.9, r = 0.17 - i * 0.012, $fn = 6);
        translate([4 * lean, 0, 3.3])
        {
            for (i = [0 : 5])
                rotate([0, 0, i * 60 + oh_rndr(seed + i, -12, 12)])
                {
                    color(i % 2 == 0 ? oh_PALMC() : [0.27, 0.44, 0.19])
                    {
                        rotate([0, -12, 0]) translate([0.85, 0, 0]) oh_boxc([1.7, 0.42, 0.06]);
                        rotate([0, -12, 0]) translate([1.6, 0, 0]) rotate([0, 42, 0])
                            translate([0.45, 0, 0]) oh_boxc([0.9, 0.3, 0.05]);
                    }
                }
            color([0.36, 0.26, 0.14]) translate([0.14, 0.1, -0.12]) sphere(r = 0.14, $fn = 6);
            color([0.36, 0.26, 0.14]) translate([-0.12, -0.12, -0.12]) sphere(r = 0.12, $fn = 6);
        }
    }
}

// 仙人掌（主干 + 0~2 只手臂）
module oh_nature_cactus(s = 1.0, seed = 0)
{
    c = oh_rnd(seed, 2) == 0 ? oh_CACTC() : [0.18, 0.32, 0.18];
    scale([s, s, s]) color(c)
    {
        cylinder(h = 2.3, r = 0.23, $fn = 7);
        translate([0, 0, 2.3]) sphere(r = 0.23, $fn = 7);
        for (i = [0, 1])
        {
            sx = i * 2 - 1;
            if (oh_rnd(seed + i * 5, 3) != 0)
            {
                az = oh_rndr(seed + i * 9, 0.8, 1.4);
                ah = oh_rndr(seed + i * 13, 0.6, 1.0);
                translate([sx * 0.2, 0, az]) rotate([0, sx * 90, 0]) cylinder(h = 0.4, r = 0.15, $fn = 6);
                translate([sx * 0.55, 0, az - 0.05]) cylinder(h = ah, r = 0.15, $fn = 6);
                translate([sx * 0.55, 0, az - 0.05 + ah]) sphere(r = 0.15, $fn = 6);
            }
        }
    }
}

// 枯树（干 + 秃枝，泥沼/荒地点缀）
module oh_nature_deadtree(s = 1.0, seed = 0)
{
    c = [0.31, 0.27, 0.22];
    scale([s, s, s]) rotate([0, 0, oh_rnd(seed, 360)])
    {
        color(c) rotate([oh_rndr(seed, -5, 5), oh_rndr(seed + 2, -5, 5), 0])
            cylinder(h = 2.8, r1 = 0.2, r2 = 0.1, $fn = 5);
        for (i = [0 : 2])
            color(c) translate([0, 0, 1.4 + i * 0.55])
                rotate([0, 0, i * 130 + oh_rnd(seed + i, 60)]) rotate([0, 55, 0])
                    cylinder(h = oh_rndr(seed + i * 7, 0.7, 1.2), r1 = 0.07, r2 = 0.02, $fn = 4);
    }
}

// 草簇
module oh_nature_grass(seed = 0)
{
    for (i = [0 : 2])
        color(i % 2 == 0 ? oh_GRASSD() : [0.5, 0.52, 0.3])
            rotate([0, 0, oh_rnd(seed + i, 180)])
                translate([0, 0, oh_rndr(seed + i + 3, 0.1, 0.18)])
                    oh_boxc([0.5, 0.06, oh_rndr(seed + i + 7, 0.2, 0.36)]);
}

// ================= 道具（底面 z=0；带朝向者 front=-y） =================

// 木板桥（沿 x 跨越，桥面 z~0.55，两端带坡道；跨河用）
module oh_prop_bridge(L = 9, W = 5.6, seed = 0)
{
    np = max(4, floor(L / 0.6));
    for (i = [0 : np - 1])
        color(i % 3 == 0 ? oh_WOODC() : oh_PLANKC())
            translate([-L / 2 + (i + 0.5) * L / np, 0, 0.55]) oh_boxc([L / np - 0.07, W, 0.11]);
    color(oh_WOODD()) for (i = [-1, 0, 1])
        translate([0, i * (W / 2 - 0.5), 0.3]) oh_boxc([L, 0.3, 0.32]);
    for (i = [0, 1])
    {
        sx = i * 2 - 1;
        color(oh_PLANKC()) translate([sx * (L / 2 + 0.8), 0, 0.28])
            rotate([0, sx * 18, 0]) oh_boxc([1.9, W - 0.3, 0.12]);
    }
    for (i = [0 : 2], j = [0, 1])
    {
        sy = j * 2 - 1;
        color(oh_WOODC()) translate([-L / 2 + 0.35 + i * (L - 0.7) / 2, sy * (W / 2 - 0.1), 1.05])
            oh_boxc([0.15, 0.15, 1.0]);
    }
    color(oh_WOODC()) for (j = [0, 1])
    {
        translate([0, (j * 2 - 1) * (W / 2 - 0.1), 1.42]) oh_boxc([L, 0.08, 0.13]);
        translate([0, (j * 2 - 1) * (W / 2 - 0.1), 1.0]) oh_boxc([L, 0.06, 0.1]);
    }
}

// 原木堆（3+2+1 金字塔 + 侧桩，货运目标物）
module oh_prop_log_pile(seed = 0)
{
    for (i = [0 : 2])
        color(i % 2 == 0 ? oh_WOODC() : [0.38, 0.28, 0.18])
            translate([0, -0.6 + i * 0.6, 0.29]) rotate([0, 90, 0])
                cylinder(h = 3.0, r = 0.29, $fn = 7, center = true);
    for (i = [0, 1])
        color(i == 0 ? [0.38, 0.28, 0.18] : oh_WOODC())
            translate([0, -0.3 + i * 0.6, 0.79]) rotate([0, 90, 0])
                cylinder(h = 2.9, r = 0.28, $fn = 7, center = true);
    color(oh_WOODC()) translate([0, 0, 1.27]) rotate([0, 90, 0])
        cylinder(h = 2.8, r = 0.27, $fn = 7, center = true);
    for (i = [0 : 2])
        color([0.55, 0.44, 0.28]) translate([1.51, -0.6 + i * 0.6, 0.29])
            rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.24, $fn = 7);
    for (i = [0, 1])
        color([0.55, 0.44, 0.28]) translate([1.46, -0.3 + i * 0.6, 0.79])
            rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.23, $fn = 7);
    color([0.55, 0.44, 0.28]) translate([1.41, 0, 1.27])
        rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.22, $fn = 7);
    color(oh_WOODD()) for (i = [0, 1])
        translate([(i * 2 - 1) * 1.2, 0.95, 0.55]) rotate([12, 0, 0]) oh_boxc([0.12, 0.12, 1.1]);
}

// A 形帐篷（脊沿 y，门开向 -y）
module oh_prop_tent(seed = 0)
{
    c = oh_tent_c(seed);
    rotate([0, 0, 90]) oh_part_roof(L = 2.5, D = 2.1, h = 1.4, ov = 0.05, rin = 0, c = c);
    color(oh_DARKC()) translate([0, -1.28, 0.42]) oh_boxc([0.55, 0.06, 0.8]);
    color(oh_WOODD()) for (i = [0, 1])
        translate([0, (i * 2 - 1) * 1.32, 0.72]) rotate([(i * 2 - 1) * 6, 0, 0])
            oh_boxc([0.06, 0.06, 1.5]);
    color(oh_TRIMW()) translate([0, -0.85, 1.4]) oh_boxc([0.32, 0.55, 0.04]);
}

// 篝火（石圈 + 架木 + 火苗）
module oh_prop_campfire(seed = 0)
{
    color([0.25, 0.24, 0.22]) cylinder(h = 0.04, r = 0.42, $fn = 8);
    for (i = [0 : 7])
        color(i % 2 == 0 ? oh_ROCKD() : oh_ROCKC())
            rotate([0, 0, i * 45 + oh_rnd(seed + i, 20)])
                translate([0.55, 0, 0.1])
                    rotate([0, 0, oh_rnd(seed + i + 3, 40)]) oh_boxc([0.3, 0.2, 0.2]);
    for (i = [0 : 2])
        color([0.16, 0.13, 0.1])
            rotate([0, 0, i * 120 + oh_rnd(seed + i, 40)])
                translate([0.18, 0, 0.05]) rotate([0, -72, 0]) cylinder(h = 0.7, r = 0.07, $fn = 5);
    color([0.85, 0.45, 0.1]) translate([0, 0, 0.16]) cylinder(h = 0.45, r1 = 0.16, r2 = 0.02, $fn = 6);
    color([0.92, 0.7, 0.24]) translate([0, 0, 0.16]) cylinder(h = 0.28, r1 = 0.09, r2 = 0.02, $fn = 6);
}

// 木路标（双向箭头板）
module oh_prop_signpost(seed = 0)
{
    color(oh_WOODD()) translate([0, 0, 1.15]) oh_boxc([0.13, 0.13, 2.3]);
    color(oh_PLANKC()) translate([0.5, 0, 1.92]) rotate([0, 0, oh_rndr(seed, -20, 20)])
        oh_boxc([1.2, 0.07, 0.3]);
    color(oh_DARKC()) translate([0.5, 0, 1.92]) rotate([0, 0, oh_rndr(seed, -20, 20)])
        translate([0, -0.045, 0]) oh_boxc([0.9, 0.01, 0.14]);
    color(oh_PLANKC()) translate([-0.45, 0, 1.5]) rotate([0, 0, 180 + oh_rndr(seed + 5, -20, 20)])
        oh_boxc([1.1, 0.07, 0.28]);
}

// 原木栅栏（X 形腿 + 双横杆，沿 x 通长）
module oh_prop_fence_log(len = 4)
{
    n = max(2, floor(len / 2) + 1);
    for (i = [0 : n - 1], j = [0, 1])
        color(oh_WOODD())
            translate([-len / 2 + i * len / (n - 1), 0, 0.52])
                rotate([(j * 2 - 1) * 26, 0, 0]) oh_boxc([0.09, 0.09, 1.15]);
    color(oh_WOODC()) translate([0, 0, 0.92]) rotate([0, 90, 0])
        cylinder(h = len + 0.4, r = 0.07, $fn = 6, center = true);
    color(oh_WOODC()) translate([0, 0.12, 0.5]) rotate([0, 90, 0])
        cylinder(h = len + 0.2, r = 0.06, $fn = 6, center = true);
}

// 油桶（seed 选色：锈/红/橄榄/灰）
module oh_prop_barrel(seed = 0)
{
    c = oh_drum_c(seed);
    color(c) cylinder(h = 0.86, r = 0.3, $fn = 8);
    color([c[0] * 0.72, c[1] * 0.72, c[2] * 0.72]) for (i = [0, 1])
        translate([0, 0, 0.26 + i * 0.32]) cylinder(h = 0.05, r = 0.315, $fn = 8);
}

// 油壶 jerry can
module oh_prop_jerrycan(seed = 0)
{
    c = oh_rnd(seed, 2) == 0 ? oh_REDC() : [0.32, 0.38, 0.26];
    color(c) translate([0, 0, 0.23]) oh_boxc([0.24, 0.36, 0.46]);
    color(c) translate([0, 0, 0.49]) oh_boxc([0.06, 0.2, 0.08]);
    color(oh_METALD()) translate([0, 0.13, 0.48]) oh_boxc([0.07, 0.07, 0.06]);
}

// 轮胎堆（3 叠 + 1 斜靠）
module oh_prop_tirestack(seed = 0)
{
    for (i = [0 : 2])
        color([0.09, 0.09, 0.09])
            translate([oh_rndr(seed + i, -0.06, 0.06), oh_rndr(seed + i + 4, -0.06, 0.06), i * 0.26])
                cylinder(h = 0.25, r = 0.42, $fn = 8);
    color([0.13, 0.13, 0.13]) translate([0, 0, 0.78]) cylinder(h = 0.01, r = 0.2, $fn = 8);
    color([0.09, 0.09, 0.09]) translate([0.75, 0.2, 0.4])
        rotate([0, 72, oh_rnd(seed, 90)]) cylinder(h = 0.25, r = 0.42, $fn = 8, center = true);
}

// 起终点旗门（双杆 + 三角旗串，检查点/赛段门）
module oh_prop_gate_flags(W = 7, seed = 0)
{
    color(oh_METALD()) for (i = [0, 1])
    {
        translate([(i * 2 - 1) * W / 2, 0, 0]) cylinder(h = 3.1, r = 0.07, $fn = 6);
        translate([(i * 2 - 1) * W / 2, 0, 0]) cylinder(h = 0.12, r = 0.16, $fn = 6);
    }
    color(oh_WOODD()) translate([0, 0, 2.88]) oh_boxc([W, 0.035, 0.035]);
    nf = max(4, floor(W / 0.62));
    for (i = [0 : nf - 1])
        color(i % 2 == 0 ? oh_REDC() : oh_TRIMW())
            translate([-W / 2 + (i + 0.75) * W / (nf + 0.5), 0, 2.68])
                rotate([0, 0, (i % 2) * 8 - 4]) oh_boxc([0.3, 0.025, 0.36]);
}

// ================= 建筑（front = -y） =================

// 原木小屋：石基 + 木墙(叠木线) + 双坡瓦 + 石烟囱 + 门窗木盖板
module oh_bldg_cabin(seed = 0, L = 6.5, D = 5)
{
    wh = 2.4;
    color([0.42, 0.4, 0.36]) oh_slab(L + 0.3, D + 0.3, 0.22);
    color(oh_WOODC()) translate([0, 0, 0.22]) oh_slab(L, D, wh - 0.22);
    for (i = [0 : 3])
        color(oh_WOODD()) translate([0, 0, 0.5 + i * 0.48]) oh_slab(L + 0.06, D + 0.06, 0.06);
    for (i = [0 : 3])
        color(oh_WOODD())
            translate([(floor(i / 2) * 2 - 1) * L / 2, (i % 2 * 2 - 1) * D / 2, 0.2])
                oh_slab(0.3, 0.3, wh - 0.2);
    translate([0, 0, wh]) oh_part_roof(L, D, 1.7, 0.55, 0, [0.24, 0.19, 0.15]);
    color(oh_WOODD()) translate([0, 0, wh + 1.56]) oh_boxc([L * 0.85, 0.3, 0.18]);
    color(oh_ROCKD()) translate([L * 0.3, D * 0.18, (wh + 2.5) / 2]) oh_boxc([0.65, 0.65, wh + 2.5]);
    color(oh_ROCKC()) translate([L * 0.3, D * 0.18, wh + 2.55]) oh_boxc([0.78, 0.78, 0.14]);
    // 门 + 窗（front=-y）
    color(oh_PLANKC()) translate([-L * 0.22, -D / 2 - 0.04, 1.05]) oh_boxc([0.95, 0.14, 1.9]);
    color(oh_WOODD()) translate([-L * 0.22, -D / 2 - 0.08, 1.05]) oh_boxc([0.12, 0.06, 1.8]);
    color(oh_DARKC()) translate([L * 0.2, -D / 2 - 0.04, 1.5]) oh_boxc([0.85, 0.1, 0.8]);
    color(oh_PLANKC()) for (i = [0, 1])
        translate([L * 0.2 + (i * 2 - 1) * 0.58, -D / 2 - 0.06, 1.5]) oh_boxc([0.28, 0.08, 0.85]);
    color(oh_DARKC()) translate([L / 2 + 0.04, D * 0.1, 1.5]) oh_boxc([0.1, 0.8, 0.7]);
    color([0.42, 0.4, 0.36]) translate([-L * 0.22, -D / 2 - 0.5, 0]) oh_slab(1.5, 0.9, 0.22);
}

// 护林瞭望塔：四内倾腿 + X 拉撑 + 平台小屋 + 四坡顶 + 爬梯（高点地标）
module oh_bldg_tower(seed = 0)
{
    ph = 5.2;
    for (i = [0 : 3])
    {
        sx = (i % 2) * 2 - 1;
        sy = (floor(i / 2)) * 2 - 1;
        color(oh_WOODD()) translate([sx * 0.92, sy * 0.92, (ph + 0.3) / 2])
            rotate([sy * 4.2, -sx * 4.2, 0]) oh_boxc([0.18, 0.18, ph + 0.3]);
    }
    for (i = [0 : 1], j = [0, 1])
        color(oh_WOODC())
            rotate([0, 0, i * 90])
                translate([0, -1.02, 1.9 + j * 1.9])
                    rotate([0, (j * 2 - 1) * 38, 0])
                        oh_boxc([2.0, 0.08, 0.08]);
    color(oh_WOODC()) for (i = [0, 1])
        translate([0, (i * 2 - 1) * 0.98, 3.0]) oh_boxc([1.9, 0.09, 0.09]);
    color(oh_PLANKC()) translate([0, 0, ph + 0.08]) oh_boxc([3.0, 3.0, 0.16]);
    for (i = [0 : 3])
        color(oh_WOODC())
            rotate([0, 0, i * 90])
                translate([0, -1.42, ph + 0.62]) oh_boxc([2.9, 0.07, 0.1]);
    for (i = [0 : 3])
        color(oh_WOODC())
            rotate([0, 0, i * 90])
                translate([1.35, -1.42, ph + 0.42]) oh_boxc([0.1, 0.1, 0.55]);
    color(oh_WOODC()) translate([0, 0, ph + 0.16]) oh_slab(2.1, 2.1, 1.0);
    color(oh_DARKC()) translate([0, 0, ph + 1.16]) oh_slab(1.9, 1.9, 0.55);
    for (i = [0 : 3])
        color(oh_WOODD())
            rotate([0, 0, i * 90])
                translate([1.02, -1.02, ph + 0.9]) oh_boxc([0.14, 0.14, 1.5]);
    translate([0, 0, ph + 1.7]) oh_part_roof(2.6, 2.6, 1.1, 0.3, 1.3, [0.24, 0.19, 0.15]);
    color(oh_METALC()) for (i = [0, 1])
        translate([(i * 2 - 1) * 0.24, -1.12, (ph + 0.2) / 2]) oh_boxc([0.06, 0.06, ph + 0.2]);
    nr = 9;
    color(oh_METALC()) for (i = [0 : nr - 1])
        translate([0, -1.12, 0.45 + i * (ph - 0.5) / (nr - 1)]) oh_boxc([0.5, 0.05, 0.05]);
}

// 波纹铁皮修车棚：单坡顶 + 大门洞 + 卷帘线 + 锈迹（front=-y）
module oh_bldg_garage(seed = 0, L = 7, D = 6)
{
    wh = 3.0;
    color(oh_METALC()) oh_slab(L, D, wh);
    nc = floor(L / 0.55);
    color(oh_METALD()) for (i = [0 : nc - 1])
        translate([-L / 2 + (i + 0.5) * L / nc, -D / 2 - 0.02, wh / 2 + 0.1])
            oh_boxc([0.06, 0.04, wh - 0.2]);
    color(oh_METALD()) translate([0, 0.3, wh + 0.16]) rotate([6, 0, 0]) oh_boxc([L + 0.6, D + 0.9, 0.12]);
    color(oh_DARKC()) translate([-L * 0.14, -D / 2 - 0.05, 1.3]) oh_boxc([3.2, 0.12, 2.6]);
    color(oh_METALD()) for (i = [0 : 3])
        translate([-L * 0.14, -D / 2 - 0.08, 2.3 - i * 0.18]) oh_boxc([3.0, 0.04, 0.05]);
    color(oh_TRIMW()) translate([-L * 0.14, -D / 2 - 0.1, 2.75]) oh_boxc([3.4, 0.06, 0.22]);
    color(oh_GLASSC()) translate([L * 0.32, -D / 2 - 0.03, 1.9]) oh_boxc([0.9, 0.08, 0.8]);
    color(oh_RUSTC())
    {
        translate([-L / 2 + 0.02, -D * 0.2, 0.5]) oh_boxc([0.03, 1.0, 0.9]);
        translate([L * 0.4, -D / 2 - 0.01, 0.4]) oh_boxc([0.7, 0.03, 0.7]);
    }
    color(oh_METALD()) translate([L / 2 - 0.5, D / 2 + 0.4, 0]) cylinder(h = 3.6, r = 0.09, $fn = 6);
}

// 荒野加油点：红雨棚 + 双油泵 + 小木亭（front=-y，油泵在前）
module oh_bldg_fuel(seed = 0)
{
    color(oh_METALD()) for (i = [0, 1])
        translate([(i * 2 - 1) * 2.0, -1.3, 0]) cylinder(h = 3.0, r = 0.09, $fn = 6);
    color(oh_REDC()) translate([0, 0, 3.0]) oh_boxc([4.9, 3.5, 0.18]);
    color(oh_TRIMW()) translate([0, -1.78, 3.0]) oh_boxc([4.9, 0.06, 0.3]);
    color(oh_WOODC()) translate([-1.3, 0.85, 0]) oh_slab(1.9, 1.5, 2.3);
    color(oh_PLANKC()) translate([-1.3, 0.85, 2.3]) oh_slab(2.2, 1.8, 0.12);
    color(oh_DARKC()) translate([-1.3, 0.08, 1.2]) oh_boxc([0.8, 0.06, 1.7]);
    color(oh_TRIMW()) translate([-1.3, 0.05, 2.0]) oh_boxc([1.5, 0.05, 0.35]);
    for (i = [0, 1])
    {
        px = 0.7 + i * 1.5;
        color(oh_REDC()) translate([px, -0.5, 0.55]) oh_boxc([0.5, 0.38, 1.1]);
        color(oh_DARKC()) translate([px, -0.7, 0.8]) oh_boxc([0.3, 0.02, 0.3]);
        color(oh_METALC()) translate([px, -0.5, 1.12]) oh_boxc([0.54, 0.42, 0.06]);
    }
    color(oh_ROCKD()) translate([1.45, -0.5, 0]) oh_slab(2.6, 1.0, 0.14);
}

// ================= 载具（车头朝 +x，底面 z=0） =================

// 越野胖轮
module oh_veh_wheel(r = 0.46, w = 0.34)
{
    color([0.09, 0.09, 0.09]) translate([0, w / 2, 0]) rotate([90, 0, 0]) cylinder(h = w, r = r, $fn = 8);
    color(oh_METALC()) translate([0, (w + 0.05) / 2, 0]) rotate([90, 0, 0])
        cylinder(h = w + 0.05, r = r * 0.42, $fn = 6);
}

// 硬派越野车：高底盘 + 行李架(帆布包/油壶) + 尾门备胎 + 前杠 + 涉水喉（参考图主角）
module oh_veh_offroader(seed = 0)
{
    c = oh_car_c(seed);
    color(oh_METALD()) translate([0, 0, 0.52]) oh_boxc([3.6, 1.5, 0.2]);
    color(c) translate([0.1, 0, 0.95]) oh_boxc([4.1, 1.86, 0.68]);
    color(c) translate([-0.5, 0, 1.55]) oh_boxc([2.4, 1.76, 0.56]);
    color(oh_GLASSC()) translate([-0.5, 0, 1.57]) oh_boxc([2.14, 1.84, 0.4]);
    color(oh_GLASSC()) translate([0.72, 0, 1.5]) oh_boxc([0.06, 1.6, 0.42]);
    color(oh_DARKC()) for (i = [0 : 3])
        translate([1.32 * ((floor(i / 2)) * 2 - 1), 0.94 * ((i % 2) * 2 - 1), 0.82]) oh_boxc([1.15, 0.1, 0.3]);
    color(oh_METALD())
    {
        for (i = [0, 1]) translate([-0.5, ((i * 2 - 1)) * 0.78, 1.88]) oh_boxc([2.2, 0.07, 0.1]);
        for (i = [0 : 2]) translate([-1.4 + i * 0.9, 0, 1.88]) oh_boxc([0.07, 1.6, 0.1]);
    }
    color([0.3, 0.34, 0.24]) translate([-0.85, 0.22, 2.08]) oh_boxc([1.15, 0.9, 0.34]);
    color(oh_REDC()) translate([0.1, -0.42, 2.02]) oh_boxc([0.26, 0.5, 0.34]);
    color([0.09, 0.09, 0.09]) translate([-2.22, 0.35, 1.05]) rotate([0, 90, 0])
        cylinder(h = 0.28, r = 0.42, $fn = 8, center = true);
    color(oh_METALC()) translate([-2.22, 0.35, 1.05]) rotate([0, 90, 0])
        cylinder(h = 0.34, r = 0.17, $fn = 6, center = true);
    color(oh_METALD())
    {
        translate([2.2, 0, 0.82]) oh_boxc([0.16, 1.7, 0.3]);
        for (i = [0, 1]) translate([2.24, ((i * 2 - 1)) * 0.5, 1.08]) oh_boxc([0.09, 0.09, 0.55]);
    }
    color([0.85, 0.83, 0.72]) for (i = [0, 1])
        translate([2.17, ((i * 2 - 1)) * 0.6, 1.12]) oh_boxc([0.06, 0.28, 0.16]);
    color([0.72, 0.14, 0.11]) for (i = [0, 1])
        translate([-2.17, ((i * 2 - 1)) * 0.6, 1.05]) oh_boxc([0.06, 0.28, 0.16]);
    color(oh_DARKC()) translate([0.55, 0.95, 1.5]) oh_boxc([0.12, 0.1, 0.8]);
    color(oh_DARKC()) translate([0.68, 0.95, 1.88]) oh_boxc([0.32, 0.1, 0.12]);
    for (i = [0 : 3])
        translate([1.35 * ((floor(i / 2)) * 2 - 1), 0.98 * ((i % 2) * 2 - 1), 0.46]) oh_veh_wheel();
}

// 旅行房车：一体厢身 + 车顶货筐 + 尾梯 + 备胎（参考图峡谷货车）
module oh_veh_van(seed = 0)
{
    c = oh_car_c(seed + 7);
    color(c) translate([-0.1, 0, 1.25]) oh_boxc([4.4, 1.9, 1.5]);
    color(c) translate([2.25, 0, 0.92]) oh_boxc([0.5, 1.8, 0.85]);
    color(oh_GLASSC()) translate([2.28, 0, 1.6]) oh_boxc([0.3, 1.7, 0.55]);
    color(oh_GLASSC()) translate([1.15, 0, 1.66]) oh_boxc([1.3, 1.94, 0.5]);
    color(oh_TRIMW()) translate([-0.1, 0, 0.95]) oh_boxc([4.44, 1.94, 0.3]);
    color(oh_METALD())
    {
        for (i = [0, 1]) translate([-0.4, ((i * 2 - 1)) * 0.75, 2.06]) oh_boxc([2.4, 0.07, 0.1]);
        for (i = [0 : 2]) translate([-1.3 + i * 0.9, 0, 2.06]) oh_boxc([0.07, 1.56, 0.1]);
    }
    color([0.34, 0.3, 0.24]) translate([-0.7, 0, 2.26]) oh_boxc([1.5, 1.2, 0.36]);
    color(oh_METALC())
    {
        for (i = [0, 1]) translate([-2.33, 0.42 + i * 0.35, 1.3]) oh_boxc([0.05, 0.05, 1.6]);
        for (i = [0 : 3]) translate([-2.35, 0.6, 0.75 + i * 0.4]) oh_boxc([0.04, 0.4, 0.05]);
    }
    color([0.09, 0.09, 0.09]) translate([-2.36, -0.4, 1.1]) rotate([0, 90, 0])
        cylinder(h = 0.24, r = 0.4, $fn = 8, center = true);
    color([0.85, 0.83, 0.72]) for (i = [0, 1])
        translate([2.52, ((i * 2 - 1)) * 0.6, 0.85]) oh_boxc([0.05, 0.28, 0.15]);
    color([0.72, 0.14, 0.11]) for (i = [0, 1])
        translate([-2.33, ((i * 2 - 1)) * 0.72, 0.9]) oh_boxc([0.05, 0.24, 0.2]);
    for (i = [0 : 3])
        translate([1.45 * ((floor(i / 2)) * 2 - 1), 0.98 * ((i % 2) * 2 - 1), 0.42])
            oh_veh_wheel(0.42, 0.3);
}

// 平板货卡：平头驾驶室 + 木质货台围栏 + 板条箱/油桶货物 + 双后轴
module oh_veh_truck_body(seed = 0)
{
    c = oh_car_c(seed + 17);
    color(oh_METALD()) translate([0.2, 0, 0.72]) oh_boxc([6.2, 1.1, 0.24]);
    color(c) translate([2.6, 0, 1.45]) oh_boxc([1.5, 2.0, 1.5]);
    color(oh_GLASSC()) translate([3.36, 0, 1.85]) oh_boxc([0.06, 1.8, 0.55]);
    color(oh_GLASSC()) translate([2.6, 0, 1.85]) oh_boxc([1.2, 2.04, 0.5]);
    color(oh_METALD()) translate([3.36, 0, 1.15]) oh_boxc([0.06, 1.6, 0.5]);
    color(oh_METALD()) translate([3.42, 0, 0.75]) oh_boxc([0.14, 2.0, 0.3]);
    color([0.85, 0.83, 0.72]) for (i = [0, 1])
        translate([3.4, ((i * 2 - 1)) * 0.7, 0.95]) oh_boxc([0.06, 0.26, 0.16]);
    color(oh_METALC()) translate([1.75, 0.8, 1.3]) oh_boxc([0.1, 0.1, 2.5]);
    color(oh_PLANKC()) translate([-0.7, 0, 1.02]) oh_boxc([4.4, 2.2, 0.14]);
    for (i = [0 : 4], j = [0, 1])
        color(oh_WOODD())
            translate([-2.7 + i * 1.0, ((j * 2 - 1)) * 1.06, 1.32]) oh_boxc([0.1, 0.08, 0.5]);
    color(oh_WOODD()) for (j = [0, 1])
        translate([-0.7, ((j * 2 - 1)) * 1.06, 1.54]) oh_boxc([4.4, 0.06, 0.1]);
    color(oh_METALC()) translate([1.2, -0.95, 0.62]) rotate([0, 90, 0])
        cylinder(h = 0.9, r = 0.26, $fn = 7, center = true);
}

module oh_veh_truck(seed = 0)
{
    oh_veh_truck_body(seed);
    color([0.42, 0.32, 0.2]) translate([-1.6, 0.3, 1.46]) oh_boxc([1.05, 1.05, 0.75]);
    color([0.48, 0.37, 0.23]) translate([-1.6, 0.3, 1.46]) oh_boxc([1.09, 0.16, 0.16]);
    color([0.42, 0.32, 0.2]) translate([-0.4, -0.45, 1.38]) oh_boxc([0.8, 0.8, 0.6]);
    color(oh_drum_c(seed + 3)) translate([0.55, 0.35, 1.09]) cylinder(h = 0.8, r = 0.28, $fn = 8);
    for (i = [0, 1]) translate([2.5, ((i * 2 - 1)) * 1.0, 0.5]) oh_veh_wheel(0.5, 0.36);
    for (i = [0, 1], k = [0, 1])
        translate([-0.6 - k * 1.15, ((i * 2 - 1)) * 1.0, 0.5]) oh_veh_wheel(0.5, 0.5);
}

// 单轴原木拖车（挂钩朝 +x，接卡车尾）
module oh_veh_trailer(seed = 0)
{
    color(oh_METALD()) translate([-0.2, 0, 0.66]) oh_boxc([2.8, 1.3, 0.16]);
    color(oh_METALD()) for (i = [0, 1])
        translate([1.55, ((i * 2 - 1)) * 0.28, 0.64]) rotate([0, 0, ((i * 2 - 1)) * -14])
            oh_boxc([1.15, 0.1, 0.1]);
    color(oh_METALC()) translate([2.1, 0, 0.6]) oh_boxc([0.18, 0.18, 0.1]);
    color(oh_METALD()) translate([1.7, 0, 0.28]) oh_boxc([0.08, 0.08, 0.6]);
    for (i = [0, 1], j = [0, 1])
        color(oh_WOODD())
            translate([-1.15 + i * 1.85, ((j * 2 - 1)) * 0.72, 1.1]) oh_boxc([0.1, 0.1, 0.85]);
    color(oh_METALD()) for (i = [0, 1])
        translate([-1.15 + i * 1.85, 0, 0.78]) oh_boxc([0.12, 1.5, 0.1]);
    for (i = [0, 1])
        color(i == 0 ? oh_WOODC() : [0.38, 0.28, 0.18])
            translate([0, ((i * 2 - 1)) * 0.28, 1.12]) rotate([0, 90, 0])
                cylinder(h = 3.2, r = 0.26, $fn = 7, center = true);
    color(oh_WOODC()) translate([0, 0, 1.56]) rotate([0, 90, 0])
        cylinder(h = 3.0, r = 0.25, $fn = 7, center = true);
    for (i = [0 : 2])
        color([0.55, 0.44, 0.28])
            translate([1.61 - (i == 2 ? 0.1 : 0), (i < 2 ? ((i * 2 - 1)) * 0.28 : 0), i < 2 ? 1.12 : 1.56])
                rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.21, $fn = 7);
    for (i = [0, 1]) translate([-0.3, ((i * 2 - 1)) * 0.75, 0.4]) oh_veh_wheel(0.4, 0.28);
}
