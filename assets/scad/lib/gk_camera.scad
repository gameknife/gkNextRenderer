// ============================================================================
// gk_camera.scad - 相机机位虚拟点标记库（零几何，不产生任何三角面）
//
// 用法：场景文件 `use <../lib/gk_camera.scad>` 后，在布局代码中像摆道具一样
// 摆放标记模块。加载 .scad 时 loader 会识别这些节点：
//   * gk_camera       -> EnvironmentSetting.cameras 定点机位（UI 相机列表可选）
//   * gk_camera_key   -> 按 path 分组、按 t 排序生成 AnimationTrack 相机动画
//
// 朝向约定（SCAD 空间，Z-up）：相机局部 front = +Y，up = +Z。
// 经 Z-up -> Y-up 转换后正好匹配引擎相机 front = -Z / up = +Y，
// 因此定点与路径相机都走与 glTF 相机相同的运行时路径（Scene 动画覆盖）。
//
// 推荐直接用 *_lookat 便捷模块（内部算 yaw/pitch，免除手写 rotate）；
// 手动摆放时用 translate + rotate([pitch, 0, yaw])：pitch 为俯仰（正=抬头），
// yaw 绕 Z（0 = 朝 +Y，90 = 朝 -X，-90 = 朝 +X，180 = 朝 -Y）。
//
// 注意：本库模块全部为空调合体（empty body），不要加 color()/几何，
// 也不要放进 kit_*.scad（catalog 会报 0 三角）。
// ============================================================================

// ---- 定点相机 ---------------------------------------------------------------
// name     : 机位名（UI 相机列表显示）
// fov      : 垂直视场角（度），默认 55
// aperture : 光圈（0 = 关景深）
// focal    : 对焦距离（米，0 = 自动）
module gk_camera(name = "cam", fov = 55, aperture = 0, focal = 0)
{
}

// ---- 路径相机关键帧 ---------------------------------------------------------
// 同一个 path 名的所有 key 构成一条相机动画；loader 按 t（秒）升序采样。
// 至少 2 个 key 才会生成动画；t 从 0 开始，最后一条 key 的 t 即时长。
module gk_camera_key(path = "fly", t = 0, fov = 55, aperture = 0, focal = 0)
{
}

// ---- lookat 便捷模块 ---------------------------------------------------------
// eye/target 为 SCAD 世界坐标（米）。内部转成 translate + rotate 后调用标记。
// 支持直接放在顶层或任何变换之内。

function _gk_cam_pitch(eye, target) =
    norm(target - eye) < 1e-6 ? 0 : asin((target.z - eye.z) / norm(target - eye));

function _gk_cam_yaw(eye, target) =
    norm([target.x - eye.x, target.y - eye.y]) < 1e-6 ? 0
        : atan2(-(target.x - eye.x), target.y - eye.y);

module gk_camera_lookat(eye, target, name = "cam", fov = 55, aperture = 0, focal = 0)
{
    translate(eye)
        rotate([_gk_cam_pitch(eye, target), 0, _gk_cam_yaw(eye, target)])
            gk_camera(name, fov, aperture, focal > 0 ? focal : norm(target - eye));
}

module gk_camera_lookat_key(eye, target, path = "fly", t = 0, fov = 55, aperture = 0, focal = 0)
{
    translate(eye)
        rotate([_gk_cam_pitch(eye, target), 0, _gk_cam_yaw(eye, target)])
            gk_camera_key(path, t, fov, aperture, focal > 0 ? focal : norm(target - eye));
}
