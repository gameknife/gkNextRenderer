# SCAD（OpenSCAD DSL）加载器

本文记录当前 `ScadLoader` 可选模块。它实现 OpenSCAD 风格子集，不是完整 OpenSCAD 兼容层；功能判断以 `src/Modules/ScadLoader/` 和 `[Scad]` 单测为准。

## 入口与文件

应用链接 `ScadLoader` 后，在加载/扫描场景前调用 `Modules::Scad::Register()`，把 `.scad` loader 注册到 `Assets::FLoaderRegistry`。主要文件：

- `ScadModule.*`：模块注册。
- `FScadLexer.*` / `FScadParser.*` / `FScadTypes.h`：词法、AST 与值模型。
- `FScadEvaluator.cpp`、`.Expr.cpp`、`.Geometry.cpp`：作用域、语言求值和几何分发。
- `FScadGeometry.*` / `FScadCsg.*` / `FScadTess.*` / `FScadText.*`：图元、Manifold boolean、earcut 和 FreeType text。
- `FScadShared.*`：scene 与 rig 共用的 `use/include` 闭包、坐标转换和法线工具。
- `FScadSourceIndex.*`：顶层语句与 module/function 定义的**字节区间索引**。parser 在解析时
  记录每条顶层语句的 `[begin, end)`（`ScadParser::Parse` 的 `outTopLevelSpans`），`use/include`
  只做掩码不删字节，因此索引里的偏移直接寻址调用方的原始源码。编辑器靠它做单语句改写；
  `ApplyScadSourceEdits` 从后往前套用编辑并丢弃重叠的陈旧区间。
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

**"直属"是字面意思**：只有 module body 里由 builtin 产生的几何留在该 Node 上，body 里每一次
user module 调用都另开一个子 Node 带走自己的几何。库里的材质包装（`ab_gold(c) { ... }` 这类
`gk_material` 的一行别名）也是 user module，所以一个 kit 件的调用节点常常自己不画任何东西，
几何全在下面一两层。玩法代码按模块名索引到的是那个调用节点，隐藏/显示/量尺寸要走
`Assets::NodeUtils` 的 `SetVisibleRecursive` / `SetRayCastVisibleRecursive` /
`GetSubtreeWorldBounds`，只改根节点会静默无效。

**`gk_flatten() { ... }`**：声明"以下全是几何、不是场景结构"，抑制子树内的 Node 创建，
几何并入最近的外层 Node。规则库（`kit_road` 把一张路网表展开成几千个子段调用）必须用它——
否则每个 module 调用各成一个 Node，也就各成一个 Model 和碰撞体。实测 1km 香港 tile
不加是 7683 个 Node、物理 shape cooking 1.2 秒；加上后 90 个 Node、34 毫秒。
分块粒度仍由调用方控制，每块要留在 65535 三角的 Model 上限之下。

`SceneEvalResult::SceneNode` 同时保留 module 调用的源行、求值后的具名参数、调用点颜色和
局部变换。ScadLibrary 使用这些作者元数据沿 module 调用树提取 Kit 实例；它不改变
运行时 Node/Model 的装配语义。

## ScadLibrary 的统一场景文档

**一个 `.scad` 根文件没有"类型"。** `FScadSceneDocument`（`Application/Editor/ScadLibrary/
ScadSceneDocument.*`）把打开的文件解析成一串顶层节点，逐条分类：

| 节点 | 源码形态 | 归谁编辑 |
| --- | --- | --- |
| `Instance` | `[color] translate rotate scale kit_module(args);` | 对象列表 + 视口 Gizmo |
| `Terrain` | `TERR = [...]` 赋值与 `gk_terrain(TERR)` 调用 | 地形画布 |
| `TerrainRule` | 顶层 `ter_*` 贴地规则 | 过程规则列表 |
| `Source` | 其余一切：循环、条件、`module`/`function` 定义、自由几何 | 源码页，可整条开关 |

四类在同一个文件里共存：`source/` 下的程序可以直接加实例和地形，`evaluated/` 下的实例
列表也可以加循环。目录只决定归档位置，不再决定能用哪个编辑器。

被识别为 `Instance` 的条件是保守的：变换参数必须是字面量，且按 `color → translate →
rotate → scale` 顺序最多各出现一次，终点是 kit 表里能解析到的 module。不满足就留作
`Source`——对象编辑器只认领它能逐字写回的语句。终点调用的实参**从源码字节切片**读取，
所以 AST 打印不回来的表达式也能原样保留。

写回是**按语句拼接**（`FScadSceneDocument::BuildSource` + `ApplyScadSourceEdits`），只重写
真正改动过的语句：文档保留解析时的实例副本与 terrain/规则序列化快照，逐条比对。打开再保存
一个没动过的场景是字节级 no-op，注释、格式和不支持的语法全部保留。`Test_ScadSceneDocument.cpp`
用 `old_city.scad` 与 `terrain_layout_demo.scad` 守这条不变量。

### 逐节点关闭与展开

`Source` 节点里的**调用语句**可以原地关掉——在语句前加 OpenSCAD 的 `*` 修饰符，求值器
（`FScadEvaluator.Detail.h` 与 `.Geometry.cpp`）本来就跳过它。赋值和 `module`/`function`
定义不接受修饰符，`FScadSceneDocument::IsSwitchable` 会拒绝。

"关闭并展开为实例"求值**整个文件**（结构依赖它前面的变量和 module，片段无法单独求值），
按顶层语句的行区间 `[line, endLine]` 把 root SceneNode 归属回该语句，转成实例写在它后面，
再关闭原语句。这是过去"整文件 Source → Evaluated 转换"的逐结构替代品；保存前可撤销。
若该结构只产生图元或未知模块（没有可归属的 Kit root），操作会带原因失败，而不是静默产出空结果。

### Gizmo 与点选

场景对象 Gizmo 在引擎 Y-up 与 SCAD Z-up 之间用 `ScadToWorldBasis` 双向转换。
拖动期间直接更新匹配实例的运行时 Node，并用 `Scene::MarkTransformDirty()` 刷新 GPU 变换；
松手把该实例写回它所在的那条语句，不重写文件其余部分，也不触发 SCAD 重载。

视口点选使用引擎 CPU Picking 返回的 render-node instance ID，再沿 `Node::GetParent()` 向上
追溯 Kit 调用节点，优先匹配 evaluator 保留的稳定 instance ID，同名实例以世界变换消歧。
选中状态同步到对象列表和 Gizmo；首次点选的同一次鼠标按压不会立即抓取刚出现的 Gizmo。

## 当前语言面

- **图元 3D**：`cube`、`sphere`、`cylinder`(含 r1/r2 圆锥)、`polyhedron`
- **图元 2D**（在 extrude 内）：`circle`、`square`、`polygon`(含 `paths` 洞)、`text`(FreeType, CJK)
- **变换**：`translate`、`rotate`(欧拉/角轴)、`scale`、`mirror`、`multmatrix`、`color`(rgba/命名色)、
  `gk_material`(颜色 + roughness/metalness)
- **CSG**：`union`、`difference`、`intersection`、`hull`（Manifold）；`minkowski`(近似为 union)
- **拉伸**：`linear_extrude`(凹/带洞/嵌套变换)、`rotate_extrude`(angle，360 闭合/部分端盖)
- **控制流**：`for`(笛卡尔多绑定)、`if/else`、`let`、`intersection_for`(按 for)
- **表达式位置的 `let`**：`function f(v) = let (l = norm(v)) [v[0]/l, v[1]/l];`。规则库的
  函数体靠它给共享子式命名（`kit_geo_city` 的轮廓外扩、`kit_road` 的单位向量）；
  没有它只能把每个共享项内联展开，既难读又重复求值。绑定不外泄，且 `let` 覆盖的是
  **整个**后续表达式（`let (a = 1) a + 2` 得 3）。列表推导里的 `let` 语义不变（拼接元素）。
- **语言**：`module`/`function` 定义（含局部 helper 与后向引用）与默认参/关键字参、`children()`/`children(i)`/`$children`、list comprehension(`for`/`if`/`let`/`each`)、`echo`/`assert`/`str`、`$fn`/`$fa`/`$fs`
- **内置函数**：`max min abs floor ceil round sqrt pow exp ln log sign sin cos tan asin acos atan atan2 len norm concat str`（三角函数为角度制）
- **引擎扩展（`gk_` 前缀，非 OpenSCAD 标准）**：`gk_terrain(TERR)` 低模高度场地形 module +
  `gk_terrain_height(TERR,x,y)` / `gk_terrain_info(TERR,x,y)` 纯函数（`FScadTerrain.{h,cpp}`）。
  TERR 的 `["hmap", ...]` 算子可引用 `.hmap` 二进制 side-car（真实 DEM 等采样高度场），
  路径为 runtime-root 相对、经 `ScadReadAsset` 读取，pak 与 loose 行为一致。
  地形桶带 faceted 标志（loader 跳过法线平滑）、水面桶拆分为 `__water` 子节点（rayCast 不可见、
  无物理体）、地形数据经 `SceneTerrain` payload 挂成引擎 `TerrainComponent`。
  详见 `AGENT_GUIDE/ScadTerrain.md`。

`gk_material(c, roughness = 1, metalness = 0, alpha = 1) children();` 是材质表达扩展。
`roughness` 和 `metalness` 都会被限制到 `0..1`，并随几何桶传入 GPU 材质；未包在
`gk_material` 中的旧 `color()` 仍然生成 roughness=1、metalness=0 的 Lambertian，
因此既有资产不改变外观。材质参数也参与颜色桶合并键，避免相同颜色的玻璃和墙面被错误合并。

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

现行手写验证场景使用 `assets/scad/source/beer_cup.scad`、`source/oldcity/old_city.scad` 等。Kit 位于
`assets/scad/lib/`，严格 JSON spec 位于 `assets/scad/specs/`，派生场景位于
`assets/scad/source/generated/` 或 `assets/scad/proc/generated/`。生成管线见
`docs/designs/scad-scene-compose-design.md`。`gnb scad compose` 仍按 spec 有没有 `terrain`
段选择输出目录，但那只是归档：产物打开后照样能同时用对象、地形和结构三个编辑器。
目录约定见 `assets/scad/README.md`。

## 验证

```bash
./gnb.sh build gkNextRenderer gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[Scad]"
./gnb.sh shot --scene assets/scad/source/oldcity/old_city.scad
./gnb.sh shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60
./out/build/<preset>/bin/gkNextUnitTests "[SceneDocument]"
./gnb.sh validate --script assets/agentscripts/scadlibrary-mixed-document.agentscript.json
```

排查顺序：先看 `SCAD:` warning 与 include 路径，再看节点/三角形/颜色 bucket 数；几何黑面检查 face winding 和 normal smoothing；复杂 boolean 慢时缩小 CSG 范围，不要对整座场景做一次巨型 operation。
