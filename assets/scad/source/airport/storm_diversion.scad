// Airport variant: storm-diversion traffic surge and crowded recovery gate.
use <../../lib/kit_airport.scad>
use <../../lib/gk_camera.scad>

$fn = 12;
FZ = 0.15;

ap_ground_base();
ap_ground_carpet();
ap_ground_apron();
ap_ground_landside();

// Open recovery hall with three entrances and temporary processing islands.
for (x = [-24, -8, 8, 24]) translate([x, -12, FZ]) ap_wall_glass_seg(12);
translate([-30, -12, FZ]) rotate([0, 0, 90]) ap_wall_glass_seg(40);
translate([30, -12, FZ]) rotate([0, 0, 90]) ap_wall_glass_seg(40);
translate([-15, 28, FZ]) ap_wall_glass_seg(20);
translate([15, 28, FZ]) ap_wall_glass_seg(20);
for (x = [-18, 0, 18]) translate([x, -12, FZ]) ap_furn_entrance();
translate([0, -12.3, 3.8]) ap_prop_big_sign("DIVERSION HUB");

for (i = [0 : 5])
    translate([-21 + i * 4.2, -1, FZ]) ap_furn_checkin_desk(str(i + 11));
for (i = [0 : 3])
    translate([-18 + i * 6, -4, FZ]) ap_prop_queue_line(5);
translate([20, -1, FZ]) ap_furn_info_desk();
translate([24, -1, FZ]) ap_prop_fids();
translate([23, -5, FZ]) ap_furn_atm();

// Dense improvised waiting area, food support, and rebooking kiosks.
for (y = [8, 12, 16, 20])
    for (x = [-20, -12, -4, 8, 16])
        translate([x, y, FZ]) rotate([0, 0, y % 8 == 0 ? 180 : 0]) ap_furn_bench_row();
for (x = [-18, -10, -2, 6]) translate([x, 5, FZ]) ap_furn_kiosk();
translate([20, 8, FZ]) ap_furn_food_counter();
translate([22, 13, FZ]) ap_furn_cafe_counter();
translate([19, 17, FZ]) ap_furn_vending([0.25, 0.45, 0.75]);
translate([23, 17, FZ]) ap_furn_vending([0.80, 0.30, 0.28]);
translate([0, 24, FZ]) ap_prop_hang_sign("RECOVERY GATES", [0.22, 0.58, 0.35], false);

// Diverted fleet: tightly parked aircraft and buses moving passengers airside.
translate([-24, 38, FZ]) rotate([0, 0, 204]) scale([0.72, 0.72, 0.72]) ap_veh_airliner();
translate([0, 39, FZ]) rotate([0, 0, 180]) scale([0.78, 0.78, 0.78])
    ap_veh_airliner([0.90, 0.90, 0.92], [0.76, 0.35, 0.18]);
translate([25, 38, FZ]) rotate([0, 0, 158]) scale([0.68, 0.68, 0.68])
    ap_veh_airliner([0.88, 0.90, 0.92], [0.24, 0.52, 0.40]);
for (x = [-20, -5, 10, 24]) translate([x, 30.2, FZ]) rotate([0, 0, 180]) ap_veh_bus();
for (x = [-26 : 6 : 26]) translate([x, 29.2, FZ]) ap_prop_cone();
translate([37, 42, FZ]) rotate([0, 0, -35]) ap_prop_windsock();

// Landside overflow transfer point.
translate([-16, -17, FZ]) rotate([0, 0, 180]) ap_veh_bus([0.88, 0.88, 0.90]);
translate([4, -17, FZ]) rotate([0, 0, 180]) ap_veh_bus([0.75, 0.28, 0.24]);
translate([22, -17, FZ]) rotate([0, 0, 180]) ap_veh_taxi();
for (x = [-27, -9, 12, 28]) translate([x, -14, FZ]) ap_prop_lamp_post();

gk_camera_lookat([50, -52, 36], [0, 9, 0], "diversion-overview", 50);
gk_camera_lookat([3, 1, 2.1], [0, 18, 1.2], "recovery-hall", 60);
gk_camera_lookat([36, 31, 8], [0, 38, 2], "parked-fleet", 48);
