// Old City variant: fortified frontier garrison guarding a mountain road.
use <../../lib/kit_old_city.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

color([0.35, 0.39, 0.24]) translate([0, 0, -0.16]) cube([116, 88, 0.32], center = true);
color([0.46, 0.39, 0.27]) translate([0, 0, 0]) oc_slab(92, 68, 0.18);

// Rectangular wall circuit with a south gate and corner towers.
translate([-32, 34, 0.18]) oc_wall_run(64);
translate([-32, -34, 0.18]) oc_wall_run(25);
translate([7, -34, 0.18]) oc_wall_run(25);
translate([-46, -34, 0.18]) rotate([0, 0, 90]) oc_wall_run(68);
translate([46, -34, 0.18]) rotate([0, 0, 90]) oc_wall_run(68);
for (p = [[-46, -34], [46, -34], [-46, 34], [46, 34]])
    translate([p[0], p[1], 0.18]) oc_bldg_corner_tower();
translate([0, -34, 0.18]) oc_bldg_gatehouse("边关");

// Barracks and stores form a protected service court.
translate([-25, 18, 0.18]) rotate([0, 0, 180]) oc_bldg_barracks(seed = 951);
translate([25, 19, 0.18]) rotate([0, 0, 180]) oc_bldg_warehouse(seed = 952);
translate([0, 22, 0.18]) oc_bldg_tower_watch();
translate([-25, -3, 0.18]) oc_bldg_tent(seed = 953);
translate([-12, -4, 0.18]) oc_bldg_tent(seed = 954);
translate([19, -4, 0.18]) oc_bldg_granary();
translate([32, -5, 0.18]) oc_bldg_shed(7, 4);

// Training yard and prepared defense tell the garrison story.
for (x = [-32, -23, -14]) translate([x, -20, 0.18]) oc_prop_target();
translate([-24, -12, 0.18]) oc_prop_rack();
translate([6, -16, 0.18]) oc_prop_cart(seed = 955);
translate([14, -15, 0.18]) oc_prop_crates(seed = 956);
translate([23, -15, 0.18]) oc_prop_hay();
translate([34, -18, 0.18]) oc_prop_flag([0.72, 0.18, 0.14], 6);
for (x = [-36 : 8 : 36]) translate([x, 30, 0.18]) oc_prop_lantern();

// Mountain silhouettes outside the eastern wall.
translate([58, 17, 0]) oc_nature_hill(r = 18, h = 8);
translate([-58, 20, 0]) oc_nature_hill(r = 16, h = 7);
lay_scatter(16, -56, 56, -41, 41, seed = 960)
    lay_pick($seed) { oc_nature_pine(s = 1.2); oc_nature_rock(s = 1.0, i = $seed); }

gk_camera_lookat([64, -60, 42], [0, 0, 0], "garrison-overview", 49);
gk_camera_lookat([0, -48, 4], [0, -12, 2], "south-gate", 54);
gk_camera_lookat([-5, -8, 3], [-24, -18, 1], "training-yard", 56);
