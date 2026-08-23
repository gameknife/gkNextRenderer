// Brotato3D variant: a roadside motel encircled by abandoned traffic and barricades.
use <../../lib/kit_deadly.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.31, 0.35, 0.22]) translate([0, 0, -0.16]) cube([104, 78, 0.32], center = true);
translate([0, 0, 0]) dd_ground_road(L = 100, W = 8, seed = 201);
translate([0, 20, 0]) dd_ground_lot(L = 84, D = 24, seed = 202, bands = 2);
translate([0, -22, 0]) dd_ground_gravel(L = 88, D = 22, seed = 203);

// Roadside strip: motel, diner and fuel stop form three arena landmarks.
translate([-23, 25, 0.20]) rotate([0, 0, 180]) dd_bldg_motel(seed = 204, units = 7);
translate([24, 24, 0.20]) rotate([0, 0, 180]) dd_bldg_diner(seed = 205);
translate([30, -21, 0.20]) dd_bldg_gasstation(seed = 206);
translate([-32, -20, 0.20]) rotate([0, 0, 180]) dd_bldg_trailer(seed = 207);
translate([-18, -24, 0.20]) dd_prop_tent(seed = 208, s = 1.1);
translate([-10, -24, 0.20]) dd_prop_campfire(seed = 209);

// Siege line: wrecks and barriers break sightlines but leave four clear gaps.
translate([-34, 1.8, 0.20]) rotate([0, 0, 8]) dd_veh_bus(seed = 210);
translate([-12, -1.5, 0.20]) rotate([0, 0, -14]) dd_veh_flipped(seed = 211);
translate([12, 1.2, 0.20]) rotate([0, 0, 16]) dd_veh_wreck(seed = 212);
translate([36, -1.5, 0.20]) rotate([0, 0, 178]) dd_veh_truck(seed = 213);
for (x = [-43, -25, 2, 24, 43]) translate([x, 5.2, 0.20]) dd_prop_barricade();
for (x = [-39, -31, 28, 36]) translate([x, -5.0, 0.20]) dd_prop_jersey(seed = x);
lay_scatter(14, -44, 44, -6, 6, seed = 214) dd_prop_debris(seed = $seed);

// Supplies tell the story of a failed survivor holdout.
translate([-3, 22, 0.20]) dd_prop_sandbags(len = 8, seed = 220);
translate([4, 24, 0.20]) dd_prop_crate(1.2);
translate([7, 24, 0.20]) dd_prop_barrel(seed = 221);
translate([10, 23, 0.20]) dd_prop_gascan();
translate([38, 27, 0.20]) dd_prop_billboard(seed = 222);
translate([42, -28, 0.20]) dd_bldg_watertower(s = 0.9, seed = 223);

for (p = [[-48, -31], [-47, 30], [47, -30], [47, 31]])
    translate([p[0], p[1], 0.20]) dd_nature_pine(s = 1.25, seed = p[0]);

gk_camera_lookat([54, -54, 36], [0, 1, 0], "motel-overview", 50);
gk_camera_lookat([-4, 11, 2.2], [-10, 0, 1], "siege-line", 58);
gk_camera_lookat([41, -10, 7], [30, -21, 1], "fuel-stop", 52);
