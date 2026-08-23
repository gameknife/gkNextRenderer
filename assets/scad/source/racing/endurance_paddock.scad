// Racing variant: endurance event paddock with refueling, driver change and heavy logistics.
use <../../lib/kit_pitlane.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.34, 0.40, 0.23]) translate([0, 0, -0.16]) cube([132, 94, 0.32], center = true);
translate([0, 25, 0]) rp_ground_track(L = 112, W = 14, seed = 1101, start = true);
translate([0, 9, 0]) rp_ground_pitlane(L = 92, W = 12, seed = 1102);
translate([0, -24, 0]) rp_ground_paddock(L = 112, D = 28, seed = 1103);

// Eight endurance garages with three distinct service stories.
for (i = [0 : 7]) translate([-35 + i * 10, -3, 0.14]) rotate([0, 0, 180]) rp_bldg_garage(seed = 1110 + i, car = i % 3 == 0 ? i : -1);
translate([-30, 6, 0.14]) rp_veh_gt3(seed = 1120);
translate([-33, 4, 0.14]) rp_prop_fuel_rig(seed = 1121);
translate([-26, 4, 0.14]) rp_prop_tire_stack(seed = 1122, n = 4);
translate([0, 6, 0.14]) rp_veh_gt3(seed = 1123);
translate([0, 3.5, 0.14]) rp_prop_lift(seed = 1124);
translate([28, 6, 0.14]) rp_veh_gt3(seed = 1125);
translate([33, 4, 0.14]) rp_prop_toolcart(seed = 1126);
for (x = [-40 : 8 : 40]) translate([x, 15, 0]) rp_prop_pitwall(8, x);

// Overnight logistics: haulers, hospitality and canopy crews.
for (x = [-42, -20, 2, 24, 46]) translate([x, -31, 0.14]) rp_veh_hauler(seed = 1130 + x);
for (x = [-35 : 12 : 37])
{
    translate([x, -18, 0.14]) rp_prop_canopy(seed = x, S = 3.5);
    translate([x, -21, 0.14]) rp_prop_tire_stack(seed = x, n = 4);
}
translate([51, -16, 0.14]) rotate([0, 0, 90]) rp_bldg_hospitality(seed = 1131, L = 13, D = 8);
translate([-52, -16, 0.14]) rotate([0, 0, -90]) rp_bldg_hospitality(seed = 1132, L = 13, D = 8);

for (p = [[-58, 34], [0, 34], [58, 34], [-58, -37], [58, -37]]) translate([p[0], p[1], 0]) rp_prop_floodlight(h = 15, seed = p[0]);
for (x = [-51 : 6 : 51]) translate([x, 37, 0]) rp_prop_fence_catch(6, 3, x);
translate([45, 10, 0.14]) rp_veh_safety_car(seed = 1133);

gk_camera_lookat([68, -61, 42], [0, 5, 0], "endurance-paddock-overview", 49);
gk_camera_lookat([-17, 14, 2], [0, 5, 1], "pit-lane-night", 56);
gk_camera_lookat_key([-46, 9, 1.4], [-30, 9, 1], "endurance-pit-drive", 0, 52);
gk_camera_lookat_key([46, 9, 1.4], [58, 15, 1], "endurance-pit-drive", 12, 52);
