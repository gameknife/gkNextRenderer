// kit_road.scad —— 路网规则库（rd_ 前缀）
//
// 数据描述 → 几何。整张路网由一张表描述，贴地的数学**集中在 rd_ribbon 一处**，
// 后续统一的路面修饰（标线 / 路缘 / 斑马线 / 井盖）只需在这里加模块，
// 不必回到生成器里改逐段坐标。
//
// 与 kit_layout / kit_terrain 同属"规则库"，不是可浏览零件：所有模块都需要
// 地形句柄或网络表才能出几何，KitCatalog 因此跳过本文件。
//
// ============================ 网络描述 ============================
//
// 生成器负责**拓扑**（在 tools/gnb/internal/geo/roadnet.go）：按 OSM 节点 id 找
// 路口、把路切成 run、从路口往回缩、mitre 出左右缘。本库负责**几何**。
//
//   net       = [ run, ... ]
//   run       = [ L, R ]        // 左右缘点列，等长，沿行进方向
//   junctions = [ ring, ... ]   // 路口面片轮廓（凸包）
//
//   rd_network(TERR, rd_ASPHALT(), net, junctions);
//
// ============================ 贴地契约 ============================
//
// 别改这四条，每一条都是在香港 tile 上实测踩出来的：
//
// 1. **一条 run 一个 polyhedron，不是一段一个盒子**。首版是逐段 cube，段与段
//    之间靠重叠遮缝，路口靠圆盘补丁 —— 拓扑上是一串独立实体，斜坡上会散架。
//    现在整条 run 是一张连通的带：站位 i 的左右缘共用同一个高度，站位之间共用
//    同一条边，没有缝也没有重叠。
//
// 2. **站位间距必须是地形格量级**。带在站位之间线性插值，站距决定了它能在两次
//    采样之间陷进多深。生成器按 stationStepM=5m 加密（roadnet.go）——注意这一步
//    容易在重构时丢掉：改成 ribbon 后曾经直接用 DP 简化后的顶点，长直段又变成
//    几十米一跨，路面重新沉进地里。
//    早先用水平板 + 段中点高度：87% 的路段有一端脱离地面，最糟的一段
//    （中環灣仔繞道，108m 长）偏 16.7m —— 一头埋进山里一头飘在半空。
//
// 3. **必须取左右缘，不能只取中心线**。这条最反直觉：只沿中心线定高时，把
//    rd_LIFT() 从 0.1m 加到 0.4m 都没用，路面依旧碎成一段段。原因是横坡——
//    香港的街道是切在山坡上的，16m 宽的路在横坡上左右缘能差 1.6m，抬升永远
//    追不上。取左右缘、锚在**上坡侧**（`max`），左右共用该高度使路面横向水平，
//    再用 rd_DEEP() 的裙边把下坡侧填到地里：这正是山地街道的真实做法（上挖下填）。
//    rd_LIFT() 需要 0.35m 这么大，是因为地形网格顶点带 XY 抖动：`gk_terrain_height`
//    在站位处的取值与站位之间的实际网格面存在不随采样加密而减小的偏差。实测把站距
//    从 5m 减到 2.5m 几乎没用，把抬升从 0.08m 加到 0.35m 才让路面完整。
//
// 4. **rd_network 必须包在 gk_flatten() 里**。见文件末尾。
//
// 路口面片比路面高 rd_JOINT_LIFT()：重叠处绝不能共面，PT 下共面重叠会 alias 出脏面。
//
// 诊断手法（下次再出问题照做）：把 rd_network 里的 color(c) 临时改成亮红，
// 再把 rd_LIFT() 调到 3m。红色能立刻分辨"几何在不在、拓扑对不对"，
// 大抬升能立刻分辨"是净空不够还是几何算错"。
//
// polyhedron 的面按 OpenSCAD 约定绕行：**从实体外面看是顺时针**。

// ============================ 参数 ============================

function rd_LIFT() = 0.35;       // 路面高出上坡侧地面（见契约第 3 条）
function rd_DEEP() = 4.0;        // 路面向下延伸（横坡上要够深，否则下坡侧悬空）
function rd_JOINT_LIFT() = 0.03; // 路口面片再高出路面

function rd_ASPHALT() = [0.09, 0.09, 0.10];
function rd_SERVICE() = [0.14, 0.14, 0.14];
function rd_LINEC()   = [0.62, 0.60, 0.52];

function rd_LINE_MIN_W() = 9.0;  // 窄于此宽度不画中线
function rd_DASH() = 4.0;        // 虚线段长
function rd_GAP() = 6.0;         // 虚线间隔
function rd_LINE_W() = 0.24;

// ---- 街面装饰（人行道 / 路灯 / 斑马线 / 信号灯）----
//
// 只有车行道的城市是一张地图；人行道那 0.16m 的台阶和一排路灯，
// 才是"街"和"路网数据"的区别。全部由生成器已经发出的左右缘点列推导，
// 生成器**不需要**再多发一个坐标。

function rd_WALK_W() = 2.4;      // 人行道宽（含路缘）
function rd_WALK_H() = 0.16;     // 人行道高出车行道
function rd_KERB_W() = 0.34;     // 路缘石宽
// 人行道与自然地面的最大高差。超过就整条 run 不铺人行道 —— 见 rd_sidewalk。
function rd_WALK_MAX_OFF() = 0.9;
function rd_WALKC() = [0.30, 0.29, 0.28];
function rd_KERBC() = [0.39, 0.38, 0.35];
function rd_ZEBRAC() = [0.60, 0.59, 0.54];
function rd_POLEC() = [0.20, 0.21, 0.22];
function rd_LAMPC() = [0.44, 0.42, 0.37];
function rd_TRUNKC() = [0.19, 0.14, 0.10];
function rd_LEAFC() = [0.13, 0.20, 0.09];
function rd_LEAFD() = [0.11, 0.17, 0.08];
function rd_PROP_STEP() = 6;     // 每 N 个站位放一件（站距 5m => 30m）
function rd_ZEBRA_MIN_W() = 9.0; // 窄于此宽度不画斑马线
function rd_JUNC_LIGHT_M() = 16; // 路口外接方短于此不配信号灯

// 街具序列：灯为主，间杂行道树与小件。定死一张表而不是纯随机，
// 是为了让"每隔一根灯有一棵树"这种节奏稳定出现。
function rd_PROP_SEQ(i) = [0, 1, 0, 3, 0, 1, 0, 4, 0, 1, 0, 5][i % 12];

function rd_rnd01(s) = (((abs(s) * 1103515245 + 12345) % 2097152) / 2097152);
function rd_unit(v) = let (l = norm(v)) l < 1e-9 ? [0, 0] : [v[0] / l, v[1] / l];
function rd_lerp2(a, b, u) = [a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u];

// ============================ 基元 ============================

// 站位高度：左右缘取 max（上挖下填），左右共用 => 路面横向水平。
function rd_station_h(t, l, r, lift) =
    max(gk_terrain_height(t, l[0], l[1]), gk_terrain_height(t, r[0], r[1]))
    + rd_LIFT() + lift;

// 一条连续的路面带。L / R 等长，沿行进方向；输出一个封闭 polyhedron：
// 顶面 + 底面 + 左右侧面 + 两端封口。
//
// 顶点索引：顶面 L = 0..n-1，顶面 R = n..2n-1，底面 L = 2n..3n-1，底面 R = 3n..4n-1。
module rd_ribbon(t, L, R, lift = 0, deep = 0)
{
    if (len(L) >= 2)
        rd_ribbon_h(L, R, [for (i = [0 : len(L) - 1]) rd_station_h(t, L[i], R[i], lift)], deep);
}

// 站位高度由外部给定的带。人行道要的正是这个：它必须和相邻车行道**共用同一串
// 高度**，自己去采样地形的话，横坡上外缘那一侧会算出另一个值，路缘就错台了。
module rd_ribbon_h(L, R, hs, deep = 0) rd_ribbon_h2(L, R, hs, hs, deep);

// 左右缘各自一串高度的带（横向不再水平）。人行道外缘要落回自然地面，
// 靠的就是它：内缘锚在车行道上，外缘锚在地形上。
module rd_ribbon_h2(L, R, hl, hr, deep = 0)
{
    n = len(L);
    if (n >= 2)
    {
        d = deep > 0 ? deep : rd_DEEP();
        pts = concat(
            [for (i = [0 : n - 1]) [L[i][0], L[i][1], hl[i]]],
            [for (i = [0 : n - 1]) [R[i][0], R[i][1], hr[i]]],
            [for (i = [0 : n - 1]) [L[i][0], L[i][1], hl[i] - d]],
            [for (i = [0 : n - 1]) [R[i][0], R[i][1], hr[i] - d]]);
        faces = concat(
            // 顶面：外部在 +z，故从上往下看要顺时针 => L(i) L(i+1) R(i+1) R(i)
            [for (i = [0 : n - 2]) [i, i + 1, n + i + 1, n + i]],
            // 底面：反绕
            [for (i = [0 : n - 2]) [2 * n + i, 3 * n + i, 3 * n + i + 1, 2 * n + i + 1]],
            // 左侧面（外部朝左）
            [for (i = [0 : n - 2]) [i, 2 * n + i, 2 * n + i + 1, i + 1]],
            // 右侧面（外部朝右）
            [for (i = [0 : n - 2]) [n + i, n + i + 1, 3 * n + i + 1, 3 * n + i]],
            // 两端封口
            [[0, n, 3 * n, 2 * n],
             [n - 1, 3 * n - 1, 4 * n - 1, 2 * n - 1]]);
        polyhedron(points = pts, faces = faces);
    }
}

// 路口面片：一块贴地的凸多边形，锚在轮廓上最高的那个角，比路面略高。
// 各进口已经在生成器里从路口往回缩过，所以这块面片正好补上中间的洞。
module rd_patch(t, ring, lift = 0, deep = 0)
{
    n = len(ring);
    if (n >= 3)
    {
        d = deep > 0 ? deep : rd_DEEP();
        // 整块面片取单一高度：路口本来就是一块平的铺装。
        h = max([for (i = [0 : n - 1]) gk_terrain_height(t, ring[i][0], ring[i][1])])
            + rd_LIFT() + rd_JOINT_LIFT() + lift;
        pts = concat(
            [for (i = [0 : n - 1]) [ring[i][0], ring[i][1], h]],
            [for (i = [0 : n - 1]) [ring[i][0], ring[i][1], h - d]]);
        // ring 是逆时针凸包 => 顶面从上看要顺时针，取反序。
        faces = concat(
            [[for (i = [n - 1 : -1 : 0]) i]],
            [[for (i = [0 : n - 1]) n + i]],
            [for (i = [0 : n - 1])
                let (j = (i + 1) % n) [i, n + i, n + j, j]]);
        polyhedron(points = pts, faces = faces);
    }
}

// ============================ 修饰（扩展点） ============================
//
// 修饰件复用 rd_station_h 的定高、比路面高一点点、颜色自带。
// 新增修饰只要写一个这样的模块，再挂进 rd_network 即可。

// 车道中线（虚线）。沿左右缘的中点走，所以它自动跟着 mitre 后的路形。
module rd_centerline(t, L, R, w)
{
    n = len(L);
    if (w >= rd_LINE_MIN_W() && n >= 2)
        color(rd_LINEC())
            for (i = [0 : n - 2])
                let (a = [(L[i][0] + R[i][0]) / 2, (L[i][1] + R[i][1]) / 2],
                     b = [(L[i + 1][0] + R[i + 1][0]) / 2, (L[i + 1][1] + R[i + 1][1]) / 2],
                     seg = norm([b[0] - a[0], b[1] - a[1]]),
                     period = rd_DASH() + rd_GAP(),
                     k = floor(seg / period))
                    if (k >= 1)
                        for (j = [0 : k - 1])
                            let (t0 = (j * period) / seg,
                                 t1 = (j * period + rd_DASH()) / seg,
                                 p0 = [a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0],
                                 p1 = [a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1],
                                 nx = -(p1[1] - p0[1]) / rd_DASH() * rd_LINE_W() / 2,
                                 ny =  (p1[0] - p0[0]) / rd_DASH() * rd_LINE_W() / 2)
                                rd_ribbon(t,
                                          [[p0[0] + nx, p0[1] + ny], [p1[0] + nx, p1[1] + ny]],
                                          [[p0[0] - nx, p0[1] - ny], [p1[0] - nx, p1[1] - ny]],
                                          rd_JOINT_LIFT() + 0.02,
                                          rd_DEEP() + rd_JOINT_LIFT() + 0.02);
}

// ============================ 街面装饰 ============================

// 人行道 + 路缘石。两条独立的带并排放（不是一块板压另一块板）：
// 共面重叠在 PT 下会 alias 出脏面，错开成两条各自封闭的实体就没有这个问题。
// 路缘那一侧的高度来自车行道的 rd_station_h，所以台阶始终是 rd_WALK_H() 那么高。
//
// **外缘必须落回自然地面**（下面的 hlo / hro）。首版整条人行道都用车行道的高度，
// 在香港这种坡地上立刻出事：路面已经锚在上坡侧（kit 头部契约第 3 条），再往外
// 2.7m 就伸到了没被路面算子压平的自然地形上，下坡侧于是变成一道悬空的坎——
// 最深处接近 rd_DEEP()。视觉上是一条飘在半空的板，功能上更糟：NavGrid 判定
// 一格能不能走要看和邻格的高差，这道坎把街道和周边地面整个切开，
// Test_GeoCityWalkable 的两点之间**找不到路**。
//
// 取 min(路面高, 地形高 + 台阶) 就是"上挖下填"的另一半：上坡侧仍然平着切进山体，
// 下坡侧顺着地面斜下去。
//
// **横坡太大的 run 干脆不铺**（rd_WALK_MAX_OFF）。这条不是美学取舍，是实测：
// 路面锚在上坡侧（契约第 3 条），人行道再往外伸两米就悬在自然地面之上，
// 那条斜面在香港的山街上轻易超过 NavGrid 的 maxSlopeAngle（50°），
// 于是整条街被自己的人行道围成一道走不过去的峡谷 —— 可达格从 22264 掉到 16995，
// `Test_GeoCityWalkable` 的两点之间找不到路。收窄没用（越窄越陡），
// 只能在横坡大的地方放弃人行道 —— 现实里那种地方也是挡土墙加台阶，不是人行道。
// 这条 run 的两侧地面是否和路面足够贴合，值不值得铺人行道。
function rd_walk_fits(t, L, R, w) =
    max([for (i = [0 : len(L) - 1])
             let (dr = rd_unit([R[i][0] - L[i][0], R[i][1] - L[i][1]]),
                  hw = rd_station_h(t, L[i], R[i], rd_WALK_H()))
                 max(abs(gk_terrain_height(t, L[i][0] - dr[0] * w, L[i][1] - dr[1] * w)
                         + rd_WALK_H() - hw),
                     abs(gk_terrain_height(t, R[i][0] + dr[0] * w, R[i][1] + dr[1] * w)
                         + rd_WALK_H() - hw))])
    <= rd_WALK_MAX_OFF();

module rd_sidewalk(t, L, R, w)
{
    n = len(L);
    if (n >= 2 && w > rd_KERB_W() + 0.2 && rd_walk_fits(t, L, R, w))
    {
        hw = [for (i = [0 : n - 1]) rd_station_h(t, L[i], R[i], rd_WALK_H())];
        hk = [for (i = [0 : n - 1]) hw[i] + 0.02];
        dr = [for (i = [0 : n - 1]) rd_unit([R[i][0] - L[i][0], R[i][1] - L[i][1]])];
        // 左缘往外是 -dr，右缘往外是 +dr
        LK = [for (i = [0 : n - 1]) [L[i][0] - dr[i][0] * rd_KERB_W(), L[i][1] - dr[i][1] * rd_KERB_W()]];
        RK = [for (i = [0 : n - 1]) [R[i][0] + dr[i][0] * rd_KERB_W(), R[i][1] + dr[i][1] * rd_KERB_W()]];
        LO = [for (i = [0 : n - 1]) [L[i][0] - dr[i][0] * w, L[i][1] - dr[i][1] * w]];
        RO = [for (i = [0 : n - 1]) [R[i][0] + dr[i][0] * w, R[i][1] + dr[i][1] * w]];
        hlo = [for (i = [0 : n - 1])
                   min(hw[i], gk_terrain_height(t, LO[i][0], LO[i][1]) + rd_WALK_H())];
        hro = [for (i = [0 : n - 1])
                   min(hw[i], gk_terrain_height(t, RO[i][0], RO[i][1]) + rd_WALK_H())];
        color(rd_KERBC()) { rd_ribbon_h(LK, L, hk); rd_ribbon_h(R, RK, hk); }
        color(rd_WALKC())
        {
            rd_ribbon_h2(LO, LK, hlo, hw);
            rd_ribbon_h2(RK, RO, hw, hro);
        }
    }
}

// 斑马线：在 run 的端头（= 路口跟前）横铺几条。生成器已经把每条 run 从路口
// 往回缩过，所以端头正好是人该过街的位置。
module rd_crosswalk(t, L, R, w, atStart)
{
    n = len(L);
    if (w >= rd_ZEBRA_MIN_W() && n >= 3)
    {
        i0 = atStart ? 0 : n - 2;
        i1 = i0 + 1;
        color(rd_ZEBRAC())
            for (k = [0 : 4])
                let (u0 = (k + 0.15) / 5, u1 = (k + 0.72) / 5,
                     La = rd_lerp2(L[i0], L[i1], u0), Lb = rd_lerp2(L[i0], L[i1], u1),
                     Ra = rd_lerp2(R[i0], R[i1], u0), Rb = rd_lerp2(R[i0], R[i1], u1),
                     // 两端各留出路缘边距，条纹不要压到路肩上
                     Aa = rd_lerp2(La, Ra, 0.07), Ab = rd_lerp2(Lb, Rb, 0.07),
                     Ba = rd_lerp2(La, Ra, 0.93), Bb = rd_lerp2(Lb, Rb, 0.93),
                     h0 = rd_station_h(t, L[i0], R[i0], rd_JOINT_LIFT() + 0.02),
                     h1 = rd_station_h(t, L[i1], R[i1], rd_JOINT_LIFT() + 0.02))
                    rd_ribbon_h([Aa, Ab], [Ba, Bb],
                                [h0 + (h1 - h0) * u0, h0 + (h1 - h0) * u1],
                                rd_DEEP() + 0.05);
    }
}

// ---- 街具本体。局部 +x 一律朝向路面，摆放模块负责转到位。----

module rd_lamp(h = 8.2)
{
    color(rd_POLEC())
    {
        cylinder(h = h, r = 0.11, $fn = 6);
        translate([0, 0, h - 0.55]) rotate([0, 62, 0]) cylinder(h = 1.7, r = 0.085, $fn = 5);
    }
    color(rd_LAMPC()) translate([1.42, 0, h - 0.02]) cube([0.78, 0.30, 0.15], center = true);
}

module rd_street_tree(s = 1.0)
{
    color(rd_TRUNKC()) cylinder(h = 2.4 * s, r = 0.17 * s, $fn = 5);
    color(rd_LEAFC()) translate([0, 0, 3.3 * s]) sphere(r = 1.55 * s, $fn = 6);
    color(rd_LEAFD()) translate([0.5 * s, 0.35 * s, 4.1 * s]) sphere(r = 0.95 * s, $fn = 5);
}

module rd_bench()
{
    color(rd_POLEC()) for (y = [-0.7, 0.7]) translate([0, y, 0.22]) cube([0.5, 0.12, 0.44], center = true);
    color([0.28, 0.22, 0.16])
    {
        translate([0, 0, 0.46]) cube([0.55, 1.8, 0.09], center = true);
        translate([-0.24, 0, 0.72]) cube([0.09, 1.8, 0.45], center = true);
    }
}

module rd_bin()
{
    color([0.20, 0.25, 0.22]) cylinder(h = 0.95, r = 0.28, $fn = 6);
    color(rd_POLEC()) translate([0, 0, 0.95]) cylinder(h = 0.07, r = 0.31, $fn = 6);
}

module rd_hydrant()
{
    color([0.34, 0.14, 0.12])
    {
        cylinder(h = 0.66, r = 0.13, $fn = 6);
        translate([0, 0, 0.66]) sphere(r = 0.14, $fn = 5);
        translate([0, 0, 0.44]) cube([0.44, 0.13, 0.13], center = true);
    }
}

module rd_traffic_light()
{
    color(rd_POLEC())
    {
        cylinder(h = 3.4, r = 0.09, $fn = 6);
        translate([0, 0, 3.4]) rotate([0, 70, 0]) cylinder(h = 1.3, r = 0.075, $fn = 5);
    }
    color([0.16, 0.17, 0.18]) translate([1.15, 0, 3.42]) cube([0.28, 0.34, 0.92], center = true);
    color([0.42, 0.15, 0.12]) translate([1.30, 0, 3.72]) sphere(r = 0.10, $fn = 5);
    color([0.44, 0.38, 0.14]) translate([1.30, 0, 3.44]) sphere(r = 0.10, $fn = 5);
    color([0.16, 0.40, 0.20]) translate([1.30, 0, 3.16]) sphere(r = 0.10, $fn = 5);
}

module rd_street_prop(v, s)
{
    if (v == 0) rd_lamp(7.6 + rd_rnd01(s) * 1.5);
    if (v == 1) rd_street_tree(0.85 + rd_rnd01(s + 31) * 0.45);
    if (v == 3) rd_bench();
    if (v == 4) rd_bin();
    if (v == 5) rd_hydrant();
}

// 沿人行道摆街具，左右交替。位置来自站位索引而不是随机采样：
// 同一条 run 重跑两次必须一模一样，站距 5m 的站位表就是天然的确定性网格。
module rd_props(t, L, R, w, seed)
{
    n = len(L);
    step = rd_PROP_STEP();
    if (n >= step + 2)
        for (i = [step : step : n - 2])
            let (k = floor(i / step),
                 dr = rd_unit([R[i][0] - L[i][0], R[i][1] - L[i][1]]),
                 sgn = (k % 2) == 0 ? -1 : 1,
                 base = (k % 2) == 0 ? L[i] : R[i],
                 px = base[0] + dr[0] * sgn * w * 0.58,
                 py = base[1] + dr[1] * sgn * w * 0.58,
                 // 人行道外缘是斜下去的（见 rd_sidewalk），所以街具也得跟着落，
                 // 否则下坡侧的灯杆会悬空半米。
                 z = min(rd_station_h(t, L[i], R[i], rd_WALK_H()),
                         gk_terrain_height(t, px, py) + rd_WALK_H()))
                translate([px, py, z])
                    rotate([0, 0, atan2(-sgn * dr[1], -sgn * dr[0])])
                        rd_street_prop(rd_PROP_SEQ(k + seed), seed * 13 + i);
}

// 路口信号灯。小路口不配：三岔小巷插四根杆子比没有还假。
module rd_junction_props(t, ring, seed)
{
    n = len(ring);
    if (n >= 3)
    {
        xs = [for (p = ring) p[0]];
        ys = [for (p = ring) p[1]];
        ext = max(max(xs) - min(xs), max(ys) - min(ys));
        if (ext >= rd_JUNC_LIGHT_M())
        {
            cx = (max(xs) + min(xs)) / 2;
            cy = (max(ys) + min(ys)) / 2;
            z = max([for (p = ring) gk_terrain_height(t, p[0], p[1])]) + rd_LIFT();
            stride = max(1, floor(n / 4));
            for (i = [0 : stride : n - 1])
                let (p = ring[i], u = rd_unit([p[0] - cx, p[1] - cy]))
                    translate([p[0] + u[0] * 1.1, p[1] + u[1] * 1.1, z])
                        rotate([0, 0, atan2(-u[1], -u[0])]) rd_traffic_light();
        }
    }
}

// ============================ 入口 ============================

// 整张网络。
//   net       = [[L, R], ...]   已 mitre 的左右缘
//   junctions = [ring, ...]     路口凸包
//   widths    = [w, ...]        与 net 等长，决定画不画中线 / 斑马线
//   sidewalks / props           街面装饰开关（背街小巷不开）
//   caps      = [[头, 尾], ...] 与 net 等长的端头掩码；空 = 两端都封。
//               一条 run 的端头默认画人行横道，因为那是街的尽头。但**被地块接缝
//               切断的 run 不是街的尽头** —— 路在下一块里继续，两侧各画一道就成了
//               马路正中间的两条斑马线。生成器按端点是否落在内部接缝上决定
//               （geo emit.go 的 markSeamCaps）。
//
// gk_flatten() 是必须的：本库把一张表展开成上千个模块调用，而**每个 user module
// 调用都会变成一个场景 Node**（=一个 Model + 一个碰撞体）。实测 1km 香港 tile 不加
// 它是 7683 个节点，光物理 shape cooking 就 1.2 秒；加上之后 90 个节点、34 毫秒。
// 几何合并到调用方那一层，分块仍由生成器控制（每块远低于 65535 三角的 Model 上限，
// 超了引擎会**静默跳过整块的碰撞体** —— 加了街面装饰后每条 run 贵了三倍，
// 所以生成器改成按三角预算切块，不再按固定条数）。
module rd_network(t, c, net, junctions = [], widths = [], markings = true,
                  sidewalks = false, props = false, seed = 0, caps = [])
{
    gk_flatten()
    {
        if (len(net) > 0)
        {
            color(c)
                for (k = [0 : len(net) - 1])
                    rd_ribbon(t, net[k][0], net[k][1]);

            if (sidewalks)
                for (k = [0 : len(net) - 1])
                    rd_sidewalk(t, net[k][0], net[k][1], rd_WALK_W());

            if (markings && len(widths) > 0)
                for (k = [0 : len(net) - 1])
                {
                    rd_centerline(t, net[k][0], net[k][1], widths[k]);
                    if (len(caps) == 0 || caps[k][0] > 0)
                        rd_crosswalk(t, net[k][0], net[k][1], widths[k], true);
                    if (len(caps) == 0 || caps[k][1] > 0)
                        rd_crosswalk(t, net[k][0], net[k][1], widths[k], false);
                }

            if (props)
                for (k = [0 : len(net) - 1])
                    rd_props(t, net[k][0], net[k][1], rd_WALK_W(), seed + k);
        }

        if (len(junctions) > 0)
        {
            color(c)
                for (k = [0 : len(junctions) - 1])
                    rd_patch(t, junctions[k]);

            if (props)
                for (k = [0 : len(junctions) - 1])
                    rd_junction_props(t, junctions[k], seed + k);
        }
    }
}
