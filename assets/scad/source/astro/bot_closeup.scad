// bot_closeup.scad —— kit_astro 角色 / 收集物 / 小道具特写验收（16×9 m 灰台）
// 在全景 showcase 里这些只有几个像素，比例、朝向、穿插问题要在这里看。
// gnb shot --scene assets/scad/source/astro/bot_closeup.scad

use <../../lib/kit_astro.scad>
use <../../lib/gk_camera.scad>

$fn = 12;

color([0.40, 0.42, 0.40]) translate([0, 0, -0.06]) cube([17, 9.5, 0.12], center = true);

// 第一排：主角机器人姿态与帽饰
translate([-6.5, 3.2, 0]) ab_char_bot(seed = 0, pose = 0, hat = 0);
translate([-4.0, 3.2, 0]) ab_char_bot(seed = 1, pose = 1, hat = 2);
translate([-1.5, 3.2, 0]) ab_char_bot(seed = 2, pose = 2, hat = 3);
translate([1.0, 3.2, 0]) ab_char_bot(seed = 3, pose = 3, hat = 4);
translate([3.5, 3.2, 0]) ab_char_bot(seed = 4, pose = 0, hat = 5);
translate([6.5, 3.2, 0]) ab_prop_cage(seed = 2);

// 第二排：被困机器人 / 敌人
translate([-6.5, 0.4, 0]) ab_char_bot_lost(seed = 5, kind = 0);
translate([-3.8, 0.4, 0]) ab_char_bot_lost(seed = 6, kind = 1);
translate([-1.0, 0.4, 0]) ab_char_enemy_walker(seed = 0);
translate([1.8, 0.4, 0]) ab_char_enemy_walker(seed = 3);
translate([4.4, 0.4, 0]) ab_char_enemy_flyer(seed = 0, hover = 0.4);
translate([7.0, 0.4, 0]) ab_char_enemy_spiky(seed = 0);

// 第三排：收集物与小道具
translate([-7.5, -2.4, 0]) ab_item_coin();
translate([-6.3, -2.4, 0]) ab_item_puzzle();
translate([-5.1, -2.4, 0]) ab_item_gem(seed = 1);
translate([-3.9, -2.4, 0]) ab_item_key();
translate([-2.6, -2.4, 0]) ab_item_star();
translate([-1.2, -2.4, 0]) ab_prop_capsule(seed = 1);
translate([0.6, -2.4, 0]) ab_prop_chest(seed = 1);
translate([2.4, -2.4, 0]) ab_prop_crate();
translate([4.4, -2.4, 0]) ab_prop_button(r = 0.7);
translate([6.2, -2.4, 0]) ab_prop_lever();
translate([7.6, -2.4, 0]) ab_prop_cone();

gk_camera_lookat(eye = [0, -13.5, 7.5], target = [0, 0.5, 0.8], name = "closeup", fov = 42);
gk_camera_lookat(eye = [-2, -4, 2.2], target = [-1, 3.2, 0.9], name = "bots", fov = 40);
