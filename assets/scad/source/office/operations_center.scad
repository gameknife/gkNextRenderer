// StudioSim office variant: live operations center with monitoring wall and server bay.
use <../../lib/kit_office.scad>
use <../../lib/gk_camera.scad>

$fn = 14;
FZ = 0.15;

module desk_ops_01() of_furn_workstation(dual = true, sticky = true, screen = [0.54, 0.82, 0.66], mug = [0.30, 0.55, 0.75]);
module desk_ops_02() of_furn_workstation(dual = true, screen = [0.82, 0.62, 0.42], mug = [0.80, 0.30, 0.28]);
module desk_qa_01() of_furn_workstation(dual = true, laptop = true, screen = [0.66, 0.78, 0.92]);
module meet_seat_01() of_furn_conf_chair();

of_office_floor();
translate([0, 8.875, 1.45]) of_wall_segment(24);
translate([-11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([-6.8, -8.875, 0.40]) of_wall_knee(10.4);
translate([6.8, -8.875, 0.40]) of_wall_knee(10.4);

// Two operator rows face a wall of status boards.
for (x = [-7.5, -4.5, -1.5, 1.5]) translate([x, 2.8, FZ]) rotate([0, 0, 180]) desk_ops_01();
for (x = [-7.5, -4.5, -1.5, 1.5]) translate([x, -1.2, FZ]) desk_ops_02();
translate([-7.5, 8.68, 1.65 + FZ]) of_prop_whiteboard();
translate([-2.8, 8.68, 1.65 + FZ]) of_prop_projector_screen();
translate([1.5, 8.68, 1.65 + FZ]) of_prop_whiteboard();
translate([-3.0, 0.8, FZ]) of_prop_planter(7.5);

// Glass incident room with QA lead station.
translate([4.4, 0.3, FZ]) rotate([0, 0, 90]) of_part_glass_wall(8.4, 5.8, 7.2);
translate([4.4, 4.5, FZ]) of_part_glass_wall(7.4);
translate([8.0, 4.8, FZ]) rotate([0, 0, 180]) desk_qa_01();
translate([8.0, 2.0, FZ]) of_furn_conf_table();
for (x = [6.3, 8.0, 9.7]) translate([x, 0.8, FZ]) rotate([0, 0, 180]) meet_seat_01();

// Server bay, print/supply strip, and overnight support lounge.
for (x = [5.6, 7.0, 8.4, 9.8]) translate([x, -7.9, FZ]) rotate([0, 0, 180]) of_prop_server_rack();
translate([10.8, -5.4, FZ]) rotate([0, 0, -90]) of_prop_locker();
translate([3.9, -7.9, FZ]) of_prop_boxes();
translate([-10.8, -6.8, FZ]) rotate([0, 0, 90]) of_prop_bookshelf_tall();
translate([-8.6, -6.0, FZ]) of_prop_fridge();
translate([-6.8, -6.0, FZ]) of_prop_water_cooler();
translate([-4.0, -6.2, FZ]) of_furn_sofa();
translate([-1.5, -6.0, FZ]) of_prop_beanbag([0.25, 0.60, 0.55]);

gk_camera_lookat([17, -21, 14], [0, 0, 0], "operations-overview", 52);
gk_camera_lookat([-3, -6, 2.1], [-3, 4, 1], "operator-floor", 58);
gk_camera_lookat([11, -2, 2.3], [8, 3, 1], "incident-room", 56);
