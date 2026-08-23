// Old City variant: lantern festival around a hillside temple courtyard.
use <../../lib/kit_old_city.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

color([0.32, 0.38, 0.23]) translate([0, 0, -0.16]) cube([106, 82, 0.32], center = true);
color([0.48, 0.41, 0.30]) translate([0, 8, 0]) oc_slab(78, 58, 0.18);

// Processional axis: paifang, incense court, inner gate and main hall.
translate([0, -34, 0.18]) oc_prop_paifang();
translate([0, -18, 0.18]) oc_prop_incense();
translate([0, 3, 0.18]) oc_bldg_inner_gate();
translate([0, 28, 0.18]) oc_bldg_keep();
translate([-25, 17, 0.18]) rotate([0, 0, 90]) oc_bldg_side_hall(L = 12, D = 7, seed = 971);
translate([25, 17, 0.18]) rotate([0, 0, -90]) oc_bldg_side_hall(L = 12, D = 7, seed = 972);

// Festival market and lantern procession.
for (x = [-34, -24, -14, 14, 24, 34])
    translate([x, -9, 0.18]) rotate([0, 0, x < 0 ? 90 : -90]) oc_bldg_stall(seed = 980 + x);
for (y = [-28 : 6 : 22])
{
    translate([-8, y, 0.18]) oc_prop_lantern();
    translate([8, y, 0.18]) oc_prop_lantern();
}
translate([-22, -22, 0.18]) oc_prop_cart(seed = 981);
translate([22, -22, 0.18]) oc_prop_crates(seed = 982);
translate([-27, -16, 0.18]) oc_prop_jar();
translate([28, -16, 0.18]) oc_prop_hay();

// Quiet garden corners contrast with the busy central axis.
translate([-34, 27, 0.18]) oc_prop_well();
translate([34, 28, 0.18]) oc_prop_stone_lamp();
translate([-31, 34, 0.18]) oc_nature_pine(s = 1.25);
translate([31, 34, 0.18]) oc_nature_pine(s = 1.25);
for (p = [[-39, 7], [39, 7], [-40, -29], [40, -29]])
    translate([p[0], p[1], 0.18]) oc_prop_flag(p[0] < 0 ? [0.72, 0.18, 0.14] : [0.74, 0.48, 0.12], 5.5);
lay_scatter(12, -48, 48, -36, 38, seed = 990)
    lay_pick($seed) { oc_nature_tree(s = 1.0, i = $seed); oc_nature_rock(s = 0.8, i = $seed); }

gk_camera_lookat([56, -55, 38], [0, 5, 0], "temple-festival-overview", 50);
gk_camera_lookat([0, -40, 3], [0, 18, 2], "processional-axis", 55);
gk_camera_lookat([35, -4, 5], [0, -9, 1], "festival-market", 54);
