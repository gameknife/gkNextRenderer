---
title: "SCAD 场景创作指南"
category: guide
status: 现行
owner: ScadLoader/editor
created: 2026-06-28
last_updated: 2026-07-17
---

# SCAD 场景创作指南

SCAD 资产按结构放入 `evaluated / source / proc`。可以直接维护 `source/**/*.scad`，或把
`assets/scad/specs/*.json` 作为源数据并生成到 `source/generated/` 或 `proc/generated/`。
不要手改 `generated/` 后期待改动保留。

## 选哪条路径

- 单一模型、特殊 CSG 或需要直接调试表达式：手写 `.scad`。
- 复用 kit、批量布局、城市矩阵、AI 生成或希望 JSON 易审查：使用 spec/compose。
- 通用零件：放到 `assets/scad/lib/kit_*.scad`，补 catalog 元数据并重跑 catalog。

当前可参考的手写场景位于 `assets/scad/source/`，包括 `beer_cup.scad`、`old_city.scad`、
`airport.scad`、`office.scad`、`habor_city*.scad` 和 showcase 场景。生成示例位于
`assets/scad/specs/` 与两个分类的 `generated/` 目录。

## 坐标、材质与兼容子集

- SCAD 创作坐标为 Z-up；loader 转换到引擎 Y-up。单位约定以具体 kit 的 `scaleClass` 和 metadata 为准，跨 scale class 组合会被 compose 拒绝或警告。
- `color([r,g,b,a])` 会形成颜色桶；alpha 小于约 0.99 的桶走透明/介质材质路径。颜色种类过多会扩大 section/material 数量。
- 引擎实现的是 OpenSCAD 风格子集，不是完整 OpenSCAD。支持项、参数和降级行为以 `AGENT_GUIDE/SCADLoader.md` 与当前 evaluator 测试为准。
- CSG 尽量局部、低复杂度；大规模场景优先由 kit module 和布局组合子实例化，不要对整城做一次巨型 boolean。
- 所有伪随机布局必须显式 seed；不要依赖求值顺序或平台随机数。

## Kit 与 spec 工作流

```bash
./gnb.sh build ScadCatalog
./gnb.sh scad catalog
./gnb.sh scad compose --spec assets/scad/specs/port_mini.json
./gnb.sh shot --scene assets/scad/source/generated/port_mini.scad
```

修改 kit 导出 module 后更新 `assets/scad/lib/catalog.json` 并提交。Spec 是严格 JSON，字段以 `tools/gnb/internal/scadcompose/spec.go` 为准；完整数据流见 [SCAD Scene Compose](../designs/scad-scene-compose-design.md)。

## 验证

```bash
./gnb.sh build ScadStudio
./gnb.sh shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60
./out/build/<preset>/bin/gkNextUnitTests "[Scad]"
```

视觉检查至少关注：warning 数、节点/三角形规模、法线与颜色分桶、透明材质、坐标/尺寸和加载时间。只改 `.scad`/JSON 通常不需要 C++ rebuild；改 ScadLoader 后按 Engine 范围构建 `gkNextRenderer gkNextUnitTests`。
