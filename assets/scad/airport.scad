// airport.scad —— 低多边形等距机场航站楼（参考 Jumbo Airport Story 风格示意图）
//
// 为后续机场模拟 demo 预留 POI 锚点（命名约定与 office.scad / OfficeMap 同构）：
//   锚点 = 具名 user module，加载后节点名 = module 名，WorldTranslation = 点位坐标。
//   锚点调用必须 translate(...) 在 rotate(...) 外层。
//   锚点类别前缀（未来 AirportMap 按前缀归类）：
//     entrance_<id>  航站楼出入口（旅客生成/离场点）×3
//     checkin_<id>   人工值机柜台 ×6         kiosk_<id>   自助值机机 ×4
//     security_<id>  安检通道（南进北出）×4   gate_<id>    登机口 ×6
//     wait_<id>      候机排椅 ×12            cafe_<id>    咖啡岛柜台 ×1
//     food_<id>      快餐店柜台 ×1           shop_<id>    便利店收银 ×1
//     book_<id>      书店收银 ×1             gift_<id>    礼品店收银 ×1
//     toilet_<id>    卫生间门（01=男 02=女）  staff_<id>   员工办公桌 ×2
//     info_<id>      问询台 ×1               atm_<id>     ATM ×1
//     vending_<id>   自动售货机 ×5
//   非锚点一律 wall_* / part_* / furn_* / prop_* / veh_* / ground_* 前缀。
//   所有带朝向的 module 约定 front = -y；布局处 rotate 调整朝向，正面前方留行走净空。
//
// OpenSCAD Z-up；地面顶面 z = 0.15（与 office.scad / kGroundY 一致）。
// 场地 84x80：航站楼 60x40（x∈[-30,30], y∈[-12,28]，玻璃幕墙、无顶棚切顶视角），
// 北侧 y>28 停机坪（3 架客机 + 地勤车队），南侧 y<-12 陆侧（人行道/马路/停车场/公交）。
// 室内分区：陆侧大厅（值机×6/自助值机/问询/ATM）→ 中部安检 ×4 → 空侧
// 西餐饮区（BURGER 快餐 + COFFEE 咖啡岛）/ 东零售街（SHOP/BOOKS/GIFTS 三连铺）
// → 北侧 6 登机口 + 双候机排椅集群；东北卫生间、东侧员工办公室。

$fn = 16;

// ---- 配色 ----
CONCC    = [0.60, 0.60, 0.58];    // 基底混凝土
APRONC   = [0.69, 0.69, 0.67];    // 停机坪面板
CARPETA  = [0.30, 0.42, 0.62];    // 航站楼地毯 A
CARPETB  = [0.34, 0.47, 0.68];    // 航站楼地毯 B
CARPETD  = [0.24, 0.33, 0.50];    // 地毯缝/深色块
PAVEC    = [0.76, 0.72, 0.64];    // 人行道
ROADC    = [0.30, 0.30, 0.32];    // 沥青
LOTC     = [0.37, 0.37, 0.39];    // 停车场
GRASSC   = [0.45, 0.62, 0.35];
WHITEC   = [0.92, 0.91, 0.88];
GRAYC    = [0.58, 0.60, 0.63];
METALC   = [0.72, 0.74, 0.77];
DARKMETC = [0.22, 0.23, 0.26];
BLACKC   = [0.10, 0.10, 0.11];
GLASSC   = [0.55, 0.72, 0.85, 0.30];
SCREENC  = [0.62, 0.78, 0.92];
SIGNBLUE = [0.16, 0.32, 0.62];    // 标识蓝
AIRBLUE  = [0.20, 0.40, 0.68];    // 设施蓝（柜台/kiosk）
SEATBLUE = [0.28, 0.45, 0.72];    // 候机排椅
BROWNC   = [0.48, 0.32, 0.20];    // 咖啡店墙
TEALC    = [0.16, 0.62, 0.55];    // 礼品店
REDFOOD  = [0.78, 0.25, 0.20];    // 快餐店
SHOPWALL = [0.82, 0.81, 0.78];    // 零售街隔墙
OAKC     = [0.78, 0.57, 0.34];
PLANTC   = [0.34, 0.58, 0.31];
PLANTDC  = [0.24, 0.45, 0.24];
POTC     = [0.70, 0.44, 0.30];
PAPERC   = [0.95, 0.95, 0.92];
YELLINE  = [0.85, 0.75, 0.25];    // 机坪黄线
REDLINE  = [0.75, 0.25, 0.22];

function goods6(i) = i == 0 ? [0.80, 0.30, 0.28] : i == 1 ? [0.95, 0.72, 0.25] : i == 2 ? [0.35, 0.60, 0.80]
                   : i == 3 ? [0.45, 0.68, 0.38] : i == 4 ? [0.70, 0.45, 0.72] : [0.90, 0.88, 0.84];
function carc5(i)  = i == 0 ? [0.75, 0.28, 0.24] : i == 1 ? [0.90, 0.90, 0.92] : i == 2 ? [0.60, 0.62, 0.66]
                   : i == 3 ? [0.22, 0.30, 0.45] : [0.85, 0.55, 0.25];
function book5(i)  = i == 0 ? [0.62, 0.30, 0.26] : i == 1 ? [0.28, 0.45, 0.60] : i == 2 ? [0.80, 0.70, 0.45]
                   : i == 3 ? [0.36, 0.55, 0.38] : [0.50, 0.38, 0.58];

// ================= 地面 =================
module ground_base() color(CONCC) translate([0, 7, 0]) cube([84, 80, 0.26], center = true);

// 航站楼蓝色方块地毯（30x20 块 2x2，棋盘 + 少量深色点缀）
module ground_carpet()
{
    color(CARPETD) translate([0, 8, 0.135]) cube([60, 40, 0.01], center = true);
    for (ix = [0 : 29], iy = [0 : 19])
        translate([-29 + ix * 2, -11 + iy * 2, 0.143])
            color(((ix * 7 + iy * 11) % 13 == 0) ? CARPETD : ((ix + iy) % 2 == 0) ? CARPETA : CARPETB)
                cube([1.94, 1.94, 0.014], center = true);
}

// 停机坪面板 + 接缝 + 黄/红标线
module ground_apron()
{
    color(APRONC) translate([6, 37, 0.14]) cube([72, 18, 0.02], center = true);
    color(APRONC) translate([36, 8, 0.14]) cube([12, 40, 0.02], center = true);   // 东侧服务区
    for (ix = [0 : 15])
        color([0.58, 0.58, 0.56]) translate([-27 + ix * 4.5, 37, 0.152]) cube([0.06, 18, 0.012], center = true);
    color([0.58, 0.58, 0.56]) translate([6, 37, 0.152]) cube([72, 0.06, 0.012], center = true);
    // 滑行引导黄线 + 三机位引入线 + 红色边界线
    color(YELLINE) translate([6, 43.5, 0.153]) cube([68, 0.14, 0.012], center = true);
    color(YELLINE) translate([-14, 37, 0.153]) cube([0.14, 13, 0.012], center = true);
    color(YELLINE) translate([8, 37.2, 0.153]) cube([0.14, 12.6, 0.012], center = true);
    color(YELLINE) translate([27, 38.7, 0.153]) cube([0.14, 9.5, 0.012], center = true);
    color(REDLINE) translate([6, 28.8, 0.153]) cube([72, 0.10, 0.012], center = true);
}

module ground_landside()
{
    color(PAVEC) translate([0, -13.5, 0.14]) cube([84, 3.0, 0.02], center = true);        // 人行道
    color([0.45, 0.45, 0.44]) translate([0, -14.97, 0.155]) cube([84, 0.12, 0.03], center = true);  // 路缘
    color(ROADC) translate([0, -17.7, 0.13]) cube([84, 5.4, 0.012], center = true);        // 马路
    for (ix = [0 : 18])
        color(WHITEC) translate([-40 + ix * 4.4, -17.7, 0.142]) cube([1.6, 0.14, 0.008], center = true);
    for (i = [0 : 6])                                                                      // 两处入口斑马线
    {
        color(WHITEC) translate([-19.9 + i * 0.62, -17.7, 0.144]) cube([0.42, 4.6, 0.008], center = true);
        color(WHITEC) translate([-1.9 + i * 0.62, -17.7, 0.144]) cube([0.42, 4.6, 0.008], center = true);
    }
    color(LOTC) translate([-19, -25.7, 0.13]) cube([42, 10.6, 0.012], center = true);      // 停车场
    for (i = [0 : 15])
        color(WHITEC) translate([-38 + i * 2.4, -28.6, 0.142]) cube([0.08, 4.8, 0.008], center = true);
    color(GRASSC) translate([23, -25.7, 0.145]) cube([38, 10.6, 0.022], center = true);    // 东南草地
    color(GRASSC) translate([-36, 7.9, 0.145]) cube([12, 39.8, 0.022], center = true);     // 西侧草地
    color(GRASSC) translate([-36, 37.5, 0.145]) cube([12, 19, 0.022], center = true);      // 西北草地
}

// ================= 玻璃幕墙 =================
// 沿 +x，底梁 + 玻璃 + 竖梃 + 顶部白檐（高 3.33）
module wall_glass_seg(len)
{
    color(DARKMETC) translate([len / 2, 0, 0.10]) cube([len, 0.12, 0.20], center = true);
    color(GLASSC) translate([len / 2, 0, 1.49]) cube([len - 0.04, 0.06, 2.58], center = true);
    for (p = [0 : 1.5 : len]) color(DARKMETC) translate([p, 0, 1.49]) cube([0.10, 0.10, 2.58], center = true);
    color(DARKMETC) translate([len, 0, 1.49]) cube([0.10, 0.10, 2.58], center = true);
    color(WHITEC) translate([len / 2, 0, 3.05]) cube([len, 0.30, 0.55], center = true);
}

module wall_corner_col() color(WHITEC) cube([0.36, 0.36, 3.32], center = true);

// 室内实墙段（卫生间等，h2.4）
module wall_solid_seg(len, c = [0.84, 0.82, 0.88])
{
    color(c) translate([len / 2, 0, 1.2]) cube([len, 0.15, 2.4], center = true);
}

// 自动滑门单元（3m 洞口）：框 + 半开双扇 + 顶檐 + 地垫
module furn_entrance()
{
    color(DARKMETC)
    {
        for (sx = [-1, 1]) translate([1.52 * sx, 0, 1.40]) cube([0.16, 0.30, 2.60], center = true);
        translate([0, 0, 2.78]) cube([3.20, 0.30, 0.16], center = true);
    }
    color(GLASSC) translate([-1.02, 0.10, 1.35]) cube([0.95, 0.05, 2.30], center = true);
    color(GLASSC) translate([1.02, -0.10, 1.35]) cube([0.95, 0.05, 2.30], center = true);
    color(WHITEC) translate([0, 0, 3.05]) cube([3.20, 0.34, 0.55], center = true);
    color([0.28, 0.30, 0.28]) translate([0, 0, 0.012]) cube([2.90, 1.90, 0.022], center = true);
}

// 入口上方大招牌
module prop_big_sign(label = "AIRPORT")
{
    color(WHITEC) cube([4.60, 0.18, 1.00], center = true);
    color(SIGNBLUE) translate([-1.38, -0.10, -0.28]) rotate([90, 0, 0]) linear_extrude(0.04) text(label, size = 0.55);
}

// ================= 室内设施库（front = -y） =================
module furn_monitor(screen = SCREENC, w = 0.52)
{
    color(DARKMETC)
    {
        cylinder(h = 0.025, r = 0.09);
        translate([0, 0.02, 0.15]) cube([0.05, 0.04, 0.26], center = true);
    }
    color(BLACKC) translate([0, 0.030, 0.40]) cube([w, 0.035, 0.32], center = true);
    color(screen) translate([0, 0.006, 0.40]) cube([w - 0.05, 0.015, 0.27], center = true);
}

module furn_task_chair(seat = [0.45, 0.47, 0.52])
{
    color(DARKMETC)
    {
        for (a = [0 : 72 : 288]) rotate([0, 0, a + 36]) translate([0.15, 0, 0.03]) cube([0.28, 0.05, 0.05], center = true);
        translate([0, 0, 0.05]) cylinder(h = 0.40, r = 0.028, $fn = 12);
        translate([0, 0.19, 0.54]) cube([0.05, 0.05, 0.15], center = true);
    }
    color(seat) translate([0, 0, 0.47]) cube([0.44, 0.43, 0.07], center = true);
    color(seat) translate([0, 0.215, 0.78]) cube([0.42, 0.06, 0.44], center = true);
}

// 值机柜台：蓝色柜体 + 白台面 + 行李秤/传送带 + 坐席 + 头顶号牌屏
module furn_checkin_desk(label = "1")
{
    color(AIRBLUE) translate([0, -0.02, 0.54]) cube([1.60, 0.62, 1.08], center = true);
    color(WHITEC) translate([0, 0, 1.10]) cube([1.70, 0.72, 0.06], center = true);
    color(WHITEC) translate([0, 0.55, 0.37]) cube([1.40, 0.50, 0.74], center = true);    // 坐席矮台
    translate([0.30, 0.62, 0.74]) rotate([0, 0, 180]) furn_monitor(SCREENC, 0.44);
    color(BLACKC) translate([-0.25, 0.55, 0.755]) cube([0.30, 0.11, 0.02], center = true);
    // 行李传送带（向北没入墙后）
    color(GRAYC) translate([-1.15, 0.65, 0.26]) cube([0.62, 1.90, 0.52], center = true);
    color([0.18, 0.19, 0.21]) translate([-1.15, 0.65, 0.535]) cube([0.50, 1.80, 0.03], center = true);
    color([0.72, 0.40, 0.26]) translate([-1.15, 0.30, 0.64]) cube([0.40, 0.55, 0.18], center = true);  // 行李箱
    // 头顶号牌
    color(DARKMETC) translate([0.62, 0.20, 1.85]) cube([0.06, 0.06, 1.50], center = true);
    color(SIGNBLUE) translate([0.62, 0.16, 2.72]) cube([0.74, 0.08, 0.52], center = true);
    color(WHITEC) translate([0.52, 0.11, 2.52]) rotate([90, 0, 0]) linear_extrude(0.03) text(label, size = 0.30);
    translate([0.15, 1.10, 0]) furn_task_chair();
}

// 自助值机 kiosk
module furn_kiosk()
{
    color(AIRBLUE) translate([0, 0, 0.62]) cube([0.55, 0.40, 1.24], center = true);
    color(DARKMETC) translate([0, 0, 0.03]) cube([0.62, 0.48, 0.06], center = true);
    color(BLACKC) translate([0, -0.205, 0.98]) rotate([14, 0, 0]) cube([0.46, 0.05, 0.40], center = true);
    color(SCREENC) translate([0, -0.225, 0.98]) rotate([14, 0, 0]) cube([0.40, 0.02, 0.33], center = true);
    color(METALC) translate([0, -0.21, 0.60]) cube([0.34, 0.04, 0.05], center = true);   // 出票口
}

// 安检通道：入口台-X光机-出口台（局部 y 0..3.1），金属探测门在 x+1.05
module furn_security_lane()
{
    color([0.62, 0.64, 0.66])
    {
        translate([0, 0.40, 0.60]) cube([0.62, 0.85, 0.10], center = true);
        translate([0, 2.70, 0.60]) cube([0.62, 0.80, 0.10], center = true);
        for (sy = [0.15, 0.68, 2.45, 2.95])
            translate([0, sy, 0.28]) cube([0.50, 0.08, 0.56], center = true);
    }
    color([0.55, 0.58, 0.62]) translate([0, 1.55, 0.56]) cube([0.92, 1.50, 1.12], center = true);
    color([0.38, 0.41, 0.46]) translate([0, 1.55, 1.18]) cube([0.94, 0.84, 0.12], center = true);
    color(BLACKC)
    {
        translate([0, 0.81, 0.62]) cube([0.50, 0.05, 0.40], center = true);
        translate([0, 2.29, 0.62]) cube([0.50, 0.05, 0.40], center = true);
        translate([0, 1.55, 0.685]) cube([0.50, 1.55, 0.03], center = true);
    }
    translate([-0.62, 1.30, 0.86]) rotate([0, 0, 90]) furn_monitor([0.55, 0.85, 0.70], 0.40);
    color(DARKMETC) translate([-0.62, 1.30, 0.30]) cube([0.06, 0.06, 0.60], center = true);
    // 行李筐
    for (i = [0 : 2])
        color([0.50, 0.55, 0.62]) translate([0.42, 0.18 + i * 0.0, 0.66 + i * 0.07]) cube([0.34, 0.46, 0.06], center = true);
    // 金属探测门
    translate([1.05, 1.30, 0])
    {
        color([0.45, 0.48, 0.53]) for (sx = [-1, 1]) translate([0.42 * sx, 0, 1.02]) cube([0.14, 0.46, 2.04], center = true);
        color([0.45, 0.48, 0.53]) translate([0, 0, 2.10]) cube([0.98, 0.46, 0.14], center = true);
        color([0.35, 0.85, 0.45]) translate([0, -0.20, 2.10]) cube([0.20, 0.07, 0.06], center = true);
    }
}

// 登机口：门框+滑门+蓝色 GATE 牌 + 登机检票台 + 隔离柱
module furn_gate_door(label = "GATE 1")
{
    color(DARKMETC)
    {
        for (sx = [-1, 1]) translate([0.95 * sx, 0.30, 1.30]) cube([0.12, 0.20, 2.60], center = true);
        translate([0, 0.30, 2.66]) cube([2.02, 0.20, 0.14], center = true);
    }
    color(GLASSC) translate([-0.45, 0.34, 1.30]) cube([0.86, 0.05, 2.40], center = true);
    color(GLASSC) translate([0.45, 0.26, 1.30]) cube([0.86, 0.05, 2.40], center = true);
    color(SIGNBLUE) translate([0, 0.26, 3.02]) cube([1.90, 0.14, 0.56], center = true);
    color(WHITEC) translate([-0.56, 0.18, 2.86]) rotate([90, 0, 0]) linear_extrude(0.035) text(label, size = 0.30);
    // 检票台
    translate([1.55, -0.55, 0]) rotate([0, 0, 12])
    {
        color(AIRBLUE) translate([0, 0, 0.52]) cube([0.66, 0.46, 1.04], center = true);
        color(WHITEC) translate([0, -0.04, 1.07]) rotate([12, 0, 0]) cube([0.70, 0.50, 0.05], center = true);
        color(BLACKC) translate([0, -0.05, 1.13]) rotate([12, 0, 0]) cube([0.30, 0.22, 0.03], center = true);
    }
    translate([-1.45, -0.45, 0]) prop_stanchion();
    translate([-1.45, -1.55, 0]) prop_stanchion();
    color([0.25, 0.40, 0.70]) translate([-1.45, -1.0, 0.86]) cube([0.05, 1.02, 0.06], center = true);
}

// 候机排椅（4 联座，front=-y）
module furn_bench_row()
{
    color(DARKMETC)
    {
        translate([0, 0, 0.33]) cube([2.30, 0.10, 0.08], center = true);
        for (sx = [-1, 1]) translate([0.95 * sx, 0, 0.16]) cube([0.10, 0.55, 0.32], center = true);
    }
    for (i = [0 : 3])
        translate([-0.855 + i * 0.57, 0, 0])
        {
            color(SEATBLUE) translate([0, -0.02, 0.43]) cube([0.50, 0.48, 0.07], center = true);
            color(SEATBLUE) translate([0, 0.225, 0.645]) rotate([8, 0, 0]) cube([0.50, 0.06, 0.44], center = true);
        }
    color(DARKMETC) for (i = [0 : 4])
        translate([-1.14 + i * 0.57, -0.02, 0.56]) cube([0.04, 0.40, 0.05], center = true);
}

// 隔离柱 + 排队线
module prop_stanchion()
{
    color(DARKMETC)
    {
        cylinder(h = 0.04, r = 0.14, $fn = 12);
        cylinder(h = 0.92, r = 0.025, $fn = 8);
        translate([0, 0, 0.92]) sphere(r = 0.045);
    }
}

module prop_queue_line(n = 5)
{
    for (i = [0 : n - 1])
    {
        translate([i * 1.1, 0, 0]) prop_stanchion();
        if (i < n - 1)
            color([0.25, 0.40, 0.70]) translate([i * 1.1 + 0.55, 0, 0.84]) cube([1.00, 0.05, 0.06], center = true);
    }
}

// 航显屏（落地双柱式；wallmount=false）
module prop_fids()
{
    color(DARKMETC) for (sx = [-1, 1]) translate([0.85 * sx, 0.05, 1.05]) cube([0.08, 0.08, 2.10], center = true);
    color([0.10, 0.13, 0.22]) translate([0, 0, 2.42]) cube([2.20, 0.10, 1.30], center = true);
    color(SIGNBLUE) translate([0, -0.055, 2.95]) cube([2.10, 0.012, 0.18], center = true);
    for (r = [0 : 4])
    {
        color([0.95, 0.80, 0.30]) translate([-0.62, -0.055, 2.70 - r * 0.21]) cube([0.70, 0.012, 0.09], center = true);
        color(WHITEC) translate([0.18, -0.055, 2.70 - r * 0.21]) cube([0.46, 0.012, 0.09], center = true);
        color((r == 2) ? [0.35, 0.85, 0.45] : WHITEC) translate([0.80, -0.055, 2.70 - r * 0.21]) cube([0.36, 0.012, 0.09], center = true);
    }
}

// 悬挂指示牌（双柱 + 色牌 + 文字 + 可选右向箭头）
module prop_hang_sign(label = "ALL GATES", c = SIGNBLUE, arrow = true)
{
    color(DARKMETC) for (sx = [-1, 1]) translate([1.30 * sx, 0.04, 1.30]) cube([0.07, 0.07, 2.60], center = true);
    color(c) translate([0, 0, 2.42]) cube([2.80, 0.10, 0.60], center = true);
    color(WHITEC) translate([-1.05, -0.055, 2.28]) rotate([90, 0, 0]) linear_extrude(0.03) text(label, size = 0.26);
    if (arrow)
        color(WHITEC)
        {
            translate([1.02, -0.06, 2.48]) rotate([0, 45, 0]) cube([0.04, 0.012, 0.26], center = true);
            translate([1.02, -0.06, 2.32]) rotate([0, -45, 0]) cube([0.04, 0.012, 0.26], center = true);
        }
}

// 咖啡柜台：木柜 + 糕点柜 + 咖啡机 + 收银
module furn_cafe_counter()
{
    color(OAKC) translate([0, 0, 0.525]) cube([3.40, 0.62, 1.05], center = true);
    color([0.60, 0.42, 0.25]) translate([0, -0.315, 0.40]) cube([3.40, 0.012, 0.26], center = true);
    color(WHITEC) translate([0, 0, 1.075]) cube([3.52, 0.70, 0.05], center = true);
    // 玻璃糕点柜（左）
    color(GLASSC) translate([-1.10, -0.02, 1.32]) cube([1.10, 0.58, 0.44], center = true);
    color([0.85, 0.65, 0.35]) translate([-1.30, 0.0, 1.16]) scale([1, 0.6, 0.5]) sphere(r = 0.14);
    color([0.80, 0.55, 0.30]) translate([-0.95, -0.08, 1.16]) scale([1, 0.6, 0.5]) sphere(r = 0.12);
    color([0.92, 0.88, 0.80]) translate([-1.12, 0.12, 1.14]) cylinder(h = 0.08, r = 0.09, $fn = 10);
    // 咖啡机（右）
    color([0.25, 0.27, 0.31]) translate([0.85, 0.10, 1.32]) cube([0.62, 0.40, 0.44], center = true);
    color(METALC) translate([0.85, -0.12, 1.18]) cube([0.40, 0.10, 0.08], center = true);
    color(WHITEC) translate([0.72, -0.10, 1.115]) cylinder(h = 0.07, r = 0.04, $fn = 10);
    color(WHITEC) translate([0.98, -0.10, 1.115]) cylinder(h = 0.07, r = 0.04, $fn = 10);
    // 收银
    color(BLACKC) translate([0.05, -0.10, 1.10]) cube([0.30, 0.24, 0.05], center = true);
    translate([0.05, 0.04, 1.12]) rotate([0, 0, 180]) furn_monitor(SCREENC, 0.30);
    // 杯塔
    color([0.90, 0.60, 0.40]) translate([1.45, 0.12, 1.10]) cylinder(h = 0.26, r1 = 0.06, r2 = 0.045, $fn = 10);
}

module furn_cafe_table(c = BROWNC)
{
    color(DARKMETC)
    {
        cylinder(h = 0.04, r = 0.22, $fn = 14);
        cylinder(h = 0.72, r = 0.035, $fn = 10);
    }
    color(OAKC) translate([0, 0, 0.72]) cylinder(h = 0.04, r = 0.38, $fn = 20);
    for (a = [40, 220])
        rotate([0, 0, a]) translate([0, -0.62, 0])
        {
            color(c)
            {
                for (sx = [-1, 1], sy = [-1, 1]) translate([0.14 * sx, 0.13 * sy, 0.21]) cube([0.04, 0.04, 0.42], center = true);
                translate([0, 0, 0.43]) cube([0.36, 0.34, 0.04], center = true);
                translate([0, 0.16, 0.66]) cube([0.36, 0.04, 0.42], center = true);
            }
        }
}

// 菜单板 / 店招（贴墙，front=-y）
module prop_menu_board()
{
    color([0.20, 0.15, 0.10]) cube([1.60, 0.06, 0.90], center = true);
    color([0.92, 0.85, 0.70]) translate([-0.45, -0.04, 0.22]) cube([0.55, 0.012, 0.07], center = true);
    color([0.92, 0.85, 0.70]) translate([-0.42, -0.04, 0.02]) cube([0.48, 0.012, 0.07], center = true);
    color([0.92, 0.85, 0.70]) translate([-0.46, -0.04, -0.18]) cube([0.50, 0.012, 0.07], center = true);
    color([0.85, 0.55, 0.30]) translate([0.42, -0.04, 0.0]) cube([0.50, 0.012, 0.50], center = true);
}

// 店铺门头（双柱 + 牌匾文字）
module prop_shop_portal(w = 4.8, label = "SHOP", c = [0.20, 0.40, 0.68])
{
    color(DARKMETC) for (sx = [-1, 1]) translate([w / 2 * sx, 0, 1.22]) cube([0.16, 0.16, 2.44], center = true);
    color(c) translate([0, 0, 2.66]) cube([w + 0.16, 0.22, 0.62], center = true);
    color(WHITEC) translate([-(0.28 * 0.55 * len(label) * 2) / 2, -0.115, 2.46]) rotate([90, 0, 0]) linear_extrude(0.035) text(label, size = 0.42);
}

// 便利店货架（双面，front 任意）
module furn_gondola()
{
    color(GRAYC) translate([0, 0, 0.06]) cube([2.20, 0.90, 0.12], center = true);
    color(GRAYC) translate([0, 0, 0.80]) cube([2.20, 0.10, 1.50], center = true);
    for (sy = [-1, 1], lv = [0 : 2])
    {
        color([0.66, 0.68, 0.71]) translate([0, 0.25 * sy, 0.42 + lv * 0.42]) cube([2.16, 0.42, 0.04], center = true);
        for (i = [0 : 6])
            color(goods6((i + lv * 2 + (sy + 1)) % 6))
                translate([-0.90 + i * 0.30, 0.25 * sy, 0.56 + lv * 0.42]) cube([0.24, 0.30, 0.24], center = true);
    }
}

// 饮料冷柜
module furn_fridge_case(c = [0.80, 0.30, 0.28])
{
    color(WHITEC) translate([0, 0, 0.95]) cube([1.00, 0.60, 1.90], center = true);
    color(c) translate([0, -0.02, 1.78]) cube([1.02, 0.58, 0.24], center = true);
    color(GLASSC) translate([0, -0.305, 0.86]) cube([0.86, 0.04, 1.46], center = true);
    for (lv = [0 : 2], i = [0 : 3])
        color(goods6((i + lv) % 6)) translate([-0.30 + i * 0.20, -0.18, 0.38 + lv * 0.46]) cube([0.13, 0.13, 0.30], center = true);
}

// 便利店收银台
module furn_checkout()
{
    color(AIRBLUE) translate([0, 0, 0.47]) cube([1.50, 0.60, 0.94], center = true);
    color(WHITEC) translate([0, 0, 0.965]) cube([1.60, 0.68, 0.05], center = true);
    translate([-0.35, 0.10, 0.99]) rotate([0, 0, 180]) furn_monitor(SCREENC, 0.34);
    color(BLACKC) translate([0.30, 0.0, 1.02]) cube([0.30, 0.30, 0.06], center = true);
    color(METALC) translate([0.62, -0.18, 0.99]) cube([0.10, 0.10, 0.14], center = true);
}

// 卫生间门 + 图标牌（front=-y；female=true 为裙装图标）
module furn_wc_door(female = false)
{
    color(DARKMETC)
    {
        for (sx = [-1, 1]) translate([0.50 * sx, 0, 1.05]) cube([0.10, 0.18, 2.10], center = true);
        translate([0, 0, 2.13]) cube([1.10, 0.18, 0.10], center = true);
    }
    color([0.25, 0.35, 0.55]) translate([0.04, 0.02, 1.02]) cube([0.90, 0.07, 2.02], center = true);
    color(METALC) translate([-0.30, -0.06, 1.00]) cube([0.04, 0.05, 0.16], center = true);
    // 图标牌
    color(SIGNBLUE) translate([0.92, 0.0, 1.70]) cube([0.50, 0.08, 0.50], center = true);
    color(WHITEC) translate([0.92, -0.05, 1.81]) sphere(r = 0.055);
    if (female)
        color(WHITEC) translate([0.92, -0.05, 1.60]) cylinder(h = 0.18, r1 = 0.105, r2 = 0.02, $fn = 12);
    else
        color(WHITEC) translate([0.92, -0.05, 1.60]) cube([0.13, 0.05, 0.20], center = true);
}

// 卫生间室内（切顶可见）：2 隔间 + 马桶 + 洗手台 + 镜子（沿 +x 约 4.6 宽、+y 约 2.0 深）
module furn_wc_interior()
{
    for (i = [0 : 1])
        translate([i * 1.05, 0, 0])
        {
            color([0.88, 0.86, 0.92]) translate([0.50, 0.55, 0.90]) cube([0.05, 1.10, 1.80], center = true);  // 隔板
            color(WHITEC)
            {
                translate([0, 0.85, 0.21]) cube([0.40, 0.45, 0.42], center = true);       // 马桶座
                translate([0, 1.04, 0.55]) cube([0.42, 0.16, 0.50], center = true);       // 水箱
            }
        }
    // 洗手台 + 镜子（右侧）
    color(WHITEC) translate([3.55, 0.90, 0.42]) cube([1.30, 0.50, 0.84], center = true);
    color([0.62, 0.78, 0.88]) for (i = [0 : 1])
        translate([3.25 + i * 0.62, 0.88, 0.875]) cylinder(h = 0.05, r = 0.16, $fn = 14);
    color(METALC) for (i = [0 : 1])
        translate([3.25 + i * 0.62, 1.06, 0.92]) cube([0.05, 0.05, 0.14], center = true);
    color([0.75, 0.85, 0.92]) translate([3.55, 1.12, 1.55]) cube([1.30, 0.04, 0.80], center = true);
}

module prop_fountain()
{
    color(METALC) translate([0, 0, 0.42]) cube([0.36, 0.30, 0.84], center = true);
    color([0.80, 0.82, 0.85]) translate([0, -0.05, 0.875]) cube([0.32, 0.26, 0.07], center = true);
    color(DARKMETC) translate([0, -0.08, 0.93]) cube([0.05, 0.05, 0.05], center = true);
}

module furn_atm()
{
    color([0.55, 0.58, 0.64]) translate([0, 0, 0.80]) cube([0.75, 0.45, 1.60], center = true);
    color(SIGNBLUE) translate([0, -0.01, 1.50]) cube([0.77, 0.45, 0.20], center = true);
    color(BLACKC) translate([0, -0.235, 1.12]) rotate([12, 0, 0]) cube([0.50, 0.05, 0.36], center = true);
    color(SCREENC) translate([0, -0.250, 1.12]) rotate([12, 0, 0]) cube([0.42, 0.02, 0.28], center = true);
    color(METALC) translate([0, -0.235, 0.82]) cube([0.40, 0.04, 0.06], center = true);
    color(BLACKC) translate([0, -0.235, 0.66]) cube([0.30, 0.04, 0.05], center = true);
}

module furn_vending(c = [0.80, 0.30, 0.28])
{
    color(c) translate([0, 0, 0.90]) cube([0.92, 0.50, 1.80], center = true);
    color(GLASSC) translate([-0.12, -0.26, 1.05]) cube([0.56, 0.04, 1.20], center = true);
    for (lv = [0 : 3], i = [0 : 2])
        color(goods6((i + lv * 2) % 6)) translate([-0.28 + i * 0.17, -0.18, 0.62 + lv * 0.30]) cube([0.11, 0.11, 0.22], center = true);
    color(BLACKC) translate([0.32, -0.26, 1.30]) cube([0.16, 0.03, 0.50], center = true);
    color(BLACKC) translate([-0.10, -0.26, 0.32]) cube([0.50, 0.04, 0.22], center = true);
}

// 问询台（胶囊形 + i 标识）
module furn_info_desk()
{
    color(AIRBLUE) translate([0, 0, 0.53]) hull()
    {
        translate([-0.65, 0, 0]) cylinder(h = 1.06, r = 0.55, $fn = 24);
        translate([0.65, 0, 0]) cylinder(h = 1.06, r = 0.55, $fn = 24);
    }
    color(WHITEC) translate([0, 0, 1.06]) hull()
    {
        translate([-0.65, 0, 0]) cylinder(h = 0.06, r = 0.62, $fn = 24);
        translate([0.65, 0, 0]) cylinder(h = 0.06, r = 0.62, $fn = 24);
    }
    color(DARKMETC) translate([0, 0.30, 1.12]) cylinder(h = 1.30, r = 0.03, $fn = 8);
    color(SIGNBLUE) translate([0, 0.30, 2.60]) rotate([90, 0, 0]) cylinder(h = 0.06, r = 0.30, $fn = 20, center = true);
    color(WHITEC) translate([-0.045, 0.26, 2.42]) rotate([90, 0, 0]) linear_extrude(0.03) text("i", size = 0.34);
    translate([0.3, 0.95, 0]) furn_task_chair();
    color(PAPERC) translate([-0.55, 0.10, 1.13]) rotate([0, 0, 18]) cube([0.22, 0.28, 0.012], center = true);
}

// 员工办公桌（紧凑版）
module furn_staff_desk()
{
    color(OAKC) translate([0, 0, 0.70]) cube([1.50, 0.75, 0.05], center = true);
    color([0.60, 0.42, 0.25]) for (sx = [-1, 1]) translate([0.70 * sx, 0, 0.34]) cube([0.06, 0.70, 0.68], center = true);
    translate([0, 0.15, 0.725]) furn_monitor();
    color(BLACKC) translate([0, -0.16, 0.74]) cube([0.36, 0.12, 0.02], center = true);
    color(PAPERC) translate([-0.50, 0.10, 0.73]) rotate([0, 0, -12]) cube([0.22, 0.28, 0.012], center = true);
    translate([0, -0.80, 0]) furn_task_chair();
}

module prop_server_rack()
{
    color([0.12, 0.13, 0.15]) translate([0, 0, 0.90]) cube([0.70, 0.60, 1.80], center = true);
    color([0.07, 0.08, 0.09]) translate([0, -0.29, 0.90]) cube([0.58, 0.03, 1.60], center = true);
    for (r = [0 : 6])
    {
        color((r % 3 == 0) ? [0.95, 0.70, 0.25] : [0.35, 0.90, 0.45])
            translate([-0.19, -0.315, 0.26 + r * 0.20]) cube([0.025, 0.015, 0.025], center = true);
        color([0.22, 0.24, 0.27]) translate([0.07, -0.315, 0.26 + r * 0.20]) cube([0.32, 0.012, 0.05], center = true);
    }
}

module prop_clock()
{
    rotate([90, 0, 0])
    {
        color(DARKMETC) cylinder(h = 0.04, r = 0.18, $fn = 24);
        color(WHITEC) translate([0, 0, 0.041]) cylinder(h = 0.012, r = 0.148, $fn = 24);
        color(BLACKC) translate([0, 0.045, 0.056]) cube([0.016, 0.10, 0.008], center = true);
        color(BLACKC) rotate([0, 0, -60]) translate([0, 0.032, 0.056]) cube([0.016, 0.075, 0.008], center = true);
    }
}

module prop_bins()
{
    color([0.25, 0.45, 0.75]) translate([-0.17, 0, 0]) cylinder(h = 0.36, r = 0.14, $fn = 12);
    color(GRAYC) translate([0.17, 0, 0]) cylinder(h = 0.36, r = 0.14, $fn = 12);
}

module prop_palm()
{
    color(POTC) cylinder(h = 0.34, r1 = 0.17, r2 = 0.21);
    color([0.48, 0.36, 0.22]) translate([0, 0, 0.30]) cylinder(h = 1.00, r = 0.05, $fn = 8);
    color([0.50, 0.38, 0.24]) translate([0.05, 0.02, 1.10]) cylinder(h = 0.40, r = 0.04, $fn = 8);
    for (a = [0 : 60 : 300])
        color((a % 120 == 0) ? PLANTC : PLANTDC)
            rotate([0, 0, a]) translate([0.42, 0.02 * a / 60, 1.52]) rotate([0, 28, 0]) cube([0.95, 0.20, 0.035], center = true);
    color(PLANTDC) translate([0.05, 0.02, 1.52]) sphere(r = 0.11);
}

module prop_planter(len = 1.6)
{
    color(WHITEC) translate([0, 0, 0.21]) cube([len, 0.34, 0.42], center = true);
    color([0.22, 0.15, 0.09]) translate([0, 0, 0.425]) cube([len - 0.06, 0.28, 0.02], center = true);
    for (i = [0 : floor(len / 0.30) - 1])
        color((i % 2 == 0) ? PLANTC : PLANTDC)
            translate([-len / 2 + 0.22 + i * 0.30, 0, 0.50]) sphere(r = 0.13);
}

// ================= 室外库（车辆/飞机/街具；落地件底面 z=0） =================
// 客机（机鼻朝 +x，轮底 z=0）：圆柱机身 + 后掠翼 + 吊挂发动机 + 垂尾
module veh_airliner(body = [0.94, 0.95, 0.96], accent = [0.25, 0.45, 0.75])
{
    color(body)
    {
        translate([-5.5, 0, 1.75]) rotate([0, 90, 0]) cylinder(h = 11.0, r = 1.05, $fn = 28);
        translate([5.5, 0, 1.75]) sphere(r = 1.05);
        translate([-5.5, 0, 1.78]) rotate([0, -85, 0]) cylinder(h = 2.9, r1 = 1.0, r2 = 0.28, $fn = 24);
    }
    color(accent) translate([-0.6, 0, 1.18]) cube([11.0, 2.12, 0.34], center = true);   // 腰线
    color(BLACKC) translate([4.9, 0, 2.15]) cube([0.80, 2.08, 0.30], center = true);    // 驾驶舱窗带
    for (sy = [-1, 1])
    {
        color([0.16, 0.22, 0.34]) translate([-0.4, 1.02 * sy, 2.10]) cube([8.6, 0.06, 0.17], center = true);  // 舷窗带
        color([0.30, 0.34, 0.42]) translate([3.85, 1.03 * sy, 1.95]) cube([0.42, 0.05, 0.85], center = true); // 前舱门
        // 主翼（后掠 28°，向机尾方向）
        color(body) translate([0.9, 0, 1.45]) rotate([0, 0, 28 * sy])
            translate([0, 2.85 * sy, 0]) cube([1.95, 5.7, 0.13], center = true);
        // 发动机
        color(METALC) translate([1.45, 2.25 * sy, 0.98]) rotate([0, 90, 0]) cylinder(h = 1.45, r = 0.46, $fn = 18);
        color(BLACKC) translate([1.42, 2.25 * sy, 0.98]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.40, $fn = 18);
        color(body) translate([2.2, 2.25 * sy, 1.36]) cube([0.85, 0.16, 0.45], center = true);   // 吊架
        // 平尾
        color(body) translate([-7.0, 0, 2.30]) rotate([0, 0, 32 * sy])
            translate([0, 1.0 * sy, 0]) cube([0.80, 2.0, 0.09], center = true);
    }
    // 垂尾 + 尾标
    color(body) translate([-7.0, 0, 3.30]) rotate([0, -36, 0]) cube([0.95, 0.13, 3.10], center = true);
    color(accent) translate([-7.85, 0, 4.30]) rotate([0, -36, 0]) cube([0.98, 0.15, 1.10], center = true);
    // 起落架
    color(DARKMETC)
    {
        translate([4.4, 0, 0.26]) cylinder(h = 0.55, r = 0.07, $fn = 8);
        for (sy = [-1, 1]) translate([-0.3, 0.85 * sy, 0.32]) cylinder(h = 0.50, r = 0.09, $fn = 8);
    }
    color(BLACKC)
    {
        for (sy = [-1, 1]) translate([4.4, 0.12 * sy, 0.26]) rotate([90, 0, 0]) cylinder(h = 0.10, r = 0.26, $fn = 14, center = true);
        for (sy = [-1, 1], sx = [-1, 1]) translate([-0.3 + 0.18 * sx, 0.85 * sy, 0.32]) rotate([90, 0, 0]) cylinder(h = 0.14, r = 0.32, $fn = 14, center = true);
    }
}

// 轿车（车头 +x，轮底 z=0）
module veh_car(c = [0.75, 0.28, 0.24])
{
    color(c) translate([0, 0, 0.71]) cube([4.10, 1.78, 0.62], center = true);
    color([0.20, 0.26, 0.34]) translate([-0.20, 0, 1.22]) cube([1.95, 1.68, 0.42], center = true);
    color(c) translate([-0.20, 0, 1.46]) cube([2.05, 1.72, 0.10], center = true);
    color([0.95, 0.92, 0.75]) for (sy = [-1, 1]) translate([2.06, 0.60 * sy, 0.78]) cube([0.04, 0.30, 0.14], center = true);
    color([0.80, 0.25, 0.20]) for (sy = [-1, 1]) translate([-2.06, 0.60 * sy, 0.78]) cube([0.04, 0.30, 0.14], center = true);
    color(BLACKC) for (sx = [-1, 1], sy = [-1, 1])
        translate([1.30 * sx, 0.92 * sy, 0.32]) rotate([90, 0, 0]) cylinder(h = 0.22, r = 0.32, $fn = 14, center = true);
}

module veh_taxi()
{
    veh_car([0.92, 0.78, 0.20]);
    color(WHITEC) translate([-0.20, 0, 1.57]) cube([0.55, 0.24, 0.16], center = true);
    color(BLACKC) translate([-0.20, 0, 1.56]) cube([0.40, 0.26, 0.06], center = true);
}

// 公交车（车头 +x，front=-y 侧为车门）
module veh_bus(c = [0.25, 0.45, 0.75])
{
    color(c) translate([0, 0, 1.70]) cube([8.60, 2.35, 2.10], center = true);
    color(WHITEC) translate([0, 0, 2.80]) cube([8.60, 2.30, 0.14], center = true);
    color(GRAYC) translate([-1.5, 0, 2.92]) cube([1.60, 1.40, 0.18], center = true);
    color([0.16, 0.22, 0.34])
    {
        translate([4.31, 0, 2.05]) cube([0.05, 2.10, 0.95], center = true);
        for (sy = [-1, 1]) translate([0.6, 1.18 * sy, 2.15]) cube([6.6, 0.04, 0.78], center = true);
    }
    color([0.14, 0.18, 0.28])
    {
        translate([3.0, -1.19, 1.45]) cube([1.00, 0.05, 1.70], center = true);   // 前门
        translate([-1.4, -1.19, 1.45]) cube([1.20, 0.05, 1.70], center = true);  // 后门
    }
    color(WHITEC) translate([4.32, 0.7, 2.70]) cube([0.04, 0.80, 0.30], center = true);  // 路牌
    color(BLACKC) for (sx = [-1, 1], i = [0, 1])
        translate([2.9 * sx - 0.0 * i, (i == 0 ? -1 : 1) * 1.05, 0.42]) rotate([90, 0, 0]) cylinder(h = 0.24, r = 0.42, $fn = 14, center = true);
}

// 行李拖车头 + 行李板车
module veh_baggage_tug()
{
    color([0.90, 0.62, 0.20]) translate([0, 0, 0.62]) cube([1.70, 1.00, 0.46], center = true);
    color([0.16, 0.22, 0.34]) translate([0.45, 0, 1.18]) cube([0.70, 0.90, 0.55], center = true);
    color([0.90, 0.62, 0.20]) translate([0.45, 0, 1.50]) cube([0.80, 0.96, 0.10], center = true);
    color(DARKMETC) for (sx = [-1, 1]) translate([0.45, 0.44 * sx, 0.95]) cube([0.06, 0.06, 1.05], center = true);
    color(BLACKC) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, 0.55 * sy, 0.24]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.24, $fn = 12, center = true);
}

module veh_baggage_cart(c1 = [0.72, 0.40, 0.26], c2 = [0.35, 0.55, 0.75])
{
    color(GRAYC) translate([0, 0, 0.46]) cube([1.55, 0.95, 0.10], center = true);
    color(GRAYC) for (sx = [-1, 1]) translate([0.72 * sx, 0, 0.95]) cube([0.06, 0.90, 0.90], center = true);
    color([0.62, 0.64, 0.66]) translate([0, 0, 1.42]) cube([1.60, 0.98, 0.06], center = true);
    color(c1) translate([-0.32, 0.05, 0.70]) cube([0.55, 0.70, 0.38], center = true);
    color(c2) translate([0.30, -0.08, 0.66]) cube([0.50, 0.60, 0.30], center = true);
    color([0.50, 0.55, 0.40]) translate([0.05, 0.10, 1.00]) cube([0.60, 0.55, 0.26], center = true);
    color(BLACKC) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, 0.42 * sy, 0.17]) rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.17, $fn = 10, center = true);
}

// 加油车
module veh_fuel_truck()
{
    color([0.90, 0.62, 0.20]) translate([1.65, 0, 1.05]) cube([1.30, 1.70, 1.10], center = true);
    color([0.16, 0.22, 0.34]) translate([2.10, 0, 1.42]) cube([0.45, 1.60, 0.55], center = true);
    color(GRAYC) translate([-0.4, 0, 0.62]) cube([3.40, 1.50, 0.35], center = true);
    color(METALC) translate([-1.9, 0, 1.25]) rotate([0, 90, 0]) cylinder(h = 3.0, r = 0.72, $fn = 20);
    color(METALC) translate([-1.95, 0, 1.25]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.76, $fn = 20);
    color(BLACKC) for (sx = [-1.4, 0.2, 1.6], sy = [-1, 1])
        translate([sx, 0.78 * sy, 0.34]) rotate([90, 0, 0]) cylinder(h = 0.20, r = 0.34, $fn = 12, center = true);
}

// 客梯车（台阶朝 +y 升高）
module veh_stairs_truck()
{
    color(GRAYC) translate([0, -0.9, 0.50]) cube([1.40, 1.60, 0.30], center = true);
    color(BLACKC) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, -0.9 + 0.55 * sy, 0.26]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.26, $fn = 12, center = true);
    for (i = [0 : 5])
        color(METALC) translate([0, -0.55 + i * 0.42, 0.50 + i * 0.21]) cube([1.10, 0.45, 0.10], center = true);
    color(METALC) translate([0, 1.75, 1.72]) cube([1.10, 0.65, 0.10], center = true);     // 顶部平台
    color(DARKMETC) for (sx = [-1, 1])
    {
        translate([0.53 * sx, 0.45, 1.45]) rotate([-26, 0, 0]) cube([0.05, 2.6, 0.05], center = true);
        translate([0.53 * sx, 1.75, 2.10]) cube([0.05, 0.65, 0.05], center = true);
    }
}

module prop_container(c = [0.55, 0.58, 0.64])
{
    color(c) translate([0, 0, 0.72]) cube([1.55, 1.40, 1.44], center = true);
    color([0.40, 0.42, 0.46]) translate([0.40, -0.705, 0.72]) cube([0.02, 0.012, 1.30], center = true);
    color([0.40, 0.42, 0.46]) translate([-0.40, -0.705, 0.72]) cube([0.02, 0.012, 1.30], center = true);
}

module prop_cone()
{
    color([0.90, 0.45, 0.18]) cylinder(h = 0.34, r1 = 0.10, r2 = 0.025, $fn = 10);
    color([0.90, 0.45, 0.18]) translate([0, 0, 0.005]) cube([0.26, 0.26, 0.02], center = true);
}

// 高杆机坪照明灯
module prop_light_mast()
{
    color(DARKMETC)
    {
        cylinder(h = 0.25, r = 0.22, $fn = 12);
        cylinder(h = 6.4, r = 0.09, $fn = 10);
        translate([0, 0, 6.30]) cube([1.60, 0.10, 0.10], center = true);
    }
    for (i = [0 : 3])
        color([0.95, 0.92, 0.78]) translate([-0.60 + i * 0.40, 0, 6.18]) cube([0.28, 0.16, 0.14], center = true);
}

module prop_windsock()
{
    color(METALC) cylinder(h = 3.0, r = 0.04, $fn = 8);
    color(WHITEC) translate([0, 0, 2.96]) sphere(r = 0.06);
    color([0.92, 0.50, 0.18]) translate([0.05, 0, 2.92]) rotate([0, 96, 8]) cylinder(h = 0.95, r1 = 0.16, r2 = 0.05, $fn = 10);
}

// 路灯 / 行道树 / 绿篱 / 室外长椅
module prop_lamp_post()
{
    color(DARKMETC)
    {
        cylinder(h = 0.18, r = 0.14, $fn = 10);
        cylinder(h = 4.4, r = 0.06, $fn = 8);
        translate([0, 0.35, 4.36]) cube([0.08, 0.80, 0.07], center = true);
    }
    color([0.95, 0.92, 0.78]) translate([0, 0.70, 4.30]) cube([0.22, 0.45, 0.12], center = true);
}

module prop_tree()
{
    color([0.48, 0.36, 0.22]) cylinder(h = 1.1, r = 0.12, $fn = 8);
    color(PLANTDC) translate([0, 0, 1.45]) sphere(r = 0.62);
    color(PLANTC) translate([0.25, 0.15, 1.85]) sphere(r = 0.45);
    color(PLANTC) translate([-0.28, -0.12, 1.70]) sphere(r = 0.38);
}

module prop_hedge(len = 2.2)
{
    color(PLANTDC) translate([0, 0, 0.30]) cube([len, 0.55, 0.60], center = true);
    color(PLANTC) translate([0.1, 0.05, 0.58]) cube([len - 0.35, 0.40, 0.14], center = true);
}

module prop_bench_out()
{
    color(DARKMETC) for (sx = [-1, 1]) translate([0.62 * sx, 0, 0.20]) cube([0.08, 0.50, 0.40], center = true);
    color(OAKC)
    {
        for (i = [0 : 2]) translate([0, -0.16 + i * 0.16, 0.43]) cube([1.55, 0.12, 0.05], center = true);
        translate([0, 0.26, 0.70]) rotate([12, 0, 0]) cube([1.55, 0.05, 0.45], center = true);
    }
}

// 公交候车亭（front=-y 朝马路）
module prop_bus_shelter()
{
    color(DARKMETC) for (sx = [-1, 1]) translate([1.45 * sx, 0.40, 1.25]) cube([0.10, 0.10, 2.50], center = true);
    color([0.55, 0.72, 0.85, 0.45]) translate([0, 0.46, 1.35]) cube([3.00, 0.05, 1.60], center = true);
    color(GRAYC) translate([0, 0.20, 2.56]) cube([3.30, 1.30, 0.10], center = true);
    color(DARKMETC) translate([0, 0.30, 0.42]) cube([2.40, 0.35, 0.08], center = true);
    color(DARKMETC) for (sx = [-1, 1]) translate([1.0 * sx, 0.30, 0.20]) cube([0.08, 0.30, 0.40], center = true);
    color(SIGNBLUE) translate([1.55, 0.40, 2.30]) cube([0.06, 0.45, 0.45], center = true);
}

// 停车场道闸 + P 牌
module prop_barrier_gate()
{
    color(GRAYC) translate([0, 0, 0.55]) cube([0.40, 0.35, 1.10], center = true);
    color(WHITEC) translate([1.35, 0, 0.98]) cube([2.60, 0.09, 0.10], center = true);
    color([0.80, 0.25, 0.20]) translate([2.50, 0, 0.98]) cube([0.30, 0.10, 0.11], center = true);
}

module prop_p_sign()
{
    color(DARKMETC) cylinder(h = 2.4, r = 0.05, $fn = 8);
    color(SIGNBLUE) translate([0, 0, 2.55]) cube([0.62, 0.08, 0.62], center = true);
    color(WHITEC) translate([-0.14, -0.045, 2.32]) rotate([90, 0, 0]) linear_extrude(0.03) text("P", size = 0.42);
}

// 机坪/陆侧分隔栅栏（沿 +x）
module prop_fence(len)
{
    for (p = [0 : 2 : len]) color(GRAYC) translate([p, 0, 0.60]) cube([0.08, 0.08, 1.20], center = true);
    color(GRAYC) translate([len / 2, 0, 1.14]) cube([len, 0.05, 0.06], center = true);
    color(GRAYC) translate([len / 2, 0, 0.30]) cube([len, 0.05, 0.06], center = true);
    color([0.70, 0.72, 0.75, 0.35]) translate([len / 2, 0, 0.72]) cube([len, 0.02, 0.80], center = true);
}

// 广告灯箱塔（双面彩屏）
module prop_ad_totem(c = [0.90, 0.55, 0.25])
{
    color(DARKMETC) translate([0, 0, 0.06]) cube([0.90, 0.34, 0.12], center = true);
    color(DARKMETC) translate([0, 0, 1.32]) cube([0.80, 0.16, 2.40], center = true);
    for (sy = [-1, 1])
    {
        color(c) translate([0, 0.085 * sy, 1.60]) cube([0.66, 0.012, 1.50], center = true);
        color(WHITEC) translate([0, 0.092 * sy, 1.18]) cube([0.50, 0.012, 0.14], center = true);
        color(WHITEC) translate([0, 0.092 * sy, 2.05]) cube([0.42, 0.012, 0.20], center = true);
    }
}

// 行李推车队（3 辆嵌套停放）
module prop_trolley_row()
{
    for (i = [0 : 2])
        translate([0, i * 0.42, 0])
        {
            color(METALC)
            {
                for (sx = [-1, 1]) translate([0.26 * sx, 0, 0.50]) cube([0.04, 0.66, 1.00], center = true);
                translate([0, 0.30, 0.96]) cube([0.56, 0.05, 0.05], center = true);    // 推把
                translate([0, -0.05, 0.32]) cube([0.52, 0.55, 0.04], center = true);   // 底篮
            }
            color(BLACKC) for (sx = [-1, 1], sy = [-1, 1])
                translate([0.24 * sx, 0.25 * sy, 0.085]) rotate([90, 0, 0]) cylinder(h = 0.04, r = 0.085, $fn = 10, center = true);
        }
}

// 书架（书店用，front = -y）
module prop_bookshelf_tall()
{
    color([0.42, 0.30, 0.20])
    {
        for (sx = [-1, 1]) translate([0.50 * sx, 0, 1.0]) cube([0.06, 0.34, 2.0], center = true);
        translate([0, 0.15, 1.0]) cube([1.06, 0.04, 2.0], center = true);
        for (i = [0 : 3]) translate([0, 0, 0.04 + i * 0.63]) cube([1.00, 0.32, 0.05], center = true);
        translate([0, 0, 1.975]) cube([1.06, 0.34, 0.05], center = true);
    }
    for (row = [0 : 2], i = [0 : 5])
        color(book5((i + row * 2) % 5))
            translate([-0.38 + i * 0.13, 0.03, 0.255 + row * 0.63 + ((i % 3 == 1) ? -0.02 : 0)])
                cube([0.085, 0.22, (i % 3 == 1) ? 0.34 : 0.38], center = true);
}

// ================= 商店细化库（front = -y） =================
// 快餐柜台：红色柜体 + 托盘滑轨 + 双收银 + 餐品 + 头顶菜单板（局部宽 5）
module furn_food_counter()
{
    color(REDFOOD) translate([0, 0, 0.50]) cube([5.00, 0.65, 1.00], center = true);
    color(WHITEC) translate([0, 0, 1.025]) cube([5.12, 0.72, 0.05], center = true);
    color(METALC) translate([0, -0.42, 0.88]) cube([4.60, 0.06, 0.04], center = true);   // 托盘轨
    color(METALC) for (sx = [-2.2, 0, 2.2]) translate([sx, -0.42, 0.70]) cube([0.05, 0.06, 0.36], center = true);
    for (sx = [-1.4, 1.4])
    {
        color(BLACKC) translate([sx, 0.08, 1.05]) cube([0.30, 0.24, 0.05], center = true);
        translate([sx, 0.18, 1.07]) rotate([0, 0, 180]) furn_monitor(SCREENC, 0.30);
    }
    // 餐品：汉堡 + 薯条盒 + 饮料杯 + 托盘
    color([0.85, 0.62, 0.30]) translate([-0.35, -0.10, 1.05]) cylinder(h = 0.05, r = 0.09, $fn = 12);
    color([0.55, 0.30, 0.16]) translate([-0.35, -0.10, 1.10]) cylinder(h = 0.04, r = 0.085, $fn = 12);
    color([0.90, 0.72, 0.38]) translate([-0.35, -0.10, 1.14]) scale([1, 1, 0.55]) sphere(r = 0.09);
    color(REDFOOD) translate([0.05, -0.12, 1.10]) cube([0.10, 0.10, 0.16], center = true);
    color([0.95, 0.82, 0.35]) translate([0.05, -0.12, 1.17]) cube([0.07, 0.07, 0.10], center = true);
    color([0.90, 0.30, 0.25]) translate([0.42, -0.08, 1.05]) cylinder(h = 0.16, r1 = 0.05, r2 = 0.065, $fn = 10);
    color([0.62, 0.64, 0.66]) translate([0.0, -0.05, 1.045]) cube([0.85, 0.50, 0.015], center = true);
    // 头顶菜单板
    color(DARKMETC) for (sx = [-1.9, 1.9]) translate([sx, 0.15, 1.85]) cube([0.07, 0.07, 1.60], center = true);
    for (i = [-1 : 1])
    {
        color([0.16, 0.13, 0.11]) translate([i * 1.45, 0.15, 2.30]) cube([1.32, 0.06, 0.80], center = true);
        color((i == 0) ? [0.85, 0.62, 0.30] : (i < 0) ? [0.90, 0.30, 0.25] : [0.95, 0.82, 0.35])
            translate([i * 1.45 - 0.30, 0.10, 2.42]) cube([0.45, 0.012, 0.40], center = true);
        color([0.92, 0.85, 0.70]) translate([i * 1.45 + 0.32, 0.10, 2.46]) cube([0.50, 0.012, 0.07], center = true);
        color([0.92, 0.85, 0.70]) translate([i * 1.45 + 0.30, 0.10, 2.28]) cube([0.46, 0.012, 0.07], center = true);
        color([0.92, 0.85, 0.70]) translate([i * 1.45 + 0.33, 0.10, 2.10]) cube([0.40, 0.012, 0.07], center = true);
    }
}

// 后厨条（贴墙）：钢制台 + 炸炉/扒炉 + 排烟罩
module furn_kitchen_strip(len = 5)
{
    color(METALC) translate([0, 0.05, 0.45]) cube([len, 0.70, 0.90], center = true);
    color([0.30, 0.32, 0.36])
    {
        translate([-len / 4, 0.0, 1.08]) cube([0.70, 0.55, 0.36], center = true);    // 炸炉
        translate([len / 4, 0.0, 1.00]) cube([0.80, 0.55, 0.20], center = true);     // 扒炉
    }
    color([0.95, 0.70, 0.25]) translate([-len / 4 - 0.12, -0.18, 1.28]) cube([0.10, 0.04, 0.04], center = true);
    color(METALC) translate([0, 0.10, 2.10]) cube([len * 0.7, 0.75, 0.45], center = true);   // 排烟罩
    color([0.55, 0.58, 0.62]) translate([0, 0.10, 1.60]) cube([0.35, 0.45, 0.55], center = true); // 烟道
}

// 书店展台：矮桌 + 平摊书堆 + 立书
module furn_book_table()
{
    color(OAKC) translate([0, 0, 0.70]) cube([1.50, 0.95, 0.06], center = true);
    color([0.60, 0.42, 0.25]) for (sx = [-1, 1]) translate([0.65 * sx, 0, 0.34]) cube([0.08, 0.85, 0.68], center = true);
    for (i = [0 : 2])
        color(book5(i)) translate([-0.45 + i * 0.45, 0.18, 0.765 + (i == 1 ? 0.02 : 0)])
            rotate([0, 0, i * 14 - 10]) cube([0.30, 0.40, 0.07 + i * 0.02], center = true);
    for (i = [0 : 2])
        color(book5(i + 2)) translate([-0.40 + i * 0.42, -0.24, 0.755]) rotate([0, 0, -i * 8]) cube([0.28, 0.38, 0.05], center = true);
    color(book5(4)) translate([0.55, 0.10, 0.90]) rotate([78, 0, -15]) cube([0.26, 0.36, 0.03], center = true);
}

// 杂志架（斜板三层）
module furn_mag_rack()
{
    color([0.60, 0.42, 0.25]) translate([0, 0.12, 0.80]) cube([1.20, 0.06, 1.60], center = true);
    color([0.60, 0.42, 0.25]) for (sx = [-1, 1]) translate([0.60 * sx, 0, 0.80]) cube([0.05, 0.32, 1.60], center = true);
    for (lv = [0 : 2])
    {
        color([0.70, 0.52, 0.33]) translate([0, -0.02, 0.40 + lv * 0.48]) rotate([16, 0, 0]) cube([1.12, 0.04, 0.34], center = true);
        for (i = [0 : 3])
            color(goods6((i + lv * 2 + 1) % 6))
                translate([-0.41 + i * 0.27, -0.05, 0.43 + lv * 0.48]) rotate([16, 0, 0]) cube([0.22, 0.015, 0.30], center = true);
    }
}

// 礼品展示柜：白基座 + 玻璃罩（items=false 时罩内留空，用于摆大件）
module furn_display_case(items = true)
{
    color(WHITEC) translate([0, 0, 0.36]) cube([1.60, 0.70, 0.72], center = true);
    color(GLASSC) translate([0, 0, 1.11]) cube([1.50, 0.60, 0.78], center = true);
    color(METALC) translate([0, 0, 1.515]) cube([1.54, 0.64, 0.03], center = true);
    if (items)
    {
        color(goods6(0)) translate([-0.45, 0.05, 0.82]) cube([0.18, 0.18, 0.20], center = true);
        color(goods6(2)) translate([0.0, -0.08, 0.80]) cube([0.15, 0.15, 0.16], center = true);
        color(goods6(4)) translate([0.40, 0.06, 0.84]) cube([0.16, 0.16, 0.24], center = true);
        color([0.85, 0.75, 0.40]) translate([0.18, 0.14, 0.72]) cylinder(h = 0.22, r = 0.05, $fn = 10);
    }
}

// 冰柜（卧式 + 玻璃顶）
module furn_freezer_chest()
{
    color(WHITEC) translate([0, 0, 0.42]) cube([1.30, 0.68, 0.84], center = true);
    color([0.55, 0.72, 0.85, 0.40]) translate([0, -0.05, 0.865]) cube([1.18, 0.50, 0.04], center = true);
    for (i = [0 : 3])
        color(goods6((i * 2 + 1) % 6)) translate([-0.42 + i * 0.28, -0.05, 0.74]) cube([0.20, 0.40, 0.10], center = true);
    color(SIGNBLUE) translate([0, 0.345, 0.62]) cube([1.30, 0.012, 0.28], center = true);
}

// 室内玻璃隔断（员工办公室用，h2.4，可留门洞；同 office.scad）
module part_glass_run(x0, x1)
{
    color(DARKMETC) translate([(x0 + x1) / 2, 0, 0.06]) cube([x1 - x0, 0.10, 0.12], center = true);
    color(GLASSC) translate([(x0 + x1) / 2, 0, 1.21]) cube([x1 - x0 - 0.02, 0.05, 2.06], center = true);
}

module part_glass_wall(len, g0 = -1, g1 = -1)
{
    color(DARKMETC) translate([len / 2, 0, 2.30]) cube([len, 0.10, 0.12], center = true);
    if (g0 >= 0) { part_glass_run(0, g0); part_glass_run(g1, len); }
    else         part_glass_run(0, len);
    if (g0 >= 0) { for (p = [0, g0, g1, len]) color(DARKMETC) translate([p, 0, 1.18]) cube([0.09, 0.11, 2.36], center = true); }
    else         { for (p = [0, len])         color(DARKMETC) translate([p, 0, 1.18]) cube([0.09, 0.11, 2.36], center = true); }
}

// ================= 功能点位（锚点；translate 必须在 rotate 外层） =================
module entrance_01() furn_entrance();
module entrance_02() furn_entrance();
module entrance_03() furn_entrance();
module checkin_01() furn_checkin_desk("1");
module checkin_02() furn_checkin_desk("2");
module checkin_03() furn_checkin_desk("3");
module checkin_04() furn_checkin_desk("4");
module checkin_05() furn_checkin_desk("5");
module checkin_06() furn_checkin_desk("6");
module kiosk_01() furn_kiosk();
module kiosk_02() furn_kiosk();
module kiosk_03() furn_kiosk();
module kiosk_04() furn_kiosk();
module security_01() furn_security_lane();
module security_02() furn_security_lane();
module security_03() furn_security_lane();
module security_04() furn_security_lane();
module gate_01() furn_gate_door("GATE 1");
module gate_02() furn_gate_door("GATE 2");
module gate_03() furn_gate_door("GATE 3");
module gate_04() furn_gate_door("GATE 4");
module gate_05() furn_gate_door("GATE 5");
module gate_06() furn_gate_door("GATE 6");
module wait_01() furn_bench_row();
module wait_02() furn_bench_row();
module wait_03() furn_bench_row();
module wait_04() furn_bench_row();
module wait_05() furn_bench_row();
module wait_06() furn_bench_row();
module wait_07() furn_bench_row();
module wait_08() furn_bench_row();
module wait_09() furn_bench_row();
module wait_10() furn_bench_row();
module wait_11() furn_bench_row();
module wait_12() furn_bench_row();
module cafe_01() furn_cafe_counter();
module food_01() furn_food_counter();
module shop_01() furn_checkout();
module book_01() furn_checkout();
module gift_01() furn_checkout();
module toilet_01() furn_wc_door(false);
module toilet_02() furn_wc_door(true);
module staff_01() furn_staff_desk();
module staff_02() furn_staff_desk();
module info_01() furn_info_desk();
module atm_01() furn_atm();
module vending_01() furn_vending([0.80, 0.30, 0.28]);
module vending_02() furn_vending([0.25, 0.45, 0.75]);
module vending_03() furn_vending([0.30, 0.60, 0.45]);
module vending_04() furn_vending([0.90, 0.62, 0.20]);
module vending_05() furn_vending([0.55, 0.45, 0.75]);

// ======================== 布局 ========================
FZ = 0.15;   // 地面顶面

ground_base();
ground_carpet();
ground_apron();
ground_landside();

// ---- 玻璃幕墙（南墙三入口洞，北墙六登机口洞） ----
translate([-30, -12, FZ]) wall_glass_seg(10.5);
translate([-16.5, -12, FZ]) wall_glass_seg(15.0);
translate([1.5, -12, FZ]) wall_glass_seg(11.0);
translate([15.5, -12, FZ]) wall_glass_seg(14.5);
translate([-30, 28, FZ]) wall_glass_seg(9.1);
translate([-19.1, 28, FZ]) wall_glass_seg(6.2);
translate([-11.1, 28, FZ]) wall_glass_seg(6.2);
translate([-3.1, 28, FZ]) wall_glass_seg(6.2);
translate([4.9, 28, FZ]) wall_glass_seg(6.2);
translate([12.9, 28, FZ]) wall_glass_seg(6.2);
translate([20.9, 28, FZ]) wall_glass_seg(9.1);
translate([-30, -12, FZ]) rotate([0, 0, 90]) wall_glass_seg(40);
translate([30, -12, FZ]) rotate([0, 0, 90]) wall_glass_seg(40);
translate([-30, -12, 1.66 + FZ]) wall_corner_col();
translate([-30, 28, 1.66 + FZ]) wall_corner_col();
translate([30, 28, 1.66 + FZ]) wall_corner_col();
translate([30, -12, 1.66 + FZ]) wall_corner_col();
// 入口 + 大招牌
translate([-18, -12, FZ]) entrance_01();
translate([0, -12, FZ]) entrance_02();
translate([14, -12, FZ]) entrance_03();
translate([-18, -12.25, 3.70 + FZ]) prop_big_sign("AIRPORT");
translate([0, -12.25, 3.70 + FZ]) prop_big_sign("AIRPORT");

// ---- 值机区（西北陆侧）：6 柜台 + 行李墙 + 排队线 + 航显 ----
translate([-27.0, 1.2, FZ]) checkin_01();
translate([-24.4, 1.2, FZ]) checkin_02();
translate([-21.8, 1.2, FZ]) checkin_03();
translate([-19.2, 1.2, FZ]) checkin_04();
translate([-16.6, 1.2, FZ]) checkin_05();
translate([-14.0, 1.2, FZ]) checkin_06();
translate([-30, 4, FZ]) wall_solid_seg(19.2, [0.88, 0.87, 0.84]);
translate([-10.8, 4, FZ]) wall_solid_seg(1.0, [0.88, 0.87, 0.84]);
translate([-27.5, -0.6, FZ]) prop_queue_line(13);
translate([-27.5, -1.8, FZ]) prop_queue_line(13);
translate([-21, 3.3, FZ]) prop_fids();

// ---- 自助值机（值机区东侧，面东） ----
translate([-10.5, 0.6, FZ]) rotate([0, 0, 90]) kiosk_01();
translate([-10.5, -0.8, FZ]) rotate([0, 0, 90]) kiosk_02();
translate([-10.5, -2.2, FZ]) rotate([0, 0, 90]) kiosk_03();
translate([-10.5, -3.6, FZ]) rotate([0, 0, 90]) kiosk_04();

// ---- 安检区（中央，南进北出）：4 通道 + 排队线 + 指示牌 + 东侧玻璃界墙 ----
translate([-9.3, 4.6, FZ]) security_01();
translate([-7.1, 4.6, FZ]) security_02();
translate([-4.9, 4.6, FZ]) security_03();
translate([-2.7, 4.6, FZ]) security_04();
translate([-0.7, 4, FZ]) part_glass_wall(30.7, 24.7, 26.1);
translate([-10.2, 2.6, FZ]) prop_queue_line(9);
translate([-10.2, 1.5, FZ]) prop_queue_line(9);
translate([-5.8, 0.6, FZ]) prop_hang_sign("DEPARTURES");
translate([-5, 9.6, FZ]) prop_hang_sign("ALL GATES");
translate([24.7, 4.4, FZ]) prop_hang_sign("EXIT", [0.22, 0.58, 0.35], false);
translate([24.0, 3.0, FZ]) prop_stanchion();
translate([25.4, 3.0, FZ]) prop_stanchion();

// ---- 登机口（北墙 6 门） ----
translate([-20, 27.6, FZ]) gate_01();
translate([-12, 27.6, FZ]) gate_02();
translate([-4, 27.6, FZ]) gate_03();
translate([4, 27.6, FZ]) gate_04();
translate([12, 27.6, FZ]) gate_05();
translate([20, 27.6, FZ]) gate_06();

// ---- 候机区（东西双集群 12 组排椅）+ 绿植 + 航显 ----
translate([-22.0, 16.5, FZ]) rotate([0, 0, 180]) wait_01();
translate([-18.6, 16.5, FZ]) rotate([0, 0, 180]) wait_02();
translate([-15.2, 16.5, FZ]) rotate([0, 0, 180]) wait_03();
translate([-22.0, 19.5, FZ]) rotate([0, 0, 180]) wait_04();
translate([-18.6, 19.5, FZ]) rotate([0, 0, 180]) wait_05();
translate([-15.2, 19.5, FZ]) rotate([0, 0, 180]) wait_06();
translate([9.0, 16.5, FZ]) rotate([0, 0, 180]) wait_07();
translate([12.4, 16.5, FZ]) rotate([0, 0, 180]) wait_08();
translate([15.8, 16.5, FZ]) rotate([0, 0, 180]) wait_09();
translate([9.0, 19.5, FZ]) rotate([0, 0, 180]) wait_10();
translate([12.4, 19.5, FZ]) rotate([0, 0, 180]) wait_11();
translate([15.8, 19.5, FZ]) rotate([0, 0, 180]) wait_12();
translate([-18.6, 18.0, FZ]) prop_planter(2.4);
translate([12.4, 18.0, FZ]) prop_planter(2.4);
translate([1.0, 21.5, FZ]) prop_fids();
translate([-6, 10.2, FZ]) prop_fids();
translate([-29.55, 18.6, FZ]) rotate([0, 0, 90]) vending_03();
translate([-29.5, 17.2, FZ]) rotate([0, 0, 90]) prop_fountain();
translate([29.55, 11.4, FZ]) rotate([0, 0, -90]) vending_01();
translate([29.55, 12.6, FZ]) rotate([0, 0, -90]) vending_02();

// ---- 西餐饮区：BURGER 快餐（后厨 + 柜台 + 门头 + 6 桌）----
translate([-27.6, 7, FZ]) rotate([0, 0, 90]) wall_solid_seg(6.4, [0.55, 0.35, 0.22]);
translate([-29.3, 10.0, FZ]) rotate([0, 0, 90]) furn_kitchen_strip(5);
translate([-26.8, 10.2, FZ]) rotate([0, 0, 90]) food_01();
translate([-25.9, 10.2, FZ]) rotate([0, 0, 90]) prop_shop_portal(6.4, "BURGER", REDFOOD);
translate([-23.0, 8.5, FZ]) furn_cafe_table(REDFOOD);
translate([-19.6, 8.5, FZ]) rotate([0, 0, 55]) furn_cafe_table(REDFOOD);
translate([-16.2, 8.5, FZ]) rotate([0, 0, -30]) furn_cafe_table(REDFOOD);
translate([-23.0, 11.8, FZ]) rotate([0, 0, 90]) furn_cafe_table(REDFOOD);
translate([-19.6, 11.8, FZ]) rotate([0, 0, -70]) furn_cafe_table(REDFOOD);
translate([-16.2, 11.8, FZ]) rotate([0, 0, 20]) furn_cafe_table(REDFOOD);
translate([-13.2, 10.0, FZ]) rotate([0, 0, 90]) prop_planter(3.0);

// ---- 中央咖啡岛：柜台 + 吊牌 + 3 桌 ----
translate([-7, 12.5, FZ]) cafe_01();
translate([-7, 13.5, FZ]) prop_hang_sign("COFFEE", BROWNC, false);
translate([-9.8, 14.8, FZ]) rotate([0, 0, 30]) furn_cafe_table();
translate([-6.2, 15.4, FZ]) rotate([0, 0, -45]) furn_cafe_table();
translate([-3.4, 13.6, FZ]) rotate([0, 0, 80]) furn_cafe_table();

// ---- 东零售街三连铺（front y=8.4，背墙 y=13.9） ----
// SHOP 便利店 x∈[3,11]
translate([3, 13.9, FZ]) wall_solid_seg(8.0, SHOPWALL);
translate([3, 8.4, FZ]) rotate([0, 0, 90]) wall_solid_seg(5.5, SHOPWALL);
translate([11, 8.4, FZ]) rotate([0, 0, 90]) wall_solid_seg(5.5, SHOPWALL);
translate([7, 8.5, FZ]) prop_shop_portal(7.6, "SHOP", AIRBLUE);
translate([4.4, 9.4, FZ]) rotate([0, 0, 180]) shop_01();
translate([5.9, 10.8, FZ]) furn_gondola();
translate([8.7, 10.8, FZ]) furn_gondola();
translate([5.9, 12.3, FZ]) furn_gondola();
translate([8.7, 12.3, FZ]) furn_gondola();
translate([4.0, 13.4, FZ]) furn_fridge_case([0.80, 0.30, 0.28]);
translate([5.1, 13.4, FZ]) furn_fridge_case([0.25, 0.45, 0.75]);
translate([6.2, 13.4, FZ]) furn_fridge_case([0.30, 0.60, 0.45]);
translate([9.9, 13.3, FZ]) furn_freezer_chest();
translate([10.6, 9.6, FZ]) rotate([0, 0, -90]) furn_mag_rack();
// BOOKS 书店 x∈[12,19]
translate([12, 13.9, FZ]) wall_solid_seg(7.0, [0.62, 0.50, 0.38]);
translate([12, 8.4, FZ]) rotate([0, 0, 90]) wall_solid_seg(5.5, [0.62, 0.50, 0.38]);
translate([19, 8.4, FZ]) rotate([0, 0, 90]) wall_solid_seg(5.5, [0.62, 0.50, 0.38]);
translate([15.5, 8.5, FZ]) prop_shop_portal(6.6, "BOOKS", BROWNC);
translate([13.4, 9.4, FZ]) rotate([0, 0, 180]) book_01();
translate([13.4, 13.45, FZ]) prop_bookshelf_tall();
translate([14.5, 13.45, FZ]) prop_bookshelf_tall();
translate([15.6, 13.45, FZ]) prop_bookshelf_tall();
translate([16.7, 13.45, FZ]) prop_bookshelf_tall();
translate([18.6, 10.6, FZ]) rotate([0, 0, -90]) prop_bookshelf_tall();
translate([18.6, 12.0, FZ]) rotate([0, 0, -90]) prop_bookshelf_tall();
translate([15.5, 11.0, FZ]) rotate([0, 0, 8]) furn_book_table();
translate([12.4, 10.8, FZ]) rotate([0, 0, 90]) furn_mag_rack();
// GIFTS 礼品店 x∈[20,28]
translate([20, 13.9, FZ]) wall_solid_seg(8.0, TEALC);
translate([20, 8.4, FZ]) rotate([0, 0, 90]) wall_solid_seg(5.5, TEALC);
translate([28, 8.4, FZ]) rotate([0, 0, 90]) wall_solid_seg(5.5, TEALC);
translate([24, 8.5, FZ]) prop_shop_portal(7.6, "GIFTS", TEALC);
translate([21.4, 9.4, FZ]) rotate([0, 0, 180]) gift_01();
translate([23.4, 11.2, FZ]) furn_display_case();
translate([26.2, 11.2, FZ]) furn_display_case(false);
translate([26.2, 11.2, 0.72 + FZ]) rotate([0, 0, -15]) scale([0.085, 0.085, 0.085])
    veh_airliner([0.92, 0.78, 0.20], [0.78, 0.25, 0.20]);   // 玻璃罩里的飞机模型
translate([24.8, 13.2, FZ]) furn_gondola();
translate([21.2, 12.8, FZ]) furn_freezer_chest();
translate([27.3, 9.8, FZ]) rotate([0, 0, -90]) furn_mag_rack();

// ---- 卫生间（东北空侧）：实墙围合 + 男/女门 + 室内 + 饮水器 ----
translate([24, 22, FZ]) wall_solid_seg(6.0);
translate([24, 16, FZ]) wall_solid_seg(6.0);
translate([24, 19, FZ]) wall_solid_seg(6.0);
translate([30, 16, FZ]) rotate([0, 0, 90]) wall_solid_seg(6.0);
translate([24, 16, FZ]) rotate([0, 0, 90]) wall_solid_seg(0.85);
translate([24, 17.95, FZ]) rotate([0, 0, 90]) wall_solid_seg(2.1);
translate([24, 21.15, FZ]) rotate([0, 0, 90]) wall_solid_seg(0.85);
translate([24, 17.4, FZ]) rotate([0, 0, -90]) toilet_01();
translate([24, 20.6, FZ]) rotate([0, 0, -90]) toilet_02();
translate([24.7, 17.6, FZ]) furn_wc_interior();
translate([29.3, 20.4, FZ]) rotate([0, 0, 180]) furn_wc_interior();
translate([25.0, 15.4, FZ]) prop_fountain();

// ---- 员工办公室（东侧陆侧）：玻璃隔断 + 2 工位 + 机柜 ----
translate([25, -6, FZ]) rotate([0, 0, 90]) part_glass_wall(5.0, 1.9, 3.1);
translate([25, -6, FZ]) part_glass_wall(5.0);
translate([25, -1, FZ]) part_glass_wall(5.0);
translate([26.7, -2.4, FZ]) rotate([0, 0, 180]) staff_01();
translate([28.7, -2.4, FZ]) rotate([0, 0, 180]) staff_02();
translate([29.3, -5.2, FZ]) rotate([0, 0, -90]) prop_server_rack();

// ---- 陆侧大厅：问询台 + 航显 + ATM + 售货机 + 长椅 ----
translate([-2.5, -7.5, FZ]) info_01();
translate([6.5, -7.0, FZ]) prop_fids();
translate([-29.55, -7.0, FZ]) rotate([0, 0, 90]) atm_01();
translate([29.55, -8.6, FZ]) rotate([0, 0, -90]) vending_04();
translate([29.55, -9.8, FZ]) rotate([0, 0, -90]) vending_05();
translate([10.0, -8.5, FZ]) furn_bench_row();
translate([14.5, -8.5, FZ]) furn_bench_row();

// ---- 室内点缀 ----
translate([-28.5, -10.5, FZ]) prop_palm();
translate([-8, -6, FZ]) prop_palm();
translate([20, -7, FZ]) prop_palm();
translate([28.3, -10.8, FZ]) prop_palm();
translate([-24.5, 21.5, FZ]) prop_palm();
translate([-12.8, 17.5, FZ]) prop_palm();
translate([18.5, 22.0, FZ]) prop_palm();
translate([1.5, 14.5, FZ]) prop_palm();
translate([28.9, 6.2, FZ]) prop_palm();
translate([-13.6, 15.6, FZ]) prop_bins();
translate([7, 17, FZ]) prop_bins();
translate([3, -9, FZ]) prop_bins();
translate([-12, -5, FZ]) prop_bins();
translate([19.5, 6.0, FZ]) prop_bins();
// 广告灯箱 + 行李推车 + 登机口间绿植带（填充大空间）
translate([0.8, 10.8, FZ]) rotate([0, 0, 90]) prop_ad_totem([0.90, 0.55, 0.25]);
translate([-2.5, 21.0, FZ]) prop_ad_totem([0.30, 0.60, 0.80]);
translate([20.5, 17.8, FZ]) rotate([0, 0, 90]) prop_ad_totem([0.45, 0.68, 0.38]);
translate([8.0, -4.5, FZ]) rotate([0, 0, 90]) prop_ad_totem([0.70, 0.45, 0.72]);
translate([-14.5, -7.5, FZ]) prop_ad_totem([0.25, 0.45, 0.75]);
translate([-21.5, -10.2, FZ]) rotate([0, 0, 15]) prop_trolley_row();
translate([17.0, -10.6, FZ]) rotate([0, 0, -75]) prop_trolley_row();
translate([-16.0, 24.5, FZ]) prop_planter(3.0);
translate([0.0, 24.5, FZ]) prop_planter(3.0);
translate([16.0, 24.5, FZ]) prop_planter(3.0);
translate([-26.5, 24.0, FZ]) prop_palm();
translate([8.0, 23.0, FZ]) prop_palm();
translate([26.5, 24.5, FZ]) prop_palm();
translate([18.0, -7.2, FZ]) prop_fids();
translate([5.5, 19.5, FZ]) prop_bins();
translate([-25.5, 6.2, FZ]) prop_bins();

// ---- 停机坪：3 客机 + 双客梯 + 地勤车队 + 灯杆 + 风向袋 ----
translate([-14, 36, FZ]) rotate([0, 0, 174]) veh_airliner();
translate([8, 36.5, FZ]) rotate([0, 0, 188]) veh_airliner([0.94, 0.95, 0.96], [0.28, 0.58, 0.42]);
translate([27, 34, FZ]) rotate([0, 0, -24]) scale([0.72, 0.72, 0.72])
    veh_airliner([0.92, 0.78, 0.20], [0.30, 0.34, 0.42]);
translate([-17.9, 33.1, FZ]) veh_stairs_truck();
translate([4.35, 32.6, FZ]) veh_stairs_truck();
translate([-22, 31.5, FZ]) rotate([0, 0, 195]) veh_baggage_tug();
translate([-23.6, 31.0, FZ]) rotate([0, 0, 185]) veh_baggage_cart();
translate([-25.1, 30.8, FZ]) rotate([0, 0, 176]) veh_baggage_cart([0.35, 0.55, 0.75], [0.72, 0.40, 0.26]);
translate([12.5, 33.5, FZ]) rotate([0, 0, 10]) veh_fuel_truck();
translate([-4, 30.6, FZ]) veh_taxi();
translate([20, 30.2, FZ]) rotate([0, 0, 180]) veh_bus([0.90, 0.90, 0.92]);
translate([-16, 31.8, FZ]) prop_cone();
translate([2, 32, FZ]) prop_cone();
translate([9.5, 33.2, FZ]) prop_cone();
translate([-6, 30.2, FZ]) prop_cone();
translate([-26, 29.6, FZ]) prop_light_mast();
translate([-2, 29.6, FZ]) prop_light_mast();
translate([22, 29.6, FZ]) prop_light_mast();
translate([39, 43, FZ]) prop_windsock();

// ---- 东侧服务区：集装箱 + 服务车 + 围栏 ----
translate([34, -7.5, FZ]) prop_container();
translate([35.7, -7.2, FZ]) rotate([0, 0, 12]) prop_container([0.62, 0.50, 0.36]);
translate([34.8, -5.8, FZ]) rotate([0, 0, -6]) prop_container([0.40, 0.55, 0.62]);
translate([33.5, 4, FZ]) rotate([0, 0, 90]) veh_car([0.90, 0.90, 0.92]);
translate([36, 14, FZ]) prop_light_mast();
translate([30, -12, FZ]) prop_fence(12);
translate([-42, 27.8, FZ]) prop_fence(12);

// ---- 陆侧：候车亭 + 公交 + 出租 + 行道树/绿篱/路灯/长椅 ----
translate([8, -13.7, FZ]) prop_bus_shelter();
translate([8.4, -16.4, FZ]) rotate([0, 0, 180]) veh_bus();
translate([-13.5, -16.4, FZ]) rotate([0, 0, 180]) veh_taxi();
translate([-9, -16.4, FZ]) rotate([0, 0, 180]) veh_taxi();
translate([15, -19.1, FZ]) veh_car([0.60, 0.62, 0.66]);
translate([30, -16.4, FZ]) rotate([0, 0, 180]) veh_car([0.90, 0.90, 0.92]);
translate([-30, -19.1, FZ]) veh_car([0.22, 0.30, 0.45]);
translate([-36, -16.4, FZ]) rotate([0, 0, 180]) veh_car([0.85, 0.55, 0.25]);
translate([-34, -13.9, FZ]) prop_tree();
translate([-25, -13.9, FZ]) prop_tree();
translate([-8, -13.9, FZ]) prop_tree();
translate([20, -13.9, FZ]) prop_tree();
translate([32, -13.9, FZ]) prop_tree();
translate([-26, -12.7, FZ]) prop_hedge();
translate([-9, -12.7, FZ]) prop_hedge();
translate([6, -12.7, FZ]) prop_hedge();
translate([20, -12.7, FZ]) prop_hedge();
translate([26, -12.7, FZ]) prop_hedge();
translate([-12, -13.0, FZ]) prop_bench_out();
translate([3.5, -13.0, FZ]) prop_bench_out();
translate([17.5, -13.0, FZ]) prop_bench_out();
translate([-38, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();
translate([-26, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();
translate([-14, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();
translate([2, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();
translate([14, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();
translate([26, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();
translate([38, -15.7, FZ]) rotate([0, 0, 180]) prop_lamp_post();

// ---- 停车场：7 辆车 + 道闸 + P 牌 ----
translate([-36.8, -28.6, FZ]) rotate([0, 0, 90]) veh_car(carc5(0));
translate([-32.0, -28.6, FZ]) rotate([0, 0, 90]) veh_car(carc5(3));
translate([-29.6, -28.6, FZ]) rotate([0, 0, 90]) veh_car(carc5(1));
translate([-22.4, -28.6, FZ]) rotate([0, 0, 90]) veh_car(carc5(4));
translate([-17.6, -28.6, FZ]) rotate([0, 0, 90]) veh_car(carc5(2));
translate([-10.4, -28.6, FZ]) rotate([0, 0, 90]) veh_car([0.45, 0.60, 0.42]);
translate([-5.6, -28.6, FZ]) rotate([0, 0, 90]) veh_car(carc5(0));
translate([1.2, -21.2, FZ]) prop_barrier_gate();
translate([3.4, -22.3, FZ]) prop_p_sign();

// ---- 东南草地 / 西侧花园 / 西北草地 ----
translate([10, -27, FZ]) prop_tree();
translate([16, -24, FZ]) prop_tree();
translate([24, -28.5, FZ]) prop_tree();
translate([32, -23.5, FZ]) prop_tree();
translate([38, -29, FZ]) prop_tree();
translate([12, -21.2, FZ]) prop_hedge();
translate([22, -21.2, FZ]) prop_hedge();
translate([34, -21.2, FZ]) prop_hedge();
translate([-36, -6, FZ]) prop_tree();
translate([-38, 4, FZ]) prop_tree();
translate([-35, 14, FZ]) prop_tree();
translate([-37, 22, FZ]) prop_tree();
translate([-33, 0, FZ]) prop_palm();
translate([-36, -10, FZ]) prop_hedge();
translate([-34, 8, FZ]) prop_hedge();
translate([-36, 32, FZ]) prop_tree();
translate([-39, 38, FZ]) prop_tree();
translate([-33, 42, FZ]) prop_tree();
translate([-37, 45, FZ]) prop_tree();
