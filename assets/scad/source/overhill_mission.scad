use <../lib/kit_overhill.scad>
include <generated/overhill_vignette.scad>

module player_truck_body() { oh_veh_truck_body(seed = 1); }
module player_wheel() { oh_veh_wheel(0.5, 0.42); }
module cargo_crate() { color([0.22, 0.14, 0.07]) cube([1.05, 1.05, 0.75], center = true); }
module mission_pickup_marker() { color([0.10, 0.28, 0.12]) difference() { cylinder(h = 0.08, r = 4.6, $fn = 48); translate([0, 0, -0.01]) cylinder(h = 0.10, r = 4.1, $fn = 48); } }
module mission_dropoff_marker() { color([0.28, 0.18, 0.04]) difference() { cylinder(h = 0.08, r = 4.6, $fn = 48); translate([0, 0, -0.01]) cylinder(h = 0.10, r = 4.1, $fn = 48); } }
module mission_fuel_marker() { color([0.06, 0.18, 0.28]) difference() { cylinder(h = 0.08, r = 4.0, $fn = 48); translate([0, 0, -0.01]) cylinder(h = 0.10, r = 3.6, $fn = 48); } }

translate([-30, 0, 0.4]) player_truck_body();
translate([-27.5, 1, 0.9]) player_wheel();
translate([-27.5, -1, 0.9]) player_wheel();
translate([-30.6, 1, 0.9]) player_wheel();
translate([-30.6, -1, 0.9]) player_wheel();
translate([-31.75, 1, 0.9]) player_wheel();
translate([-31.75, -1, 0.9]) player_wheel();
translate([-30, 0, -5]) cargo_crate();
// Hardened roadside pads, offset from building collision rather than centered inside it.
translate([27, 4, 0.04]) mission_pickup_marker();
translate([-38, 1, 0.04]) mission_dropoff_marker();
translate([16, 4, 0.04]) mission_fuel_marker();
