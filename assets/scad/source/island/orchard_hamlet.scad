// Island variant: orchard hamlet centered on fruit growing and a produce dock.
use <../../lib/kit_island.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;
ZGRASS = 0.26;

is_ground_water_blob(126, 104, 0.10, is_SEADEEP(), 801, 12, is_WATER_ROUGH_DEEP());
translate([0, 0, 0.04]) is_ground_water_blob(92, 72, 0.12, is_SEASHAL(), 802, 11, is_WATER_ROUGH_SHALLOW());
translate([0, 1, 0.08]) is_ground_blob(82, 64, 0.12, is_SANDC(), 803, 12);
translate([0, 4, 0.12]) is_ground_blob(72, 54, 0.14, is_GRASSC(), 804, 12);

// Hamlet homes frame a central packing green.
translate([0, 16, ZGRASS]) is_ground_plaza(18, 13);
translate([0, 25, ZGRASS]) is_bldg_hall(seed = 805, L = 12, D = 8);
translate([-22, 7, ZGRASS]) rotate([0, 0, 90]) is_bldg_house(seed = 806);
translate([22, 8, ZGRASS]) rotate([0, 0, -90]) is_bldg_house(seed = 807);
translate([0, -10, ZGRASS]) is_bldg_shop(seed = 808, L = 9, D = 7);
translate([-7, 15, 0.40]) is_prop_fountain(0.8);
translate([7, 15, 0.40]) is_prop_flagpole(6.5);

// Three distinct orchard blocks and a harvest staging lane.
translate([-24, 20, ZGRASS]) lay_grid(3, 3, 5, 5, 810)
    lay_jitter($seed, 0.8, 0.8, 15) is_nature_apple(s = 1.0, seed = $seed);
translate([23, 22, ZGRASS]) lay_grid(3, 3, 5, 5, 820)
    lay_jitter($seed, 0.8, 0.8, 15) is_nature_orange(s = 1.0, seed = $seed);
translate([-21, -13, ZGRASS]) lay_grid(3, 2, 5, 5, 830)
    lay_jitter($seed, 0.8, 0.8, 15) is_nature_peach(s = 1.05, seed = $seed);
for (x = [-14 : 5 : 14]) translate([x, 4, ZGRASS]) is_prop_crate(seed = x);
translate([15, 1, ZGRASS]) is_prop_wateringcan();
translate([-15, 1, ZGRASS]) is_prop_sign(seed = 831);

// Southern produce dock and outbound boats.
translate([10, -25, 0.36]) rotate([0, 0, -90]) is_bldg_dock(15, 3.2);
translate([10, -39, 0.12]) rotate([0, 0, 10]) is_veh_boat(seed = 832);
translate([15, -27, 0.36]) is_prop_crate(seed = 833);
translate([19, -25, 0.20]) is_item_fruit(kind = 1, seed = 834);
for (x = [-31, 29]) translate([x, -22, 0.20]) is_nature_coconut(s = 1.05, seed = x);

gk_camera_lookat([45, -50, 30], [0, 4, 0], "orchard-hamlet-overview", 48);
gk_camera_lookat([-5, 2, 4], [-20, 19, 2], "orchard-lane", 54);
gk_camera_lookat([20, -42, 5], [10, -25, 1], "produce-dock", 54);
