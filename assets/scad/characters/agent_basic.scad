// ============================================================================
// agent_basic.scad - ScadRig 基础角色（AirportSim 角色代理）
//
// 约定（docs/ScadRig-Design.md）：
//   * 1 unit = 1 m，Z-up；根骨骼原点 = 两脚间地面投影（z=0）
//   * bone_ 前缀 module = 骨骼；调用点外层 translate/rotate = 绑定 pivot
//   * 顶层 anim_<name> 变量 = 动画 clip（时间秒，rot 为度，OpenSCAD rotate 语义）
//   * ROLECOLOR 纯品红 = 运行时换色占位（职业色/调色板色）
//   * 面朝 -Y（引擎空间 +Z）
//
// 在原版 OpenSCAD / ScadStudio 中直接打开即显示绑定姿态。
// ============================================================================

ROLECOLOR = [1, 0, 1];          // 换色占位（躯干 + 上臂袖子）
SKIN     = [0.92, 0.76, 0.62];
PANTS    = [0.25, 0.25, 0.30];
SHOES    = [0.15, 0.15, 0.15];
HAIR     = [0.20, 0.14, 0.10];

// ---- 部件 helper（非骨骼，几何折叠进所属骨骼） ----

module part_arm() {            // 肩 pivot 在原点，手臂垂下（-Z）
    color(ROLECOLOR) translate([0, 0, -0.14]) cube([0.10, 0.11, 0.28], center = true); // 袖子
    color(SKIN)      translate([0, 0, -0.38]) cube([0.09, 0.10, 0.22], center = true); // 小臂+手
}

module part_leg() {            // 髋 pivot 在原点，腿垂下（-Z）
    color(PANTS) translate([0, 0, -0.38])          cube([0.13, 0.14, 0.76], center = true);
    color(SHOES) translate([0, -0.04, -0.745])     cube([0.13, 0.24, 0.07], center = true); // 鞋尖朝 -Y
}

// ---- 骨骼层级 ----

module bone_head() {
    color(SKIN) translate([0, 0, 0.11])        cube([0.22, 0.22, 0.22], center = true);
    color(HAIR) translate([0, 0.02, 0.205])    cube([0.24, 0.24, 0.05], center = true);  // 头发
    color(SKIN) translate([0, -0.12, 0.08])    cube([0.05, 0.04, 0.05], center = true);  // 鼻（朝向参考）
}

module bone_arm_l() { part_arm(); }
module bone_arm_r() { mirror([1, 0, 0]) part_arm(); }   // mirror 在骨骼体内：烘进网格

module bone_leg_l() { part_leg(); }
module bone_leg_r() { mirror([1, 0, 0]) part_leg(); }

module bone_torso() {
    color(ROLECOLOR) translate([0, 0, 0.26]) cube([0.34, 0.20, 0.52], center = true);
    translate([0, 0, 0.54]) bone_head();
    translate([-0.22, 0, 0.46]) bone_arm_l();
    translate([ 0.22, 0, 0.46]) bone_arm_r();
}

module bone_root() {           // 盆骨；原点落地
    translate([0, 0, 0.84]) bone_torso();
    translate([-0.10, 0, 0.78]) bone_leg_l();
    translate([ 0.10, 0, 0.78]) bone_leg_r();
}

bone_root();

// ---- 动画 clips ----

// 待机：2s 呼吸（躯干微倾）+ 手臂微摆
anim_idle = [
    ["bone_torso", "rot", [for (t = [0 : 0.25 : 2]) [t, [2 * sin(180 * t), 0, 0]]]],
    ["bone_arm_l", "rot", [for (t = [0 : 0.5 : 2]) [t, [3 * sin(180 * t), 0,  2]]]],
    ["bone_arm_r", "rot", [for (t = [0 : 0.5 : 2]) [t, [-3 * sin(180 * t), 0, -2]]]],
];

// 行走：0.8s 腿 ±35°、臂反相 ±30°、root 上下 bob
anim_walk = [
    ["bone_leg_l", "rot", [[0, [ 35, 0, 0]], [0.4, [-35, 0, 0]], [0.8, [ 35, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-35, 0, 0]], [0.4, [ 35, 0, 0]], [0.8, [-35, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-30, 0, 0]], [0.4, [ 30, 0, 0]], [0.8, [-30, 0, 0]]]],
    ["bone_arm_r", "rot", [[0, [ 30, 0, 0]], [0.4, [-30, 0, 0]], [0.8, [ 30, 0, 0]]]],
    ["bone_root",  "pos", [[0, [0, 0, 0]], [0.2, [0, 0, 0.03]], [0.4, [0, 0, 0]],
                           [0.6, [0, 0, 0.03]], [0.8, [0, 0, 0]]]],
];

// 坐姿：单帧姿态（root 下沉、大腿前抬 90°、小臂搭膝）
anim_sit = [
    ["loop", false],
    ["bone_root",  "pos", [[0, [0, 0, -0.42]]]],
    ["bone_leg_l", "rot", [[0, [-90, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-90, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-25, 0, 0]]]],
    ["bone_arm_r", "rot", [[0, [-25, 0, 0]]]],
];

// 工作：1.5s 手臂小幅操作动作
anim_work = [
    ["bone_arm_r", "rot", [[0, [-40, 0, 0]], [0.4, [-65, 0, 0]], [0.75, [-45, 0, 0]],
                           [1.1, [-70, 0, 0]], [1.5, [-40, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-35, 0, 0]], [0.75, [-50, 0, 0]], [1.5, [-35, 0, 0]]]],
    ["bone_torso", "rot", [[0, [4, 0, 0]], [0.75, [7, 0, 0]], [1.5, [4, 0, 0]]]],
];
