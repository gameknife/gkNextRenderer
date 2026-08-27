// Habor City variant: marina resort with hotel promenade, beach and yacht basin.
use <../../lib/kit_city_hd.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

gk_material([0.13, 0.31, 0.39], roughness = 0.18, metalness = 0)
    translate([0, -18, -0.18]) cube([132, 76, 0.36], center = true);
color([0.36, 0.38, 0.33]) translate([0, 29, 0]) hc_slab(132, 30, 0.22);
color([0.58, 0.50, 0.34]) translate([-42, -3, 0.02]) hc_slab(44, 32, 0.18);
color([0.48, 0.46, 0.41]) translate([28, 10, 0.02]) hc_slab(68, 8, 0.22);

// Hotel and dining promenade face the marina.
translate([-32, 35, 0.22]) hc_bldg_hotel(L = 20, D = 13, F = 5);
translate([-8, 34, 0.22]) hc_bldg_cafe(L = 12, D = 9, H = 4.4);
translate([11, 34, 0.22]) hc_bldg_burger(L = 12, D = 9, H = 4.8);
translate([34, 34, 0.22]) hc_bldg_shop_unit("MARINA", hc_BLUEC(), 14, 9, 4.6);
for (x = [-48 : 12 : 48]) translate([x, 16, 0.22]) hc_prop_lamp();
for (x = [-42 : 12 : 42]) translate([x, 20, 0.22]) hc_nature_palm(s = 1.0, lean = x % 3);
for (x = [-26, -14, -2, 10, 22]) translate([x, 15, 0.22]) hc_furn_cafe_table();

// Marina fingers with mixed pleasure craft.
for (x = [-8, 12, 32, 52]) translate([x, 12, 0.24]) hc_prop_pier(len = 32, w = 3.4);
translate([-13, -12, -0.42]) rotate([0, 0, 90]) hc_boat_sail([0.28, 0.35, 0.52]);
translate([7, -18, -0.42]) rotate([0, 0, -90]) hc_boat_speed([0.86, 0.84, 0.78], false);
translate([27, -11, -0.42]) rotate([0, 0, 90]) hc_boat_sail([0.62, 0.24, 0.20]);
translate([47, -18, -0.42]) rotate([0, 0, -90]) hc_boat_speed(hc_REDC(), true);
for (x = [-5, 15, 35, 55]) translate([x, 8, 0.24]) hc_prop_lifering_post();

// West beach creates a quieter resort counterpoint to the working marina.
for (i = [0 : 5])
{
    translate([-58 + (i % 3) * 12, -8 - floor(i / 3) * 10, 0.20]) hc_beach_umbrella(i % 2 == 0 ? hc_REDC() : hc_BLUEC());
    translate([-54 + (i % 3) * 12, -10 - floor(i / 3) * 10, 0.20]) rotate([0, 0, i * 17]) hc_beach_lounger();
}
translate([-56, 3, 0.20]) hc_beach_lifeguard();
translate([-30, -22, 0.20]) hc_beach_hut(hc_TEALC());

gk_camera_lookat([72, -68, 44], [0, 8, 0], "marina-resort-overview", 48);
gk_camera_lookat([-58, -32, 5], [-45, -9, 1], "resort-beach", 55);
gk_camera_lookat_key([-5, -27, 4], [6, 7, 1], "marina-cruise", 0, 52);
gk_camera_lookat_key([58, -25, 4], [45, 8, 1], "marina-cruise", 8, 52);
