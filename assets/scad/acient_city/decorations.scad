// ============================================================
// Ancient Chinese City - scene decorations
// ============================================================

$fn = 32;

use <props.scad>

module decorations() {
    for (y=[-66,-42,-18,28,52,76]) {
        translate([-6.8,y,0]) lantern_post(4.4);
        translate([ 6.8,y,0]) lantern_post(4.4);
    }

    translate([16,-6,0]) well();

    for (p=[
        [-112,72],[-82,78],[-32,74],[32,74],[82,78],[112,70],
        [-112,-72],[-76,-78],[-32,-72],[32,-74],[78,-78],[112,-70],
        [-18,50],[22,52],[-20,-54],[22,-52]
    ]) {
        translate([p[0],p[1],0])
        tree();
    }
}

// Standalone preview.
decorations();
