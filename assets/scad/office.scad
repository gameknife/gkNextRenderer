// office.scad —— StudioSim 游戏工作室办公室（开放式 + 功能分区）
//
// 命名约定见 docs/StudioSim-MVP-Plan.md §6：
//   功能点位 = 具名 user module（desk_<role>_<id> / meet_seat_<id> / pantry_<id> /
//   lounge_<id>），加载后节点名 = module 名，供 OfficeMap 解析为锚点。
//   墙体/隔断/家具用非点位命名（wall_* / part_* / *_furn 等），OfficeMap 自动忽略。
//
// OpenSCAD 为 Z-up：地板在 XY 平面、Z 为高度；引擎加载时转 Y-up。
// 整层 24 x 18，外墙围合（南墙留主入口），内部用半透明玻璃矮隔断划分：
//   左上 = 茶水间，右上 = 会议室，右下 = 洽谈室，中央 = 开放工位区。

$fn = 20;

// ---- 配色 ----
FLOORC  = [0.86, 0.86, 0.88];
WALLC   = [0.92, 0.90, 0.86];
GLASSC  = [0.55, 0.72, 0.85, 0.40];   // 半透明玻璃隔断
DESKC   = [0.55, 0.40, 0.25];
CUBEC   = [0.50, 0.55, 0.62];         // 矮 cubicle 隔板
MEETC   = [0.30, 0.50, 0.75];
TABLEC  = [0.45, 0.32, 0.20];
PANTRYC = [0.85, 0.80, 0.45];
LOUNGEC = [0.65, 0.45, 0.70];
CHAIRC  = [0.28, 0.30, 0.34];
PLANTC  = [0.30, 0.55, 0.30];

// ================= 静态环境（非锚点） =================
module office_floor() color(FLOORC) cube([24, 18, 0.3], center = true);

// 外墙（高 2.6，南墙留主入口 x∈[-1.75,1.75]）
module wall_north()       color(WALLC) cube([24,    0.25, 2.6], center = true);
module wall_east()        color(WALLC) cube([0.25,  18,   2.6], center = true);
module wall_west()        color(WALLC) cube([0.25,  18,   2.6], center = true);
module wall_south_left()  color(WALLC) cube([10.25, 0.25, 2.6], center = true);
module wall_south_right() color(WALLC) cube([10.25, 0.25, 2.6], center = true);

// 玻璃矮隔断（高 1.3，半透明，各功能区都留开口保持开放感）
module part_meet_s()   color(GLASSC) cube([6.25, 0.15, 1.3], center = true);
module part_meet_w()   color(GLASSC) cube([0.15, 4.25, 1.3], center = true);
module part_pantry_s() color(GLASSC) cube([5.0,  0.15, 1.3], center = true);
module part_pantry_e() color(GLASSC) cube([0.15, 4.25, 1.3], center = true);
module part_lounge_n() color(GLASSC) cube([4.25, 0.15, 1.3], center = true);
module part_lounge_w() color(GLASSC) cube([0.15, 4.25, 1.3], center = true);

// 开放工位区的矮 cubicle 隔板（高 0.55，暗示工位但不封闭）
module cubicle_mid() color(CUBEC) cube([8.5,  0.12, 0.55], center = true);
module cubicle_v1()  color(CUBEC) cube([0.12, 3.0,  0.55], center = true);
module cubicle_v2()  color(CUBEC) cube([0.12, 3.0,  0.55], center = true);

// 家具 / 装饰（非锚点，可复用）
module furn_chair() color(CHAIRC) cube([0.5, 0.5, 0.5], center = true);
module plant()     color(PLANTC) cylinder(h = 0.7, r = 0.22);
module furn_table()     color(TABLEC)  cube([2.6, 1.3, 0.75], center = true);
module furn_counter() color(PANTRYC) cube([4.5, 0.7, 0.95], center = true);
module furn_sofa()    color(LOUNGEC) cube([2.2, 0.85, 0.65], center = true);

// ================= 功能点位（锚点，OfficeMap 解析） =================
// 工位（按职位）
module desk_engineer_01() color(DESKC) cube([1.6, 0.9, 0.75], center = true);
module desk_engineer_02() color(DESKC) cube([1.6, 0.9, 0.75], center = true);
module desk_artist_01()   color(DESKC) cube([1.6, 0.9, 0.75], center = true);
module desk_designer_01() color(DESKC) cube([1.6, 0.9, 0.75], center = true);
module desk_pm_01()       color(DESKC) cube([1.6, 0.9, 0.75], center = true);
module desk_qa_01()       color(DESKC) cube([1.6, 0.9, 0.75], center = true);
// 会议座 / 茶水间 / 洽谈室
module meet_seat_01() color(MEETC)          cube([0.5, 0.5, 0.5], center = true);
module meet_seat_02() color(MEETC)          cube([0.5, 0.5, 0.5], center = true);
module meet_seat_03() color(MEETC)          cube([0.5, 0.5, 0.5], center = true);
module meet_seat_04() color(MEETC)          cube([0.5, 0.5, 0.5], center = true);
module meet_seat_05() color(MEETC)          cube([0.5, 0.5, 0.5], center = true);
module meet_seat_06() color(MEETC)          cube([0.5, 0.5, 0.5], center = true);
module pantry_01()    color([0.80,0.78,0.5])cube([0.9, 0.9, 0.85], center = true);
module lounge_01()    color([0.70,0.55,0.72])cube([1.3, 1.3, 0.40], center = true);

// ======================== 布局 ========================
office_floor();

// 外墙
translate([0,      8.875, 1.45]) wall_north();
translate([11.875, 0,     1.45]) wall_east();
translate([-11.875,0,     1.45]) wall_west();
translate([-6.875, -8.875,1.45]) wall_south_left();
translate([6.875,  -8.875,1.45]) wall_south_right();

// 会议室（右上）：南、西玻璃隔断，西南角留开口
translate([8.625, 3.0,   0.80]) part_meet_s();
translate([4.0,   6.625, 0.80]) part_meet_w();
translate([8.0,   6.0,   0.525]) furn_table();
translate([6.8,   4.85,  0.40]) meet_seat_01();
translate([8.0,   4.85,  0.40]) meet_seat_02();
translate([9.2,   4.85,  0.40]) meet_seat_03();
translate([6.8,   7.15,  0.40]) meet_seat_04();
translate([8.0,   7.15,  0.40]) meet_seat_05();
translate([9.2,   7.15,  0.40]) meet_seat_06();

// 茶水间（左上）：南、东玻璃隔断，东南角留开口
translate([-9.0, 3.0,   0.80]) part_pantry_s();
translate([-5.0, 6.625, 0.80]) part_pantry_e();
translate([-8.5, 8.15,  0.625]) furn_counter();
translate([-8.0, 5.2,   0.575]) pantry_01();

// 洽谈室（右下）：北、西玻璃隔断，西北角留开口
translate([9.625, -3.0,   0.80]) part_lounge_n();
translate([6.0,   -5.875, 0.80]) part_lounge_w();
translate([9.0,   -7.7,   0.475]) furn_sofa();
translate([9.0,   -5.4,   0.35]) lounge_01();

// 开放工位区（中央）：两排工位 + 矮 cubicle 隔板
translate([-6.0, -0.5, 0.425]) cubicle_mid();
translate([-7.5, -0.5, 0.425]) cubicle_v1();
translate([-4.5, -0.5, 0.425]) cubicle_v2();
// 北排
translate([-9.0, 1.0, 0.525]) desk_engineer_01();
translate([-6.0, 1.0, 0.525]) desk_engineer_02();
translate([-3.0, 1.0, 0.525]) desk_artist_01();
translate([-9.0, 1.8, 0.40]) furn_chair();
translate([-6.0, 1.8, 0.40]) furn_chair();
translate([-3.0, 1.8, 0.40]) furn_chair();
// 南排
translate([-9.0, -2.0, 0.525]) desk_designer_01();
translate([-6.0, -2.0, 0.525]) desk_pm_01();
translate([-3.0, -2.0, 0.525]) desk_qa_01();
translate([-9.0, -2.8, 0.40]) furn_chair();
translate([-6.0, -2.8, 0.40]) furn_chair();
translate([-3.0, -2.8, 0.40]) furn_chair();

// 入口绿植点缀
translate([-2.3, -7.6, 0.50]) plant();
translate([2.3,  -7.6, 0.50]) plant();
