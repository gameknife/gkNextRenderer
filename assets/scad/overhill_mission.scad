use <lib/kit_overhill.scad>
include <gen/overhill_vignette.scad>

module player_truck_body() { oh_veh_truck_body(seed = 1); }
module player_wheel() { oh_veh_wheel(0.5, 0.42); }
module cargo_crate() { color([0.22, 0.14, 0.07]) cube([1.05, 1.05, 0.75], center = true); }

translate([-30, 0, 0.4]) player_truck_body();
translate([-27.5, 1, 0.9]) player_wheel();
translate([-27.5, -1, 0.9]) player_wheel();
translate([-30.6, 1, 0.9]) player_wheel();
translate([-30.6, -1, 0.9]) player_wheel();
translate([-31.75, 1, 0.9]) player_wheel();
translate([-31.75, -1, 0.9]) player_wheel();
translate([-30, 0, -5]) cargo_crate();
