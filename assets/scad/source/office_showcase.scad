// office_showcase.scad —— kit_office 零件总览（按类别排列，验收/选型用）
// gnb shot --scene assets/scad/source/office_showcase.scad

use <../lib/kit_office.scad>

$fn = 12;
FZ = 0.15;

of_office_floor();

// 墙体与隔断
translate([0, 8.85, 1.45]) of_wall_segment(18);
translate([-11.85, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(12);
translate([0, 3.7, FZ]) of_part_glass_wall(8);

// 工位与会议家具
translate([-8.4, 5.7, FZ]) of_furn_conf_table();
translate([-9.8, 4.5, FZ]) rotate([0, 0, 180]) of_furn_conf_chair();
translate([-6.9, 4.5, FZ]) of_furn_conf_chair();
translate([-7.8, 0.0, FZ]) of_furn_workstation(dual = true, plant = true, sticky = true);
translate([-3.9, 0.0, FZ]) of_furn_workstation(laptop = true, tablet = true);
translate([0.4, 0.0, FZ]) of_furn_exec_desk();
translate([4.8, 0.0, FZ]) of_furn_sofa();

// 办公室道具
translate([-9.2, -5.8, FZ]) of_prop_reception();
translate([-5.5, -5.8, FZ]) of_prop_plant_tall();
translate([-2.4, -5.8, FZ]) of_prop_water_cooler();
translate([0.8, -5.8, FZ]) of_prop_printer_island();
translate([4.2, -5.8, FZ]) of_prop_server_rack();
translate([7.6, -5.8, FZ]) of_prop_locker();
translate([10.4, -5.8, FZ]) of_prop_bookshelf_tall();
