// airport.scad —— 低多边形等距机场航站楼（参考 Jumbo Airport Story 风格示意图）
//
// 为后续机场模拟 demo 预留 POI 锚点（命名约定与 office.scad / OfficeMap 同构）：
//   锚点 = 具名 user module，加载后节点名 = module 名，WorldTranslation = 点位坐标。
//   锚点调用必须 translate(...) 在 rotate(...) 外层。
//   锚点类别前缀（未来 AirportMap 按前缀归类）：
//     entrance_<id>  航站楼出入口（旅客生成/离场点）×3
//     checkin_<id>   人工值机柜台 ×6         kiosk_<id>   自助值机机 ×4
//     security_<id>  安检通道（南进北出）×4   gate_<id>    登机口 ×6
//     wait_<id>      候机排椅 ×12            cafe_<id>    咖啡岛柜台 ×1
//     food_<id>      快餐店柜台 ×1           shop_<id>    便利店收银 ×1
//     book_<id>      书店收银 ×1             gift_<id>    礼品店收银 ×1
//     toilet_<id>    卫生间门（01=男 02=女）  staff_<id>   员工办公桌 ×2
//     info_<id>      问询台 ×1               atm_<id>     ATM ×1
//     vending_<id>   自动售货机 ×5
//   非锚点一律 wall_* / part_* / furn_* / prop_* / veh_* / ground_* 前缀。
//   所有带朝向的 module 约定 front = -y；布局处 rotate 调整朝向，正面前方留行走净空。
//
// OpenSCAD Z-up；地面顶面 z = 0.15（与 office.scad / kGroundY 一致）。
// 场地 84x80：航站楼 60x40（x∈[-30,30], y∈[-12,28]，玻璃幕墙、无顶棚切顶视角），
// 北侧 y>28 停机坪（3 架客机 + 地勤车队），南侧 y<-12 陆侧（人行道/马路/停车场/公交）。
// 室内分区：陆侧大厅（值机×6/自助值机/问询/ATM）→ 中部安检 ×4 → 空侧
// 西餐饮区（BURGER 快餐 + COFFEE 咖啡岛）/ 东零售街（SHOP/BOOKS/GIFTS 三连铺）
// → 北侧 6 登机口 + 双候机排椅集群；东北卫生间、东侧员工办公室。

use <lib/kit_airport.scad>

$fn = 16;
TEALC    = [0.16, 0.62, 0.55];    // 礼品店
SHOPWALL = [0.82, 0.81, 0.78];    // 零售街隔墙

// ================= 功能点位（锚点；translate 必须在 rotate 外层） =================
module entrance_01() ap_furn_entrance();
module entrance_02() ap_furn_entrance();
module entrance_03() ap_furn_entrance();
module checkin_01() ap_furn_checkin_desk("1");
module checkin_02() ap_furn_checkin_desk("2");
module checkin_03() ap_furn_checkin_desk("3");
module checkin_04() ap_furn_checkin_desk("4");
module checkin_05() ap_furn_checkin_desk("5");
module checkin_06() ap_furn_checkin_desk("6");
module kiosk_01() ap_furn_kiosk();
module kiosk_02() ap_furn_kiosk();
module kiosk_03() ap_furn_kiosk();
module kiosk_04() ap_furn_kiosk();
module security_01() ap_furn_security_lane();
module security_02() ap_furn_security_lane();
module security_03() ap_furn_security_lane();
module security_04() ap_furn_security_lane();
module gate_01() ap_furn_gate_door("GATE 1");
module gate_02() ap_furn_gate_door("GATE 2");
module gate_03() ap_furn_gate_door("GATE 3");
module gate_04() ap_furn_gate_door("GATE 4");
module gate_05() ap_furn_gate_door("GATE 5");
module gate_06() ap_furn_gate_door("GATE 6");
module wait_01() ap_furn_bench_row();
module wait_02() ap_furn_bench_row();
module wait_03() ap_furn_bench_row();
module wait_04() ap_furn_bench_row();
module wait_05() ap_furn_bench_row();
module wait_06() ap_furn_bench_row();
module wait_07() ap_furn_bench_row();
module wait_08() ap_furn_bench_row();
module wait_09() ap_furn_bench_row();
module wait_10() ap_furn_bench_row();
module wait_11() ap_furn_bench_row();
module wait_12() ap_furn_bench_row();
module cafe_01() ap_furn_cafe_counter();
module food_01() ap_furn_food_counter();
module shop_01() ap_furn_checkout();
module book_01() ap_furn_checkout();
module gift_01() ap_furn_checkout();
module toilet_01() ap_furn_wc_door(false);
module toilet_02() ap_furn_wc_door(true);
module staff_01() ap_furn_staff_desk();
module staff_02() ap_furn_staff_desk();
module info_01() ap_furn_info_desk();
module atm_01() ap_furn_atm();
module vending_01() ap_furn_vending([0.80, 0.30, 0.28]);
module vending_02() ap_furn_vending([0.25, 0.45, 0.75]);
module vending_03() ap_furn_vending([0.30, 0.60, 0.45]);
module vending_04() ap_furn_vending([0.90, 0.62, 0.20]);
module vending_05() ap_furn_vending([0.55, 0.45, 0.75]);

// ======================== 布局 ========================
FZ = 0.15;   // 地面顶面

ap_ground_base();
ap_ground_carpet();
ap_ground_apron();
ap_ground_landside();

// ---- 玻璃幕墙（南墙三入口洞，北墙六登机口洞） ----
translate([-30, -12, FZ]) ap_wall_glass_seg(10.5);
translate([-16.5, -12, FZ]) ap_wall_glass_seg(15.0);
translate([1.5, -12, FZ]) ap_wall_glass_seg(11.0);
translate([15.5, -12, FZ]) ap_wall_glass_seg(14.5);
translate([-30, 28, FZ]) ap_wall_glass_seg(9.1);
translate([-19.1, 28, FZ]) ap_wall_glass_seg(6.2);
translate([-11.1, 28, FZ]) ap_wall_glass_seg(6.2);
translate([-3.1, 28, FZ]) ap_wall_glass_seg(6.2);
translate([4.9, 28, FZ]) ap_wall_glass_seg(6.2);
translate([12.9, 28, FZ]) ap_wall_glass_seg(6.2);
translate([20.9, 28, FZ]) ap_wall_glass_seg(9.1);
translate([-30, -12, FZ]) rotate([0, 0, 90]) ap_wall_glass_seg(40);
translate([30, -12, FZ]) rotate([0, 0, 90]) ap_wall_glass_seg(40);
translate([-30, -12, 1.66 + FZ]) ap_wall_corner_col();
translate([-30, 28, 1.66 + FZ]) ap_wall_corner_col();
translate([30, 28, 1.66 + FZ]) ap_wall_corner_col();
translate([30, -12, 1.66 + FZ]) ap_wall_corner_col();
// 入口 + 大招牌
translate([-18, -12, FZ]) entrance_01();
translate([0, -12, FZ]) entrance_02();
translate([14, -12, FZ]) entrance_03();
translate([-18, -12.25, 3.70 + FZ]) ap_prop_big_sign("AIRPORT");
translate([0, -12.25, 3.70 + FZ]) ap_prop_big_sign("AIRPORT");

// ---- 值机区（西北陆侧）：6 柜台 + 行李墙 + 排队线 + 航显 ----
translate([-27.0, 1.2, FZ]) checkin_01();
translate([-24.4, 1.2, FZ]) checkin_02();
translate([-21.8, 1.2, FZ]) checkin_03();
translate([-19.2, 1.2, FZ]) checkin_04();
translate([-16.6, 1.2, FZ]) checkin_05();
translate([-14.0, 1.2, FZ]) checkin_06();
translate([-30, 4, FZ]) ap_wall_solid_seg(19.2, [0.88, 0.87, 0.84]);
translate([-10.8, 4, FZ]) ap_wall_solid_seg(1.0, [0.88, 0.87, 0.84]);
translate([-27.5, -0.6, FZ]) ap_prop_queue_line(13);
translate([-27.5, -1.8, FZ]) ap_prop_queue_line(13);
translate([-21, 3.3, FZ]) ap_prop_fids();

// ---- 自助值机（值机区东侧，面东） ----
translate([-10.5, 0.6, FZ]) rotate([0, 0, 90]) kiosk_01();
translate([-10.5, -0.8, FZ]) rotate([0, 0, 90]) kiosk_02();
translate([-10.5, -2.2, FZ]) rotate([0, 0, 90]) kiosk_03();
translate([-10.5, -3.6, FZ]) rotate([0, 0, 90]) kiosk_04();

// ---- 安检区（中央，南进北出）：4 通道 + 排队线 + 指示牌 + 东侧玻璃界墙 ----
translate([-9.3, 4.6, FZ]) security_01();
translate([-7.1, 4.6, FZ]) security_02();
translate([-4.9, 4.6, FZ]) security_03();
translate([-2.7, 4.6, FZ]) security_04();
translate([-0.7, 4, FZ]) ap_part_glass_wall(30.7, 24.7, 26.1);
translate([-10.2, 2.6, FZ]) ap_prop_queue_line(9);
translate([-10.2, 1.5, FZ]) ap_prop_queue_line(9);
translate([-5.8, 0.6, FZ]) ap_prop_hang_sign("DEPARTURES");
translate([-5, 9.6, FZ]) ap_prop_hang_sign("ALL GATES");
translate([24.7, 4.4, FZ]) ap_prop_hang_sign("EXIT", [0.22, 0.58, 0.35], false);
translate([24.0, 3.0, FZ]) ap_prop_stanchion();
translate([25.4, 3.0, FZ]) ap_prop_stanchion();

// ---- 登机口（北墙 6 门） ----
translate([-20, 27.6, FZ]) gate_01();
translate([-12, 27.6, FZ]) gate_02();
translate([-4, 27.6, FZ]) gate_03();
translate([4, 27.6, FZ]) gate_04();
translate([12, 27.6, FZ]) gate_05();
translate([20, 27.6, FZ]) gate_06();

// ---- 候机区（东西双集群 12 组排椅）+ 绿植 + 航显 ----
translate([-22.0, 16.5, FZ]) rotate([0, 0, 180]) wait_01();
translate([-18.6, 16.5, FZ]) rotate([0, 0, 180]) wait_02();
translate([-15.2, 16.5, FZ]) rotate([0, 0, 180]) wait_03();
translate([-22.0, 19.5, FZ]) rotate([0, 0, 180]) wait_04();
translate([-18.6, 19.5, FZ]) rotate([0, 0, 180]) wait_05();
translate([-15.2, 19.5, FZ]) rotate([0, 0, 180]) wait_06();
translate([9.0, 16.5, FZ]) rotate([0, 0, 180]) wait_07();
translate([12.4, 16.5, FZ]) rotate([0, 0, 180]) wait_08();
translate([15.8, 16.5, FZ]) rotate([0, 0, 180]) wait_09();
translate([9.0, 19.5, FZ]) rotate([0, 0, 180]) wait_10();
translate([12.4, 19.5, FZ]) rotate([0, 0, 180]) wait_11();
translate([15.8, 19.5, FZ]) rotate([0, 0, 180]) wait_12();
translate([-18.6, 18.0, FZ]) ap_prop_planter(2.4);
translate([12.4, 18.0, FZ]) ap_prop_planter(2.4);
translate([1.0, 21.5, FZ]) ap_prop_fids();
translate([-6, 10.2, FZ]) ap_prop_fids();
translate([-29.55, 18.6, FZ]) rotate([0, 0, 90]) vending_03();
translate([-29.5, 17.2, FZ]) rotate([0, 0, 90]) ap_prop_fountain();
translate([29.55, 11.4, FZ]) rotate([0, 0, -90]) vending_01();
translate([29.55, 12.6, FZ]) rotate([0, 0, -90]) vending_02();

// ---- 西餐饮区：BURGER 快餐（后厨 + 柜台 + 门头 + 6 桌）----
translate([-27.6, 7, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(6.4, [0.55, 0.35, 0.22]);
translate([-29.3, 10.0, FZ]) rotate([0, 0, 90]) ap_furn_kitchen_strip(5);
translate([-26.8, 10.2, FZ]) rotate([0, 0, 90]) food_01();
translate([-25.9, 10.2, FZ]) rotate([0, 0, 90]) ap_prop_shop_portal(6.4, "BURGER", ap_REDFOOD());
translate([-23.0, 8.5, FZ]) ap_furn_cafe_table(ap_REDFOOD());
translate([-19.6, 8.5, FZ]) rotate([0, 0, 55]) ap_furn_cafe_table(ap_REDFOOD());
translate([-16.2, 8.5, FZ]) rotate([0, 0, -30]) ap_furn_cafe_table(ap_REDFOOD());
translate([-23.0, 11.8, FZ]) rotate([0, 0, 90]) ap_furn_cafe_table(ap_REDFOOD());
translate([-19.6, 11.8, FZ]) rotate([0, 0, -70]) ap_furn_cafe_table(ap_REDFOOD());
translate([-16.2, 11.8, FZ]) rotate([0, 0, 20]) ap_furn_cafe_table(ap_REDFOOD());
translate([-13.2, 10.0, FZ]) rotate([0, 0, 90]) ap_prop_planter(3.0);

// ---- 中央咖啡岛：柜台 + 吊牌 + 3 桌 ----
translate([-7, 12.5, FZ]) cafe_01();
translate([-7, 13.5, FZ]) ap_prop_hang_sign("COFFEE", ap_BROWNC(), false);
translate([-9.8, 14.8, FZ]) rotate([0, 0, 30]) ap_furn_cafe_table();
translate([-6.2, 15.4, FZ]) rotate([0, 0, -45]) ap_furn_cafe_table();
translate([-3.4, 13.6, FZ]) rotate([0, 0, 80]) ap_furn_cafe_table();

// ---- 东零售街三连铺（front y=8.4，背墙 y=13.9） ----
// SHOP 便利店 x∈[3,11]
translate([3, 13.9, FZ]) ap_wall_solid_seg(8.0, SHOPWALL);
translate([3, 8.4, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(5.5, SHOPWALL);
translate([11, 8.4, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(5.5, SHOPWALL);
translate([7, 8.5, FZ]) ap_prop_shop_portal(7.6, "SHOP", ap_AIRBLUE());
translate([4.4, 9.4, FZ]) rotate([0, 0, 180]) shop_01();
translate([5.9, 10.8, FZ]) ap_furn_gondola();
translate([8.7, 10.8, FZ]) ap_furn_gondola();
translate([5.9, 12.3, FZ]) ap_furn_gondola();
translate([8.7, 12.3, FZ]) ap_furn_gondola();
translate([4.0, 13.4, FZ]) ap_furn_fridge_case([0.80, 0.30, 0.28]);
translate([5.1, 13.4, FZ]) ap_furn_fridge_case([0.25, 0.45, 0.75]);
translate([6.2, 13.4, FZ]) ap_furn_fridge_case([0.30, 0.60, 0.45]);
translate([9.9, 13.3, FZ]) ap_furn_freezer_chest();
translate([10.6, 9.6, FZ]) rotate([0, 0, -90]) ap_furn_mag_rack();
// BOOKS 书店 x∈[12,19]
translate([12, 13.9, FZ]) ap_wall_solid_seg(7.0, [0.62, 0.50, 0.38]);
translate([12, 8.4, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(5.5, [0.62, 0.50, 0.38]);
translate([19, 8.4, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(5.5, [0.62, 0.50, 0.38]);
translate([15.5, 8.5, FZ]) ap_prop_shop_portal(6.6, "BOOKS", ap_BROWNC());
translate([13.4, 9.4, FZ]) rotate([0, 0, 180]) book_01();
translate([13.4, 13.45, FZ]) ap_prop_bookshelf_tall();
translate([14.5, 13.45, FZ]) ap_prop_bookshelf_tall();
translate([15.6, 13.45, FZ]) ap_prop_bookshelf_tall();
translate([16.7, 13.45, FZ]) ap_prop_bookshelf_tall();
translate([18.6, 10.6, FZ]) rotate([0, 0, -90]) ap_prop_bookshelf_tall();
translate([18.6, 12.0, FZ]) rotate([0, 0, -90]) ap_prop_bookshelf_tall();
translate([15.5, 11.0, FZ]) rotate([0, 0, 8]) ap_furn_book_table();
translate([12.4, 10.8, FZ]) rotate([0, 0, 90]) ap_furn_mag_rack();
// GIFTS 礼品店 x∈[20,28]
translate([20, 13.9, FZ]) ap_wall_solid_seg(8.0, TEALC);
translate([20, 8.4, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(5.5, TEALC);
translate([28, 8.4, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(5.5, TEALC);
translate([24, 8.5, FZ]) ap_prop_shop_portal(7.6, "GIFTS", TEALC);
translate([21.4, 9.4, FZ]) rotate([0, 0, 180]) gift_01();
translate([23.4, 11.2, FZ]) ap_furn_display_case();
translate([26.2, 11.2, FZ]) ap_furn_display_case(false);
translate([26.2, 11.2, 0.72 + FZ]) rotate([0, 0, -15]) scale([0.085, 0.085, 0.085])
    ap_veh_airliner([0.92, 0.78, 0.20], [0.78, 0.25, 0.20]);   // 玻璃罩里的飞机模型
translate([24.8, 13.2, FZ]) ap_furn_gondola();
translate([21.2, 12.8, FZ]) ap_furn_freezer_chest();
translate([27.3, 9.8, FZ]) rotate([0, 0, -90]) ap_furn_mag_rack();

// ---- 卫生间（东北空侧）：实墙围合 + 男/女门 + 室内 + 饮水器 ----
translate([24, 22, FZ]) ap_wall_solid_seg(6.0);
translate([24, 16, FZ]) ap_wall_solid_seg(6.0);
translate([24, 19, FZ]) ap_wall_solid_seg(6.0);
translate([30, 16, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(6.0);
translate([24, 16, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(0.85);
translate([24, 17.95, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(2.1);
translate([24, 21.15, FZ]) rotate([0, 0, 90]) ap_wall_solid_seg(0.85);
translate([24, 17.4, FZ]) rotate([0, 0, -90]) toilet_01();
translate([24, 20.6, FZ]) rotate([0, 0, -90]) toilet_02();
translate([24.7, 17.6, FZ]) ap_furn_wc_interior();
translate([29.3, 20.4, FZ]) rotate([0, 0, 180]) ap_furn_wc_interior();
translate([25.0, 15.4, FZ]) ap_prop_fountain();

// ---- 员工办公室（东侧陆侧）：玻璃隔断 + 2 工位 + 机柜 ----
translate([25, -6, FZ]) rotate([0, 0, 90]) ap_part_glass_wall(5.0, 1.9, 3.1);
translate([25, -6, FZ]) ap_part_glass_wall(5.0);
translate([25, -1, FZ]) ap_part_glass_wall(5.0);
translate([26.7, -2.4, FZ]) rotate([0, 0, 180]) staff_01();
translate([28.7, -2.4, FZ]) rotate([0, 0, 180]) staff_02();
translate([29.3, -5.2, FZ]) rotate([0, 0, -90]) ap_prop_server_rack();

// ---- 陆侧大厅：问询台 + 航显 + ATM + 售货机 + 长椅 ----
translate([-2.5, -7.5, FZ]) info_01();
translate([6.5, -7.0, FZ]) ap_prop_fids();
translate([-29.55, -7.0, FZ]) rotate([0, 0, 90]) atm_01();
translate([29.55, -8.6, FZ]) rotate([0, 0, -90]) vending_04();
translate([29.55, -9.8, FZ]) rotate([0, 0, -90]) vending_05();
translate([10.0, -8.5, FZ]) ap_furn_bench_row();
translate([14.5, -8.5, FZ]) ap_furn_bench_row();

// ---- 室内点缀 ----
translate([-28.5, -10.5, FZ]) ap_prop_palm();
translate([-8, -6, FZ]) ap_prop_palm();
translate([20, -7, FZ]) ap_prop_palm();
translate([28.3, -10.8, FZ]) ap_prop_palm();
translate([-24.5, 21.5, FZ]) ap_prop_palm();
translate([-12.8, 17.5, FZ]) ap_prop_palm();
translate([18.5, 22.0, FZ]) ap_prop_palm();
translate([1.5, 14.5, FZ]) ap_prop_palm();
translate([28.9, 6.2, FZ]) ap_prop_palm();
translate([-13.6, 15.6, FZ]) ap_prop_bins();
translate([7, 17, FZ]) ap_prop_bins();
translate([3, -9, FZ]) ap_prop_bins();
translate([-12, -5, FZ]) ap_prop_bins();
translate([19.5, 6.0, FZ]) ap_prop_bins();
// 广告灯箱 + 行李推车 + 登机口间绿植带（填充大空间）
translate([0.8, 10.8, FZ]) rotate([0, 0, 90]) ap_prop_ad_totem([0.90, 0.55, 0.25]);
translate([-2.5, 21.0, FZ]) ap_prop_ad_totem([0.30, 0.60, 0.80]);
translate([20.5, 17.8, FZ]) rotate([0, 0, 90]) ap_prop_ad_totem([0.45, 0.68, 0.38]);
translate([8.0, -4.5, FZ]) rotate([0, 0, 90]) ap_prop_ad_totem([0.70, 0.45, 0.72]);
translate([-14.5, -7.5, FZ]) ap_prop_ad_totem([0.25, 0.45, 0.75]);
translate([-21.5, -10.2, FZ]) rotate([0, 0, 15]) ap_prop_trolley_row();
translate([17.0, -10.6, FZ]) rotate([0, 0, -75]) ap_prop_trolley_row();
translate([-16.0, 24.5, FZ]) ap_prop_planter(3.0);
translate([0.0, 24.5, FZ]) ap_prop_planter(3.0);
translate([16.0, 24.5, FZ]) ap_prop_planter(3.0);
translate([-26.5, 24.0, FZ]) ap_prop_palm();
translate([8.0, 23.0, FZ]) ap_prop_palm();
translate([26.5, 24.5, FZ]) ap_prop_palm();
translate([18.0, -7.2, FZ]) ap_prop_fids();
translate([5.5, 19.5, FZ]) ap_prop_bins();
translate([-25.5, 6.2, FZ]) ap_prop_bins();

// ---- 停机坪：3 客机 + 双客梯 + 地勤车队 + 灯杆 + 风向袋 ----
translate([-14, 36, FZ]) rotate([0, 0, 174]) ap_veh_airliner();
translate([8, 36.5, FZ]) rotate([0, 0, 188]) ap_veh_airliner([0.94, 0.95, 0.96], [0.28, 0.58, 0.42]);
translate([27, 34, FZ]) rotate([0, 0, -24]) scale([0.72, 0.72, 0.72])
    ap_veh_airliner([0.92, 0.78, 0.20], [0.30, 0.34, 0.42]);
translate([-17.9, 33.1, FZ]) ap_veh_stairs_truck();
translate([4.35, 32.6, FZ]) ap_veh_stairs_truck();
translate([-22, 31.5, FZ]) rotate([0, 0, 195]) ap_veh_baggage_tug();
translate([-23.6, 31.0, FZ]) rotate([0, 0, 185]) ap_veh_baggage_cart();
translate([-25.1, 30.8, FZ]) rotate([0, 0, 176]) ap_veh_baggage_cart([0.35, 0.55, 0.75], [0.72, 0.40, 0.26]);
translate([12.5, 33.5, FZ]) rotate([0, 0, 10]) ap_veh_fuel_truck();
translate([-4, 30.6, FZ]) ap_veh_taxi();
translate([20, 30.2, FZ]) rotate([0, 0, 180]) ap_veh_bus([0.90, 0.90, 0.92]);
translate([-16, 31.8, FZ]) ap_prop_cone();
translate([2, 32, FZ]) ap_prop_cone();
translate([9.5, 33.2, FZ]) ap_prop_cone();
translate([-6, 30.2, FZ]) ap_prop_cone();
translate([-26, 29.6, FZ]) ap_prop_light_mast();
translate([-2, 29.6, FZ]) ap_prop_light_mast();
translate([22, 29.6, FZ]) ap_prop_light_mast();
translate([39, 43, FZ]) ap_prop_windsock();

// ---- 东侧服务区：集装箱 + 服务车 + 围栏 ----
translate([34, -7.5, FZ]) ap_prop_container();
translate([35.7, -7.2, FZ]) rotate([0, 0, 12]) ap_prop_container([0.62, 0.50, 0.36]);
translate([34.8, -5.8, FZ]) rotate([0, 0, -6]) ap_prop_container([0.40, 0.55, 0.62]);
translate([33.5, 4, FZ]) rotate([0, 0, 90]) ap_veh_car([0.90, 0.90, 0.92]);
translate([36, 14, FZ]) ap_prop_light_mast();
translate([30, -12, FZ]) ap_prop_fence(12);
translate([-42, 27.8, FZ]) ap_prop_fence(12);

// ---- 陆侧：候车亭 + 公交 + 出租 + 行道树/绿篱/路灯/长椅 ----
translate([8, -13.7, FZ]) ap_prop_bus_shelter();
translate([8.4, -16.4, FZ]) rotate([0, 0, 180]) ap_veh_bus();
translate([-13.5, -16.4, FZ]) rotate([0, 0, 180]) ap_veh_taxi();
translate([-9, -16.4, FZ]) rotate([0, 0, 180]) ap_veh_taxi();
translate([15, -19.1, FZ]) ap_veh_car([0.60, 0.62, 0.66]);
translate([30, -16.4, FZ]) rotate([0, 0, 180]) ap_veh_car([0.90, 0.90, 0.92]);
translate([-30, -19.1, FZ]) ap_veh_car([0.22, 0.30, 0.45]);
translate([-36, -16.4, FZ]) rotate([0, 0, 180]) ap_veh_car([0.85, 0.55, 0.25]);
translate([-34, -13.9, FZ]) ap_prop_tree();
translate([-25, -13.9, FZ]) ap_prop_tree();
translate([-8, -13.9, FZ]) ap_prop_tree();
translate([20, -13.9, FZ]) ap_prop_tree();
translate([32, -13.9, FZ]) ap_prop_tree();
translate([-26, -12.7, FZ]) ap_prop_hedge();
translate([-9, -12.7, FZ]) ap_prop_hedge();
translate([6, -12.7, FZ]) ap_prop_hedge();
translate([20, -12.7, FZ]) ap_prop_hedge();
translate([26, -12.7, FZ]) ap_prop_hedge();
translate([-12, -13.0, FZ]) ap_prop_bench_out();
translate([3.5, -13.0, FZ]) ap_prop_bench_out();
translate([17.5, -13.0, FZ]) ap_prop_bench_out();
translate([-38, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();
translate([-26, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();
translate([-14, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();
translate([2, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();
translate([14, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();
translate([26, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();
translate([38, -15.7, FZ]) rotate([0, 0, 180]) ap_prop_lamp_post();

// ---- 停车场：7 辆车 + 道闸 + P 牌 ----
translate([-36.8, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car(ap_carc5(0));
translate([-32.0, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car(ap_carc5(3));
translate([-29.6, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car(ap_carc5(1));
translate([-22.4, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car(ap_carc5(4));
translate([-17.6, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car(ap_carc5(2));
translate([-10.4, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car([0.45, 0.60, 0.42]);
translate([-5.6, -28.6, FZ]) rotate([0, 0, 90]) ap_veh_car(ap_carc5(0));
translate([1.2, -21.2, FZ]) ap_prop_barrier_gate();
translate([3.4, -22.3, FZ]) ap_prop_p_sign();

// ---- 东南草地 / 西侧花园 / 西北草地 ----
translate([10, -27, FZ]) ap_prop_tree();
translate([16, -24, FZ]) ap_prop_tree();
translate([24, -28.5, FZ]) ap_prop_tree();
translate([32, -23.5, FZ]) ap_prop_tree();
translate([38, -29, FZ]) ap_prop_tree();
translate([12, -21.2, FZ]) ap_prop_hedge();
translate([22, -21.2, FZ]) ap_prop_hedge();
translate([34, -21.2, FZ]) ap_prop_hedge();
translate([-36, -6, FZ]) ap_prop_tree();
translate([-38, 4, FZ]) ap_prop_tree();
translate([-35, 14, FZ]) ap_prop_tree();
translate([-37, 22, FZ]) ap_prop_tree();
translate([-33, 0, FZ]) ap_prop_palm();
translate([-36, -10, FZ]) ap_prop_hedge();
translate([-34, 8, FZ]) ap_prop_hedge();
translate([-36, 32, FZ]) ap_prop_tree();
translate([-39, 38, FZ]) ap_prop_tree();
translate([-33, 42, FZ]) ap_prop_tree();
translate([-37, 45, FZ]) ap_prop_tree();
