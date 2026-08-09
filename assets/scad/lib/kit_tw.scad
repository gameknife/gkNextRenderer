// kit_tw.scad — NextTotalwar 低模战场与角色零件库。
// 纯零件库；落地件底面 z=0，带朝向件 front=-y。

use <kit_char.scad>

function tw_WOOD() = [0.30, 0.20, 0.12];
function tw_WOOD_DARK() = [0.18, 0.11, 0.07];
function tw_STONE() = [0.34, 0.34, 0.31];
function tw_METAL() = [0.20, 0.22, 0.23];
function tw_LEATHER() = [0.28, 0.16, 0.09];
function tw_CLOTH() = ch_TINT();
function tw_sq(x) = (x * x + x * 613 + 47) % 65521;
function tw_rnd(s, m) = tw_sq(tw_sq(((s % 65521) + 65521) % 65521) + 17) % m;

module tw_boxc(s) cube(s, center=true);
module tw_slab(L=4, D=4, t=0.2) translate([0, 0, t/2]) tw_boxc([L, D, t]);

module tw_bldg_palisade(L=8, H=2.7, seed=0)
{
    n = max(3, floor(L / 0.42));
    for (i=[0:n-1])
        color(i % 3 == 0 ? tw_WOOD_DARK() : tw_WOOD())
            translate([-L/2 + (i+0.5)*L/n, 0, H/2])
                cylinder(h=H, r=0.22, $fn=6, center=true);
    color(tw_WOOD_DARK()) for (z=[0.75, 1.8])
        translate([0, 0.12, z]) tw_boxc([L+0.15, 0.16, 0.14]);
}

module tw_bldg_watchtower(seed=0)
{
    H = 6.0;
    for (sx=[-1,1], sy=[-1,1])
        color(tw_WOOD_DARK())
            translate([sx*1.05, sy*1.05, H/2])
                rotate([sy*5, sx*-5, 0]) tw_boxc([0.20, 0.20, H]);
    color(tw_WOOD()) translate([0, 0, H]) tw_slab(3.4, 3.4, 0.22);
    color(tw_WOOD()) translate([0, 0, H+0.22]) tw_slab(2.5, 2.5, 1.25);
    color([0.22, 0.13, 0.08]) translate([0, 0, H+1.47])
        cylinder(h=1.15, r1=2.0, r2=0.15, $fn=4);
    color(tw_WOOD()) for (i=[0:7])
        translate([0, -1.28, 0.45+i*0.65]) tw_boxc([0.75, 0.08, 0.07]);
}

module tw_bldg_gatehouse(W=6, seed=0)
{
    color(tw_STONE()) for (sx=[-1,1])
        translate([sx*(W/2+1.0), 0, 1.8]) tw_boxc([1.8, 2.4, 3.6]);
    color(tw_WOOD()) translate([0, 0, 3.1]) tw_boxc([W, 1.1, 0.55]);
    color(tw_WOOD_DARK()) for (x=[-W/2+0.3:0.55:W/2-0.3])
        translate([x, 0, 1.35]) tw_boxc([0.16, 0.28, 2.7]);
}

module tw_bldg_hut(seed=0)
{
    color(tw_WOOD()) tw_slab(4.8, 3.8, 2.2);
    color([0.25, 0.15, 0.09]) translate([0, 0, 2.2])
        rotate([0, 0, 45]) cylinder(h=1.6, r1=3.25, r2=0.15, $fn=4);
    color([0.08, 0.07, 0.06]) translate([0, -1.94, 1.0]) tw_boxc([0.85, 0.08, 1.7]);
}

module tw_prop_banner(seed=0, tint=ch_TINT())
{
    color(tw_WOOD_DARK()) translate([0, 0, 1.9]) cylinder(h=3.8, r=0.055, $fn=6, center=true);
    color(tint) translate([0.58, 0, 3.05]) tw_boxc([1.15, 0.035, 0.85]);
    color(tw_METAL()) translate([0, 0, 3.86]) sphere(r=0.11, $fn=6);
}

module tw_prop_stakes(seed=0)
{
    for (i=[-1,0,1])
        color(tw_WOOD_DARK()) translate([i*1.1, 0, 0.88])
            rotate([45, 0, 0]) tw_boxc([0.13, 2.3, 0.13]);
    color(tw_WOOD()) translate([0, 0, 0.65]) tw_boxc([3.3, 0.15, 0.15]);
}

module tw_prop_cart(seed=0)
{
    color(tw_WOOD()) translate([0, 0, 0.75]) tw_boxc([2.2, 1.25, 0.18]);
    color(tw_WOOD_DARK()) for (sy=[-1,1])
        translate([-0.65, sy*0.72, 0.52]) rotate([90, 0, 0])
            cylinder(h=0.14, r=0.52, $fn=10, center=true);
    color(tw_WOOD_DARK()) for (sy=[-1,1])
        translate([1.65, sy*0.38, 1.10]) rotate([0, 75, 0]) tw_boxc([2.2, 0.10, 0.10]);
}

module tw_prop_haystack(seed=0)
{
    color([0.54, 0.38, 0.12]) cylinder(h=1.35, r1=1.25, r2=0.68, $fn=8);
    color([0.64, 0.48, 0.16]) translate([0, 0, 1.35])
        cylinder(h=0.65, r1=0.68, r2=0.08, $fn=8);
}

module tw_prop_marker(seed=0)
{
    color(ch_TINT()) tw_slab(1.6, 1.6, 0.045);
    color([0.85, 0.72, 0.22]) translate([0, 0, 0.05])
        cylinder(h=0.035, r=0.48, $fn=16);
}

module tw_head_soldier(style=0)
{
    ch_head_box(ch_SKIN(style % 3));
    color(style == 2 ? tw_LEATHER() : tw_METAL())
        translate([0, 0, 0.27]) cylinder(h=0.15, r1=0.20, r2=0.14, $fn=8);
}

module tw_torso_soldier(style=0, tint=ch_TINT())
{
    color(tint) translate([0, 0, -0.06]) tw_boxc([0.43, 0.25, 0.60]);
    color(style == 1 ? tw_METAL() : tw_LEATHER())
        translate([0, 0.07, -0.05]) tw_boxc([0.34, 0.08, 0.48]);
}

module tw_arm_soldier(style=0, tint=ch_TINT(), weapon=0)
{
    ch_arm_long(tint);
    if (weapon == 1)
        color(tw_WOOD_DARK()) translate([0, -0.07, -0.55]) cylinder(h=2.2, r=0.035, $fn=6);
    if (weapon == 2)
        color(tw_METAL()) translate([0, -0.10, -0.42]) rotate([8, 0, 0])
            tw_boxc([0.07, 0.06, 0.92]);
    if (weapon == 3)
        color(tw_WOOD()) translate([0, -0.08, -0.38])
            rotate([0, 90, 0]) cylinder(h=0.48, r=0.30, $fn=12, center=true);
}

module tw_leg_soldier(style=0) { ch_leg_boots([0.25, 0.22, 0.18], tw_LEATHER()); }

// Looping melee clips share the standard seven-bone soldier skeleton.
function tw_clip_attack_thrust() = [
    ["bone_arm_r", "rot", [[0.00, [-72, 0, -6]], [0.28, [-108, 0, -3]],
                            [0.48, [-108, 0, -3]], [0.90, [-72, 0, -6]]]],
    ["bone_arm_l", "rot", [[0.00, [-48, 0, 12]], [0.28, [-66, 0, 7]],
                            [0.48, [-66, 0, 7]], [0.90, [-48, 0, 12]]]],
    ["bone_torso", "rot", [[0.00, [2, 0, 0]], [0.28, [-10, 0, 0]],
                            [0.48, [-10, 0, 0]], [0.90, [2, 0, 0]]]],
];

function tw_clip_attack_slash() = [
    ["bone_arm_r", "rot", [[0.00, [-48, 0, -48]], [0.25, [-132, 0, 34]],
                            [0.52, [-82, 0, 58]], [0.95, [-48, 0, -48]]]],
    ["bone_arm_l", "rot", [[0.00, [-24, 0, 18]], [0.52, [-52, 0, -12]],
                            [0.95, [-24, 0, 18]]]],
    ["bone_torso", "rot", [[0.00, [0, 0, -8]], [0.25, [3, 0, 10]],
                            [0.52, [6, 0, 16]], [0.95, [0, 0, -8]]]],
];

function tw_clip_die_fall() = [
    ["loop", false],
    ["bone_root", "rot", [[0.00, [0, 0, 0]], [0.18, [-8, 0, 0]],
                           [0.55, [-58, 0, 0]], [0.80, [-85, 0, 0]]]],
    ["bone_root", "pos", [[0.00, [0, 0, 0]], [0.55, [0, 0, -0.03]],
                           [0.80, [0, 0, -0.08]]]],
    ["bone_arm_l", "rot", [[0.00, [0, 0, 0]], [0.80, [-28, 0, -62]]]],
    ["bone_arm_r", "rot", [[0.00, [0, 0, 0]], [0.80, [-18, 0, 68]]]],
    ["bone_leg_l", "rot", [[0.00, [0, 0, 0]], [0.80, [18, 0, -22]]]],
    ["bone_leg_r", "rot", [[0.00, [0, 0, 0]], [0.80, [-12, 0, 26]]]],
];
