// ============================================================
// Ancient Chinese City - buildings
// ============================================================

$fn = 32;

use <common.scad>
use <roof.scad>
use <props.scad>

module small_house() {
    L = 16;
    D = 10;
    H = 5.2;
    RH = 3.0;

    color(c_plaster())
    translate([0,0,H/2])
    boxc([L,D,H]);

    color(c_stone_lite())
    translate([0,0,0.35])
    boxc([L+0.3,D+0.3,0.7]);

    color(c_wood_dark())
    translate([0,-D/2-0.08,1.8])
    boxc([2.3,0.28,3.6]);

    for (x=[-L/3,L/3]) {
        color(c_wood_dark())
        translate([x,-D/2-0.06,3.0])
        boxc([1.45,0.18,1.15]);
    }

    translate([0,0,H])
    gable_roof(L,D,RH,1.15);
}

module courtyard_house(L=34, D=26) {
    color(c_stone_lite())
    difference() {
        translate([0,0,1.1])
        boxc([L,D,2.2]);

        translate([0,0,1.15])
        boxc([L-2.2,D-2.2,2.5]);
    }

    color(c_wood_dark())
    translate([0,-D/2-0.05,1.1])
    boxc([4.0,0.35,2.1]);

    translate([0,5.5,0])
    small_house();

    translate([-9.2,-3.5,0])
    rotate([0,0,90])
    small_house();

    translate([9.2,-3.5,0])
    rotate([0,0,90])
    small_house();

    translate([0,-1.2,0])
    tree(3.8,1.4);
}

module shop(L=14, D=8, H=4.0) {
    color([0.82,0.77,0.65])
    translate([0,0,H/2])
    boxc([L,D,H]);

    color(c_wood_dark())
    translate([0,-D/2-0.05,1.6])
    boxc([L*0.58,0.25,2.8]);

    translate([0,0,H])
    gable_roof(L,D,2.2,1.1);

    color([0.70,0.18,0.08])
    translate([0,-D/2-0.9,2.6])
    rotate([15,0,0])
    boxc([L*0.85,1.4,0.18]);
}

module railing_x(L=20, y=0, z=0) {
    color(c_wood_dark())
    translate([0,y,z])
    boxc([L,0.18,0.18]);

    color(c_wood_dark())
    translate([0,y,z+1.0])
    boxc([L,0.18,0.18]);

    for (x=[-L/2+1.0:1.6:L/2-1.0]) {
        color(c_wood_dark())
        translate([x,y,z+0.5])
        boxc([0.12,0.15,1.0]);
    }
}

module window_grid(x=0, y=0, z=0) {
    color(c_wood_dark())
    translate([x,y,z])
    boxc([2.2,0.12,1.4]);

    color(c_plaster())
    translate([x,y-0.01,z])
    boxc([1.8,0.08,1.0]);

    color(c_wood_dark())
    translate([x,y-0.04,z])
    boxc([0.12,0.12,1.3]);

    color(c_wood_dark())
    translate([x,y-0.04,z])
    boxc([2.0,0.12,0.12]);
}

module wine_shop() {
    stepped_base(34,22,0.9,0.5);

    color([0.76,0.55,0.34])
    translate([0,0,3.1])
    boxc([28,17,5.4]);

    for (x=[-11,-7,-3,3,7,11]) {
        color(c_wood_dark())
        translate([x,-8.2,3.0])
        cylinder(h=5.4, r=0.16, center=true);
    }

    color(c_wood_dark())
    translate([0,-8.3,5.7])
    boxc([27,0.32,0.35]);

    color(c_wood_dark())
    translate([0,-8.65,2.1])
    boxc([3.6,0.35,3.6]);

    for (x=[-8.5,8.5]) {
        window_grid(x,-8.72,3.1);
    }

    color(c_wood())
    translate([0,0,8.1])
    boxc([25,15,4.5]);

    color(c_wood_dark())
    translate([0,-9.4,6.1])
    boxc([26,2.4,0.35]);

    railing_x(25, -10.55, 6.35);

    for (x=[-10,-5,0,5,10]) {
        color(c_wood_dark())
        translate([x,-10.0,7.6])
        cylinder(h=3.0, r=0.13, center=true);
    }

    for (x=[-8,0,8]) {
        window_grid(x,-7.65,8.4);
    }

    translate([0,0,10.4])
    gable_roof(31,20,4.8,2.0);

    color(c_wood())
    translate([0,0,14.3])
    boxc([16,10,3.8]);

    for (x=[-5.5,0,5.5]) {
        window_grid(x,-5.05,14.5);
    }

    translate([0,0,16.2])
    gable_roof(20,13,3.6,1.5);

    color(c_wood_dark())
    translate([0,-10.9,6.8])
    boxc([9.2,0.45,2.4]);

    color([0.95,0.76,0.32])
    translate([0,-11.16,6.45])
    linear_extrude(0.08)
    text("酒楼", size=2.2, halign="center", valign="center");

    translate([14.2,-7.0,4.0])
    rotate([0,18,0])
    flag_pole(7.5);

    color(c_flag())
    translate([15.0,-7.0,8.6])
    boxc([0.18,0.05,2.8]);

    for (x=[-12,-6,6,12]) {
        translate([x,-9.3,5.4])
        color(c_lantern())
        scale([0.35,0.35,0.5])
        sphere(r=0.9);
    }

    for (i=[0:3]) {
        color(c_stone_lite())
        translate([0,-12.0-i*0.55,0.18+i*0.16])
        boxc([12+i*1.2,0.5,0.35+i*0.12]);
    }
}

module official_building() {
    stepped_base(36,24,1.0,0.6);

    color(c_plaster())
    translate([0,0,5])
    boxc([28,16,8]);

    for (x=[-10,-5,0,5,10]) {
        color(c_wood_dark())
        translate([x,-7.0,4.5])
        cylinder(h=7, r=0.17, center=true);
    }

    translate([0,0,9])
    gable_roof(34,21,4.8,2.0);
}

module market_stall() {
    color(c_wood())
    translate([0,0,1.05])
    boxc([5,3.2,2.1]);

    translate([0,0,2.15])
    gable_roof(5.4,3.6,1.25,0.5);
}

module buildings() {
    translate([-42,-22,0])
    wine_shop();

    translate([0,14,0])
    official_building();

    translate([-92,60,0]) courtyard_house();
    translate([-54,60,0]) courtyard_house();
    translate([ 54,60,0]) courtyard_house();
    translate([ 92,60,0]) courtyard_house();

    translate([-94,-62,0]) courtyard_house();
    translate([ 94,-62,0]) courtyard_house();

    for (x=[-106,-82,-58,-34]) {
        translate([x,30,0]) small_house();
        translate([x,-40,0]) small_house();
    }

    for (x=[34,58,82,106]) {
        translate([x,30,0]) small_house();
        translate([x,-40,0]) small_house();
    }

    for (x=[-104,-84,-64]) {
        translate([x,-8,0]) shop();
        translate([x,12,0]) shop();
    }

    for (x=[64,84,104]) {
        translate([x,-8,0]) shop();
        translate([x,12,0]) shop();
    }

    for (x=[28,40,52], y=[-24,-14]) {
        translate([x,y,0])
        market_stall();
    }
}

// Standalone preview.
buildings();
