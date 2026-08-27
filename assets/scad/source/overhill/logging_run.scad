// Overhill variant: logging camp, timber crossing and loaded mountain convoy.
use <../../lib/kit_overhill.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.32, 0.39, 0.23]) translate([0, 0, -0.16]) cube([112, 84, 0.32], center = true);
translate([-28, 0, 0]) oh_ground_grass(56, 74, 1001);
translate([30, 0, 0]) oh_ground_grass(52, 74, 1002);
translate([0, 0, 0.20]) oh_ground_trail(L = 104, W = 7, seed = 1003);
translate([0, 18, 0.22]) rotate([0, 0, 90]) oh_ground_river(L = 72, W = 8, seed = 1004);
translate([0, 18, 0.36]) oh_prop_bridge(L = 14, W = 6.5, seed = 1005);

// Logging camp and saw-yard staging area.
translate([-31, 25, 0.20]) rotate([0, 0, 180]) oh_bldg_cabin(seed = 1006, L = 8, D = 6);
translate([-18, 27, 0.20]) rotate([0, 0, 180]) oh_bldg_garage(seed = 1007, L = 9, D = 7);
translate([-41, 28, 0.20]) oh_bldg_tower(seed = 1008);
for (p = [[-31, 14], [-25, 13], [-19, 14], [-13, 13]]) translate([p[0], p[1], 0.20]) oh_prop_log_pile(seed = p[0]);
translate([-23, 21, 0.20]) oh_prop_campfire(seed = 1009);
translate([-37, 17, 0.20]) oh_prop_tent(seed = 1010);

// Convoy climbing toward the bridge.
translate([-35, -1, 0.22]) rotate([0, 0, 4]) oh_veh_truck(seed = 1011);
translate([-17, 1, 0.22]) rotate([0, 0, -6]) oh_veh_trailer(seed = 1012);
translate([22, -1, 0.22]) rotate([0, 0, 175]) oh_veh_offroader(seed = 1013);
translate([39, 1, 0.22]) rotate([0, 0, 184]) oh_veh_van(seed = 1014);
for (x = [-45 : 10 : 45]) translate([x, -5, 0.20]) oh_prop_signpost(seed = x);

lay_scatter(30, -52, 52, -36, 36, seed = 1020)
    lay_pick($seed) { oh_nature_pine(s = 1.35, seed = $seed); oh_nature_autumn(s = 1.15, seed = $seed); oh_nature_bush(s = 1.1, seed = $seed); }
translate([48, 27, 0]) oh_terrain_hill(s = 1.4, seed = 1021);
translate([-49, -29, 0]) oh_rock_cluster(s = 1.4, seed = 1022);

gk_camera_lookat([60, -58, 40], [0, 5, 0], "logging-run-overview", 50);
gk_camera_lookat([-48, -7, 3], [-15, 1, 1], "timber-convoy", 55);
gk_camera_lookat([13, 5, 5], [0, 18, 1], "timber-bridge", 54);
