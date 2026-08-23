// Island variant: harvest festival plaza, farm contests and beach celebration.
use <../../lib/kit_island.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;
ZGRASS = 0.26;

is_ground_water_blob(126, 106, 0.10, is_SEADEEP(), 861, 12, is_WATER_ROUGH_DEEP());
translate([0, 1, 0.04]) is_ground_water_blob(94, 76, 0.12, is_SEASHAL(), 862, 11, is_WATER_ROUGH_SHALLOW());
translate([0, 3, 0.08]) is_ground_blob(82, 64, 0.12, is_SANDC(), 863, 12);
translate([0, 7, 0.12]) is_ground_blob(72, 54, 0.14, is_GRASSC(), 864, 12);

// Festival square with market stalls represented by shop fronts and decorated lanes.
translate([0, 12, ZGRASS]) is_ground_plaza(30, 19);
translate([0, 25, ZGRASS]) is_bldg_hall(seed = 865, L = 13, D = 9);
translate([-18, 17, ZGRASS]) rotate([0, 0, 90]) is_bldg_shop(seed = 866, L = 8, D = 6);
translate([18, 17, ZGRASS]) rotate([0, 0, -90]) is_bldg_shop(seed = 867, L = 8, D = 6);
translate([0, 7, 0.40]) is_prop_fountain(0.9);
for (x = [-12, -6, 6, 12]) translate([x, 12, 0.40]) is_prop_umbrella();
for (x = [-13 : 4 : 13]) translate([x, 2, ZGRASS]) is_prop_torch();
for (p = [[-13, 21], [13, 21], [-13, 4], [13, 4]]) translate([p[0], p[1], 0.40]) is_nature_flowerbed(seed = p[0]);

// Harvest competition: crop plots, scarecrows, fruit displays and picnic seating.
translate([-24, -4, ZGRASS]) is_ground_field(15, 10, 870, 1);
translate([24, -4, ZGRASS]) is_ground_field(15, 10, 871, 2);
translate([-29, -1, ZGRASS]) is_prop_scarecrow(seed = 872);
translate([29, -1, ZGRASS]) is_prop_scarecrow(seed = 873);
for (x = [-12 : 4 : 12])
{
    translate([x, -8, ZGRASS]) is_prop_crate(seed = 874 + x);
    translate([x, -10, ZGRASS]) is_item_fruit(kind = (x + 12) / 4 % 4, seed = x);
}
for (x = [-10, -3, 4, 11]) translate([x, -16, ZGRASS]) rotate([0, 0, 180]) is_prop_bench();

// Evening bonfire and ferry arrival on the south shore.
translate([-11, -29, 0.20]) is_prop_firepit();
for (a = [0 : 45 : 315]) translate([-11 + 6 * cos(a), -29 + 6 * sin(a), 0.20]) rotate([0, 0, a + 90]) is_prop_lounger();
translate([13, -24, 0.34]) rotate([0, 0, -90]) is_bldg_dock(14, 3.2);
translate([13, -39, 0.12]) rotate([0, 0, 12]) is_veh_boat(seed = 875);
for (x = [-32, 32]) translate([x, -23, 0.20]) is_nature_coconut(s = 1.1, seed = x);

gk_camera_lookat([46, -52, 31], [0, 5, 0], "harvest-festival-overview", 48);
gk_camera_lookat([0, -1, 4], [0, 13, 1], "festival-plaza", 55);
gk_camera_lookat([-23, -40, 5], [-11, -29, 1], "bonfire-circle", 55);
