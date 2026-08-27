// NextRA variant: fortified industrial base under a multi-directional siege.
use <../others/next_ra_battlefield.scad>
use <../../lib/gk_camera.scad>

$fn = 10;
ROLECOLOR = [0.76, 0.055, 0.045]; ROLEACCENT = [0.96, 0.65, 0.10];
UNIFORM = [0.18, 0.21, 0.20]; VEST = [0.095, 0.11, 0.11]; METAL = [0.15, 0.17, 0.18];
METALL = [0.46, 0.50, 0.51]; SKIN = [0.72, 0.52, 0.38]; BOOT = [0.065, 0.06, 0.055];
RED = ROLECOLOR; REDD = [0.46, 0.035, 0.032]; REDL = [0.92, 0.11, 0.065];
ORANGE = [0.92, 0.34, 0.055]; YELLOW = ROLEACCENT; STEEL = [0.35, 0.38, 0.39];
STEELL = [0.57, 0.60, 0.60]; STEELD = [0.16, 0.18, 0.19]; CONCRETE = [0.46, 0.46, 0.44];
CONCD = [0.31, 0.32, 0.31]; CONCL = [0.59, 0.59, 0.56]; GLASS = [0.18, 0.48, 0.60, 0.72];
GLASSL = [0.42, 0.76, 0.86, 0.72]; BLACK = [0.045, 0.05, 0.055]; SNOW = [0.82, 0.87, 0.88];
SNOWL = [0.94, 0.97, 0.97]; SNOWD = [0.66, 0.73, 0.74]; EARTH = [0.37, 0.31, 0.26];
ROCK = [0.29, 0.31, 0.32]; ROCKL = [0.43, 0.45, 0.45]; WATER = [0.16, 0.43, 0.56];
WATERL = [0.28, 0.63, 0.72]; ORE = [0.96, 0.72, 0.14]; ORED = [0.66, 0.42, 0.075]; ENERGY = [0.20, 0.78, 0.92];

module base_01() building_command_center();
module factory_01() building_war_factory();
module power_01() building_power_plant();
module radar_01() building_radar();
module coil_01() building_arc_coil();
module turret_01() building_turret();
module infantry_01() unit_rifleman();

color(ROCK) translate([0, 0, -0.34]) slab(142, 104, 0.34);
color(SNOWD) translate([0, 0, 0]) slab(140, 102, 0.16);
color(CONCD) translate([0, 0, 0.16]) slab(82, 64, 0.12);

// Dense base core with complementary silhouettes.
translate([0, 15, 0.28]) base_01();
translate([-24, -12, 0.28]) rotate([0, 0, 180]) factory_01();
translate([25, -14, 0.28]) power_01();
translate([-26, 24, 0.28]) radar_01();
translate([28, 25, 0.28]) coil_01();
for (p = [[-36, -27], [36, -27], [-36, 31], [36, 31]]) translate([p[0], p[1], 0.28]) rotate([0, 0, atan2(p[1], p[0]) - 90]) turret_01();

// Complete wall perimeter leaves one breached southern segment.
for (x = [-36 : 6 : 36])
{
    translate([x, 39, 0.28]) building_wall_segment(6);
    if (abs(x) > 10) translate([x, -39, 0.28]) building_wall_segment(6);
}
for (y = [-33 : 6 : 33])
{
    translate([-42, y, 0.28]) rotate([0, 0, 90]) building_wall_segment(6);
    translate([42, y, 0.28]) rotate([0, 0, 90]) building_wall_segment(6);
}
translate([0, -38, 0.28]) prop_crates();
translate([-7, -39, 0.28]) rotate([0, 0, 15]) prop_barrel();
translate([8, -39, 0.28]) rotate([0, 0, -12]) prop_barrel(ORED);

// Siege forces converge from south and west.
translate([0, -47, 0.16]) unit_heavy_tank();
translate([-18, -48, 0.16]) rotate([0, 0, 10]) unit_medium_tank();
translate([20, -48, 0.16]) rotate([0, 0, -12]) unit_medium_tank();
translate([-55, -18, 0.16]) rotate([0, 0, -90]) unit_scout();
for (p = [[-13, -45, 0], [12, -45, 0], [-50, -7, -90], [-48, 6, -90], [51, -3, 90]])
    translate([p[0], p[1], 0.16]) rotate([0, 0, p[2]]) infantry_01();
for (p = [[-62, 38], [62, 38], [-62, -39], [62, -40]]) translate([p[0], p[1], 0.16]) nature_rock(1.4);

gk_camera_lookat([76, -73, 50], [0, 0, 0], "siege-perimeter-overview", 47);
gk_camera_lookat([0, -63, 4], [0, -15, 2], "southern-breach", 53);
gk_camera_lookat([50, 42, 12], [5, 10, 3], "base-core", 50);
