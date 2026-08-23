// Brotato3D variant: improvised quarantine depot in an abandoned logistics yard.
use <../../lib/kit_deadly.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.29, 0.33, 0.23]) translate([0, 0, -0.16]) cube([108, 82, 0.32], center = true);
translate([0, 2, 0]) dd_ground_concrete(L = 94, D = 64, seed = 301);
translate([0, -22, 0.20]) dd_ground_road(L = 104, W = 8, seed = 302);

// Depot buildings and fenced isolation wing.
translate([-28, 21, 0.20]) rotate([0, 0, 180]) dd_bldg_warehouse(seed = 303, L = 34, D = 17);
translate([25, 23, 0.20]) rotate([0, 0, 180]) dd_bldg_block(seed = 304, L = 20, D = 13, floors = 2);
translate([39, 18, 0.20]) dd_prop_radiomast(s = 0.9, seed = 305);
for (x = [-48 : 8 : 48]) translate([x, 34, 0.20]) dd_prop_chainlink(len = 8, seed = x);
for (y = [-26 : 8 : 26]) translate([-50, y, 0.20]) rotate([0, 0, 90]) dd_prop_chainlink(len = 8, seed = y);

// Triage tents, supply lanes and decontamination choke point.
for (x = [-4, 6, 16]) translate([x, 8, 0.20]) dd_prop_tent(seed = 310 + x, s = 1.15);
translate([5, 2, 0.20]) dd_prop_sandbags(len = 14, seed = 311);
for (x = [-9 : 6 : 21]) translate([x, -4, 0.20]) dd_prop_jersey(seed = x);
for (p = [[-6, 13], [3, 13], [12, 13], [21, 13]])
    translate([p[0], p[1], 0.20]) dd_prop_container(seed = p[0], stack = 1);
translate([-2, 18, 0.20]) dd_prop_pallet(seed = 312);
translate([6, 18, 0.20]) dd_prop_crate(1.2);
translate([10, 18, 0.20]) dd_prop_barrel(seed = 313);

// Failed evacuation convoy blocks the southern access road.
translate([-30, -22, 0.20]) rotate([0, 0, 5]) dd_veh_bus(seed = 320);
translate([-8, -22, 0.20]) rotate([0, 0, -7]) dd_veh_van(seed = 321);
translate([17, -22, 0.20]) rotate([0, 0, 12]) dd_veh_truck(seed = 322, trailer = 0);
translate([38, -21, 0.20]) rotate([0, 0, 175]) dd_veh_pickup(seed = 323);
for (x = [-43 : 7 : 43]) translate([x, -15, 0.20]) dd_prop_cone(seed = x);
lay_scatter(16, -46, 46, -31, 31, seed = 324)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_trash(seed = $seed); dd_nature_bush(seed = $seed); }

gk_camera_lookat([56, -56, 38], [0, 3, 0], "depot-overview", 50);
gk_camera_lookat([4, -12, 2.1], [6, 11, 1], "triage-lane", 58);
gk_camera_lookat([-42, -17, 4], [-15, -22, 1], "evacuation-convoy", 52);
