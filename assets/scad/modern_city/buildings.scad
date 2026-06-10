// ============================================================
// Modern City Diorama - 建筑模块与街区布局
// 所有建筑：本地原点在底面中心，正面朝 -Y
// ============================================================

$fn = 24;

use <common.scad>
use <props.scad>
use <roads.scad>

// 连续玻璃窗带（四面环绕）
module win_band(L, D, z, hh=1.25) {
    color(c_glass()) translate([0, -D/2 - 0.05, z]) boxc([L*0.80, 0.1, hh]);
    color(c_glass()) translate([0,  D/2 + 0.05, z]) boxc([L*0.80, 0.1, hh]);
    color(c_glass()) translate([-L/2 - 0.05, 0, z]) boxc([0.1, D*0.72, hh]);
    color(c_glass()) translate([ L/2 + 0.05, 0, z]) boxc([0.1, D*0.72, hh]);
}

// 平屋顶杂项（大楼：双空调 + 水箱）
module roof_stuff(L, D) {
    translate([-L/4, -D/5, 0]) ac_unit();
    translate([-L/4 + 2.2, -D/5, 0]) ac_unit();
    color(c_grey())  translate([L/4, D/5, 0.7]) cylinder(h=1.4, r=0.55, $fn=8, center=true);
    color(c_dgrey()) translate([L/4, D/5, 1.45]) cylinder(h=0.25, r=0.65, $fn=8, center=true);
}

// 平屋顶杂项（小楼：单空调）
module roof_small(L, D) {
    translate([L/5, -D/6, 0]) ac_unit();
}

// 通用平顶楼
module flat_building(L=14, D=12, H=10, floors=3, c=[0.95,0.90,0.77]) {
    color(c) translate([0, 0, H/2]) boxc([L, D, H]);
    for (f = [0 : floors - 1])
        win_band(L, D, 1.9 + f*(floors > 1 ? (H - 3.4)/(floors - 1) : 0), 1.25);
    color(c_white()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3, 0.45, 0.6);
    translate([0, 0, H]) roof_stuff(L, D);
}

// 屋顶直升机停机坪
module helipad(r=6) {
    color(c_blue()) cylinder(h=0.25, r=r, $fn=8);
    color(c_mark()) translate([0, 0, 0.25])
    linear_extrude(0.04)
    difference() {
        circle(r=r*0.92, $fn=8);
        circle(r=r*0.78, $fn=8);
    }
    color(c_mark()) translate([-1.1, 0, 0.27]) boxc([0.75, 3.6, 0.05]);
    color(c_mark()) translate([ 1.1, 0, 0.27]) boxc([0.75, 3.6, 0.05]);
    color(c_mark()) translate([0, 0, 0.27]) boxc([1.50, 0.75, 0.05]);
}

// ---- 医院：主楼 + 带停机坪副楼 + 入口雨棚 ----
module hospital() {
    // 主楼 30 x 18 x 14
    translate([-4, 0, 0]) {
        color(c_white()) translate([0, 0, 7]) boxc([30, 18, 14]);
        win_band(30, 18, 2.2, 1.3);
        win_band(30, 18, 5.4, 1.3);
        win_band(30, 18, 8.6, 1.3);
        color(c_white()) translate([0, 0, 14]) parapet(30.3, 18.3, 0.45, 0.55);
        translate([0, 0, 14]) roof_stuff(30, 18);

        // 蓝色标识带 + 字
        color(c_blue()) translate([0, -9.18, 12.6]) boxc([22, 0.3, 1.7]);
        color(c_mark()) translate([0, -9.40, 12.6]) rotate([90, 0, 0])
            linear_extrude(0.07)
            text("HOSPITAL", size=1.05, halign="center", valign="center");

        // 蓝十字
        color(c_mark()) translate([0, -9.18, 6.2]) boxc([3.2, 0.28, 3.2]);
        color(c_blue()) translate([0, -9.34, 6.2]) boxc([0.85, 0.28, 2.5]);
        color(c_blue()) translate([0, -9.34, 6.2]) boxc([2.5, 0.28, 0.85]);

        // 入口雨棚
        color(c_blue()) translate([0, -11.2, 3.5]) boxc([8.5, 4.6, 0.35]);
        color(c_grey()) translate([-3.5, -12.9, 0]) cylinder(h=3.5, r=0.16, $fn=8);
        color(c_grey()) translate([ 3.5, -12.9, 0]) cylinder(h=3.5, r=0.16, $fn=8);
    }

    // 副楼 14 x 16 x 9，屋顶停机坪
    translate([13.5, 1, 0]) {
        color(c_white()) translate([0, 0, 4.5]) boxc([14, 16, 9]);
        win_band(14, 16, 2.4, 1.2);
        win_band(14, 16, 5.6, 1.2);
        color(c_white()) translate([0, 0, 9]) parapet(14.3, 16.3, 0.45, 0.5);
        translate([0, 0, 9.05]) helipad(6);
    }
}

// ---- 快餐店：锯齿招牌 + 条纹遮阳棚 ----
module zigzag_sign(W=10, h=2) {
    color(c_red())
    rotate([90, 0, 0])
    linear_extrude(0.45, center=true)
    polygon(points=[
        [-W/2, 0], [-W*0.375, h], [-W*0.25, h*0.45],
        [-W*0.125, h], [0, h*0.45],
        [W*0.125, h], [W*0.25, h*0.45], [W*0.375, h], [W/2, 0]
    ]);
}

module fastfood() {
    L = 16; D = 12; H = 4.8;
    color(c_white()) translate([0, 0, H/2]) boxc([L, D, H]);
    win_band(L, D, 2.2, 1.5);
    color(c_red()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3, 0.4, 0.5);
    translate([0, -D/2 + 0.8, H]) zigzag_sign(L*0.7, 2.2);
    translate([0, -D/2 - 0.05, 2.9]) awning(L*0.85, 1.6);
    color(c_dgrey()) translate([0, -D/2 - 0.06, 1.1]) boxc([2.6, 0.12, 2.2]);
    translate([0, 0, H]) roof_small(L, D);
}

// ---- 咖啡店 ----
module coffee_shop() {
    L = 13; D = 10; H = 5.5;
    color(c_white()) translate([0, 0, H/2]) boxc([L, D, H]);
    win_band(L, D, 2.0, 1.4);
    color(c_brown()) translate([0, -D/2 - 0.15, H - 0.8]) boxc([L*0.8, 0.3, 1.3]);
    color(c_mark())  translate([0, -D/2 - 0.32, H - 0.8]) rotate([90, 0, 0])
        linear_extrude(0.06)
        text("COFFEE", size=0.85, halign="center", valign="center");
    translate([0, -D/2 - 0.05, 3.2]) awning(L*0.8, 1.5);
    color(c_white()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3);
    translate([0, 0, H]) roof_small(L, D);
}

// ---- 理发店：转柱 + 招牌 ----
module barber_pole() {
    color(c_dgrey()) cylinder(h=2.6, r=0.09, $fn=8);
    color(c_mark()) translate([0, 0, 2.00]) cylinder(h=0.9, r=0.26, $fn=10);
    color(c_red())  translate([0, 0, 2.25]) cylinder(h=0.22, r=0.28, $fn=10);
    color(c_blue()) translate([0, 0, 2.62]) cylinder(h=0.22, r=0.28, $fn=10);
}

module barber_shop() {
    L = 12; D = 10; H = 6;
    color(c_brown()) translate([0, 0, H/2]) boxc([L, D, H]);
    color(c_white()) translate([0, 0, 0.5]) boxc([L + 0.2, D + 0.2, 1.0]);
    win_band(L, D, 2.1, 1.5);
    color(c_dark())   translate([0, -D/2 - 0.15, H - 1.0]) boxc([L*0.85, 0.3, 1.4]);
    color(c_yellow()) translate([0, -D/2 - 0.32, H - 1.0]) rotate([90, 0, 0])
        linear_extrude(0.06)
        text("BARBER", size=0.8, halign="center", valign="center");
    color(c_white()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3);
    translate([0, 0, H]) roof_small(L, D);
    translate([L/2 + 0.4, -D/2 + 0.6, 0]) barber_pole();
}

// ---- 警察局：蓝色裙楼 + 屋顶八角标志 ----
module police_station() {
    L = 16; D = 13; H = 7.5;
    color(c_white()) translate([0, 0, H/2]) boxc([L, D, H]);
    color(c_blue())  translate([0, 0, 1.4]) boxc([L + 0.25, D + 0.25, 2.8]);
    win_band(L, D, 4.6, 1.4);
    color(c_glass()) translate([0, -D/2 - 0.20, 1.5]) boxc([L*0.7, 0.12, 1.5]);
    color(c_dblue()) translate([0, -D/2 - 0.18, H - 1.1]) boxc([L*0.75, 0.32, 1.5]);
    color(c_mark())  translate([0, -D/2 - 0.36, H - 1.1]) rotate([90, 0, 0])
        linear_extrude(0.07)
        text("POLICE", size=0.95, halign="center", valign="center");
    color(c_white()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3);
    translate([0, 0, H]) roof_small(L, D);

    // 屋顶八角标志牌
    color(c_dgrey()) translate([-L/4, D/4, H]) cylinder(h=1.8, r=0.1, $fn=8);
    color(c_blue())  translate([-L/4, D/4, H + 2.2]) rotate([90, 0, 0]) cylinder(h=0.35, r=1.6, $fn=8, center=true);
    color(c_mark())  translate([-L/4, D/4 - 0.12, H + 2.2]) rotate([90, 0, 0])
        linear_extrude(0.06)
        difference() {
            circle(r=1.25, $fn=8);
            circle(r=0.95, $fn=8);
        }
}

// ---- 旅馆 ----
module hotel() {
    L = 14; D = 12; H = 13;
    color(c_cream()) translate([0, 0, H/2]) boxc([L, D, H]);
    for (f = [0 : 3]) win_band(L, D, 2.0 + f*2.9, 1.25);
    color(c_red()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3);
    translate([0, 0, H]) roof_small(L, D);
    color(c_red())  translate([0, -D/2 - 0.18, H - 1.2]) boxc([6.4, 0.3, 1.6]);
    color(c_mark()) translate([0, -D/2 - 0.35, H - 1.2]) rotate([90, 0, 0])
        linear_extrude(0.07)
        text("HOTEL", size=1.0, halign="center", valign="center");
    color(c_red())  translate([0, -D/2 - 1.6, 3.0]) boxc([5, 3.2, 0.3]);
    color(c_dgrey()) translate([-2, -D/2 - 2.8, 0]) cylinder(h=3.0, r=0.13, $fn=8);
    color(c_dgrey()) translate([ 2, -D/2 - 2.8, 0]) cylinder(h=3.0, r=0.13, $fn=8);
}

// ---- 餐馆（大面橙色遮阳棚 + 立式招牌）----
module restaurant() {
    L = 16; D = 12; H = 6.5;
    color(c_cream()) translate([0, 0, H/2]) boxc([L, D, H]);
    win_band(L, D, 2.0, 1.5);
    win_band(L, D, H - 1.2, 1.0);
    color(c_orange()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3, 0.45, 0.6);
    translate([0, -D/2 - 0.05, 3.6]) awning(L*0.9, 2.0, [0.92,0.58,0.24], [0.95,0.90,0.77]);
    color(c_red()) translate([-L/2 + 0.2, -D/2 + 1.5, H + 1.3]) boxc([0.4, 3.0, 2.6]);
    translate([0, 0, H]) roof_stuff(L, D);
}

// ---- 三开间商铺（彩色遮阳棚）----
module shop_row(L=20, D=10, H=5) {
    color(c_cream()) translate([0, 0, H/2]) boxc([L, D, H]);
    color(c_white()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3, 0.4, 0.5);
    s = L/3;
    for (i = [0 : 2]) {
        translate([-L/2 + s*(i + 0.5), 0, 0]) {
            color(c_glass()) translate([0, -D/2 - 0.06, 1.6]) boxc([s*0.7, 0.12, 1.9]);
            translate([0, -D/2 - 0.05, 3.4])
            awning(s*0.85, 1.4,
                   i == 0 ? [0.86,0.22,0.18] : i == 1 ? [0.92,0.58,0.24] : [0.27,0.56,0.84],
                   [0.95,0.95,0.94]);
        }
    }
    win_band(L, D, H - 1.3, 1.1);
    translate([0, 0, H]) roof_small(L, D);
}

// ---- 小住宅（双坡顶 + 烟囱）----
module house(c=[0.95,0.90,0.77], rc=[0.86,0.22,0.18]) {
    L = 9; D = 7; H = 3.4;
    color(c) translate([0, 0, H/2]) boxc([L, D, H]);
    color(c_glass()) translate([-1.8, -D/2 - 0.06, 1.7]) boxc([1.6, 0.12, 1.2]);
    color(c_glass()) translate([ 1.8, -D/2 - 0.06, 1.7]) boxc([1.6, 0.12, 1.2]);
    color(c_brown()) translate([0, -D/2 - 0.06, 1.05]) boxc([1.3, 0.12, 2.1]);
    translate([0, 0, H]) gable_roof(L, D, 2.4, 0.7, rc);
    color(c_grey()) translate([L/2 - 1.2, 1.0, H + 1.2]) boxc([0.8, 0.8, 2.2]);
}

// ---- 写字楼 ----
module office_tower(L=16, D=16, H=20) {
    color(c_white()) translate([0, 0, H/2]) boxc([L, D, H]);
    for (f = [0 : 4]) win_band(L, D, 2.2 + f*3.6, 1.6);
    color(c_grey()) translate([0, 0, H]) parapet(L + 0.3, D + 0.3, 0.5, 0.7);
    translate([0, 0, H]) roof_stuff(L, D);
    color(c_dgrey()) translate([L/4, -D/4, H]) cylinder(h=3.0, r=0.07, $fn=6);
    color(c_red())   translate([L/4, -D/4, H + 3.1]) blob(0.18);
}

// ============================================================
// 街区布局
// ============================================================
module city_buildings() {
    zb = curb_h();

    // B3 医院街区 (-35, 60)
    translate([-36, 66, zb]) hospital();
    translate([-25, 41, 0]) parking_lot(16, 10, 3);

    // C3 商业街区 (35, 60)
    translate([19, 47, zb]) barber_shop();
    translate([40, 48, zb]) restaurant();
    translate([26, 72, zb]) flat_building(18, 12, 11, 3, [0.90, 0.81, 0.66]);
    translate([50, 71, zb]) flat_building(14, 11, 8, 2, [0.95, 0.90, 0.77]);

    // D3 公寓街区 (95, 60)
    translate([95, 73, zb]) flat_building(15, 11, 12, 4, [0.95, 0.90, 0.77]);
    translate([95, 47, zb]) flat_building(15, 11, 9, 3, [0.80, 0.45, 0.30]);

    // A3 公园 (-95, 60)
    translate([-95, 64, 0]) pond(5.5);

    // A2 写字楼街区 (-95, -5)
    translate([-95, 8, zb]) office_tower(17, 16, 20);
    translate([-95, -21, 0]) parking_lot(22, 12, 4);

    // B2 快餐 + 广场街区 (-35, -5)
    translate([-49, 11, 0]) plaza_spiral(21, 5);
    translate([-24, 2, zb]) fastfood();
    translate([-25, -27, 0]) parking_lot(20, 12, 4);

    // C2 警局 + 咖啡 + 酒店街区 (35, -5)
    translate([22, -16, zb]) police_station();
    translate([21, 12, zb]) coffee_shop();
    translate([49, 6, zb]) hotel();
    translate([47, -18, zb]) flat_building(13, 11, 7, 2, [0.90, 0.81, 0.66]);

    // D2 商铺街 (95, -5)，面朝西侧道路
    translate([86, 8, zb])  rotate([0, 0, -90]) shop_row(22, 10, 5.5);
    translate([86, -18, zb]) rotate([0, 0, -90]) shop_row(16, 10, 5);

    // D1 旅馆街区 (95, -65)
    translate([95, -57, zb]) hotel();
    translate([95, -77, 0]) parking_lot(22, 10, 4);

    // B1 住宅街区 (-35, -65)，门面朝北侧道路
    translate([-52, -64, zb]) rotate([0, 0, 180]) house([0.95,0.90,0.77], [0.86,0.22,0.18]);
    translate([-35, -64, zb]) rotate([0, 0, 180]) house([0.88,0.92,0.95], [0.30,0.45,0.65]);
    translate([-18, -64, zb]) rotate([0, 0, 180]) house([0.93,0.85,0.70], [0.55,0.35,0.20]);

    // C1 球场街区 (35, -65)
    translate([24, -65, 0]) basketball_court(18, 11);
    translate([50, -64, zb]) flat_building(12, 10, 6, 2, [0.95, 0.95, 0.94]);
}

// 标准预览
city_buildings();
