// kit_deadly.scad —— Deadly Days: Roadtrip 风格美式郊区零件库（丧尸末日小镇）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "dd_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具车头朝 +x）。调用方自设 $fn（建议 12）。
// 尺度：mid（与 kit_old_city 同级；民房 ~8x6、轿车长 ~4）。
// 主题元素：木板房/门廊房/工具棚/石教堂/街角店/红谷仓、沥青路网、层叠松、白栅栏、
//           电线杆/公路绿牌（含倒塌残骸）、油桶/锥桶/路障、轿车/厢式车/皮卡/烧毁残骸/
//           翻覆车/联合收割机、泥地斑块/水洼、垄沟田/南瓜田/树桩/倒木、
//           干草捆/木托盘/轮胎堆/油壶/地面杂物簇/倒塌风机。

// ================= 配色（低饱和末日郊区；PT 强日光下会整体提亮，故基色偏深偏饱和） =================
function dd_ASPHC()  = [0.32, 0.32, 0.34];   // 沥青路面
function dd_ASPHD()  = [0.26, 0.26, 0.28];   // 沥青补丁
function dd_WALKC()  = [0.55, 0.54, 0.51];   // 人行道混凝土
function dd_WALKD()  = [0.44, 0.43, 0.41];   // 混凝土伸缩缝/压顶
function dd_MARKW()  = [0.80, 0.80, 0.75];   // 白标线
function dd_MARKY()  = [0.76, 0.58, 0.14];   // 黄标线
function dd_GRASSC() = [0.39, 0.45, 0.26];   // 草地
function dd_GRASSD() = [0.32, 0.38, 0.21];   // 深草斑
function dd_SOILC()  = [0.40, 0.30, 0.21];   // 泥土/田地
function dd_WOODD()  = [0.26, 0.19, 0.14];   // 深木（棚屋/电线杆）
function dd_TRUNKC() = [0.32, 0.24, 0.16];   // 树干
function dd_PINED()  = [0.12, 0.23, 0.13];   // 松针深
function dd_PINEC()  = [0.17, 0.31, 0.16];   // 松针中
function dd_LEAFC()  = [0.34, 0.45, 0.21];   // 阔叶
function dd_LEAFD()  = [0.27, 0.37, 0.17];   // 阔叶深
function dd_ROOFS()  = [0.20, 0.23, 0.28];   // 蓝灰石板瓦
function dd_ROOFD()  = [0.14, 0.16, 0.20];   // 深瓦/正脊
function dd_TERRA()  = [0.58, 0.25, 0.10];   // 教堂陶瓦橙
function dd_STONEC() = [0.60, 0.58, 0.53];   // 教堂石墙
function dd_TRIMW()  = [0.82, 0.81, 0.76];   // 白木饰边/栅栏
function dd_PLINTH() = [0.46, 0.45, 0.42];   // 房基混凝土
function dd_DARKC()  = [0.11, 0.10, 0.10];   // 窗芯/洞口
function dd_GLASSC() = [0.34, 0.44, 0.50];   // 车窗玻璃
function dd_METALC() = [0.46, 0.48, 0.50];   // 镀锌金属
function dd_METALD() = [0.29, 0.31, 0.33];   // 深金属
function dd_RUSTC()  = [0.38, 0.20, 0.13];   // 锈
function dd_REDC()   = [0.62, 0.17, 0.12];   // 消防栓红
function dd_BLUEC()  = [0.22, 0.36, 0.53];   // 垃圾箱/板条箱蓝
function dd_SIGNG()  = [0.10, 0.34, 0.21];   // 公路牌绿
function dd_CONEO()  = [0.80, 0.37, 0.08];   // 锥桶橙

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function dd_sq(x) = (x * x + x * 613 + 29) % 65521;
function dd_rnd(s, m) = dd_sq(dd_sq(((s % 65521) + 65521) % 65521) + 7) % m;
function dd_rndf(s) = dd_rnd(s, 1000) / 999;                       // [0, 1]
function dd_rndr(s, a, b) = a + (b - a) * dd_rndf(s);              // [a, b]

// ---- 变体调色板 ----
function dd_wall_c(i) = [[0.50, 0.24, 0.16], [0.55, 0.38, 0.23], [0.48, 0.45, 0.38],
                         [0.36, 0.30, 0.24], [0.54, 0.48, 0.37]][dd_rnd(i, 5)];      // 板墙
function dd_roof_c(i) = [dd_ROOFS(), dd_ROOFD(), [0.27, 0.22, 0.20], [0.23, 0.27, 0.33]][dd_rnd(i, 4)];
function dd_car_c(i)  = [[0.58, 0.16, 0.12], [0.70, 0.52, 0.15], [0.20, 0.40, 0.36],
                         [0.64, 0.64, 0.60], [0.27, 0.30, 0.37], [0.46, 0.28, 0.16]][dd_rnd(i, 6)];
function dd_drum_c(i) = [dd_RUSTC(), dd_BLUEC(), [0.34, 0.44, 0.30], dd_METALD()][dd_rnd(i, 4)];

// ---- 基础工具 ----
module dd_boxc(s) cube(s, center = true);
module dd_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) dd_boxc([L, D, t]);   // 底面 z=0 平板

// ================= 通用构件 =================

// 坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙皮，h=脊高，ov=出檐，
// rin=山面内收（0=双坡，rin>=L/2=攒尖）。面序为 OpenSCAD 约定（从外看顺时针）。
module dd_part_roof(L = 8, D = 6, h = 1.8, ov = 0.5, rin = 0, c = [0.28, 0.31, 0.36])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// 白框窗（贴墙面用，front=-y，锚点=窗中心）
module dd_part_window(w = 0.9, h = 1.0)
{
    color(dd_TRIMW()) dd_boxc([w, 0.14, h]);
    color(dd_DARKC()) translate([0, -0.03, 0]) dd_boxc([w - 0.2, 0.14, h - 0.2]);
    color(dd_TRIMW()) translate([0, -0.06, 0]) dd_boxc([0.08, 0.12, h - 0.2]);
}

// 木门带白框（front=-y，底面 z=0）
module dd_part_door(w = 1.0, h = 2.0, c = [0.33, 0.25, 0.19])
{
    color(dd_TRIMW()) translate([0, 0, h / 2]) dd_boxc([w + 0.24, 0.12, h]);
    color(c) translate([0, -0.03, h / 2]) dd_boxc([w, 0.14, h]);
    color([0.80, 0.72, 0.30]) translate([w * 0.3, -0.11, h * 0.5]) dd_boxc([0.08, 0.06, 0.08]);
}

// ================= 地面（沿 x 铺设，底面 z=0） =================

// 沥青路段：双黄中线 + 两侧白虚边线 + 破损补丁
module dd_ground_road(L = 24, W = 7, seed = 0)
{
    color(dd_ASPHC()) dd_slab(L, W, 0.12);
    for (i = [0 : 2])
        color(dd_ASPHD())
            translate([dd_rndr(seed * 7 + i * 31, -(L - 4) / 2, (L - 4) / 2),
                       dd_rndr(seed * 13 + i * 17 + 5, -(W - 2.4) / 2, (W - 2.4) / 2), 0.12])
                dd_slab(1.2 + dd_rnd(seed + i, 3) * 0.5, 0.9 + dd_rnd(seed + i + 9, 3) * 0.4, 0.012);
    color(dd_MARKY()) for (sy = [-1, 1]) translate([0, sy * 0.14, 0.12]) dd_slab(L, 0.11, 0.02);
    nd = floor(L / 2.4);
    color(dd_MARKW()) for (i = [0 : nd - 1], sy = [-1, 1])
        translate([-L / 2 + 1.2 + i * 2.4, sy * (W / 2 - 0.38), 0.12]) dd_slab(1.4, 0.13, 0.02);
}

// 十字路口块：W x W 沥青 + 四向人行横道
module dd_ground_cross(W = 7, seed = 0)
{
    color(dd_ASPHC()) dd_slab(W, W, 0.12);
    color(dd_ASPHD()) translate([dd_rndr(seed + 3, -1.5, 1.5), dd_rndr(seed + 11, -1.5, 1.5), 0.12])
        dd_slab(1.6, 1.2, 0.012);
    for (a = [0, 90, 180, 270])
        rotate([0, 0, a])
            color(dd_MARKW()) for (i = [0 : 4])
                translate([-W / 2 + 0.9 + i * (W - 1.8) / 4, -W / 2 + 0.75, 0.12])
                    dd_slab(0.4, 1.1, 0.02);
}

// 人行道（混凝土板 + 伸缩缝）
module dd_ground_sidewalk(L = 8, W = 1.8)
{
    color(dd_WALKC()) dd_slab(L, W, 0.16);
    nj = floor(L / 1.6);
    color(dd_WALKD()) for (i = [1 : nj - 1])
        translate([-L / 2 + i * 1.6, 0, 0.16]) dd_slab(0.07, W, 0.012);
}

// 草地块（带深色草斑，用于覆盖 spec ground 之上的院子）
module dd_ground_grass(L = 20, D = 20, seed = 0)
{
    color(dd_GRASSC()) dd_slab(L, D, 0.10);
    for (i = [0 : 5])
        color(dd_GRASSD())
            translate([dd_rndr(seed * 31 + i * 47, -(L - 4) / 2, (L - 4) / 2),
                       dd_rndr(seed * 17 + i * 71 + 3, -(D - 4) / 2, (D - 4) / 2), 0.10])
                dd_slab(dd_rndr(seed + i, 1.6, 3.4), dd_rndr(seed + i + 5, 1.2, 2.6), 0.012);
}

// ================= 建筑（front = -y） =================

// 郊区木板房：混凝土基座 + 板墙 + 蓝灰双坡瓦 + 白饰边 + 砖烟囱。
// porch=1 时门居中并带全宽门廊（甲板/白柱/披檐/栏杆），否则门偏一侧带台阶。
module dd_bldg_house(seed = 0, L = 8, D = 6, porch = 0)
{
    wh = 2.6;
    wc = dd_wall_c(seed);
    rc = dd_roof_c(seed + 3);
    dx = porch == 1 ? 0 : (dd_rnd(seed + 5, 2) == 0 ? -1 : 1) * L * 0.24;
    color(dd_PLINTH()) dd_slab(L + 0.2, D + 0.2, 0.25);
    color(wc) translate([0, 0, 0.25]) dd_slab(L, D, wh - 0.25);
    color(dd_TRIMW()) translate([0, 0, wh]) dd_slab(L + 0.16, D + 0.16, 0.16);
    translate([0, 0, wh + 0.16]) dd_part_roof(L, D, 1.9, 0.55, 0, rc);
    color(dd_ROOFD()) translate([0, 0, wh + 2.06]) dd_boxc([L * 0.8, 0.34, 0.2]);
    color([0.48, 0.30, 0.24]) translate([L * 0.28, D * 0.18, wh + 1.0]) dd_boxc([0.6, 0.6, 2.4]);
    color(dd_PLINTH()) translate([L * 0.28, D * 0.18, wh + 2.26]) dd_boxc([0.74, 0.74, 0.14]);
    // 门与前窗
    translate([dx, -D / 2 - 0.04, 0.25]) dd_part_door();
    translate([-dx == 0 ? L * 0.3 : -dx, -D / 2 - 0.04, 1.55]) dd_part_window();
    if (porch == 1) translate([-L * 0.3, -D / 2 - 0.04, 1.55]) dd_part_window();
    // 侧窗（front=-y 的窗旋转贴东西山墙）
    translate([L / 2 + 0.04, -D * 0.15, 1.55]) rotate([0, 0, 90]) dd_part_window();
    translate([-L / 2 - 0.04, D * 0.15, 1.55]) rotate([0, 0, -90]) dd_part_window();
    if (porch == 0)
        color(dd_PLINTH()) translate([dx, -D / 2 - 0.5, 0]) dd_slab(1.7, 1.0, 0.25);
    if (porch == 1)
    {
        pd = 1.9;
        color(dd_PLINTH()) translate([0, -D / 2 - pd / 2, 0]) dd_slab(L, pd, 0.3);
        color(dd_TRIMW()) for (i = [0 : 3])
            translate([-L / 2 + 0.35 + i * (L - 0.7) / 3, -D / 2 - pd + 0.25, 1.4]) dd_boxc([0.16, 0.16, 2.2]);
        color(rc) translate([0, -D / 2 - pd / 2 - 0.15, 2.72]) rotate([7, 0, 0]) dd_boxc([L + 0.5, pd + 0.7, 0.14]);
        // 栏杆（门洞居中留空）
        color(dd_TRIMW()) for (sx = [-1, 1])
        {
            translate([sx * (L / 2 - (L - 1.6) / 8 - 0.1), -D / 2 - pd + 0.25, 0.95]) dd_boxc([(L - 1.6) / 4 + 0.4, 0.07, 0.1]);
            for (i = [0 : 2])
                translate([sx * (0.95 + i * (L / 2 - 1.4) / 2.6), -D / 2 - pd + 0.25, 0.62]) dd_boxc([0.07, 0.05, 0.62]);
        }
        color(dd_PLINTH()) translate([0, -D / 2 - pd - 0.45, 0]) dd_slab(1.7, 0.9, 0.18);
    }
}

// 带门廊变体（catalog 直选）
module dd_bldg_house_porch(seed = 1, L = 9, D = 6.5) dd_bldg_house(seed, L, D, 1);

// 后院工具棚：深木板 + 单坡顶
module dd_bldg_shed(seed = 0, L = 3.4, D = 2.8)
{
    wh = 2.0;
    color(dd_WOODD()) dd_slab(L, D, wh);
    color(dd_roof_c(seed + 1)) translate([0, 0, wh + 0.14]) rotate([7, 0, 0]) dd_boxc([L + 0.5, D + 0.6, 0.14]);
    color(dd_METALD()) translate([0, -D / 2 - 0.04, 0.9]) dd_boxc([1.1, 0.08, 1.8]);
    color(dd_TRIMW()) translate([0, -D / 2 - 0.09, 0.9]) dd_boxc([0.07, 0.04, 1.7]);
    color(dd_TRIMW()) translate([0, -D / 2 - 0.09, 1.78]) dd_boxc([1.24, 0.04, 0.07]);
}

// 小镇石砌教堂：陡坡陶瓦顶 + 前塔尖 + 尖窗（front=-y，长轴沿 y）
module dd_bldg_church(seed = 0, L = 7, D = 12)
{
    wh = 4.2;
    color(dd_PLINTH()) dd_slab(L + 0.3, D + 0.3, 0.3);
    color(dd_STONEC()) translate([0, 0, 0.3]) dd_slab(L, D, wh - 0.3);
    rotate([0, 0, 90]) translate([0, 0, wh]) dd_part_roof(D, L, 2.8, 0.5, 0, dd_TERRA());
    color([0.55, 0.28, 0.16]) translate([0, 0, wh + 2.92]) dd_boxc([0.3, D * 0.8, 0.18]);
    // 侧尖窗
    for (sx = [-1, 1], i = [0 : 2])
    {
        color(dd_DARKC()) translate([sx * (L / 2 + 0.03), -D / 2 + 3 + i * 3, 2.4]) dd_boxc([0.1, 0.7, 1.7]);
        color(dd_TRIMW()) translate([sx * (L / 2 + 0.05), -D / 2 + 3 + i * 3, 1.5]) dd_boxc([0.12, 0.86, 0.14]);
    }
    // 前塔（半嵌入）+ 攒尖顶 + 十字
    th = 6.4;
    color(dd_STONEC()) translate([0, -D / 2 - 0.5, 0]) dd_slab(3, 3, th);
    translate([0, -D / 2 - 0.5, th]) dd_part_roof(3.2, 3.2, 2.0, 0.25, 1.6, dd_TERRA());
    color(dd_TRIMW())
    {
        translate([0, -D / 2 - 0.5, th + 2.5]) dd_boxc([0.09, 0.09, 1.0]);
        translate([0, -D / 2 - 0.5, th + 2.72]) dd_boxc([0.5, 0.09, 0.09]);
    }
    color(dd_DARKC()) translate([0, -D / 2 - 0.5, th - 1.2]) dd_boxc([0.8, 3.1, 1.2]);   // 钟窗
    translate([0, -D / 2 - 2.03, 0]) dd_part_door(w = 1.3, h = 2.4, c = [0.30, 0.20, 0.14]);
    color(dd_PLINTH()) translate([0, -D / 2 - 2.5, 0]) dd_slab(2.2, 1.0, 0.2);
}

// 街角便利店：平顶女儿墙 + 橱窗 + 条纹雨棚 + 色块招牌（front=-y）
module dd_bldg_shop(seed = 0, L = 8, D = 6)
{
    wh = 3.2;
    wc = [[0.54, 0.49, 0.41], [0.52, 0.36, 0.26], [0.46, 0.46, 0.43], [0.41, 0.33, 0.29]][dd_rnd(seed, 4)];
    sc = [[0.62, 0.17, 0.12], [0.72, 0.52, 0.12], [0.20, 0.36, 0.52], [0.20, 0.42, 0.26]][dd_rnd(seed + 2, 4)];
    color(wc) dd_slab(L, D, wh);
    color(dd_WALKD()) translate([0, 0, wh]) dd_slab(L + 0.2, D + 0.2, 0.15);
    color(dd_METALC()) translate([L * 0.2, D * 0.15, wh + 0.15]) dd_boxc([1.4, 1.0, 0.7]);   // 屋顶机组
    // 橱窗带 + 玻璃门
    color(dd_DARKC()) translate([-0.9, -D / 2 - 0.03, 1.35]) dd_boxc([L - 3.2, 0.1, 1.5]);
    color(dd_TRIMW()) for (i = [0 : 2])
        translate([-0.9 - (L - 3.2) / 2 + (i + 0.5) * (L - 3.2) / 3 + (i - 1) * 0.0, -D / 2 - 0.06, 1.35])
            dd_boxc([0.07, 0.08, 1.5]);
    color(dd_DARKC()) translate([L / 2 - 1.3, -D / 2 - 0.03, 1.05]) dd_boxc([1.0, 0.1, 2.1]);
    color(dd_METALD()) translate([L / 2 - 1.3, -D / 2 - 0.08, 1.05]) dd_boxc([0.08, 0.06, 1.9]);
    // 招牌带 + 字块示意
    color(sc) translate([0, -D / 2 - 0.12, 2.7]) dd_boxc([L - 0.8, 0.2, 0.75]);
    color(dd_TRIMW()) translate([-L * 0.12, -D / 2 - 0.24, 2.7]) dd_boxc([2.4, 0.03, 0.4]);
    // 条纹雨棚
    for (i = [0 : 5])
        color(i % 2 == 0 ? sc : dd_TRIMW())
            translate([-0.9 - (L - 2.8) / 2 + (i + 0.5) * (L - 2.8) / 6, -D / 2 - 0.55, 1.98])
                rotate([14, 0, 0]) dd_boxc([(L - 2.8) / 6, 1.15, 0.06]);
}

// ================= 植被（底面 z=0） =================

// 层叠针叶松（路旁标志树）：4 层锥叠，seed 交替深浅
module dd_nature_pine(s = 1.0, seed = 0)
{
    c1 = dd_rnd(seed, 2) == 0 ? dd_PINED() : dd_PINEC();
    c2 = dd_rnd(seed, 2) == 0 ? dd_PINEC() : dd_PINED();
    scale([s, s, s])
    {
        color(dd_TRUNKC()) cylinder(h = 1.0, r = 0.22, $fn = 6);
        color(c1) translate([0, 0, 0.8]) cylinder(h = 1.7, r1 = 1.55, r2 = 0.95, $fn = 7);
        color(c2) translate([0, 0, 2.1]) cylinder(h = 1.6, r1 = 1.25, r2 = 0.62, $fn = 7);
        color(c1) translate([0, 0, 3.4]) cylinder(h = 1.5, r1 = 0.95, r2 = 0.34, $fn = 7);
        color(c2) translate([0, 0, 4.6]) cylinder(h = 1.2, r1 = 0.60, r2 = 0.04, $fn = 7);
    }
}

// 阔叶树（团状低模）
module dd_nature_tree(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(dd_TRUNKC()) cylinder(h = 1.5, r = 0.22, $fn = 6);
        color(dd_rnd(seed, 2) == 0 ? dd_LEAFC() : dd_LEAFD()) translate([0, 0, 2.5]) sphere(r = 1.5, $fn = 6);
        color(dd_LEAFD()) translate([0.7, 0.4, 3.2]) sphere(r = 0.9, $fn = 6);
        color(dd_LEAFC()) translate([-0.6, -0.35, 3.3]) sphere(r = 0.75, $fn = 6);
    }
}

// 灌木丛
module dd_nature_bush(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(dd_rnd(seed, 2) == 0 ? dd_LEAFD() : dd_PINEC()) translate([0, 0, 0.42]) sphere(r = 0.55, $fn = 6);
        color(dd_LEAFC()) translate([0.4, 0.15, 0.36]) sphere(r = 0.38, $fn = 6);
        color(dd_LEAFD()) translate([-0.35, -0.2, 0.34]) sphere(r = 0.34, $fn = 6);
    }
}

// 草簇（院子/路缝点缀）
module dd_nature_grass(seed = 0)
{
    for (i = [0 : 2])
        color(i % 2 == 0 ? dd_GRASSD() : [0.52, 0.58, 0.35])
            rotate([0, 0, dd_rnd(seed + i, 180)])
                translate([0, 0, dd_rndr(seed + i + 3, 0.1, 0.18)])
                    dd_boxc([0.5, 0.06, dd_rndr(seed + i + 7, 0.2, 0.36)]);
}

// 玉米田块：田土 + 成行秸秆（末日废耕地）
module dd_nature_crop_patch(L = 4.5, D = 3.5, seed = 0)
{
    color(dd_SOILC()) dd_slab(L, D, 0.15);
    nr = floor(D / 1.1);
    nc = floor(L / 0.65);
    for (r = [0 : nr - 1], c = [0 : nc - 1])
    {
        ci = dd_rnd(seed + r * 31 + c * 7, 3);
        translate([-L / 2 + 0.5 + c * (L - 1.0) / (nc - 1), -D / 2 + 0.6 + r * (D - 1.2) / max(1, nr - 1), 0.15])
        {
            color(ci == 0 ? [0.62, 0.58, 0.30] : dd_LEAFD()) cylinder(h = 0.9 + ci * 0.12, r = 0.05, $fn = 5);
            color([0.78, 0.64, 0.26]) translate([0, 0, 0.85 + ci * 0.12]) dd_boxc([0.1, 0.1, 0.28]);
        }
    }
}

// ================= 道具（底面 z=0；带朝向者 front=-y） =================

// 白尖桩栅栏（沿 x 通长，两道横档）
module dd_prop_fence(len = 4, h = 1.05)
{
    n = max(2, floor(len / 0.45));
    color(dd_TRIMW())
    {
        for (i = [0 : n - 1])
            translate([-len / 2 + (i + 0.5) * len / n, 0, h / 2]) dd_boxc([0.24, 0.06, h]);
        translate([0, 0, h - 0.3]) dd_boxc([len, 0.05, 0.12]);
        translate([0, 0, h * 0.42]) dd_boxc([len, 0.05, 0.12]);
    }
}

// 郊区路灯（暖光灯头悬向 -y）
module dd_prop_lamp()
{
    color(dd_METALD())
    {
        cylinder(h = 0.14, r = 0.16, $fn = 7);
        cylinder(h = 4.6, r = 0.07, $fn = 6);
        translate([0, -0.42, 4.55]) dd_boxc([0.09, 0.95, 0.08]);
    }
    color([0.95, 0.90, 0.72]) translate([0, -0.82, 4.48]) dd_boxc([0.24, 0.55, 0.14]);
}

// 木电线杆：横担沿 y（沿 x 路边排布时电线方向即路向）
module dd_prop_pole(seed = 0)
{
    color(dd_WOODD()) cylinder(h = 6.8, r = 0.15, $fn = 6);
    color(dd_WOODD()) translate([0, 0, 6.15]) dd_boxc([0.16, 2.0, 0.15]);
    color(dd_METALC()) for (i = [-1, 0, 1])
        translate([0, i * 0.8, 6.3]) cylinder(h = 0.16, r = 0.05, $fn = 5);
    if (dd_rnd(seed, 3) == 0)
        color(dd_WOODD()) translate([0, 0, 5.2]) rotate([0, 0, 30]) dd_boxc([0.14, 1.4, 0.13]);
}

// 公路绿牌（front=-y，双柱）
module dd_prop_sign(seed = 0)
{
    color(dd_METALC()) for (sx = [-1, 1])
        translate([sx * 1.15, 0, 1.6]) dd_boxc([0.1, 0.1, 3.2]);
    color(dd_SIGNG()) translate([0, -0.07, 2.75]) dd_boxc([3.0, 0.08, 1.3]);
    color(dd_MARKW())
    {
        translate([0, -0.12, 2.75]) dd_boxc([2.84, 0.01, 1.14]);
        translate([-0.35, -0.14, 2.95]) dd_boxc([1.6, 0.01, 0.24]);
        translate([0.25, -0.14, 2.55]) dd_boxc([1.2, 0.01, 0.2]);
    }
    color(dd_SIGNG()) translate([0, -0.13, 2.75]) dd_boxc([2.7, 0.01, 1.0]);
}

// 倒塌的公路牌残骸（撞弯的柱 + 砸地的牌面）
module dd_prop_sign_fallen(seed = 0)
{
    color(dd_METALC()) translate([-1.1, 0, 0]) cylinder(h = 0.9, r = 0.06, $fn = 5);
    color(dd_METALC()) translate([-1.1, 0, 0.85]) rotate([0, 65, dd_rnd(seed, 40) - 20]) dd_boxc([0.1, 0.1, 2.0]);
    translate([0.7, 0.3, 0.28]) rotate([12, 72, dd_rnd(seed + 3, 360)])
    {
        color(dd_SIGNG()) dd_boxc([3.0, 0.08, 1.3]);
        color(dd_MARKW()) translate([0, -0.05, 0]) dd_boxc([2.84, 0.01, 1.14]);
        color(dd_SIGNG()) translate([0, -0.06, 0]) dd_boxc([2.7, 0.01, 1.0]);
        color(dd_MARKW()) translate([-0.3, -0.07, 0.1]) dd_boxc([1.5, 0.01, 0.24]);
    }
    color(dd_ASPHD()) for (i = [0 : 2])
        translate([dd_rndr(seed + i * 13, -0.6, 1.4), dd_rndr(seed + i * 29 + 1, -0.8, 0.8), 0])
            dd_slab(0.3, 0.24, 0.14);
}

// 消防栓
module dd_prop_hydrant()
{
    color(dd_METALD()) cylinder(h = 0.08, r = 0.19, $fn = 7);
    color(dd_REDC())
    {
        cylinder(h = 0.62, r = 0.14, $fn = 7);
        translate([0, 0, 0.62]) sphere(r = 0.14, $fn = 7);
        for (sy = [-1, 1]) translate([0, sy * 0.1, 0.42]) rotate([sy * -90, 0, 0]) cylinder(h = 0.12, r = 0.07, $fn = 6);
    }
    color([0.85, 0.80, 0.55]) translate([0, 0, 0.74]) cylinder(h = 0.06, r = 0.05, $fn = 6);
}

// 信箱（front=-y，小红旗）
module dd_prop_mailbox()
{
    color(dd_WOODD()) translate([0, 0, 0.55]) dd_boxc([0.1, 0.1, 1.1]);
    color(dd_METALC()) translate([0, 0, 1.24]) dd_boxc([0.36, 0.56, 0.3]);
    color(dd_DARKC()) translate([0, -0.285, 1.24]) dd_boxc([0.3, 0.02, 0.24]);
    color(dd_REDC()) translate([0.2, 0.12, 1.42]) dd_boxc([0.04, 0.18, 0.06]);
}

// 镀锌垃圾桶
module dd_prop_trash(seed = 0)
{
    color(dd_METALC()) cylinder(h = 0.78, r1 = 0.26, r2 = 0.3, $fn = 8);
    if (dd_rnd(seed, 3) != 0)
        color(dd_METALD()) translate([0, 0, 0.78]) cylinder(h = 0.07, r = 0.32, $fn = 8);
    if (dd_rnd(seed, 3) == 0)
        color(dd_METALD()) translate([0.5, 0.15, 0.03]) rotate([0, 12, 30]) cylinder(h = 0.07, r = 0.32, $fn = 8);
}

// 油桶（seed 选色：锈/蓝/绿/灰）
module dd_prop_barrel(seed = 0)
{
    c = dd_drum_c(seed);
    color(c) cylinder(h = 0.86, r = 0.3, $fn = 8);
    color([c[0] * 0.75, c[1] * 0.75, c[2] * 0.75]) for (z = [0.26, 0.58])
        translate([0, 0, z]) cylinder(h = 0.05, r = 0.315, $fn = 8);
}

// 蓝色板条箱
module dd_prop_crate(s = 1.0)
{
    scale([s, s, s])
    {
        color(dd_BLUEC()) translate([0, 0, 0.4]) dd_boxc([0.85, 0.85, 0.8]);
        color([0.22, 0.34, 0.48])
        {
            for (sz = [0.06, 0.74]) translate([0, 0, sz]) dd_boxc([0.89, 0.89, 0.1]);
            translate([0, -0.43, 0.4]) rotate([0, 45, 0]) dd_boxc([0.92, 0.03, 0.12]);
            translate([0, -0.43, 0.4]) rotate([0, -45, 0]) dd_boxc([0.92, 0.03, 0.12]);
        }
    }
}

// 蓝色大垃圾箱（dumpster，front=-y）
module dd_prop_dumpster()
{
    color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 0.9, 0, 0.09]) dd_boxc([0.3, 1.1, 0.18]);
    color(dd_BLUEC()) translate([0, 0, 0.72]) dd_boxc([2.2, 1.2, 1.1]);
    color([0.24, 0.36, 0.50])
    {
        translate([-0.56, -0.03, 1.32]) dd_boxc([1.06, 1.26, 0.1]);
        translate([0.56, 0.08, 1.42]) rotate([12, 0, 0]) dd_boxc([1.06, 1.26, 0.1]);   // 半开盖
    }
    color(dd_DARKC()) translate([0.56, -0.55, 1.22]) dd_boxc([0.9, 0.16, 0.24]);
}

// 交通锥
module dd_prop_cone()
{
    color(dd_CONEO()) translate([0, 0, 0.02]) dd_boxc([0.34, 0.34, 0.05]);
    color(dd_CONEO()) cylinder(h = 0.55, r1 = 0.15, r2 = 0.04, $fn = 7);
    color(dd_MARKW()) translate([0, 0, 0.26]) cylinder(h = 0.1, r1 = 0.115, r2 = 0.095, $fn = 7);
}

// 公园长椅（front=-y）
module dd_prop_bench()
{
    color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 0.7, 0, 0.22]) dd_boxc([0.08, 0.5, 0.44]);
    color(dd_WOODD()) for (y = [-0.16, 0, 0.16]) translate([0, y, 0.46]) dd_boxc([1.7, 0.13, 0.05]);
    color(dd_WOODD()) for (z = [0.66, 0.82]) translate([0, 0.26, z]) dd_boxc([1.7, 0.05, 0.12]);
}

// 警示路障（橙白条纹横杆 + A 字腿）
module dd_prop_barricade()
{
    for (sx = [-1, 1])
        color(dd_METALD()) translate([sx * 1.0, 0, 0])
        {
            rotate([18, 0, 0]) translate([0, 0, 0.45]) dd_boxc([0.08, 0.06, 0.9]);
            rotate([-18, 0, 0]) translate([0, 0, 0.45]) dd_boxc([0.08, 0.06, 0.9]);
        }
    color(dd_CONEO()) translate([0, 0, 0.78]) dd_boxc([2.4, 0.1, 0.3]);
    color(dd_MARKW()) for (i = [0 : 2])
        translate([-0.75 + i * 0.75, -0.055, 0.78]) dd_boxc([0.28, 0.01, 0.3]);
}

// ================= 载具（车头朝 +x，底面 z=0） =================

module dd_veh_wheel(r = 0.34, w = 0.26)
{
    color([0.10, 0.10, 0.10]) translate([0, w / 2, 0]) rotate([90, 0, 0]) cylinder(h = w, r = r, $fn = 7);
    color(dd_METALC()) translate([0, (w + 0.04) / 2, 0]) rotate([90, 0, 0]) cylinder(h = w + 0.04, r = r * 0.45, $fn = 7);
}

// 家用轿车（seed 选色）
module dd_veh_sedan(seed = 0)
{
    c = dd_car_c(seed);
    color(c) translate([0, 0, 0.62]) dd_boxc([4.0, 1.8, 0.62]);
    color(c) translate([-0.25, 0, 1.2]) dd_boxc([2.1, 1.7, 0.56]);
    color(dd_GLASSC()) translate([-0.25, 0, 1.2]) dd_boxc([1.82, 1.78, 0.4]);
    color(dd_MARKW()) for (sy = [-1, 1]) translate([2.01, sy * 0.6, 0.7]) dd_boxc([0.05, 0.3, 0.16]);
    color([0.72, 0.14, 0.11]) for (sy = [-1, 1]) translate([-2.01, sy * 0.6, 0.7]) dd_boxc([0.05, 0.3, 0.16]);
    color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 2.0, 0, 0.42]) dd_boxc([0.07, 1.55, 0.2]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.25 * sx, 0.92 * sy, 0.34]) dd_veh_wheel();
}

// 厢式送货车
module dd_veh_van(seed = 0)
{
    c = dd_car_c(seed + 13);
    color(c) translate([-0.35, 0, 1.15]) dd_boxc([3.9, 1.9, 1.6]);
    color(c) translate([2.0, 0, 0.85]) dd_boxc([0.9, 1.85, 1.0]);
    color(dd_GLASSC()) translate([1.62, 0, 1.55]) dd_boxc([0.5, 1.8, 0.55]);
    color(dd_MARKW()) translate([-0.35, 0, 1.15]) dd_boxc([3.0, 1.94, 0.5]);   // 侧身涂装带
    color(dd_METALD()) translate([-2.32, 0, 1.1]) dd_boxc([0.06, 1.7, 1.35]);
    color(dd_MARKW()) for (sy = [-1, 1]) translate([2.46, sy * 0.62, 0.72]) dd_boxc([0.05, 0.3, 0.16]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.35 * sx, 0.95 * sy, 0.38]) dd_veh_wheel(0.38, 0.28);
}

// 皮卡（开放货斗）
module dd_veh_pickup(seed = 0)
{
    c = dd_car_c(seed + 29);
    color(c) translate([0, 0, 0.66]) dd_boxc([4.3, 1.85, 0.7]);
    color(c) translate([0.65, 0, 1.32]) dd_boxc([1.5, 1.75, 0.62]);
    color(dd_GLASSC()) translate([0.65, 0, 1.32]) dd_boxc([1.25, 1.83, 0.44]);
    color([c[0] * 0.7, c[1] * 0.7, c[2] * 0.7]) translate([-1.15, 0, 1.06]) dd_boxc([1.9, 1.7, 0.1]);   // 斗底
    color(c) for (sy = [-1, 1]) translate([-1.15, sy * 0.86, 1.2]) dd_boxc([1.95, 0.1, 0.34]);
    color(c) translate([-2.1, 0, 1.2]) dd_boxc([0.1, 1.8, 0.34]);
    color(dd_MARKW()) for (sy = [-1, 1]) translate([2.16, sy * 0.62, 0.76]) dd_boxc([0.05, 0.3, 0.16]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.4 * sx, 0.95 * sy, 0.4]) dd_veh_wheel(0.4, 0.28);
}

// 烧毁的残骸车：褪色车体 + 掀开的引擎盖 + 锈斑 + 缺一只轮
module dd_veh_wreck(seed = 0)
{
    c = [0.30, 0.29, 0.28];
    rotate([0, 0, dd_rnd(seed, 7) - 3]) translate([0, 0, -0.06])
    {
        color(c) translate([0, 0, 0.6]) dd_boxc([4.0, 1.8, 0.6]);
        color([0.24, 0.23, 0.22]) translate([-0.25, 0, 1.16]) dd_boxc([2.1, 1.7, 0.54]);
        color(dd_DARKC()) translate([-0.25, 0, 1.18]) dd_boxc([1.8, 1.76, 0.36]);
        color([0.22, 0.21, 0.20]) translate([1.5, 0, 1.15]) rotate([0, -38, 0]) dd_boxc([1.1, 1.5, 0.07]);   // 掀盖
        color(dd_DARKC()) translate([1.3, 0, 0.92]) dd_boxc([0.9, 1.2, 0.2]);   // 烧空机舱
        color(dd_RUSTC())
        {
            translate([0.9, 0.91, 0.62]) dd_boxc([0.8, 0.02, 0.3]);
            translate([-1.4, -0.91, 0.55]) dd_boxc([1.0, 0.02, 0.34]);
            translate([-2.0, 0.3, 0.6]) dd_boxc([0.02, 0.7, 0.3]);
        }
        for (sx = [-1, 1]) translate([1.25 * sx, 0.92, 0.34]) dd_veh_wheel();
        translate([1.25, -0.92, 0.34]) dd_veh_wheel();
        color(dd_METALD()) translate([-1.25, -0.92, 0.3]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.16, $fn = 7);
    }
    color(dd_ASPHD()) translate([1.9, 0.3, 0]) dd_slab(1.3, 1.0, 0.02);   // 油渍
}

// 翻覆的轿车：座舱着地压扁、底盘朝天、轮子冲天、甩出一扇车门
module dd_veh_flipped(seed = 0)
{
    c0 = dd_car_c(seed + 41);
    c = [c0[0] * 0.82, c0[1] * 0.82, c0[2] * 0.82];
    rotate([0, 0, dd_rnd(seed, 30) - 15])
    {
        color([0.22, 0.21, 0.20]) translate([-0.25, 0, 0.26]) dd_boxc([2.1, 1.7, 0.52]);
        color(dd_GLASSC()) translate([-0.25, 0, 0.28]) dd_boxc([1.82, 1.78, 0.32]);
        color(c) translate([0, 0, 0.83]) dd_boxc([4.0, 1.8, 0.62]);
        color([0.13, 0.13, 0.13]) translate([0, 0, 1.18]) dd_boxc([3.6, 1.5, 0.14]);   // 底盘朝天
        color(dd_METALD()) translate([-0.7, 0, 1.26]) dd_boxc([1.6, 0.4, 0.14]);       // 排气/传动轴
        translate([1.25, 0.92, 1.3]) dd_veh_wheel();
        translate([-1.25, 0.92, 1.3]) dd_veh_wheel();
        translate([-1.25, -0.92, 1.3]) dd_veh_wheel();
        color(dd_METALD()) translate([1.25, -0.92, 1.28]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.16, $fn = 7);
        // 甩出的车门 + 玻璃碎片
        translate([2.7, -1.5, 0]) rotate([0, 0, dd_rnd(seed + 7, 70)])
        {
            color(c) dd_slab(1.1, 0.8, 0.08);
            color(dd_GLASSC()) translate([0.1, 0.1, 0.08]) dd_slab(0.7, 0.4, 0.02);
        }
        for (i = [0 : 2])
            color(dd_GLASSC())
                translate([dd_rndr(seed + i * 13, -2.4, -1.2), dd_rndr(seed + i * 19 + 2, 1.1, 1.9), 0])
                    dd_slab(0.22, 0.16, 0.03);
    }
    color(dd_ASPHD()) translate([-1.6, -0.4, 0]) dd_slab(1.2, 0.9, 0.02);   // 油渍
}

// 绿色联合收割机：主机体 + 驾驶室 + 顶部粮斗 + 前割台拨禾轮 + 卸粮筒（车头 +x）
module dd_veh_harvester(seed = 0)
{
    g = [0.16, 0.36, 0.16];
    gd = [0.12, 0.28, 0.12];
    color(g) translate([-0.4, 0, 1.7]) dd_boxc([3.8, 2.1, 1.5]);
    color(gd) translate([-2.1, 0, 1.85]) dd_boxc([0.7, 1.9, 1.15]);      // 尾部发动机罩
    color(g) translate([1.35, 0, 2.75]) dd_boxc([1.3, 1.7, 1.0]);        // 驾驶室
    color(dd_GLASSC()) translate([1.42, 0, 2.75]) dd_boxc([1.25, 1.76, 0.8]);
    // 顶部粮斗（外扩四壁）
    color(dd_METALC())
    {
        for (sy = [-1, 1]) translate([-0.5, sy * 0.78, 2.8]) rotate([sy * -12, 0, 0]) dd_boxc([1.8, 0.1, 0.9]);
        for (sx = [-1, 1]) translate([-0.5 + sx * 0.85, 0, 2.8]) rotate([0, sx * 12, 0]) dd_boxc([0.1, 1.7, 0.9]);
    }
    color(gd) translate([-1.2, 0.9, 2.7]) rotate([-75, 0, 10]) cylinder(h = 2.2, r = 0.16, $fn = 6);   // 卸粮筒
    // 前割台 + 拨禾轮 + 分禾齿
    color(dd_METALC()) translate([2.6, 0, 0.55]) dd_boxc([1.1, 3.6, 0.65]);
    color(dd_METALD()) translate([2.7, 0, 1.05]) rotate([90, 0, 0]) cylinder(h = 3.4, r = 0.28, center = true, $fn = 7);
    color([0.60, 0.54, 0.24]) for (i = [0 : 8]) translate([3.18, -1.6 + i * 0.4, 0.35]) dd_boxc([0.24, 0.12, 0.22]);
    color(gd) translate([1.9, 0, 1.15]) rotate([0, 25, 0]) dd_boxc([1.3, 1.1, 0.5]);   // 进料喉
    for (sy = [-1, 1]) translate([0.9, sy * 1.0, 0.62]) dd_veh_wheel(r = 0.62, w = 0.4);
    for (sy = [-1, 1]) translate([-1.7, sy * 0.95, 0.4]) dd_veh_wheel(r = 0.4, w = 0.3);
}

// ================= 地面细节（打散大片平地；底面 z=0） =================

// 裸土/泥地斑块：多边形叠片近似有机轮廓，带深色湿泥斑。
// 沙漠地图可覆盖 c1/c2 为沙色（如 [0.52,0.36,0.19] / [0.42,0.28,0.14]）。
module dd_ground_dirt(L = 10, D = 8, seed = 0, c1 = [0.40, 0.30, 0.21], c2 = [0.30, 0.22, 0.15])
{
    color(c1)
    {
        rotate([0, 0, dd_rnd(seed, 180)]) scale([L * 0.5, D * 0.5, 1]) cylinder(h = 0.08, r = 1, $fn = 9);
        translate([L * 0.2, D * 0.14, 0]) rotate([0, 0, dd_rnd(seed + 3, 180)])
            scale([L * 0.3, D * 0.26, 1]) cylinder(h = 0.08, r = 1, $fn = 8);
        translate([-L * 0.22, -D * 0.15, 0]) rotate([0, 0, dd_rnd(seed + 5, 180)])
            scale([L * 0.26, D * 0.3, 1]) cylinder(h = 0.08, r = 1, $fn = 8);
    }
    for (i = [0 : 2])
        color(c2)
            translate([dd_rndr(seed * 7 + i * 31, -L * 0.28, L * 0.28),
                       dd_rndr(seed * 11 + i * 17 + 3, -D * 0.28, D * 0.28), 0.08])
                scale([dd_rndr(seed + i, L * 0.08, L * 0.18), dd_rndr(seed + i + 5, D * 0.08, D * 0.18), 1])
                    cylinder(h = 0.012, r = 1, $fn = 8);
}

// 泥水洼：深泥缘 + 暗水面（雨后农家院/土路点缀）
module dd_ground_puddle(s = 1.0, seed = 0)
{
    color([0.28, 0.21, 0.14]) rotate([0, 0, dd_rnd(seed, 180)]) scale([1.3 * s, 0.95 * s, 1]) cylinder(h = 0.05, r = 1, $fn = 9);
    color([0.22, 0.29, 0.33]) translate([0.06 * s, 0, 0]) rotate([0, 0, dd_rnd(seed, 180)])
        scale([1.02 * s, 0.7 * s, 1]) cylinder(h = 0.065, r = 1, $fn = 9);
    color([0.22, 0.29, 0.33]) translate([1.05 * s, 0.4 * s, 0]) scale([0.35 * s, 0.25 * s, 1]) cylinder(h = 0.06, r = 1, $fn = 7);
}

// ================= 农场（底面 z=0） =================

// 垄沟菜田：起垄 + 成排低矮作物丛（seed 决定缺株与枯行）
module dd_nature_field_rows(L = 12, D = 9, seed = 0)
{
    color(dd_SOILC()) dd_slab(L, D, 0.14);
    nr = max(2, floor(D / 1.3));
    for (r = [0 : nr - 1])
    {
        py = -D / 2 + 0.8 + r * (D - 1.6) / (nr - 1);
        color([0.33, 0.25, 0.17]) translate([0, py, 0.14]) dd_slab(L - 0.8, 0.55, 0.14);
        nc = floor((L - 1.2) / 0.85);
        for (c = [0 : nc - 1])
            if (dd_rnd(seed + r * 31 + c * 7, 5) != 0)
                color(dd_rnd(seed + r * 17, 4) == 0 ? [0.55, 0.46, 0.22]
                      : (dd_rnd(seed + r * 13 + c * 3, 2) == 0 ? dd_LEAFD() : [0.24, 0.40, 0.20]))
                    translate([-L / 2 + 0.7 + c * 0.85, py, 0.42]) sphere(r = 0.26, $fn = 6);
    }
}

// 南瓜田：田土 + 藤蔓 + 橙色南瓜（seed 决定缺位与大小）
module dd_nature_pumpkin_patch(L = 10, D = 8, seed = 0)
{
    color(dd_SOILC()) dd_slab(L, D, 0.14);
    for (i = [0 : 3])
        color([0.20, 0.30, 0.14])
            translate([dd_rndr(seed * 7 + i * 13, -(L - 2.4) / 2, (L - 2.4) / 2),
                       dd_rndr(seed * 11 + i * 29 + 3, -(D - 2.4) / 2, (D - 2.4) / 2), 0.14])
                rotate([0, 0, dd_rnd(seed + i, 180)]) dd_slab(dd_rndr(seed + i + 5, 1.4, 2.6), 0.5, 0.03);
    nr = max(2, floor(D / 1.5));
    nc = max(2, floor(L / 1.5));
    for (r = [0 : nr - 1], c = [0 : nc - 1])
        if (dd_rnd(seed + r * 37 + c * 11, 4) != 0)
        {
            ps = dd_rndr(seed + r * 19 + c * 5, 0.24, 0.40);
            translate([-L / 2 + 0.9 + c * (L - 1.8) / (nc - 1) + dd_rndr(seed + r * 7 + c * 23, -0.25, 0.25),
                       -D / 2 + 0.9 + r * (D - 1.8) / (nr - 1) + dd_rndr(seed + r * 3 + c * 31 + 9, -0.25, 0.25),
                       0.14 + ps * 0.35])
            {
                color([0.62, 0.28, 0.07]) scale([1, 1, 0.72]) sphere(r = ps, $fn = 7);
                color([0.30, 0.36, 0.16]) translate([0, 0, ps * 0.66]) cylinder(h = 0.14, r = 0.045, $fn = 5);
            }
        }
}

// 树桩（砍伐后的林缘细节）
module dd_nature_stump(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(dd_TRUNKC()) cylinder(h = 0.55, r = 0.34, $fn = 7);
        color([0.55, 0.44, 0.28]) translate([0, 0, 0.55]) cylinder(h = 0.04, r = 0.29, $fn = 7);
        color(dd_TRUNKC()) rotate([0, 0, dd_rnd(seed, 180)]) translate([0.3, 0.12, 0]) cylinder(h = 0.2, r = 0.13, $fn = 5);
    }
}

// 倒木（沿 x 躺放，浅色断面 + 残枝）
module dd_nature_log(seed = 0)
{
    color(dd_TRUNKC()) translate([-1.2, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 2.4, r = 0.26, $fn = 7);
    color([0.55, 0.44, 0.28]) translate([1.2, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.21, $fn = 7);
    color([0.55, 0.44, 0.28]) translate([-1.24, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.21, $fn = 7);
    color(dd_TRUNKC()) translate([0.35, 0.12, 0.38]) rotate([-35, 0, 0]) cylinder(h = 0.5, r = 0.08, $fn = 5);
    if (dd_rnd(seed, 2) == 0)
        color(dd_LEAFD()) translate([-0.6, -0.15, 0.42]) sphere(r = 0.24, $fn = 6);   // 残叶/青苔
}

// ================= 农场建筑 =================

// 红色美式谷仓：阶梯山墙芯 + 折坡（gambrel）屋面板 + 白框大门带 X 撑 + 阁楼草料口。
// 正脊沿 y，front = -y（大门面向 -y）。
module dd_bldg_barn(seed = 0, L = 10, D = 12)
{
    wh = 3.0;
    bw = [0.44, 0.13, 0.10];
    bd = [0.34, 0.10, 0.08];
    rc = dd_rnd(seed, 2) == 0 ? dd_ROOFD() : [0.30, 0.30, 0.32];
    a1 = atan(1.3 / (L * 0.11));
    l1 = sqrt(1.3 * 1.3 + L * 0.11 * L * 0.11) + 0.6;
    a2 = atan(1.05 / (L * 0.39));
    l2 = sqrt(1.05 * 1.05 + L * 0.39 * L * 0.39) + 0.4;
    color(dd_PLINTH()) dd_slab(L + 0.3, D + 0.3, 0.25);
    color(bw) translate([0, 0, 0.25]) dd_slab(L, D, wh - 0.25);
    // 阶梯阁楼芯（低模 gambrel 内腔）
    color(bw) translate([0, 0, wh]) dd_slab(L * 0.78, D, 1.3);
    color(bw) translate([0, 0, wh + 1.3]) dd_slab(L * 0.42, D, 0.95);
    // 折坡屋面板（下陡上缓）+ 脊盖
    for (sx = [-1, 1])
    {
        color(rc) translate([sx * L * 0.445, 0, wh + 0.65]) rotate([0, sx * a1, 0]) dd_boxc([l1, D + 0.9, 0.14]);
        color(rc) translate([sx * L * 0.195, 0, wh + 1.83]) rotate([0, sx * a2, 0]) dd_boxc([l2, D + 0.9, 0.14]);
    }
    color(dd_ROOFD()) translate([0, 0, wh + 2.42]) dd_boxc([0.5, D * 0.9, 0.16]);
    // 前立面：白框推拉大门 + X 撑 + 阁楼草料口
    translate([0, -D / 2 - 0.05, 0])
    {
        color(dd_TRIMW()) translate([0, 0, 1.3]) dd_boxc([2.9, 0.1, 2.6]);
        color(bd) translate([0, -0.03, 1.3]) dd_boxc([2.6, 0.1, 2.4]);
        color(dd_TRIMW()) translate([0, -0.08, 1.3]) dd_boxc([0.09, 0.04, 2.4]);
        for (sl = [-1, 1], sa = [-1, 1])
            color(dd_TRIMW()) translate([sl * 0.66, -0.08, 1.3]) rotate([0, sa * 28, 0]) dd_boxc([0.09, 0.04, 2.6]);
        color(dd_TRIMW()) translate([0, 0, wh + 0.75]) dd_boxc([1.15, 0.1, 1.15]);
        color(dd_DARKC()) translate([0, -0.03, wh + 0.75]) dd_boxc([0.95, 0.1, 0.95]);
        for (sx = [-1, 1])
            color(dd_TRIMW()) translate([sx * (L / 2 - 0.1), 0.02, wh / 2 + 0.12]) dd_boxc([0.18, 0.08, wh - 0.25]);
    }
    // 侧墙小窗
    translate([L / 2 + 0.04, -D * 0.2, 1.7]) rotate([0, 0, 90]) dd_part_window(w = 0.8, h = 0.8);
    translate([-L / 2 - 0.04, D * 0.2, 1.7]) rotate([0, 0, -90]) dd_part_window(w = 0.8, h = 0.8);
}

// ================= 农场/废墟道具（底面 z=0） =================

// 方形干草捆（seed 偶尔叠放第二捆）
module dd_prop_haybale(seed = 0)
{
    hc = [0.62, 0.50, 0.20];
    hd = [0.50, 0.39, 0.15];
    rotate([0, 0, dd_rnd(seed, 30) - 15])
    {
        color(hc) translate([0, 0, 0.45]) dd_boxc([1.5, 0.95, 0.9]);
        color(hd) for (x = [-0.45, 0.15]) translate([x, 0, 0.45]) dd_boxc([0.08, 0.99, 0.94]);
        if (dd_rnd(seed, 3) == 0)
        {
            color(hc) translate([0.1, 0.05, 1.32]) rotate([0, 0, 14]) dd_boxc([1.4, 0.9, 0.85]);
            color(hd) translate([0.1, 0.05, 1.32]) rotate([0, 0, 14]) dd_boxc([0.08, 0.94, 0.89]);
        }
    }
}

// 红色油壶（补给点/残骸旁小物）
module dd_prop_gascan()
{
    color([0.55, 0.13, 0.09]) translate([0, 0, 0.24]) dd_boxc([0.34, 0.2, 0.46]);
    color([0.44, 0.10, 0.07]) translate([0, 0, 0.51]) dd_boxc([0.2, 0.16, 0.1]);
    color([0.72, 0.66, 0.36]) translate([0.13, 0, 0.5]) cylinder(h = 0.12, r = 0.045, $fn = 5);
}

// 木托盘（seed 偶尔带斜靠散板）
module dd_prop_pallet(seed = 0)
{
    pc = [0.42, 0.32, 0.20];
    pd = [0.33, 0.24, 0.15];
    rotate([0, 0, dd_rnd(seed, 40) - 20])
    {
        color(pd) for (y = [-0.5, 0, 0.5]) translate([0, y, 0.07]) dd_boxc([1.2, 0.14, 0.14]);
        color(pc) for (i = [0 : 4]) translate([-0.52 + i * 0.26, 0, 0.17]) dd_boxc([0.2, 1.2, 0.06]);
        if (dd_rnd(seed, 2) == 0)
            color(pc) translate([0.78, 0.2, 0.4]) rotate([0, 68, 15]) dd_boxc([0.18, 1.1, 0.05]);
    }
}

// 轮胎堆：2-4 只叠放 + 偶尔一只立靠
module dd_prop_tires(seed = 0)
{
    n = 2 + dd_rnd(seed, 3);
    for (i = [0 : n - 1])
        color(i % 2 == 0 ? [0.10, 0.10, 0.10] : [0.14, 0.14, 0.15])
            translate([dd_rndr(seed + i * 7, -0.07, 0.07), dd_rndr(seed + i * 13 + 3, -0.07, 0.07), 0.11 + i * 0.22])
                cylinder(h = 0.22, r = 0.36, center = true, $fn = 8);
    if (dd_rnd(seed + 5, 2) == 0)
        color([0.10, 0.10, 0.10]) translate([0.64, 0.1, 0.36]) rotate([90, 0, dd_rnd(seed, 90)])
            cylinder(h = 0.22, r = 0.36, center = true, $fn = 8);
}

// 地面杂物簇：污渍 + 纸张 + 骨头 + 空瓶（seed 决定组合），撒在空地打散平面感
module dd_prop_debris(seed = 0)
{
    if (dd_rnd(seed, 3) != 2)
        color([0.25, 0.19, 0.13]) rotate([0, 0, dd_rnd(seed, 180)])
            scale([dd_rndr(seed, 0.5, 0.9), dd_rndr(seed + 3, 0.35, 0.7), 1]) cylinder(h = 0.03, r = 1, $fn = 8);
    for (i = [0 : 1 + dd_rnd(seed, 2)])
        color([0.72, 0.70, 0.62])
            translate([dd_rndr(seed + i * 11, -0.9, 0.9), dd_rndr(seed + i * 17 + 5, -0.9, 0.9), 0.03])
                rotate([0, 0, dd_rnd(seed + i, 180)]) dd_slab(0.32, 0.24, 0.015);
    if (dd_rnd(seed + 1, 2) == 0)
        translate([dd_rndr(seed + 21, -0.6, 0.6), dd_rndr(seed + 23, -0.6, 0.6), 0])
            rotate([0, 0, dd_rnd(seed + 25, 180)])
            {
                color([0.78, 0.74, 0.62]) translate([0, 0, 0.06]) dd_boxc([0.42, 0.07, 0.07]);
                color([0.78, 0.74, 0.62]) for (sx = [-1, 1], sy = [-1, 1])
                    translate([sx * 0.21, sy * 0.045, 0.06]) sphere(r = 0.055, $fn = 5);
            }
    if (dd_rnd(seed + 2, 2) == 0)
        color([0.22, 0.38, 0.24])
            translate([dd_rndr(seed + 31, -0.7, 0.7), dd_rndr(seed + 33, -0.7, 0.7), 0.06])
                rotate([0, 90, dd_rnd(seed + 35, 180)]) cylinder(h = 0.3, r = 0.06, $fn = 6);
}

// 倒塌的风力发电机：混凝土基座 + 撕裂塔根 + 躺倒塔身 + 机舱 + 摊平的三叶转子。
// 全长约 16*s 米，横贯空旷地带的大型地标；rotate 由调用方控制倒伏方向（塔身向 +x 躺倒）。
module dd_prop_windturbine_fallen(seed = 0, s = 1.0)
{
    wtw = [0.68, 0.68, 0.66];
    wtd = [0.50, 0.50, 0.49];
    scale([s, s, s])
    {
        color(dd_PLINTH()) cylinder(h = 0.5, r = 1.1, $fn = 8);
        color(wtw) translate([0, 0, 0.5]) cylinder(h = 1.6, r1 = 0.55, r2 = 0.48, $fn = 8);
        color(dd_METALD()) translate([0, 0, 2.0]) rotate([0, 18, 0]) cylinder(h = 0.5, r = 0.42, $fn = 7);
        rotate([0, 0, dd_rnd(seed, 24) - 12])
        {
            color(wtw) translate([1.4, 0, 0.5]) rotate([0, 87, 0]) cylinder(h = 9.5, r1 = 0.46, r2 = 0.28, $fn = 8);
            color(wtd) translate([11.4, 0, 0.55]) dd_boxc([1.6, 0.9, 0.9]);
            // 转子摔在机舱前：叶盘近水平摊开，两叶尖触地
            translate([12.5, 0, 0.6]) rotate([0, 75, 0])
            {
                color(wtd) sphere(r = 0.42, $fn = 7);
                for (a = [0, 120, 240])
                    color(wtw) rotate([a, 0, 0]) translate([0, 0, 2.3]) dd_boxc([0.16, 0.55, 4.4]);
            }
            // 散落碎片
            for (i = [0 : 2])
                color(wtd)
                    translate([dd_rndr(seed + i * 17, 3, 10), dd_rndr(seed + i * 23 + 3, -2.2, 2.2), 0])
                        rotate([0, 0, dd_rnd(seed + i, 180)]) dd_slab(0.8, 0.4, 0.12);
        }
    }
}
