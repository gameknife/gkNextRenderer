// kit_pitlane.scad —— 赛车场维修区（Pit Lane / Paddock）零件库（GT3 耐力赛围场风格）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "rp_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具车头朝 +x）。
//   rp_ground_pitlane：车库侧 = -y（front），快车道在 +y 侧（贴维修墙）。
//   rp_prop_gantry：沿 y 跨越赛道，中心为锚点。
// 调用方自设 $fn（建议 12）。尺度：mid，1 unit = 1 m。
// 类别：ground 地面 / bldg 建筑 / prop 道具设施 / veh 载具。
// 覆盖场景：赛车场维修区——P 房排、维修通道、赛道段落、看台、控制塔、围场后勤区。

// ================= 配色（赛车场现代低饱和；PT 强日光下会整体提亮，故基色偏深偏饱和） =================
function rp_ASPH()    = [0.27, 0.27, 0.29];   // 赛道沥青
function rp_ASHD()    = [0.20, 0.20, 0.22];   // 沥青补丁/胎痕
function rp_PITC()    = [0.31, 0.31, 0.33];   // 维修通道沥青（略浅）
function rp_CONC()    = [0.50, 0.50, 0.48];   // 混凝土（围场坪/墙）
function rp_CONCD()   = [0.38, 0.38, 0.36];   // 脏污混凝土/基座
function rp_PANEL()   = [0.56, 0.57, 0.58];   // P 房墙板浅灰
function rp_PANELD()  = [0.42, 0.43, 0.44];   // 墙板拼缝/层间
function rp_METALC()  = [0.45, 0.47, 0.49];   // 镀锌金属
function rp_METALD()  = [0.25, 0.27, 0.29];   // 深金属/卷帘门
function rp_CARBON()  = [0.09, 0.09, 0.10];   // 碳纤维（前铲/尾翼/扩散器）
function rp_TIREC()   = [0.08, 0.08, 0.08];   // 轮胎
function rp_DARKC()   = [0.10, 0.10, 0.11];   // 洞口/屏幕/窗芯
function rp_GLASSC()  = [0.28, 0.38, 0.44];   // 玻璃
function rp_EPOXY()   = [0.34, 0.37, 0.40];   // 车库环氧地坪
function rp_MARKW()   = [0.76, 0.76, 0.72];   // 白标线
function rp_YELLOWC() = [0.74, 0.56, 0.10];   // 警示黄
function rp_CURBR()   = [0.58, 0.12, 0.09];   // 路肩红
function rp_CURBW()   = [0.72, 0.72, 0.68];   // 路肩白
function rp_GRASSC()  = [0.34, 0.40, 0.22];   // 草地
function rp_GRASSD()  = [0.27, 0.33, 0.18];   // 深草斑
function rp_GRAVEL()  = [0.50, 0.46, 0.36];   // 砂石缓冲区
function rp_GRAVELD() = [0.42, 0.38, 0.30];   // 砂石暗斑
function rp_TRUNKC()  = [0.30, 0.23, 0.16];   // 树干
function rp_LEAFC()   = [0.30, 0.40, 0.19];   // 阔叶
function rp_LEAFD()   = [0.23, 0.32, 0.15];   // 阔叶深
function rp_TOOLR()   = [0.55, 0.12, 0.10];   // 工具柜红
function rp_AMBER()   = [0.85, 0.55, 0.10];   // 警示灯橙（略亮，自发光观感）

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function rp_sq(x) = (x * x + x * 563 + 41) % 65521;
function rp_rnd(s, m) = rp_sq(rp_sq(((s % 65521) + 65521) % 65521) + 17) % m;
function rp_rndf(s) = rp_rnd(s, 1000) / 999;                       // [0, 1]
function rp_rndr(s, a, b) = a + (b - a) * rp_rndf(s);              // [a, b]

// ---- 车队涂装调色板（主色 + 饰条色；先赋局部变量再下标） ----
function rp_team_c(i)  = [[0.62, 0.10, 0.08], [0.10, 0.28, 0.55], [0.14, 0.44, 0.20], [0.75, 0.42, 0.08],
                          [0.52, 0.53, 0.55], [0.48, 0.10, 0.32], [0.10, 0.44, 0.48], [0.26, 0.26, 0.30]][rp_rnd(i, 8)];
function rp_team_ac(i) = [[0.78, 0.78, 0.74], [0.10, 0.10, 0.11], [0.74, 0.56, 0.10], [0.10, 0.28, 0.55],
                          [0.62, 0.10, 0.08], [0.78, 0.78, 0.74], [0.10, 0.10, 0.11], [0.74, 0.56, 0.10]][rp_rnd(i + 31, 8)];
function rp_seat_c(i)  = [[0.20, 0.34, 0.52], [0.58, 0.14, 0.10], [0.64, 0.60, 0.54], [0.24, 0.42, 0.22]][rp_rnd(i, 4)];

// ---- 基础工具 ----
module rp_boxc(s) cube(s, center = true);
module rp_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) rp_boxc([L, D, t]);   // 底面 z=0 平板

// 攒尖顶 polyhedron（帐篷/雨棚；底面 LxD 于 z=0，尖高 h）
module rp_part_pyramid(L = 3, D = 3, h = 0.8, c = [0.5, 0.5, 0.5])
{
    color(c) polyhedron(
        points = [[-L/2, -D/2, 0], [L/2, -D/2, 0], [L/2, D/2, 0], [-L/2, D/2, 0], [0, 0, h]],
        faces = [[0, 1, 2, 3], [0, 4, 1], [1, 4, 2], [2, 4, 3], [3, 4, 0]]);
}

// 单条轮胎（轴沿 y，胎心为原点；竖立/横放由调用方旋转）
module rp_part_tire(r = 0.34, w = 0.26)
{
    color(rp_TIREC()) difference()
    {
        rotate([90, 0, 0]) cylinder(h = w, r = r, center = true, $fn = 10);
        rotate([90, 0, 0]) cylinder(h = w + 0.1, r = r * 0.55, center = true, $fn = 10);
    }
}

// 标准车轮（胎 + 毂；轮毂心 z=0，调用方抬 r 落地）
module rp_veh_wheel(r = 0.34, w = 0.30)
{
    color(rp_TIREC()) translate([0, w / 2, 0]) rotate([90, 0, 0]) cylinder(h = w, r = r, $fn = 8);
    color(rp_METALC()) translate([0, (w + 0.04) / 2, 0]) rotate([90, 0, 0]) cylinder(h = w + 0.04, r = r * 0.45, $fn = 8);
}

// ================= 地面（底面 z=0 薄板；同层不重叠，叠层抬 0.2 一级） =================

// 赛道段：沥青 + 两侧白边线 + 红白路肩 + 补丁/胎痕。start=true 时中段画发车格线与起跑线。
module rp_ground_track(L = 24, W = 12, seed = 0, start = false)
{
    color(rp_ASPH()) rp_slab(L, W, 0.14);
    for (sy = [-1, 1])
    {
        color(rp_MARKW()) translate([0, sy * (W / 2 - 0.55), 0.14]) rp_slab(L, 0.15, 0.012);
        for (i = [0 : floor(L / 2) - 1])
            color(rp_rnd(i + seed * 7, 2) == 0 ? rp_CURBR() : rp_CURBW())
                translate([-L / 2 + 1 + i * 2, sy * (W / 2 - 1.25), 0.14]) rp_slab(2, 1.2, 0.014);
    }
    for (i = [0 : 2])
        color(rp_ASHD())
            translate([rp_rndr(seed * 7 + i * 31, -(L - 6) / 2, (L - 6) / 2),
                       rp_rndr(seed * 13 + i * 17 + 5, -(W - 5) / 2, (W - 5) / 2), 0.14])
                rp_slab(1.6 + rp_rnd(seed + i, 3), 1.1 + rp_rnd(seed + i + 9, 3) * 0.4, 0.012);
    // 刹车区胎痕（弯前拖黑）
    color([0.16, 0.16, 0.17]) for (sy = [-1, 1])
        translate([L * 0.28, sy * W * 0.18, 0.14]) rp_slab(L * 0.3, 0.4, 0.01);
    if (start)
    {
        // 起跑线：两排交错白块（棋盘观感）
        for (r = [0 : 1], i = [0 : floor(W / 0.8) - 1])
            color(rp_MARKW()) translate([r * 0.4, -W / 2 + 1.9 + i * 0.8 + r * 0.4, 0.14]) rp_slab(0.4, 0.4, 0.016);
        // 发车格：横向短划线，每格一行
        for (g = [1 : 6], sy = [-1, 1])
            color(rp_MARKW()) translate([-g * 3.2 - 1, sy * W * 0.22, 0.14]) rp_slab(0.18, 1.6, 0.014);
    }
}

// 维修通道：车库侧（-y）画 pit box 白框，+y 侧为快车道分隔线。box 间距 8m（对齐 P 房开间）。
module rp_ground_pitlane(L = 24, W = 12, seed = 0)
{
    color(rp_PITC()) rp_slab(L, W, 0.14);
    color(rp_MARKW()) translate([0, W / 2 - 1.4, 0.14]) rp_slab(L, 0.15, 0.012);      // 快车道分隔线
    nb = floor(L / 8);
    for (i = [0 : nb - 1])
    {
        bx = -L / 2 + 4 + i * 8;
        // pit box 白框（4.8 x 3.6，开口向车库）
        color(rp_MARKW())
        {
            translate([bx, -W / 2 + 0.4, 0.14]) rp_slab(4.8, 0.14, 0.012);
            translate([bx, -W / 2 + 4.0, 0.14]) rp_slab(4.8, 0.14, 0.012);
            for (sx = [-1, 1]) translate([bx + sx * 2.4, -W / 2 + 2.2, 0.14]) rp_slab(0.14, 3.6, 0.012);
        }
        // 停车定位黄块
        color(rp_YELLOWC()) translate([bx, -W / 2 + 2.2, 0.14]) rp_slab(0.5, 0.5, 0.014);
    }
    for (i = [0 : 1])
        color(rp_ASHD())
            translate([rp_rndr(seed * 11 + i * 23, -(L - 6) / 2, (L - 6) / 2), W / 2 - 3.5, 0.14])
                rp_slab(2.0, 0.8, 0.012);
}

// 围场混凝土坪：分格缝 + 排水缝
module rp_ground_paddock(L = 12, D = 10, seed = 0)
{
    color(rp_CONC()) rp_slab(L, D, 0.14);
    color(rp_CONCD())
    {
        for (i = [1 : floor(L / 3) - 1])
            translate([-L / 2 + i * 3, 0, 0.14]) rp_slab(0.06, D, 0.01);
        for (i = [1 : floor(D / 3) - 1])
            translate([0, -D / 2 + i * 3, 0.14]) rp_slab(L, 0.06, 0.01);
    }
    color(rp_METALD()) translate([0, -D / 2 + 1.2, 0.14]) rp_slab(L * 0.9, 0.18, 0.012);
}

// 草地 / 砂石缓冲区
module rp_ground_grass(L = 8, D = 7, seed = 0)
{
    color(rp_GRASSC()) rp_slab(L, D, 0.12);
    for (i = [0 : 2])
        color(rp_GRASSD())
            translate([rp_rndr(seed * 5 + i * 11, -(L - 2) / 2, (L - 2) / 2),
                       rp_rndr(seed * 9 + i * 7 + 2, -(D - 2) / 2, (D - 2) / 2), 0.12])
                rp_slab(1.4 + rp_rnd(seed + i, 3) * 0.6, 1.2 + rp_rnd(seed + i + 5, 3) * 0.5, 0.012);
}

module rp_ground_gravel(L = 8, D = 6, seed = 0)
{
    color(rp_GRAVEL()) rp_slab(L, D, 0.12);
    for (i = [0 : 3])
        color(rp_GRAVELD())
            translate([rp_rndr(seed * 3 + i * 13, -(L - 2) / 2, (L - 2) / 2),
                       rp_rndr(seed * 7 + i * 19 + 1, -(D - 2) / 2, (D - 2) / 2), 0.12])
                rp_slab(0.9 + rp_rnd(seed + i, 3) * 0.5, 0.7 + rp_rnd(seed + i + 3, 2) * 0.5, 0.012);
}

// ================= 建筑 =================

// P 房（单开间，可进入，带全套内饰；front=-y 面向维修通道）
// W8 x D10 x 两层。一层车库：工作台/轮胎架/工具车/气瓶/储物柜/顶灯/车位标线。
// 二层围场招待区：玻璃带 + 阳台；屋面：女儿墙 + 机组 + 队旗。
// seed 定车队色；car >= 0 时内置一辆 rp_veh_gt3(seed=car)；closed=true 卷帘门关闭。
module rp_bldg_garage(seed = 0, car = -1, closed = false)
{
    W = 8; D = 10; h1 = 3.6; h2 = 3.0; t = 0.3;
    tc = rp_team_c(seed);
    ta = rp_team_ac(seed);
    // 环氧地坪 + 车位标线
    color(rp_EPOXY()) rp_slab(W, D, 0.14);
    color(rp_MARKW())
    {
        for (sx = [-1, 1]) translate([sx * 1.15, -D / 2 + 3.4, 0.14]) rp_slab(0.12, 4.6, 0.012);
        translate([0, -D / 2 + 5.6, 0.14]) rp_slab(2.4, 0.12, 0.012);
    }
    color(tc) translate([0, -D / 2 + 0.5, 0.14]) rp_slab(W - 2 * t, 0.3, 0.012);   // 门口队色门槛带
    // 墙体（后墙 + 侧墙 两层通高）
    color(rp_PANEL())
    {
        translate([0, D / 2 - t / 2, (h1 + h2) / 2]) rp_boxc([W, t, h1 + h2]);
        for (sx = [-1, 1]) translate([sx * (W / 2 - t / 2), 0, (h1 + h2) / 2]) rp_boxc([t, D, h1 + h2]);
        for (sx = [-1, 1]) translate([sx * (W / 2 - t / 2), -D / 2 + t / 2, h1 / 2]) rp_boxc([t, t, h1]);
        translate([0, -D / 2 + t / 2, h1 - 0.3]) rp_boxc([W, t, 0.6]);              // 前楣
    }
    // 卷帘门：开启=卷于楣下；关闭=整面落下（横纹由拼色条表示）
    if (closed)
    {
        color(rp_METALD()) translate([0, -D / 2 + t + 0.06, (h1 - 0.6) / 2]) rp_boxc([W - 2 * t, 0.1, h1 - 0.6]);
        color(rp_METALC()) for (i = [0 : 5])
            translate([0, -D / 2 + t + 0.02, 0.5 + i * 0.45]) rp_boxc([W - 2 * t, 0.06, 0.06]);
    }
    else
    {
        color(rp_METALD()) translate([0, -D / 2 + t + 0.2, h1 - 0.32]) rp_boxc([W - 2 * t, 0.35, 0.55]);
        color(rp_METALC()) translate([0, -D / 2 + t + 0.2, h1 - 0.62]) rp_boxc([W - 2 * t, 0.3, 0.08]);
    }
    // 门楣队色带 + 号码牌
    color(tc) translate([0, -D / 2 - 0.02, h1 - 0.3]) rp_boxc([W, 0.08, 0.52]);
    color(rp_MARKW()) translate([0, -D / 2 - 0.07, h1 - 0.3]) rp_boxc([0.9, 0.04, 0.38]);
    color(rp_DARKC()) translate([0, -D / 2 - 0.1, h1 - 0.3]) rp_boxc([0.45, 0.02, 0.2]);
    // 二层玻璃带 + 竖梃 + 层间板
    color([0.30, 0.38, 0.44, 0.6]) translate([0, -D / 2 + 0.06, h1 + h2 / 2 - 0.1]) rp_boxc([W - 2 * t, 0.05, h2 - 0.7]);
    color(rp_METALD()) for (i = [0 : 4])
        translate([-W / 2 + 1.2 + i * (W - 2.4) / 4, -D / 2 + 0.03, h1 + h2 / 2 - 0.1]) rp_boxc([0.12, 0.1, h2 - 0.7]);
    color(tc) translate([0, -D / 2 + 0.02, h1 + h2 - 0.35]) rp_boxc([W - 2 * t, 0.08, 0.5]);  // 二层顶队色带
    color(rp_PANELD()) translate([0, 0, h1]) rp_boxc([W, D, 0.2]);                            // 一层顶板
    // 阳台 + 栏杆
    color(rp_CONCD()) translate([0, -D / 2 - 0.55, h1 + 0.05]) rp_boxc([W - 0.4, 1.1, 0.14]);
    color(rp_METALC())
    {
        translate([0, -D / 2 - 1.05, h1 + 0.55]) rp_boxc([W - 0.4, 0.06, 0.06]);
        for (i = [0 : 5]) translate([-W / 2 + 0.4 + i * (W - 0.8) / 5, -D / 2 - 1.05, h1 + 0.3]) rp_boxc([0.05, 0.05, 0.5]);
    }
    // 屋面：女儿墙 + 空调机组 + 队旗
    color(rp_PANELD()) translate([0, 0, h1 + h2]) difference()
    {
        rp_slab(W + 0.16, D + 0.16, 0.4);
        translate([0, 0, -0.05]) rp_slab(W - 0.4, D - 0.4, 0.6);
    }
    color(rp_METALC()) translate([W * 0.22, D * 0.15, h1 + h2]) rp_boxc([1.6, 1.1, 0.7]);
    color(rp_METALD()) translate([W * 0.22, D * 0.15, h1 + h2 + 0.72]) rp_boxc([1.7, 1.2, 0.06]);
    translate([-W * 0.32, D * 0.2, h1 + h2 + 0.4]) rp_prop_flag(seed = seed, h = 4.2);
    // ---- 室内（一层）----
    // 顶灯带 ×2
    color([0.78, 0.78, 0.74]) for (sy = [-1, 1])
        translate([0, sy * 1.6, h1 - 0.12]) rp_boxc([W - 2, 0.18, 0.06]);
    // 后墙：工作台（左）+ 轮胎架（右）
    translate([-W / 2 + 1.5, D / 2 - 0.75, 0.14]) rp_prop_workbench(seed = seed);
    translate([W / 2 - 1.7, D / 2 - 0.6, 0.14]) rotate([0, 0, 180]) rp_prop_tire_rack(seed = seed + 1);
    // 侧墙：储物柜（右）+ 工具车（左前）
    translate([W / 2 - 0.65, 0.8, 0.14]) rotate([0, 0, -90]) rp_prop_locker(seed = seed);
    translate([-W / 2 + 0.75, -1.6, 0.14]) rotate([0, 0, 90]) rp_prop_toolcart(seed = seed);
    // 气瓶 + 备件箱
    translate([W / 2 - 0.8, -D / 2 + 1.4, 0.14]) rp_prop_bottle(seed = seed);
    color(rp_METALD()) translate([-W / 2 + 0.9, 2.2, 0.5]) rp_boxc([1.2, 0.8, 0.7]);
    // 内置赛车
    if (car >= 0)
        translate([0, -D / 2 + 3.2, 0.14]) rotate([0, 0, -90]) rp_veh_gt3(seed = car);
}

// 控制塔：混凝土井筒 + 顶部玻璃控制室 + 挑檐 + 天线
module rp_bldg_control_tower(seed = 0, h = 13)
{
    tc = rp_team_c(seed);
    color(rp_CONC()) translate([0, 0, h / 2]) rp_boxc([3.6, 3.6, h]);
    color(rp_CONCD()) rp_slab(4.6, 4.6, 0.3);
    // 井筒竖窗缝
    color(rp_DARKC()) for (i = [0 : floor(h / 2.6) - 1])
        translate([0, -1.81, 1.8 + i * 2.6]) rp_boxc([0.5, 0.05, 1.3]);
    // 顶部控制室（放大玻璃盒）
    color(rp_PANEL()) translate([0, 0, h + 1.5]) rp_boxc([5.4, 5.0, 3.0]);
    color([0.30, 0.38, 0.44, 0.6]) translate([0, 0, h + 1.6]) rp_boxc([5.5, 5.1, 1.5]);
    color(rp_METALD()) for (i = [0 : 5], sy = [-1, 1])
        translate([-2.2 + i * 0.9, sy * 2.56, h + 1.6]) rp_boxc([0.1, 0.06, 1.5]);
    color(tc) translate([0, -2.54, h + 2.9]) rp_boxc([5.4, 0.06, 0.4]);          // 队色檐口带
    // 挑檐屋面
    color(rp_PANELD()) translate([0, 0, h + 3.1]) rp_boxc([6.4, 6.0, 0.25]);
    // 天线 + 风速杆
    color(rp_METALD()) translate([1.8, 1.4, h + 3.2]) cylinder(h = 3.2, r = 0.05, $fn = 6);
    color(rp_METALC()) translate([-1.8, 1.4, h + 3.9]) rp_boxc([0.5, 0.04, 0.3]);
    color(rp_DARKC()) translate([0, 0, h + 3.25]) rp_boxc([2.2, 1.6, 0.12]);      // 屋面设备
}

// 看台：钢混阶梯座椅 + 尾墙 + 悬挑顶棚。front=-y（面向赛道），底部 z=0
module rp_bldg_grandstand(L = 24, rows = 7, seed = 0)
{
    D = rows * 0.95 + 2.2;
    bh = 0.55 + rows * 0.52;                       // 末排椅面高
    // 阶梯 + 座椅
    for (r = [0 : rows - 1])
    {
        color(rp_CONC()) translate([0, -D / 2 + 1.3 + r * 0.95, 0.3 + r * 0.52 / 2 + r * 0.26])
            rp_boxc([L, 0.95, 0.6 + r * 0.52]);
        sc = rp_seat_c(seed + r);
        na = floor(L / 0.62);
        for (i = [0 : na - 1])
        {
            sx = -L / 2 + 0.5 + i * 0.62;
            if (abs(sx) > 1.0 && abs(abs(sx) - L / 4) > 0.9)      // 留两条走道
                color(rp_rnd(seed * 3 + r * 17 + i, 5) == 0 ? rp_seat_c(seed + r + 2) : sc)
                    translate([sx, -D / 2 + 1.15 + r * 0.95, 0.72 + r * 0.52]) rp_boxc([0.5, 0.4, 0.28]);
        }
    }
    // 前墙 + 尾墙 + 侧墙
    color(rp_CONCD())
    {
        translate([0, -D / 2 + 0.15, 0.45]) rp_boxc([L, 0.3, 0.9]);
        translate([0, D / 2 - 0.15, (bh + 1.0) / 2]) rp_boxc([L, 0.3, bh + 1.0]);
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.15), 0, (bh + 0.6) / 2]) rp_boxc([0.3, D, bh + 0.6]);
    }
    // 顶棚立柱（尾墙顶）
    color(rp_METALD()) for (i = [0 : 3])
        translate([-L / 2 + 1.2 + i * (L - 2.4) / 3, D / 2 - 0.6, bh + 1.0 + 1.6]) rp_boxc([0.22, 0.22, 3.2]);
    // 悬挑顶棚（前倾）
    color(rp_PANEL()) translate([0, -0.4, bh + 4.3]) rotate([-7, 0, 0]) rp_boxc([L + 0.6, D + 1.2, 0.16]);
    color(rp_METALD()) translate([0, -D / 2 - 0.9, bh + 3.6]) rp_boxc([L + 0.6, 0.14, 0.5]);  // 檐口梁
}

// 围场招待所（两层；玻璃立面 + 队色楣带 + 露台栏板 + 屋面机组）
module rp_bldg_hospitality(seed = 0, L = 12, D = 7)
{
    h1 = 3.2; h2 = 3.0;
    tc = rp_team_c(seed);
    color(rp_CONCD()) rp_slab(L + 0.3, D + 0.3, 0.18);
    color(rp_PANEL()) translate([0, 0, (h1 + h2) / 2 + 0.18]) rp_boxc([L, D, h1 + h2]);
    // 两层玻璃带（front=-y）
    color([0.30, 0.38, 0.44, 0.6]) for (f = [0, 1])
        translate([0, -D / 2 - 0.02, 0.18 + f * h1 + 1.55]) rp_boxc([L - 1.2, 0.06, 2.2]);
    color(rp_METALD()) for (f = [0, 1], i = [0 : 5])
        translate([-L / 2 + 1.0 + i * (L - 2.0) / 5, -D / 2 - 0.05, 0.18 + f * h1 + 1.55]) rp_boxc([0.1, 0.06, 2.2]);
    // 队色楣带 + 层间板线
    color(tc) translate([0, -D / 2 - 0.04, 0.18 + h1 + h2 - 0.35]) rp_boxc([L, 0.05, 0.5]);
    color(rp_PANELD()) translate([0, -D / 2 - 0.04, 0.18 + h1]) rp_boxc([L, 0.05, 0.24]);
    // 入口门（front）
    color(rp_METALD()) translate([-L * 0.3, -D / 2 - 0.06, 1.25]) rp_boxc([1.4, 0.08, 2.2]);
    // 二层露台 + 栏板
    color(rp_CONCD()) translate([0, -D / 2 - 0.7, 0.18 + h1]) rp_boxc([L - 1.0, 1.4, 0.16]);
    color(rp_METALC()) translate([0, -D / 2 - 1.35, 0.18 + h1 + 0.5]) rp_boxc([L - 1.0, 0.05, 0.05]);
    color(rp_METALC()) for (i = [0 : 6])
        translate([-L / 2 + 0.7 + i * (L - 1.4) / 6, -D / 2 - 1.35, 0.18 + h1 + 0.28]) rp_boxc([0.05, 0.05, 0.45]);
    // 屋面机组
    color(rp_METALC()) translate([L * 0.25, D * 0.1, 0.18 + h1 + h2]) rp_boxc([1.8, 1.2, 0.8]);
    color(rp_METALD()) translate([-L * 0.25, -D * 0.1, 0.18 + h1 + h2]) rp_boxc([1.2, 1.0, 0.5]);
}

// ================= 道具 / 设施 =================

// 维修墙（赛道侧设施；front=-y 朝维修通道）：混凝土矮墙 + 白压顶 + 上方护网 + 数据屏
module rp_prop_pitwall(len = 8, seed = 0)
{
    color(rp_CONC()) translate([0, 0, 0.5]) rp_boxc([len, 0.5, 1.0]);
    color(rp_MARKW()) translate([0, 0, 1.02]) rp_boxc([len, 0.56, 0.08]);
    // 护网（半透明）
    color([0.3, 0.32, 0.34, 0.5]) translate([0, 0, 2.0]) rp_boxc([len, 0.04, 1.9]);
    color(rp_METALD()) for (i = [0 : floor(len / 2.6)])
        translate([-len / 2 + 0.4 + i * 2.6, 0, 1.95]) rp_boxc([0.08, 0.08, 2.0]);
    // 数据屏（伸向维修通道侧）
    color(rp_METALD())
    {
        translate([-len / 4, -0.55, 2.3]) rp_boxc([0.08, 1.0, 0.08]);
        translate([-len / 4, -1.0, 2.15]) rp_boxc([1.1, 0.12, 0.7]);
    }
    color([0.14, 0.3, 0.34]) translate([-len / 4, -1.07, 2.15]) rp_boxc([0.95, 0.03, 0.55]);
}

// 赛道灯架（沿 y 跨越赛道，中心锚点）：双柱桁架 + 五红灯组 + 广告面
module rp_prop_gantry(L = 14, seed = 0)
{
    tc = rp_team_c(seed);
    color(rp_METALD()) for (sy = [-1, 1])
    {
        translate([0, sy * (L / 2), 3.0]) rp_boxc([0.35, 0.35, 6.0]);
        translate([0, sy * (L / 2), 0]) rp_slab(1.0, 1.0, 0.25);
    }
    // 桁架主梁（上下弦 + 斜腹）
    color(rp_METALD())
    {
        translate([0, 0, 6.0]) rotate([0, 0, 90]) rp_boxc([L, 0.3, 0.3]);
        translate([0, 0, 5.1]) rotate([0, 0, 90]) rp_boxc([L, 0.3, 0.3]);
        for (i = [0 : floor(L / 1.6) - 1])
            translate([0, -L / 2 + 0.8 + i * 1.6, 5.55]) rotate([i % 2 == 0 ? 38 : -38, 0, 0]) rp_boxc([0.12, 0.12, 1.2]);
    }
    // 广告面（面向来车 -x？面向 +y 维修区侧）
    color(tc) translate([0.18, 0, 5.55]) rp_boxc([0.06, L - 2, 0.7]);
    color(rp_MARKW()) for (i = [0 : 3])
        translate([0.22, -L / 4 + i * L / 6, 5.55]) rp_boxc([0.03, 0.8, 0.4]);
    // 五红灯组（挂在梁下，朝 -x 来车方向）
    for (i = [0 : 4])
    {
        color(rp_DARKC()) translate([-0.3, -1.0 + i * 0.5, 4.9]) rp_boxc([0.3, 0.34, 0.4]);
        color([0.5, 0.08, 0.06]) translate([-0.46, -1.0 + i * 0.5, 4.9]) rp_boxc([0.04, 0.22, 0.22]);
    }
}

// 高杆泛光灯
module rp_prop_floodlight(h = 14, seed = 0)
{
    color(rp_METALD())
    {
        cylinder(h = h, r1 = 0.28, r2 = 0.14, $fn = 8);
        rp_slab(1.1, 1.1, 0.25);
        translate([0, 0, h - 0.2]) rp_boxc([2.4, 0.2, 0.2]);
    }
    color(rp_METALC()) for (sx = [-1, 1], i = [0, 1])
        translate([sx * 0.9, -0.15 + i * 0.3, h - 0.55]) rotate([-20, 0, 0]) rp_boxc([0.7, 0.25, 0.45]);
    color([0.75, 0.75, 0.68]) for (sx = [-1, 1], i = [0, 1])
        translate([sx * 0.9, -0.28 + i * 0.3, h - 0.62]) rotate([-20, 0, 0]) rp_boxc([0.6, 0.03, 0.35]);
}

// 队旗/赛道旗
module rp_prop_flag(seed = 0, h = 6)
{
    color(rp_METALC()) cylinder(h = h, r = 0.05, $fn = 6);
    color(rp_team_c(seed)) translate([0.45, 0, h - 0.45]) rp_boxc([0.9, 0.03, 0.6]);
    color(rp_team_ac(seed)) translate([0.85, 0, h - 0.75]) rotate([0, 18, 0]) rp_boxc([0.5, 0.03, 0.3]);
}

// 记分塔：窄塔 + 名次灯牌
module rp_prop_pylon(seed = 0, h = 7)
{
    color(rp_DARKC()) translate([0, 0, h / 2]) rp_boxc([0.9, 0.6, h]);
    color(rp_CONCD()) rp_slab(1.3, 1.0, 0.2);
    for (i = [0 : 4])
    {
        color([0.62, 0.62, 0.58]) translate([0, -0.31, h - 1.0 - i * 1.05]) rp_boxc([0.7, 0.04, 0.8]);
        color(rp_DARKC()) translate([-0.14, -0.34, h - 1.0 - i * 1.05]) rp_boxc([0.28, 0.02, 0.5]);
        color(rp_team_c(seed + i)) translate([0.2, -0.34, h - 1.0 - i * 1.05]) rp_boxc([0.22, 0.02, 0.5]);
    }
}

// 锥桶
module rp_prop_cone(seed = 0)
{
    color([0.72, 0.28, 0.08])
    {
        rp_slab(0.34, 0.34, 0.04);
        translate([0, 0, 0.25]) cylinder(h = 0.5, r1 = 0.15, r2 = 0.05, $fn = 8);
    }
    color(rp_MARKW()) translate([0, 0, 0.32]) cylinder(h = 0.12, r1 = 0.115, r2 = 0.095, $fn = 8);
}

// 轮胎堆（平放叠码 3~4 条；part_tire 轴沿 y，需转 90° 使轴竖直）
module rp_prop_tire_stack(seed = 0, n = 3)
{
    for (i = [0 : n - 1])
        translate([0, 0, 0.14 + i * 0.27]) rotate([90, 0, 0]) rp_part_tire(0.34, 0.26);
    if (rp_rnd(seed, 3) == 0)
        color(rp_MARKW()) translate([0, 0, 0.14 + (n - 1) * 0.27 + 0.14]) rp_slab(0.5, 0.5, 0.03);
}

// 轮胎防撞墙（沿 x；红白相间条胎堆 + 固定带）
module rp_prop_tire_wall(len = 4, seed = 0)
{
    n = max(2, floor(len / 0.8));
    for (i = [0 : n - 1])
    {
        c = rp_rnd(seed + i, 2) == 0 ? rp_CURBR() : rp_CURBW();
        for (j = [0 : 1])
        {
            translate([-len / 2 + 0.4 + i * 0.8, 0, 0.34 + j * 0.67]) rp_part_tire(0.33, 0.3);
            color(c) translate([-len / 2 + 0.4 + i * 0.8, 0, 0.34 + j * 0.67]) rotate([90, 0, 0])
                difference()
                {
                    cylinder(h = 0.32, r = 0.345, center = true, $fn = 10);
                    cylinder(h = 0.36, r = 0.30, center = true, $fn = 10);
                }
        }
    }
}

// 波形护栏（沿 x）
module rp_prop_guardrail(len = 6, seed = 0)
{
    color(rp_METALD()) for (i = [0 : floor(len / 2)])
        translate([-len / 2 + 0.4 + i * 2, 0, 0.35]) rp_boxc([0.12, 0.12, 0.7]);
    color(rp_METALC()) translate([0, -0.06, 0.55]) rp_boxc([len, 0.06, 0.32]);
    color(rp_METALC()) translate([0, -0.06, 0.28]) rp_boxc([len, 0.06, 0.2]);
}

// 捕捉网围栏（沿 x；半透明网面）
module rp_prop_fence_catch(len = 6, h = 3.0, seed = 0)
{
    color(rp_METALD()) for (i = [0 : floor(len / 3)])
        translate([-len / 2 + 0.5 + i * 3, 0, h / 2]) rp_boxc([0.1, 0.1, h]);
    color([0.28, 0.30, 0.32, 0.5]) translate([0, 0, 0.5 + (h - 0.8) / 2]) rp_boxc([len, 0.03, h - 0.8]);
    color(rp_METALC()) translate([0, 0, h - 0.15]) rp_boxc([len, 0.05, 0.08]);
}

// 围栏广告布（沿 x，挂在围栏上的赞助商标识带）
module rp_prop_banner(len = 6, seed = 0)
{
    color(rp_team_c(seed)) translate([0, 0, 0.6]) rp_boxc([len, 0.03, 0.9]);
    color(rp_MARKW()) for (i = [0 : 3])
        translate([-len * 0.32 + i * len * 0.21, -0.02, 0.6]) rp_boxc([len * 0.12, 0.02, 0.5]);
}

// 滚动工具柜
module rp_prop_toolcart(seed = 0)
{
    color(rp_TOOLR())
    {
        translate([0, 0, 0.55]) rp_boxc([0.9, 0.55, 0.9]);
        translate([0, 0, 1.02]) rp_boxc([0.95, 0.6, 0.06]);
    }
    color(rp_METALD()) for (i = [0 : 3])
        translate([0, -0.28, 0.3 + i * 0.2]) rp_boxc([0.78, 0.03, 0.14]);
    color(rp_DARKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 0.35, sy * 0.2, 0.05]) sphere(r = 0.05, $fn = 6);
    color(rp_METALC()) translate([0.2, 0, 1.09]) rp_boxc([0.3, 0.4, 0.05]);
}

// 工作台 + 挂板
module rp_prop_workbench(seed = 0, L = 2.4)
{
    color(rp_METALD()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (L / 2 - 0.15), sy * 0.3, 0.45]) rp_boxc([0.08, 0.08, 0.9]);
    color([0.42, 0.32, 0.22]) translate([0, 0, 0.95]) rp_boxc([L, 0.7, 0.08]);
    color(rp_METALD()) translate([0, 0.32, 1.6]) rp_boxc([L, 0.06, 1.2]);
    // 挂板工具（剪影）
    color(rp_METALC()) for (i = [0 : 4])
        translate([-L / 2 + 0.4 + i * (L - 0.8) / 4, 0.28, 1.5 + rp_rnd(seed + i, 3) * 0.2])
            rp_boxc([0.1 + rp_rnd(seed + i + 3, 2) * 0.1, 0.04, 0.3]);
    // 台面色块（零件/纸盒）
    color(rp_TEAMJUNK(seed)) translate([-0.6, 0, 1.06]) rp_boxc([0.4, 0.3, 0.14]);
    color(rp_METALC()) translate([0.5, -0.1, 1.05]) rp_boxc([0.35, 0.25, 0.1]);
}
function rp_TEAMJUNK(i) = [[0.62, 0.5, 0.16], [0.2, 0.32, 0.46], [0.4, 0.4, 0.42], [0.55, 0.14, 0.1]][rp_rnd(i + 77, 4)];

// 轮胎架（车库用：两柱横档 + 横放胎 ×4）
module rp_prop_tire_rack(seed = 0, L = 2.4)
{
    color(rp_METALD())
    {
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.05), 0, 1.0]) rp_boxc([0.08, 0.5, 2.0]);
        for (z = [0.5, 1.4]) translate([0, 0, z]) rp_boxc([L, 0.08, 0.08]);
    }
    for (i = [0 : 2])
        translate([-L / 2 + 0.45 + i * 0.75, 0, 0.86]) rp_part_tire(0.32, 0.3);
    for (i = [0 : 2])
        translate([-L / 2 + 0.5 + i * 0.75, 0, 1.76]) rp_part_tire(0.32, 0.3);
}

// 储物柜排
module rp_prop_locker(seed = 0, n = 3)
{
    tc = rp_team_c(seed);
    for (i = [0 : n - 1])
    {
        color(tc) translate([-((n - 1) * 0.65) / 2 + i * 0.65, 0, 1.0]) rp_boxc([0.6, 0.5, 2.0]);
        color(rp_METALD()) translate([-((n - 1) * 0.65) / 2 + i * 0.65, -0.26, 1.0]) rp_boxc([0.5, 0.02, 1.8]);
        color(rp_MARKW()) translate([-((n - 1) * 0.65) / 2 + i * 0.65 + 0.18, -0.28, 1.1]) rp_boxc([0.05, 0.02, 0.25]);
    }
}

// 氮气瓶组（小车底座 + 双瓶）
module rp_prop_bottle(seed = 0)
{
    color(rp_METALD()) rp_slab(0.6, 0.4, 0.08);
    for (i = [0, 1])
    {
        color(i == 0 ? [0.2, 0.28, 0.4] : rp_METALC()) translate([-0.12 + i * 0.24, 0, 0.75]) cylinder(h = 1.35, r = 0.13, $fn = 8);
        color(rp_DARKC()) translate([-0.12 + i * 0.24, 0, 1.46]) cylinder(h = 0.08, r = 0.05, $fn = 6);
    }
}

// 双柱举升机（含托臂；可把车放 z=0.75 悬停）
module rp_prop_lift(seed = 0)
{
    color(rp_METALD()) for (sx = [-1, 1])
    {
        translate([sx * 1.5, 0, 1.3]) rp_boxc([0.25, 0.3, 2.6]);
        translate([sx * 1.5, 0, 0]) rp_slab(0.5, 0.5, 0.08);
    }
    color(rp_YELLOWC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 1.1, sy * 0.7, 0.72]) rotate([0, 0, sy * sx * 25]) rp_boxc([0.9, 0.12, 0.08]);
    color(rp_METALC()) translate([0, 0, 2.62]) rp_boxc([3.2, 0.15, 0.15]);
}

// 加油立架（围场加油区：井字架 + 横置油鼓 + 软管箱）
module rp_prop_fuel_rig(seed = 0)
{
    color(rp_METALD())
    {
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.5, sy * 0.5, 1.2]) rp_boxc([0.1, 0.1, 2.4]);
        translate([0, 0, 2.4]) rp_boxc([1.2, 1.2, 0.1]);
    }
    color([0.2, 0.33, 0.44]) translate([0, 0, 1.7]) rotate([0, 90, 0]) cylinder(h = 1.0, r = 0.4, $fn = 10);
    color(rp_DARKC()) translate([0, 0, 0.5]) rp_boxc([0.8, 0.6, 0.7]);
    color(rp_YELLOWC()) translate([0, 0, 0]) rp_slab(1.6, 1.6, 0.04);
}

// 柴油发电机
module rp_prop_generator(seed = 0)
{
    color(rp_YELLOWC()) translate([0, 0, 0.65]) rp_boxc([1.7, 0.9, 1.1]);
    color(rp_DARKC()) for (i = [0 : 3])
        translate([-0.6 + i * 0.25, -0.46, 0.7]) rp_boxc([0.15, 0.03, 0.6]);
    color(rp_METALD())
    {
        translate([0.6, 0.2, 1.35]) cylinder(h = 0.35, r = 0.06, $fn = 6);
        translate([0, 0, 0.08]) rp_slab(1.9, 1.1, 0.12);
    }
    color(rp_DARKC()) translate([-0.4, 0, 1.22]) rp_boxc([0.5, 0.5, 0.05]);
}

// 围场帐篷（四腿 + 攒尖布顶 + 围裙带）
module rp_prop_canopy(seed = 0, S = 3.2)
{
    tc = rp_team_c(seed);
    color(rp_METALC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (S / 2 - 0.15), sy * (S / 2 - 0.15), 1.1]) rp_boxc([0.07, 0.07, 2.2]);
    color(tc) translate([0, 0, 2.2]) rp_part_pyramid(S + 0.4, S + 0.4, 0.9, tc);
    color(tc) for (sy = [-1, 1])
        translate([0, sy * (S / 2 + 0.1), 2.05]) rp_boxc([S + 0.2, 0.04, 0.35]);
    color(rp_MARKW()) translate([0, -(S / 2 + 0.13), 2.05]) rp_boxc([S * 0.4, 0.02, 0.2]);
}

// 领奖台（三级 + 背板 + 旗杆 ×3）
module rp_prop_podium(seed = 0)
{
    color(rp_CONC())
    {
        translate([0, 0, 0.3]) rp_boxc([1.6, 1.4, 0.6]);
        translate([-1.6, 0, 0.2]) rp_boxc([1.6, 1.4, 0.4]);
        translate([1.6, 0, 0.1]) rp_boxc([1.6, 1.4, 0.2]);
    }
    color(rp_MARKW()) translate([0, -0.7, 0.62]) rp_boxc([1.2, 0.02, 0.12]);
    tc = rp_team_c(seed);
    color(rp_PANEL()) translate([0, 1.1, 1.6]) rp_boxc([5.6, 0.15, 2.4]);
    color(tc) translate([0, 1.0, 2.4]) rp_boxc([5.6, 0.04, 0.5]);
    color(rp_DARKC()) for (i = [0 : 2])
        translate([-1.6 + i * 1.6, 0.99, 1.5]) rp_boxc([1.0, 0.03, 0.7]);
    for (i = [0 : 2])
        translate([-2.2 + i * 2.2, 1.6, 0]) rp_prop_flag(seed = seed + i + 1, h = 5);
}

// 数据监视架（pit box 前指挥位：立柱 + 三屏）
module rp_prop_monitor(seed = 0)
{
    color(rp_METALD())
    {
        translate([0, 0, 1.0]) rp_boxc([0.1, 0.1, 2.0]);
        rp_slab(0.5, 0.5, 0.06);
        translate([0, -0.05, 1.85]) rp_boxc([1.5, 0.12, 0.85]);
    }
    for (i = [0 : 2])
    {
        color([0.14, 0.3, 0.34]) translate([-0.48 + i * 0.48, -0.12, 1.85]) rp_boxc([0.42, 0.03, 0.7]);
        color(rp_team_c(seed + i)) translate([-0.48 + i * 0.48, -0.14, 2.05]) rp_boxc([0.3, 0.02, 0.12]);
    }
}

// ================= 载具（车头朝 +x，底面 z=0） =================

// GT3 赛车：低趴宽体 + 大尾翼 + 前铲 + 扩散器 + 车队涂装（seed 定涂装与号码）
module rp_veh_gt3(seed = 0)
{
    c = rp_team_c(seed);
    a = rp_team_ac(seed);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 1.42, sy * 0.92, 0.33]) rp_veh_wheel(0.33, 0.32);
    // 底板/前铲/侧裙（碳纤维）
    color(rp_CARBON())
    {
        translate([0, 0, 0.17]) rp_boxc([4.4, 1.86, 0.1]);
        translate([2.3, 0, 0.13]) rp_boxc([0.55, 2.0, 0.07]);
        for (sy = [-1, 1]) translate([0, sy * 0.99, 0.22]) rp_boxc([2.6, 0.08, 0.14]);
        translate([-2.25, 0, 0.3]) rp_boxc([0.35, 1.7, 0.3]);                          // 扩散器
    }
    // 车身主色
    color(c)
    {
        translate([0, 0, 0.52]) rp_boxc([4.2, 1.84, 0.55]);                            // 下体
        translate([1.55, 0, 0.76]) rotate([0, -5, 0]) rp_boxc([1.5, 1.7, 0.18]);       // 机盖
        translate([-1.75, 0, 0.8]) rp_boxc([0.9, 1.8, 0.3]);                           // 尾部
        translate([-0.55, 0, 1.26]) rp_boxc([1.3, 1.35, 0.12]);                        // 车顶
        for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 1.42, sy * 0.9, 0.55]) rp_boxc([0.95, 0.2, 0.4]);          // 宽体轮眉
    }
    // 玻璃舱
    color(rp_GLASSC())
    {
        translate([-0.35, 0, 1.02]) rp_boxc([1.9, 1.5, 0.5]);
        translate([0.62, 0, 0.95]) rotate([0, -18, 0]) rp_boxc([0.5, 1.45, 0.4]);      // 风挡
    }
    // 饰条 + 号码牌
    color(a)
    {
        translate([0.9, 0, 0.85]) rp_boxc([2.2, 0.5, 0.05]);                           // 机盖条
        for (sy = [-1, 1]) translate([0.1, sy * 0.94, 0.62]) rp_boxc([1.8, 0.03, 0.22]); // 侧裙拉花
    }
    color(rp_MARKW()) translate([1.35, 0, 0.86]) rp_boxc([0.55, 0.55, 0.03]);
    color(rp_DARKC()) translate([1.35, 0, 0.88]) rp_boxc([0.3, 0.3, 0.03]);
    // 尾翼：立柱 + 主翼 + 端板
    color(rp_CARBON()) for (sy = [-1, 1])
    {
        translate([-1.9, sy * 0.5, 1.05]) rp_boxc([0.1, 0.1, 0.6]);
        translate([-2.0, sy * 0.92, 1.34]) rotate([0, 10, 0]) rp_boxc([0.5, 0.05, 0.3]);
    }
    color(c) translate([-2.0, 0, 1.34]) rotate([0, 10, 0]) rp_boxc([0.45, 1.8, 0.07]);
    // 后视镜 + 车顶进气
    color(c) for (sy = [-1, 1]) translate([0.5, sy * 0.99, 1.05]) rp_boxc([0.18, 0.08, 0.1]);
    color(rp_DARKC()) translate([-0.25, 0, 1.36]) rp_boxc([0.45, 0.32, 0.08]);
    // 前后灯
    color([0.8, 0.8, 0.72]) for (sy = [-1, 1]) translate([2.11, sy * 0.6, 0.62]) rp_boxc([0.06, 0.3, 0.14]);
    color([0.55, 0.08, 0.06]) for (sy = [-1, 1]) translate([-2.11, sy * 0.62, 0.66]) rp_boxc([0.06, 0.36, 0.14]);
    // 排气
    color(rp_METALC()) for (sy = [-1, 1]) translate([-2.28, sy * 0.3, 0.42]) rotate([0, 90, 0]) cylinder(h = 0.1, r = 0.06, $fn = 6);
}

// 安全车：四门跑车 + 车顶排灯
module rp_veh_safety_car(seed = 0)
{
    c = [0.58, 0.58, 0.6];
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 1.4, sy * 0.85, 0.32]) rp_veh_wheel(0.32, 0.26);
    color(c)
    {
        translate([0, 0, 0.55]) rp_boxc([4.4, 1.8, 0.6]);
        translate([-0.3, 0, 1.15]) rp_boxc([2.2, 1.65, 0.55]);
    }
    color(rp_GLASSC())
    {
        translate([-0.3, 0, 1.18]) rp_boxc([1.9, 1.7, 0.4]);
        translate([0.75, 0, 1.1]) rotate([0, -20, 0]) rp_boxc([0.45, 1.6, 0.42]);
    }
    // 涂装条纹 + 车顶灯排
    color([0.72, 0.28, 0.08]) for (sy = [-1, 1]) translate([0, sy * 0.91, 0.62]) rp_boxc([4.0, 0.03, 0.2]);
    color(rp_DARKC()) translate([-0.3, 0, 1.5]) rp_boxc([0.7, 1.0, 0.14]);
    color(rp_AMBER()) translate([-0.3, -0.28, 1.56]) rp_boxc([0.6, 0.2, 0.08]);
    color([0.2, 0.4, 0.6]) translate([-0.3, 0.28, 1.56]) rp_boxc([0.6, 0.2, 0.08]);
    color([0.8, 0.8, 0.72]) for (sy = [-1, 1]) translate([2.21, sy * 0.6, 0.66]) rp_boxc([0.06, 0.3, 0.14]);
    color([0.55, 0.08, 0.06]) for (sy = [-1, 1]) translate([-2.21, sy * 0.6, 0.66]) rp_boxc([0.06, 0.32, 0.14]);
}

// 车队厢式货车（Sprinter 式）
module rp_veh_van(seed = 0)
{
    c = rp_team_c(seed);
    color([0.62, 0.63, 0.65]) translate([-0.2, 0, 1.3]) rp_boxc([5.2, 2.0, 2.0]);
    color([0.62, 0.63, 0.65]) translate([2.3, 0, 1.05]) rotate([0, 12, 0]) rp_boxc([0.7, 1.95, 1.3]);
    color(rp_GLASSC()) translate([2.25, 0, 1.6]) rotate([0, 10, 0]) rp_boxc([0.5, 1.9, 0.55]);
    color(rp_GLASSC()) translate([1.6, -1.01, 1.6]) rp_boxc([0.7, 0.04, 0.5]);
    color(c) for (sy = [-1, 1]) translate([-0.3, sy * 1.01, 1.1]) rp_boxc([4.4, 0.02, 0.5]);  // 队色侧带
    color(rp_MARKW()) for (sy = [-1, 1]) translate([2.62, sy * 0.65, 0.85]) cylinder(h = 0.06, r = 0.1, $fn = 6);
    color(rp_METALD()) translate([-2.81, 0, 1.2]) rp_boxc([0.06, 1.8, 1.6]);                  // 尾门
    color(c) translate([-2.85, 0, 1.2]) rp_boxc([0.03, 1.2, 0.6]);
    for (sy = [-1, 1])
    {
        translate([1.7, sy * 0.95, 0.4]) rp_veh_wheel(0.4, 0.28);
        translate([-1.5, sy * 0.95, 0.4]) rp_veh_wheel(0.4, 0.28);
    }
}

// 车队运输卡车（牵引头 + 厢式挂车，队色条纹）
module rp_veh_hauler(seed = 0)
{
    c = rp_team_c(seed);
    // 牵引头
    color(c)
    {
        translate([5.6, 0, 1.5]) rp_boxc([2.0, 2.4, 2.2]);
        translate([6.35, 0, 1.0]) rp_boxc([0.8, 2.3, 0.9]);
    }
    color(rp_GLASSC()) translate([6.15, 0, 2.0]) rp_boxc([0.9, 2.3, 0.7]);
    color(rp_METALD()) translate([5.6, 0, 2.75]) rp_boxc([1.6, 2.0, 0.3]);                   // 导流罩
    // 挂车
    color([0.6, 0.61, 0.63]) translate([-1.4, 0, 2.0]) rp_boxc([9.6, 2.5, 3.0]);
    color(c) for (sy = [-1, 1]) translate([-1.4, sy * 1.26, 2.0]) rp_boxc([9.6, 0.02, 0.8]);
    color(rp_MARKW()) translate([-3.5, -1.27, 2.0]) rp_boxc([2.4, 0.02, 1.2]);
    color(rp_METALD()) translate([-6.21, 0, 2.0]) rp_boxc([0.06, 2.3, 2.8]);                 // 尾门
    color(rp_METALC()) translate([-6.25, 0, 0.35]) rp_boxc([0.3, 2.4, 0.5]);                 // 尾板
    // 车轮
    for (sy = [-1, 1])
    {
        translate([6.0, sy * 1.1, 0.45]) rp_veh_wheel(0.45, 0.3);
        translate([4.2, sy * 1.1, 0.45]) rp_veh_wheel(0.45, 0.3);
        translate([-3.6, sy * 1.15, 0.45]) rp_veh_wheel(0.45, 0.3);
        translate([-4.8, sy * 1.15, 0.45]) rp_veh_wheel(0.45, 0.3);
    }
}

// 围场牵引小车（平板行李拖车头）
module rp_veh_cart(seed = 0)
{
    color(rp_YELLOWC())
    {
        translate([0, 0, 0.45]) rp_boxc([1.6, 0.9, 0.35]);
        translate([-0.5, 0, 0.85]) rp_boxc([0.6, 0.8, 0.5]);
    }
    color(rp_DARKC()) translate([-0.5, 0, 1.15]) rp_boxc([0.5, 0.7, 0.12]);                  // 座椅
    color(rp_METALD()) translate([0.45, 0, 0.95]) rotate([0, -20, 0]) rp_boxc([0.08, 0.5, 0.5]);
    color(rp_GLASSC()) translate([0.72, 0, 0.62]) rp_boxc([0.05, 0.3, 0.12]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.55, sy * 0.42, 0.22]) rp_veh_wheel(0.22, 0.16);
}

// ================= 植被（围场绿化） =================

module rp_nature_tree(s = 1.0, seed = 0)
{
    color(rp_TRUNKC()) cylinder(h = 2.6 * s, r1 = 0.18 * s, r2 = 0.12 * s, $fn = 7);
    c1 = rp_LEAFC(); c2 = rp_LEAFD();
    color(c1) translate([0, 0, 3.4 * s]) scale([1, 1, 1.15]) sphere(r = 1.5 * s, $fn = 8);
    color(c2) translate([rp_rndr(seed, -0.6, 0.6) * s, rp_rndr(seed + 1, -0.6, 0.6) * s, 2.7 * s]) sphere(r = 1.0 * s, $fn = 7);
    color(c1) translate([rp_rndr(seed + 2, -0.8, 0.8) * s, rp_rndr(seed + 3, -0.8, 0.8) * s, 4.1 * s]) sphere(r = 0.8 * s, $fn = 7);
}

module rp_nature_bush(s = 1.0, seed = 0)
{
    color(rp_LEAFD()) translate([0, 0, 0.3 * s]) scale([1, 1, 0.75]) sphere(r = 0.6 * s, $fn = 7);
    color(rp_LEAFC()) translate([0.3 * s, 0.15 * s, 0.42 * s]) sphere(r = 0.4 * s, $fn = 6);
    color(rp_LEAFC()) translate([-0.25 * s, -0.1 * s, 0.38 * s]) sphere(r = 0.35 * s, $fn = 6);
}

module rp_nature_hedge(L = 4, seed = 0)
{
    color(rp_LEAFD()) translate([0, 0, 0.5]) rp_boxc([L, 0.7, 1.0]);
    color(rp_LEAFC()) translate([0, 0, 1.02]) rp_boxc([L - 0.2, 0.6, 0.1]);
}
