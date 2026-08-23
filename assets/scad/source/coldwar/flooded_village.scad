// Coldwar variant: flooded rural village split by a swollen river crossing.
use <../../lib/kit_coldwar.scad>
use <../../lib/kit_layout.scad>
use <../../lib/gk_camera.scad>

$fn = 10;

color([0.31, 0.37, 0.22]) translate([0, 0, -0.18]) cube([112, 86, 0.36], center = true);
translate([-34, 0, 0]) cw_ground_grass(L = 40, D = 78, seed = 501);
translate([34, 0, 0]) cw_ground_grass(L = 40, D = 78, seed = 502);
// River runs north-south; bridge contract spans along x.
color([0.16, 0.27, 0.31, 0.82]) translate([0, 0, 0.04]) cube([28, 86, 0.08], center = true);
translate([0, 0, 0.12]) cw_prop_bridge(L = 38, W = 8, seed = 503);
translate([-40, 0, 0.20]) cw_ground_road(L = 34, W = 7, seed = 504);
translate([40, 0, 0.20]) cw_ground_road(L = 34, W = 7, seed = 505);

// Western homes and eastern communal buildings face the raised road.
for (p = [[-42, 22, 180], [-28, 25, 180], [-42, -22, 0], [-28, -25, 0]])
    translate([p[0], p[1], 0.20]) rotate([0, 0, p[2]]) cw_bldg_house_rural(seed = p[0] + p[1]);
translate([31, 24, 0.20]) rotate([0, 0, 180]) cw_bldg_chapel(seed = 506);
translate([43, 23, 0.20]) rotate([0, 0, 180]) cw_bldg_shop_row(seed = 507, L = 10, D = 6);
translate([35, -24, 0.20]) cw_bldg_bus_stop(seed = 508);
translate([48, -23, 0.20]) cw_bldg_ruin(seed = 509);

// Evacuation attempt stalled at the bridgehead.
translate([-18, 1.2, 0.32]) rotate([0, 0, 8]) cw_veh_tractor(seed = 510);
translate([18, -1.0, 0.32]) rotate([0, 0, 174]) cw_veh_uaz_van(seed = 511);
translate([-22, 7, 0.20]) cw_prop_cart_shop(seed = 512);
translate([-27, 4, 0.20]) cw_prop_crate_ammo(seed = 513);
translate([23, -7, 0.20]) cw_item_medkit();
translate([26, -7, 0.20]) cw_item_backpack(seed = 514);
for (y = [-36 : 6 : 36])
{
    translate([-12, y, 0.12]) cw_nature_reeds(seed = y);
    translate([12, y + 2, 0.12]) cw_nature_reeds(seed = y + 1);
}

// High-ground camp and sparse wetland vegetation.
translate([42, 33, 0.20]) cw_prop_tent_military(seed = 515);
translate([36, 33, 0.20]) cw_prop_campfire(seed = 516);
lay_scatter(16, -52, 52, -38, 38, seed = 520)
    lay_pick($seed) { cw_nature_birch(s = 1.2, seed = $seed); cw_nature_tree_dead(seed = $seed); cw_nature_bush(seed = $seed); }

gk_camera_lookat([58, -58, 38], [0, 0, 0], "flooded-village-overview", 50);
gk_camera_lookat([-8, -18, 3], [0, 0, 1], "bridge-approach", 55);
gk_camera_lookat([49, 38, 8], [37, 26, 1], "evacuation-camp", 52);
