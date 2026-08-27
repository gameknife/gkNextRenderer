// Coldwar procedural variant: high-altitude listening post and winding supply road.
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_coldwar.scad>
use <../../lib/gk_camera.scad>

$fn = 9;

TERR = ["gkterr1", [240, 240], [144, 144], 541, [1, 3.0, 0.52], undef, "alpine",
    [
        ["mountain", [60, 62], 78, 38, 0.55],
        ["ridge", [[-115, 70], [-35, 88], [45, 70], [110, 38]], 42, 18],
        ["plateau", [48, 54], 34, 8],
        ["road", [[-118, -86], [-58, -44], [-12, 2], [48, 48]], 7],
        ["pad", [48, 54], [56, 42], -8],
        ["pad", [-54, -42], [28, 20], 24]
    ]];

gk_terrain(TERR);

// Summit compound.
ter_place(TERR, 48, 54) rotate([0, 0, 172]) cw_bldg_bunker(seed = 542);
ter_place(TERR, 63, 63) cw_prop_antenna(seed = 543);
ter_place(TERR, 32, 66) cw_bldg_guard_tower(seed = 544);
ter_place(TERR, 51, 38) rotate([0, 0, 172]) cw_bldg_barracks(seed = 545, L = 13, D = 6);
ter_place(TERR, 69, 45) cw_prop_searchlight(seed = 546);
for (x = [24 : 6 : 72]) ter_place(TERR, x, 76) cw_prop_fence_barbed(len = 6);

// Mid-slope relay and stalled resupply convoy.
ter_place(TERR, -54, -42) cw_prop_tent_military(seed = 550, L = 5, D = 3.5);
ter_place(TERR, -65, -38) cw_prop_antenna(seed = 551);
ter_place(TERR, -42, -47) rotate([0, 0, 25]) cw_veh_truck_canvas(seed = 552);
ter_place(TERR, -16, -2) rotate([0, 0, 42]) cw_veh_uaz_van(seed = 553);
ter_place(TERR, 15, 24) rotate([0, 0, 42]) cw_veh_wreck(seed = 554);
ter_place(TERR, -58, -49) cw_prop_crate_ammo(seed = 555);
ter_place(TERR, -51, -49) cw_item_radio(seed = 556);

// Wind-beaten tree line leaves the summit silhouette readable.
ter_scatter(TERR, 560, 74, [-112, -112, 112, 112], [0, 26, 30, 3, ["grass", "grass_dark", "rock"]], rot = true, dz = 0, variants = 3, scale = [0.8, 1.25])
    lay_pick($seed) {
        cw_nature_pine(s = lay_randr($seed, 3, 1.0, 1.7), seed = $seed);
        cw_nature_tree_dead(s = lay_randr($seed, 4, 0.9, 1.4), seed = $seed);
        cw_nature_rock(s = lay_randr($seed, 5, 0.8, 1.5), seed = $seed);
    }

gk_camera_lookat([-128, -126, 86], [22, 25, 8], "listening-post-overview", 48);
gk_camera_lookat([9, 7, 17], [49, 54, 10], "summit-road", 52);
gk_camera_lookat_key([-104, -82, 8], [-70, -54, 5], "supply-climb", 0, 54);
gk_camera_lookat_key([28, 36, 14], [52, 55, 10], "supply-climb", 10, 54);
