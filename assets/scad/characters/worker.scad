// ============================================================================
// worker.scad - 工地工人（kit_char 组装角色范例）
//
// ScadRig 契约同 agent_basic：1 unit = 1 m，Z-up，root 原点落地，面朝 -Y。
// 部件全部来自 assets/scad/lib/kit_char.scad；骨架 pivot 用 ch_pivot_*()，
// 因此 clip 直接复用库函数 ch_clip_*()。
// ch_TINT() 纯品红 = 运行时换色占位（反光背心 + 手套袖）。
// ============================================================================

use <../lib/kit_char.scad>

$fn = 8;

module bone_head()
{
    ch_head_box(ch_SKIN(1));
    ch_hat_helmet();
}

module bone_arm_l() { ch_arm_gloved(ch_TINT()); }
module bone_arm_r() { mirror([1, 0, 0]) ch_arm_gloved(ch_TINT()); }

module bone_leg_l() { ch_leg_boots(ch_DENIMC()); }
module bone_leg_r() { mirror([1, 0, 0]) ch_leg_boots(ch_DENIMC()); }

module bone_torso()
{
    ch_torso_vest(ch_TINT());
    ch_acc_toolbelt();
    ch_acc_backpack();
    translate(ch_pivot_head())  bone_head();
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

// ---- 动画 clips（库函数） ----

anim_idle = ch_clip_idle();
anim_walk = ch_clip_walk();
anim_sit  = ch_clip_sit();
anim_work = ch_clip_work();
anim_wave = ch_clip_wave();
