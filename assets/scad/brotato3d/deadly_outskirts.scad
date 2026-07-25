// Brotato3D 固定场景：公路郊外（deadly_outskirts）
// =====================================================================================
// 设计前提（同 deadly_town.scad 抬头，改图前必读）：
//   * 竞技场 halfExtent = [320, 200]；|x|>320 / |y|>200 永远看不见，别往外堆东西。
//   * 相机 = 从 -y 往 +y 的 70° 俯视，单屏约 66 x 41 m；front = -y 的件正对镜头。
//   * 建筑不挡路，街区的价值在"可读的规划 + 方位地标"。
//
// 规划：一条东西向的分离式公路（双向各一幅 + 中央绿化带 + 波形护栏）贯穿全图，
//       出生点 (0,0) 正好落在中央服务区路口；南北由两条县道接出去。
//       农业区用大田块网格（dd_nature_field_big）铺满，形成俯视下最强的秩序感。
//
//        x -320   -220    -110     0      110     220     320
//   y 200 ┌──────────────────────────────────────────────┐ 松林带
//     120 │      农 场 / 谷 仓 / 筒 仓  │   大 田 块 网 格      │
//      40 │ 公路小镇 │ 拖车营地 │ 服务区（加油站/餐车/旅馆）│ 卡车服务站 │
//       0 ├════════════════ 州 际 公 路 ══════════════════┤
//     -40 │  水塔/教堂 │ 废车堆场 │ 检查站残骸 │  仓储 / 油罐区    │
//    -120 │      伐 木 场 / 林 地      │  露 营 地 / 池 塘  │ 采石场 │
//    -200 └──────────────────────────────────────────────┘ 松林带
//
// 叙事焦点：路口翻覆油罐车、服务区、公路封锁线、伐木场、露营地、东部卡车服务站。
// =====================================================================================

use <../lib/kit_deadly.scad>
use <../lib/kit_layout.scad>

$fn = 12;

// ================= 构件 =================

// 分离式公路的一段（沿 x）：南北两幅路面 + 中央草带护栏 + 外侧护栏
module do_highway(L = 200, seed = 0)
{
    for (sy = [-1, 1]) translate([0, sy * 7.5, 0]) dd_ground_road(L = L, W = 9, seed = seed + sy * 17);
    color(dd_GRASSD()) dd_slab(L, 6, 0.06);
    translate([0, 1.6, 0]) dd_prop_guardrail(len = L, seed = seed + 3);
    translate([0, -1.6, 0]) rotate([0, 0, 180]) dd_prop_guardrail(len = L, seed = seed + 5);
    translate([0, 13.4, 0]) rotate([0, 0, 180]) dd_prop_guardrail(len = L, seed = seed + 7);
    translate([0, -13.4, 0]) dd_prop_guardrail(len = L, seed = seed + 9);
}

// 农田网格：cols x rows 块大田，块间留田埂土路
module do_farmland(cols = 3, rows = 2, seed = 0, cw = 46, ch = 34)
{
    lay_grid(cols, rows, cw, ch, seed = seed)
        dd_nature_field_big(L = cw - 6, D = ch - 6, seed = $seed);
    for (r = [0 : rows - 1])
        translate([0, (r - (rows - 1) / 2) * ch + ch / 2, 0]) dd_ground_track(L = cols * cw, W = 3.6, seed = seed + r);
    for (c = [0 : cols - 1])
        translate([(c - (cols - 1) / 2) * cw + cw / 2, 0, 0])
            rotate([0, 0, 90]) dd_ground_track(L = rows * ch, W = 3.2, seed = seed + c * 7 + 3);
}

// 乡村住宅（临路排屋，原点 = 路缘中点，朝 -y）
module do_rural_lot(seed = 0, wide = 16)
{
    v = lay_randi(seed, 1, 10);
    translate([0, 10, 0]) dd_ground_grass(L = wide, D = 16, seed = seed);
    color([0.38, 0.31, 0.22]) translate([wide * 0.3, 6, 0]) dd_slab(3.4, 12, 0.10);
    if (v < 3) translate([0, 13, 0]) dd_bldg_house(seed = seed, L = 9, D = 7);
    else if (v < 6) translate([0, 13.5, 0]) dd_bldg_house_porch(seed = seed, L = 10, D = 7);
    else if (v < 8) translate([0, 13, 0]) dd_bldg_trailer(seed = seed, L = 9.5, D = 4);
    else translate([0, 13, 0]) dd_bldg_ruin(seed = seed, L = 9, D = 7);
    translate([-wide * 0.28, 20, 0]) dd_bldg_shed(seed = seed + 5, L = 4, D = 3);
    translate([wide * 0.3, 20, 0]) dd_nature_tree(s = lay_randr(seed, 4, 0.9, 1.3), seed = seed);
    translate([0, 23, 0]) dd_prop_fence(len = wide);
    translate([wide * 0.42, 1.6, 0]) dd_prop_mailbox();
    if (v % 3 == 0) translate([wide * 0.3, 4, 0]) rotate([0, 0, 90]) dd_veh_pickup(seed = seed);
}

// ================= 地基 =================

color([0.35, 0.40, 0.24]) translate([0, 0, -0.18]) cube([700, 440, 0.35], center = true);

// ================= 公路与县道 =================

do_highway(L = 700, seed = 101);
// 县道：西镇进出 + 东部工业区 + 北农场路 + 南林场路
translate([-220, 40, 0]) rotate([0, 0, 90]) dd_ground_road(L = 150, W = 7.5, seed = 111);
translate([-220, -60, 0]) rotate([0, 0, 90]) dd_ground_road(L = 130, W = 7.5, seed = 112);
translate([0, 90, 0]) rotate([0, 0, 90]) dd_ground_road(L = 150, W = 7.5, seed = 113);
translate([0, -80, 0]) rotate([0, 0, 90]) dd_ground_road(L = 130, W = 7.5, seed = 114);
translate([220, 60, 0]) rotate([0, 0, 90]) dd_ground_road(L = 90, W = 7.5, seed = 115);
translate([220, -70, 0]) rotate([0, 0, 90]) dd_ground_road(L = 110, W = 7.5, seed = 116);
translate([-300, 96, 0]) dd_ground_road(L = 180, W = 7, seed = 117);           // 镇内横街
translate([250, -120, 0]) dd_ground_road(L = 140, W = 7, seed = 118);          // 采石场支路
// 匝道口：公路两侧的加宽铺装
for (x = [-220, 0, 220])
    for (sy = [-1, 1])
        translate([x, sy * 7.5, 0]) dd_ground_cross(W = 9.5, seed = x + sy * 31);
translate([-220, 0, 0]) dd_ground_concrete(L = 12, D = 16, seed = 119);
translate([0, 0, 0]) dd_ground_concrete(L = 12, D = 16, seed = 120);
translate([220, 0, 0]) dd_ground_concrete(L = 12, D = 16, seed = 121);

// ================= 中央服务区（x -110..110, 出生点周边） =================

// 服务区贴着路缘布置：公路生意都靠"离匝道多近"活着，拉远了就成了空地上的孤楼。
translate([38, 30, 0]) dd_bldg_gasstation(seed = 131);
translate([38, 15, 0]) rotate([0, 0, 180]) dd_prop_billboard(seed = 132);
translate([-38, 28, 0]) dd_bldg_diner(seed = 133);
translate([-38, 17, 0]) dd_ground_lot(L = 30, D = 10, seed = 134, bands = 1);
lay_scatter(6, -50, -26, 15, 21, seed = 135)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); dd_veh_van(seed = $seed); }
translate([88, 36, 0]) dd_bldg_motel(seed = 136, units = 8);
translate([88, 22, 0]) dd_ground_lot(L = 42, D = 12, seed = 137, bands = 1);
lay_scatter(7, 68, 108, 18, 26, seed = 138)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); dd_veh_van(seed = $seed); }
translate([-88, 26, 0]) dd_bldg_shop(seed = 139, L = 16, D = 10);
translate([-88, 16, 0]) dd_ground_concrete(L = 26, D = 10, seed = 140);
translate([-104, 22, 0]) dd_prop_dumpster();

// 公路南侧休息区：厕所小屋 + 野餐长椅 + 铺装场 + 弃车
translate([-84, -28, 0]) dd_ground_concrete(L = 34, D = 18, seed = 153);
translate([-84, -34, 0]) rotate([0, 0, 180]) dd_bldg_shed(seed = 154, L = 8, D = 6);
for (p = [[-96, -24], [-84, -22], [-72, -24]]) translate([p[0], p[1], 0]) dd_prop_bench();
translate([-96, -32, 0]) dd_nature_tree(s = 1.5, seed = 155);
translate([-70, -33, 0]) dd_nature_tree(s = 1.3, seed = 156);
translate([-60, -26, 0]) rotate([0, 0, 200]) dd_veh_van(seed = 157);
translate([-100, -20, 0]) dd_prop_trash(seed = 158);
translate([-76, -20, 0]) dd_prop_trash(seed = 159);
translate([56, -28, 0]) dd_ground_gravel(L = 40, D = 20, seed = 160);          // 路政料场
translate([44, -30, 0]) dd_prop_container(seed = 173, stack = 1);
lay_scatter(9, 40, 74, -36, -22, seed = 174)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_cone(); dd_prop_pallet(seed = $seed); dd_prop_tires(seed = $seed); }
translate([70, -34, 0]) rotate([0, 0, 160]) dd_veh_truck(seed = 175, trailer = 0);

// 公路红线栅栏：把路侧的绿色切成"路权带 + 农田"，一眼就能看出这是被规划过的乡野
for (sx = [-1, 1])
    for (i = [0 : 5])
        lay_along([[sx * (40 + i * 46), 23.5], [sx * (40 + i * 46) + sx * 42, 23.5]], step = 8.5, seed = 176 + i * sx)
            dd_prop_fence(len = 8.5);
for (sx = [-1, 1])
    for (i = [0 : 5])
        lay_along([[sx * (40 + i * 46), -23.5], [sx * (40 + i * 46) + sx * 42, -23.5]], step = 8.5, seed = 186 + i * sx)
            dd_prop_fence(len = 8.5);

// 路口封锁线残骸：翻覆油罐车 + 隔离墩 + 沙袋 + 军车（出生点第一眼）
translate([-14, 9.5, 0]) rotate([0, 0, 168]) dd_veh_truck(seed = 141);
translate([16, -8.6, 0]) rotate([0, 0, 12]) dd_veh_flipped(seed = 142);
lay_along([[-30, -2.6], [-6, -2.6]], step = 3.2, seed = 143) dd_prop_jersey(len = 3.2, seed = $seed);
lay_along([[8, 2.6], [32, 2.6]], step = 3.2, seed = 144) dd_prop_jersey(len = 3.2, seed = $seed);
translate([26, -4.5, 0]) dd_prop_sandbags(len = 6, h = 1.0, seed = 145);
translate([-26, 4.5, 0]) dd_prop_sandbags(len = 6, h = 1.0, seed = 146);
lay_scatter(16, -40, 40, -12, 12, seed = 147) dd_prop_cone();
lay_scatter(10, -50, 50, -14, 14, seed = 148)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_gascan(); }
translate([-34, -12, 0]) dd_prop_barricade();
translate([34, 12, 0]) rotate([0, 0, 180]) dd_prop_barricade();
translate([-8, -22, 0]) dd_prop_tent(seed = 149, s = 1.5);
translate([4, -24, 0]) rotate([0, 0, -16]) dd_prop_tent(seed = 150, s = 1.3);
translate([-2, -30, 0]) dd_prop_campfire(seed = 151);
translate([0, -18, 0]) dd_ground_dirt(L = 34, D = 20, seed = 152);

// 公路堵车：西行道排成长龙，东行道零散
translate([-190, 4.4, 0]) lay_row(11, 11.5, 0, seed = 161)
    lay_jitter($seed, 1.1, 0.7, 5)
        lay_pick($seed)
        {
            dd_veh_sedan(seed = $seed);
            dd_veh_van(seed = $seed);
            dd_veh_pickup(seed = $seed);
            dd_veh_wreck(seed = $seed);
        }
translate([70, 10.6, 0]) lay_row(8, 12, 0, seed = 162)
    lay_jitter($seed, 1.2, 0.6, 5)
        lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); dd_veh_van(seed = $seed); }
translate([-120, -10.6, 0]) lay_row(6, 12.5, 0, seed = 163)
    lay_jitter($seed, 1.0, 0.6, 5)
        lay_pick($seed) { dd_veh_pickup(seed = $seed); dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([190, -4.4, 0]) rotate([0, 0, 180]) lay_row(7, 12, 0, seed = 164)
    lay_jitter($seed, 1.0, 0.6, 5)
        lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_van(seed = $seed); dd_veh_wreck(seed = $seed); }
translate([-150, 11, 0]) rotate([0, 0, 8]) dd_veh_bus(seed = 165);
translate([160, -11, 0]) rotate([0, 0, 186]) dd_veh_truck(seed = 166);
lay_along([[-310, 17], [310, 17]], step = 46, seed = 167, offset = 20) dd_prop_pole(seed = $seed);
lay_along([[-300, -17], [300, -17]], step = 52, seed = 168, offset = 12) dd_prop_pole(seed = $seed);
translate([-300, 24, 0]) dd_prop_billboard(seed = 169);
translate([300, -24, 0]) rotate([0, 0, 180]) dd_prop_billboard(seed = 170);
translate([-268, -22, 0]) dd_prop_sign_fallen(seed = 171);
translate([272, 22, 0]) rotate([0, 0, 180]) dd_prop_sign(seed = 172);

// ================= 西部公路小镇（x -320..-160, y 20..150） =================

translate([-296, 100.2, 0]) do_rural_lot(seed = 181, wide = 16);
translate([-278, 100.2, 0]) do_rural_lot(seed = 182, wide = 16);
translate([-260, 100.2, 0]) do_rural_lot(seed = 183, wide = 16);
translate([-242, 100.2, 0]) do_rural_lot(seed = 184, wide = 16);
translate([-224, 100.2, 0]) do_rural_lot(seed = 185, wide = 16);
translate([-296, 91.8, 0]) rotate([0, 0, 180]) do_rural_lot(seed = 186, wide = 16);
translate([-314, 91.8, 0]) rotate([0, 0, 180]) do_rural_lot(seed = 187, wide = 16);
translate([-260, 91.8, 0]) rotate([0, 0, 180]) do_rural_lot(seed = 188, wide = 16);
translate([-242, 91.8, 0]) rotate([0, 0, 180]) do_rural_lot(seed = 189, wide = 16);
// 镇中心：教堂 + 杂货店 + 谷物仓
translate([-278, 74, 0]) dd_bldg_church(seed = 191, L = 9, D = 16);
translate([-306, 70, 0]) dd_bldg_shop(seed = 192, L = 15, D = 10);
translate([-306, 58, 0]) dd_ground_lot(L = 26, D = 12, seed = 193, bands = 1);
lay_scatter(5, -316, -294, 55, 62, seed = 194)
    lay_pick($seed) { dd_veh_sedan(seed = $seed); dd_veh_pickup(seed = $seed); }
translate([-236, 72, 0]) dd_bldg_silo(seed = 195, s = 0.9);
translate([-228, 74, 0]) dd_bldg_silo(seed = 196, s = 0.8);
translate([-242, 60, 0]) dd_ground_gravel(L = 30, D = 20, seed = 197);
translate([-252, 56, 0]) rotate([0, 0, 22]) dd_veh_truck(seed = 198, trailer = 0);
translate([-292, 48, 0]) dd_bldg_watertower(s = 1.1, seed = 199);
translate([-268, 118, 0]) dd_bldg_warehouse(seed = 200, L = 22, D = 14);
translate([-268, 132, 0]) dd_ground_gravel(L = 30, D = 16, seed = 201);
lay_scatter(9, -300, -230, 112, 138, seed = 202)
    lay_pick($seed) { dd_prop_pallet(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_crate(); dd_prop_tires(seed = $seed); }
// 镇口加油站与路边小品
translate([-220, 26, 0]) dd_bldg_gasstation(seed = 203);
lay_along([[-318, 86], [-200, 86]], step = 34, seed = 204, offset = 10) dd_prop_lamp();
lay_scatter(12, -318, -200, 86, 94, seed = 205) dd_prop_debris(seed = $seed);

// ================= 拖车营地（x -180..-100, y 40..110） =================

translate([-140, 74, 0]) dd_ground_gravel(L = 76, D = 60, seed = 211);
for (i = [0 : 4])
    translate([-172 + i * 16, 92, 0]) rotate([0, 0, lay_randr(211 + i, 3, -5, 5)])
        dd_bldg_trailer(seed = 220 + i, L = 9.5, D = 4);
for (i = [0 : 4])
    translate([-172 + i * 16, 62, 0]) rotate([0, 0, 180 + lay_randr(216 + i, 3, -5, 5)])
        dd_bldg_trailer(seed = 230 + i, L = 9.5, D = 4);
translate([-140, 77, dd_layer(1)]) dd_ground_track(L = 72, W = 4.5, seed = 212);
lay_scatter(10, -176, -104, 52, 100, seed = 213)
    lay_pick($seed) { dd_veh_pickup(seed = $seed); dd_veh_sedan(seed = $seed); dd_veh_wreck(seed = $seed); }
lay_scatter(18, -176, -104, 50, 102, seed = 214)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_trash(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_debris(seed = $seed); }
translate([-118, 96, 0]) dd_prop_campfire(seed = 215);
translate([-124, 100, 0]) rotate([0, 0, 24]) dd_prop_tent(seed = 216, s = 1.3);
translate([-112, 100, 0]) dd_prop_tent(seed = 217, s = 1.2);
lay_along([[-180, 46], [-100, 46]], step = 8.5, seed = 218) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([-140, 108, 0]) dd_prop_radiomast(s = 0.85, seed = 219);

// ================= 北部农业区（y 120..200） =================

translate([-60, 160, 0]) do_farmland(cols = 3, rows = 2, seed = 241, cw = 52, ch = 36);
translate([160, 160, 0]) do_farmland(cols = 3, rows = 2, seed = 242, cw = 50, ch = 36);
// 农场主体：谷仓 + 筒仓 + 场院
translate([64, 126, 0]) rotate([0, 0, 180]) dd_bldg_barn(seed = 251, L = 13, D = 15);
translate([84, 128, 0]) dd_bldg_silo(seed = 252, s = 1.0);
translate([92, 130, 0]) dd_bldg_silo(seed = 253, s = 0.85);
translate([70, 110, 0]) dd_ground_dirt(L = 46, D = 20, seed = 254);
translate([44, 112, 0]) rotate([0, 0, -24]) dd_veh_harvester(seed = 255);
lay_scatter(8, 40, 100, 104, 118, seed = 256, rot = false) dd_prop_haybale(seed = $seed);
translate([104, 112, 0]) dd_bldg_shed(seed = 257, L = 12, D = 8);
translate([28, 124, 0]) dd_bldg_house_porch(seed = 258, L = 10, D = 7.5);
lay_along([[-10, 104], [130, 104]], step = 8.5, seed = 259) dd_prop_fence(len = 8.5);
lay_along([[-150, 128], [-150, 196]], step = 8.5, seed = 260) dd_prop_fence(len = 8.5);
translate([-190, 150, 0]) dd_prop_windturbine_fallen(seed = 261, s = 1.1);
translate([-250, 168, 0]) dd_nature_pumpkin_patch(L = 22, D = 16, seed = 262);
translate([-286, 150, 0]) dd_nature_field_rows(L = 20, D = 14, seed = 263);
translate([250, 118, 0]) dd_bldg_warehouse(seed = 264, L = 24, D = 15);
translate([250, 102, 0]) dd_ground_gravel(L = 34, D = 16, seed = 265);
translate([282, 106, 0]) dd_prop_container(seed = 266, stack = 2);
lay_scatter(9, 220, 296, 96, 112, seed = 267)
    lay_pick($seed) { dd_prop_pallet(seed = $seed); dd_prop_crate(); dd_prop_haybale(seed = $seed); }

// ================= 东部卡车服务站与油罐区（x 160..320, y -110..80） =================

translate([250, 42, 0]) dd_ground_concrete(L = 90, D = 40, seed = 271);
for (i = [0 : 3])
    translate([214 + i * 24, 52, 0]) rotate([0, 0, 178 + lay_randr(271 + i, 3, -4, 4)]) dd_veh_truck(seed = 280 + i);
for (i = [0 : 2])
    translate([222 + i * 26, 30, 0]) rotate([0, 0, lay_randr(275 + i, 3, -4, 4)]) dd_veh_truck(seed = 290 + i, trailer = 0);
translate([292, 36, 0]) dd_bldg_shop(seed = 272, L = 16, D = 11);
translate([196, 34, 0]) dd_prop_tank(seed = 273, s = 1.2);
translate([196, 18, 0]) dd_prop_tank(seed = 274, s = 1.0);
translate([250, 66, 0]) dd_prop_billboard(seed = 275);
lay_scatter(12, 200, 300, 20, 62, seed = 276)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_pallet(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_debris(seed = $seed); }
translate([252, -46, 0]) dd_bldg_warehouse(seed = 281, L = 32, D = 18);
translate([252, -26, 0]) dd_ground_concrete(L = 44, D = 18, seed = 282);
translate([206, -60, 0]) dd_ground_gravel(L = 44, D = 40, seed = 283);
translate([200, -52, 0]) dd_prop_container(seed = 284, stack = 2);
translate([212, -66, 0]) rotate([0, 0, 7]) dd_prop_container(seed = 285, stack = 1);
translate([196, -74, 0]) rotate([0, 0, -6]) dd_prop_container(seed = 286, stack = 3);
translate([296, -60, 0]) dd_prop_tank(seed = 287, s = 1.3);
translate([296, -84, 0]) dd_prop_tank(seed = 288, s = 1.1);
translate([272, -74, 0]) dd_ground_gravel(L = 40, D = 30, seed = 289);
lay_along([[180, -96], [312, -96]], step = 8.5, seed = 290) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[180, -96], [180, -14]], step = 8.5, seed = 291) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([310, 4, 0]) dd_prop_radiomast(s = 1.0, seed = 292);

// 采石场（东南角）
translate([268, -150, 0]) dd_ground_gravel(L = 90, D = 60, seed = 301);
translate([268, -150, dd_layer(1)]) dd_ground_dirt(L = 60, D = 36, seed = 302,
                                         c1 = [0.42, 0.38, 0.31], c2 = [0.32, 0.29, 0.24]);
lay_scatter(14, 232, 306, -176, -126, seed = 303)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_tires(seed = $seed); dd_prop_barrel(seed = $seed); }
translate([236, -128, 0]) rotate([0, 0, 24]) dd_veh_truck(seed = 304, trailer = 0);
translate([298, -132, 0]) dd_bldg_shed(seed = 305, L = 10, D = 7);
translate([250, -178, 0]) dd_prop_container(seed = 306, stack = 1);

// ================= 西南废车堆场与教堂（x -320..-140, y -180..-30） =================

translate([-250, -70, 0]) dd_ground_gravel(L = 90, D = 70, seed = 311);
lay_scatter(30, -290, -212, -100, -40, seed = 312)
    lay_pick($seed)
    {
        dd_veh_wreck(seed = $seed);
        dd_veh_sedan(seed = $seed);
        dd_veh_flipped(seed = $seed);
        dd_veh_van(seed = $seed);
    }
lay_scatter(16, -290, -212, -100, -40, seed = 313)
    lay_pick($seed) { dd_prop_tires(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_debris(seed = $seed); }
translate([-216, -44, 0]) dd_bldg_shed(seed = 314, L = 12, D = 8);
translate([-292, -96, 0]) dd_prop_container(seed = 315, stack = 2);
lay_along([[-296, -108], [-206, -108]], step = 8.5, seed = 316) dd_prop_chainlink(len = 8.5, seed = $seed);
lay_along([[-296, -108], [-296, -36]], step = 8.5, seed = 317) dd_prop_chainlink(len = 8.5, seed = $seed);
translate([-160, -52, 0]) dd_bldg_church(seed = 318, L = 9, D = 15);
translate([-160, -70, dd_layer(1)]) dd_ground_track(L = 30, W = 4, seed = 319);
lay_scatter(6, -180, -140, -80, -60, seed = 320)
    lay_pick($seed) { dd_nature_tree(s = 1.2, seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); }

// ================= 南部伐木场与露营地（y -180..-40） =================

// 伐木场：碎石场院 + 原木堆 + 树桩带 + 卡车
translate([-70, -120, 0]) dd_ground_gravel(L = 70, D = 44, seed = 331);
for (i = [0 : 4])
    translate([-96 + i * 13, -112, 0]) rotate([0, 0, 90]) dd_nature_log(seed = 331 + i);
for (i = [0 : 4])
    translate([-96 + i * 13, -108, 0]) rotate([0, 0, 90]) dd_nature_log(seed = 341 + i);
translate([-46, -128, 0]) rotate([0, 0, 200]) dd_veh_truck(seed = 332);
translate([-92, -134, 0]) dd_bldg_shed(seed = 333, L = 12, D = 8);
lay_scatter(20, -108, -34, -142, -100, seed = 334)
    lay_pick($seed) { dd_nature_stump(s = lay_randr($seed, 5, 1.0, 1.5), seed = $seed); dd_nature_log(seed = $seed); }
lay_scatter(10, -108, -34, -142, -100, seed = 335)
    lay_pick($seed) { dd_prop_barrel(seed = $seed); dd_prop_debris(seed = $seed); dd_prop_pallet(seed = $seed); }
translate([-30, -104, 0]) dd_prop_windturbine_fallen(seed = 336, s = 0.9);

// 露营地与池塘
translate([70, -120, dd_layer(1)]) dd_ground_dirt(L = 46, D = 30, seed = 341,
                                        c1 = [0.36, 0.31, 0.21], c2 = [0.28, 0.24, 0.17]);
for (p = [[58, -128, 3.0], [72, -124, 3.4], [84, -130, 2.6], [66, -136, 2.2]])
    translate([p[0], p[1], dd_layer(1) + 0.01]) dd_ground_puddle(s = p[2], seed = 341 + p[0]);
translate([60, -104, 0]) dd_prop_tent(seed = 342, s = 1.5);
translate([74, -102, 0]) rotate([0, 0, -20]) dd_prop_tent(seed = 343, s = 1.3);
translate([86, -106, 0]) rotate([0, 0, 26]) dd_prop_tent(seed = 344, s = 1.4);
translate([72, -110, 0]) dd_prop_campfire(seed = 345);
translate([96, -100, 0]) rotate([0, 0, 30]) dd_veh_van(seed = 346);
translate([48, -98, 0]) dd_bldg_shed(seed = 347, L = 8, D = 6);
lay_scatter(9, 48, 100, -114, -96, seed = 348)
    lay_pick($seed) { dd_prop_crate(); dd_prop_barrel(seed = $seed); dd_prop_gascan(); dd_prop_debris(seed = $seed); }
translate([128, -132, 0]) dd_ground_track(L = 52, W = 3.6, seed = 349);

// ================= 路侧田带（紧贴公路红线，消灭"路边一整条纯草皮"） =================
// 真实公路两侧不是草坪，是一直铺到路权线的田块与休耕地；这条带同时把公路和远景分层。

for (i = [0 : 4])
    translate([-290 + i * 66, 40, 0]) dd_nature_field_big(L = 62, D = 28, seed = 461 + i);
for (i = [0 : 2])
    translate([196 + i * 62, 40, 0]) dd_nature_field_big(L = 58, D = 28, seed = 471 + i);
for (i = [0 : 3])
    translate([-282 + i * 62, -44, 0]) dd_nature_field_big(L = 58, D = 30, seed = 481 + i);
for (i = [0 : 3])
    translate([176 + i * 48, -44, 0]) dd_ground_dirt(L = 44, D = 26, seed = 491 + i,
                                                     c1 = [0.38, 0.34, 0.22], c2 = [0.30, 0.27, 0.18]);
lay_scatter(16, -320, 320, 30, 54, seed = 495) dd_nature_grass(seed = $seed);
lay_scatter(14, -320, 320, -56, -32, seed = 496)
    lay_pick($seed) { dd_nature_grass(seed = $seed); dd_prop_debris(seed = $seed); dd_nature_bush(s = 1.2, seed = $seed); }

// ================= 果园与牧场（补齐公路以北的空档，x -110..210, y 45..120） =================

// 果园：树网格 + 田埂，是俯视下第二强的秩序纹理（仅次于田块）
translate([-52, 78, dd_layer(1)]) dd_ground_dirt(L = 96, D = 62, seed = 401,
                                       c1 = [0.36, 0.36, 0.22], c2 = [0.30, 0.31, 0.19]);
lay_grid(8, 6, 12, 10, seed = 402)
    translate([-52, 78, 0]) dd_nature_tree(s = lay_randr($seed, 5, 0.95, 1.25), seed = $seed);
lay_along([[-100, 46], [-4, 46]], step = 8.5, seed = 403) dd_prop_fence(len = 8.5);
lay_along([[-100, 110], [-4, 110]], step = 8.5, seed = 404) dd_prop_fence(len = 8.5);
translate([-16, 60, 0]) dd_bldg_shed(seed = 405, L = 10, D = 7);
translate([-30, 52, 0]) rotate([0, 0, 30]) dd_veh_pickup(seed = 406);
lay_scatter(6, -96, -12, 50, 106, seed = 407, rot = false) dd_prop_haybale(seed = $seed);
lay_scatter(8, -96, -12, 50, 106, seed = 408)
    lay_pick($seed) { dd_prop_crate(); dd_prop_debris(seed = $seed); dd_prop_pallet(seed = $seed); }

// 牧场：栅栏围出的三块围场 + 马厩 + 水槽
translate([164, 88, 0]) dd_ground_grass(L = 76, D = 56, seed = 411);
for (px = [130, 178])
    for (py = [62, 90])
    {
        lay_along([[px, py], [px + 46, py]], step = 8.5, seed = px + py) dd_prop_fence(len = 8.5);
        lay_along([[px, py], [px, py + 26]], step = 8.5, seed = px + py + 3) dd_prop_fence(len = 8.5);
    }
lay_along([[130, 116], [202, 116]], step = 8.5, seed = 412) dd_prop_fence(len = 8.5);
translate([196, 74, 0]) rotate([0, 0, 180]) dd_bldg_barn(seed = 413, L = 10, D = 12);
translate([160, 68, dd_layer(1)]) dd_ground_dirt(L = 26, D = 14, seed = 414);
translate([150, 70, 0]) dd_prop_haybale(seed = 415);
translate([144, 96, 0]) dd_prop_haybale(seed = 416);
translate([186, 104, 0]) dd_nature_tree(s = 1.5, seed = 417);
translate([134, 104, 0]) dd_nature_tree(s = 1.3, seed = 418);
translate([206, 96, 0]) dd_bldg_house(seed = 419, L = 10, D = 8);

// 东北再补一片田块，别让 x>235 的北角空着
translate([278, 162, 0]) do_farmland(cols = 2, rows = 2, seed = 421, cw = 48, ch = 36);

// ================= 南部牧草地与荒田（补齐公路以南的空档） =================

translate([-160, -70, 0]) dd_nature_field_big(L = 70, D = 44, seed = 431);
translate([-160, -108, 0]) dd_nature_field_big(L = 70, D = 26, seed = 432);
lay_along([[-196, -124], [-124, -124]], step = 8.5, seed = 433) dd_prop_fence(len = 8.5);
lay_along([[-196, -124], [-196, -46]], step = 8.5, seed = 434) dd_prop_fence(len = 8.5);
translate([-124, -78, dd_layer(1)]) rotate([0, 0, 90]) dd_ground_track(L = 60, W = 3.6, seed = 435);
translate([-186, -46, 0]) dd_bldg_shed(seed = 436, L = 9, D = 6);
translate([-150, -46, 0]) rotate([0, 0, 14]) dd_veh_harvester(seed = 437);

translate([20, -120, 0]) dd_nature_field_big(L = 46, D = 40, seed = 441);
translate([-8, -96, 0]) rotate([0, 0, 90]) dd_prop_hedge(len = 26, h = 1.4, seed = 442);
lay_scatter(9, 0, 44, -142, -100, seed = 443)
    lay_pick($seed) { dd_nature_stump(s = 1.2, seed = $seed); dd_prop_debris(seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); }
translate([140, -70, 0]) dd_nature_field_big(L = 60, D = 44, seed = 444);
translate([140, -108, 0]) dd_ground_dirt(L = 56, D = 26, seed = 445);
lay_along([[110, -48], [170, -48]], step = 8.5, seed = 446) dd_prop_fence(len = 8.5);
translate([112, -92, 0]) dd_prop_windturbine_fallen(seed = 447, s = 0.85);

// ================= 疏林与林带（分区团块，不做全图均匀噪点） =================

// 南部主林区（伐木场与露营地之间/外围）
lay_scatter(64, -320, -120, -190, -140, seed = 361)
    lay_pick($seed)
    {
        dd_nature_pine(s = lay_randr($seed, 5, 1.2, 1.8), seed = $seed);
        dd_nature_tree(s = lay_randr($seed, 6, 1.0, 1.4), seed = $seed);
        dd_nature_bush(s = 1.5, seed = $seed);
    }
lay_scatter(52, -10, 190, -190, -146, seed = 362)
    lay_pick($seed)
    {
        dd_nature_pine(s = lay_randr($seed, 5, 1.2, 1.7), seed = $seed);
        dd_nature_tree(s = lay_randr($seed, 6, 1.0, 1.4), seed = $seed);
    }
lay_scatter(34, -130, 30, -96, -60, seed = 363)
    lay_pick($seed) { dd_nature_tree(s = 1.2, seed = $seed); dd_nature_pine(s = 1.3, seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); }
lay_scatter(26, 110, 200, -100, -40, seed = 364)
    lay_pick($seed) { dd_nature_tree(s = 1.1, seed = $seed); dd_nature_bush(s = 1.4, seed = $seed); }
// 北部防风林与农田间的树篱
lay_along([[-330, 122], [-160, 122]], step = 13, seed = 365) dd_nature_tree(s = lay_randr($seed, 5, 1.0, 1.4), seed = $seed);
lay_along([[130, 122], [320, 122]], step = 14, seed = 366) dd_nature_tree(s = lay_randr($seed, 5, 1.0, 1.4), seed = $seed);
lay_scatter(24, -330, -200, 150, 196, seed = 367)
    lay_pick($seed) { dd_nature_pine(s = 1.4, seed = $seed); dd_nature_tree(s = 1.2, seed = $seed); }

// 边界林带（视觉围墙）
lay_scatter(72, -336, 336, 180, 198, seed = 371)
    lay_pick($seed) { dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed); dd_nature_tree(s = 1.4, seed = $seed); }
lay_scatter(72, -336, 336, -198, -180, seed = 372)
    lay_pick($seed) { dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed); dd_nature_tree(s = 1.4, seed = $seed); }
lay_scatter(40, -336, -304, -176, 176, seed = 373)
    lay_pick($seed) { dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed); dd_nature_tree(s = 1.3, seed = $seed); }
lay_scatter(40, 304, 336, -176, 176, seed = 374)
    lay_pick($seed) { dd_nature_pine(s = lay_randr($seed, 5, 1.3, 1.9), seed = $seed); dd_nature_tree(s = 1.3, seed = $seed); }
lay_scatter(22, -330, 330, 166, 182, seed = 375)
    lay_pick($seed) { dd_nature_stump(s = 1.2, seed = $seed); dd_nature_log(seed = $seed); dd_nature_bush(s = 1.5, seed = $seed); }
lay_scatter(22, -330, 330, -182, -166, seed = 376)
    lay_pick($seed) { dd_nature_stump(s = 1.2, seed = $seed); dd_nature_log(seed = $seed); dd_nature_bush(s = 1.5, seed = $seed); }

// ================= 收尾：路肩、田间与空地纹理 =================

lay_scatter(30, -310, 310, -20, 20, seed = 381) dd_prop_debris(seed = $seed);
lay_scatter(10, -300, 300, 18, 24, seed = 382)
    lay_pick($seed) { dd_prop_tires(seed = $seed); dd_prop_barrel(seed = $seed); dd_prop_crate(); }
lay_scatter(10, -300, 300, -24, -18, seed = 383)
    lay_pick($seed) { dd_prop_debris(seed = $seed); dd_prop_gascan(); dd_prop_tires(seed = $seed); }
// 公路与农田/林地之间的过渡草带
lay_scatter(40, -320, 320, 26, 100, seed = 384) dd_nature_grass(seed = $seed);
lay_scatter(40, -320, 320, -100, -26, seed = 385) dd_nature_grass(seed = $seed);
lay_scatter(30, -320, 320, 100, 190, seed = 386) dd_nature_grass(seed = $seed);
lay_scatter(30, -320, 320, -190, -100, seed = 387) dd_nature_grass(seed = $seed);
lay_scatter(9, -190, 190, 60, 96, seed = 388)
    dd_ground_dirt(L = lay_randr($seed, 5, 8, 18), D = lay_randr($seed, 6, 6, 12), seed = $seed);
lay_scatter(8, -120, 180, -80, -40, seed = 389)
    dd_ground_dirt(L = lay_randr($seed, 5, 8, 16), D = lay_randr($seed, 6, 6, 11), seed = $seed);
lay_scatter(7, -320, 320, -170, 170, seed = 390) dd_ground_puddle(s = lay_randr($seed, 5, 1.0, 2.0), seed = $seed);
