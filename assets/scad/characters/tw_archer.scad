use <../lib/kit_char.scad>
use <../lib/kit_tw.scad>
$fn=8;
module bone_head(){ tw_head_soldier(2); }
module bone_arm_l(){ tw_arm_soldier(2, ch_TINT(), 0); }
module bone_arm_r(){ mirror([1,0,0]) tw_arm_soldier(2, ch_TINT(), 0); }
module bone_leg_l(){ tw_leg_soldier(2); }
module bone_leg_r(){ mirror([1,0,0]) tw_leg_soldier(2); }
module bone_torso(){
    tw_torso_soldier(2, ch_TINT());
    color(tw_WOOD()) translate([0.23,0.10,0.08]) rotate([90,0,0])
        cylinder(h=0.08, r=0.38, $fn=14);
    translate(ch_pivot_head()) bone_head();
    translate(ch_pivot_arm_l()) bone_arm_l();
    translate(ch_pivot_arm_r()) bone_arm_r();
}
module bone_root(){
    translate(ch_pivot_torso()) bone_torso();
    translate(ch_pivot_leg_l()) bone_leg_l();
    translate(ch_pivot_leg_r()) bone_leg_r();
}
bone_root();
anim_idle=ch_clip_idle();
anim_walk=ch_clip_walk();
anim_march=ch_clip_walk();
anim_run=ch_clip_walk();
