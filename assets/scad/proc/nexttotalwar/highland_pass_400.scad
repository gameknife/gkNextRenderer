// NextTotalwar procedural variant: narrow highland pass contested by two hill camps.
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_overhill.scad>
use <../../lib/kit_tw.scad>
use <../../lib/gk_camera.scad>

$fn = 8;
TERR = ["gkterr1", [400, 400], [176, 176], 1241, [2, 3.0, 0.54], undef, "alpine",
    [
        ["mountain", [-120, 78], 120, 48, 0.62],
        ["mountain", [118, -72], 118, 45, 0.58],
        ["ridge", [[-195, -120], [-70, -25], [0, 10]], 52, 20],
        ["ridge", [[0, 10], [75, 65], [195, 130]], 48, 19],
        ["road", [[-200, -155], [-105, -83], [-15, 0], [80, 76], [200, 150]], 7],
        ["pad", [-88, -66], [52, 38], 38],
        ["pad", [86, 78], [52, 38], 38],
        ["pad", [0, 5], [32, 24], 38]
    ]];
gk_terrain(TERR);

// Opposing camps face each other across the center pass.
ter_place(TERR, -88, -66) rotate([0, 0, 38]) tw_bldg_gatehouse(7, 1242);
ter_place(TERR, -101, -57) tw_bldg_hut(1243);
ter_place(TERR, -72, -77) tw_bldg_watchtower(1244);
ter_place(TERR, -94, -82) tw_bldg_palisade(34, 2.8, 1245);
ter_place(TERR, -83, -55) tw_prop_banner(1246, [0.64, 0.15, 0.12]);

ter_place(TERR, 86, 78) rotate([0, 0, 218]) tw_bldg_gatehouse(7, 1250);
ter_place(TERR, 72, 87) tw_bldg_hut(1251);
ter_place(TERR, 101, 68) tw_bldg_watchtower(1252);
ter_place(TERR, 80, 61) tw_bldg_palisade(34, 2.8, 1253);
ter_place(TERR, 90, 90) tw_prop_banner(1254, [0.18, 0.34, 0.62]);

// Neutral caravan wreck in the pass is the scenario objective.
ter_place(TERR, 0, 5) rotate([0, 0, 38]) tw_prop_cart(seed = 1260);
ter_place(TERR, -7, 0) tw_prop_haystack(seed = 1261);
ter_place(TERR, 8, 10) tw_prop_stakes(seed = 1262);
ter_place(TERR, -18, -8) oh_prop_campfire(seed = 1263);

ter_scatter(TERR, 1270, 130, [-195, -195, 195, 195], [2, 42, 30, 3, ["grass", "grass_dark", "rock"]], rot = true, dz = 0, variants = 3, scale = [0.8, 1.3])
    lay_pick($seed) { oh_nature_pine(lay_randr($seed, 1, 1.1, 1.9), $seed); oh_rock_boulder(lay_randr($seed, 2, 0.8, 1.6), $seed); oh_nature_bush(lay_randr($seed, 3, 0.9, 1.4), $seed); }

gk_camera_lookat([-230, -225, 155], [0, 0, 10], "highland-pass-overview", 46);
gk_camera_lookat([-35, -28, 14], [0, 5, 5], "caravan-objective", 52);
gk_camera_lookat_key([-150, -112, 8], [-110, -82, 5], "pass-march", 0, 54);
gk_camera_lookat_key([140, 118, 9], [100, 87, 5], "pass-march", 12, 54);
