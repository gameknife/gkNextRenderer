// StudioSim office variant: client-facing executive suite and formal boardroom.
use <../../lib/kit_office.scad>
use <../../lib/gk_camera.scad>

$fn = 14;
FZ = 0.15;

module desk_boss_01() of_furn_exec_desk();
module desk_pm_01() of_furn_workstation(laptop = true, plant = true, mug = [0.68, 0.35, 0.28]);
module meet_seat_01() of_furn_conf_chair();
module lounge_01() of_furn_sofa();

of_office_floor();
translate([0, 8.875, 1.45]) of_wall_segment(24);
translate([-11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([-6.8, -8.875, 0.40]) of_wall_knee(10.4);
translate([6.8, -8.875, 0.40]) of_wall_knee(10.4);

// West boardroom behind glass.
*translate([-11.7, 1.0, FZ]) of_part_glass_wall(10.8, 8.2, 9.7);
translate([-11.7000, 1.9796, 0.1500]) rotate([0.0000, -0.0000, 0.0000]) scale([1.00000, 1.00000, 1.00000]) of_part_glass_wall(len = 10.8, g0 = 8.2, g1 = 9.7);
translate([-0.9, 1.0, FZ]) rotate([0, 0, 90]) of_part_glass_wall(7.8);
translate([-6.2, 5.0, FZ]) of_prop_rug(8.4, 5.6, [0.40, 0.46, 0.52]);
translate([-6.2, 5.0, FZ]) of_furn_conf_table();
for (a = [0 : 45 : 315]) translate([-6.2 + 2.4 * cos(a), 5.0 + 1.8 * sin(a), FZ]) rotate([0, 0, a + 90]) meet_seat_01();
translate([-11.55, 5.0, 1.7 + FZ]) rotate([0, 0, 90]) of_prop_projector_screen();

// Executive office and assistant station.
translate([3.0, 1.0, FZ]) of_part_glass_wall(8.7, 0.8, 2.2);
translate([3.0, 1.0, FZ]) rotate([0, 0, 90]) of_part_glass_wall(7.8);
translate([7.4, 5.2, FZ]) of_prop_rug(5.5, 4.7, [0.48, 0.32, 0.29]);
translate([7.2, 5.4, FZ]) rotate([0, 0, 180]) desk_boss_01();
translate([4.4, 3.2, FZ]) rotate([0, 0, 155]) of_furn_armchair();
translate([10.8, 7.0, FZ]) rotate([0, 0, -90]) of_prop_bookshelf_tall();
translate([7.6, -2.0, FZ]) desk_pm_01();

// Client lounge and formal reception.
translate([-5.0, -5.2, FZ]) of_prop_rug(6.5, 4.8, [0.72, 0.66, 0.56]);
translate([-7.2, -5.2, FZ]) rotate([0, 0, 90]) lounge_01();
translate([-3.7, -6.8, FZ]) rotate([0, 0, 180]) lounge_01();
translate([-4.8, -5.3, FZ]) of_prop_credenza();
translate([1.8, -6.5, FZ]) rotate([0, 0, -90]) of_prop_reception();
translate([10.8, -6.7, FZ]) of_prop_coat_stand();
for (p = [[-10.8, -7.8], [-10.8, 7.6], [10.8, -3.8], [2.0, 7.7]]) translate([p[0], p[1], FZ]) of_prop_plant_tall();

gk_camera_lookat([17, -21, 14], [0, 0, 0], "executive-overview", 52);
gk_camera_lookat([-1, 1, 2.3], [-6, 5, 1], "boardroom", 56);
gk_camera_lookat([2, 1, 2.2], [7, 5, 1], "executive-office", 56);
