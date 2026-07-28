// coldwar/riverland_1km.scad —— 1km x 1km 冷战生存开放地图
// 北部山脉 → 河流蜿蜒向南；主公路东西横贯（混凝土桥过河）；支路引向各废弃据点。
// 据点：西部村庄 / 桥西加油站 / 桥东碉堡 / 东北高地军事基地 / 东南小镇 /
//       河畔工厂 / 西山通信站 / 北坡直升机坠机点 / 西南湖畔营地。
// 地形 176x176 cells（<180^2，保留物理 MeshShape，可行走/寻路）。
// gnb shot --scene assets/scad/proc/coldwar/riverland_1km.scad
use <../../lib/kit_layout.scad>
use <../../lib/kit_terrain.scad>
use <../../lib/kit_coldwar.scad>

$fn = 12;

TERR = ["gkterr1", [1000, 1000], [176, 176], 7, [0, 2.6, 0.5], undef, "temperate",
    [
        // 北部山脉：双主峰 + 连接山脊；西侧丘陵；东部台地
        ["mountain", [-260, 380], 190, 52, 0.6],
        ["mountain", [80, 430], 170, 44, 0.55],
        ["ridge", [[-430, 310], [-180, 400], [60, 390], [270, 340]], 130, 24],
        ["mountain", [-420, 60], 130, 20, 0.5],
        ["plateau", [350, 140], 155, 12],
        ["plateau", [-120, -430], 100, 5],
        // 西南湖
        ["lake", [-330, -330], 55, 2.6],
        // 河：北山峡谷发源，蜿蜒穿越中部流出南缘
        ["river", [[-60, 470], [-30, 300], [-110, 160], [-60, 20], [20, -160], [-20, -330], [30, -500]], 12, 2.4],
        // 主公路（西→东，过河处留桥沟）+ 支路
        ["road", [[-500, -140], [-390, -110], [-300, -150], [-210, -110], [-120, -150], [-30, -158],
                  [60, -150], [150, -110], [240, -140], [330, -100], [420, -130], [500, -115]], 8],
        ["road", [[150, -110], [185, -10], [240, 80], [292, 150]], 7],     // → 军事基地
        ["road", [[240, -140], [240, -216]], 7],                           // → 小镇
        ["road", [[60, -150], [75, -240], [95, -308]], 6],                 // → 工厂
        ["road", [[-390, -110], [-380, -20], [-350, 60], [-345, 100]], 5], // 土路 → 通信站
        // 据点基座（压平，按用途定尺寸）
        ["pad", [-300, -174], [46, 32], 0],    // 村庄
        ["pad", [-30, -177], [30, 18], 0],     // 加油站
        ["pad", [50, -176], [16, 12], 0],      // 桥东碉堡
        ["pad", [340, 165], [90, 64], 0],      // 军事基地
        ["pad", [240, -246], [70, 52], 0],     // 小镇
        ["pad", [95, -332], [52, 40], 0],      // 工厂
        ["pad", [-345, 112], [20, 16], 0],     // 通信站
        ["pad", [-140, 240], [18, 14], 0]      // 坠机点
    ]];

gk_terrain(TERR);

// ================= 公路桥（跨河点 ~(17,-154)，锚下切带外路面） =================
translate([17, -154, gk_terrain_height(TERR, 40, -152)])
    rotate([0, 0, 5]) cw_prop_bridge(L = 34, W = 9);

// ================= POI 1：西部村庄（pad -300,-174；公路在北） =================
ter_place(TERR, -300, -174)
{
    translate([-15, 4, 0]) rotate([0, 0, 180]) cw_bldg_house_rural(seed = 1);
    translate([0, 5, 0]) rotate([0, 0, 172]) cw_bldg_house_rural(seed = 4, L = 8, D = 6);
    translate([15, 4, 0]) rotate([0, 0, 188]) cw_bldg_house_rural(seed = 6);
    translate([-4, -9, 0]) rotate([0, 0, 20]) cw_bldg_ruin(seed = 2, L = 7, D = 5.5);
    translate([9, -9, 0]) cw_prop_well(seed = 0);
    translate([-13, -8, 0]) cw_prop_woodpile(seed = 1);
    translate([-20, -11, 0]) cw_prop_fence_barbed(len = 7);
    translate([13, -12, 0]) cw_prop_fence_barbed(len = 8);
    translate([19, -6, 0]) cw_prop_barrel(seed = 2);
    translate([5, -12.5, 0]) cw_prop_campfire(seed = 1);
    translate([3, -14, 0]) cw_item_bedroll(seed = 1);
    translate([6.8, -11, 0]) cw_item_can(seed = 3);
    translate([16, 0.5, 0]) cw_wpn_mosin(seed = 1);
    translate([-9, 1, 0]) cw_item_jerrycan(seed = 2);
    // 室内物资（可进入木屋，掩体攻防）
    translate([-15, 4, 0]) cw_item_can(seed = 5);
    translate([0, 5, 0]) cw_item_medkit();
    translate([15, 4, 0]) cw_wpn_shotgun(seed = 3);
}
ter_place(TERR, -335, -140) rotate([0, 0, -24]) cw_prop_sign_town(seed = 1);
ter_place(TERR, -270, -158) cw_nature_tree_dead(s = 1.6, seed = 2);

// ================= POI 2：桥西加油站（pad -30,-177；公路在北） =================
ter_place(TERR, -30, -177)
{
    translate([0, 0, 0]) rotate([0, 0, 180]) cw_bldg_gas_canopy(seed = 0);
    translate([-3.5, 1, 0]) rotate([0, 0, 180]) cw_prop_pump_gas(seed = 0);
    translate([0, 1, 0]) rotate([0, 0, 180]) cw_prop_pump_gas(seed = 1);
    translate([3.5, 1, 0]) rotate([0, 0, 180]) cw_prop_pump_gas(seed = 2);
    translate([11, 3, 0]) cw_prop_sign_pylon(seed = 0);
    translate([9, -5, 0]) cw_prop_fueltank(seed = 0);
    translate([-10, 3, 0]) rotate([0, 0, 100]) cw_veh_wreck(seed = 2);
    translate([-12, -4, 0]) cw_prop_tires(seed = 1);
    translate([-8, -6, 0]) cw_prop_debris(seed = 3);
    translate([1.5, -3.5, 0]) cw_item_jerrycan(seed = 1);
    translate([4.5, -4.2, 0]) cw_item_jerrycan(seed = 4);
    translate([-5.5, -3, 0]) cw_prop_cart_shop(seed = 2);
    // 室内物资（可进入收银亭）
    translate([0, -4.9, 0]) cw_item_medkit();
}

// ================= POI 3：桥东碉堡哨点（pad 50,-176） =================
ter_place(TERR, 50, -176)
{
    cw_bldg_bunker(seed = 0);
    translate([-1, 4.6, 0]) cw_prop_wall_sandbag(len = 4);
    translate([-5.5, 2, 0]) rotate([0, 0, 90]) cw_prop_wall_sandbag(len = 3);
    translate([5, 4.4, 0]) rotate([0, 0, 20]) cw_prop_hedgehog();
    translate([-4, -4.5, 0]) cw_prop_crate_ammo(seed = 1);
    translate([-2.2, -3.6, 0]) cw_wpn_ak(seed = 1);
    translate([-5.8, -3.2, 0]) cw_item_helmet(seed = 1);
    translate([4.5, -4, 0]) cw_item_ammobox(seed = 2);
}
// 桥头封锁线（路面上）
ter_place(TERR, -2, -158) rotate([0, 0, 95]) cw_prop_barrier_gate(seed = 0);
ter_place(TERR, 0, -155, 0) rotate([0, 0, 40]) cw_prop_hedgehog();
ter_place(TERR, 36, -152) rotate([0, 0, -15]) cw_prop_hedgehog();

// ================= POI 4：军事基地（pad 340,165；大门朝西接支路） =================
ter_place(TERR, 340, 165)
{
    // 周界铁丝网（西边留门洞）+ 四角岗塔
    for (x = [-35, -23, -11, 1, 13, 25, 35])
    {
        translate([x, 28, 0]) cw_prop_fence_chain(len = 12);
        translate([x, -28, 0]) cw_prop_fence_chain(len = 12);
    }
    for (y = [-22, -10, 2, 14, 22]) translate([41, y, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 11);
    for (y = [2, 14, 22]) translate([-41, y, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 11);
    translate([-41, -25, 0]) rotate([0, 0, 90]) cw_prop_fence_chain(len = 7);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 38, sy * 25, 0]) cw_bldg_guard_tower(seed = sx + sy * 2);
    // 大门（西边 y=-15 洞口）
    translate([-38, -20, 0]) cw_bldg_checkpoint(seed = 1);
    translate([-39, -14, 0]) rotate([0, 0, 90]) cw_prop_barrier_gate(seed = 0);
    translate([-44, -8, 0]) cw_prop_hedgehog();
    translate([-44, -22, 0]) rotate([0, 0, 30]) cw_prop_hedgehog();
    translate([-36, -11, 0]) cw_prop_wall_sandbag(len = 3);
    // 指挥区
    translate([-24, 12, 0]) rotate([0, 0, 180]) cw_bldg_hq(seed = 0, L = 13, D = 8);
    translate([-24, -2, 0]) cw_prop_monument(seed = 1);
    translate([-38, 20, 0]) cw_prop_antenna(seed = 0);
    translate([-33, -26, 0]) cw_prop_searchlight(seed = 0);
    // 营房排
    translate([-8, -22, 0]) cw_bldg_barracks(seed = 1, L = 13, D = 6);
    translate([8, -22, 0]) cw_bldg_barracks(seed = 3, L = 13, D = 6);
    // 机库 / 后勤（东侧）
    translate([28, 12, 0]) rotate([0, 0, -90]) cw_bldg_hangar(seed = 0, L = 12, D = 14);
    translate([28, -6, 0]) rotate([0, 0, -90]) cw_bldg_hangar(seed = 1, L = 12, D = 14);
    translate([36, -24, 0]) cw_bldg_warehouse(seed = 2, L = 11, D = 7);
    translate([18, -26, 0]) cw_prop_fueltank(seed = 1);
    translate([38, 24, 0]) cw_bldg_water_tower(seed = 1);
    // 停机坪 + 装备
    translate([2, 20, 0]) cw_ground_helipad(S = 12);
    translate([0, 2, 0]) rotate([0, 0, 15]) cw_veh_btr(seed = 0);
    translate([13, 6, 0]) rotate([0, 0, -8]) cw_veh_tank(seed = 0);
    translate([-10, 0, 0]) rotate([0, 0, 90]) cw_veh_truck_canvas(seed = 0);
    translate([-15, -10, 0]) rotate([0, 0, 96]) cw_veh_uaz_van(seed = 0);
    // 帐篷营区 + 物资（北带）
    translate([-27, 24, 0]) cw_prop_tent_military(seed = 0);
    translate([-19, 25, 0]) rotate([0, 0, -12]) cw_prop_tent_military(seed = 1);
    translate([-23, 19.5, 0]) cw_prop_campfire(seed = 1);
    translate([-14, 20.5, 0]) cw_wpn_crate(seed = 0);
    translate([-11, 23, 0]) cw_prop_crate_ammo(seed = 2);
    translate([-25.5, 17, 0]) cw_item_bedroll(seed = 3);
    translate([-20.5, 17.5, 0]) cw_item_radio(seed = 0);
    translate([-12.5, 25.8, 0]) cw_item_ammobox(seed = 1);
    translate([-16, 18.6, 0]) cw_item_helmet(seed = 1);
    translate([-18, 22, 0]) cw_wpn_svd(seed = 1);
    translate([-9, 18.5, 0]) cw_item_backpack(seed = 2);
    translate([14, 26, 0]) cw_item_crate_supply(seed = 1);
    // 室内物资（可进入 HQ / 营房 / 仓库，掩体攻防）
    translate([-24, 12, 0]) cw_prop_crate_ammo(seed = 5);
    translate([-25.5, 11, 0]) cw_item_radio(seed = 2);
    translate([-8, -22, 0]) cw_wpn_ak(seed = 4);
    translate([8, -22, 0]) cw_item_ammobox(seed = 4);
    translate([36, -24, 0]) cw_item_crate_supply(seed = 3);
}
ter_place(TERR, 160, -100) cw_prop_sign_road(seed = 0);

// ================= POI 5：东南小镇（pad 240,-246；支路从北接入） =================
ter_place(TERR, 240, -246)
{
    // 内部街道（南北主街 + 东西横街）
    translate([0, -2, 0]) rotate([0, 0, 90]) cw_ground_road(L = 44, W = 6, seed = 1);
    translate([0, 8, 0]) cw_ground_cross(W = 6, seed = 2);
    translate([-18, 8, 0]) cw_ground_road(L = 28, W = 6, seed = 3);
    translate([18, 8, 0]) cw_ground_road(L = 28, W = 6, seed = 4);
    // 北排：板楼
    translate([-18, 19, 0]) cw_bldg_panel_flat(seed = 1, floors = 3, L = 15, D = 9);
    translate([16, 19, 0]) cw_bldg_panel_flat(seed = 5, floors = 4, L = 14, D = 9);
    translate([30, 17, 0]) cw_bldg_chapel(seed = 0);
    // 南排：超市 + 商铺
    translate([-16, -6, 0]) rotate([0, 0, 180]) cw_bldg_supermarket(seed = 0, L = 17, D = 11);
    translate([14, -5, 0]) rotate([0, 0, 180]) cw_bldg_shop_row(seed = 2, L = 11, D = 6);
    translate([-30, -18, 0]) cw_bldg_house_rural(seed = 8);
    translate([16, -19, 0]) rotate([0, 0, 8]) cw_bldg_ruin(seed = 3, L = 8, D = 6);
    // 街道家具与洗劫痕迹
    translate([7, 12, 0]) cw_bldg_bus_stop(seed = 1);
    translate([-7, 12.5, 0]) cw_prop_kiosk(seed = 0);
    translate([-10.5, 12, 0]) cw_prop_phone_booth(seed = 0);
    translate([-28, 12, 0]) cw_prop_monument(seed = 2);
    translate([24, -14, 0]) cw_prop_playground(seed = 1);
    translate([-13, 1, 0]) cw_prop_cart_shop(seed = 1);
    translate([-19, 0.5, 0]) rotate([0, 0, 15]) cw_prop_shelf_market(seed = 2);
    translate([-10, 0.8, 0]) cw_item_can(seed = 2);
    translate([-21.5, 2.2, 0]) cw_item_medkit();
    translate([12, -0.5, 0]) cw_wpn_pistol(seed = 1);
    translate([-32, 4, 0]) cw_prop_dumpster();
    translate([26, 5, 0]) cw_prop_debris(seed = 5);
    for (x = [-24, -4, 16]) translate([x, 11.6, 0]) cw_prop_lamp();
    for (x = [-14, 8]) translate([x, 4.4, 0]) rotate([0, 0, 180]) cw_prop_lamp();
    // 街面弃车
    translate([-6, 6.2, 0]) rotate([0, 0, 4]) cw_veh_lada(seed = 2);
    translate([16, 9.8, 0]) rotate([0, 0, 182]) cw_veh_bus(seed = 1);
    translate([1.5, -12, 0]) rotate([0, 0, 92]) cw_veh_wreck(seed = 3);
    translate([-2, -24, 0]) rotate([0, 0, 180]) cw_prop_billboard(seed = 0);
    // 室内物资（可进入超市 / 商铺 / 板楼 / 教堂 / 民房，掩体攻防）
    translate([-16, -6, 0]) cw_item_can(seed = 6);
    translate([-13, -7, 0]) cw_item_medkit();
    translate([14, -5, 0]) cw_wpn_pistol(seed = 3);
    translate([-18, 19, 0]) cw_item_backpack(seed = 5);
    translate([16, 19, 0]) cw_item_ammobox(seed = 6);
    translate([30, 17, 0]) cw_item_lantern(seed = 2);
    translate([-30, -18, 0]) cw_item_bedroll(seed = 6);
}
ter_place(TERR, 232, -150) cw_prop_sign_road(seed = 1);

// ================= POI 6：河畔工厂（pad 95,-332；支路从北接入） =================
ter_place(TERR, 95, -332)
{
    translate([-12, 6, 0]) cw_bldg_factory_hall(seed = 1, L = 15, D = 10);
    translate([9, 9, 0]) cw_bldg_factory_saw(seed = 2, L = 13, D = 9);
    translate([20, 13, 0]) cw_bldg_smokestack(seed = 0, h = 14);
    translate([15, -10, 0]) cw_bldg_warehouse(seed = 3, L = 12, D = 8);
    translate([-18, -10, 0]) cw_prop_fueltank(seed = 0);
    translate([-22, -4, 0]) cw_prop_barrel(seed = 1);
    translate([-19.6, -4.6, 0]) cw_prop_barrel(seed = 6);
    translate([-2, -6, 0]) rotate([0, 0, 20]) cw_veh_truck_canvas(seed = 1);
    translate([-10, -14, 0]) rotate([0, 0, 170]) cw_veh_tractor(seed = 1);
    translate([2, -14, 0]) cw_prop_pallet(seed = 2);
    translate([5, -13.4, 0]) cw_prop_crate_ammo(seed = 3);
    translate([0, -16.5, 0]) cw_prop_tires(seed = 1);
    translate([-6, -16, 0]) cw_prop_debris(seed = 2);
    translate([4, -12.2, 0]) cw_wpn_shotgun(seed = 1);
    translate([-16.8, -6.5, 0]) cw_item_jerrycan(seed = 3);
    for (x = [-22, -6, 12]) translate([x, -19, 0]) cw_prop_fence_concrete(len = 13);
    translate([-24, 16, 0]) cw_prop_lamp();
    translate([10, -2, 0]) cw_prop_lamp();
    // 室内物资（可进入厂房 / 仓库，掩体攻防）
    translate([-12, 6, 0]) cw_prop_crate_ammo(seed = 6);
    translate([-13.5, 5, 0]) cw_wpn_svd(seed = 2);
    translate([9, 9, 0]) cw_item_jerrycan(seed = 5);
    translate([15, -10, 0]) cw_item_crate_supply(seed = 4);
}

// ================= POI 7：西山通信站（pad -345,112；土路从南接入） =================
ter_place(TERR, -345, 112)
{
    translate([0, 3, 0]) cw_prop_antenna(seed = 0);
    translate([-6, -3, 0]) cw_bldg_guard_tower(seed = 1);
    translate([5, -4, 0]) cw_bldg_checkpoint(seed = 2);
    translate([5, 4, 0]) cw_prop_searchlight(seed = 1);
    translate([-2, -6.5, 0]) cw_prop_wall_sandbag(len = 3);
    translate([1.5, -4.8, 0]) cw_item_radio(seed = 1);
    translate([-3.5, -5.2, 0]) cw_item_ammobox(seed = 3);
    translate([3.2, -6.8, 0]) cw_item_medkit();
    translate([-7, 5, 0]) cw_prop_barrel(seed = 3);
    translate([7.5, -0.5, 0]) rotate([0, 0, 30]) cw_prop_fence_barbed(len = 5);
}

// ================= POI 8：北坡坠机点（pad -140,240） =================
ter_place(TERR, -140, 240)
{
    rotate([0, 0, 30]) cw_veh_heli_wreck(seed = 0);
    translate([6.5, -4.5, 0]) cw_item_crate_supply(seed = 1);
    translate([5, -6, 0]) cw_item_medkit();
    translate([-6, 5.5, 0]) cw_wpn_ak(seed = 3);
    translate([7.5, 3.5, 0]) cw_item_backpack(seed = 1);
}
ter_place(TERR, -128, 228) cw_prop_campfire(seed = 4);
ter_place(TERR, -126, 226) cw_item_bedroll(seed = 2);

// ================= 湖畔营地（西南湖东岸，无 pad 贴地） =================
ter_place(TERR, -266, -318) rotate([0, 0, 250]) cw_prop_tent_military(seed = 3);
ter_place(TERR, -262, -326) cw_prop_campfire(seed = 2);
ter_place(TERR, -264, -330, 0) cw_item_bedroll(seed = 4);
ter_place(TERR, -260, -322) cw_item_can(seed = 7);
ter_place(TERR, -268, -312) cw_item_lantern(seed = 1);
ter_place(TERR, -258, -334) cw_wpn_shotgun(seed = 2);
// 湖岸芦苇圈
ter_place(TERR, -273, -330) cw_nature_reeds(seed = 1);
ter_place(TERR, -286, -293) cw_nature_reeds(seed = 2);
ter_place(TERR, -320, -274) cw_nature_reeds(seed = 3);
ter_place(TERR, -374, -293) cw_nature_reeds(seed = 4);
ter_place(TERR, -384, -350) cw_nature_reeds(seed = 5);
ter_place(TERR, -320, -386) cw_nature_reeds(seed = 6);
ter_place(TERR, -286, -367) cw_nature_reeds(seed = 7);

// ================= 公路道具 =================
// 电力杆沿主公路北侧
ter_along(TERR, [[-500, -140], [-390, -110], [-300, -150], [-210, -110], [-120, -150], [-30, -158]],
          step = 85, seed = 5) translate([0, 5.5, 0]) cw_prop_pole_concrete(seed = $seed);
ter_along(TERR, [[60, -150], [150, -110], [240, -140], [330, -100], [420, -130], [500, -115]],
          step = 85, seed = 6) translate([0, 5.5, 0]) cw_prop_pole_concrete(seed = $seed);
// 路面弃车与路牌
ter_place(TERR, -210, -110) rotate([0, 0, 204]) cw_veh_lada(seed = 5);
ter_place(TERR, -120, -150) rotate([0, 0, -22]) cw_veh_wreck(seed = 4);
ter_place(TERR, 330, -100) rotate([0, 0, 12]) cw_veh_truck_canvas(seed = 2);
ter_place(TERR, 100, -138) rotate([0, 0, 160]) cw_veh_uaz_van(seed = 3);
ter_place(TERR, -444, -128) rotate([0, 0, -14]) cw_veh_bus(seed = 2);
ter_place(TERR, 100, -160) rotate([0, 0, 180]) cw_prop_billboard(seed = 1);
ter_place(TERR, 152, -102) rotate([0, 0, 65]) cw_prop_sign_town(seed = 2);
ter_place(TERR, 66, -242, 0) rotate([0, 0, 80]) cw_prop_sign_road(seed = 2);

// ================= 植被生态（ter_scatter 过滤散布） =================
// 针叶/白桦混交林：缓坡草地，避水
ter_scatter(TERR, 101, 420, [-490, -490, 490, 490], [0.5, 40, 26, 3, ["grass", "grass_dark"]])
    lay_pick($seed)
    {
        cw_nature_pine(s = lay_randr($seed, 5, 1.7, 2.7), seed = $seed);
        cw_nature_pine(s = lay_randr($seed, 6, 1.9, 2.9), seed = $seed + 1);
        cw_nature_birch(s = lay_randr($seed, 7, 1.5, 2.3), seed = $seed);
        cw_nature_pine(s = lay_randr($seed, 8, 1.5, 2.2), seed = $seed + 2);
    }
// 北坡密林带
ter_scatter(TERR, 103, 200, [-490, 120, 490, 460], [2, 42, 28, 2, ["grass", "grass_dark"]])
    lay_pick($seed)
    {
        cw_nature_pine(s = lay_randr($seed, 5, 1.8, 2.8), seed = $seed);
        cw_nature_birch(s = lay_randr($seed, 6, 1.6, 2.4), seed = $seed);
    }
// 高地岩块（随机朝向）
ter_scatter(TERR, 107, 50, [-490, -490, 490, 490], [1, 60, 60, 0, ["rock", "rock_high"]])
    cw_nature_rock(s = lay_randr($seed, 4, 1.4, 3.0), seed = $seed);
// 枯草带灌木与枯树
ter_scatter(TERR, 109, 55, [-490, -490, 490, 490], [1, 45, 30, 1, ["dry_grass"]])
    lay_pick($seed)
    {
        cw_nature_bush(s = lay_randr($seed, 3, 1.2, 2.0), seed = $seed);
        cw_nature_tree_dead(s = lay_randr($seed, 4, 1.3, 2.0), seed = $seed);
        cw_nature_grass_tuft(seed = $seed);
    }
// 河谷两岸疏林（让河道有掩体接近路线）
ter_scatter(TERR, 115, 80, [-150, -400, 120, 300], [0.5, 30, 24, 4, ["grass", "grass_dark"]])
    lay_pick($seed)
    {
        cw_nature_birch(s = lay_randr($seed, 5, 1.5, 2.2), seed = $seed);
        cw_nature_pine(s = lay_randr($seed, 6, 1.6, 2.4), seed = $seed);
        cw_nature_bush(s = lay_randr($seed, 7, 1.2, 1.8), seed = $seed);
    }
// 低地灌木丛
ter_scatter(TERR, 113, 60, [-490, -490, 490, 490], [0.3, 25, 22, 2, ["grass", "grass_dark"]])
    lay_pick($seed)
    {
        cw_nature_bush(s = lay_randr($seed, 3, 1.0, 1.8), seed = $seed);
        cw_nature_grass_tuft(seed = $seed);
        cw_nature_stump(s = lay_randr($seed, 5, 1.0, 1.5), seed = $seed);
    }
