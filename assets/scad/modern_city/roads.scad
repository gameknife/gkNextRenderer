// ============================================================
// Modern City Diorama - 道路网络 / 标线 / 斑马线 / 停车场
// ============================================================

$fn = 24;

use <common.scad>

// 沿 X 的路面（不带标线）
module road_x(L, W=10) {
    color(c_road()) slab(L, W, road_t());
}

// 沿 Y 的路面
module road_y(L, W=10) {
    color(c_road()) slab(W, L, road_t());
}

// 沿 X 的虚线中线
module dash_x(L) {
    color(c_mark())
    for (x = [-L/2 + 2.4 : 6 : L/2 - 2.0])
        translate([x, 0, road_t() + 0.012])
        boxc([2.8, 0.32, 0.025]);
}

module dash_y(L) { rotate([0, 0, 90]) dash_x(L); }

// 斑马线：横跨一条沿 Y 的道路（行人沿 X 方向通过）
module crosswalk_x(W=10) {
    color(c_mark())
    for (x = [-W/2 + 1.3 : 1.7 : W/2 - 1.1])
        translate([x, 0, road_t() + 0.018])
        boxc([0.95, 2.6, 0.025]);
}

// 斑马线：横跨一条沿 X 的道路
module crosswalk_y(W=10) { rotate([0, 0, 90]) crosswalk_x(W); }

// 地面停车场（置于街区台面上，含分隔线）
module parking_lot(W=20, D=11, bays=4) {
    color(c_lot())
    translate([0, 0, curb_h() - 0.04])
    slab(W, D, 0.06);

    color(c_mark())
    for (i = [0 : bays])
        translate([-W/2 + i*(W/bays), -D*0.2, curb_h() + 0.03])
        boxc([0.28, D*0.6, 0.025]);
}

// 道路网络：路面整条铺设，虚线按路口分段，路口处布置斑马线
module road_network() {
    // 路面
    for (x = vroads()) translate([x, 0, 0]) road_y(tile_d(), road_w());
    for (y = hroads()) translate([0, y, 0]) road_x(tile_w(), road_w());

    // 纵向虚线分段（行间）
    for (x = vroads()) {
        translate([x, -65, 0]) dash_y(32);
        translate([x,  -5, 0]) dash_y(52);
        translate([x,  60, 0]) dash_y(42);
    }

    // 横向虚线分段（列间）
    for (y = hroads()) {
        translate([-95, y, 0]) dash_x(32);
        translate([-35, y, 0]) dash_x(52);
        translate([ 35, y, 0]) dash_x(52);
        translate([ 95, y, 0]) dash_x(32);
    }

    // 六个路口的四向斑马线
    for (x = vroads())
        for (y = hroads()) {
            translate([x, y - 6.8, 0]) crosswalk_x(road_w());
            translate([x, y + 6.8, 0]) crosswalk_x(road_w());
            translate([x - 6.8, y, 0]) crosswalk_y(road_w());
            translate([x + 6.8, y, 0]) crosswalk_y(road_w());
        }
}

// 标准预览
road_network();
