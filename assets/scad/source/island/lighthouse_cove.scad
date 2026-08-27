// Island variant: rocky lighthouse cove with a tiny keeper settlement.
use <../../lib/kit_island.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;
ZGRASS = 0.26;

is_ground_water_blob(132, 112, 0.10, is_SEADEEP(), 841, 13, is_WATER_ROUGH_DEEP());
translate([0, 3, 0.04]) is_ground_water_blob(96, 78, 0.12, is_SEASHAL(), 842, 11, is_WATER_ROUGH_SHALLOW());
translate([0, 6, 0.08]) is_ground_blob(80, 62, 0.12, is_SANDC(), 843, 12);
translate([0, 10, 0.12]) is_ground_blob(68, 50, 0.14, [0.32, 0.43, 0.24], 844, 11);

// Lighthouse dominates the eastern headland; rocks expose the dangerous cove.
translate([25, 25, ZGRASS]) is_bldg_lighthouse(s = 1.18, seed = 845);
for (p = [[18, 31, 1.4], [31, 18, 1.2], [36, 7, 1.0], [28, -4, 0.9], [-33, -18, 1.1]])
    translate([p[0], p[1], p[0] < -30 ? 0.20 : ZGRASS]) is_nature_rock(s = p[2], seed = p[0] + p[1]);
translate([18, 20, ZGRASS]) is_prop_sign(seed = 846);
translate([31, 26, ZGRASS]) is_prop_flagpole(7);

// Keeper cottages and a sheltered supply garden.
translate([-13, 18, ZGRASS]) rotate([0, 0, 25]) is_bldg_house(seed = 847);
translate([-22, 3, ZGRASS]) rotate([0, 0, 70]) is_bldg_house(seed = 848);
translate([-7, 5, ZGRASS]) is_bldg_shop(seed = 849, L = 8, D = 6);
translate([-23, 17, ZGRASS]) is_ground_field(10, 7, 850, 2);
translate([-27, 20, ZGRASS]) is_prop_scarecrow(seed = 851);
for (p = [[-30, 28], [-17, 29], [-31, 8], [2, 28]])
    translate([p[0], p[1], ZGRASS]) is_nature_tree(s = 1.05, seed = p[0]);

// Cove landing: short pier, rescue fire and two weather-beaten boats.
translate([1, -18, 0.34]) rotate([0, 0, -90]) is_bldg_dock(12, 3);
translate([1, -31, 0.12]) rotate([0, 0, -18]) is_veh_boat(seed = 852);
translate([-8, -28, 0.12]) rotate([0, 0, 24]) is_veh_boat(seed = 853);
translate([-14, -18, 0.20]) is_prop_firepit();
translate([-18, -16, 0.20]) is_prop_torch();
translate([-10, -16, 0.20]) is_prop_torch();
lay_scatter(10, -35, 35, -24, 35, 854)
    translate([0, 0, ZGRASS]) lay_pick($seed) { is_nature_bush(seed = $seed); is_nature_rock(0.7, $seed); }

gk_camera_lookat([48, -51, 32], [4, 8, 1], "lighthouse-cove-overview", 48);
gk_camera_lookat([39, 5, 8], [25, 25, 5], "lighthouse-headland", 52);
gk_camera_lookat([-18, -34, 4], [1, -18, 1], "cove-landing", 55);
