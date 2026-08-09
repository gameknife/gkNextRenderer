// kit_layout.scad —— 布局组合子库（SCAD Scene Compose L1，见 docs/designs/scad-scene-compose-design.md §5）
//
// 组合子只做放置（transform + 每实例 $ 变量），不产几何；children 是被摆放的零件。
// 随机全部由 seed 派生，确定性（引擎与 OpenSCAD 渲染一致，可回归截图对比）。
// CSG 守则：布局层一律 union 拼接；difference/intersection 只允许出现在零件内部。
//
// children 内可读的每实例变量：
//   $idx   线性序号            $col / $row  网格坐标（lay_grid）
//   $seed  每实例派生种子      $t           段内参数 0..1（lay_along）
//   $px / $py  本实例在组合子自身坐标系内的放置偏移（kit_terrain 的 ter_snap 用它贴地）
//
// 已在引擎 loader 验证：for 体内 `$var = ...;` 赋值经动态作用域穿透 children()。

// ================= 确定性伪随机 =================
// 必须含平方项：线性同余的任意组合仍是线性，连续 seed 会输出等差序列
// （散布成格线/选型全同的伪影）。x < 65521 时 x*x < 2^53，double 精确。
function lay_sq(x) = (x * x + x * 587 + 41) % 65521;
function lay_hash(x) = lay_sq(lay_sq(((x % 65521) + 65521) % 65521) + 13);
function lay_rand(seed, i = 0) = (lay_hash(seed + i * 131) * 65521 + lay_hash(seed * 3 + i * 977 + 5)) % 9973;
function lay_randf(seed, i = 0) = lay_rand(seed, i) / 9972;            // [0, 1]
function lay_randi(seed, i, m) = lay_rand(seed, i) % m;                // 0 .. m-1
function lay_randr(seed, i, a, b) = a + (b - a) * lay_randf(seed, i);  // [a, b]

// ================= 网格 =================
// cols x rows 网格，格距 cw x ch；center=true 时整体以原点居中。
module lay_grid(cols = 3, rows = 3, cw = 10, ch = 10, seed = 0, center = true)
{
    for (r = [0 : rows - 1], c = [0 : cols - 1])
    {
        $col = c;
        $row = r;
        $idx = r * cols + c;
        $seed = seed * 8191 + r * 131 + c * 7;
        $px = center ? (c - (cols - 1) / 2) * cw : c * cw;
        $py = center ? (r - (rows - 1) / 2) * ch : r * ch;
        translate([$px, $py, 0])
            children();
    }
}

// ================= 直线阵 =================
module lay_row(n = 4, dx = 10, dy = 0, seed = 0)
{
    for (i = [0 : n - 1])
    {
        $idx = i;
        $seed = seed * 8191 + i * 131;
        $px = i * dx;
        $py = i * dy;
        translate([$px, $py, 0]) children();
    }
}

// ================= 环阵 =================
// 半径 r 上均布 n 件，a0 为起始角。face: 1=front(-y) 朝圆心，-1=朝外，0=保持世界朝向。
module lay_ring(n = 6, r = 10, seed = 0, face = 1, a0 = 0)
{
    for (i = [0 : n - 1])
    {
        $idx = i;
        $seed = seed * 8191 + i * 131;
        lay_ring_a = a0 + i * 360 / n;
        $px = r * sin(lay_ring_a);
        $py = -r * cos(lay_ring_a);
        rotate([0, 0, lay_ring_a]) translate([0, -r, 0])
            rotate([0, 0, face == 1 ? 180 : (face == -1 ? 0 : -lay_ring_a)])
                children();
    }
}

// ================= 区域散布 =================
// 在 [x0,x1]x[y0,y1] 内确定性散布 n 件；rot=true 随机朝向。无最小间距保证，
// 密集场景用多个小 seed 区域拼接控制。
module lay_scatter(n = 10, x0 = -20, x1 = 20, y0 = -20, y1 = 20, seed = 0, rot = true)
{
    for (i = [0 : n - 1])
    {
        $idx = i;
        $seed = seed * 8191 + i * 131;
        $px = lay_randr($seed, 1, x0, x1);
        $py = lay_randr($seed, 2, y0, y1);
        translate([$px, $py, 0])
            rotate([0, 0, rot ? lay_randi($seed, 3, 360) : 0])
                children();
    }
}

// ================= 折线撒点 =================
// 沿折线 pts（[[x,y],...]）每 step 放一件，offset 为首件沿线偏移。
// 子件随段方向旋转：段朝 +x 时 front(-y) 指向行进方向右侧。
module lay_along(pts, step = 6, seed = 0, offset = 0)
{
    for (s = [0 : len(pts) - 2])
    {
        lay_al_d = [pts[s + 1][0] - pts[s][0], pts[s + 1][1] - pts[s][1]];
        lay_al_len = norm(lay_al_d);
        lay_al_a = atan2(lay_al_d[1], lay_al_d[0]);
        lay_al_cnt = floor((lay_al_len - offset) / step);
        if (lay_al_cnt >= 0)
        {
            for (k = [0 : lay_al_cnt])
            {
                $idx = s * 1024 + k;
                $seed = seed * 8191 + s * 131 + k * 7;
                $t = (offset + k * step) / lay_al_len;
                $px = pts[s][0] + lay_al_d[0] * $t;
                $py = pts[s][1] + lay_al_d[1] * $t;
                translate([$px, $py, 0])
                    rotate([0, 0, lay_al_a])
                        children();
            }
        }
    }
}

// ================= 候选选一 =================
// 从 children 里选一个实例化：i >= 0 时定选，否则按 seed 确定性随机。
module lay_pick(seed = 0, i = -1)
{
    children(i >= 0 ? i % $children : lay_randi(seed, 0, $children));
}

// ================= 随机抖动包装 =================
// 给 children 加 ±dx/±dy 平移和 ±rot 度旋转（用每格 $seed 调用可无重复感）。
module lay_jitter(seed = 0, dx = 1, dy = 1, rot = 0)
{
    translate([lay_randr(seed, 11, -dx, dx), lay_randr(seed, 12, -dy, dy), 0])
        rotate([0, 0, rot > 0 ? lay_randr(seed, 13, -rot, rot) : 0])
            children();
}
