// ============================================================
// Modern City Diorama - 街道小品 / 树木 / 广场 / 球场
// ============================================================

$fn = 24;

use <common.scad>

// 低多边形行道树
module tree(h=2.6, r=2.3) {
    color(c_trunk()) cylinder(h=h, r=0.32, $fn=7);
    translate([0, 0, h]) {
        color(c_leaf())   blob(r);
        color(c_leaf_d()) translate([ r*0.55,  r*0.25, -r*0.15]) blob(r*0.62);
        color(c_leaf())   translate([-r*0.50, -r*0.30,  r*0.20]) blob(r*0.55);
    }
}

module bush(r=0.9) {
    color(c_leaf_d()) translate([0, 0, r*0.55]) blob(r);
}

// 路灯：灯臂朝 +X
module street_lamp(h=5.6) {
    color(c_dgrey()) cylinder(h=h, r=0.13, $fn=8);
    color(c_dgrey()) translate([1.0, 0, h - 0.12]) boxc([2.2, 0.16, 0.16]);
    color([1.0, 0.95, 0.75]) translate([1.9, 0, h - 0.30]) boxc([1.1, 0.4, 0.22]);
}

// 红绿灯：悬臂朝 +X，灯面朝 -Y
module traffic_light(h=4.8) {
    color(c_dark()) cylinder(h=h, r=0.13, $fn=8);
    color(c_dark()) translate([1.3, 0, h - 0.12]) boxc([2.6, 0.14, 0.14]);
    color(c_dark()) translate([2.4, 0, h - 0.75]) boxc([0.5, 0.42, 1.35]);
    color([0.90, 0.22, 0.16]) translate([2.4, -0.23, h - 0.35]) rotate([90, 0, 0]) cylinder(h=0.05, r=0.15, $fn=10);
    color([0.95, 0.76, 0.20]) translate([2.4, -0.23, h - 0.75]) rotate([90, 0, 0]) cylinder(h=0.05, r=0.15, $fn=10);
    color([0.25, 0.80, 0.32]) translate([2.4, -0.23, h - 1.15]) rotate([90, 0, 0]) cylinder(h=0.05, r=0.15, $fn=10);
}

module hydrant() {
    color(c_red()) cylinder(h=0.85, r=0.22, $fn=8);
    color(c_red()) translate([0, 0, 0.85]) blob(0.20);
    color(c_red()) translate([0, 0, 0.55]) rotate([0, 90, 0]) cylinder(h=0.7, r=0.09, center=true, $fn=6);
}

module mailbox() {
    color(c_red())  translate([0, 0, 0.75]) boxc([0.8, 0.55, 0.9]);
    color(c_mark()) translate([0, -0.29, 1.0]) boxc([0.5, 0.04, 0.08]);
    color(c_dark()) translate([-0.25, 0, 0.18]) boxc([0.12, 0.4, 0.36]);
    color(c_dark()) translate([ 0.25, 0, 0.18]) boxc([0.12, 0.4, 0.36]);
}

// 公园长椅：靠背朝 +Y
module bench() {
    color([0.62, 0.44, 0.26]) translate([0, 0, 0.45]) boxc([1.9, 0.5, 0.08]);
    color([0.62, 0.44, 0.26]) translate([0, 0.28, 0.75]) rotate([12, 0, 0]) boxc([1.9, 0.08, 0.5]);
    color(c_dark()) translate([-0.8, 0, 0.22]) boxc([0.1, 0.45, 0.45]);
    color(c_dark()) translate([ 0.8, 0, 0.22]) boxc([0.1, 0.45, 0.45]);
}

// 公交候车亭：开口朝 -Y（面向道路）
module bus_stop() {
    color(c_dgrey()) translate([0, 0.95, 2.45]) boxc([5.2, 2.3, 0.18]);
    color([0.55, 0.72, 0.88, 0.45]) translate([0, 1.95, 1.2]) boxc([5.0, 0.1, 2.2]);
    color([0.55, 0.72, 0.88, 0.45]) translate([-2.45, 0.95, 1.2]) boxc([0.1, 2.0, 2.2]);
    color([0.55, 0.72, 0.88, 0.45]) translate([ 2.45, 0.95, 1.2]) boxc([0.1, 2.0, 2.2]);
    color(c_dgrey()) translate([0, 1.6, 0.55]) boxc([4.0, 0.5, 0.12]);
    color(c_dgrey()) translate([-1.6, 1.6, 0.25]) boxc([0.12, 0.4, 0.5]);
    color(c_dgrey()) translate([ 1.6, 1.6, 0.25]) boxc([0.12, 0.4, 0.5]);
}

// 红白通讯塔
module radio_mast(h=26) {
    for (i = [0 : 4]) {
        color(i % 2 == 0 ? c_mark() : c_red())
        translate([0, 0, i*h/5])
        cylinder(h=h/5 + 0.02, r1=0.55 - 0.085*i, r2=0.55 - 0.085*(i + 1), $fn=8);
    }
    color(c_red()) translate([0, 0, h]) blob(0.35);

    for (z = [h*0.35, h*0.65]) {
        color(c_dgrey()) translate([0, 0, z]) rotate([0, 90, 0]) cylinder(h=4.5 - z*0.08, r=0.09, center=true, $fn=6);
        color(c_dgrey()) translate([0, 0, z]) rotate([90, 0, 0]) cylinder(h=4.5 - z*0.08, r=0.09, center=true, $fn=6);
    }
}

// 蓝白同心方环广场（参考图中的螺旋纹铺装）
module sq_ring(S, w, z) {
    translate([0, 0, z])
    linear_extrude(0.03)
    difference() {
        square([S, S], center=true);
        square([S - 2*w, S - 2*w], center=true);
    }
}

module plaza_spiral(S=20, n=5) {
    color(c_mark()) translate([0, 0, curb_h()]) slab(S, S, 0.04);
    w = S/(2*n);
    for (i = [0 : n-2]) {
        color(i % 2 == 0 ? c_blue() : c_mark())
        sq_ring(S - 2*w*i, w, curb_h() + 0.05 + i*0.012);
    }
    color((n - 1) % 2 == 0 ? c_blue() : c_mark())
    translate([0, 0, curb_h() + 0.05 + (n - 1)*0.012])
    slab(S - 2*w*(n - 1), S - 2*w*(n - 1), 0.03);
}

// 篮球架：立柱在原点，篮板朝 +X
module hoop() {
    color(c_dgrey())  cylinder(h=3.0, r=0.10, $fn=8);
    color(c_mark())   translate([0.55, 0, 2.6]) boxc([0.06, 1.5, 1.0]);
    color(c_orange()) translate([0.95, 0, 2.25])
        linear_extrude(0.06)
        difference() {
            circle(r=0.42, $fn=12);
            circle(r=0.30, $fn=12);
        }
}

// 篮球场（含围网立柱）
module basketball_court(L=17, W=10) {
    color([0.30, 0.55, 0.45]) translate([0, 0, curb_h()]) slab(L, W, 0.05);

    color(c_mark()) translate([0, 0, curb_h() + 0.05])
    linear_extrude(0.02)
    difference() {
        square([L - 1.2, W - 1.2], center=true);
        square([L - 1.8, W - 1.8], center=true);
    }

    color(c_mark()) translate([0, 0, curb_h() + 0.05])
    linear_extrude(0.02)
    difference() {
        circle(r=1.8, $fn=20);
        circle(r=1.5, $fn=20);
    }

    translate([-(L/2 - 0.8), 0, curb_h()]) hoop();
    translate([ (L/2 - 0.8), 0, curb_h()]) rotate([0, 0, 180]) hoop();

    color(c_dgrey())
    for (x = [-L/2, -L/4, 0, L/4, L/2]) {
        translate([x, -W/2 - 0.8, curb_h()]) cylinder(h=2.8, r=0.08, $fn=6);
        translate([x,  W/2 + 0.8, curb_h()]) cylinder(h=2.8, r=0.08, $fn=6);
    }
    color(c_dgrey()) translate([0, -W/2 - 0.8, curb_h() + 2.75]) boxc([L + 0.2, 0.08, 0.08]);
    color(c_dgrey()) translate([0,  W/2 + 0.8, curb_h() + 2.75]) boxc([L + 0.2, 0.08, 0.08]);
}

// 公园水池 + 喷泉
module pond(r=6) {
    color(c_mark()) translate([0, 0, curb_h() + 0.02]) cylinder(h=0.12, r=r + 0.8, $fn=10);
    color([0.35, 0.62, 0.85]) translate([0, 0, curb_h() + 0.06]) cylinder(h=0.12, r=r, $fn=10);
    color(c_mark()) translate([0, 0, curb_h() + 0.10]) cylinder(h=0.5, r=0.5, $fn=8);
    color([0.35, 0.62, 0.85]) translate([0, 0, curb_h() + 0.60]) blob(0.42);
}

// 标准预览
translate([-14, 0, 0]) tree();
translate([-9, 0, 0]) street_lamp();
translate([-5, 0, 0]) traffic_light();
translate([-1, 0, 0]) hydrant();
translate([2, 0, 0]) mailbox();
translate([5, 0, 0]) bench();
translate([10, 0, 0]) bus_stop();
translate([18, 0, 0]) radio_mast(16);
translate([0, -18, 0]) plaza_spiral();
translate([0, 16, 0]) basketball_court();
translate([24, 14, 0]) pond(4);
