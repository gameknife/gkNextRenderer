# SCAD 资产目录约定

`assets/scad` 按场景的可编辑结构分类。目录类别既是资产所有权，也是 ScadLibrary 选择编辑器
和 AI adapter 的依据：

- `lib/kit_*.scad`：只放可复用模块库，不放完整场景。
- `evaluated/**/*.scad`：确定的 Kit 实例列表；支持对象选择、Gizmo 和定向对象 AI。
- `source/**/*.scad`：保留循环、条件、变量和局部 module 的 SCAD 程序；只使用源码编辑和源码 AI。
- `proc/**/*.scad`：带 `gk_terrain(TERR)` 与 `ter_*` 规则的过程场景；使用过程节点编辑和过程 AI。
- `characters/*.scad`：角色 Rig 与角色资产，不出现在场景组装浏览器中。
- `specs/*.json`：声明式场景规格。
- `source/generated/` 与 `proc/generated/`：由 `gnb scad compose` 生成的分类派生产物；重新
  生成时会覆盖手工改动。

## ScadLibrary 场景组装

场景列表只扫描 `evaluated / source / proc`，并显示类别标签：

1. `source` 不再在打开时隐式求值为对象列表。若需要对象级编辑，必须点击“转换为 Evaluated
   副本”；转换会把当前求值结果显式写到 `evaluated/<原名>_evaluated.scad`，不会覆盖源码。
2. `evaluated` 的平铺场景可无损往返对象列表并允许直接保存。AI snapshot 会携带当前选中实例；
   精确选择语义仅在这一类场景启用。
3. 在“对象”页点选实例后，视口会在该对象原点显示移动/旋转 Gizmo。拖动期间实时更新
   运行时 Scene Node 和 SCAD Z-up 坐标；松开时只写回 `translate` / `rotate`，不重新解析
   或上传整场景。平铺场景直接写回。
   也可以直接点击视口中的几何；Picking 命中渲染子节点后会沿父链找到所属 Kit 实例，
   同步右侧对象列表的高亮和滚动位置，再进入同一套 Gizmo 编辑流程。
4. “导出场景”默认写到 `assets/scad/evaluated/`，并始终使用相对 Kit 路径。
5. 每类编辑器只能写回自己的目录：对象到 `evaluated`、源码到 `source`、过程到 `proc`。

可编辑副本格式刻意保持简单：一个求值后的 Kit module 实例占一行，变换顺序固定为
`translate(...) rotate(...) scale(...) module(...);`。循环、条件、局部 module 和布局组合器
会在副本中展开为确定实例；需要继续维护其生成逻辑时，应编辑原文件的“源码”页。

## Terrain 过程编辑

含顶层 `gk_terrain(TERR)` 的场景会自动进入“过程”页，不再把求值结果展开成数百个普通对象：

1. “地形画布”编辑 size、cells、seed、基底起伏、粗糙度、水位和 palette。
2. “Terrain Features”按实际求值顺序编辑、增删和上下移动
   `mountain/ridge/plateau/lake/river/road/pad`；ridge/river/road 的折点可逐点编辑。
3. 中央视口同步显示 feature 的地表轮廓：圆形 feature 的半径、山峰/台地的高度标尺、
   ridge/river/road 的中心折线和宽度边界，以及 pad 占地框。点击/拖动圆点可选择并移动
   中心或折点；选中圆形 feature 后可拖动半径手柄。竖直手柄可编辑山峰/台地/山脊高度和
   湖泊/河流深度，横向手柄可编辑 ridge/river/road 宽度。
4. “贴地过程规则”结构化编辑 `ter_place`、`ter_place_tilt`、`ter_snap`、`ter_along` 和
   `ter_scatter`；桥梁常用的
   `translate([x,y,gk_terrain_height(TERR,sampleX,sampleY)+dz])` 也会显示为独立高度锚点。
   组合子的 child 保持为一段 SCAD，可继续写模块参数、`rotate` 链、`lay_pick` 或代码块。视口
   用绿色方形手柄显示规则落点/折点/区域角点；只有当前选中的规则会展开 `ter_along` 实例点或
   `ter_scatter` 过滤后的确定性点集，未选中规则只保留编辑轮廓和手柄。scatter 推荐圆形
   `[cx,cy,r]` 区域，可直接拖圆心和半径，并通过竖直 `count` 手柄调整数量；旧 AABB
   `[x0,y0,x1,y1]` 可在面板中保留或切换。
   第一次点击未选中手柄只负责选择；再次按住已选中手柄拖动才会改参数。右侧会自动滚动并只展开
   当前选中的 Feature 或规则。按住 Shift 拖动已选中项会复制完整 Feature/规则，并继续拖动
   新副本；单击或未超过拖动阈值不会误复制。
   视口中右键拖动始终旋转相机；未拖动手柄时，左键拖动用于平移相机。滚轮缩放速度按场景
   bounds 自适应，因此小零件与公里级地形都能保持合适的导航速度。
5. 在左侧 Kit 浏览器点模块的“+”，会向当前过程场景追加一条以该模块为 child 的
   `ter_place`。
6. 自动刷新只生成工作区预览；“保存”才写回源文件。写回只替换 TERR 赋值和已识别的顶层
   `ter_*` 语句，桥梁高度表达式、注释和其他自由 SCAD 原样保留。过程预览重建会保留当前
   相机位置、方向和缩放，便于连续编辑控制点。

过程参数必须是数值、布尔、字符串或数组字面量。使用变量/函数计算参数的 `ter_*` 语句会留在
源码中并显示提示，不会被错误地结构化。需要编辑这类表达式时使用“源码”页。

命令行可以直接打开过程场景，适合快速验证：

```bash
gnb run ScadLibrary --scene assets/scad/proc/terrain_layout_demo.scad
gnb validate --script assets/agentscripts/scadlibrary-terrain-process.agentscript.json
```
