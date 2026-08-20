// geo_city_probe.scad —— kit_geo_city / kit_road 街面装饰的探针场景
//
// `gnb geo` 生成的 tile 有上千栋楼、几万个几何件，改一条腰线规则要重跑整条管线
// 再从鸟瞰图里找那一栋楼，反馈慢得没法迭代。这个场景把两条规则库的**全部方案**
// 各摆一份放在一条街边上，配街面机位：改 kit 后一条 `gnb shot` 就能看清楚。
//
//   gnb shot --scene assets/scad/source/geo_city_probe.scad
//
// 这里不接真实 tile 数据：地形是一块平板，建筑轮廓是手写的，z 直接给常数。
// 目的是看**规则**对不对，不是看某个地点对不对。

use <../lib/gk_camera.scad>
use <../lib/kit_road.scad>
use <../lib/kit_geo_city.scad>

$fn = 8;

// 平地形。真实 tile 用 .hmap，这里只要一个能让 kit_road 采样的句柄。
TERR = ["gkterr1", [400, 400], [64, 64], 5,
        [0, 0, 0], undef, "urban", []];

// ---------------------------------------------------------------- 建筑
//
// 一排展示楼，从西往东：素面 / 幕墙塔 / 现代板楼 / 砌体开窗 / 厂房 / 低层商住。
// 每种都给一个"矮的坡屋顶版本"摆在它北面，这样六种立面 x 两种屋顶一屏看全。

// 一栋展示楼：立面方案 fac、层高 fh、高度 h。
// 轮廓一律 CCW，和生成器发出的一致（gc_ring_offset 的外法线靠这个绕向）。
module probe_tower(cx, fac, h, fh, wallTone, glassTone, clutter)
{
    // roof 的后三位是屋面杂物的锚点（轮廓内的点 + 安全半径），真实 tile 由
    // 生成器的 RoofAnchor 算出；这里轮廓是正方形，中心 + 半边长即可。
    gc_bld([[cx - 9, -9], [cx + 9, -9], [cx + 9, 9], [cx - 9, 9]],
           0, h, [fac, wallTone, glassTone, fh, cx * 7 + 13],
           [1, 2, 0, 0, clutter, cx, 0, 9], [cx, 0, 18, 18, 0], 1.0);
}

// 一栋低矮坡屋顶房：脊沿长轴（obb 的 w），ridgeFrac = 1 是双坡、< 1 是四坡。
module probe_house(cx, roofTone, ridgeFrac, wallTone)
{
    gc_bld([[cx - 7, 34], [cx + 7, 34], [cx + 7, 45], [cx - 7, 45]],
           0, 7, [5, wallTone, 3, 3.0, cx * 11 + 5],
           [2, roofTone, 3.4, ridgeFrac, 0, cx, 39.5, 5.5], [cx, 39.5, 14, 11, 0], 1.0);
}

module probe_buildings()
{
    gk_flatten()
    {
        probe_tower(-95, 0, 9, 3.2, 0, 2, 0);    // 素面：工棚 / 极小体量
        probe_tower(-57, 1, 140, 3.9, 3, 1, 3);  // 玻璃幕墙塔楼
        probe_tower(-19, 2, 62, 3.1, 1, 0, 2);   // 现代板楼
        probe_tower(19, 3, 24, 3.2, 7, 2, 1);    // 砌体开窗（欧洲旧城）
        probe_tower(57, 4, 14, 5.2, 4, 3, 1);    // 厂房：实墙 + 带窗
        probe_tower(95, 5, 9, 3.4, 2, 0, 0);     // 低层沿街商住

        probe_house(-76, 0, 1.0, 2);   // 红瓦双坡
        probe_house(-38, 1, 0.45, 0);  // 石板四坡
        probe_house(0, 2, 0.08, 7);    // 水泥攒尖
        probe_house(38, 3, 1.0, 4);    // 深灰双坡
        probe_house(76, 4, 0.6, 3);    // 褐瓦四坡
    }
}

// ---------------------------------------------------------------- 街道
//
// 一条 22m 宽的直路（够宽 => 有中线和斑马线）和一条 7m 的背街小巷。
// 站距按生成器的 stationStepM = 5m 铺，街具间距才和真实 tile 一致。

function probe_edge(y, n) = [for (i = [0 : n - 1]) [-110 + i * 5, y]];

module probe_street()
{
    rd_network(TERR, rd_ASPHALT(),
               [[probe_edge(-25, 45), probe_edge(-47, 45)]],
               [], [22], true, true, true, 3);
}

module probe_alley()
{
    rd_network(TERR, rd_SERVICE(),
               [[probe_edge(22, 45), probe_edge(15, 45)]],
               [], [7], false, false, false, 9);
}

// ---------------------------------------------------------------- 装配

gk_terrain(TERR);
probe_street();
probe_alley();
probe_buildings();

// 街面机位在前：改 kit 最常看的就是它（勒脚、雨篷、路缘、路灯、斑马线）。
gk_camera_lookat([-30, -70, 6.5], [-20, -20, 14], "street", 62);
gk_camera_lookat([-40, -120, 42], [-20, 0, 30], "block", 55);
gk_camera_lookat([0, -190, 130], [0, 10, 30], "skyline", 48);
gk_camera_lookat([0, 0, 260], [0, 6, 0], "top", 60);
