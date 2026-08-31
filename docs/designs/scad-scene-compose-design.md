---
title: "SCAD Scene Compose 数据管线"
category: design
status: 现行
owner: tools/editor
created: 2026-06-28
last_updated: 2026-07-17
---

# SCAD Scene Compose 数据管线

SCAD 场景生成采用“kit 库 → catalog → 严格 JSON spec → 确定性 `.scad`”四层结构。几何和可复用模块属于 SCAD kit；Go 侧只做发现、校验和模板展开，不复制几何算法。

## 当前文件与所有权

| 层 | 位置 | 事实来源 |
| --- | --- | --- |
| kit | `assets/scad/lib/kit_*.scad` | 可复用 module、元数据和布局组合子 |
| catalog | `assets/scad/lib/catalog.json` | `ScadCatalog` 从 kit 重新生成，不手改 |
| spec | `assets/scad/specs/*.json` | 人工或 AI 编辑的场景源数据 |
| generated source | `assets/scad/source/generated/*.scad` | 普通 spec 的派生产物，会被覆盖 |
| generated proc | `assets/scad/proc/generated/*.scad` | 含 terrain spec 的派生产物，会被覆盖 |

编辑器的 kit 浏览使用 `src/Application/Editor/ScadLibrary/KitCatalog.*`；catalog 工具位于 `src/Application/Util/ScadCatalog/`；compose 与 AI 生成分别位于 `tools/gnb/internal/scadcompose/`、`tools/gnb/internal/scadgen/` 和命名 workflow `tools/gnb/internal/ai/workflow/scadscene/`。

## 命令

```bash
./gnb.sh build ScadCatalog
./gnb.sh scad catalog
./gnb.sh scad compose --spec assets/scad/specs/deadly_roadtrip_map.json
./gnb.sh scad generate "一个港口旁的小镇"
./gnb.sh shot --scene assets/scad/source/generated/deadly_roadtrip_map.scad
```

kit 新增、删除或修改导出 module 后必须重跑 `scad catalog`。compose 会校验 module 是否存在、是否由已声明 kit 拥有、矩阵形状及 `scaleClass` 混用等问题，并把产物镜像到当前 build assets，通常无需为预览重新构建。

## Spec v1

解析器使用 `DisallowUnknownFields`；JSON 不允许注释。顶层当前字段为：

- 基本信息：`name`、`fn`、`seed`、`kits`、`ground`
- 显式与批量布局：`placements`、`grids`、`rows`、`rings`、`scatters`、`alongs`
- 城市矩阵：`blockTypes`、`blockGrids`

具体结构以 `tools/gnb/internal/scadcompose/spec.go` 为准。Terrain、snap 与过滤字段已经进入现行
SCAD/TERR 工作流；不要根据旧计划猜 schema，使用当前结构定义和校验错误作为事实来源。

## 确定性与维护规则

- 相同 spec 和 catalog 必须生成相同字节；随机性只通过显式 seed 下沉到 SCAD。
- `*/generated/*.scad` 文件头记录源 spec 与内容 hash。修改生成文件不会反向更新 spec，下次 compose 会覆盖修改。
- catalog 是生成物，但必须随 kit 变更一并提交，保证编辑器和 gnb 使用同一菜单。
- AI 只负责产生 spec；产品侧仍要严格解析、校验并限制修复轮数。不得让模型直接绕过 catalog 写任意引擎操作。
- 需要手写单一场景时可直接维护顶层 `.scad`；需要重复布局、AI 生成或可审查数据源时使用 spec/compose。
