// NextRA variant: ore rush around a contested central resource field.
// Reuse the modular library embedded in the original showcase without executing its layout.
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
module refinery_01() building_refinery();
module harvester_01() unit_harvester();
module tank_01() unit_medium_tank();
module infantry_01() unit_rifleman();

// Snowy basin and three visually distinct ore seams.
color(ROCK) translate([0, 0, -0.32]) slab(132, 94, 0.32);
color(SNOWD) translate([0, 0, 0]) slab(130, 92, 0.16);
color(EARTH) translate([0, 0, 0.16]) scale([3.0, 1.35, 1]) cylinder(h = 0.05, r = 8, $fn = 16);
for (a = [0 : 30 : 330]) translate([22 * cos(a), 10 * sin(a), 0.21]) resource_crystal(0.75 + (a % 60) / 120);
for (p = [[-48, 26], [-43, 30], [46, -27], [51, -23]]) translate([p[0], p[1], 0.16]) resource_crystal(0.9);

// Red mining base and protected processing lane.
translate([43, 20, 0.16]) rotate([0, 0, 180]) base_01();
translate([38, -18, 0.16]) rotate([0, 0, 180]) refinery_01();
translate([58, -9, 0.16]) building_power_plant();
translate([62, 16, 0.16]) building_turret();
for (y = [-34 : 5 : 32]) translate([68, y, 0.16]) rotate([0, 0, 90]) building_wall_segment(5);

// Harvesters race to the middle while armor screens the route.
translate([18, -6, 0.16]) rotate([0, 0, -65]) harvester_01();
translate([-24, 9, 0.16]) rotate([0, 0, 75]) unit_harvester();
translate([25, 15, 0.16]) rotate([0, 0, -55]) tank_01();
translate([-34, -12, 0.16]) rotate([0, 0, 70]) unit_heavy_tank();
for (p = [[34, 7, 20], [30, 2, 35], [-28, -3, -70], [-23, -8, -55]])
    translate([p[0], p[1], 0.16]) rotate([0, 0, p[2]]) infantry_01();
for (p = [[-60, -34], [-55, 36], [55, 37], [4, 39]]) translate([p[0], p[1], 0.16]) nature_rock(1.2);

gk_camera_lookat([70, -66, 46], [0, 0, 0], "ore-rush-overview", 48);
gk_camera_lookat([-34, -28, 4], [0, 0, 1], "contested-ore", 54);
gk_camera_lookat([66, -37, 10], [40, 0, 3], "mining-base", 52);
