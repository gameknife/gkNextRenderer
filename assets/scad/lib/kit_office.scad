// kit_office.scad —— generated from office.scad by tools/scadkit (kit split M0)
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "of_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y。调用方自设 $fn（建议 12）。



// ---- 配色（对齐示意图：绿墙 / 木地板 / 浅木桌 / 藏青沙发 / 青色前台） ----
function of_WALLC() = [0.64, 0.80, 0.52];    // 浅绿墙
function of_TRIMC() = [0.58, 0.56, 0.76];    // 薰衣草踢脚线
function of_SEAMC() = [0.30, 0.18, 0.10];    // 地板缝/深木
function of_OAKC() = [0.78, 0.57, 0.34];    // 浅橡木桌面
function of_OAKDARKC() = [0.60, 0.42, 0.25];    // 桌体/茶几
function of_DARKWOODC() = [0.30, 0.21, 0.14];    // 老板桌深木
function of_CONFWOODC() = [0.63, 0.43, 0.24];    // 会议桌
function of_WHITEC() = [0.92, 0.91, 0.88];
function of_GRAYC() = [0.58, 0.60, 0.63];    // 柜子/储物
function of_METALC() = [0.72, 0.74, 0.77];    // 银色金属
function of_DARKMETC() = [0.22, 0.23, 0.26];    // 玻璃框/椅脚/显示器
function of_BLACKC() = [0.10, 0.10, 0.11];
function of_GLASSC() = [0.55, 0.72, 0.85, 0.30];
function of_SCREENC() = [0.62, 0.78, 0.92];    // 亮屏
function of_CHAIRWC() = [0.90, 0.89, 0.85];    // 白色工学椅
function of_BEIGEC() = [0.85, 0.77, 0.60];    // 米色会议椅
function of_NAVYC() = [0.22, 0.27, 0.37];    // 藏青沙发
function of_NAVYLITC() = [0.30, 0.36, 0.48];    // 沙发坐垫
function of_REDC() = [0.72, 0.16, 0.14];    // 老板椅
function of_TEALC() = [0.16, 0.62, 0.55];    // 前台
function of_PLANTC() = [0.34, 0.58, 0.31];
function of_PLANTDC() = [0.24, 0.45, 0.24];
function of_POTC() = [0.70, 0.44, 0.30];    // 陶土盆
function of_CARDC() = [0.76, 0.62, 0.42];    // 纸箱
function of_DOORC() = [0.70, 0.44, 0.22];    // 木门
function of_PAPERC() = [0.95, 0.95, 0.92];

// 离散取色 helper（避免依赖 list 下标）
function of_tone3(i)  = i == 0 ? [0.55, 0.345, 0.185] : i == 1 ? [0.50, 0.31, 0.165] : [0.60, 0.385, 0.215];
function of_bind4(i)  = i == 0 ? [0.30, 0.45, 0.75] : i == 1 ? [0.88, 0.88, 0.86]
                   : i == 2 ? [0.55, 0.45, 0.75] : [0.25, 0.55, 0.55];
function of_book5(i)  = i == 0 ? [0.62, 0.30, 0.26] : i == 1 ? [0.28, 0.45, 0.60] : i == 2 ? [0.80, 0.70, 0.45]
                   : i == 3 ? [0.36, 0.55, 0.38] : [0.50, 0.38, 0.58];

// ================= 地板 / 墙体 =================
// 深色基板 + 三色木条拼板（顶面 z=0.15，对齐 kGroundY）
module of_office_floor()
{
    color(of_SEAMC()) cube([24, 18, 0.26], center = true);
    for (ix = [0:7], iy = [0:29])
        translate([-12 + 1.5 + ix * 3, -9 + 0.3 + iy * 0.6, 0.125])
            color(of_tone3((ix * 7 + iy * 13) % 3))
                cube([2.92, 0.56, 0.05], center = true);
}

// 全高墙段（沿 +x 居中，含薰衣草踢脚线）
module of_wall_segment(len)
{
    color(of_WALLC()) cube([len, 0.25, 2.6], center = true);
    translate([0, 0, -1.23]) color(of_TRIMC()) cube([len, 0.33, 0.14], center = true);
}

// 南侧矮墙（切墙视角）：高 0.5 + 顶部薰衣草压条
module of_wall_knee(len)
{
    color(of_WALLC()) cube([len, 0.25, 0.5], center = true);
    translate([0, 0, 0.28]) color(of_TRIMC()) cube([len, 0.29, 0.06], center = true);
}

// ================= 玻璃隔断（深框全高玻璃，可留门洞） =================
module of_part_glass_run(x0, x1)
{
    color(of_DARKMETC()) translate([(x0 + x1) / 2, 0, 0.06]) cube([x1 - x0, 0.10, 0.12], center = true);
    color(of_GLASSC())   translate([(x0 + x1) / 2, 0, 1.21]) cube([x1 - x0 - 0.02, 0.05, 2.06], center = true);
    for (p = [x0 + 1.35 : 1.35 : x1 - 0.25])
        color(of_DARKMETC()) translate([p, 0, 1.21]) cube([0.05, 0.08, 2.06], center = true);
}

// 沿 +x 长 len；g0/g1 为门洞区间（局部坐标），-1 = 无门洞。门洞上方保留横梁。
module of_part_glass_wall(len, g0 = -1, g1 = -1)
{
    color(of_DARKMETC()) translate([len / 2, 0, 2.30]) cube([len, 0.10, 0.12], center = true);
    if (g0 >= 0) { of_part_glass_run(0, g0); of_part_glass_run(g1, len); }
    else         of_part_glass_run(0, len);
    for (p = (g0 >= 0) ? [0, g0, g1, len] : [0, len])
        color(of_DARKMETC()) translate([p, 0, 1.18]) cube([0.09, 0.11, 2.36], center = true);
}

// ================= 家具库（统一约定：front = -y，底面 z=0） =================
module of_furn_monitor(screen = of_SCREENC(), w = 0.56)
{
    color(of_DARKMETC())
    {
        cylinder(h = 0.025, r = 0.10);
        translate([0, 0.02, 0.16]) cube([0.05, 0.04, 0.28], center = true);
    }
    color(of_BLACKC())  translate([0, 0.030, 0.42]) cube([w, 0.035, 0.34], center = true);
    color(screen)  translate([0, 0.006, 0.42]) cube([w - 0.05, 0.015, 0.29], center = true);
}

module of_furn_task_chair(seat = of_CHAIRWC())
{
    color(of_DARKMETC())
    {
        for (a = [0 : 72 : 288]) rotate([0, 0, a + 36]) translate([0.16, 0, 0.03]) cube([0.30, 0.05, 0.05], center = true);
        translate([0, 0, 0.05]) cylinder(h = 0.42, r = 0.030, $fn = 12);
        translate([0, 0.20, 0.56]) cube([0.05, 0.05, 0.16], center = true);
    }
    color(seat) translate([0, 0, 0.49]) cube([0.46, 0.45, 0.07], center = true);
    color(seat) translate([0, 0.225, 0.81]) cube([0.44, 0.06, 0.46], center = true);
}

module of_furn_conf_chair()
{
    color(of_DARKMETC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.17 * sx, 0.16 * sy, 0]) cylinder(h = 0.41, r = 0.020, $fn = 10);
    color(of_BEIGEC()) translate([0, 0, 0.45]) cube([0.46, 0.44, 0.08], center = true);
    color(of_BEIGEC()) translate([0, 0.195, 0.76]) cube([0.44, 0.07, 0.54], center = true);
    color(of_DARKMETC()) for (sx = [-1, 1])
    {
        translate([0.235 * sx, 0.02, 0.555]) cube([0.04, 0.04, 0.13], center = true);
        translate([0.235 * sx, -0.04, 0.63]) cube([0.05, 0.30, 0.04], center = true);
    }
}

module of_furn_boss_chair()
{
    color(of_BLACKC())
    {
        for (a = [0 : 72 : 288]) rotate([0, 0, a + 36]) translate([0.19, 0, 0.035]) cube([0.36, 0.06, 0.06], center = true);
        translate([0, 0, 0.06]) cylinder(h = 0.38, r = 0.035, $fn = 12);
        for (sx = [-1, 1])
        {
            translate([0.28 * sx, 0.0, 0.55]) cube([0.05, 0.05, 0.18], center = true);
            translate([0.28 * sx, -0.03, 0.66]) cube([0.07, 0.34, 0.05], center = true);
        }
    }
    color(of_REDC()) translate([0, 0, 0.47]) cube([0.52, 0.50, 0.10], center = true);
    color(of_REDC()) translate([0, 0.245, 0.92]) cube([0.50, 0.09, 0.80], center = true);
    color(of_BLACKC()) translate([0, 0.255, 1.24]) cube([0.34, 0.10, 0.16], center = true);  // 头枕
}

module of_furn_armchair(c = of_NAVYC())
{
    color(of_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.33 * sx, 0.28 * sy, 0.025]) cube([0.07, 0.07, 0.05], center = true);
    color(c)
    {
        translate([0, 0, 0.23]) cube([0.78, 0.70, 0.36], center = true);
        for (sx = [-1, 1]) translate([0.345 * sx, 0, 0.52]) cube([0.15, 0.70, 0.26], center = true);
        translate([0, 0.27, 0.60]) cube([0.78, 0.16, 0.44], center = true);
    }
    color(of_NAVYLITC()) translate([0, -0.04, 0.45]) cube([0.46, 0.50, 0.09], center = true);
}

module of_furn_sofa()
{
    color(of_BLACKC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.76 * sx, 0.28 * sy, 0.025]) cube([0.07, 0.07, 0.05], center = true);
    color(of_NAVYC())
    {
        translate([0, 0, 0.23]) cube([1.70, 0.72, 0.36], center = true);
        for (sx = [-1, 1]) translate([0.79 * sx, 0, 0.50]) cube([0.13, 0.72, 0.24], center = true);
        translate([0, 0.28, 0.58]) cube([1.70, 0.16, 0.46], center = true);
    }
    color(of_NAVYLITC()) for (sx = [-1, 1])
    {
        translate([0.36 * sx, -0.04, 0.45]) cube([0.68, 0.52, 0.09], center = true);
        translate([0.36 * sx, 0.245, 0.66]) cube([0.66, 0.12, 0.34], center = true);
    }
}

// 标准工位：橡木桌 + 抽屉柜 + 显示器/外设 + 工学椅（front=-y 为椅子侧）
module of_furn_workstation(dual = false, laptop = false, tablet = false, sticky = false,
                        screen = of_SCREENC(), mug = [0.40, 0.60, 0.80], plant = false, crot = 0)
{
    // 桌体
    color(of_OAKC()) translate([0, 0, 0.715]) cube([1.70, 0.80, 0.05], center = true);
    color(of_OAKDARKC())
    {
        for (sx = [-1, 1]) translate([0.80 * sx, 0, 0.345]) cube([0.06, 0.74, 0.69], center = true);
        translate([0, 0.34, 0.50]) cube([1.58, 0.05, 0.40], center = true);
    }
    // 抽屉柜（桌下右侧）
    color(of_OAKC()) translate([0.52, 0.04, 0.31]) cube([0.40, 0.56, 0.62], center = true);
    for (i = [0 : 2])
    {
        color(of_OAKDARKC()) translate([0.52, -0.245, 0.12 + i * 0.20]) cube([0.32, 0.035, 0.155], center = true);
        color(of_DARKMETC()) translate([0.52, -0.265, 0.17 + i * 0.20]) cube([0.12, 0.02, 0.025], center = true);
    }
    // 桌面外设
    color([0.16, 0.18, 0.22]) translate([0.27, -0.17, 0.745]) cube([0.30, 0.24, 0.012], center = true);
    color(of_BLACKC()) translate([-0.06, -0.16, 0.755]) cube([0.40, 0.135, 0.025], center = true);
    color(of_BLACKC()) translate([0.30, -0.17, 0.765]) cube([0.06, 0.10, 0.030], center = true);
    if (dual)
    {
        translate([-0.26, 0.16, 0.74]) rotate([0, 0, -9]) of_furn_monitor(screen, 0.50);
        translate([ 0.26, 0.16, 0.74]) rotate([0, 0,  9]) of_furn_monitor(screen, 0.50);
    }
    else
        translate([0, 0.16, 0.74]) of_furn_monitor(screen);
    if (laptop)
    {
        translate([-0.46, -0.08, 0.74]) rotate([0, 0, 18])
        {
            color(of_DARKMETC()) translate([0, 0, 0.01]) cube([0.34, 0.24, 0.02], center = true);
            color(of_BLACKC()) translate([0, 0.135, 0.115]) rotate([12, 0, 0]) cube([0.34, 0.02, 0.21], center = true);
            color(screen) translate([0, 0.122, 0.115]) rotate([12, 0, 0]) cube([0.30, 0.01, 0.17], center = true);
        }
    }
    if (tablet)
    {
        color(of_BLACKC()) translate([-0.05, -0.18, 0.748]) rotate([0, 0, -6]) cube([0.38, 0.27, 0.016], center = true);
        color([0.45, 0.48, 0.54]) translate([-0.05, -0.17, 0.758]) rotate([0, 0, -6]) cube([0.30, 0.20, 0.008], center = true);
        color(of_BLACKC()) translate([0.18, -0.30, 0.752]) rotate([0, 0, 35]) cube([0.012, 0.14, 0.012], center = true);
    }
    if (sticky)
    {
        color([0.95, 0.88, 0.40]) translate([-0.42, -0.30, 0.748]) rotate([0, 0, 12]) cube([0.06, 0.06, 0.008], center = true);
        color([0.92, 0.60, 0.72]) translate([-0.33, -0.34, 0.748]) rotate([0, 0, -8]) cube([0.06, 0.06, 0.008], center = true);
    }
    if (plant)
    {
        color(of_WHITEC()) translate([0.70, 0.22, 0.74]) cylinder(h = 0.08, r = 0.05, $fn = 12);
        color(of_PLANTC()) translate([0.70, 0.22, 0.88]) sphere(r = 0.085);
    }
    color(of_PAPERC()) translate([-0.55, 0.16, 0.745]) rotate([0, 0, -14]) cube([0.24, 0.30, 0.012], center = true);
    color(mug) translate([-0.66, -0.12, 0.74]) cylinder(h = 0.10, r = 0.045, $fn = 12);
    color([0.35, 0.37, 0.40]) translate([-0.70, -0.45, 0]) cylinder(h = 0.32, r = 0.13, $fn = 12);
    translate([0, -0.85, 0]) rotate([0, 0, crot]) of_furn_task_chair();
}

// 老板桌：深木 + 东侧返台 + 红色行政椅（front=-y 为老板侧）
module of_furn_exec_desk()
{
    color(of_DARKWOODC())
    {
        translate([0, 0, 0.73]) cube([2.00, 0.90, 0.06], center = true);
        for (sx = [-1, 1]) translate([0.93 * sx, 0, 0.35]) cube([0.07, 0.84, 0.70], center = true);
        translate([0, 0.39, 0.475]) cube([1.86, 0.06, 0.45], center = true);
        translate([1.30, 0.32, 0.32]) cube([0.55, 1.15, 0.64], center = true);   // L 返台
    }
    for (i = [0 : 1])
        color(of_DARKMETC()) translate([1.045, 0.10 + i * 0.40, 0.50]) cube([0.02, 0.14, 0.025], center = true);
    color([0.18, 0.24, 0.20]) translate([0, -0.08, 0.765]) cube([0.62, 0.40, 0.015], center = true);
    translate([0, 0.20, 0.76]) of_furn_monitor(of_SCREENC());
    color(of_BLACKC()) translate([0, -0.10, 0.77]) cube([0.36, 0.12, 0.022], center = true);
    // 电话
    color(of_BLACKC()) translate([0.62, 0.12, 0.785]) cube([0.17, 0.21, 0.05], center = true);
    color(of_DARKMETC()) translate([0.555, 0.12, 0.825]) cube([0.045, 0.19, 0.035], center = true);
    // 铭牌 + 笔筒 + 文件
    color([0.80, 0.66, 0.36]) translate([-0.10, -0.38, 0.795]) cube([0.30, 0.05, 0.07], center = true);
    color(of_DARKMETC()) translate([0.40, 0.28, 0.76]) cylinder(h = 0.10, r = 0.035, $fn = 10);
    color(of_PAPERC()) translate([-0.62, 0.18, 0.775]) rotate([0, 0, 10]) cube([0.26, 0.33, 0.025], center = true);
    // 台灯
    color(of_DARKMETC())
    {
        translate([-0.75, 0.26, 0.76]) cylinder(h = 0.02, r = 0.07, $fn = 12);
        translate([-0.75, 0.26, 0.78]) cylinder(h = 0.34, r = 0.018, $fn = 8);
    }
    color([0.93, 0.89, 0.78]) translate([-0.70, 0.22, 1.13]) rotate([0, 25, -30]) cube([0.20, 0.10, 0.07], center = true);
    translate([0, -0.98, 0]) of_furn_boss_chair();
}

// 椭圆会议桌（hull 胶囊面）+ 会议电话 + 散落文件
module of_furn_conf_table()
{
    color(of_CONFWOODC()) translate([0, 0, 0.70]) hull()
    {
        translate([-1.05, 0, 0]) cylinder(h = 0.07, r = 0.95, $fn = 48);
        translate([ 1.05, 0, 0]) cylinder(h = 0.07, r = 0.95, $fn = 48);
    }
    color(of_DARKMETC()) for (sx = [-1, 1]) translate([0.95 * sx, 0, 0])
    {
        cylinder(h = 0.05, r = 0.30, $fn = 24);
        cylinder(h = 0.70, r = 0.09, $fn = 16);
    }
    color(of_BLACKC()) translate([0, 0, 0.77]) cylinder(h = 0.035, r = 0.11, $fn = 18);
    color(of_DARKMETC()) for (a = [0 : 120 : 240])
        rotate([0, 0, a]) translate([0.15, 0, 0.785]) cube([0.10, 0.03, 0.018], center = true);
    color(of_PAPERC())
    {
        translate([-0.85, 0.42, 0.775]) rotate([0, 0, 15]) cube([0.30, 0.22, 0.012], center = true);
        translate([0.70, -0.40, 0.775]) rotate([0, 0, -25]) cube([0.30, 0.22, 0.012], center = true);
    }
    color([0.30, 0.34, 0.40]) translate([0.0, 0.45, 0.775]) rotate([0, 0, 80]) cube([0.30, 0.21, 0.018], center = true);
}

// ================= 道具库 =================
module of_prop_rug(w, d, c) color(c) translate([0, 0, 0.01]) cube([w, d, 0.022], center = true);
module of_prop_mat() color([0.28, 0.30, 0.28]) translate([0, 0, 0.011]) cube([2.7, 1.05, 0.022], center = true);

module of_prop_plant_tall()
{
    color(of_POTC()) cylinder(h = 0.32, r1 = 0.15, r2 = 0.19);
    color([0.25, 0.17, 0.10]) translate([0, 0, 0.30]) cylinder(h = 0.48, r = 0.035, $fn = 8);
    color(of_PLANTDC()) translate([0, 0, 0.86]) sphere(r = 0.26);
    color(of_PLANTC())  translate([0.14, 0.06, 1.04]) sphere(r = 0.20);
    color(of_PLANTC())  translate([-0.13, -0.09, 0.70]) sphere(r = 0.17);
}

// 矮绿植隔断（开放区软分隔）
module of_prop_planter(len = 2.4)
{
    color(of_WHITEC()) translate([0, 0, 0.21]) cube([len, 0.34, 0.42], center = true);
    color([0.22, 0.15, 0.09]) translate([0, 0, 0.425]) cube([len - 0.06, 0.28, 0.02], center = true);
    for (i = [0 : floor(len / 0.30) - 1])
        color((i % 2 == 0) ? of_PLANTC() : of_PLANTDC())
            translate([-len / 2 + 0.22 + i * 0.30, 0, 0.50]) sphere(r = 0.13);
}

module of_prop_clock()
{
    rotate([90, 0, 0])
    {
        color(of_DARKMETC()) cylinder(h = 0.04, r = 0.18, $fn = 24);
        color(of_WHITEC()) translate([0, 0, 0.041]) cylinder(h = 0.012, r = 0.148, $fn = 24);
        color(of_BLACKC()) translate([0, 0.045, 0.056]) cube([0.016, 0.10, 0.008], center = true);
        color(of_BLACKC()) rotate([0, 0, -60]) translate([0, 0.032, 0.056]) cube([0.016, 0.075, 0.008], center = true);
    }
}

module of_prop_frame(w = 0.45, h = 0.34, art = [0.45, 0.60, 0.70])
{
    color(of_DARKWOODC()) cube([w, 0.035, h], center = true);
    color(art) translate([0, -0.012, 0]) cube([w - 0.07, 0.025, h - 0.07], center = true);
}

module of_prop_certificates()
{
    for (i = [0 : 2])
        translate([(i - 1) * 0.30, 0, (i == 1) ? 0.16 : 0])
        {
            color(of_WHITEC()) cube([0.22, 0.03, 0.28], center = true);
            color([0.85, 0.82, 0.72]) translate([0, -0.01, 0]) cube([0.17, 0.025, 0.23], center = true);
        }
}

module of_prop_whiteboard()
{
    color(of_METALC()) cube([2.40, 0.06, 1.30], center = true);
    color([0.97, 0.97, 0.95]) translate([0, -0.015, 0]) cube([2.28, 0.045, 1.18], center = true);
    color(of_METALC()) translate([0, -0.06, -0.66]) cube([0.85, 0.10, 0.04], center = true);
    color([0.75, 0.25, 0.22]) translate([-0.55, -0.042, 0.25]) rotate([0, -8, 0]) cube([0.55, 0.012, 0.035], center = true);
    color([0.25, 0.40, 0.70]) translate([-0.45, -0.042, -0.05]) rotate([0, 5, 0]) cube([0.70, 0.012, 0.035], center = true);
    color([0.30, 0.55, 0.35]) translate([0.62, -0.042, 0.10]) cube([0.45, 0.012, 0.30], center = true);
}

module of_prop_projector_screen()
{
    color(of_DARKMETC()) translate([0, 0, 0.86]) cube([3.10, 0.10, 0.12], center = true);
    color([0.96, 0.96, 0.94]) translate([0, 0, 0]) cube([2.86, 0.035, 1.60], center = true);
    color(of_DARKMETC()) translate([0, 0, -0.83]) cube([2.90, 0.06, 0.06], center = true);
}

module of_prop_window_blinds()
{
    color(of_WHITEC())
    {
        translate([0, 0, 0.66]) cube([1.70, 0.08, 0.08], center = true);
        translate([0, 0, -0.66]) cube([1.70, 0.08, 0.08], center = true);
        for (sx = [-1, 1]) translate([0.81 * sx, 0, 0]) cube([0.08, 0.08, 1.40], center = true);
    }
    color([0.70, 0.82, 0.92]) cube([1.56, 0.03, 1.26], center = true);
    for (i = [0 : 6])
        color([0.93, 0.91, 0.85]) translate([0, -0.03, 0.51 - i * 0.17]) cube([1.54, 0.022, 0.105], center = true);
    color(of_DARKMETC()) translate([0.68, -0.05, 0.22]) cube([0.012, 0.012, 0.55], center = true);
}

module of_prop_door_double()
{
    color(of_DARKWOODC())
    {
        for (sx = [-1, 1]) translate([1.00 * sx, 0, 1.025]) cube([0.10, 0.16, 2.05], center = true);
        translate([0, 0, 2.10]) cube([2.10, 0.16, 0.10], center = true);
    }
    for (sx = [-1, 1]) translate([0.475 * sx, 0, 0])
    {
        color(of_DOORC()) translate([0, 0, 1.0]) cube([0.93, 0.07, 2.0], center = true);
        color([0.60, 0.36, 0.17])
        {
            translate([0, -0.025, 1.50]) cube([0.70, 0.03, 0.62], center = true);
            translate([0, -0.025, 0.62]) cube([0.70, 0.03, 0.80], center = true);
        }
        color(of_METALC()) translate([-0.36 * sx, -0.05, 1.02]) sphere(r = 0.038);
    }
}

module of_prop_water_cooler()
{
    color(of_WHITEC()) translate([0, 0, 0.475]) cube([0.34, 0.34, 0.95], center = true);
    color([0.30, 0.55, 0.80]) translate([0.06, -0.165, 0.78]) cube([0.05, 0.04, 0.05], center = true);
    color([0.80, 0.30, 0.30]) translate([-0.06, -0.165, 0.78]) cube([0.05, 0.04, 0.05], center = true);
    color([0.55, 0.75, 0.92, 0.45])
    {
        translate([0, 0, 0.95]) cylinder(h = 0.26, r = 0.125);
        translate([0, 0, 1.21]) sphere(r = 0.125);
    }
    color(of_WHITEC()) translate([0, 0, 1.31]) cylinder(h = 0.05, r = 0.045, $fn = 12);
}

module of_prop_fridge()
{
    color([0.82, 0.84, 0.86]) translate([0, 0, 0.79]) cube([0.68, 0.60, 1.58], center = true);
    color([0.62, 0.64, 0.67]) translate([0, -0.305, 1.06]) cube([0.62, 0.012, 0.012], center = true);
    color(of_DARKMETC())
    {
        translate([-0.24, -0.32, 1.28]) cube([0.04, 0.03, 0.42], center = true);
        translate([-0.24, -0.32, 0.62]) cube([0.04, 0.03, 0.60], center = true);
    }
}

// 彩色文件夹置物架
module of_prop_binder_shelf()
{
    color(of_DARKWOODC())
    {
        for (sx = [-1, 1]) translate([0.67 * sx, 0, 0.925]) cube([0.06, 0.34, 1.85], center = true);
        translate([0, 0.15, 0.925]) cube([1.40, 0.04, 1.85], center = true);
        for (i = [0 : 3]) translate([0, 0, 0.04 + i * 0.59]) cube([1.34, 0.32, 0.05], center = true);
    }
    for (row = [0 : 2], i = [0 : 7])
        color(of_bind4((i + row * 3) % 4))
            translate([-0.56 + i * 0.16, 0.02, 0.225 + row * 0.59])
                rotate([0, 0, (i == 5 && row == 1) ? 14 : 0])
                    cube([0.07, 0.24, 0.31], center = true);
}

// 通用书架（老板办公室 / 西墙）
module of_prop_bookshelf_tall()
{
    color(of_DARKWOODC())
    {
        for (sx = [-1, 1]) translate([0.50 * sx, 0, 1.0]) cube([0.06, 0.34, 2.0], center = true);
        translate([0, 0.15, 1.0]) cube([1.06, 0.04, 2.0], center = true);
        for (i = [0 : 3]) translate([0, 0, 0.04 + i * 0.63]) cube([1.00, 0.32, 0.05], center = true);
        translate([0, 0, 1.975]) cube([1.06, 0.34, 0.05], center = true);
    }
    for (row = [0 : 2], i = [0 : 5])
        color(of_book5((i + row * 2) % 5))
            translate([-0.38 + i * 0.13, 0.03, 0.255 + row * 0.63 + ((i % 3 == 1) ? -0.02 : 0)])
                cube([0.085, 0.22, (i % 3 == 1) ? 0.34 : 0.38], center = true);
}

module of_prop_credenza()
{
    color(of_CONFWOODC()) translate([0, 0, 0.36]) cube([1.60, 0.42, 0.72], center = true);
    color(of_DARKWOODC()) translate([0, -0.215, 0.36]) cube([0.02, 0.012, 0.60], center = true);
    color(of_DARKMETC()) for (sx = [-1, 1]) translate([0.20 * sx, -0.22, 0.50]) cube([0.10, 0.02, 0.022], center = true);
    for (i = [0 : 2])
        color(of_bind4(i)) translate([-0.45 + i * 0.13, 0.05, 0.875]) cube([0.09, 0.24, 0.31], center = true);
    color(of_WHITEC()) translate([0.55, 0, 0.72]) cylinder(h = 0.07, r = 0.06, $fn = 12);
    color(of_PLANTC()) translate([0.55, 0, 0.86]) sphere(r = 0.10);
}

// 打印角：矮柜 + 复印机 + 纸堆
module of_prop_printer_island()
{
    color(of_WHITEC()) translate([0, 0, 0.32]) cube([0.92, 0.55, 0.64], center = true);
    color(of_DARKMETC()) translate([0, 0, 0.03]) cube([0.86, 0.50, 0.06], center = true);
    color([0.80, 0.81, 0.83]) translate([-0.10, 0, 0.73]) cube([0.55, 0.44, 0.18], center = true);
    color([0.70, 0.71, 0.74]) translate([-0.10, 0, 0.89]) cube([0.50, 0.40, 0.14], center = true);
    color(of_BLACKC()) translate([-0.10, -0.21, 0.84]) cube([0.40, 0.02, 0.04], center = true);
    color(of_TEALC()) translate([0.08, -0.215, 0.93]) cube([0.10, 0.012, 0.05], center = true);
    color(of_PAPERC())
    {
        translate([-0.10, -0.29, 0.80]) cube([0.30, 0.16, 0.018], center = true);
        translate([0.30, 0.05, 0.70]) cube([0.24, 0.30, 0.12], center = true);
    }
}

module of_prop_bins()
{
    color([0.25, 0.45, 0.75]) translate([-0.17, 0, 0]) cylinder(h = 0.36, r = 0.14, $fn = 12);
    color(of_GRAYC()) translate([0.17, 0, 0]) cylinder(h = 0.36, r = 0.14, $fn = 12);
}

module of_prop_locker()
{
    color(of_GRAYC()) translate([0, 0, 0.925]) cube([0.78, 0.50, 1.85], center = true);
    for (sx = [-1, 1])
    {
        color([0.66, 0.68, 0.71]) translate([0.19 * sx, -0.255, 0.925]) cube([0.33, 0.012, 1.73], center = true);
        color(of_DARKMETC()) translate([0.055 * sx, -0.27, 1.05]) cube([0.025, 0.02, 0.11], center = true);
        color([0.50, 0.52, 0.55]) translate([0.19 * sx, -0.265, 1.62]) cube([0.26, 0.012, 0.05], center = true);
        color([0.50, 0.52, 0.55]) translate([0.19 * sx, -0.265, 1.52]) cube([0.26, 0.012, 0.05], center = true);
    }
}

module of_prop_server_rack()
{
    color([0.12, 0.13, 0.15]) translate([0, 0, 0.96]) cube([0.72, 0.62, 1.92], center = true);
    color([0.07, 0.08, 0.09]) translate([0, -0.30, 0.96]) cube([0.60, 0.03, 1.72], center = true);
    for (r = [0 : 7])
    {
        color((r % 3 == 0) ? [0.95, 0.70, 0.25] : [0.35, 0.90, 0.45])
            translate([-0.20, -0.325, 0.24 + r * 0.21]) cube([0.025, 0.015, 0.025], center = true);
        color([0.30, 0.60, 0.95]) translate([-0.13, -0.325, 0.24 + r * 0.21]) cube([0.025, 0.015, 0.025], center = true);
        color([0.22, 0.24, 0.27]) translate([0.08, -0.325, 0.24 + r * 0.21]) cube([0.34, 0.012, 0.05], center = true);
    }
}

module of_prop_boxes()
{
    color(of_CARDC()) translate([0, 0, 0.26]) cube([0.56, 0.56, 0.52], center = true);
    color([0.82, 0.70, 0.50]) translate([0, 0, 0.525]) cube([0.56, 0.09, 0.012], center = true);
    color(of_CARDC()) translate([0.02, -0.03, 0.73]) rotate([0, 0, 18]) cube([0.50, 0.50, 0.42], center = true);
    color([0.82, 0.70, 0.50]) translate([0.02, -0.03, 0.945]) rotate([0, 0, 18]) cube([0.50, 0.08, 0.012], center = true);
    color(of_CARDC()) translate([0.62, 0.10, 0.225]) rotate([0, 0, -14]) cube([0.46, 0.46, 0.45], center = true);
}

module of_prop_shelf_unit()
{
    color(of_GRAYC())
    {
        for (sx = [-1, 1]) translate([0.70 * sx, 0, 0.75]) cube([0.05, 0.40, 1.50], center = true);
        translate([0, 0.185, 0.75]) cube([1.45, 0.03, 1.50], center = true);
        for (i = [0 : 2]) translate([0, 0, 0.05 + i * 0.70]) cube([1.40, 0.38, 0.05], center = true);
    }
    for (i = [0 : 2])
        color(of_CARDC()) translate([-0.42 + i * 0.42, 0, 0.26]) cube([0.36, 0.32, 0.32], center = true);
    for (i = [0 : 1])
        color(of_bind4(i + 1)) translate([-0.30 + i * 0.30, 0, 0.92]) cube([0.24, 0.28, 0.30], center = true);
}

module of_prop_floor_lamp()
{
    color(of_DARKMETC())
    {
        cylinder(h = 0.03, r = 0.16, $fn = 16);
        cylinder(h = 1.42, r = 0.022, $fn = 8);
    }
    color([0.93, 0.89, 0.78]) translate([0, 0, 1.30]) cylinder(h = 0.28, r1 = 0.20, r2 = 0.14);
}

module of_prop_coat_stand()
{
    color(of_DARKWOODC())
    {
        cylinder(h = 0.04, r = 0.17, $fn = 12);
        cylinder(h = 1.70, r = 0.025, $fn = 8);
        for (a = [0 : 90 : 270]) rotate([0, 0, a + 45]) translate([0.09, 0, 1.58]) rotate([0, -22, 0]) cube([0.17, 0.03, 0.03], center = true);
    }
}

module of_prop_beanbag(c) color(c) translate([0, 0, 0.19]) scale([1, 1, 0.55]) sphere(r = 0.36);

module of_prop_foosball()
{
    color(of_DARKMETC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.55 * sx, 0.26 * sy, 0.31]) cube([0.07, 0.07, 0.62], center = true);
    color(of_OAKDARKC()) translate([0, 0, 0.74]) cube([1.30, 0.72, 0.24], center = true);
    color([0.30, 0.58, 0.32]) translate([0, 0, 0.862]) cube([1.16, 0.58, 0.012], center = true);
    for (i = [0 : 5])
    {
        color(of_METALC()) translate([-0.45 + i * 0.18, 0, 0.92]) rotate([90, 0, 0]) cylinder(h = 1.04, r = 0.016, $fn = 8, center = true);
        color(of_BLACKC()) translate([-0.45 + i * 0.18, 0.50, 0.92]) rotate([90, 0, 0]) cylinder(h = 0.09, r = 0.034, $fn = 8, center = true);
        color(of_BLACKC()) translate([-0.45 + i * 0.18, -0.50, 0.92]) rotate([90, 0, 0]) cylinder(h = 0.09, r = 0.034, $fn = 8, center = true);
        for (j = [-1 : 1])
            color((i % 2 == 0) ? [0.78, 0.22, 0.18] : [0.25, 0.42, 0.72])
                translate([-0.45 + i * 0.18, j * 0.17 + ((i % 2 == 0) ? 0.05 : -0.05), 0.875]) cube([0.04, 0.05, 0.11], center = true);
    }
}

// 青色 L 形前台（front=-y；左端返台在 +y 侧；含接待椅/显示器/logo）
module of_prop_reception()
{
    // 主吧台
    color(of_TEALC()) translate([0, 0, 0.53]) cube([2.70, 0.55, 1.06], center = true);
    color([0.12, 0.50, 0.44]) translate([0, -0.276, 0.36]) cube([2.70, 0.012, 0.18], center = true);
    color(of_WHITEC()) translate([0, 0, 1.09]) cube([2.84, 0.68, 0.06], center = true);
    // 返台（左端，往 +y）
    color(of_TEALC()) translate([-1.62, 0.925, 0.53]) cube([0.55, 1.30, 1.06], center = true);
    color(of_WHITEC()) translate([-1.62, 0.955, 1.09]) cube([0.68, 1.44, 0.06], center = true);
    // 内侧工作台 + 设备
    color(of_WHITEC()) translate([0.30, 0.475, 0.70]) cube([2.0, 0.42, 0.05], center = true);
    translate([0.0, 0.50, 0.725]) rotate([0, 0, 180]) of_furn_monitor(of_SCREENC(), 0.50);
    color(of_BLACKC()) translate([0.55, 0.42, 0.74]) cube([0.15, 0.19, 0.045], center = true);
    color(of_BLACKC()) translate([-0.05, 0.30, 0.735]) cube([0.34, 0.12, 0.02], center = true);
    // 台面摆件：绿植 + 前台铃 + 糖果碗
    color(of_WHITEC()) translate([-1.05, 0, 1.12]) cylinder(h = 0.07, r = 0.06, $fn = 12);
    color(of_PLANTC()) translate([-1.05, 0, 1.26]) sphere(r = 0.10);
    color(of_METALC()) translate([0.95, -0.05, 1.12]) cylinder(h = 0.05, r1 = 0.05, r2 = 0.02, $fn = 12);
    color([0.85, 0.50, 0.30]) translate([0.45, -0.02, 1.13]) cylinder(h = 0.045, r = 0.09, $fn = 12);
    // 正面 logo
    color(of_WHITEC()) translate([-0.62, -0.287, 0.52]) rotate([90, 0, 0]) linear_extrude(0.025) text("StudioSim", size = 0.20);
    // 接待椅（朝吧台）
    translate([0.45, 1.05, 0]) rotate([0, 0, 0]) of_furn_task_chair([0.85, 0.83, 0.78]);
}