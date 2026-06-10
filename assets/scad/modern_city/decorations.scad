// ============================================================
// Modern City Diorama - 场景小品摆放（树木/路灯/红绿灯等）
// ============================================================

$fn = 24;

use <common.scad>
use <props.scad>

module city_props() {
    zb = curb_h();

    // —— A1 通讯塔草坪 ——
    translate([-103, -73, zb]) radio_mast(26);
    translate([ -90, -60, zb]) radio_mast(18);
    color(c_grey())  translate([-97, -66, zb + 1.0]) boxc([4.5, 3.2, 2.0]);
    color(c_dgrey()) translate([-97, -66, zb + 2.1]) boxc([4.9, 3.6, 0.2]);

    // —— A3 公园 ——
    for (p = [
        [-108, 75], [-99, 78], [-88, 76], [-82, 68],
        [-84, 52], [-95, 44], [-106, 48], [-107, 62], [-88, 62]
    ]) translate([p[0], p[1], zb]) tree();

    translate([-103, 55, zb]) bush(1.1);
    translate([ -86, 45, zb]) bush(0.9);
    translate([-101, 68, zb]) bench();
    translate([ -89, 68, zb]) bench();

    // —— 各街区绿化树 ——
    for (p = [
        [-62, 38], [-8, 38], [-62, 82], [-8, 82],          // B3 医院四角
        [8, 42], [62, 42], [8, 80], [56, 80],              // C3
        [86, 64], [104, 60],                               // D3
        [-110, 12], [-80, 16],                             // A2
        [-58, -2], [-63, 22], [-12, 16], [-10, -18],       // B2
        [8, 2], [34, 20], [60, -8], [10, -28],             // C2
        [108, 16], [108, -12],                             // D2
        [-62, -50], [-44, -78], [-26, -78], [-8, -52],     // B1 庭院
        [12, -50], [44, -78], [58, -52],                   // C1
        [78, -50], [110, -50],                             // D1
        [-86, -80], [-108, -52]                            // A1
    ]) translate([p[0], p[1], zb]) tree(2.3, 2.0);

    // —— 路灯（沿横向道路两侧交错）——
    for (x = [-95, -45, 25, 95]) translate([x, -46.5, zb]) rotate([0, 0,  90]) street_lamp();
    for (x = [-85, -15, 55, 105]) translate([x, -33.5, zb]) rotate([0, 0, -90]) street_lamp();
    for (x = [-95, -45, 25, 95]) translate([x,  23.5, zb]) rotate([0, 0,  90]) street_lamp();
    for (x = [-85, -15, 55, 105]) translate([x,  36.5, zb]) rotate([0, 0, -90]) street_lamp();

    // —— 路灯（沿中央纵向道路）——
    for (y = [-70, -12, 52]) translate([-6.5, y, zb]) street_lamp();
    for (y = [-58, 8, 72])  translate([ 6.5, y, zb]) rotate([0, 0, 180]) street_lamp();

    // —— 红绿灯（中央两个路口四角）——
    for (cy = [-40, 30]) {
        translate([-6.5, cy - 6.5, zb]) rotate([0, 0,  90]) traffic_light();
        translate([ 6.5, cy - 6.5, zb]) rotate([0, 0, 180]) traffic_light();
        translate([ 6.5, cy + 6.5, zb]) rotate([0, 0, -90]) traffic_light();
        translate([-6.5, cy + 6.5, zb]) traffic_light();
    }

    // —— 公交候车亭 ——
    translate([ 40, -32.6, zb]) bus_stop();
    translate([-60,  36.8, zb]) bus_stop();

    // —— 消防栓 / 邮筒 / 长椅 ——
    translate([-9, -49, zb]) hydrant();
    translate([ 9,  40, zb]) hydrant();
    translate([60,  23, zb]) hydrant();
    translate([80,   4, zb]) mailbox();
    translate([-47, -13, zb]) bench();
    translate([-41, -13, zb]) bench();
}

// 标准预览
city_props();
