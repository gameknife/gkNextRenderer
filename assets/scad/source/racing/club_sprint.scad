// Racing variant: compact club sprint circuit with a modest paddock and local crowd.
use <../../lib/kit_pitlane.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.36, 0.42, 0.24]) translate([0, 0, -0.16]) cube([104, 78, 0.32], center = true);
translate([0, 20, 0]) rp_ground_track(L = 88, W = 12, seed = 1141, start = true);
translate([0, 6, 0]) rp_ground_pitlane(L = 64, W = 10, seed = 1142);
translate([0, -22, 0]) rp_ground_paddock(L = 80, D = 24, seed = 1143);

// Four club garages and an open scrutineering bay.
for (i = [0 : 3]) translate([-18 + i * 12, -5, 0.14]) rotate([0, 0, 180]) rp_bldg_garage(seed = 1150 + i, car = i);
translate([-35, -4, 0.14]) rp_prop_canopy(seed = 1154, S = 4);
translate([-35, -4, 0.14]) rotate([0, 0, 90]) rp_prop_lift(seed = 1155);
translate([-35, -4, 0.8]) rotate([0, 0, -90]) rp_veh_gt3(seed = 1156);
translate([-41, -3, 0.14]) rp_prop_toolcart(seed = 1157);
for (x = [-28 : 8 : 28]) translate([x, 12, 0]) rp_prop_pitwall(8, x);

// Local spectator and marshal facilities.
translate([0, 32, 0]) rp_bldg_grandstand(L = 26, rows = 6, seed = 1160);
translate([40, 8, 0]) rp_bldg_control_tower(seed = 1161, h = 10);
translate([-41, 8, 0]) rp_prop_pylon(seed = 1162, h = 6);
translate([34, 8, 0.14]) rp_veh_safety_car(seed = 1163);
for (x = [-40 : 5 : 40]) translate([x, 27, 0.12]) rp_prop_guardrail(5, x);
for (x = [-42 : 7 : 42]) translate([x, 13.5, 0.14]) rp_prop_cone(seed = x);

// Grassroots paddock: vans, trailers and family canopies.
for (x = [-32, -12, 10, 30]) translate([x, -28, 0.14]) rp_veh_van(seed = x);
for (x = [-25, -5, 15, 35]) translate([x, -18, 0.14]) rp_prop_canopy(seed = x, S = 3.2);
translate([-45, -25, 0.14]) rp_veh_hauler(seed = 1164);
for (x = [-43 : 8 : 43]) translate([x, -36, 0]) rp_nature_hedge(6, x);

gk_camera_lookat([55, -52, 35], [0, 3, 0], "club-sprint-overview", 50);
gk_camera_lookat([37, 17, 3], [0, 20, 1], "start-grid", 52);
gk_camera_lookat([-46, -16, 4], [-20, -8, 1], "club-paddock", 55);
