// kit_city_hd.scad —— 由一次性 tools/scadkit 迁移生成；原顶层场景已删除，本文件现在是 hc_ kit 的事实来源。
// 纯零件库：只含 module/function 定义，无顶层几何。命名空间前缀 "hc_"。
// 常量以零参 function 内置（use 语义不传播顶层赋值），调用方无需重申环境常量。
// 放置契约：落地件底面 z=0，带朝向件 front = -y。调用方自设 $fn（建议 12）。

// 以下注释描述该 kit 原始海滨城市场景的尺度与约定。
//
// 与 habor_city.scad（微缩沙盘 672x508）不同，本场景为**人尺度**（1 单位 = 1 米），
// 细节密度对齐 airport.scad：门是门、灯是灯、车是车，角色可直接行走其中。
// 范围收缩为海滨核心区：双横三纵路网 + 四核心街区 + 东西配套带 + 南侧完整海滨港区。
//
// 构造约定（沿用 airport.scad / office.scad / habor_city.scad 经验）：
//   * 所有带朝向 module 约定 front = -y；布局处 rotate 调整朝向；
//   * 落地件底面 z=0，布局时 translate 到所在台面高度；
//   * 路面顶 z=0.15（与 office/airport kGroundY 一致），人行道/台面顶 z=0.30，海面 z=-0.50；
//   * 伪随机 hc_rnd(i,m) 整数散列，确定性（引擎与 OpenSCAD 渲染一致）；
//   * 仅用引擎 SCADLoader 已支持特性（无 offset/projection/minkowski/import/resize）；
//   * 避免 difference，画家叠层 + 四板女儿墙；text() 注意三角形成本，招牌点到为止。
//
// POI 锚点（命名约定与 airport.scad / office.scad 同构）：
//   锚点 = 具名 user module，加载后节点名 = module 名，WorldTranslation = 点位坐标。
//   锚点调用必须 translate(...) 在 rotate(...) 外层。锚点自带少量几何（井盖/门垫/泵机…）。
//   —— 交通 / 配送 玩法 ——
//     node_<id>    路口图节点（井盖）×6        spawn_<id>  车辆生成/离场点（路缘标）×7
//     stop_<id>    公交站（候车亭）×2          park_<id>   停车位（标线泊位）×14
//     fuel_<id>    加油泵 ×2                   heli_<id>   直升机坪（医院/警局）×2
//     dock_<id>    码头泊位（系船柱，01 货运 02 客运栈桥）×2
//     load_<id>    装卸点（01 吊机 02 仓库 03 超市后门 04 酒店后勤）×4
//   —— 城市经营 / 模拟 玩法 ——
//     hospital_01 police_01 hotel_01 market_01 cafe_01 burger_01 barber_01 gas_01
//     warehouse_01 shop_01..02 apt_01..02 house_01..04 court_01 beach_01 pier_01
//     plaza_01 turbine_01..03 （锚点放业务入口门垫/设施本体）
//   非锚点一律 ground_* / road_* / part_* / bldg_* / prop_* / furn_* / veh_* / boat_* / nature_* 前缀。
//
// OpenSCAD Z-up。+y 北（丘陵收边），-y 南（海滨）。总平面 172 x 158：x∈[-86,86], y∈[-62,96]。
//   y∈[80,96]    北部丘陵 + 松林 + 信号塔（低细节背景带）
//   y∈[-4,80]    城市路网：横路 Harbor Ave(y=0) / Main St(y=38)；纵路 x=-48/0/48（北抵 y=80）
//       北排街区  NW 医院(屋顶停机坪)        NE 住宅 x4 + 篮球场
//       中排街区  SW 超市 + 停车场           SE 酒店 + 咖啡屋 + 喷泉广场
//       西条带    商铺排(理发/面包/书店) + 公寓 | 北段 风电公园
//       东条带    加油站 + 汉堡店            | 北段 警局(停机坪) + 公寓
//   y∈[-10,-4]   滨海步道（棕榈/长椅/路灯）
//   y∈[-26,-10]  西:沙滩  中:游艇栈桥+码头办公  东:集装箱码头(吊机/箱堆/仓库)
//   y∈[-62,-26]  海洋（货轮靠泊/帆船/快艇/浮标/海鸥）



// ================= 标高常量 =================
function hc_GZT() = 0.15;    // 路面/基准地面顶
function hc_WZT() = 0.30;    // 人行道/街区台面顶
function hc_SEAZ() = -0.50;   // 海面顶
function hc_RW() = 8;       // 路宽（双车道）

// ================= 配色 =================
function hc_ROADC() = [0.27, 0.28, 0.31];   // 沥青
function hc_MARKC() = [0.94, 0.94, 0.92];   // 道路标线
function hc_WALKC() = [0.83, 0.83, 0.80];   // 人行道砖
function hc_WALKD() = [0.74, 0.74, 0.71];   // 砖缝/深色砖
function hc_CURBC() = [0.69, 0.69, 0.66];   // 路缘石
function hc_LOTC() = [0.44, 0.45, 0.47];   // 停车场
function hc_APRONC() = [0.65, 0.65, 0.62];   // 码头混凝土
function hc_PLAZAC() = [0.86, 0.84, 0.79];   // 广场砖
function hc_GRASSC() = [0.55, 0.78, 0.35];   // 草地
function hc_GRASSD() = [0.47, 0.70, 0.29];   // 深草
function hc_SANDC() = [0.93, 0.84, 0.58];   // 干沙
function hc_SANDW() = [0.87, 0.77, 0.54];   // 湿沙
function hc_SEAC() = [0.25, 0.60, 0.77];   // 海面
function hc_SEAD() = [0.21, 0.54, 0.72];   // 深海
function hc_FOAMC() = [0.95, 0.97, 0.97];   // 浪花
function hc_BASEC() = [0.24, 0.21, 0.28];   // 展台底座
function hc_WHITEC() = [0.94, 0.93, 0.90];
function hc_CREAMC() = [0.93, 0.88, 0.73];
function hc_GREYC() = [0.62, 0.64, 0.66];
function hc_DGREYC() = [0.36, 0.38, 0.41];
function hc_DARKC() = [0.15, 0.16, 0.18];
function hc_METALC() = [0.72, 0.74, 0.77];
function hc_GLASSL() = [0.55, 0.74, 0.90];   // 浅蓝玻璃
function hc_GLASSB() = [0.33, 0.56, 0.82];   // 蓝幕墙
function hc_GLASSD() = [0.20, 0.36, 0.60];   // 深蓝玻璃
function hc_GLASSA() = [0.55, 0.72, 0.85, 0.35];  // 透明玻璃（门/候车亭）
function hc_REDC() = [0.85, 0.26, 0.20];
function hc_ORANGEC() = [0.92, 0.57, 0.22];
function hc_YELLOWC() = [0.95, 0.78, 0.20];
function hc_BLUEC() = [0.27, 0.47, 0.75];
function hc_POLBLUE() = [0.30, 0.55, 0.82];   // 警局蓝
function hc_TEALC() = [0.30, 0.62, 0.58];
function hc_BROWND() = [0.30, 0.22, 0.16];   // 理发店深棕
function hc_TRUNKC() = [0.45, 0.31, 0.18];
function hc_LEAFC() = [0.49, 0.76, 0.27];
function hc_LEAFD() = [0.38, 0.64, 0.22];
function hc_PALMC() = [0.30, 0.62, 0.30];
function hc_OAKC() = [0.76, 0.56, 0.34];   // 木栈道
function hc_COURTR() = [0.72, 0.30, 0.26];   // 球场红
function hc_COURTB() = [0.30, 0.45, 0.65];   // 球场蓝
function hc_YELLINE() = [0.85, 0.75, 0.25];   // 黄色警示线

// ---- 伪随机 / 调色板 ----
function hc_rnd(seedValue, m) = (((((seedValue * 73 + 31) % 97 + 97) % 97) * 13) + ((seedValue % 7 + 7) % 7)) % m;
function hc_car_c(i)   = [[0.85,0.26,0.20],[0.93,0.94,0.95],[0.60,0.62,0.66],[0.24,0.32,0.48],[0.92,0.57,0.22],[0.35,0.55,0.78]][hc_rnd(i,6)];
function hc_ctn_c(i)   = [[0.80,0.28,0.24],[0.92,0.57,0.22],[0.28,0.50,0.76],[0.40,0.65,0.36],[0.62,0.44,0.70],[0.90,0.88,0.84]][hc_rnd(i,6)];
function hc_umb_c(i)   = [[0.85,0.26,0.20],[0.95,0.78,0.20],[0.28,0.50,0.76],[0.40,0.65,0.36],[0.92,0.57,0.22],[0.84,0.45,0.62]][hc_rnd(i,6)];
function hc_house_c(i) = [[0.93,0.88,0.73],[0.88,0.91,0.94],[0.93,0.82,0.66],[0.82,0.88,0.78]][hc_rnd(i,4)];
function hc_roof_c(i)  = [[0.85,0.26,0.20],[0.55,0.34,0.20],[0.30,0.44,0.62],[0.92,0.57,0.22]][hc_rnd(i,4)];
function hc_leaf_c(i)  = [[0.49,0.76,0.27],[0.38,0.64,0.22],[0.56,0.80,0.32]][hc_rnd(i,3)];

// ---- 基础工具 ----
module hc_boxc(s) cube(s, center = true);
module hc_slab(L, D, t) translate([0, 0, t / 2]) hc_boxc([L, D, t]);   // 底面 z=0 平板

// ================= 植被（底面 z=0） =================
// 低多边形团状树（参考图风格：多面体球叠簇，s 缩放）
module hc_nature_tree(s = 1.0, i = 0)
{
    scale([s, s, s])
    {
        color(hc_TRUNKC()) cylinder(h = 1.3, r = 0.20, $fn = 6);
        color(hc_leaf_c(i))     translate([0, 0, 2.3])    sphere(r = 1.45, $fn = 6);
        color(hc_leaf_c(i + 1)) translate([0.7, 0.4, 3.0]) sphere(r = 0.95, $fn = 6);
        color(hc_leaf_c(i + 2)) translate([-0.6, -0.3, 3.1]) sphere(r = 0.80, $fn = 6);
    }
}

// 球冠行道树（整齐，沿街用）
module hc_nature_tree_street(s = 1.0, i = 0)
{
    scale([s, s, s])
    {
        color(hc_TRUNKC()) cylinder(h = 1.8, r = 0.16, $fn = 6);
        color(hc_leaf_c(i)) translate([0, 0, 2.9]) sphere(r = 1.25, $fn = 7);
    }
}

module hc_nature_pine(s = 1.0)
{
    scale([s, s, s])
    {
        color(hc_TRUNKC()) cylinder(h = 0.9, r = 0.16, $fn = 6);
        color(hc_LEAFD()) translate([0, 0, 0.8]) cylinder(h = 2.2, r1 = 1.2, r2 = 0.32, $fn = 7);
        color(hc_LEAFC()) translate([0, 0, 2.5]) cylinder(h = 1.5, r1 = 0.8, r2 = 0.05, $fn = 7);
    }
}

// 棕榈树（滨海步道/沙滩）
module hc_nature_palm(s = 1.0, lean = 6)
{
    scale([s, s, s]) rotate([0, lean, 0])
    {
        color([0.52, 0.40, 0.26]) cylinder(h = 4.6, r1 = 0.24, r2 = 0.15, $fn = 6);
        for (a = [0 : 60 : 300])
            color((a % 120 == 0) ? hc_PALMC() : hc_LEAFD())
                rotate([0, 0, a]) translate([1.15, 0, 4.65]) rotate([0, 30, 0]) hc_boxc([2.3, 0.5, 0.09]);
        color([0.40, 0.55, 0.28]) translate([0, 0, 4.6]) sphere(r = 0.26, $fn = 7);
    }
}

module hc_nature_hedge(len = 4)
{
    color(hc_LEAFD()) translate([0, 0, 0.36]) hc_boxc([len, 0.8, 0.72]);
    color(hc_LEAFC()) translate([0, 0, 0.74]) hc_boxc([len - 0.4, 0.55, 0.20]);
}

// 花坛（砖框 + 草 + 花点）
module hc_nature_flowerbed(L = 3.0, D = 1.4, seed = 0)
{
    color(hc_WALKD()) hc_slab(L, D, 0.32);
    color(hc_GRASSD()) translate([0, 0, 0.32]) hc_slab(L - 0.3, D - 0.3, 0.10);
    for (i = [0 : 4])
        color(hc_umb_c(seed + i))
            translate([-L / 2 + 0.5 + i * (L - 1.0) / 4, (hc_rnd(seed + i, 2) == 0 ? 0.25 : -0.25), 0.46])
                sphere(r = 0.10, $fn = 6);
}

// 盆栽（店门口）
module hc_nature_pot_plant()
{
    color([0.70, 0.44, 0.30]) cylinder(h = 0.45, r1 = 0.22, r2 = 0.28, $fn = 7);
    color(hc_LEAFD()) translate([0, 0, 0.65]) sphere(r = 0.32, $fn = 6);
    color(hc_LEAFC()) translate([0.1, 0.08, 0.85]) sphere(r = 0.2, $fn = 6);
}

// ================= 街道小品（底面 z=0；带朝向者 front=-y） =================
module hc_prop_lamp()
{
    color(hc_DGREYC())
    {
        cylinder(h = 0.14, r = 0.15, $fn = 7);
        cylinder(h = 5.0, r = 0.07, $fn = 6);
        translate([0, 0.45, 4.95]) hc_boxc([0.09, 1.0, 0.08]);
    }
    color([0.97, 0.93, 0.75]) translate([0, 0.88, 4.88]) hc_boxc([0.22, 0.5, 0.13]);
}

// 红绿灯（灯头朝 -y）
module hc_prop_traffic_light()
{
    color(hc_DGREYC()) cylinder(h = 3.6, r = 0.07, $fn = 6);
    color(hc_DARKC()) translate([0, -0.07, 3.0]) hc_boxc([0.26, 0.16, 0.78]);
    color(hc_REDC())    translate([0, -0.16, 3.26]) sphere(r = 0.075, $fn = 6);
    color(hc_YELLOWC()) translate([0, -0.16, 3.04]) sphere(r = 0.075, $fn = 6);
    color([0.30, 0.75, 0.35]) translate([0, -0.16, 2.82]) sphere(r = 0.075, $fn = 6);
}

// 消防栓
module hc_prop_hydrant()
{
    color(hc_REDC())
    {
        cylinder(h = 0.55, r = 0.12, $fn = 7);
        translate([0, 0, 0.55]) sphere(r = 0.13, $fn = 7);
        translate([0, 0, 0.30]) hc_boxc([0.36, 0.10, 0.10]);
    }
    color(hc_WHITEC()) translate([0, 0, 0.66]) cylinder(h = 0.06, r = 0.06, $fn = 6);
}

// 邮筒
module hc_prop_mailbox(c = hc_REDC())
{
    color(hc_DGREYC()) for (sx = [-1, 1]) translate([0.14 * sx, 0, 0.25]) hc_boxc([0.06, 0.06, 0.5]);
    color(c) translate([0, 0, 0.78]) hc_boxc([0.56, 0.40, 0.60]);
    color(c) translate([0, 0, 1.08]) rotate([90, 0, 90]) cylinder(h = 0.56, r = 0.20, center = true, $fn = 8);
    color(hc_DARKC()) translate([0, -0.205, 0.92]) hc_boxc([0.34, 0.02, 0.06]);
}

module hc_prop_bench()
{
    color(hc_DGREYC()) for (sx = [-1, 1]) translate([0.62 * sx, 0, 0.20]) hc_boxc([0.08, 0.48, 0.40]);
    color(hc_OAKC()) translate([0, 0, 0.42]) hc_boxc([1.55, 0.48, 0.07]);
    color(hc_OAKC()) translate([0, 0.24, 0.66]) rotate([10, 0, 0]) hc_boxc([1.55, 0.06, 0.42]);
}

module hc_prop_bin(c = [0.30, 0.50, 0.40])
{
    color(c) cylinder(h = 0.72, r = 0.24, $fn = 7);
    color(hc_DARKC()) translate([0, 0, 0.72]) cylinder(h = 0.06, r = 0.25, $fn = 7);
}

// 公交候车亭（开口朝 -y 即道路侧）
module hc_prop_bus_shelter()
{
    color(hc_DGREYC()) for (sx = [-1, 1]) translate([1.45 * sx, 0.40, 1.25]) hc_boxc([0.10, 0.10, 2.50]);
    color(hc_GLASSA()) translate([0, 0.46, 1.40]) hc_boxc([3.00, 0.05, 1.70]);
    color(hc_GREYC()) translate([0, 0.20, 2.58]) hc_boxc([3.30, 1.35, 0.10]);
    color(hc_DGREYC()) translate([0, 0.28, 0.44]) hc_boxc([2.40, 0.35, 0.08]);
    color(hc_BLUEC()) translate([1.56, 0.40, 2.30]) hc_boxc([0.06, 0.45, 0.45]);
    color(hc_WHITEC()) translate([1.56, 0.30, 2.30]) hc_boxc([0.02, 0.28, 0.28]);
}

// 立式广告牌 / 菜单牌（店门口）
module hc_prop_board(c = hc_BLUEC())
{
    color(hc_DGREYC()) translate([0, 0, 0.45]) hc_boxc([0.65, 0.08, 0.95]);
    color(hc_WHITEC()) translate([0, -0.045, 0.50]) hc_boxc([0.52, 0.02, 0.68]);
    color(c) translate([0, -0.055, 0.70]) hc_boxc([0.42, 0.02, 0.18]);
}

// 井盖（路口节点锚点用）
module hc_prop_manhole()
{
    color(hc_DGREYC()) cylinder(h = 0.012, r = 0.42, $fn = 9);
    color(hc_DARKC()) translate([0, 0, 0.012]) cylinder(h = 0.004, r = 0.34, $fn = 9);
}

// 路缘生成标（spawn 锚点用：白色道钉横条）
module hc_prop_spawn_stud()
{
    color(hc_MARKC()) translate([0, 0, 0.008]) hc_boxc([0.9, 0.25, 0.016]);
    color(hc_YELLOWC()) translate([0, 0, 0.02]) hc_boxc([0.3, 0.12, 0.025]);
}

// 装卸点地标（load 锚点用：黄色斜纹框）
module hc_prop_load_mark()
{
    color(hc_YELLINE())
    {
        for (sy = [-1, 1]) translate([0, sy * 1.1, 0.008]) hc_boxc([2.6, 0.14, 0.016]);
        for (sx = [-1, 1]) translate([sx * 1.3, 0, 0.008]) hc_boxc([0.14, 2.06, 0.016]);
    }
    color(hc_YELLINE()) for (i = [-1 : 1]) translate([i * 0.8, 0, 0.008]) rotate([0, 0, 45]) hc_boxc([1.2, 0.12, 0.016]);
}

// 门垫（经营类 POI 锚点用：深色垫 + 蓝色点）
module hc_prop_door_mat(w = 1.8)
{
    color([0.28, 0.30, 0.28]) translate([0, 0, 0.011]) hc_boxc([w, 1.0, 0.022]);
    color(hc_BLUEC()) translate([0, 0, 0.024]) hc_boxc([0.3, 0.3, 0.012]);
}

// 螺旋纹喷泉广场（参考图蓝白回纹方台）
module hc_prop_fountain_spiral()
{
    color(hc_PLAZAC()) hc_slab(7.2, 7.2, 0.14);
    for (i = [0 : 3])
    {
        w = 6.2 - i * 1.5;
        color((i % 2 == 0) ? hc_BLUEC() : hc_WHITEC()) translate([0, 0, 0.14 + i * 0.10]) hc_slab(w, w, 0.10);
    }
    color(hc_GLASSL()) translate([0, 0, 0.56]) hc_slab(1.0, 1.0, 0.10);
}

// 风力发电机（参考图：白塔 + 三叶 + 红条带）
module hc_prop_wind_turbine(h = 16, az = 0)
{
    color(hc_WHITEC()) cylinder(h = h, r1 = 0.55, r2 = 0.30, $fn = 8);
    color(hc_REDC()) translate([0, 0, h * 0.45]) cylinder(h = 0.5, r = 0.41, $fn = 8);
    translate([0, -0.55, h]) rotate([90, az, 0])
    {
        color(hc_WHITEC()) rotate([0, 90, 0]) cylinder(h = 1.4, r = 0.42, center = true, $fn = 7);
        for (a = [0 : 120 : 240])
            rotate([0, 0, a]) color(hc_WHITEC()) translate([0, 2.6, 0.4]) hc_boxc([0.35, 5.2, 0.12]);
    }
}

// 信号塔（北侧背景）
module hc_prop_radio_tower(h = 14)
{
    for (i = [0 : 4])
        color((i % 2 == 0) ? [0.88, 0.30, 0.24] : hc_WHITEC())
            translate([0, 0, i * h / 5]) cylinder(h = h / 5, r1 = 0.55 - i * 0.08, r2 = 0.55 - (i + 1) * 0.08, $fn = 6);
    color(hc_DGREYC()) for (zz = [h * 0.4, h * 0.75]) translate([0, 0, zz]) hc_boxc([2.2, 0.14, 0.14]);
    color(hc_WHITEC()) translate([0, 0, h]) cylinder(h = 2.2, r = 0.06, $fn = 6);
    color([0.88, 0.30, 0.24]) translate([0, 0, h + 2.2]) sphere(r = 0.2, $fn = 7);
}

// 链网围栏段（沿 +x 长 len；球场用）
module hc_prop_fence(len = 10, h = 3.2)
{
    color(hc_DGREYC())
    {
        for (x = [0 : 2.5 : len]) translate([x, 0, h / 2]) cylinder(h = h, r = 0.05, center = true, $fn = 6);
        translate([len / 2, 0, h - 0.05]) hc_boxc([len, 0.05, 0.06]);
        translate([len / 2, 0, 0.35]) hc_boxc([len, 0.05, 0.06]);
    }
    color([0.45, 0.48, 0.52, 0.35]) translate([len / 2, 0, h / 2 + 0.15]) hc_boxc([len, 0.02, h - 0.7]);
}

// 旗杆
module hc_prop_flag(c = hc_BLUEC())
{
    color(hc_GREYC()) cylinder(h = 6.4, r = 0.07, $fn = 6);
    color(c) translate([0.85, 0, 5.9]) hc_boxc([1.6, 0.05, 0.95]);
}

// ================= 地面与道路 =================
// 展台底座 + 城市基底（路面以下）
module hc_ground_base()
{
    color(hc_BASEC()) translate([0, 17, -1.45]) hc_boxc([176, 162, 1.5]);            // 展台
    color([0.42, 0.40, 0.38]) translate([0, 44, -0.30]) hc_boxc([172, 104, 0.86]); // 城区基底（顶 z≈0.13）
    color([0.42, 0.40, 0.38]) translate([0, -8, -0.30]) hc_boxc([172, 8, 0.86]);   // 滨海步道基底
}

// 沿 x 道路段（含中线虚线；x0..x1 为虚线绘制区间）
module hc_road_x(L, dash0 = 0, dash1 = 0)
{
    color(hc_ROADC()) hc_slab(L, hc_RW(), hc_GZT());
    if (dash1 > dash0)
        color(hc_MARKC()) for (x = [dash0 : 4 : dash1])
            translate([x, 0, hc_GZT()]) hc_boxc([1.8, 0.13, 0.014]);
}

module hc_road_y(L, dash0 = 0, dash1 = 0)
{
    color(hc_ROADC()) hc_slab(hc_RW(), L, hc_GZT());
    if (dash1 > dash0)
        color(hc_MARKC()) for (y = [dash0 : 4 : dash1])
            translate([0, y, hc_GZT()]) hc_boxc([0.13, 1.8, 0.014]);
}

// 斑马线（横跨沿 x 道路；行人沿 y 通过。rotate 90 后用于纵路）
module hc_road_crosswalk()
{
    color(hc_MARKC()) for (i = [-3 : 3]) translate([i * 0.66, 0, hc_GZT()]) hc_boxc([0.44, 6.6, 0.016]);
}

// 街区人行道台面（顶 z=hc_WZT()，含砖缝）
module hc_ground_block_pad(W, D, seams = true)
{
    color(hc_WALKC()) hc_slab(W, D, hc_WZT());
    if (seams)
    {
        color(hc_WALKD()) for (x = [-W / 2 + 2.4 : 2.4 : W / 2 - 0.5]) translate([x, 0, hc_WZT()]) hc_boxc([0.05, D, 0.006]);
        color(hc_WALKD()) for (y = [-D / 2 + 2.4 : 2.4 : D / 2 - 0.5]) translate([0, y, hc_WZT()]) hc_boxc([W, 0.05, 0.006]);
    }
}

// 路缘石（沿 +x 长 len，置于道路边）
module hc_ground_curb_x(len) color(hc_CURBC()) translate([0, 0, 0.11]) hc_boxc([len, 0.30, 0.22]);
module hc_ground_curb_y(len) color(hc_CURBC()) translate([0, 0, 0.11]) hc_boxc([0.30, len, 0.22]);

// 停车场面层 + 泊位线（W 宽含 bays 个泊位，开口朝 -y；置于台面顶）
module hc_road_parking_row(bays = 4, bw = 2.8, bd = 5.4)
{
    W = bays * bw;
    color(hc_LOTC()) translate([0, 0, 0]) hc_slab(W + 0.6, bd + 0.6, 0.05);
    color(hc_MARKC()) for (i = [0 : bays])
        translate([-W / 2 + i * bw, 0.1, 0.05]) hc_boxc([0.12, bd, 0.016]);
    color(hc_MARKC()) translate([0, bd / 2 + 0.04, 0.05]) hc_boxc([W + 0.12, 0.12, 0.016]);
}

// ================= 建筑公共件（front = -y） =================
// 玻璃双开门（带框 + 顶檐）
module hc_part_door_glass(w = 2.4, h = 2.5)
{
    color(hc_DGREYC())
    {
        for (sx = [-1, 1]) translate([(w / 2) * sx, 0, h / 2]) hc_boxc([0.12, 0.24, h]);
        translate([0, 0, h + 0.06]) hc_boxc([w + 0.24, 0.24, 0.12]);
    }
    color(hc_GLASSA()) translate([-w / 4, 0.05, h / 2]) hc_boxc([w / 2 - 0.06, 0.05, h - 0.1]);
    color(hc_GLASSA()) translate([w / 4, -0.05, h / 2]) hc_boxc([w / 2 - 0.06, 0.05, h - 0.1]);
    color(hc_DGREYC()) translate([0, 0, h / 2]) hc_boxc([0.06, 0.20, h]);
}

// 实木单开门
module hc_part_door_solid(c = [0.46, 0.30, 0.18], w = 1.0, h = 2.2)
{
    color(hc_WHITEC()) translate([0, 0, h / 2]) hc_boxc([w + 0.28, 0.16, h + 0.14]);
    color(c) translate([0, -0.04, h / 2]) hc_boxc([w, 0.12, h]);
    color(hc_YELLOWC()) translate([w * 0.32, -0.11, h * 0.48]) sphere(r = 0.05, $fn = 6);
}

// 卷帘门（仓库/车库）
module hc_part_door_roller(w = 3.4, h = 3.2)
{
    color(hc_GREYC()) translate([0, 0, h / 2]) hc_boxc([w, 0.14, h]);
    color(hc_DGREYC()) for (z = [0.45 : 0.45 : h - 0.3]) translate([0, -0.075, z]) hc_boxc([w, 0.02, 0.06]);
    color(hc_DGREYC()) translate([0, 0, h + 0.22]) hc_boxc([w + 0.5, 0.30, 0.44]);
}

// 单窗（白框 + 玻璃，贴墙面用：墙面在 y=0，窗朝 -y）
module hc_part_window(w = 1.4, h = 1.5)
{
    color(hc_WHITEC()) hc_boxc([w, 0.12, h]);
    color(hc_GLASSL()) translate([0, -0.04, 0]) hc_boxc([w - 0.26, 0.08, h - 0.26]);
}

// 一层窗带（沿 x 排 n 扇，窗心 z=0）
module hc_part_win_row(L, n, w = 1.4, h = 1.5)
{
    pitch = L / n;
    for (i = [0 : n - 1]) translate([-L / 2 + pitch * (i + 0.5), 0, 0]) hc_part_window(w, h);
}

// 条纹遮阳棚（宽 w，两色相间，front=-y 外挑）
module hc_part_awning(w = 3.0, c = hc_REDC(), n = 5)
{
    sw = w / n;
    for (i = [0 : n - 1])
        color((i % 2 == 0) ? c : hc_WHITEC())
            translate([-w / 2 + sw * (i + 0.5), -0.62, -0.18]) rotate([26, 0, 0]) hc_boxc([sw, 1.45, 0.07]);
    color(hc_DGREYC()) translate([0, -1.22, -0.50]) hc_boxc([w, 0.06, 0.06]);
}

// 四板女儿墙（避免 difference）
module hc_part_parapet(L, D, c = hc_WHITEC(), h = 0.55, t = 0.35)
{
    color(c)
    {
        for (sy = [-1, 1]) translate([0, sy * (D - t) / 2, h / 2]) hc_boxc([L, t, h]);
        for (sx = [-1, 1]) translate([sx * (L - t) / 2, 0, h / 2]) hc_boxc([t, D - 2 * t, h]);
    }
}

// 屋顶空调机组（参考图：白色机箱 + 风扇圆环，n 台沿 x 排）
module hc_part_roof_ac(n = 2)
{
    for (i = [0 : n - 1])
        translate([i * 2.0, 0, 0])
        {
            color(hc_WHITEC()) translate([0, 0, 0.42]) hc_boxc([1.6, 1.3, 0.84]);
            color(hc_GREYC()) translate([-0.35, 0, 0.86]) cylinder(h = 0.05, r = 0.42, $fn = 9);
            color(hc_DARKC()) translate([-0.35, 0, 0.88]) cylinder(h = 0.04, r = 0.30, $fn = 9);
            color(hc_GREYC()) translate([0.45, 0, 0.86]) hc_boxc([0.5, 1.1, 0.05]);
        }
}

// 屋面排风口 + 天线杂项
module hc_part_roof_misc(seed = 0)
{
    color(hc_GREYC()) cylinder(h = 0.9, r = 0.28, $fn = 7);
    color(hc_DGREYC()) translate([0, 0, 0.9]) cylinder(h = 0.16, r = 0.38, $fn = 7);
    if (hc_rnd(seed, 2) == 0)
    {
        color(hc_DGREYC()) translate([1.4, 0.4, 0]) cylinder(h = 1.8, r = 0.05, $fn = 6);
        color(hc_REDC()) translate([1.4, 0.4, 1.85]) sphere(r = 0.10, $fn = 6);
    }
}

// 招牌字（fascia 上的立体字，居中，竖直面朝 -y）
module hc_part_sign_text(label, size = 0.7, c = hc_WHITEC())
{
    color(c) translate([-len(label) * size * 0.36, 0, -size * 0.45]) rotate([90, 0, 0]) linear_extrude(0.06) text(label, size = size);
}

// 直升机坪（圆坪 + 白圈 + H；置于屋面）
module hc_part_helipad(r = 3.4, base = hc_BLUEC())
{
    color(base) cylinder(h = 0.10, r = r, $fn = 9);
    color(hc_MARKC()) translate([0, 0, 0.10]) cylinder(h = 0.02, r = r * 0.82, $fn = 9);
    color(base) translate([0, 0, 0.12]) cylinder(h = 0.02, r = r * 0.68, $fn = 9);
    color(hc_MARKC()) translate([-0.95, -1.25, 0.14]) linear_extrude(0.04) text("H", size = 2.6);
}

// 入口雨棚（双柱 + 平板，front=-y 外挑）
module hc_part_entry_canopy(w = 4.5, d = 2.6, c = hc_REDC())
{
    color(hc_GREYC()) for (sx = [-1, 1]) translate([sx * (w / 2 - 0.3), -d + 0.3, 0]) cylinder(h = 2.9, r = 0.10, $fn = 6);
    color(c) translate([0, -d / 2, 2.9]) hc_boxc([w, d, 0.24]);
    color(hc_WHITEC()) translate([0, -d / 2, 3.08]) hc_boxc([w + 0.2, d + 0.2, 0.08]);
}

// ================= 建筑库（front = -y，底面 z=0） =================
// 医院：白体 + 蓝色窗带 + 入口雨棚 + 红十字 + 屋顶直升机坪（参考图左上）
module hc_bldg_hospital(L = 26, D = 15)
{
    H = 10.5;
    color(hc_WHITEC()) hc_slab(L, D, H);
    // 三层蓝色连续窗带（前后 + 侧）
    for (f = [0 : 2])
    {
        zc = 2.1 + f * 3.1;
        color(hc_GLASSB()) for (sy = [-1, 1]) translate([0, sy * (D / 2 + 0.05), zc]) hc_boxc([L - 2.2, 0.10, 1.5]);
        color(hc_GLASSB()) for (sx = [-1, 1]) translate([sx * (L / 2 + 0.05), 0, zc]) hc_boxc([0.10, D - 2.2, 1.5]);
        color(hc_WHITEC()) for (sy = [-1, 1], i = [-3 : 3]) translate([i * 3.2, sy * (D / 2 + 0.07), zc]) hc_boxc([0.30, 0.10, 1.6]);
    }
    // 入口：玻璃门 + 白雨棚 + 蓝字 + 红十字
    translate([0, -D / 2, 0]) hc_part_door_glass(3.0, 2.6);
    translate([0, -D / 2, 0]) hc_part_entry_canopy(6.0, 2.8, hc_WHITEC());
    color(hc_GLASSB()) translate([0, -D / 2 - 0.08, 4.6]) hc_boxc([7.8, 0.16, 1.1]);
    translate([0, -D / 2 - 0.17, 4.65]) hc_part_sign_text("HOSPITAL", 0.72, hc_WHITEC());
    color(hc_REDC()) translate([5.0, -D / 2 - 0.12, 4.6]) hc_boxc([0.32, 0.1, 1.0]);
    color(hc_REDC()) translate([5.0, -D / 2 - 0.12, 4.6]) hc_boxc([1.0, 0.1, 0.32]);
    // 东侧急诊门 + 坡道
    translate([L / 2 - 4, -D / 2, 0]) hc_part_door_glass(2.2, 2.5);
    color(hc_REDC()) translate([L / 2 - 4, -D / 2 - 0.06, 3.0]) hc_boxc([3.0, 0.12, 0.55]);
    translate([L / 2 - 4, -D / 2 - 0.16, 3.02]) hc_part_sign_text("ER", 0.42, hc_WHITEC());
    // 屋顶：女儿墙 + 设备 + 西端抬升机坪
    color(hc_GREYC()) translate([0, 0, H]) hc_slab(L - 0.4, D - 0.4, 0.12);
    translate([0, 0, H + 0.12]) hc_part_parapet(L, D, hc_WHITEC());
    translate([2.0, 2.5, H + 0.12]) hc_part_roof_ac(3);
    translate([8.5, -3.0, H + 0.12]) hc_part_roof_misc(1);
    color(hc_WHITEC()) translate([-7.5, 0, H + 0.12]) hc_slab(9.5, 9.5, 0.9);   // 机坪抬升台（坪面由 heli_01 锚点提供）
}

// 警局：白体 + 蓝带 + POLICE + 屋顶蓝色机坪（参考图右下）
module hc_bldg_police(L = 13, D = 10)
{
    H = 7.2;
    color(hc_WHITEC()) hc_slab(L, D, H);
    color(hc_POLBLUE()) translate([0, 0, 0.45]) hc_boxc([L + 0.16, D + 0.16, 0.9]);
    // 两层窗
    for (f = [0 : 1])
        translate([0, -D / 2 - 0.05, 2.2 + f * 3.0]) hc_part_win_row(L - 2.5, 4, 1.3, 1.4);
    color(hc_GLASSL()) for (sx = [-1, 1], f = [0 : 1])
        translate([sx * (L / 2 + 0.05), 0, 2.2 + f * 3.0]) hc_boxc([0.10, D * 0.5, 1.4]);
    // 蓝色檐带 + 字 + 警徽
    color(hc_POLBLUE()) translate([0, 0, H - 0.55]) hc_boxc([L + 0.2, D + 0.2, 1.1]);
    translate([0, -D / 2 - 0.16, H - 0.5]) hc_part_sign_text("POLICE", 0.66, hc_WHITEC());
    color(hc_YELLOWC()) translate([-4.2, -D / 2 - 0.14, H - 0.5]) rotate([90, 0, 0]) cylinder(h = 0.06, r = 0.42, $fn = 7);
    // 入口
    translate([0, -D / 2, 0]) hc_part_door_glass(2.2, 2.4);
    translate([0, -D / 2, 0]) hc_part_entry_canopy(3.6, 2.0, hc_POLBLUE());
    // 屋顶（坪面由 heli_02 锚点提供）+ 天线
    translate([0, 0, H]) hc_part_parapet(L, D, hc_WHITEC(), 0.5, 0.3);
    color(hc_DGREYC()) translate([-5.0, 3.4, H]) cylinder(h = 2.4, r = 0.06, $fn = 6);
    color(hc_REDC()) translate([-5.0, 3.4, H + 2.4]) sphere(r = 0.13, $fn = 6);
}

// 酒店：奶油色 4 层 + 满铺阳台 + 红檐 HOTEL（参考图右下角）
module hc_bldg_hotel(L = 16, D = 12, F = 4)
{
    H = F * 3.2 + 1.4;
    color(hc_CREAMC()) hc_slab(L, D, H);
    for (f = [1 : F])
    {
        zc = 1.0 + f * 3.2;
        translate([0, -D / 2 - 0.05, zc + 1.0]) hc_part_win_row(L - 3.0, 4, 1.5, 1.5);
        // 阳台
        color(hc_WHITEC()) translate([0, -D / 2 - 0.55, zc - 0.05]) hc_boxc([L - 2.4, 1.1, 0.12]);
        color(hc_WHITEC()) translate([0, -D / 2 - 1.05, zc + 0.40]) hc_boxc([L - 2.4, 0.07, 0.78]);
        color(hc_WHITEC()) for (sx = [-1, 1]) translate([sx * (L / 2 - 1.24), -D / 2 - 0.55, zc + 0.40]) hc_boxc([0.07, 1.0, 0.78]);
    }
    color(hc_GLASSL()) for (sx = [-1, 1], f = [1 : F])
        translate([sx * (L / 2 + 0.05), 0, 2.0 + f * 3.2]) hc_boxc([0.10, D * 0.5, 1.4]);
    // 入口大堂
    translate([0, -D / 2, 0]) hc_part_door_glass(2.8, 2.6);
    translate([0, -D / 2, 0]) hc_part_entry_canopy(5.0, 2.6, hc_REDC());
    // 屋顶红檐 + HOTEL 字
    color(hc_REDC()) translate([0, 0, H]) hc_boxc([L + 0.3, D + 0.3, 0.7]);
    translate([0, -D / 2 - 0.22, H]) hc_part_sign_text("HOTEL", 0.78, hc_WHITEC());
    translate([0, 0, H + 0.35]) hc_part_parapet(L, D, hc_CREAMC(), 0.4, 0.3);
    translate([-3.0, 2.0, H + 0.35]) hc_part_roof_ac(2);
}

// 超市：白盒 + 红色 fascia + 玻璃门面 + 购物车廊（参考图中上）
module hc_bldg_market(L = 22, D = 13, H = 6)
{
    color(hc_WHITEC()) hc_slab(L, D, H);
    // 玻璃门面
    color(hc_GLASSL()) translate([0, -D / 2 - 0.06, 1.7]) hc_boxc([L * 0.66, 0.12, 2.8]);
    color(hc_DGREYC()) for (i = [-3 : 3]) translate([i * L * 0.094, -D / 2 - 0.10, 1.7]) hc_boxc([0.10, 0.12, 2.8]);
    translate([-2.2, -D / 2 - 0.1, 0]) hc_part_door_glass(2.4, 2.5);
    // 红色 fascia + 字
    color(hc_REDC()) translate([0, -D / 2 - 0.14, H - 1.0]) hc_boxc([L * 0.45, 0.28, 1.7]);
    translate([0, -D / 2 - 0.32, H - 0.95]) hc_part_sign_text("SUPERMARKET", 0.62, hc_WHITEC());
    // 购物车廊（蓝棚 + 车）
    color(hc_BLUEC()) translate([L / 2 - 2.4, -D / 2 - 1.4, 2.2]) hc_boxc([3.4, 2.4, 0.16]);
    color(hc_GREYC()) for (sx = [-1, 1]) translate([L / 2 - 2.4 + sx * 1.5, -D / 2 - 2.4, 0]) cylinder(h = 2.2, r = 0.08, $fn = 6);
    for (i = [0 : 2])
        color(hc_METALC()) translate([L / 2 - 2.4, -D / 2 - 1.1 - i * 0.45, 0.45]) hc_boxc([0.8, 0.5, 0.5]);
    // 屋顶
    translate([0, 0, H]) hc_part_parapet(L, D, hc_WHITEC());
    translate([-6, 2.5, H]) hc_part_roof_ac(3);
    translate([6, -2.0, H]) hc_part_roof_misc(0);
    // 北侧卸货高台 + 卷帘门
    translate([3, D / 2, 0]) rotate([0, 0, 180]) hc_part_door_roller(3.2, 2.8);
    color(hc_DGREYC()) translate([3, D / 2 + 1.0, 0.55]) hc_boxc([4.8, 2.0, 1.1]);
}

// 汉堡店：白盒 + 红顶带 + 条纹遮阳棚 + 立柱汉堡招牌（参考图中左）
module hc_bldg_burger(L = 11, D = 9, H = 4.6)
{
    color(hc_WHITEC()) hc_slab(L, D, H);
    color(hc_REDC()) translate([0, 0, H - 0.40]) hc_boxc([L + 0.2, D + 0.2, 0.8]);
    translate([0, -D / 2 - 0.16, H - 0.38]) hc_part_sign_text("BURGER", 0.6, hc_WHITEC());
    // 门 + 窗 + 条纹棚
    translate([-2.8, -D / 2, 0]) hc_part_door_glass(1.8, 2.3);
    color(hc_GLASSL()) translate([1.6, -D / 2 - 0.05, 1.7]) hc_boxc([5.0, 0.10, 1.6]);
    translate([1.6, -D / 2, 2.85]) hc_part_awning(6.0, hc_REDC(), 7);
    color(hc_GLASSL()) translate([L / 2 + 0.05, -1.0, 1.6]) hc_boxc([0.10, 3.0, 1.4]);  // 取餐侧窗
    color(hc_REDC()) translate([L / 2 + 0.10, -1.0, 2.6]) hc_boxc([0.08, 3.4, 0.3]);
    // 屋顶
    translate([0, 0, H + 0.4]) hc_part_parapet(L, D, hc_WHITEC(), 0.4, 0.3);
    translate([-2.5, 1.5, H + 0.4]) hc_part_roof_ac(2);
    // 立柱汉堡招牌
    translate([-L / 2 - 2.0, -D / 2 + 1.0, 0])
    {
        color(hc_GREYC()) cylinder(h = 4.6, r = 0.14, $fn = 7);
        color(hc_CREAMC()) translate([0, 0, 4.7]) cylinder(h = 0.42, r1 = 0.95, r2 = 0.78, $fn = 9);
        color([0.55, 0.34, 0.18]) translate([0, 0, 4.5]) cylinder(h = 0.22, r = 1.0, $fn = 9);
        color(hc_LEAFC()) translate([0, 0, 4.42]) cylinder(h = 0.10, r = 1.04, $fn = 9);
        color(hc_CREAMC()) translate([0, 0, 4.18]) cylinder(h = 0.26, r = 0.95, $fn = 9);
    }
}

// 咖啡屋：奶油体 + 棕红条纹棚 + COFFEE HOUSE（参考图右中）
module hc_bldg_cafe(L = 10, D = 8, H = 4.2)
{
    color(hc_CREAMC()) hc_slab(L, D, H);
    color([0.62, 0.30, 0.24]) translate([0, -D / 2 - 0.12, H - 0.75]) hc_boxc([L + 0.2, 0.24, 1.3]);
    translate([0, -D / 2 - 0.30, H - 0.72]) hc_part_sign_text("COFFEE HOUSE", 0.46, hc_WHITEC());
    translate([-2.6, -D / 2, 0]) hc_part_door_glass(1.7, 2.3);
    color(hc_GLASSL()) translate([1.8, -D / 2 - 0.05, 1.6]) hc_boxc([4.6, 0.10, 1.7]);
    translate([0, -D / 2, 2.9]) hc_part_awning(L - 1.0, [0.62, 0.30, 0.24], 9);
    color(hc_GLASSL()) translate([L / 2 + 0.05, 0, 1.7]) hc_boxc([0.10, D * 0.5, 1.3]);
    translate([0, 0, H]) hc_part_parapet(L, D, hc_CREAMC(), 0.4, 0.3);
    translate([-1.5, 1.0, H]) hc_part_roof_ac(2);
}

// 理发店：白上身 + 深棕 fascia + 三色转灯（参考图中部）
module hc_bldg_barber(L = 9, D = 8, H = 4.4)
{
    color(hc_WHITEC()) hc_slab(L, D, H);
    color(hc_BROWND()) translate([0, -D / 2 - 0.12, 2.9]) hc_boxc([L + 0.2, 0.24, 1.1]);
    translate([0, -D / 2 - 0.30, 2.95]) hc_part_sign_text("BARBER SHOP", 0.44, hc_WHITEC());
    translate([-2.4, -D / 2, 0]) hc_part_door_solid([0.32, 0.24, 0.18], 1.0, 2.2);
    color(hc_GLASSL()) translate([1.4, -D / 2 - 0.05, 1.5]) hc_boxc([4.2, 0.10, 1.6]);
    color(hc_BROWND()) translate([1.4, -D / 2 - 0.4, 2.42]) rotate([22, 0, 0]) hc_boxc([4.6, 1.0, 0.07]);
    // 三色转灯
    translate([-3.6, -D / 2 - 0.42, 1.7])
    {
        color(hc_DGREYC()) cylinder(h = 0.10, r = 0.13, $fn = 7);
        for (i = [0 : 4])
            color(i % 3 == 0 ? hc_REDC() : (i % 3 == 1) ? hc_WHITEC() : hc_BLUEC())
                translate([0, 0, 0.10 + i * 0.14]) cylinder(h = 0.14, r = 0.11, $fn = 7);
        color(hc_DGREYC()) translate([0, 0, 0.80]) sphere(r = 0.12, $fn = 7);
    }
    translate([0, 0, H]) hc_part_parapet(L, D, hc_WHITEC(), 0.4, 0.3);
    translate([1.0, 1.0, H]) hc_part_roof_misc(2);
}

// 通用小商铺（西条带商铺排）
module hc_bldg_shop_unit(label = "BAKERY", c = hc_TEALC(), L = 9, D = 8, H = 4.2)
{
    color(hc_WHITEC()) hc_slab(L, D, H);
    color(c) translate([0, -D / 2 - 0.12, 2.9]) hc_boxc([L + 0.2, 0.24, 1.1]);
    translate([0, -D / 2 - 0.30, 2.95]) hc_part_sign_text(label, 0.46, hc_WHITEC());
    translate([-2.4, -D / 2, 0]) hc_part_door_glass(1.6, 2.3);
    color(hc_GLASSL()) translate([1.5, -D / 2 - 0.05, 1.5]) hc_boxc([4.0, 0.10, 1.6]);
    translate([1.5, -D / 2, 2.40]) hc_part_awning(4.6, c, 5);
    translate([0, 0, H]) hc_part_parapet(L, D, hc_WHITEC(), 0.4, 0.3);
    translate([-1.0, 1.2, H]) hc_part_roof_ac(1);
}

// 公寓楼：4 层窗阵 + 中柱阳台（参考图白色公寓）
module hc_bldg_apartment(L = 13, D = 11, F = 4, bc = hc_WHITEC(), seed = 0)
{
    H = F * 3.0 + 1.2;
    color(hc_DGREYC()) hc_slab(L + 0.3, D + 0.3, 0.5);
    color(bc) translate([0, 0, 0.5]) hc_slab(L, D, H - 0.5);
    for (f = [0 : F - 1])
    {
        zc = 2.4 + f * 3.0;
        translate([0, -D / 2 - 0.05, zc]) hc_part_win_row(L - 2.0, 3, 1.4, 1.5);
        translate([0, D / 2 + 0.05, zc]) rotate([0, 0, 180]) hc_part_win_row(L - 2.0, 3, 1.4, 1.5);
        color(hc_GLASSL()) for (sx = [-1, 1]) translate([sx * (L / 2 + 0.05), 0, zc]) hc_boxc([0.10, D * 0.45, 1.4]);
    }
    // 中柱阳台
    for (f = [1 : F - 1])
    {
        zc = 1.0 + f * 3.0;
        color(hc_WHITEC()) translate([0, -D / 2 - 0.5, zc]) hc_boxc([2.6, 1.0, 0.12]);
        color(hc_DGREYC()) translate([0, -D / 2 - 0.95, zc + 0.42]) hc_boxc([2.6, 0.06, 0.72]);
    }
    translate([0, -D / 2, 0]) hc_part_door_glass(1.8, 2.3);
    color(hc_DGREYC()) translate([0, -D / 2 - 0.7, 2.5]) hc_boxc([2.6, 1.4, 0.14]);
    translate([0, 0, H]) hc_part_parapet(L, D, bc, 0.5, 0.3);
    translate([1.0, 1.5, H]) hc_part_roof_ac(2);
    translate([-3.5, -2.0, H]) hc_part_roof_misc(seed);
}

// 独栋住宅：坡顶 + 门廊 + 烟囱（front=-y）
module hc_bldg_house(seed = 0)
{
    L = 9; D = 7; H = 3.0;
    bc = hc_house_c(seed); rc = hc_roof_c(seed + 2);
    color(bc) hc_slab(L, D, H);
    color(rc) translate([0, 0, H]) rotate([90, 0, 90])
        linear_extrude(L + 1.0, center = true) polygon([[-D / 2 - 0.5, 0], [D / 2 + 0.5, 0], [0, 2.2]]);
    // 前门廊
    color(hc_WALKC()) translate([-1.8, -D / 2 - 0.9, 0]) hc_slab(3.0, 1.8, 0.16);
    color(hc_WHITEC()) for (sx = [-1, 1]) translate([-1.8 + sx * 1.2, -D / 2 - 1.5, 0.16]) cylinder(h = 2.1, r = 0.09, $fn = 6);
    color(rc) translate([-1.8, -D / 2 - 0.85, 2.30]) hc_boxc([3.2, 2.0, 0.14]);
    translate([-1.8, -D / 2, 0.16]) hc_part_door_solid([0.42, 0.28, 0.16], 1.0, 2.1);
    // 窗
    color(hc_WHITEC()) translate([1.6, -D / 2 - 0.06, 1.7]) hc_boxc([1.7, 0.12, 1.3]);
    color(hc_GLASSL()) translate([1.6, -D / 2 - 0.10, 1.7]) hc_boxc([1.4, 0.08, 1.05]);
    color(hc_WHITEC()) translate([L / 2 + 0.05, 0.4, 1.7]) hc_boxc([0.12, 1.6, 1.2]);
    color(hc_GLASSL()) translate([L / 2 + 0.09, 0.4, 1.7]) hc_boxc([0.08, 1.3, 0.95]);
    color(hc_GREYC()) translate([L / 2 - 1.6, 1.4, H + 0.9]) hc_boxc([0.7, 0.7, 1.8]);
}

// 仓库：灰色波纹墙 + 双卷帘 + 装卸高台（front=-y 朝码头）
module hc_bldg_warehouse(L = 18, D = 12, H = 6.5)
{
    color([0.58, 0.60, 0.63]) hc_slab(L, D, H);
    color([0.50, 0.52, 0.55]) for (x = [-L / 2 + 1.5 : 3 : L / 2 - 1]) translate([x, -D / 2 - 0.03, H / 2]) hc_boxc([0.18, 0.06, H]);
    color([0.50, 0.52, 0.55]) translate([0, 0, H]) rotate([90, 0, 90])
        linear_extrude(L + 0.6, center = true) polygon([[-D / 2 - 0.3, 0], [D / 2 + 0.3, 0], [0, 1.6]]);
    color(hc_GLASSL()) translate([0, 0, H + 1.0]) hc_boxc([L - 3, 0.5, 0.5]);   // 屋脊采光带
    for (sx = [-1, 1]) translate([sx * 4.2, -D / 2, 1.1]) hc_part_door_roller(3.4, 3.2);
    color(hc_DGREYC()) translate([0, -D / 2 - 1.4, 0.55]) hc_boxc([14, 2.8, 1.1]);   // 装卸高台
    color(hc_YELLINE()) translate([0, -D / 2 - 2.75, 0.55]) hc_boxc([14, 0.10, 1.1]);
    translate([L / 2 - 1.5, -D / 2, 0]) hc_part_door_solid(hc_DGREYC(), 1.0, 2.2);
    color(hc_DARKC()) translate([-2, -D / 2 - 0.14, H - 0.85]) hc_boxc([7.4, 0.24, 1.1]);
    translate([-2, -D / 2 - 0.30, H - 0.80]) hc_part_sign_text("HARBOR LOGISTICS", 0.5, hc_YELLOWC());
}

// 加油站：黄棚 + 双泵岛 + 便利店（front=-y）
module hc_bldg_gas()
{
    // 便利店
    translate([6.0, 3.2, 0])
    {
        color(hc_WHITEC()) hc_slab(8, 6, 3.6);
        color(hc_REDC()) translate([0, 0, 3.6]) hc_slab(8.4, 6.4, 0.55);
        translate([0, -3.2, 0]) hc_part_door_glass(1.8, 2.3);
        color(hc_GLASSL()) translate([2.2, -3.05, 1.6]) hc_boxc([2.6, 0.10, 1.5]);
        translate([0, -3.42, 3.85]) hc_part_sign_text("GAS", 0.55, hc_WHITEC());
    }
    // 雨棚
    color(hc_GREYC()) for (px = [-4.5, 3.5]) translate([px - 1.0, 0, 0]) cylinder(h = 4.6, r = 0.24, $fn = 7);
    color(hc_WHITEC()) translate([-1.5, 0, 4.30]) hc_boxc([14.4, 9.9, 0.12]);
    color(hc_YELLOWC()) translate([-1.5, 0, 4.62]) hc_boxc([14.0, 9.6, 0.55]);
    color(hc_REDC()) translate([-1.5, -4.83, 4.62]) hc_boxc([14.0, 0.07, 0.57]);
    // 泵岛 ×2
    for (px = [-5.0, 2.0])
        translate([px, 0, 0])
        {
            color(hc_WALKC()) hc_slab(3.4, 1.7, 0.20);
            for (dx = [-0.8, 0.9])
            {
                color(hc_REDC()) translate([dx, 0, 0.20]) hc_slab(0.85, 0.6, 1.5);
                color(hc_DARKC()) translate([dx, -0.31, 1.30]) hc_boxc([0.5, 0.05, 0.32]);
                color(hc_WHITEC()) translate([dx, -0.31, 0.85]) hc_boxc([0.55, 0.04, 0.4]);
            }
        }
    // 价目牌
    color(hc_DGREYC()) translate([-10.0, -3.4, 0]) cylinder(h = 4.8, r = 0.16, $fn = 6);
    color(hc_YELLOWC()) translate([-10.0, -3.4, 4.8]) hc_boxc([2.0, 0.3, 1.7]);
    color(hc_DARKC()) translate([-10.0, -3.22, 4.8]) hc_boxc([1.5, 0.05, 1.1]);
}

// 篮球场：红场 + 蓝禁区 + 白线 + 双篮架 + 三面围栏（长边沿 x）
module hc_bldg_court(L = 17, W = 10)
{
    color(hc_COURTR()) hc_slab(L, W, 0.10);
    color(hc_COURTB()) for (sx = [-1, 1]) translate([sx * (L / 2 - 2.2), 0, 0.10]) hc_slab(4.4, 4.6, 0.014);
    color(hc_MARKC())
    {
        for (sy = [-1, 1]) translate([0, sy * (W / 2 - 0.2), 0.115]) hc_boxc([L - 0.2, 0.12, 0.014]);
        for (sx = [-1, 1]) translate([sx * (L / 2 - 0.2), 0, 0.115]) hc_boxc([0.12, W - 0.2, 0.014]);
        translate([0, 0, 0.115]) hc_boxc([0.12, W - 0.2, 0.014]);
        translate([0, 0, 0.11]) cylinder(h = 0.016, r = 1.6, $fn = 12);
    }
    color(hc_COURTR()) translate([0, 0, 0.118]) cylinder(h = 0.014, r = 1.35, $fn = 12);
    // 篮架
    for (sx = [-1, 1])
        translate([sx * (L / 2 - 0.6), 0, 0]) rotate([0, 0, (sx < 0) ? 0 : 180])
        {
            color(hc_DGREYC()) translate([-0.3, 0, 0]) cylinder(h = 3.4, r = 0.09, $fn = 6);
            color(hc_WHITEC()) translate([0.25, 0, 3.2]) hc_boxc([0.06, 1.6, 1.0]);
            color(hc_ORANGEC()) translate([0.45, 0, 2.95]) rotate([0, 90, 0]) cylinder(h = 0.03, r = 0.24, $fn = 8);
        }
    // 三面围栏（南侧开口）
    translate([-L / 2, W / 2 + 0.3, 0.1]) hc_prop_fence(L, 3.2);
    translate([-L / 2 - 0.3, -W / 2, 0.1]) rotate([0, 0, 90]) hc_prop_fence(W, 3.2);
    translate([L / 2 + 0.3, -W / 2, 0.1]) rotate([0, 0, 90]) hc_prop_fence(W, 3.2);
}

// 咖啡外摆桌（圆桌 + 双椅 + 伞）
module hc_furn_cafe_table(c = hc_REDC())
{
    color(hc_DGREYC()) cylinder(h = 0.72, r = 0.05, $fn = 6);
    color(hc_WHITEC()) translate([0, 0, 0.72]) cylinder(h = 0.04, r = 0.48, $fn = 9);
    for (a = [40, 220])
        rotate([0, 0, a]) translate([0.75, 0, 0])
        {
            color(hc_DGREYC()) translate([0, 0, 0.23]) hc_boxc([0.38, 0.38, 0.46]);
            color(hc_DGREYC()) translate([0.19, 0, 0.62]) hc_boxc([0.05, 0.38, 0.42]);
        }
    color(hc_GREYC()) translate([0, 0, 0.76]) cylinder(h = 1.5, r = 0.04, $fn = 6);
    color(c) translate([0, 0, 2.0]) cylinder(h = 0.5, r1 = 1.05, r2 = 0.08, $fn = 8);
}

// ================= 车辆（车头 +x，轮底 z=0） =================
module hc_veh_wheel(r = 0.34, w = 0.24)
{
    color(hc_DARKC()) rotate([90, 0, 0]) cylinder(h = w, r = r, center = true, $fn = 9);
    color(hc_GREYC()) rotate([90, 0, 0]) cylinder(h = w + 0.02, r = r * 0.45, $fn = 7);
}

module hc_veh_car(c = [0.85, 0.26, 0.20])
{
    color(c) translate([0, 0, 0.62]) hc_boxc([4.0, 1.75, 0.6]);
    color(c) translate([-0.2, 0, 1.18]) hc_boxc([2.1, 1.66, 0.52]);
    color(hc_GLASSL()) translate([-0.2, 0, 1.18]) hc_boxc([1.85, 1.74, 0.38]);
    color(hc_MARKC()) for (sy = [-1, 1]) translate([2.01, 0.58 * sy, 0.70]) hc_boxc([0.05, 0.3, 0.15]);
    color([0.80, 0.15, 0.12]) for (sy = [-1, 1]) translate([-2.01, 0.58 * sy, 0.70]) hc_boxc([0.05, 0.3, 0.15]);
    color(hc_DGREYC()) translate([2.0, 0, 0.45]) hc_boxc([0.06, 1.5, 0.22]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.25 * sx, 0.9 * sy, 0.34]) hc_veh_wheel();
}

module hc_veh_taxi()
{
    hc_veh_car(hc_YELLOWC());
    color(hc_DARKC()) translate([-0.2, 0, 1.50]) hc_boxc([0.62, 0.30, 0.18]);
    color(hc_WHITEC()) translate([-0.2, 0, 1.52]) hc_boxc([0.40, 0.32, 0.12]);
}

module hc_veh_police_car()
{
    hc_veh_car(hc_WHITEC());
    color(hc_POLBLUE()) translate([0, 0, 0.62]) hc_boxc([4.04, 1.79, 0.22]);
    color(hc_REDC()) translate([-0.45, 0, 1.50]) hc_boxc([0.30, 0.30, 0.14]);
    color(hc_BLUEC()) translate([0.05, 0, 1.50]) hc_boxc([0.30, 0.30, 0.14]);
}

// 救护车（白箱体 + 红十字 + 蓝灯）
module hc_veh_ambulance()
{
    color(hc_WHITEC()) translate([1.45, 0, 1.0]) hc_boxc([1.5, 2.0, 1.4]);
    color(hc_GLASSL()) translate([2.16, 0, 1.25]) hc_boxc([0.14, 1.7, 0.6]);
    color(hc_WHITEC()) translate([-0.7, 0, 1.25]) hc_boxc([2.9, 2.1, 1.9]);
    color(hc_REDC()) translate([-0.7, 0, 0.55]) hc_boxc([2.94, 2.14, 0.3]);
    color(hc_REDC()) for (sy = [-1, 1])
    {
        translate([-0.7, sy * 1.06, 1.45]) hc_boxc([0.9, 0.04, 0.26]);
        translate([-0.7, sy * 1.06, 1.45]) hc_boxc([0.26, 0.04, 0.9]);
    }
    color(hc_BLUEC()) translate([0.55, 0, 2.28]) hc_boxc([0.5, 1.2, 0.16]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([1.3 * sx, 0.95 * sy, 0.4]) hc_veh_wheel(0.4, 0.28);
}

module hc_veh_bus(c = hc_BLUEC())
{
    color(c) translate([0, 0, 1.35]) hc_boxc([7.6, 2.25, 1.95]);
    color(hc_GLASSL()) translate([0, 0, 1.80]) hc_boxc([7.2, 2.31, 0.72]);
    color(hc_GLASSL()) translate([3.76, 0, 1.55]) hc_boxc([0.14, 1.85, 0.95]);
    color(hc_MARKC()) translate([0, 0, 0.62]) hc_boxc([7.62, 2.27, 0.42]);
    color(hc_GLASSA()) translate([1.2, -1.14, 1.25]) hc_boxc([1.0, 0.04, 1.6]);   // 前门
    for (sx = [-1, 1], sy = [-1, 1]) translate([2.5 * sx, 1.0 * sy, 0.42]) hc_veh_wheel(0.42, 0.3);
}

module hc_veh_truck_box(cab = [0.30, 0.55, 0.80], box = hc_WHITEC())
{
    color(cab) translate([2.3, 0, 1.0]) hc_boxc([1.6, 2.1, 1.5]);
    color(hc_GLASSL()) translate([3.04, 0, 1.28]) hc_boxc([0.16, 1.8, 0.55]);
    color(box) translate([-0.6, 0, 1.45]) hc_boxc([4.2, 2.25, 2.1]);
    color(hc_GREYC()) translate([-2.72, 0, 1.45]) hc_boxc([0.06, 2.1, 1.9]);
    for (sx = [2.3, -1.8], sy = [-1, 1]) translate([sx, 1.0 * sy, 0.42]) hc_veh_wheel(0.42, 0.3);
}

// 集装箱卡车（拖头 + 板车 + 彩色箱）
module hc_veh_truck_ct(seed = 0)
{
    color(hc_REDC()) translate([3.3, 0, 1.1]) hc_boxc([1.8, 2.2, 1.7]);
    color(hc_GLASSL()) translate([4.14, 0, 1.45]) hc_boxc([0.16, 1.9, 0.6]);
    color(hc_DGREYC()) translate([-0.8, 0, 0.85]) hc_boxc([7.0, 2.2, 0.35]);
    color(hc_ctn_c(seed)) translate([-0.8, 0, 2.05]) hc_boxc([6.4, 2.3, 2.1]);
    for (sx = [3.3, -2.6, -4.0], sy = [-1, 1]) translate([sx, 1.0 * sy, 0.42]) hc_veh_wheel(0.42, 0.3);
}

// 叉车（码头用）
module hc_veh_forklift(c = hc_YELLOWC())
{
    color(c) translate([0, 0, 0.7]) hc_boxc([1.7, 1.1, 0.8]);
    color(hc_DARKC()) translate([-0.6, 0, 1.35]) hc_boxc([0.5, 1.0, 0.5]);
    color(c) for (sy = [-1, 1]) translate([-0.2, sy * 0.5, 1.65]) hc_boxc([1.3, 0.08, 0.08]);
    color(c) translate([-0.85, 0, 1.62]) hc_boxc([0.08, 1.0, 0.5]);
    color(hc_DGREYC()) for (sy = [-1, 1]) translate([1.0, sy * 0.35, 0.9]) hc_boxc([0.06, 0.06, 1.8]);
    color(hc_DGREYC()) translate([1.12, 0, 0.12]) hc_boxc([0.9, 0.9, 0.08]);
    color(hc_DGREYC()) for (sy = [-1, 1]) translate([1.45, sy * 0.3, 0.06]) hc_boxc([0.8, 0.12, 0.06]);
    for (sx = [-1, 1], sy = [-1, 1]) translate([0.55 * sx, 0.62 * sy, 0.26]) hc_veh_wheel(0.26, 0.2);
}

// 直升机（fly=false 旋翼静止十字）
module hc_veh_helicopter(c = hc_REDC(), fly = true)
{
    color(c) translate([0.2, 0, 0.95]) hc_boxc([2.6, 1.3, 1.1]);
    color(hc_GLASSD()) translate([1.45, 0, 1.0]) hc_boxc([0.3, 1.1, 0.8]);
    color(c) translate([-1.9, 0, 1.25]) hc_boxc([2.2, 0.3, 0.3]);
    color(c) translate([-3.0, 0, 1.7]) hc_boxc([0.25, 0.08, 0.9]);
    color(hc_DGREYC()) translate([-3.0, 0.1, 1.95]) rotate([90, 0, 0]) cylinder(h = 0.2, r = 0.4, $fn = 7);
    color(hc_DGREYC()) for (sy = [-1, 1])
    {
        translate([0.2, 0.62 * sy, 0.18]) hc_boxc([2.4, 0.12, 0.12]);
        translate([0.2, 0.62 * sy, 0.32]) hc_boxc([0.10, 0.10, 0.35]);
    }
    color(hc_DGREYC()) translate([0.2, 0, 1.5]) cylinder(h = 0.35, r = 0.13, $fn = 6);
    if (fly)
        color([0.55, 0.57, 0.60, 0.22]) translate([0.2, 0, 1.82]) cylinder(h = 0.05, r = 2.3, $fn = 14);
    else
    {
        color(hc_DGREYC()) translate([0.2, 0, 1.82]) hc_boxc([5.0, 0.22, 0.06]);
        color(hc_DGREYC()) translate([0.2, 0, 1.82]) hc_boxc([0.22, 5.0, 0.06]);
    }
}

// ================= 船舶（船头 +x，吃水线 z≈0 放于海面） =================
module hc_boat_speed(c = hc_WHITEC(), wake = true)
{
    color(c) translate([0, 0, 0.35]) hc_boxc([4.0, 1.5, 0.7]);
    color(c) hull()
    {
        translate([2.0, 0, 0.35]) hc_boxc([0.1, 1.5, 0.7]);
        translate([3.1, 0, 0.55]) hc_boxc([0.1, 0.4, 0.3]);
    }
    color(hc_GLASSD()) translate([0.9, 0, 0.85]) rotate([0, -18, 0]) hc_boxc([0.10, 1.2, 0.55]);
    color(hc_DGREYC()) translate([-0.6, 0, 0.78]) hc_boxc([1.2, 0.9, 0.20]);
    color(hc_DARKC()) translate([-2.05, 0, 0.45]) hc_boxc([0.3, 0.5, 0.55]);
    if (wake)
    {
        color(hc_FOAMC()) translate([-3.4, 0, 0.04]) hc_boxc([2.6, 0.9, 0.06]);
        color(hc_FOAMC()) translate([-5.6, 0, 0.03]) hc_boxc([1.8, 1.6, 0.05]);
    }
}

module hc_boat_sail(hc = [0.24, 0.32, 0.48])
{
    color(hc) translate([0, 0, 0.30]) hc_boxc([3.6, 1.2, 0.6]);
    color(hc) hull()
    {
        translate([1.8, 0, 0.30]) hc_boxc([0.1, 1.2, 0.6]);
        translate([2.6, 0, 0.45]) hc_boxc([0.1, 0.3, 0.3]);
    }
    color(hc_OAKC()) translate([0, 0, 0.6]) hc_boxc([3.2, 0.8, 0.10]);
    color(hc_GREYC()) translate([0.3, 0, 0.6]) cylinder(h = 4.6, r = 0.06, $fn = 6);
    color(hc_WHITEC()) translate([0.42, 0.03, 0.9]) rotate([90, 0, 0]) linear_extrude(0.05) polygon([[0, 0], [1.7, 0], [0, 3.9]]);
    color(hc_WHITEC()) translate([0.18, -0.03, 1.1]) rotate([90, 0, 180]) linear_extrude(0.05) polygon([[0, 0], [1.3, 0], [0, 3.3]]);
}

// 近海集装箱货轮（靠泊用；船头 +x）
module hc_boat_cargo(len = 44, hc = [0.62, 0.24, 0.20], seed = 0)
{
    W = len * 0.20;
    color(hc) translate([-len * 0.06, 0, 1.1]) hc_boxc([len * 0.82, W, 2.6]);
    color(hc) hull()
    {
        translate([len * 0.35, 0, 1.1]) hc_boxc([0.2, W, 2.6]);
        translate([len * 0.50, 0, 1.5]) hc_boxc([0.2, W * 0.25, 1.8]);
    }
    color(hc_DARKC()) translate([-len * 0.06, 0, -0.25]) hc_boxc([len * 0.82 + 0.04, W + 0.04, 0.9]);
    color(hc_WHITEC()) translate([-len * 0.06, 0, 2.42]) hc_boxc([len * 0.82, W + 0.06, 0.18]);
    // 艉楼
    color(hc_WHITEC()) translate([-len * 0.38, 0, 4.4]) hc_boxc([len * 0.12, W * 0.85, 6.0]);
    color(hc_GLASSD()) translate([-len * 0.38, 0, 6.6]) hc_boxc([len * 0.12 + 0.06, W * 0.75, 0.8]);
    color(hc_YELLOWC()) translate([-len * 0.33, 0, 7.6]) cylinder(h = 1.6, r = 0.5, $fn = 7);
    // 集装箱堆
    for (i = [0 : 4], sy = [-1, 1], lv = [0 : 1 + hc_rnd(seed + i, 2)])
        color(hc_ctn_c(seed + i * 2 + lv + sy))
            translate([len * 0.26 - i * len * 0.105, sy * W * 0.24, 2.5 + 1.05 + lv * 2.1]) hc_boxc([len * 0.095, W * 0.42, 2.1]);
    color(hc_GREYC()) translate([len * 0.44, 0, 2.4]) cylinder(h = 2.2, r = 0.10, $fn = 6);
}

module hc_prop_buoy(c = hc_REDC())
{
    color(c) cylinder(h = 0.85, r1 = 0.42, r2 = 0.16, $fn = 7);
    color(hc_WHITEC()) translate([0, 0, 0.85]) sphere(r = 0.14, $fn = 6);
}

module hc_prop_gull()
{
    color(hc_WHITEC())
    {
        translate([0.45, 0, 0.12]) rotate([0, -14, 0]) hc_boxc([0.95, 0.1, 0.05]);
        translate([-0.45, 0, 0.12]) rotate([0, 14, 0]) hc_boxc([0.95, 0.1, 0.05]);
        hc_boxc([0.32, 0.30, 0.12]);
    }
}

// ================= 港区设施 =================
// 单个集装箱（门朝 -y）
module hc_prop_container(c = hc_REDC())
{
    color(c) translate([0, 0, 1.05]) hc_boxc([5.4, 2.3, 2.1]);
    color([c[0] * 0.82, c[1] * 0.82, c[2] * 0.82]) for (x = [-2.2 : 0.55 : 2.2])
        translate([x, 0, 1.05]) hc_boxc([0.16, 2.34, 1.9]);
}

// 集装箱堆（2-3 层，seed 配色）
module hc_prop_container_stack(seed = 0, n = 2)
{
    for (lv = [0 : n - 1])
        translate([hc_rnd(seed + lv, 2) * 0.15, 0, lv * 2.1]) hc_prop_container(hc_ctn_c(seed + lv * 3));
}

// 码头岸吊（门式基座 + 斜臂 + 吊缆 + 悬挂集装箱；臂朝 -y 伸向海）
module hc_prop_crane(c = hc_ORANGEC())
{
    // 门式基座（四腿 + 横梁，卡车可穿行）
    color(c) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 2.6, sy * 2.0, 3.0]) hc_boxc([0.55, 0.55, 6.0]);
    color(c) for (sy = [-1, 1]) translate([0, sy * 2.0, 6.0]) hc_boxc([5.75, 0.5, 0.5]);
    color(c) translate([0, 0, 6.4]) hc_boxc([6.2, 4.8, 0.8]);
    // 塔身 + 司机室
    color(c) translate([0, 0.8, 8.6]) hc_boxc([1.4, 1.4, 3.6]);
    color(hc_DGREYC()) translate([0, -0.9, 9.6]) hc_boxc([1.7, 1.9, 1.5]);
    color(hc_GLASSL()) translate([0, -1.86, 9.6]) hc_boxc([1.4, 0.06, 1.0]);
    // 斜臂 + 拉索
    color(c) translate([0, -4.4, 11.6]) rotate([18, 0, 0]) hc_boxc([0.9, 11.0, 0.7]);
    color(hc_DARKC()) translate([0, -2.2, 11.9]) rotate([47, 0, 0]) hc_boxc([0.10, 5.6, 0.10]);
    // 吊缆 + 吊具 + 悬箱
    color(hc_DARKC()) translate([0, -7.6, 8.2]) hc_boxc([0.08, 0.08, 4.6]);
    color(hc_YELLOWC()) translate([0, -7.6, 5.9]) hc_boxc([2.6, 1.2, 0.4]);
    translate([0, -7.6, 3.6]) rotate([0, 0, 90]) hc_prop_container(hc_ctn_c(7));
}

// 系船柱（dock 锚点用：双柱）
module hc_prop_bollard_pair()
{
    for (sx = [-1, 1])
        translate([sx * 1.6, 0, 0])
        {
            color(hc_DARKC()) cylinder(h = 0.55, r = 0.22, $fn = 7);
            color(hc_DARKC()) translate([0, 0, 0.55]) sphere(r = 0.26, $fn = 7);
        }
}

// 木箱货堆
module hc_prop_crate_pile(seed = 0)
{
    color(hc_OAKC()) translate([0, 0, 0.45]) hc_boxc([0.9, 0.9, 0.9]);
    color([0.68, 0.50, 0.30]) translate([1.0, 0.2, 0.35]) hc_boxc([0.7, 0.7, 0.7]);
    if (hc_rnd(seed, 2) == 0) color(hc_OAKC()) translate([0.4, -0.1, 1.25]) rotate([0, 0, 20]) hc_boxc([0.7, 0.7, 0.7]);
}

// 救生圈桩
module hc_prop_lifering_post()
{
    color(hc_WHITEC()) cylinder(h = 1.2, r = 0.07, $fn = 6);
    color(hc_ORANGEC()) translate([0, 0.02, 0.95]) rotate([90, 0, 0]) cylinder(h = 0.1, r = 0.32, $fn = 9);
    color(hc_WHITEC()) translate([0, -0.04, 0.95]) rotate([90, 0, 0]) cylinder(h = 0.04, r = 0.18, $fn = 8);
}

// 码头办公小屋（front=-y；游艇码头用）
module hc_prop_harbor_hut()
{
    color(hc_TEALC()) hc_slab(4.2, 3.4, 2.6);
    color(hc_WHITEC()) translate([0, 0, 2.6]) rotate([90, 0, 90]) linear_extrude(4.8, center = true) polygon([[-1.9, 0], [1.9, 0], [0, 0.9]]);
    translate([-1.0, -1.7, 0]) hc_part_door_solid(hc_WHITEC(), 0.9, 2.1);
    color(hc_GLASSL()) translate([1.1, -1.76, 1.6]) hc_boxc([1.3, 0.10, 0.9]);
    color(hc_WHITEC()) translate([0, -1.85, 2.78]) hc_boxc([3.0, 0.10, 0.5]);
    translate([0, -1.92, 2.80]) hc_part_sign_text("MARINA", 0.34, hc_DGREYC());
    translate([2.6, -1.2, 0]) hc_prop_lifering_post();
}

// 木栈桥（沿 -y 伸入海；板面顶 z≈0.3，桩入水）
module hc_prop_pier(len = 22, w = 4)
{
    for (yy = [-0.7 : -1.3 : -len])
        color(hc_OAKC()) translate([0, yy, 0.20]) hc_boxc([w, 1.15, 0.20]);
    for (yy = [-2 : -6 : -len])
        color([0.55, 0.40, 0.24]) for (sx = [-1, 1])
            translate([sx * (w / 2 - 0.35), yy, -1.6]) cylinder(h = 2.0, r = 0.24, $fn = 6);
    // 白色护柱
    for (yy = [-4 : -6 : -len + 2])
        color(hc_WHITEC()) for (sx = [-1, 1]) translate([sx * (w / 2 - 0.25), yy, 0.30]) cylinder(h = 0.85, r = 0.10, $fn = 6);
}

// ================= 沙滩小品 =================
module hc_beach_umbrella(c = hc_REDC())
{
    color(hc_WHITEC()) cylinder(h = 2.0, r = 0.06, $fn = 6);
    color(c) translate([0, 0, 1.55]) cylinder(h = 0.65, r1 = 1.35, r2 = 0.06, $fn = 8);
}

module hc_beach_lounger(c = hc_BLUEC())
{
    color(hc_WHITEC()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.75, sy * 0.35, 0.16]) hc_boxc([0.10, 0.10, 0.32]);
    color(c) translate([-0.25, 0, 0.36]) hc_boxc([1.45, 0.85, 0.10]);
    color(c) translate([0.78, 0, 0.62]) rotate([0, -38, 0]) hc_boxc([0.85, 0.85, 0.10]);
}

module hc_beach_towel(c = hc_YELLOWC())
{
    color(c) hc_slab(1.0, 1.9, 0.04);
    color(hc_WHITEC()) translate([0, 0.7, 0.04]) hc_boxc([1.0, 0.3, 0.045]);
}

module hc_beach_lifeguard()
{
    color(hc_WHITEC()) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 1.0, sy * 0.9, 1.0]) hc_boxc([0.18, 0.18, 2.0]);
    color(hc_REDC()) translate([0, 0, 2.0]) hc_slab(2.8, 2.4, 0.18);
    color(hc_WHITEC()) translate([0, 0.2, 2.18]) hc_slab(2.4, 2.0, 1.5);
    color(hc_REDC()) translate([0, 0.2, 3.68]) rotate([90, 0, 90]) linear_extrude(2.8, center = true) polygon([[-1.3, 0], [1.3, 0], [0, 0.8]]);
    color(hc_GLASSL()) translate([0, -0.82, 2.95]) hc_boxc([1.5, 0.08, 0.7]);
    color(hc_OAKC()) translate([0, -2.2, 1.0]) rotate([-38, 0, 0]) hc_boxc([0.9, 3.2, 0.10]);
    color(hc_ORANGEC()) translate([1.3, 0.2, 4.5]) hc_boxc([0.55, 0.04, 0.4]);
    color(hc_REDC()) translate([1.05, 0.2, 4.1]) cylinder(h = 0.9, r = 0.05, $fn = 6);
}

module hc_beach_hut(c = hc_TEALC())
{
    color(c) hc_slab(3.2, 2.6, 2.3);
    color(hc_WHITEC()) translate([0, -1.32, 1.0]) hc_boxc([0.9, 0.08, 1.9]);
    color(hc_WHITEC()) translate([0, 0, 2.3]) rotate([90, 0, 90]) linear_extrude(3.6, center = true) polygon([[-1.5, 0], [1.5, 0], [0, 0.9]]);
    color(hc_WHITEC()) for (sx = [-1, 1]) translate([sx * 1.0, -1.32, 1.75]) hc_boxc([0.7, 0.06, 0.7]);
}

// 北侧丘陵（低细节背景；底面 z=0）
module hc_nature_hill(r = 16, h = 9)
{
    color([0.50, 0.72, 0.32]) cylinder(h = h, r1 = r, r2 = r * 0.45, $fn = 8);
    color([0.56, 0.77, 0.36]) translate([r * 0.2, -r * 0.15, h * 0.8]) cylinder(h = h * 0.45, r1 = r * 0.42, r2 = r * 0.12, $fn = 7);
}

// 停车位挡轮石（park 锚点用）
module hc_prop_wheel_stop()
{
    color(hc_YELLINE()) translate([0, 0, 0.07]) hc_boxc([1.6, 0.18, 0.14]);
    color(hc_MARKC()) translate([0, -0.6, 0.008]) hc_boxc([0.5, 0.5, 0.016]);
}

// 加油位地标（fuel 锚点用）
module hc_prop_fuel_mark()
{
    color(hc_YELLINE())
    {
        for (sy = [-1, 1]) translate([0, sy * 1.0, 0.008]) hc_boxc([4.0, 0.12, 0.016]);
        for (sx = [-1, 1]) translate([sx * 2.0, 0, 0.008]) hc_boxc([0.12, 2.0, 0.016]);
    }
}
