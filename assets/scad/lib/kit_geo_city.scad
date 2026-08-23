// kit_geo_city.scad —— 真实城市 tile 的建筑细节规则库（gc_ 前缀）
//
// `gnb geo` 从 OSM 直出的建筑是**一个挤出棱柱**：轮廓对、高度对，但没有一处
// 细节，整片城市读起来像地图软件而不是场景。本库补的就是那一层：幕墙/窗格、
// 勒脚与檐口、平屋顶女儿墙与屋面杂物、低矮房屋的坡屋顶。
//
// 与 kit_road 同属"规则库"，不是可浏览零件：所有模块都要生成器给的轮廓/包围盒
// 才能出几何，KitCatalog 因此跳过本文件。
//
// ============================ 分工 ============================
//
// 生成器（tools/gnb/internal/geo/detail.go）负责**分类与量测**：
//   - 按高度 / building=* / 地域 profile 选立面与屋顶方案
//   - 轮廓的最小面积包围盒（旋转卡壳）与矩形度 —— 坡屋顶要靠它定脊向
//   - 每栋的确定性 seed
// 本库负责**几何**。要统一改全世界所有 tile 的立面，改这一个文件。
//
//   gc_bld(pts, z, h, fac, roof, obb, paths);
//     pts   轮廓点（含洞时是拍平后的点表，配合 paths）
//     z     地面高（生成器在调用点用 gz(...) 采样地形）
//     h     墙高
//     fac   [facadeIdx, wallTone, glassTone, floorH, seed]
//     roof  [kind, tone, rise, ridgeFrac, clutter, anchorX, anchorY, anchorR]
//     obb   [cx, cy, w, d, angDeg]   w >= d，w 为脊向
//     skirt 壳体往地下多拉的一截（防坡地露底）
//     paths 有洞时的 polygon paths，无洞传 []
//
// ============================ 三条硬约束 ============================
//
// 1. **不用 difference()**。女儿墙、窗洞用 CSG 挖是最直观的写法，但 Manifold
//    对一片 tile 上千栋建筑做 boolean 会把加载时间打穿。全库只用加法：
//    实体带（slab）、贴边墙（edge_wall）、贴面竖挺（mullion）互相叠加。
//
// 2. **窗户不是一个个盒子**。逐窗建模在 40 层塔上是几千个 cube；改成
//    "每层一圈水平腰线 + 沿周长的竖向窗挺"，露出来的深色壳体本身就是玻璃：
//    一栋 40 层塔约 1.5k 三角而不是 40k，远近都读得出窗格。
//    **壳体取深色玻璃调，腰线/窗挺取墙面调** —— 顺序反了就成了黑楼白框。
//
// 3. **调用方必须在 gk_flatten() 里**。每个 user module 调用都会变成一个场景
//    Node（= 一个 Model + 一个碰撞体）。生成器把建筑按街区分组并包 flatten，
//    每组按三角预算切分，留在 65535 三角的物理网格上限之下。
//
// 4. **一切装饰只许内缩，不许越出 OSM 轮廓**。轮廓是导航与碰撞的契约：NavGrid
//    判定一格的地面是从上往下打射线取第一个命中，人行道上方哪怕只挑出 0.2m 的
//    檐口，那一格的"地面"也会变成檐口高度，相邻格跨不过去 —— 密集城区的窄街
//    就此断开。首版把腰线/勒脚/雨篷都往外贴，`Test_GeoCityWalkable` 的两点之间
//    直接找不到路（可达格从 22264 掉到 16764）。
//    做法：壳体内缩 relief，装饰再填回到原轮廓，最大外廓因此恒等于轮廓本身。

// ============================ 配色 ============================
//
// 路径追踪强日光下 albedo 会整体提亮：0.5 就是白色，所以全部压在 0.10~0.36。

function gc_WALL(i) = [
    [0.34, 0.33, 0.31],   // 0 浅混凝土
    [0.27, 0.27, 0.28],   // 1 灰混凝土
    [0.31, 0.29, 0.26],   // 2 暖色涂料
    [0.25, 0.27, 0.29],   // 3 冷灰石材
    [0.29, 0.26, 0.23],   // 4 面砖
    [0.24, 0.26, 0.24],   // 5 灰绿
    [0.22, 0.23, 0.25],   // 6 深灰石材
    [0.36, 0.34, 0.30]    // 7 浅石材
][i % 8];

// 壳体（= 窗）。腰线之间露出来的就是它。
//
// 别再往下压了：首版取 0.11~0.14，配上腰线外挑造成的自阴影，整座塔在 PT 下
// 是一根黑柱子——比原来的素棱柱还难看。玻璃在天光下反的是天空，读出来是
// 中灰偏蓝，不是黑。0.17~0.22 配 30% 左右的墙面覆盖率，整栋平均落在 0.22 上下。
function gc_GLASS(i) = [
    [0.17, 0.19, 0.22],   // 0 灰蓝玻璃
    [0.15, 0.20, 0.25],   // 1 蓝绿幕墙
    [0.19, 0.19, 0.20],   // 2 中性灰
    [0.21, 0.20, 0.17]    // 3 茶色玻璃
][i % 4];

function gc_ROOF(i) = [
    [0.26, 0.13, 0.10],   // 0 红瓦
    [0.18, 0.19, 0.21],   // 1 石板灰
    [0.24, 0.24, 0.23],   // 2 水泥灰
    [0.16, 0.18, 0.20],   // 3 深灰蓝（东亚屋面）
    [0.22, 0.18, 0.14],   // 4 褐瓦
    [0.20, 0.21, 0.19]    // 5 苔绿灰
][i % 6];

function gc_PLINTHC() = [0.23, 0.21, 0.19];   // 勒脚/店面（比墙略深，别压到 0.17：街面一层会糊成黑带）
function gc_METALC()  = [0.30, 0.31, 0.33];   // 屋面设备
function gc_TANKC()   = [0.28, 0.27, 0.25];   // 水箱
function gc_MASTC()   = [0.23, 0.23, 0.24];   // 天线

// Surface helper. `color()` remains the default for all existing SCAD assets;
// only the glass shell/band opts into a glossy, metal-like response so the
// curtain wall can reflect the sky and neighbouring buildings.
module gc_SURFACE(c, glass = 0)
{
    if (glass > 0)
        gk_material(c, roughness = 0.06, metalness = 0.85) children();
    else
        color(c) children();
}

// ============================ 立面方案 ============================
//
// [0]bandH 腰线高  [1]relief 浮雕深度  [2]mullW 窗挺宽  [3]mullPitch 窗挺间距
// [4]hasPlinth  [5]hasCrown  [6]shellGlass
//
// 腰线越高、窗挺越宽 => 露出的玻璃越小 => 越"实"。1 是全玻璃幕墙，
// 3 是欧洲砌体的开窗立面，两端之间是现代板楼。
//
// [6] 决定图底关系，是本表最要紧的一位：
//   1 = 壳体深玻璃、腰线与窗挺是墙  -> 满铺窗的城市楼宇
//   0 = 壳体是墙、腰线是玻璃        -> 大片实墙上开一条带窗（厂房、工棚）
// 反了就是"黑楼白框"，一眼假。
//
// 覆盖率（腰线高/层高 + 窗挺宽/间距）决定整栋的平均反照率。低于 25% 会黑成
// 一根柱子，高于 60% 就看不出是窗了。1 号在 30% 上下，3 号砌体在 55% 上下。
//
// **relief 是往里挖的，不是往外贴的**（见文件头约束 4）：壳体整体内缩 relief，
// 腰线/窗挺/檐口再填回到原轮廓，所以幕墙的玻璃真的退在窗挺后面，
// 而整栋楼的最大外廓仍然等于 OSM 轮廓。
function gc_FAC(i) = [
    [0.00, 0.00, 0.00, 0.0, 0, 0, 0],   // 0 素面（工棚、极小体量）
    [0.55, 0.12, 0.32, 2.9, 1, 1, 1],   // 1 玻璃幕墙塔楼
    [1.00, 0.18, 0.55, 3.3, 1, 1, 1],   // 2 现代板楼 / 写字楼
    [1.45, 0.28, 0.95, 3.2, 1, 1, 1],   // 3 砌体开窗（欧洲旧城）
    [1.05, 0.15, 0.90, 7.5, 0, 1, 0],   // 4 厂房 / 仓库（实墙 + 带窗）
    [0.80, 0.16, 0.62, 3.6, 1, 0, 1]    // 5 低层沿街商住
][i % 6];

function gc_PARAPET_H() = 0.95;
function gc_PARAPET_T() = 0.34;
// 屋顶与楼体顶盖不要落在同一个 z 平面。楼体和屋顶是分开的 mesh，
// 即使只是 1~2cm 的共面，也会在远景/斜视角下触发 z-fighting。
function gc_ROOF_CLEARANCE() = 0.05;
// 坡屋顶出檐。这是全库**唯一**允许越出轮廓的东西：没有挑檐的坡顶不像屋顶。
// 压到 0.25 是因为它同样吃 NavGrid 的可走格（约束 4），而坡顶只出现在矮房上，
// 檐口离地近、遮住的正是紧贴墙根那一圈。
function gc_EAVE() = 0.25;

// ============================ 工具 ============================

// 确定性 PRNG。LCG 低位质量差，所以取高位并做两轮混合；
// 输入保持在 1e6 量级，s * 1103515245 才不会越过 double 的 2^53 精度。
function gc_hash(s) = ((abs(s) * 1103515245 + 12345) % 2097152);
function gc_rnd01(s) = gc_hash(gc_hash(s) + 7919) / 2097152;
function gc_rnd(s, m) = floor(gc_rnd01(s) * m);

function gc_unit(v) = let (l = norm(v)) l < 1e-9 ? [0, 0] : [v[0] / l, v[1] / l];

// CCW 环上边 a→b 的外法线。轮廓由生成器保证 CCW；洞环是 CW，
// 同一个函数在洞上给出的"外法线"指向洞内，正好让洞跟着一起外扩。
function gc_out(a, b) = gc_unit([b[1] - a[1], -(b[0] - a[0])]);

// 顶点沿角平分线外移 d。k 是半角余弦的下限：锐角处不夹住的话尖角会飞出去。
function gc_off_pt(p0, p1, p2, d) =
    let (n0 = gc_out(p0, p1), n1 = gc_out(p1, p2),
         sum = [n0[0] + n1[0], n0[1] + n1[1]],
         m = norm(sum) < 1e-6 ? n1 : gc_unit(sum),
         k = max(m[0] * n1[0] + m[1] * n1[1], 0.35))
    [p1[0] + m[0] * d / k, p1[1] + m[1] * d / k];

function gc_ring_offset(ring, d) =
    let (n = len(ring))
    [for (i = [0 : n - 1]) gc_off_pt(ring[(i + n - 1) % n], ring[i], ring[(i + 1) % n], d)];

// 带洞轮廓整体外扩：每个 path 各自按自身绕向外扩，索引结构不变。
function gc_offset_pts(pts, paths, d) =
    len(paths) == 0
        ? gc_ring_offset(pts, d)
        : [for (p = paths) each gc_ring_offset([for (i = p) pts[i]], d)];

// 取外轮廓（腰线/女儿墙/杂物只认它）。
function gc_outer(pts, paths) = len(paths) == 0 ? pts : [for (i = paths[0]) pts[i]];

// 一层实体板：整个轮廓外扩 d 后挤出。腰线、屋面板、檐口都是它。
module gc_slab(pts, paths, d, z, hh)
{
    gc_slab_at(gc_offset_pts(pts, paths, d), paths, z, hh);
}

// 外扩点表已经算好时用这个。**楼层腰线必须走这条路径**：外扩是解释器里最贵的
// 一段（每个顶点若干次函数调用），一栋 40 层的塔按层各算一次就是同一份结果算
// 40 遍。实测把它提到每栋算一次，整块 tile 的解析时间下降明显。
module gc_slab_at(op, paths, z, hh)
{
    if (len(paths) == 0)
        translate([0, 0, z]) linear_extrude(height = hh) polygon(points = op);
    else
        translate([0, 0, z]) linear_extrude(height = hh) polygon(points = op, paths = paths);
}

// 沿轮廓各边立一圈墙（女儿墙）。用贴边盒子而不是"大板减小板"：
// 挖洞要 CSG，一片 tile 上千次 boolean 扛不住（见文件头约束 1）。
// 每条边加长 thick 把转角填实，转角处两块实体相交无妨。
module gc_edge_wall(ring, z, hh, thick, out)
{
    n = len(ring);
    if (n >= 3 && hh > 0.01)
        for (i = [0 : n - 1])
            let (a = ring[i], b = ring[(i + 1) % n],
                 ex = b[0] - a[0], ey = b[1] - a[1], L = norm([ex, ey]))
                if (L > 0.05)
                    let (u = [ex / L, ey / L], no = [u[1], -u[0]])
                        translate([(a[0] + b[0]) / 2 + no[0] * out,
                                   (a[1] + b[1]) / 2 + no[1] * out,
                                   z + hh / 2])
                            rotate([0, 0, atan2(u[1], u[0])])
                                cube([L + thick, thick, hh], center = true);
}

// 竖向窗挺：沿每条边按间距摆一排薄板，从内缩的壳体填回到轮廓。
// 盒子厚 2*relief、中心退在轮廓内 relief 处 => **外表面正好落在轮廓上**
// （约束 4），内侧那一半陷进壳体里，不会因为角度误差在墙面留缝。
module gc_mullions(ring, z, hh, w, relief, pitch)
{
    n = len(ring);
    if (n >= 3 && hh > 0.4 && pitch > 0.5 && relief > 0.001)
        for (i = [0 : n - 1])
            let (a = ring[i], b = ring[(i + 1) % n],
                 ex = b[0] - a[0], ey = b[1] - a[1], L = norm([ex, ey]),
                 cnt = floor(L / pitch))
                if (L > 1.2 && cnt >= 1)
                    for (k = [0 : cnt])
                        let (u = [ex / L, ey / L], no = [u[1], -u[0]],
                             t = (k + 0.5) * L / (cnt + 1))
                            translate([a[0] + u[0] * t - no[0] * relief,
                                       a[1] + u[1] * t - no[1] * relief,
                                       z + hh / 2])
                                rotate([0, 0, atan2(u[1], u[0])])
                                    cube([w, relief * 2, hh], center = true);
}

// ============================ 立面 ============================

// 腰线 + 窗挺。楼层多到一定程度就隔层画腰线：120 层的塔不需要 120 圈，
// 视觉上分辨不出来，只会白白吃掉三角预算。
module gc_facade(pts, paths, ring, z, h, f, floorH, bandC)
{
    z0 = z + gc_plinth_top(f, h, floorH);
    hh = h - gc_plinth_top(f, h, floorH) - gc_crown_h(f, h);
    if (hh > 1.0)
    {
        if (f[3] > 0.5 && f[2] > 0.01)
            gc_mullions(ring, z0, hh, f[2], f[1], f[3]);

        if (f[0] > 0.01)
        {
            n = floor(hh / floorH);
            if (n >= 1)
            {
                step = max(1, ceil(n / 48));
                cnt = floor((n - 1) / step);
                gc_SURFACE(bandC, f[6] > 0 ? 0 : 1)
                    for (k = [0 : cnt])
                        gc_slab_at(pts, paths, z0 + k * step * floorH, f[0]);
            }
        }
    }
}

// 勒脚高度：底层做店面。楼太矮就不做，否则整栋都是勒脚。
function gc_plinth_top(f, h, floorH) =
    (f[4] > 0 && h > floorH * 2.2) ? min(floorH * 1.15, 5.0) : 0;
function gc_crown_h(f, h) = (f[5] > 0 && h > 6.0) ? 0.7 : 0;

// ============================ 屋顶 ============================

// 坡屋顶。生成器给的是最小面积包围盒（w >= d，w 为脊向）与矩形度，
// 只有足够接近矩形的轮廓才会走到这里——任意凹多边形要直骨架，不值当。
//
// ridgeFrac = 1 是双坡（山墙到边），< 1 是四坡（歇山/庑殿的低模），
// 趋近 0 是攒尖。两者共用同一套拓扑，只是脊的两端往里收。
//
// polyhedron 面按 OpenSCAD 约定：从实体**外面**看是顺时针，
// 等价于按列出顺序做右手法则得到的法线指向实体**内部**。下面每个面都按这条核过。
module gc_roof_pitched(obb, z, rise, ridgeFrac, over)
{
    if (len(obb) >= 5 && obb[2] > 1.0 && obb[3] > 1.0 && rise > 0.05)
    {
        L = obb[2] / 2 + over;
        W = obb[3] / 2 + over;
        R = max(L * max(min(ridgeFrac, 1.0), 0.02), 0.05);
        translate([obb[0], obb[1], z]) rotate([0, 0, obb[4]])
            polyhedron(
                points = [[-L, -W, 0], [L, -W, 0], [L, W, 0], [-L, W, 0],
                          [-R, 0, rise], [R, 0, rise]],
                faces = [[0, 1, 2, 3],      // 底
                         [0, 4, 5, 1],      // -y 坡
                         [2, 5, 4, 3],      // +y 坡
                         [3, 4, 0],         // -x 端（双坡时是山墙）
                         [1, 5, 2]]);       // +x 端
    }
}

// 屋面杂物。密集城区从空中看下去，楼顶占了画面的一大半，
// 一片纯色平顶是"地图感"最大的来源之一。
// level：0 无 / 1 稀疏 / 2 常规 / 3 密集（港式水箱+空调森林）
module gc_roof_item(v, level, s)
{
    if (v == 0)                                  // 空调外机组
        color(gc_METALC())
            translate([0, 0, 0.6]) cube([1.9 + gc_rnd01(s) * 1.4, 1.2, 1.2], center = true);
    if (v == 1)                                  // 水箱
        color(gc_TANKC())
            translate([0, 0, 0.05]) cylinder(h = 1.9 + gc_rnd01(s + 5) * 1.2,
                                             r = 0.85 + gc_rnd01(s + 9) * 0.5, $fn = 8);
    if (v == 2)                                  // 楼梯间 / 电梯机房
        color(gc_METALC())
            translate([0, 0, 1.5]) cube([3.6 + gc_rnd01(s + 3) * 2.4, 3.0, 3.0], center = true);
    if (v == 3)                                  // 排风管一组
        color(gc_METALC())
            for (k = [0 : 2])
                translate([k * 0.9 - 0.9, 0, 0.55]) cylinder(h = 1.1, r = 0.22, $fn = 6);
    if (v == 4)
    {
        if (level >= 2)                          // 太阳能热水器 / 集热排
            color(gc_TANKC())
            {
                translate([0, 0, 0.75]) rotate([0, 0, 0])
                    cube([2.6, 1.5, 0.18], center = true);
                translate([0, -0.8, 0.45]) cylinder(h = 1.6, r = 0.32, $fn = 6);
            }
        else
            color(gc_METALC()) translate([0, 0, 0.35]) cube([1.4, 1.4, 0.7], center = true);
    }
}

// anchor = [x, y, r]：生成器算出的**保证在轮廓内**的点和它到最近边的距离。
//
// 不要改回用包围盒散点。L 形楼的最小包围盒盖住了凹口，水箱会散到凹口上空 ——
// 那不只是难看：NavGrid 判定一格的地面是从天上往下打射线取第一个命中，
// 一个飘在人行道上方 40m 的水箱会让那几格的"地面"变成 40m 高，
// 相邻格跨不过去，整条街就被切断了。Test_GeoCityWalkable 就是这么挂的。
module gc_roof_clutter(anchor, z, level, seed)
{
    if (level > 0 && len(anchor) >= 3 && anchor[2] >= 2.5)
    {
        n = min(level * 2 + 1, 7);
        // 留出女儿墙的厚度，件子不要压在墙上。
        rad = max(anchor[2] - 1.1, 0);
        translate([anchor[0], anchor[1], z])
        {
            for (k = [0 : n - 1])
                let (s = seed * 31 + k * 7,
                     ang = gc_rnd01(s) * 360,
                     // sqrt 让点在圆内均匀，不然全挤在中心
                     rr = rad * sqrt(gc_rnd01(s + 101)))
                    translate([rr * cos(ang), rr * sin(ang), 0])
                        gc_roof_item(gc_rnd(s + 211, 5), level, s);

            // 高层再加一根桅杆：天际线的辨识度基本靠它。锚点即最安全的位置。
            if (level >= 3)
                color(gc_MASTC())
                {
                    cylinder(h = 8 + gc_rnd01(seed + 77) * 9, r = 0.16, $fn = 5);
                    translate([0, 0, 3.2]) cube([2.0, 0.14, 0.14], center = true);
                    translate([0, 0, 5.0]) cube([1.4, 0.14, 0.14], center = true);
                }
        }
    }
}

// roof = [kind, tone, rise, ridgeFrac, clutter, anchorX, anchorY, anchorR]
// kind 0 无 / 1 平顶 / 2 坡顶
module gc_roof(pts, paths, ring, z, h, roof, obb, wall, seed)
{
    if (len(roof) >= 5)
    {
        if (roof[0] == 1)
        {
            // 屋面板。壳体顶盖是玻璃色，鸟瞰视角下整片城市会变成一张深色板；
            // 盖一层薄的屋面色板，Aerial 视图才有屋顶该有的样子。
            deckZ = z + h + gc_ROOF_CLEARANCE();
            color(gc_ROOF(roof[1])) gc_slab_at(pts, paths, deckZ, 0.16);
            // 女儿墙底面再抬一段，避免与楼体顶面及屋面板的边界面共面；
            // 它仍然嵌入屋面板内部，不会在外观上留下缝隙。
            // out = -thick/2 让女儿墙外表面正好压在轮廓上（约束 4）。
            parapetZ = deckZ + gc_ROOF_CLEARANCE();
            color(wall) gc_edge_wall(ring, parapetZ, gc_PARAPET_H(), gc_PARAPET_T(),
                                     -gc_PARAPET_T() / 2);
            if (len(roof) >= 8)
                // 杂物底面略嵌入屋面板，避开屋面板顶面共面但不产生悬空。
                gc_roof_clutter([roof[5], roof[6], roof[7]],
                                 deckZ + 0.16 - gc_ROOF_CLEARANCE(), roof[4], seed);
        }
        if (roof[0] == 2)
            color(gc_ROOF(roof[1]))
                gc_roof_pitched(obb, z + h + gc_ROOF_CLEARANCE(),
                                roof[2], roof[3], gc_EAVE());
    }
}

// ============================ 入口 ============================

// 一栋建筑。调用方（生成器发出的 blk_* 模块）必须已经在 gk_flatten() 里。
//
// z 是**地面**高度（生成器在调用点用 gz(...) 采样地形），h 是墙高，
// 屋顶因此稳定落在 z + h。skirt 只把壳体往地下多拉一截防坡地露底 ——
// 勒脚、腰线、檐口、屋顶全部从 z 起算，不受 skirt 影响。
// 早先把 skirt 折进 z 里（z - skirt，h + skirt），结果底层店面有一半埋在地里。
module gc_bld(pts, z, h, fac, roof, obb = [], skirt = 0, paths = [])
{
    ring = gc_outer(pts, paths);
    f = gc_FAC(fac[0]);
    wall = gc_WALL(fac[1]);
    glass = gc_GLASS(fac[2]);
    floorH = max(fac[3], 2.4);
    seed = fac[4];

    // 壳体。f[6] 决定它是玻璃还是墙，腰线取另一个（见 gc_FAC 注释）。
    shellC = f[6] > 0 ? glass : wall;
    bandC = f[6] > 0 ? wall : glass;
    // 壳体内缩 relief：装饰再填回到轮廓，最大外廓恒等于 OSM 轮廓（约束 4）。
    spts = f[1] > 0.001 ? gc_offset_pts(pts, paths, -f[1]) : pts;
    if (len(paths) == 0)
        gc_SURFACE(shellC, f[6]) translate([0, 0, z - skirt]) linear_extrude(height = h + skirt)
            polygon(points = spts);
    else
        gc_SURFACE(shellC, f[6]) translate([0, 0, z - skirt]) linear_extrude(height = h + skirt)
            polygon(points = spts, paths = paths);

    gc_facade(pts, paths, ring, z, h, f, floorH, bandC);

    pt = gc_plinth_top(f, h, floorH);
    ch = gc_crown_h(f, h);
    // 勒脚 + 雨篷：街面视角最吃这一层，塔楼从下往上看主要看到的就是它。
    if (pt > 0.1)
        color(gc_PLINTHC())
        {
            gc_slab(pts, paths, -0.06, z, pt - 0.30);
            gc_slab_at(pts, paths, z + pt - 0.30, 0.30);
        }
    // 檐口：楼身与天空之间的一道横线，没有它高层看着像被切掉了顶。
    if (ch > 0.1)
        color(wall) gc_slab_at(pts, paths, z + h - ch, ch);

    gc_roof(pts, paths, ring, z, h, roof, obb, wall, seed);
}
