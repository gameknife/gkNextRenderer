// Brotato3D 固定场景：沙漠荒野（deadly_desert）
// =====================================================================================
// 设计前提（同 deadly_town.scad 抬头，改图前必读）：
//   * 竞技场 halfExtent = [340, 220]；|x|>340 / |y|>220 永远看不见。
//   * 相机 = 从 -y 往 +y 的 70° 俯视，单屏约 66 x 41 m；front = -y 的件正对镜头。
//
// 沙漠地图最容易做成"一整块沙色 + 噪点"。这版的结构来自四层，而不是撒点：
//   1) 地表分区：板结沙地 / 龟裂干湖 / 碎石戈壁 / 沙丘带 —— 大块地表本身就是构图
//   2) 一条笔直的沙漠公路 + 一条矿区支路，把地图切成可读的四象限
//   3) 三处人造聚落（路口镇、废车场、矿区）各自有铺装、围栏和用途
//   4) 岩丘/水塔/信号塔/中心支轴灌溉圈作为远景地标，跑图时始终能定位
//
//        x -340    -200     -60     60      200     340
//   y 220 ┌────────────────────────────────────────────┐ 沙丘 + 岩带
//     120 │  干 湖 盐 碱 滩   │ 灌溉圈 │  岩 丘 群 / 风 电 残 骸 │
//      40 │ 拖车营地 │  废 车 场  │ 路口镇（加油/旅馆/餐车）│ 矿 区 │
//       0 ├══════════════ 沙 漠 公 路 ═══════════════════┤
//     -60 │  仙人掌荒滩 │ 检查站 │ 车队残骸 │ 采 石 台 地   │
//    -140 │      干 涸 河 床（自西向东）      │ 骨 骸 与 岩 群   │
//    -220 └────────────────────────────────────────────┘ 沙丘 + 岩带
//
// 叙事焦点：路口镇、公路车队残骸、军事检查站、废车场、矿区、干河床里的翻覆卡车。
// =====================================================================================

use <../lib/kit_deadly.scad>
use <../lib/kit_overhill.scad>   // 沙漠植被：oh_nature_cactus / oh_nature_deadtree
use <../lib/kit_layout.scad>

$fn = 12;

// ================= 地貌构件 =================

module desert_dune(x, y, sx, sy, seed)
{
    color([0.55 + (seed % 4) * 0.018, 0.39, 0.21])
        translate([x, y, -0.28]) scale([sx, sy, 0.55]) sphere(r = 1, $fn = 16);
}

module desert_mesa(x, y, s, seed)
{
    color([0.39 + (seed % 3) * 0.025, 0.25, 0.14])
    {
        translate([x, y, 1.4 * s]) cylinder(h = 2.8 * s, r1 = 3.5 * s, r2 = 2.7 * s, center = true, $fn = 7);
        translate([x + 0.5 * s, y - 0.3 * s, 3.1 * s]) cylinder(h = 1.0 * s, r1 = 2.4 * s, r2 = 2.0 * s, center = true, $fn = 7);
    }
}

module desert_rock(s = 1.0, seed = 0)
{
    rock_color = [0.34 + (seed % 4) * 0.025, 0.25 + (seed % 3) * 0.018, 0.17];
    color(rock_color)
        rotate([0, 0, seed % 180])
            scale([1.35 * s, 0.9 * s, 0.65 * s]) sphere(r = 1, $fn = 7);
    if (seed % 3 == 0)
        color([rock_color[0] * 0.86, rock_color[1] * 0.86, rock_color[2] * 0.86])
            translate([0.9 * s, -0.35 * s, 0])
                scale([0.55 * s, 0.42 * s, 0.34 * s]) sphere(r = 1, $fn = 6);
}

module desert_rock_cluster(n, x0, x1, y0, y1, seed, min_s = 0.8, max_s = 2.2)
{
    lay_scatter(n, x0, x1, y0, y1, seed = seed)
        desert_rock(s = lay_randr($seed, 4, min_s, max_s), seed = $seed);
}

// 干涸河床的一段（沿 x）：龟裂河底 + 两岸砾石埂 + 冲积巨石
module desert_wash(L = 80, W = 26, seed = 0)
{
    dd_ground_cracked(L = L, D = W, seed = seed, c = [0.50, 0.42, 0.28]);
    for (sy = [-1, 1])
        color([0.44, 0.34, 0.21]) translate([0, sy * W * 0.52, 0.02]) dd_slab(L, W * 0.16, 0.12);
    lay_scatter(7, -L / 2, L / 2, -W * 0.4, W * 0.4, seed = seed + 3)
        desert_rock(s = lay_randr($seed, 4, 0.6, 1.6), seed = $seed);
    lay_scatter(4, -L / 2, L / 2, -W * 0.45, W * 0.45, seed = seed + 7)
        oh_nature_deadtree(s = lay_randr($seed, 5, 0.8, 1.2), seed = $seed);
}

// 中心支轴灌溉圈（废弃）：绿褐色圆盘 + 同心环 + 中心机组 + 一条支轴臂。
// 俯视地图上没有比"沙漠里一个绿色圆"更强的地标。
module desert_pivot_field(r = 42, seed = 0)
{
    color([0.34, 0.36, 0.19]) cylinder(h = 0.10, r = r, $fn = 20);
    for (i = [1 : 4])
        color(i % 2 == 0 ? [0.30, 0.33, 0.17] : [0.38, 0.38, 0.21])
            translate([0, 0, 0.10]) cylinder(h = 0.02, r = r * (1 - i * 0.18), $fn = 20);
    color([0.44, 0.38, 0.22]) translate([0, 0, 0.12])
        rotate([0, 0, dd_rnd(seed, 90)]) dd_slab(r * 1.9, 2.2, 0.04);
    color(dd_METALC()) rotate([0, 0, dd_rnd(seed, 90)])
    {
        for (i = [1 : 5]) translate([i * r / 5.5, 0, 0]) dd_boxc([0.5, 0.5, 3.4]);
        translate([r * 0.5, 0, 3.6]) dd_boxc([r, 0.3, 0.3]);
    }
    color(dd_METALD()) cylinder(h = 4.2, r = 0.9, $fn = 8);
    color(dd_METALC()) translate([0, 0, 4.2]) cylinder(h = 0.8, r1 = 1.2, r2 = 0.5, $fn = 8);
}

// ================= 地基与地表分区 =================

color([0.47, 0.34, 0.19]) translate([0, 0, -0.18]) cube([740, 480, 0.35], center = true);

// 大块地表：干湖（西北）、戈壁（东北）、板结沙地（南）、河床冲积扇
// 铺砖式区域地表：块与块之间留 2 m 缝，绝不重叠 —— 两块地面件重叠时下层的细节片会从
// 上层表面钻出几毫米，那些面被裹在实体里收不到光，PT 下就是一片黑斑。
for (i = [0 : 3])
    translate([-250 + i * 62, 152, 0]) dd_ground_cracked(L = 58, D = 64, seed = 11 + i, c = [0.56, 0.49, 0.34]);
for (i = [0 : 2])
    translate([-240 + i * 66, 96, 0]) dd_ground_cracked(L = 62, D = 38, seed = 21 + i, c = [0.53, 0.45, 0.30]);
for (i = [0 : 3])
    translate([142 + i * 58, 150, 0]) dd_ground_gravel(L = 54, D = 60, seed = 31 + i, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
for (i = [0 : 4])
    translate([-260 + i * 70, -92, 0]) dd_ground_dirt(L = 66, D = 44, seed = 41 + i,
                                                      c1 = [0.44, 0.31, 0.17], c2 = [0.37, 0.26, 0.14]);
for (i = [0 : 3])
    translate([80 + i * 66, -96, 0]) dd_ground_dirt(L = 62, D = 40, seed = 51 + i,
                                                    c1 = [0.42, 0.30, 0.16], c2 = [0.35, 0.24, 0.13]);
// 中段与东段补砖，让沙色基底不再有整屏纯色
for (i = [0 : 3])
    translate([-238 + i * 66, 40, 0]) dd_ground_cracked(L = 62, D = 34, seed = 61 + i, c = [0.51, 0.43, 0.29]);
for (i = [0 : 2])
    translate([170 + i * 62, 46, 0]) dd_ground_dirt(L = 58, D = 36, seed = 71 + i,
                                                    c1 = [0.45, 0.33, 0.18], c2 = [0.37, 0.27, 0.15]);
for (i = [0 : 4])
    translate([-266 + i * 66, -190, 0]) dd_ground_cracked(L = 62, D = 40, seed = 81 + i, c = [0.52, 0.44, 0.29]);
for (i = [0 : 3])
    translate([98 + i * 62, -190, 0]) dd_ground_gravel(L = 58, D = 40, seed = 91 + i, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);

// 沙丘带（南北两条风积带 + 边界丘）
for (d = [[-320, 196, 30, 10, 1], [-262, 208, 24, 8, 2], [-200, 194, 28, 9, 3],
          [-140, 210, 22, 8, 4], [-70, 196, 26, 9, 5], [0, 208, 30, 10, 6],
          [70, 194, 24, 8, 7], [140, 206, 28, 9, 8], [210, 196, 22, 8, 9],
          [280, 208, 26, 9, 10], [330, 194, 24, 8, 11],
          [-320, -200, 28, 10, 12], [-250, -212, 24, 8, 13], [-180, -198, 30, 10, 14],
          [-110, -210, 22, 8, 15], [-40, -200, 26, 9, 16], [30, -212, 28, 9, 17],
          [100, -198, 24, 8, 18], [170, -210, 30, 10, 19], [240, -200, 22, 8, 20],
          [310, -210, 26, 9, 21]])
    desert_dune(d[0], d[1], d[2], d[3], d[4]);

// ================= 公路与支路 =================

dd_ground_road(L = 740, W = 9, seed = 101);                                    // 沙漠公路 y = 0
translate([-120, 60, 0]) rotate([0, 0, 90]) dd_ground_road(L = 130, W = 7, seed = 102);   // 废车场支路
translate([210, -70, 0]) rotate([0, 0, 90]) dd_ground_road(L = 150, W = 7, seed = 103);   // 矿区支路
translate([20, 70, 0]) rotate([0, 0, 90]) dd_ground_road(L = 150, W = 6.5, seed = 104);   // 灌溉圈支路
for (x = [-120, 20, 210]) translate([x, 0, 0]) dd_ground_cross(W = 9, seed = x + 500);
// 路肩与里程柱
lay_along([[-330, 6.6], [330, 6.6]], step = 54, seed = 105, offset = 20) dd_prop_pole(seed = $seed);
lay_along([[-320, -6.6], [320, -6.6]], step = 62, seed = 106, offset = 14) dd_prop_pole(seed = $seed);

// ================= 路口镇（x -70..110, 出生点周边） =================

translate([46, 26, 0]) dd_bldg_gasstation(seed = 111);
translate([46, 11, 0]) rotate([0, 0, 180]) dd_prop_billboard(seed = 112);
translate([-42, 24, 0]) dd_bldg_diner(seed = 113);
translate([-42, 13, dd_layer(1)]) dd_ground_lot(L = 28, D = 10, seed = 114, bands = 1);
lay_scatter(5, -54, -30, 11, 17, seed = 115)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([-40, 48, 0]) dd_bldg_motel(seed = 116, units = 7);
translate([-40, 35, dd_layer(1)]) dd_ground_lot(L = 34, D = 10, seed = 117, bands = 1);
lay_scatter(6, -56, -24, 31, 39, seed = 118)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_van(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([96, 24, 0]) dd_bldg_shop(seed = 119, L = 16, D = 10);
translate([96, 12, dd_layer(1)]) dd_ground_concrete(L = 26, D = 12, seed = 120);
translate([112, 18, 0]) dd_prop_dumpster();
translate([8, 40, 0]) dd_bldg_watertower(s = 1.1, seed = 121);
translate([8, 26, dd_layer(1)]) dd_ground_gravel(L = 24, D = 14, seed = 122, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
// 镇南侧：住家与工棚
translate([-56, -26, 0]) rotate([0, 0, 180]) dd_bldg_trailer(seed = 123, L = 9.5, D = 4);
translate([-32, -28, 0]) rotate([0, 0, 180]) dd_bldg_trailer(seed = 124, L = 9.5, D = 4);
translate([-6, -30, 0]) rotate([0, 0, 180]) dd_bldg_house(seed = 125, L = 9, D = 7);
translate([26, -30, 0]) rotate([0, 0, 180]) dd_bldg_ruin(seed = 126, L = 9, D = 7);
translate([62, -32, 0]) dd_bldg_shed(seed = 127, L = 12, D = 8);
translate([-30, -18, dd_layer(1)]) dd_ground_gravel(L = 96, D = 16, seed = 128, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
lay_scatter(12, -70, 80, -36, -14, seed = 129)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_crate(); }
lay_along([[-80, -42], [88, -42]], step = 8.5, seed = 130) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([88, -30, 0]) dd_prop_tank(seed = 131, s = 0.8);
translate([-72, 8, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 132);

// ================= 公路车队残骸（出生点向东，x 120..260） =================

translate([130, 3.2, 0]) lay_row(11, 11.5, 0, seed = 141)
    lay_jitter($seed, 1.1, 0.7, 6)
        lay_pick($seed)
        {
            dd_veh_wreck(seed = $seed);
            dd_veh_van(seed = $seed);
            dd_veh_pickup(seed = $seed);
            dd_veh_sedan(seed = $seed);
        }
translate([168, -8.5, 0]) rotate([0, 0, 172]) dd_veh_truck(seed = 142);
translate([206, -3.4, 0]) rotate([0, 0, 8]) dd_veh_flipped(seed = 143);
translate([246, 4.0, 0]) rotate([0, 0, 186]) dd_veh_bus(seed = 144);
lay_scatter(18, 120, 270, -12, 12, seed = 145) dd_prop_cone();
lay_scatter(14, 118, 272, -16, 16, seed = 146)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_gascan(); dd_prop_tires(seed = $seed); }
translate([118, -9, 0]) dd_prop_barricade();
translate([272, 9, 0]) rotate([0, 0, 180]) dd_prop_barricade();

// ================= 军事检查站（公路西段 x -230..-160） =================

translate([-196, 0, dd_layer(1)]) dd_ground_concrete(L = 60, D = 34, seed = 151);
lay_along([[-216, 10], [-176, 10]], step = 3.2, seed = 152) dd_prop_jersey(len = 3.2, seed = $seed);
lay_along([[-216, -10], [-176, -10]], step = 3.2, seed = 153) dd_prop_jersey(len = 3.2, seed = $seed);
translate([-222, 15, 0]) dd_prop_sandbags(len = 8, h = 1.2, seed = 154);
translate([-170, -15, 0]) dd_prop_sandbags(len = 8, h = 1.2, seed = 155);
translate([-206, 18, 0]) rotate([0, 0, 90]) dd_prop_sandbags(len = 10, h = 1.1, seed = 156);
translate([-186, -18, 0]) rotate([0, 0, 90]) dd_prop_sandbags(len = 10, h = 1.1, seed = 157);
translate([-212, 22, 0]) dd_prop_tent(seed = 158, s = 1.6);
translate([-196, 24, 0]) rotate([0, 0, -12]) dd_prop_tent(seed = 159, s = 1.4);
translate([-204, 30, 0]) dd_prop_campfire(seed = 160);
translate([-176, 20, 0]) rotate([0, 0, 100]) dd_veh_truck(seed = 161, trailer = 0);
translate([-228, -18, 0]) rotate([0, 0, 74]) dd_veh_bus(seed = 162);
for (x = [-224, -168]) translate([x, 12, 0]) dd_prop_lamp();
lay_scatter(14, -230, -164, -24, 24, seed = 163)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_cone(); dd_prop_debris(seed = $seed); }
translate([-240, -8, 0]) dd_prop_sign_fallen(seed = 164);
translate([-196, -28, 0]) dd_prop_radiomast(s = 0.8, seed = 165);

// ================= 废车场（x -300..-140, y 40..140） =================

translate([-220, 92, dd_layer(1)]) dd_ground_gravel(L = 150, D = 90, seed = 171, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
lay_scatter(40, -288, -152, 54, 130, seed = 172)
    lay_pick($seed)
    {
        dd_veh_wreck(seed = $seed);
        dd_veh_sedan(seed = $seed);
        dd_veh_flipped(seed = $seed);
        dd_veh_van(seed = $seed);
        dd_veh_pickup(seed = $seed);
    }
lay_scatter(22, -288, -152, 54, 130, seed = 173)
    lay_pick($seed) { dd_prop_tires(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_pallet(seed = $seed); }
translate([-286, 60, 0]) dd_bldg_shed(seed = 174, L = 14, D = 9);
translate([-262, 58, 0]) dd_bldg_warehouse(seed = 175, L = 22, D = 14);
translate([-160, 120, 0]) dd_prop_container(seed = 176, stack = 3);
translate([-172, 106, 0]) rotate([0, 0, 8]) dd_prop_container(seed = 177, stack = 2);
translate([-290, 128, 0]) dd_prop_container(seed = 178, stack = 1);
lay_along([[-296, 46], [-146, 46]], step = 8.5, seed = 179) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[-296, 138], [-146, 138]], step = 8.5, seed = 180) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[-296, 46], [-296, 138]], step = 8.5, seed = 181) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[-146, 46], [-146, 138]], step = 8.5, seed = 182) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([-124, 74, 0]) rotate([0, 0, 12]) dd_veh_truck(seed = 183);

// ================= 拖车营地（x -330..-250, y 30..110） =================

translate([-300, 40, dd_layer(1)]) dd_ground_dirt(L = 70, D = 30, seed = 191,
                                                  c1 = [0.46, 0.34, 0.19], c2 = [0.38, 0.28, 0.15]);
for (i = [0 : 3])
    translate([-328 + i * 17, 34, 0]) rotate([0, 0, lay_randr(191 + i, 3, -6, 6)])
        dd_bldg_trailer(seed = 200 + i, L = 9.5, D = 4);
lay_scatter(8, -334, -262, 24, 48, seed = 192)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_debris(seed = $seed); }
translate([-318, 50, 0]) dd_prop_campfire(seed = 193);
translate([-310, 54, 0]) rotate([0, 0, 20]) dd_prop_tent(seed = 194, s = 1.3);
translate([-296, 26, 0]) rotate([0, 0, 200]) dd_veh_pickup(seed = 195);
translate([-330, 20, 0]) dd_prop_tank(seed = 196, s = 0.7);

// ================= 矿区 / 采石台地（x 160..340, y -180..-30） =================

translate([250, -110, dd_layer(1)]) dd_ground_gravel(L = 170, D = 130, seed = 201, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
translate([250, -110, dd_layer(2)]) dd_ground_dirt(L = 120, D = 80, seed = 202,
                                                   c1 = [0.40, 0.33, 0.24], c2 = [0.31, 0.26, 0.19]);
// 台阶式采掘面（三级平台）
for (i = [0 : 2])
    color([0.40 - i * 0.03, 0.30 - i * 0.02, 0.20 - i * 0.015])
        translate([300, -150 + i * 14, 0]) dd_slab(120 - i * 26, 14, 2.2 + i * 2.0);
desert_rock_cluster(26, 200, 330, -178, -128, 203, 1.0, 2.8);
translate([196, -52, 0]) dd_bldg_warehouse(seed = 204, L = 28, D = 16);
translate([196, -34, dd_layer(2)]) dd_ground_concrete(L = 40, D = 16, seed = 205);
translate([246, -46, 0]) dd_prop_tank(seed = 206, s = 1.2);
translate([246, -70, 0]) dd_prop_tank(seed = 207, s = 1.0);
translate([286, -50, 0]) dd_bldg_silo(seed = 208, s = 1.1);
translate([298, -54, 0]) dd_bldg_silo(seed = 209, s = 0.9);
translate([320, -74, 0]) dd_prop_container(seed = 210, stack = 2);
translate([306, -84, 0]) rotate([0, 0, 9]) dd_prop_container(seed = 211, stack = 1);
translate([222, -88, 0]) rotate([0, 0, 24]) dd_veh_truck(seed = 212, trailer = 0);
translate([268, -96, 0]) rotate([0, 0, 190]) dd_veh_truck(seed = 213);
lay_scatter(16, 180, 330, -120, -40, seed = 214)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_pallet(seed = $seed); dd_prop_debris(seed = $seed); }
lay_along([[176, -128], [176, -30]], step = 8.5, seed = 215) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[176, -30], [330, -30]], step = 8.5, seed = 216) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([170, -20, 0]) dd_prop_radiomast(s = 1.0, seed = 217);

// ================= 灌溉圈与风电残骸（北部 x -60..200, y 90..210） =================

translate([20, 150, dd_layer(1)]) desert_pivot_field(r = 46, seed = 221);
translate([20, 96, dd_layer(1)]) dd_ground_gravel(L = 40, D = 20, seed = 222, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
translate([8, 92, 0]) rotate([0, 0, 14]) dd_bldg_shed(seed = 223, L = 12, D = 8);
translate([34, 90, 0]) dd_prop_tank(seed = 224, s = 0.9);
translate([-16, 96, 0]) rotate([0, 0, -22]) dd_veh_harvester(seed = 225);
lay_scatter(8, -10, 50, 84, 104, seed = 226)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_pallet(seed = $seed); dd_prop_debris(seed = $seed); }
translate([-90, 150, 0]) dd_prop_windturbine_fallen(seed = 227, s = 1.3);
translate([-40, 196, 0]) rotate([0, 0, 140]) dd_prop_windturbine_fallen(seed = 228, s = 1.1);
translate([110, 186, 0]) rotate([0, 0, -60]) dd_prop_windturbine_fallen(seed = 229, s = 1.2);

// ================= 东部荒漠农庄（x 250..340, y 10..90，补东侧空档） =================
// 一处被沙埋掉的自耕农庄：干枯果园成行、畜栏栅栏、风车、塌掉的主屋。

translate([292, 48, 0]) dd_ground_dirt(L = 76, D = 62, seed = 301,
                                       c1 = [0.45, 0.33, 0.18], c2 = [0.37, 0.27, 0.15]);
translate([272, 66, dd_layer(1)]) dd_bldg_ruin(seed = 302, L = 11, D = 8);
translate([300, 70, dd_layer(1)]) dd_bldg_shed(seed = 303, L = 10, D = 7);
translate([322, 62, 0]) dd_bldg_watertower(s = 0.75, seed = 304);
translate([288, 30, dd_layer(1)]) dd_ground_gravel(L = 30, D = 16, seed = 305, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
translate([276, 28, 0]) rotate([0, 0, 22]) dd_veh_pickup(seed = 306);
translate([304, 26, 0]) rotate([0, 0, 190]) dd_veh_wreck(seed = 307);
lay_grid(5, 3, 11, 10, seed = 308)
    translate([296, 52, 0]) oh_nature_deadtree(s = lay_randr($seed, 5, 0.9, 1.4), seed = $seed);
lay_along([[256, 20], [332, 20]], step = 8.5, seed = 309) dd_prop_fence(len = 8.5);
lay_along([[256, 20], [256, 80]], step = 8.5, seed = 310) dd_prop_fence(len = 8.5);
lay_along([[256, 80], [332, 80]], step = 8.5, seed = 311) dd_prop_fence(len = 8.5);
lay_scatter(10, 258, 330, 22, 78, seed = 312)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_haybale(seed = $seed); }
translate([334, 44, 0]) dd_prop_windturbine_fallen(seed = 313, s = 0.8);

// ================= 岩丘群（东北远景地标） =================

for (m = [[168, 96, 5, 1], [206, 128, 7, 2], [248, 100, 5, 3], [286, 140, 8, 4],
          [318, 104, 6, 5], [232, 176, 7, 6], [300, 190, 5, 7], [150, 150, 4, 8]])
    desert_mesa(m[0], m[1], m[2], m[3]);
desert_rock_cluster(30, 140, 336, 84, 200, 231, 0.9, 2.4);
lay_scatter(20, 140, 336, 84, 204, seed = 232)
    lay_pick($seed) { oh_nature_cactus(s = 1.2, seed = $seed); dd_nature_bush(s = 1.0, seed = $seed); }

// ================= 干涸河床（南部，自西向东） =================

for (i = [0 : 7])
    translate([-300 + i * 88, -150 + (i % 2) * 12, 0]) rotate([0, 0, (i % 2 == 0 ? 4 : -4)])
        desert_wash(L = 84, W = 30, seed = 241 + i);
translate([-40, -144, 0]) rotate([0, 0, 150]) dd_veh_truck(seed = 251);
translate([86, -156, 0]) rotate([0, 0, 30]) dd_veh_flipped(seed = 252);
translate([-186, -140, 0]) rotate([0, 0, 200]) dd_veh_wreck(seed = 253);
lay_scatter(16, -320, 320, -168, -132, seed = 254)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_tires(seed = $seed); }
// 河床边的废弃汲水点
translate([140, -132, 0]) dd_bldg_shed(seed = 255, L = 10, D = 7);
translate([158, -136, 0]) dd_prop_tank(seed = 256, s = 0.8);
translate([124, -126, dd_layer(1)]) dd_ground_gravel(L = 30, D = 16, seed = 257, c1 = [0.46, 0.40, 0.29], c2 = [0.38, 0.32, 0.22]);
translate([-252, -128, 0]) dd_bldg_ruin(seed = 258, L = 10, D = 8);

// ================= 南部仙人掌荒滩与岩带 =================

desert_rock_cluster(34, -330, -170, -110, -40, 261, 0.8, 2.4);
desert_rock_cluster(28, -90, 60, -120, -50, 262, 0.9, 2.2);
desert_rock_cluster(24, -320, -60, -212, -180, 263, 0.8, 2.0);
desert_rock_cluster(22, 60, 330, -212, -184, 264, 0.9, 2.2);
lay_scatter(44, -330, 330, -212, -40, seed = 265)
    lay_pick($seed)
    {
        oh_nature_cactus(s = lay_randr($seed, 5, 0.9, 1.6), seed = $seed);
        oh_nature_deadtree(s = lay_randr($seed, 6, 0.9, 1.3), seed = $seed);
        dd_nature_bush(s = lay_randr($seed, 7, 0.9, 1.2), seed = $seed);
    }
lay_scatter(38, -330, 330, 40, 210, seed = 266)
    lay_pick($seed)
    {
        oh_nature_cactus(s = lay_randr($seed, 5, 0.9, 1.6), seed = $seed);
        oh_nature_deadtree(s = lay_randr($seed, 6, 0.9, 1.3), seed = $seed);
        dd_nature_bush(s = lay_randr($seed, 7, 0.9, 1.2), seed = $seed);
    }

// ================= 边界岩墙（视觉围墙：沙漠没有树，用岩带和沙丘封边） =================

desert_rock_cluster(34, -340, 340, 196, 216, 271, 1.4, 3.2);
desert_rock_cluster(34, -340, 340, -216, -196, 272, 1.4, 3.2);
desert_rock_cluster(26, -340, -312, -190, 190, 273, 1.4, 3.0);
desert_rock_cluster(26, 312, 340, -190, 190, 274, 1.4, 3.0);
for (m = [[-330, 60, 6, 11], [-326, -60, 7, 12], [330, 40, 6, 13], [326, -160, 7, 14],
          [-120, 212, 6, 15], [180, -214, 6, 16], [60, 214, 5, 17], [-240, -210, 5, 18]])
    desert_mesa(m[0], m[1], m[2], m[3]);

// ================= 收尾：风蚀纹理与路侧细节 =================

lay_scatter(26, -330, 330, -18, 18, seed = 283) dd_prop_debris(seed = $seed);
// 中纬度稀疏带：仙人掌丛、孤石与小沙丘，保证跑图时每屏都有前景物
lay_scatter(26, -330, -60, 46, 92, seed = 287)
    lay_pick($seed) { oh_nature_cactus(s = lay_randr($seed, 5, 1.0, 1.6), seed = $seed); desert_rock(s = lay_randr($seed, 6, 1.0, 2.2), seed = $seed); }
lay_scatter(22, 60, 330, -46, 10, seed = 288)
    lay_pick($seed) { oh_nature_cactus(s = lay_randr($seed, 5, 1.0, 1.5), seed = $seed); desert_rock(s = lay_randr($seed, 6, 1.0, 2.4), seed = $seed); }
lay_scatter(24, -330, -60, -60, 20, seed = 289)
    lay_pick($seed) { oh_nature_deadtree(s = 1.2, seed = $seed); desert_rock(s = lay_randr($seed, 6, 0.9, 2.2), seed = $seed); dd_nature_bush(s = 1.1, seed = $seed); }
lay_scatter(20, -120, 340, 156, 200, seed = 290)
    lay_pick($seed) { desert_rock(s = lay_randr($seed, 6, 1.1, 2.6), seed = $seed); oh_nature_cactus(s = 1.3, seed = $seed); }
for (d = [[-160, 66, 22, 8, 31], [-60, 120, 26, 9, 32], [120, 60, 20, 7, 33],
          [240, 120, 24, 8, 34], [-280, -40, 22, 8, 35], [40, -60, 26, 9, 36],
          [180, -160, 22, 8, 37], [-120, -100, 24, 8, 38]])
    desert_dune(d[0], d[1], d[2], d[3], d[4]);
lay_scatter(60, -330, 330, 30, 210, seed = 284) dd_nature_grass(seed = $seed);
lay_scatter(60, -330, 330, -210, -30, seed = 285) dd_nature_grass(seed = $seed);
lay_scatter(14, -330, 330, -210, 210, seed = 286)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_crate(s = 0.85); dd_prop_tires(seed = $seed); }
translate([-322, 14, 0]) dd_prop_billboard(seed = 291);
translate([322, -14, 0]) rotate([0, 0, 180]) dd_prop_billboard(seed = 292);
translate([-300, -8, 0]) dd_prop_sign_fallen(seed = 293);
translate([300, 10, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 294);
