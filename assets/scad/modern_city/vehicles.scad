// ============================================================
// Modern City Diorama - 车辆模型与摆放
// 所有车辆：车头朝 +X，车底 z=0
// ============================================================

$fn = 24;

use <common.scad>

module wheel(r=0.45, w=0.36) {
    color([0.13, 0.13, 0.14]) rotate([90, 0, 0]) cylinder(h=w, r=r, center=true, $fn=10);
    color([0.78, 0.79, 0.80]) rotate([90, 0, 0]) cylinder(h=w + 0.04, r=r*0.45, center=true, $fn=8);
}

// 轿车
module car(body=[0.85, 0.25, 0.20]) {
    color(body) translate([0, 0, 0.72]) boxc([4.2, 1.9, 0.78]);
    color(body) translate([-0.25, 0, 1.36]) boxc([2.3, 1.74, 0.62]);
    color(c_glass()) translate([-0.25, 0, 1.36]) boxc([2.00, 1.82, 0.44]);
    color(c_glass()) translate([-0.25, 0, 1.36]) boxc([2.42, 1.60, 0.44]);
    color(c_mark()) translate([2.11,  0.6, 0.82]) boxc([0.06, 0.34, 0.18]);
    color(c_mark()) translate([2.11, -0.6, 0.82]) boxc([0.06, 0.34, 0.18]);
    color([0.80, 0.15, 0.12]) translate([-2.11,  0.6, 0.82]) boxc([0.06, 0.34, 0.18]);
    color([0.80, 0.15, 0.12]) translate([-2.11, -0.6, 0.82]) boxc([0.06, 0.34, 0.18]);
    translate([ 1.35,  0.93, 0.45]) wheel();
    translate([ 1.35, -0.93, 0.45]) wheel();
    translate([-1.35,  0.93, 0.45]) wheel();
    translate([-1.35, -0.93, 0.45]) wheel();
}

// 出租车
module taxi() {
    car([0.96, 0.78, 0.15]);
    color(c_mark()) translate([-0.25, 0, 1.78]) boxc([0.7, 0.32, 0.24]);
}

// 警车
module police_car() {
    car([0.93, 0.94, 0.95]);
    color(c_dblue()) translate([0, 0, 0.80]) boxc([4.24, 1.94, 0.30]);
    color([0.90, 0.20, 0.16]) translate([-0.45, 0, 1.76]) boxc([0.34, 0.9, 0.20]);
    color(c_blue())           translate([-0.05, 0, 1.76]) boxc([0.34, 0.9, 0.20]);
}

// 救护车
module ambulance() {
    color(c_white()) translate([-0.4, 0, 1.45]) boxc([4.4, 2.1, 1.9]);
    color(c_white()) translate([2.3, 0, 1.05]) boxc([1.4, 1.9, 1.1]);
    color(c_glass()) translate([2.95, 0, 1.30]) boxc([0.20, 1.7, 0.5]);
    color(c_red()) translate([-0.4, 0, 1.05]) boxc([4.44, 2.14, 0.30]);
    color(c_red()) translate([-0.4,  1.06, 1.80]) boxc([0.80, 0.05, 0.26]);
    color(c_red()) translate([-0.4,  1.06, 1.80]) boxc([0.26, 0.05, 0.80]);
    color(c_red()) translate([-0.4, -1.06, 1.80]) boxc([0.80, 0.05, 0.26]);
    color(c_red()) translate([-0.4, -1.06, 1.80]) boxc([0.26, 0.05, 0.80]);
    color(c_blue()) translate([2.3, 0, 1.72]) boxc([0.3, 1.2, 0.22]);
    translate([ 1.7,  1.0, 0.5]) wheel(0.5, 0.4);
    translate([ 1.7, -1.0, 0.5]) wheel(0.5, 0.4);
    translate([-1.6,  1.0, 0.5]) wheel(0.5, 0.4);
    translate([-1.6, -1.0, 0.5]) wheel(0.5, 0.4);
}

// 厢式货车
module box_truck(cabc=[0.30, 0.55, 0.80]) {
    color(cabc) translate([2.5, 0, 1.15]) boxc([1.7, 2.2, 1.7]);
    color(c_glass()) translate([3.28, 0, 1.45]) boxc([0.2, 1.9, 0.6]);
    color(c_grey())  translate([-0.55, 0, 1.55]) boxc([4.4, 2.30, 2.3]);
    color(c_white()) translate([-0.55, 0, 1.55]) boxc([4.2, 2.34, 1.9]);
    translate([ 2.5,  1.05, 0.5]) wheel(0.5, 0.4);
    translate([ 2.5, -1.05, 0.5]) wheel(0.5, 0.4);
    translate([-1.9,  1.05, 0.5]) wheel(0.5, 0.4);
    translate([-1.9, -1.05, 0.5]) wheel(0.5, 0.4);
}

// 城市巴士
module city_bus() {
    color(c_blue()) translate([0, 0, 1.5]) boxc([7.0, 2.3, 2.2]);
    color(c_glass()) translate([0, 0, 1.95]) boxc([6.6, 2.36, 0.8]);
    color(c_glass()) translate([3.46, 0, 1.70]) boxc([0.16, 1.9, 1.0]);
    color(c_mark()) translate([0, 0, 0.75]) boxc([7.02, 2.32, 0.5]);
    translate([ 2.4,  1.06, 0.5]) wheel(0.5, 0.4);
    translate([ 2.4, -1.06, 0.5]) wheel(0.5, 0.4);
    translate([-2.4,  1.06, 0.5]) wheel(0.5, 0.4);
    translate([-2.4, -1.06, 0.5]) wheel(0.5, 0.4);
}

// ============================================================
// 车辆摆放（右侧通行；路面 z=road_t，台面 z=curb_h+0.02）
// ============================================================
module city_vehicles() {
    zr = road_t();
    zb = curb_h() + 0.02;

    // 纵向主路 x=0
    translate([ 2.5, -62, zr]) rotate([0, 0,  90]) taxi();
    translate([-2.5,  -8, zr]) rotate([0, 0, -90]) car([0.85, 0.25, 0.20]);
    translate([ 2.5,  52, zr]) rotate([0, 0,  90]) city_bus();

    // 纵向路 x=-70
    translate([-67.5, -14, zr]) rotate([0, 0,  90]) car([0.30, 0.55, 0.80]);
    translate([-72.5,  48, zr]) rotate([0, 0, -90]) car([0.93, 0.94, 0.95]);

    // 纵向路 x=70
    translate([72.5, -18, zr]) rotate([0, 0,  90]) police_car();
    translate([67.5,  55, zr]) rotate([0, 0, -90]) car([0.92, 0.58, 0.24]);

    // 横向路 y=-40
    translate([-30, -42.5, zr]) box_truck();
    translate([ 58, -37.5, zr]) rotate([0, 0, 180]) car([0.93, 0.94, 0.95]);
    translate([ 92, -42.5, zr]) taxi();

    // 横向路 y=30
    translate([-90, 32.5, zr]) rotate([0, 0, 180]) car([0.45, 0.70, 0.45]);
    translate([ 44, 27.5, zr]) car([0.85, 0.25, 0.20]);

    // 医院门前救护车
    translate([-52, 38.5, zb]) ambulance();

    // B2 快餐店停车场
    translate([-31, -27, zb]) rotate([0, 0, 90]) car([0.30, 0.55, 0.80]);
    translate([-25, -27, zb]) rotate([0, 0, 90]) car([0.93, 0.94, 0.95]);
    translate([-19, -27, zb]) rotate([0, 0, 90]) taxi();

    // A2 写字楼停车场
    translate([-101, -23, zb]) rotate([0, 0, 90]) car([0.85, 0.25, 0.20]);
    translate([ -89, -23, zb]) rotate([0, 0, 90]) car([0.92, 0.58, 0.24]);

    // D1 旅馆停车场
    translate([ 89, -77, zb]) rotate([0, 0, 90]) car([0.45, 0.70, 0.45]);
    translate([101, -77, zb]) rotate([0, 0, 90]) car([0.30, 0.55, 0.80]);
}

// 标准预览
translate([0,  0, 0]) car();
translate([0,  4, 0]) taxi();
translate([0,  8, 0]) police_car();
translate([0, 12, 0]) ambulance();
translate([0, 16, 0]) box_truck();
translate([0, 21, 0]) city_bus();
