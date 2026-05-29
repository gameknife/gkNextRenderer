// ============================================================
// Ancient Chinese City - people and props
// ============================================================

$fn = 32;

use <common.scad>

module soldier(s=1) {
    color([0.20,0.25,0.16])
    translate([0,0,0.85*s])
    cylinder(h=1.7*s, r=0.27*s, center=true);

    color([0.82,0.65,0.48])
    translate([0,0,1.85*s])
    sphere(r=0.22*s);

    color([0.12,0.12,0.10])
    translate([0,0,2.05*s])
    cylinder(h=0.22*s, r=0.26*s, center=true);

    color(c_wood_dark())
    translate([0.45*s,0,1.55*s])
    cylinder(h=2.6*s, r=0.035*s, center=true);

    color(c_stone_lite())
    translate([0.45*s,0,2.9*s])
    cylinder(h=0.35*s, r1=0.09*s, r2=0.0, center=true);
}

module lantern_post(h=4.6) {
    color(c_wood_dark())
    cylinder(h=h, r=0.12);

    color(c_wood_dark())
    translate([0,0,h-0.5])
    rotate([90,0,0])
    cylinder(h=1.6, r=0.06, center=true);

    for (x=[-0.75,0.75]) {
        translate([x,0,h-0.9])
        color(c_lantern())
        scale([0.38,0.38,0.55])
        sphere(r=0.65);
    }
}

module tree(h=5.5, crown=2.2) {
    color([0.28,0.16,0.07])
    cylinder(h=h, r1=0.28, r2=0.20);

    translate([0,0,h])
    color(c_green())
    union() {
        sphere(r=crown);
        translate([0.8,0.4,-0.2]) sphere(r=crown*0.65);
        translate([-0.7,-0.5,0]) sphere(r=crown*0.60);
    }
}

module well() {
    color(c_stone_dark())
    difference() {
        cylinder(h=1.1, r=1.45);
        translate([0,0,-0.1])
        cylinder(h=1.4, r=0.9);
    }

    color(c_wood_dark())
    translate([0,0,1.1]) {
        translate([-1.1,0,1.1]) cylinder(h=2.2, r=0.08);
        translate([ 1.1,0,1.1]) cylinder(h=2.2, r=0.08);
        translate([0,0,2.1]) rotate([90,0,0]) cylinder(h=2.4, r=0.06, center=true);
    }
}

module flag_pole(h=8) {
    color(c_wood_dark())
    cylinder(h=h, r=0.11);

    color(c_flag())
    translate([0,0,h-2.0])
    linear_extrude(0.08)
    polygon(points=[[0,0],[2.4,0.65],[0,1.3]]);
}

// Standalone preview.
translate([-8,0,0]) soldier();
translate([-3,0,0]) lantern_post();
translate([3,0,0]) tree();
translate([8,0,0]) well();
translate([13,0,0]) flag_pole();
