// ============================================================
// Ancient Chinese City - terrain, moat and bridge
// ============================================================

$fn = 32;

use <common.scad>
use <roads.scad>

module moat() {
    color(c_water())
    difference() {
        translate([0,0,0.015])
        boxc([city_w()+42, city_h()+42, 0.06]);

        translate([0,0,0.015])
        boxc([city_w()+22, city_h()+22, 0.08]);
    }
}

module bridge_to_gate() {
    color(c_stone_lite())
    translate([0,-city_h()/2-10,0.35])
    boxc([20,26,0.7]);

    for (x=[-8,8]) {
        color(c_stone_dark())
        translate([x,-city_h()/2-10,1.1])
        boxc([0.5,26,1.4]);
    }
}

module city_ground() {
    color(c_ground())
    translate([0,0,-0.22])
    boxc([city_w()+70, city_h()+70, 0.44]);

    moat();
    bridge_to_gate();

    translate([0,-city_h()/2-45,0])
    road_y(70,16);
}

// Standalone preview.
city_ground();
