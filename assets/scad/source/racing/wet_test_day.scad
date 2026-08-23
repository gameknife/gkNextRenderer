// Racing variant: wet-weather test day with standing water and safety drills.
use <../../lib/kit_pitlane.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.31, 0.37, 0.23]) translate([0, 0, -0.16]) cube([112, 82, 0.32], center = true);
translate([0, 21, 0]) rp_ground_track(L = 96, W = 14, seed = 1171, start = true);
translate([0, 6, 0]) rp_ground_pitlane(L = 72, W = 11, seed = 1172);
translate([0, -23, 0]) rp_ground_paddock(L = 88, D = 24, seed = 1173);

// Thin transparent puddles make the weather state legible from the default camera.
gk_material([0.30, 0.36, 0.39], roughness = 0.10, metalness = 0, alpha = 1)
{
    translate([-25, 21, 0.16]) scale([2.8, 1.0, 1]) cylinder(h = 0.025, r = 2.2, $fn = 18);
    translate([8, 19, 0.16]) scale([3.4, 0.8, 1]) cylinder(h = 0.025, r = 2.0, $fn = 18);
    translate([27, 7, 0.16]) scale([2.4, 0.9, 1]) cylinder(h = 0.025, r = 1.8, $fn = 18);
}

// Three test garages, telemetry wall and tire-change practice.
for (i = [0 : 4]) translate([-24 + i * 12, -6, 0.14]) rotate([0, 0, 180]) rp_bldg_garage(seed = 1180 + i, car = i == 4 ? -1 : i);
translate([-22, 4, 0.14]) rp_veh_gt3(seed = 1185);
translate([0, 4, 0.14]) rp_veh_gt3(seed = 1186);
translate([23, 4, 0.14]) rp_veh_safety_car(seed = 1187);
for (x = [-24, -8, 8, 24]) translate([x, 12, 0]) rp_prop_monitor(seed = x);
translate([34, -2, 0.14]) rp_prop_tire_stack(seed = 1188, n = 5);
translate([39, -2, 0.14]) rp_prop_toolcart(seed = 1189);

// Wet braking lane and recovery equipment.
for (x = [-40 : 6 : 40]) translate([x, 14, 0.14]) rp_prop_cone(seed = x);
for (x = [-43 : 6 : 43]) translate([x, 28.5, 0.12]) rp_prop_tire_wall(6, x);
translate([43, 9, 0]) rp_bldg_control_tower(seed = 1190, h = 11);
translate([-43, 9, 0]) rp_prop_pylon(seed = 1191, h = 7);
translate([0, -24, 0.14]) rp_veh_hauler(seed = 1192);
for (x = [-30, -15, 15, 30]) translate([x, -20, 0.14]) rp_prop_canopy(seed = x);
for (p = [[-49, 33], [49, 33], [-49, -32], [49, -32]]) translate([p[0], p[1], 0]) rp_prop_floodlight(14, p[0]);

gk_camera_lookat([59, -55, 38], [0, 4, 0], "wet-test-overview", 49);
gk_camera_lookat([-37, 16, 2], [20, 21, 1], "wet-braking-lane", 52);
gk_camera_lookat([31, -15, 3], [8, 2, 1], "test-garages", 55);
