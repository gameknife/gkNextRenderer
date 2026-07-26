// ============================================================================
// nextdayz_survivor.scad - Articulated rigid-body player for NextDayz 3C.
//
// ScadRig: 1 unit = 1 metre, Z-up, faces -Y, bone_root at ground.
// The weapon is attached at runtime to the empty bone_weapon_socket. Locomotion
// is in-place; the Jolt character controller owns all world translation.
// ============================================================================

$fn = 8;

ROLECOLOR = [1.00, 0.00, 1.00];
UNIFORM   = [0.22, 0.25, 0.22];
VEST      = [0.12, 0.14, 0.12];
SKIN      = [0.72, 0.52, 0.38];
BOOT      = [0.07, 0.065, 0.06];
METAL     = [0.18, 0.19, 0.18];

module part_pelvis()
{
    color(UNIFORM) cube([0.36, 0.22, 0.22], center = true);
    color(VEST) translate([0, 0, 0.07]) cube([0.39, 0.25, 0.09], center = true);
}

module part_torso()
{
    color(UNIFORM) translate([0, 0, 0.27]) cube([0.40, 0.23, 0.54], center = true);
    color(ROLECOLOR) translate([0, -0.145, 0.30]) cube([0.37, 0.055, 0.34], center = true);
    color(VEST)
    {
        translate([0, -0.18, 0.28]) cube([0.28, 0.05, 0.28], center = true);
        translate([0, 0.155, 0.28]) cube([0.31, 0.08, 0.34], center = true);
    }
}

module part_head()
{
    color(SKIN) translate([0, -0.005, 0.12]) cube([0.22, 0.22, 0.24], center = true);
    color(ROLECOLOR) translate([0, 0.005, 0.245]) scale([1.25, 1.18, 0.62]) sphere(r = 0.15);
    color(METAL) translate([0, -0.13, 0.14]) cube([0.15, 0.025, 0.055], center = true);
}

module part_upperarm()
{
    color(ROLECOLOR) translate([0, 0, -0.15]) cube([0.13, 0.14, 0.30], center = true);
}

module part_forearm()
{
    color(UNIFORM) translate([0, 0, -0.14]) cube([0.115, 0.125, 0.28], center = true);
}

module part_hand()
{
    color(SKIN) translate([0, -0.01, -0.06]) cube([0.105, 0.11, 0.12], center = true);
}

module part_thigh()
{
    color(UNIFORM) translate([0, 0, -0.21]) cube([0.17, 0.18, 0.42], center = true);
    color(VEST) translate([0, 0, -0.05]) cube([0.18, 0.19, 0.10], center = true);
}

module part_calf()
{
    color(UNIFORM) translate([0, 0, -0.20]) cube([0.15, 0.16, 0.40], center = true);
}

module part_foot()
{
    color(BOOT) translate([0, -0.07, 0.06]) cube([0.17, 0.31, 0.12], center = true);
    color(ROLECOLOR) translate([0, -0.20, 0.07]) cube([0.11, 0.045, 0.06], center = true);
}

module bone_weapon_socket()
{
    // Empty on purpose. Runtime weapons use this pivot. Local -Y is muzzle-forward.
}

module bone_hand_l() { part_hand(); }
module bone_hand_r()
{
    mirror([1, 0, 0]) part_hand();
    translate([0, -0.06, -0.08]) bone_weapon_socket();
}

module bone_forearm_l()
{
    part_forearm();
    translate([0, 0, -0.28]) bone_hand_l();
}
module bone_forearm_r()
{
    mirror([1, 0, 0]) part_forearm();
    translate([0, 0, -0.28]) bone_hand_r();
}

module bone_upperarm_l()
{
    part_upperarm();
    translate([0, 0, -0.30]) bone_forearm_l();
}
module bone_upperarm_r()
{
    mirror([1, 0, 0]) part_upperarm();
    translate([0, 0, -0.30]) bone_forearm_r();
}

module bone_head() { part_head(); }

module bone_torso()
{
    part_torso();
    translate([0, 0, 0.62]) bone_head();
    translate([-0.255, 0, 0.51]) rotate([0, 0, -3]) bone_upperarm_l();
    translate([ 0.255, 0, 0.51]) rotate([0, 0,  3]) bone_upperarm_r();
}

module bone_foot_l() { part_foot(); }
module bone_foot_r() { mirror([1, 0, 0]) part_foot(); }

module bone_calf_l()
{
    part_calf();
    translate([0, 0, -0.40]) bone_foot_l();
}
module bone_calf_r()
{
    mirror([1, 0, 0]) part_calf();
    translate([0, 0, -0.40]) bone_foot_r();
}

module bone_thigh_l()
{
    part_thigh();
    translate([0, 0, -0.42]) bone_calf_l();
}
module bone_thigh_r()
{
    mirror([1, 0, 0]) part_thigh();
    translate([0, 0, -0.42]) bone_calf_r();
}

module bone_pelvis()
{
    part_pelvis();
    translate([0, 0, 0.02]) bone_torso();
    translate([-0.105, 0, -0.08]) bone_thigh_l();
    translate([ 0.105, 0, -0.08]) bone_thigh_r();
}

module bone_root()
{
    translate([0, 0, 0.90]) bone_pelvis();
}

bone_root();

// --- Clip helpers -----------------------------------------------------------

function nd_stand_cycle(duration, leg_x, leg_y, arm_x, torso_x, torso_z, bounce) = [
    ["bone_thigh_l", "rot", [[0, [ leg_x,  leg_y, 0]], [duration/2, [-leg_x, -leg_y, 0]],
                               [duration, [ leg_x,  leg_y, 0]]]],
    ["bone_thigh_r", "rot", [[0, [-leg_x, -leg_y, 0]], [duration/2, [ leg_x,  leg_y, 0]],
                               [duration, [-leg_x, -leg_y, 0]]]],
    ["bone_calf_l", "rot", [[0, [8, 0, 0]], [duration/2, [20, 0, 0]], [duration, [8, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [20, 0, 0]], [duration/2, [8, 0, 0]], [duration, [20, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [-arm_x, 0, -2]], [duration/2, [ arm_x, 0, 2]],
                                  [duration, [-arm_x, 0, -2]]]],
    ["bone_upperarm_r", "rot", [[0, [ arm_x, 0, 2]], [duration/2, [-arm_x, 0, -2]],
                                  [duration, [ arm_x, 0, 2]]]],
    ["bone_torso", "rot", [[0, [torso_x, 0, torso_z]], [duration, [torso_x, 0, torso_z]]]],
    ["bone_pelvis", "pos", [[0, [0, 0, 0]], [duration/4, [0, 0, bounce]],
                              [duration/2, [0, 0, 0]], [3*duration/4, [0, 0, bounce]],
                              [duration, [0, 0, 0]]]]
];

function nd_crouch_cycle(duration, swing_x, swing_y, torso_z) = [
    ["bone_pelvis", "pos", [[0, [0, 0, -0.31]], [duration/4, [0, 0, -0.295]],
                              [duration/2, [0, 0, -0.31]], [3*duration/4, [0, 0, -0.295]],
                              [duration, [0, 0, -0.31]]]],
    ["bone_torso", "rot", [[0, [14, 0, torso_z]], [duration, [14, 0, torso_z]]]],
    ["bone_thigh_l", "rot", [[0, [-48+swing_x,  swing_y, 0]],
                               [duration/2, [-48-swing_x, -swing_y, 0]],
                               [duration, [-48+swing_x, swing_y, 0]]]],
    ["bone_thigh_r", "rot", [[0, [-48-swing_x, -swing_y, 0]],
                               [duration/2, [-48+swing_x, swing_y, 0]],
                               [duration, [-48-swing_x, -swing_y, 0]]]],
    ["bone_calf_l", "rot", [[0, [92, 0, 0]], [duration/2, [78, 0, 0]], [duration, [92, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [78, 0, 0]], [duration/2, [92, 0, 0]], [duration, [78, 0, 0]]]],
    ["bone_foot_l", "rot", [[0, [-42, 0, 0]], [duration/2, [-34, 0, 0]], [duration, [-42, 0, 0]]]],
    ["bone_foot_r", "rot", [[0, [-34, 0, 0]], [duration/2, [-42, 0, 0]], [duration, [-34, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [-8, 0, -3]], [duration/2, [8, 0, 3]], [duration, [-8, 0, -3]]]],
    ["bone_upperarm_r", "rot", [[0, [8, 0, 3]], [duration/2, [-8, 0, -3]], [duration, [8, 0, 3]]]]
];

function nd_weapon_ready_pose() = [
    ["bone_torso", "rot", [[0, [2, 0, 0]]]],
    // Both arms converge on the rifle: right hand on the grip, left hand
    // supporting the receiver/fore-end. The rifle stays in a low-ready angle.
    ["bone_upperarm_l", "rot", [[0, [-52, 0, 42]]]],
    ["bone_forearm_l", "rot", [[0, [-14, 0, 14]]]],
    ["bone_hand_l", "rot", [[0, [4, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [-54, 0, -15]]]],
    ["bone_forearm_r", "rot", [[0, [-12, 0, -5]]]],
    ["bone_hand_r", "rot", [[0, [2, 0, 0]]]],
    // Cancel the right-arm chain rotation so the rifle itself remains in a
    // slight low-ready attitude instead of pointing upward with the forearm.
    ["bone_weapon_socket", "rot", [[0, [71.4, 0, 48]]]]
];

function nd_aim_pose(pitch) = [
    ["bone_torso", "rot", [[0, [pitch * 0.28, 0, 0]]]],
    // Shoulder the stock while both arms close inward around the rifle.
    ["bone_upperarm_l", "rot", [[0, [-72 + pitch * 0.70, 0, 42]]]],
    ["bone_forearm_l", "rot", [[0, [-14 + pitch * 0.20, 0, 12]]]],
    ["bone_hand_l", "rot", [[0, [4, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [-74 + pitch * 0.72, 0, -15]]]],
    ["bone_forearm_r", "rot", [[0, [-12 + pitch * 0.18, 0, -5]]]],
    ["bone_hand_r", "rot", [[0, [2, 0, 0]]]],
    // Compensate the complete non-commutative right-arm chain, then preserve
    // the requested camera pitch in the rifle's visible muzzle direction.
    ["bone_weapon_socket", "rot", [[0, [75.8 + pitch * 0.06, 0, 70.2 + pitch * 0.44]]]]
];

// --- Locomotion -------------------------------------------------------------

anim_stand_idle = [
    ["bone_torso", "rot", [[0, [0, 0, -1]], [1, [1.5, 0, 1]], [2, [0, 0, -1]]]],
    ["bone_head", "rot", [[0, [0, 0, -2]], [1, [0, 0, 2]], [2, [0, 0, -2]]]]
];

anim_stand_walk_f = nd_stand_cycle(1.00, 28,  0, 18,  2,  0, 0.018);
anim_stand_walk_b = nd_stand_cycle(1.00,-24,  0, 15, -3,  0, 0.014);
anim_stand_walk_l = nd_stand_cycle(1.00,  5,-24, 12,  0, -4, 0.014);
anim_stand_walk_r = nd_stand_cycle(1.00,  5, 24, 12,  0,  4, 0.014);

anim_stand_run_f = nd_stand_cycle(0.72, 42,  0, 28,  7,  0, 0.032);
anim_stand_run_b = nd_stand_cycle(0.72,-34,  0, 24, -6,  0, 0.026);
anim_stand_run_l = nd_stand_cycle(0.72,  8,-34, 22,  1, -7, 0.026);
anim_stand_run_r = nd_stand_cycle(0.72,  8, 34, 22,  1,  7, 0.026);

anim_stand_sprint_f = nd_stand_cycle(0.56, 58,  0, 42, 13,  0, 0.050);
anim_stand_sprint_b = nd_stand_cycle(0.56,-44,  0, 34,-10,  0, 0.038);
anim_stand_sprint_l = nd_stand_cycle(0.56, 10,-46, 32,  4,-11, 0.038);
anim_stand_sprint_r = nd_stand_cycle(0.56, 10, 46, 32,  4, 11, 0.038);

anim_crouch_idle = [
    ["bone_pelvis", "pos", [[0, [0, 0, -0.31]]]],
    ["bone_torso", "rot", [[0, [14, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [-48, 0, 0]]]],
    ["bone_thigh_r", "rot", [[0, [-48, 0, 0]]]],
    ["bone_calf_l", "rot", [[0, [85, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [85, 0, 0]]]],
    ["bone_foot_l", "rot", [[0, [-38, 0, 0]]]],
    ["bone_foot_r", "rot", [[0, [-38, 0, 0]]]]
];

anim_crouch_walk_f = nd_crouch_cycle(1.10,  12,  0,  0);
anim_crouch_walk_b = nd_crouch_cycle(1.10, -10,  0,  0);
anim_crouch_walk_l = nd_crouch_cycle(1.10,   3,-12, -5);
anim_crouch_walk_r = nd_crouch_cycle(1.10,   3, 12,  5);

// --- Jump ------------------------------------------------------------------
// World translation stays on the character controller. These clips only add
// anticipation, an airborne silhouette and landing compression to the rig.

anim_jump_up = [
    ["loop", false],
    ["bone_pelvis", "pos", [[0, [0, 0, -0.10]], [0.12, [0, 0, -0.04]], [0.24, [0, 0, 0.04]]]],
    ["bone_torso", "rot", [[0, [10, 0, 0]], [0.12, [3, 0, 0]], [0.24, [-5, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [-30, 0, -3]], [0.12, [-12, 0, -2]], [0.24, [14, 0, -1]]]],
    ["bone_thigh_r", "rot", [[0, [-30, 0, 3]], [0.12, [-12, 0, 2]], [0.24, [8, 0, 1]]]],
    ["bone_calf_l", "rot", [[0, [58, 0, 0]], [0.12, [34, 0, 0]], [0.24, [12, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [58, 0, 0]], [0.12, [34, 0, 0]], [0.24, [18, 0, 0]]]],
    ["bone_foot_l", "rot", [[0, [-28, 0, 0]], [0.24, [-8, 0, 0]]]],
    ["bone_foot_r", "rot", [[0, [-28, 0, 0]], [0.24, [-12, 0, 0]]]]
];

anim_jump_air_loop = [
    ["bone_pelvis", "pos", [[0, [0, 0, 0.04]], [0.30, [0, 0, 0.025]], [0.60, [0, 0, 0.04]]]],
    ["bone_torso", "rot", [[0, [-5, 0, 0]], [0.30, [-3, 0, 0]], [0.60, [-5, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [18, 0, -2]], [0.30, [14, 0, -1]], [0.60, [18, 0, -2]]]],
    ["bone_thigh_r", "rot", [[0, [-8, 0, 2]], [0.30, [-12, 0, 1]], [0.60, [-8, 0, 2]]]],
    ["bone_calf_l", "rot", [[0, [18, 0, 0]], [0.30, [22, 0, 0]], [0.60, [18, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [38, 0, 0]], [0.30, [34, 0, 0]], [0.60, [38, 0, 0]]]],
    ["bone_foot_l", "rot", [[0, [-10, 0, 0]], [0.60, [-10, 0, 0]]]],
    ["bone_foot_r", "rot", [[0, [-20, 0, 0]], [0.60, [-20, 0, 0]]]]
];

anim_jump_down = [
    ["loop", false],
    ["bone_pelvis", "pos", [[0, [0, 0, 0.03]], [0.12, [0, 0, -0.13]], [0.32, [0, 0, 0]]]],
    ["bone_torso", "rot", [[0, [-3, 0, 0]], [0.12, [12, 0, 0]], [0.32, [0, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [12, 0, -1]], [0.12, [-34, 0, 0]], [0.32, [0, 0, 0]]]],
    ["bone_thigh_r", "rot", [[0, [-10, 0, 1]], [0.12, [-34, 0, 0]], [0.32, [0, 0, 0]]]],
    ["bone_calf_l", "rot", [[0, [22, 0, 0]], [0.12, [62, 0, 0]], [0.32, [0, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [36, 0, 0]], [0.12, [62, 0, 0]], [0.32, [0, 0, 0]]]],
    ["bone_foot_l", "rot", [[0, [-12, 0, 0]], [0.12, [-30, 0, 0]], [0.32, [0, 0, 0]]]],
    ["bone_foot_r", "rot", [[0, [-18, 0, 0]], [0.12, [-30, 0, 0]], [0.32, [0, 0, 0]]]]
];

// --- Contextual traversal ---------------------------------------------------

anim_vault = [
    ["loop", false],
    ["bone_pelvis", "pos", [[0, [0, 0, 0]], [0.18, [0, 0, -0.10]],
                              [0.34, [0, 0, 0.05]], [0.52, [0, 0, 0]]]],
    ["bone_torso", "rot", [[0, [0, 0, 0]], [0.18, [28, 0, -5]],
                             [0.34, [12, 0, 6]], [0.52, [0, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [0, 0, 0]], [0.18, [-68, 0, -18]],
                                  [0.34, [-38, 0, -8]], [0.52, [0, 0, 0]]]],
    ["bone_forearm_l", "rot", [[0, [0, 0, 0]], [0.18, [42, 0, 0]],
                                 [0.34, [24, 0, 0]], [0.52, [0, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [0, 0, 0]], [0.18, [-58, 0, 24]],
                                  [0.34, [-28, 0, 12]], [0.52, [0, 0, 0]]]],
    ["bone_forearm_r", "rot", [[0, [0, 0, 0]], [0.18, [35, 0, 0]],
                                 [0.34, [18, 0, 0]], [0.52, [0, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [0, 0, 0]], [0.18, [-62, 0, -14]],
                               [0.34, [36, 0, -8]], [0.52, [0, 0, 0]]]],
    ["bone_calf_l", "rot", [[0, [0, 0, 0]], [0.18, [92, 0, 0]],
                              [0.34, [28, 0, 0]], [0.52, [0, 0, 0]]]],
    ["bone_thigh_r", "rot", [[0, [0, 0, 0]], [0.18, [34, 0, 12]],
                               [0.34, [-54, 0, 8]], [0.52, [0, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [0, 0, 0]], [0.18, [24, 0, 0]],
                              [0.34, [82, 0, 0]], [0.52, [0, 0, 0]]]]
];

anim_climb_up = [
    ["loop", false],
    ["bone_pelvis", "pos", [[0, [0, 0, 0]], [0.30, [0, 0, -0.08]],
                              [0.58, [0, 0, -0.18]], [0.86, [0, 0, 0]]]],
    ["bone_torso", "rot", [[0, [0, 0, 0]], [0.30, [18, 0, 0]],
                             [0.58, [42, 0, 0]], [0.86, [0, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [0, 0, 0]], [0.20, [-112, 0, -12]],
                                  [0.58, [-76, 0, -8]], [0.86, [0, 0, 0]]]],
    ["bone_forearm_l", "rot", [[0, [0, 0, 0]], [0.20, [18, 0, 0]],
                                 [0.58, [72, 0, 0]], [0.86, [0, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [0, 0, 0]], [0.20, [-112, 0, 12]],
                                  [0.58, [-76, 0, 8]], [0.86, [0, 0, 0]]]],
    ["bone_forearm_r", "rot", [[0, [0, 0, 0]], [0.20, [18, 0, 0]],
                                 [0.58, [72, 0, 0]], [0.86, [0, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [0, 0, 0]], [0.30, [-58, 0, -4]],
                               [0.58, [-70, 0, -3]], [0.86, [0, 0, 0]]]],
    ["bone_calf_l", "rot", [[0, [0, 0, 0]], [0.30, [84, 0, 0]],
                              [0.58, [102, 0, 0]], [0.86, [0, 0, 0]]]],
    ["bone_thigh_r", "rot", [[0, [0, 0, 0]], [0.30, [-28, 0, 4]],
                               [0.58, [-48, 0, 3]], [0.86, [0, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [0, 0, 0]], [0.30, [48, 0, 0]],
                              [0.58, [78, 0, 0]], [0.86, [0, 0, 0]]]]
];

// --- Upper-body layers ------------------------------------------------------

anim_weapon_ready      = nd_weapon_ready_pose();
anim_aim_rifle_down   = nd_aim_pose( 22);
anim_aim_rifle_center = nd_aim_pose(  0);
anim_aim_rifle_up     = nd_aim_pose(-24);

anim_reload_rifle = [
    ["loop", false],
    ["bone_torso", "rot", [[0, [2, 0, 0]], [0.55, [5, 0, -3]], [1.45, [5, 0, -3]], [2.2, [2, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [-52, 0, 42]], [0.45, [-34, 0, 55]],
                                  [1.15, [-18, 0, 62]], [1.65, [-46, 0, 48]], [2.2, [-52, 0, 42]]]],
    ["bone_forearm_l", "rot", [[0, [-14, 0, 14]], [0.45, [48, 0, 18]],
                                 [1.15, [72, 0, 12]], [1.65, [8, 0, 14]], [2.2, [-14, 0, 14]]]],
    ["bone_hand_l", "rot", [[0, [4, 0, 0]], [0.45, [18, 0, -10]],
                              [1.15, [-12, 0, 8]], [1.65, [10, 0, 0]], [2.2, [4, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [-54, 0, -15]], [1.1, [-58, 0, -13]], [2.2, [-54, 0, -15]]]],
    ["bone_forearm_r", "rot", [[0, [-12, 0, -5]], [1.1, [-16, 0, -4]], [2.2, [-12, 0, -5]]]],
    ["bone_hand_r", "rot", [[0, [2, 0, 0]], [1.1, [5, 0, 0]], [2.2, [2, 0, 0]]]],
    ["bone_weapon_socket", "rot", [[0, [71.4, 0, 48]], [1.1, [75, 0, 50]], [2.2, [71.4, 0, 48]]]]
];

anim_switch_weapon = [
    ["loop", false],
    ["bone_torso", "rot", [[0, [2, 0, 0]], [0.45, [4, 0, 8]], [0.9, [2, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [-52, 0, 42]], [0.45, [-20, 0, 28]], [0.9, [-52, 0, 42]]]],
    ["bone_forearm_l", "rot", [[0, [-14, 0, 14]], [0.45, [35, 0, 8]], [0.9, [-14, 0, 14]]]],
    ["bone_hand_l", "rot", [[0, [4, 0, 0]], [0.45, [12, 0, 0]], [0.9, [4, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [-54, 0, -15]], [0.45, [32, 0, -38]], [0.9, [-54, 0, -15]]]],
    ["bone_forearm_r", "rot", [[0, [-12, 0, -5]], [0.45, [28, 0, -18]], [0.9, [-12, 0, -5]]]],
    ["bone_hand_r", "rot", [[0, [2, 0, 0]], [0.45, [-18, 0, 12]], [0.9, [2, 0, 0]]]],
    ["bone_weapon_socket", "rot", [[0, [71.4, 0, 48]], [0.45, [8, 0, 12]], [0.9, [71.4, 0, 48]]]]
];

anim_recoil_rifle = [
    ["loop", false],
    ["bone_torso", "rot", [[0, [0, 0, 0]], [0.055, [-4, 0, 0]], [0.20, [0, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [0, 0, 0]], [0.055, [5, 0, 0]], [0.20, [0, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [0, 0, 0]], [0.055, [7, 0, 0]], [0.20, [0, 0, 0]]]],
    ["bone_weapon_socket", "rot", [[0, [0, 0, 0]], [0.055, [9, 0, 0]], [0.20, [0, 0, 0]]]]
];

anim_loot_ground = [
    ["loop", false],
    ["bone_pelvis", "pos", [[0, [0, 0, 0]], [0.28, [0, 0, -0.16]], [0.62, [0, 0, -0.16]],
                              [0.90, [0, 0, 0]]]],
    ["bone_torso", "rot", [[0, [0, 0, 0]], [0.32, [58, 0, 0]], [0.60, [58, 0, 0]],
                              [0.90, [0, 0, 0]]]],
    ["bone_thigh_l", "rot", [[0, [0, 0, 0]], [0.32, [-24, 0, 0]], [0.60, [-24, 0, 0]],
                               [0.90, [0, 0, 0]]]],
    ["bone_thigh_r", "rot", [[0, [0, 0, 0]], [0.32, [-24, 0, 0]], [0.60, [-24, 0, 0]],
                               [0.90, [0, 0, 0]]]],
    ["bone_calf_l", "rot", [[0, [0, 0, 0]], [0.32, [44, 0, 0]], [0.60, [44, 0, 0]],
                              [0.90, [0, 0, 0]]]],
    ["bone_calf_r", "rot", [[0, [0, 0, 0]], [0.32, [44, 0, 0]], [0.60, [44, 0, 0]],
                              [0.90, [0, 0, 0]]]],
    ["bone_upperarm_l", "rot", [[0, [0, 0, 0]], [0.32, [-30, 0, -8]], [0.60, [-30, 0, -8]],
                                  [0.90, [0, 0, 0]]]],
    ["bone_upperarm_r", "rot", [[0, [0, 0, 0]], [0.32, [-35, 0, 8]], [0.60, [-35, 0, 8]],
                                  [0.90, [0, 0, 0]]]]
];
