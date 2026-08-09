// ============================================================================
// next_ra_soldier.scad - NextRA low-poly rigid soldier
//
// ScadRig contract:
//   * 1 unit = 1 metre, Z-up, root origin on the ground between the boots.
//   * Character faces -Y (engine +Z). Bound height is 1.78 m.
//   * Pure magenta ROLECOLOR is the runtime faction-tint placeholder.
//   * loadout 0 = rifleman, loadout 1 = rocketeer. The standalone rig uses 0.
//
// Scene usage:
//   use <characters/next_ra_soldier.scad>
//   Define the palette variables below in the caller, then call bone_root(0/1).
// ============================================================================

$fn = 8;

ROLECOLOR  = [1.00, 0.00, 1.00];
ROLEACCENT = [0.88, 0.68, 0.16];
UNIFORM    = [0.20, 0.23, 0.22];
VEST       = [0.12, 0.14, 0.14];
METAL      = [0.18, 0.19, 0.20];
METALL     = [0.48, 0.51, 0.52];
SKIN       = [0.72, 0.52, 0.38];
BOOT       = [0.08, 0.075, 0.07];

module part_leg()
{
    color(UNIFORM) translate([0, 0, -0.33]) cube([0.15, 0.16, 0.66], center = true);
    color(VEST) translate([0, 0, -0.08]) cube([0.17, 0.18, 0.13], center = true);
    color(BOOT) translate([0, -0.055, -0.705]) cube([0.16, 0.28, 0.12], center = true);
    color(ROLECOLOR) translate([0, -0.13, -0.69]) cube([0.10, 0.035, 0.06], center = true);
}

module part_arm()
{
    color(ROLECOLOR) translate([0, 0, -0.13]) cube([0.12, 0.14, 0.26], center = true);
    color(UNIFORM) translate([0, 0, -0.34]) cube([0.105, 0.12, 0.18], center = true);
    color(SKIN) translate([0, -0.015, -0.47]) cube([0.10, 0.11, 0.10], center = true);
}

module part_rifle()
{
    color(METAL) translate([0.12, -0.25, 0.18]) rotate([12, 0, -18]) cube([0.10, 0.76, 0.10], center = true);
    color(METALL) translate([0.03, -0.58, 0.10]) rotate([12, 0, -18]) cube([0.055, 0.46, 0.055], center = true);
    color(BOOT) translate([0.22, 0.03, 0.24]) rotate([12, 0, -18]) cube([0.13, 0.28, 0.15], center = true);
    color(ROLEACCENT) translate([0.03, -0.81, 0.06]) rotate([90, 0, 0]) cylinder(h = 0.08, r = 0.045, center = true);
}

module part_rocket_launcher()
{
    color(METAL) translate([0.20, -0.20, 0.46]) rotate([90, 0, 0]) cylinder(h = 1.00, r = 0.105, center = true);
    color(METALL) translate([0.20, 0.25, 0.46]) rotate([90, 0, 0]) cylinder(h = 0.10, r1 = 0.17, r2 = 0.105, center = true);
    color(ROLEACCENT) translate([0.20, -0.68, 0.46]) rotate([90, 0, 0]) cylinder(h = 0.08, r = 0.13, center = true);
    color(BOOT) translate([0.20, -0.16, 0.25]) cube([0.07, 0.26, 0.30], center = true);
    color(ROLECOLOR)
    {
        translate([-0.16, 0.13, 0.17]) cube([0.14, 0.19, 0.38], center = true);
        translate([ 0.16, 0.13, 0.17]) cube([0.14, 0.19, 0.38], center = true);
    }
}

module bone_head()
{
    color(SKIN) translate([0, -0.005, 0.13]) cube([0.22, 0.22, 0.24], center = true);
    color(ROLECOLOR) translate([0, 0.005, 0.245]) scale([1.25, 1.18, 0.62]) sphere(r = 0.15);
    color(VEST) translate([0, -0.145, 0.235]) cube([0.27, 0.055, 0.055], center = true);
    color(METAL) translate([0, -0.126, 0.13]) cube([0.14, 0.025, 0.06], center = true);
}

module bone_arm_l() { part_arm(); }
module bone_arm_r() { mirror([1, 0, 0]) part_arm(); }
module bone_leg_l() { part_leg(); }
module bone_leg_r() { mirror([1, 0, 0]) part_leg(); }

module bone_torso(loadout = 0)
{
    color(UNIFORM) translate([0, 0, 0.25]) cube([0.39, 0.23, 0.50], center = true);
    color(ROLECOLOR) translate([0, -0.135, 0.30]) cube([0.36, 0.055, 0.34], center = true);
    color(VEST)
    {
        translate([0, -0.175, 0.27]) cube([0.26, 0.045, 0.25], center = true);
        translate([0, 0.155, 0.26]) cube([0.29, 0.09, 0.31], center = true);
        translate([-0.15, -0.175, 0.23]) cube([0.06, 0.05, 0.12], center = true);
        translate([ 0.15, -0.175, 0.23]) cube([0.06, 0.05, 0.12], center = true);
    }
    color(ROLEACCENT) translate([0, -0.205, 0.39]) cube([0.14, 0.018, 0.055], center = true);

    if (loadout == 0) part_rifle();
    else part_rocket_launcher();

    translate([0, 0, 0.56]) bone_head();
    translate([-0.255, 0, 0.48]) rotate([0, 0, -3]) bone_arm_l();
    translate([ 0.255, 0, 0.48]) rotate([0, 0,  3]) bone_arm_r();
}

module bone_root(loadout = 0)
{
    translate([0, 0, 0.88]) bone_torso(loadout);
    translate([-0.105, 0, 0.84]) bone_leg_l();
    translate([ 0.105, 0, 0.84]) bone_leg_r();
}

bone_root(0);

anim_idle = [
    ["bone_torso", "rot", [for (t = [0 : 0.25 : 2]) [t, [1.5 * sin(180 * t), 0, 0]]]],
    ["bone_head", "rot", [[0, [0, 0, -3]], [1, [0, 0, 3]], [2, [0, 0, -3]]]],
];

anim_walk = [
    ["bone_leg_l", "rot", [[0, [ 32, 0, 0]], [0.4, [-32, 0, 0]], [0.8, [ 32, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-32, 0, 0]], [0.4, [ 32, 0, 0]], [0.8, [-32, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-20, 0, 0]], [0.4, [ 20, 0, 0]], [0.8, [-20, 0, 0]]]],
    ["bone_arm_r", "rot", [[0, [ 20, 0, 0]], [0.4, [-20, 0, 0]], [0.8, [ 20, 0, 0]]]],
    ["bone_root", "pos", [[0, [0, 0, 0]], [0.2, [0, 0, 0.035]], [0.4, [0, 0, 0]],
                            [0.6, [0, 0, 0.035]], [0.8, [0, 0, 0]]]],
];

anim_fire = [
    ["loop", false],
    ["bone_torso", "rot", [[0, [2, 0, 0]], [0.08, [-4, 0, 0]], [0.22, [2, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-52, 0, -12]], [0.22, [-52, 0, -12]]]],
    ["bone_arm_r", "rot", [[0, [-58, 0, 10]], [0.22, [-58, 0, 10]]]],
];
