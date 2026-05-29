// ============================================================
// Ancient Chinese City - roads
// ============================================================

$fn = 32;

use <common.scad>

module road_x(L,W) {
    color(c_road())
    translate([0,0,0.03])
    boxc([L,W,0.06]);
}

module road_y(L,W) {
    color(c_road())
    translate([0,0,0.03])
    boxc([W,L,0.06]);
}

module inner_roads() {
    road_y(city_h()-30, main_road_w());
    road_x(city_w()-30, main_road_w());

    translate([0, 48, 0]) road_x(city_w()-48, side_road_w());
    translate([0,-44, 0]) road_x(city_w()-48, side_road_w());

    translate([-68,0,0]) road_y(city_h()-50, side_road_w());
    translate([ 68,0,0]) road_y(city_h()-50, side_road_w());

    for (x=[-96,-40,40,96]) {
        translate([x,35,0]) road_y(36,3.2);
        translate([x,-32,0]) road_y(32,3.2);
    }

    color([0.66,0.59,0.47])
    translate([0,0,0.06])
    boxc([32,26,0.1]);
}

// Standalone preview.
inner_roads();
