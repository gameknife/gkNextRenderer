// kit_deadly.scad —— Deadly Days: Roadtrip 风格美式郊区零件库（丧尸末日小镇）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "dd_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具车头朝 +x）。调用方自设 $fn（建议 12）。
// 尺度：mid（与 kit_old_city 同级；民房 ~8x6、轿车长 ~4）。
// 主题元素：木板房/门廊房/工具棚/石教堂/街角店/红谷仓、沥青路网、层叠松、白栅栏、
//           电线杆/公路绿牌（含倒塌残骸）、油桶/锥桶/路障、轿车/厢式车/皮卡/烧毁残骸/
//           翻覆车/联合收割机、泥地斑块/水洼、垄沟田/南瓜田/树桩/倒木、
//           干草捆/木托盘/轮胎堆/油壶/地面杂物簇/倒塌风机。
// v2 追加（街区规划词汇，见文件末尾分段）：停车场/碎石场/混凝土地坪/土路、
//           绿篱/铁丝网/隔离墩/沙袋/护栏、联排商业楼/厂房/加油站/汽车旅馆/餐车/
//           拖车房/废墟/水塔/筒仓、广告牌/集装箱/储罐/信号塔/帐篷/篝火、校车/半挂。

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
    // 伸缩缝 3.2 m 一道：1.6 m 时缝距接近板宽，俯视下整条人行道会读成"铁轨"
    nj = floor(L / 3.2);
    color(dd_WALKD()) for (i = [1 : nj - 1])
        translate([-L / 2 + i * 3.2, 0, 0.16]) dd_slab(0.07, W, 0.012);
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
    // A 字腿：顶端并拢、脚下叉开（X 角与 y 偏移同号）
    for (sx = [-1, 1], sy = [-1, 1])
        color(dd_METALD()) translate([sx * 1.0, sy * 0.139, 0.428]) rotate([sy * 18, 0, 0]) dd_boxc([0.08, 0.06, 0.9]);
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

// =====================================================================================
// v2 扩展：街区规划词汇
// 起因：Brotato3D 三张地图是固定 70° 俯视、单屏约 66 x 41 m 的生存肉鸽战场。旧库只有
// "房子 + 散点道具"，写出来的地图必然是"平板 + 噪点"。本段补三类词汇：
//   1) 成片地面：停车场/碎石场/混凝土地坪/土路 —— 让每块地都有用途，从根上消灭纯色平地
//   2) 线性界定件：绿篱/铁丝网/隔离墩/沙袋/护栏 —— 划出地块红线与街道节奏
//   3) 地标与叙事件：水塔/筒仓/加油站/汽车旅馆/餐车/拖车/废墟/广告牌/货柜/储罐/信号塔/
//      帐篷/篝火/校车/半挂 —— 俯视下提供轮廓、长阴影与方位感
// 契约同上：落地件底面 z=0，带朝向件 front = -y（正对游戏相机），载具车头 +x。
// =====================================================================================

// ---- v2 配色补充 ----
function dd_CONCC()  = [0.47, 0.46, 0.43];   // 浇筑混凝土
function dd_CONCD()  = [0.41, 0.40, 0.38];   // 混凝土缝/深斑
function dd_GRAVC()  = [0.40, 0.38, 0.35];   // 碎石场
function dd_BRICKC() = [0.45, 0.24, 0.18];   // 红砖
function dd_HEDGEC() = [0.21, 0.33, 0.16];   // 绿篱深
function dd_HEDGEL() = [0.26, 0.38, 0.19];   // 绿篱浅
function dd_SANDBC() = [0.46, 0.41, 0.27];   // 沙袋
function dd_BUSY()   = [0.72, 0.55, 0.10];   // 校车黄

// 商业招牌/雨棚彩色（低饱和，PT 下不过曝）
function dd_sign_c(i) = [[0.58, 0.17, 0.12], [0.66, 0.48, 0.12], [0.19, 0.36, 0.52],
                         [0.20, 0.42, 0.26], [0.40, 0.31, 0.22]][dd_rnd(i, 5)];
function dd_blockwall_c(i) = [dd_BRICKC(), [0.50, 0.44, 0.35], [0.44, 0.42, 0.38],
                              [0.52, 0.35, 0.25], [0.38, 0.35, 0.31]][dd_rnd(i, 5)];

// ---- 地面叠层高度（必读的放置契约） ----
// 所有 dd_ground_* 都是底面 z=0 的薄板，厚度 0.08~0.16 且表面还有 1~2 cm 的细节片。
// 两块地面件平面重叠时，下层的细节会从上层表面钻出几毫米：那些面被包在上层实体里收不到光，
// 在 PT 下渲染成黑斑（沙漠图的"黑洞"就是这么来的）。叠放时把上层抬到 dd_layer(n)，
// 20 cm 的台阶在 70° 俯视下看不出来，却能彻底避免这个 artifact。
function dd_layer(n = 1) = n * 0.2;

// ================= v2 地面（成片铺装，底面 z=0） =================

// 沥青停车场：底板 + 破损补丁 + bands 排车位白线（车位沿 y 停放）。
// 街区里最有用的一块地：给空地一个用途，同时是俯视下可读的大面积节奏块。
module dd_ground_lot(L = 30, D = 20, seed = 0, bands = 2, stall = 2.8)
{
    color(dd_ASPHC()) dd_slab(L, D, 0.12);
    for (i = [0 : 3])
        color(dd_ASPHD())
            translate([dd_rndr(seed * 7 + i * 31, -(L - 4) / 2, (L - 4) / 2),
                       dd_rndr(seed * 13 + i * 17 + 5, -(D - 4) / 2, (D - 4) / 2), 0.12])
                dd_slab(dd_rndr(seed + i, 1.8, 4.4), dd_rndr(seed + i + 9, 1.2, 3.0), 0.014);
    sd = min(5.0, D / max(1, bands) - 0.8);
    n = max(1, floor((L - 1.2) / stall));
    for (b = [0 : bands - 1])
    {
        by = bands == 1 ? 0 : -D / 2 + sd / 2 + 0.6 + b * (D - sd - 1.2) / (bands - 1);
        color(dd_MARKW())
        {
            for (i = [0 : n])
                translate([-n * stall / 2 + i * stall, by, 0.12]) dd_slab(0.12, sd, 0.02);
            for (sy = [-1, 1])
                translate([0, by + sy * sd / 2, 0.12]) dd_slab(n * stall, 0.11, 0.02);
        }
    }
}

// 碎石/矿渣场地：浅灰底 + 深色湿斑 + 零星石块（工业院子、货场、车场地面）。
// c1/c2 可覆盖为沙色，沙漠场景里默认灰会在强日光下发白，读起来像雪地。
module dd_ground_gravel(L = 20, D = 14, seed = 0, c1 = [0.40, 0.38, 0.35], c2 = [0.36, 0.34, 0.31])
{
    color(c1) dd_slab(L, D, 0.10);
    for (i = [0 : 5])
        color(c2)
            translate([dd_rndr(seed * 7 + i * 23, -(L - 3) / 2, (L - 3) / 2),
                       dd_rndr(seed * 13 + i * 41 + 5, -(D - 3) / 2, (D - 3) / 2), 0.10])
                rotate([0, 0, dd_rnd(seed + i, 180)])
                    scale([dd_rndr(seed + i, 1.0, 2.8), dd_rndr(seed + i + 7, 0.8, 2.0), 1])
                        cylinder(h = 0.014, r = 1, $fn = 7);
    for (i = [0 : 6])
        color([c1[0] * 1.3, c1[1] * 1.3, c1[2] * 1.3])
            translate([dd_rndr(seed * 11 + i * 37, -(L - 2) / 2, (L - 2) / 2),
                       dd_rndr(seed * 17 + i * 29 + 3, -(D - 2) / 2, (D - 2) / 2), 0.10])
                rotate([0, 0, dd_rnd(seed + i * 3, 180)]) dd_boxc([0.34, 0.28, 0.14]);
}

// 混凝土地坪：分缝网格 + 裂纹/油污（厂房前场、废弃基础、检查站）
module dd_ground_concrete(L = 20, D = 14, seed = 0)
{
    color(dd_CONCC()) dd_slab(L, D, 0.12);
    nx = max(1, floor(L / 5));
    ny = max(1, floor(D / 5));
    color(dd_CONCD())
    {
        if (nx > 1) for (i = [1 : nx - 1]) translate([-L / 2 + i * L / nx, 0, 0.12]) dd_slab(0.09, D, 0.014);
        if (ny > 1) for (i = [1 : ny - 1]) translate([0, -D / 2 + i * D / ny, 0.12]) dd_slab(L, 0.09, 0.014);
    }
    for (i = [0 : 3])
        color([0.32, 0.31, 0.29])
            translate([dd_rndr(seed * 7 + i * 19, -(L - 3) / 2, (L - 3) / 2),
                       dd_rndr(seed * 23 + i * 31 + 7, -(D - 3) / 2, (D - 3) / 2), 0.12])
                rotate([0, 0, dd_rnd(seed + i * 5, 180)])
                    dd_slab(dd_rndr(seed + i, 1.6, 4.5), 0.16, 0.014);
}

// 土路/车辙（沿 x）：压实土带 + 两道车辙 + 中间草脊。用来把散落的据点连成路网。
module dd_ground_track(L = 30, W = 4, seed = 0)
{
    color([0.38, 0.31, 0.22]) dd_slab(L, W, 0.09);
    color([0.29, 0.23, 0.16]) for (sy = [-1, 1])
        translate([0, sy * W * 0.24, 0.09]) dd_slab(L, W * 0.26, 0.014);
    for (i = [0 : 2])
        color(dd_GRASSD())
            translate([dd_rndr(seed * 7 + i * 29, -(L - 4) / 2, (L - 4) / 2), 0, 0.09])
                dd_slab(dd_rndr(seed + i, 1.5, 4.5), W * 0.13, 0.02);
}

// ================= v2 线性界定件（沿 x 通长，底面 z=0） =================

// 修剪绿篱：分段起伏，住宅红线/公园边界
module dd_prop_hedge(len = 6, h = 1.2, seed = 0)
{
    n = max(1, floor(len / 1.3));
    for (i = [0 : n - 1])
    {
        hh = h * dd_rndr(seed + i * 13, 0.86, 1.10);
        color(dd_rnd(seed + i * 7, 2) == 0 ? dd_HEDGEC() : dd_HEDGEL())
            translate([-len / 2 + (i + 0.5) * len / n, dd_rndr(seed + i * 11, -0.09, 0.09), hh / 2])
                dd_boxc([len / n + 0.12, 0.9, hh]);
    }
}

// 铁丝网（半透明网面 + 镀锌立柱，seed 偶尔给一段塌下来的破口）
module dd_prop_chainlink(len = 8, h = 2.2, seed = 0)
{
    np = max(2, floor(len / 2.6) + 1);
    color(dd_METALC())
    {
        for (i = [0 : np - 1])
            translate([-len / 2 + i * len / (np - 1), 0, h / 2]) cylinder(h = h, r = 0.05, center = true, $fn = 5);
        translate([0, 0, h - 0.07]) dd_boxc([len, 0.05, 0.06]);
        translate([0, 0, 0.14]) dd_boxc([len, 0.04, 0.05]);
    }
    color([0.62, 0.64, 0.62, 0.45]) translate([0, 0, h / 2]) dd_boxc([len, 0.02, h - 0.2]);
    if (dd_rnd(seed, 3) == 0)
        color([0.62, 0.64, 0.62, 0.45])
            translate([dd_rndr(seed + 3, -len * 0.28, len * 0.28), 0.55, 0.07])
                rotate([74, 0, dd_rnd(seed + 5, 24) - 12]) dd_boxc([len * 0.22, 0.02, h * 0.85]);
}

// 混凝土隔离墩（新泽西护栏，检查站/封锁线）
module dd_prop_jersey(len = 3.0, seed = 0)
{
    color(dd_CONCC())
    {
        translate([0, 0, 0.12]) dd_boxc([len, 0.62, 0.24]);
        translate([0, 0, 0.42]) dd_boxc([len, 0.42, 0.36]);
        translate([0, 0, 0.72]) dd_boxc([len, 0.28, 0.24]);
    }
    color(dd_CONCD()) translate([0, 0, 0.845]) dd_boxc([len, 0.3, 0.05]);
    if (dd_rnd(seed, 3) == 0)
        color(dd_CONEO()) translate([dd_rndr(seed + 7, -len * 0.3, len * 0.3), -0.15, 0.5]) dd_boxc([0.5, 0.02, 0.3]);
}

// 沙袋墙：错缝叠放（军队检查站/临时工事）
module dd_prop_sandbags(len = 3.0, h = 0.9, seed = 0)
{
    rows = max(1, floor(h / 0.22));
    n = max(2, floor(len / 0.62));
    for (r = [0 : rows - 1], i = [0 : n - 1])
    {
        px = -len / 2 + 0.32 + i * (len - 0.64) / max(1, n - 1) + (r % 2) * 0.16;
        color(dd_rnd(seed + r * 31 + i * 7, 2) == 0 ? dd_SANDBC() : [0.39, 0.34, 0.22])
            translate([px, dd_rndr(seed + r * 13 + i * 5, -0.06, 0.06), 0.11 + r * 0.21])
                rotate([0, 0, dd_rndr(seed + r * 3 + i * 11, -9, 9)])
                    dd_boxc([0.62, 0.44, 0.22]);
    }
}

// 公路波形护栏（沿 x，板面朝 -y）
module dd_prop_guardrail(len = 8, seed = 0)
{
    np = max(2, floor(len / 4) + 1);
    color(dd_METALD()) for (i = [0 : np - 1])
        translate([-len / 2 + i * len / (np - 1), 0.05, 0.35]) dd_boxc([0.12, 0.14, 0.7]);
    color(dd_METALC())
    {
        translate([0, -0.08, 0.62]) dd_boxc([len, 0.07, 0.34]);
        translate([0, -0.14, 0.62]) dd_boxc([len, 0.05, 0.11]);
    }
    if (dd_rnd(seed, 4) == 0)   // 撞断的一段
        color(dd_METALC()) translate([len * 0.3, -0.5, 0.3]) rotate([0, 0, 24]) dd_boxc([len * 0.25, 0.07, 0.34]);
}

// ================= v2 建筑（front = -y） =================

// 镇中心联排商业楼（1–3 层，平顶）：底层橱窗 + 招牌带 + 雨棚 + 上层窗格 + 女儿墙 +
// 屋面机组/楼梯间/通风管。俯视下屋面就是它的正脸，所以屋顶必须有东西。
module dd_bldg_block(seed = 0, L = 14, D = 10, floors = 2)
{
    fh = 3.4;
    h = floors * fh;
    wc = dd_blockwall_c(seed);
    wd = [wc[0] * 0.84, wc[1] * 0.84, wc[2] * 0.84];
    ac = dd_sign_c(seed + 2);
    color(wc) dd_slab(L, D, h);
    color([0.25, 0.25, 0.26]) translate([0, 0, h]) dd_slab(L - 0.3, D - 0.3, 0.07);   // 屋面卷材
    color(wd)                                                                        // 女儿墙
    {
        for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.14), h + 0.32]) dd_boxc([L + 0.16, 0.28, 0.64]);
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.14), 0, h + 0.32]) dd_boxc([0.28, D + 0.16, 0.64]);
    }
    color(dd_CONCD())
    {
        for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.14), h + 0.68]) dd_boxc([L + 0.3, 0.4, 0.1]);
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.14), 0, h + 0.68]) dd_boxc([0.4, D + 0.3, 0.1]);
    }
    // 底层橱窗 + 入口 + 招牌 + 雨棚
    color(dd_DARKC()) translate([0, -D / 2 - 0.05, 1.65]) dd_boxc([L - 2.6, 0.12, 2.1]);
    color(dd_TRIMW()) for (i = [0 : 3])
        translate([-(L - 2.6) / 2 + i * (L - 2.6) / 3, -D / 2 - 0.11, 1.65]) dd_boxc([0.1, 0.1, 2.1]);
    color([0.24, 0.24, 0.26]) translate([L / 2 - 1.5, -D / 2 - 0.1, 1.15]) dd_boxc([1.2, 0.1, 2.3]);
    color(ac) translate([0, -D / 2 - 0.15, 3.05]) dd_boxc([L - 0.6, 0.26, 0.72]);
    color(dd_TRIMW()) translate([-L * 0.1, -D / 2 - 0.3, 3.05]) dd_boxc([L * 0.42, 0.04, 0.34]);
    color([ac[0] * 0.82, ac[1] * 0.82, ac[2] * 0.82])
        translate([-0.6, -D / 2 - 0.75, 2.5]) rotate([13, 0, 0]) dd_boxc([L - 3.2, 1.6, 0.08]);
    // 上层窗
    if (floors > 1)
    {
        nw = max(2, floor(L / 3.4));
        for (f = [1 : floors - 1], i = [0 : nw - 1])
            translate([-L / 2 + (i + 0.5) * L / nw, -D / 2 - 0.05, f * fh + 1.6]) dd_part_window(1.05, 1.5);
        nsw = max(1, floor(D / 4.0));
        for (f = [1 : floors - 1], i = [0 : nsw - 1], sx = [-1, 1])
            translate([sx * (L / 2 + 0.05), -D / 2 + (i + 0.5) * D / nsw, f * fh + 1.6])
                rotate([0, 0, sx * 90]) dd_part_window(1.0, 1.4);
    }
    // 屋面设备（俯视焦点）
    color(dd_METALC()) translate([L * 0.24, D * 0.2, h + 0.55]) dd_boxc([2.1, 1.5, 0.9]);
    color(dd_METALD()) translate([L * 0.24, D * 0.2, h + 1.04]) cylinder(h = 0.14, r = 0.52, $fn = 8);
    color(wd) translate([-L * 0.26, D * 0.12, h + 1.05]) dd_boxc([2.4, 2.1, 2.0]);
    color(dd_ROOFD()) translate([-L * 0.26, D * 0.12, h + 2.11]) dd_boxc([2.7, 2.4, 0.14]);
    color(dd_METALC()) for (i = [0 : 1])
        translate([-L * 0.04 + i * 1.1, -D * 0.26, h + 0.45]) cylinder(h = 0.8, r = 0.17, $fn = 6);
    if (dd_rnd(seed + 7, 2) == 0)
    {
        color(dd_WOODD()) translate([L * 0.06, -D * 0.3, h + 0.9]) cylinder(h = 1.5, r = 0.75, $fn = 8);
        color(dd_METALD()) translate([L * 0.06, -D * 0.3, h + 0.2]) dd_boxc([1.4, 1.4, 0.16]);
    }
}

// 工业厂房/仓库：波纹金属墙 + 缓坡顶带亮色天窗带 + 两樘卷帘门 + 装卸平台 + 屋顶通风球
module dd_bldg_warehouse(seed = 0, L = 26, D = 16)
{
    wh = 6.4;
    wc = [[0.46, 0.47, 0.45], [0.42, 0.44, 0.46], [0.48, 0.44, 0.38]][dd_rnd(seed, 3)];
    wd = [wc[0] * 0.82, wc[1] * 0.82, wc[2] * 0.82];
    color(dd_CONCD()) dd_slab(L + 0.8, D + 0.8, 0.3);
    color(wc) translate([0, 0, 0.3]) dd_slab(L, D, wh - 0.3);
    nr = max(2, floor(L / 2.4));
    color(wd) for (i = [0 : nr - 1])
        translate([-L / 2 + (i + 0.5) * L / nr, -D / 2 - 0.04, wh / 2 + 0.3]) dd_boxc([0.15, 0.1, wh - 0.6]);
    // 缓坡双坡顶（脊沿 x）+ 天窗带
    ra = atan(1.6 / (D / 2));
    rl = sqrt(1.6 * 1.6 + (D / 2) * (D / 2)) + 0.6;
    for (sy = [-1, 1])
    {
        color(dd_METALD()) translate([0, sy * D * 0.25, wh + 0.8]) rotate([sy * -ra, 0, 0]) dd_boxc([L + 0.9, rl, 0.16]);
        color([0.74, 0.76, 0.72]) translate([0, sy * D * 0.25, wh + 0.89]) rotate([sy * -ra, 0, 0]) dd_boxc([L * 0.46, rl * 0.26, 0.05]);
    }
    color(dd_METALC()) translate([0, 0, wh + 1.66]) dd_boxc([L + 0.7, 0.55, 0.22]);
    color(dd_METALC()) for (i = [-1, 0, 1]) translate([i * L * 0.3, 0, wh + 1.8]) cylinder(h = 0.55, r = 0.36, $fn = 7);
    // 卷帘门 + 装卸平台
    for (sx = [-1, 1])
    {
        color(dd_METALD()) translate([sx * L * 0.24, -D / 2 - 0.06, 2.3]) dd_boxc([4.2, 0.12, 4.4]);
        color([0.56, 0.57, 0.55]) for (k = [0 : 5])
            translate([sx * L * 0.24, -D / 2 - 0.13, 0.7 + k * 0.72]) dd_boxc([4.0, 0.04, 0.1]);
    }
    color(dd_CONCC()) translate([0, -D / 2 - 1.6, 0]) dd_slab(L * 0.72, 3.2, 1.2);
    color(dd_CONCD()) translate([0, -D / 2 - 3.3, 0]) dd_slab(2.6, 1.4, 0.6);
    color(dd_TRIMW()) translate([0, -D / 2 - 0.2, 5.6]) dd_boxc([L * 0.34, 0.1, 0.9]);   // 厂名牌
}

// 加油站：便利店 + 大雨棚 + 两组加油岛 + 价格牌。公路题材第一地标。
module dd_bldg_gasstation(seed = 0)
{
    bc = [[0.52, 0.48, 0.42], [0.47, 0.45, 0.41]][dd_rnd(seed, 2)];
    ac = dd_sign_c(seed + 3);
    color(dd_CONCC()) translate([0, 1.5, 0]) dd_slab(30, 24, 0.12);
    color(dd_CONCD()) translate([0, 1.5, 0.12]) dd_slab(29, 0.16, 0.014);
    // 便利店
    translate([0, 8.5, 0])
    {
        color(bc) dd_slab(12, 6.5, 3.4);
        color(dd_CONCD()) translate([0, 0, 3.4]) dd_slab(12.4, 6.9, 0.4);
        color(dd_DARKC()) translate([0, -3.3, 1.6]) dd_boxc([8.4, 0.12, 2.2]);
        color(dd_TRIMW()) for (i = [0 : 2]) translate([-2.8 + i * 2.8, -3.38, 1.6]) dd_boxc([0.1, 0.1, 2.2]);
        color([0.24, 0.24, 0.26]) translate([4.4, -3.3, 1.15]) dd_boxc([1.4, 0.12, 2.3]);
        color(ac) translate([0, -3.5, 3.2]) dd_boxc([11, 0.22, 0.85]);
        color(dd_TRIMW()) translate([-1.4, -3.63, 3.2]) dd_boxc([4.6, 0.04, 0.4]);
        color(dd_METALC()) translate([3.6, 1.8, 3.9]) dd_boxc([1.7, 1.3, 0.7]);
    }
    // 雨棚
    color(dd_METALC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 5.6, sy * 3.5, 2.45]) dd_boxc([0.44, 0.44, 4.9]);
    color([0.80, 0.79, 0.74]) translate([0, 0, 4.9]) dd_slab(16, 11, 0.2);
    color(ac) translate([0, 0, 5.1]) dd_slab(16.6, 11.6, 0.55);
    color(dd_TRIMW()) translate([0, -5.85, 5.35]) dd_boxc([16.6, 0.06, 0.26]);
    // 加油岛
    for (sy = [-1, 1])
        translate([0, sy * 2.8, 0])
        {
            color(dd_CONCD()) dd_slab(9.5, 1.7, 0.22);
            for (sx = [-1, 1])
                translate([sx * 2.7, 0, 0.22])
                {
                    color([0.70, 0.68, 0.64]) translate([0, 0, 0.78]) dd_boxc([1.05, 0.75, 1.56]);
                    color(dd_DARKC()) translate([0, -0.4, 1.12]) dd_boxc([0.62, 0.06, 0.42]);
                    color(dd_METALD()) translate([0, 0, 1.64]) dd_boxc([1.15, 0.85, 0.16]);
                    color(ac) translate([0, 0, 1.76]) dd_boxc([0.9, 0.6, 0.1]);
                }
        }
    // 价格牌
    translate([-11.5, -8.0, 0])
    {
        color(dd_METALC()) for (sx = [-1, 1]) translate([sx * 0.75, 0, 2.7]) dd_boxc([0.17, 0.17, 5.4]);
        color(ac) translate([0, 0, 5.9]) dd_boxc([3.4, 0.26, 2.0]);
        color(dd_TRIMW()) translate([0, -0.15, 6.4]) dd_boxc([2.7, 0.04, 0.75]);
        color(dd_TRIMW()) translate([0, -0.15, 5.4]) dd_boxc([2.3, 0.04, 0.45]);
    }
}

// 汽车旅馆：单层长条 + 前廊 + 每单元门窗 + 端头竖招牌（roadtrip 味道的核心建筑）
module dd_bldg_motel(seed = 0, units = 8)
{
    L = units * 4.2;
    D = 7.0;
    wc = [[0.55, 0.48, 0.38], [0.50, 0.45, 0.42], [0.52, 0.42, 0.32]][dd_rnd(seed, 3)];
    ac = dd_sign_c(seed + 5);
    color(dd_CONCD()) dd_slab(L + 1.0, D + 3.6, 0.25);
    color(wc) translate([0, 1.6, 0.25]) dd_slab(L, D, 2.9);
    translate([0, 1.6, 3.15]) dd_part_roof(L, D, 0.9, 0.6, 0, dd_roof_c(seed + 1));
    color(wc) translate([0, -1.4, 3.05]) dd_boxc([L, 3.3, 0.18]);
    color(dd_TRIMW()) for (i = [0 : units])
        translate([-L / 2 + i * L / units, -2.9, 1.55]) dd_boxc([0.14, 0.14, 3.1]);
    for (i = [0 : units - 1])
    {
        px = -L / 2 + (i + 0.5) * L / units;
        translate([px - 1.0, -1.95, 0.25])
            dd_part_door(0.95, 2.1, dd_rnd(seed + i * 7, 2) == 0 ? [0.30, 0.22, 0.16] : [0.23, 0.29, 0.33]);
        translate([px + 1.0, -1.95, 1.75]) dd_part_window(1.3, 1.2);
        if (dd_rnd(seed + i * 13, 3) == 0)
            color(dd_METALC()) translate([px + 1.0, -2.0, 1.05]) dd_boxc([0.7, 0.4, 0.4]);   // 窗机空调
    }
    // 端头竖牌
    translate([-L / 2 - 3.8, -2.2, 0])
    {
        color(dd_METALD()) cylinder(h = 7.4, r = 0.19, $fn = 6);
        color(ac) translate([0, 0, 5.8]) dd_boxc([1.9, 0.32, 4.4]);
        color(dd_TRIMW()) for (k = [0 : 3]) translate([0, -0.19, 7.2 - k * 0.95]) dd_boxc([1.15, 0.04, 0.62]);
        color([0.78, 0.70, 0.30]) translate([0, -0.22, 3.3]) dd_boxc([2.6, 0.07, 0.75]);
        color(dd_METALD()) translate([0, 0, 3.3]) dd_boxc([2.7, 0.14, 0.85]);
    }
}

// 路边餐车（diner）：不锈钢车身 + 一端圆头 + 环窗带 + 彩色檐口 + 屋顶招牌
module dd_bldg_diner(seed = 0)
{
    bc = [0.66, 0.66, 0.63];
    ac = dd_sign_c(seed + 1);
    color(dd_CONCD()) dd_slab(16, 11, 0.25);
    color(bc) translate([-1, 0, 0.25]) dd_slab(10, 7.4, 3.2);
    color(bc) translate([4, 0, 0.25]) cylinder(h = 3.2, r = 3.7, $fn = 12);
    color(dd_GLASSC())
    {
        translate([-1, 0, 2.05]) dd_boxc([10.2, 7.6, 1.4]);
        translate([4, 0, 2.05]) cylinder(h = 1.4, r = 3.8, center = true, $fn = 12);
    }
    color(ac)
    {
        translate([-1, 0, 3.6]) dd_boxc([10.6, 7.8, 0.55]);
        translate([4, 0, 3.6]) cylinder(h = 0.55, r = 3.9, center = true, $fn = 12);
    }
    color([0.50, 0.50, 0.48])
    {
        translate([-1, 0, 3.92]) dd_boxc([10.4, 7.6, 0.14]);
        translate([4, 0, 3.92]) cylinder(h = 0.14, r = 3.85, center = true, $fn = 12);
    }
    color(dd_TRIMW()) translate([-1, 0, 1.25]) dd_boxc([10.3, 7.7, 0.14]);
    // 屋顶招牌
    translate([-2.5, -1.2, 4.0])
    {
        color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 1.7, 0, 0.9]) dd_boxc([0.15, 0.15, 1.8]);
        color(ac) translate([0, 0, 2.25]) dd_boxc([4.6, 0.32, 1.6]);
        color([0.82, 0.76, 0.42]) translate([0, -0.2, 2.25]) dd_boxc([3.7, 0.05, 0.85]);
    }
    translate([-1, -3.75, 0.25]) dd_part_door(1.2, 2.2, [0.26, 0.26, 0.28]);
    color(dd_CONCC()) translate([-1, -4.6, 0]) dd_slab(2.6, 1.6, 0.25);
}

// 拖车房（mobile home）：砖垛支腿 + 金属底裙 + 浅坡顶 + 门廊台阶 + 遮阳篷 + 端部空调
module dd_bldg_trailer(seed = 0, L = 9, D = 3.6)
{
    wc = [[0.58, 0.56, 0.50], [0.50, 0.52, 0.49], [0.56, 0.48, 0.38], [0.44, 0.48, 0.50]][dd_rnd(seed, 4)];
    color(dd_CONCD()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (L / 2 - 0.9), sy * (D / 2 - 0.5), 0.21]) dd_boxc([0.55, 0.55, 0.42]);
    color([0.30, 0.30, 0.32]) translate([0, 0, 0.42]) dd_slab(L, D, 0.16);
    color(wc) translate([0, 0, 0.58]) dd_slab(L, D, 2.1);
    color(dd_METALC()) translate([0, 0, 0.5]) dd_slab(L + 0.08, D + 0.08, 0.12);
    color([wc[0] * 0.8, wc[1] * 0.8, wc[2] * 0.8]) translate([0, 0, 2.6]) dd_slab(L + 0.1, D + 0.1, 0.1);
    translate([0, 0, 2.7]) dd_part_roof(L, D, 0.55, 0.35, 0, dd_roof_c(seed + 2));
    translate([-L * 0.26, -D / 2 - 0.05, 0.58]) dd_part_door(0.9, 1.95, [0.29, 0.23, 0.17]);
    color(dd_METALD()) translate([-L * 0.26, -D / 2 - 0.6, 0.16]) dd_slab(1.3, 1.1, 0.42);
    color(dd_rnd(seed + 3, 2) == 0 ? [0.52, 0.27, 0.17] : [0.28, 0.38, 0.44])
        translate([-L * 0.26, -D / 2 - 0.75, 2.55]) rotate([14, 0, 0]) dd_boxc([2.6, 1.7, 0.07]);
    translate([L * 0.16, -D / 2 - 0.05, 1.8]) dd_part_window(1.3, 0.95);
    translate([L * 0.42, -D / 2 - 0.05, 1.8]) dd_part_window(0.85, 0.95);
    translate([-L * 0.42, D / 2 + 0.05, 1.8]) rotate([0, 0, 180]) dd_part_window(1.0, 0.9);
    color(dd_METALC()) translate([L / 2 + 0.12, 0, 1.75]) dd_boxc([0.55, 1.05, 0.85]);
    color([0.70, 0.68, 0.60]) translate([L / 2 + 0.55, D * 0.28, 0.55]) cylinder(h = 1.0, r = 0.28, $fn = 7);
}

// 烧毁/坍塌的房屋：基础板 + 高低不齐的残墙 + 立着的砖烟囱 + 塌落屋面板 + 瓦砾与焦木。
// 在整齐的住宅排里插一两栋，立刻把排屋阵列打成经历过灾难的街区。
module dd_bldg_ruin(seed = 0, L = 9, D = 7)
{
    color(dd_PLINTH()) dd_slab(L + 0.4, D + 0.4, 0.3);
    color([0.21, 0.19, 0.18]) translate([0, 0, 0.3]) dd_slab(L, D, 0.09);
    wc = dd_wall_c(seed);
    cc = [wc[0] * 0.5, wc[1] * 0.48, wc[2] * 0.46];
    for (sy = [-1, 1])
    {
        hh = dd_rndr(seed + (sy + 2) * 17, 0.5, 2.6);
        color(cc) translate([dd_rndr(seed + (sy + 2) * 7, -L * 0.16, L * 0.16), sy * D / 2, 0.3 + hh / 2])
            dd_boxc([L * dd_rndr(seed + (sy + 2) * 11, 0.45, 0.95), 0.26, hh]);
    }
    for (sx = [-1, 1])
    {
        hh = dd_rndr(seed + (sx + 2) * 23 + 5, 0.6, 2.8);
        color(cc) translate([sx * L / 2, dd_rndr(seed + (sx + 2) * 13, -D * 0.16, D * 0.16), 0.3 + hh / 2])
            dd_boxc([0.26, D * dd_rndr(seed + (sx + 2) * 19, 0.4, 0.9), hh]);
    }
    color([0.38, 0.25, 0.20]) translate([L * 0.3, D * 0.22, 0.3]) dd_slab(0.75, 0.75, 3.5);
    color(dd_PLINTH()) translate([L * 0.3, D * 0.22, 3.8]) dd_slab(0.9, 0.9, 0.14);
    for (i = [0 : 2])
        color(dd_ROOFD())
            translate([dd_rndr(seed + i * 29, -L * 0.3, L * 0.3), dd_rndr(seed + i * 31 + 3, -D * 0.3, D * 0.3), 0.52])
                rotate([dd_rndr(seed + i * 7, -22, 22), dd_rndr(seed + i * 11, -18, 18), dd_rnd(seed + i, 180)])
                    dd_boxc([dd_rndr(seed + i, 2.2, 4.0), dd_rndr(seed + i + 9, 1.4, 2.6), 0.12]);
    for (i = [0 : 4])
        color(i % 2 == 0 ? [0.33, 0.31, 0.29] : [0.23, 0.20, 0.17])
            translate([dd_rndr(seed * 3 + i * 13, -L * 0.42, L * 0.42),
                       dd_rndr(seed * 5 + i * 17 + 7, -D * 0.42, D * 0.42), 0.3])
                rotate([0, 0, dd_rnd(seed + i, 180)])
                    scale([dd_rndr(seed + i, 0.7, 1.7), dd_rndr(seed + i + 3, 0.6, 1.2), 1])
                        cylinder(h = dd_rndr(seed + i + 5, 0.25, 0.6), r = 1, $fn = 7);
    for (i = [0 : 2])
        color([0.16, 0.14, 0.13])
            translate([dd_rndr(seed + i * 41, -L * 0.35, L * 0.35), dd_rndr(seed + i * 43 + 5, -D * 0.35, D * 0.35), 0.62])
                rotate([0, dd_rndr(seed + i * 3, 72, 108), dd_rnd(seed + i, 180)])
                    dd_boxc([0.16, 0.16, dd_rndr(seed + i + 7, 1.8, 3.4)]);
}

// 美式水塔（全高约 18*s）：外八字四腿（顶端内收）+ 两层圈梁 + 交叉斜撑 + 罐体 + 检修盘。
// 一张图放一座，跑图时它就是玩家的方位锚。
module dd_bldg_watertower(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        lh = 11.0;
        r0 = 3.4;
        tl = 8;
        for (sx = [-1, 1], sy = [-1, 1])
            color(dd_METALD()) translate([sx * r0, sy * r0, lh / 2]) rotate([sy * tl, sx * -tl, 0]) dd_boxc([0.32, 0.32, lh]);
        for (z = [3.4, 7.6])
        {
            rz = r0 + (lh / 2 - z) * tan(tl);
            color(dd_METALD())
            {
                for (sy = [-1, 1]) translate([0, sy * rz, z]) dd_boxc([rz * 2, 0.16, 0.16]);
                for (sx = [-1, 1]) translate([sx * rz, 0, z]) dd_boxc([0.16, rz * 2, 0.16]);
            }
        }
        rz0 = r0 + (lh / 2 - 3.4) * tan(tl);
        rz1 = r0 + (lh / 2 - 7.6) * tan(tl);
        bl = sqrt((rz0 + rz1) * (rz0 + rz1) + 4.2 * 4.2);
        ba = atan((rz0 + rz1) / 4.2);
        for (a = [0, 90, 180, 270])
            rotate([0, 0, a])
                for (sd = [-1, 1])
                    color(dd_METALD()) translate([0, (rz0 + rz1) / 2, 5.5]) rotate([0, sd * ba, 0]) dd_boxc([0.12, 0.12, bl]);
        tr = 3.3;
        color(dd_METALC())
        {
            translate([0, 0, lh - 1.5]) cylinder(h = 1.6, r1 = 0.7, r2 = 2.8, $fn = 10);
            translate([0, 0, lh + 0.1]) cylinder(h = 4.6, r = tr, $fn = 10);
            translate([0, 0, lh + 4.7]) cylinder(h = 1.7, r1 = tr, r2 = 1.0, $fn = 10);
            translate([0, 0, lh + 6.4]) cylinder(h = 0.5, r = 0.42, $fn = 7);
        }
        color([0.54, 0.52, 0.49]) translate([0, 0, lh + 0.9]) cylinder(h = 0.45, r = tr + 0.04, $fn = 10);
        color([0.58, 0.19, 0.14]) translate([0, 0, lh + 2.3]) cylinder(h = 1.3, r = tr + 0.05, $fn = 10);
        color(dd_METALD()) translate([0, 0, lh + 0.05]) cylinder(h = 0.09, r = tr + 0.6, $fn = 10);
        color(dd_METALD()) for (a = [0 : 45 : 315])
            rotate([0, 0, a]) translate([tr + 0.5, 0, lh + 0.5]) dd_boxc([0.07, 0.07, 0.9]);
        // 爬梯
        color(dd_METALD()) for (sx = [-1, 1])
            translate([sx * 0.24, -r0 - 0.5, lh / 2 + 0.1]) dd_boxc([0.07, 0.07, lh]);
        color(dd_METALD()) for (i = [0 : 12])
            translate([0, -r0 - 0.5, 0.8 + i * 0.85]) dd_boxc([0.5, 0.05, 0.05]);
    }
}

// 谷物筒仓（全高约 14*s）：波纹罐身 + 锥顶 + 爬梯 + 卸料斗。与谷仓成组出现。
module dd_bldg_silo(seed = 0, s = 1.0)
{
    scale([s, s, s])
    {
        r = 2.6;
        h = 12;
        color([0.58, 0.56, 0.51]) cylinder(h = h, r = r, $fn = 10);
        color([0.50, 0.48, 0.44]) for (i = [0 : 5]) translate([0, 0, 1.2 + i * 1.9]) cylinder(h = 0.22, r = r + 0.05, $fn = 10);
        color(dd_METALD()) translate([0, 0, h]) cylinder(h = 1.9, r1 = r + 0.12, r2 = 0.55, $fn = 10);
        color(dd_METALD()) translate([0, 0, h + 1.9]) cylinder(h = 0.5, r = 0.45, $fn = 7);
        color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 0.22, -r - 0.14, h / 2]) dd_boxc([0.07, 0.07, h]);
        color(dd_METALD()) for (i = [0 : 13]) translate([0, -r - 0.14, 0.6 + i * 0.85]) dd_boxc([0.46, 0.05, 0.05]);
        color(dd_METALC()) translate([r + 0.45, 0, 1.4]) rotate([0, 22, 0]) dd_boxc([0.55, 0.75, 2.8]);
        if (dd_rnd(seed, 3) == 0)
            color(dd_RUSTC()) translate([0, 0, 3.0]) cylinder(h = 1.4, r = r + 0.03, $fn = 10);
    }
}

// ================= v2 道具与载具 =================

// 公路广告牌（front = -y）：双柱 + 牌面 + 检修走道，seed 偶尔给一角撕裂的画布
module dd_prop_billboard(seed = 0)
{
    ac = dd_sign_c(seed);
    color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 2.5, 0, 3.0]) dd_boxc([0.3, 0.3, 6.0]);
    color([0.48, 0.46, 0.43]) translate([0, 0.1, 6.7]) dd_boxc([9.8, 0.2, 3.5]);
    color(ac) translate([0, -0.06, 6.7]) dd_boxc([9.5, 0.12, 3.3]);
    color(dd_TRIMW())
    {
        translate([-1.6, -0.15, 7.4]) dd_boxc([5.4, 0.04, 0.95]);
        translate([1.2, -0.15, 6.1]) dd_boxc([3.4, 0.04, 0.5]);
    }
    if (dd_rnd(seed + 3, 3) == 0)
        color([0.42, 0.40, 0.37]) translate([3.6, -0.24, 5.5]) rotate([26, 0, 10]) dd_boxc([1.9, 0.05, 1.7]);
    color(dd_METALD()) translate([0, 0.18, 4.85]) dd_boxc([9.2, 0.12, 0.14]);
    color(dd_METALD()) for (sx = [-1, 1]) translate([sx * 2.5, 0.5, 3.6]) rotate([0, 0, 0]) rotate([32, 0, 0]) dd_boxc([0.12, 0.12, 7.0]);
}

// 海运集装箱（stack 层叠放，上层轻微错位）：货场/路障/临时仓库
module dd_prop_container(seed = 0, stack = 1)
{
    for (k = [0 : stack - 1])
    {
        cc = [[0.52, 0.27, 0.12], [0.19, 0.35, 0.48], [0.23, 0.40, 0.27],
              [0.46, 0.44, 0.40], [0.48, 0.16, 0.12]][dd_rnd(seed + k * 17, 5)];
        translate([dd_rndr(seed + k * 7, -0.3, 0.3), dd_rndr(seed + k * 11, -0.18, 0.18), k * 2.64])
            rotate([0, 0, k == 0 ? 0 : dd_rndr(seed + k * 13, -3, 3)])
            {
                color(cc) translate([0, 0, 1.3]) dd_boxc([6.1, 2.44, 2.6]);
                color([cc[0] * 0.78, cc[1] * 0.78, cc[2] * 0.78])
                {
                    for (i = [0 : 7]) translate([-2.45 + i * 0.7, 0, 1.35]) dd_boxc([0.12, 2.5, 2.25]);
                    translate([0, 0, 0.13]) dd_boxc([6.16, 2.5, 0.26]);
                    translate([0, 0, 2.5]) dd_boxc([6.16, 2.5, 0.22]);
                    translate([3.07, 0, 1.35]) dd_boxc([0.06, 2.2, 2.2]);
                }
                color([0.28, 0.28, 0.28]) for (sx = [-1, 1], sy = [-1, 1])
                    translate([sx * 3.0, sy * 1.16, 0.14]) dd_boxc([0.26, 0.22, 0.3]);
            }
    }
}

// 立式储罐（全高约 8*s）：罐身 + 加强环 + 锥顶 + 爬梯 + 底部管路。工业区/加油站后场
module dd_prop_tank(seed = 0, s = 1.0)
{
    scale([s, s, s])
    {
        color([0.56, 0.55, 0.51]) cylinder(h = 6.4, r = 3.0, $fn = 10);
        color([0.48, 0.47, 0.44]) for (z = [1.6, 3.4, 5.2]) translate([0, 0, z]) cylinder(h = 0.2, r = 3.06, $fn = 10);
        color(dd_METALD()) translate([0, 0, 6.4]) cylinder(h = 0.9, r1 = 3.0, r2 = 2.1, $fn = 10);
        color(dd_METALD()) translate([0, 0, 7.3]) cylinder(h = 0.45, r = 0.4, $fn = 6);
        if (dd_rnd(seed, 2) == 0)
            color(dd_RUSTC()) translate([2.0, -1.8, 1.2]) rotate([0, 0, 0]) dd_boxc([1.4, 0.06, 2.6]);
        color(dd_METALD())
        {
            for (sx = [-1, 1]) translate([sx * 0.24, -3.15, 3.2]) dd_boxc([0.07, 0.07, 6.4]);
            for (i = [0 : 6]) translate([0, -3.15, 0.6 + i * 0.9]) dd_boxc([0.5, 0.05, 0.05]);
            translate([0, 0, 6.42]) cylinder(h = 0.07, r = 2.2, $fn = 9);
        }
        color(dd_METALC())
        {
            translate([3.4, 0, 0.5]) rotate([0, 90, 0]) cylinder(h = 1.6, r = 0.2, $fn = 6);
            translate([4.2, 0, 0.9]) cylinder(h = 0.9, r = 0.2, $fn = 6);
            translate([4.2, 0, 1.85]) dd_boxc([0.5, 0.34, 0.3]);
        }
    }
}

// 格构信号塔（全高约 26*s）：收分四腿 + 层间横撑 + 天线板 + 塔顶航空障碍灯
module dd_prop_radiomast(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        h = 22;
        r0 = 1.7;
        tl = 3.2;
        for (sx = [-1, 1], sy = [-1, 1])
            color(dd_METALD()) translate([sx * r0, sy * r0, h / 2]) rotate([sy * tl, sx * -tl, 0]) dd_boxc([0.18, 0.18, h]);
        for (k = [0 : 6])
        {
            z = 1.6 + k * 3.2;
            rz = r0 + (h / 2 - z) * tan(tl);
            color(dd_METALD())
            {
                for (sy = [-1, 1]) translate([0, sy * rz, z]) dd_boxc([rz * 2, 0.1, 0.1]);
                for (sx = [-1, 1]) translate([sx * rz, 0, z]) dd_boxc([0.1, rz * 2, 0.1]);
            }
        }
        for (k = [0 : 5])
        {
            z0 = 1.6 + k * 3.2;
            zc = z0 + 1.6;
            rz0 = r0 + (h / 2 - z0) * tan(tl);
            rz1 = r0 + (h / 2 - z0 - 3.2) * tan(tl);
            bl = sqrt((rz0 + rz1) * (rz0 + rz1) + 10.24);
            ba = atan((rz0 + rz1) / 3.2);
            for (sy = [-1, 1])
                color(dd_METALD()) translate([0, sy * (rz0 + rz1) / 2, zc])
                    rotate([0, (k % 2 == 0 ? 1 : -1) * ba, 0]) dd_boxc([0.08, 0.08, bl]);
        }
        color(dd_METALC()) translate([0, 0, h]) cylinder(h = 3.6, r = 0.13, $fn = 5);
        color(dd_REDC()) translate([0, 0, h + 3.6]) sphere(r = 0.24, $fn = 6);
        color(dd_METALC()) for (a = [0, 120, 240])
            rotate([0, 0, a + dd_rnd(seed, 30)]) translate([1.6, 0, h - 3.4]) dd_boxc([0.5, 1.5, 1.0]);
        color(dd_METALD()) dd_slab(4.6, 4.6, 0.5);
    }
}

// 幸存者帐篷（front = -y）：地布 + 双坡篷面 + 门帘 + 地钉绳
module dd_prop_tent(seed = 0, s = 1.0)
{
    tc = [[0.30, 0.35, 0.23], [0.40, 0.33, 0.20], [0.23, 0.29, 0.35], [0.47, 0.40, 0.22]][dd_rnd(seed, 4)];
    scale([s, s, s])
    {
        color([0.27, 0.25, 0.21]) dd_slab(3.2, 2.8, 0.06);
        translate([0, 0, 0.06]) dd_part_roof(2.5, 2.4, 1.5, 0.3, 0, tc);
        color([tc[0] * 0.7, tc[1] * 0.7, tc[2] * 0.7]) translate([0, -1.28, 0.6]) dd_boxc([0.9, 0.06, 1.1]);
        color([tc[0] * 1.05, tc[1] * 1.05, tc[2] * 1.05])
            translate([0.72, -1.24, 0.75]) rotate([0, 0, 22]) dd_boxc([0.55, 0.05, 1.3]);
        color(dd_METALD()) for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 1.6, sy * 1.5, 0.24]) rotate([sy * 34, sx * -20, 0]) dd_boxc([0.04, 0.04, 0.55]);
    }
}

// 营地篝火：灰烬圈 + 石头圈 + 交叉焦木 + 余烬（seed 决定要不要三脚架吊锅）
module dd_prop_campfire(seed = 0)
{
    color([0.23, 0.21, 0.19]) scale([1.5, 1.4, 1]) cylinder(h = 0.04, r = 1, $fn = 9);
    for (i = [0 : 7])
        color([0.44, 0.42, 0.39])
            rotate([0, 0, i * 45 + dd_rnd(seed, 22)]) translate([1.05, 0, 0.09]) dd_boxc([0.32, 0.26, 0.18]);
    for (i = [0 : 3])
        color([0.19, 0.16, 0.14])
            rotate([0, 0, i * 45 + 20]) translate([0, 0, 0.2]) rotate([0, 72, 0]) dd_boxc([0.15, 0.15, 1.1]);
    color([0.68, 0.32, 0.08]) translate([0, 0, 0.1]) cylinder(h = 0.26, r1 = 0.32, r2 = 0.05, $fn = 6);
    if (dd_rnd(seed + 3, 2) == 0)
    {
        color(dd_METALD())
        {
            for (a = [0, 120, 240]) rotate([0, 0, a]) translate([0.62, 0, 0.85]) rotate([0, 22, 0]) dd_boxc([0.06, 0.06, 1.8]);
            translate([0, 0, 1.12]) dd_boxc([0.5, 0.06, 0.06]);
            translate([0, 0, 0.85]) cylinder(h = 0.34, r = 0.26, $fn = 8);
        }
    }
}

// 校车 / 城际巴士（车头 +x，seed 选黄校车或灰巴士）
module dd_veh_bus(seed = 0)
{
    school = dd_rnd(seed, 3) != 0;
    bc = school ? dd_BUSY() : [0.40, 0.42, 0.45];
    color(bc)
    {
        translate([-0.6, 0, 1.8]) dd_boxc([9.4, 2.5, 1.9]);
        translate([4.6, 0, 1.35]) dd_boxc([1.7, 2.3, 1.1]);
        translate([-0.6, 0, 2.78]) dd_boxc([9.2, 2.4, 0.16]);
    }
    color(dd_GLASSC())
    {
        translate([-0.6, 0, 2.2]) dd_boxc([9.0, 2.54, 0.95]);
        translate([4.1, 0, 2.3]) dd_boxc([0.5, 2.2, 0.85]);
    }
    color([0.14, 0.14, 0.14])
    {
        translate([-0.6, 0, 1.2]) dd_boxc([9.5, 2.54, 0.22]);
        translate([1.0, 0, 0.62]) dd_boxc([8.0, 2.2, 0.44]);
    }
    color(school ? [0.12, 0.12, 0.12] : dd_METALC()) translate([-5.35, 0, 1.9]) dd_boxc([0.12, 2.2, 1.5]);
    color(dd_REDC()) for (sy = [-1, 1])
    {
        translate([-5.36, sy * 0.9, 2.6]) dd_boxc([0.1, 0.32, 0.22]);
        translate([-0.6, sy * 1.28, 3.0]) dd_boxc([0.5, 0.16, 0.28]);
    }
    color(dd_MARKW()) for (sy = [-1, 1]) translate([5.44, sy * 0.75, 1.1]) dd_boxc([0.06, 0.34, 0.2]);
    for (sy = [-1, 1])
    {
        translate([3.4, sy * 1.15, 0.5]) dd_veh_wheel(0.5, 0.3);
        translate([-2.6, sy * 1.15, 0.5]) dd_veh_wheel(0.5, 0.3);
        translate([-3.7, sy * 1.15, 0.5]) dd_veh_wheel(0.5, 0.3);
    }
}

// 半挂卡车（车头 +x，trailer=0 时只有牵引车）：公路上最大的一块可读体量
module dd_veh_truck(seed = 0, trailer = 1)
{
    cc = dd_car_c(seed + 7);
    color(cc)
    {
        translate([4.9, 0, 1.75]) dd_boxc([2.7, 2.5, 2.1]);
        translate([6.9, 0, 1.15]) dd_boxc([1.5, 2.5, 1.2]);
    }
    color(dd_GLASSC()) translate([6.2, 0, 2.35]) dd_boxc([0.45, 2.3, 0.85]);
    color(dd_METALC())
    {
        translate([7.7, 0, 0.75]) dd_boxc([0.26, 2.5, 0.55]);
        for (sy = [-1, 1]) translate([3.7, sy * 1.2, 2.2]) cylinder(h = 2.0, r = 0.14, $fn = 6);
    }
    color([0.22, 0.22, 0.24]) translate([2.6, 0, 0.85]) dd_boxc([5.6, 2.2, 0.5]);
    color(dd_MARKW()) for (sy = [-1, 1]) translate([7.66, sy * 0.8, 1.35]) dd_boxc([0.07, 0.36, 0.24]);
    for (sy = [-1, 1])
    {
        translate([6.0, sy * 1.2, 0.55]) dd_veh_wheel(0.55, 0.32);
        translate([3.4, sy * 1.2, 0.55]) dd_veh_wheel(0.55, 0.32);
        translate([2.2, sy * 1.2, 0.55]) dd_veh_wheel(0.55, 0.32);
    }
    if (trailer == 1)
    {
        tc = [[0.60, 0.59, 0.55], [0.46, 0.48, 0.50], [0.55, 0.45, 0.32]][dd_rnd(seed + 11, 3)];
        color(tc) translate([-4.0, 0, 2.7]) dd_boxc([11.6, 2.55, 2.9]);
        color([tc[0] * 0.82, tc[1] * 0.82, tc[2] * 0.82])
        {
            for (i = [0 : 9]) translate([-9.4 + i * 1.2, 0, 2.7]) dd_boxc([0.1, 2.6, 2.8]);
            translate([-9.78, 0, 2.7]) dd_boxc([0.1, 2.4, 2.6]);
        }
        color(dd_sign_c(seed + 5)) translate([-4.0, -1.3, 3.3]) dd_boxc([5.0, 0.06, 1.1]);
        color([0.22, 0.22, 0.24]) translate([-4.0, 0, 1.15]) dd_boxc([11.4, 2.0, 0.34]);
        color(dd_METALD()) for (sy = [-1, 1]) translate([-9.0, sy * 0.9, 0.55]) dd_boxc([0.14, 0.14, 1.1]);
        for (sy = [-1, 1])
        {
            translate([-8.2, sy * 1.22, 0.55]) dd_veh_wheel(0.55, 0.32);
            translate([-7.0, sy * 1.22, 0.55]) dd_veh_wheel(0.55, 0.32);
        }
    }
}

// ================= v2 大尺度地表（俯视地图用，成本 < 250 三角/块） =================

// 大田块：土色底 + 犁沟条纹 + 草埂。dd_nature_crop_patch 逐株建模，铺 40x30 会到 2 万三角；
// 这个模块专为"一屏一块田"的农业区设计，可以放心铺满北部农田。
module dd_nature_field_big(L = 40, D = 30, seed = 0)
{
    c = [[0.42, 0.33, 0.20], [0.37, 0.34, 0.19], [0.33, 0.36, 0.21], [0.45, 0.40, 0.22]][dd_rnd(seed, 4)];
    color(c) dd_slab(L, D, 0.10);
    n = max(3, floor(D / 2.4));
    for (i = [0 : n - 1])
        color(dd_rnd(seed + i * 7, 3) == 0 ? [c[0] * 0.80, c[1] * 0.80, c[2] * 0.80]
                                           : [c[0] * 1.10, c[1] * 1.08, c[2] * 1.02])
            translate([0, -D / 2 + (i + 0.5) * D / n, 0.10]) dd_slab(L - 1.2, D / n * 0.55, 0.02);
    color(dd_GRASSD()) for (sy = [-1, 1]) translate([0, sy * (D / 2 - 0.4), 0.10]) dd_slab(L, 0.8, 0.03);
    if (dd_rnd(seed + 5, 3) == 0)   // 未收割的一角
        color([0.55, 0.47, 0.22]) translate([dd_rndr(seed, -L * 0.25, L * 0.25), 0, 0.12])
            dd_slab(L * 0.3, D * 0.7, 0.16);
}

// 干裂地表（旱地/干涸河床/盐碱滩）：浅色底 + 龟裂纹 + 风蚀深斑
module dd_ground_cracked(L = 30, D = 20, seed = 0, c = [0.54, 0.45, 0.30])
{
    color(c) dd_slab(L, D, 0.09);
    for (i = [0 : 9])
        color([c[0] * 0.72, c[1] * 0.72, c[2] * 0.72])
            translate([dd_rndr(seed * 7 + i * 31, -L * 0.42, L * 0.42),
                       dd_rndr(seed * 13 + i * 17 + 5, -D * 0.42, D * 0.42), 0.09])
                rotate([0, 0, dd_rnd(seed + i * 11, 180)])
                    dd_slab(dd_rndr(seed + i, L * 0.14, L * 0.42), 0.18, 0.014);
    for (i = [0 : 2])
        color([c[0] * 0.86, c[1] * 0.84, c[2] * 0.80])
            translate([dd_rndr(seed * 3 + i * 23, -L * 0.3, L * 0.3),
                       dd_rndr(seed * 5 + i * 29 + 7, -D * 0.3, D * 0.3), 0.09])
                rotate([0, 0, dd_rnd(seed + i * 5, 180)])
                    scale([dd_rndr(seed + i, L * 0.1, L * 0.24), dd_rndr(seed + i + 3, D * 0.1, D * 0.22), 1])
                        cylinder(h = 0.016, r = 1, $fn = 8);
}
