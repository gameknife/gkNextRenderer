// beer_cup.scad —— 带把手的啤酒杯样例(半透明玻璃 / 啤酒 / 泡沫 / 气泡)
//
// 尺度:1 unit = 1 m,OpenSCAD Z-up;杯高 0.09m,整体包围盒 < 0.12m。
// 作为 ScadStudio 默认样例与 ScadLoader 单元测试样本:
//   * 玻璃杯体 alpha = 0.28 -> Dielectric 材质(Test_ScadLoader 以该 alpha 定位杯体桶)
//   * difference 挖空杯体:杯口敞开(顶面无中心顶点),内壁半径 0.0235
//   * rotate_extrude 把手 / sphere 泡沫,全特性 0 warning

$fn = 48;

// 玻璃杯体:外径 0.025,内径 0.0235,底厚 0.004,敞口
color([0.85, 0.92, 0.97], 0.28)
difference()
{
    cylinder(h = 0.09, r = 0.025);
    translate([0, 0, 0.004]) cylinder(h = 0.096, r = 0.0235);
}

// 把手:整环圆环体,内缘贴杯壁
color([0.85, 0.92, 0.97], 0.30)
translate([0.0355, 0, 0.045])
rotate([90, 0, 0])
rotate_extrude()
translate([0.0105, 0, 0])
circle(r = 0.0035);

// 啤酒:琥珀色液体
color([0.95, 0.65, 0.12], 0.75)
translate([0, 0, 0.006])
cylinder(h = 0.066, r = 0.0228);

// 泡沫:压扁球体盖在酒面
color([0.98, 0.96, 0.90])
translate([0, 0, 0.074])
scale([1, 1, 0.45])
sphere(r = 0.0225);

// 泡沫溢边:杯口小球
color([0.98, 0.96, 0.90])
for (i = [0 : 3])
    rotate([0, 0, i * 90 + 30])
    translate([0.021, 0, 0.082])
    sphere(r = 0.004);

// 气泡:啤酒内的小气泡
color([0.99, 0.9, 0.6], 0.85)
for (i = [0 : 4])
    rotate([0, 0, i * 72])
    translate([0.008 + (i % 3) * 0.004, 0, 0.02 + i * 0.01])
    sphere(r = 0.0012);
