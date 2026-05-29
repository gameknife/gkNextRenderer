// ============================================================
// Ancient Chinese City - scene assembly
// ============================================================

$fn = 32;

use <terrain.scad>
use <roads.scad>
use <walls.scad>
use <buildings.scad>
use <decorations.scad>

module ancient_city() {
    city_ground();
    inner_roads();
    city_walls();
    buildings();
    decorations();
}

// Standalone preview.
ancient_city();
