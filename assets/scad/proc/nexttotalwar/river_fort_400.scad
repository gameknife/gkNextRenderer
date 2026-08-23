// NextTotalwar procedural variant: river fort controlling the only reliable crossing.
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_overhill.scad>
use <../../lib/kit_tw.scad>
use <../../lib/gk_camera.scad>

$fn = 8;
TERR = ["gkterr1", [400, 400], [176, 176], 1201, [0, 2.4, 0.46], undef, "temperate",
    [
        ["ridge", [[-195, 135], [-80, 160], [30, 142]], 54, 18],
        ["ridge", [[45, -150], [125, -125], [195, -90]], 48, 15],
        ["river", [[-15, 200], [5, 80], [-8, -40], [18, -200]], 11, 2.0],
        ["road", [[-200, -18], [-70, -5], [65, 2], [200, 18]], 7],
        ["road", [[-30, 190], [-16, 80], [-8, -40], [15, -190]], 5],
        ["pad", [54, 44], [84, 62], 4],
        ["pad", [-138, -58], [42, 32], -6]
    ]];
gk_terrain(TERR);

// Road bridge and fortified eastern bridgehead.
translate([-3, -1, gk_terrain_height(TERR, -24, -2)]) rotate([0, 0, 3]) oh_prop_bridge(L = 30, W = 7, seed = 1202);
ter_place(TERR, 54, 44) rotate([0, 0, 4]) tw_bldg_gatehouse(W = 8, seed = 1203);
for (x = [18 : 8 : 90]) ter_place(TERR, x, 73) tw_bldg_palisade(8, 3, x);
for (x = [18 : 8 : 90]) ter_place(TERR, x, 15) tw_bldg_palisade(8, 3, x + 1);
for (y = [23 : 8 : 65])
{
    ter_place(TERR, 14, y) rotate([0, 0, 90]) tw_bldg_palisade(8, 3, y);
    ter_place(TERR, 94, y) rotate([0, 0, 90]) tw_bldg_palisade(8, 3, y + 1);
}
for (p = [[16, 17], [92, 17], [16, 71], [92, 71]]) ter_place(TERR, p[0], p[1]) tw_bldg_watchtower(p[0] + p[1]);
ter_place(TERR, 45, 47) tw_bldg_hut(seed = 1204);
ter_place(TERR, 70, 47) tw_bldg_hut(seed = 1205);
ter_place(TERR, 57, 60) tw_prop_banner(seed = 1206, tint = [0.66, 0.15, 0.11]);

// Western supply village creates the attacker spawn narrative.
ter_place(TERR, -148, -55) tw_bldg_hut(seed = 1210);
ter_place(TERR, -132, -51) oh_bldg_cabin(seed = 1211, L = 7, D = 5);
ter_place(TERR, -144, -68) tw_prop_cart(seed = 1212);
ter_place(TERR, -128, -66) tw_prop_haystack(seed = 1213);
ter_place(TERR, -160, -62) tw_bldg_watchtower(seed = 1214);

ter_scatter(TERR, 1220, 150, [-194, -194, 194, 194], [0, 40, 28, 4, ["grass", "grass_dark"]], rot = true, dz = 0, variants = 2, scale = [0.8, 1.25])
    lay_pick($seed) { oh_nature_pine(lay_randr($seed, 1, 1.1, 1.9), $seed); oh_nature_autumn(lay_randr($seed, 2, 1.0, 1.7), $seed); }
ter_scatter(TERR, 1221, 38, [-194, -194, 194, 194], [2, 48, 55, 2, ["rock", "rock_high"]], rot = true)
    oh_rock_boulder(lay_randr($seed, 3, 0.8, 1.6), $seed);

gk_camera_lookat([-225, -220, 145], [0, 5, 0], "river-fort-overview", 46);
gk_camera_lookat([-48, -20, 12], [18, 20, 4], "bridgehead", 52);
