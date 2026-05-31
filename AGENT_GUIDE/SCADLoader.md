# SCAD（OpenSCAD）文件加载器

本文档记录 `.scad`（OpenSCAD DSL）加载功能的实现细节、架构决策与已知限制。设计与开发计划见 `docs/SCADLoader-Design.md`。

## 功能概览

加载 `.scad` 文件，解析 `use`/`include` 闭包，求值出几何，按颜色分组生成可渲染场景。

- **支持格式**：`.scad`
- **资源依赖**：`assets/scad/`（示例场景）、`assets/fonts/DroidSansFallback.ttf`（text() 字形，含 CJK）
- **验证场景**：
  - `assets/scad/acient_city.scad`（古城：城墙/城门/房屋/装饰/招牌"酒楼"，0 warning）
  - `assets/scad/beer_cup.scad`（啤酒杯：rotate_extrude 把手 / sphere 泡沫 / 半透明玻璃，0 warning）
- **运行**：`./gnb run gkNextRenderer --load-scene "assets/scad/acient_city.scad"`

## 文件结构

```
src/Engine/Assets/Loaders/
├── FScadTypes.h            # ScadLoadOptions、Value 变体、AST（Expr/Stmt）、常量
├── FScadLexer.h/.cpp       # 词法（注释/数字/字符串/$特殊变量/运算符/范围）
├── FScadParser.h/.cpp      # 递归下降解析 → AST（module/function/for/if/let、表达式、list comprehension）
├── FScadEvaluator.h/.cpp   # AST + Context → 按颜色分组的三角汤（GeomList）；CSG/2D 子求值
├── FScadGeometry.h/.cpp    # 图元 cube/sphere/cylinder/polyhedron + linear/rotate extrude
├── FScadCsg.h/.cpp         # CSG 布尔后端（Manifold，GK_WITH_MANIFOLD）
├── FScadTess.h/.cpp        # earcut 凹多边形/带洞三角化（GK_WITH_EARCUT，排除 unity）
├── FScadText.h/.cpp        # text() 字形 → 轮廓（FreeType，GK_WITH_FREETYPE，排除 unity）
└── FScadLoader.h/.cpp      # 场景组装：颜色桶→Model/Material/Node、法线平滑、相机、环境
```

修改的现有文件：
- `SceneList.cpp`：`.scad` 扩展名分发 + `assets/scad/` 扫描（仅顶层，子文件经 use/include 拉入）
- `EngineCVars.cpp` + `UserSettings.hpp`：CVar `sys.scadToWorldScale`
- `assets/CMakeLists.txt`：`scad` 资源拷贝
- `src/CMakeLists.txt`：链接 `manifold::manifold` / `Freetype::Freetype`，定义 `GK_WITH_*`
- `vcpkg.json`：新增 `manifold`、`earcut-hpp`、`freetype`（桌面）
- `UserInterface.hpp`：补 `#include CoreMinimal.hpp`（修复 unity 顺序脆弱点）
- `UserInterface.hpp` 同时附带修复一个与 SCAD 无关的潜在 unity-build bug

## 数据流

```
.scad → ExtractDirectives(剥离 use/include) → Lexer → Parser → AST(每文件)
   → 合并 module/function 定义表 + main 顶层语句
   → Evaluator（transform/color 栈 + Context 动态作用域）
       图元→三角汤(Z-up)；CSG 节点调用 FScadCsg；2D 节点经 Collect2D + FScadTess
   → 按 quantized RGBA 分组的颜色桶
   → Loader：Z-up→Y-up(绕 X −90°) + scale；法线平滑；每桶 → Model + Material + Node
```

## 关键设计决策

| 议题 | 决策 |
|------|------|
| 坐标系 | OpenSCAD Z-up 右手 → 引擎 Y-up 右手，绕 X −90°：`world=(x, z, −y)`，纯旋转 det+1，**不翻 winding** |
| 缩放 | `ScadLoadOptions::scadToWorldScale`（CVar `sys.scadToWorldScale`，默认 1.0） |
| 几何→模型 | **按颜色分组**：每种颜色 1 个 Model + 1 个单材质 Node（规避 Node 16 材质槽上限） |
| CSG | `union/group` = 拼接（不透明等价、省布尔开销）；`difference/intersection` 先 `ScadCsg::Union` 合并正侧再走 **Manifold** 真布尔；`hull` = Manifold Hull |
| 求值 | 未建独立 CSG 树：Evaluator 带 transform/color 栈遍历，返回 `GeomList`（色→三角汤），CSG/extrude 节点就地调用后端 |
| 变量/定义作用域 | 变量采用动态作用域（模块/函数体可见调用链变量），比 OpenSCAD 宽松；`$fn` 因此天然动态生效。`module`/`function` 定义按 scope 建局部定义栈，支持模块内 helper 和后向引用 |
| 法线 | 平滑：按位置焊接相邻面，面积加权，夹角 ≤ `smoothAngleDegrees`（默认 35°）才平滑——曲面光滑、硬边保留 |
| 材质 | `color` 不透明→`Lambertian`；`alpha < 0.99`→`Dielectric(ior=1.45)` + `Diffuse=(rgb,a)`（玻璃/液体）。颜色分桶含 alpha |
| 2D | `Collect2D`（`glm::dmat3` 仿射栈）收集 `linear/rotate_extrude` 子轮廓，支持嵌套 translate/rotate/scale/union/circle/square/polygon(含 paths 洞)/用户模块 |
| 三角化 | text/凹多边形/带洞经 `FScadTess`（earcut，even-odd 嵌套分组）；无 earcut 时退化为凸多边形扇形 |

## 已实现的 OpenSCAD 子集

- **图元 3D**：`cube`、`sphere`、`cylinder`(含 r1/r2 圆锥)、`polyhedron`
- **图元 2D**（在 extrude 内）：`circle`、`square`、`polygon`(含 `paths` 洞)、`text`(FreeType, CJK)
- **变换**：`translate`、`rotate`(欧拉/角轴)、`scale`、`mirror`、`multmatrix`、`color`(rgba/命名色)
- **CSG**：`union`、`difference`、`intersection`、`hull`（Manifold）；`minkowski`(近似为 union)
- **拉伸**：`linear_extrude`(凹/带洞/嵌套变换)、`rotate_extrude`(angle，360 闭合/部分端盖)
- **控制流**：`for`(笛卡尔多绑定)、`if/else`、`let`、`intersection_for`(按 for)
- **语言**：`module`/`function` 定义（含局部 helper 与后向引用）与默认参/关键字参、`children()`/`children(i)`/`$children`、list comprehension(`for`/`if`/`let`/`each`)、`echo`/`assert`/`str`、`$fn`/`$fa`/`$fs`
- **内置函数**：`max min abs floor ceil round sqrt pow exp ln log sign sin cos tan asin acos atan atan2 len norm concat str`（三角函数为角度制）

## 依赖与编译开关

- `GK_WITH_MANIFOLD`（Manifold 3.2.1，clipper2 依赖，TBB 关闭/串行 `MANIFOLD_PAR=-1`）：真 CSG。关闭→difference/intersection 降级首子。`-DGK_DISABLE_MANIFOLD=ON` 强制关闭。
- `GK_WITH_FREETYPE`（FreeType）：text()。关闭→跳过 text + warn。`-DGK_DISABLE_SCAD_TEXT=ON` 强制关闭。
- `GK_WITH_EARCUT`（earcut-hpp，header-only）：凹多边形/带洞。关闭→凸扇形退化。
- **CMake 隔离要点**：Manifold imported target 的 INTERFACE 暴露 `cxx_std_17` + `-D` 选项会污染引擎 C++20 编译，已 `set_target_properties(... INTERFACE_COMPILE_FEATURES "" INTERFACE_COMPILE_OPTIONS "")` 剥离并手动补 define。
- **Unity build**：`FScadText.cpp`/`FScadTess.cpp` 因重型三方头文件 `SKIP_UNITY_BUILD_INCLUSION`。

## 性能数据（RTX 5070 Ti）

| 场景 | 颜色组 | 三角形 | CPU 解析 | GPU 上传 |
|------|--------|--------|----------|----------|
| acient_city.scad | 24 | ~154596 | ~40ms | ~250ms |
| beer_cup.scad | 7 | ~102520 | ~100ms | ~160ms |

## 已知限制与后续方向

1. **`difference(union)` 透明度**：正侧已用 Manifold union 合并为单实体，重叠二次着色问题已解决。但若正侧含多种颜色，结果统一取第一个颜色（与 OpenSCAD 行为一致）。
2. **`resize` 为 no-op**：融合求值下无法在不重排管线的前提下正确按 bbox 缩放；当前透传 + warn。
3. **`offset` / `projection` 未实现**：示例未用；offset 可接已装好的 clipper2，projection 需切片。
4. **`minkowski` 近似为 union**：真闵可夫斯基需凸分解。
5. **`import`（STL/DXF/SVG）/ `surface` 未实现**。
6. **rotate_extrude 部分扫掠端盖**为扇形（凸轮廓正确）；凹轮廓端盖未走 earcut。
7. **变量动态作用域**：非严格 OpenSCAD 词法作用域；`include` 顶层语句位置丢失（追加到 main 后，示例全用 use 不受影响）。
8. **无 per-instance 节点**：一种颜色 = 一个大 Model，编辑器无法单独选中部件；如需可改"顶层模块实例 + 颜色"分组。
9. **无磁盘缓存**：每次重新解析（SCAD 解析很快，~40-100ms，暂不需要）。

## 调试技巧

- 日志前缀 `SCAD:`，`grep "SCAD"` 过滤；`ECHO:` 为 `echo()` 输出。
- 启动：`--load-scene "assets/scad/<file>.scad"`；`--agent-validation` 自动截图。
- 单测：`gkNextUnitTests "[Scad]"`（lexer/parser/evaluator/几何/CSG/text/loader 全覆盖）。
- warning 数 > 0 时检查降级特性（unknown module / text 缺后端 / difference 无后端）。
- 几何朝向/黑面：检查法线平滑阈值与 polyhedron face winding（OpenSCAD 约定 clockwise-from-outside，加载器已 reverse）。
