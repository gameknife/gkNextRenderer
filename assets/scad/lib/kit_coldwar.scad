// kit_coldwar.scad —— 冷战东欧生存射击零件库（DayZ 风格废弃世界）
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "cw_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y（载具车头朝 +x，武器枪口朝 +x）。
// 调用方自设 $fn（建议 12）。尺度：mid，1 unit = 1 m。
// 类别：ground 地面 / bldg 建筑 / nature 植被生态 / prop 道具路标 / veh 载具 /
//       wpn 武器（平躺地面 loot）/ item 物资（小件 loot）。
// 覆盖场景：废弃城镇、工厂、加油站、军事基地、监狱、城市超市、城市医院。

// ================= 配色（低饱和铁幕灰绿；PT 强日光下会整体提亮，故基色偏深偏饱和） =================
function cw_ASPH()    = [0.29, 0.29, 0.31];   // 沥青路面
function cw_ASPHD()   = [0.22, 0.22, 0.24];   // 沥青裂缝/补丁
function cw_CONC()    = [0.52, 0.51, 0.48];   // 混凝土
function cw_CONCD()   = [0.40, 0.39, 0.37];   // 脏污混凝土/基座
function cw_PANEL()   = [0.55, 0.52, 0.46];   // 预制板米灰
function cw_PANELD()  = [0.42, 0.40, 0.36];   // 预制板拼缝
function cw_BRICK()   = [0.46, 0.23, 0.15];   // 红砖
function cw_BRICKD()  = [0.35, 0.18, 0.12];   // 深砖
function cw_PLASTER() = [0.54, 0.46, 0.32];   // 赭石抹灰
function cw_WHITEW()  = [0.64, 0.63, 0.58];   // 白灰墙（医院/检查站）
function cw_RUSTC()   = [0.36, 0.19, 0.12];   // 锈
function cw_METALC()  = [0.44, 0.46, 0.48];   // 镀锌金属
function cw_METALD()  = [0.26, 0.28, 0.30];   // 深金属
function cw_OLIVE()   = [0.24, 0.28, 0.16];   // 军绿
function cw_OLIVED()  = [0.17, 0.20, 0.12];   // 深军绿
function cw_KHAKI()   = [0.42, 0.38, 0.24];   // 卡其帆布
function cw_WOODC()   = [0.37, 0.27, 0.18];   // 原木
function cw_WOODD()   = [0.24, 0.18, 0.13];   // 深木
function cw_TRUNKC()  = [0.30, 0.23, 0.16];   // 树干
function cw_BIRCHW()  = [0.66, 0.64, 0.58];   // 白桦树皮
function cw_PINEC()   = [0.15, 0.27, 0.15];   // 松针中
function cw_PINED()   = [0.11, 0.20, 0.12];   // 松针深
function cw_LEAFC()   = [0.31, 0.41, 0.20];   // 阔叶
function cw_LEAFD()   = [0.24, 0.33, 0.16];   // 阔叶深
function cw_GRASSC()  = [0.36, 0.41, 0.24];   // 草地
function cw_GRASSD()  = [0.29, 0.34, 0.19];   // 深草斑
function cw_SOILC()   = [0.36, 0.27, 0.19];   // 泥土
function cw_DARKC()   = [0.10, 0.10, 0.10];   // 窗芯/洞口
function cw_GLASSC()  = [0.30, 0.40, 0.46];   // 玻璃
function cw_MARKW()   = [0.78, 0.78, 0.72];   // 白标线/白漆
function cw_REDC()    = [0.56, 0.13, 0.10];   // 苏式红
function cw_YELLOWC() = [0.72, 0.55, 0.12];   // 警示黄
function cw_SIGNB()   = [0.13, 0.29, 0.48];   // 路牌蓝
function cw_ROOFM()   = [0.32, 0.35, 0.37];   // 金属屋面
function cw_ROOFR()   = [0.40, 0.20, 0.14];   // 锈红屋面

// ---- 确定性伪随机（必须含平方项：线性同余的组合仍是线性，连续 seed 会出等差伪影） ----
function cw_sq(x) = (x * x + x * 601 + 37) % 65521;
function cw_rnd(s, m) = cw_sq(cw_sq(((s % 65521) + 65521) % 65521) + 11) % m;
function cw_rndf(s) = cw_rnd(s, 1000) / 999;                       // [0, 1]
function cw_rndr(s, a, b) = a + (b - a) * cw_rndf(s);              // [a, b]

// ---- 变体调色板 ----
function cw_panel_c(i) = [[0.55, 0.52, 0.46], [0.51, 0.47, 0.39], [0.49, 0.51, 0.49],
                          [0.54, 0.44, 0.34]][cw_rnd(i, 4)];                          // 预制板墙
function cw_wall_c(i)  = [cw_PLASTER(), cw_BRICK(), [0.47, 0.44, 0.37], cw_WHITEW(),
                          [0.42, 0.34, 0.26]][cw_rnd(i, 5)];                          // 杂墙
function cw_roof_c(i)  = [cw_ROOFM(), cw_ROOFR(), [0.25, 0.28, 0.32], [0.34, 0.25, 0.17]][cw_rnd(i, 4)];
function cw_car_c(i)   = [[0.54, 0.15, 0.11], [0.60, 0.58, 0.52], [0.20, 0.34, 0.44],
                          [0.62, 0.50, 0.14], [0.24, 0.38, 0.24], [0.30, 0.30, 0.34]][cw_rnd(i, 6)];
function cw_drum_c(i)  = [cw_RUSTC(), cw_OLIVE(), [0.20, 0.33, 0.44], cw_METALD()][cw_rnd(i, 4)];
function cw_goods_c(i) = [[0.56, 0.14, 0.10], [0.18, 0.34, 0.48], [0.62, 0.52, 0.16],
                          [0.24, 0.40, 0.24], [0.60, 0.58, 0.52], [0.40, 0.24, 0.14]][cw_rnd(i, 6)];

// ---- 基础工具 ----
module cw_boxc(s) cube(s, center = true);
module cw_slab(L = 4, D = 4, t = 0.2) translate([0, 0, t / 2]) cw_boxc([L, D, t]);   // 底面 z=0 平板

// ================= 通用构件 =================

// 坡屋面 polyhedron：正脊沿 x。L,D=檐口对应墙皮，h=脊高，ov=出檐，
// rin=山面内收（0=双坡，rin>=L/2=攒尖）。面序为 OpenSCAD 约定（从外看顺时针）。
module cw_part_roof(L = 8, D = 6, h = 1.8, ov = 0.5, rin = 0, c = [0.28, 0.31, 0.36])
{
    hw = L / 2 + ov;
    hd = D / 2 + ov;
    rx = max(0.02, L / 2 - rin);
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-rx, 0, h], [rx, 0, h]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

// 标准窗（贴墙面用，front=-y，锚点=窗中心）：浅框 + 深芯 + 中梃
module cw_part_window(w = 1.0, h = 1.3)
{
    color(cw_MARKW()) cw_boxc([w + 0.16, 0.10, h + 0.14]);
    color(cw_DARKC()) translate([0, -0.03, 0]) cw_boxc([w, 0.10, h]);
    color(cw_MARKW()) translate([0, -0.05, 0]) cw_boxc([0.07, 0.08, h]);
}

// 破损/钉板窗变体：seed 决定完好/钉板/破洞
module cw_part_window_worn(w = 1.0, h = 1.3, seed = 0)
{
    v = cw_rnd(seed, 4);
    if (v == 0)
    {
        color(cw_MARKW()) cw_boxc([w + 0.16, 0.10, h + 0.14]);
        color(cw_WOODD()) translate([0, -0.03, 0]) cw_boxc([w, 0.10, h]);
        color(cw_WOODC()) translate([0, -0.06, 0.1]) rotate([0, 14, 0]) cw_boxc([w * 1.05, 0.06, 0.24]);
        color(cw_WOODC()) translate([0, -0.06, -0.2]) rotate([0, -10, 0]) cw_boxc([w * 1.05, 0.06, 0.24]);
    }
    else
        cw_part_window(w, h);
}

// 铁栏窗（监狱/仓库，front=-y）
module cw_part_window_bar(w = 0.8, h = 0.9)
{
    color(cw_CONCD()) cw_boxc([w + 0.16, 0.10, h + 0.14]);
    color(cw_DARKC()) translate([0, -0.03, 0]) cw_boxc([w, 0.10, h]);
    color(cw_METALD()) for (i = [-1, 0, 1])
        translate([i * w * 0.28, -0.06, 0]) cw_boxc([0.05, 0.06, h]);
}

// 木门带框（front=-y，底面 z=0）
module cw_part_door(w = 1.0, h = 2.1, c = [0.30, 0.22, 0.16])
{
    color(cw_CONCD()) translate([0, 0, h / 2]) cw_boxc([w + 0.24, 0.12, h]);
    color(c) translate([0, -0.03, h / 2]) cw_boxc([w, 0.14, h]);
    color([0.70, 0.62, 0.28]) translate([w * 0.3, -0.11, h * 0.5]) cw_boxc([0.07, 0.06, 0.07]);
}

// 铁门（军营/监狱，front=-y，底面 z=0）
module cw_part_door_metal(w = 1.1, h = 2.1, c = [0.26, 0.28, 0.30])
{
    color(cw_CONCD()) translate([0, 0, h / 2]) cw_boxc([w + 0.22, 0.12, h]);
    color(c) translate([0, -0.03, h / 2]) cw_boxc([w, 0.12, h]);
    color(cw_RUSTC()) translate([-w * 0.2, -0.10, h * 0.3]) cw_boxc([0.3, 0.02, 0.4]);
    color(cw_METALC()) translate([w * 0.3, -0.10, h * 0.5]) cw_boxc([0.06, 0.05, 0.16]);
}

// ---- 可进入空心外壳（DayZ 掩体攻防：角色可走入，室内可放 loot） ----
// 四面墙 + 混凝土脚圈，室内挖空直通地面（无门槛），门/窗为纯洞口。front = -y。
// 本 evaluator 中 difference() 结果整体取首个子件颜色，故墙体与脚圈各用独立单色 difference。
// 每个颜色桶都会生成三角网碰撞：任何装饰件都不得横跨门洞的行走高度，否则会挡住入口。
//   L,D,wh   外墙足印/墙高；wc 墙色；t 墙厚
//   dw,dh,dx 前墙门洞 宽/高/中心x（洞口直通地面）
//   roomH    室内净高：0 => 直通墙顶（坡屋顶另置于其上）；>0 => 该高度以上留实心（平顶/多层地面楼板）
//   wins     窗洞列表 [face,u,z,w,h]；face 0=前(-y)/1=后(+y)/2=左(-x)/3=右(+x)，u=沿墙偏移，z=洞心高
//   plinth   是否绘制混凝土脚圈
module cw_part_shell_hole(L, D, t, w)
{
    if (w[0] == 0)      translate([w[1], -D / 2, w[2]]) cw_boxc([w[3], t * 4, w[4]]);
    else if (w[0] == 1) translate([w[1],  D / 2, w[2]]) cw_boxc([w[3], t * 4, w[4]]);
    else if (w[0] == 2) translate([-L / 2, w[1], w[2]]) cw_boxc([t * 4, w[3], w[4]]);
    else                translate([ L / 2, w[1], w[2]]) cw_boxc([t * 4, w[3], w[4]]);
}

module cw_part_shell(L, D, wh, wc, t = 0.28, dw = 1.5, dh = 2.1, dx = 0, roomH = 0, wins = [], plinth = true)
{
    ih = (roomH > 0 ? roomH : wh + 1.0);
    // 墙体：实心块挖空 + 门洞 + 窗洞（单色 wc）
    color(wc) difference()
    {
        cw_slab(L, D, wh);
        translate([0, 0, -0.1]) cw_slab(L - 2 * t, D - 2 * t, ih + 0.1);
        translate([dx, -D / 2, dh / 2 - 0.06]) cw_boxc([dw, t * 4, dh]);
        for (w = wins) cw_part_shell_hole(L, D, t, w);
    }
    // 混凝土脚圈：门洞处断开，避免门槛（单色）
    if (plinth)
        color(cw_CONCD()) difference()
        {
            cw_slab(L + 0.25, D + 0.25, 0.16);
            translate([0, 0, -0.1]) cw_slab(L - 2 * t, D - 2 * t, 0.5);
            translate([dx, -D / 2, 0.12]) cw_boxc([dw, 1.4, 0.6]);
        }
}

// ================= 地面（沿 x 铺设，底面 z=0） =================

// 老化沥青路段：褪色单虚中线 + 裂缝 + 补丁
module cw_ground_road(L = 24, W = 7, seed = 0)
{
    color(cw_ASPH()) cw_slab(L, W, 0.12);
    for (i = [0 : 2])
        color(cw_ASPHD())
            translate([cw_rndr(seed * 7 + i * 31, -(L - 4) / 2, (L - 4) / 2),
                       cw_rndr(seed * 13 + i * 17 + 5, -(W - 2.4) / 2, (W - 2.4) / 2), 0.12])
                cw_slab(1.4 + cw_rnd(seed + i, 3) * 0.5, 1.0 + cw_rnd(seed + i + 9, 3) * 0.4, 0.012);
    for (i = [0 : 3])
        color(cw_ASPHD())
            translate([cw_rndr(seed * 3 + i * 43, -(L - 3) / 2, (L - 3) / 2),
                       cw_rndr(seed * 5 + i * 23 + 7, -(W - 1.6) / 2, (W - 1.6) / 2), 0.12])
                rotate([0, 0, cw_rnd(seed + i * 7, 180)]) cw_slab(1.8, 0.09, 0.014);
    nd = floor(L / 3.0);
    color(cw_MARKW()) for (i = [0 : nd - 1])
        if (cw_rnd(seed + i * 13, 4) != 0)
            translate([-L / 2 + 1.5 + i * 3.0, 0, 0.12]) cw_slab(1.5, 0.13, 0.02);
}

// 十字路口块：W x W 沥青 + 褪色人行横道
module cw_ground_cross(W = 7, seed = 0)
{
    color(cw_ASPH()) cw_slab(W, W, 0.12);
    color(cw_ASPHD()) translate([cw_rndr(seed + 3, -1.5, 1.5), cw_rndr(seed + 11, -1.5, 1.5), 0.12])
        cw_slab(1.8, 1.3, 0.012);
    for (a = [0, 90, 180, 270])
        rotate([0, 0, a])
            color(cw_MARKW()) for (i = [0 : 4])
                if (cw_rnd(seed + a + i * 7, 5) != 0)
                    translate([-W / 2 + 0.9 + i * (W - 1.8) / 4, -W / 2 + 0.75, 0.12])
                        cw_slab(0.4, 1.1, 0.02);
}

// 混凝土板人行道（伸缩缝 + 缺角破损）
module cw_ground_sidewalk(L = 8, W = 1.8, seed = 0)
{
    color(cw_CONC()) cw_slab(L, W, 0.16);
    nj = floor(L / 1.6);
    color(cw_CONCD()) for (i = [1 : nj - 1])
        translate([-L / 2 + i * 1.6, 0, 0.16]) cw_slab(0.07, W, 0.012);
    if (cw_rnd(seed, 3) == 0)
        color(cw_CONCD()) translate([cw_rndr(seed, -L / 3, L / 3), W * 0.2, 0.16]) cw_slab(0.9, 0.7, 0.012);
}

// 荒草地块（深色草斑）
module cw_ground_grass(L = 20, D = 20, seed = 0)
{
    color(cw_GRASSC()) cw_slab(L, D, 0.10);
    for (i = [0 : 5])
        color(cw_GRASSD())
            translate([cw_rndr(seed * 31 + i * 47, -(L - 4) / 2, (L - 4) / 2),
                       cw_rndr(seed * 17 + i * 71 + 3, -(D - 4) / 2, (D - 4) / 2), 0.10])
                cw_slab(cw_rndr(seed + i, 1.6, 3.6), cw_rndr(seed + i + 5, 1.2, 2.8), 0.012);
}

// 裸土斑块：多边形叠片近似有机轮廓
module cw_ground_dirt(L = 10, D = 8, seed = 0)
{
    color(cw_SOILC())
    {
        rotate([0, 0, cw_rnd(seed, 180)]) scale([L * 0.5, D * 0.5, 1]) cylinder(h = 0.08, r = 1, $fn = 9);
        translate([L * 0.2, D * 0.14, 0]) rotate([0, 0, cw_rnd(seed + 3, 180)])
            scale([L * 0.3, D * 0.26, 1]) cylinder(h = 0.08, r = 1, $fn = 8);
        translate([-L * 0.22, -D * 0.15, 0]) rotate([0, 0, cw_rnd(seed + 5, 180)])
            scale([L * 0.26, D * 0.3, 1]) cylinder(h = 0.08, r = 1, $fn = 8);
    }
    for (i = [0 : 2])
        color([0.28, 0.20, 0.14])
            translate([cw_rndr(seed * 7 + i * 31, -L * 0.28, L * 0.28),
                       cw_rndr(seed * 11 + i * 17 + 3, -D * 0.28, D * 0.28), 0.08])
                scale([cw_rndr(seed + i, L * 0.08, L * 0.18), cw_rndr(seed + i + 5, D * 0.08, D * 0.18), 1])
                    cylinder(h = 0.012, r = 1, $fn = 8);
}

// 停车场：沥青 + 褪色白线车位（车位口朝 -y）
module cw_ground_parking(L = 18, D = 10, seed = 0)
{
    color(cw_ASPH()) cw_slab(L, D, 0.12);
    ns = floor(L / 3.0);
    color(cw_MARKW()) for (i = [0 : ns])
        if (cw_rnd(seed + i * 17, 5) != 0)
            translate([-L / 2 + 0.6 + i * (L - 1.2) / ns, -D / 2 + 2.75, 0.12]) cw_slab(0.12, 5.5, 0.02);
    color(cw_ASPHD()) translate([cw_rndr(seed, -L / 3, L / 3), cw_rndr(seed + 7, -D / 3, D / 3), 0.12])
        cw_slab(2.2, 1.4, 0.012);
}

// 停机坪：方形混凝土 + 白 H + 圆环
module cw_ground_helipad(S = 12)
{
    color(cw_CONC()) cw_slab(S, S, 0.15);
    color(cw_MARKW())
    {
        translate([0, 0, 0.15]) difference()
        {
            cylinder(h = 0.02, r = S * 0.36, $fn = 12);
            translate([0, 0, -0.05]) cylinder(h = 0.2, r = S * 0.31, $fn = 12);
        }
        for (sx = [-1, 1]) translate([sx * S * 0.09, 0, 0.15]) cw_slab(0.34, S * 0.3, 0.02);
        translate([0, 0, 0.15]) cw_slab(0.34, 0.9, 0.02);
        rotate([0, 0, 90]) translate([0, 0, 0.15]) cw_slab(0.34, S * 0.12, 0.02);
    }
}

// 铁路轨道段（沿 x）：道砟 + 枕木 + 双轨
module cw_ground_rail(L = 20, seed = 0)
{
    color([0.34, 0.33, 0.31]) cw_slab(L, 2.6, 0.14);
    nt = floor(L / 0.85);
    color(cw_WOODD()) for (i = [0 : nt - 1])
        translate([-L / 2 + 0.5 + i * (L - 1.0) / (nt - 1), 0, 0.14]) cw_slab(0.26, 2.2, 0.10);
    color(cw_RUSTC()) for (sy = [-1, 1])
        translate([0, sy * 0.72, 0.24]) cw_slab(L, 0.10, 0.12);
}

// ================= 建筑（front = -y） =================

// 苏式预制板筒子楼（赫鲁晓夫楼）：板缝 + 窗阵（部分钉板）+ 阳台 + 门斗 + 女儿墙
module cw_bldg_panel_flat(seed = 0, floors = 4, L = 16, D = 10)
{
    fh = 2.8;
    wh = floors * fh + 0.5;
    wc = cw_panel_c(seed);
    sc = [wc[0] * 0.78, wc[1] * 0.78, wc[2] * 0.78];
    nw = max(3, floor((L - 2.6) / 2.2));
    dx = -L * 0.18;
    // 底层可进入（门斗洞 + 底层前后窗洞），以上楼层实心
    cw_part_shell(L, D, wh, wc, t = 0.3, dw = 1.5, dh = 2.1, dx = dx, roomH = fh - 0.3,
                  wins = [ for (i = [0 : nw - 1]) if (abs(-L / 2 + 1.5 + i * (L - 3.0) / (nw - 1) - dx) > 1.2)
                               [0, -L / 2 + 1.5 + i * (L - 3.0) / (nw - 1), 1.6, 1.0, 1.2],
                           for (i = [0 : nw - 1]) [1, -L / 2 + 1.5 + i * (L - 3.0) / (nw - 1), 1.6, 1.0, 1.2] ]);
    // 竖向板缝（前后立面）
    nb = max(2, floor(L / 3.2));
    color(sc) for (i = [1 : nb - 1], sy = [-1, 1])
        translate([-L / 2 + i * L / nb, sy * (D / 2 + 0.01), wh / 2 + 0.25]) cw_boxc([0.10, 0.05, wh - 0.5]);
    // 横向层缝
    color(sc) for (f = [1 : floors - 1], sy = [-1, 1])
        translate([0, sy * (D / 2 + 0.01), 0.5 + f * fh]) cw_boxc([L, 0.05, 0.10]);
    // 上层窗阵（贴墙，seed 决定钉板窗；底层为洞口）
    for (f = [1 : floors - 1], i = [0 : nw - 1], sy = [-1, 1])
        translate([-L / 2 + 1.5 + i * (L - 3.0) / (nw - 1), sy * (D / 2 + 0.04), 0.5 + f * fh + 1.5])
            rotate([0, 0, sy > 0 ? 180 : 0])
                cw_part_window_worn(1.0, 1.3, seed * 13 + f * 31 + i * 7 + sy * 3);
    // 阳台（前立面部分开间，二层起）
    if (floors > 1)
        for (f = [1 : floors - 1], i = [0 : nw - 1])
            if (cw_rnd(seed + f * 17 + i * 29, 3) == 0)
                translate([-L / 2 + 1.5 + i * (L - 3.0) / (nw - 1), -D / 2 - 0.55, 0.5 + f * fh])
                {
                    color(cw_CONCD()) translate([0, 0, 0.75]) cw_slab(1.7, 1.1, 0.12);
                    color(cw_rnd(seed + f + i, 2) == 0 ? cw_METALD() : [0.44, 0.48, 0.42])
                        translate([0, -0.5, 1.35]) cw_boxc([1.7, 0.08, 0.9]);
                }
    // 门斗雨棚（门为洞口）
    color(cw_CONCD()) translate([dx, -D / 2 - 0.6, 2.7]) cw_boxc([1.9, 1.2, 0.12]);
    // 女儿墙 + 屋面构件
    color(sc) translate([0, 0, wh]) difference()
    {
        cw_slab(L + 0.16, D + 0.16, 0.5);
        translate([0, 0, -0.05]) cw_slab(L - 0.5, D - 0.5, 0.7);
    }
    color(cw_CONCD()) translate([L * 0.25, D * 0.12, wh]) cw_boxc([1.6, 1.4, 1.6]);
    color(cw_METALD()) translate([-L * 0.2, -D * 0.1, wh + 0.4]) cylinder(h = 0.9, r = 0.12, $fn = 6);
}

// 乡村木屋：原木墙 + 白窗套 + 金属坡顶 + 砖烟囱 + 门廊
module cw_bldg_house_rural(seed = 0, L = 7, D = 5.5)
{
    wh = 2.5;
    rc = cw_roof_c(seed + 3);
    wc = cw_rnd(seed, 2) == 0 ? cw_WOODC() : [0.44, 0.34, 0.22];
    dx = (cw_rnd(seed + 5, 2) == 0 ? -1 : 1) * L * 0.24;
    // 可进入空心木屋：门洞 + 前/侧窗洞（纯洞）
    cw_part_shell(L, D, wh, wc, t = 0.24, dw = 1.3, dh = 2.0, dx = dx,
                  wins = [[0, -dx, 1.5, 0.9, 1.1], [2, D * 0.12, 1.5, 0.9, 1.1], [3, -D * 0.12, 1.5, 0.9, 1.1]]);
    // 原木水平线（仅后檐墙外侧，避免横跨门/窗洞挡路）
    color(cw_WOODD()) for (z = [0.9, 1.5, 2.1])
        translate([0, D / 2 + 0.01, z]) cw_boxc([L, 0.04, 0.07]);
    translate([0, 0, wh]) cw_part_roof(L, D, 1.6, 0.5, 0, rc);
    color(cw_BRICKD()) translate([L * 0.26, D * 0.15, wh + 0.8]) cw_boxc([0.5, 0.5, 2.0]);
    // 门廊台阶
    color(cw_WOODD()) translate([dx, -D / 2 - 0.5, 0]) cw_slab(1.5, 0.9, 0.28);
}

// 单层临街商铺排：抹灰墙 + 大橱窗（部分钉板）+ 色块招牌带
module cw_bldg_shop_row(seed = 0, L = 10, D = 6)
{
    wh = 3.4;
    wc = cw_wall_c(seed);
    sc = cw_goods_c(seed + 2);
    nw = max(2, floor(L / 3.4));
    dx = L / 2 - 1.2;
    // 可进入商铺：右侧门洞 + 敞开橱窗洞（避开门），平顶留楼板
    cw_part_shell(L, D, wh, wc, t = 0.22, dw = 1.0, dh = 2.1, dx = dx, roomH = wh - 0.35,
                  wins = [ for (i = [0 : nw - 1]) let (wx = -L / 2 + (i + 0.5) * L / nw)
                               if (abs(wx - dx) > 1.4) [0, wx, 1.5, 2.0, 1.6] ]);
    color(cw_CONCD()) translate([0, 0, wh]) cw_slab(L + 0.2, D + 0.2, 0.15);
    // 招牌带（色块 + 白字块示意）
    color(sc) translate([0, -D / 2 - 0.10, 2.85]) cw_boxc([L - 0.6, 0.16, 0.7]);
    color(cw_MARKW()) translate([-L * 0.1, -D / 2 - 0.20, 2.85]) cw_boxc([L * 0.4, 0.03, 0.34]);
}

// 城市超市（大盒子）：肋板墙 + 玻璃门面带 + 大招牌 + 后侧装卸台
module cw_bldg_supermarket(seed = 0, L = 18, D = 12)
{
    wh = 4.6;
    dx = -L * 0.12;
    // 可进入超市：敞开店面（中门 + 两侧橱窗洞）+ 侧/后窗洞，平顶留楼板
    cw_part_shell(L, D, wh, cw_PANEL(), t = 0.3, dw = 2.0, dh = 2.5, dx = dx, roomH = wh - 0.4,
                  wins = [[0, dx - L * 0.22, 1.4, L * 0.16, 1.9], [0, dx + L * 0.22, 1.4, L * 0.16, 1.9],
                          [1, -L * 0.22, 1.8, 1.4, 1.2], [1, -L * 0.02, 1.8, 1.4, 1.2],
                          [2, 0, 1.8, 1.4, 1.2], [3, 0, 1.8, 1.4, 1.2]]);
    // 竖肋（前排避开店面洞口 → 充当橱窗竖梃）
    color(cw_PANELD()) for (i = [0 : floor(L / 1.6)])
    {
        rx = -L / 2 + 0.8 + i * 1.6;
        translate([rx, D / 2 + 0.01, wh / 2 + 0.6]) cw_boxc([0.18, 0.05, wh - 1.6]);
        if (abs(rx - dx) > 1.2)
            translate([rx, -D / 2 - 0.01, wh / 2 + 0.6]) cw_boxc([0.18, 0.05, wh - 1.6]);
    }
    // 大招牌带（红底白块）
    color(cw_REDC()) translate([0, -D / 2 - 0.14, wh - 0.55]) cw_boxc([L - 1.0, 0.2, 0.95]);
    color(cw_MARKW()) for (i = [0 : 5])
        translate([-L * 0.3 + i * L * 0.12, -D / 2 - 0.26, wh - 0.55]) cw_boxc([L * 0.07, 0.03, 0.5]);
    // 屋顶机组 + 女儿墙
    color(cw_CONCD()) translate([0, 0, wh]) difference()
    {
        cw_slab(L + 0.16, D + 0.16, 0.4);
        translate([0, 0, -0.05]) cw_slab(L - 0.5, D - 0.5, 0.6);
    }
    color(cw_METALC()) translate([L * 0.22, D * 0.1, wh]) cw_boxc([2.0, 1.4, 0.9]);
    // 背面装卸台
    color(cw_CONCD()) translate([L * 0.2, D / 2 + 0.9, 0]) cw_slab(4.5, 1.8, 1.0);
    color(cw_METALD()) translate([L * 0.2, D / 2 + 0.04, 1.9]) cw_boxc([2.6, 0.08, 1.8]);
}

// 城市医院：白灰板楼 + 红十字 + 入口雨棚柱廊 + 急救坡道
module cw_bldg_hospital(seed = 0, floors = 3, L = 18, D = 9)
{
    fh = 3.0;
    wh = floors * fh + 0.5;
    nw = max(3, floor((L - 2.6) / 2.0));
    // 底层可进入（中央大门 + 底层前后窗洞），以上楼层实心
    cw_part_shell(L, D, wh, cw_WHITEW(), t = 0.3, dw = 2.2, dh = 2.4, dx = 0, roomH = fh - 0.3,
                  wins = [ for (i = [0 : nw - 1]) if (abs(-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)) > 1.4)
                               [0, -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), 1.55, 1.1, 1.4],
                           for (i = [0 : nw - 1]) if (abs(-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)) > 1.4)
                               [1, -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), 1.55, 1.1, 1.4] ]);
    // 层间带
    color([0.48, 0.52, 0.52]) for (f = [1 : floors - 1], sy = [-1, 1])
        translate([0, sy * (D / 2 + 0.01), 0.5 + f * fh]) cw_boxc([L, 0.05, 0.5]);
    // 上层窗阵（贴墙装饰；底层为洞口）
    for (f = [1 : floors - 1], i = [0 : nw - 1], sy = [-1, 1])
        translate([-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), sy * (D / 2 + 0.04), 0.5 + f * fh + 1.55])
            rotate([0, 0, sy > 0 ? 180 : 0]) cw_part_window(1.1, 1.4);
    // 红十字招牌（front 顶部）
    color(cw_MARKW()) translate([0, -D / 2 - 0.12, wh - 0.9]) cw_boxc([1.7, 0.16, 1.7]);
    color(cw_REDC())
    {
        translate([0, -D / 2 - 0.22, wh - 0.9]) cw_boxc([0.42, 0.03, 1.3]);
        translate([0, -D / 2 - 0.22, wh - 0.9]) cw_boxc([1.3, 0.03, 0.42]);
    }
    // 入口柱廊雨棚 + 坡道
    color(cw_CONCD())
    {
        translate([0, -D / 2 - 1.3, 3.0]) cw_boxc([5.5, 2.6, 0.2]);
        for (sx = [-1, 1]) translate([sx * 2.3, -D / 2 - 2.2, 1.5]) cw_boxc([0.3, 0.3, 3.0]);
        translate([0, -D / 2 - 0.9, 0]) cw_slab(5.0, 1.8, 0.4);
        translate([-4.2, -D / 2 - 0.9, 0]) rotate([0, -7, 0]) cw_slab(3.6, 1.6, 0.36);
    }
    // 女儿墙 + 电梯机房
    color([0.52, 0.54, 0.52]) translate([0, 0, wh]) difference()
    {
        cw_slab(L + 0.16, D + 0.16, 0.45);
        translate([0, 0, -0.05]) cw_slab(L - 0.5, D - 0.5, 0.65);
    }
    color(cw_WHITEW()) translate([-L * 0.28, 0, wh]) cw_boxc([2.4, 2.6, 1.7])
    ;
}

// 厂房（双坡 + 天窗脊）：砖墙 + 高窗带 + 大推拉门
module cw_bldg_factory_hall(seed = 0, L = 14, D = 9)
{
    wh = 4.6;
    bc = cw_rnd(seed, 2) == 0 ? cw_BRICK() : [0.44, 0.40, 0.34];
    // 可进入厂房：前墙大门洞 + 前后高侧窗（天窗带），坡顶在上
    cw_part_shell(L, D, wh, bc, t = 0.3, dw = 3.0, dh = 3.4, dx = -L * 0.15,
                  wins = [[0, L * 0.28, 3.95, L * 0.3, 1.0], [1, 0, 3.95, L - 2.4, 1.0]]);
    translate([0, 0, wh]) cw_part_roof(L, D, 1.8, 0.4, 0, cw_roof_c(seed + 1));
    // 天窗脊
    color(cw_GLASSC()) translate([0, 0, wh + 1.35]) cw_boxc([L * 0.65, 0.9, 0.7]);
    color(cw_METALD()) translate([0, 0, wh + 1.75]) cw_boxc([L * 0.68, 1.1, 0.12]);
    // 门楣导轨
    color(cw_METALC()) translate([-L * 0.15, -D / 2 - 0.13, 3.6]) cw_boxc([5.6, 0.08, 0.14]);
}

// 锯齿厂房：三连锯齿顶（北向天窗）+ 砖墙 + 侧门
module cw_bldg_factory_saw(seed = 0, L = 14, D = 10)
{
    wh = 3.8;
    bc = cw_rnd(seed, 2) == 0 ? cw_BRICK() : cw_PANELD();
    // 可进入锯齿厂房：前墙装卸大门 + 右侧小门洞 + 左墙高窗洞，锯齿顶在上
    cw_part_shell(L, D, wh, bc, t = 0.3, dw = 2.2, dh = 3.0, dx = -L * 0.2,
                  wins = [[0, L * 0.25, 1.05, 1.1, 2.1],
                          for (i = [0 : 2]) [2, -D / 2 + 2 + i * 3, 2.6, 1.0, 1.2]]);
    nt = 3;
    for (i = [0 : nt - 1])
    {
        ty = -D / 2 + (i + 0.5) * D / nt;
        color(cw_roof_c(seed + 2)) translate([0, ty + D / nt * 0.08, wh + 0.75])
            rotate([-22, 0, 0]) cw_boxc([L + 0.4, D / nt * 1.12, 0.14]);
        color(cw_GLASSC()) translate([0, ty - D / nt * 0.42, wh + 0.62]) rotate([70, 0, 0]) cw_boxc([L - 0.8, 1.2, 0.1]);
    }
}

// 砖砌烟囱：锥柱 + 白环带 + 顶箍
module cw_bldg_smokestack(seed = 0, h = 15)
{
    color(cw_BRICKD()) cylinder(h = 1.2, r = 1.7, $fn = 9);
    color(cw_BRICK()) cylinder(h = h, r1 = 1.35, r2 = 0.85, $fn = 9);
    color(cw_MARKW()) translate([0, 0, h * 0.82]) cylinder(h = h * 0.06, r1 = 0.97, r2 = 0.94, $fn = 9);
    color(cw_MARKW()) translate([0, 0, h * 0.62]) cylinder(h = h * 0.06, r1 = 1.06, r2 = 1.03, $fn = 9);
    color(cw_METALD()) translate([0, 0, h - 0.3]) cylinder(h = 0.35, r = 0.92, $fn = 9);
}

// 金属仓库：波纹墙（双色条）+ 浅坡金属顶 + 推拉大门
module cw_bldg_warehouse(seed = 0, L = 12, D = 8)
{
    wh = 3.6;
    mc = cw_rnd(seed, 2) == 0 ? cw_METALC() : [0.38, 0.42, 0.38];
    md = [mc[0] * 0.8, mc[1] * 0.8, mc[2] * 0.8];
    dx = -L * 0.12;
    // 可进入仓库：前墙卷帘大门 + 前后高窗洞，坡顶在上
    cw_part_shell(L, D, wh, mc, t = 0.26, dw = 2.8, dh = 2.6, dx = dx,
                  wins = [[0, L * 0.35, wh - 0.7, 1.2, 0.6], [1, L * 0.35, wh - 0.7, 1.2, 0.6],
                          [1, -L * 0.25, wh - 0.7, 1.2, 0.6]]);
    // 波纹竖肋（前排避开门洞）
    color(md) for (i = [0 : floor(L / 1.1)])
    {
        rx = -L / 2 + 0.55 + i * 1.1;
        translate([rx, D / 2 + 0.01, wh / 2 + 0.15]) cw_boxc([0.34, 0.04, wh - 0.3]);
        if (abs(rx - dx) > 1.6)
            translate([rx, -D / 2 - 0.01, wh / 2 + 0.15]) cw_boxc([0.34, 0.04, wh - 0.3]);
    }
    translate([0, 0, wh]) cw_part_roof(L, D, 1.0, 0.45, 0, cw_roof_c(seed + 4));
}

// 加油站雨棚 + 收银亭（油泵用 cw_prop_pump_gas 另摆；front=-y）
module cw_bldg_gas_canopy(seed = 0)
{
    // 雨棚
    color(cw_CONC()) for (sx = [-1, 1]) translate([sx * 3.0, 0, 1.9]) cw_boxc([0.4, 0.4, 3.8]);
    color(cw_MARKW()) translate([0, 0, 3.9]) cw_boxc([10.5, 6.5, 0.35]);
    color(cw_REDC()) translate([0, -3.3, 3.9]) cw_boxc([10.5, 0.12, 0.35]);
    color(cw_REDC()) translate([0, 3.3, 3.9]) cw_boxc([10.5, 0.12, 0.35]);
    // 收银亭（雨棚后侧，可进入：右门洞 + 左橱窗洞）
    translate([0, 4.9, 0])
    {
        cw_part_shell(4.5, 3.2, 2.9, cw_WHITEW(), t = 0.2, dw = 0.95, dh = 2.1, dx = 1.5, roomH = 2.6,
                      wins = [[0, -0.6, 1.5, 1.9, 1.2]]);
        color(cw_CONCD()) translate([0, 0, 2.9]) cw_slab(4.7, 3.4, 0.15);
        color(cw_REDC()) translate([0, -1.7, 2.55]) cw_boxc([4.0, 0.1, 0.5]);
    }
}

// 军用瞭望塔：四内倾腿 + 平台 + 舱室环窗 + 攒尖顶 + 爬梯
module cw_bldg_guard_tower(seed = 0)
{
    ph = 4.2;
    tl = 4;      // 腿倾角：顶端内收
    r0 = 1.0;    // 腿在杆件中点高度的半间距
    // 顶端内收 = rotate 的 X 角与 y 偏移同号、Y 角与 x 偏移反号
    color(cw_WOODD()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * r0, sy * r0, ph / 2]) rotate([sy * tl, sx * -tl, 0]) cw_boxc([0.18, 0.18, ph + 0.4]);
    color(cw_WOODD()) for (z = [1.4, 2.8])
    {
        rz = r0 + (ph / 2 - z) * tan(tl) + 0.05;   // 横撑跟随腿的收分
        translate([0, -rz, z]) cw_boxc([rz * 2 + 0.2, 0.08, 0.12]);
        translate([0, rz, z]) cw_boxc([rz * 2 + 0.2, 0.08, 0.12]);
        translate([-rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.2, 0.12]);
        translate([rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.2, 0.12]);
    }
    color(cw_WOODC()) translate([0, 0, ph]) cw_slab(2.9, 2.9, 0.14);
    color(cw_OLIVED()) translate([0, 0, ph + 0.14]) cw_slab(2.6, 2.6, 1.0);
    color(cw_DARKC()) translate([0, 0, ph + 1.45]) cw_boxc([2.62, 2.62, 0.62]);
    color(cw_OLIVED()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * 1.24, sy * 1.24, ph + 1.45]) cw_boxc([0.14, 0.14, 0.66]);
    translate([0, 0, ph + 1.78]) cw_part_roof(3.1, 3.1, 1.0, 0.2, 1.55, cw_ROOFM());
    // 爬梯
    color(cw_METALD())
    {
        for (sx = [-1, 1]) translate([sx * 0.25, -1.35, ph / 2 + 0.2]) cw_boxc([0.06, 0.06, ph + 0.4]);
        for (i = [0 : 7]) translate([0, -1.35, 0.5 + i * 0.5]) cw_boxc([0.5, 0.05, 0.05]);
    }
}

// 军营板房：长条单层 + 军绿墙 + 成排窗 + 低坡顶
module cw_bldg_barracks(seed = 0, L = 14, D = 6)
{
    wh = 2.8;
    wc = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_KHAKI();
    nw = max(3, floor((L - 2.4) / 2.0));
    // 可进入营房：中央门洞 + 前后成排窗洞（前排避开门）
    cw_part_shell(L, D, wh, wc, t = 0.24, dw = 1.5, dh = 2.1, dx = 0,
                  wins = [ for (i = [0 : nw - 1]) [1, -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), 1.6, 0.9, 1.0],
                           for (i = [0 : nw - 1]) if (abs(-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)) > 1.1)
                               [0, -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), 1.6, 0.9, 1.0] ]);
    translate([0, 0, wh]) cw_part_roof(L, D, 1.1, 0.45, 0, cw_roof_c(seed + 7));
    color(cw_METALD()) translate([L * 0.32, D * 0.1, wh + 0.9]) cylinder(h = 1.1, r = 0.1, $fn = 6);
}

// 军事指挥部：两层赭石抹灰 + 门廊双柱 + 红星徽
module cw_bldg_hq(seed = 0, L = 12, D = 8)
{
    wh = 6.2;
    nw = max(3, floor((L - 2.4) / 2.2));
    // 底层可进入（中央大门 + 底层前后窗洞），二层实心
    cw_part_shell(L, D, wh, cw_PLASTER(), t = 0.3, dw = 1.6, dh = 2.4, dx = 0, roomH = 2.7,
                  wins = [ for (i = [0 : nw - 1]) if (abs(-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)) > 1.2)
                               [0, -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), 1.7, 1.0, 1.4],
                           for (i = [0 : nw - 1]) if (abs(-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1)) > 1.2)
                               [1, -L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), 1.7, 1.0, 1.4] ]);
    color(cw_CONCD()) for (sy = [-1, 1]) translate([0, sy * (D / 2 + 0.01), 3.2]) cw_boxc([L, 0.06, 0.3]);
    // 二层窗阵（贴墙装饰；底层为洞口）
    for (i = [0 : nw - 1], sy = [-1, 1])
        translate([-L / 2 + 1.4 + i * (L - 2.8) / (nw - 1), sy * (D / 2 + 0.04), 1.7 + 2.9])
            rotate([0, 0, sy > 0 ? 180 : 0]) cw_part_window(1.0, 1.4);
    // 中央门廊 + 红星
    color(cw_WHITEW()) for (sx = [-1, 1]) translate([sx * 1.1, -D / 2 - 0.9, 1.55]) cw_boxc([0.34, 0.34, 3.1]);
    color(cw_CONCD()) translate([0, -D / 2 - 0.85, 3.1]) cw_boxc([3.4, 2.0, 0.25]);
    color(cw_REDC())
    {
        translate([0, -D / 2 - 0.10, wh - 0.85]) cw_boxc([0.75, 0.10, 0.28]);
        translate([0, -D / 2 - 0.10, wh - 0.62]) cw_boxc([0.28, 0.10, 0.5]);
        translate([-0.2, -D / 2 - 0.10, wh - 1.02]) rotate([0, 40, 0]) cw_boxc([0.24, 0.10, 0.5]);
        translate([0.2, -D / 2 - 0.10, wh - 1.02]) rotate([0, -40, 0]) cw_boxc([0.24, 0.10, 0.5]);
    }
    color(cw_CONCD()) translate([0, 0, wh]) difference()
    {
        cw_slab(L + 0.16, D + 0.16, 0.4);
        translate([0, 0, -0.05]) cw_slab(L - 0.5, D - 0.5, 0.6);
    }
    color(cw_METALD()) translate([-L * 0.3, D * 0.2, wh + 1.0]) cylinder(h = 2.2, r = 0.05, $fn = 5);
    color(cw_REDC()) translate([-L * 0.3 + 0.35, D * 0.2, wh + 2.9]) cw_boxc([0.7, 0.02, 0.4]);
}

// 拱顶机库：椭拱壳 + 后墙 + 前大门（半开）
module cw_bldg_hangar(seed = 0, L = 12, D = 14)
{
    // 拱壳（difference 去除地下半）
    color(cw_OLIVED()) difference()
    {
        scale([1, 1, 0.62]) rotate([90, 0, 0]) cylinder(h = D, r = L / 2, center = true, $fn = 10);
        translate([0, 0, -L / 2]) cw_boxc([L + 2, D + 2, L]);
    }
    // 后墙封板
    color(cw_OLIVE()) difference()
    {
        translate([0, D / 2 - 0.15, 0]) scale([0.96, 1, 0.60]) rotate([90, 0, 0]) cylinder(h = 0.5, r = L / 2, $fn = 10);
        translate([0, D / 2, -L / 2]) cw_boxc([L + 2, 3, L]);
    }
    // 前门框 + 双扇滑门（一扇半开露黑）
    color(cw_METALD()) translate([-L * 0.14, -D / 2 - 0.05, 1.9]) cw_boxc([L * 0.36, 0.16, 3.8]);
    color(cw_OLIVE()) translate([L * 0.22, -D / 2 - 0.10, 1.9]) cw_boxc([L * 0.32, 0.14, 3.8]);
    color(cw_DARKC()) translate([L * 0.05, -D / 2 - 0.02, 1.8]) cw_boxc([L * 0.34, 0.06, 3.6]);
    color(cw_MARKW()) translate([-L * 0.14, -D / 2 - 0.16, 2.4]) cw_boxc([L * 0.3, 0.03, 0.5]);
}

// 混凝土碉堡：八角低墩 + 射击缝 + 顶板 + 后门
module cw_bldg_bunker(seed = 0)
{
    color(cw_CONCD()) cylinder(h = 1.7, r1 = 2.7, r2 = 2.3, $fn = 8);
    color(cw_CONC()) translate([0, 0, 1.7]) cylinder(h = 0.35, r = 2.5, $fn = 8);
    color(cw_DARKC()) for (a = [-40, 0, 40])
        rotate([0, 0, a]) translate([0, -2.3, 1.25]) cw_boxc([1.1, 0.5, 0.28]);
    color(cw_DARKC()) translate([0, 2.35, 0.9]) cw_boxc([1.0, 0.5, 1.8]);
    color(cw_SOILC()) translate([0, -1.2, 0]) rotate([0, 0, 30]) scale([2.2, 1.4, 1]) cylinder(h = 0.25, r = 1, $fn = 8);
}

// 监狱囚室楼：灰墙 + 小铁栏窗阵 + 铁门 + 探照灯
module cw_bldg_prison_block(seed = 0, floors = 2, L = 14, D = 7)
{
    fh = 2.9;
    wh = floors * fh + 0.4;
    nw = max(4, floor((L - 2.0) / 1.7));
    // 底层可进入（铁门洞），二层实心；铁栏窗保留为贴墙装饰（窄且高，不可穿越）
    cw_part_shell(L, D, wh, [0.46, 0.46, 0.44], t = 0.3, dw = 1.5, dh = 2.1, dx = -L * 0.2, roomH = fh - 0.3);
    for (f = [0 : floors - 1], i = [0 : nw - 1], sy = [-1, 1])
        translate([-L / 2 + 1.2 + i * (L - 2.4) / (nw - 1), sy * (D / 2 + 0.04), 0.4 + f * fh + 1.7])
            rotate([0, 0, sy > 0 ? 180 : 0]) cw_part_window_bar(0.7, 0.8);
    color(cw_CONCD()) translate([0, 0, wh]) difference()
    {
        cw_slab(L + 0.14, D + 0.14, 0.4);
        translate([0, 0, -0.05]) cw_slab(L - 0.5, D - 0.5, 0.6);
    }
    color(cw_METALD()) translate([L * 0.35, -D / 2 + 0.4, wh + 0.35]) cw_boxc([0.1, 0.1, 0.7]);
    color(cw_YELLOWC()) translate([L * 0.35, -D / 2 + 0.25, wh + 0.72]) rotate([30, 0, 0]) cw_boxc([0.3, 0.3, 0.24]);
}

// 监狱高墙段（沿 x 通长）：壁柱 + 顶部铁丝网
module cw_bldg_prison_wall(len = 10)
{
    h = 4.0;
    color([0.48, 0.48, 0.46]) translate([0, 0, h / 2]) cw_boxc([len, 0.5, h]);
    color(cw_CONCD()) for (i = [0 : floor(len / 5)])
        translate([min(-len / 2 + i * 5, len / 2 - 0.3), 0, h / 2]) cw_boxc([0.7, 0.7, h]);
    color(cw_CONCD()) translate([0, 0, h]) cw_boxc([len, 0.6, 0.18]);
    // 顶部斜撑铁丝
    color(cw_METALD()) for (i = [0 : floor(len / 2.5)])
        translate([-len / 2 + 0.5 + i * 2.5, 0, h + 0.35]) rotate([25, 0, 0]) cw_boxc([0.06, 0.06, 0.75]);
    color(cw_METALD()) for (k = [0 : 2])
        translate([0, -0.14 * k + 0.14, h + 0.25 + k * 0.16]) cw_boxc([len, 0.025, 0.025]);
}

// 监狱大门段：门楼 + 双扇铁门 + 灯
module cw_bldg_prison_gate(seed = 0)
{
    h = 4.6;
    for (sx = [-1, 1]) color([0.48, 0.48, 0.46]) translate([sx * 2.4, 0, h / 2]) cw_boxc([1.4, 1.0, h]);
    color(cw_CONCD()) translate([0, 0, h]) cw_boxc([6.6, 1.1, 0.7]);
    color(cw_REDC()) translate([0, -0.58, h + 0.02]) cw_boxc([2.4, 0.04, 0.4]);
    color(cw_METALD())
    {
        translate([-0.9, -0.2, 1.75]) rotate([0, 0, -14]) cw_boxc([1.9, 0.1, 3.5]);
        translate([1.1, 0.1, 1.75]) rotate([0, 0, 10]) cw_boxc([1.9, 0.1, 3.5]);
    }
    color(cw_MARKW()) translate([0, -0.5, h - 0.75]) cw_boxc([0.9, 0.08, 0.3]);
    color(cw_YELLOWC()) translate([0, -0.62, h + 0.5]) cw_boxc([0.4, 0.3, 0.26]);
}

// 检查站岗亭：白亭红带 + 环窗
module cw_bldg_checkpoint(seed = 0)
{
    color(cw_CONCD()) cw_slab(2.6, 2.6, 0.25);
    color(cw_WHITEW()) translate([0, 0, 0.25]) cw_slab(2.2, 2.2, 2.3);
    color(cw_REDC()) translate([0, 0, 0.7]) cw_boxc([2.26, 2.26, 0.28]);
    color(cw_DARKC()) translate([0, 0, 1.95]) cw_boxc([2.26, 1.7, 0.75]);
    color(cw_DARKC()) translate([0, 0, 1.95]) cw_boxc([1.7, 2.26, 0.75]);
    color(cw_ROOFM()) translate([0, 0, 2.55]) cw_slab(2.7, 2.7, 0.14);
    color(cw_DARKC()) translate([0, -1.13, 1.3]) cw_boxc([0.8, 0.06, 1.9]);
}

// 水塔：四内倾腿桁架 + 柱身筒 + 锥顶罐
module cw_bldg_water_tower(seed = 0)
{
    lh = 6.5;
    tl = 6;      // 腿倾角：顶端内收
    r0 = 1.2;    // 腿在杆件中点高度的半间距
    // 顶端内收 = rotate 的 X 角与 y 偏移同号、Y 角与 x 偏移反号
    color(cw_RUSTC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * r0, sy * r0, lh / 2]) rotate([sy * tl, sx * -tl, 0]) cw_boxc([0.2, 0.2, lh + 0.5]);
    color(cw_METALD()) for (z = [2.0, 4.2])
    {
        rz = r0 + (lh / 2 - z) * tan(tl) + 0.12;   // 横撑跟随腿的收分
        translate([0, -rz, z]) cw_boxc([rz * 2 + 0.16, 0.08, 0.12]);
        translate([0, rz, z]) cw_boxc([rz * 2 + 0.16, 0.08, 0.12]);
        translate([-rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.16, 0.12]);
        translate([rz, 0, z]) cw_boxc([0.08, rz * 2 + 0.16, 0.12]);
    }
    color(cw_rnd(seed, 2) == 0 ? [0.34, 0.38, 0.34] : cw_RUSTC()) translate([0, 0, lh]) cylinder(h = 2.8, r = 1.9, $fn = 9);
    color(cw_METALD()) translate([0, 0, lh + 2.8]) cylinder(h = 1.0, r1 = 2.0, r2 = 0.2, $fn = 9);
    // 角部斜撑：与腿同向内倾
    color(cw_METALD()) translate([0.9, 0.9, lh / 2]) rotate([0, -12, 45]) cw_boxc([0.06, 0.06, lh]);
}

// 乡村小教堂：白墙 + 坡顶 + 鼓座洋葱顶 + 十字
module cw_bldg_chapel(seed = 0)
{
    wh = 3.4;
    L = 4.2;
    // 可进入小教堂：前门洞 + 三面高窗洞，坡顶洋葱顶在上
    cw_part_shell(L, L, wh, cw_WHITEW(), t = 0.24, dw = 1.0, dh = 2.0, dx = 0,
                  wins = [[1, 0, 2.1, 0.55, 1.1], [2, 0, 2.1, 0.55, 1.1], [3, 0, 2.1, 0.55, 1.1]]);
    translate([0, 0, wh]) cw_part_roof(L, L, 1.3, 0.4, 0, cw_ROOFM());
    color(cw_WHITEW()) translate([0, 0, wh + 1.2]) cylinder(h = 1.3, r = 0.75, $fn = 8);
    color([0.58, 0.44, 0.14])
    {
        translate([0, 0, wh + 2.5]) sphere(r = 0.85, $fn = 8);
        translate([0, 0, wh + 3.1]) cylinder(h = 1.0, r1 = 0.6, r2 = 0.04, $fn = 8);
    }
    color([0.58, 0.44, 0.14])
    {
        translate([0, 0, wh + 4.1]) cw_boxc([0.06, 0.06, 0.55]);
        translate([0, 0, wh + 4.28]) cw_boxc([0.3, 0.06, 0.06]);
    }
}

// 公交候车亭：混凝土壳 + 马赛克色带 + 内置坐凳
module cw_bldg_bus_stop(seed = 0)
{
    color(cw_CONCD()) cw_slab(4.2, 2.0, 0.15);
    color(cw_CONC())
    {
        translate([0, 0.85, 1.25]) cw_boxc([4.2, 0.18, 2.2]);
        for (sx = [-1, 1]) translate([sx * 2.0, 0.2, 1.25]) cw_boxc([0.18, 1.5, 2.2]);
        translate([0, 0.1, 2.35]) cw_boxc([4.6, 2.1, 0.16]);
    }
    for (i = [0 : 7])
        color(cw_goods_c(seed * 3 + i)) translate([-1.7 + i * 0.5, 0.74, 1.5]) cw_boxc([0.42, 0.04, 0.6]);
    color(cw_WOODD()) translate([0, 0.45, 0.55]) cw_boxc([3.4, 0.5, 0.1]);
    color(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 1.4, 0.45, 0.28]) cw_boxc([0.3, 0.4, 0.45]);
}

// 废墟残楼：断墙圈 + 齿状破口 + 瓦砾堆
module cw_bldg_ruin(seed = 0, L = 8, D = 6)
{
    bc = cw_rnd(seed, 2) == 0 ? cw_BRICKD() : cw_CONCD();
    hs = [2.6, 1.1, 1.9, 0.7, 2.2, 1.5];
    // 前后墙（分段随机高度）
    for (i = [0 : 3], sy = [-1, 1])
    {
        hh = hs[cw_rnd(seed + i * 7 + sy * 3, 6)];
        color(bc) translate([-L / 2 + (i + 0.5) * L / 4, sy * (D / 2 - 0.15), hh / 2]) cw_boxc([L / 4 + 0.02, 0.3, hh]);
    }
    for (i = [0 : 2], sx = [-1, 1])
    {
        hh = hs[cw_rnd(seed + i * 13 + sx * 5 + 1, 6)];
        color(bc) translate([sx * (L / 2 - 0.15), -D / 2 + (i + 0.5) * D / 3, hh / 2]) cw_boxc([0.3, D / 3 + 0.02, hh]);
    }
    color(cw_DARKC()) translate([-L * 0.1, -(D / 2 - 0.15), 1.0]) cw_boxc([1.2, 0.34, 2.0]);
    // 瓦砾
    color(cw_CONCD()) translate([L * 0.1, 0, 0]) scale([L * 0.32, D * 0.3, 1]) cylinder(h = 0.55, r = 1, $fn = 8);
    for (i = [0 : 4])
        color(cw_rnd(seed + i, 2) == 0 ? bc : cw_CONC())
            translate([cw_rndr(seed * 5 + i * 17, -L * 0.3, L * 0.3), cw_rndr(seed * 7 + i * 23 + 2, -D * 0.28, D * 0.28),
                       0.3 + i * 0.06])
                rotate([0, 0, cw_rnd(seed + i * 3, 180)]) cw_boxc([0.8, 0.5, 0.28]);
    color(cw_RUSTC()) translate([-L * 0.25, D * 0.1, 0.6]) rotate([0, 70, 25]) cylinder(h = 1.8, r = 0.05, $fn = 5);
}

// ================= 植被生态（底面 z=0） =================

// 云杉（东欧针叶林主树）：窄高多层锥
module cw_nature_pine(s = 1.0, seed = 0)
{
    c1 = cw_rnd(seed, 2) == 0 ? cw_PINED() : cw_PINEC();
    c2 = cw_rnd(seed, 2) == 0 ? cw_PINEC() : cw_PINED();
    scale([s, s, s])
    {
        color(cw_TRUNKC()) cylinder(h = 1.2, r = 0.20, $fn = 6);
        color(c1) translate([0, 0, 0.9]) cylinder(h = 1.9, r1 = 1.45, r2 = 0.85, $fn = 7);
        color(c2) translate([0, 0, 2.4]) cylinder(h = 1.8, r1 = 1.15, r2 = 0.55, $fn = 7);
        color(c1) translate([0, 0, 3.9]) cylinder(h = 1.6, r1 = 0.85, r2 = 0.28, $fn = 7);
        color(c2) translate([0, 0, 5.2]) cylinder(h = 1.4, r1 = 0.52, r2 = 0.03, $fn = 7);
    }
}

// 白桦：白干黑斑 + 疏朗浅色团冠
module cw_nature_birch(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(cw_BIRCHW()) cylinder(h = 3.4, r1 = 0.16, r2 = 0.10, $fn = 6);
        color(cw_DARKC()) for (i = [0 : 4])
            translate([0.1 * (cw_rnd(seed + i, 2) == 0 ? 1 : -1), 0.08, 0.5 + i * 0.6])
                cw_boxc([0.14, 0.14, 0.12]);
        color(cw_rnd(seed, 2) == 0 ? [0.40, 0.48, 0.24] : cw_LEAFC()) translate([0, 0, 3.6]) sphere(r = 1.1, $fn = 6);
        color(cw_LEAFC()) translate([0.55, 0.3, 4.2]) sphere(r = 0.7, $fn = 6);
        color([0.44, 0.52, 0.26]) translate([-0.5, -0.25, 4.3]) sphere(r = 0.6, $fn = 6);
    }
}

// 枯树：光秃分叉
module cw_nature_tree_dead(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color([0.28, 0.24, 0.20]) cylinder(h = 3.0, r1 = 0.24, r2 = 0.12, $fn = 6);
        color([0.28, 0.24, 0.20])
        {
            translate([0, 0, 2.6]) rotate([0, 35, cw_rnd(seed, 180)]) cylinder(h = 1.5, r1 = 0.10, r2 = 0.03, $fn = 5);
            translate([0, 0, 2.2]) rotate([0, -42, cw_rnd(seed + 3, 180)]) cylinder(h = 1.2, r1 = 0.09, r2 = 0.03, $fn = 5);
            translate([0, 0, 1.5]) rotate([0, 55, cw_rnd(seed + 7, 180)]) cylinder(h = 0.9, r1 = 0.07, r2 = 0.02, $fn = 5);
        }
    }
}

// 灌木丛
module cw_nature_bush(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(cw_rnd(seed, 2) == 0 ? cw_LEAFD() : cw_PINEC()) translate([0, 0, 0.42]) sphere(r = 0.55, $fn = 6);
        color(cw_LEAFC()) translate([0.4, 0.15, 0.36]) sphere(r = 0.38, $fn = 6);
        color(cw_LEAFD()) translate([-0.35, -0.2, 0.34]) sphere(r = 0.34, $fn = 6);
    }
}

// 荒草簇
module cw_nature_grass_tuft(seed = 0)
{
    for (i = [0 : 2])
        color(i % 2 == 0 ? cw_GRASSD() : [0.50, 0.52, 0.30])
            rotate([0, 0, cw_rnd(seed + i, 180)])
                translate([0, 0, cw_rndr(seed + i + 3, 0.1, 0.18)])
                    cw_boxc([0.5, 0.06, cw_rndr(seed + i + 7, 0.2, 0.38)]);
}

// 芦苇丛（水边/沟渠）
module cw_nature_reeds(seed = 0)
{
    for (i = [0 : 5])
    {
        rx = cw_rndr(seed * 3 + i * 11, -0.35, 0.35);
        ry = cw_rndr(seed * 5 + i * 17 + 2, -0.3, 0.3);
        rh = cw_rndr(seed + i, 0.9, 1.5);
        color([0.36, 0.42, 0.22]) translate([rx, ry, rh / 2]) rotate([cw_rnd(seed + i, 8) - 4, 0, 0]) cw_boxc([0.05, 0.05, rh]);
        if (i % 2 == 0)
            color([0.34, 0.22, 0.12]) translate([rx, ry, rh]) cylinder(h = 0.22, r = 0.045, $fn = 5);
    }
}

// 冰碛岩块
module cw_nature_rock(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color([0.42, 0.42, 0.40]) rotate([0, 0, cw_rnd(seed, 180)]) scale([1, 0.75, 0.55]) sphere(r = 0.9, $fn = 7);
        color([0.36, 0.36, 0.35]) translate([0.5, 0.25, 0]) scale([0.6, 0.5, 0.4]) sphere(r = 0.8, $fn = 6);
        color([0.47, 0.47, 0.44]) translate([-0.4, -0.2, 0.1]) scale([0.5, 0.4, 0.45]) sphere(r = 0.7, $fn = 6);
    }
}

// 树桩
module cw_nature_stump(s = 1.0, seed = 0)
{
    scale([s, s, s])
    {
        color(cw_TRUNKC()) cylinder(h = 0.5, r = 0.32, $fn = 7);
        color([0.52, 0.42, 0.27]) translate([0, 0, 0.5]) cylinder(h = 0.04, r = 0.27, $fn = 7);
        color(cw_TRUNKC()) rotate([0, 0, cw_rnd(seed, 180)]) translate([0.28, 0.1, 0]) cylinder(h = 0.18, r = 0.12, $fn = 5);
    }
}

// 倒木
module cw_nature_log(seed = 0)
{
    color(cw_TRUNKC()) translate([-1.2, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 2.4, r = 0.26, $fn = 7);
    color([0.52, 0.42, 0.27]) translate([1.2, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.21, $fn = 7);
    color([0.52, 0.42, 0.27]) translate([-1.24, 0, 0.26]) rotate([0, 90, 0]) cylinder(h = 0.04, r = 0.21, $fn = 7);
    color(cw_TRUNKC()) translate([0.35, 0.12, 0.38]) rotate([-35, 0, 0]) cylinder(h = 0.5, r = 0.08, $fn = 5);
    if (cw_rnd(seed, 2) == 0)
        color(cw_LEAFD()) translate([-0.6, -0.15, 0.42]) sphere(r = 0.24, $fn = 6);
}

// 撂荒农田：土垄 + 杂草侵占
module cw_nature_field(L = 12, D = 9, seed = 0)
{
    color(cw_SOILC()) cw_slab(L, D, 0.14);
    nr = max(2, floor(D / 1.4));
    for (r = [0 : nr - 1])
    {
        py = -D / 2 + 0.8 + r * (D - 1.6) / (nr - 1);
        color([0.31, 0.23, 0.16]) translate([0, py, 0.14]) cw_slab(L - 0.8, 0.5, 0.12);
    }
    for (i = [0 : 7])
        color(cw_rnd(seed + i, 2) == 0 ? cw_GRASSD() : [0.46, 0.46, 0.26])
            translate([cw_rndr(seed * 7 + i * 19, -(L - 2) / 2, (L - 2) / 2),
                       cw_rndr(seed * 11 + i * 29 + 4, -(D - 2) / 2, (D - 2) / 2), 0.26])
                sphere(r = cw_rndr(seed + i + 8, 0.24, 0.5), $fn = 6);
}

// ================= 道具 / 路标（底面 z=0；带朝向者 front=-y） =================

// 预制板围墙段（ПО-2 式，沿 x 通长）
module cw_prop_fence_concrete(len = 5, h = 2.0)
{
    np = floor(len / 2.5);
    color(cw_CONCD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, 0, (h + 0.3) / 2]) cw_boxc([0.3, 0.34, h + 0.3]);
    color(cw_CONC()) translate([0, 0, h / 2 + 0.1]) cw_boxc([len, 0.14, h - 0.2]);
    color([0.44, 0.43, 0.40]) for (i = [0 : np - 1])
        translate([-len / 2 + (i + 0.5) * len / np, -0.08, h / 2 + 0.1]) cw_boxc([len / np - 0.5, 0.02, h - 0.7]);
}

// 铁丝网围栏段（半透明网面，沿 x 通长）
module cw_prop_fence_chain(len = 6, h = 2.2)
{
    np = floor(len / 3);
    color(cw_METALD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, 0, h / 2]) cw_boxc([0.09, 0.09, h]);
    color(cw_METALD()) translate([0, 0, h - 0.05]) cw_boxc([len, 0.05, 0.05]);
    color([0.40, 0.42, 0.44, 0.45]) translate([0, 0, h / 2 + 0.05]) cw_boxc([len, 0.02, h - 0.3]);
    // 顶部三道刺线斜出
    color(cw_METALD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, -0.18, h + 0.2]) rotate([28, 0, 0]) cw_boxc([0.06, 0.05, 0.55]);
    color(cw_METALD()) for (k = [0 : 1])
        translate([0, -0.28 + k * 0.14, h + 0.30 - k * 0.06]) cw_boxc([len, 0.02, 0.02]);
}

// 木桩铁丝网（战地/农田边界，沿 x 通长）
module cw_prop_fence_barbed(len = 6)
{
    np = floor(len / 2);
    color(cw_WOODD()) for (i = [0 : np])
        translate([-len / 2 + i * len / np, 0, 0.6]) rotate([cw_rnd(i * 7, 10) - 5, 0, 0]) cw_boxc([0.09, 0.09, 1.2]);
    color(cw_METALD()) for (k = [0 : 2])
        translate([0, 0, 0.35 + k * 0.35]) cw_boxc([len, 0.025, 0.025]);
}

// 沙袋胸墙（沿 x 通长，双层错缝）
module cw_prop_wall_sandbag(len = 3)
{
    n = floor(len / 0.55);
    for (r = [0 : 2], i = [0 : n - 1 - (r % 2 == 1 ? 1 : 0)])
        color(r % 2 == 0 ? cw_KHAKI() : [0.38, 0.34, 0.21])
            translate([-len / 2 + 0.3 + i * 0.55 + (r % 2) * 0.27, 0, 0.14 + r * 0.24])
                scale([1, 0.65, 0.45]) sphere(r = 0.30, $fn = 6);
}

// 反坦克拒马（三向工字钢，交叉点离地）
module cw_prop_hedgehog()
{
    translate([0, 0, 0.68])
    {
        color(cw_RUSTC())
        {
            rotate([0, 45, 0]) cw_boxc([0.16, 0.16, 1.9]);
            rotate([0, -45, 0]) cw_boxc([0.16, 0.16, 1.9]);
            rotate([45, 0, 90]) cw_boxc([0.16, 0.16, 1.9]);
        }
        color(cw_METALD()) cw_boxc([0.2, 0.2, 0.2]);
    }
}

// 苏式路灯（混凝土杆，灯头悬向 -y）
module cw_prop_lamp()
{
    color(cw_CONCD())
    {
        cylinder(h = 0.16, r = 0.18, $fn = 7);
        cylinder(h = 5.2, r1 = 0.11, r2 = 0.07, $fn = 6);
    }
    color(cw_METALD()) translate([0, -0.35, 5.05]) rotate([18, 0, 0]) cw_boxc([0.08, 0.85, 0.07]);
    color([0.93, 0.88, 0.70]) translate([0, -0.72, 5.14]) cw_boxc([0.2, 0.5, 0.12]);
}

// 混凝土电力杆：横担 + 绝缘子（沿 x 路边排布时线向即路向）
module cw_prop_pole_concrete(seed = 0)
{
    color(cw_CONCD()) cylinder(h = 7.2, r1 = 0.16, r2 = 0.10, $fn = 6);
    color(cw_METALD()) translate([0, 0, 6.5]) cw_boxc([0.12, 1.8, 0.12]);
    color(cw_MARKW()) for (i = [-1, 0, 1])
        translate([0, i * 0.7, 6.6]) cylinder(h = 0.14, r = 0.05, $fn = 5);
    if (cw_rnd(seed, 3) == 0)
        color(cw_METALD()) translate([0, 0.4, 5.6]) rotate([35, 0, 0]) cw_boxc([0.1, 0.1, 1.6]);
}

// 城镇名牌（白板黑带，出城版红斜杠；front=-y）
module cw_prop_sign_town(seed = 0)
{
    color(cw_METALC()) for (sx = [-1, 1]) translate([sx * 0.8, 0, 1.1]) cw_boxc([0.09, 0.09, 2.2]);
    color(cw_MARKW()) translate([0, -0.06, 1.8]) cw_boxc([2.2, 0.08, 0.75]);
    color(cw_DARKC()) translate([-0.2, -0.11, 1.85]) cw_boxc([1.4, 0.02, 0.26]);
    if (cw_rnd(seed, 2) == 0)
        color(cw_REDC()) translate([0, -0.12, 1.8]) rotate([0, 24, 0]) cw_boxc([2.3, 0.02, 0.16]);
}

// 蓝底方向路牌（单柱白箭头；front=-y）
module cw_prop_sign_road(seed = 0)
{
    color(cw_METALC()) translate([0, 0, 1.35]) cw_boxc([0.09, 0.09, 2.7]);
    color(cw_SIGNB()) translate([0, -0.07, 2.35]) cw_boxc([2.0, 0.08, 0.9]);
    color(cw_MARKW())
    {
        translate([-0.3, -0.12, 2.5]) cw_boxc([1.1, 0.02, 0.16]);
        translate([-0.3, -0.12, 2.2]) cw_boxc([0.8, 0.02, 0.14]);
        translate([0.62, -0.12, 2.35]) rotate([0, 45, 0]) cw_boxc([0.3, 0.02, 0.3]);
    }
}

// 宣传牌坊（红底白字块 + 五角星；front=-y）
module cw_prop_billboard(seed = 0)
{
    color(cw_METALD()) for (sx = [-1, 1]) translate([sx * 2.2, 0, 1.5]) cw_boxc([0.18, 0.18, 3.0]);
    color(cw_REDC()) translate([0, -0.1, 3.7]) cw_boxc([5.6, 0.16, 2.2]);
    color([0.46, 0.10, 0.08]) translate([1.4, -0.12, 3.3]) cw_boxc([1.8, 0.16, 1.0]);
    color(cw_MARKW())
    {
        translate([-1.1, -0.20, 4.1]) cw_boxc([2.6, 0.03, 0.4]);
        translate([-0.8, -0.20, 3.5]) cw_boxc([3.2, 0.03, 0.3]);
    }
    color(cw_YELLOWC())
    {
        translate([2.0, -0.20, 4.35]) cw_boxc([0.55, 0.03, 0.2]);
        translate([2.0, -0.20, 4.1]) rotate([0, 36, 0]) cw_boxc([0.5, 0.03, 0.18]);
        translate([2.0, -0.20, 4.1]) rotate([0, -36, 0]) cw_boxc([0.5, 0.03, 0.18]);
    }
    color(cw_METALD()) for (sx = [-1, 1]) translate([sx * 1.6, 0.5, 1.7]) rotate([28, 0, 0]) cw_boxc([0.12, 0.12, 3.4]);
}

// 纪念碑：台阶基座 + 方尖碑 + 红星
module cw_prop_monument(seed = 0)
{
    color(cw_CONC()) cw_slab(3.2, 3.2, 0.3);
    color(cw_CONC()) translate([0, 0, 0.3]) cw_slab(2.4, 2.4, 0.3);
    color(cw_CONCD()) translate([0, 0, 0.6]) cw_slab(1.3, 1.3, 0.8);
    color([0.56, 0.56, 0.54]) translate([0, 0, 1.4]) cylinder(h = 4.2, r1 = 0.55, r2 = 0.32, $fn = 4);
    color(cw_REDC())
    {
        translate([0, 0, 5.9]) cw_boxc([0.7, 0.1, 0.24]);
        translate([0, 0, 6.1]) cw_boxc([0.24, 0.1, 0.45]);
        translate([-0.18, 0, 5.75]) rotate([0, 40, 0]) cw_boxc([0.2, 0.1, 0.45]);
        translate([0.18, 0, 5.75]) rotate([0, -40, 0]) cw_boxc([0.2, 0.1, 0.45]);
    }
    color(cw_MARKW()) translate([0, -0.66, 1.0]) cw_boxc([0.9, 0.03, 0.3]);
}

// 铜像：基座 + 抬臂人像（front=-y）
module cw_prop_statue(seed = 0)
{
    color(cw_CONCD()) cw_slab(1.8, 1.8, 0.25);
    color(cw_CONC()) translate([0, 0, 0.25]) cw_slab(1.2, 1.2, 1.6);
    color([0.20, 0.24, 0.20])
    {
        translate([0, 0, 2.35]) cw_boxc([0.55, 0.4, 1.1]);      // 大衣躯干
        translate([0, 0, 3.1]) sphere(r = 0.19, $fn = 6);       // 头
        translate([-0.38, -0.18, 2.85]) rotate([0, 55, 15]) cw_boxc([0.16, 0.16, 0.8]);   // 抬臂指向
        translate([0.3, 0, 2.5]) rotate([0, -12, 0]) cw_boxc([0.15, 0.16, 0.7]);
        translate([0, 0, 1.95]) cw_boxc([0.5, 0.36, 0.35]);
    }
    color(cw_MARKW()) translate([0, -0.62, 1.0]) cw_boxc([0.7, 0.03, 0.24]);
}

// 油桶（seed 选色：锈/军绿/蓝/灰）
module cw_prop_barrel(seed = 0)
{
    c = cw_drum_c(seed);
    color(c) cylinder(h = 0.86, r = 0.3, $fn = 8);
    color([c[0] * 0.75, c[1] * 0.75, c[2] * 0.75]) for (z = [0.26, 0.58])
        translate([0, 0, z]) cylinder(h = 0.05, r = 0.315, $fn = 8);
}

// 军用弹药箱堆（大件；seed 决定 1-3 只错叠）
module cw_prop_crate_ammo(seed = 0)
{
    n = 1 + cw_rnd(seed, 3);
    for (i = [0 : n - 1])
        translate([cw_rndr(seed + i * 7, -0.2, 0.2), cw_rndr(seed + i * 13 + 2, -0.15, 0.15), i * 0.42])
            rotate([0, 0, cw_rnd(seed + i * 3, 30) - 15])
            {
                color(cw_OLIVE()) translate([0, 0, 0.21]) cw_boxc([1.0, 0.5, 0.42]);
                color(cw_OLIVED()) translate([0, 0, 0.40]) cw_boxc([1.04, 0.54, 0.06]);
                color(cw_MARKW()) translate([0, -0.26, 0.2]) cw_boxc([0.5, 0.02, 0.12]);
                color(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.52, 0, 0.18]) cw_boxc([0.04, 0.3, 0.1]);
            }
}

// 木托盘
module cw_prop_pallet(seed = 0)
{
    rotate([0, 0, cw_rnd(seed, 40) - 20])
    {
        color(cw_WOODC()) for (i = [0 : 4]) translate([-0.5 + i * 0.25, 0, 0.13]) cw_boxc([0.16, 1.1, 0.04])
        ;
        color(cw_WOODD()) for (y = [-0.45, 0, 0.45]) translate([0, y, 0.06]) cw_boxc([1.15, 0.14, 0.1]);
    }
}

// 轮胎堆
module cw_prop_tires(seed = 0)
{
    for (i = [0 : 2])
        color([0.11, 0.11, 0.11])
            translate([cw_rndr(seed + i, -0.1, 0.1), cw_rndr(seed + i + 4, -0.1, 0.1), 0.11 + i * 0.22])
                rotate([0, 0, cw_rnd(seed + i, 60)])
                    difference()
                    {
                        cylinder(h = 0.22, r = 0.36, center = true, $fn = 8);
                        cylinder(h = 0.3, r = 0.17, center = true, $fn = 8);
                    }
    color([0.11, 0.11, 0.11]) translate([0.62, 0.3, 0.36]) rotate([80, 0, 20])
        difference()
        {
            cylinder(h = 0.22, r = 0.36, center = true, $fn = 8);
            cylinder(h = 0.3, r = 0.17, center = true, $fn = 8);
        }
}

// 铁皮垃圾箱（front=-y）
module cw_prop_dumpster()
{
    color(cw_METALD()) for (sx = [-1, 1]) translate([sx * 0.9, 0, 0.09]) cw_boxc([0.3, 1.1, 0.18]);
    color([0.30, 0.36, 0.30]) translate([0, 0, 0.72]) cw_boxc([2.2, 1.2, 1.1]);
    color(cw_RUSTC()) translate([-0.7, -0.61, 0.5]) cw_boxc([0.6, 0.02, 0.4]);
    color(cw_METALD())
    {
        translate([-0.56, -0.03, 1.32]) cw_boxc([1.06, 1.26, 0.1]);
        translate([0.56, 0.08, 1.42]) rotate([12, 0, 0]) cw_boxc([1.06, 1.26, 0.1]);
    }
}

// 加油机（front=-y）
module cw_prop_pump_gas(seed = 0)
{
    color(cw_CONCD()) cw_slab(1.2, 0.8, 0.14);
    color(cw_rnd(seed, 2) == 0 ? cw_REDC() : [0.20, 0.34, 0.44]) translate([0, 0, 0.14]) cw_slab(0.8, 0.45, 1.5);
    color(cw_MARKW()) translate([0, -0.24, 1.25]) cw_boxc([0.6, 0.04, 0.5]);
    color(cw_DARKC()) translate([0, -0.26, 1.3]) cw_boxc([0.44, 0.03, 0.24]);
    color(cw_METALD())
    {
        translate([0.42, 0, 1.2]) cw_boxc([0.06, 0.12, 0.5]);
        translate([0.42, 0, 0.95]) rotate([0, 25, 0]) cw_boxc([0.05, 0.05, 0.55]);
    }
    color(cw_DARKC()) translate([0.34, 0, 1.5]) cw_boxc([0.12, 0.2, 0.16]);
}

// 卧式储油罐：鞍座 + 顶部人孔 + 爬梯（长轴沿 x）
module cw_prop_fueltank(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? [0.34, 0.38, 0.34] : cw_RUSTC();
    color(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 1.4, 0, 0.35]) cw_boxc([0.5, 1.6, 0.7]);
    color(c) translate([-2.4, 0, 1.5]) rotate([0, 90, 0]) cylinder(h = 4.8, r = 1.1, $fn = 9);
    color(cw_METALD())
    {
        translate([0.6, 0, 2.6]) cylinder(h = 0.3, r = 0.28, $fn = 7);
        translate([-2.45, 0, 1.5]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 1.12, $fn = 9);
        translate([2.39, 0, 1.5]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 1.12, $fn = 9);
    }
    color(cw_RUSTC()) translate([-1.0, -1.06, 1.35]) cw_boxc([1.1, 0.04, 0.5]);
    color(cw_METALD())
    {
        for (sy = [-1, 1]) translate([2.7, sy * 0.2, 1.3]) cw_boxc([0.05, 0.05, 2.6]);
        for (i = [0 : 4]) translate([2.7, 0, 0.4 + i * 0.5]) cw_boxc([0.05, 0.45, 0.05]);
    }
}

// 加油站价目立牌
module cw_prop_sign_pylon(seed = 0)
{
    color(cw_METALD()) translate([0, 0, 2.2]) cw_boxc([0.45, 0.3, 4.4]);
    color(cw_REDC()) translate([0, 0, 4.9]) cw_boxc([1.7, 0.4, 1.0]);
    color(cw_MARKW())
    {
        translate([0, -0.22, 5.1]) cw_boxc([1.2, 0.03, 0.3]);
        for (i = [0 : 2]) translate([-0.4 + i * 0.4, -0.22, 4.68]) cw_boxc([0.24, 0.03, 0.24]);
    }
    color(cw_MARKW()) translate([0, 0, 3.6]) cw_boxc([1.0, 0.34, 0.55]);
}

// 道闸（杆指向 +x；seed 决定开/合）
module cw_prop_barrier_gate(seed = 0)
{
    up = cw_rnd(seed, 2);
    color(cw_CONCD()) cw_slab(0.7, 0.7, 0.2);
    color(cw_METALD()) translate([0, 0, 0.2]) cw_slab(0.4, 0.4, 0.9);
    rotate([0, up == 1 ? -62 : 0, 0]) translate([1.9, 0, 1.0])
    {
        color(cw_MARKW()) cw_boxc([3.8, 0.12, 0.16]);
        color(cw_REDC()) for (i = [0 : 2]) translate([-1.2 + i * 1.2, 0, 0]) cw_boxc([0.55, 0.13, 0.17]);
    }
    color(cw_METALD()) translate([-0.5, 0, 1.0]) cw_boxc([0.6, 0.2, 0.3]);
    if (up == 0) color(cw_METALD()) translate([3.7, 0, 0.45]) cw_boxc([0.1, 0.3, 0.9]);
}

// 桁架通信塔：三段收分 + 横撑 + 鞭状天线
module cw_prop_antenna(seed = 0)
{
    hs = [4.0, 3.2, 2.6];
    ws = [1.4, 1.0, 0.65];
    for (k = [0 : 2])
    {
        z0 = k == 0 ? 0 : (k == 1 ? 4.0 : 7.2);
        // 顶端内收 = rotate 的 X 角与 y 偏移同号、Y 角与 x 偏移反号
        color(cw_METALD()) for (sx = [-1, 1], sy = [-1, 1])
        {
            w0 = ws[k];
            translate([sx * w0 / 2, sy * w0 / 2, z0 + hs[k] / 2]) rotate([sy * 2.5, sx * -2.5, 0]) cw_boxc([0.1, 0.1, hs[k]]);
        }
        color(cw_METALD())
        {
            w1 = ws[k];
            translate([0, -w1 / 2, z0 + hs[k] * 0.55]) cw_boxc([w1, 0.06, 0.08]);
            translate([0, w1 / 2, z0 + hs[k] * 0.55]) cw_boxc([w1, 0.06, 0.08]);
            translate([-w1 / 2, 0, z0 + hs[k] * 0.55]) cw_boxc([0.06, w1, 0.08]);
            translate([w1 / 2, 0, z0 + hs[k] * 0.55]) cw_boxc([0.06, w1, 0.08]);
        }
    }
    color(cw_METALC()) translate([0, 0, 9.8]) cylinder(h = 2.4, r = 0.04, $fn = 5);
    color(cw_REDC()) translate([0, 0, 12.2]) sphere(r = 0.12, $fn = 6);
    color(cw_MARKW()) translate([0.35, 0, 8.2]) cw_boxc([0.5, 0.06, 0.4]);
}

// 探照灯（front=-y 照向）
module cw_prop_searchlight(seed = 0)
{
    color(cw_METALD()) for (a = [0, 120, 240])
        rotate([0, 0, a]) translate([0, 0.35, 0.55]) rotate([16, 0, 0]) cw_boxc([0.08, 0.08, 1.2]);
    color(cw_OLIVED()) translate([0, 0, 1.15]) sphere(r = 0.14, $fn = 6);
    color(cw_OLIVED()) translate([0, -0.15, 1.35]) rotate([55, 0, 0]) cylinder(h = 0.5, r = 0.32, $fn = 8);
    color([0.93, 0.88, 0.70]) translate([0, -0.31, 1.55]) rotate([55, 0, 0]) cylinder(h = 0.04, r = 0.29, $fn = 8);
}

// 军用帐篷（脊沿 x，front=-y 开口）
module cw_prop_tent_military(seed = 0, L = 4.5, D = 3.2)
{
    c = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_KHAKI();
    cd = [c[0] * 0.8, c[1] * 0.8, c[2] * 0.8];
    color(c)
    {
        translate([0, D * 0.28, 0]) rotate([62, 0, 0]) cw_boxc([L, 0.1, D * 0.72]);
        translate([0, -D * 0.28, 0]) rotate([-62, 0, 0]) cw_boxc([L, 0.1, D * 0.72]);
    }
    color(cd)
    {
        translate([0, 0, 1.35]) cw_boxc([L + 0.15, 0.3, 0.12]);
        translate([L / 2 - 0.04, 0, 0.62]) rotate([0, 0, 90]) rotate([76, 0, 0]) cw_boxc([D * 0.94, 0.08, 1.5]);
    }
    color(cw_DARKC()) translate([-L / 2 + 0.02, 0, 0.6]) rotate([0, 0, 90]) rotate([76, 0, 0]) cw_boxc([D * 0.8, 0.06, 1.3]);
    color(cw_WOODD()) for (sy = [-1, 1], sx = [-1, 1])
        translate([sx * (L / 2 - 0.3), sy * (D / 2 + 0.3), 0.15]) rotate([sy * 40, 0, 0]) cw_boxc([0.05, 0.05, 0.4]);
}

// 篝火营地：石圈 + 焦木 + 吊锅架
module cw_prop_campfire(seed = 0)
{
    for (a = [0 : 45 : 315])
        color([0.40, 0.40, 0.38]) rotate([0, 0, a + cw_rnd(seed + a, 14)]) translate([0.5, 0, 0.1])
            scale([1, 0.7, 0.6]) sphere(r = 0.17, $fn = 6);
    color([0.14, 0.12, 0.10])
    {
        translate([0, 0, 0.1]) rotate([0, 80, 20]) cylinder(h = 0.7, r = 0.07, $fn = 5);
        translate([0.1, -0.1, 0.1]) rotate([0, 82, 130]) cylinder(h = 0.6, r = 0.06, $fn = 5);
        translate([-0.1, 0.1, 0.1]) rotate([0, 78, 260]) cylinder(h = 0.65, r = 0.06, $fn = 5);
    }
    color(cw_DARKC()) translate([0, 0, 0]) cylinder(h = 0.05, r = 0.42, $fn = 8);
    color(cw_WOODD()) for (a = [0, 120, 240])
        rotate([0, 0, a]) translate([0, 0.4, 0.55]) rotate([20, 0, 0]) cw_boxc([0.05, 0.05, 1.2]);
    color(cw_METALD()) translate([0, 0, 0.85]) cylinder(h = 0.25, r = 0.15, $fn = 7);
}

// 购物车（front=-y 推行向，网面半透明）
module cw_prop_cart_shop(seed = 0)
{
    rotate([cw_rnd(seed, 3) == 0 ? 78 : 0, 0, cw_rnd(seed, 50)])   // 部分翻倒
    {
        color(cw_METALC())
        {
            for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.32, sy * 0.42, 0.5]) cw_boxc([0.05, 0.05, 0.5]);
            translate([0, 0.5, 1.02]) cw_boxc([0.75, 0.06, 0.06]);
        }
        color([0.55, 0.57, 0.60, 0.5])
        {
            translate([0, 0, 0.75]) cw_boxc([0.7, 0.9, 0.03]);
            for (sx = [-1, 1]) translate([sx * 0.34, 0, 0.9]) cw_boxc([0.02, 0.9, 0.34]);
            translate([0, -0.44, 0.9]) cw_boxc([0.7, 0.02, 0.34]);
            translate([0, 0.44, 0.9]) cw_boxc([0.7, 0.02, 0.34]);
        }
        color(cw_DARKC()) for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 0.28, sy * 0.38, 0.12]) sphere(r = 0.07, $fn = 6);
        color(cw_REDC()) translate([0, 0.52, 0.98]) cw_boxc([0.5, 0.05, 0.05]);
    }
}

// 超市货架（front=-y 取货面；seed 决定余货/倾倒）
module cw_prop_shelf_market(seed = 0, L = 2.4)
{
    color(cw_METALC()) translate([0, 0.12, 0]) cw_slab(L, 0.5, 0.1);
    color([0.52, 0.54, 0.52]) translate([0, 0.3, 1.0]) cw_boxc([L, 0.06, 2.0]);
    color(cw_METALC()) for (z = [0.55, 1.1, 1.65])
        translate([0, 0.1, z]) cw_boxc([L, 0.44, 0.05]);
    for (z = [0.55, 1.1, 1.65], i = [0 : floor(L / 0.35)])
        if (cw_rnd(seed * 5 + z * 41 + i * 7, 3) == 0)
            color(cw_goods_c(seed * 7 + z * 13 + i))
                translate([-L / 2 + 0.25 + i * 0.35, 0.08, z + 0.14])
                    rotate([0, 0, cw_rnd(seed + i * 3, 40) - 20]) cw_boxc([0.2, 0.2, 0.28]);
    // 散落地面的货品
    for (i = [0 : 2])
        color(cw_goods_c(seed * 11 + i * 5))
            translate([cw_rndr(seed + i * 13, -L / 2, L / 2), -0.55 - cw_rndr(seed + i * 7, 0, 0.4), 0.1])
                rotate([cw_rnd(seed + i, 80), 0, cw_rnd(seed + i * 17, 180)]) cw_boxc([0.2, 0.2, 0.28]);
}

// 报刊亭（front=-y；seed 决定卷帘关闭）
module cw_prop_kiosk(seed = 0)
{
    color(cw_CONCD()) cw_slab(2.4, 2.0, 0.15);
    color(cw_rnd(seed, 2) == 0 ? [0.22, 0.36, 0.30] : [0.42, 0.30, 0.22]) translate([0, 0, 0.15]) cw_slab(2.2, 1.8, 2.3);
    if (cw_rnd(seed + 1, 2) == 0)
        color(cw_DARKC()) translate([0, -0.92, 1.5]) cw_boxc([1.6, 0.08, 0.9]);
    else
        color(cw_METALC()) translate([0, -0.92, 1.5]) cw_boxc([1.6, 0.08, 0.9]);
    color(cw_MARKW()) translate([0, -0.94, 2.15]) cw_boxc([1.9, 0.06, 0.4]);
    color(cw_ROOFM()) translate([0, 0, 2.45]) cw_slab(2.5, 2.1, 0.12);
}

// 电话亭（front=-y，半透明玻璃）
module cw_prop_phone_booth(seed = 0)
{
    color(cw_METALD())
    {
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.44, sy * 0.44, 1.15]) cw_boxc([0.1, 0.1, 2.3]);
        translate([0, 0, 2.3]) cw_slab(1.1, 1.1, 0.14);
        cw_slab(1.0, 1.0, 0.1);
    }
    color([0.45, 0.52, 0.56, 0.5]) for (a = [0, 90, 180])
        rotate([0, 0, a]) translate([0, -0.47, 1.3]) cw_boxc([0.8, 0.03, 1.7]);
    color(cw_DARKC()) translate([0, 0.42, 1.6]) cw_boxc([0.3, 0.1, 0.4]);
}

// 村井：石圈 + 双柱小顶 + 辘轳
module cw_prop_well(seed = 0)
{
    color([0.44, 0.44, 0.42]) difference()
    {
        cylinder(h = 0.8, r = 0.7, $fn = 8);
        translate([0, 0, 0.15]) cylinder(h = 0.8, r = 0.5, $fn = 8);
    }
    color(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.62, 0, 1.0]) cw_boxc([0.12, 0.12, 1.6]);
    translate([0, 0, 1.8]) cw_part_roof(1.9, 1.5, 0.55, 0.15, 0, cw_WOODD());
    color(cw_WOODC()) translate([-0.62, 0, 1.35]) rotate([0, 90, 0]) cylinder(h = 1.24, r = 0.08, $fn = 6);
    color(cw_METALD()) translate([0.72, 0, 1.35]) rotate([0, 0, 30]) cw_boxc([0.3, 0.06, 0.06]);
    color(cw_METALD()) translate([0, 0, 0.9]) cw_boxc([0.02, 0.02, 0.9]);
    color(cw_METALC()) translate([0, 0, 0.75]) cylinder(h = 0.2, r = 0.12, $fn = 6);
}

// 柴垛（两桩夹圆木）
module cw_prop_woodpile(seed = 0)
{
    for (r = [0 : 2], i = [0 : 3 - (r % 2 == 1 ? 1 : 0)])
        color(cw_rnd(seed + r * 7 + i, 2) == 0 ? cw_WOODC() : cw_TRUNKC())
            translate([0, -0.45 + i * 0.3 + (r % 2) * 0.15, 0.15 + r * 0.26])
                rotate([0, 90, 0]) cylinder(h = 1.6, r = 0.15, center = true, $fn = 6);
    color(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.75, 0, 0.6]) cw_boxc([0.08, 0.08, 1.2]);
}

// 混凝土腿长椅（front=-y）
module cw_prop_bench()
{
    color(cw_CONCD()) for (sx = [-1, 1]) translate([sx * 0.7, 0, 0.2]) cw_boxc([0.12, 0.48, 0.4]);
    color(cw_WOODC()) for (y = [-0.16, 0, 0.16]) translate([0, y, 0.44]) cw_boxc([1.8, 0.13, 0.05]);
    color(cw_WOODC()) for (z = [0.64, 0.8]) translate([0, 0.26, z]) cw_boxc([1.8, 0.05, 0.12]);
}

// 废弃儿童乐园：锈秋千 + 滑梯 + 沙坑
module cw_prop_playground(seed = 0)
{
    // 秋千
    translate([-1.6, 0, 0])
    {
        color(cw_RUSTC()) for (sx = [-1, 1])
        {
            translate([sx * 1.1, 0.45, 1.1]) rotate([20, 0, 0]) cw_boxc([0.09, 0.09, 2.3]);
            translate([sx * 1.1, -0.45, 1.1]) rotate([-20, 0, 0]) cw_boxc([0.09, 0.09, 2.3]);
        }
        color(cw_RUSTC()) translate([0, 0, 2.15]) rotate([0, 90, 0]) cylinder(h = 2.4, r = 0.05, center = true, $fn = 6);
        color(cw_METALD()) translate([-0.5, 0, 1.55]) cw_boxc([0.03, 0.03, 1.2]);
        color(cw_METALD()) translate([-0.35, 0, 1.55]) cw_boxc([0.03, 0.03, 1.2]);
        color(cw_WOODD()) translate([-0.42, 0, 0.95]) cw_boxc([0.4, 0.24, 0.06]);
        color(cw_METALD()) translate([0.5, 0.1, 2.0]) rotate([35, 0, 0]) cw_boxc([0.03, 0.03, 1.0]);   // 缠住的断链
    }
    // 滑梯
    translate([1.8, 0.3, 0])
    {
        color([0.20, 0.34, 0.44]) translate([0.8, 0, 0.75]) rotate([0, 32, 0]) cw_boxc([2.0, 0.6, 0.08]);
        color(cw_METALC()) for (sy = [-1, 1]) translate([1.55, sy * 0.32, 0.5]) rotate([0, 32, 0]) cw_boxc([1.9, 0.05, 0.16]);
        color(cw_RUSTC()) translate([-0.25, 0, 0.7]) cw_boxc([0.1, 0.5, 1.4]);
        color(cw_METALD()) for (i = [0 : 3]) translate([-0.25, 0, 0.25 + i * 0.35]) cw_boxc([0.05, 0.44, 0.05]);
        color([0.20, 0.34, 0.44]) translate([0.05, 0, 1.35]) cw_boxc([0.5, 0.6, 0.07]);
    }
    // 沙坑
    color([0.48, 0.42, 0.30]) translate([0, -2.0, 0]) cw_slab(2.6, 1.6, 0.12);
    color(cw_WOODD()) for (sy = [-1, 1]) translate([0, -2.0 + sy * 0.83, 0]) cw_slab(2.7, 0.14, 0.2);
    color(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 1.32, -2.0, 0]) cw_slab(0.14, 1.6, 0.2);
}

// 病床（front=-y；seed 决定翻倒）
module cw_prop_bed_hospital(seed = 0)
{
    rotate([0, cw_rnd(seed, 4) == 0 ? 85 : 0, cw_rnd(seed, 30) - 15])
    {
        color(cw_METALC()) for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 0.85, sy * 0.38, 0.3]) cw_boxc([0.07, 0.07, 0.6]);
        color(cw_MARKW()) translate([0, 0, 0.66]) cw_boxc([2.0, 0.9, 0.16]);
        color([0.60, 0.62, 0.60]) translate([0.55, 0, 0.77]) cw_boxc([0.5, 0.7, 0.1]);
        color(cw_METALC())
        {
            translate([0.98, 0, 0.85]) cw_boxc([0.06, 0.9, 0.5]);
            translate([-0.98, 0, 0.8]) cw_boxc([0.06, 0.9, 0.4]);
        }
        color(cw_DARKC()) for (sx = [-1, 1], sy = [-1, 1])
            translate([sx * 0.85, sy * 0.38, 0.05]) sphere(r = 0.06, $fn = 6);
    }
}

// 杂物堆：碎砖 + 板材 + 管件
module cw_prop_debris(seed = 0)
{
    color(cw_CONCD()) scale([1.3, 1.0, 0.5]) cylinder(h = 0.4, r = 1, $fn = 8);
    for (i = [0 : 4])
        color(cw_rnd(seed + i, 3) == 0 ? cw_BRICKD() : (cw_rnd(seed + i + 7, 2) == 0 ? cw_WOODD() : cw_CONC()))
            translate([cw_rndr(seed * 3 + i * 13, -0.9, 0.9), cw_rndr(seed * 5 + i * 19 + 1, -0.7, 0.7), 0.25 + i * 0.07])
                rotate([cw_rnd(seed + i, 20), cw_rnd(seed + i + 3, 16), cw_rnd(seed + i * 7, 180)])
                    cw_boxc([0.7, 0.34, 0.12]);
    color(cw_RUSTC()) translate([0.4, -0.5, 0.35]) rotate([0, 65, 30]) cylinder(h = 1.1, r = 0.05, $fn = 5);
}

// 混凝土公路桥（沿 x 跨越；桥面顶 z≈0.8，锚点=引桥端路面高度，桥墩向下没入河床）
// 布置契约（见 AGENT_GUIDE/ScadTerrain.md）：桥长 ≥ 2.5x河宽，锚点取河岸下切带外的路面高度。
module cw_prop_bridge(L = 30, W = 9, seed = 0)
{
    color(cw_CONC()) translate([0, 0, 0.45]) cw_boxc([L, W, 0.5]);             // 主梁
    color(cw_CONCD()) translate([0, 0, 0.72]) cw_boxc([L, W + 0.4, 0.14]);     // 桥面
    for (sx = [-1, 1])
        color(cw_CONCD()) translate([sx * (L / 2 + 1.6), 0, 0.35]) rotate([0, sx * 12, 0]) cw_boxc([3.8, W + 0.4, 0.14]);
    color(cw_CONCD()) for (px = [-L / 5, L / 5], sy = [-1, 1])
        translate([px, sy * (W / 2 - 0.9), -2.6]) cw_boxc([1.0, 1.0, 6.4]);    // 桥墩
    color(cw_METALD()) for (sy = [-1, 1])
    {
        translate([0, sy * (W / 2 + 0.1), 1.35]) cw_boxc([L, 0.08, 0.12]);
        for (i = [0 : floor(L / 2.4)])
            translate([-L / 2 + 0.6 + i * 2.4, sy * (W / 2 + 0.1), 1.05]) cw_boxc([0.1, 0.08, 0.55]);
    }
    color(cw_MARKW()) translate([0, 0, 0.80]) cw_boxc([L * 0.88, 0.12, 0.02]);
}

// ================= 载具（车头朝 +x，底面 z=0） =================

module cw_veh_wheel(r = 0.34, w = 0.26)
{
    color([0.10, 0.10, 0.10]) translate([0, w / 2, 0]) rotate([90, 0, 0]) cylinder(h = w, r = r, $fn = 7);
    color(cw_METALC()) translate([0, (w + 0.04) / 2, 0]) rotate([90, 0, 0]) cylinder(h = w + 0.04, r = r * 0.42, $fn = 7);
}

// 方盒轿车（拉达式；seed 选色）
module cw_veh_lada(seed = 0)
{
    c = cw_car_c(seed);
    color(c) translate([0, 0, 0.6]) cw_boxc([3.9, 1.7, 0.58]);
    color(c) translate([-0.2, 0, 1.16]) cw_boxc([1.9, 1.6, 0.54]);
    color(cw_GLASSC()) translate([-0.2, 0, 1.16]) cw_boxc([1.65, 1.68, 0.4]);
    color(cw_METALC()) for (sx = [-1, 1]) translate([sx * 1.96, 0, 0.4]) cw_boxc([0.07, 1.5, 0.18]);
    color(cw_MARKW()) for (sy = [-1, 1]) translate([1.96, sy * 0.55, 0.68]) cw_boxc([0.05, 0.26, 0.14]);
    color([0.70, 0.13, 0.10]) for (sy = [-1, 1]) translate([-1.96, sy * 0.55, 0.68]) cw_boxc([0.05, 0.26, 0.14]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.2 * sx, 0.87 * sy, 0.33]) cw_veh_wheel();
}

// 面包车（UAZ 452 式圆头厢车）
module cw_veh_uaz_van(seed = 0)
{
    c = cw_rnd(seed, 3) == 0 ? cw_OLIVE() : [0.48, 0.54, 0.52];
    color(c) translate([-0.2, 0, 1.06]) cw_boxc([3.5, 1.8, 1.5]);
    color(c) translate([1.65, 0, 0.95]) rotate([0, 14, 0]) cw_boxc([0.5, 1.76, 1.2]);
    color(cw_GLASSC()) translate([1.55, 0, 1.5]) rotate([0, 10, 0]) cw_boxc([0.4, 1.7, 0.5]);
    color(cw_GLASSC()) for (sy = [-1, 1]) translate([0.6, sy * 0.92, 1.5]) cw_boxc([0.7, 0.05, 0.45]);
    color(cw_MARKW()) for (sy = [-1, 1]) translate([1.86, sy * 0.55, 0.85]) cylinder(h = 0.06, r = 0.11, $fn = 6);
    color(cw_METALD()) translate([-1.97, 0, 1.0]) cw_boxc([0.06, 1.6, 1.3]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.1 * sx, 0.9 * sy, 0.36]) cw_veh_wheel(0.36, 0.24);
}

// 军卡（乌拉尔式长鼻卡车 + 帆布篷）
module cw_veh_truck_canvas(seed = 0)
{
    color(cw_OLIVED()) translate([2.2, 0, 1.0]) cw_boxc([1.4, 1.9, 0.85]);   // 长鼻机盖
    color(cw_OLIVE()) translate([1.15, 0, 1.25]) cw_boxc([0.9, 2.0, 1.4]);   // 驾驶室
    color(cw_GLASSC()) translate([1.52, 0, 1.62]) cw_boxc([0.3, 1.9, 0.5]);
    color(cw_OLIVED()) translate([-1.1, 0, 1.25]) cw_boxc([3.4, 2.1, 0.5]);  // 货台
    // 帆布拱篷
    color(cw_KHAKI()) difference()
    {
        translate([-1.1, 0, 1.5]) scale([1, 1, 0.75]) rotate([0, 90, 0]) cylinder(h = 3.4, r = 1.05, center = true, $fn = 8);
        translate([-1.1, 0, 0.2]) cw_boxc([3.6, 2.6, 2.6]);
    }
    color(cw_DARKC()) translate([-2.78, 0, 1.95]) scale([1, 1, 0.72]) rotate([0, 90, 0]) cylinder(h = 0.06, r = 0.9, $fn = 8);
    color(cw_MARKW()) for (sy = [-1, 1]) translate([2.92, sy * 0.7, 1.0]) cylinder(h = 0.06, r = 0.1, $fn = 6);
    color(cw_METALD()) translate([2.95, 0, 0.62]) cw_boxc([0.1, 1.9, 0.24]);
    for (sy = [-1, 1])
    {
        translate([2.0, sy * 1.0, 0.48]) cw_veh_wheel(0.48, 0.34);
        translate([-0.4, sy * 1.0, 0.48]) cw_veh_wheel(0.48, 0.34);
        translate([-1.6, sy * 1.0, 0.48]) cw_veh_wheel(0.48, 0.34);
    }
}

// 中巴（PAZ 式；seed 决定涂装）
module cw_veh_bus(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? [0.62, 0.58, 0.44] : [0.20, 0.38, 0.44];
    color(c) translate([0, 0, 1.15]) cw_boxc([6.4, 2.1, 1.9]);
    color(cw_GLASSC()) translate([0.2, 0, 1.75]) cw_boxc([5.4, 2.16, 0.65]);
    color(cw_GLASSC()) translate([3.16, 0, 1.45]) cw_boxc([0.12, 1.8, 0.9]);
    color(cw_DARKC()) translate([2.2, -1.02, 0.95]) cw_boxc([0.9, 0.12, 1.5]);
    color([c[0] * 0.7, c[1] * 0.7, c[2] * 0.7]) translate([0, 0, 0.45]) cw_boxc([6.4, 2.12, 0.5]);
    color(cw_MARKW()) for (sy = [-1, 1]) translate([3.18, sy * 0.7, 0.75]) cylinder(h = 0.06, r = 0.11, $fn = 6);
    color([0.70, 0.13, 0.10]) for (sy = [-1, 1]) translate([-3.18, sy * 0.7, 0.75]) cw_boxc([0.05, 0.26, 0.16]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([2.0 * sx, 1.06 * sy, 0.42]) cw_veh_wheel(0.42, 0.3);
}

// 农用拖拉机（开放驾驶位）
module cw_veh_tractor(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? [0.20, 0.38, 0.24] : [0.56, 0.16, 0.12];
    color(c) translate([0.9, 0, 1.05]) cw_boxc([1.6, 0.9, 0.9]);          // 机罩
    color(cw_METALD()) translate([1.72, 0, 1.0]) cw_boxc([0.1, 0.8, 0.7]);
    color(cw_METALD()) translate([1.2, 0.25, 1.9]) cylinder(h = 0.7, r = 0.06, $fn = 5);   // 排气管
    color(c) translate([-0.5, 0, 1.0]) cw_boxc([1.2, 1.0, 0.5]);
    color(cw_DARKC()) translate([-0.7, 0, 1.5]) cw_boxc([0.5, 0.5, 0.16]);   // 座椅
    color(cw_METALD()) translate([-0.15, 0, 1.6]) rotate([0, -30, 0]) cylinder(h = 0.4, r = 0.03, $fn = 5);
    color(cw_METALD()) translate([-0.05, 0, 1.95]) rotate([0, 60, 0]) cylinder(h = 0.05, r = 0.2, $fn = 7);
    for (sy = [-1, 1]) translate([-0.9, sy * 0.85, 0.75]) cw_veh_wheel(0.75, 0.4);
    for (sy = [-1, 1]) translate([1.3, sy * 0.7, 0.42]) cw_veh_wheel(0.42, 0.26);
}

// 装甲运兵车（BTR 式八轮 + 小炮塔）
module cw_veh_btr(seed = 0)
{
    color(cw_OLIVE()) translate([0, 0, 1.15]) cw_boxc([6.2, 2.3, 0.9]);
    color(cw_OLIVED()) translate([2.6, 0, 1.35]) rotate([0, 24, 0]) cw_boxc([1.6, 2.28, 0.55]);   // 斜首上
    color(cw_OLIVED()) translate([-2.9, 0, 1.2]) rotate([0, -55, 0]) cw_boxc([0.9, 2.26, 0.5]);   // 斜尾
    color(cw_OLIVED()) for (sy = [-1, 1]) translate([0, sy * 1.12, 0.85]) rotate([sy * 30, 0, 0]) cw_boxc([6.0, 0.5, 0.5]);
    color(cw_OLIVE()) translate([0.6, 0, 1.85]) cylinder(h = 0.5, r = 0.65, $fn = 8);            // 炮塔
    color(cw_METALD()) translate([1.1, 0, 2.2]) rotate([0, 87, 0]) cylinder(h = 1.5, r = 0.06, $fn = 5);
    color(cw_OLIVED()) for (i = [0 : 1]) translate([-1.2 - i * 0.9, 0, 1.72]) cylinder(h = 0.14, r = 0.3, $fn = 7);   // 舱盖
    color(cw_MARKW()) translate([-2.2, -1.16, 1.2]) cw_boxc([0.02, 0.02, 0.02]);
    for (sy = [-1, 1], i = [0 : 3])
        translate([2.1 - i * 1.35, sy * 1.05, 0.55]) cw_veh_wheel(0.55, 0.4);
}

// 主战坦克（T 系）：履带箱 + 车体 + 扁圆炮塔 + 长管
module cw_veh_tank(seed = 0)
{
    for (sy = [-1, 1])
    {
        color(cw_METALD()) translate([0, sy * 1.25, 0.55]) cw_boxc([6.0, 0.85, 0.85]);
        color([0.16, 0.17, 0.16]) for (i = [0 : 4])
            translate([-2.2 + i * 1.1, sy * 1.25, 0.5]) rotate([90, 0, 0]) cylinder(h = 0.9, r = 0.42, center = true, $fn = 7);
    }
    color(cw_OLIVE()) translate([0, 0, 1.15]) cw_boxc([6.2, 2.0, 0.55]);
    color(cw_OLIVED()) translate([2.9, 0, 1.15]) rotate([0, 30, 0]) cw_boxc([0.9, 1.98, 0.4]);
    color(cw_OLIVE()) translate([-0.3, 0, 1.55]) scale([1.25, 1, 0.5]) sphere(r = 1.05, $fn = 8);
    color(cw_OLIVED()) translate([1.8, 0, 1.75]) rotate([0, 88, 0]) cylinder(h = 3.2, r1 = 0.10, r2 = 0.07, $fn = 6);
    color(cw_METALD()) translate([4.98, 0, 1.62]) rotate([0, 88, 0]) cylinder(h = 0.3, r = 0.11, $fn = 6);
    color(cw_OLIVED()) translate([-0.5, 0.4, 2.05]) cylinder(h = 0.12, r = 0.26, $fn = 7);
    color(cw_RUSTC()) for (sy = [-1, 1]) translate([-3.1, sy * 0.5, 1.3]) rotate([0, 90, 0]) cylinder(h = 0.5, r = 0.22, $fn = 7);   // 尾部油桶
}

// 坠毁运输直升机（Mi-8 式）：机身侧倾 + 断尾梁 + 折弯旋翼 + 焦土
module cw_veh_heli_wreck(seed = 0)
{
    color(cw_ASPHD()) scale([5.5, 3.5, 1]) cylinder(h = 0.03, r = 1, $fn = 9);   // 焦土
    rotate([0, 0, cw_rnd(seed, 40) - 20])
    {
        rotate([8, 0, 0]) translate([0, 0, 0.9])
        {
            color(cw_OLIVE()) scale([1, 0.62, 0.62]) rotate([0, 90, 0]) cylinder(h = 6.0, r = 1.5, center = true, $fn = 8);   // 机身
            color(cw_GLASSC()) translate([2.9, 0, 0]) scale([0.5, 0.56, 0.56]) sphere(r = 1.45, $fn = 8);   // 机头玻璃
            color(cw_OLIVED()) translate([0.3, 0, 1.0]) cw_boxc([2.6, 1.3, 0.6]);   // 发动机罩
            color(cw_DARKC()) translate([0.5, -0.94, 0.2]) cw_boxc([1.3, 0.1, 0.9]);   // 侧舱门开
            // 旋翼毂 + 折弯桨叶
            color(cw_METALD()) translate([0.3, 0, 1.4]) cylinder(h = 0.5, r = 0.22, $fn = 7);
            for (a = [0, 72, 144, 216, 288])
                color(cw_OLIVED()) rotate([0, 0, a + cw_rnd(seed, 30)]) translate([2.1, 0, 1.75])
                    rotate([0, a % 144 == 0 ? 28 : 14, 0]) cw_boxc([4.0, 0.32, 0.07]);
        }
        // 断尾梁（甩在机尾方向）
        translate([-4.6, 1.2, 0.4]) rotate([0, 0, 25])
        {
            color(cw_OLIVE()) rotate([0, 85, 0]) cylinder(h = 3.4, r1 = 0.5, r2 = 0.28, $fn = 7);
            color(cw_OLIVED()) translate([3.2, 0, 0.6]) cw_boxc([0.8, 0.08, 1.3]);
            color(cw_METALD()) translate([3.3, 0.35, 0.9]) rotate([90, 0, 0]) cylinder(h = 0.16, r = 0.5, $fn = 6);
        }
        color(cw_OLIVED()) translate([1.5, -2.6, 0.1]) rotate([0, 0, 70]) cw_boxc([2.6, 0.3, 0.1]);   // 甩出的桨叶
        for (i = [0 : 3])
            color(cw_rnd(seed + i, 2) == 0 ? cw_OLIVED() : cw_METALD())
                translate([cw_rndr(seed * 3 + i * 17, -3.5, 3.5), cw_rndr(seed * 7 + i * 23 + 2, -2.6, 2.6), 0.1])
                    rotate([0, 0, cw_rnd(seed + i * 5, 180)]) cw_boxc([0.6, 0.4, 0.14]);
    }
}

// 锈蚀弃车残骸：无玻璃 + 缺轮 + 引擎盖掀开
module cw_veh_wreck(seed = 0)
{
    c = [0.32, 0.27, 0.23];
    rotate([0, 0, cw_rnd(seed, 7) - 3]) translate([0, 0, -0.05])
    {
        color(c) translate([0, 0, 0.58]) cw_boxc([3.9, 1.7, 0.56]);
        color([0.27, 0.23, 0.20]) translate([-0.2, 0, 1.12]) cw_boxc([1.9, 1.6, 0.5]);
        color(cw_DARKC()) translate([-0.2, 0, 1.14]) cw_boxc([1.66, 1.66, 0.34]);
        color(cw_RUSTC()) translate([1.45, 0, 1.1]) rotate([0, -35, 0]) cw_boxc([1.0, 1.4, 0.06]);
        color(cw_DARKC()) translate([1.25, 0, 0.9]) cw_boxc([0.8, 1.1, 0.18]);
        color(cw_RUSTC())
        {
            translate([0.8, 0.86, 0.6]) cw_boxc([0.9, 0.02, 0.3]);
            translate([-1.3, -0.86, 0.52]) cw_boxc([1.1, 0.02, 0.32]);
        }
        for (sx = [-1, 1]) translate([1.2 * sx, 0.87, 0.33]) cw_veh_wheel();
        translate([1.2, -0.87, 0.33]) cw_veh_wheel();
        color(cw_METALD()) translate([-1.2, -0.87, 0.28]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.15, $fn = 7);
    }
}

// ================= 武器（loot：平躺地面，枪口朝 +x，profile 顶视可读） =================

// AK 突击步枪（木托 + 弧形弹匣）
module cw_wpn_ak(seed = 0)
{
    translate([0, 0, 0.05])
    {
        color(cw_METALD()) cw_boxc([0.42, 0.07, 0.09]);                                     // 机匣
        color(cw_WOODC()) translate([-0.4, 0, 0]) rotate([0, 0, 4]) cw_boxc([0.4, 0.07, 0.08]);   // 枪托
        color(cw_WOODC()) translate([0.32, 0, 0]) cw_boxc([0.22, 0.06, 0.08]);              // 护木
        color(cw_METALD()) translate([0.62, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.4, r = 0.018, $fn = 5);   // 枪管
        color(cw_METALD()) translate([0.80, 0, 0.015]) cw_boxc([0.05, 0.03, 0.05]);         // 准星座
        color(cw_METALD()) translate([0.06, -0.1, 0]) rotate([0, 0, -18]) cw_boxc([0.07, 0.2, 0.07]);   // 弧形弹匣（平躺伸向 -y）
        color(cw_METALD()) translate([0.1, -0.24, 0]) rotate([0, 0, -38]) cw_boxc([0.06, 0.12, 0.07]);
        color(cw_WOODD()) translate([-0.1, -0.08, 0]) cw_boxc([0.05, 0.09, 0.06]);          // 握把
    }
}

// SVD 狙击步枪（镂空托 + 长管 + 瞄具）
module cw_wpn_svd(seed = 0)
{
    translate([0, 0, 0.05])
    {
        color(cw_METALD()) cw_boxc([0.4, 0.06, 0.08]);
        color(cw_WOODC()) translate([-0.42, -0.02, 0]) cw_boxc([0.42, 0.045, 0.07]);        // 托上梁
        color(cw_WOODC()) translate([-0.5, -0.11, 0]) rotate([0, 0, 16]) cw_boxc([0.3, 0.045, 0.07]);   // 托下斜
        color(cw_WOODC()) translate([0.33, 0, 0]) cw_boxc([0.24, 0.05, 0.07]);
        color(cw_METALD()) translate([0.7, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.55, r = 0.015, $fn = 5);
        color(cw_METALD()) translate([0.99, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.07, r = 0.026, $fn = 5);   // 消焰器
        color(cw_METALD()) translate([-0.05, 0.055, 0.02]) rotate([0, 90, 0]) cylinder(h = 0.24, r = 0.03, $fn = 6);   // 侧置瞄准镜
        color(cw_METALD()) translate([0.02, -0.09, 0]) rotate([0, 0, -14]) cw_boxc([0.05, 0.12, 0.06]);
    }
}

// 莫辛纳甘栓动步枪（全木床）
module cw_wpn_mosin(seed = 0)
{
    translate([0, 0, 0.04])
    {
        color(cw_WOODC()) cw_boxc([0.95, 0.055, 0.07]);
        color(cw_WOODC()) translate([-0.52, -0.03, 0]) rotate([0, 0, 8]) cw_boxc([0.24, 0.06, 0.07]);
        color(cw_METALD()) translate([0.55, 0, 0.01]) rotate([0, 90, 0]) cylinder(h = 0.32, r = 0.014, $fn = 5);
        color(cw_METALD()) translate([0.05, -0.05, 0.02]) rotate([0, 0, -55]) cw_boxc([0.03, 0.09, 0.03]);   // 栓柄
        color(cw_METALD()) translate([0.12, 0, -0.01]) cw_boxc([0.1, 0.06, 0.03]);          // 弹仓
    }
}

// 双管猎枪
module cw_wpn_shotgun(seed = 0)
{
    translate([0, 0, 0.04])
    {
        color(cw_WOODD()) translate([-0.3, -0.015, 0]) rotate([0, 0, 6]) cw_boxc([0.32, 0.06, 0.07]);
        color(cw_WOODC()) translate([0.02, 0, 0]) cw_boxc([0.3, 0.055, 0.07]);
        color(cw_METALD()) for (sy = [-1, 1])
            translate([0.42, sy * 0.016, 0]) rotate([0, 90, 0]) cylinder(h = 0.5, r = 0.016, $fn = 5);
    }
}

// 马卡洛夫手枪
module cw_wpn_pistol(seed = 0)
{
    translate([0, 0, 0.03])
    {
        color(cw_METALD()) cw_boxc([0.17, 0.03, 0.05]);
        color(cw_WOODD()) translate([-0.055, -0.065, 0]) rotate([0, 0, -20]) cw_boxc([0.045, 0.1, 0.05]);
        color(cw_METALD()) translate([0.02, -0.04, 0]) cw_boxc([0.05, 0.05, 0.04]);
    }
}

// RPG 火箭筒（战斗部朝 +x）
module cw_wpn_rpg(seed = 0)
{
    translate([0, 0, 0.06])
    {
        color(cw_OLIVED()) rotate([0, 90, 0]) cylinder(h = 0.85, r = 0.045, center = true, $fn = 6);
        color(cw_WOODC()) translate([-0.28, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.22, r = 0.06, $fn = 6);
        color(cw_METALD()) translate([-0.5, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.14, r1 = 0.05, r2 = 0.09, $fn = 6);   // 尾喷口
        color(cw_OLIVE()) translate([0.52, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.2, r1 = 0.1, r2 = 0.05, $fn = 6);   // 战斗部
        color(cw_METALD()) translate([0.05, -0.07, 0]) cw_boxc([0.04, 0.1, 0.05]);
        color(cw_METALD()) translate([0.16, -0.06, 0]) cw_boxc([0.04, 0.08, 0.05]);
    }
}

// 开盖武器箱（内置步枪 loot）
module cw_wpn_crate(seed = 0)
{
    color(cw_OLIVE()) difference()
    {
        translate([0, 0, 0.26]) cw_boxc([1.4, 0.7, 0.52]);
        translate([0, 0, 0.36]) cw_boxc([1.24, 0.54, 0.5]);
    }
    color(cw_OLIVED()) translate([0, 0.52, 0.12]) rotate([70, 0, 0]) cw_boxc([1.44, 0.74, 0.05]);   // 掀开盖靠边
    color(cw_DARKC()) translate([0, 0, 0.14]) cw_boxc([1.22, 0.52, 0.06]);
    translate([-0.1, 0.1, 0.12]) rotate([0, 0, 8]) cw_wpn_ak(seed);
    translate([0.1, -0.12, 0.12]) rotate([0, 0, -6]) cw_wpn_ak(seed + 3);
    color(cw_MARKW()) translate([0, -0.36, 0.3]) cw_boxc([0.5, 0.02, 0.14]);
}

// ================= 物资（小件 loot，底面 z=0） =================

// 罐头
module cw_item_can(seed = 0)
{
    color(cw_METALC()) cylinder(h = 0.13, r = 0.05, $fn = 7);
    color(cw_goods_c(seed)) translate([0, 0, 0.03]) cylinder(h = 0.07, r = 0.052, $fn = 7);
}

// 油壶（jerry can）
module cw_item_jerrycan(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_REDC();
    color(c) translate([0, 0, 0.24]) cw_boxc([0.35, 0.16, 0.48]);
    color([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8])
    {
        translate([0, 0, 0.45]) rotate([0, 45, 0]) cw_boxc([0.1, 0.17, 0.1]);
        translate([-0.09, 0, 0.51]) cw_boxc([0.1, 0.05, 0.05]);
    }
    color(cw_METALD()) translate([0.13, 0, 0.5]) cylinder(h = 0.05, r = 0.035, $fn = 6);
}

// 医疗箱（白盒红十字）
module cw_item_medkit()
{
    color(cw_MARKW()) translate([0, 0, 0.11]) cw_boxc([0.36, 0.26, 0.22]);
    color(cw_REDC())
    {
        translate([0, -0.131, 0.12]) cw_boxc([0.05, 0.004, 0.14]);
        translate([0, -0.131, 0.12]) cw_boxc([0.14, 0.004, 0.05]);
        translate([0, 0, 0.221]) cw_boxc([0.05, 0.14, 0.004]);
        translate([0, 0, 0.221]) cw_boxc([0.14, 0.05, 0.004]);
    }
    color(cw_METALD()) translate([0, -0.12, 0.2]) cw_boxc([0.08, 0.03, 0.03]);
}

// 弹药盒（军绿提箱）
module cw_item_ammobox(seed = 0)
{
    color(cw_OLIVE()) translate([0, 0, 0.13]) cw_boxc([0.32, 0.16, 0.26]);
    color(cw_OLIVED()) translate([0, 0, 0.25]) cw_boxc([0.34, 0.18, 0.05]);
    color(cw_MARKW()) translate([0, -0.081, 0.12]) cw_boxc([0.18, 0.004, 0.07]);
    color(cw_METALD()) translate([0, 0, 0.29]) cw_boxc([0.1, 0.04, 0.03]);
}

// 行军背包（靠放）
module cw_item_backpack(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_KHAKI() : cw_OLIVE();
    rotate([0, 0, cw_rnd(seed, 90)]) rotate([-16, 0, 0])
    {
        color(c) translate([0, 0, 0.3]) scale([1, 0.6, 1.15]) sphere(r = 0.26, $fn = 7);
        color([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8]) translate([0, -0.1, 0.52]) scale([0.8, 0.5, 0.5]) sphere(r = 0.2, $fn = 6);
        color(cw_WOODD()) for (sx = [-1, 1]) translate([sx * 0.12, 0.15, 0.3]) cw_boxc([0.05, 0.03, 0.4]);
    }
}

// 野战电台（旋钮 + 鞭天线）
module cw_item_radio(seed = 0)
{
    color(cw_OLIVED()) translate([0, 0, 0.16]) cw_boxc([0.38, 0.2, 0.32]);
    color(cw_METALD()) for (i = [0 : 2]) translate([-0.1 + i * 0.1, -0.11, 0.22]) cylinder(h = 0.025, r = 0.025, $fn = 6);
    color(cw_DARKC()) translate([0.05, -0.11, 0.1]) cw_boxc([0.14, 0.01, 0.06]);
    color(cw_METALC()) translate([0.14, 0.06, 0.32]) cylinder(h = 0.9, r = 0.012, $fn = 5);
}

// 煤油马灯（暖光罩）
module cw_item_lantern(seed = 0)
{
    color(cw_METALD()) cylinder(h = 0.05, r = 0.08, $fn = 7);
    color([0.90, 0.80, 0.52, 0.8]) translate([0, 0, 0.05]) cylinder(h = 0.14, r1 = 0.06, r2 = 0.045, $fn = 7);
    color(cw_METALD())
    {
        translate([0, 0, 0.19]) cylinder(h = 0.04, r = 0.06, $fn = 7);
        translate([0, 0, 0.24]) rotate([0, 90, 0]) cylinder(h = 0.02, r = 0.06, $fn = 6);
    }
}

// 铺盖卷（行军床垫）
module cw_item_bedroll(seed = 0)
{
    c = cw_rnd(seed, 2) == 0 ? cw_OLIVE() : [0.44, 0.36, 0.28];
    color(c) translate([-0.35, 0, 0.12]) rotate([0, 90, 0]) cylinder(h = 0.7, r = 0.12, $fn = 8);
    color([c[0] * 0.8, c[1] * 0.8, c[2] * 0.8]) for (x = [-0.2, 0.15])
        translate([x, 0, 0.12]) rotate([0, 90, 0]) cylinder(h = 0.05, r = 0.125, $fn = 8);
}

// 钢盔
module cw_item_helmet(seed = 0)
{
    color(cw_rnd(seed, 2) == 0 ? cw_OLIVE() : cw_OLIVED()) difference()
    {
        scale([1.15, 1, 0.8]) sphere(r = 0.15, $fn = 8);
        translate([0, 0, -0.2]) cw_boxc([0.5, 0.5, 0.4]);
    }
    color(cw_OLIVED()) cylinder(h = 0.02, r = 0.16, $fn = 8);
}

// 救援物资箱（木箱红十字，空投样式）
module cw_item_crate_supply(seed = 0)
{
    color([0.48, 0.38, 0.24]) translate([0, 0, 0.3]) cw_boxc([0.8, 0.8, 0.6]);
    color(cw_WOODD())
    {
        for (sz = [0.06, 0.55]) translate([0, 0, sz]) cw_boxc([0.84, 0.84, 0.09]);
        for (a = [45, -45]) translate([0, -0.41, 0.3]) rotate([0, a, 0]) cw_boxc([0.85, 0.02, 0.1]);
    }
    color(cw_MARKW()) translate([0, -0.42, 0.32]) cw_boxc([0.3, 0.02, 0.3]);
    color(cw_REDC())
    {
        translate([0, -0.43, 0.32]) cw_boxc([0.07, 0.02, 0.24]);
        translate([0, -0.43, 0.32]) cw_boxc([0.24, 0.02, 0.07]);
    }
}
