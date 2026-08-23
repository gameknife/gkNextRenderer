// Overhill variant: remote canyon fuel stop and off-road rally checkpoint.
use <../../lib/kit_overhill.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.46, 0.34, 0.20]) translate([0, 0, -0.16]) cube([116, 88, 0.32], center = true);
translate([0, 0, 0]) oh_ground_sand(104, 76, 1031);
translate([0, -4, 0.20]) oh_ground_trail(L = 108, W = 8, seed = 1032);
translate([16, 14, 0.22]) rotate([0, 0, 90]) oh_ground_mud(L = 20, W = 8, seed = 1033);

// Fuel stop cluster and a shaded roadside repair court.
translate([20, 23, 0.22]) rotate([0, 0, 180]) oh_bldg_fuel(seed = 1034);
translate([37, 24, 0.22]) rotate([0, 0, 180]) oh_bldg_garage(seed = 1035, L = 9, D = 7);
translate([49, 27, 0.22]) oh_bldg_tower(seed = 1036);
translate([4, 22, 0.22]) oh_prop_tent(seed = 1037);
translate([8, 15, 0.22]) oh_prop_campfire(seed = 1038);
translate([31, 13, 0.22]) oh_prop_tirestack(seed = 1039);
translate([36, 13, 0.22]) oh_prop_jerrycan(seed = 1040);
translate([43, 14, 0.22]) oh_prop_barrel(seed = 1041);

// Rally checkpoint and stopped vehicles.
translate([-7, -4, 0.22]) oh_prop_gate_flags(W = 8, seed = 1042);
translate([-30, -2, 0.22]) rotate([0, 0, 8]) oh_veh_offroader(seed = 1043);
translate([12, -6, 0.22]) rotate([0, 0, -5]) oh_veh_truck(seed = 1044);
translate([34, -2, 0.22]) rotate([0, 0, 178]) oh_veh_van(seed = 1045);
for (x = [-45 : 12 : 45]) translate([x, 3, 0.22]) oh_prop_jerrycan(seed = x);

// Canyon walls and arid vegetation frame, rather than crowd, the road.
translate([-47, 28, 0]) oh_terrain_mesa(s = 1.6, seed = 1050);
translate([-45, -30, 0]) oh_terrain_mesa(s = 1.4, seed = 1051);
translate([50, -29, 0]) oh_rock_cluster(s = 1.8, seed = 1052, red = 1);
lay_scatter(24, -52, 52, -38, 38, seed = 1053)
    lay_pick($seed) { oh_nature_cactus(s = 1.1, seed = $seed); oh_nature_deadtree(s = 1.0, seed = $seed); oh_rock_boulder(s = 1.0, seed = $seed, red = 1); }

gk_camera_lookat([62, -61, 42], [0, 0, 0], "canyon-stop-overview", 49);
gk_camera_lookat([-42, -12, 3], [-7, -4, 1], "rally-checkpoint", 55);
gk_camera_lookat([50, 7, 5], [25, 21, 1], "fuel-court", 54);
