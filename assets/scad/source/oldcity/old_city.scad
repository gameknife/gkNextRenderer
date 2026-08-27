// old_city.scad —— 低多边形古代城池（参考低多边形中式围城示意图）
//
// 人尺度：1 unit = 1 m，OpenSCAD Z-up，+y 北 / -y 南；带朝向 module 约定 front = -y。
// 落地件底面 z=0，布局时 translate 到所在台面高度。角色（ScadRig，身高 ~1.7m）可直接行走：
// 门洞净宽 8 净高 6、马道坡度 ~21°、墙顶步道净宽 ~5m、街道 6~10m。
//
// 总平面（城墙外皮 192 x 192：x,y∈[-96,96]；地形 340 x 340）：
//   城外  四向官道通往地图边缘；西南水塘、西北石山、东北土丘、四周散树/岩石
//   城墙  高 9 厚 7；四角角楼、四面中央城门楼（北/东/南/西）、每面 2 座马面敌台
//         北门东侧 / 南门西侧各一条登城马道（可走上墙顶巡逻道）
//   城内  十字大街（宽 10，连四门）+ 宫城环街（宽 8）+ 巷道（宽 6）+ 顺城街（宽 5）
//   宫城  中央 56x56 内城（主城正殿 + 东西配殿 + 后殿 + 南宫门），环街四角箭塔
//   街区  西北民居 | 北仓库群(圆囤) | 东北民居 | 东北角民居
//         西侧兵营/校场 + 军帐营地 | 东侧民居 + 市场(彩棚摊位)
//         西南农田 | 南农田/果园 | 南酒楼 | 东南农田/打谷场
//
// 标高：地形草地顶 z=0.30(oc_TZ())；城基台地顶 z=0.50(oc_CZ())；街面 oc_CZ()+0.05；墙顶步道 oc_CZ()+9.12。
//
// POI 锚点（命名约定与 airport.scad / habor_city_v2.scad 同构）：
//   锚点 = 具名 user module，加载后节点名 = module 名，WorldTranslation = 点位坐标。
//   锚点调用必须 translate(...) 在 rotate(...) 外层。锚点即建筑/设施本体或点位标石。
//   —— 攻防 / 巡逻 玩法 ——
//     gate_01..04    城门楼（北/东/南/西，含墩台+城楼+常开门扇）
//     tower_01..04   角楼（NW/NE/SE/SW，墙顶平台 + 两层攒尖楼）
//     watch_01..04   箭塔（宫城环街四角，独立哨塔）
//     ramp_01..02    马道上口界石（北门东侧 / 南门西侧，墙顶标高）
//     node_01..08    路网节点界石（01..04 环街四角 NW/NE/SE/SW，05..08 四门内广场 北/东/南/西）
//     spawn_01..04   城外官道生成点（四门外 ~22m 路面道钉，北/东/南/西）
//   —— 经营 玩法 ——
//     keep_01        主城（正殿，重檐庑殿 + 三重台基 + "主城"匾）
//     innergate_01   宫城南门（门屋 + 常开门扇）
//     barracks_01..02 兵营   warehouse_01..02 仓库   inn_01 酒楼
//     house_01..16   民居    farm_01..08 农田        market_01..08 市场摊位
//     well_01..02    水井
//   非锚点一律 ground_* / part_* / wall_* / bldg_* / prop_* / farm_* / nature_* 前缀。
//
// 构造约定（沿用 airport.scad / habor_city_v2.scad 经验）：
//   * 仅用引擎 SCADLoader 已支持特性（无 offset/projection/minkowski/import/resize）；
//   * 避免 difference：门洞用分段墩台 + 过梁拼出，屋面用 polyhedron 画家叠层；
//   * 伪随机 oc_rnd(i,m) 整数散列，确定性（引擎与 OpenSCAD 渲染一致）；
//   * text() 只用于匾额（CJK 走 DroidSansFallback），字宽按全角 size 手动居中（不用 len() 数多字节）。

use <../../lib/kit_old_city.scad>

$fn = 10;

// ================= 功能锚点（节点名 = module 名；调用处 translate 在 rotate 外层） =================
module gate_01() oc_bldg_gatehouse("北门");
module gate_02() oc_bldg_gatehouse("东门");
module gate_03() oc_bldg_gatehouse("南门");
module gate_04() oc_bldg_gatehouse("西门");
module tower_01() oc_bldg_corner_tower();   // 角楼 NW
module tower_02() oc_bldg_corner_tower();   // 角楼 NE
module tower_03() oc_bldg_corner_tower();   // 角楼 SE
module tower_04() oc_bldg_corner_tower();   // 角楼 SW
module watch_01() oc_bldg_tower_watch();    // 箭塔 环街NW
module watch_02() oc_bldg_tower_watch();    // 箭塔 环街NE
module watch_03() oc_bldg_tower_watch();    // 箭塔 环街SE
module watch_04() oc_bldg_tower_watch();    // 箭塔 环街SW
module ramp_01() oc_prop_marker();          // 马道上口 北门东
module ramp_02() oc_prop_marker();          // 马道上口 南门西
module node_01() oc_prop_marker();          // 环街 NW
module node_02() oc_prop_marker();          // 环街 NE
module node_03() oc_prop_marker();          // 环街 SE
module node_04() oc_prop_marker();          // 环街 SW
module node_05() oc_prop_marker();          // 北门内广场
module node_06() oc_prop_marker();          // 东门内广场
module node_07() oc_prop_marker();          // 南门内广场
module node_08() oc_prop_marker();          // 西门内广场
module spawn_01() oc_prop_spawn_stone();    // 北官道
module spawn_02() oc_prop_spawn_stone();    // 东官道
module spawn_03() oc_prop_spawn_stone();    // 南官道
module spawn_04() oc_prop_spawn_stone();    // 西官道
module keep_01() oc_bldg_keep();
module innergate_01() oc_bldg_inner_gate();
module barracks_01() oc_bldg_barracks(1);
module barracks_02() oc_bldg_barracks(2);
module warehouse_01() oc_bldg_warehouse(1);
module warehouse_02() oc_bldg_warehouse(2);
module inn_01() oc_bldg_inn();
module well_01() oc_prop_well();
module well_02() oc_prop_well();
module house_01() oc_bldg_house(1);
module house_02() oc_bldg_house(2, 8.5, 6.2);
module house_03() oc_bldg_house(3, 9.5, 7);
module house_04() oc_bldg_house(4);
module house_05() oc_bldg_house(5, 8.5, 6);
module house_06() oc_bldg_house(6);
module house_07() oc_bldg_house(7, 8, 6.2);
module house_08() oc_bldg_house(8, 10, 7);
module house_09() oc_bldg_house(9);
module house_10() oc_bldg_house(10, 9.5, 6.8);
module house_11() oc_bldg_house(11, 8.5, 6.2);
module house_12() oc_bldg_house(12);
module house_13() oc_bldg_house(13, 9.5, 7);
module house_14() oc_bldg_house(14, 8.5, 6.4);
module house_15() oc_bldg_house(15);
module house_16() oc_bldg_house(16, 9, 6.8);
module farm_01() oc_farm_plot(1);
module farm_02() oc_farm_plot(2, 13, 9);
module farm_03() oc_farm_plot(3);
module farm_04() oc_farm_plot(4, 12, 8);
module farm_05() oc_farm_plot(5, 14, 9);
module farm_06() oc_farm_plot(6);
module farm_07() oc_farm_plot(7, 12, 8.5);
module farm_08() oc_farm_plot(8);
module market_01() oc_bldg_stall(1);
module market_02() oc_bldg_stall(2);
module market_03() oc_bldg_stall(3);
module market_04() oc_bldg_stall(4);
module market_05() oc_bldg_stall(5);
module market_06() oc_bldg_stall(6);
module market_07() oc_bldg_stall(7);
module market_08() oc_bldg_stall(8);

// ======================== 总装 ========================

oc_ground_base();
oc_ground_roads_out();
oc_ground_pond();
oc_ground_city();

// ---- 城墙（外皮 ±96；北/南墙全宽，东/西墙嵌于其间；门洞留 8m 缺口） ----
for (sy = [-1, 1], sx = [-1, 1])
    translate([sx * 50, sy * 92.5, oc_CZ()]) rotate([0, 0, sy > 0 ? 0 : 180])
        oc_wall_run(92, (sx * sy > 0) ? -21 : 0, (sx * sy > 0) ? -11 : 0);   // 豁口=马道登城平台
for (sx = [-1, 1], sy = [-1, 1])
    translate([sx * 92.5, sy * 50, oc_CZ()]) rotate([0, 0, (sx > 0) ? -90 : 90]) oc_wall_run(92);

// 马面敌台（每面两座，旗/铺房交替）
for (s = [-1, 1])
{
    translate([s * 48, 92.5, oc_CZ()]) oc_wall_bastion(s > 0 ? 1 : 0);
    translate([s * 48, -92.5, oc_CZ()]) rotate([0, 0, 180]) oc_wall_bastion(s > 0 ? 0 : 1);
    translate([92.5, s * 48, oc_CZ()]) rotate([0, 0, -90]) oc_wall_bastion(s > 0 ? 1 : 0);
    translate([-92.5, s * 48, oc_CZ()]) rotate([0, 0, 90]) oc_wall_bastion(s > 0 ? 0 : 1);
}

// 城门楼（北/东/南/西）
translate([0, 92.5, oc_CZ()]) gate_01();
translate([92.5, 0, oc_CZ()]) rotate([0, 0, -90]) gate_02();
translate([0, -92.5, oc_CZ()]) rotate([0, 0, 180]) gate_03();
translate([-92.5, 0, oc_CZ()]) rotate([0, 0, 90]) gate_04();

// 角楼（旋转使垛墙豁口朝向相接城墙）
translate([-92.5, 92.5, oc_CZ()]) rotate([0, 0, 90]) tower_01();
translate([92.5, 92.5, oc_CZ()]) tower_02();
translate([92.5, -92.5, oc_CZ()]) rotate([0, 0, -90]) tower_03();
translate([-92.5, -92.5, oc_CZ()]) rotate([0, 0, 180]) tower_04();

// 登城马道（北门东 / 南门西）+ 上口界石锚点
translate([8, 87.3, oc_CZ()]) oc_wall_ramp();
translate([-8, -87.3, oc_CZ()]) rotate([0, 0, 180]) oc_wall_ramp();
translate([34, 87.3, oc_CZ() + oc_WH() + 0.2]) ramp_01();
translate([-34, -87.3, oc_CZ() + oc_WH() + 0.2]) ramp_02();

// ---- 宫城（内城 56x56：主城正殿 + 东西配殿 + 后殿 + 南宫门） ----
translate([0, 28, oc_CZ()]) oc_wall_palace_run(57.2);
for (sx = [-1, 1]) translate([sx * 28, 0, oc_CZ()]) rotate([0, 0, 90]) oc_wall_palace_run(54);
for (sx = [-1, 1]) translate([sx * 16.6, -28, oc_CZ()]) oc_wall_palace_run(22.8);
for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 28, sy * 28, oc_CZ()]) oc_wall_palace_pier();
translate([0, -28, oc_CZ()]) innergate_01();
translate([0, 6, oc_CZ() + 0.04]) keep_01();
translate([-19.5, 0, oc_CZ() + 0.04]) rotate([0, 0, 90]) oc_bldg_side_hall(11, 6, 1);
translate([19.5, 0, oc_CZ() + 0.04]) rotate([0, 0, -90]) oc_bldg_side_hall(11, 6, 2);
translate([0, 22, oc_CZ() + 0.04]) oc_bldg_side_hall(14, 6.5, 3);
translate([0, -12.5, oc_CZ() + 0.04]) oc_prop_incense();
for (sx = [-1, 1]) translate([sx * 8, -18.5, oc_CZ() + 0.04]) oc_prop_flag(oc_REDC(), 7);
for (sx = [-1, 1], iy = [0 : 1]) translate([sx * 3.8, -15 - iy * 5, oc_CZ() + 0.04]) oc_prop_stone_lamp();
for (sx = [-1, 1]) translate([sx * 23, 22, oc_CZ() + 0.04]) oc_nature_pine(1.1);
for (sx = [-1, 1]) translate([sx * 23.5, -22, oc_CZ() + 0.04]) oc_nature_tree(0.95, 41 + sx);

// 环街四角箭塔
translate([-45.5, 45.5, oc_CZ()]) watch_01();
translate([45.5, 45.5, oc_CZ()]) watch_02();
translate([45.5, -45.5, oc_CZ()]) watch_03();
translate([-45.5, -45.5, oc_CZ()]) watch_04();

// ---- 西北街区：民居 x4 + 水井 ----
translate([-76, 71, oc_CZ()]) house_01();
translate([-57, 71, oc_CZ()]) house_02();
translate([-76, 53, oc_CZ()]) rotate([0, 0, 180]) house_03();
translate([-57, 53, oc_CZ()]) rotate([0, 0, 180]) house_04();
translate([-66.5, 62, oc_CZ()]) well_01();
translate([-74.5, 62, oc_CZ()]) oc_nature_tree(0.9, 3);
translate([-59, 61.5, oc_CZ()]) oc_nature_tree(0.8, 9);
translate([-70, 65, oc_CZ()]) oc_prop_jar();
translate([-62.5, 59, oc_CZ()]) oc_prop_crates(2);
translate([-81, 60, oc_CZ()]) oc_prop_cart(1);

// ---- 北街区（西）：仓库群 + 圆囤粮仓 ----
color(oc_DIRTD()) translate([-24, 63, oc_CZ()]) oc_slab(36, 40, 0.03);
translate([-33, 56, oc_CZ() + 0.03]) warehouse_01();
translate([-15, 56, oc_CZ() + 0.03]) warehouse_02();
translate([-34, 75, oc_CZ() + 0.03]) oc_bldg_granary();
translate([-24, 76, oc_CZ() + 0.03]) oc_bldg_granary();
translate([-14, 75, oc_CZ() + 0.03]) oc_bldg_granary();
translate([-37, 46.5, oc_CZ() + 0.03]) oc_prop_crates(4);
translate([-28, 46, oc_CZ() + 0.03]) oc_prop_cart(2);
translate([-19, 46.5, oc_CZ() + 0.03]) oc_prop_crates(7);
translate([-10, 47, oc_CZ() + 0.03]) oc_prop_hay();
translate([-40.5, 70, oc_CZ() + 0.03]) oc_prop_jar();

// ---- 北街区（东）：民居 x5 ----
translate([12, 71, oc_CZ()]) house_05();
translate([25, 71, oc_CZ()]) house_06();
translate([38, 71, oc_CZ()]) house_07();
translate([17, 52, oc_CZ()]) rotate([0, 0, 180]) house_08();
translate([33, 52, oc_CZ()]) rotate([0, 0, 180]) house_09();
translate([25, 61.5, oc_CZ()]) oc_nature_tree(0.85, 13);
translate([9, 60, oc_CZ()]) oc_prop_jar();
translate([40, 60, oc_CZ()]) oc_prop_crates(9);

// ---- 东北街区：民居 x3 + 园地 ----
color(oc_GRASSD()) translate([66.5, 62, oc_CZ()]) oc_slab(22, 8, 0.03);
translate([58, 71, oc_CZ()]) house_10();
translate([75, 71, oc_CZ()]) house_11();
translate([66, 52, oc_CZ()]) rotate([0, 0, 180]) house_12();
translate([59, 62, oc_CZ() + 0.03]) oc_nature_tree(0.9, 17);
translate([74, 62, oc_CZ() + 0.03]) oc_nature_tree(0.85, 18);
translate([80, 55, oc_CZ()]) oc_prop_hay();

// ---- 西街区（北）：兵营 + 校场 ----
color(oc_DIRTD()) translate([-64, 23.5, oc_CZ()]) oc_slab(38, 35, 0.04);
translate([-64, 34, oc_CZ() + 0.04]) barracks_01();
for (i = [0 : 2]) translate([-78 + i * 6, 27, oc_CZ() + 0.04]) oc_prop_rack();
for (i = [0 : 1]) translate([-49.5, 11 + i * 8, oc_CZ() + 0.04]) rotate([0, 0, -90]) oc_prop_target();
for (sx = [-1, 1]) translate([-64 + sx * 16, 8.5, oc_CZ() + 0.04]) oc_prop_flag(oc_REDC(), 5.5);
translate([-77, 12, oc_CZ() + 0.04]) oc_prop_crates(11);

// ---- 西街区（南）：兵营 + 军帐营地 ----
color(oc_DIRTD()) translate([-64, -23.5, oc_CZ()]) oc_slab(38, 35, 0.04);
translate([-64, -12, oc_CZ() + 0.04]) barracks_02();
translate([-76, -27, oc_CZ() + 0.04]) oc_bldg_tent(1);
translate([-67, -30, oc_CZ() + 0.04]) oc_bldg_tent(2);
translate([-56, -27, oc_CZ() + 0.04]) oc_bldg_tent(3);
translate([-71, -38, oc_CZ() + 0.04]) oc_bldg_tent(4);
translate([-58, -37, oc_CZ() + 0.04]) oc_prop_rack();
translate([-50, -33, oc_CZ() + 0.04]) oc_prop_hay();
translate([-79, -37, oc_CZ() + 0.04]) oc_prop_crates(13);
translate([-48, -9, oc_CZ() + 0.04]) oc_prop_flag(oc_REDC(), 5.5);

// ---- 东街区（北）：民居 x4 + 水井 ----
translate([55, 32, oc_CZ()]) house_13();
translate([72, 32, oc_CZ()]) house_14();
translate([55, 13, oc_CZ()]) rotate([0, 0, 180]) house_15();
translate([72, 13, oc_CZ()]) rotate([0, 0, 180]) house_16();
translate([63.5, 22.5, oc_CZ()]) well_02();
translate([47.5, 23, oc_CZ()]) oc_nature_tree(0.85, 21);
translate([80, 22, oc_CZ()]) oc_nature_tree(0.8, 22);
translate([76, 27, oc_CZ()]) oc_prop_jar();

// ---- 东街区（南）：市场（双排彩棚摊位） ----
color(oc_PAVEC()) translate([64, -23.5, oc_CZ()]) oc_slab(38, 35, 0.045);
translate([50, -14, oc_CZ() + 0.045]) market_01();
translate([59, -14, oc_CZ() + 0.045]) market_02();
translate([68, -14, oc_CZ() + 0.045]) market_03();
translate([77, -14, oc_CZ() + 0.045]) market_04();
translate([50, -33, oc_CZ() + 0.045]) rotate([0, 0, 180]) market_05();
translate([59, -33, oc_CZ() + 0.045]) rotate([0, 0, 180]) market_06();
translate([68, -33, oc_CZ() + 0.045]) rotate([0, 0, 180]) market_07();
translate([77, -33, oc_CZ() + 0.045]) rotate([0, 0, 180]) market_08();
translate([55, -23.5, oc_CZ() + 0.045]) oc_prop_cart(5);
translate([70, -23, oc_CZ() + 0.045]) oc_prop_crates(15);
translate([63, -24, oc_CZ() + 0.045]) oc_prop_jar();
translate([82.5, -39, oc_CZ()]) oc_nature_tree(0.85, 25);

// ---- 西南街区：农田 x4 + 草垛 ----
color(oc_GRASSD()) translate([-66.5, -63, oc_CZ()]) oc_slab(33, 40, 0.03);
translate([-76, -54, oc_CZ() + 0.03]) farm_01();
translate([-57, -54, oc_CZ() + 0.03]) farm_02();
translate([-76, -71, oc_CZ() + 0.03]) farm_03();
translate([-57, -71, oc_CZ() + 0.03]) farm_04();
translate([-66, -80.5, oc_CZ() + 0.03]) oc_prop_hay();
translate([-78, -80, oc_CZ() + 0.03]) oc_prop_hay();
translate([-49.5, -80, oc_CZ() + 0.03]) oc_prop_cart(7);

// ---- 南街区（西）：农田 x2 + 果园 ----
color(oc_GRASSD()) translate([-24, -63, oc_CZ()]) oc_slab(36, 40, 0.03);
translate([-33, -54, oc_CZ() + 0.03]) farm_05();
translate([-14, -54, oc_CZ() + 0.03]) farm_06();
for (ix = [0 : 3], iy = [0 : 1])
    translate([-38 + ix * 9, -68 - iy * 9, oc_CZ() + 0.03])
        oc_nature_tree(0.85 + 0.05 * oc_rnd(ix + iy * 4, 3), 30 + ix + iy * 7);
translate([-9, -78, oc_CZ() + 0.03]) oc_prop_hay();

// ---- 南街区（东）：酒楼 + 草棚后院 ----
color(oc_DIRTD()) translate([24, -63, oc_CZ()]) oc_slab(36, 40, 0.035);
translate([13, -57, oc_CZ() + 0.035]) rotate([0, 0, -90]) inn_01();
translate([30, -55, oc_CZ() + 0.035]) oc_bldg_shed();
translate([24, -73, oc_CZ() + 0.035]) oc_prop_cart(9);
translate([33, -74, oc_CZ() + 0.035]) oc_prop_crates(17);
translate([15, -75, oc_CZ() + 0.035]) oc_prop_jar();
translate([17.5, -75, oc_CZ() + 0.035]) oc_prop_jar();
translate([39, -70, oc_CZ()]) oc_nature_tree(0.9, 33);

// ---- 东南街区：农田 x2 + 打谷场 ----
color(oc_GRASSD()) translate([66.5, -63, oc_CZ()]) oc_slab(33, 40, 0.03);
translate([57, -54, oc_CZ() + 0.03]) farm_07();
translate([76, -54, oc_CZ() + 0.03]) farm_08();
color(oc_DIRTD()) translate([66, -74, oc_CZ() + 0.03]) cylinder(h = 0.04, r = 7, $fn = 12);
translate([62, -74, oc_CZ() + 0.07]) oc_prop_hay();
translate([70, -76, oc_CZ() + 0.07]) oc_prop_cart(11);
translate([80.5, -78, oc_CZ()]) oc_nature_tree(0.85, 35);

// ---- 街道家具与节点 ----
translate([0, -50, oc_CZ() + 0.05]) oc_prop_paifang();
for (sx = [-1, 1], i = [0 : 2])
{
    translate([sx * 4.3, -47 - i * 9, oc_CZ() + 0.05]) oc_prop_stone_lamp();
    translate([sx * 4.3, 47 + i * 9, oc_CZ() + 0.05]) oc_prop_stone_lamp();
    translate([47 + i * 9, sx * 4.3, oc_CZ() + 0.05]) oc_prop_stone_lamp();
    translate([-47 - i * 9, sx * 4.3, oc_CZ() + 0.05]) oc_prop_stone_lamp();
}
translate([7.5, 78, oc_CZ() + 0.05]) oc_prop_crates(21);
translate([-7.5, 77, oc_CZ() + 0.05]) oc_prop_cart(15);
translate([78, 7.5, oc_CZ() + 0.05]) oc_prop_crates(23);
translate([-77, -7, oc_CZ() + 0.05]) oc_prop_jar();
translate([7, -78, oc_CZ() + 0.05]) oc_prop_crates(25);

translate([-38, 38, oc_CZ() + 0.05]) node_01();
translate([38, 38, oc_CZ() + 0.05]) node_02();
translate([38, -38, oc_CZ() + 0.05]) node_03();
translate([-38, -38, oc_CZ() + 0.05]) node_04();
translate([0, 82, oc_CZ() + 0.05]) node_05();
translate([82, 0, oc_CZ() + 0.05]) node_06();
translate([0, -82, oc_CZ() + 0.05]) node_07();
translate([-82, 0, oc_CZ() + 0.05]) node_08();

// ---- 城外：官道生成点 + 门外旗 + 地景 ----
translate([0, 118, oc_TZ() + 0.05]) spawn_01();
translate([118, 0, oc_TZ() + 0.05]) rotate([0, 0, 90]) spawn_02();
translate([0, -118, oc_TZ() + 0.05]) spawn_03();
translate([-118, 0, oc_TZ() + 0.05]) rotate([0, 0, 90]) spawn_04();
for (a = [0, 90, 180, 270]) rotate([0, 0, a])
{
    translate([6.5, 100.5, oc_TZ()]) oc_prop_flag(oc_REDC(), 5);
    translate([-6.5, 100.5, oc_TZ()]) oc_prop_flag(oc_REDC(), 5);
    translate([5.6, 128, oc_TZ()]) oc_nature_rock(0.8, a + 1);
    translate([-5.8, 168, oc_TZ()]) oc_nature_rock(0.7, a + 2);
}

translate([-138, 138, oc_TZ()]) oc_nature_mount(1.1);
translate([-100, 156, oc_TZ()]) oc_nature_mount(0.65);
translate([136, 142, oc_TZ()]) oc_nature_hill(22, 9.5);
translate([156, 108, oc_TZ()]) oc_nature_hill(14, 6);
translate([-152, 52, oc_TZ()]) oc_nature_hill(13, 5.5);
oc_nature_scatter(1, 16, 14, 162, 106, 162);
oc_nature_scatter(2, 10, -96, -14, 106, 158);
oc_nature_scatter(3, 16, 106, 162, 12, 95);
oc_nature_scatter(4, 14, 106, 162, -95, -12);
oc_nature_scatter(5, 14, -162, -106, 12, 95);
oc_nature_scatter(6, 12, -96, -14, -162, -106);
oc_nature_scatter(7, 14, 14, 162, -162, -106);
oc_nature_scatter(8, 10, -162, -106, -95, -50);
