// ============================================================================
// next_ra_battlefield.scad - original low-poly red industrial RTS battlefield
//
// Visual direction: chunky crimson steel, concrete pads, tall silhouettes,
// readable team accents, snow/ore/river terrain, and an isometric RTS layout.
// It is inspired by 1990s base-building strategy games without reproducing any
// specific protected building or unit model.
//
// Scale and gameplay contract:
//   * 1 unit = 1 metre, OpenSCAD Z-up, +Y north, front = -Y.
//   * Grounded modules have their lowest point at z=0.
//   * The imported ScadRig soldier is 1.78 m tall; doors, rails, vehicles and
//     buildings use real-world proportions around that reference.
//   * Current NextRA mappings: base_01, barracks_01, infantry_*, rocketeer_*,
//     tank_*, turret_*, wall_*. Future prototypes can also use power_01,
//     refinery_01, factory_01, radar_01, coil_01, harvester_01 and scout_01.
//   * `use <next_ra_battlefield.scad>` imports the whole modular asset library
//     without executing the showcase layout below.
//
// Full scene footprint: 150 x 112 m. Buildings use realistic dimensions, while
// the present NextRA sim still uses compact placeholder footprints; integration
// should choose a cell size/footprint table rather than shrinking these meshes.
// ============================================================================

use <../../characters/next_ra_soldier.scad>

$fn = 10;

// ================= Palette (also supplies the imported rig) =================
ROLECOLOR  = [0.76, 0.055, 0.045];
ROLEACCENT = [0.96, 0.65, 0.10];
UNIFORM    = [0.18, 0.21, 0.20];
VEST       = [0.095, 0.11, 0.11];
METAL      = [0.15, 0.17, 0.18];
METALL     = [0.46, 0.50, 0.51];
SKIN       = [0.72, 0.52, 0.38];
BOOT       = [0.065, 0.06, 0.055];

RED        = ROLECOLOR;
REDD       = [0.46, 0.035, 0.032];
REDL       = [0.92, 0.11, 0.065];
ORANGE     = [0.92, 0.34, 0.055];
YELLOW     = ROLEACCENT;
STEEL      = [0.35, 0.38, 0.39];
STEELL     = [0.57, 0.60, 0.60];
STEELD     = [0.16, 0.18, 0.19];
CONCRETE   = [0.46, 0.46, 0.44];
CONCD      = [0.31, 0.32, 0.31];
CONCL      = [0.59, 0.59, 0.56];
GLASS      = [0.18, 0.48, 0.60, 0.72];
GLASSL     = [0.42, 0.76, 0.86, 0.72];
BLACK      = [0.045, 0.05, 0.055];
SNOW       = [0.82, 0.87, 0.88];
SNOWL      = [0.94, 0.97, 0.97];
SNOWD      = [0.66, 0.73, 0.74];
EARTH      = [0.37, 0.31, 0.26];
ROCK       = [0.29, 0.31, 0.32];
ROCKL      = [0.43, 0.45, 0.45];
WATER      = [0.16, 0.43, 0.56];
WATERL     = [0.28, 0.63, 0.72];
ORE        = [0.96, 0.72, 0.14];
ORED       = [0.66, 0.42, 0.075];
ENERGY     = [0.20, 0.78, 0.92];

GROUND_Z = 0.30;

// ================= Basic construction helpers =================
module slab(L, D, H)
{
    translate([0, 0, H / 2]) cube([L, D, H], center = true);
}

module part_foundation(L, D, H = 0.36)
{
    color(CONCD) slab(L + 1.6, D + 1.6, H * 0.45);
    color(CONCRETE) translate([0, 0, H * 0.45]) slab(L, D, H * 0.55);
    color(RED) translate([0, -D / 2 - 0.22, H * 0.58]) cube([L * 0.56, 0.26, 0.12], center = true);
}

module part_gable_roof(L, D, H, overhang = 0.45, c = REDD)
{
    hw = L / 2 + overhang;
    hd = D / 2 + overhang;
    color(c) polyhedron(
        points = [[-hw, -hd, 0], [hw, -hd, 0], [hw, hd, 0], [-hw, hd, 0], [-hw, 0, H], [hw, 0, H]],
        faces = [[0, 1, 2, 3], [4, 5, 1, 0], [5, 4, 3, 2], [5, 2, 1], [4, 0, 3]]);
}

module part_window(W = 1.5, H = 1.2, frame = RED)
{
    color(frame) cube([W + 0.18, 0.14, H + 0.18], center = true);
    color(GLASS) translate([0, -0.09, 0]) cube([W, 0.08, H], center = true);
    color(STEELD)
    {
        cube([0.08, 0.18, H], center = true);
        cube([W, 0.18, 0.07], center = true);
    }
}

module part_star_sign(S = 1.0)
{
    color(YELLOW) scale([S, S, S]) linear_extrude(height = 0.09)
        polygon(points = [[0,1], [0.22,0.31], [0.95,0.31], [0.36,-0.12], [0.59,-0.81],
                          [0,-0.39], [-0.59,-0.81], [-0.36,-0.12], [-0.95,0.31], [-0.22,0.31]]);
}

module part_warning_stripe(W = 2.0, H = 0.38)
{
    color(YELLOW) cube([W, 0.06, H], center = true);
    for (x = [-W / 2 + 0.20 : 0.42 : W / 2 - 0.10])
        color(BLACK) translate([x, -0.04, 0]) rotate([0, 25, 0]) cube([0.16, 0.07, H * 1.25], center = true);
}

module part_chimney(H = 8, R = 0.8)
{
    color(REDD) cylinder(h = H, r1 = R * 1.10, r2 = R * 0.82);
    color(REDL) translate([0, 0, H - 0.55]) cylinder(h = 0.55, r = R * 1.02);
    color(BLACK) translate([0, 0, H]) cylinder(h = 0.12, r = R * 0.72);
    color(STEELL) for (z = [1.2 : 1.5 : H - 1]) translate([0, 0, z]) cylinder(h = 0.10, r = R * 1.02);
}

module part_tank(R = 2.0, H = 3.4)
{
    color(STEELD) cylinder(h = 0.20, r = R + 0.18);
    color(STEEL) translate([0, 0, 0.20]) cylinder(h = H, r = R);
    color(STEELL) translate([0, 0, H]) cylinder(h = 0.18, r = R + 0.08);
    color(RED) translate([0, -R - 0.05, H * 0.58]) cube([R * 1.35, 0.14, 0.42], center = true);
}

module part_pipe_horizontal(L = 4, R = 0.22)
{
    color(STEELL) rotate([0, 90, 0]) cylinder(h = L, r = R, center = true);
    color(RED) for (x = [-L / 2 + 0.4, L / 2 - 0.4]) translate([x, 0, 0]) rotate([0, 90, 0]) cylinder(h = 0.12, r = R * 1.35, center = true);
}

module part_rail(L = 5, H = 1.05)
{
    color(STEELD)
    {
        translate([0, 0, H]) cube([L, 0.09, 0.09], center = true);
        for (x = [-L / 2 : 1.25 : L / 2]) translate([x, 0, H / 2]) cube([0.08, 0.08, H], center = true);
    }
}

module prop_flag(H = 7.0)
{
    color(STEELD) translate([0, 0, H / 2]) cylinder(h = H, r = 0.07, center = true);
    color(RED) translate([1.0, 0, H - 0.78]) cube([2.0, 0.08, 1.35], center = true);
    translate([1.0, -0.055, H - 0.78]) rotate([90, 0, 0]) part_star_sign(0.35);
}

module prop_barrel(c = RED)
{
    color(c) cylinder(h = 0.82, r = 0.31);
    color(STEELD)
    {
        translate([0, 0, 0.10]) cylinder(h = 0.07, r = 0.325);
        translate([0, 0, 0.65]) cylinder(h = 0.07, r = 0.325);
    }
}

module prop_crates()
{
    color([0.35, 0.25, 0.16])
    {
        translate([-0.48, 0, 0.40]) cube([0.82, 0.72, 0.80], center = true);
        translate([0.39, 0.08, 0.32]) cube([0.72, 0.66, 0.64], center = true);
        translate([0.15, 0.02, 0.94]) cube([0.68, 0.60, 0.55], center = true);
    }
    color(STEELD) for (z = [0.13, 0.67]) translate([-0.48, -0.37, z]) cube([0.86, 0.04, 0.055], center = true);
}

module prop_light(H = 5.5)
{
    color(STEELD) translate([0, 0, H / 2]) cylinder(h = H, r = 0.095, center = true);
    color(RED) translate([0, 0, H]) cylinder(h = 0.18, r1 = 0.38, r2 = 0.26, center = true);
    color([0.88, 0.93, 0.82]) translate([0, -0.10, H - 0.05]) cube([0.40, 0.24, 0.22], center = true);
}

module nature_rock(S = 1.0)
{
    color(ROCK) scale([1.5 * S, 1.1 * S, 0.8 * S]) sphere(r = 1, $fn = 7);
    color(SNOW) translate([-0.18 * S, 0.05 * S, 0.62 * S]) scale([1.05 * S, 0.72 * S, 0.18 * S]) sphere(r = 0.8, $fn = 7);
}

module resource_crystal(S = 1.0)
{
    color(ORED) cylinder(h = 0.20 * S, r = 0.62 * S, $fn = 7);
    color(ORE)
    {
        translate([-0.24 * S, 0.05 * S, 0.12 * S]) cylinder(h = 1.18 * S, r1 = 0.28 * S, r2 = 0.05 * S, $fn = 6);
        translate([0.25 * S, 0.12 * S, 0.12 * S]) rotate([0, 14, 18]) cylinder(h = 0.92 * S, r1 = 0.24 * S, r2 = 0.04 * S, $fn = 6);
        translate([0.02 * S, -0.24 * S, 0.10 * S]) rotate([0, -18, -8]) cylinder(h = 0.72 * S, r1 = 0.20 * S, r2 = 0.035 * S, $fn = 6);
    }
}

// ================= Terrain and battlefield dressing =================
module ground_battlefield()
{
    color(ROCK) translate([0, 0, -0.45]) slab(150, 112, 0.45);
    color(SNOWD) slab(150, 112, 0.18);
    color(SNOW) translate([0, 0, 0.18]) slab(148.5, 110.5, 0.12);

    // Winding river: one low-poly extruded ribbon plus shallow highlight facets.
    color(WATER) translate([0, 0, 0.305]) linear_extrude(height = 0.055)
        polygon(points = [[-24,-56],[-10,-56],[-7,-42],[-13,-28],[-12,-14],[-5,-2],[-8,12],[-2,27],[-5,42],[2,56],[-16,56],[-22,42],[-19,27],[-25,12],[-21,-3],[-29,-17],[-25,-31],[-30,-44]]);
    color(WATERL) translate([0, 0, 0.363]) linear_extrude(height = 0.012)
        polygon(points = [[-21,-56],[-17,-56],[-14,-42],[-18,-28],[-15,-15],[-10,-3],[-13,12],[-7,27],[-10,42],[-4,56],[-8,56],[-14,42],[-12,27],[-18,12],[-15,-3],[-22,-17],[-19,-31],[-24,-44]]);

    // Concrete base roads and pads.
    color(CONCD)
    {
        translate([35, 4, 0.31]) slab(76, 5.5, 0.08);
        translate([35, -18, 0.31]) slab(6.5, 48, 0.08);
        translate([17, -32, 0.31]) slab(38, 5.0, 0.08);
        translate([31, 25, 0.31]) slab(46, 4.2, 0.08);
    }
    color(YELLOW)
    {
        for (x = [2 : 7 : 69]) translate([x, 4, 0.405]) cube([3.5, 0.16, 0.025], center = true);
        for (y = [-38 : 7 : 2]) translate([35, y, 0.405]) cube([0.16, 3.5, 0.025], center = true);
    }

    // Dirty clearings under the base and ore field.
    color(EARTH) translate([0, 0, 0.372]) linear_extrude(height = 0.025)
        polygon(points = [[5,-44],[63,-46],[73,-33],[69,43],[58,51],[8,48],[-1,34],[2,-8]]);
    color([0.48, 0.40, 0.25]) translate([0, 0, 0.373]) linear_extrude(height = 0.027)
        polygon(points = [[-66,12],[-32,8],[-24,23],[-37,42],[-65,39],[-72,28]]);

    // Snow islands soften the large clearings.
    color(SNOWL)
    {
        translate([51, 43, 0.404]) scale([12, 5, 0.03]) sphere(r = 1, $fn = 10);
        translate([2, 40, 0.404]) scale([8, 3.5, 0.03]) sphere(r = 1, $fn = 10);
        translate([62, -38, 0.404]) scale([7, 3.2, 0.03]) sphere(r = 1, $fn = 10);
    }
}

module ground_bridge()
{
    color(ROCK) translate([-12, 0, 0.36]) slab(28, 10, 0.45);
    color(CONCRETE) translate([-12, 0, 0.80]) slab(27, 8.4, 0.35);
    color(CONCL)
    {
        translate([-12, -4.35, 1.18]) cube([28, 0.44, 0.72], center = true);
        translate([-12,  4.35, 1.18]) cube([28, 0.44, 0.72], center = true);
    }
    color(RED) for (x = [-24, -18, -12, -6, 0])
    {
        translate([x, -4.62, 1.60]) cube([0.24, 0.24, 1.0], center = true);
        translate([x,  4.62, 1.60]) cube([0.24, 0.24, 1.0], center = true);
    }
    color(YELLOW) for (x = [-22 : 5 : -2]) translate([x, 0, 1.00]) cube([2.2, 0.15, 0.025], center = true);
}

module ground_ore_field()
{
    for (i = [0 : 17])
        translate([-62 + (i * 19) % 31, 13 + (i * 13) % 25, 0.40]) rotate([0, 0, (i * 37) % 360])
            resource_crystal(0.68 + (i % 4) * 0.16);
}

module ground_rocks()
{
    translate([-68, -36, 0.38]) nature_rock(2.3);
    translate([-57, -45, 0.38]) rotate([0, 0, 35]) nature_rock(1.7);
    translate([-45, 48, 0.38]) nature_rock(2.0);
    translate([-28, 36, 0.38]) nature_rock(1.2);
    translate([-34, -18, 0.38]) nature_rock(1.4);
    translate([70, 49, 0.38]) nature_rock(1.8);
    translate([68, -50, 0.38]) nature_rock(1.5);
}

// ================= Buildings =================
module building_command_center()
{
    part_foundation(26, 22, 0.55);
    color(STEELD) translate([0, 1, 3.8]) cube([21, 16, 7.1], center = true);
    color(CONCRETE) translate([0, 1, 1.35]) cube([23, 18, 2.1], center = true);
    color(RED) for (sx = [-1, 1])
    {
        translate([sx * 10.3, -5.3, 5.0]) cube([2.8, 5.2, 9.2], center = true);
        translate([sx * 10.3, -8.1, 9.1]) cylinder(h = 2.0, r1 = 1.6, r2 = 0.85);
    }
    color(REDD) translate([0, 1, 7.7]) cube([16.0, 13.0, 1.1], center = true);
    color(REDL) translate([0, -7.6, 5.9]) cube([9.0, 0.55, 5.8], center = true);
    color(BLACK) translate([0, -7.92, 4.1]) cube([5.8, 0.30, 4.0], center = true);
    translate([0, -8.13, 2.5]) part_warning_stripe(5.3, 0.42);

    for (sx = [-1, 1]) translate([sx * 6.2, -7.95, 5.0]) part_window(2.8, 2.2, RED);
    translate([0, -8.24, 7.25]) rotate([90, 0, 0]) part_star_sign(1.15);

    // Command mast and a deliberately asymmetrical crane silhouette.
    color(STEEL) translate([0, 2, 9.2]) cylinder(h = 4.5, r1 = 2.1, r2 = 1.35);
    color(GLASSL) translate([0, 2, 11.6]) cylinder(h = 1.15, r = 1.48);
    color(STEELD) translate([0, 2, 13.0]) cylinder(h = 5.2, r = 0.18);
    color(RED) translate([2.5, 2, 15.2]) cube([5.2, 0.30, 0.30], center = true);
    color(YELLOW) translate([5.0, 2, 13.6]) cube([0.20, 0.20, 3.2], center = true);
    translate([-8.7, 6.2, 8.2]) prop_flag(7.0);
}

module building_barracks()
{
    part_foundation(18, 13, 0.42);
    color(CONCRETE) translate([0, 0.4, 3.1]) cube([15.2, 10.2, 5.8], center = true);
    color(STEELD) translate([0, 0.4, 6.0]) cube([15.8, 10.8, 0.45], center = true);
    translate([0, 0.4, 6.15]) part_gable_roof(15.5, 10.5, 2.2, 0.2, REDD);
    color(RED) for (sx = [-1, 1])
    {
        translate([sx * 7.8, -3.8, 4.5]) cube([2.4, 3.7, 8.2], center = true);
        translate([sx * 7.8, -3.8, 8.5]) cylinder(h = 1.4, r1 = 1.35, r2 = 0.55);
    }
    color(BLACK) translate([0, -5.0, 2.65]) cube([4.0, 0.28, 4.7], center = true);
    color(REDL)
    {
        translate([-2.35, -5.28, 2.75]) cube([0.38, 0.36, 5.1], center = true);
        translate([ 2.35, -5.28, 2.75]) cube([0.38, 0.36, 5.1], center = true);
        translate([0, -5.28, 5.10]) cube([5.1, 0.36, 0.38], center = true);
    }
    for (sx = [-1, 1]) translate([sx * 5.0, -5.2, 3.9]) part_window(2.0, 1.65, RED);
    translate([0, -5.32, 6.65]) rotate([90, 0, 0]) part_star_sign(0.85);
    color(STEELD) translate([0, 4.7, 6.6]) cylinder(h = 4.2, r = 0.12);
    color(RED) translate([0, 4.7, 10.6]) sphere(r = 0.35, $fn = 8);
}

module building_power_plant()
{
    part_foundation(20, 15, 0.46);
    color(STEELD) translate([0, 0, 3.3]) cube([18, 12.5, 6.3], center = true);
    color(REDD) translate([0, 0, 6.65]) cube([18.6, 13.0, 0.75], center = true);
    for (sx = [-1, 1]) translate([sx * 5.3, 1.8, 6.8]) part_chimney(8.4, 1.05);
    for (sx = [-1, 1])
    {
        color(STEEL) translate([sx * 5.1, -5.5, 3.2]) rotate([90, 0, 0]) cylinder(h = 1.0, r = 2.4, center = true);
        color(RED) translate([sx * 5.1, -6.0, 3.2]) rotate([90, 0, 0]) cylinder(h = 0.22, r = 2.0, center = true);
        color(YELLOW) for (a = [0 : 60 : 300]) translate([sx * 5.1 + 1.3 * cos(a), -6.14, 3.2 + 1.3 * sin(a)])
            rotate([90, 0, 0]) cylinder(h = 0.12, r = 0.12, center = true);
    }
    translate([0, -6.45, 3.3]) part_warning_stripe(4.6, 0.62);
    color(RED) translate([0, -6.2, 5.8]) cube([4.8, 0.25, 1.1], center = true);
}

module building_war_factory()
{
    part_foundation(30, 23, 0.52);
    color(STEEL) translate([0, 1, 4.8]) cube([27.5, 18.5, 8.9], center = true);
    color(STEELD) translate([0, 1, 9.2]) cube([28.2, 19.2, 0.65], center = true);
    translate([0, 1, 9.5]) part_gable_roof(28, 19, 3.5, 0.4, REDD);

    // Wide vehicle bay with a thick red frame.
    color(BLACK) translate([0, -8.45, 4.15]) cube([15.5, 0.35, 7.4], center = true);
    color(STEELD) for (x = [-6.0 : 2.0 : 6.0]) translate([x, -8.68, 4.15]) cube([0.09, 0.12, 6.8], center = true);
    color(REDL)
    {
        translate([-8.25, -8.75, 4.35]) cube([0.55, 0.55, 8.5], center = true);
        translate([ 8.25, -8.75, 4.35]) cube([0.55, 0.55, 8.5], center = true);
        translate([0, -8.75, 8.35]) cube([17.0, 0.55, 0.55], center = true);
    }
    translate([0, -9.08, 1.15]) part_warning_stripe(14.8, 0.52);
    for (sx = [-1, 1]) translate([sx * 11.0, -8.72, 5.2]) part_window(2.7, 2.1, RED);

    // Roof gantry and exhaust boxes.
    color(STEELD)
    {
        translate([-8.5, 1.5, 13.0]) cube([0.30, 12.0, 0.30], center = true);
        translate([ 8.5, 1.5, 13.0]) cube([0.30, 12.0, 0.30], center = true);
        translate([0, -4.3, 13.0]) cube([17.3, 0.30, 0.30], center = true);
    }
    color(RED) translate([0, -4.3, 12.3]) cube([4.3, 0.65, 1.0], center = true);
    color(STEELD) for (sx = [-1, 1]) translate([sx * 10.2, 6.5, 10.5]) cylinder(h = 3.0, r = 0.58);
}

module building_refinery()
{
    part_foundation(28, 20, 0.50);
    color(STEELD) translate([3.0, 0, 3.7]) cube([18.5, 16.0, 6.9], center = true);
    color(REDD) translate([3.0, 0, 7.25]) cube([19.2, 16.6, 0.70], center = true);

    // Ore receiving hopper and conveyor on the left.
    color(ORED) translate([-9.0, -1.0, 1.0]) cylinder(h = 4.8, r1 = 2.0, r2 = 4.2, $fn = 8);
    color(STEELD) translate([-9.0, -1.0, 5.9]) cylinder(h = 0.35, r = 4.3, $fn = 8);
    color(STEEL) translate([-5.1, 0.2, 5.0]) rotate([0, 17, 0]) cube([8.0, 2.0, 1.0], center = true);
    color(YELLOW) translate([-5.1, -0.86, 5.0]) rotate([0, 17, 0]) cube([7.7, 0.10, 0.20], center = true);

    translate([6.2, 2.5, 7.6]) part_tank(2.25, 6.8);
    translate([1.0, 4.2, 7.6]) part_tank(1.75, 5.2);
    translate([4.0, -5.0, 9.0]) rotate([0, 0, 90]) part_pipe_horizontal(9.5, 0.30);

    color(BLACK) translate([5.0, -8.1, 3.0]) cube([5.2, 0.25, 5.0], center = true);
    color(RED) translate([5.0, -8.32, 6.0]) cube([7.2, 0.34, 0.45], center = true);
    translate([5.0, -8.52, 1.1]) part_warning_stripe(4.7, 0.50);
    translate([-2.5, -8.35, 4.4]) part_window(2.8, 2.1, RED);
}

module building_radar()
{
    part_foundation(18, 18, 0.44);
    color(CONCRETE) cylinder(h = 1.1, r = 8.2, $fn = 12);
    color(RED) translate([0, 0, 1.1]) cylinder(h = 0.45, r1 = 7.6, r2 = 6.8, $fn = 12);
    color(STEELD) translate([0, 0, 1.55]) cylinder(h = 5.5, r = 5.8, $fn = 12);
    color(REDL) translate([0, 0, 6.8]) cylinder(h = 0.55, r = 6.25, $fn = 12);
    color(STEELL) translate([0, 0, 7.2]) scale([1, 1, 0.76]) sphere(r = 5.25, $fn = 12);
    color(CONCL) translate([-1.7, -4.8, 7.8]) scale([0.62, 0.62, 0.48]) sphere(r = 4.6, $fn = 10);
    color(ENERGY) translate([-1.7, -7.25, 7.6]) rotate([90, 0, 0]) cylinder(h = 0.22, r = 1.35, center = true, $fn = 12);
    color(STEELD) translate([0, 0, 12.0]) cylinder(h = 3.2, r = 0.13);
    color(RED) translate([0, 0, 15.0]) sphere(r = 0.38, $fn = 8);
}

module building_arc_coil()
{
    part_foundation(7, 7, 0.42);
    color(CONCD) cylinder(h = 1.0, r = 3.15, $fn = 10);
    color(RED) translate([0, 0, 1.0]) cylinder(h = 0.55, r1 = 2.8, r2 = 2.3, $fn = 10);
    color(STEELD) translate([0, 0, 1.55]) cylinder(h = 10.8, r1 = 0.70, r2 = 0.32, $fn = 8);
    for (z = [2.4 : 1.35 : 10.5])
    {
        color(STEELL) translate([0, 0, z]) cylinder(h = 0.18, r = 1.35 - z * 0.045, $fn = 12);
        color(ENERGY) translate([0, 0, z + 0.18]) cylinder(h = 0.08, r = 1.18 - z * 0.038, $fn = 12);
    }
    color(STEELL) translate([0, 0, 12.4]) sphere(r = 1.05, $fn = 10);
    color(ENERGY) translate([0, 0, 12.4]) scale([1.08, 1.08, 1.08]) sphere(r = 0.72, $fn = 10);
    color(STEELD) for (a = [0 : 120 : 240]) rotate([0, 0, a]) translate([2.3, 0, 4.2]) rotate([0, -17, 0]) cube([0.20, 0.20, 6.0], center = true);
}

module building_turret()
{
    color(CONCD) cylinder(h = 0.36, r = 3.2, $fn = 10);
    color(RED) translate([0, 0, 0.36]) cylinder(h = 0.48, r1 = 2.8, r2 = 2.25, $fn = 10);
    color(STEELD) translate([0, 0, 0.84]) cylinder(h = 2.5, r = 1.72, $fn = 10);
    color(REDL) translate([0, 0, 3.1]) cylinder(h = 0.65, r1 = 1.85, r2 = 1.45, $fn = 10);
    color(STEEL) translate([0, -0.30, 3.85]) cube([2.7, 2.5, 1.3], center = true);
    color(BLACK) translate([0, -1.62, 3.95]) cube([1.4, 0.18, 0.50], center = true);
    color(STEELD) translate([0, -1.55, 4.1]) rotate([90, 0, 0]) cylinder(h = 4.2, r = 0.22, $fn = 8);
    color(YELLOW) translate([0, -5.68, 4.1]) rotate([90, 0, 0]) cylinder(h = 0.10, r = 0.29, center = true, $fn = 8);
    color(ENERGY) translate([0, -1.74, 4.12]) cube([0.62, 0.08, 0.20], center = true);
}

module building_wall_segment(L = 4.0)
{
    color(CONCD) slab(L, 1.45, 0.30);
    color(CONCRETE) translate([0, 0, 0.30]) slab(L - 0.18, 1.20, 2.15);
    color(STEELD) translate([0, 0, 2.45]) cube([L, 1.35, 0.24], center = true);
    color(RED) for (sx = [-1, 1]) translate([sx * (L / 2 - 0.20), 0, 1.40]) cube([0.35, 1.55, 2.65], center = true);
    color(YELLOW) translate([0, -0.72, 1.15]) cube([L * 0.70, 0.06, 0.18], center = true);
}

// ================= Units =================
module unit_rifleman() { bone_root(0); }
module unit_rocketeer() { bone_root(1); }

module part_track(L = 4.8, H = 1.0)
{
    color(BLACK) translate([0, 0, H / 2]) cube([0.62, L, H], center = true);
    color(STEELD) for (y = [-L / 2 + 0.55 : 1.18 : L / 2 - 0.45])
        translate([0, y, H * 0.50]) rotate([0, 90, 0]) cylinder(h = 0.72, r = H * 0.34, center = true, $fn = 8);
}

module unit_medium_tank(barrel_count = 1)
{
    translate([-1.48, 0, 0]) part_track(5.2, 1.05);
    translate([ 1.48, 0, 0]) part_track(5.2, 1.05);
    color(REDD) translate([0, 0.18, 0.95]) cube([2.65, 4.75, 0.88], center = true);
    color(RED) translate([0, -0.20, 1.55]) polyhedron(
        points = [[-1.20,-2.0,0],[1.20,-2.0,0],[1.32,1.7,0],[-1.32,1.7,0],[-0.88,-1.55,0.82],[0.88,-1.55,0.82],[0.98,1.35,0.82],[-0.98,1.35,0.82]],
        faces = [[0,1,2,3],[4,7,6,5],[0,4,5,1],[1,5,6,2],[2,6,7,3],[3,7,4,0]]);
    color(STEELD) translate([0, -0.20, 2.35]) cylinder(h = 0.48, r = 1.05, $fn = 10);
    color(REDL) translate([0, -0.42, 2.62]) cube([1.65, 1.85, 0.55], center = true);
    if (barrel_count == 1)
    {
        color(STEELD) translate([0, -1.25, 2.73]) rotate([90, 0, 0]) cylinder(h = 3.3, r = 0.16, $fn = 8);
        color(YELLOW) translate([0, -4.55, 2.73]) rotate([90, 0, 0]) cylinder(h = 0.10, r = 0.21, center = true, $fn = 8);
    }
    color(GLASSL) translate([0, -1.38, 2.86]) cube([0.62, 0.08, 0.18], center = true);
    color(YELLOW) for (sx = [-1, 1]) translate([sx * 0.76, -2.14, 1.56]) rotate([90, 0, 0]) cylinder(h = 0.10, r = 0.15, center = true);
}

module unit_heavy_tank()
{
    scale([1.20, 1.25, 1.12]) unit_medium_tank(0);
    color(STEELD)
    {
        translate([-0.42, -1.55, 3.0]) rotate([90, 0, 0]) cylinder(h = 3.6, r = 0.15, $fn = 8);
        translate([ 0.42, -1.55, 3.0]) rotate([90, 0, 0]) cylinder(h = 3.6, r = 0.15, $fn = 8);
    }
    color(RED) translate([0, 1.8, 1.55]) cube([2.4, 0.48, 0.8], center = true);
}

module unit_harvester()
{
    translate([-1.72, 0, 0]) part_track(6.1, 1.18);
    translate([ 1.72, 0, 0]) part_track(6.1, 1.18);
    color(ORED) translate([0, 0.55, 1.25]) cube([3.2, 4.2, 1.55], center = true);
    color(RED) translate([0, -1.45, 1.55]) polyhedron(
        points = [[-1.55,-1.0,0],[1.55,-1.0,0],[1.45,1.0,0],[-1.45,1.0,0],[-1.18,-0.65,1.55],[1.18,-0.65,1.55],[1.05,0.75,1.55],[-1.05,0.75,1.55]],
        faces = [[0,1,2,3],[4,7,6,5],[0,4,5,1],[1,5,6,2],[2,6,7,3],[3,7,4,0]]);
    color(GLASSL) translate([0, -2.12, 2.25]) rotate([74, 0, 0]) cube([1.75, 0.10, 0.82], center = true);
    color(STEELD) translate([0, -3.15, 0.82]) rotate([0, 90, 0]) cylinder(h = 4.15, r = 0.48, center = true, $fn = 8);
    color(YELLOW) for (x = [-1.75 : 0.70 : 1.75]) translate([x, -3.55, 0.42]) rotate([22, 0, 0]) cube([0.11, 1.1, 0.16], center = true);
    color(ORE) translate([0, 1.65, 2.35]) cube([2.65, 1.05, 1.1], center = true);
    color(RED) translate([0, 1.65, 3.0]) cube([3.1, 1.3, 0.25], center = true);
}

module unit_scout()
{
    color(BLACK) for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 0.88, sy * 1.38, 0.48]) rotate([0, 90, 0]) cylinder(h = 0.42, r = 0.48, center = true, $fn = 8);
    color(REDD) translate([0, 0, 0.78]) cube([1.75, 3.15, 0.65], center = true);
    color(RED) translate([0, -0.30, 1.32]) polyhedron(
        points = [[-0.82,-1.20,0],[0.82,-1.20,0],[0.78,1.18,0],[-0.78,1.18,0],[-0.58,-0.75,0.80],[0.58,-0.75,0.80],[0.50,0.65,0.80],[-0.50,0.65,0.80]],
        faces = [[0,1,2,3],[4,7,6,5],[0,4,5,1],[1,5,6,2],[2,6,7,3],[3,7,4,0]]);
    color(GLASSL) translate([0, -1.12, 1.73]) rotate([72, 0, 0]) cube([0.95, 0.08, 0.52], center = true);
    color(STEELD) translate([0, 0.15, 2.18]) rotate([90, 0, 0]) cylinder(h = 2.1, r = 0.105, center = true, $fn = 8);
    color(YELLOW) for (sx = [-1, 1]) translate([sx * 0.58, -1.93, 0.98]) rotate([90, 0, 0]) cylinder(h = 0.08, r = 0.13, center = true);
}

module spawn_marker()
{
    color(CONCD) cylinder(h = 0.12, r = 1.8, $fn = 8);
    color(RED) translate([0, 0, 0.12]) cylinder(h = 0.055, r = 1.45, $fn = 8);
    color(BLACK) translate([0, 0, 0.177]) linear_extrude(height = 0.02)
        polygon(points = [[0,-1.0],[0.82,0.5],[0.28,0.38],[0,0.75],[-0.28,0.38],[-0.82,0.5]]);
}

// ================= Named gameplay anchors =================
module base_01() building_command_center();
module barracks_01() building_barracks();
module power_01() building_power_plant();
module factory_01() building_war_factory();
module refinery_01() building_refinery();
module radar_01() building_radar();
module coil_01() building_arc_coil();
module turret_01() building_turret();
module turret_02() building_turret();

module infantry_01() unit_rifleman();
module infantry_02() unit_rifleman();
module infantry_03() unit_rifleman();
module infantry_04() unit_rifleman();
module rocketeer_01() unit_rocketeer();
module rocketeer_02() unit_rocketeer();
module tank_01() unit_medium_tank();
module tank_02() unit_medium_tank();
module heavy_tank_01() unit_heavy_tank();
module harvester_01() unit_harvester();
module scout_01() unit_scout();
module spawn_01() spawn_marker();

module wall_01() building_wall_segment();
module wall_02() building_wall_segment();
module wall_03() building_wall_segment();
module wall_04() building_wall_segment();
module wall_05() building_wall_segment();
module wall_06() building_wall_segment();
module wall_07() building_wall_segment();
module wall_08() building_wall_segment();

// ================= Showcase scene layout =================
ground_battlefield();
ground_bridge();
ground_ore_field();
ground_rocks();

// Main red industrial base.
translate([45, -18, GROUND_Z + 0.10]) base_01();
translate([53,  20, GROUND_Z + 0.10]) rotate([0, 0, -8]) barracks_01();
translate([26,  28, GROUND_Z + 0.10]) rotate([0, 0, 4]) power_01();
translate([15, -34, GROUND_Z + 0.10]) rotate([0, 0, -2]) factory_01();
translate([18,   7, GROUND_Z + 0.10]) rotate([0, 0, 3]) refinery_01();
translate([61,  40, GROUND_Z + 0.10]) radar_01();
translate([64,   1, GROUND_Z + 0.10]) coil_01();

// Defensive line; each wall/turret remains individually addressable.
translate([69, -10, GROUND_Z + 0.10]) turret_01();
translate([40,  42, GROUND_Z + 0.10]) rotate([0, 0, 180]) turret_02();
translate([68, -18, GROUND_Z + 0.10]) rotate([0, 0, 90]) wall_01();
translate([68, -14, GROUND_Z + 0.10]) rotate([0, 0, 90]) wall_02();
translate([68,  -6, GROUND_Z + 0.10]) rotate([0, 0, 90]) wall_03();
translate([68,  -2, GROUND_Z + 0.10]) rotate([0, 0, 90]) wall_04();
translate([50,  49, GROUND_Z + 0.10]) wall_05();
translate([54,  49, GROUND_Z + 0.10]) wall_06();
translate([58,  49, GROUND_Z + 0.10]) wall_07();
translate([62,  49, GROUND_Z + 0.10]) wall_08();

// Production yard props and rally point.
translate([34, -32, GROUND_Z + 0.12]) spawn_01();
translate([31, -40, GROUND_Z + 0.10]) prop_crates();
translate([34, -40, GROUND_Z + 0.10]) prop_barrel();
translate([35, -39.5, GROUND_Z + 0.10]) prop_barrel(ORED);
translate([39, -35, GROUND_Z + 0.10]) prop_light();
translate([39,  16, GROUND_Z + 0.10]) prop_light();
translate([62, -29, GROUND_Z + 0.10]) prop_light();

// Units use actual metre-scale silhouettes. Front is -Y before rotation.
translate([30, -30, GROUND_Z + 0.10]) rotate([0, 0, 18]) tank_01();
translate([ 1,  -4, 1.15]) rotate([0, 0, -74]) tank_02();
translate([-22,  3, 1.15]) rotate([0, 0, -82]) heavy_tank_01();
translate([-47, 26, GROUND_Z + 0.10]) rotate([0, 0, 32]) harvester_01();
translate([-31, -8, GROUND_Z + 0.10]) rotate([0, 0, -58]) scout_01();

translate([38, 10, GROUND_Z + 0.10]) rotate([0, 0, 8]) infantry_01();
translate([41,  8, GROUND_Z + 0.10]) rotate([0, 0, -15]) infantry_02();
translate([34, 13, GROUND_Z + 0.10]) rotate([0, 0, 25]) rocketeer_01();
translate([-4, -2, 1.15]) rotate([0, 0, -78]) infantry_03();
translate([-8,  2, 1.15]) rotate([0, 0, -92]) infantry_04();
translate([-13, -2, 1.15]) rotate([0, 0, -84]) rocketeer_02();
