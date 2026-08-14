# SCAD（OpenSCAD DSL）加载器

本文记录当前 `ScadLoader` 可选模块。它实现 OpenSCAD 风格子集，不是完整 OpenSCAD 兼容层；功能判断以 `src/Modules/ScadLoader/` 和 `[Scad]` 单测为准。

## 入口与文件

应用链接 `ScadLoader` 后，在加载/扫描场景前调用 `Modules::Scad::Register()`，把 `.scad` loader 注册到 `Assets::FLoaderRegistry`。主要文件：

- `ScadModule.*`：模块注册。
- `FScadLexer.*` / `FScadParser.*` / `FScadTypes.h`：词法、AST 与值模型。
- `FScadEvaluator.cpp`、`.Expr.cpp`、`.Geometry.cpp`：作用域、语言求值和几何分发。
- `FScadGeometry.*` / `FScadCsg.*` / `FScadTess.*` / `FScadText.*`：图元、Manifold boolean、earcut 和 FreeType text。
- `FScadShared.*`：scene 与 rig 共用的 `use/include` 闭包、坐标转换和法线工具。
- `FScadLoader.*`：颜色桶、module 调用树、Model/Node/材质组装。
- `FScadRig.*`：ScadRig 资产读取；约定见 [ScadRig](ScadRig.md)。

不要再从旧的 `src/Engine/Assets/Loaders/FScad*` 路径寻找实现；2026-06-10 后它属于 `Modules/ScadLoader`，Engine 核心不依赖该模块。

## 数据流

```text
.scad + use/include closure
  → Lexer → Parser → AST
  → evaluator（动态变量作用域、局部 module/function 定义、transform/color 栈）
  → SCAD-space triangle soup + user-module 调用树 + RGBA bucket
  → loader：Z-up → Y-up、scale、法线、材质、Model/Node 层级
```

坐标转换是右手 Z-up 到右手 Y-up：`world=(x,z,-y)`，等价绕 X -90°，不翻 winding。`sys.scadToWorldScale` 写入 `ScadLoadOptions::scadToWorldScale`，默认 1；默认 `smoothAngleDegrees=35`。

每个 user module 调用实例形成逻辑 Node；直属几何按量化 RGBA 分桶并尽量合并进多-section Model。材质槽超过引擎上限时拆成同名 render 子节点。alpha `< 0.99` 的 bucket 使用透明 Dielectric 路径。

`SceneEvalResult::SceneNode` 同时保留 module 调用的源行、求值后的具名参数、调用点颜色和
局部变换。ScadLibrary 使用这些作者元数据沿 module 调用树提取最终 Kit 实例；它不改变
运行时 Node/Model 的装配语义。

ScadLibrary 场景对象 Gizmo 在引擎 Y-up 与 SCAD Z-up 之间用 `ScadToWorldBasis` 双向转换。
拖动期间直接更新匹配实例的运行时 Node，并用 `Scene::MarkTransformDirty()` 刷新 GPU 变换；
松手只持久化文件，不触发 SCAD 重载。平铺场景会把三轴 `translate` / `rotate` 写回原 SCAD；
复杂 Source 场景经显式转换后写入 `assets/scad/evaluated/*_evaluated.scad` 的确定实例副本，禁止用展开结果
覆盖原程序结构。

视口点选使用引擎 CPU Picking 返回的 render-node instance ID，再沿 `Node::GetParent()` 向上
追溯 Kit 调用节点。求值场景优先匹配 evaluator 保留的稳定 instance ID；平铺场景以模块名
和世界变换消除同名实例歧义。选中状态同步到对象列表和 Gizmo；首次点选的同一次鼠标按压
不会立即抓取刚出现的 Gizmo。

程序化场景扁平化时，ScadLibrary 会从原源码独立提取 TERR 声明与 `gk_terrain(...)` 语句，
并在 Kit 实例列表之前原样写入预览/可编辑副本。Terrain 仍由原生 `gk_terrain` evaluator
重建，不应被转换为普通 Kit 实例或在 Gizmo 保存时丢弃。

## 当前语言面

- **图元 3D**：`cube`、`sphere`、`cylinder`(含 r1/r2 圆锥)、`polyhedron`
- **图元 2D**（在 extrude 内）：`circle`、`square`、`polygon`(含 `paths` 洞)、`text`(FreeType, CJK)
- **变换**：`translate`、`rotate`(欧拉/角轴)、`scale`、`mirror`、`multmatrix`、`color`(rgba/命名色)
- **CSG**：`union`、`difference`、`intersection`、`hull`（Manifold）；`minkowski`(近似为 union)
- **拉伸**：`linear_extrude`(凹/带洞/嵌套变换)、`rotate_extrude`(angle，360 闭合/部分端盖)
- **控制流**：`for`(笛卡尔多绑定)、`if/else`、`let`、`intersection_for`(按 for)
- **语言**：`module`/`function` 定义（含局部 helper 与后向引用）与默认参/关键字参、`children()`/`children(i)`/`$children`、list comprehension(`for`/`if`/`let`/`each`)、`echo`/`assert`/`str`、`$fn`/`$fa`/`$fs`
- **内置函数**：`max min abs floor ceil round sqrt pow exp ln log sign sin cos tan asin acos atan atan2 len norm concat str`（三角函数为角度制）
- **引擎扩展（`gk_` 前缀，非 OpenSCAD 标准）**：`gk_terrain(TERR)` 低模高度场地形 module +
  `gk_terrain_height(TERR,x,y)` / `gk_terrain_info(TERR,x,y)` 纯函数（`FScadTerrain.{h,cpp}`）。
  地形桶带 faceted 标志（loader 跳过法线平滑）、水面桶拆分为 `__water` 子节点（rayCast 不可见、
  无物理体）、地形数据经 `SceneTerrain` payload 挂成引擎 `TerrainComponent`。
  详见 `AGENT_GUIDE/ScadTerrain.md`。

## 相机机位（虚拟点）

场景可内嵌零几何相机标记（`use <../lib/gk_camera.scad>`，marker 是空 module，**不产生三角面、
不进 kit/catalog**）。Loader（`FScadLoader.cpp` `BuildScadCameras`）识别标记节点并转换为与
glTF 相机完全相同的运行时结构：定点机位进 `EnvironmentSetting.cameras`（UI 相机列表可选），
路径机位额外生成根级动画节点 + `AnimationTrack`（选中该机位且 track 播放时由
`Scene::HasCameraAnimation` 驱动画面跟随）。

- `gk_camera(name=..., fov=55, aperture=0, focal=0)` — 定点机位。文件中第一个 `gk_camera`
  即场景默认入场视角（`cameras[0]`）。
- `gk_camera_key(path=..., t=秒, fov=...)` — 路径关键帧。同一 `path` 名 ≥2 个 key 生成一条
  相机动画（按 t 升序，key 世界变换烘焙进 track，引擎侧 ping-pong 回放）。
- 推荐用 lookat 便捷模块免除手写旋转：`gk_camera_lookat(eye, target, name, fov, focal)` /
  `gk_camera_lookat_key(eye, target, path, t, fov)`（focal 缺省取 eye→target 距离）。
- **朝向约定**：手动摆放时相机局部 front = +Y、up = +Z（SCAD 空间），即
  `translate(eye) rotate([pitch,0,yaw])`（pitch 正=抬头；yaw 0=朝 +Y，-90=朝 +X）。
  该约定经 Z-up→Y-up 转换后与引擎相机 front=-Z/up=+Y 一致。
- marker 节点保留在场景层级中（重命名为 `cam_<name>` / `camkey_<path>_<i>` /
  `campath_<path>`），Outliner 可见、便于排查。

变量采用调用链可见的动态作用域，定义使用局部 definition stack；它与 OpenSCAD 的所有边角语义不保证完全一致。遇到不支持语法应补最小 parser/evaluator test，而不是在资产中依赖偶然降级。

## 后端与降级

- `GK_WITH_MANIFOLD`：真实 CSG。缺失时 difference/intersection 保留第一个 child 并 warning。
- `GK_WITH_FREETYPE`：`text()`。缺失时跳过并 warning。
- `GK_WITH_EARCUT`：凹多边形/洞与 glyph tessellation；iOS 当前关闭。

这些 capability 在 `src/Modules/CMakeLists.txt` 配置。warning 不是成功兼容的证明；正式资产以 0 warning 为目标。

## 资产与 compose

现行手写验证场景使用 `assets/scad/source/beer_cup.scad`、`source/old_city.scad` 等。Kit 位于
`assets/scad/lib/`，严格 JSON spec 位于 `assets/scad/specs/`，派生场景按结构位于
`assets/scad/source/generated/` 或 `assets/scad/proc/generated/`。生成管线见
`docs/designs/scad-scene-compose-design.md`。

## 验证

```bash
./gnb.sh build gkNextRenderer gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[Scad]"
./gnb.sh shot --scene assets/scad/source/old_city.scad
./gnb.sh shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60
```

排查顺序：先看 `SCAD:` warning 与 include 路径，再看节点/三角形/颜色 bucket 数；几何黑面检查 face winding 和 normal smoothing；复杂 boolean 慢时缩小 CSG 范围，不要对整座场景做一次巨型 operation。
