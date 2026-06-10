// ============================================================
// Modern City Diorama - 场景总装
// ============================================================

$fn = 24;

use <terrain.scad>
use <roads.scad>
use <buildings.scad>
use <decorations.scad>
use <vehicles.scad>

module modern_city() {
    city_ground();
    road_network();
    city_buildings();
    city_props();
    city_vehicles();
}

// 标准预览
modern_city();
