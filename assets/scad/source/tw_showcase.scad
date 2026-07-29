use <../lib/kit_tw.scad>
use <../lib/kit_overhill.scad>
$fn=10;
color([0.27,0.34,0.18]) translate([0,0,-0.12]) cube([54,30,0.2], center=true);

translate([-19,7,0]) tw_bldg_watchtower(1);
translate([-9,7,0]) tw_bldg_gatehouse(6,2);
translate([2,7,0]) tw_bldg_hut(3);
translate([12,7,0]) tw_bldg_palisade(9,2.7,4);
translate([21,7,0]) tw_prop_cart(5);

translate([-19,-6,0]) tw_prop_banner(1,[0.15,0.32,0.75]);
translate([-14,-6,0]) tw_prop_stakes(2);
translate([-8,-6,0]) tw_prop_haystack(3);
translate([-2,-6,0]) tw_prop_marker(4);

// 三兵种部件特写：头、躯干、武器臂、腿按列陈列。
for (i=[0:2]) {
    translate([6+i*7,-7,1.35]) tw_head_soldier(i);
    translate([6+i*7,-7,0.75]) tw_torso_soldier(i, i==0?[0.12,0.32,0.72]:i==1?[0.65,0.16,0.11]:[0.20,0.48,0.25]);
    translate([5.5+i*7,-7,1.0]) tw_arm_soldier(i,[0.25,0.35,0.55],i+1);
    translate([6+i*7,-7,0.45]) tw_leg_soldier(i);
}
