// NextDayz infected ScadRig: fixed seven-bone human skeleton, shared meshes,
// and the six clips required by the productized PVE loop.
use <../lib/kit_char.scad>
use <../lib/kit_tw.scad>

$fn = 8;

module bone_head()
{
    ch_head_slim([0.46, 0.52, 0.40]);
    color([0.18, 0.12, 0.08]) translate([0, 0.02, 0.215]) cube([0.20, 0.20, 0.025], center = true);
}

module bone_arm_l() { ch_arm_long(ch_TINT(), [0.46, 0.52, 0.40]); }
module bone_arm_r() { mirror([1, 0, 0]) ch_arm_long(ch_TINT(), [0.46, 0.52, 0.40]); }
module bone_leg_l() { ch_leg_pants([0.18, 0.21, 0.20]); }
module bone_leg_r() { mirror([1, 0, 0]) ch_leg_pants([0.18, 0.21, 0.20]); }

module bone_torso()
{
    ch_torso_shirt(ch_TINT());
    translate(ch_pivot_head()) bone_head();
    translate(ch_pivot_arm_l()) bone_arm_l();
    translate(ch_pivot_arm_r()) bone_arm_r();
}

module bone_root()
{
    translate(ch_pivot_torso()) bone_torso();
    translate(ch_pivot_leg_l()) bone_leg_l();
    translate(ch_pivot_leg_r()) bone_leg_r();
}

bone_root();

anim_idle = ch_clip_idle();
anim_walk = ch_clip_walk();
anim_run = ch_clip_walk();
anim_attack = tw_clip_attack_slash();
anim_hit = tw_clip_attack_thrust();
anim_die = tw_clip_die_fall();
