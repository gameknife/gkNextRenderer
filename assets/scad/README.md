# SCAD 资产目录约定

`assets/scad` 的目录是**归档方式**，不是场景的类型。

一个 `.scad` 根文件可以同时拥有三种节点：确定的 Kit 实例、`gk_terrain(TERR)` 地形与
`ter_*` 过程规则、以及保留循环/条件/局部 module 的源码结构。ScadLibrary 打开文件时**逐条
顶层语句分类**，每类节点由对应编辑器负责，写回时只替换被改动的那几条语句，其余字节
（注释、格式、不支持的语法）原样保留。因此不再需要把整个文件"转换"成某一类。

- `lib/kit_*.scad`：只放可复用模块库，不放完整场景。
- `lib/gk_camera.scad`：相机机位虚拟点标记库（`gk_camera` / `gk_camera_key` 零几何 marker），
  场景 `use` 后即可内嵌定点/路径机位；不进 catalog。约定见 `docs/AGENT_GUIDE/SCADLoader.md`。
- `evaluated/**/*.scad`：以确定 Kit 实例为主的场景。
- `source/**/*.scad`：以 SCAD 程序（循环、条件、变量、局部 module）为主的场景。
- `proc/**/*.scad`：以 `gk_terrain(TERR)` 与 `ter_*` 规则为主的场景。
- `characters/*.scad`：角色 Rig 与角色资产，不出现在场景组装浏览器中。
- `specs/*.json`：声明式场景规格。
- **`gnb geo` 生成的真实城市 tile 不在这个目录下**，在 `assets/geo/<tile>/`：一个 tile 的
  `.scad`、`terrain.hmap`、`poi.json`、`ATTRIBUTION.md` 同放一处，整体 gitignore 并由
  `gnb geo pak` 打进 `assets/paks/geo.pak`。
- `source/generated/` 与 `proc/generated/`：由 `gnb scad compose` 生成的派生产物；重新
  生成时会覆盖手工改动。

上面三个场景目录只表达"这个文件主要是什么"，**不限制它能有什么**：`source/` 下的程序可以
直接手放实例、加地形和散布规则，`evaluated/` 下的实例列表也可以加一段循环。新增场景时按
主要内容挑一个目录即可。

### 场景组装浏览器的分类

ScadLibrary 的"场景"资源库按类别（目录名）分组，条目上标注**组成**而不是类型：

- `source/<类别>/` 与 `proc/<类别>/` 会合并到同一个类别下。例如
  `source/coldwar/` 和 `proc/coldwar/` 都显示在 `coldwar`。
- 条目图标表示它归档在哪个目录；后面的徽标表示它实际含有什么（含地形 / 含程序结构）。
- `source/brotato3d/`、`proc/nexttotalwar/` 等游戏/展示目录会显示为对应的游戏分组；
  `deadly_showcase.scad` 归入 `Brotato3D`，`tw_showcase.scad` 归入 `NextTotalwar`。
- 新增特殊游戏或 Showcase 场景时，优先在 `assets/scad/source/<类别>/` 或
  `assets/scad/proc/<类别>/` 下放置文件。只要类别目录名称相同，Source/Proc 就会自动合并，
  不需要额外维护注册表。
- 具体项目场景也按项目目录归档：机场放在 `source/airport/`，办公室放在 `source/office/`，
  Overhill 任务放在 `source/overhill/`，海港城市放在 `source/habor_city/`；对应的 kit 展示场景
  使用根目录的 `airport_showcase.scad`、`office_showcase.scad`、`overhill_showcase.scad` 和
  `habor_city_showcase.scad`。
- Racing 的 `pit_lane.scad` 与 `kit_pitlane` 展示统一位于 `source/racing/`；Old City 的
  `old_city.scad` 与 `kit_old_city` 展示统一位于 `source/oldcity/`。

## ScadLibrary 场景组装

打开任意场景后，右侧检视面板按内容给出编辑页：

1. **对象**：文件里所有被识别为实例的顶层语句，形如
   `[color(...)] translate(...) rotate(...) scale(...) kit_module(args);`。变换必须是字面量
   且按 `color → translate → rotate → scale` 顺序出现，写回才能逐字复原；不满足的语句留在
   "源码"类，不会被对象编辑器改写。支持列表编辑、视口 Gizmo 和定向对象 AI。
2. **过程**：文件含 `gk_terrain(TERR)` 时出现。编辑地形画布、Terrain Features 和贴地规则。
3. **结构**：文件的**全部**顶层节点列表，标注每个节点是实例、地形、过程规则还是源码，
   并提供逐节点操作（见下）。
4. **源码**：完整 SCAD 文本。改动后在预览、保存或点"立即解析"时重新解析，其他页随之刷新。

保存写回**打开的那个文件**，不再按编辑器限制目标目录。只有被改动的语句会重写：打开再保存
一个没动过的场景是字节级 no-op。

### 逐节点关闭与展开（取代整体转换）

在"结构"页选中一个源码调用语句后：

- **关闭**：在语句前加 OpenSCAD 的 `*` 修饰符。求值器本来就跳过 `*` 语句，所以这是原地
  停用，不删代码，随时可"启用"。赋值语句和 `module`/`function` 定义不能带修饰符，面板会
  拒绝并说明。
- **关闭并展开为实例**：求值整个文件（结构通常依赖它前面的变量和 module，不能单独求值），
  取出这一条语句产生的 Kit 实例，写进同一个文件紧跟其后，并把原语句关闭。其余源码结构
  不受影响。保存前可以"撤销展开"。

这就是"把 source 里的一个结构关掉、换成可编辑实例"的做法，不需要像以前那样整文件转成
`evaluated/` 副本。展开后的实例是普通语句，重新打开时仍然是实例节点。

### Terrain 过程编辑

含 `gk_terrain(TERR)` 的场景会出现"过程"页：

1. "地形画布"编辑 size、cells、seed、基底起伏、粗糙度、水位和 palette。
2. "Terrain Features"按实际求值顺序编辑、增删和上下移动
   `mountain/ridge/plateau/lake/river/road/pad`；ridge/river/road 的折点可逐点编辑。
3. 中央视口同步显示 feature 的地表轮廓：圆形 feature 的半径、山峰/台地的高度标尺、
   ridge/river/road 的中心折线和宽度边界，以及 pad 占地框。点击/拖动圆点可选择并移动
   中心或折点；选中圆形 feature 后可拖动半径手柄。竖直手柄可编辑山峰/台地/山脊高度和
   湖泊/河流深度，横向手柄可编辑 ridge/river/road 宽度。
4. "贴地过程规则"结构化编辑 `ter_place`、`ter_place_tilt`、`ter_snap`、`ter_along` 和
   `ter_scatter`；桥梁常用的
   `translate([x,y,gk_terrain_height(TERR,sampleX,sampleY)+dz])` 也会显示为独立高度锚点。
   组合子的 child 保持为一段 SCAD，可继续写模块参数、`rotate` 链、`lay_pick` 或代码块。视口
   用绿色方形手柄显示规则落点/折点/区域角点；只有当前选中的规则会展开 `ter_along` 实例点或
   `ter_scatter` 过滤后的确定性点集。scatter 推荐圆形 `[cx,cy,r]` 区域，可直接拖圆心和半径，
   并通过竖直 `count` 手柄调整数量；旧 AABB `[x0,y0,x1,y1]` 可在面板中保留或切换。
   第一次点击未选中手柄只负责选择；再次按住已选中手柄拖动才会改参数。按住 Shift 拖动已选中项
   会复制完整 Feature/规则，并继续拖动新副本。
   视口中右键拖动始终旋转相机；未拖动手柄时，左键拖动用于平移相机。滚轮缩放速度按场景
   bounds 自适应。
5. 在左侧 Kit 浏览器点模块的"+"：当前在"过程"页时追加一条以该模块为 child 的 `ter_place`，
   否则追加一个普通实例。
6. 自动刷新只生成工作区预览；"保存"才写回源文件。写回只替换被改过的 TERR 赋值和 `ter_*`
   语句，桥梁高度表达式、注释和其他自由 SCAD 原样保留。过程预览重建会保留当前相机位置。

过程参数必须是数值、布尔、字符串或数组字面量。使用变量/函数计算参数的 `ter_*` 语句会留在
源码中并显示提示，不会被错误地结构化。需要编辑这类表达式时使用"源码"页。

命令行可以直接打开场景，适合快速验证：

```bash
gnb run ScadLibrary --scene assets/scad/proc/terrain_layout_demo.scad
gnb validate --script assets/agentscripts/scadlibrary-terrain-process.agentscript.json
gnb validate --script assets/agentscripts/scadlibrary-mixed-document.agentscript.json
```
