// ============================================================
// Modern City Diorama - 展台底座 / 街区台面 / 草地
// ============================================================

$fn = 24;

use <common.scad>

// 展台底座 + 整体铺装层
module display_base() {
    color(c_base())
    translate([0, 0, -base_h()])
    slab(tile_w() + 6, tile_d() + 6, base_h());

    color(c_pave())
    slab(tile_w(), tile_d(), 0.06);
}

// 单个街区台面（人行道，带路缘高度）
module block_plate(W, D) {
    color(c_pave()) slab(W, D, curb_h());
}

module grass_patch(W, D) {
    color(c_grass())
    translate([0, 0, curb_h()])
    slab(W, D, 0.08);
}

// 12 个街区：列 x = -95 / -35 / 35 / 95，行 y = -65 / -5 / 60
module city_blocks() {
    // 第 1 行 (y=-65, 深 40)
    translate([-95, -65, 0]) { block_plate(40, 40); grass_patch(34, 34); }  // A1 通讯塔草坪
    translate([-35, -65, 0]) { block_plate(60, 40); grass_patch(54, 34); }  // B1 住宅
    translate([ 35, -65, 0]) block_plate(60, 40);                           // C1 球场
    translate([ 95, -65, 0]) block_plate(40, 40);                           // D1 旅馆

    // 第 2 行 (y=-5, 深 60)
    translate([-95, -5, 0]) block_plate(40, 60);                            // A2 写字楼
    translate([-35, -5, 0]) block_plate(60, 60);                            // B2 快餐+广场
    translate([ 35, -5, 0]) block_plate(60, 60);                            // C2 警局+咖啡+酒店
    translate([ 95, -5, 0]) block_plate(40, 60);                            // D2 商铺街

    // 第 3 行 (y=60, 深 50)
    translate([-95, 60, 0]) { block_plate(40, 50); grass_patch(34, 44); }   // A3 公园
    translate([-35, 60, 0]) block_plate(60, 50);                            // B3 医院
    translate([ 35, 60, 0]) block_plate(60, 50);                            // C3 商业
    translate([ 95, 60, 0]) { block_plate(40, 50); grass_patch(34, 44); }   // D3 公寓
}

module city_ground() {
    display_base();
    city_blocks();
}

// 标准预览
city_ground();
