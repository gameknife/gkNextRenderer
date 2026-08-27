// Airport variant: compact regional terminal with two gates and a bus apron.
use <../../lib/kit_airport.scad>
use <../../lib/gk_camera.scad>

$fn = 12;
FZ = 0.15;

ap_ground_base();
ap_ground_carpet();
ap_ground_apron();
ap_ground_landside();

// Glass-fronted terminal shell, front = -y.
translate([-22, -12, FZ]) ap_wall_glass_seg(14);
translate([0, -12, FZ]) ap_wall_glass_seg(18);
translate([22, -12, FZ]) ap_wall_glass_seg(14);
translate([-30, -12, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(40);
translate([30, -12, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(40);
translate([-15, 28, FZ]) ap_wall_glass_seg(22);
translate([15, 28, FZ]) ap_wall_glass_seg(22);
translate([0, -12, FZ]) ap_furn_entrance();
translate([0, -12.3, 3.8]) ap_prop_big_sign("REGIONAL");

// Check-in and security form one short, readable passenger route.
for (i = [0 : 3])
    translate([-18 + i * 2.7, 0, FZ]) ap_furn_checkin_desk(str(i + 1));
for (i = [0 : 1])
    translate([-5 + i * 2.4, 5.2, FZ]) ap_furn_security_lane();
translate([-19, -2.0, FZ]) ap_prop_queue_line(10);
translate([-10, -3.2, FZ]) rotate([0, 0, 90]) ap_furn_kiosk();
translate([-10, -0.8, FZ]) rotate([0, 0, 90]) ap_furn_kiosk();
translate([-3.8, 2.4, FZ]) ap_prop_fids();

// Two gates, waiting lounge, local cafe and convenience corner.
translate([-12, 27.6, FZ]) ap_furn_gate_door("GATE A");
translate([12, 27.6, FZ]) ap_furn_gate_door("GATE B");
for (x = [-18, -14.5, 8, 11.5, 15])
    translate([x, 18, FZ]) rotate([0, 0, 180]) ap_furn_bench_row();
translate([-16, 11, FZ]) ap_furn_cafe_counter();
translate([-12.5, 12.3, FZ]) ap_furn_cafe_table();
translate([-9.5, 11.0, FZ]) ap_furn_cafe_table([0.25, 0.45, 0.75]);
translate([14, 10, FZ]) ap_prop_shop_portal(7, "LOCAL");
translate([11.5, 12, FZ]) ap_furn_gondola();
translate([15.0, 12, FZ]) ap_furn_gondola();
translate([17.2, 9.5, FZ]) rotate([0, 0, -90]) ap_furn_checkout();

// Apron narrative: one turboprop-sized airliner loaded by bus and tug.
translate([-13, 38, FZ]) rotate([0, 0, 184]) scale([0.78, 0.78, 0.78]) ap_veh_airliner();
translate([18, 35, FZ]) rotate([0, 0, 180]) ap_veh_bus([0.84, 0.86, 0.88]);
translate([-23, 31.5, FZ]) rotate([0, 0, 190]) ap_veh_baggage_tug();
translate([-24.5, 31.0, FZ]) rotate([0, 0, 182]) ap_veh_baggage_cart();
translate([-8, 32, FZ]) ap_veh_stairs_truck();
for (p = [[-27, 30], [-4, 30], [22, 30]]) translate([p[0], p[1], FZ]) ap_prop_light_mast();

// Landside bus interchange and planted forecourt.
translate([8, -14, FZ]) ap_prop_bus_shelter();
translate([8, -17, FZ]) rotate([0, 0, 180]) ap_veh_bus();
translate([-14, -17, FZ]) rotate([0, 0, 180]) ap_veh_taxi();
for (x = [-26, -18, 20, 28]) translate([x, -14, FZ]) ap_prop_tree();
for (x = [-22, 24]) translate([x, -12.8, FZ]) ap_prop_hedge(4.5);

gk_camera_lookat([48, -48, 34], [0, 8, 0], "regional-overview", 50);
gk_camera_lookat([-3, -7, 2.2], [-5, 15, 1.2], "passenger-route", 58);
gk_camera_lookat_key([-30, 35, 4], [-12, 38, 2], "apron-bus", 0, 52);
gk_camera_lookat_key([25, 34, 4], [5, 36, 2], "apron-bus", 7, 52);
