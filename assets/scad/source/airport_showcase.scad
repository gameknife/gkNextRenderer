// airport_showcase.scad —— kit_airport 零件总览（按类别排列，验收/选型用）
// gnb shot --scene assets/scad/source/airport_showcase.scad

use <../lib/kit_airport.scad>

$fn = 12;
FZ = 0.15;

// 展台与地面
ap_ground_base();
translate([0, 23, FZ]) ap_ground_apron();
translate([0, -24, FZ]) ap_ground_landside();

// 航站楼结构与旅客设施
translate([-30, 19, FZ]) ap_wall_glass_seg(12);
translate([-15, 19, FZ]) ap_wall_solid_seg(10);
translate([0, 19, FZ]) ap_wall_corner_col();
translate([-29, 4, FZ]) ap_furn_entrance();
translate([-20, 4, FZ]) ap_furn_checkin_desk("A1");
translate([-13, 4, FZ]) ap_furn_kiosk();
translate([-6, 4, FZ]) ap_furn_security_lane();
translate([4, 4, FZ]) ap_furn_gate_door("GATE 1");
translate([13, 4, FZ]) ap_furn_bench_row();

// 商业、服务与道具
translate([-29, -9, FZ]) ap_furn_cafe_counter();
translate([-20, -9, FZ]) ap_furn_cafe_table();
translate([-11, -9, FZ]) ap_prop_shop_portal(label = "SHOP");
translate([-2, -9, FZ]) ap_furn_atm();
translate([6, -9, FZ]) ap_furn_vending();
translate([15, -9, FZ]) ap_prop_container();
translate([25, -9, FZ]) ap_prop_big_sign("AIRPORT");

// 地勤与场外载具
translate([-29, 34, FZ]) rotate([0, 0, 174]) ap_veh_airliner();
translate([-5, 34, FZ]) rotate([0, 0, 188]) ap_veh_bus();
translate([10, 34, FZ]) rotate([0, 0, 90]) ap_veh_taxi();
translate([20, 34, FZ]) ap_prop_light_mast();
translate([29, 34, FZ]) ap_prop_windsock();
