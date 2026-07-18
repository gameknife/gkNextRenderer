// office.scad —— StudioSim 游戏工作室办公室（参考低多边形等距办公室示意图重制）
//
// StudioSim::OfficeMap 使用以下现行命名约定：
//   功能点位 = 具名 user module（desk_<role>_<id> / meet_seat_<id> / pantry_<id> /
//   lounge_<id>），加载后节点名 = module 名，供 OfficeMap 解析为锚点。
//   墙体/隔断/家具用非点位命名（wall_* / part_* / furn_* / prop_*），OfficeMap 自动忽略。
//   注意：锚点调用必须 translate(...) 在 rotate(...) 之外，保证 WorldTranslation = 点位坐标。
//
// OpenSCAD 为 Z-up：地板在 XY 平面、Z 为高度；引擎加载时转 Y-up。
// 地板顶面 z = 0.15（= EmployeeSystem kGroundY，勿改）。
// 游戏相机固定从南向北俯视 45°，因此南墙做 0.5 矮墙（切墙视角，仍阻挡 NavGrid），
// 北/东/西墙全高 2.6。NavGrid：cell 0.5 / agent 半径 0.3 / 步高 0.35——
// 高于 0.35 的家具都会阻挡寻路，各分区门洞 >= 1.3，主过道 >= 0.9。
//
// 分区：西北=会议室(玻璃)、北墙=茶水间/打印置物带、东北=制作人办公室(玻璃)、
//       西南=老板办公室(玻璃, desk_boss_01)、中央=开放工位、南中=前台+休息区、东南=储物/机房角。

use <lib/kit_office.scad>

$fn = 16;

// ================= 功能点位（锚点，OfficeMap 解析；调用处 translate 必须在 rotate 外层） =================
module desk_engineer_01() of_furn_workstation(mug = [0.32, 0.62, 0.85], plant = true, crot = 8);
module desk_engineer_02() of_furn_workstation(dual = true, mug = [0.85, 0.45, 0.25], crot = -6);
module desk_artist_01()   of_furn_workstation(tablet = true, screen = [0.88, 0.72, 0.82], mug = [0.92, 0.60, 0.30], plant = true, crot = 14);
module desk_designer_01() of_furn_workstation(laptop = true, mug = [0.55, 0.45, 0.85], crot = -10);
module desk_qa_01()       of_furn_workstation(dual = true, sticky = true, screen = [0.65, 0.85, 0.70], mug = [0.85, 0.30, 0.30], crot = 5);
module desk_pm_01()       of_furn_workstation(laptop = true, mug = [0.90, 0.55, 0.75], plant = true, crot = -4);
module desk_boss_01()     of_furn_exec_desk();

module meet_seat_01() of_furn_conf_chair();
module meet_seat_02() of_furn_conf_chair();
module meet_seat_03() of_furn_conf_chair();
module meet_seat_04() of_furn_conf_chair();
module meet_seat_05() of_furn_conf_chair();
module meet_seat_06() of_furn_conf_chair();

// 茶水吧台：白柜体 + 木台面 + 咖啡机/马克杯/果盘
module pantry_01()
{
    color(of_WHITEC()) translate([0, 0, 0.45]) cube([2.40, 0.60, 0.90], center = true);
    color(of_DARKMETC()) translate([0, 0, 0.03]) cube([2.34, 0.55, 0.06], center = true);
    color(of_OAKC()) translate([0, 0, 0.925]) cube([2.50, 0.66, 0.05], center = true);
    for (i = [0 : 3])
        color([0.80, 0.81, 0.83]) translate([-0.90 + i * 0.60, -0.305, 0.50]) cube([0.50, 0.012, 0.70], center = true);
    color(of_DARKMETC()) for (i = [0 : 3]) translate([-0.90 + i * 0.60, -0.315, 0.80]) cube([0.16, 0.02, 0.025], center = true);
    // 咖啡机
    color([0.20, 0.22, 0.26]) translate([-0.70, 0.08, 1.14]) cube([0.34, 0.36, 0.38], center = true);
    color(of_METALC()) translate([-0.70, -0.08, 1.05]) cube([0.20, 0.10, 0.06], center = true);
    color([0.30, 0.20, 0.12]) translate([-0.70, -0.06, 1.00]) cylinder(h = 0.05, r = 0.035, $fn = 10);
    // 马克杯 + 果盘 + 水壶
    color([0.85, 0.45, 0.30]) translate([-0.15, 0.10, 0.95]) cylinder(h = 0.10, r = 0.045, $fn = 12);
    color([0.40, 0.62, 0.80]) translate([0.02, -0.06, 0.95]) cylinder(h = 0.10, r = 0.045, $fn = 12);
    color([0.55, 0.75, 0.50]) translate([0.18, 0.12, 0.95]) cylinder(h = 0.10, r = 0.045, $fn = 12);
    color(of_WHITEC()) translate([0.65, 0.02, 0.95]) cylinder(h = 0.06, r = 0.13, $fn = 14);
    color([0.90, 0.55, 0.25]) translate([0.60, 0.05, 1.03]) sphere(r = 0.05);
    color([0.80, 0.28, 0.25]) translate([0.70, -0.02, 1.03]) sphere(r = 0.05);
    color([0.55, 0.75, 0.35]) translate([0.66, 0.09, 1.03]) sphere(r = 0.05);
    color(of_METALC()) translate([1.00, 0.05, 0.95]) cylinder(h = 0.22, r1 = 0.075, r2 = 0.055, $fn = 12);
}

// 休息区茶几（锚点）：木几 + 杂志 + 小绿植
module lounge_01()
{
    color(of_DARKMETC()) for (sx = [-1, 1], sy = [-1, 1])
        translate([0.40 * sx, 0.22 * sy, 0]) cylinder(h = 0.34, r = 0.022, $fn = 10);
    color(of_OAKDARKC()) translate([0, 0, 0.365]) cube([0.96, 0.60, 0.05], center = true);
    color([0.70, 0.30, 0.28]) translate([-0.18, 0.05, 0.40]) rotate([0, 0, 12]) cube([0.26, 0.19, 0.012], center = true);
    color([0.30, 0.45, 0.70]) translate([-0.12, -0.02, 0.412]) rotate([0, 0, -6]) cube([0.26, 0.19, 0.012], center = true);
    color(of_WHITEC()) translate([0.28, -0.08, 0.39]) cylinder(h = 0.06, r = 0.055, $fn = 12);
    color(of_PLANTC()) translate([0.28, -0.08, 0.50]) sphere(r = 0.09);
}

// ======================== 布局 ========================
FZ = 0.15;   // 地板顶面

of_office_floor();

// ---- 外墙（北/东/西全高，南矮墙留入口 x∈[-1.75,1.75]） ----
translate([0, 8.875, 1.45]) of_wall_segment(24);
translate([-11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([ 11.875, 0, 1.45]) rotate([0, 0, 90]) of_wall_segment(18);
translate([-6.875, -8.875, 0.40]) of_wall_knee(10.25);
translate([ 6.875, -8.875, 0.40]) of_wall_knee(10.25);
translate([0, -8.55, FZ]) of_prop_mat();

// ---- 会议室（西北，玻璃隔断；门洞在南墙东端） ----
translate([-11.75, 3.1, FZ]) of_part_glass_wall(7.30, 5.45, 6.85);
translate([-4.45, 3.1, FZ]) rotate([0, 0, 90]) of_part_glass_wall(5.65);
translate([-8.1, 5.85, FZ]) of_prop_rug(5.8, 3.8, [0.46, 0.52, 0.56]);
translate([-8.1, 5.85, FZ]) of_furn_conf_table();
translate([-9.5, 4.60, FZ]) rotate([0, 0, 180]) meet_seat_01();
translate([-8.1, 4.55, FZ]) rotate([0, 0, 172]) meet_seat_02();
translate([-6.7, 4.60, FZ]) rotate([0, 0, 188]) meet_seat_03();
translate([-9.5, 7.10, FZ]) rotate([0, 0, 6]) meet_seat_04();
translate([-8.1, 7.15, FZ]) meet_seat_05();
translate([-6.7, 7.10, FZ]) rotate([0, 0, -8]) meet_seat_06();
translate([-11.70, 5.85, 1.55 + FZ]) rotate([0, 0, 90]) of_prop_projector_screen();
translate([-8.0, 8.66, 1.70 + FZ]) of_prop_whiteboard();
translate([-5.4, 8.46, FZ]) of_prop_credenza();
translate([-5.4, 8.70, 2.30 + FZ]) of_prop_frame(0.45, 0.34, [0.75, 0.55, 0.40]);
translate([-11.15, 8.15, FZ]) of_prop_plant_tall();

// ---- 制作人办公室（东北，玻璃隔断；门洞在南墙西端） ----
translate([5.05, 3.1, FZ]) of_part_glass_wall(6.70, 0.65, 1.95);
translate([5.05, 3.1, FZ]) rotate([0, 0, 90]) of_part_glass_wall(5.65);
translate([8.7, 5.9, FZ]) of_prop_rug(3.6, 2.8, [0.30, 0.45, 0.44]);
translate([8.7, 6.1, FZ]) rotate([0, 0, 180]) desk_pm_01();
translate([8.0, 4.85, FZ]) rotate([0, 0, 170]) of_furn_conf_chair();
translate([9.45, 4.85, FZ]) rotate([0, 0, 192]) of_furn_conf_chair();
translate([11.42, 6.6, FZ]) rotate([0, 0, -90]) of_prop_binder_shelf();
translate([11.15, 3.75, FZ]) of_prop_plant_tall();
translate([8.7, 8.70, 2.10 + FZ]) of_prop_frame(0.55, 0.40, [0.40, 0.62, 0.58]);
translate([7.7, 8.70, 2.05 + FZ]) of_prop_frame(0.40, 0.30, [0.80, 0.60, 0.35]);

// ---- 北墙公共带：双开门 + 饮水机 + 茶水吧台 + 冰箱 + 文件夹架 ----
translate([-2.4, 8.69, FZ]) of_prop_door_double();
translate([-0.75, 8.50, FZ]) of_prop_water_cooler();
translate([1.5, 8.32, FZ]) pantry_01();
translate([3.12, 8.43, FZ]) of_prop_fridge();
translate([4.24, 8.55, FZ]) of_prop_binder_shelf();
translate([-0.25, 8.72, 2.35 + FZ]) of_prop_clock();
translate([1.2, 8.70, 2.20 + FZ]) of_prop_frame(0.42, 0.32, [0.50, 0.65, 0.80]);
translate([1.85, 8.70, 2.12 + FZ]) of_prop_frame(0.34, 0.26, [0.85, 0.70, 0.45]);

// ---- 老板办公室（西南，玻璃隔断；门洞在北墙东端） ----
translate([-11.75, -3.55, FZ]) of_part_glass_wall(6.70, 4.85, 6.15);
translate([-5.05, -8.75, FZ]) rotate([0, 0, 90]) of_part_glass_wall(5.20);
translate([-8.3, -6.1, FZ]) of_prop_rug(3.8, 2.8, [0.50, 0.34, 0.30]);
translate([-8.4, -6.3, FZ]) desk_boss_01();
translate([-6.35, -4.75, FZ]) rotate([0, 0, -45]) of_furn_armchair();
translate([-11.42, -7.55, FZ]) rotate([0, 0, 90]) of_prop_bookshelf_tall();
translate([-11.72, -4.55, 1.80 + FZ]) rotate([0, 0, 90]) of_prop_certificates();
translate([-11.72, -6.35, 1.55 + FZ]) rotate([0, 0, 90]) of_prop_window_blinds();
translate([-5.65, -8.10, FZ]) of_prop_plant_tall();

// ---- 开放工位区（中央）：北排 3 + 南排 2 + 打印角 + 绿植隔断 ----
translate([-9.0, 1.0, FZ]) rotate([0, 0, 180]) desk_engineer_01();
translate([-6.9, 1.0, FZ]) rotate([0, 0, 180]) desk_engineer_02();
translate([-3.9, 1.0, FZ]) rotate([0, 0, 180]) desk_artist_01();
translate([-8.0, -2.0, FZ]) desk_designer_01();
translate([-5.3, -2.0, FZ]) desk_qa_01();
translate([-7.4, -0.5, FZ]) of_prop_planter(2.4);
translate([-4.3, -0.5, FZ]) of_prop_planter(1.8);
translate([-2.55, -2.05, FZ]) rotate([0, 0, 180]) of_prop_printer_island();
translate([-1.75, -1.95, FZ]) of_prop_bins();

// ---- 中央休闲角：桌上足球 + 懒人沙发 ----
translate([1.9, 0.4, FZ]) rotate([0, 0, 16]) of_prop_foosball();
translate([3.5, -1.0, FZ]) of_prop_beanbag([0.90, 0.55, 0.25]);
translate([4.25, -1.7, FZ]) of_prop_beanbag([0.25, 0.60, 0.55]);

// ---- 前台 + 休息区（南中） ----
translate([2.75, -6.45, FZ]) rotate([0, 0, -90]) of_prop_reception();
translate([-2.55, -6.35, FZ]) of_prop_rug(3.5, 2.6, [0.80, 0.76, 0.68]);
translate([-3.70, -6.30, FZ]) rotate([0, 0, 90]) of_furn_sofa();
translate([-2.40, -7.55, FZ]) rotate([0, 0, 180]) of_furn_sofa();
translate([-2.2, -6.2, FZ]) lounge_01();
translate([-4.15, -7.75, FZ]) of_prop_floor_lamp();
translate([-3.55, -8.35, FZ]) of_prop_coat_stand();

// ---- 东南储物 / 机房角 ----
translate([11.42, -3.95, FZ]) rotate([0, 0, -90]) of_prop_locker();
translate([11.42, -4.85, FZ]) rotate([0, 0, -90]) of_prop_locker();
translate([11.42, -5.75, FZ]) rotate([0, 0, -90]) of_prop_locker();
translate([11.42, -6.65, FZ]) rotate([0, 0, -90]) of_prop_locker();
translate([10.45, -8.25, FZ]) rotate([0, 0, 180]) of_prop_server_rack();
translate([9.55, -8.25, FZ]) rotate([0, 0, 180]) of_prop_server_rack();
translate([8.05, -8.20, FZ]) of_prop_boxes();
translate([6.35, -8.32, FZ]) rotate([0, 0, 180]) of_prop_shelf_unit();
translate([11.25, -7.65, FZ]) of_prop_plant_tall();

// ---- 东墙 / 西墙装饰（主区段） ----
translate([11.72, 2.45, 2.35 + FZ]) rotate([0, 0, -90]) of_prop_clock();
translate([11.72, 0.85, 1.60 + FZ]) rotate([0, 0, -90]) of_prop_window_blinds();
translate([11.72, -1.45, 1.60 + FZ]) rotate([0, 0, -90]) of_prop_window_blinds();
translate([11.72, -2.85, 1.85 + FZ]) rotate([0, 0, -90]) of_prop_frame(0.50, 0.38, [0.62, 0.45, 0.62]);
translate([-11.42, 0.2, FZ]) rotate([0, 0, 90]) of_prop_bookshelf_tall();
translate([-11.20, 2.30, FZ]) of_prop_plant_tall();
translate([-11.72, 1.40, 1.90 + FZ]) rotate([0, 0, 90]) of_prop_frame(0.50, 0.38, [0.50, 0.62, 0.50]);

// ---- 入口点缀 ----
translate([1.95, -8.15, FZ]) of_prop_plant_tall();
