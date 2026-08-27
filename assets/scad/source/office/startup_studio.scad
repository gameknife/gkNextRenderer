// StudioSim office variant: compact startup studio with an open creative floor.
use <../../lib/kit_office.scad>
use <../../lib/gk_camera.scad>

$fn = 14;
FZ = 0.15;

module desk_engineer_01() of_furn_workstation(dual = true, mug = [0.25, 0.55, 0.82], plant = true);
module desk_engineer_02() of_furn_workstation(laptop = true, sticky = true, mug = [0.82, 0.42, 0.24]);
module desk_artist_01() of_furn_workstation(tablet = true, screen = [0.88, 0.68, 0.78], plant = true);
module desk_designer_01() of_furn_workstation(laptop = true, mug = [0.55, 0.42, 0.82]);
module lounge_01() of_prop_foosball();

of_office_floor();
translate([0, 8.875, 1.45]) of_wall_segment(24);
translate([-11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([-6.8, -8.875, 0.40]) of_wall_knee(10.4);
translate([6.8, -8.875, 0.40]) of_wall_knee(10.4);

// Four-discipline work island around a shared printer and planter spine.
translate([-6.5, 3.0, FZ]) rotate([0, 0, 180]) desk_engineer_01();
translate([-3.8, 3.0, FZ]) rotate([0, 0, 180]) desk_engineer_02();
translate([-6.5, -0.4, FZ]) desk_artist_01();
translate([-3.8, -0.4, FZ]) desk_designer_01();
translate([-5.1, 1.2, FZ]) of_prop_planter(2.4);
translate([-1.8, 1.2, FZ]) of_prop_printer_island();

// Pitch corner and relaxed play-test lounge.
translate([5.8, 4.7, FZ]) of_furn_conf_table();
for (a = [0 : 60 : 300]) translate([5.8 + 2.2 * cos(a), 4.7 + 1.7 * sin(a), FZ]) rotate([0, 0, a + 90]) of_furn_conf_chair();
translate([8.7, 8.62, 1.75 + FZ]) of_prop_projector_screen();
translate([2.2, -2.0, FZ]) lounge_01();
translate([4.4, -3.0, FZ]) of_prop_beanbag([0.90, 0.55, 0.25]);
translate([5.4, -1.8, FZ]) of_prop_beanbag([0.25, 0.60, 0.55]);
translate([7.7, -2.5, FZ]) of_furn_sofa();

// Reception, pantry and the deliberately visible startup server corner.
translate([1.5, -6.7, FZ]) rotate([0, 0, -90]) of_prop_reception();
translate([-3.0, 8.45, FZ]) of_prop_fridge();
translate([-1.5, 8.40, FZ]) of_prop_water_cooler();
translate([0.3, 8.45, FZ]) of_prop_binder_shelf();
translate([10.7, -7.8, FZ]) rotate([0, 0, 180]) of_prop_server_rack();
translate([8.8, -7.8, FZ]) of_prop_boxes();
translate([-10.7, -7.7, FZ]) of_prop_plant_tall();
translate([10.7, 7.6, FZ]) of_prop_plant_tall();

gk_camera_lookat([17, -21, 14], [0, 0, 0], "startup-overview", 52);
gk_camera_lookat([-1, -5, 2], [-5, 2, 1], "creative-floor", 58);
