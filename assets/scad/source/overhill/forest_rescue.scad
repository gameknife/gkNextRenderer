// Overhill variant: forest rescue operation around a washed-out trail.
use <../../lib/kit_overhill.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.30, 0.39, 0.24]) translate([0, 0, -0.16]) cube([110, 84, 0.32], center = true);
translate([0, 0, 0]) oh_ground_grass(100, 76, 1061);
translate([0, -3, 0.20]) oh_ground_trail(L = 102, W = 6.5, seed = 1062);
translate([6, 11, 0.22]) rotate([0, 0, 90]) oh_ground_river(L = 72, W = 9, seed = 1063);
translate([6, 11, 0.36]) oh_prop_bridge(L = 15, W = 6, seed = 1064);

// Rescue base on the east bank.
translate([29, 24, 0.22]) rotate([0, 0, 180]) oh_bldg_cabin(seed = 1065, L = 8, D = 6);
translate([43, 25, 0.22]) oh_bldg_tower(seed = 1066);
translate([18, 24, 0.22]) oh_prop_tent(seed = 1067);
translate([19, 17, 0.22]) oh_prop_campfire(seed = 1068);
translate([27, 15, 0.22]) oh_prop_jerrycan(seed = 1069);
translate([33, 15, 0.22]) oh_prop_barrel(seed = 1070);

// Narrative focus: stranded van below the bridge and vehicles staging a recovery.
translate([-4, 6, 0.20]) rotate([9, 0, 28]) oh_veh_van(seed = 1071);
translate([-24, -1, 0.22]) rotate([0, 0, 5]) oh_veh_offroader(seed = 1072);
translate([25, -2, 0.22]) rotate([0, 0, 180]) oh_veh_truck(seed = 1073);
translate([14, -4, 0.22]) oh_prop_tirestack(seed = 1074);
translate([8, -5, 0.22]) oh_prop_signpost(seed = 1075);
translate([-10, 12, 0.20]) oh_prop_log_pile(seed = 1076);

lay_scatter(34, -50, 50, -37, 37, seed = 1080)
    lay_pick($seed) { oh_nature_pine(s = 1.45, seed = $seed); oh_nature_autumn(s = 1.2, seed = $seed); oh_nature_bush(s = 1.15, seed = $seed); }
for (p = [[-45, 28], [-42, -28], [45, -28]]) translate([p[0], p[1], 0]) oh_terrain_hill(s = 1.1, seed = p[0]);

gk_camera_lookat([58, -58, 40], [0, 5, 0], "forest-rescue-overview", 50);
gk_camera_lookat([-19, -8, 3], [-3, 7, 1], "stranded-van", 55);
gk_camera_lookat([38, 8, 5], [24, 21, 1], "rescue-base", 54);
