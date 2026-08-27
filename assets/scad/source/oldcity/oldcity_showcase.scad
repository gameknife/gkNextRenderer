// oldcity_showcase.scad —— kit_old_city 零件总览（按类别排列，验收/选型用）
// gnb shot --scene assets/scad/source/oldcity/oldcity_showcase.scad

use <../../lib/kit_old_city.scad>

$fn = 12;

// 展台：上排建筑，中排城墙/门楼，下排街具与地景。
color(oc_BASEC()) translate([0, 0, -0.22]) cube([132, 88, 0.44], center = true);

// 建筑
translate([-45, 25, 0]) oc_bldg_gatehouse("北门");
translate([-22, 25, 0]) oc_bldg_corner_tower();
translate([-1, 25, 0]) oc_bldg_house(seed = 1);
translate([14, 25, 0]) oc_bldg_inn();
translate([34, 25, 0]) oc_bldg_barracks(seed = 2);
translate([52, 25, 0]) oc_bldg_warehouse(seed = 3);

// 城墙与公共结构
translate([-45, 5, 0]) oc_wall_run(18);
translate([-24, 5, 0]) oc_wall_bastion(flag = 0);
translate([-5, 5, 0]) oc_wall_ramp(len = 16, w = 3);
translate([19, 5, 0]) oc_prop_paifang();
translate([38, 5, 0]) oc_bldg_inner_gate();

// 街道小品与经营道具
translate([-43, -15, 0]) oc_prop_well();
translate([-31, -15, 0]) oc_prop_stone_lamp();
translate([-22, -15, 0]) oc_prop_cart(seed = 1);
translate([-9, -15, 0]) oc_prop_crates(seed = 2);
translate([2, -15, 0]) oc_prop_rack();
translate([14, -15, 0]) oc_prop_target();
translate([26, -15, 0]) oc_prop_flag();
translate([38, -15, 0]) oc_prop_paifang();

// 植被与自然地景
translate([-42, -34, 0]) oc_nature_tree(s = 1.2, i = 1);
translate([-29, -34, 0]) oc_nature_pine(s = 1.0);
translate([-17, -34, 0]) oc_nature_rock(s = 1.2, i = 2);
translate([2, -34, 0]) oc_nature_hill(r = 7, h = 5);
translate([20, -34, 0]) oc_prop_lantern();
translate([30, -34, 0]) oc_prop_jar();
translate([40, -34, 0]) oc_prop_hay();
