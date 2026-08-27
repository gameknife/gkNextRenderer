// Habor City variant: working fishing quay with morning market and small-boat harbor.
use <../../lib/kit_city_hd.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

gk_material([0.12, 0.25, 0.31], roughness = 0.22, metalness = 0)
    translate([0, -16, -0.18]) cube([128, 82, 0.36], center = true);
color([0.33, 0.34, 0.32]) translate([0, 25, 0]) hc_slab(128, 34, 0.22);
color([0.45, 0.43, 0.38]) translate([0, 7, 0.01]) hc_slab(128, 4, 0.24);

// Market street faces the quay.
translate([-34, 29, 0.22]) hc_bldg_market(L = 24, D = 13, H = 6);
translate([-8, 29, 0.22]) hc_bldg_cafe(L = 11, D = 9, H = 4.4);
translate([13, 29, 0.22]) hc_bldg_shop_unit("FISH CO-OP", hc_TEALC(), 14, 9, 4.8);
translate([35, 29, 0.22]) hc_prop_harbor_hut();
translate([44, 29, 0.22]) hc_prop_harbor_hut();
for (x = [-48 : 8 : 48]) translate([x, 10, 0.24]) hc_prop_bollard_pair();
for (x = [-44 : 11 : 44]) translate([x, 15, 0.22]) hc_prop_lamp();

// Three timber piers and an active unloading lane.
for (x = [-36, 0, 36]) translate([x, 8, 0.24]) hc_prop_pier(len = 34, w = 4);
translate([-31, -14, -0.42]) rotate([0, 0, 90]) hc_boat_sail([0.28, 0.36, 0.48]);
translate([-40, -21, -0.42]) rotate([0, 0, -90]) hc_boat_speed([0.76, 0.30, 0.20], false);
translate([4, -18, -0.42]) rotate([0, 0, 90]) hc_boat_speed([0.86, 0.85, 0.78], true);
translate([31, -17, -0.42]) rotate([0, 0, -90]) hc_boat_sail([0.42, 0.27, 0.20]);

// Narrative focus: the day's catch is transferred into a waiting box truck.
translate([27, 17, 0.22]) rotate([0, 0, 180]) hc_veh_truck_box([0.22, 0.45, 0.58], [0.80, 0.82, 0.80]);
translate([34, 15, 0.22]) hc_veh_forklift();
translate([22, 11, 0.24]) hc_prop_crate_pile(seed = 701);
translate([28, 10, 0.24]) hc_prop_crate_pile(seed = 702);
translate([40, 12, 0.24]) hc_prop_lifering_post();
for (p = [[-20, -4, 8], [12, -8, 5], [45, -3, 10]])
    translate([p[0], p[1], p[2]]) rotate([0, 0, p[0]]) hc_prop_gull();

gk_camera_lookat([70, -66, 46], [0, 10, 0], "fishing-quay-overview", 48);
gk_camera_lookat([18, -3, 3], [28, 14, 1], "catch-unloading", 55);
gk_camera_lookat_key([-52, -26, 5], [-36, 3, 1], "harbor-walk", 0, 52);
gk_camera_lookat_key([50, -24, 5], [36, 8, 1], "harbor-walk", 10, 52);
