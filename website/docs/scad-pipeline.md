# OpenSCAD 程序化内容管线 (SCAD Pipeline)

gkNextEngine 内置了原生的 OpenSCAD（`.scad`）解析器与求值引擎，将代码化的几何脚本直接转为高效的实时网格，极度契合程序化生成与 AI 驱动建模。

---

## 📐 为什么使用 OpenSCAD？

- **纯文本可编程**：模型就是代码，极便于 Git 版本控制与大语言模型（LLM）直接生成。
- **Manifold CSG 极速求值**：底层集成业界领先的 Manifold 几何库，布尔运算（`union`, `difference`, `intersection`）在毫秒内完成。
- **ScadRig 角色刚体绑定**：通过 SCAD 描述角色的骨骼层级与旋转关节，实现低多边形风格的程序化动画。

---

## 示例代码

```scad
// assets/scad/source/demo_building.scad
module building(floors = 3) {
    difference() {
        cube([20, 20, floors * 8], center = true);
        for (f = [0 : floors - 1]) {
            translate([0, 0, f * 8 - (floors * 4) + 4])
                cube([16, 22, 5], center = true); // 窗户镂空
        }
    }
}

building(floors = 5);
```

启动 `ScadStudio` 即可实时预览并热重载上述模型：
```bash
./gnb.sh run ScadStudio
```
