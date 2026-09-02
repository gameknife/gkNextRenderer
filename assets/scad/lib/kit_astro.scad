// kit_astro.scad —— AstroBot 风格 3D 平台跳跃解密零件库（玩具感低模 + PBR 亮面塑料）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "ab_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 尺度：真实世界比例，1 unit = 1 m；主角机器人身高 1.6 m（ab_char_bot）。
//   玩法尺度基准：单跳高 ≈ 2.0 m、悬浮跨越 ≈ 5 m、台阶 ≤ 1.2 m、通道宽 ≥ 2 m、
//   滑索抓握高 ≈ 2.9 m、弹跳垫弹起 ≈ 6 m。
// 放置契约：
//   * 落地件底面 z=0；带朝向件 front = -y；线性件（轨道/滑索/传送带/桥/滚筒/管道）沿 +x 延伸。
//   * 悬浮岛 ab_ground_island / ab_ground_island_round **顶面 z=0**，岛体向下延伸
//     （zMin 为负是预期），岛上内容直接用 z=0 局部坐标摆放。
//   * ab_plat_zipline：起点柱落 z=0，终点柱落 z=-drop（落在更低的岛上），zMin 为负是预期。
//   * ab_plat_bridge：桥面端点 z≈0.1，中段下垂 sag（zMin 为负是预期）。
//   * 收集物（item）默认悬浮 hover 高度（底面 z=hover），caller 传 hover=0 即落地。
//   * 悬浮敌人 ab_char_enemy_flyer 底面 z=hover。
// 类别：ground 地面岛屿 / plat 平台机关 / bldg 结构 / nature 植被地景 / prop 道具机关 /
//       item 收集物 / char 角色（主角机器人、被困机器人、敌人）。
// 材质：ab_gloss / ab_plastic / ab_matte / ab_rubber / ab_leaf / ab_metal / ab_chrome / ab_gold /
//       ab_glass / ab_water 包装 gk_material，提供 roughness/metalness，PT 管线下呈现
//       玩具塑料 / 金属 / 玻璃 / 水面质感。全库不用透明 alpha：透明 Dielectric 在默认暗天空下渲染成黑球，
//       玻璃/水柱/激光束一律用不透明高光色块表达。

// ================= 配色（明快玩具色；PT 强日光下会整体提亮，故基色略压深） =================
function ab_WHITE()   = [0.72, 0.73, 0.74];   // 机器人白（高光塑料）
function ab_CREAM()   = [0.74, 0.71, 0.62];   // 奶白（棋格亮块/饰边）
function ab_BLUE()    = [0.08, 0.30, 0.66];   // 主蓝（机器人配色）
function ab_BLUEL()   = [0.22, 0.56, 0.86];   // 亮蓝（目镜/灯）
function ab_VISOR()   = [0.05, 0.06, 0.09];   // 目镜黑
function ab_DARK()    = [0.10, 0.10, 0.11];   // 深色（橡胶/洞口）
function ab_CHECKD()  = [0.24, 0.27, 0.32];   // 棋格暗块（蓝灰）
function ab_GRASS()   = [0.31, 0.51, 0.19];   // 草皮
function ab_GRASSD()  = [0.24, 0.42, 0.16];   // 深草斑
function ab_SOIL()    = [0.46, 0.31, 0.19];   // 岛土层
function ab_SOILD()   = [0.35, 0.23, 0.15];   // 深土层
function ab_ROCK()    = [0.47, 0.45, 0.43];   // 岩石
function ab_ROCKD()   = [0.35, 0.33, 0.32];   // 深岩
function ab_SAND()    = [0.64, 0.56, 0.37];   // 沙
function ab_SNOW()    = [0.76, 0.78, 0.80];   // 雪/冰面
function ab_WATER()   = [0.10, 0.42, 0.52];   // 水
function ab_LAVA()    = [0.92, 0.36, 0.05];   // 岩浆（高亮）
function ab_WOOD()    = [0.52, 0.36, 0.20];   // 木
function ab_WOODD()   = [0.36, 0.25, 0.14];   // 深木
function ab_WOODL()   = [0.64, 0.49, 0.29];   // 浅木板
function ab_TRUNK()   = [0.40, 0.28, 0.18];   // 树干
function ab_YELLOW()  = [0.82, 0.64, 0.10];   // 玩具黄
function ab_ORANGE()  = [0.84, 0.42, 0.08];   // 玩具橙
function ab_RED()     = [0.68, 0.14, 0.11];   // 玩具红
function ab_PINK()    = [0.84, 0.44, 0.55];   // 粉
function ab_TEAL()    = [0.10, 0.54, 0.52];   // 青
function ab_PURPLE()  = [0.44, 0.24, 0.62];   // 紫
function ab_GREENL()  = [0.42, 0.64, 0.22];   // 亮绿
function ab_METAL()   = [0.62, 0.63, 0.65];   // 亮金属
function ab_METALD()  = [0.30, 0.31, 0.34];   // 深金属
function ab_GOLD()    = [0.92, 0.74, 0.26];   // 金币金
function ab_GOLDD()   = [0.70, 0.52, 0.14];   // 金币暗纹
function ab_CLOUD()   = [0.76, 0.77, 0.80];   // 云
function ab_ENEMY()   = [0.40, 0.22, 0.50];   // 敌人紫
function ab_TERRA()   = [0.66, 0.36, 0.22];   // 陶土花盆

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function ab_sq(x) = (x * x + x * 641 + 53) % 65521;
function ab_rnd(s, m) = ab_sq(ab_sq(((s % 65521) + 65521) % 65521) + 19) % m;
function ab_rndf(s) = ab_rnd(s, 1000) / 999;                       // [0, 1]
function ab_rndr(s, a, b) = a + (b - a) * ab_rndf(s);              // [a, b]

// ---- 变体调色板（列表字面量直接下标；勿对函数调用结果直接下标） ----
function ab_block_c(i)   = [ab_RED(), ab_BLUE(), ab_YELLOW(), ab_GREENL(), ab_ORANGE(), ab_TEAL(),
                            ab_PURPLE(), ab_PINK()][ab_rnd(i, 8)];                    // 积木/平台
function ab_leaf_c(i)    = [[0.32, 0.56, 0.22], [0.26, 0.50, 0.30], [0.44, 0.60, 0.20],
                            [0.20, 0.47, 0.38], [0.52, 0.58, 0.18]][ab_rnd(i, 5)];    // 树冠
function ab_wall_c(i)    = [[0.74, 0.70, 0.60], [0.64, 0.72, 0.74], [0.76, 0.66, 0.60],
                            [0.70, 0.72, 0.52]][ab_rnd(i, 4)];                        // 玩具屋墙
function ab_roof_c(i)    = [ab_RED(), ab_BLUE(), ab_TEAL(), ab_ORANGE(), ab_PURPLE()][ab_rnd(i, 5)];
function ab_balloon_c(i) = [ab_RED(), ab_YELLOW(), ab_BLUEL(), ab_PINK(), ab_GREENL(), ab_PURPLE()][ab_rnd(i, 6)];
function ab_sign_c(i)    = [ab_YELLOW(), ab_BLUEL(), ab_ORANGE(), ab_TEAL()][ab_rnd(i, 4)];
function ab_flower_c(i)  = [ab_RED(), ab_YELLOW(), ab_PINK(), [0.80, 0.78, 0.74], ab_PURPLE(),
                            ab_ORANGE()][ab_rnd(i, 6)];
function ab_enemy_c(i)   = [ab_ENEMY(), [0.52, 0.20, 0.24], [0.26, 0.30, 0.40], [0.50, 0.32, 0.12]][ab_rnd(i, 4)];
function ab_quad_c(i)    = [ab_RED(), ab_YELLOW(), ab_BLUE(), ab_GREENL()][i % 4];

// ================= 材质包装（PBR） =================
module ab_gloss(c)   { gk_material(c, roughness = 0.12, metalness = 0) children(); }   // 高光塑料（机器人/亮件）
module ab_plastic(c) { gk_material(c, roughness = 0.30, metalness = 0) children(); }   // 亮面塑料（平台/积木）
module ab_matte(c)   { color(c) children(); }                                          // 漫反射（草皮/土/岩）
module ab_rubber(c)  { gk_material(c, roughness = 0.75, metalness = 0) children(); }   // 橡胶
module ab_leaf(c)    { gk_material(c, roughness = 0.55, metalness = 0) children(); }   // 树冠（半哑光）
module ab_metal(c)   { gk_material(c, roughness = 0.38, metalness = 0.6) children(); } // 金属（保留漫反射，暗天空下不发黑）
module ab_chrome(c)  { gk_material(c, roughness = 0.12, metalness = 0.8) children(); } // 镜面金属
module ab_gold()     { gk_material(ab_GOLD(), roughness = 0.28, metalness = 0.5) children(); }  // 半金属：暗环境仍保留金色
module ab_glass(c)   { gk_material(c, roughness = 0.05, metalness = 0) children(); }   // 亮面“玻璃”：不透明高光（透明 Dielectric 在暗天空下会发黑）
module ab_water(c)   { gk_material(c, roughness = 0.02, metalness = 0) children(); }   // 不透明低粗糙度水面

// ================= 基础工具 =================
module ab_boxc(s) cube(s, center = true);
module ab_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) ab_boxc([L, D, t]);   // 底面 z=0 平板
module ab_ellipsoid(rx = 1, ry = 1, rz = 1) scale([rx, ry, rz]) sphere(r = 1);

// 圆角矩形柱（底面 z=0）
module ab_rbox(L = 2, D = 2, H = 1, r = 0.3)
{
    rr = min(r, L / 2 - 0.01, D / 2 - 0.01);
    hull() for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (L / 2 - rr), sy * (D / 2 - rr), 0]) cylinder(h = H, r = rr);
}

// 顶边倒角块（底面 z=0）
module ab_bevel(L = 2, D = 2, H = 1, b = 0.15)
{
    bb = min(b, H * 0.6, L / 2 - 0.01, D / 2 - 0.01);
    hull()
    {
        ab_slab(L, D, max(0.01, H - bb));
        translate([0, 0, H - bb]) ab_slab(L - 2 * bb, D - 2 * bb, bb);
    }
}

// 圆角立方（中心为原点）
module ab_rcube(s = 1, r = 0.1)
{
    hull()
    {
        ab_boxc([s - 2 * r, s, s - 2 * r]);
        ab_boxc([s, s - 2 * r, s - 2 * r]);
        ab_boxc([s - 2 * r, s - 2 * r, s]);
    }
}

// 圆环（绕 z 轴，环心 z=0）
module ab_torus(R = 1, r = 0.1, angle = 360) rotate_extrude(angle = angle) translate([R, 0]) circle(r = r);

// 半球（底面 z=0，穹顶向上）
module ab_dome(r = 1, squash = 1.0)
{
    difference()
    {
        scale([1, 1, squash]) sphere(r = r);
        translate([0, 0, -r * 2]) ab_slab(r * 4, r * 4, r * 2);
    }
}

// 五角星薄片（linear_extrude，底面 z=0，尖角朝 +y）
module ab_star(ro = 0.5, ri = 0.22, t = 0.1)
{
    linear_extrude(t) polygon([for (i = [0 : 9]) [(i % 2 == 0 ? ro : ri) * sin(i * 36 + 180),
                                                  -(i % 2 == 0 ? ro : ri) * cos(i * 36 + 180)]]);
}

// 坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙皮，h=脊高，ov=出檐（面序为 OpenSCAD 约定）
module ab_part_roof(L = 5, D = 4, h = 1.8, ov = 0.6, c = [0.6, 0.2, 0.15])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = L / 2;
    ab_plastic(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// 黄黑警示条（沿 x，中心为原点，贴在 -y 面用）
module ab_part_hazard(L = 3, h = 0.2)
{
    n = max(1, floor(L / 0.4));
    for (i = [0 : n - 1])
        ab_plastic(i % 2 == 0 ? ab_YELLOW() : ab_DARK())
            translate([-L / 2 + (i + 0.5) * L / n, 0, 0]) ab_boxc([L / n, 0.03, h]);
}

// ================= 地面 / 悬浮岛 =================

// 悬浮岛（顶面 z=0；top 0=草 1=沙 2=雪）：圆角草皮 + 土层 + 岩层 + 倒锥底 + 悬垂碎岩。L,D ≥ 8。
module ab_ground_island(L = 20, D = 16, seed = 0, top = 0)
{
    tc = top == 1 ? ab_SAND() : (top == 2 ? ab_SNOW() : ab_GRASS());
    td = top == 1 ? [0.55, 0.47, 0.30] : (top == 2 ? [0.66, 0.70, 0.74] : ab_GRASSD());
    r0 = min(L, D) * 0.28;
    gt = 0.55;
    ab_matte(tc) translate([0, 0, -gt]) ab_rbox(L, D, gt, r0);
    for (i = [0 : 5])
        ab_matte(td)
            translate([ab_rndr(seed * 7 + i * 13, -(L / 2 - 2.2), L / 2 - 2.2),
                       ab_rndr(seed * 11 + i * 17 + 3, -(D / 2 - 2.2), D / 2 - 2.2), 0])
                cylinder(h = 0.012, r = ab_rndr(seed + i * 5, 0.8, 1.9), $fn = 7);
    ab_matte(ab_SOIL())  translate([0, 0, -gt - 1.5]) ab_rbox(L - 1.4, D - 1.4, 1.52, r0 * 0.8);
    ab_matte(ab_SOILD()) translate([0, 0, -gt - 2.6]) ab_rbox(L - 3.2, D - 3.2, 1.12, r0 * 0.6);
    ab_matte(ab_ROCK())  translate([0, 0, -gt - 4.0]) ab_rbox(L - 5.5, D - 5.5, 1.42, r0 * 0.45);
    ab_matte(ab_ROCKD()) translate([0, 0, -gt - 7.0])
        scale([(L - 6) / 2, (D - 6) / 2, 1]) cylinder(h = 3.02, r1 = 0.12, r2 = 1, $fn = 8);
    ro = max(0.2, L / 2 - 6);
    rd = max(0.2, D / 2 - 6);
    for (i = [0 : 2])
        ab_matte(ab_ROCK())
            translate([ab_rndr(seed * 3 + i * 29, -ro, ro), ab_rndr(seed * 5 + i * 31 + 7, -rd, rd),
                       -gt - 5.0 - ab_rndf(seed + i) * 1.0])
                rotate([ab_rnd(seed + i, 30), ab_rnd(seed + i * 3, 30), 0]) ab_boxc([1.2, 1.0, 1.4]);
}

// 圆形悬浮岛（顶面 z=0）：小型踏脚岛/终点岛用。r ≥ 3。
module ab_ground_island_round(r = 8, seed = 0, top = 0)
{
    tc = top == 1 ? ab_SAND() : (top == 2 ? ab_SNOW() : ab_GRASS());
    gt = 0.55;
    ab_matte(tc) translate([0, 0, -gt]) cylinder(h = gt, r = r, $fn = 16);
    for (i = [0 : 3])
        ab_matte(ab_GRASSD())
            translate([ab_rndr(seed * 7 + i * 13, -r * 0.6, r * 0.6), ab_rndr(seed * 11 + i * 17 + 3, -r * 0.6, r * 0.6), 0])
                cylinder(h = 0.012, r = min(1.6, r * 0.2), $fn = 7);
    ab_matte(ab_SOIL())  translate([0, 0, -gt - 1.3]) cylinder(h = 1.32, r = r - 0.6, $fn = 12);
    ab_matte(ab_SOILD()) translate([0, 0, -gt - 2.2]) cylinder(h = 0.92, r = r - 1.4, $fn = 10);
    ab_matte(ab_ROCK())  translate([0, 0, -gt - 3.3]) cylinder(h = 1.12, r = r - 2.2, $fn = 9);
    ab_matte(ab_ROCKD()) translate([0, 0, -gt - 3.3 - r * 0.45])
        cylinder(h = r * 0.45 + 0.02, r1 = 0.1, r2 = r - 2.3, $fn = 7);
}

// 棋格塑料地板（底面 z=0，顶面 z=0.16）：Playroom 风格
module ab_ground_tile(L = 8, D = 8, cell = 1.0)
{
    ab_plastic(ab_CREAM()) ab_bevel(L, D, 0.16, 0.04);
    nx = max(1, floor(L / cell));
    ny = max(1, floor(D / cell));
    for (i = [0 : nx - 1], j = [0 : ny - 1])
        if ((i + j) % 2 == 0)
            ab_plastic(ab_CHECKD())
                translate([-L / 2 + (i + 0.5) * L / nx, -D / 2 + (j + 0.5) * D / ny, 0.16])
                    ab_slab(L / nx - 0.04, D / ny - 0.04, 0.012);
}

// 草皮薄板（底面 z=0，厚 0.08）：铺在棋格地板/平台上打散
module ab_ground_grass(L = 6, D = 6, seed = 0)
{
    ab_matte(ab_GRASS()) ab_rbox(L, D, 0.08, min(L, D) * 0.2);
    for (i = [0 : 3])
        ab_matte(ab_GRASSD())
            translate([ab_rndr(seed * 7 + i * 13, -(L / 2 - 1), L / 2 - 1), ab_rndr(seed * 11 + i * 17 + 3, -(D / 2 - 1), D / 2 - 1), 0.08])
                cylinder(h = 0.012, r = ab_rndr(seed + i * 5, 0.5, 1.1), $fn = 7);
}

// 沙地薄板（底面 z=0）：带波纹
module ab_ground_sand(L = 6, D = 6, seed = 0)
{
    ab_matte(ab_SAND()) ab_rbox(L, D, 0.08, min(L, D) * 0.2);
    for (i = [0 : 4])
        ab_matte([0.56, 0.48, 0.31])
            translate([ab_rndr(seed * 7 + i * 13, -(L / 2 - 1), L / 2 - 1), ab_rndr(seed * 11 + i * 17 + 3, -(D / 2 - 1), D / 2 - 1), 0.08])
                rotate([0, 0, ab_rnd(seed + i, 40) - 20]) ab_slab(1.6, 0.08, 0.012);
}

// 水池（底面 z=0）：圆角奶白围边 + 深色池底 + 低粗糙度水面（顶 z=depth）
module ab_ground_pool(L = 8, D = 6, depth = 0.6)
{
    ab_plastic(ab_CREAM()) difference()
    {
        ab_rbox(L, D, depth + 0.2, 1.0);
        translate([0, 0, 0.15]) ab_rbox(L - 0.8, D - 0.8, depth + 0.5, 0.7);
    }
    ab_matte([0.08, 0.30, 0.38]) translate([0, 0, 0.15]) ab_rbox(L - 0.8, D - 0.8, 0.05, 0.7);
    ab_water(ab_WATER()) translate([0, 0, 0.2]) ab_rbox(L - 0.8, D - 0.8, depth - 0.2, 0.7);
}

// 岩浆池（底面 z=0）：深岩围边 + 高亮岩浆面 + 漂浮黑色结壳
module ab_ground_lava(L = 8, D = 6, seed = 0)
{
    ab_matte(ab_ROCKD()) difference()
    {
        ab_rbox(L, D, 0.7, 1.2);
        translate([0, 0, 0.15]) ab_rbox(L - 0.9, D - 0.9, 1.0, 0.8);
    }
    ab_gloss(ab_LAVA()) translate([0, 0, 0.15]) ab_rbox(L - 0.9, D - 0.9, 0.35, 0.8);
    for (i = [0 : 4])
        ab_matte(ab_DARK())
            translate([ab_rndr(seed * 7 + i * 13, -(L / 2 - 1.6), L / 2 - 1.6), ab_rndr(seed * 11 + i * 17 + 3, -(D / 2 - 1.4), D / 2 - 1.4), 0.5])
                rotate([0, 0, ab_rnd(seed + i, 90)]) cylinder(h = 0.03, r = ab_rndr(seed + i * 3, 0.4, 0.9), $fn = 5);
}

// 踏脚石小径（沿 x，底面 z=0）
module ab_ground_path(L = 8, W = 1.6, seed = 0)
{
    n = max(1, floor(L / 1.3));
    for (i = [0 : n - 1])
        ab_matte(ab_ROCK())
            translate([-L / 2 + 0.65 + i * 1.3, ab_rndr(seed + i * 7, -0.25, 0.25), 0])
                rotate([0, 0, ab_rnd(seed + i, 360)]) scale([1, W / 1.6 * 0.85, 1]) cylinder(h = 0.1, r = 0.58, $fn = 6);
}

// ================= 平台机关（plat） =================

// 玩具积木块（倒角亮面塑料，底面 z=0）：顶面奶白内嵌板
module ab_plat_block(L = 2, D = 2, H = 1, c = ab_YELLOW())
{
    ab_plastic(c) ab_bevel(L, D, H, 0.12);
    ab_plastic(ab_CREAM()) translate([0, 0, H]) ab_slab(max(0.3, L - 0.6), max(0.3, D - 0.6), 0.02);
}

// 积木台阶：沿 +x 上升（起点在 -x 侧），n 级，每级深 run 高 rise，宽 w（沿 y）
module ab_plat_stairs(n = 4, w = 3, rise = 0.5, run = 0.9, seed = 0)
{
    for (i = [0 : n - 1])
        translate([-n * run / 2 + (i + 0.5) * run, 0, 0])
            ab_plat_block(run, w, (i + 1) * rise, ab_block_c(seed + i * 3));
}

// 悬浮圆盘平台（底面 z=0，顶面 z=0.5）：底部收分 + 奶白顶盘 + 蓝色推进毂
module ab_plat_float(r = 2, c = ab_TEAL())
{
    ab_plastic(c) hull()
    {
        cylinder(h = 0.1, r = r - 0.3, $fn = 16);
        translate([0, 0, 0.12]) cylinder(h = 0.38, r = r, $fn = 16);
    }
    ab_plastic(ab_CREAM()) translate([0, 0, 0.5]) cylinder(h = 0.02, r = r - 0.35, $fn = 16);
    ab_gloss(ab_BLUEL()) cylinder(h = 0.1, r1 = 0.35, r2 = 0.55, $fn = 10);
}

// 轨道移动平台（轨道沿 x 长 rail，底面 z=0；平台顶面 z=0.7，t∈[0,1] 为当前位置）
module ab_plat_moving(rail = 10, L = 3, W = 2, t = 0.5, c = ab_ORANGE())
{
    n = max(1, floor(rail / 2.5));
    for (sy = [-1, 1])
    {
        ab_metal(ab_METAL()) translate([0, sy * (W / 2 - 0.2), 0.32]) rotate([0, 90, 0]) cylinder(h = rail, r = 0.07, center = true, $fn = 8);
        for (i = [0 : n])
            ab_metal(ab_METALD()) translate([-rail / 2 + 0.4 + i * (rail - 0.8) / n, sy * (W / 2 - 0.2), 0]) cylinder(h = 0.32, r = 0.06, $fn = 6);
        for (sx = [-1, 1])
            ab_plastic(ab_RED()) translate([sx * (rail / 2 - 0.1), sy * (W / 2 - 0.2), 0.32]) ab_boxc([0.2, 0.3, 0.3]);
    }
    px = -rail / 2 + L / 2 + t * (rail - L);
    translate([px, 0, 0])
    {
        for (sx = [-1, 1], sy = [-1, 1])
            ab_rubber(ab_DARK()) translate([sx * (L / 2 - 0.4), sy * (W / 2 - 0.2), 0.32]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.16, center = true, $fn = 10);
        ab_plastic(c) translate([0, 0, 0.3]) ab_bevel(L, W, 0.4, 0.08);
        ab_plastic(ab_CREAM()) translate([0, 0, 0.7]) ab_slab(L - 0.5, W - 0.5, 0.02);
        for (sy = [-1, 1]) translate([0, sy * (W / 2 + 0.005), 0.5]) ab_part_hazard(L - 0.3, 0.18);
    }
}

// 旋转圆盘平台（底面 z=0，顶面 z=0.55）：四色扇区 + 中心毂 + 白色转向箭头
module ab_plat_spin(r = 3)
{
    ab_metal(ab_METALD()) cylinder(h = 0.25, r = 0.6, $fn = 12);
    for (i = [0 : 3])
        ab_plastic(ab_quad_c(i)) translate([0, 0, 0.25]) rotate([0, 0, i * 90]) rotate_extrude(angle = 90) square([r, 0.3]);
    ab_metal(ab_METAL()) translate([0, 0, 0.4]) ab_torus(r, 0.05);
    ab_plastic(ab_CREAM()) translate([0, 0, 0.55]) cylinder(h = 0.1, r = 0.6, $fn = 12);
    for (i = [0 : 3])
        rotate([0, 0, i * 90 + 45]) translate([r * 0.62, 0, 0.55]) rotate([0, 0, 90])
            ab_plastic(ab_CREAM()) linear_extrude(0.015)
                polygon([[-0.45, -0.18], [0.15, -0.18], [0.15, -0.38], [0.5, 0], [0.15, 0.38], [0.15, 0.18], [-0.45, 0.18]]);
}

// 跷跷板（沿 x，底面 z=0）：红色三角支点 + 倾斜板 + 两端踏板
module ab_plat_seesaw(L = 6, W = 2, tilt = 8)
{
    ab_plastic(ab_RED()) rotate([90, 0, 0]) translate([0, 0, -W * 0.25]) linear_extrude(W * 0.5) polygon([[-0.9, 0], [0.9, 0], [0, 0.95]]);
    ab_metal(ab_METAL()) translate([0, 0, 0.92]) rotate([90, 0, 0]) cylinder(h = W * 0.7, r = 0.1, center = true, $fn = 8);
    translate([0, 0, 0.98]) rotate([0, tilt, 0])
    {
        ab_plastic(ab_BLUEL()) ab_boxc([L, W, 0.18]);
        for (sx = [-1, 1])
            ab_plastic(ab_CREAM()) translate([sx * (L / 2 - 0.6), 0, 0.09]) ab_slab(1.0, W - 0.3, 0.02);
        for (sy = [-1, 1]) translate([0, sy * (W / 2 + 0.005), 0]) ab_part_hazard(L - 0.2, 0.16);
    }
}

// 开裂易碎石板（底面 z=0，顶面 z=0.4）：踩上后塌落的踏板
module ab_plat_crumble(L = 2, D = 2, seed = 0)
{
    ab_matte(ab_ROCKD()) ab_slab(L, D, 0.3);
    for (sx = [-1, 1], sy = [-1, 1])
        ab_matte(ab_ROCK())
            translate([sx * L / 4 + ab_rndr(seed + sx * 3 + sy, -0.03, 0.03), sy * D / 4 + ab_rndr(seed + sy * 5, -0.03, 0.03), 0.3])
                rotate([0, 0, ab_rndr(seed + sx + sy * 2, -3, 3)]) ab_slab(L / 2 - 0.14, D / 2 - 0.14, 0.1);
    for (i = [0 : 3])
        ab_matte(ab_ROCKD())
            translate([ab_rndr(seed * 5 + i * 7, -L / 2 + 0.4, L / 2 - 0.4), ab_rndr(seed * 3 + i * 11, -D / 2 + 0.4, D / 2 - 0.4), 0.4])
                rotate([0, 0, ab_rnd(seed + i, 180)]) ab_slab(0.6, 0.04, 0.012);
}

// 弹跳垫（底面 z=0）：金属底座 + 黄圈 + 红色橡胶穹顶 + 白色环纹；踩上弹起约 6 m
module ab_plat_bounce(r = 1.2)
{
    ab_metal(ab_METALD()) cylinder(h = 0.2, r = r + 0.18, $fn = 16);
    ab_plastic(ab_YELLOW()) translate([0, 0, 0.2]) cylinder(h = 0.1, r = r + 0.06, $fn = 16);
    ab_rubber(ab_RED()) translate([0, 0, 0.3]) ab_dome(r, 0.45);
    ab_plastic(ab_CREAM()) translate([0, 0, 0.3 + 0.45 * r * 0.87]) ab_torus(r * 0.5, 0.04);
    ab_plastic(ab_CREAM()) translate([0, 0, 0.3 + 0.45 * r - 0.02]) cylinder(h = 0.03, r = 0.12, $fn = 8);
}

// 弹簧发射台（底面 z=0，顶面 z=h）：金属线圈 + 蓝色顶盘
module ab_plat_spring(r = 0.8, h = 1.2)
{
    ab_metal(ab_METALD()) cylinder(h = 0.15, r = r + 0.2, $fn = 14);
    for (i = [0 : 4])
        ab_metal(ab_METAL()) translate([0, 0, 0.27 + i * (h - 0.55) / 4]) ab_torus(r, 0.09);
    ab_plastic(ab_BLUE()) translate([0, 0, h - 0.15]) cylinder(h = 0.15, r = r + 0.15, $fn = 14);
    ab_plastic(ab_CREAM()) translate([0, 0, h]) cylinder(h = 0.02, r = r * 0.6, $fn = 14);
}

// 滚筒（沿 x，底面 z=0）：A 形支架 + 分段双色旋转圆筒；筒顶 z = 2r + 0.3
module ab_plat_roller(L = 5, r = 1.0)
{
    H = r + 0.3;
    for (sx = [-1, 1])
    {
        for (sy = [-1, 1])
            ab_metal(ab_METALD()) translate([sx * (L / 2 + 0.3), sy * 0.5, H / 2]) rotate([sy * 18, 0, 0]) ab_boxc([0.14, 0.14, H / cos(18)]);
        ab_metal(ab_METALD()) translate([sx * (L / 2 + 0.3), 0, H * 0.5]) ab_boxc([0.14, 1.1, 0.1]);
    }
    ab_metal(ab_METAL()) translate([0, 0, H]) rotate([0, 90, 0]) cylinder(h = L + 0.9, r = 0.1, center = true, $fn = 8);
    n = max(2, floor(L / 0.8));
    for (i = [0 : n - 1])
        ab_plastic(i % 2 == 0 ? ab_CREAM() : ab_ORANGE())
            translate([-L / 2 + (i + 0.5) * L / n, 0, H]) rotate([0, 90, 0]) cylinder(h = L / n + 0.004, r = r, center = true, $fn = 14);
}

// 摆锤平台（底面 z=0）：门架（立柱在 ±y）+ 摆臂 + 黄色平台；ang 为当前摆角（xz 面内）
module ab_plat_pendulum(h = 7, arm = 5, ang = 25, w = 2.2)
{
    for (sy = [-1, 1])
    {
        ab_metal(ab_METALD()) translate([0, sy * (w / 2 + 1.2), 0]) cylinder(h = h, r = 0.16, $fn = 8);
        ab_metal(ab_METALD()) translate([0, sy * (w / 2 + 1.2), 0]) cylinder(h = 0.2, r = 0.5, $fn = 10);
    }
    ab_metal(ab_METALD()) translate([0, 0, h]) rotate([90, 0, 0]) cylinder(h = w + 2.8, r = 0.16, center = true, $fn = 8);
    translate([0, 0, h]) rotate([0, ang, 0])
    {
        ab_metal(ab_METAL()) translate([0, 0, -arm / 2]) cylinder(h = arm, r = 0.1, center = true, $fn = 8);
        ab_plastic(ab_YELLOW()) translate([0, 0, -arm]) ab_boxc([2.4, w, 0.3]);
        ab_plastic(ab_CHECKD()) translate([0, 0, -arm + 0.16]) ab_boxc([2.0, w - 0.3, 0.02]);
    }
}

// 绳桥（沿 x）：桥面端点 z≈0.1、中段下垂 sag（zMin 为负是预期）+ 两端立柱 + 分段扶手绳
module ab_plat_bridge(L = 8, W = 1.8, sag = 0.35)
{
    n = max(3, floor(L / 0.45));
    for (i = [0 : n - 1])
        ab_plastic(ab_WOODL())
            translate([-L / 2 + (i + 0.5) * L / n, 0, 0.1 - sag * sin((i + 0.5) / n * 180)])
                ab_boxc([L / n - 0.06, W, 0.08]);
    for (sx = [-1, 1], sy = [-1, 1])
        ab_plastic(ab_WOOD()) translate([sx * (L / 2 + 0.15), sy * (W / 2 + 0.08), -0.05]) cylinder(h = 1.25, r = 0.09, $fn = 8);
    for (sy = [-1, 1], i = [0 : n - 1])
        ab_rubber(ab_WOODD()) hull()
        {
            translate([-L / 2 + i * L / n, sy * (W / 2 + 0.08), 1.15 - sag * sin(i / n * 180)]) sphere(r = 0.03, $fn = 6);
            translate([-L / 2 + (i + 1) * L / n, sy * (W / 2 + 0.08), 1.15 - sag * sin((i + 1) / n * 180)]) sphere(r = 0.03, $fn = 6);
        }
}

// 传送带（沿 x，底面 z=0，带面 z=0.5）：深色橡胶带 + 黄色 V 形箭头（指向 +x）+ 两端滚轴
module ab_plat_conveyor(L = 6, W = 2)
{
    ab_metal(ab_METALD()) ab_bevel(L + 0.4, W + 0.3, 0.32, 0.06);
    ab_rubber(ab_DARK()) translate([0, 0, 0.32]) ab_slab(L, W - 0.1, 0.18);
    for (sx = [-1, 1])
        ab_metal(ab_METAL()) translate([sx * (L / 2), 0, 0.41]) rotate([90, 0, 0]) cylinder(h = W, r = 0.11, center = true, $fn = 10);
    n = max(1, floor(L / 1.0));
    for (i = [0 : n - 1])
        ab_plastic(ab_YELLOW()) translate([-L / 2 + 0.5 + i * 1.0, 0, 0.5]) linear_extrude(0.012)
            polygon([[-0.25, -W / 2 + 0.3], [0.05, -W / 2 + 0.3], [0.35, 0], [0.05, W / 2 - 0.3], [-0.25, W / 2 - 0.3], [0.05, 0]]);
}

// 圆柱立柱平台（底面 z=0，顶面 z=h）：环带装饰 + 顶部外扩盘
module ab_plat_pillar(h = 4, r = 1.2, c = ab_TEAL())
{
    ab_plastic(c) cylinder(h = h - 0.3, r = r * 0.8, $fn = 14);
    nb = max(0, floor((h - 1.5) / 1.2));
    for (i = [0 : nb])
        ab_plastic(ab_CREAM()) translate([0, 0, 0.8 + i * 1.2]) cylinder(h = 0.2, r = r * 0.83, $fn = 14);
    ab_plastic(c) translate([0, 0, h - 0.3]) hull()
    {
        cylinder(h = 0.05, r = r * 0.8, $fn = 14);
        translate([0, 0, 0.15]) cylinder(h = 0.15, r = r, $fn = 14);
    }
    ab_plastic(ab_CREAM()) translate([0, 0, h]) cylinder(h = 0.02, r = r - 0.2, $fn = 14);
}

// 滑索（沿 +x）：起点柱落 z=0（缆高 2.9），终点柱落 z=-drop；t 为滑车位置。zMin 为负是预期。
module ab_plat_zipline(L = 14, drop = 4, t = 0.3)
{
    for (p = [[0, 0], [L, -drop]])
    {
        ab_metal(ab_METALD()) translate([p[0], 0, p[1]]) cylinder(h = 0.15, r = 0.45, $fn = 10);
        ab_metal(ab_METAL()) translate([p[0], 0, p[1]]) cylinder(h = 3.1, r = 0.1, $fn = 8);
        ab_metal(ab_METAL()) translate([p[0], 0, p[1] + 3.05]) rotate([90, 0, 0]) cylinder(h = 0.9, r = 0.06, center = true, $fn = 8);
    }
    ab_rubber(ab_DARK()) hull()
    {
        translate([0, 0, 2.9]) sphere(r = 0.035, $fn = 6);
        translate([L, 0, 2.9 - drop]) sphere(r = 0.035, $fn = 6);
    }
    tx = t * L;
    tz = 2.9 - t * drop;
    translate([tx, 0, tz])
    {
        ab_plastic(ab_YELLOW()) translate([0, 0, 0.02]) ab_boxc([0.5, 0.2, 0.28]);
        ab_metal(ab_METAL()) translate([0, 0, -0.5]) cylinder(h = 0.9, r = 0.035, $fn = 6);
        ab_rubber(ab_DARK()) translate([0, 0, -0.52]) rotate([0, 90, 0]) cylinder(h = 0.7, r = 0.05, center = true, $fn = 8);
    }
}

// 单个字母积木块（底面 z=0）：圆角立方 + 四面浅色内嵌方
module ab_plat_toyblock(s = 1.5, c = ab_RED())
{
    ab_plastic(c) translate([0, 0, s / 2]) ab_rcube(s, s * 0.1);
    for (a = [0, 90, 180, 270])
        rotate([0, 0, a]) ab_plastic(ab_CREAM()) translate([0, -s / 2 - 0.005, s / 2]) ab_boxc([s * 0.6, 0.02, s * 0.6]);
    ab_plastic(ab_CREAM()) translate([0, 0, s]) ab_slab(s * 0.6, s * 0.6, 0.015);
}

// 字母积木叠塔（底面 z=0）：三块错位叠放，可当攀爬台阶
module ab_plat_toyblocks(seed = 0, s = 1.5)
{
    ab_plat_toyblock(s, ab_block_c(seed));
    translate([0.25, -0.15, s]) rotate([0, 0, 14]) ab_plat_toyblock(s, ab_block_c(seed + 7));
    translate([-0.2, 0.15, 2 * s]) rotate([0, 0, -20]) ab_plat_toyblock(s, ab_block_c(seed + 11));
}

// ================= 结构（bldg） =================

// 关卡终点门（front = -y，底面 z=0）：双层棋格圆台 + 蓝白糖果条纹立环 + 金星 + 两侧旗杆
module ab_bldg_goal(r = 2.6)
{
    for (k = [0 : 1])
        for (i = [0 : 11])
            ab_plastic((i + k) % 2 == 0 ? ab_CREAM() : ab_CHECKD())
                translate([0, 0, k * 0.3]) rotate([0, 0, i * 30]) rotate_extrude(angle = 30) square([4.2 - k * 1.0, 0.3]);
    zc = 0.6 + r + 0.2;
    for (i = [0 : 15])
        ab_gloss(i % 2 == 0 ? ab_BLUE() : ab_CREAM())
            translate([0, 0, zc]) rotate([90, 0, 0]) rotate([0, 0, i * 22.5]) rotate_extrude(angle = 22.5) translate([r, 0]) circle(r = 0.28);
    ab_gloss(ab_BLUEL()) translate([0, 0, 0.6]) cylinder(h = 0.03, r = r - 0.5, $fn = 20);
    ab_gold() translate([0, 0, zc + r + 0.28]) rotate([90, 0, 0]) translate([0, 0, -0.07]) ab_star(0.55, 0.24, 0.14);
    for (sx = [-1, 1]) translate([sx * (r + 1.3), 0, 0.3]) ab_prop_flag(4, sx + 2);
}

// 出生/降落平台（底面 z=0，顶面 z=0.25）：同心圆环 + 中心蓝盘 + 四盏地灯
module ab_bldg_startpad(r = 3.5)
{
    ab_plastic(ab_CHECKD()) cylinder(h = 0.25, r = r, $fn = 20);
    ab_gloss(ab_CREAM()) translate([0, 0, 0.25]) cylinder(h = 0.03, r = r - 0.3, $fn = 20);
    for (rr = [r - 0.9, r - 1.8])
        ab_gloss(ab_BLUE()) translate([0, 0, 0.28]) rotate_extrude() translate([rr - 0.12, 0]) square([0.24, 0.02]);
    ab_gloss(ab_BLUE()) translate([0, 0, 0.28]) cylinder(h = 0.02, r = 0.7, $fn = 16);
    for (a = [45, 135, 225, 315])
        rotate([0, 0, a]) translate([r + 0.5, 0, 0])
        {
            ab_metal(ab_METALD()) cylinder(h = 0.5, r = 0.07, $fn = 6);
            ab_gloss(ab_BLUEL()) translate([0, 0, 0.55]) sphere(r = 0.12, $fn = 8);
        }
}

// 主角飞船（机头朝 +x，底面 z=0，长约 6 m）：白色胶囊机身 + 玻璃座舱 + 后掠蓝翼 + 双引擎 + 三点起落架
module ab_bldg_ship(seed = 0)
{
    ab_gloss(ab_WHITE()) translate([0, 0, 1.4]) rotate([0, 90, 0]) hull()
    {
        translate([0, 0, -2.2]) sphere(r = 0.85, $fn = 14);
        translate([0, 0, 1.6]) sphere(r = 0.75, $fn = 14);
        translate([0, 0, 2.7]) sphere(r = 0.35, $fn = 10);
    }
    ab_glass(ab_BLUEL()) translate([0.9, 0, 1.95]) ab_ellipsoid(1.1, 0.68, 0.55);
    ab_gloss(ab_BLUE()) translate([0, 0, 1.35]) rotate([0, 90, 0]) hull()
    {
        translate([0, 0, -2.0]) sphere(r = 0.5, $fn = 10);
        translate([0, 0, 1.0]) sphere(r = 0.4, $fn = 10);
    }
    for (sy = [-1, 1])
    {
        ab_gloss(ab_BLUE()) translate([-0.6, sy * 1.6, 1.15]) rotate([0, 0, sy * -18]) ab_boxc([2.4, 2.4, 0.14]);
        ab_gloss(ab_CREAM()) translate([-0.9, sy * 2.5, 1.23]) rotate([0, 0, sy * -18]) ab_boxc([1.2, 0.5, 0.02]);
        ab_metal(ab_METALD()) translate([-2.5, sy * 0.85, 1.25]) rotate([0, 90, 0]) cylinder(h = 1.1, r = 0.38, center = true, $fn = 12);
        ab_gloss(ab_ORANGE()) translate([-3.06, sy * 0.85, 1.25]) rotate([0, 90, 0]) cylinder(h = 0.05, r = 0.28, $fn = 12);
    }
    ab_gloss(ab_RED()) translate([-2.0, 0, 2.5]) rotate([0, 12, 0]) ab_boxc([1.2, 0.12, 1.1]);
    for (p = [[1.6, 0], [-1.4, 0.9], [-1.4, -0.9]])
    {
        ab_metal(ab_METAL()) translate([p[0], p[1], 0.1]) cylinder(h = 0.8, r = 0.06, $fn = 6);
        ab_rubber(ab_DARK()) translate([p[0], p[1], 0]) cylinder(h = 0.12, r = 0.22, $fn = 8);
    }
}

// 玩具灯塔（底面 z=0）：分段双色圆柱 + 阳台圈 + 锥顶 + 金球；顶面阳台可作滑索起点
module ab_bldg_tower(h = 8, r = 1.5, seed = 0)
{
    n = 4;
    c1 = ab_block_c(seed);
    c2 = ab_CREAM();
    for (i = [0 : n - 1])
        ab_plastic(i % 2 == 0 ? c1 : c2) translate([0, 0, i * h / n]) cylinder(h = h / n + 0.01, r1 = r - i * 0.08, r2 = r - (i + 1) * 0.08, $fn = 12);
    for (i = [1 : n - 1])
        ab_plastic(ab_CHECKD()) translate([0, 0, i * h / n - 0.06]) cylinder(h = 0.12, r = r - i * 0.08 + 0.12, $fn = 12);
    ab_plastic(ab_CREAM()) translate([0, 0, h]) cylinder(h = 0.2, r = r + 0.4, $fn = 12);
    for (i = [0 : 9])
        rotate([0, 0, i * 36]) ab_plastic(c1) translate([r + 0.3, 0, h + 0.2]) cylinder(h = 0.9, r = 0.04, $fn = 6);
    ab_plastic(c1) translate([0, 0, h + 1.05]) ab_torus(r + 0.3, 0.04);
    ab_gloss(ab_BLUEL()) translate([0, 0, h + 0.2]) cylinder(h = 1.0, r = r * 0.42, $fn = 10);
    ab_plastic(c1) translate([0, 0, h + 1.2]) cylinder(h = 1.6, r1 = r + 0.2, r2 = 0.05, $fn = 12);
    ab_gold() translate([0, 0, h + 2.85]) sphere(r = 0.18, $fn = 8);
    for (i = [0 : 2])
        ab_plastic(ab_DARK()) rotate([0, 0, i * 120 - 90]) translate([r - 0.05, 0, 1.2 + i * 1.9]) rotate([0, 90, 0]) cylinder(h = 0.16, r = 0.26, $fn = 8);
}

// 风车（front = -y，底面 z=0）：锥台塔身 + 圆锥顶 + 四叶风车叶
module ab_bldg_windmill(h = 7, seed = 0)
{
    c = ab_wall_c(seed);
    ab_plastic(c) cylinder(h = h, r1 = 2.2, r2 = 1.5, $fn = 12);
    ab_plastic(ab_roof_c(seed + 2)) translate([0, 0, h]) cylinder(h = 1.8, r1 = 1.75, r2 = 0.08, $fn = 12);
    ab_plastic(ab_WOODD()) translate([0, -1.95, 1.0]) rotate([90, 0, 0]) cylinder(h = 0.24, r = 0.55, $fn = 10, center = true);
    ab_plastic(ab_WOODD()) translate([0, -2.0, 0.5]) ab_boxc([1.1, 0.24, 1.0]);
    ab_metal(ab_METALD()) translate([0, -1.6, h + 0.3]) rotate([90, 0, 0]) cylinder(h = 1.0, r = 0.12, $fn = 8);
    translate([0, -2.5, h + 0.3])
    {
        ab_metal(ab_METAL()) rotate([90, 0, 0]) cylinder(h = 0.4, r = 0.3, $fn = 10, center = true);
        for (i = [0 : 3])
            rotate([0, i * 90 + 20, 0])
            {
                ab_plastic(ab_WOOD()) translate([0, 0, 1.7]) ab_boxc([0.16, 0.12, 3.4]);
                ab_plastic(ab_CREAM()) translate([0.45, 0, 2.1]) ab_boxc([0.75, 0.05, 2.4]);
            }
    }
}

// 糖果拱门（沿 x 跨越，底面 z=0）：双柱 + 半环条纹拱
module ab_bldg_arch(w = 5, h = 5, seed = 0)
{
    r = w / 2;
    c1 = ab_block_c(seed);
    for (sx = [-1, 1])
    {
        ab_plastic(ab_CREAM()) translate([sx * r, 0, 0]) cylinder(h = 0.3, r = 0.7, $fn = 10);
        ab_plastic(c1) translate([sx * r, 0, 0.3]) cylinder(h = h - r - 0.3, r = 0.42, $fn = 10);
    }
    for (i = [0 : 5])
        ab_plastic(i % 2 == 0 ? c1 : ab_CREAM())
            translate([0, 0, h - r]) rotate([90, 0, 0]) rotate([0, 0, i * 30]) rotate_extrude(angle = 30) translate([r, 0]) circle(r = 0.42);
}

// 可击碎积木墙（沿 x，底面 z=0）：错缝彩色砖块 + 中央裂纹标记（拳击后露出金币）
module ab_bldg_wall_break(L = 4, h = 3, seed = 0)
{
    bw = 0.8;
    bh = 0.5;
    rows = max(1, floor(h / bh));
    cols = max(1, floor(L / bw));
    for (r = [0 : rows - 1], c = [0 : cols - 1])
    {
        off = (r % 2) * bw / 2;
        bx = -L / 2 + off + (c + 0.5) * bw;
        if (bx + bw / 2 <= L / 2 + 0.01)
            ab_plastic(ab_block_c(seed + r * 17 + c * 5))
                translate([bx, ab_rndr(seed + r * 7 + c, -0.02, 0.02), r * bh])
                    rotate([0, 0, ab_rndr(seed + r * 3 + c * 11, -2, 2)]) ab_bevel(bw - 0.06, 0.6, bh - 0.04, 0.05);
    }
    for (a = [45, -45])
        ab_matte(ab_DARK()) translate([0, -0.31, h / 2]) rotate([0, a, 0]) ab_boxc([0.08, 0.02, 1.4]);
}

// 大管道（沿 x，底面 z=0，空心可穿行）：圆管 + 两端法兰 + 鞍座
module ab_bldg_pipe(L = 6, r = 1.5, c = ab_GREENL())
{
    ab_plastic(c) translate([0, 0, r + 0.2]) rotate([0, 90, 0]) difference()
    {
        cylinder(h = L, r = r, center = true, $fn = 16);
        cylinder(h = L + 0.2, r = r - 0.15, center = true, $fn = 16);
    }
    for (sx = [-1, 1])
        ab_plastic(ab_CREAM()) translate([sx * (L / 2 - 0.2), 0, r + 0.2]) rotate([0, 90, 0]) difference()
        {
            cylinder(h = 0.4, r = r + 0.18, center = true, $fn = 16);
            cylinder(h = 0.6, r = r - 0.15, center = true, $fn = 16);
        }
    for (sx = [-1, 1])
        ab_metal(ab_METALD()) translate([sx * (L / 2 - 1.0), 0, 0]) ab_slab(0.6, r * 1.6, 0.25);
}

// 竖管入口（底面 z=0）：绿色竖管 + 顶部法兰 + 深色洞口
module ab_bldg_pipe_up(h = 2.2, r = 1.2, c = ab_GREENL())
{
    ab_plastic(c) difference()
    {
        cylinder(h = h - 0.4, r = r, $fn = 16);
        translate([0, 0, 0.2]) cylinder(h = h, r = r - 0.15, $fn = 16);
    }
    ab_plastic(c) translate([0, 0, h - 0.4]) difference()
    {
        cylinder(h = 0.4, r = r + 0.15, $fn = 16);
        translate([0, 0, -0.1]) cylinder(h = 0.6, r = r - 0.15, $fn = 16);
    }
    ab_matte(ab_DARK()) translate([0, 0, 0.2]) cylinder(h = 0.02, r = r - 0.15, $fn = 16);
}

// 玩具小屋（front = -y，底面 z=0）：圆角彩墙 + 双坡顶 + 拱门 + 圆窗 + 烟囱
module ab_bldg_house(seed = 0, L = 5, D = 4.5)
{
    wh = 2.8;
    wc = ab_wall_c(seed);
    rc = ab_roof_c(seed + 3);
    ab_plastic(ab_CREAM()) ab_bevel(L + 0.5, D + 0.5, 0.2, 0.05);
    ab_plastic(wc) translate([0, 0, 0.2]) ab_rbox(L, D, wh - 0.2, 0.35);
    translate([0, 0, wh]) ab_part_roof(L, D, 2.0, 0.6, rc);
    ab_plastic(rc) translate([0, 0, wh + 1.95]) ab_boxc([L * 0.7, 0.3, 0.16]);
    ab_plastic(ab_CREAM()) translate([L * 0.28, D * 0.15, wh + 0.8]) ab_boxc([0.5, 0.5, 1.6]);
    ab_plastic(ab_CHECKD()) translate([L * 0.28, D * 0.15, wh + 1.6]) ab_boxc([0.62, 0.62, 0.12]);
    ab_plastic(ab_CHECKD()) translate([0, -D / 2 - 0.02, 0.2]) rotate([90, 0, 0])
    {
        translate([0, 0.7, 0]) cylinder(h = 0.1, r = 0.55, $fn = 12, center = true);
        translate([0, 0.35, 0]) ab_boxc([1.1, 0.7, 0.1]);
    }
    ab_gold() translate([0.35, -D / 2 - 0.09, 1.1]) sphere(r = 0.06, $fn = 6);
    for (sx = [-1, 1])
    {
        ab_plastic(ab_CREAM()) translate([sx * L * 0.32, -D / 2 - 0.03, 1.9]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.45, $fn = 12, center = true);
        ab_gloss(ab_BLUEL()) translate([sx * L * 0.32, -D / 2 - 0.05, 1.9]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.34, $fn = 12, center = true);
    }
    ab_plastic(ab_CREAM()) translate([L / 2 + 0.03, 0, 1.9]) rotate([0, 90, 0]) cylinder(h = 0.1, r = 0.4, $fn = 12, center = true);
    ab_gloss(ab_BLUEL()) translate([L / 2 + 0.05, 0, 1.9]) rotate([0, 90, 0]) cylinder(h = 0.1, r = 0.3, $fn = 12, center = true);
}

// 门架（横梁沿 x，跨度 w，底面 z=0）：吊挂刺球/秋千用
module ab_bldg_gantry(w = 6, h = 6)
{
    for (sx = [-1, 1])
    {
        ab_metal(ab_METALD()) translate([sx * w / 2, 0, 0]) cylinder(h = 0.2, r = 0.5, $fn = 10);
        ab_metal(ab_METALD()) translate([sx * w / 2, 0, 0]) cylinder(h = h, r = 0.16, $fn = 8);
    }
    ab_metal(ab_METALD()) translate([0, 0, h]) rotate([0, 90, 0]) cylinder(h = w + 0.4, r = 0.16, center = true, $fn = 8);
    for (sx = [-1, 1])
        ab_metal(ab_METALD()) translate([sx * (w / 2 - 0.6), 0, h - 0.6]) rotate([0, sx * 45, 0]) ab_boxc([0.1, 0.1, 1.6]);
}

// 广告牌（front = -y，底面 z=0）：双柱 + 圆角彩板 + 大箭头图案
module ab_bldg_billboard(seed = 0, dir = 0)
{
    c = ab_sign_c(seed);
    for (sx = [-1, 1]) ab_metal(ab_METALD()) translate([sx * 1.6, 0, 0]) cylinder(h = 3.2, r = 0.1, $fn = 8);
    ab_plastic(ab_CREAM()) translate([0, -0.1, 2.6]) rotate([90, 0, 0]) translate([0, 0, -0.1]) ab_rbox(4.6, 2.6, 0.2, 0.3);
    ab_plastic(c) translate([0, -0.22, 2.6]) rotate([90, 0, 0]) translate([0, 0, -0.02]) ab_rbox(4.2, 2.2, 0.04, 0.25);
    ab_plastic(ab_CREAM()) translate([0, -0.26, 2.6]) rotate([0, 0, dir]) rotate([90, 0, 0]) linear_extrude(0.03)
        polygon([[-1.3, -0.4], [0.5, -0.4], [0.5, -0.8], [1.5, 0], [0.5, 0.8], [0.5, 0.4], [-1.3, 0.4]]);
}

// ================= 植被地景（nature） =================

// 球冠树（底面 z=0，s=1 高约 5.2 m）：多球团冠、粉彩绿
module ab_nature_tree_ball(s = 1.0, seed = 0)
{
    c1 = ab_leaf_c(seed);
    c2 = ab_leaf_c(seed + 5);
    scale([s, s, s])
    {
        ab_matte(ab_TRUNK()) cylinder(h = 2.4, r1 = 0.28, r2 = 0.18, $fn = 7);
        ab_leaf(c1) translate([0, 0, 3.2]) sphere(r = 1.5, $fn = 10);
        ab_leaf(c2) translate([0.8, 0.4, 3.9]) sphere(r = 0.95, $fn = 8);
        ab_leaf(c1) translate([-0.7, -0.5, 3.8]) sphere(r = 0.85, $fn = 8);
        ab_leaf(c2) translate([0, 0, 4.6]) sphere(r = 0.7, $fn = 8);
    }
}

// 锥冠树（底面 z=0，s=1 高约 5 m）：三层圆锥叠冠
module ab_nature_tree_cone(s = 1.0, seed = 0)
{
    c1 = ab_leaf_c(seed);
    c2 = ab_leaf_c(seed + 3);
    scale([s, s, s])
    {
        ab_matte(ab_TRUNK()) cylinder(h = 1.4, r = 0.2, $fn = 7);
        ab_leaf(c1) translate([0, 0, 1.0]) cylinder(h = 1.7, r1 = 1.5, r2 = 0.9, $fn = 8);
        ab_leaf(c2) translate([0, 0, 2.3]) cylinder(h = 1.6, r1 = 1.15, r2 = 0.55, $fn = 8);
        ab_leaf(c1) translate([0, 0, 3.5]) cylinder(h = 1.5, r1 = 0.75, r2 = 0.05, $fn = 8);
    }
}

// 棕榈树（底面 z=0，s=1 高约 6 m）：倾斜分段树干 + 放射叶片 + 椰子
module ab_nature_palm(s = 1.0, seed = 0)
{
    lean = ab_rnd(seed, 360);
    scale([s, s, s])
    {
        for (i = [0 : 4])
            ab_matte(ab_TRUNK()) rotate([0, 0, lean]) translate([i * i * 0.05, 0, i * 1.05]) rotate([0, i * 4, 0]) cylinder(h = 1.15, r1 = 0.26 - i * 0.02, r2 = 0.22 - i * 0.02, $fn = 7);
        rotate([0, 0, lean]) translate([0.9, 0, 5.4])
        {
            for (i = [0 : 6])
                rotate([0, 0, i * 360 / 7 + 10]) rotate([0, 28, 0]) ab_leaf([0.24, 0.50, 0.24]) translate([1.5, 0, 0]) ab_ellipsoid(1.6, 0.42, 0.08);
            for (i = [0 : 2])
                ab_matte([0.44, 0.30, 0.16]) rotate([0, 0, i * 120]) translate([0.32, 0, -0.25]) sphere(r = 0.22, $fn = 7);
        }
    }
}

// 灌木（底面 z=0）：三球 hull 团
module ab_nature_bush(s = 1.0, seed = 0)
{
    c = ab_leaf_c(seed + 2);
    scale([s, s, s]) ab_leaf(c) hull()
    {
        translate([0, 0, 0.55]) sphere(r = 0.6, $fn = 8);
        translate([0.5, 0.2, 0.45]) sphere(r = 0.45, $fn = 8);
        translate([-0.4, -0.3, 0.5]) sphere(r = 0.5, $fn = 8);
    }
}

// 大花（底面 z=0，s=1 高约 1.6 m）：茎 + 两片叶 + 六瓣花 + 花心
module ab_nature_flower(s = 1.0, seed = 0)
{
    pc = ab_flower_c(seed);
    scale([s, s, s])
    {
        ab_matte([0.30, 0.50, 0.22]) cylinder(h = 1.4, r = 0.05, $fn = 6);
        for (i = [0 : 1])
            ab_leaf([0.34, 0.56, 0.24]) rotate([0, 0, i * 160 + 30]) translate([0.25, 0, 0.5 + i * 0.35]) rotate([0, -30, 0]) ab_ellipsoid(0.32, 0.14, 0.04);
        for (i = [0 : 5])
            ab_gloss(pc) rotate([0, 0, i * 60]) translate([0.32, 0, 1.45]) ab_ellipsoid(0.32, 0.2, 0.06);
        ab_gloss(ab_YELLOW()) translate([0, 0, 1.47]) sphere(r = 0.16, $fn = 8);
    }
}

// 草丛（底面 z=0）：几根倾斜细锥叶
module ab_nature_grass_tuft(seed = 0)
{
    for (i = [0 : 5])
        ab_leaf(i % 2 == 0 ? ab_GREENL() : ab_GRASSD())
            rotate([0, 0, i * 60 + ab_rnd(seed + i, 30)]) translate([0.1, 0, 0]) rotate([0, 22 + ab_rnd(seed + i * 3, 12), 0])
                cylinder(h = 0.45 + ab_rndf(seed + i * 7) * 0.3, r1 = 0.05, r2 = 0.005, $fn = 4);
}

// 低模巨石（底面 z=0）：两块面数极低的拉伸球体
module ab_nature_rock(s = 1.0, seed = 0)
{
    rz = ab_rnd(seed, 360);
    scale([s, s, s])
    {
        ab_matte(ab_ROCK()) translate([0, 0, 0.55]) rotate([0, 0, rz]) scale([1.3, 1.0, 0.75]) sphere(r = 0.8, $fn = 6);
        ab_matte(ab_ROCKD()) translate([0.7, 0.4, 0.3]) rotate([0, 0, rz + 40]) scale([0.9, 0.8, 0.6]) sphere(r = 0.5, $fn = 5);
    }
}

// 大蘑菇（底面 z=0，s=1 高约 1.8 m）：奶白菌柄 + 红色菌盖 + 白点；菌盖可弹跳
module ab_nature_mushroom(s = 1.0, seed = 0)
{
    cc = ab_rnd(seed, 3) == 0 ? ab_ORANGE() : (ab_rnd(seed, 3) == 1 ? ab_PINK() : ab_RED());
    scale([s, s, s])
    {
        ab_plastic(ab_CREAM()) cylinder(h = 1.1, r1 = 0.36, r2 = 0.28, $fn = 10);
        ab_gloss(cc) translate([0, 0, 1.0]) ab_dome(1.15, 0.62);
        for (i = [0 : 4])
            ab_gloss(ab_CREAM()) rotate([0, 0, i * 72 + ab_rnd(seed + i, 40)]) translate([0.55, 0, 1.0 + 0.62 * 1.01]) ab_ellipsoid(0.16, 0.16, 0.05);
        ab_gloss(ab_CREAM()) translate([0, 0, 1.0 + 0.62 * 1.15 - 0.03]) ab_ellipsoid(0.2, 0.2, 0.05);
    }
}

// 云朵（底面 z=0，可悬浮摆放）：五球团簇、高光白
module ab_nature_cloud(s = 1.0, seed = 0)
{
    scale([s, s, s]) ab_gloss(ab_CLOUD())
    {
        translate([0, 0, 1.0]) sphere(r = 1.0, $fn = 10);
        translate([1.1, 0.2, 0.8]) sphere(r = 0.8, $fn = 10);
        translate([-1.1, -0.1, 0.75]) sphere(r = 0.75, $fn = 10);
        translate([0.4, -0.5, 0.65]) sphere(r = 0.65, $fn = 10);
        translate([-0.3, 0.6, 0.7]) sphere(r = 0.7, $fn = 10);
    }
}

// 巨型水果（底面 z=0，s=1 直径约 1.8 m）：kind 0 苹果 / 1 橙子 / 2 草莓
module ab_nature_fruit(kind = 0, s = 1.0)
{
    scale([s, s, s])
    {
        if (kind == 0)
        {
            ab_gloss(ab_RED()) translate([0, 0, 0.85]) ab_ellipsoid(0.9, 0.9, 0.85);
            ab_matte(ab_TRUNK()) translate([0, 0, 1.55]) rotate([0, 12, 0]) cylinder(h = 0.45, r = 0.05, $fn = 6);
            ab_leaf([0.34, 0.56, 0.24]) translate([0.25, 0, 1.85]) rotate([0, -25, 0]) ab_ellipsoid(0.32, 0.16, 0.04);
        }
        else if (kind == 1)
        {
            ab_gloss(ab_ORANGE()) translate([0, 0, 0.85]) sphere(r = 0.85, $fn = 14);
            ab_matte(ab_TRUNK()) translate([0, 0, 1.62]) cylinder(h = 0.2, r = 0.06, $fn = 6);
            ab_leaf([0.30, 0.52, 0.24]) translate([0.22, 0, 1.75]) rotate([0, -20, 0]) ab_ellipsoid(0.3, 0.14, 0.04);
        }
        else
        {
            ab_gloss(ab_RED()) hull()
            {
                translate([0, 0, 1.0]) sphere(r = 0.75, $fn = 12);
                translate([0, 0, 0.25]) sphere(r = 0.25, $fn = 8);
            }
            for (i = [0 : 7])
                ab_gloss(ab_YELLOW()) rotate([0, 0, i * 45]) translate([0.55, 0, 0.5 + (i % 3) * 0.3]) sphere(r = 0.05, $fn = 5);
            for (i = [0 : 5])
                ab_leaf([0.30, 0.52, 0.24]) rotate([0, 0, i * 60]) translate([0.35, 0, 1.68]) rotate([0, -10, 0]) ab_ellipsoid(0.42, 0.14, 0.04);
        }
    }
}

// 仙人掌（底面 z=0，s=1 高约 2.4 m）：胶囊主干 + 两臂 + 顶花
module ab_nature_cactus(s = 1.0, seed = 0)
{
    c = [0.26, 0.50, 0.30];
    scale([s, s, s])
    {
        ab_plastic(c) ab_capsule_up(0.32, 2.4);
        for (sx = [-1, 1])
        {
            ab_plastic(c) translate([sx * 0.32, 0, 1.0 + (sx + 1) * 0.2]) rotate([0, sx * 90, 0]) cylinder(h = 0.55, r = 0.18, $fn = 8);
            ab_plastic(c) translate([sx * 0.8, 0, 1.0 + (sx + 1) * 0.2]) ab_capsule_up(0.18, 0.9);
        }
        ab_gloss(ab_PINK()) translate([0, 0, 2.42]) sphere(r = 0.2, $fn = 8);
    }
}

// 竖直胶囊（底面 z=0）
module ab_capsule_up(r = 0.3, h = 1)
{
    hull()
    {
        translate([0, 0, r]) sphere(r = r, $fn = 10);
        translate([0, 0, max(r, h - r)]) sphere(r = r, $fn = 10);
    }
}

// 水晶簇（底面 z=0）：倾斜六棱锥柱，高光材质
module ab_nature_crystal(s = 1.0, seed = 0)
{
    c = ab_rnd(seed, 2) == 0 ? [0.55, 0.78, 0.86] : [0.72, 0.52, 0.86];
    scale([s, s, s])
    {
        ab_matte(ab_ROCKD()) cylinder(h = 0.25, r = 0.7, $fn = 6);
        ab_glass(c) cylinder(h = 1.6, r1 = 0.28, r2 = 0.05, $fn = 6);
        for (i = [0 : 2])
            ab_glass(c) rotate([0, 0, i * 120 + ab_rnd(seed, 60)]) translate([0.35, 0, 0.1]) rotate([0, 28, 0]) cylinder(h = 0.9 + i * 0.15, r1 = 0.18, r2 = 0.03, $fn = 6);
    }
}

// 睡莲叶（底面 z=0，放水面上）：带缺口圆叶 + 小花
module ab_nature_lilypad(r = 0.9, seed = 0)
{
    ab_leaf([0.28, 0.52, 0.26]) difference()
    {
        cylinder(h = 0.05, r = r, $fn = 12);
        rotate([0, 0, ab_rnd(seed, 360)]) translate([r * 0.7, 0, -0.1]) cylinder(h = 0.3, r = r * 0.3, $fn = 6);
    }
    ab_gloss(ab_PINK()) translate([-r * 0.3, r * 0.2, 0.05]) sphere(r = 0.16, $fn = 8);
}

// ================= 道具机关（prop） =================

// 木箱（底面 z=0）：可拳击打碎，内藏金币
module ab_prop_crate(s = 1.0, seed = 0)
{
    c = ab_rnd(seed, 3) == 0 ? ab_WOODL() : ab_WOOD();
    scale([s, s, s])
    {
        ab_plastic(c) translate([0, 0, 0.5]) ab_rcube(1.0, 0.06);
        for (a = [0, 90, 180, 270])
            rotate([0, 0, a]) ab_plastic(ab_WOODD())
            {
                translate([0, -0.5, 0.5]) ab_boxc([1.02, 0.04, 0.1]);
                translate([0, -0.5, 0.06]) ab_boxc([1.02, 0.04, 0.08]);
                translate([0, -0.5, 0.94]) ab_boxc([1.02, 0.04, 0.08]);
                translate([-0.47, -0.5, 0.5]) ab_boxc([0.08, 0.04, 1.02]);
            }
    }
}

// 牢笼（底面 z=0）：金属栏杆 + 穹顶 + 吊环，内困一名挥手求救的机器人
module ab_prop_cage(seed = 0, r = 1.1, h = 2.3)
{
    ab_metal(ab_METALD()) cylinder(h = 0.12, r = r + 0.15, $fn = 12);
    for (i = [0 : 9])
        ab_metal(ab_METAL()) rotate([0, 0, i * 36]) translate([r, 0, 0.12]) cylinder(h = h - 0.3, r = 0.04, $fn = 6);
    ab_metal(ab_METALD()) translate([0, 0, h - 0.3]) ab_torus(r, 0.06);
    ab_metal(ab_METALD()) translate([0, 0, h - 0.3]) difference()
    {
        ab_dome(r + 0.05, 0.5);
        translate([0, 0, -0.1]) ab_dome(r - 0.05, 0.5);
    }
    ab_metal(ab_METAL()) translate([0, 0, h - 0.3 + (r + 0.05) * 0.5 + 0.12]) rotate([90, 0, 0]) ab_torus(0.16, 0.035);
    translate([0, 0, 0.12]) ab_char_bot(seed = seed, pose = 1);
}

// 箭头路牌（底面 z=0）：木杆 + 彩色箭头板（dir 为箭头朝向角，0 = +x）
module ab_prop_sign_arrow(seed = 0, dir = 0)
{
    ab_plastic(ab_WOOD()) cylinder(h = 1.9, r = 0.07, $fn = 8);
    ab_plastic(ab_CREAM()) translate([0, 0, 1.55]) rotate([0, 0, dir]) rotate([90, 0, 0]) translate([0, 0, -0.05]) linear_extrude(0.1)
        polygon([[-0.78, -0.28], [0.35, -0.28], [0.35, -0.46], [0.9, 0], [0.35, 0.46], [0.35, 0.28], [-0.78, 0.28]]);
    ab_plastic(ab_sign_c(seed)) translate([0, 0, 1.55]) rotate([0, 0, dir]) rotate([90, 0, 0]) translate([0, 0, -0.065]) linear_extrude(0.13)
        polygon([[-0.7, -0.2], [0.35, -0.2], [0.35, -0.36], [0.78, 0], [0.35, 0.36], [0.35, 0.2], [-0.7, 0.2]]);
}

// 圆角告示牌（front = -y，底面 z=0）：双杆 + 彩板 + 白色图标块
module ab_prop_sign_board(seed = 0)
{
    for (sx = [-1, 1]) ab_plastic(ab_WOOD()) translate([sx * 0.55, 0, 0]) cylinder(h = 1.6, r = 0.05, $fn = 8);
    ab_plastic(ab_CREAM()) translate([0, -0.06, 1.35]) rotate([90, 0, 0]) translate([0, 0, -0.05]) ab_rbox(1.6, 1.0, 0.1, 0.15);
    ab_plastic(ab_sign_c(seed)) translate([0, -0.14, 1.35]) rotate([90, 0, 0]) translate([0, 0, -0.02]) ab_rbox(1.4, 0.8, 0.04, 0.12);
    ab_plastic(ab_CREAM()) translate([0, -0.17, 1.42]) ab_boxc([0.5, 0.02, 0.14]);
    ab_plastic(ab_CREAM()) translate([0, -0.17, 1.2]) ab_boxc([0.8, 0.02, 0.08]);
}

// 旗杆（底面 z=0）：杆 + 三角旗 + 金顶
module ab_prop_flag(h = 4, seed = 0)
{
    ab_metal(ab_METAL()) cylinder(h = h, r = 0.05, $fn = 8);
    ab_gold() translate([0, 0, h]) sphere(r = 0.09, $fn = 6);
    ab_plastic(ab_balloon_c(seed)) translate([0.05, 0, h - 0.1]) rotate([90, 0, 0]) translate([0, 0, -0.015]) linear_extrude(0.03)
        polygon([[0, 0], [1.3, -0.3], [0, -0.7]]);
}

// 玩具围栏（沿 x，底面 z=0）：球顶立柱 + 双横栏
module ab_prop_fence(len = 4, c = ab_CREAM())
{
    n = max(1, floor(len / 1.3));
    for (i = [0 : n])
    {
        ab_plastic(c) translate([-len / 2 + i * len / n, 0, 0]) cylinder(h = 0.95, r = 0.06, $fn = 8);
        ab_gloss(ab_BLUE()) translate([-len / 2 + i * len / n, 0, 1.0]) sphere(r = 0.09, $fn = 8);
    }
    for (z = [0.4, 0.78])
        ab_plastic(c) translate([0, 0, z]) rotate([0, 90, 0]) cylinder(h = len, r = 0.04, center = true, $fn = 6);
}

// 路灯（底面 z=0）：弯头灯杆 + 奶白灯罩球
module ab_prop_lamp(h = 3.2)
{
    ab_metal(ab_METALD()) cylinder(h = 0.15, r = 0.3, $fn = 10);
    ab_metal(ab_METALD()) cylinder(h = h, r = 0.07, $fn = 8);
    ab_metal(ab_METALD()) translate([0, 0, h]) rotate([0, 90, 0]) cylinder(h = 0.6, r = 0.06, $fn = 8);
    ab_gloss([0.86, 0.84, 0.72]) translate([0.6, 0, h - 0.02]) sphere(r = 0.34, $fn = 12);
}

// 地刺条（沿 x，底面 z=0）：深色底板 + 两排镀铬尖刺
module ab_prop_spikes(len = 3, w = 1.0)
{
    ab_metal(ab_METALD()) ab_bevel(len, w, 0.12, 0.03);
    n = max(1, floor(len / 0.5));
    for (i = [0 : n - 1], sy = [-1, 1])
        ab_chrome(ab_METAL()) translate([-len / 2 + (i + 0.5) * len / n, sy * w * 0.25, 0.12]) cylinder(h = 0.45, r1 = 0.14, r2 = 0.01, $fn = 8);
}

// 刺球（底面 z=0，球心 z=r+0.35）：金属球 + 14 根锥刺
module ab_prop_spike_ball(r = 0.7)
{
    translate([0, 0, r + 0.35])
    {
        ab_metal(ab_METALD()) sphere(r = r, $fn = 12);
        for (a = [[0, 0], [180, 0], [90, 0], [-90, 0], [0, 90], [0, -90],
                  [45, 45], [45, -45], [-45, 45], [-45, -45], [135, 45], [135, -45], [-135, 45], [-135, -45]])
            ab_chrome(ab_METAL()) rotate([a[0], a[1], 0]) translate([0, 0, r - 0.05]) cylinder(h = 0.4, r1 = 0.12, r2 = 0.01, $fn = 7);
    }
}

// 吊挂刺球（底面 z=0，吊钩顶 z=h）：挂在门架横梁下；链条 + 刺球
module ab_prop_spike_ball_chain(h = 4, r = 0.7)
{
    zb = 2 * r + 0.7;
    ab_metal(ab_METAL()) translate([0, 0, h - 0.15]) rotate([90, 0, 0]) ab_torus(0.14, 0.035);
    n = max(1, floor((h - 0.3 - zb) / 0.22));
    for (i = [0 : n - 1])
        ab_metal(ab_METAL()) translate([0, 0, zb + 0.1 + i * 0.22]) rotate([i % 2 == 0 ? 90 : 0, 0, 0]) ab_torus(0.09, 0.025);
    ab_prop_spike_ball(r);
}

// 风扇（front = -y，向 -y 吹风，底面 z=0）：底座 + 立杆 + 护圈 + 四叶
module ab_prop_fan(s = 1.0)
{
    scale([s, s, s])
    {
        ab_metal(ab_METALD()) cylinder(h = 0.15, r = 0.6, $fn = 12);
        ab_metal(ab_METALD()) cylinder(h = 1.3, r = 0.08, $fn = 8);
        translate([0, 0, 1.5])
        {
            ab_metal(ab_METAL()) rotate([90, 0, 0]) ab_torus(0.95, 0.05);
            ab_metal(ab_METAL()) rotate([90, 0, 0]) translate([0, 0, -0.1]) cylinder(h = 0.3, r = 0.16, $fn = 8);
            for (i = [0 : 3])
                ab_gloss(ab_BLUEL()) rotate([0, i * 90, 0]) translate([0.5, 0, 0]) rotate([15, 0, 0]) ab_boxc([0.75, 0.03, 0.34]);
            for (i = [0 : 3])
                ab_metal(ab_METAL()) rotate([0, i * 90 + 45, 0]) translate([0.47, 0, 0]) ab_boxc([0.95, 0.03, 0.03]);
        }
    }
}

// 地面大按钮（底面 z=0）：金属底座 + 黄圈 + 红色穹顶（踩下触发机关）
module ab_prop_button(r = 1.0)
{
    ab_metal(ab_METALD()) cylinder(h = 0.18, r = r + 0.2, $fn = 16);
    ab_plastic(ab_YELLOW()) translate([0, 0, 0.18]) cylinder(h = 0.08, r = r + 0.1, $fn = 16);
    ab_gloss(ab_RED()) translate([0, 0, 0.26]) ab_dome(r, 0.32);
}

// 拉杆开关（front = -y，底面 z=0）：基座 + 倾斜拉杆 + 红球
module ab_prop_lever()
{
    ab_metal(ab_METALD()) ab_bevel(0.6, 0.5, 0.35, 0.05);
    ab_metal(ab_METAL()) translate([0, 0, 0.35]) rotate([90, 0, 0]) cylinder(h = 0.4, r = 0.06, center = true, $fn = 8);
    translate([0, 0, 0.35]) rotate([0, -30, 0])
    {
        ab_metal(ab_METAL()) cylinder(h = 0.9, r = 0.035, $fn = 6);
        ab_gloss(ab_RED()) translate([0, 0, 0.92]) sphere(r = 0.1, $fn = 8);
    }
}

// 栅栏门（沿 x，front = -y，底面 z=0）：双柱 + 竖栏 + 顶梁 + 红色锁灯（按钮触发后升起）
module ab_prop_gate_bars(w = 4, h = 3)
{
    for (sx = [-1, 1])
        ab_metal(ab_METALD()) translate([sx * w / 2, 0, 0]) ab_bevel(0.4, 0.4, h + 0.3, 0.05);
    ab_metal(ab_METALD()) translate([0, 0, h + 0.15]) ab_boxc([w, 0.34, 0.3]);
    n = max(2, floor(w / 0.45));
    for (i = [1 : n - 1])
        ab_metal(ab_METAL()) translate([-w / 2 + i * w / n, 0, 0.05]) cylinder(h = h + 0.1, r = 0.04, $fn = 6);
    ab_metal(ab_METAL()) translate([0, 0, h * 0.5]) ab_boxc([w - 0.4, 0.06, 0.1]);
    ab_gloss(ab_RED()) translate([0, -0.2, h + 0.15]) sphere(r = 0.12, $fn = 8);
}

// 气球束（底面 z=0）：配重 + 细绳 + 彩色气球（可作悬浮踏点提示）
module ab_prop_balloon(seed = 0, n = 3)
{
    ab_metal(ab_METALD()) ab_rcube(0.3, 0.05);
    for (i = [0 : n - 1])
    {
        a = i * 360 / n + ab_rnd(seed, 60);
        px = 0.5 * sin(a);
        py = 0.5 * cos(a);
        hz = 2.4 + ab_rndf(seed + i * 7) * 0.8;
        ab_rubber(ab_DARK()) hull()
        {
            translate([0, 0, 0.3]) sphere(r = 0.015, $fn = 4);
            translate([px, py, hz - 0.45]) sphere(r = 0.015, $fn = 4);
        }
        ab_gloss(ab_balloon_c(seed + i * 3)) translate([px, py, hz]) ab_ellipsoid(0.4, 0.4, 0.48);
        ab_gloss(ab_balloon_c(seed + i * 3)) translate([px, py, hz - 0.5]) cylinder(h = 0.08, r1 = 0.03, r2 = 0.07, $fn = 6);
    }
}

// 弹珠台弹碰柱（底面 z=0）：红色橡胶柱 + 白环 + 奶白顶盖 + 星标
module ab_prop_bumper(r = 0.7)
{
    ab_rubber(ab_RED()) cylinder(h = 0.9, r = r, $fn = 14);
    ab_plastic(ab_CREAM()) translate([0, 0, 0.42]) cylinder(h = 0.14, r = r + 0.03, $fn = 14);
    ab_plastic(ab_CREAM()) translate([0, 0, 0.9]) cylinder(h = 0.12, r = r + 0.1, $fn = 14);
    ab_gloss(ab_YELLOW()) translate([0, 0, 1.02]) ab_star(r * 0.55, r * 0.24, 0.03);
}

// 宝箱（front = -y，底面 z=0）：木箱 + 半开箱盖 + 金币堆
module ab_prop_chest(seed = 0)
{
    ab_plastic(ab_WOOD()) ab_bevel(1.3, 0.9, 0.7, 0.05);
    for (sx = [-1, 1]) ab_metal(ab_METALD()) translate([sx * 0.45, 0, 0.35]) ab_boxc([0.08, 0.94, 0.72]);
    ab_gold() translate([0, 0, 0.72]) ab_slab(1.1, 0.7, 0.16);
    for (i = [0 : 5])
        ab_gold() translate([ab_rndr(seed + i * 3, -0.4, 0.4), ab_rndr(seed + i * 5 + 1, -0.2, 0.2), 0.88]) cylinder(h = 0.04, r = 0.12, $fn = 8);
    translate([0, 0.45, 0.7]) rotate([-55, 0, 0]) translate([0, -0.45, 0])
    {
        ab_plastic(ab_WOODD()) difference()
        {
            rotate([0, 90, 0]) cylinder(h = 1.3, r = 0.45, center = true, $fn = 12);
            translate([0, 0, -0.5]) ab_boxc([2, 2, 1]);
        }
        ab_metal(ab_METALD()) translate([0, -0.45, 0.02]) ab_boxc([1.34, 0.08, 0.08]);
    }
}

// 激光发射器（沿 +x 发射，底面 z=0）：发射盒 + 红色光束 + 接收器
module ab_prop_laser(L = 6)
{
    ab_metal(ab_METALD()) ab_bevel(0.6, 0.6, 1.0, 0.05);
    ab_gloss(ab_RED()) translate([0.3, 0, 0.7]) rotate([0, 90, 0]) cylinder(h = 0.05, r = 0.12, $fn = 8);
    ab_gloss([0.95, 0.16, 0.10]) translate([0.3, 0, 0.7]) rotate([0, 90, 0]) cylinder(h = L - 0.6, r = 0.05, $fn = 6);
    ab_metal(ab_METALD()) translate([L, 0, 0]) ab_bevel(0.6, 0.6, 1.0, 0.05);
    ab_gloss(ab_DARK()) translate([L - 0.3, 0, 0.7]) rotate([0, 90, 0]) cylinder(h = 0.05, r = 0.12, $fn = 8);
}

// 喷泉水柱（底面 z=0，顶 z=h）：喷嘴 + 亮蓝水柱 + 顶部浪花（可把角色托起）
module ab_prop_fountain_jet(h = 4, r = 0.45)
{
    ab_metal(ab_METALD()) cylinder(h = 0.2, r = r + 0.3, $fn = 12);
    ab_gloss(ab_BLUEL()) translate([0, 0, 0.2]) cylinder(h = 0.15, r = r + 0.1, $fn = 12);
    ab_gloss([0.50, 0.78, 0.88]) translate([0, 0, 0.3]) cylinder(h = h - 0.5, r1 = r * 0.8, r2 = r, $fn = 10);
    ab_gloss([0.80, 0.86, 0.88]) translate([0, 0, h - 0.3]) ab_torus(r + 0.15, 0.14);
    for (i = [0 : 4])
        ab_gloss([0.80, 0.86, 0.88]) rotate([0, 0, i * 72]) translate([r + 0.35, 0, h - 0.5]) sphere(r = 0.09, $fn = 6);
}

// 花盆（底面 z=0）：陶土盆 + 土 + 小花
module ab_prop_pot(seed = 0)
{
    ab_plastic(ab_TERRA()) cylinder(h = 0.55, r1 = 0.28, r2 = 0.36, $fn = 10);
    ab_plastic(ab_TERRA()) translate([0, 0, 0.5]) cylinder(h = 0.1, r = 0.4, $fn = 10);
    ab_matte(ab_SOILD()) translate([0, 0, 0.58]) cylinder(h = 0.03, r = 0.33, $fn = 10);
    translate([0, 0, 0.6]) ab_nature_flower(0.6, seed);
}

// 扭蛋（底面 z=0）：双色球壳
module ab_prop_capsule(seed = 0, r = 0.5)
{
    c = ab_balloon_c(seed);
    translate([0, 0, r])
    {
        ab_gloss(c) ab_dome(r, 1.0);
        ab_gloss(ab_CREAM()) rotate([180, 0, 0]) ab_dome(r, 1.0);
        ab_gloss(c) ab_torus(r - 0.02, 0.04);
    }
}

// 跳环（front = -y，底面 z=0）：立杆 + 站立圆环（穿过收集金币）
module ab_prop_hoop(R = 1.4)
{
    ab_metal(ab_METALD()) cylinder(h = 0.15, r = 0.5, $fn = 10);
    ab_metal(ab_METAL()) cylinder(h = 0.9, r = 0.07, $fn = 8);
    for (i = [0 : 7])
        ab_gloss(i % 2 == 0 ? ab_RED() : ab_CREAM())
            translate([0, 0, 0.9 + R + 0.1]) rotate([90, 0, 0]) rotate([0, 0, i * 45]) rotate_extrude(angle = 45) translate([R, 0]) circle(r = 0.1);
}

// 泡泡（底面 z=0，可悬浮摆放）：亮面淡蓝球
module ab_prop_bubble(r = 0.9)
{
    ab_glass([0.70, 0.84, 0.90]) translate([0, 0, r]) sphere(r = r, $fn = 14);
}

// 检查点（底面 z=0）：蓝白圆环底座 + 旗杆 + 蓝旗
module ab_prop_checkpoint(h = 3)
{
    ab_gloss(ab_CREAM()) cylinder(h = 0.12, r = 1.0, $fn = 16);
    ab_gloss(ab_BLUE()) translate([0, 0, 0.12]) rotate_extrude() translate([0.6, 0]) square([0.25, 0.02]);
    ab_metal(ab_METAL()) cylinder(h = h, r = 0.05, $fn = 8);
    ab_gold() translate([0, 0, h]) sphere(r = 0.09, $fn = 6);
    ab_gloss(ab_BLUE()) translate([0.05, 0, h - 0.1]) rotate([90, 0, 0]) translate([0, 0, -0.015]) linear_extrude(0.03) polygon([[0, 0], [1.1, -0.35], [0, -0.7]]);
}

// 玩具长椅（front = -y，底面 z=0）
module ab_prop_bench()
{
    for (sx = [-1, 1]) ab_metal(ab_METALD()) translate([sx * 0.7, 0, 0.22]) ab_boxc([0.08, 0.5, 0.44]);
    ab_plastic(ab_WOODL()) translate([0, 0, 0.44]) ab_rbox(1.8, 0.5, 0.08, 0.1);
    ab_plastic(ab_WOODL()) translate([0, 0.24, 0.5]) rotate([12, 0, 0]) ab_rbox(1.8, 0.08, 0.5, 0.03);
}

// 梯子（靠墙竖直，front = -y，底面 z=0）
module ab_prop_ladder(h = 3)
{
    for (sx = [-1, 1]) ab_plastic(ab_YELLOW()) translate([sx * 0.3, 0, 0]) cylinder(h = h, r = 0.05, $fn = 6);
    n = max(1, floor(h / 0.35));
    for (i = [1 : n])
        ab_plastic(ab_CREAM()) translate([0, 0, i * h / (n + 0.5)]) rotate([0, 90, 0]) cylinder(h = 0.6, r = 0.035, center = true, $fn = 6);
}

// 交通锥（底面 z=0）
module ab_prop_cone()
{
    ab_rubber(ab_DARK()) ab_bevel(0.5, 0.5, 0.05, 0.02);
    ab_plastic(ab_ORANGE()) translate([0, 0, 0.05]) cylinder(h = 0.65, r1 = 0.2, r2 = 0.05, $fn = 10);
    ab_plastic(ab_CREAM()) translate([0, 0, 0.32]) cylinder(h = 0.1, r1 = 0.14, r2 = 0.12, $fn = 10);
}

// 彩色油桶（底面 z=0）
module ab_prop_barrel(seed = 0)
{
    c = ab_block_c(seed);
    ab_plastic(c) cylinder(h = 0.9, r = 0.32, $fn = 12);
    for (z = [0.25, 0.65]) ab_plastic(ab_CREAM()) translate([0, 0, z]) cylinder(h = 0.06, r = 0.335, $fn = 12);
    ab_metal(ab_METALD()) translate([0, 0, 0.9]) cylinder(h = 0.02, r = 0.3, $fn = 12);
}

// ================= 收集物（item） =================

// 金币（悬浮 hover，立放朝 -y，币心 z = hover + r）
module ab_item_coin(hover = 0.8, r = 0.35)
{
    translate([0, 0, hover + r])
    {
        ab_gold() rotate([90, 0, 0]) cylinder(h = 0.08, r = r, center = true, $fn = 14);
        for (sy = [-1, 1])
            gk_material(ab_GOLDD(), roughness = 0.28, metalness = 0.5)
                translate([0, sy * 0.045, 0]) rotate([90, 0, 0]) cylinder(h = 0.01, r = r * 0.72, center = true, $fn = 14);
    }
}

// 金币直列（沿 x，n 枚，间距 dx，悬浮 hover）
module ab_item_coin_row(n = 5, dx = 1.2, hover = 0.8)
{
    for (i = [0 : n - 1]) translate([-(n - 1) * dx / 2 + i * dx, 0, 0]) ab_item_coin(hover);
}

// 金币抛物弧（沿 x，跨度 L，弧高 h，起落点悬浮 hover）：引导跳跃/悬浮距离
module ab_item_coin_arc(n = 7, L = 6, h = 2.5, hover = 0.8)
{
    for (i = [0 : n - 1])
    {
        x = -L / 2 + i * L / (n - 1);
        translate([x, 0, h * (1 - pow(2 * x / L, 2))]) ab_item_coin(hover);
    }
}

// 金币圆环（半径 R，n 枚，币面朝外，悬浮 hover）
module ab_item_coin_ring(n = 8, R = 2, hover = 1.0)
{
    for (i = [0 : n - 1]) rotate([0, 0, i * 360 / n]) translate([R, 0, 0]) rotate([0, 0, 90]) ab_item_coin(hover);
}

// 拼图碎片（悬浮 hover，立放朝 -y）：蓝色高光拼图
module ab_item_puzzle(hover = 0.8, s = 0.9)
{
    translate([0, 0, hover + s / 2 + 0.2]) rotate([90, 0, 0]) ab_gloss(ab_BLUE()) difference()
    {
        union()
        {
            ab_boxc([s, s, 0.16]);
            translate([0, s / 2, 0]) cylinder(h = 0.16, r = s * 0.2, center = true, $fn = 10);
            translate([s / 2, 0, 0]) cylinder(h = 0.16, r = s * 0.2, center = true, $fn = 10);
        }
        translate([0, -s / 2, 0]) cylinder(h = 0.2, r = s * 0.2, center = true, $fn = 10);
        translate([-s / 2, 0, 0]) cylinder(h = 0.2, r = s * 0.2, center = true, $fn = 10);
    }
}

// 宝石（悬浮 hover）：双锥八面体，高光
module ab_item_gem(hover = 0.8, seed = 0)
{
    c = [[0.20, 0.62, 0.80], [0.80, 0.24, 0.40], [0.40, 0.76, 0.34], [0.72, 0.50, 0.86]][ab_rnd(seed, 4)];
    gk_material(c, roughness = 0.03, metalness = 0) translate([0, 0, hover])
    {
        translate([0, 0, 0.32]) cylinder(h = 0.32, r1 = 0.3, r2 = 0.01, $fn = 6);
        cylinder(h = 0.32, r1 = 0.01, r2 = 0.3, $fn = 6);
    }
}

// 钥匙（悬浮 hover，立放朝 -y）：金环 + 杆 + 齿
module ab_item_key(hover = 0.8)
{
    translate([0, 0, hover + 0.3]) ab_gold()
    {
        rotate([90, 0, 0]) ab_torus(0.2, 0.05);
        translate([0.35, 0, 0]) ab_boxc([0.5, 0.06, 0.08]);
        translate([0.5, 0, -0.12]) ab_boxc([0.06, 0.06, 0.18]);
        translate([0.38, 0, -0.1]) ab_boxc([0.06, 0.06, 0.14]);
    }
}

// 金星（悬浮 hover，立放朝 -y）：关卡大奖
module ab_item_star(hover = 0.8, s = 1.0)
{
    translate([0, 0, hover + s * 0.5]) rotate([90, 0, 0]) translate([0, 0, -0.08]) ab_gold() ab_star(s * 0.5, s * 0.22, 0.16);
}

// ================= 角色（char） =================

// 主角机器人（front = -y，底面 z=0，身高 1.6 m）：白色高光机身 + 蓝色目镜/耳灯/手足
//   pose 0 待机 / 1 挥手 / 2 张臂 / 3 欢呼；hat -1 由 seed 决定，否则指定
//   帽饰：0-1 无 / 2 派对帽 / 3 皇冠 / 4 头盔 / 5 螺旋桨帽
module ab_char_bot(seed = 0, pose = 0, s = 1.0, hat = -1)
{
    hh = hat >= 0 ? hat : ab_rnd(seed, 6);
    scale([s, s, s])
    {
        for (sx = [-1, 1])
        {
            ab_gloss(ab_BLUE()) translate([sx * 0.15, -0.03, 0.10]) ab_ellipsoid(0.12, 0.17, 0.10);
            ab_gloss(ab_WHITE()) translate([sx * 0.15, 0, 0.30]) cylinder(h = 0.26, r = 0.07, center = true, $fn = 8);
        }
        ab_gloss(ab_WHITE()) translate([0, 0, 0.70]) ab_ellipsoid(0.28, 0.24, 0.34);
        ab_gloss(ab_BLUE()) translate([0, 0, 0.46]) ab_ellipsoid(0.245, 0.21, 0.08);
        ab_gloss(ab_BLUE()) translate([0, -0.22, 0.78]) ab_ellipsoid(0.10, 0.05, 0.10);
        ab_gloss(ab_BLUE()) translate([0, 0.24, 0.72]) ab_ellipsoid(0.16, 0.09, 0.20);
        ab_gloss(ab_WHITE()) translate([0, 0, 1.22]) sphere(r = 0.36, $fn = 16);
        ab_gloss(ab_VISOR()) translate([0, -0.18, 1.22]) ab_ellipsoid(0.30, 0.22, 0.25);
        for (sx = [-1, 1])
        {
            ab_gloss(ab_BLUEL()) translate([sx * 0.11, -0.385, 1.25]) ab_ellipsoid(0.05, 0.03, 0.10);
            ab_gloss(ab_BLUE()) translate([sx * 0.37, 0, 1.22]) rotate([0, 90, 0]) cylinder(h = 0.10, r = 0.12, center = true, $fn = 10);
            a = (pose == 3 || (pose == 1 && sx == 1)) ? 165 : (pose == 2 ? 80 : 15);
            translate([sx * 0.30, 0, 0.92]) rotate([0, -sx * a, 0])
            {
                ab_gloss(ab_WHITE()) translate([0, 0, -0.20]) cylinder(h = 0.40, r = 0.06, center = true, $fn = 8);
                ab_gloss(ab_BLUE()) translate([0, 0, -0.44]) sphere(r = 0.09, $fn = 8);
            }
        }
        if (hh == 2)
            ab_gloss(ab_RED()) translate([0, 0, 1.52]) cylinder(h = 0.45, r1 = 0.18, r2 = 0.01, $fn = 8);
        if (hh == 3)
        {
            ab_gold() translate([0, 0, 1.50]) difference()
            {
                cylinder(h = 0.18, r = 0.21, $fn = 8);
                translate([0, 0, 0.05]) cylinder(h = 0.3, r = 0.16, $fn = 8);
            }
            for (i = [0 : 4]) ab_gold() rotate([0, 0, i * 72]) translate([0.18, 0, 1.68]) cylinder(h = 0.14, r1 = 0.05, r2 = 0.005, $fn = 5);
        }
        if (hh == 4)
            ab_gloss(ab_YELLOW()) translate([0, 0, 1.36]) ab_dome(0.39, 0.7);
        if (hh == 5)
        {
            ab_gloss(ab_GREENL()) translate([0, 0, 1.40]) ab_dome(0.38, 0.55);
            ab_metal(ab_METAL()) translate([0, 0, 1.6]) cylinder(h = 0.14, r = 0.02, $fn = 6);
            for (i = [0, 90]) ab_gloss(ab_RED()) translate([0, 0, 1.74]) rotate([0, 0, i]) ab_boxc([0.44, 0.06, 0.02]);
        }
    }
}

// 被困机器人（底面 z=0）：kind 0 半埋沙堆只露头肩、双臂求救 / kind 1 倒栽葱腿朝天
module ab_char_bot_lost(seed = 0, kind = 0)
{
    if (kind == 0)
    {
        ab_matte(ab_SAND()) ab_dome(0.95, 0.4);
        ab_gloss(ab_WHITE()) translate([0, 0, 0.5]) sphere(r = 0.36, $fn = 16);
        ab_gloss(ab_VISOR()) translate([0, -0.18, 0.5]) ab_ellipsoid(0.30, 0.22, 0.25);
        for (sx = [-1, 1])
        {
            ab_gloss(ab_BLUEL()) translate([sx * 0.11, -0.385, 0.53]) ab_ellipsoid(0.05, 0.03, 0.10);
            ab_gloss(ab_BLUE()) translate([sx * 0.37, 0, 0.5]) rotate([0, 90, 0]) cylinder(h = 0.10, r = 0.12, center = true, $fn = 10);
            translate([sx * 0.45, 0, 0.25]) rotate([0, -sx * 20, 0])
            {
                ab_gloss(ab_WHITE()) cylinder(h = 0.75, r = 0.06, $fn = 8);
                ab_gloss(ab_BLUE()) translate([0, 0, 0.8]) sphere(r = 0.09, $fn = 8);
            }
        }
    }
    else
    {
        ab_matte(ab_SOIL()) ab_dome(0.7, 0.3);
        translate([0, 0, 1.62]) rotate([180, 0, 0]) ab_char_bot(seed, 2, 1.0, 0);
    }
}

// 巡逻敌人（front = -y，底面 z=0，高约 1.4 m）：圆球身 + 怒眉大眼 + 头顶尖刺
module ab_char_enemy_walker(seed = 0)
{
    c = ab_enemy_c(seed);
    for (sx = [-1, 1]) ab_rubber(ab_DARK()) translate([sx * 0.22, 0, 0.12]) ab_ellipsoid(0.14, 0.2, 0.12);
    ab_gloss(c) translate([0, 0, 0.62]) sphere(r = 0.45, $fn = 14);
    for (sx = [-1, 1])
    {
        ab_gloss(ab_CREAM()) translate([sx * 0.16, -0.36, 0.72]) sphere(r = 0.11, $fn = 8);
        ab_gloss(ab_DARK()) translate([sx * 0.16, -0.45, 0.72]) sphere(r = 0.05, $fn = 6);
        ab_gloss(ab_DARK()) translate([sx * 0.16, -0.42, 0.86]) rotate([0, -sx * 25, 0]) ab_boxc([0.24, 0.05, 0.05]);
        ab_gloss(c) translate([sx * 0.5, -0.05, 0.55]) sphere(r = 0.1, $fn = 8);
    }
    ab_metal(ab_METALD()) translate([0, 0, 0.98]) ab_torus(0.22, 0.05);
    ab_chrome(ab_METAL()) translate([0, 0, 1.0]) cylinder(h = 0.42, r1 = 0.12, r2 = 0.01, $fn = 8);
}

// 飞行敌人（底面 z=hover）：螺旋桨无人机 + 独眼镜头
module ab_char_enemy_flyer(seed = 0, hover = 2.0)
{
    c = ab_enemy_c(seed + 3);
    translate([0, 0, hover])
    {
        ab_metal(ab_METALD()) ab_torus(0.28, 0.04);
        ab_gloss(c) translate([0, 0, 0.45]) sphere(r = 0.36, $fn = 14);
        ab_gloss(ab_CREAM()) translate([0, -0.3, 0.5]) sphere(r = 0.14, $fn = 8);
        ab_gloss(ab_RED()) translate([0, -0.41, 0.5]) sphere(r = 0.06, $fn = 6);
        ab_metal(ab_METAL()) translate([0, 0, 0.78]) cylinder(h = 0.2, r = 0.025, $fn = 6);
        for (i = [0 : 2]) ab_gloss(ab_METALD()) translate([0, 0, 0.98]) rotate([0, 0, i * 120]) translate([0.3, 0, 0]) ab_boxc([0.6, 0.08, 0.02]);
        for (sx = [-1, 1]) ab_metal(ab_METALD()) translate([sx * 0.38, 0, 0.3]) rotate([0, sx * 20, 0]) cylinder(h = 0.3, r = 0.03, $fn = 6);
    }
}

// 刺背敌人（front = -y，底面 z=0）：紫色胶囊身沿 y + 背脊锥刺（不能踩）
module ab_char_enemy_spiky(seed = 0)
{
    c = ab_enemy_c(seed + 5);
    ab_gloss(c) translate([0, 0, 0.42]) rotate([90, 0, 0]) hull()
    {
        translate([0, 0, -0.5]) sphere(r = 0.42, $fn = 12);
        translate([0, 0, 0.5]) sphere(r = 0.36, $fn = 12);
    }
    for (i = [0 : 4])
        ab_chrome(ab_METAL()) translate([0, -0.5 + i * 0.25, 0.78 - abs(i - 2) * 0.03]) cylinder(h = 0.38, r1 = 0.1, r2 = 0.01, $fn = 7);
    for (sx = [-1, 1])
    {
        ab_gloss(ab_CREAM()) translate([sx * 0.15, -0.8, 0.5]) sphere(r = 0.1, $fn = 8);
        ab_gloss(ab_DARK()) translate([sx * 0.15, -0.88, 0.5]) sphere(r = 0.045, $fn = 6);
    }
    for (sx = [-1, 1], sy = [-1, 1])
        ab_rubber(ab_DARK()) translate([sx * 0.3, sy * 0.35, 0.08]) ab_ellipsoid(0.1, 0.12, 0.08);
}
