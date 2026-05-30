// Beer Mug / 啤酒杯
// OpenSCAD script

$fn = 96;

// ---------- 参数 ----------
cup_height = 90;
cup_outer_radius = 28;
cup_inner_radius = 24;
bottom_thickness = 8;

beer_level = 62;
foam_height = 12;

handle_radius = 18;
handle_thickness = 6;

// ---------- 主模型 ----------
beer_mug();

module beer_mug() {
    // 玻璃杯身
    color([0.85, 0.95, 1.0, 0.28])
    difference() {
        union() {
            // 外杯壁
            cylinder(h = cup_height, r1 = cup_outer_radius, r2 = cup_outer_radius * 0.95);

            // 厚杯底
            cylinder(h = bottom_thickness, r = cup_outer_radius * 1.02);
        }

        // 挖空内部
        translate([0, 0, bottom_thickness])
        cylinder(
            h = cup_height + 1,
            r1 = cup_inner_radius,
            r2 = cup_inner_radius * 0.95
        );
    }

    // 杯口厚边
    color([0.9, 0.98, 1.0, 0.35])
    translate([0, 0, cup_height])
    difference() {
        cylinder(h = 3, r = cup_outer_radius);
        translate([0, 0, -0.5])
        cylinder(h = 4, r = cup_inner_radius);
    }

    // 啤酒液体
    color([1.0, 0.62, 0.08, 0.65])
    translate([0, 0, bottom_thickness + 1])
    cylinder(
        h = beer_level,
        r1 = cup_inner_radius - 1,
        r2 = cup_inner_radius * 0.93
    );

    // 泡沫主体
    color([1.0, 0.96, 0.78, 0.95])
    translate([0, 0, bottom_thickness + beer_level])
    cylinder(h = foam_height, r = cup_inner_radius * 0.93);

    // 泡沫顶部不规则气泡
    foam_bubbles();

    // 杯把
    handle();
}

// ---------- 杯把 ----------
module handle() {
    color([0.85, 0.95, 1.0, 0.35])
    translate([cup_outer_radius + 2, 0, cup_height * 0.48])
    rotate([90, 0, 0])
    difference() {
        torus_like(handle_radius, handle_thickness);

        // 切掉靠近杯身的一部分，让把手像 C 形
        translate([-handle_radius - 6, -20, -20])
        cube([handle_radius + 8, 40, 40]);
    }

    // 上连接块
    color([0.85, 0.95, 1.0, 0.35])
    translate([cup_outer_radius - 1, -handle_thickness / 2, cup_height * 0.62])
    cube([10, handle_thickness, 9]);

    // 下连接块
    color([0.85, 0.95, 1.0, 0.35])
    translate([cup_outer_radius - 1, -handle_thickness / 2, cup_height * 0.22])
    cube([10, handle_thickness, 9]);
}

// 用 rotate_extrude 做类似圆环的把手截面
module torus_like(r, tube_r) {
    rotate_extrude(angle = 360)
    translate([r, 0, 0])
    circle(r = tube_r);
}

// ---------- 泡沫细节 ----------
module foam_bubbles() {
    bubble_z = bottom_thickness + beer_level + foam_height;

    color([1.0, 0.97, 0.82, 1.0]) {
        translate([-12, 6, bubble_z])
        sphere(r = 6);

        translate([0, -8, bubble_z + 2])
        sphere(r = 8);

        translate([11, 5, bubble_z + 1])
        sphere(r = 5);

        translate([6, 15, bubble_z])
        sphere(r = 4);

        translate([-16, -10, bubble_z - 1])
        sphere(r = 5);
    }

    // 杯内泡泡
    color([1.0, 0.95, 0.65, 0.75]) {
        translate([-8, -5, 42]) sphere(r = 1.8);
        translate([7, 9, 50]) sphere(r = 1.4);
        translate([11, -7, 36]) sphere(r = 1.2);
        translate([-12, 10, 58]) sphere(r = 1.6);
        translate([3, 3, 28]) sphere(r = 1.1);
    }
}