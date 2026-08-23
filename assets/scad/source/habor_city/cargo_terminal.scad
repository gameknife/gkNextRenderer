// Habor City variant: container terminal, customs warehouse and deep-water berth.
use <../../lib/kit_city_hd.scad>
use <../../lib/kit_city_blocks.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

gk_material([0.11, 0.22, 0.29], roughness = 0.20, metalness = 0)
    translate([0, -34, -0.20]) cube([156, 60, 0.40], center = true);
color([0.31, 0.32, 0.32]) translate([0, 18, 0]) hc_slab(156, 72, 0.24);
color([0.42, 0.40, 0.36]) translate([0, -17, 0.02]) hc_slab(156, 4, 0.28);

// Customs and logistics buildings define the landward edge.
translate([-47, 37, 0.24]) hc_bldg_warehouse(L = 34, D = 18, H = 7.5);
translate([-18, 39, 0.24]) v2_bldg_harbor_office(L = 18, D = 11);
translate([15, 38, 0.24]) hc_bldg_warehouse(L = 30, D = 18, H = 7);
translate([49, 39, 0.24]) hc_bldg_police(L = 14, D = 10);
translate([66, 30, 0.24]) hc_prop_radio_tower(h = 17);

// Container blocks leave a clear center aisle for forklifts and trucks.
for (r = [0 : 2], c = [0 : 4])
    translate([-56 + c * 11, 11 + r * 6, 0.24])
        rotate([0, 0, 90]) hc_prop_container_stack(seed = 710 + r * 7 + c, n = 1 + (r + c) % 3);
for (r = [0 : 1], c = [0 : 3])
    translate([23 + c * 12, 10 + r * 7, 0.24])
        rotate([0, 0, 90]) hc_prop_container_stack(seed = 740 + r * 5 + c, n = 2);
translate([-5, 10, 0.24]) rotate([0, 0, 90]) hc_veh_forklift();
translate([8, 22, 0.24]) rotate([0, 0, -90]) hc_veh_forklift([0.74, 0.38, 0.18]);
translate([-24, 31, 0.24]) hc_veh_truck_ct(seed = 751);
translate([24, 31, 0.24]) rotate([0, 0, 180]) hc_veh_truck_box();

// Berth equipment and a ship under active loading.
translate([-44, -13, 0.28]) hc_prop_crane(hc_ORANGEC());
translate([0, -13, 0.28]) hc_prop_crane(hc_YELLOWC());
translate([44, -13, 0.28]) hc_prop_crane(hc_REDC());
translate([4, -42, -0.50]) rotate([0, 0, 3]) hc_boat_cargo(64, [0.55, 0.22, 0.18], 752);
for (x = [-68 : 12 : 68]) translate([x, -17, 0.28]) hc_prop_bollard_pair();
for (p = [[-70, 28], [-15, 28], [55, 28]]) translate([p[0], p[1], 0.24]) hc_prop_flag(hc_ORANGEC());

gk_camera_lookat([84, -82, 54], [0, 8, 0], "cargo-terminal-overview", 46);
gk_camera_lookat([-10, -29, 5], [0, 10, 2], "crane-aisle", 52);
gk_camera_lookat([43, -55, 8], [4, -39, 3], "berth-operation", 50);
