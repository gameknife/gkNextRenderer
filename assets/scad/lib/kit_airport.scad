// Initially extracted from airport.scad by the one-time tools/scadkit migration; this checked-in file is now authoritative.
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "ap_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y。调用方自设 $fn（建议 12）。



// ---- 配色 ----
function ap_CONCC() = [0.60, 0.60, 0.58];    // 基底混凝土
function ap_APRONC() = [0.69, 0.69, 0.67];    // 停机坪面板
function ap_CARPETA() = [0.30, 0.42, 0.62];    // 航站楼地毯 A
function ap_CARPETB() = [0.34, 0.47, 0.68];    // 航站楼地毯 B
function ap_CARPETD() = [0.24, 0.33, 0.50];    // 地毯缝/深色块
function ap_PAVEC() = [0.76, 0.72, 0.64];    // 人行道
function ap_ROADC() = [0.30, 0.30, 0.32];    // 沥青
function ap_LOTC() = [0.37, 0.37, 0.39];    // 停车场
function ap_GRASSC() = [0.45, 0.62, 0.35];
function ap_WHITEC() = [0.92, 0.91, 0.88];
function ap_GRAYC() = [0.58, 0.60, 0.63];
function ap_METALC() = [0.72, 0.74, 0.77];
function ap_DARKMETC() = [0.22, 0.23, 0.26];
function ap_BLACKC() = [0.10, 0.10, 0.11];
function ap_GLASSC() = [0.55, 0.72, 0.85, 0.30];
function ap_SCREENC() = [0.62, 0.78, 0.92];
function ap_SIGNBLUE() = [0.16, 0.32, 0.62];    // 标识蓝
function ap_AIRBLUE() = [0.20, 0.40, 0.68];    // 设施蓝（柜台/kiosk）
function ap_SEATBLUE() = [0.28, 0.45, 0.72];    // 候机排椅
function ap_BROWNC() = [0.48, 0.32, 0.20];    // 咖啡店墙
function ap_REDFOOD() = [0.78, 0.25, 0.20];    // 快餐店
function ap_OAKC() = [0.78, 0.57, 0.34];
function ap_PLANTC() = [0.34, 0.58, 0.31];
function ap_PLANTDC() = [0.24, 0.45, 0.24];
function ap_POTC() = [0.70, 0.44, 0.30];
function ap_PAPERC() = [0.95, 0.95, 0.92];
function ap_YELLINE() = [0.85, 0.75, 0.25];    // 机坪黄线
function ap_REDLINE() = [0.75, 0.25, 0.22];

function ap_goods6(i) = i == 0 ? [0.80, 0.30, 0.28] : i == 1 ? [0.95, 0.72, 0.25] : i == 2 ? [0.35, 0.60, 0.80]
                   : i == 3 ? [0.45, 0.68, 0.38] : i == 4 ? [0.70, 0.45, 0.72] : [0.90, 0.88, 0.84];
function ap_carc5(i)  = i == 0 ? [0.75, 0.28, 0.24] : i == 1 ? [0.90, 0.90, 0.92] : i == 2 ? [0.60, 0.62, 0.66]
                   : i == 3 ? [0.22, 0.30, 0.45] : [0.85, 0.55, 0.25];
function ap_book5(i)  = i == 0 ? [0.62, 0.30, 0.26] : i == 1 ? [0.28, 0.45, 0.60] : i == 2 ? [0.80, 0.70, 0.45]
                   : i == 3 ? [0.36, 0.55, 0.38] : [0.50, 0.38, 0.58];

// ================= 地面 =================
module ap_ground_base() color(ap_CONCC()) translate([0, 7, 0]) cube([84, 80, 0.26], center = true);

// 航站楼蓝色方块地毯（30x20 块 2x2，棋盘 + 少量深色点缀）
module ap_ground_carpet()
{
    color(ap_CARPETD()) translate([0, 8, 0.135]) cube([60, 40, 0.01], center = true);
    for (ix = [0 : 29], iy = [0 : 19])
        translate([-29 + ix * 2, -11 + iy * 2, 0.143])
            color(((ix * 7 + iy * 11) % 13 == 0) ? ap_CARPETD() : ((ix + iy) % 2 == 0) ? ap_CARPETA() : ap_CARPETB())
                cube([1.94, 1.94, 0.014], center = true);
}

// 停机坪面板 + 接缝 + 黄/红标线
module ap_ground_apron()
{
    color(ap_APRONC()) translate([6, 37, 0.14]) cube([72, 18, 0.02], center = true);
    color(ap_APRONC()) translate([36, 8, 0.14]) cube([12, 40, 0.02], center = true);   // 东侧服务区
    for (ix = [0 : 15])
        color([0.58, 0.58, 0.56]) translate([-27 + ix * 4.5, 37, 0.152]) cube([0.06, 18, 0.012], center = true);
    color([0.58, 0.58, 0.56]) translate([6, 37, 0.152]) cube([72, 0.06, 0.012], center = true);
    // 滑行引导黄线 + 三机位引入线 + 红色边界线
    color(ap_YELLINE()) translate([6, 43.5, 0.153]) cube([68, 0.14, 0.012], center = true);
    color(ap_YELLINE()) translate([-14, 37, 0.153]) cube([0.14, 13, 0.012], center = true);
    color(ap_YELLINE()) translate([8, 37.2, 0.153]) cube([0.14, 12.6, 0.012], center = true);
    color(ap_YELLINE()) translate([27, 38.7, 0.153]) cube([0.14, 9.5, 0.012], center = true);
    color(ap_REDLINE()) translate([6, 28.8, 0.153]) cube([72, 0.10, 0.012], center = true);
}

module ap_ground_landside()
{
    color(ap_PAVEC()) translate([0, -13.5, 0.14]) cube([84, 3.0, 0.02], center = true);        // 人行道
    color([0.45, 0.45, 0.44]) translate([0, -14.97, 0.155]) cube([84, 0.12, 0.03], center = true);  // 路缘
    color(ap_ROADC()) translate([0, -17.7, 0.13]) cube([84, 5.4, 0.012], center = true);        // 马路
    for (ix = [0 : 18])
        color(ap_WHITEC()) translate([-40 + ix * 4.4, -17.7, 0.142]) cube([1.6, 0.14, 0.008], center = true);
    for (i = [0 : 6])                                                                      // 两处入口斑马线
    {
        color(ap_WHITEC()) translate([-19.9 + i * 0.62, -17.7, 0.144]) cube([0.42, 4.6, 0.008], center = true);
        color(ap_WHITEC()) translate([-1.9 + i * 0.62, -17.7, 0.144]) cube([0.42, 4.6, 0.008], center = true);
    }
    color(ap_LOTC()) translate([-19, -25.7, 0.13]) cube([42, 10.6, 0.012], center = true);      // 停车场
    for (i = [0 : 15])
        color(ap_WHITEC()) translate([-38 + i * 2.4, -28.6, 0.142]) cube([0.08, 4.8, 0.008], center = true);
    color(ap_GRASSC()) translate([23, -25.7, 0.145]) cube([38, 10.6, 0.022], center = true);    // 东南草地
    color(ap_GRASSC()) translate([-36, 7.9, 0.145]) cube([12, 39.8, 0.022], center = true);     // 西侧草地
    color(ap_GRASSC()) translate([-36, 37.5, 0.145]) cube([12, 19, 0.022], center = true);      // 西北草地
}

// ================= 玻璃幕墙 =================
// 沿 +x，底梁 + 玻璃 + 竖梃 + 顶部白檐（高 3.33）
module ap_wall_glass_seg(len)
{
    color(ap_DARKMETC()) translate([len / 2, 0, 0.10]) cube([len, 0.12, 0.20], center = true);
    color(ap_GLASSC()) translate([len / 2, 0, 1.49]) cube([len - 0.04, 0.06, 2.58], center = true);
    for (p = [0 : 1.5 : len]) color(ap_DARKMETC()) translate([p, 0, 1.49]) cube([0.10, 0.10, 2.58], center = true);
    color(ap_DARKMETC()) translate([len, 0, 1.49]) cube([0.10, 0.10, 2.58], center = true);
    color(ap_WHITEC()) translate([len / 2, 0, 3.05]) cube([len, 0.30, 0.55], center = true);
}

module ap_wall_corner_col() color(ap_WHITEC()) cube([0.36, 0.36, 3.32], center = true);

// 室内实墙段（卫生间等，h2.4）
module ap_wall_solid_seg(len, c = [0.84, 0.82, 0.88])
{
    color(c) translate([len / 2, 0, 1.2]) cube([len, 0.15, 2.4], center = true);
}

// 自动滑门单元（3m 洞口）：框 + 半开双扇 + 顶檐 + 地垫
module ap_furn_entrance()
{
    color(ap_DARKMETC())
    {
        for (sx = [-1, 1]) translate([1.52 * sx, 0, 1.40]) cube([0.16, 0.30, 2.60], center = true);
        translate([0, 0, 2.78]) cube([3.20, 0.30, 0.16], center = true);
    }
    color(ap_GLASSC()) translate([-1.02, 0.10, 1.35]) cube([0.95, 0.05, 2.30], center = true);
    color(ap_GLASSC()) translate([1.02, -0.10, 1.35]) cube([0.95, 0.05, 2.30], center = true);
    color(ap_WHITEC()) translate([0, 0, 3.05]) cube([3.20, 0.34, 0.55], center = true);
    color([0.28, 0.30, 0.28]) translate([0, 0, 0.012]) cube([2.90, 1.90, 0.022], center = true);
}

// 入口上方大招牌
module ap_prop_big_sign(label = "AIRPORT")
{
    color(ap_WHITEC()) cube([4.60, 0.18, 1.00], center = true);
    color(ap_SIGNBLUE()) translate([-1.38, -0.10, -0.28]) rotate([90, 0, 0]) linear_extrude(0.04) text(label, size = 0.55);
}

// ================= 室内设施库（front = -y） =================
module ap_furn_monitor(screen = ap_SCREENC(), w = 0.52)
{
    color(ap_DARKMETC())
    {
        cylinder(h = 0.025, r = 0.09);
        translate([0, 0.02, 0.15]) cube([0.05, 0.04, 0.26], center = true);
    }
    color(ap_BLACKC()) translate([0, 0.030, 0.40]) cube([w, 0.035, 0.32], center = true);
    color(screen) translate([0, 0.006, 0.40]) cube([w - 0.05, 0.015, 0.27], center = true);
}

module ap_furn_task_chair(seat = [0.45, 0.47, 0.52])
{
    color(ap_DARKMETC())
    {
        for (a = [0 : 72 : 288]) rotate([0, 0, a + 36]) translate([0.15, 0, 0.03]) cube([0.28, 0.05, 0.05], center = true);
        translate([0, 0, 0.05]) cylinder(h = 0.40, r = 0.028, $fn = 12);
        translate([0, 0.19, 0.54]) cube([0.05, 0.05, 0.15], center = true);
    }
    color(seat) translate([0, 0, 0.47]) cube([0.44, 0.43, 0.07], center = true);
    color(seat) translate([0, 0.215, 0.78]) cube([0.42, 0.06, 0.44], center = true);
}

// 值机柜台：蓝色柜体 + 白台面 + 行李秤/传送带 + 坐席 + 头顶号牌屏
module ap_furn_checkin_desk(label = "1")
{
    color(ap_AIRBLUE()) translate([0, -0.02, 0.54]) cube([1.60, 0.62, 1.08], center = true);
    color(ap_WHITEC()) translate([0, 0, 1.10]) cube([1.70, 0.72, 0.06], center = true);
    color(ap_WHITEC()) translate([0, 0.55, 0.37]) cube([1.40, 0.50, 0.74], center = true);    // 坐席矮台
    translate([0.30, 0.62, 0.74]) rotate([0, 0, 180]) ap_furn_monitor(ap_SCREENC(), 0.44);
    color(ap_BLACKC()) translate([-0.25, 0.55, 0.755]) cube([0.30, 0.11, 0.02], center = true);
    // 行李传送带（向北没入墙后）
    color(ap_GRAYC()) translate([-1.15, 0.65, 0.26]) cube([0.62, 1.90, 0.52], center = true);
    color([0.18, 0.19, 0.21]) translate([-1.15, 0.65, 0.535]) cube([0.50, 1.80, 0.03], center = true);
    color([0.72, 0.40, 0.26]) translate([-1.15, 0.30, 0.64]) cube([0.40, 0.55, 0.18], center = true);  // 行李箱
    // 头顶号牌
    color(ap_DARKMETC()) translate([0.62, 0.20, 1.85]) cube([0.06, 0.06, 1.50], center = true);
    color(ap_SIGNBLUE()) translate([0.62, 0.16, 2.72]) cube([0.74, 0.08, 0.52], center = true);
    color(ap_WHITEC()) translate([0.52, 0.11, 2.52]) rotate([90, 0, 0]) linear_extrude(0.03) text(label, size = 0.30);
    translate([0.15, 1.10, 0]) ap_furn_task_chair();
}

// 自助值机 kiosk
module ap_furn_kiosk()
{
    color(ap_AIRBLUE()) translate([0, 0, 0.62]) cube([0.55, 0.40, 1.24], center = true);
    color(ap_DARKMETC()) translate([0, 0, 0.03]) cube([0.62, 0.48, 0.06], center = true);
    color(ap_BLACKC()) translate([0, -0.205, 0.98]) rotate([14, 0, 0]) cube([0.46, 0.05, 0.40], center = true);
    color(ap_SCREENC()) translate([0, -0.225, 0.98]) rotate([14, 0, 0]) cube([0.40, 0.02, 0.33], center = true);
    color(ap_METALC()) translate([0, -0.21, 0.60]) cube([0.34, 0.04, 0.05], center = true);   // 出票口
}

// 安检通道：入口台-X光机-出口台（局部 y 0..3.1），金属探测门在 x+1.05
module ap_furn_security_lane()
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
    color(ap_BLACKC())
    {
        translate([0, 0.81, 0.62]) cube([0.50, 0.05, 0.40], center = true);
        translate([0, 2.29, 0.62]) cube([0.50, 0.05, 0.40], center = true);
        translate([0, 1.55, 0.685]) cube([0.50, 1.55, 0.03], center = true);
    }
    translate([-0.62, 1.30, 0.86]) rotate([0, 0, 90]) ap_furn_monitor([0.55, 0.85, 0.70], 0.40);
    color(ap_DARKMETC()) translate([-0.62, 1.30, 0.30]) cube([0.06, 0.06, 0.60], center = true);
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
module ap_furn_gate_door(label = "GATE 1")
{
    color(ap_DARKMETC())
    {
        for (sx = [-1, 1]) translate([0.95 * sx, 0.30, 1.30]) cube([0.12, 0.20, 2.60], center = true);
        translate([0, 0.30, 2.66]) cube([2.02, 0.20, 0.14], center = true);
    }
    color(ap_GLASSC()) translate([-0.45, 0.34, 1.30]) cube([0.86, 0.05, 2.40], center = true);
    color(ap_GLASSC()) translate([0.45, 0.26, 1.30]) cube([0.86, 0.05, 2.40], center = true);
    color(ap_SIGNBLUE()) translate([0, 0.26, 3.02]) cube([1.90, 0.14, 0.56], center = true);
    color(ap_WHITEC()) translate([-0.56, 0.18, 2.86]) rotate([90, 0, 0]) linear_extrude(0.035) text(label, size = 0.30);
    // 检票台
    translate([1.55, -0.55, 0]) rotate([0, 0, 12])
    {
        color(ap_AIRBLUE()) translate([0, 0, 0.52]) cube([0.66, 0.46, 1.04], center = true);
        color(ap_WHITEC()) translate([0, -0.04, 1.07]) rotate([12, 0, 0]) cube([0.70, 0.50, 0.05], center = true);
        color(ap_BLACKC()) translate([0, -0.05, 1.13]) rotate([12, 0, 0]) cube([0.30, 0.22, 0.03], center = true);
    }
    translate([-1.45, -0.45, 0]) ap_prop_stanchion();
    translate([-1.45, -1.55, 0]) ap_prop_stanchion();
    color([0.25, 0.40, 0.70]) translate([-1.45, -1.0, 0.86]) cube([0.05, 1.02, 0.06], center = true);
}

// 候机排椅（4 联座，front=-y）
module ap_furn_bench_row()
{
    color(ap_DARKMETC())
    {
        translate([0, 0, 0.33]) cube([2.30, 0.10, 0.08], center = true);
        for (sx = [-1, 1]) translate([0.95 * sx, 0, 0.16]) cube([0.10, 0.55, 0.32], center = true);
    }
    for (i = [0 : 3])
        translate([-0.855 + i * 0.57, 0, 0])
        {
            color(ap_SEATBLUE()) translate([0, -0.02, 0.43]) cube([0.50, 0.48, 0.07], center = true);
            color(ap_SEATBLUE()) translate([0, 0.225, 0.645]) rotate([8, 0, 0]) cube([0.50, 0.06, 0.44], center = true);
        }
    color(ap_DARKMETC()) for (i = [0 : 4])
        translate([-1.14 + i * 0.57, -0.02, 0.56]) cube([0.04, 0.40, 0.05], center = true);
}

// 隔离柱 + 排队线
module ap_prop_stanchion()
{
    color(ap_DARKMETC())
    {
        cylinder(h = 0.04, r = 0.14, $fn = 12);
        cylinder(h = 0.92, r = 0.025, $fn = 8);
        translate([0, 0, 0.92]) sphere(r = 0.045);
    }
}

module ap_prop_queue_line(n = 5)
{
    for (i = [0 : n - 1])
    {
        translate([i * 1.1, 0, 0]) ap_prop_stanchion();
        if (i < n - 1)
            color([0.25, 0.40, 0.70]) translate([i * 1.1 + 0.55, 0, 0.84]) cube([1.00, 0.05, 0.06], center = true);
    }
}

// 航显屏（落地双柱式；wallmount=false）
module ap_prop_fids()
{
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([0.85 * sx, 0.05, 1.05]) cube([0.08, 0.08, 2.10], center = true);
    color([0.10, 0.13, 0.22]) translate([0, 0, 2.42]) cube([2.20, 0.10, 1.30], center = true);
    color(ap_SIGNBLUE()) translate([0, -0.055, 2.95]) cube([2.10, 0.012, 0.18], center = true);
    for (r = [0 : 4])
    {
        color([0.95, 0.80, 0.30]) translate([-0.62, -0.055, 2.70 - r * 0.21]) cube([0.70, 0.012, 0.09], center = true);
        color(ap_WHITEC()) translate([0.18, -0.055, 2.70 - r * 0.21]) cube([0.46, 0.012, 0.09], center = true);
        color((r == 2) ? [0.35, 0.85, 0.45] : ap_WHITEC()) translate([0.80, -0.055, 2.70 - r * 0.21]) cube([0.36, 0.012, 0.09], center = true);
    }
}

// 悬挂指示牌（双柱 + 色牌 + 文字 + 可选右向箭头）
module ap_prop_hang_sign(label = "ALL GATES", c = ap_SIGNBLUE(), arrow = true)
{
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([1.30 * sx, 0.04, 1.30]) cube([0.07, 0.07, 2.60], center = true);
    color(c) translate([0, 0, 2.42]) cube([2.80, 0.10, 0.60], center = true);
    color(ap_WHITEC()) translate([-1.05, -0.055, 2.28]) rotate([90, 0, 0]) linear_extrude(0.03) text(label, size = 0.26);
    if (arrow)
        color(ap_WHITEC())
        {
            translate([1.02, -0.06, 2.48]) rotate([0, 45, 0]) cube([0.04, 0.012, 0.26], center = true);
            translate([1.02, -0.06, 2.32]) rotate([0, -45, 0]) cube([0.04, 0.012, 0.26], center = true);
        }
}

// 咖啡柜台：木柜 + 糕点柜 + 咖啡机 + 收银
module ap_furn_cafe_counter()
{
    color(ap_OAKC()) translate([0, 0, 0.525]) cube([3.40, 0.62, 1.05], center = true);
    color([0.60, 0.42, 0.25]) translate([0, -0.315, 0.40]) cube([3.40, 0.012, 0.26], center = true);
    color(ap_WHITEC()) translate([0, 0, 1.075]) cube([3.52, 0.70, 0.05], center = true);
    // 玻璃糕点柜（左）
    color(ap_GLASSC()) translate([-1.10, -0.02, 1.32]) cube([1.10, 0.58, 0.44], center = true);
    color([0.85, 0.65, 0.35]) translate([-1.30, 0.0, 1.16]) scale([1, 0.6, 0.5]) sphere(r = 0.14);
    color([0.80, 0.55, 0.30]) translate([-0.95, -0.08, 1.16]) scale([1, 0.6, 0.5]) sphere(r = 0.12);
    color([0.92, 0.88, 0.80]) translate([-1.12, 0.12, 1.14]) cylinder(h = 0.08, r = 0.09, $fn = 10);
    // 咖啡机（右）
    color([0.25, 0.27, 0.31]) translate([0.85, 0.10, 1.32]) cube([0.62, 0.40, 0.44], center = true);
    color(ap_METALC()) translate([0.85, -0.12, 1.18]) cube([0.40, 0.10, 0.08], center = true);
    color(ap_WHITEC()) translate([0.72, -0.10, 1.115]) cylinder(h = 0.07, r = 0.04, $fn = 10);
    color(ap_WHITEC()) translate([0.98, -0.10, 1.115]) cylinder(h = 0.07, r = 0.04, $fn = 10);
    // 收银
    color(ap_BLACKC()) translate([0.05, -0.10, 1.10]) cube([0.30, 0.24, 0.05], center = true);
    translate([0.05, 0.04, 1.12]) rotate([0, 0, 180]) ap_furn_monitor(ap_SCREENC(), 0.30);
    // 杯塔
    color([0.90, 0.60, 0.40]) translate([1.45, 0.12, 1.10]) cylinder(h = 0.26, r1 = 0.06, r2 = 0.045, $fn = 10);
}

module ap_furn_cafe_table(c = ap_BROWNC())
{
    color(ap_DARKMETC())
    {
        cylinder(h = 0.04, r = 0.22, $fn = 14);
        cylinder(h = 0.72, r = 0.035, $fn = 10);
    }
    color(ap_OAKC()) translate([0, 0, 0.72]) cylinder(h = 0.04, r = 0.38, $fn = 20);
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
module ap_prop_menu_board()
{
    color([0.20, 0.15, 0.10]) cube([1.60, 0.06, 0.90], center = true);
    color([0.92, 0.85, 0.70]) translate([-0.45, -0.04, 0.22]) cube([0.55, 0.012, 0.07], center = true);
    color([0.92, 0.85, 0.70]) translate([-0.42, -0.04, 0.02]) cube([0.48, 0.012, 0.07], center = true);
    color([0.92, 0.85, 0.70]) translate([-0.46, -0.04, -0.18]) cube([0.50, 0.012, 0.07], center = true);
    color([0.85, 0.55, 0.30]) translate([0.42, -0.04, 0.0]) cube([0.50, 0.012, 0.50], center = true);
}

// 店铺门头（双柱 + 牌匾文字）
module ap_prop_shop_portal(w = 4.8, label = "SHOP", c = [0.20, 0.40, 0.68])
{
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([w / 2 * sx, 0, 1.22]) cube([0.16, 0.16, 2.44], center = true);
    color(c) translate([0, 0, 2.66]) cube([w + 0.16, 0.22, 0.62], center = true);
    color(ap_WHITEC()) translate([-(0.28 * 0.55 * len(label) * 2) / 2, -0.115, 2.46]) rotate([90, 0, 0]) linear_extrude(0.035) text(label, size = 0.42);
}

// 便利店货架（双面，front 任意）
module ap_furn_gondola()
{
    color(ap_GRAYC()) translate([0, 0, 0.06]) cube([2.20, 0.90, 0.12], center = true);
    color(ap_GRAYC()) translate([0, 0, 0.80]) cube([2.20, 0.10, 1.50], center = true);
    for (sy = [-1, 1], lv = [0 : 2])
    {
        color([0.66, 0.68, 0.71]) translate([0, 0.25 * sy, 0.42 + lv * 0.42]) cube([2.16, 0.42, 0.04], center = true);
        for (i = [0 : 6])
            color(ap_goods6((i + lv * 2 + (sy + 1)) % 6))
                translate([-0.90 + i * 0.30, 0.25 * sy, 0.56 + lv * 0.42]) cube([0.24, 0.30, 0.24], center = true);
    }
}

// 饮料冷柜
module ap_furn_fridge_case(c = [0.80, 0.30, 0.28])
{
    color(ap_WHITEC()) translate([0, 0, 0.95]) cube([1.00, 0.60, 1.90], center = true);
    color(c) translate([0, -0.02, 1.78]) cube([1.02, 0.58, 0.24], center = true);
    color(ap_GLASSC()) translate([0, -0.305, 0.86]) cube([0.86, 0.04, 1.46], center = true);
    for (lv = [0 : 2], i = [0 : 3])
        color(ap_goods6((i + lv) % 6)) translate([-0.30 + i * 0.20, -0.18, 0.38 + lv * 0.46]) cube([0.13, 0.13, 0.30], center = true);
}

// 便利店收银台
module ap_furn_checkout()
{
    color(ap_AIRBLUE()) translate([0, 0, 0.47]) cube([1.50, 0.60, 0.94], center = true);
    color(ap_WHITEC()) translate([0, 0, 0.965]) cube([1.60, 0.68, 0.05], center = true);
    translate([-0.35, 0.10, 0.99]) rotate([0, 0, 180]) ap_furn_monitor(ap_SCREENC(), 0.34);
    color(ap_BLACKC()) translate([0.30, 0.0, 1.02]) cube([0.30, 0.30, 0.06], center = true);
    color(ap_METALC()) translate([0.62, -0.18, 0.99]) cube([0.10, 0.10, 0.14], center = true);
}

// 卫生间门 + 图标牌（front=-y；female=true 为裙装图标）
module ap_furn_wc_door(female = false)
{
    color(ap_DARKMETC())
    {
        for (sx = [-1, 1]) translate([0.50 * sx, 0, 1.05]) cube([0.10, 0.18, 2.10], center = true);
        translate([0, 0, 2.13]) cube([1.10, 0.18, 0.10], center = true);
    }
    color([0.25, 0.35, 0.55]) translate([0.04, 0.02, 1.02]) cube([0.90, 0.07, 2.02], center = true);
    color(ap_METALC()) translate([-0.30, -0.06, 1.00]) cube([0.04, 0.05, 0.16], center = true);
    // 图标牌
    color(ap_SIGNBLUE()) translate([0.92, 0.0, 1.70]) cube([0.50, 0.08, 0.50], center = true);
    color(ap_WHITEC()) translate([0.92, -0.05, 1.81]) sphere(r = 0.055);
    if (female)
        color(ap_WHITEC()) translate([0.92, -0.05, 1.60]) cylinder(h = 0.18, r1 = 0.105, r2 = 0.02, $fn = 12);
    else
        color(ap_WHITEC()) translate([0.92, -0.05, 1.60]) cube([0.13, 0.05, 0.20], center = true);
}

// 卫生间室内（切顶可见）：2 隔间 + 马桶 + 洗手台 + 镜子（沿 +x 约 4.6 宽、+y 约 2.0 深）
module ap_furn_wc_interior()
{
    for (i = [0 : 1])
        translate([i * 1.05, 0, 0])
        {
            color([0.88, 0.86, 0.92]) translate([0.50, 0.55, 0.90]) cube([0.05, 1.10, 1.80], center = true);  // 隔板
            color(ap_WHITEC())
            {
                translate([0, 0.85, 0.21]) cube([0.40, 0.45, 0.42], center = true);       // 马桶座
                translate([0, 1.04, 0.55]) cube([0.42, 0.16, 0.50], center = true);       // 水箱
            }
        }
    // 洗手台 + 镜子（右侧）
    color(ap_WHITEC()) translate([3.55, 0.90, 0.42]) cube([1.30, 0.50, 0.84], center = true);
    color([0.62, 0.78, 0.88]) for (i = [0 : 1])
        translate([3.25 + i * 0.62, 0.88, 0.875]) cylinder(h = 0.05, r = 0.16, $fn = 14);
    color(ap_METALC()) for (i = [0 : 1])
        translate([3.25 + i * 0.62, 1.06, 0.92]) cube([0.05, 0.05, 0.14], center = true);
    color([0.75, 0.85, 0.92]) translate([3.55, 1.12, 1.55]) cube([1.30, 0.04, 0.80], center = true);
}

module ap_prop_fountain()
{
    color(ap_METALC()) translate([0, 0, 0.42]) cube([0.36, 0.30, 0.84], center = true);
    color([0.80, 0.82, 0.85]) translate([0, -0.05, 0.875]) cube([0.32, 0.26, 0.07], center = true);
    color(ap_DARKMETC()) translate([0, -0.08, 0.93]) cube([0.05, 0.05, 0.05], center = true);
}

module ap_furn_atm()
{
    color([0.55, 0.58, 0.64]) translate([0, 0, 0.80]) cube([0.75, 0.45, 1.60], center = true);
    color(ap_SIGNBLUE()) translate([0, -0.01, 1.50]) cube([0.77, 0.45, 0.20], center = true);
    color(ap_BLACKC()) translate([0, -0.235, 1.12]) rotate([12, 0, 0]) cube([0.50, 0.05, 0.36], center = true);
    color(ap_SCREENC()) translate([0, -0.250, 1.12]) rotate([12, 0, 0]) cube([0.42, 0.02, 0.28], center = true);
    color(ap_METALC()) translate([0, -0.235, 0.82]) cube([0.40, 0.04, 0.06], center = true);
    color(ap_BLACKC()) translate([0, -0.235, 0.66]) cube([0.30, 0.04, 0.05], center = true);
}

module ap_furn_vending(c = [0.80, 0.30, 0.28])
{
    color(c) translate([0, 0, 0.90]) cube([0.92, 0.50, 1.80], center = true);
    color(ap_GLASSC()) translate([-0.12, -0.26, 1.05]) cube([0.56, 0.04, 1.20], center = true);
    for (lv = [0 : 3], i = [0 : 2])
        color(ap_goods6((i + lv * 2) % 6)) translate([-0.28 + i * 0.17, -0.18, 0.62 + lv * 0.30]) cube([0.11, 0.11, 0.22], center = true);
    color(ap_BLACKC()) translate([0.32, -0.26, 1.30]) cube([0.16, 0.03, 0.50], center = true);
    color(ap_BLACKC()) translate([-0.10, -0.26, 0.32]) cube([0.50, 0.04, 0.22], center = true);
}

// 问询台（胶囊形 + i 标识）
module ap_furn_info_desk()
{
    color(ap_AIRBLUE()) translate([0, 0, 0.53]) hull()
    {
        translate([-0.65, 0, 0]) cylinder(h = 1.06, r = 0.55, $fn = 24);
        translate([0.65, 0, 0]) cylinder(h = 1.06, r = 0.55, $fn = 24);
    }
    color(ap_WHITEC()) translate([0, 0, 1.06]) hull()
    {
        translate([-0.65, 0, 0]) cylinder(h = 0.06, r = 0.62, $fn = 24);
        translate([0.65, 0, 0]) cylinder(h = 0.06, r = 0.62, $fn = 24);
    }
    color(ap_DARKMETC()) translate([0, 0.30, 1.12]) cylinder(h = 1.30, r = 0.03, $fn = 8);
    color(ap_SIGNBLUE()) translate([0, 0.30, 2.60]) rotate([90, 0, 0]) cylinder(h = 0.06, r = 0.30, $fn = 20, center = true);
    color(ap_WHITEC()) translate([-0.045, 0.26, 2.42]) rotate([90, 0, 0]) linear_extrude(0.03) text("i", size = 0.34);
    translate([0.3, 0.95, 0]) ap_furn_task_chair();
    color(ap_PAPERC()) translate([-0.55, 0.10, 1.13]) rotate([0, 0, 18]) cube([0.22, 0.28, 0.012], center = true);
}

// 员工办公桌（紧凑版）
module ap_furn_staff_desk()
{
    color(ap_OAKC()) translate([0, 0, 0.70]) cube([1.50, 0.75, 0.05], center = true);
    color([0.60, 0.42, 0.25]) for (sx = [-1, 1]) translate([0.70 * sx, 0, 0.34]) cube([0.06, 0.70, 0.68], center = true);
    translate([0, 0.15, 0.725]) ap_furn_monitor();
    color(ap_BLACKC()) translate([0, -0.16, 0.74]) cube([0.36, 0.12, 0.02], center = true);
    color(ap_PAPERC()) translate([-0.50, 0.10, 0.73]) rotate([0, 0, -12]) cube([0.22, 0.28, 0.012], center = true);
    translate([0, -0.80, 0]) ap_furn_task_chair();
}

module ap_prop_server_rack()
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

module ap_prop_clock()
{
    rotate([90, 0, 0])
    {
        color(ap_DARKMETC()) cylinder(h = 0.04, r = 0.18, $fn = 24);
        color(ap_WHITEC()) translate([0, 0, 0.041]) cylinder(h = 0.012, r = 0.148, $fn = 24);
        color(ap_BLACKC()) translate([0, 0.045, 0.056]) cube([0.016, 0.10, 0.008], center = true);
        color(ap_BLACKC()) rotate([0, 0, -60]) translate([0, 0.032, 0.056]) cube([0.016, 0.075, 0.008], center = true);
    }
}

module ap_prop_bins()
{
    color([0.25, 0.45, 0.75]) translate([-0.17, 0, 0]) cylinder(h = 0.36, r = 0.14, $fn = 12);
    color(ap_GRAYC()) translate([0.17, 0, 0]) cylinder(h = 0.36, r = 0.14, $fn = 12);
}

module ap_prop_palm()
{
    color(ap_POTC()) cylinder(h = 0.34, r1 = 0.17, r2 = 0.21);
    color([0.48, 0.36, 0.22]) translate([0, 0, 0.30]) cylinder(h = 1.00, r = 0.05, $fn = 8);
    color([0.50, 0.38, 0.24]) translate([0.05, 0.02, 1.10]) cylinder(h = 0.40, r = 0.04, $fn = 8);
    for (a = [0 : 60 : 300])
        color((a % 120 == 0) ? ap_PLANTC() : ap_PLANTDC())
            rotate([0, 0, a]) translate([0.42, 0.02 * a / 60, 1.52]) rotate([0, 28, 0]) cube([0.95, 0.20, 0.035], center = true);
    color(ap_PLANTDC()) translate([0.05, 0.02, 1.52]) sphere(r = 0.11);
}

module ap_prop_planter(len = 1.6)
{
    color(ap_WHITEC()) translate([0, 0, 0.21]) cube([len, 0.34, 0.42], center = true);
    color([0.22, 0.15, 0.09]) translate([0, 0, 0.425]) cube([len - 0.06, 0.28, 0.02], center = true);
    for (i = [0 : floor(len / 0.30) - 1])
        color((i % 2 == 0) ? ap_PLANTC() : ap_PLANTDC())
            translate([-len / 2 + 0.22 + i * 0.30, 0, 0.50]) sphere(r = 0.13);
}

// ================= 室外库（车辆/飞机/街具；落地件底面 z=0） =================
// 客机（机鼻朝 +x，轮底 z=0）：圆柱机身 + 后掠翼 + 吊挂发动机 + 垂尾
module ap_veh_airliner(body = [0.94, 0.95, 0.96], accent = [0.25, 0.45, 0.75])
{
    color(body)
    {
        translate([-5.5, 0, 1.75]) rotate([0, 90, 0]) cylinder(h = 11.0, r = 1.05, $fn = 28);
        translate([5.5, 0, 1.75]) sphere(r = 1.05);
        translate([-5.5, 0, 1.78]) rotate([0, -85, 0]) cylinder(h = 2.9, r1 = 1.0, r2 = 0.28, $fn = 24);
    }
    color(accent) translate([-0.6, 0, 1.18]) cube([11.0, 2.12, 0.34], center = true);   // 腰线
    color(ap_BLACKC()) translate([4.9, 0, 2.15]) cube([0.80, 2.08, 0.30], center = true);    // 驾驶舱窗带
    for (sy = [-1, 1])
    {
        color([0.16, 0.22, 0.34]) translate([-0.4, 1.02 * sy, 2.10]) cube([8.6, 0.06, 0.17], center = true);  // 舷窗带
        color([0.30, 0.34, 0.42]) translate([3.85, 1.03 * sy, 1.95]) cube([0.42, 0.05, 0.85], center = true); // 前舱门
        // 主翼（后掠 28°，向机尾方向）
        color(body) translate([0.9, 0, 1.45]) rotate([0, 0, 28 * sy])
            translate([0, 2.85 * sy, 0]) cube([1.95, 5.7, 0.13], center = true);
        // 发动机
        color(ap_METALC()) translate([1.45, 2.25 * sy, 0.98]) rotate([0, 90, 0]) cylinder(h = 1.45, r = 0.46, $fn = 18);
        color(ap_BLACKC()) translate([1.42, 2.25 * sy, 0.98]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.40, $fn = 18);
        color(body) translate([2.2, 2.25 * sy, 1.36]) cube([0.85, 0.16, 0.45], center = true);   // 吊架
        // 平尾
        color(body) translate([-7.0, 0, 2.30]) rotate([0, 0, 32 * sy])
            translate([0, 1.0 * sy, 0]) cube([0.80, 2.0, 0.09], center = true);
    }
    // 垂尾 + 尾标
    color(body) translate([-7.0, 0, 3.30]) rotate([0, -36, 0]) cube([0.95, 0.13, 3.10], center = true);
    color(accent) translate([-7.85, 0, 4.30]) rotate([0, -36, 0]) cube([0.98, 0.15, 1.10], center = true);
    // 起落架
    color(ap_DARKMETC())
    {
        translate([4.4, 0, 0.26]) cylinder(h = 0.55, r = 0.07, $fn = 8);
        for (sy = [-1, 1]) translate([-0.3, 0.85 * sy, 0.32]) cylinder(h = 0.50, r = 0.09, $fn = 8);
    }
    color(ap_BLACKC())
    {
        for (sy = [-1, 1]) translate([4.4, 0.12 * sy, 0.26]) rotate([90, 0, 0]) cylinder(h = 0.10, r = 0.26, $fn = 14, center = true);
        for (sy = [-1, 1], sx = [-1, 1]) translate([-0.3 + 0.18 * sx, 0.85 * sy, 0.32]) rotate([90, 0, 0]) cylinder(h = 0.14, r = 0.32, $fn = 14, center = true);
    }
}

// 轿车（车头 +x，轮底 z=0）
module ap_veh_car(c = [0.75, 0.28, 0.24])
{
    color(c) translate([0, 0, 0.71]) cube([4.10, 1.78, 0.62], center = true);
    color([0.20, 0.26, 0.34]) translate([-0.20, 0, 1.22]) cube([1.95, 1.68, 0.42], center = true);
    color(c) translate([-0.20, 0, 1.46]) cube([2.05, 1.72, 0.10], center = true);
    color([0.95, 0.92, 0.75]) for (sy = [-1, 1]) translate([2.06, 0.60 * sy, 0.78]) cube([0.04, 0.30, 0.14], center = true);
    color([0.80, 0.25, 0.20]) for (sy = [-1, 1]) translate([-2.06, 0.60 * sy, 0.78]) cube([0.04, 0.30, 0.14], center = true);
    color(ap_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([1.30 * sx, 0.92 * sy, 0.32]) rotate([90, 0, 0]) cylinder(h = 0.22, r = 0.32, $fn = 14, center = true);
}

module ap_veh_taxi()
{
    ap_veh_car([0.92, 0.78, 0.20]);
    color(ap_WHITEC()) translate([-0.20, 0, 1.57]) cube([0.55, 0.24, 0.16], center = true);
    color(ap_BLACKC()) translate([-0.20, 0, 1.56]) cube([0.40, 0.26, 0.06], center = true);
}

// 公交车（车头 +x，front=-y 侧为车门）
module ap_veh_bus(c = [0.25, 0.45, 0.75])
{
    color(c) translate([0, 0, 1.70]) cube([8.60, 2.35, 2.10], center = true);
    color(ap_WHITEC()) translate([0, 0, 2.80]) cube([8.60, 2.30, 0.14], center = true);
    color(ap_GRAYC()) translate([-1.5, 0, 2.92]) cube([1.60, 1.40, 0.18], center = true);
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
    color(ap_WHITEC()) translate([4.32, 0.7, 2.70]) cube([0.04, 0.80, 0.30], center = true);  // 路牌
    color(ap_BLACKC()) for (sx = [-1, 1], i = [0, 1])
        translate([2.9 * sx - 0.0 * i, (i == 0 ? -1 : 1) * 1.05, 0.42]) rotate([90, 0, 0]) cylinder(h = 0.24, r = 0.42, $fn = 14, center = true);
}

// 行李拖车头 + 行李板车
module ap_veh_baggage_tug()
{
    color([0.90, 0.62, 0.20]) translate([0, 0, 0.62]) cube([1.70, 1.00, 0.46], center = true);
    color([0.16, 0.22, 0.34]) translate([0.45, 0, 1.18]) cube([0.70, 0.90, 0.55], center = true);
    color([0.90, 0.62, 0.20]) translate([0.45, 0, 1.50]) cube([0.80, 0.96, 0.10], center = true);
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([0.45, 0.44 * sx, 0.95]) cube([0.06, 0.06, 1.05], center = true);
    color(ap_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, 0.55 * sy, 0.24]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.24, $fn = 12, center = true);
}

module ap_veh_baggage_cart(c1 = [0.72, 0.40, 0.26], c2 = [0.35, 0.55, 0.75])
{
    color(ap_GRAYC()) translate([0, 0, 0.46]) cube([1.55, 0.95, 0.10], center = true);
    color(ap_GRAYC()) for (sx = [-1, 1]) translate([0.72 * sx, 0, 0.95]) cube([0.06, 0.90, 0.90], center = true);
    color([0.62, 0.64, 0.66]) translate([0, 0, 1.42]) cube([1.60, 0.98, 0.06], center = true);
    color(c1) translate([-0.32, 0.05, 0.70]) cube([0.55, 0.70, 0.38], center = true);
    color(c2) translate([0.30, -0.08, 0.66]) cube([0.50, 0.60, 0.30], center = true);
    color([0.50, 0.55, 0.40]) translate([0.05, 0.10, 1.00]) cube([0.60, 0.55, 0.26], center = true);
    color(ap_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, 0.42 * sy, 0.17]) rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.17, $fn = 10, center = true);
}

// 加油车
module ap_veh_fuel_truck()
{
    color([0.90, 0.62, 0.20]) translate([1.65, 0, 1.05]) cube([1.30, 1.70, 1.10], center = true);
    color([0.16, 0.22, 0.34]) translate([2.10, 0, 1.42]) cube([0.45, 1.60, 0.55], center = true);
    color(ap_GRAYC()) translate([-0.4, 0, 0.62]) cube([3.40, 1.50, 0.35], center = true);
    color(ap_METALC()) translate([-1.9, 0, 1.25]) rotate([0, 90, 0]) cylinder(h = 3.0, r = 0.72, $fn = 20);
    color(ap_METALC()) translate([-1.95, 0, 1.25]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.76, $fn = 20);
    color(ap_BLACKC()) for (sx = [-1.4, 0.2, 1.6], sy = [-1, 1])
        translate([sx, 0.78 * sy, 0.34]) rotate([90, 0, 0]) cylinder(h = 0.20, r = 0.34, $fn = 12, center = true);
}

// 客梯车（台阶朝 +y 升高）
module ap_veh_stairs_truck()
{
    color(ap_GRAYC()) translate([0, -0.9, 0.50]) cube([1.40, 1.60, 0.30], center = true);
    color(ap_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, -0.9 + 0.55 * sy, 0.26]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.26, $fn = 12, center = true);
    for (i = [0 : 5])
        color(ap_METALC()) translate([0, -0.55 + i * 0.42, 0.50 + i * 0.21]) cube([1.10, 0.45, 0.10], center = true);
    color(ap_METALC()) translate([0, 1.75, 1.72]) cube([1.10, 0.65, 0.10], center = true);     // 顶部平台
    color(ap_DARKMETC()) for (sx = [-1, 1])
    {
        translate([0.53 * sx, 0.45, 1.45]) rotate([26, 0, 0]) cube([0.05, 2.6, 0.05], center = true);
        translate([0.53 * sx, 1.75, 2.10]) cube([0.05, 0.65, 0.05], center = true);
    }
}

module ap_prop_container(c = [0.55, 0.58, 0.64])
{
    color(c) translate([0, 0, 0.72]) cube([1.55, 1.40, 1.44], center = true);
    color([0.40, 0.42, 0.46]) translate([0.40, -0.705, 0.72]) cube([0.02, 0.012, 1.30], center = true);
    color([0.40, 0.42, 0.46]) translate([-0.40, -0.705, 0.72]) cube([0.02, 0.012, 1.30], center = true);
}

module ap_prop_cone()
{
    color([0.90, 0.45, 0.18]) cylinder(h = 0.34, r1 = 0.10, r2 = 0.025, $fn = 10);
    color([0.90, 0.45, 0.18]) translate([0, 0, 0.005]) cube([0.26, 0.26, 0.02], center = true);
}

// 高杆机坪照明灯
module ap_prop_light_mast()
{
    color(ap_DARKMETC())
    {
        cylinder(h = 0.25, r = 0.22, $fn = 12);
        cylinder(h = 6.4, r = 0.09, $fn = 10);
        translate([0, 0, 6.30]) cube([1.60, 0.10, 0.10], center = true);
    }
    for (i = [0 : 3])
        color([0.95, 0.92, 0.78]) translate([-0.60 + i * 0.40, 0, 6.18]) cube([0.28, 0.16, 0.14], center = true);
}

module ap_prop_windsock()
{
    color(ap_METALC()) cylinder(h = 3.0, r = 0.04, $fn = 8);
    color(ap_WHITEC()) translate([0, 0, 2.96]) sphere(r = 0.06);
    color([0.92, 0.50, 0.18]) translate([0.05, 0, 2.92]) rotate([0, 96, 8]) cylinder(h = 0.95, r1 = 0.16, r2 = 0.05, $fn = 10);
}

// 路灯 / 行道树 / 绿篱 / 室外长椅
module ap_prop_lamp_post()
{
    color(ap_DARKMETC())
    {
        cylinder(h = 0.18, r = 0.14, $fn = 10);
        cylinder(h = 4.4, r = 0.06, $fn = 8);
        translate([0, 0.35, 4.36]) cube([0.08, 0.80, 0.07], center = true);
    }
    color([0.95, 0.92, 0.78]) translate([0, 0.70, 4.30]) cube([0.22, 0.45, 0.12], center = true);
}

module ap_prop_tree()
{
    color([0.48, 0.36, 0.22]) cylinder(h = 1.1, r = 0.12, $fn = 8);
    color(ap_PLANTDC()) translate([0, 0, 1.45]) sphere(r = 0.62);
    color(ap_PLANTC()) translate([0.25, 0.15, 1.85]) sphere(r = 0.45);
    color(ap_PLANTC()) translate([-0.28, -0.12, 1.70]) sphere(r = 0.38);
}

module ap_prop_hedge(len = 2.2)
{
    color(ap_PLANTDC()) translate([0, 0, 0.30]) cube([len, 0.55, 0.60], center = true);
    color(ap_PLANTC()) translate([0.1, 0.05, 0.58]) cube([len - 0.35, 0.40, 0.14], center = true);
}

module ap_prop_bench_out()
{
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([0.62 * sx, 0, 0.20]) cube([0.08, 0.50, 0.40], center = true);
    color(ap_OAKC())
    {
        for (i = [0 : 2]) translate([0, -0.16 + i * 0.16, 0.43]) cube([1.55, 0.12, 0.05], center = true);
        translate([0, 0.26, 0.70]) rotate([12, 0, 0]) cube([1.55, 0.05, 0.45], center = true);
    }
}

// 公交候车亭（front=-y 朝马路）
module ap_prop_bus_shelter()
{
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([1.45 * sx, 0.40, 1.25]) cube([0.10, 0.10, 2.50], center = true);
    color([0.55, 0.72, 0.85, 0.45]) translate([0, 0.46, 1.35]) cube([3.00, 0.05, 1.60], center = true);
    color(ap_GRAYC()) translate([0, 0.20, 2.56]) cube([3.30, 1.30, 0.10], center = true);
    color(ap_DARKMETC()) translate([0, 0.30, 0.42]) cube([2.40, 0.35, 0.08], center = true);
    color(ap_DARKMETC()) for (sx = [-1, 1]) translate([1.0 * sx, 0.30, 0.20]) cube([0.08, 0.30, 0.40], center = true);
    color(ap_SIGNBLUE()) translate([1.55, 0.40, 2.30]) cube([0.06, 0.45, 0.45], center = true);
}

// 停车场道闸 + P 牌
module ap_prop_barrier_gate()
{
    color(ap_GRAYC()) translate([0, 0, 0.55]) cube([0.40, 0.35, 1.10], center = true);
    color(ap_WHITEC()) translate([1.35, 0, 0.98]) cube([2.60, 0.09, 0.10], center = true);
    color([0.80, 0.25, 0.20]) translate([2.50, 0, 0.98]) cube([0.30, 0.10, 0.11], center = true);
}

module ap_prop_p_sign()
{
    color(ap_DARKMETC()) cylinder(h = 2.4, r = 0.05, $fn = 8);
    color(ap_SIGNBLUE()) translate([0, 0, 2.55]) cube([0.62, 0.08, 0.62], center = true);
    color(ap_WHITEC()) translate([-0.14, -0.045, 2.32]) rotate([90, 0, 0]) linear_extrude(0.03) text("P", size = 0.42);
}

// 机坪/陆侧分隔栅栏（沿 +x）
module ap_prop_fence(len)
{
    for (p = [0 : 2 : len]) color(ap_GRAYC()) translate([p, 0, 0.60]) cube([0.08, 0.08, 1.20], center = true);
    color(ap_GRAYC()) translate([len / 2, 0, 1.14]) cube([len, 0.05, 0.06], center = true);
    color(ap_GRAYC()) translate([len / 2, 0, 0.30]) cube([len, 0.05, 0.06], center = true);
    color([0.70, 0.72, 0.75, 0.35]) translate([len / 2, 0, 0.72]) cube([len, 0.02, 0.80], center = true);
}

// 广告灯箱塔（双面彩屏）
module ap_prop_ad_totem(c = [0.90, 0.55, 0.25])
{
    color(ap_DARKMETC()) translate([0, 0, 0.06]) cube([0.90, 0.34, 0.12], center = true);
    color(ap_DARKMETC()) translate([0, 0, 1.32]) cube([0.80, 0.16, 2.40], center = true);
    for (sy = [-1, 1])
    {
        color(c) translate([0, 0.085 * sy, 1.60]) cube([0.66, 0.012, 1.50], center = true);
        color(ap_WHITEC()) translate([0, 0.092 * sy, 1.18]) cube([0.50, 0.012, 0.14], center = true);
        color(ap_WHITEC()) translate([0, 0.092 * sy, 2.05]) cube([0.42, 0.012, 0.20], center = true);
    }
}

// 行李推车队（3 辆嵌套停放）
module ap_prop_trolley_row()
{
    for (i = [0 : 2])
        translate([0, i * 0.42, 0])
        {
            color(ap_METALC())
            {
                for (sx = [-1, 1]) translate([0.26 * sx, 0, 0.50]) cube([0.04, 0.66, 1.00], center = true);
                translate([0, 0.30, 0.96]) cube([0.56, 0.05, 0.05], center = true);    // 推把
                translate([0, -0.05, 0.32]) cube([0.52, 0.55, 0.04], center = true);   // 底篮
            }
            color(ap_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
                translate([0.24 * sx, 0.25 * sy, 0.085]) rotate([90, 0, 0]) cylinder(h = 0.04, r = 0.085, $fn = 10, center = true);
        }
}

// 书架（书店用，front = -y）
module ap_prop_bookshelf_tall()
{
    color([0.42, 0.30, 0.20])
    {
        for (sx = [-1, 1]) translate([0.50 * sx, 0, 1.0]) cube([0.06, 0.34, 2.0], center = true);
        translate([0, 0.15, 1.0]) cube([1.06, 0.04, 2.0], center = true);
        for (i = [0 : 3]) translate([0, 0, 0.04 + i * 0.63]) cube([1.00, 0.32, 0.05], center = true);
        translate([0, 0, 1.975]) cube([1.06, 0.34, 0.05], center = true);
    }
    for (row = [0 : 2], i = [0 : 5])
        color(ap_book5((i + row * 2) % 5))
            translate([-0.38 + i * 0.13, 0.03, 0.255 + row * 0.63 + ((i % 3 == 1) ? -0.02 : 0)])
                cube([0.085, 0.22, (i % 3 == 1) ? 0.34 : 0.38], center = true);
}

// ================= 商店细化库（front = -y） =================
// 快餐柜台：红色柜体 + 托盘滑轨 + 双收银 + 餐品 + 头顶菜单板（局部宽 5）
module ap_furn_food_counter()
{
    color(ap_REDFOOD()) translate([0, 0, 0.50]) cube([5.00, 0.65, 1.00], center = true);
    color(ap_WHITEC()) translate([0, 0, 1.025]) cube([5.12, 0.72, 0.05], center = true);
    color(ap_METALC()) translate([0, -0.42, 0.88]) cube([4.60, 0.06, 0.04], center = true);   // 托盘轨
    color(ap_METALC()) for (sx = [-2.2, 0, 2.2]) translate([sx, -0.42, 0.70]) cube([0.05, 0.06, 0.36], center = true);
    for (sx = [-1.4, 1.4])
    {
        color(ap_BLACKC()) translate([sx, 0.08, 1.05]) cube([0.30, 0.24, 0.05], center = true);
        translate([sx, 0.18, 1.07]) rotate([0, 0, 180]) ap_furn_monitor(ap_SCREENC(), 0.30);
    }
    // 餐品：汉堡 + 薯条盒 + 饮料杯 + 托盘
    color([0.85, 0.62, 0.30]) translate([-0.35, -0.10, 1.05]) cylinder(h = 0.05, r = 0.09, $fn = 12);
    color([0.55, 0.30, 0.16]) translate([-0.35, -0.10, 1.10]) cylinder(h = 0.04, r = 0.085, $fn = 12);
    color([0.90, 0.72, 0.38]) translate([-0.35, -0.10, 1.14]) scale([1, 1, 0.55]) sphere(r = 0.09);
    color(ap_REDFOOD()) translate([0.05, -0.12, 1.10]) cube([0.10, 0.10, 0.16], center = true);
    color([0.95, 0.82, 0.35]) translate([0.05, -0.12, 1.17]) cube([0.07, 0.07, 0.10], center = true);
    color([0.90, 0.30, 0.25]) translate([0.42, -0.08, 1.05]) cylinder(h = 0.16, r1 = 0.05, r2 = 0.065, $fn = 10);
    color([0.62, 0.64, 0.66]) translate([0.0, -0.05, 1.045]) cube([0.85, 0.50, 0.015], center = true);
    // 头顶菜单板
    color(ap_DARKMETC()) for (sx = [-1.9, 1.9]) translate([sx, 0.15, 1.85]) cube([0.07, 0.07, 1.60], center = true);
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
module ap_furn_kitchen_strip(len = 5)
{
    color(ap_METALC()) translate([0, 0.05, 0.45]) cube([len, 0.70, 0.90], center = true);
    color([0.30, 0.32, 0.36])
    {
        translate([-len / 4, 0.0, 1.08]) cube([0.70, 0.55, 0.36], center = true);    // 炸炉
        translate([len / 4, 0.0, 1.00]) cube([0.80, 0.55, 0.20], center = true);     // 扒炉
    }
    color([0.95, 0.70, 0.25]) translate([-len / 4 - 0.12, -0.18, 1.28]) cube([0.10, 0.04, 0.04], center = true);
    color(ap_METALC()) translate([0, 0.10, 2.10]) cube([len * 0.7, 0.75, 0.45], center = true);   // 排烟罩
    color([0.55, 0.58, 0.62]) translate([0, 0.10, 1.60]) cube([0.35, 0.45, 0.55], center = true); // 烟道
}

// 书店展台：矮桌 + 平摊书堆 + 立书
module ap_furn_book_table()
{
    color(ap_OAKC()) translate([0, 0, 0.70]) cube([1.50, 0.95, 0.06], center = true);
    color([0.60, 0.42, 0.25]) for (sx = [-1, 1]) translate([0.65 * sx, 0, 0.34]) cube([0.08, 0.85, 0.68], center = true);
    for (i = [0 : 2])
        color(ap_book5(i)) translate([-0.45 + i * 0.45, 0.18, 0.765 + (i == 1 ? 0.02 : 0)])
            rotate([0, 0, i * 14 - 10]) cube([0.30, 0.40, 0.07 + i * 0.02], center = true);
    for (i = [0 : 2])
        color(ap_book5(i + 2)) translate([-0.40 + i * 0.42, -0.24, 0.755]) rotate([0, 0, -i * 8]) cube([0.28, 0.38, 0.05], center = true);
    color(ap_book5(4)) translate([0.55, 0.10, 0.90]) rotate([78, 0, -15]) cube([0.26, 0.36, 0.03], center = true);
}

// 杂志架（斜板三层）
module ap_furn_mag_rack()
{
    color([0.60, 0.42, 0.25]) translate([0, 0.12, 0.80]) cube([1.20, 0.06, 1.60], center = true);
    color([0.60, 0.42, 0.25]) for (sx = [-1, 1]) translate([0.60 * sx, 0, 0.80]) cube([0.05, 0.32, 1.60], center = true);
    for (lv = [0 : 2])
    {
        color([0.70, 0.52, 0.33]) translate([0, -0.02, 0.40 + lv * 0.48]) rotate([16, 0, 0]) cube([1.12, 0.04, 0.34], center = true);
        for (i = [0 : 3])
            color(ap_goods6((i + lv * 2 + 1) % 6))
                translate([-0.41 + i * 0.27, -0.05, 0.43 + lv * 0.48]) rotate([16, 0, 0]) cube([0.22, 0.015, 0.30], center = true);
    }
}

// 礼品展示柜：白基座 + 玻璃罩（items=false 时罩内留空，用于摆大件）
module ap_furn_display_case(items = true)
{
    color(ap_WHITEC()) translate([0, 0, 0.36]) cube([1.60, 0.70, 0.72], center = true);
    color(ap_GLASSC()) translate([0, 0, 1.11]) cube([1.50, 0.60, 0.78], center = true);
    color(ap_METALC()) translate([0, 0, 1.515]) cube([1.54, 0.64, 0.03], center = true);
    if (items)
    {
        color(ap_goods6(0)) translate([-0.45, 0.05, 0.82]) cube([0.18, 0.18, 0.20], center = true);
        color(ap_goods6(2)) translate([0.0, -0.08, 0.80]) cube([0.15, 0.15, 0.16], center = true);
        color(ap_goods6(4)) translate([0.40, 0.06, 0.84]) cube([0.16, 0.16, 0.24], center = true);
        color([0.85, 0.75, 0.40]) translate([0.18, 0.14, 0.72]) cylinder(h = 0.22, r = 0.05, $fn = 10);
    }
}

// 冰柜（卧式 + 玻璃顶）
module ap_furn_freezer_chest()
{
    color(ap_WHITEC()) translate([0, 0, 0.42]) cube([1.30, 0.68, 0.84], center = true);
    color([0.55, 0.72, 0.85, 0.40]) translate([0, -0.05, 0.865]) cube([1.18, 0.50, 0.04], center = true);
    for (i = [0 : 3])
        color(ap_goods6((i * 2 + 1) % 6)) translate([-0.42 + i * 0.28, -0.05, 0.74]) cube([0.20, 0.40, 0.10], center = true);
    color(ap_SIGNBLUE()) translate([0, 0.345, 0.62]) cube([1.30, 0.012, 0.28], center = true);
}

// 室内玻璃隔断（员工办公室用，h2.4，可留门洞；同 office.scad）
module ap_part_glass_run(x0, x1)
{
    color(ap_DARKMETC()) translate([(x0 + x1) / 2, 0, 0.06]) cube([x1 - x0, 0.10, 0.12], center = true);
    color(ap_GLASSC()) translate([(x0 + x1) / 2, 0, 1.21]) cube([x1 - x0 - 0.02, 0.05, 2.06], center = true);
}

module ap_part_glass_wall(len, g0 = -1, g1 = -1)
{
    color(ap_DARKMETC()) translate([len / 2, 0, 2.30]) cube([len, 0.10, 0.12], center = true);
    if (g0 >= 0) { ap_part_glass_run(0, g0); ap_part_glass_run(g1, len); }
    else         ap_part_glass_run(0, len);
    if (g0 >= 0) { for (p = [0, g0, g1, len]) color(ap_DARKMETC()) translate([p, 0, 1.18]) cube([0.09, 0.11, 2.36], center = true); }
    else         { for (p = [0, len])         color(ap_DARKMETC()) translate([p, 0, 1.18]) cube([0.09, 0.11, 2.36], center = true); }
}
