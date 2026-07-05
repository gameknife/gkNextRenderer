---
title: "SCAD 加载器（SCADLoader）设计与开发计划"
category: design
status: 已完成
owner: engine
created: 2026-05-30
last_updated: 2026-05-30
---

# SCAD 加载器（SCADLoader）设计与开发计划

> 状态：**Phase 0–7 基本完成**（Manifold 真·CSG + 法线平滑 + FreeType text + rotate_extrude + 半透明材质 + earcut 凹/带洞 + children()/list comprehension/echo/assert/str + 编辑器集成 + AGENT_GUIDE 文档）。剩 `offset`/`projection`/`resize`/`minkowski`/`import`（示例未用）。
> 实现细节文档已落 `AGENT_GUIDE/SCADLoader.md`（并挂入 `AGENTS.md` Key References）。
> 验证场景：`acient_city.scad`（古城，0 warn）+ `beer_cup.scad`（啤酒杯：rotate_extrude 把手 / sphere 泡沫 / 半透明玻璃，0 warn）。全量构建绿、单测 103 用例 / 4035 断言通过。
> 关联示例：`assets/scad/acient_city.scad`（+ `assets/scad/acient_city/*.scad`）。
> 参考实现：[openscad/openscad](https://github.com/openscad/openscad)、本仓库 LDraw 加载器（`AGENT_GUIDE/LDrawLoader.md`）。

## 0. 实现状态（Phase 0–5 已落地）

**已完成（Phase 0–5）**，`gnb build` 全绿（全部 targets，MSVC `/WX` + unity build），单测全过（96 用例 / 3996 断言），端到端验证：
`./gnb run gkNextRenderer --load-scene "assets/scad/acient_city.scad"` →
`uploaded scene [acient_city.scad] to gpu`（CPU 解析 ~39ms，GPU 上传 ~246ms，**23 个颜色组 / 153552 三角形**，仅 1 条 warn=text）。
门洞/拱、护城河、水井等 `difference` 现已**真实掏空**（Manifold 布尔）。

落地文件（`src/Engine/Assets/Loaders/`）：`FScadTypes.h`、`FScadLexer.{h,cpp}`、`FScadParser.{h,cpp}`、`FScadEvaluator.{h,cpp}`、`FScadGeometry.{h,cpp}`、`FScadCsg.{h,cpp}`、`FScadLoader.{h,cpp}`。
集成点：`SceneList`（`.scad` 分发 + `assets/scad/` 扫描）、`assets/CMakeLists.txt`（`scad` 资源拷贝）、`EngineCVars.cpp` + `UserSettings.hpp`（`sys.scadToWorldScale`）、`vcpkg.json`（`manifold`，桌面）、`src/CMakeLists.txt`（`find_package(manifold)` + `GK_WITH_MANIFOLD`）、`Test_ScadLoader.cpp`（12 用例）、`Test_SceneList.cpp`、`visual_test.json`。

**CSG 后端（Phase 5）：**
- `FScadCsg`（`GK_WITH_MANIFOLD` 守卫）用 **Manifold 3.2.1**（vcpkg，依赖 clipper2；TBB 关闭、串行 `MANIFOLD_PAR=-1`）实现 `difference/intersection/hull`，只对 TriSoup 操作，evaluator 后端无关。
- `union/group` 仍为拼接（不透明渲染等价、省去布尔开销）；只有 `difference/intersection/hull` 走 Manifold。
- 输入网格按精确顶点焊接成 `MeshGL`→`Manifold`；布尔失败（非流形/退化）自动回退原始正几何并 warn。
- Manifold 不可用时（如移动端）`FScadCsg` 退化为首子近似，`GK_WITH_MANIFOLD=0`，加载仍成功。
- **CMake 隔离要点**：Manifold 的 imported target 通过 INTERFACE 暴露 `cxx_std_17` + `-D` 选项，会污染引擎 C++20 编译——已 `set_target_properties(... INTERFACE_COMPILE_FEATURES "" INTERFACE_COMPILE_OPTIONS "")` 剥离，并手动以 `target_compile_definitions` 补回 `MANIFOLD_CROSS_SECTION`/`MANIFOLD_PAR=-1`。可用 `-DGK_DISABLE_MANIFOLD=ON` 强制关闭。

**法线平滑（Phase 6）：** bake 阶段按位置焊接收集相邻面，**面积加权**平滑共享法线，仅在相邻面夹角 ≤ `ScadLoadOptions::smoothAngleDegrees`（默认 35°）时生效——球/圆柱/圆锥变光滑，cube/cap/屋脊保持硬边。`ScadComputeSmoothNormals` 在 `FScadLoader`。

**text()（Phase 6）：** `FScadText`（`GK_WITH_FREETYPE` 守卫）用 **FreeType** 解析字形轮廓（`assets/fonts/DroidSansFallback.ttf`，含 CJK，"酒楼"正常），贝塞尔离散→按 even-odd 嵌套分组成「外环 + 洞」→ **earcut**（`mapbox/earcut.hpp`，header-only）三角化顶/底盖 + 逐环侧墙 → 沿 +Z 拉伸，直接产出 object-space 三角汤喂给 `linear_extrude`。FScadText.cpp 因重型三方头文件**排除出 unity build**；FreeType 不可用时 `text()` 跳过并 warn（`-DGK_DISABLE_SCAD_TEXT=ON` 可强制关闭）。验证：城加载 **0 warning / 24 颜色组 / 154596 三角形**。

**rotate_extrude + 2D 子求值（Phase 6）：** `FScadEvaluator::Collect2D` 用 `glm::dmat3` 2D 仿射栈收集 `linear_extrude`/`rotate_extrude` 子节点的闭合 2D 轮廓，支持嵌套 `translate/rotate/scale/union/circle/square/polygon` 及用户模块展开。`ScadGeometry::BuildRotateExtrude` 把 CCW 轮廓绕 Z 轴旋转 `angle` 度（`$fn` 控制分段；360° 闭合、<360° 加端盖）。验证：`beer_cup` 的 `torus_like`（`rotate_extrude` + `translate` + `circle`）作为把手，再被 `difference` 切成 C 形。

**半透明材质（Phase 6）：** `color([r,g,b,a])` 的 `a < 0.99` → `Material::Dielectric(ior=1.45)` 且 `Diffuse=(rgb,a)`（玻璃/液体）；否则 `Lambertian`。颜色分桶含 alpha，故不同透明度自动分到不同材质。验证：`beer_cup` 玻璃杯身/啤酒/泡沫呈半透明。

**仍存在的有意偏差（务必知悉）：**
- **求值与几何融合**：未建独立 CSG 树；`FScadEvaluator` 走 transform/color 栈，返回按颜色分组的三角汤（`GeomList`），CSG 节点就地调用 `FScadCsg`。
- **变量为动态作用域**：模块/函数体可见调用链变量（比 OpenSCAD 宽松），示例无碰撞；`$fn` 因此天然动态生效。
- **`include` 顶层语句追加在 main 之后**（丢失插入位置）；示例全为 `use`，不受影响。
- **`rotate_extrude` / `offset` / `projection` 尚未实现**（示例未用）；`polygon()` 的凹多边形目前仍是扇形三角化（text 路径已用 earcut，可后续复用到 polygon）。

**附带修复（非 SCAD）：** `UserInterface.hpp` 使用 `GK_NON_COPIABLE` 却未 include `CoreMinimal.hpp`，是潜在的 unity-build 顺序脆弱点（该文件成为某 unity 批次首文件时编译失败）；已补 include。

---

## 1. 目标

让引擎能够像加载 `.gltf` / `.ldr` 一样，直接把 OpenSCAD 的 `.scad` 文件加载为可渲染场景：

```
./gnb run gkNextRenderer --load-scene "assets/scad/acient_city.scad"
```

最终验收：日志出现 `uploaded scene [...] to gpu`，画面渲染出古城（城墙 / 城门 / 房屋 / 装饰），几何与颜色与 OpenSCAD 预览大体一致。

非目标（明确排除）：
- 不做 OpenSCAD 的 GUI / 实时编辑回写 `.scad`。
- 不追求与 OpenSCAD 渲染结果像素级一致，只要求**结构正确、颜色正确、可渲染**。
- 不实现 `import`（导入外部 stl/dxf/svg）、`surface`、`projection`、`offset`、`minkowski`（首批），列入后续可选。

---

## 2. 背景调研

### 2.1 OpenSCAD 的三段式管线（目标仓库结构）

OpenSCAD 的核心被组织成三层，落在 `src/core` 与 `src/geometry`：

| 阶段 | 目标仓库位置 | 职责 |
|------|------------|------|
| **① 词法/语法 → AST** | `lexer.l`(flex) + `parser.y`(bison)，`src/core/Expression.*`、`Assignment.*`、`ModuleInstantiation.*`、`UserModule.*`、`LocalScope.*` | 把源文件解析成抽象语法树：表达式、赋值、模块/函数定义、模块实例化（带子作用域） |
| **② AST → CSG 节点树** | `src/core/*`：`Context`/`ScopeContext`/`EvaluationSession`、`CSGNode`、`CsgOpNode`、`TransformNode`、`ColorNode`、`LinearExtrudeNode`、`TextNode` 等；`UserModule` 内联 + `children()` 代换 | 在变量上下文（Context）中求值 AST，实例化产生 `AbstractNode` 树（即 CSG 树）。`$fn/$fa/$fs/$t` 等特殊变量随 Context 动态作用域传递 |
| **③ CSG 树 → 网格** | `src/geometry`：`GeometryEvaluator`、`PolySet`、`Polygon2d`、布尔后端 `geometry/manifold/` 与 `geometry/cgal/`、`ClipperUtils`（2D 裁剪）、`PolySetBuilder` | 遍历 CSG 树：图元→`PolySet`/`Polygon2d`；变换乘矩阵；`union/difference/intersection` 调用布尔后端（**Manifold 为现代默认后端**，CGAL Nef 为旧后端）；2D→3D 走 `linear_extrude`/`rotate_extrude` |

**对我们最关键的三个语义点：**

1. **`use <file>` vs `include <file>`**：`use` **只导入模块/函数定义，不执行被导入文件的顶层实例化语句**；`include` 等价文本内联并执行顶层语句。
   - 示例里每个子文件（`scene.scad`/`walls.scad`/…）末尾都有 `ancient_city();`、`city_walls();` 之类的 **standalone preview** 调用。因为它们是通过 `use <>` 引入的，这些预览调用**不会被执行**。**加载器必须正确实现该语义，否则会重复渲染每个子模块的预览。**
2. **隐式 union**：文件顶层的多条实例化、模块体内的多个子节点，会被隐式并集成一个根。我们的"按颜色分组合并"天然满足。
3. **特殊变量动态作用域**：`$fn=32` 在文件顶层设置后，对其后所有 `sphere/cylinder/circle` 的细分数生效；模块内可被覆盖。需要在求值上下文里把 `$`-变量按动态作用域处理。

### 2.2 示例文件用到的语言特性（实现范围的"地面真值"）

逐个扫描 `assets/scad/acient_city/*.scad` 后，**首批必须支持**的特性集合：

**图元（3D）**
- `cube(size, center=)`（`size` 可为标量或 `[x,y,z]`）
- `cylinder(h=, r=|r1=,r2=, center=, $fn)`（含圆锥 `r1/r2`）
- `sphere(r=, $fn)`
- `polyhedron(points=[...], faces=[[...]])`（faces 含三角形与四边形）

**图元（2D，仅在 `linear_extrude` 内）**
- `polygon(points=[...])`
- `text("...", size=, halign=, valign=)`（含中文 "酒楼"）

**变换 / 装饰**
- `translate([x,y,z])`、`rotate([x,y,z])`、`scale([x,y,z])`
- `color([r,g,b])`（参数也可是函数返回的 vec3）

**CSG**
- `union() { ... }`、`difference() { ... }`（城门洞/拱、护城河、水井）

**2D→3D**
- `linear_extrude(height) { polygon | text }`

**语言/控制流**
- `module name(p=default, ...) { ... }`、`function name(p) = expr;`
- 链式单子变换：`color(...) translate(...) boxc(...);`
- `for (i=[a:b])`、`for (i=[a:step:b])`、`for (x=[v0,v1,...])`、笛卡尔 `for (x=[...], y=[...])`
- 变量赋值 `L = 16;`；关键字实参 `cube(s, center=true)`
- 表达式：算术 `+ - * / `、一元负号、索引 `p[0]`、内置函数 `max/floor`
- 特殊变量 `$fn`
- `use <relative/path.scad>`（相对主文件目录解析）

**首批可降级/暂缓**（不阻塞示例加载，见 §3.3 / §3.7）：
- `difference`/`intersection` 的**精确**布尔（MVP 降级，Phase 5 接 Manifold）
- `text` 的真实字形（MVP 占位，Phase 6 接 freetype）
- `rotate_extrude`、`hull`、`minkowski`、`mirror`、`multmatrix`、`resize`、`children()` 透传（按需在 Phase 5/6 补）

### 2.3 gkNextEngine 加载管线与复用点

加载器的输出契约（与 `FLDrawLoader` / `FSceneLoader` 完全一致）：

```cpp
// 入口签名（镜像 FLDrawLoader::LoadLDrawScene）
static bool LoadScadScene(
    const std::string& filename,
    EnvironmentSetting& cameraInit,
    std::vector<std::shared_ptr<Node>>& nodes,
    std::vector<Model>& models,
    std::vector<FMaterial>& materials,
    std::vector<LightObject>& lights,
    std::vector<AnimationTrack>& tracks,
    std::vector<Skeleton>& skeletons,
    const ScadLoadOptions& options = {});
```

| 复用点 | 位置 | 用途 |
|--------|------|------|
| 扩展名分发 | `SceneList::LoadScene` (`src/Engine/Runtime/Scene/SceneList.cpp:1681`) | 在 `.ldr/.mpd` 分支后新增 `.scad` 分支 |
| 场景分类 / 扫描 | `GetSceneCategory`、`kSupportedSceneExtensions`、`ScanScenes`（同文件 `:60/:78/:1594`） | 新增 `ESceneCategory::Scad`、扩展名、扫描 `assets/scad/` |
| 网格构造 | `FProcModel::CreateFromBuffers(name, vertices&&, indices&&, genTSpace)` (`FProcModel.h:13`) | 用自定义三角形 buffer 造 `Model`（AABB/切线自动算） |
| 现成图元 | `FProcModel::CreateBox/CreateSphere` | `cube`/`sphere` 可直接复用或作参考 |
| 材质工厂 | `Material::Lambertian/Metallic/Dielectric/Mixture/DiffuseLight` (`Material.hpp:10+`) | `color()` → 材质 |
| 节点构造 | `SceneBuilder::CreateRenderNode(name, t, s, instanceId, modelId, materialId, …)` (`SceneBuilder.h:18`) | 每个（颜色分组）网格 → 一个 Node |
| 相机自动对焦 | `FSceneLoader::AutoFocusCamera(cameraInit, nodes, models)` (`FSceneLoader.h:12`) | 无相机时按包围盒对焦 |
| 展示地板 / 环境 | `FLDrawLoader.cpp:374 AppendLDrawFloor` + `:851 HasSky/HasSun` | 参考其"白色地板 + sky on/sun off"的收尾 |
| 缩放 CVar 模式 | `EngineCVars.cpp:110 sys.ldrawLduToWorldScale` + `UserSettings.LDrawLduToWorldScale` | 新增 `sys.scadToWorldScale` |

**关键约束（必须遵守）：**

- **`Model` 构造私有**：需在 `Model.hpp:171` 的 friend 列表加 `friend class FScadLoader;`（或统一走 `FProcModel::CreateFromBuffers`，避免改 Model）。**推荐走 `CreateFromBuffers`，零侵入。**
- **每个 Node 材质槽位上限 16**：`SceneBuilder::CreateRenderNode` 的多材质重载是 `std::array<uint32_t,16>`。古城里独立颜色约 20+ 种，**单 Node + 多 section 会超限**。→ 采用 **"按颜色分组：每种颜色一个 Model + 一个单材质 Node"**（§3.4），每 Node 只占 1 槽，彻底规避。
- **GPU 顶点 `MaterialIndex` 仅 8 bit**（`Vertex.hpp:97 packUint8`），即单模型 section ≤ 256。按颜色分组下每模型只有 1 个 section，无压力。
- **坐标系**：引擎是 **Y-up 右手**（`lookAt(..., up=(0,1,0))`、procedural 场景用 `vec3(x, height, z)`）。OpenSCAD 是 **Z-up 右手**。见 §3.1。
- `Model` 顶点结构 `Vertex{Position, Normal, Tangent(vec4), TexCoord, MaterialIndex}`（`Vertex.hpp:9`）。

---

## 3. 关键设计决策

### 3.1 坐标系转换：Z-up → Y-up

OpenSCAD（右手 Z-up）→ 引擎（右手 Y-up），用绕 X 轴 −90° 旋转：

```
world = (x, z, -y)     // 即 y' = z, z' = -y
```

该映射行列式为 **+1（纯旋转）**，**不翻转三角形 winding，无需补偿**（比 LDraw 的 Y-flip 简单）。法线同样用该旋转变换。

实现要点：在**最终写入引擎顶点时**统一应用 `ScadToWorld`（4x4 旋转矩阵），CSG/几何计算阶段全程保持 SCAD 原生 Z-up 坐标，降低心智负担。

### 3.2 单位与缩放

示例注释"1 unit ≈ 1 meter"，古城约 260×190 units → 直接当米即可（城市尺度合理）。
提供 `ScadLoadOptions::scadToWorldScale`（默认 `1.0`），并接 CVar `sys.scadToWorldScale`（`Archive` 持久化到 `cvar_user.json`），镜像 `sys.ldrawLduToWorldScale` 的做法。缩放在 §3.1 的矩阵里一并烘焙。

### 3.3 CSG 布尔策略（分阶段）

布尔运算是 OpenSCAD 的灵魂，也是最重的部分。分两步落地：

- **MVP（Phase 3/4）——降级语义，保证能加载：**
  - `union()`：把所有子几何**直接拼接**（concat 三角形）。语义正确。
  - `difference()` / `intersection()`：**只取第一个子操作数的几何，忽略其余**，并打 `SCAD: difference approximated` 警告。
    - 对古城影响：城门洞/拱（`difference` 掏洞）会变成实心块、护城河变实心薄板、水井不掏空。**结构与画面整体仍成立**，可接受作为里程碑。
- **Phase 5——接入 Manifold 真布尔：**
  - 引入 vcpkg `manifold`（OpenSCAD 现代默认后端，MIT/Apache，轻量、鲁棒）。
  - 几何中间表示用三角网格（`manifold::MeshGL`），叶子图元三角化后喂入；`union/difference/intersection` 调用 `manifold::Boolean`；`hull`→`manifold::Hull`。
  - 结果回 `Vertex/index`，再做法线（按面或按光滑角）。
  - 备选：若 `manifold` 在某平台 vcpkg 不可用，回退到 header-only 的 `csgjs`（BSP 布尔，MIT），鲁棒性略差但零外部依赖。

> 决策依据：CGAL Nef 太重 + 许可（GPL）风险；Manifold 是 OpenSCAD 自身现代默认，工程上最对口。

### 3.4 几何 → 引擎模型映射：按颜色分组（group-by-color）

CSG 树求值完成后，得到一组**叶子几何 + 其生效颜色**（颜色来自最近的祖先 `color()`，默认色用一个中性灰）。映射：

```
叶子三角形 → 按 quantized RGBA 聚桶
每个颜色桶 → 一个 Model（CreateFromBuffers）+ 一个单材质 Node（CreateRenderNode）
颜色桶 → 一个 FMaterial（§3.5）
```

优点：① 每 Node 仅 1 材质槽（规避 16 限制）；② 模型数 = 颜色数（古城 ~20 个），BLAS 数可控；③ 实现简单。
缺点：失去 per-instance 节点（编辑器里不能单独选中一个房子）。→ 列入 Phase 7 可选项 B：**按"顶层模块实例 + 颜色"分组**，兼顾可选中性与实例化。

### 3.5 颜色 → 材质映射

- `color([r,g,b])` / `color([r,g,b,a])` / `color("red")`（命名色表，对齐 CSS/OpenSCAD 名）。
- 默认（无 `color`）：中性灰 `Lambertian(0.73)`（与引擎 `root_default` 一致）。
- 不透明 → `Material::Lambertian(rgb)`（古城几乎全是漫反射，先全 Lambertian，质感足够）。
- `a < 1`（半透明，如护城河水面可调）→ `Material::Dielectric` 或带 alpha 的 Mixture（Phase 5 再细化）。
- 颜色去重：`std::map<quantizedRGBA, materialIndex>` 保证确定性（参考 LDraw 用有序 map 避免排序抖动）。

### 3.6 三角化

- **凸多边形 / 三角形 / 四边形**：扇形三角化（fan）即可——示例的 `polygon`（旗帜=三角形）、`polyhedron` faces（三角形+四边形）全部满足。MVP 用 fan。
- **任意（凹）多边形**：Phase 6 引入 header-only `earcut.hpp`（mapbox，ISC 许可，可直接放 `src/ThirdParty/` 或 vcpkg `earcut-hpp`）。
- `polyhedron`：逐 face 三角化（fan），按 face 顶点顺序定法线（OpenSCAD 约定 faces 顶点顺时针朝外，需与引擎 CCW 对齐——见 §3.1 旋转不翻 winding，故按 SCAD 原序生成后法线方向需校验，必要时整体翻一次）。

### 3.7 text / 字体策略

- **MVP**：`text()` 生成一个与 `size`/对齐近似的**占位扁平矩形**（或直接跳过并 warn）。古城的 "酒楼" 招牌缺字不影响整体。
- **Phase 6**：用 freetype（已随 `imgui[freetype]` 传递可用）取字形 outline → 贝塞尔离散为闭合 2D 轮廓（含洞）→ earcut 三角化 → `linear_extrude`。需处理 CJK（示例是中文），字体路径走引擎现有 `assets/fonts`。

### 3.8 求值模型（解释器语义）

为控制复杂度，采用"先收集、后执行"的作用域模型（与 OpenSCAD 行为足够接近）：

1. **解析**整个 `use`/`include` 闭包，建立模块/函数定义表（`use` 只收定义，不执行顶层语句；`include` 收定义且把顶层语句加入执行序列）。
2. **文件/模块作用域求值**：先把该作用域内所有 `name = expr;` 赋值收集进 Context（后值覆盖前值），再按出现顺序执行实例化语句。
3. **模块实例化**：内置模块 → 直接产 CSG 节点；用户模块 → 新建子 Context（绑定形参/默认值、`$`-变量动态继承），执行其体；`children()` 把调用点的子节点代入。
4. **特殊变量**：`$fn/$fa/$fs/$t` 在 Context 链上动态查找；图元细分数 `getFragmentsFromR(r, $fn,$fa,$fs)` 复刻 OpenSCAD 公式。
5. **递归保护**：模块/函数调用深度上限（防恶意/笔误无限递归），超限 warn 并截断。

---

## 4. 模块与文件结构（镜像 `Loaders/FLDraw*`）

```
src/Engine/Assets/Loaders/
├── FScadTypes.h           # ScadLoadOptions、Value（变体）、AST 节点、CSG 节点枚举、共享常量
├── FScadLexer.h/.cpp      # 词法：注释/数字/字符串/标识符/$特殊变量/运算符/范围
├── FScadParser.h/.cpp     # 语法：表达式 + 语句 → AST（模块/函数定义、实例化、for/if/let、赋值）
├── FScadEvaluator.h/.cpp  # AST + Context → CSG 节点树（变量/表达式求值、模块内联、children、$fn）
├── FScadGeometry.h/.cpp   # CSG 节点树 → 三角网格：图元生成 + 三角化 + 变换 + 布尔后端 + 按颜色分组
└── FScadLoader.h/.cpp     # 场景组装：颜色桶→Model/Material/Node、相机对焦、环境、地板、include 解析
```

依赖方向：`Loader → Geometry → Evaluator → Parser → Lexer → Types`（单向，便于单测每层）。

**改动的现有文件：**
- `SceneList.cpp`：`GetSceneCategory` 加 `.scad`、`kSupportedSceneExtensions` 加 `.scad`、`LoadScene` 加分支、`ScanScenes` 扫 `assets/scad/`（仅顶层 `.scad`，跳过子目录里的被 `use` 文件）。
- `assets/CMakeLists.txt`：`ASSET_DIRS` 追加 `scad`（当前为 `anims configs fonts ... typescript`，无 `scad`）。
- `EngineCVars.cpp` + `UserSettings.hpp`：新增 `sys.scadToWorldScale`。
- （可选）`Model.hpp`：若不走 `CreateFromBuffers`，加 `friend class FScadLoader;`。**推荐不改，走 `FProcModel::CreateFromBuffers`。**
- `assets/configs/visual_test.json`：加一条 `acient_city.scad` 场景做视觉回归。

---

## 5. 数据流

```
acient_city.scad
   │  (FScadLoader: 解析 use/include 闭包，定位主文件目录)
   ▼
FScadLexer → tokens
   ▼
FScadParser → AST（每个 .scad 文件一棵；定义表 + 顶层语句序列）
   ▼
FScadEvaluator（Context: 变量 + 模块/函数表 + $fn 等）
   │  顶层隐式 union；用户模块内联；for/if 展开；color/transform 入栈
   ▼
CSG 节点树（叶=图元/extrude；内=union/difference/intersection/transform/color）
   ▼
FScadGeometry
   │  图元→网格(Z-up)；变换乘矩阵；布尔(MVP=union concat / Phase5=Manifold)
   │  收集 (三角形, 生效颜色)；Z-up→Y-up 旋转 + scadToWorldScale
   ▼
按颜色分组 → { 颜色: (vertices, indices) }
   ▼
FScadLoader
   │  每色: CreateFromBuffers→Model；color→FMaterial；CreateRenderNode→Node
   │  AutoFocusCamera；HasSky=on/HasSun=off；(可选)展示地板
   ▼
nodes / models / materials / lights  →  SceneList → GPU
```

---

## 6. 开发计划（分阶段里程碑）

> 每个 Phase 末尾都要能 `gnb build --reconfigure` 通过。Phase 4 末尾要求 `--load-scene acient_city.scad` 端到端跑通（降级 CSG）。
> 建议拆成 `.spec/TODO.md` 任务，ID 形如 `scad-p0-01`。

### Phase 0 — 脚手架与分发（0.5d）✅ 已完成
- [ ] 建 `FScadTypes.h` + 六个空 `.cpp/.h`，加入 `src/Engine` 的 CMake 源列表（确认 glob 或显式列表机制）。
- [ ] `SceneList`：`ESceneCategory::Scad`、`.scad` 进 `kSupportedSceneExtensions`、`GetSceneCategory`、`LoadScene` 分支调用 `FScadLoader::LoadScadScene`（先返回 `false` 占位）。
- [ ] `ScanScenes` 扫描 `assets/scad/` **顶层** `.scad`（不递归进 `acient_city/`，否则会把子模块文件当独立场景）。
- [ ] `assets/CMakeLists.txt` `ASSET_DIRS` 加 `scad`。
- [ ] 单测：`Test_SceneList` 补 `.scad` 识别用例。
- **验收**：构建通过；场景列表里出现 `assets/scad/acient_city.scad`；选中能进 loader（暂空场景）。

### Phase 1 — Lexer + Parser → AST（2–3d）✅ 已完成
- [ ] `FScadLexer`：注释（`//`、`/*…*/`）、数字（含小数/科学计数）、字符串（含转义/UTF-8）、标识符、`$fn` 等特殊标识、运算符、`[ ] { } ( ) , ; = :`。
- [ ] `FScadParser`：
  - 表达式：字面量/向量/范围、变量引用、函数调用、索引 `[]`、成员 `.x/.y/.z`、算术/比较/逻辑/三元、`let()`。
  - 语句：`name = expr;`、`module`/`function` 定义、模块实例化（位置+关键字实参、单子链式 / `{}` 多子）、`for`/`if`/`else`/`intersection_for`/`let`、修饰符 `* ! # %`、`use`/`include`。
- [ ] 错误恢复：行号 + 友好报错（`SCAD:` 前缀），单条语句失败不整体崩。
- [ ] 单测 `Test_ScadParser`：覆盖 `common.scad`/`props.scad` 全部语法形态（AST 结构断言）。
- **验收**：能无错解析 `acient_city/` 全部 9 个文件。

### Phase 2 — Evaluator → CSG 节点树（3–4d）✅ 已完成（融合实现，未建独立 CSG 树）
- [ ] `Value` 变体（number/bool/string/vector/range/undef）+ 运算符语义（含向量逐元素、布尔短路）。
- [ ] 内置函数：`max min floor ceil abs sqrt sin cos tan len concat …`（先覆盖示例用到的 `max/floor`，留扩展点）。
- [ ] Context/Scope：变量收集（后值覆盖）、模块/函数表、`$`-变量动态作用域、`getFragments`（`$fn` → 段数）。
- [ ] 实例化：内置模块产 CSG 节点；用户模块内联 + 形参默认值 + `children()` 代换；`for`/`if`/`let` 展开。
- [ ] **`use` 仅收定义不执行顶层；`include` 文本内联执行**（核心语义，务必单测）。
- [ ] 递归深度保护。
- [ ] 单测 `Test_ScadEvaluator`：`for` 展开计数、模块实参默认值、`use` 不触发 preview、`$fn` 传播。
- **验收**：`acient_city()` 求值出预期数量的叶子节点（与手算量级一致），无重复 preview。

### Phase 3 — 几何生成 + 按颜色分组（3–4d）✅ 已完成
- [ ] 图元三角化（Z-up）：`cube`、`cylinder`（含 `r1/r2` 圆锥、`$fn` 段数）、`sphere`、`polyhedron`（faces fan 三角化 + 法线方向校验）。
- [ ] 2D：`polygon` + `linear_extrude(height)`（fan 三角化凸多边形 → 拉伸；复用/参考 `FProcModel::CreateExtrudedConvexPolygon` 的侧面/顶底生成）。
- [ ] 变换：`translate/rotate/scale`（矩阵栈）；`color` 颜色栈。
- [ ] **CSG MVP**：`union`=concat；`difference`/`intersection`=取首操作数 + warn。
- [ ] Z-up→Y-up 旋转 + `scadToWorldScale`；按颜色分桶累积 `Vertex/index`。
- [ ] 单测 `Test_ScadGeometry`：cube 顶点数/AABB、cylinder 段数随 `$fn`、polyhedron 法线朝外、颜色分桶数。
- **验收**：单测绿；几何缓冲生成正确（离线 dump 顶点数核对）。

### Phase 4 — 场景组装 + 端到端（1–2d）✅ 已完成
- [ ] `FScadLoader`：颜色桶 → `CreateFromBuffers`(Model) + `FMaterial` + `CreateRenderNode`(Node)。
- [ ] `include` 路径解析（相对主文件目录）、主文件目录作为根。
- [ ] `AutoFocusCamera`；`HasSky=true`/`HasSun=false`（可加暖色 sun 增强古城氛围，留 option）；可选 `AppendScadFloor`。
- [ ] `sys.scadToWorldScale` CVar 接线（`EngineCVars` + `UserSettings` + `LoadScene` 传入）。
- **验收**：`./gnb run gkNextRenderer --load-scene "assets/scad/acient_city.scad"` 日志出 `uploaded scene [...] to gpu`，画面渲染古城（CSG 降级，门洞实心可接受）。补 `Test_ScadLoader` 加载冒烟测试 + `visual_test.json` 增项。

### Phase 5 — 真·CSG（Manifold）（3–5d）✅ 已完成
- [ ] `vcpkg.json` 加 `manifold`；CMake 链接；平台条件（桌面优先，移动端按需）。
- [ ] 几何中间层切到 manifold 网格：图元→`MeshGL`；`union/difference/intersection`→`manifold::Boolean`；`hull`→`Hull`。
- [ ] 结果→`Vertex/index` + 法线（按光滑角，参考 LDraw 45° 阈值）。
- [ ] 回归：门洞/拱/护城河/水井恢复正确掏空。
- [ ] 退路：manifold 不可用时 `csgjs`(BSP) header-only 兜底。
- **验收**：古城 difference 结构正确；视觉对照 OpenSCAD 截图基本一致。

### Phase 6 — 2D 子系统补全 + text（按需，2–4d）✅ 基本完成
- [x] 法线平滑（按角度阈值；`FScadLoader::ScadComputeSmoothNormals`）。
- [x] `square/circle`（在 `linear_extrude` 内）、`mirror/multmatrix`（evaluator 已支持）。
- [x] `text()`：freetype outline → 贝塞尔离散 → even-odd 分组 → earcut → extrude；CJK 走 `DroidSansFallback.ttf`。
- [x] earcut.hpp 引入（带洞三角化，text 路径）。
- [x] `rotate_extrude`（`FScadEvaluator::Collect2D` + `ScadGeometry::BuildRotateExtrude`）。
- [x] 半透明材质（`color` alpha → Dielectric）。
- [ ] `offset`（Clipper）、`projection`、`resize`、`polygon()` 凹多边形改走 earcut（剩余，示例未用）。
- **验收**：招牌 "酒楼" 正确成形 ✅；古城 0 warning ✅；啤酒杯（rotate_extrude 把手 / 半透明玻璃）0 warning ✅。

### Phase 7 — 优化与集成（按需）
- [ ] 可选分组策略 B：按"顶层模块实例 + 颜色"建 Node，提升编辑器可选中性 / 为 GPU instancing 铺路。
- [ ] 延迟材质创建；几何/解析磁盘缓存（`.cache/<hash>.bin`，参考 LDraw 改进项）。
- [ ] 编辑器 `ContentBrowserPanel` 支持 `.scad`（与 `.ldr` 同列）。
- [ ] `AGENT_GUIDE/SCADLoader.md` 落地（实现细节/已知限制，仿 `LDrawLoader.md`），并在 `AGENTS.md` 的 Key References 挂链接。

---

## 7. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| 写一个 OpenSCAD 子集解释器工作量被低估 | 进度 | 严格按 §2.2 "示例地面真值"裁剪首批特性；分层单测；非示例特性一律 warn 跳过不阻塞 |
| CSG 布尔鲁棒性（自相交/共面/退化） | 几何错误/崩溃 | MVP 先降级保证能加载；Phase 5 用工业级 Manifold；兜底 csgjs；布尔失败 try/catch + warn 回退到 concat |
| `manifold` 在某平台 vcpkg 不可用 | 跨平台 | 桌面优先；header-only `csgjs` 兜底；`ENABLE_SCAD_CSG` 编译开关 |
| `use` 语义实现错（执行了 preview） | 场景重复/爆量 | Phase 2 专门单测：`use` 后顶层语句不执行 |
| 16 材质槽 / 8bit MaterialIndex 限制 | 渲染错乱 | 按颜色分组（§3.4），每 Node 单材质，根除 |
| 坐标系/winding 翻转 | 黑面/镜像 | §3.1 纯旋转不翻 winding；polyhedron 法线方向加显式校验单测 |
| text/CJK 字形复杂 | 招牌缺字 | MVP 占位；非阻塞；Phase 6 再补 |
| 巨型场景顶点量 | 内存/构建慢 | 按颜色分组天然合并；后续磁盘缓存 + LOD（Phase 7） |

---

## 8. 测试策略

- **单元（Catch2，`src/Tests/`）**：`Test_ScadLexer`、`Test_ScadParser`、`Test_ScadEvaluator`、`Test_ScadGeometry`、`Test_ScadLoader`；`Test_SceneList` 补 `.scad`。每层独立可测（依赖单向）。
- **集成**：`LoadScadScene("assets/scad/acient_city.scad", …)` 冒烟——返回 true、nodes/models 非空、AABB 合理、materials 数 ≈ 颜色数。
- **视觉**：`assets/configs/visual_test.json` 增 `acient_city.scad`，`gkNextVisualTest` 出截图人工对照 OpenSCAD 预览。
- **调试约定**：所有日志前缀 `SCAD:`（`grep "SCAD"` 过滤）；warn 列出降级/跳过的特性与行号。

---

## 9. 关键复用点速查

| 现有功能 | 文件:行 | 用途 |
|---------|---------|------|
| 扩展名分发 / 场景分类 | `SceneList.cpp:60,78,1681,1594` | 接入 `.scad` |
| `FProcModel::CreateFromBuffers` | `FProcModel.h:13` | 三角形 buffer → Model（零侵入，免改 Model friend） |
| `FProcModel::CreateExtrudedConvexPolygon` | `FProcModel.cpp:231` | `linear_extrude` 顶底/侧面生成参考 |
| `Material::Lambertian/...` | `Material.hpp:10+` | `color()` → 材质 |
| `SceneBuilder::CreateRenderNode` | `SceneBuilder.h:18` | 颜色桶 → Node |
| `FSceneLoader::AutoFocusCamera` | `FSceneLoader.h:12` | 相机对焦 |
| `AppendLDrawFloor` / 环境收尾 | `FLDrawLoader.cpp:374,851` | 地板 + sky/sun 设定模板 |
| CVar 持久化模式 | `EngineCVars.cpp:110` + `UserSettings.hpp:15` | `sys.scadToWorldScale` |
| `Vertex` 结构 / 8bit 材质 | `Vertex.hpp:9,97` | 顶点装配约束 |

## 10. 参考资料

- OpenSCAD 源码：`src/core`（lexer/parser/AST/CSG 节点）、`src/geometry`（GeometryEvaluator/PolySet/manifold,cgal 布尔后端）—— https://github.com/openscad/openscad
- OpenSCAD 语言手册（语义权威）：https://en.wikibooks.org/wiki/OpenSCAD_User_Manual
- 本仓库 `AGENT_GUIDE/LDrawLoader.md`：分层 Loader/Parser/Geometry 拆分、坐标转换、材质分组、法线平滑、地板/相机收尾的**直接参照范本**。
- Manifold 布尔库：https://github.com/elalish/manifold ；兜底 csgjs：https://github.com/dabroz/csgjs-cpp ；earcut：https://github.com/mapbox/earcut.hpp
