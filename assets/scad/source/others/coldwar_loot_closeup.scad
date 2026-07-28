// coldwar_loot_closeup.scad —— 武器/物资特写验证（临时验收用）
use <../../lib/kit_coldwar.scad>

$fn = 12;

color([0.42, 0.42, 0.40]) translate([0, 0, -0.06]) cube([13, 8, 0.12], center = true);

translate([-5, 2.6, 0]) cw_wpn_ak(seed = 0);
translate([-2.5, 2.6, 0]) cw_wpn_svd(seed = 0);
translate([0.5, 2.6, 0]) cw_wpn_mosin(seed = 0);
translate([3, 2.6, 0]) cw_wpn_shotgun(seed = 0);
translate([5, 2.6, 0]) cw_wpn_pistol(seed = 0);

translate([-5, 1.2, 0]) cw_wpn_rpg(seed = 0);
translate([-2.5, 1.2, 0]) cw_wpn_crate(seed = 0);
translate([0.5, 1.2, 0]) cw_item_crate_supply(seed = 0);
translate([2.5, 1.2, 0]) cw_item_backpack(seed = 0);
translate([4.5, 1.2, 0]) cw_item_bedroll(seed = 0);

translate([-5, -0.6, 0]) cw_item_can(seed = 1);
translate([-3.5, -0.6, 0]) cw_item_jerrycan(seed = 0);
translate([-2, -0.6, 0]) cw_item_medkit();
translate([-0.5, -0.6, 0]) cw_item_ammobox(seed = 0);
translate([1, -0.6, 0]) cw_item_radio(seed = 0);
translate([2.5, -0.6, 0]) cw_item_lantern(seed = 0);
translate([4, -0.6, 0]) cw_item_helmet(seed = 0);

translate([-4.5, -2.2, 0]) cw_prop_campfire(seed = 0);
translate([-1.5, -2.2, 0]) cw_prop_crate_ammo(seed = 1);
translate([1.5, -2.2, 0]) cw_prop_pump_gas(seed = 0);
translate([3.5, -2.2, 0]) cw_prop_cart_shop(seed = 0);
translate([5.5, -2.2, 0]) cw_prop_hedgehog();
