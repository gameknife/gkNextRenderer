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
    n = len(L);
    if (n >= 2)
    {
        d = deep > 0 ? deep : rd_DEEP();
        hs = [for (i = [0 : n - 1]) rd_station_h(t, L[i], R[i], lift)];
        pts = concat(
            [for (i = [0 : n - 1]) [L[i][0], L[i][1], hs[i]]],
            [for (i = [0 : n - 1]) [R[i][0], R[i][1], hs[i]]],
            [for (i = [0 : n - 1]) [L[i][0], L[i][1], hs[i] - d]],
            [for (i = [0 : n - 1]) [R[i][0], R[i][1], hs[i] - d]]);
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

// ============================ 入口 ============================

// 整张网络。
//   net       = [[L, R], ...]   已 mitre 的左右缘
//   junctions = [ring, ...]     路口凸包
//   widths    = [w, ...]        与 net 等长，仅用于决定画不画中线
//
// gk_flatten() 是必须的：本库把一张表展开成上千个模块调用，而**每个 user module
// 调用都会变成一个场景 Node**（=一个 Model + 一个碰撞体）。实测 1km 香港 tile 不加
// 它是 7683 个节点，光物理 shape cooking 就 1.2 秒；加上之后 90 个节点、34 毫秒。
// 几何合并到调用方那一层，分块仍由生成器控制（每块远低于 65535 三角的 Model 上限）。
module rd_network(t, c, net, junctions = [], widths = [], markings = true)
{
    gk_flatten()
    {
        color(c)
        {
            for (k = [0 : len(net) - 1])
                rd_ribbon(t, net[k][0], net[k][1]);
            if (len(junctions) > 0)
                for (k = [0 : len(junctions) - 1])
                    rd_patch(t, junctions[k]);
        }

        if (markings && len(widths) > 0)
            for (k = [0 : len(net) - 1])
                rd_centerline(t, net[k][0], net[k][1], widths[k]);
    }
}
