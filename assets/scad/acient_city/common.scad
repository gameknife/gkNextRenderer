// ============================================================
// Ancient Chinese City - shared parameters and primitive helpers
// ============================================================

function city_w() = 260;
function city_h() = 190;

function wall_t() = 14;
function wall_h() = 15;
function parapet_h() = 2.4;

function gate_w() = 28;

function main_road_w() = 9;
function side_road_w() = 5;

function c_ground()     = [0.60, 0.73, 0.48];
function c_road()       = [0.60, 0.54, 0.42];
function c_stone()      = [0.53, 0.50, 0.45];
function c_stone_dark() = [0.36, 0.34, 0.31];
function c_stone_lite() = [0.70, 0.66, 0.58];
function c_walkway()    = [0.64, 0.61, 0.54];

function c_wood()       = [0.42, 0.22, 0.10];
function c_wood_dark()  = [0.22, 0.11, 0.05];
function c_plaster()    = [0.84, 0.79, 0.67];

function c_roof()       = [0.55, 0.10, 0.06];
function c_roof_dark()  = [0.26, 0.06, 0.035];

function c_lantern()    = [0.86, 0.16, 0.08];
function c_flag()       = [0.72, 0.08, 0.06];
function c_green()      = [0.18, 0.42, 0.14];
function c_water()      = [0.28, 0.46, 0.60];

module boxc(s) {
    cube(s, center=true);
}

module stepped_base(L=20, D=16, H1=0.8, H2=0.5) {
    color(c_stone_dark())
    translate([0,0,H1/2])
    boxc([L,D,H1]);

    color(c_stone_lite())
    translate([0,0,H1+H2/2])
    boxc([L-1.4,D-1.4,H2]);
}

// Standalone preview.
stepped_base();
