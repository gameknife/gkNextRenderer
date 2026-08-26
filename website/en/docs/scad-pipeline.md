# OpenSCAD Procedural Pipeline

gkNextEngine embeds a native OpenSCAD (`.scad`) DSL parser and evaluator, translating text-based geometry code into optimized GPU meshes via the high-performance Manifold CSG library.

---

## 📐 Why OpenSCAD?

- **Code as Assets**: 3D models are pure text files, easily version-controlled and directly authorable by LLMs.
- **Sub-millisecond Manifold CSG**: Fast Boolean operations (`union`, `difference`, `intersection`).
- **ScadRig Bone Hierarchies**: Rig rigid body bones and joint hierarchies purely in SCAD code.

```scad
// assets/scad/source/demo_building.scad
module building(floors = 3) {
    difference() {
        cube([20, 20, floors * 8], center = true);
        for (f = [0 : floors - 1]) {
            translate([0, 0, f * 8 - (floors * 4) + 4])
                cube([16, 22, 5], center = true);
        }
    }
}

building(floors = 5);
```
