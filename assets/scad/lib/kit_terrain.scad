// kit_terrain.scad —— 地形贴地组合子库(SCAD Terrain M1,见 docs/designs/scad-terrain-design.md §6)
//
// 与 kit_layout.scad 同一族:组合子只做放置(transform + 每实例 $ 变量),不产几何;
// 随机全部 seed 派生,确定性。依赖引擎扩展函数(gk_ 前缀,非 OpenSCAD 标准):
//   gk_terrain_height(TERR, x, y) → 地表高度(与渲染网格逐三角形一致)
//   gk_terrain_info(TERR, x, y)   → [height, slopeDeg, water(0/1), biomeId]
//
// biomeId:0 grass / 1 grass_dark / 2 dry_grass / 3 sand / 4 rock / 5 rock_high
//         6 snow / 7 bed(水下) / 8 road / 9 pad
//
// children 内可读的每实例变量(与 kit_layout 一致):$idx / $seed / $t

use <kit_layout.scad>

// ================= 生物群系名 → id =================
function ter_biome(name) =
    name == "grass" ? 0 : name == "grass_dark" ? 1 : name == "dry_grass" ? 2 :
    name == "sand" ? 3 : name == "rock" ? 4 : name == "rock_high" ? 5 :
    name == "snow" ? 6 : name == "bed" ? 7 : name == "road" ? 8 :
    name == "pad" ? 9 : -1;

function ter_in_biomes(b, list, i = 0) =
    i >= len(list) ? false :
    (list[i] == b || ter_biome(list[i]) == b) ? true : ter_in_biomes(b, list, i + 1);

// ================= 过滤谓词 =================
// filt = [hMin, hMax, slopeMax, avoidWater, biomes],可只给前若干项:
//   hMin/hMax    海拔带(默认无限制)
//   slopeMax     坡度上限,度(默认 90)
//   avoidWater   <0 允许水中;0 仅排除水域格;>0 额外要求四方向 avoidWater 距离内无水
//   biomes       生物群系白名单(名字或 id 列表;空 = 不限)
function ter_filtnum(filt, i, def) = i < len(filt) ? filt[i] : def;

function ter_dry(t, x, y, d) =
    gk_terrain_info(t, x, y)[2] == 0 &&
    (d <= 0 || (gk_terrain_info(t, x + d, y)[2] == 0 && gk_terrain_info(t, x - d, y)[2] == 0 &&
                gk_terrain_info(t, x, y + d)[2] == 0 && gk_terrain_info(t, x, y - d)[2] == 0));

function ter_ok_info(info, filt) =
    info[0] >= ter_filtnum(filt, 0, -1e9) &&
    info[0] <= ter_filtnum(filt, 1, 1e9) &&
    info[1] <= ter_filtnum(filt, 2, 90) &&
    (len(filt) < 5 || len(filt[4]) == 0 || ter_in_biomes(info[3], filt[4]));

function ter_ok(t, x, y, filt) =
    ter_ok_info(gk_terrain_info(t, x, y), filt) &&
    (ter_filtnum(filt, 3, 0) < 0 || ter_dry(t, x, y, ter_filtnum(filt, 3, 0)));

// ================= 贴地放置 =================
// children 平移到 (x, y, 地表高度 + dz)。建筑等直立件用这个。
module ter_place(t, x, y, dz = 0)
{
    translate([x, y, gk_terrain_height(t, x, y) + dz]) children();
}

// 组合子实例贴地:配合 kit_layout 组合子的 $px/$py 使用。
// at 为组合子自身的外层平移(compose 的 translate(at) 前缀),世界位置 = at + ($px,$py)。
//   lay_grid(...) ter_snap(TERR, at) 零件();
module ter_snap(t, at = [0, 0], dz = 0)
{
    translate([0, 0, gk_terrain_height(t, at[0] + $px, at[1] + $py) + dz]) children();
}

// 贴地 + 随坡面倾斜(岩石/倒木/植被用;建筑不要用)。
// 中心差分估梯度,倾角上限 maxTilt 度。
module ter_place_tilt(t, x, y, dz = 0, maxTilt = 12, probe = 0.8)
{
    ter_pt_gx = (gk_terrain_height(t, x + probe, y) - gk_terrain_height(t, x - probe, y)) / (2 * probe);
    ter_pt_gy = (gk_terrain_height(t, x, y + probe) - gk_terrain_height(t, x, y - probe)) / (2 * probe);
    ter_pt_slope = atan(sqrt(ter_pt_gx * ter_pt_gx + ter_pt_gy * ter_pt_gy));
    ter_pt_tilt = min(ter_pt_slope, maxTilt);
    ter_pt_azim = atan2(ter_pt_gy, ter_pt_gx); // 上坡方位角
    translate([x, y, gk_terrain_height(t, x, y) + dz])
        rotate([0, 0, ter_pt_azim])
            rotate([0, -ter_pt_tilt, 0])
                rotate([0, 0, -ter_pt_azim])
                    children();
}

// ================= 折线贴地撒点 =================
// lay_along 的贴地版:沿折线每 step 一件,每件落到地表 + dz(路灯沿路、栅栏沿坡)。
// 子件随段方向旋转,$t 为段内参数。
module ter_along(t, pts, step = 6, seed = 0, offset = 0, dz = 0)
{
    for (s = [0 : len(pts) - 2])
    {
        ter_al_d = [pts[s + 1][0] - pts[s][0], pts[s + 1][1] - pts[s][1]];
        ter_al_len = norm(ter_al_d);
        ter_al_a = atan2(ter_al_d[1], ter_al_d[0]);
        ter_al_cnt = floor((ter_al_len - offset) / step);
        if (ter_al_cnt >= 0)
        {
            for (k = [0 : ter_al_cnt])
            {
                $idx = s * 1024 + k;
                $seed = seed * 8191 + s * 131 + k * 7;
                $t = (offset + k * step) / ter_al_len;
                ter_al_x = pts[s][0] + ter_al_d[0] * $t;
                ter_al_y = pts[s][1] + ter_al_d[1] * $t;
                translate([ter_al_x, ter_al_y, gk_terrain_height(t, ter_al_x, ter_al_y) + dz])
                    rotate([0, 0, ter_al_a])
                        children();
            }
        }
    }
}

// ================= 过滤散布(拒绝采样) =================
// 在 region = [x0, y0, x1, y1](min/max 角点)内确定性散布,候选上限 4n,
// 逐点用 ter_ok 过滤后取前 n 件,贴地放置。rot=true 随机朝向。
// 同 (t, seed, n, region, filt) → 同点集,可回归。
module ter_scatter(t, seed = 0, n = 10, region = [-20, -20, 20, 20], filt = [], rot = true, dz = 0)
{
    ter_sc_pts = [for (i = [0 : n * 4 - 1])
        if (ter_ok(t,
                   lay_randr(seed * 8191 + i * 131, 1, region[0], region[2]),
                   lay_randr(seed * 8191 + i * 131, 2, region[1], region[3]),
                   filt))
            [lay_randr(seed * 8191 + i * 131, 1, region[0], region[2]),
             lay_randr(seed * 8191 + i * 131, 2, region[1], region[3]),
             seed * 8191 + i * 131]];
    ter_sc_cnt = min(n, len(ter_sc_pts));
    if (ter_sc_cnt > 0)
    {
        for (i = [0 : ter_sc_cnt - 1])
        {
            $idx = i;
            $seed = ter_sc_pts[i][2];
            translate([ter_sc_pts[i][0], ter_sc_pts[i][1],
                       gk_terrain_height(t, ter_sc_pts[i][0], ter_sc_pts[i][1]) + dz])
                rotate([0, 0, rot ? lay_randi($seed, 3, 360) : 0])
                    children();
        }
    }
}
