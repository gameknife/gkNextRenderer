// ============================================================
// Ancient Chinese City - roof modules
// ============================================================

$fn = 32;

use <common.scad>

module gable_end_trim(xpos, D=12, H=4, over=1.4, t=0.28) {
    y0 = -(D/2 + over);
    y1 =  (D/2 + over);

    color(c_roof_dark())
    polyhedron(
        points = [
            [xpos - t/2, y0, 0],
            [xpos - t/2, y1, 0],
            [xpos - t/2, 0,  H],

            [xpos + t/2, y0, 0],
            [xpos + t/2, y1, 0],
            [xpos + t/2, 0,  H]
        ],
        faces = [
            [0,1,2],
            [3,5,4],
            [0,3,4,1],
            [1,4,5,2],
            [2,5,3,0]
        ]
    );
}

module gable_roof(L=20, D=12, H=4, over=1.4) {
    color(c_roof())
    polyhedron(
        points = [
            [-(L/2+over), -(D/2+over), 0],
            [ (L/2+over), -(D/2+over), 0],
            [ (L/2+over),  (D/2+over), 0],
            [-(L/2+over),  (D/2+over), 0],
            [-(L/2+over),  0,          H],
            [ (L/2+over),  0,          H]
        ],
        faces = [
            [0,1,2,3],
            [0,4,5,1],
            [3,2,5,4],
            [0,3,4],
            [1,5,2]
        ]
    );

    color(c_roof_dark())
    translate([0,0,H+0.12])
    boxc([L + over*2.1, 0.45, 0.30]);

    gable_end_trim(-(L/2+over), D, H, over);
    gable_end_trim( (L/2+over), D, H, over);

    color(c_roof_dark())
    translate([0, -(D/2+over), 0.18])
    boxc([L + over*2.1, 0.35, 0.35]);

    color(c_roof_dark())
    translate([0,  (D/2+over), 0.18])
    boxc([L + over*2.1, 0.35, 0.35]);
}

// Standalone preview.
gable_roof();
