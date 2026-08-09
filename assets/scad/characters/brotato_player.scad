// ============================================================================
// brotato_player.scad - compact ScadRig hero for Brotato3D.
//
// The rig faces -Y in SCAD space (engine +Z). The gameplay driver rotates the
// world/root toward movement and twists bone_torso toward the current aim.
// ROLECOLOR is replaced with the selected character colour at runtime.
// ============================================================================

$fn = 8;

ROLECOLOR = [1, 0, 1];
SKIN = [0.92, 0.70, 0.48];
PANTS = [0.12, 0.15, 0.22];
BOOT = [0.055, 0.06, 0.075];
GLOVE = [0.11, 0.12, 0.14];

module part_leg()
{
    color(PANTS) translate([0, 0, -0.23]) cube([0.15, 0.18, 0.46], center = true);
    color(BOOT) translate([0, -0.09, -0.46]) cube([0.18, 0.35, 0.11], center = true);
}

module part_arm()
{
    color(ROLECOLOR) translate([0, 0, -0.15]) cube([0.13, 0.15, 0.30], center = true);
    color(SKIN) translate([0, 0, -0.37]) cube([0.11, 0.13, 0.18], center = true);
    color(GLOVE) translate([0, -0.01, -0.49]) cube([0.13, 0.15, 0.11], center = true);
}

module bone_head()
{
    color(SKIN) scale([1.05, 0.96, 1.0]) sphere(r = 0.155);
    color(ROLECOLOR) translate([0, 0.02, 0.14]) scale([1.12, 1.04, 0.36]) sphere(r = 0.16);
    color(GLOVE) translate([0, -0.15, 0.01]) cube([0.075, 0.035, 0.055], center = true);
}

module bone_arm_l() { part_arm(); }
module bone_arm_r() { mirror([1, 0, 0]) part_arm(); }
module bone_leg_l() { part_leg(); }
module bone_leg_r() { mirror([1, 0, 0]) part_leg(); }

module bone_torso()
{
    color(ROLECOLOR) translate([0, 0, 0.21]) cube([0.50, 0.29, 0.42], center = true);
    color(PANTS) translate([0, 0.02, -0.02]) cube([0.50, 0.31, 0.15], center = true);
    translate([0, 0, 0.49]) bone_head();

    // Arms are authored in a compact ready pose. The entire upper-body branch
    // follows bone_torso when gameplay applies the aim-relative yaw.
    translate([-0.32, -0.02, 0.34]) rotate([-72, 0, -10]) bone_arm_l();
    translate([ 0.32, -0.02, 0.34]) rotate([-76, 0,  10]) bone_arm_r();
}

module bone_root()
{
    translate([0, 0, 0.61]) bone_torso();
    translate([-0.15, 0, 0.50]) bone_leg_l();
    translate([ 0.15, 0, 0.50]) bone_leg_r();
}

bone_root();

anim_idle = [
    ["bone_head", "rot", [[0, [0, 0, -2]], [0.8, [0, 0, 2]], [1.6, [0, 0, -2]]]],
];

// Upper-body bones are intentionally absent: gameplay owns their aim pose.
anim_walk = [
    ["bone_leg_l", "rot", [[0, [32, 0, 0]], [0.35, [-32, 0, 0]], [0.7, [32, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-32, 0, 0]], [0.35, [32, 0, 0]], [0.7, [-32, 0, 0]]]],
    ["bone_root", "pos", [[0, [0, 0, 0]], [0.175, [0, 0, 0.035]], [0.35, [0, 0, 0]],
                           [0.525, [0, 0, 0.035]], [0.7, [0, 0, 0]]]],
];
