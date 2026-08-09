// Brotato3D 固定场景：末日小镇（deadly_town）
// =====================================================================================
// 设计前提（来自 Brotato3D 运行时，改图前必读）：
//   * 竞技场半径 halfExtent = [260, 160]（arenas.json），SCAD 的 x/y 直接对应，
//     |x|>260 或 |y|>160 之外有隐形墙，且相机 clamp 保证那里永远看不见 —— 不要往外堆东西。
//   * 相机固定：位于目标 +30 高 / +11（世界 z）处俯视，FOV 60°。换算到 SCAD 空间就是
//     "从 -y 方向往 +y 看的 70° 俯视"，单屏地面约 66 m (x) x 41 m (y)。
//     => front = -y 的件正对镜头；朝 +y 的建筑只会露背面和屋顶。
//   * 场景纯装饰：玩家/敌人不与建筑碰撞，所以街区的价值在"可读的规划 + 方位地标"，
//     而不是阻挡。空地要有用途（停车场/院子/球场），不能是裸草皮。
//
// 规划：65 x 58 m 的方格街坊（一街坊约一屏），南北路 x = 0, ±65, ±130, ±195，
//       东西路 y = 0, ±58, ±116；主街 y=0 与州道 x=0 贯穿全图并出图。
//
//        x -260   -195   -130    -65     0      65     130    195    260
//   y 160 ┌───────────────────────────────────────────────────────────┐  松林带
//     116 │ 农场/筒仓 │  住宅   │  住宅  │ 住宅 │ 学校 │ 公园/营地│工场│
//      58 │ 拖车营地 │  住宅   │ 教堂区 │ 市中心（联排商业）│汽车旅馆│废车场│
//       0 ├─────────────────── 主  街 ────────────────────────────────┤
//     -58 │  水塔    │  住宅   │  超市  │加油站│餐车/车行│  仓储工业区   │
//    -116 │  林地    │  住宅   │  住宅  │ 检查站 │ 学校运动场 │  货场   │
//    -160 └───────────────────────────────────────────────────────────┘  松林带
//
// 叙事焦点（每 1–2 屏一个）：主街翻覆校车、超市停车场、加油站、州道南口军事检查站、
//                            公园幸存者营地、东侧废车场、北侧农场。
// =====================================================================================

use <../../lib/kit_deadly.scad>
use <../../lib/kit_layout.scad>

$fn = 12;

// ================= 街区构件（局部原点 = 临街路缘中点，朝向 -y 面对街道） =================

// 一户住宅地块：草坪 + 车道 + 绿篱 + 主体（房/门廊房/废墟/拖车）+ 后院棚树栅栏 + 街缘信箱。
// 纵深 26 m，两排背靠背正好填满 58 m 街坊并留出后巷。
module dt_lot(seed = 0, wide = 15)
{
    v = lay_randi(seed, 1, 12);
    translate([0, 10, 0]) dd_ground_grass(L = wide, D = 15, seed = seed);
    color(dd_ASPHC()) translate([wide * 0.34, 6.5, 0]) dd_slab(3.2, 13, 0.11);
    if (v % 3 != 0)
        translate([-wide / 2 + 0.5, 9, 0]) rotate([0, 0, 90]) dd_prop_hedge(len = 13, h = 1.05, seed = seed + 3);
    if (v < 4) translate([0, 13.5, 0]) dd_bldg_house(seed = seed, L = 9, D = 7);
    else if (v < 7) translate([0, 14, 0]) dd_bldg_house_porch(seed = seed, L = 9.5, D = 7);
    else if (v < 9) translate([0, 13.5, 0]) dd_bldg_house(seed = seed + 5, L = 10, D = 7.5);
    else if (v < 11) translate([0, 13.5, 0]) dd_bldg_ruin(seed = seed, L = 9, D = 7);
    else translate([0, 13, 0]) dd_bldg_trailer(seed = seed, L = 9, D = 4);
    translate([-wide * 0.26, 21.5, 0]) dd_bldg_shed(seed = seed + 7, L = 3.2, D = 2.6);
    translate([wide * 0.3, 20.5, 0]) dd_nature_tree(s = lay_randr(seed, 4, 0.85, 1.2), seed = seed);
    translate([0, 24.5, 0]) dd_prop_fence(len = wide);
    translate([wide * 0.42, 1.4, 0]) dd_prop_mailbox();
    if (v % 4 == 0) translate([wide * 0.34, 4.2, 0]) rotate([0, 0, 90]) dd_veh_sedan(seed = seed);
    if (v % 5 == 0) translate([-wide * 0.32, 4.6, 0]) dd_prop_trash(seed = seed);
    if (v % 7 == 0) translate([wide * 0.18, 18, 0]) dd_nature_bush(s = 1.2, seed = seed + 2);
}

// 住宅排：n 户沿 x 排开（原点 = 第一户的临街路缘点）
module dt_res_row(n = 4, seed = 0, step = 16)
{
    lay_row(n, step, 0, seed = seed) dt_lot(seed = $seed, wide = step - 1);
}

// 联排商业（店面朝 -y，退线到街心 12.9 m 处，正好贴人行道）
module dt_shops(n = 4, seed = 0, step = 16, fmax = 3)
{
    lay_row(n, step, 0, seed = seed)
        translate([0, 12.9 + lay_randr($seed, 5, -0.4, 0.4), 0])
            dd_bldg_block(seed = $seed, L = step - 0.7, D = 11, floors = 1 + lay_randi($seed, 6, fmax));
}

// 街道人行道对（沿 x 铺在路两侧）
module dt_walks(L = 100, half = 5.9)
{
    for (sy = [-1, 1]) translate([0, sy * half, 0]) dd_ground_sidewalk(L = L, W = 1.8);
}

// ================= 地基 =================

color([0.34, 0.39, 0.23]) translate([0, 0, -0.18]) cube([580, 380, 0.35], center = true);

// ================= 路网 =================

dd_ground_road(L = 560, W = 10, seed = 11);                                   // 主街 y = 0
rotate([0, 0, 90]) dd_ground_road(L = 360, W = 9, seed = 12);                  // 州道 x = 0
for (y = [-116, -58, 58, 116]) translate([0, y, 0]) dd_ground_road(L = 400, W = 7.5, seed = 300 + y);
for (x = [-195, -130, -65, 65, 130, 195])
    translate([x, 0, 0]) rotate([0, 0, 90]) dd_ground_road(L = 260, W = 7.5, seed = 400 + x);
for (x = [-195, -130, -65, 0, 65, 130, 195], y = [-116, -58, 0, 58, 116])
    translate([x, y, 0]) dd_ground_cross(W = 8.2, seed = x + y * 3 + 900);

// 人行道只铺在市中心与主街商业段：它是"街区被规划过"的最直接信号
translate([0, 0, 0]) dt_walks(L = 250, half = 5.9);
translate([0, 58, 0]) dt_walks(L = 200, half = 4.65);
translate([0, -58, 0]) dt_walks(L = 200, half = 4.65);
for (x = [-65, 0, 65])
    translate([x, 0, 0]) rotate([0, 0, 90]) dt_walks(L = 110, half = 4.65);

// ================= 市中心（x -65..65, y -58..58） =================

// 主街北侧联排商业（正脸朝镜头）
translate([-54, 0, 0]) dt_shops(n = 4, seed = 21, step = 14, fmax = 3);
translate([13, 0, 0]) dt_shops(n = 3, seed = 22, step = 16, fmax = 3);
translate([-4, 16, 0]) dd_bldg_block(seed = 23, L = 12, D = 12, floors = 3);   // 转角银行
// 主街南侧：屋顶与背立面朝镜头，用低矮体量 + 大面积铺装承接
translate([-14, -18, 0]) rotate([0, 0, 180]) dd_bldg_block(seed = 24, L = 16, D = 12, floors = 2);
translate([30, -18, 0]) rotate([0, 0, 180]) dd_bldg_block(seed = 25, L = 14, D = 12, floors = 1);
translate([50, -18.5, 0]) rotate([0, 0, 180]) dd_bldg_block(seed = 26, L = 13, D = 12, floors = 2);

// 街坊内部停车场（把"街坊背面"变成有用途的开阔战斗场）
translate([-34, 38, 0]) dd_ground_lot(L = 52, D = 26, seed = 31, bands = 2);
translate([34, 40, 0]) dd_ground_lot(L = 48, D = 22, seed = 32, bands = 2);
translate([-36, -44, 0]) dd_ground_lot(L = 50, D = 20, seed = 33, bands = 2);
lay_scatter(9, -56, -12, 30, 46, seed = 34)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_van(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(7, 12, 56, 32, 48, seed = 35)
    lay_pick($seed) { dd_veh_pickup(seed = $seed); dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(6, -58, -14, -48, -32, seed = 36)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
for (p = [[-52, 30], [-16, 30], [-52, 46], [-16, 46], [14, 32], [50, 32], [-54, -32], [-18, -32]])
    translate([p[0], p[1], 0]) dd_prop_lamp();
translate([-58, 26, 0]) dd_prop_dumpster();
translate([-40, 26, 0]) rotate([0, 0, 12]) dd_prop_dumpster();
translate([22, 27, 0]) dd_prop_dumpster();

// 市政广场 + 教堂（尖塔是市中心的方位锚）
translate([46, 46, 0]) dd_bldg_church(seed = 41, L = 9, D = 16);
translate([30, 34, dd_layer(1)]) dd_ground_concrete(L = 22, D = 16, seed = 42);
for (p = [[24, 30], [34, 30], [24, 38], [34, 38]]) translate([p[0], p[1], 0]) dd_prop_bench();
translate([29, 34, 0]) dd_nature_tree(s = 1.4, seed = 43);
translate([21, 44, 0]) dd_prop_hedge(len = 14, h = 1.2, seed = 44);

// 主街事故点：横在路口的半挂 + 翻覆校车 + 锥桶（出生点 (0,0) 正前方的第一眼焦点）
translate([-30, 1.6, 0]) rotate([0, 0, 194]) dd_veh_bus(seed = 51);
translate([-16, -2.0, 0]) rotate([0, 0, 8]) dd_veh_flipped(seed = 52);
translate([-44, 2.2, 0]) rotate([0, 0, 176]) dd_veh_wreck(seed = 53);
translate([-62, -1.8, 0]) rotate([0, 0, 158]) dd_veh_truck(seed = 56);
lay_scatter(14, -50, -6, -4.2, 4.2, seed = 54) dd_prop_cone();
translate([-22, 4.6, 0]) dd_prop_gascan();
translate([-38, -4.4, 0]) dd_prop_barricade();
translate([-52, 4.4, 0]) dd_prop_barricade();
lay_scatter(10, -55, 5, -4.5, 4.5, seed = 55) dd_prop_debris(seed = $seed);

// 主街路边停车（平行停在路缘）：把宽路面切成"车道 + 停车带"，一眼就是有秩序的街
for (p = [[-72, 3.4, 4], [-64, 3.4, -3], [26, 3.4, 2], [34, 3.4, -4], [44, 3.4, 3],
          [-8, -3.4, 182], [2, -3.4, 176], [56, -3.4, 184], [-46, -3.4, 178]])
    translate([p[0], p[1], 0]) rotate([0, 0, p[2]])
        lay_pick(p[0] + 700) { dd_veh_sedan(seed = p[0] + 3); dd_veh_pickup(seed = p[0]); dd_veh_van(seed = p[0] + 7); dd_veh_wreck(seed = p[0]); }

// ================= 超市（主街南侧，x -65..0） =================

translate([-34, -46, 0]) rotate([0, 0, 180]) dd_bldg_warehouse(seed = 61, L = 34, D = 17);
translate([-34, -23, 0]) dd_ground_lot(L = 56, D = 20, seed = 62, bands = 2);
lay_scatter(11, -60, -8, -32, -16, seed = 63)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_van(seed = $seed); dd_veh_pickup(seed = $seed); dd_veh_wreck(seed = $seed); }
for (x = [-58, -40, -22, -8]) translate([x, -14, 0]) dd_prop_lamp();
translate([-58, -34, 0]) dd_prop_container(seed = 64, stack = 1);
translate([-50, -37, 0]) dd_prop_dumpster();
lay_scatter(8, -62, -6, -38, -14, seed = 65)
    lay_pick($seed) { dd_prop_crate(); dd_prop_debris(seed = $seed); dd_prop_pallet(seed = $seed); }
translate([-62, -20, 0]) rotate([0, 0, 90]) dd_prop_chainlink(len = 22, seed = 66);

// ================= 加油站 + 餐车（主街南侧，x 0..65） =================

translate([22, -26, 0]) dd_bldg_gasstation(seed = 71);
translate([6, -12, 0]) rotate([0, 0, 22]) dd_veh_pickup(seed = 72);
translate([40, -14, 0]) rotate([0, 0, 200]) dd_veh_wreck(seed = 73);
translate([2, -40, 0]) dd_prop_tank(seed = 74, s = 0.7);
lay_scatter(9, 4, 42, -44, -34, seed = 75)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_debris(seed = $seed); }
translate([54, -40, 0]) dd_bldg_diner(seed = 76);
translate([54, -25, 0]) dd_ground_lot(L = 22, D = 12, seed = 77, bands = 1);
lay_scatter(4, 46, 62, -28, -22, seed = 78)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); }
translate([62, -54, 0]) rotate([0, 0, 90]) dd_prop_hedge(len = 18, h = 1.3, seed = 79);

// ================= 教堂区与西部商业（x -130..-65） =================

translate([-117, 0, 0]) dt_shops(n = 3, seed = 81, step = 18, fmax = 2);
translate([-70, -14, 0]) rotate([0, 0, 180]) dd_bldg_block(seed = 82, L = 15, D = 12, floors = 2);
translate([-96, -16, 0]) rotate([0, 0, 180]) dd_bldg_block(seed = 83, L = 14, D = 11, floors = 1);
translate([-112, -34, 0]) dd_ground_lot(L = 30, D = 18, seed = 84, bands = 2);
lay_scatter(6, -124, -100, -40, -28, seed = 85)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([-80, -44, 0]) dd_bldg_warehouse(seed = 86, L = 22, D = 14);
translate([-80, -28, 0]) dd_ground_concrete(L = 26, D = 12, seed = 87);
translate([-100, 40, 0]) dd_bldg_church(seed = 88, L = 9, D = 16);
translate([-100, 24, 0]) dd_ground_concrete(L = 18, D = 10, seed = 89);
translate([-114, 30, 0]) rotate([0, 0, 90]) dd_prop_hedge(len = 24, h = 1.2, seed = 90);
translate([-86, 30, 0]) rotate([0, 0, 90]) dd_prop_hedge(len = 24, h = 1.2, seed = 91);
lay_scatter(7, -126, -70, 30, 52, seed = 92) dd_nature_tree(s = lay_randr($seed, 5, 1.0, 1.4), seed = $seed);
translate([-124, 46, 0]) dd_prop_bench();
translate([-121, 20, 0]) dd_prop_bench();

// ================= 住宅区（西 / 北 / 南） =================

// 放置约定：dt_res_row 沿 +x 生长，所以 rotate(180) 的背面排必须从街坊「东端」起算，
// 起点 x = 正面排起点 + (n-1) * step，否则整排会长到隔壁街坊甚至地图外。
// 街坊宽 65（路心距），路半宽 3.75，n=4/step=14 的排占 55 m，起点取街心 +11.5。

// x -195..-130
translate([-183.5, 62.2, 0]) dt_res_row(n = 4, seed = 101, step = 14);
translate([-141.5, 53.8, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 102, step = 14);
translate([-183.5, 4.2, 0]) dt_res_row(n = 4, seed = 103, step = 14);
translate([-141.5, -4.2, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 104, step = 14);
translate([-183.5, -53.8, 0]) dt_res_row(n = 4, seed = 105, step = 14);
translate([-141.5, -62.2, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 106, step = 14);
translate([-183.5, -111.8, 0]) dt_res_row(n = 4, seed = 107, step = 14);
translate([-141.5, 111.8, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 108, step = 14);

// x -130..-65 上下两排（中段留给教堂区）
translate([-118.5, 62.2, 0]) dt_res_row(n = 4, seed = 111, step = 14);
translate([-76.5, 111.8, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 112, step = 14);
translate([-76.5, -62.2, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 113, step = 14);
translate([-118.5, -111.8, 0]) dt_res_row(n = 4, seed = 114, step = 14);

// x -65..0
translate([-53.5, 62.2, 0]) dt_res_row(n = 4, seed = 121, step = 14);
translate([-11.5, 111.8, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 122, step = 14);
translate([-53.5, -111.8, 0]) dt_res_row(n = 4, seed = 123, step = 14);
translate([-11.5, -62.2, 0]) rotate([0, 0, 180]) dt_res_row(n = 4, seed = 124, step = 14);

// x 0..65 北段（两排背靠背，别让 y 80..116 空着）
translate([11.5, 62.2, 0]) dt_res_row(n = 3, seed = 131, step = 16);
translate([43.5, 111.8, 0]) rotate([0, 0, 180]) dt_res_row(n = 3, seed = 132, step = 16);

// x 130..195 北段
translate([141.5, 62.2, 0]) dt_res_row(n = 3, seed = 133, step = 16);
translate([173.5, 111.8, 0]) rotate([0, 0, 180]) dt_res_row(n = 3, seed = 134, step = 16);
translate([190, 88, 0]) dd_ground_dirt(L = 20, D = 34, seed = 135);
lay_scatter(8, 182, 196, 66, 108, seed = 136)
    lay_pick($seed) { dd_nature_tree(s = 1.1, seed = $seed); dd_prop_debris(seed = $seed); dd_nature_bush(s = 1.3, seed = $seed); }

// 后巷：住宅排背靠背之间的土路与杂物，避免出现"两排屋顶夹一条纯草缝"
for (y = [29, -29, 87, -87]) translate([-130, y, 0]) dd_ground_track(L = 128, W = 3.6, seed = 140 + y);
lay_scatter(16, -196, -66, -32, -26, seed = 141)
    lay_pick($seed) { dd_prop_trash(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_barrel(seed = $seed); }
lay_scatter(14, -196, -66, 26, 32, seed = 142)
    lay_pick($seed) { dd_prop_trash(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_tires(seed = $seed); }
lay_scatter(10, -196, -70, 84, 90, seed = 143)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_pallet(seed = $seed); }

// ================= 学校（x 65..130, y -116..-58） =================

translate([98, -100, 0]) dd_bldg_block(seed = 151, L = 46, D = 14, floors = 2);
translate([98, -78, 0]) dd_ground_lot(L = 52, D = 20, seed = 152, bands = 2);
translate([76, -70, 0]) rotate([0, 0, 4]) dd_veh_bus(seed = 153);
translate([76, -76, 0]) rotate([0, 0, -3]) dd_veh_bus(seed = 154);
translate([118, -72, 0]) rotate([0, 0, 186]) dd_veh_bus(seed = 155);
lay_scatter(6, 78, 118, -84, -74, seed = 156)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([98, -112, 0]) dd_ground_grass(L = 54, D = 16, seed = 157);            // 操场
translate([98, -112, dd_layer(1)]) dd_ground_track(L = 50, W = 3.0, seed = 158);
translate([70, -112, 0]) rotate([0, 0, 90]) dd_prop_chainlink(len = 14, seed = 159);
translate([126, -112, 0]) rotate([0, 0, 90]) dd_prop_chainlink(len = 14, seed = 160);
lay_along([[70, -120], [126, -120]], step = 9, seed = 161) dd_prop_chainlink(len = 9, seed = $seed);
translate([132, -90, 0]) dd_bldg_watertower(s = 0.8, seed = 162);

// ================= 公园与幸存者营地（x 65..130, y 58..116） =================

translate([98, 88, 0]) dd_ground_grass(L = 56, D = 48, seed = 171);
lay_scatter(26, 70, 128, 64, 112, seed = 172)
    lay_pick($seed)
    {
        dd_nature_tree(s = lay_randr($seed, 5, 1.0, 1.5), seed = $seed);
        dd_nature_pine(s = lay_randr($seed, 6, 1.1, 1.5), seed = $seed);
        dd_nature_bush(s = 1.3, seed = $seed);
    }
// 公园水塘：泥滩 + 数摊水面（单个大 puddle 会变成一个黑坑）
translate([84, 74, dd_layer(1)]) dd_ground_dirt(L = 26, D = 18, seed = 173,
                                      c1 = [0.34, 0.30, 0.20], c2 = [0.27, 0.24, 0.17]);
for (p = [[80, 72, 2.2], [88, 76, 2.6], [84, 70, 1.8], [90, 70, 1.4]])
    translate([p[0], p[1], dd_layer(1) + 0.01]) dd_ground_puddle(s = p[2], seed = 1730 + p[0]);
lay_scatter(7, 72, 96, 66, 82, seed = 1731) dd_nature_bush(s = 1.4, seed = $seed);
translate([110, 100, dd_layer(1)]) dd_ground_dirt(L = 24, D = 16, seed = 174);    // 营地夯土场
translate([104, 102, 0]) dd_prop_tent(seed = 175, s = 1.3);
translate([112, 105, 0]) rotate([0, 0, 34]) dd_prop_tent(seed = 176, s = 1.2);
translate([118, 98, 0]) rotate([0, 0, -22]) dd_prop_tent(seed = 177, s = 1.4);
translate([110, 96, 0]) dd_prop_campfire(seed = 178);
translate([120, 106, 0]) dd_prop_tank(seed = 179, s = 0.45);
lay_along([[96, 92], [124, 92]], step = 3.2, seed = 180) dd_prop_sandbags(len = 3.2, seed = $seed);
lay_scatter(8, 100, 124, 94, 110, seed = 181)
    lay_pick($seed) { dd_prop_crate(); dd_prop_barrel(seed = $seed); dd_prop_gascan(); dd_prop_pallet(seed = $seed); }
translate([94, 66, 0]) dd_prop_bench();
translate([106, 66, 0]) dd_prop_bench();
translate([72, 96, 0]) rotate([0, 0, 12]) dd_veh_bus(seed = 182);
translate([98, 62, 0]) dd_ground_track(L = 52, W = 3.0, seed = 183);

// ================= 诊所与药房街坊（x 65..130, y -58..0） =================
// 主街南侧的救护据点：分诊帐篷 + 隔离墩 + 救护车，是全图第二个"人类还在抵抗"的点。

translate([98, -22, 0]) rotate([0, 0, 180]) dd_bldg_block(seed = 401, L = 32, D = 14, floors = 2);
translate([98, -6, 0]) dd_ground_concrete(L = 40, D = 12, seed = 402);
translate([98, -42, 0]) dd_ground_lot(L = 44, D = 18, seed = 403, bands = 2);
translate([82, -9, 0]) rotate([0, 0, 174]) dd_veh_van(seed = 404);
translate([92, -9, 0]) rotate([0, 0, 182]) dd_veh_van(seed = 405);
translate([112, -10, 0]) rotate([0, 0, 12]) dd_prop_tent(seed = 406, s = 1.6);
translate([120, -12, 0]) rotate([0, 0, -18]) dd_prop_tent(seed = 407, s = 1.4);
lay_along([[74, -13], [126, -13]], step = 3.2, seed = 408) dd_prop_jersey(len = 3.2, seed = $seed);
lay_scatter(9, 78, 120, -50, -36, seed = 409)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); dd_veh_pickup(seed = $seed); }
lay_scatter(8, 74, 124, -18, -4, seed = 410)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_cone(); dd_prop_debris(seed = $seed); }
for (x = [78, 100, 122]) translate([x, -34, 0]) dd_prop_lamp();
translate([126, -50, 0]) rotate([0, 0, 90]) dd_prop_hedge(len = 16, h = 1.2, seed = 411);

// ================= 东主街商业带（x 65..130, y 0..58） =================

translate([76, 0, 0]) dt_shops(n = 4, seed = 421, step = 14, fmax = 2);
translate([100, 38, 0]) dd_ground_lot(L = 50, D = 22, seed = 422, bands = 2);
lay_scatter(8, 78, 122, 30, 46, seed = 423)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_van(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([76, 50, 0]) dd_prop_dumpster();
translate([120, 50, 0]) rotate([0, 0, 14]) dd_prop_dumpster();
for (p = [[80, 30], [118, 30], [80, 46], [118, 46]]) translate([p[0], p[1], 0]) dd_prop_lamp();
translate([100, 52, 0]) rotate([0, 0, 0]) dd_prop_chainlink(len = 30, seed = 424);

// ================= 汽车旅馆与二手车行（东主街 x 130..195） =================

translate([162, 22, 0]) dd_bldg_motel(seed = 191, units = 8);
translate([162, 10, 0]) dd_ground_lot(L = 40, D = 12, seed = 192, bands = 1);
lay_scatter(6, 146, 180, 8, 14, seed = 193)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); dd_veh_van(seed = $seed); }
translate([138, 44, 0]) dd_bldg_block(seed = 194, L = 14, D = 11, floors = 2);
translate([176, 46, 0]) dd_bldg_house(seed = 195, L = 10, D = 8);
translate([160, -22, 0]) dd_ground_lot(L = 54, D = 26, seed = 196, bands = 3);    // 二手车行
lay_scatter(16, 136, 184, -32, -12, seed = 197)
    lay_pick($seed)
    {
        dd_veh_sedan(seed = $seed);
        dd_veh_pickup(seed = $seed);
        dd_veh_van(seed = $seed);
        dd_veh_wreck(seed = $seed);
    }
translate([160, -8, 0]) dd_prop_billboard(seed = 198);
for (x = [140, 160, 180]) translate([x, -34, 0]) dd_prop_lamp();
lay_along([[134, -36], [186, -36]], step = 8.5, seed = 199) dd_prop_chainlink(len = 8.5, seed = $seed);

// ================= 仓储工业区（x 130..195, y -116..-58 东侧 + 195..260） =================

translate([166, -74, 0]) dd_bldg_warehouse(seed = 201, L = 30, D = 18);
translate([166, -54, 0]) dd_ground_concrete(L = 40, D = 16, seed = 202);
translate([196, -70, 0]) dd_prop_tank(seed = 203, s = 1.1);
translate([214, -74, 0]) dd_prop_tank(seed = 204, s = 0.9);
translate([205, -92, 0]) dd_ground_gravel(L = 40, D = 26, seed = 205);
translate([196, -96, 0]) dd_prop_container(seed = 206, stack = 2);
translate([208, -100, 0]) rotate([0, 0, 6]) dd_prop_container(seed = 207, stack = 1);
translate([216, -88, 0]) rotate([0, 0, -8]) dd_prop_container(seed = 208, stack = 2);
translate([150, -52, 0]) rotate([0, 0, 184]) dd_veh_truck(seed = 209);
translate([186, -50, 0]) rotate([0, 0, 172]) dd_veh_truck(seed = 210, trailer = 0);
lay_scatter(12, 140, 196, -68, -46, seed = 211)
    lay_pick($seed) { dd_prop_pallet(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_tires(seed = $seed); }
lay_along([[228, -110], [228, -40]], step = 8.5, seed = 212) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[136, -112], [228, -112]], step = 8.5, seed = 213) dd_prop_chainlink(len = 8.5, seed = $seed);

// 废车场（x 195..260, y -40..40）
translate([224, 0, 0]) dd_ground_gravel(L = 60, D = 76, seed = 221);
lay_scatter(34, 200, 250, -34, 34, seed = 222)
    lay_pick($seed)
    {
        dd_veh_wreck(seed = $seed);
        dd_veh_sedan(seed = $seed);
        dd_veh_flipped(seed = $seed);
        dd_veh_van(seed = $seed);
    }
lay_scatter(20, 200, 250, -34, 34, seed = 223)
    lay_pick($seed) { dd_prop_tires(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_debris(seed = $seed); }
translate([206, 30, 0]) dd_bldg_shed(seed = 224, L = 12, D = 8);
translate([246, 22, 0]) dd_prop_container(seed = 225, stack = 3);
lay_along([[196, 40], [252, 40], [252, -40], [196, -40]], step = 8.5, seed = 226) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([236, 62, 0]) dd_prop_radiomast(s = 0.9, seed = 227);
translate([252, 4, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 228);

// 东北工场
translate([214, 96, 0]) dd_bldg_warehouse(seed = 231, L = 24, D = 15);
translate([214, 78, dd_layer(1)]) dd_ground_gravel(L = 34, D = 18, seed = 232);
translate([196, 82, 0]) dd_prop_container(seed = 233, stack = 1);
translate([236, 108, 0]) dd_bldg_silo(seed = 234, s = 0.9);
lay_scatter(8, 196, 240, 72, 88, seed = 235)
    lay_pick($seed) { dd_prop_pallet(seed = $seed); dd_prop_crate(); dd_prop_barrel(seed = $seed); }

// ================= 拖车营地与水塔（x -260..-195） =================

translate([-228, -50, 0]) dd_ground_gravel(L = 56, D = 60, seed = 241);
for (i = [0 : 3])
    translate([-248 + i * 13, -32, 0]) rotate([0, 0, lay_randr(241 + i, 3, -4, 4)])
        dd_bldg_trailer(seed = 250 + i, L = 9, D = 4);
for (i = [0 : 3])
    translate([-248 + i * 13, -58, 0]) rotate([0, 0, 180 + lay_randr(245 + i, 3, -4, 4)])
        dd_bldg_trailer(seed = 260 + i, L = 9, D = 4);
translate([-228, -45, dd_layer(1)]) dd_ground_track(L = 54, W = 4, seed = 242);
lay_scatter(9, -252, -204, -66, -26, seed = 243)
    lay_pick($seed) { dd_veh_pickup(seed = $seed); dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(14, -252, -204, -68, -24, seed = 244)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_trash(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_tires(seed = $seed); }
translate([-236, -74, 0]) dd_prop_campfire(seed = 245);
translate([-230, -72, 0]) rotate([0, 0, 20]) dd_prop_tent(seed = 246, s = 1.2);
lay_along([[-256, -80], [-200, -80]], step = 8.5, seed = 247) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([-226, 62, 0]) dd_bldg_watertower(s = 1.15, seed = 248);
translate([-226, 44, 0]) dd_ground_gravel(L = 26, D = 14, seed = 249);
translate([-238, 40, 0]) dd_prop_barrel(seed = 250);

// 西侧近郊：果园与荒地
translate([-228, 6, 0]) rotate([0, 0, 90]) dd_nature_field_rows(L = 30, D = 22, seed = 251);
lay_scatter(18, -256, -202, 92, 130, seed = 252)
    lay_pick($seed) { dd_nature_tree(s = 1.2, seed = $seed); dd_nature_pine(s = 1.3, seed = $seed); }
translate([-228, -102, 0]) dd_ground_dirt(L = 40, D = 26, seed = 253);
lay_scatter(10, -252, -204, -114, -90, seed = 254)
    lay_pick($seed) { dd_nature_stump(s = 1.1, seed = $seed); dd_nature_log(seed = $seed); dd_prop_debris(seed = $seed); }

// ================= 农场（北带 y 116..150） =================

translate([-30, 132, 0]) rotate([0, 0, 180]) dd_bldg_barn(seed = 261, L = 12, D = 14);
translate([-12, 134, 0]) dd_bldg_silo(seed = 262, s = 1.0);
translate([-4, 136, 0]) dd_bldg_silo(seed = 263, s = 0.85);
translate([-30, 118, 0]) dd_ground_dirt(L = 44, D = 16, seed = 264);
translate([-72, 134, 0]) dd_nature_field_rows(L = 28, D = 20, seed = 265);
translate([26, 132, 0]) dd_nature_pumpkin_patch(L = 26, D = 20, seed = 266);
translate([56, 128, 0]) dd_nature_crop_patch(L = 22, D = 16, seed = 267);
translate([4, 122, 0]) rotate([0, 0, -28]) dd_veh_harvester(seed = 268);
lay_scatter(7, -50, 10, 116, 126, seed = 269, rot = false) dd_prop_haybale(seed = $seed);
lay_along([[-80, 116], [70, 116]], step = 8.5, seed = 270) dd_prop_fence(len = 8.5);
translate([80, 136, 0]) dd_prop_windturbine_fallen(seed = 271, s = 1.0);

// ================= 军事检查站（州道南口 y -116..-150） =================

translate([0, -132, 0]) dd_ground_concrete(L = 46, D = 26, seed = 281);
lay_along([[-15, -124], [15, -124]], step = 3.2, seed = 282) dd_prop_jersey(len = 3.2, seed = $seed);
lay_along([[-15, -140], [15, -140]], step = 3.2, seed = 283) dd_prop_jersey(len = 3.2, seed = $seed);
translate([-12, -130, 0]) dd_prop_sandbags(len = 6, h = 1.1, seed = 284);
translate([12, -130, 0]) dd_prop_sandbags(len = 6, h = 1.1, seed = 285);
translate([-18, -136, 0]) rotate([0, 0, 90]) dd_prop_sandbags(len = 8, h = 1.0, seed = 286);
translate([18, -136, 0]) rotate([0, 0, 90]) dd_prop_sandbags(len = 8, h = 1.0, seed = 287);
translate([-8, -146, 0]) dd_prop_tent(seed = 288, s = 1.5);
translate([8, -146, 0]) rotate([0, 0, -14]) dd_prop_tent(seed = 289, s = 1.4);
translate([0, -150, 0]) dd_prop_campfire(seed = 290);
translate([24, -128, 0]) rotate([0, 0, 96]) dd_veh_truck(seed = 291);
translate([-26, -142, 0]) rotate([0, 0, 74]) dd_veh_bus(seed = 292);
for (x = [-20, 20]) translate([x, -122, 0]) dd_prop_lamp();
lay_scatter(12, -22, 22, -148, -120, seed = 293)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_cone(); dd_prop_debris(seed = $seed); }
translate([-30, -120, 0]) dd_prop_barricade();
translate([30, -120, 0]) dd_prop_barricade();
translate([0, -118, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 294);

// ================= 主街堵车与东西出口 =================

translate([-250, 2.4, 0]) lay_row(9, 11, 0, seed = 301)
    lay_jitter($seed, 1.1, 0.6, 5)
        lay_pick($seed)
        {
            dd_veh_sedan(seed = $seed);
            dd_veh_van(seed = $seed);
            dd_veh_pickup(seed = $seed);
            dd_veh_wreck(seed = $seed);
        }
translate([200, -2.4, 0]) lay_row(5, 11.5, 0, seed = 302)
    lay_jitter($seed, 1.0, 0.6, 5)
        lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); dd_veh_van(seed = $seed); }
translate([-252, -12, 0]) dd_prop_billboard(seed = 303);
translate([248, 14, 0]) rotate([0, 0, 180]) dd_prop_billboard(seed = 304);
translate([-244, -6, 0]) dd_prop_sign_fallen(seed = 305);
lay_along([[-256, 8.5], [-140, 8.5]], step = 30, seed = 306, offset = 8) dd_prop_pole(seed = $seed);
lay_along([[140, -8.5], [256, -8.5]], step = 30, seed = 307, offset = 6) dd_prop_pole(seed = $seed);
lay_along([[-6.2, -156], [-6.2, -120]], step = 26, seed = 308, offset = 6) dd_prop_pole(seed = $seed);

// 街角小品：消防栓与路灯按街口节奏排布（不铺满，只在有铺装的地方出现）
for (p = [[-60, 6.4], [-20, 6.4], [20, 6.4], [60, 6.4], [-60, -6.4], [20, -6.4],
          [-60, 51.6], [20, 51.6], [-60, -51.6], [20, -51.6]])
    translate([p[0], p[1], 0]) dd_prop_hydrant();
for (p = [[-62, 7.2], [-24, 7.2], [14, 7.2], [52, 7.2], [-44, -7.2], [-6, -7.2], [32, -7.2], [66, -7.2]])
    translate([p[0], p[1], 0]) dd_prop_lamp();

// ================= 边界林带（视觉围墙：告诉玩家"这里过不去"） =================

lay_scatter(66, -272, 272, 140, 158, seed = 311)
    lay_pick($seed)
    {
        dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed);
        dd_nature_pine(s = lay_randr($seed, 6, 1.1, 1.6), seed = $seed);
        dd_nature_tree(s = lay_randr($seed, 7, 1.2, 1.6), seed = $seed);
    }
lay_scatter(66, -272, 272, -158, -140, seed = 312)
    lay_pick($seed)
    {
        dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed);
        dd_nature_pine(s = lay_randr($seed, 6, 1.1, 1.6), seed = $seed);
        dd_nature_tree(s = lay_randr($seed, 7, 1.2, 1.6), seed = $seed);
    }
lay_scatter(44, -272, -238, -138, 138, seed = 313)
    lay_pick($seed)
    {
        dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed);
        dd_nature_tree(s = lay_randr($seed, 6, 1.1, 1.6), seed = $seed);
    }
lay_scatter(38, 240, 272, -138, 138, seed = 314)
    lay_pick($seed)
    {
        dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed);
        dd_nature_tree(s = lay_randr($seed, 6, 1.1, 1.6), seed = $seed);
    }
// 林缘过渡：树桩、倒木与灌木，避免林带像一堵贴上去的墙
lay_scatter(20, -260, 260, 128, 142, seed = 315)
    lay_pick($seed) { dd_nature_stump(s = 1.1, seed = $seed); dd_nature_log(seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); }
lay_scatter(20, -260, 260, -142, -128, seed = 316)
    lay_pick($seed) { dd_nature_stump(s = 1.1, seed = $seed); dd_nature_log(seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); }
lay_scatter(14, -238, -226, -130, 130, seed = 317)
    lay_pick($seed) { dd_nature_bush(s = 1.4, seed = $seed); dd_nature_stump(s = 1.0, seed = $seed); }
lay_scatter(12, 228, 240, -130, 130, seed = 318)
    lay_pick($seed) { dd_nature_bush(s = 1.4, seed = $seed); dd_nature_log(seed = $seed); }

// ================= 收尾：街坊内部的疏密纹理 =================

// 未开发的空地（南部两个街坊）：荒草、土斑、伐木痕迹，与规划街区形成对比
translate([40, -90, 0]) dd_ground_dirt(L = 46, D = 30, seed = 321);
lay_scatter(14, 12, 62, -110, -66, seed = 322)
    lay_pick($seed) { dd_nature_tree(s = 1.1, seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); dd_nature_stump(s = 1.1, seed = $seed); }
lay_scatter(10, 12, 62, -110, -66, seed = 323)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_tires(seed = $seed); dd_nature_log(seed = $seed); }
translate([-160, 132, 0]) dd_ground_dirt(L = 50, D = 24, seed = 324);
lay_scatter(12, -190, -130, 120, 140, seed = 325)
    lay_pick($seed) { dd_nature_stump(s = 1.2, seed = $seed); dd_nature_log(seed = $seed); dd_nature_bush(s = 1.3, seed = $seed); }

// 街道级碎屑：只沿铺装边缘撒，保持路面可读
lay_scatter(26, -250, 250, -7.5, 7.5, seed = 331) dd_prop_debris(seed = $seed);
lay_scatter(16, -190, 190, 51, 65, seed = 332) dd_prop_debris(seed = $seed);
lay_scatter(16, -190, 190, -65, -51, seed = 333) dd_prop_debris(seed = $seed);
lay_scatter(12, -190, 190, 109, 123, seed = 334) dd_prop_debris(seed = $seed);
lay_scatter(12, -190, 190, -123, -109, seed = 335) dd_prop_debris(seed = $seed);
lay_scatter(30, -250, 250, -150, 150, seed = 336) dd_nature_grass(seed = $seed);
lay_scatter(18, -250, 250, -150, 150, seed = 337) dd_ground_puddle(s = lay_randr($seed, 5, 0.8, 1.6), seed = $seed);
