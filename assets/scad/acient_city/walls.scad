// ============================================================
// Ancient Chinese City - walls, towers and gatehouse
// ============================================================

$fn = 32;

use <common.scad>
use <roof.scad>
use <props.scad>

module battlements_x(L, y_side, count) {
    step = L / count;

    for (i=[0:count-1]) {
        translate([
            -L/2 + i*step + step*0.28,
            y_side,
            wall_h() + 1.2 + parapet_h()/2
        ])
        color(c_stone_lite())
        boxc([step*0.48, 1.4, parapet_h()]);
    }
}

module battlements_y(L, x_side, count) {
    step = L / count;

    for (i=[0:count-1]) {
        translate([
            x_side,
            -L/2 + i*step + step*0.28,
            wall_h() + 1.2 + parapet_h()/2
        ])
        color(c_stone_lite())
        boxc([1.4, step*0.48, parapet_h()]);
    }
}

module wall_segment_x(L) {
    color(c_stone())
    translate([0,0,wall_h()/2])
    boxc([L, wall_t(), wall_h()]);

    color(c_walkway())
    translate([0,0,wall_h()+0.22])
    boxc([L-1.0, wall_t()-3.2, 0.44]);

    for (y=[-(wall_t()/2-1.0), wall_t()/2-1.0]) {
        color(c_stone_lite())
        translate([0,y,wall_h()+0.8])
        boxc([L, 1.2, 1.6]);

        battlements_x(L, y, max(6, floor(L/7)));
    }

    for (x=[-L/2+8:16:L/2-8]) {
        color(c_stone_dark())
        translate([x, -wall_t()/2-0.02, wall_h()*0.48])
        boxc([0.35,0.12,wall_h()*0.65]);
    }
}

module wall_segment_y(L) {
    color(c_stone())
    translate([0,0,wall_h()/2])
    boxc([wall_t(), L, wall_h()]);

    color(c_walkway())
    translate([0,0,wall_h()+0.22])
    boxc([wall_t()-3.2, L-1.0, 0.44]);

    for (x=[-(wall_t()/2-1.0), wall_t()/2-1.0]) {
        color(c_stone_lite())
        translate([x,0,wall_h()+0.8])
        boxc([1.2, L, 1.6]);

        battlements_y(L, x, max(6, floor(L/7)));
    }

    for (y=[-L/2+8:16:L/2-8]) {
        color(c_stone_dark())
        translate([-wall_t()/2-0.02, y, wall_h()*0.48])
        boxc([0.12,0.35,wall_h()*0.65]);
    }
}

module wall_stairs_y(total=18, W=5.5, H=wall_h(), steps=12, dir=1) {
    d = total / steps;
    for (i=[0:steps-1]) {
        translate([
            0,
            dir * (-total/2 + i*d + d/2),
            ((i+1) * H/steps) / 2
        ])
        color(c_stone_lite())
        boxc([W, d, (i+1)*H/steps]);
    }
}

module corner_tower() {
    stepped_base(22,22,1.2,0.8);

    color(c_stone_dark())
    translate([0,0,8])
    boxc([18,18,16]);

    color(c_walkway())
    translate([0,0,16.3])
    boxc([16,16,0.45]);

    color(c_wood())
    translate([0,0,21])
    boxc([13,13,8]);

    for (x=[-5,5], y=[-5,5]) {
        color(c_wood_dark())
        translate([x,y,21])
        cylinder(h=8, r=0.22, center=true);
    }

    translate([0,0,25])
    gable_roof(17,17,4.5,1.8);

    translate([-4,0,16.7]) soldier(0.9);
    translate([ 4,0,16.7]) soldier(0.9);
}

module city_gatehouse() {
    stepped_base(48,28,1.5,0.9);

    color(c_stone_dark())
    translate([0,0,9])
    difference() {
        boxc([44,24,18]);

        translate([0,0,2.5])
        boxc([15,30,12]);

        translate([0,0,8])
        rotate([90,0,0])
        cylinder(h=32, r=7.5, center=true);
    }

    // 双开城门贴在门洞前侧，比例接近下层门洞宽高。
    door_y = -12.35;
    color(c_wood_dark())
    translate([-3.35,door_y,5.4])
    boxc([6.35,0.55,10.8]);

    color(c_wood_dark())
    translate([3.35,door_y,5.4])
    boxc([6.35,0.55,10.8]);

    color(c_wood())
    translate([0,door_y-0.04,5.4])
    boxc([0.32,0.18,10.9]);

    for (x=[-5.6,-3.6,-1.6,1.6,3.6,5.6]) {
        color(c_wood())
        translate([x,door_y-0.04,5.4])
        boxc([0.18,0.18,10.4]);
    }

    color(c_wood())
    translate([0,0,22])
    boxc([34,16,8]);

    for (x=[-13,-8,-3,3,8,13]) {
        color(c_wood_dark())
        translate([x,-6.8,22])
        cylinder(h=8.4, r=0.20, center=true);
    }

    translate([0,0,26])
    gable_roof(40,22,5.8,2.2);

    color(c_wood())
    translate([0,0,31])
    boxc([23,12,5]);

    translate([0,0,33.5])
    gable_roof(28,16,4.5,1.8);

    translate([-15,0,31]) flag_pole(8);
    translate([ 15,0,31]) flag_pole(8);

    translate([-9,-4,18.6]) soldier(0.9);
    translate([ 9,-4,18.6]) soldier(0.9);
}

module city_walls() {
    north_y =  city_h()/2 - wall_t()/2;
    south_y = -city_h()/2 + wall_t()/2;
    east_x  =  city_w()/2 - wall_t()/2;
    west_x  = -city_w()/2 + wall_t()/2;

    translate([0,north_y,0])
    wall_segment_x(city_w());

    side_len = (city_w() - gate_w()) / 2;

    translate([-(gate_w()/2 + side_len/2), south_y, 0])
    wall_segment_x(side_len);

    translate([ (gate_w()/2 + side_len/2), south_y, 0])
    wall_segment_x(side_len);

    translate([west_x,0,0])
    wall_segment_y(city_h());

    translate([east_x,0,0])
    wall_segment_y(city_h());

    translate([west_x,south_y,0]) corner_tower();
    translate([east_x,south_y,0]) corner_tower();
    translate([west_x,north_y,0]) corner_tower();
    translate([east_x,north_y,0]) corner_tower();

    translate([0,south_y,0])
    city_gatehouse();

    translate([-34,south_y+18,0])
    wall_stairs_y(20,5.5,wall_h(),13,1);

    translate([34,south_y+18,0])
    wall_stairs_y(20,5.5,wall_h(),13,1);

    for (x=[-92,-58,-24,28,64,98]) {
        translate([x,north_y,wall_h()+0.55])
        soldier(0.95);
    }

    for (x=[-88,-46,48,88]) {
        translate([x,south_y,wall_h()+0.55])
        soldier(0.95);
    }

    for (y=[-58,-22,28,62]) {
        translate([west_x,y,wall_h()+0.55])
        soldier(0.95);

        translate([east_x,y,wall_h()+0.55])
        soldier(0.95);
    }
}

// Standalone preview.
city_walls();
