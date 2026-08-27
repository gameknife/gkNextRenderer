// Brotato3D variant: infected farmstead arena with crops, barn and broken machinery.
use <../../lib/kit_deadly.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.34, 0.38, 0.22]) translate([0, 0, -0.16]) cube([96, 76, 0.32], center = true);
translate([0, -2, 0]) dd_ground_dirt(L = 82, D = 58, seed = 101);
translate([0, 0, 0.20]) dd_ground_track(L = 88, W = 4.5, seed = 102);

// Northern working farm and eastern homestead.
translate([-26, 18, 0.20]) rotate([0, 0, 180]) dd_bldg_barn(seed = 103, L = 15, D = 16);
translate([-8, 22, 0.20]) dd_bldg_silo(seed = 104, s = 1.1);
translate([26, 20, 0.20]) dd_bldg_house_porch(seed = 105, L = 10, D = 7);
translate([36, 18, 0.20]) dd_bldg_shed(seed = 106);
translate([25, 28, 0.20]) dd_prop_mailbox();

// Crop lanes create readable combat channels without closing the arena.
translate([-24, -17, 0.20]) dd_nature_field_rows(L = 26, D = 14, seed = 110);
translate([5, -18, 0.20]) dd_nature_pumpkin_patch(L = 20, D = 13, seed = 111);
translate([30, -18, 0.20]) dd_nature_crop_patch(L = 16, D = 12, seed = 112);
for (x = [-38 : 6 : 38]) translate([x, -31, 0.20]) dd_prop_fence(len = 5.5);

// Narrative focus: evacuation abandoned around the harvester.
translate([-4, 3, 0.20]) rotate([0, 0, 18]) dd_veh_harvester(seed = 120);
translate([8, 2, 0.20]) rotate([0, 0, -20]) dd_veh_pickup(seed = 121);
translate([-12, -1, 0.20]) dd_prop_haybale(seed = 122);
translate([-15, 4, 0.20]) rotate([0, 0, 30]) dd_prop_haybale(seed = 123);
translate([14, 6, 0.20]) dd_prop_gascan();
translate([12, -3, 0.20]) dd_prop_debris(seed = 124);

// Perimeter cover and sparse dead vegetation keep the center playable.
for (p = [[-42, -28], [-43, 20], [42, -25], [43, 27]])
    translate([p[0], p[1], 0.20]) dd_nature_tree(s = 1.35, seed = p[0] + p[1]);
lay_scatter(18, -44, 44, -30, 30, seed = 130)
    lay_pick($seed) {
        dd_nature_bush(s = 1.1, seed = $seed);
        dd_nature_stump(s = 1.0, seed = $seed);
        dd_prop_trash(seed = $seed);
    }

gk_camera_lookat([48, -52, 34], [0, 0, 0], "farmstead-overview", 50);
gk_camera_lookat([-2, -13, 2.4], [-5, 6, 1.2], "harvester-ambush", 58);
