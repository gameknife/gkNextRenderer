// Coldwar variant: railway customs checkpoint and contested freight yard.
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.32, 0.37, 0.23]) translate([0, 0, -0.16]) cube([112, 82, 0.32], center = true);
translate([0, 0, 0]) cw_ground_dirt(L = 100, D = 68, seed = 401);
translate([0, -12, 0.20]) cw_ground_rail(L = 104, seed = 402);
translate([0, 8, 0.20]) cw_ground_rail(L = 104, seed = 403);
translate([0, -2, 0.40]) rotate([0, 0, 90]) cw_ground_road(L = 72, W = 7, seed = 404);

// Customs buildings straddle the road without blocking the tracks.
translate([-16, 23, 0.20]) rotate([0, 0, 180]) cw_bldg_warehouse(seed = 405, L = 22, D = 10);
translate([21, 22, 0.20]) rotate([0, 0, 180]) cw_bldg_checkpoint(seed = 406);
translate([35, 24, 0.20]) cw_bldg_guard_tower(seed = 407);
translate([-38, 22, 0.20]) cw_bldg_water_tower(seed = 408);
translate([0, 14, 0.20]) cw_prop_barrier_gate(seed = 409);
translate([0, -20, 0.20]) rotate([0, 0, 180]) cw_prop_barrier_gate(seed = 410);

// Freight convoy frozen midway through inspection.
translate([-32, -12, 0.40]) rotate([0, 0, 8]) cw_veh_truck_canvas(seed = 411);
translate([-8, 8, 0.40]) rotate([0, 0, -5]) cw_veh_uaz_van(seed = 412);
translate([22, -12, 0.40]) rotate([0, 0, 176]) cw_veh_wreck(seed = 413);
translate([39, 8, 0.40]) rotate([0, 0, 185]) cw_veh_lada(seed = 414);
for (x = [-45, -30, -15, 15, 30, 45]) translate([x, 14, 0.20]) cw_prop_hedgehog();

// Searched cargo and a small military holdout tell the scene story.
for (p = [[-28, 30], [-24, 30], [-20, 30], [12, 28], [16, 28]])
    translate([p[0], p[1], 0.20]) cw_prop_crate_ammo(seed = p[0]);
translate([-30, 25, 0.20]) cw_prop_pallet(seed = 415);
translate([10, 24, 0.20]) cw_prop_wall_sandbag(len = 6);
translate([15, 20, 0.20]) cw_prop_searchlight(seed = 416);
translate([42, -27, 0.20]) cw_prop_sign_road(seed = 417);

lay_scatter(18, -52, 52, -34, 34, seed = 420)
    lay_pick($seed) { cw_nature_birch(s = 1.2, seed = $seed); cw_nature_bush(seed = $seed); cw_nature_rock(seed = $seed); }

gk_camera_lookat([58, -56, 38], [0, 0, 0], "rail-checkpoint-overview", 50);
gk_camera_lookat([-46, -5, 3], [5, -2, 1], "rail-crossing", 52);
gk_camera_lookat([28, 13, 7], [20, 22, 2], "customs-yard", 55);
