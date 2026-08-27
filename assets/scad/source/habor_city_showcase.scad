// habor_city_showcase.scad —— kit_city_hd + kit_city_blocks 零件总览
// gnb shot --scene assets/scad/source/habor_city_showcase.scad

use <../lib/kit_city_hd.scad>
use <../lib/kit_city_blocks.scad>

$fn = 12;

// 小型展台：上排展示可组合街区，下排展示 HD 单体、交通与港口零件。
color(hc_BASEC()) translate([0, 5, -0.22]) cube([168, 108, 0.44], center = true);

// 组合街区（kit_city_blocks）
translate([-52, 27, 0]) v2_block_houses(seed = 1);
translate([0, 27, 0]) v2_block_market(seed = 2);
translate([52, 27, 0]) v2_block_waterfront(seed = 3);

// HD 建筑、街具与自然（kit_city_hd）
translate([-60, -31, 0]) hc_bldg_hospital();
translate([-36, -31, 0]) hc_bldg_hotel(F = 4);
translate([-13, -31, 0]) hc_bldg_market();
translate([11, -31, 0]) hc_bldg_house(seed = 4);
translate([29, -31, 0]) hc_veh_bus();
translate([42, -31, 0]) hc_veh_car();
translate([56, -31, 0]) hc_prop_lamp();
translate([67, -31, 0]) hc_nature_tree();

// 港口道具
translate([-57, -49, 0]) hc_boat_speed();
translate([-29, -49, 0]) hc_prop_container_stack(seed = 5, n = 3);
translate([5, -49, 0]) hc_prop_crane();
translate([32, -49, 0]) hc_prop_pier(len = 22, w = 4);
translate([61, -49, 0]) hc_prop_harbor_hut();
