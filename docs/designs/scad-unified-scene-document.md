---
title: "SCAD 统一场景文档"
category: design
status: 现行
owner: ScadLibrary/ScadLoader
created: 2026-08-30
last_updated: 2026-08-30
---

# SCAD 统一场景文档

## 取代了什么

在此之前，一个 `.scad` 场景的可编辑方式由**它所在的目录**决定，三者互斥：

| 目录 | 唯一编辑器 | 代价 |
| --- | --- | --- |
| `evaluated/` | 对象列表 + Gizmo | 只能是扁平实例；写回前先整文件重建 |
| `source/` | 源码文本 | 打开时不求值，没有对象编辑，也没有 Gizmo |
| `proc/` | 地形画布 + 过程规则 | 同一文件里的普通实例无法编辑 |

想给一个 `source/` 程序里的某个结构做对象级编辑，只能整文件"转换为 Evaluated 副本"：
把求值结果写成 `evaluated/<name>_evaluated.scad`，循环、条件和局部 module 全部展开成确定实例，
生成逻辑就此丢失，两份文件从此分叉。这个代价与需求不成比例——用户想改的通常只是其中一个结构。

## 现在的模型

**场景文件没有类型；它的每条顶层语句有。** `FScadSceneDocument`
（`src/Application/Editor/ScadLibrary/ScadSceneDocument.{hpp,cpp}`）把打开的文件解析成一串
节点，逐条分类：

| 节点 | 源码形态 | 编辑器 |
| --- | --- | --- |
| `Instance` | `[color] translate rotate scale kit_module(args);` | 对象列表 + 视口 Gizmo |
| `Terrain` | `TERR = [...]` 与 `gk_terrain(TERR)` | 地形画布 |
| `TerrainRule` | 顶层 `ter_*` | 过程规则列表 |
| `Source` | 其余一切 | 源码页，可逐条开关/展开 |

四类在同一个根文件里共存。`assets/scad/{evaluated,source,proc}` 退化为归档目录：决定条目在
浏览器里的分组和图标，不决定能用哪个编辑器，也不再限制保存目标。

## 不变量

### 1. 未改动的字节不被重写

写回是**按语句拼接**，不是重新生成文件。`FScadSceneDocument::BuildSource` 收集三类编辑：
地形文档的 `CollectEdits`、被开关的语句、被移动/新增/删除的实例；交给
`Assets::Scad::ApplyScadSourceEdits` 从后往前套用。

判断"改动过"用的是解析时的快照：实例比对 `FBenchItem::SamePlacement`，地形与规则比对解析时
的序列化字符串（`FTerrainProcessRule::parsedSerialization`）。**打开再保存一个没动过的场景是
字节级 no-op**——注释、缩进、不支持的语法全部保留。`Test_ScadSceneDocument.cpp` 用
`old_city.scad` 和 `terrain_layout_demo.scad` 守这条。

这条不变量以前只有地形编辑器有；现在是全文档的。

### 2. 区间由 parser 给出，不靠行号推断

`ScadParser::Parse` 接受 `outTopLevelSpans`，在解析时记录每条顶层语句的 `[begin, end)`。
`BuildScadSourceIndex` 把它和 AST 一起放进 `FScadSourceIndex::{statements, topLevel}`。
`use/include` 只做**掩码**不删字节，所以索引里的偏移直接寻址调用方的原始源码。

以前地形文档按 `statement->line` 再在该行里搜 token 名来还原区间，同一行两条语句就会错。
现在没有这种推断，`Test_ScadSceneDocument.cpp` 里 `a = 1; b = 2;` 同行两语句各自得到正确区间。

`ApplyScadSourceEdits` 丢弃与已套用编辑重叠的区间，因此一个过期 span 最坏是不生效，
不会把文件改坏。

### 3. 对象编辑器只认领它能逐字写回的语句

`FScadSceneDocument::ClassifyInstance` 是保守的：变换参数必须是**字面量**，且按
`color → translate → rotate → scale` 顺序**最多各出现一次**，终点必须是 kit 表里解析得到的
module 且无 children。任何一条不满足就留作 `Source`。

这保证"重新序列化 == 原语句"，因此实例编辑永远不会把 `translate([x, 0, 0])` 这类依赖变量的
表达式悄悄常量化。终点调用的实参**从源码字节切片**读取而不是从 AST 打印，所以 AST 打印不
回来的表达式也原样保留。

Kit module 的判定由调用方注入（`isKitModule`），因此 `cube`、`gk_terrain`、`gk_camera`
这些内建与 marker 不会被误认成可拖动的实例。

## 逐节点关闭与展开

这是"整文件转换"的替代品。

**关闭**：在语句前加 OpenSCAD 的 `*` 修饰符。求值器本来就跳过带 `*` 的语句
（`FScadEvaluator.Detail.h:716`、`FScadEvaluator.Geometry.cpp:330`），所以这是原地停用而不是
删代码，随时可启用。赋值与 `module`/`function` 定义不接受修饰符，`IsSwitchable` 会拒绝并说明。

**关闭并展开为实例**：
1. 求值**整个文件**——结构通常依赖它前面的变量和 module，片段无法单独求值；
2. 按顶层语句的行区间 `[line, endLine]` 把 root `SceneNode` 归属回该语句
   （`SceneNode::sourceLine` 是调用点行号，必然落在该语句的区间内）；
3. 转成实例写在原语句之后，并关闭原语句。

若该结构只产生图元或未知模块（没有可归属的 Kit root），操作带原因失败，不静默产出空结果。
保存前可"撤销展开"（`CollapseSegment`）。保存后原语句是一条 `disabled` 的 `Source` 节点，
展开出的实例是普通实例节点——状态可读、可逆，没有隐藏元数据。

## 性能约束

`ReparseDocument` 有 `reevaluate` 开关。重建语句索引只要 lex + parse，很便宜；求值
`use/include` 闭包不便宜（`old_city.scad` 约 260ms），而**只有地形 spec 需要它**。

编辑器自己写出文件后（Gizmo 松手会 `SaveAssembly(false, false)`）用 `reevaluate = false`
复用缓存的顶层绑定：移动一个实例不可能改变顶层变量。打开文件、以及用户改了原始源码后，
才走完整求值。少了这个开关，每次 Gizmo 松手都会卡约 0.25 秒。

## 相关文件

- `src/Modules/ScadLoader/FScadParser.{h,cpp}` — 顶层语句区间记录
- `src/Modules/ScadLoader/FScadSourceIndex.{h,cpp}` — 语句索引与 `ApplyScadSourceEdits`
- `src/Application/Editor/ScadLibrary/ScadSceneDocument.{hpp,cpp}` — 统一文档
- `src/Application/Editor/ScadLibrary/TerrainProcessDocument.{hpp,cpp}` — 地形子文档，现在只
  贡献编辑而不自己重写文件
- `src/Tests/Test_ScadSceneDocument.cpp` — 分类、往返、开关、展开与真实资产回归
- `assets/agentscripts/scadlibrary-mixed-document.agentscript.json` — 端到端交互验证

## 验证

```bash
./gnb.sh build ScadLibrary gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[SceneDocument]"
./gnb.sh validate --script assets/agentscripts/scadlibrary-terrain-process.agentscript.json
./gnb.sh validate --script assets/agentscripts/scadlibrary-mixed-document.agentscript.json
```
