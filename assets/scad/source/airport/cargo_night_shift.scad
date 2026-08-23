// Airport variant: cargo apron during a dense overnight handling shift.
use <../../lib/kit_airport.scad>
use <../../lib/gk_camera.scad>

$fn = 12;
FZ = 0.15;

color([0.27, 0.28, 0.29]) translate([0, 0, -0.12]) cube([112, 82, 0.24], center = true);

// Freight terminal: a low glass office attached to four loading bays.
translate([34, -24, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(32, [0.36, 0.38, 0.40]);
translate([10, -24, FZ]) ap_wall_solid_seg(24, [0.36, 0.38, 0.40]);
translate([10, 8, FZ]) ap_wall_solid_seg(24, [0.36, 0.38, 0.40]);
translate([-2, -24, FZ]) rotate([0, 0, 90]) ap_wall_glass_seg(32);
translate([-2.2, -8, FZ]) rotate([0, 0, 90]) ap_furn_entrance();
translate([9, -17, FZ]) ap_furn_staff_desk();
translate([15, -17, FZ]) ap_prop_server_rack();
translate([24, -17, FZ]) ap_prop_big_sign("AIR CARGO");

// Container lanes and a cross-dock queue.
for (i = [0 : 5])
    translate([6 + (i % 3) * 8, -3 + floor(i / 3) * 7, FZ])
        rotate([0, 0, i % 2 == 0 ? 0 : 180]) ap_prop_container(i % 3 == 0 ? [0.54, 0.25, 0.20] : [0.31, 0.46, 0.58]);
for (i = [0 : 3])
    translate([4 + i * 8, 12, FZ]) rotate([0, 0, 180]) ap_veh_baggage_cart();
translate([0, 12, FZ]) ap_veh_baggage_tug();
translate([30, 13, FZ]) rotate([0, 0, 180]) ap_veh_fuel_truck();

// Two freighters and their service choreography.
translate([-29, 17, FZ]) rotate([0, 0, 92]) scale([0.88, 0.88, 0.88])
    ap_veh_airliner([0.82, 0.84, 0.86], [0.72, 0.34, 0.16]);
translate([-30, -17, FZ]) rotate([0, 0, 88]) scale([0.72, 0.72, 0.72])
    ap_veh_airliner([0.86, 0.88, 0.90], [0.20, 0.43, 0.62]);
translate([-11, 14, FZ]) rotate([0, 0, 90]) ap_veh_stairs_truck();
translate([-12, -20, FZ]) rotate([0, 0, 90]) ap_veh_baggage_tug();
for (i = [0 : 7])
    translate([-13 + (i % 4) * 4, -10 + floor(i / 4) * 22, FZ]) ap_prop_cone();

// Night-shift lighting, fenced customs corner, and staff parking.
for (p = [[-50, -32], [-50, 32], [-8, -32], [-8, 32], [42, -32], [42, 32]])
    translate([p[0], p[1], FZ]) ap_prop_light_mast();
for (y = [-26 : 6 : 26]) translate([49, y, FZ]) rotate([0, 0, 90]) ap_prop_fence(6);
translate([43, -20, FZ]) ap_prop_barrier_gate();
translate([43, -13, FZ]) ap_prop_container([0.62, 0.50, 0.34]);
translate([43, -5, FZ]) rotate([0, 0, 90]) ap_veh_car([0.75, 0.28, 0.24]);
translate([43, 2, FZ]) rotate([0, 0, 90]) ap_veh_car([0.28, 0.38, 0.55]);

gk_camera_lookat([58, -62, 42], [0, 0, 0], "cargo-overview", 48);
gk_camera_lookat([3, -31, 3], [20, 0, 1], "cross-dock", 55);
gk_camera_lookat_key([-45, -35, 3], [-30, -17, 2], "night-ramp", 0, 50);
gk_camera_lookat_key([-45, 35, 3], [-29, 17, 2], "night-ramp", 8, 50);
