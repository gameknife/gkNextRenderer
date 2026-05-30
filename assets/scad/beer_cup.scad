// Beer Mug / 啤酒杯
// OpenSCAD script
// 单位：1 unit = 1 meter；尺寸按毫米书写后转换为米

$fn = 96;

// ---------- 参数 ----------
mm = 0.001;

cup_height = 90 * mm;
cup_outer_radius = 28 * mm;
cup_inner_radius = 24 * mm;
bottom_thickness = 8 * mm;

beer_level = 62 * mm;
foam_height = 12 * mm;

handle_radius = 18 * mm;
handle_thickness = 6 * mm;

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
            h = cup_height + 1 * mm,
            r1 = cup_inner_radius,
            r2 = cup_inner_radius * 0.95
        );
    }

    // 杯口厚边
    color([0.9, 0.98, 1.0, 0.35])
    translate([0, 0, cup_height])
    difference() {
        cylinder(h = 3 * mm, r = cup_outer_radius);
        translate([0, 0, -0.5 * mm])
        cylinder(h = 4 * mm, r = cup_inner_radius);
    }

    // 啤酒液体
    color([1.0, 0.62, 0.08, 0.65])
    translate([0, 0, bottom_thickness + 1 * mm])
    cylinder(
        h = beer_level,
        r1 = cup_inner_radius - 1 * mm,
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
    translate([cup_outer_radius + 2 * mm, 0, cup_height * 0.48])
    rotate([90, 0, 0])
    difference() {
        torus_like(handle_radius, handle_thickness);

        // 切掉靠近杯身的一部分，让把手像 C 形
        translate([-handle_radius - 6 * mm, -20 * mm, -20 * mm])
        cube([handle_radius + 8 * mm, 40 * mm, 40 * mm]);
    }

    // 上连接块
    color([0.85, 0.95, 1.0, 0.35])
    translate([cup_outer_radius - 1 * mm, -handle_thickness / 2, cup_height * 0.62])
    cube([10 * mm, handle_thickness, 9 * mm]);

    // 下连接块
    color([0.85, 0.95, 1.0, 0.35])
    translate([cup_outer_radius - 1 * mm, -handle_thickness / 2, cup_height * 0.22])
    cube([10 * mm, handle_thickness, 9 * mm]);
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
        translate([-12 * mm, 6 * mm, bubble_z])
        sphere(r = 6 * mm);

        translate([0, -8 * mm, bubble_z + 2 * mm])
        sphere(r = 8 * mm);

        translate([11 * mm, 5 * mm, bubble_z + 1 * mm])
        sphere(r = 5 * mm);

        translate([6 * mm, 15 * mm, bubble_z])
        sphere(r = 4 * mm);

        translate([-16 * mm, -10 * mm, bubble_z - 1 * mm])
        sphere(r = 5 * mm);
    }

    // 杯内泡泡
    color([1.0, 0.95, 0.65, 0.75]) {
        translate([-8 * mm, -5 * mm, 42 * mm]) sphere(r = 1.8 * mm);
        translate([7 * mm, 9 * mm, 50 * mm]) sphere(r = 1.4 * mm);
        translate([11 * mm, -7 * mm, 36 * mm]) sphere(r = 1.2 * mm);
        translate([-12 * mm, 10 * mm, 58 * mm]) sphere(r = 1.6 * mm);
        translate([3 * mm, 3 * mm, 28 * mm]) sphere(r = 1.1 * mm);
    }
}
