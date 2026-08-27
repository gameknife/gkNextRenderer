// Old City variant: riverside market town at a busy ferry landing.
use <../../lib/kit_old_city.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

color([0.34, 0.38, 0.24]) translate([0, 6, -0.16]) cube([112, 76, 0.32], center = true);
color([0.15, 0.27, 0.31, 0.86]) translate([0, -30, 0.02]) cube([112, 20, 0.08], center = true);
color([0.43, 0.38, 0.29]) translate([0, -18, 0]) oc_slab(108, 6, 0.20);

// Main street enters through a paifang and terminates at the ferry quay.
translate([0, 31, 0.16]) oc_prop_paifang();
translate([0, 21, 0.16]) rotate([0, 0, 180]) oc_bldg_inn();
translate([-30, 23, 0.16]) rotate([0, 0, 180]) oc_bldg_house(seed = 901, L = 10, D = 7);
translate([30, 23, 0.16]) rotate([0, 0, 180]) oc_bldg_house(seed = 902, L = 10, D = 7);
translate([-43, 18, 0.16]) rotate([0, 0, 180]) oc_bldg_warehouse(seed = 903);
translate([43, 18, 0.16]) rotate([0, 0, 180]) oc_bldg_granary();

// Market rows face a broad central aisle.
for (x = [-38 : 12 : 38])
{
    translate([x, 6, 0.16]) oc_bldg_stall(seed = 910 + x);
    translate([x, -5, 0.16]) rotate([0, 0, 180]) oc_bldg_stall(seed = 920 + x);
}
translate([-14, 1, 0.16]) rotate([0, 0, 18]) oc_prop_cart(seed = 930);
translate([10, 1, 0.16]) oc_prop_crates(seed = 931);
translate([17, -1, 0.16]) oc_prop_jar();
translate([24, 1, 0.16]) oc_prop_hay();
translate([-26, 0, 0.16]) oc_prop_well();

// Ferry landing and waterside storytelling.
for (x = [-48 : 6 : 48]) translate([x, -16, 0.20]) oc_prop_stone_lamp();
translate([-18, -20, 0.20]) oc_prop_crates(seed = 932);
translate([-9, -21, 0.20]) oc_prop_cart(seed = 933);
translate([16, -20, 0.20]) oc_prop_rack();
translate([25, -20, 0.20]) oc_prop_jar();
for (x = [-39, -27, 32, 44]) translate([x, -24, 0.10]) oc_nature_rock(s = 1.1, i = x);

lay_scatter(14, -51, 51, 12, 33, seed = 940)
    lay_pick($seed) { oc_nature_tree(s = 1.1, i = $seed); oc_nature_pine(s = 1.0); }

gk_camera_lookat([58, -55, 38], [0, 2, 0], "riverside-market-overview", 50);
gk_camera_lookat([0, -24, 3], [0, 8, 1], "market-street", 56);
gk_camera_lookat([42, -35, 6], [18, -18, 1], "ferry-landing", 52);
