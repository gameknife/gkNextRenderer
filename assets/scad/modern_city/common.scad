// ============================================================
// Modern City Diorama - 共享参数 / 配色 / 基础工具
// 单位近似：1 unit ≈ 1 米
// ============================================================

$fn = 24;

// ---- 地块总体尺寸 ----
function tile_w() = 230;     // X 方向
function tile_d() = 170;     // Y 方向
function base_h() = 2.2;     // 展台底座厚度

// ---- 道路网格 ----
function road_w() = 10;      // 双车道宽
function road_t() = 0.15;    // 路面厚
function curb_h() = 0.35;    // 街区台面(人行道)高
function vroads() = [-70, 0, 70];   // 纵向道路中心 x
function hroads() = [-40, 30];      // 横向道路中心 y

// ---- 配色（低多边形卡通风）----
function c_base()   = [0.78, 0.79, 0.80];
function c_pave()   = [0.88, 0.88, 0.86];
function c_road()   = [0.27, 0.29, 0.31];
function c_lot()    = [0.45, 0.47, 0.49];
function c_mark()   = [0.93, 0.93, 0.92];
function c_grass()  = [0.55, 0.78, 0.30];
function c_leaf()   = [0.45, 0.72, 0.22];
function c_leaf_d() = [0.33, 0.60, 0.18];
function c_trunk()  = [0.42, 0.28, 0.16];

function c_white()  = [0.95, 0.95, 0.94];
function c_cream()  = [0.95, 0.90, 0.77];
function c_beige()  = [0.90, 0.81, 0.66];
function c_grey()   = [0.63, 0.65, 0.67];
function c_dgrey()  = [0.36, 0.38, 0.40];
function c_dark()   = [0.16, 0.17, 0.18];

function c_red()    = [0.86, 0.22, 0.18];
function c_blue()   = [0.27, 0.56, 0.84];
function c_dblue()  = [0.15, 0.32, 0.55];
function c_orange() = [0.92, 0.58, 0.24];
function c_brown()  = [0.35, 0.23, 0.14];
function c_yellow() = [0.96, 0.78, 0.18];
function c_glass()  = [0.52, 0.72, 0.90];

// ---- 基础工具 ----
module boxc(s) { cube(s, center=true); }

// 置于 z=0 之上的平板
module slab(L, W, t) {
    translate([0, 0, t/2]) boxc([L, W, t]);
}

// 低多边形球（树冠等）
module blob(r) { sphere(r=r, $fn=8); }

// 屋顶女儿墙
module parapet(L, D, t=0.5, h=0.6) {
    difference() {
        translate([0, 0, h/2]) boxc([L, D, h]);
        translate([0, 0, h/2]) boxc([L - 2*t, D - 2*t, h + 0.2]);
    }
}

// 屋顶空调外机
module ac_unit() {
    color(c_grey())  translate([0, 0, 0.5]) boxc([1.7, 1.2, 1.0]);
    color(c_dgrey()) translate([0.35, 0, 1.02]) cylinder(h=0.08, r=0.42, $fn=12);
    color(c_dgrey()) translate([-0.55, 0, 1.02]) boxc([0.35, 0.9, 0.06]);
}

// 条纹遮阳棚：挂点在本地原点，向 -Y 出挑并下倾
module awning(W=8, D=1.7, ca=[0.86,0.22,0.18], cb=[0.95,0.95,0.94], n=6) {
    rotate([18, 0, 0])
    for (i = [0 : n-1]) {
        color(i % 2 == 0 ? ca : cb)
        translate([-W/2 + (i + 0.5)*W/n, -D/2, 0])
        boxc([W/n + 0.02, D, 0.14]);
    }
}

// 双坡屋顶（小住宅用）
module gable_roof(L=10, D=8, H=2.6, over=0.8, c=[0.86,0.22,0.18]) {
    color(c)
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
}

// 标准预览
translate([-8, 0, 0]) ac_unit();
awning();
translate([10, 0, 2]) gable_roof();
