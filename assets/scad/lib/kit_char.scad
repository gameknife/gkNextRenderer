// ============================================================================
// kit_char.scad - ScadRig 角色件库（prefix ch_，scaleClass human）
//
// 与其它 kit 相同的 use <> 语义：顶层赋值会被丢弃，所以常量一律零参函数。
// 部件遵循 ScadRig 约定（AGENT_GUIDE/ScadRig.md）：
//   * 1 unit = 1 m，Z-up，角色面朝 -Y（引擎空间 +Z）
//   * 每个部件的原点 = 所属骨骼 pivot：头部原点在颈部、手臂原点在肩、
//     腿部原点在髋（几何垂向 -Z）、躯干原点在盆骨顶、配饰使用躯干局部坐标
//   * 手臂/腿以左侧建模，右侧骨骼体内 mirror([1,0,0])
//   * ch_TINT() 纯品红 = 运行时换色占位
//
// 骨架标准（与 agent_basic 相同，clip 可跨角色复用）：
//   bone_root(地面) → bone_torso(+0.84) → bone_head(+0.54)
//                                        → bone_arm_l/r(±0.22, +0.46)
//                   → bone_leg_l/r(±0.10, +0.78)
//
// 角色文件写法（assets/scad/characters/worker.scad 为范例）：
//   use <../lib/kit_char.scad>
//   module bone_head() { ch_head_box(); ch_hat_helmet(); }
//   ... bone_* 模块拼部件，translate(ch_pivot_*()) 挂骨架 ...
//   bone_root();
//   anim_idle = ch_clip_idle();   // clip 由库函数提供
// ============================================================================

// ---- 调色板（零参/单参函数） ----

function ch_TINT()       = [1, 0, 1];                    // 运行时换色占位（纯品红）
function ch_SKIN(i = 0)  = [[0.92, 0.76, 0.62], [0.80, 0.62, 0.46],
                            [0.62, 0.44, 0.30], [0.45, 0.32, 0.22]][i];
function ch_HAIRC(i = 0) = [[0.20, 0.14, 0.10], [0.10, 0.10, 0.10],
                            [0.55, 0.42, 0.20], [0.60, 0.60, 0.62],
                            [0.48, 0.22, 0.12]][i];
function ch_PANTSC()     = [0.25, 0.25, 0.30];
function ch_SHOEC()      = [0.15, 0.15, 0.15];
function ch_WHITEC()     = [0.94, 0.94, 0.92];
function ch_DENIMC()     = [0.30, 0.38, 0.52];

// ---- 骨架标准 pivot（角色文件与库内共用，勿改：改了 clip 会漂） ----

function ch_pivot_torso() = [0, 0, 0.84];
function ch_pivot_head()  = [0, 0, 0.54];      // 相对 torso
function ch_pivot_arm_l() = [-0.22, 0, 0.46];  // 相对 torso
function ch_pivot_arm_r() = [ 0.22, 0, 0.46];  // 相对 torso
function ch_pivot_leg_l() = [-0.10, 0, 0.78];  // 相对 root
function ch_pivot_leg_r() = [ 0.10, 0, 0.78];  // 相对 root

// ---- 头部（原点在颈，几何向 +Z；鼻子朝 -Y 作朝向参考） ----

module ch_head_box(skin = ch_SKIN())
{
    color(skin) translate([0, 0, 0.11])     cube([0.22, 0.22, 0.22], center = true);
    color(skin) translate([0, -0.12, 0.08]) cube([0.05, 0.04, 0.05], center = true);
}

module ch_head_round(skin = ch_SKIN())
{
    color(skin) translate([0, 0, 0.115])    scale([1.0, 1.0, 1.05]) sphere(r = 0.125);
    color(skin) translate([0, -0.12, 0.08]) cube([0.05, 0.04, 0.05], center = true);
}

module ch_head_slim(skin = ch_SKIN())
{
    color(skin) translate([0, 0, 0.12])      cube([0.19, 0.20, 0.24], center = true);
    color(skin) translate([0, -0.11, 0.085]) cube([0.045, 0.04, 0.05], center = true);
}

// ---- 发型（头部局部坐标，盖在 box/slim 头顶 z≈0.22 附近） ----

module ch_hair_flat(c = ch_HAIRC())
{
    color(c) translate([0, 0.02, 0.205]) cube([0.24, 0.24, 0.05], center = true);
}

module ch_hair_buzz(c = ch_HAIRC(1))
{
    color(c) translate([0, 0.01, 0.215]) cube([0.23, 0.23, 0.03], center = true);
}

module ch_hair_swept(c = ch_HAIRC())
{
    color(c) translate([0, 0.02, 0.205])    cube([0.24, 0.24, 0.05], center = true);
    color(c) translate([-0.09, -0.10, 0.17]) cube([0.06, 0.05, 0.10], center = true);
}

module ch_hair_ponytail(c = ch_HAIRC())
{
    color(c) translate([0, 0.02, 0.205])  cube([0.24, 0.24, 0.05], center = true);
    color(c) translate([0, 0.135, 0.06])  cube([0.07, 0.05, 0.24], center = true);
}

// ---- 帽饰（头部局部坐标，可叠在发型/光头上） ----

module ch_hat_cap(c = ch_TINT())
{
    color(c) translate([0, 0.01, 0.235])   cube([0.24, 0.24, 0.08], center = true);
    color(c) translate([0, -0.16, 0.215])  cube([0.20, 0.12, 0.025], center = true);
}

module ch_hat_helmet(c = [0.95, 0.78, 0.12])
{
    color(c) translate([0, 0, 0.25])   cube([0.25, 0.25, 0.10], center = true);
    color(c) translate([0, 0, 0.205])  cube([0.30, 0.30, 0.025], center = true);
}

module ch_hat_chef(c = ch_WHITEC())
{
    color(c) translate([0, 0, 0.225])  cube([0.24, 0.24, 0.05], center = true);
    color(c) translate([0, 0, 0.33])   cube([0.21, 0.21, 0.17], center = true);
}

module ch_hat_beanie(c = ch_TINT())
{
    color(c) translate([0, 0, 0.235])  cube([0.24, 0.24, 0.07], center = true);
    color(c) translate([0, 0, 0.20])   cube([0.25, 0.25, 0.03], center = true);
}

// ---- 躯干（原点在盆骨顶，主体占 z 0..0.52） ----

module ch_torso_shirt(c = ch_TINT())
{
    color(c) translate([0, 0, 0.26]) cube([0.34, 0.20, 0.52], center = true);
}

module ch_torso_hoodie(c = ch_TINT())
{
    color(c) translate([0, 0, 0.26])      cube([0.34, 0.20, 0.52], center = true);
    color(c) translate([0, 0.13, 0.44])   cube([0.24, 0.08, 0.14], center = true);
    color(c) translate([0, -0.105, 0.12]) cube([0.20, 0.025, 0.10], center = true);
}

module ch_torso_vest(c = ch_TINT(), base = [0.35, 0.38, 0.42])
{
    color(base) translate([0, 0, 0.26]) cube([0.34, 0.20, 0.52], center = true);
    color(c) translate([0, 0, 0.33])    cube([0.37, 0.24, 0.28], center = true);
    color([0.85, 0.86, 0.88]) translate([0, 0, 0.33]) cube([0.376, 0.246, 0.05], center = true);
}

module ch_torso_suit(c = [0.22, 0.24, 0.30], shirt = ch_WHITEC())
{
    color(c) translate([0, 0, 0.26])          cube([0.34, 0.20, 0.52], center = true);
    color(shirt) translate([0, -0.105, 0.40]) cube([0.12, 0.02, 0.20], center = true);
    color(c) translate([-0.07, -0.112, 0.46]) cube([0.07, 0.015, 0.09], center = true);
    color(c) translate([ 0.07, -0.112, 0.46]) cube([0.07, 0.015, 0.09], center = true);
}

module ch_torso_dress(c = ch_TINT())
{
    color(c) translate([0, 0, 0.26])   cube([0.34, 0.20, 0.52], center = true);
    color(c) translate([0, 0, -0.07])  cube([0.38, 0.24, 0.14], center = true);
    color(c) translate([0, 0, -0.21])  cube([0.44, 0.28, 0.16], center = true);
}

// ---- 手臂（左侧，原点在肩，垂向 -Z；右骨骼 mirror([1,0,0])） ----

module ch_arm_shirt(c = ch_TINT(), skin = ch_SKIN())
{
    color(c) translate([0, 0, -0.14])    cube([0.10, 0.11, 0.28], center = true);
    color(skin) translate([0, 0, -0.38]) cube([0.09, 0.10, 0.22], center = true);
}

module ch_arm_long(c = ch_TINT(), skin = ch_SKIN())
{
    color(c) translate([0, 0, -0.21])    cube([0.10, 0.11, 0.42], center = true);
    color(skin) translate([0, 0, -0.45]) cube([0.09, 0.10, 0.10], center = true);
}

module ch_arm_gloved(c = ch_TINT(), glove = [0.30, 0.30, 0.32])
{
    color(c) translate([0, 0, -0.21])     cube([0.10, 0.11, 0.42], center = true);
    color(glove) translate([0, 0, -0.45]) cube([0.10, 0.11, 0.12], center = true);
}

// ---- 腿部（左侧，原点在髋，垂向 -Z 落地；右骨骼 mirror([1,0,0])） ----

module ch_leg_pants(c = ch_PANTSC(), shoe = ch_SHOEC())
{
    color(c) translate([0, 0, -0.38])          cube([0.13, 0.14, 0.76], center = true);
    color(shoe) translate([0, -0.04, -0.745])  cube([0.13, 0.24, 0.07], center = true);
}

module ch_leg_shorts(c = ch_PANTSC(), skin = ch_SKIN(), shoe = ch_SHOEC())
{
    color(c) translate([0, 0, -0.16])          cube([0.14, 0.15, 0.32], center = true);
    color(skin) translate([0, 0, -0.53])       cube([0.11, 0.12, 0.42], center = true);
    color(shoe) translate([0, -0.04, -0.745])  cube([0.13, 0.24, 0.07], center = true);
}

module ch_leg_boots(c = ch_PANTSC(), boot = [0.32, 0.24, 0.18])
{
    color(c) translate([0, 0, -0.26])          cube([0.13, 0.14, 0.52], center = true);
    color(boot) translate([0, 0, -0.63])       cube([0.14, 0.15, 0.22], center = true);
    color(boot) translate([0, -0.045, -0.74])  cube([0.14, 0.26, 0.08], center = true);
}

// ---- 配饰（躯干局部坐标，叠加在 torso 部件上） ----

module ch_acc_backpack(c = [0.48, 0.32, 0.20])
{
    color(c) translate([0, 0.16, 0.28])  cube([0.28, 0.12, 0.34], center = true);
    color(c) translate([0, 0.16, 0.47])  cube([0.29, 0.13, 0.05], center = true);
}

module ch_acc_toolbelt(c = [0.35, 0.24, 0.14])
{
    color(c) translate([0, 0, 0.035])         cube([0.36, 0.22, 0.06], center = true);
    color(c) translate([-0.14, -0.10, -0.01]) cube([0.08, 0.06, 0.12], center = true);
    color(c) translate([ 0.14, -0.10, -0.01]) cube([0.08, 0.06, 0.12], center = true);
}

module ch_acc_tie(c = [0.65, 0.16, 0.18])
{
    color(c) translate([0, -0.108, 0.33]) cube([0.06, 0.02, 0.26], center = true);
    color(c) translate([0, -0.11, 0.475]) cube([0.07, 0.025, 0.05], center = true);
}

module ch_acc_scarf(c = [0.75, 0.25, 0.22])
{
    color(c) translate([0, 0, 0.50]) cube([0.28, 0.24, 0.07], center = true);
}

module ch_acc_apron(c = ch_WHITEC())
{
    color(c) translate([0, -0.108, 0.16]) cube([0.30, 0.02, 0.36], center = true);
    color(c) translate([0, -0.11, 0.35])  cube([0.18, 0.022, 0.06], center = true);
}

// ---- 整装预设（bind 姿态静态摆放，供 catalog/组合台一键预览；无骨骼） ----

module ch_char_casual(tint = [0.30, 0.52, 0.75], skin = ch_SKIN(), hair = ch_HAIRC())
{
    translate(ch_pivot_torso())
    {
        ch_torso_shirt(tint);
        translate(ch_pivot_head()) { ch_head_box(skin); ch_hair_flat(hair); }
        translate(ch_pivot_arm_l()) ch_arm_shirt(tint, skin);
        translate(ch_pivot_arm_r()) mirror([1, 0, 0]) ch_arm_shirt(tint, skin);
    }
    translate(ch_pivot_leg_l()) ch_leg_pants();
    translate(ch_pivot_leg_r()) mirror([1, 0, 0]) ch_leg_pants();
}

module ch_char_worker(tint = [0.95, 0.60, 0.10], skin = ch_SKIN(1))
{
    translate(ch_pivot_torso())
    {
        ch_torso_vest(tint);
        ch_acc_toolbelt();
        translate(ch_pivot_head()) { ch_head_box(skin); ch_hat_helmet(); }
        translate(ch_pivot_arm_l()) ch_arm_gloved(tint);
        translate(ch_pivot_arm_r()) mirror([1, 0, 0]) ch_arm_gloved(tint);
    }
    translate(ch_pivot_leg_l()) ch_leg_boots(ch_DENIMC());
    translate(ch_pivot_leg_r()) mirror([1, 0, 0]) ch_leg_boots(ch_DENIMC());
}

module ch_char_chef(skin = ch_SKIN())
{
    translate(ch_pivot_torso())
    {
        ch_torso_shirt(ch_WHITEC());
        ch_acc_apron([0.82, 0.30, 0.24]);
        translate(ch_pivot_head()) { ch_head_round(skin); ch_hat_chef(); }
        translate(ch_pivot_arm_l()) ch_arm_shirt(ch_WHITEC(), skin);
        translate(ch_pivot_arm_r()) mirror([1, 0, 0]) ch_arm_shirt(ch_WHITEC(), skin);
    }
    translate(ch_pivot_leg_l()) ch_leg_pants([0.16, 0.16, 0.18]);
    translate(ch_pivot_leg_r()) mirror([1, 0, 0]) ch_leg_pants([0.16, 0.16, 0.18]);
}

module ch_char_suit(tint = [0.22, 0.24, 0.30], skin = ch_SKIN())
{
    translate(ch_pivot_torso())
    {
        ch_torso_suit(tint);
        ch_acc_tie();
        translate(ch_pivot_head()) { ch_head_slim(skin); ch_hair_swept(); }
        translate(ch_pivot_arm_l()) ch_arm_long(tint, skin);
        translate(ch_pivot_arm_r()) mirror([1, 0, 0]) ch_arm_long(tint, skin);
    }
    translate(ch_pivot_leg_l()) ch_leg_pants(tint);
    translate(ch_pivot_leg_r()) mirror([1, 0, 0]) ch_leg_pants(tint);
}

module ch_char_citizen(tint = [0.68, 0.30, 0.42], skin = ch_SKIN(), hair = ch_HAIRC(2))
{
    translate(ch_pivot_torso())
    {
        ch_torso_dress(tint);
        translate(ch_pivot_head()) { ch_head_slim(skin); ch_hair_ponytail(hair); }
        translate(ch_pivot_arm_l()) ch_arm_shirt(tint, skin);
        translate(ch_pivot_arm_r()) mirror([1, 0, 0]) ch_arm_shirt(tint, skin);
    }
    translate(ch_pivot_leg_l()) ch_leg_shorts([0.85, 0.83, 0.80], skin);
    translate(ch_pivot_leg_r()) mirror([1, 0, 0]) ch_leg_shorts([0.85, 0.83, 0.80], skin);
}

// ---- 动画 clip（针对骨架标准 bone 名；角色文件 anim_x = ch_clip_x();） ----

// 待机：2s 呼吸 + 手臂微摆
function ch_clip_idle() = [
    ["bone_torso", "rot", [for (t = [0 : 0.25 : 2]) [t, [2 * sin(180 * t), 0, 0]]]],
    ["bone_arm_l", "rot", [for (t = [0 : 0.5 : 2]) [t, [3 * sin(180 * t), 0,  2]]]],
    ["bone_arm_r", "rot", [for (t = [0 : 0.5 : 2]) [t, [-3 * sin(180 * t), 0, -2]]]],
];

// 行走：0.8s 腿 ±35°、臂反相 ±30°、root 上下 bob
function ch_clip_walk() = [
    ["bone_leg_l", "rot", [[0, [ 35, 0, 0]], [0.4, [-35, 0, 0]], [0.8, [ 35, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-35, 0, 0]], [0.4, [ 35, 0, 0]], [0.8, [-35, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-30, 0, 0]], [0.4, [ 30, 0, 0]], [0.8, [-30, 0, 0]]]],
    ["bone_arm_r", "rot", [[0, [ 30, 0, 0]], [0.4, [-30, 0, 0]], [0.8, [ 30, 0, 0]]]],
    ["bone_root",  "pos", [[0, [0, 0, 0]], [0.2, [0, 0, 0.03]], [0.4, [0, 0, 0]],
                           [0.6, [0, 0, 0.03]], [0.8, [0, 0, 0]]]],
];

// 坐姿：单帧姿态
function ch_clip_sit() = [
    ["loop", false],
    ["bone_root",  "pos", [[0, [0, 0, -0.42]]]],
    ["bone_leg_l", "rot", [[0, [-90, 0, 0]]]],
    ["bone_leg_r", "rot", [[0, [-90, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-25, 0, 0]]]],
    ["bone_arm_r", "rot", [[0, [-25, 0, 0]]]],
];

// 工作：1.5s 手臂小幅操作
function ch_clip_work() = [
    ["bone_arm_r", "rot", [[0, [-40, 0, 0]], [0.4, [-65, 0, 0]], [0.75, [-45, 0, 0]],
                           [1.1, [-70, 0, 0]], [1.5, [-40, 0, 0]]]],
    ["bone_arm_l", "rot", [[0, [-35, 0, 0]], [0.75, [-50, 0, 0]], [1.5, [-35, 0, 0]]]],
    ["bone_torso", "rot", [[0, [4, 0, 0]], [0.75, [7, 0, 0]], [1.5, [4, 0, 0]]]],
];

// 挥手：1.2s 右臂举起摆动 + 头微偏
function ch_clip_wave() = [
    ["bone_arm_r", "rot", [[0, [-150, 0, 0]], [0.3, [-150, 0, 22]], [0.6, [-150, 0, -22]],
                           [0.9, [-150, 0, 22]], [1.2, [-150, 0, 0]]]],
    ["bone_head",  "rot", [[0, [0, 0, 6]], [0.6, [0, 0, 10]], [1.2, [0, 0, 6]]]],
];
