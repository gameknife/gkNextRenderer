// layout_demo.scad —— kit_layout 布局组合子库演示（街区级）
//
// 演示 6 个组合子怎么把 kit 零件按规则组成场景（docs/designs/scad-scene-compose-design.md §5）：
//   lay_grid    民居区网格（$seed 尺寸/式样变体）
//   lay_jitter  每格随机抖动（去除整齐感）
//   lay_ring    市场摊位环阵（front 朝心）
//   lay_along   路灯/灯笼沿折线撒点
//   lay_row     沿街车位直线阵
//   lay_pick    每个车位/树位从候选里确定性选型
//   lay_scatter 南侧树林散布
// 所有随机由 seed 派生，确定性可回归。

use <lib/kit_layout.scad>
use <lib/kit_old_city.scad>
use <lib/kit_city_hd.scad>

$fn = 12;

// ================= 地面与步道 =================
color([0.55, 0.66, 0.42]) translate([0, 0, -0.2]) cube([170, 130, 0.4], center = true);
color([0.72, 0.70, 0.64]) translate([0, 6, -0.02]) cube([160, 6, 0.24], center = true);
color([0.72, 0.70, 0.64]) translate([-30, 0, -0.02]) cube([6, 110, 0.24], center = true);

// ================= 民居区：网格 + 抖动 + 变体 =================
translate([-42, 36, 0])
    lay_grid(4, 3, 16, 14, seed = 7)
        lay_jitter($seed, 1.2, 1.0, 12)
            oc_bldg_house(seed = $seed, L = 8 + lay_randi($seed, 5, 3), D = 6);

// ================= 市场：水井 + 摊位环阵 + 罐子散布 =================
translate([44, 38, 0])
{
    oc_prop_well();
    lay_ring(8, 11, seed = 3, face = 1) oc_bldg_stall(seed = $seed);
    lay_scatter(6, -16, 16, -16, 16, seed = 9, rot = true) oc_prop_jar();
}

// ================= 街道：路灯与灯笼沿线 =================
lay_along([[-78, 10.6], [78, 10.6]], step = 16, seed = 1) hc_prop_lamp();
lay_along([[-25.6, -50], [-25.6, 50]], step = 18, seed = 2, offset = 8) oc_prop_lantern();

// ================= 沿街车位：直线阵 + 选型 =================
translate([-20, -1, 0])
    lay_row(6, 13, seed = 4)
        rotate([0, 0, 90])
            lay_pick($seed)
            {
                hc_veh_car(hc_car_c($seed));
                hc_veh_taxi();
                hc_veh_bus();
                hc_veh_truck_box();
            }

// ================= 南侧树林：散布 + 选型 =================
lay_scatter(16, -80, 80, -58, -22, seed = 11)
    lay_pick($seed)
    {
        oc_nature_tree(1.0 + lay_randf($seed, 4) * 0.6, $seed);
        oc_nature_pine(1.1 + lay_randf($seed, 5) * 0.4);
        oc_nature_rock(1.0, $seed);
        oc_nature_tree(0.8, $seed + 1);
    }
