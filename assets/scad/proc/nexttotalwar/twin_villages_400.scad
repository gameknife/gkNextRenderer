// NextTotalwar procedural variant: twin farming villages separated by a contested meadow.
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_overhill.scad>
use <../../lib/kit_tw.scad>
use <../../lib/gk_camera.scad>

$fn = 8;
TERR = ["gkterr1", [400, 400], [176, 176], 1281, [0, 2.0, 0.42], undef, "temperate",
    [
        ["ridge", [[-195, 155], [-90, 178], [10, 160]], 44, 12],
        ["ridge", [[20, -165], [110, -150], [195, -112]], 42, 11],
        ["river", [[-200, 18], [-72, 4], [60, 14], [200, -8]], 8, 1.3],
        ["road", [[-185, -105], [-92, -55], [0, 8], [95, 65], [185, 112]], 6],
        ["road", [[-180, 104], [-78, 70], [5, 10], [95, -62], [180, -105]], 5],
        ["pad", [-112, -68], [66, 50], 18],
        ["pad", [112, 70], [66, 50], 18],
        ["pad", [0, 8], [34, 26], 0]
    ]];
gk_terrain(TERR);

// Central bridge and trading marker.
translate([0, 9, gk_terrain_height(TERR, -14, 8)]) rotate([0, 0, 2]) oh_prop_bridge(L = 22, W = 6, seed = 1282);
ter_place(TERR, 0, 8) tw_prop_marker(seed = 1283);
ter_place(TERR, -8, 3) tw_prop_cart(seed = 1284);
ter_place(TERR, 8, 13) tw_prop_haystack(seed = 1285);

// Southwest village.
for (p = [[-130, -78, 12], [-108, -82, -8], [-121, -57, 185], [-91, -56, 170]])
    ter_place(TERR, p[0], p[1]) rotate([0, 0, p[2]]) tw_bldg_hut(seed = p[0] + p[1]);
ter_place(TERR, -143, -61) tw_bldg_watchtower(seed = 1290);
ter_place(TERR, -111, -91) tw_bldg_palisade(38, 2.5, 1291);
ter_place(TERR, -101, -68) tw_prop_banner(1292, [0.62, 0.16, 0.12]);
for (p = [[-137, -88], [-126, -91], [-91, -87]]) ter_place(TERR, p[0], p[1]) tw_prop_haystack(p[0]);

// Northeast village.
for (p = [[93, 58, 12], [116, 55, -8], [102, 80, 185], [132, 84, 170]])
    ter_place(TERR, p[0], p[1]) rotate([0, 0, p[2]]) tw_bldg_hut(seed = p[0] + p[1]);
ter_place(TERR, 143, 65) tw_bldg_watchtower(seed = 1300);
ter_place(TERR, 112, 93) tw_bldg_palisade(38, 2.5, 1301);
ter_place(TERR, 121, 68) tw_prop_banner(1302, [0.20, 0.36, 0.64]);
for (p = [[92, 91], [105, 94], [136, 89]]) ter_place(TERR, p[0], p[1]) tw_prop_haystack(p[0]);

// Tree belts frame the open central battlefield.
ter_scatter(TERR, 1310, 110, [-194, -194, 194, 194], [0, 32, 25, 4, ["grass", "grass_dark"]], rot = true, dz = 0, variants = 3, scale = [0.8, 1.2])
    lay_pick($seed) { oh_nature_pine(lay_randr($seed, 1, 1.0, 1.7), $seed); oh_nature_autumn(lay_randr($seed, 2, 1.0, 1.6), $seed); oh_nature_bush(lay_randr($seed, 3, 0.9, 1.3), $seed); }

gk_camera_lookat([-225, -220, 145], [0, 5, 0], "twin-villages-overview", 46);
gk_camera_lookat([-30, -28, 10], [0, 8, 2], "contested-meadow", 52);
