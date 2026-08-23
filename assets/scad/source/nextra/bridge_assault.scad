// NextRA variant: armored bridge assault across an icy industrial river.
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
module barracks_01() building_barracks();
module turret_01() building_turret();
module tank_01() unit_medium_tank();
module infantry_01() unit_rifleman();
module rocketeer_01() unit_rocketeer();

color(ROCK) translate([0, 0, -0.34]) slab(136, 98, 0.34);
color(SNOWD) translate([0, 0, 0]) slab(134, 96, 0.16);
gk_material(WATER, roughness = 0.18, metalness = 0)
    translate([0, 0, 0.17]) slab(28, 96, 0.07);

// Two-lane industrial bridge with guarded ramps.
color(CONCD) translate([0, 0, 0.24]) slab(42, 11, 0.70);
color(STEELD)
{
    translate([0, -5.2, 1.0]) cube([42, 0.35, 1.5], center = true);
    translate([0, 5.2, 1.0]) cube([42, 0.35, 1.5], center = true);
}
for (x = [-18 : 6 : 18])
{
    color(YELLOW) translate([x, -4.9, 1.1]) cube([0.25, 0.18, 1.7], center = true);
    color(YELLOW) translate([x, 4.9, 1.1]) cube([0.25, 0.18, 1.7], center = true);
}

// Eastern defense base protects the crossing.
translate([49, 25, 0.16]) rotate([0, 0, 180]) base_01();
translate([43, -22, 0.16]) rotate([0, 0, 180]) barracks_01();
translate([29, 14, 0.16]) rotate([0, 0, 90]) turret_01();
translate([29, -14, 0.16]) rotate([0, 0, 90]) building_turret();
for (y = [-39 : 6 : 39]) translate([66, y, 0.16]) rotate([0, 0, 90]) building_wall_segment(6);

// Western armored column presses onto the bridge.
translate([-52, -15, 0.16]) rotate([0, 0, -90]) unit_heavy_tank();
translate([-38, 8, 0.16]) rotate([0, 0, -90]) tank_01();
translate([-18, -2, 0.60]) rotate([0, 0, -90]) unit_medium_tank();
for (p = [[-57, 20, -90], [-50, 25, -85], [-42, 19, -95], [-29, -18, -80]])
    translate([p[0], p[1], 0.16]) rotate([0, 0, p[2]]) infantry_01();
translate([-35, -24, 0.16]) rotate([0, 0, -85]) rocketeer_01();
for (y = [-38 : 12 : 38]) translate([-18, y, 0.16]) prop_barrel(y % 24 == 0 ? RED : ORED);

gk_camera_lookat([72, -70, 48], [0, 0, 0], "bridge-assault-overview", 47);
gk_camera_lookat([-62, -6, 4], [10, 0, 2], "armored-crossing", 52);
gk_camera_lookat([33, -35, 7], [30, 0, 2], "defense-line", 54);
