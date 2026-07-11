---
title: "SCAD Scene Compose：基于既有 module 库的大规模场景组合系统"
category: design
status: M0–M3 已落地（kit 拆分 / ScadLibrary / catalog / 布局组合子 / spec+compose），M4 LLM 待做
owner: engine
created: 2026-07-11
last_updated: 2026-07-11
---

# SCAD Scene Compose：基于既有 module 库的大规模场景组合系统

> 目标：把 `assets/scad/` 下已积累的 module（habor_city_hd / old_city / office / airport …）
> 变成可复用的**零件库（Kit）**，并提供一套**按需组合规则**，能低成本产出新的大规模场景
> （海港城、古城、办公室、机场，以及它们的混合体）。
>
> 前置：SCAD loader（`AGENT_GUIDE/SCADLoader.md`）、SCAD Studio（`docs/designs/scad-model-generator-design.md`，
> 其 v1 out-of-scope 中的「多模型同场景拼装」即本文范围）。

---

## 1. 现状盘点

### 1.1 已有资产（assets/scad/）

| 文件 | 规模 | 内容 | 角色 |
|------|------|------|------|
| `habor_city_hd.scad` | 52 KB | 人尺度细节零件库，**无总装** | 事实上的 Kit（唯一） |
| `habor_city_v2.scad` | 33 KB | `use <habor_city_hd>` + 16 种功能街区 + `V2_LAYOUT` 类型矩阵 + 总装循环 | 事实上的手写"场景组合" |
| `old_city.scad` | 52 KB | 古城：`part_/prop_/nature_/wall_/bldg_` 模块 + 总装 | 库和总装混在一个文件 |
| `office.scad` | 38 KB | 办公室：`furn_/prop_/wall_` + 功能点位锚点 | 同上 |
| `airport.scad` | 68 KB | 机场：`furn_/prop_/veh_/ground_` | 同上 |

### 1.2 已经自发形成的约定（这是本方案的地基）

- **命名前缀分类**：`part_`（构件）/ `prop_`（道具）/ `furn_`（家具）/ `bldg_`（建筑）/
  `nature_`（植被地景）/ `veh_`（载具）/ `wall_`（墙体系统）/ `ground_`（地面）/ `v2_block_*`（功能街区）。
- **放置契约**：绝大多数模块遵循「**底面 z=0，front = -y**」，参数含脚印 `L/D`、随机 `seed`。
- **街区瓦片**：v2 的功能街区统一 48×42 脚印，靠 `V2_LAYOUT` 二维类型矩阵 + 双层 for 循环总装。
- **seed 驱动变化**：同一 block/module 用 seed 产生确定性变体。

**结论：habor_city_v2 已经是一个手写的「Kit + 布局矩阵 + 总装」组合实例。本方案要做的是把这个模式
显式化、规范化、跨主题化，并在其上加一层可被工具/LLM 驱动的规格层。**

### 1.3 必须先解决的障碍（调研核实）

1. **跨文件重名冲突（29 个）**：`furn_monitor`、`furn_task_chair`、`veh_car`、`nature_tree`、
   `prop_clock`、`ground_base` … 在 office/airport/old_city/habor_city_hd 间重复定义。
   同时 `use` 两个文件时后者覆盖前者（或行为未定义），**跨主题组合直接不可行**。
2. **环境常量不随 `use` 传播**：loader 的 `use` 只合并 module/function 定义表，顶层赋值不引入
   （与 OpenSCAD 一致）。所以 v2 必须"在调用侧重申模块所需全局常量"（GZT/ROADC/WHITEC…约 50 个）。
   Kit 依赖的环境目前是**隐式契约**，组合器无从得知某个 kit 需要哪些常量。
3. **尺度类别混杂**：office/airport 是室内人尺度精细件（0.02~5 单位），habor_city 是城市尺度
   低模（街区 48×42），old_city 居中。无标注地混用会出「城市街区里出现一比一显示器」的错误。
4. **loader 无「入口 module」选项**：`ScadLoadOptions` 只有 scale/floor/递归深度/法线角，
   加载即求值整个文件顶层。不能"只加载某文件的某个 module"。

---

## 2. 方案空间

用户设想的三个方向逐一评估：

### 方向 A：纯 SCAD 内规则（layout 组合子库 + 手写顶层文件）

把 v2 的布局逻辑抽成通用 scad 库（`grid_place` / `scatter` / `along_row` …），新场景 = 手写一个
顶层 .scad：`use` 若干 kit + 声明布局矩阵 + 调组合子。

- ✅ 零引擎改动、零新工具；SCAD 本身是图灵完备的（loader 已支持 for/if/children/list comprehension）。
- ✅ v2 证明了该模式能撑起 672×508 的城市。
- ❌ 布局仍是代码，非数据：工具/LLM 难以可靠地读写、校验、diff；
- ❌ 不解决重名、环境常量、尺度标注问题——这些是规范问题不是语法问题。

### 方向 B：全新高层 DSL + 引擎内解释器

设计一门 scene-DSL，引擎新写 parser 直接建 Node/Model。

- ✅ 理论上可对接实例化、流式加载。
- ❌ 重复造轮子：SCAD loader 已把「文本 → 层级化场景」整条链路做完（模块树→Node 层级、
  颜色分桶、材质、法线、相机）；新 DSL 要重做一遍并永远维护两套。
- ❌ 失去 CSG 能力与 OpenSCAD 生态兼容（现有 kit 无法直接被新 DSL 求值）。
- ❌ 与 <50k LOC 引擎目标相悖。

### 方向 C：结构化场景规格（数据）→ 生成顶层 .scad（推荐，与 A 分层共存）

**"DSL"不是新语言，而是一份 JSON 场景规格（Scene Spec）**；一个小生成器把 spec 展开成
普通的顶层 .scad（`use` kits + 常量 + 布局调用）。产物走现有 loader，全链路零改动。

- ✅ 数据层（spec）可校验、可 diff、可被 LLM 可靠生成——比让 LLM 直接写 60 KB scad 稳定得多；
- ✅ 产物是普通 .scad：可检视、可手改、可回灌 OpenSCAD 本体验证；
- ✅ 生成器很薄（模板展开），复杂度留在 scad 组合子库（方向 A）里；
- ✅ 天然衔接 ScadStudio / gnb llm：spec 就是对话式场景生成的理想目标格式。

**结论：A + C 分层组合。A 是运行时表达（scad 组合子 + kit），C 是创作/工具层（spec + 生成器）。
B 不做。** 引擎侧实例化/流式属于远期演进（§7），不阻塞本方案。

---

## 3. 总体架构（三层）

```
 L2  Scene Spec（JSON, 数据）           ←  人手写 / ScadStudio 对话 / LLM 生成
      │  gnb scad compose（Go, 薄模板展开 + 校验）
      ▼
 L1  顶层场景 .scad（生成物, 可手改）    =  use <kits> + 环境常量块 + 布局组合子调用
      │  依赖
      ▼
 L0  Kit 零件库（assets/scad/lib/*.scad）+ catalog.json（机器可读零件目录）
      │  现有 SCAD loader（零改动）
      ▼
     引擎场景（Node 层级 / 多 section Model / PathTracing 或 SwModern）
```

---

## 4. L0：Kit 规范与零件目录

### 4.1 Kit 文件规范（`assets/scad/lib/kit_<theme>.scad`）

1. **纯库**：文件顶层只有常量与 `module`/`function` 定义，**不得有顶层几何**（`use` 语义下顶层
   几何本来也不会被引入，但规范上必须写明，避免误 include）。
2. **命名空间前缀**：所有 module 加 kit 短前缀——`hc_`（habor_city）、`oc_`（old_city）、
   `of_`（office）、`ap_`（airport）。分类前缀保留在其后：`oc_prop_lantern`、`ap_veh_airliner`。
   一次性机械重命名，29 个重名冲突就此消除。
3. **自洽环境**：kit 依赖的常量一律改为**函数化默认**（`function oc_roadc() = [0.27,0.28,0.31];`）
   或模块参数默认值；确需环境覆盖的（全局标高、主色调），在 catalog 中显式声明 `env` 列表，
   由 L2 生成器负责在顶层文件输出对应常量块。**禁止隐式依赖调用方全局变量。**
4. **放置契约成文**：底面 z=0、front = -y、参数命名 `L/D/h/seed` 统一；不满足契约的模块
   在 catalog 里标注 anchor 例外。
5. **尺度类别**：每个 kit 声明 `scaleClass`: `city`（街区级低模）/ `human`（人尺度精细）/
   `mid`（介于两者，如 old_city 建筑）。

改造结果（2026-07-11，`tools/scadkit` 机械变换 + 逐字节指标回归）：
- `lib/kit_city_hd.scad`（hc_，91 modules）← habor_city_hd.scad（原文件删除）
- `lib/kit_city_blocks.scad`（保留 v2_ 前缀，37 modules，内部 `use <kit_city_hd.scad>`）← habor_city_v2 的街区库
- `lib/kit_old_city.scad`（oc_，54）/ `lib/kit_office.scad`（of_，41）/ `lib/kit_airport.scad`（ap_，70）
- 原场景文件保留为「参考总装」：`use` 自家 kit + 玩法锚点 module（`*_NN`，AirportSim/StudioSim
  按节点名查 POI，不可改名）+ 布局常量 + 总装语句。

首个按规范全新创建（非拆分）的 kit（2026-07-11）：
- `lib/kit_deadly.scad`（dd_，38 modules，scaleClass mid）—— Deadly Days: Roadtrip 风格
  美式郊区丧尸末日主题，规划用作 Brotato3D 场景元素。分类：`bldg`（木板房/门廊房/工具棚/
  石教堂/街角店）、`ground`（沥青路段/十字口/人行道/草地块）、`nature`（层叠松/阔叶树/灌木/
  草簇/玉米田）、`prop`（白栅栏/路灯/电线杆/公路绿牌+倒塌残骸/消防栓/信箱/垃圾桶/油桶/
  板条箱/大垃圾箱/锥桶/长椅/路障）、`veh`（轿车/厢式车/皮卡/烧毁残骸）。
  配色注意：PT 强日光 + tonemap 下 albedo ≈0.5 即近白，深色件基色需压到 0.10–0.30 区间。
  零件总览：`assets/scad/deadly_showcase.scad`；示例场景 spec：`specs/deadly_town.json`
  （十字路口小镇，73 roots / 1615 nodes / 33580 tri / 0 warning）；游戏级整图 spec：
  `specs/deadly_roadtrip_map.json`（170×130：南缘公路残骸带 + 主街商业区/教堂广场 +
  东西住宅 grid（jitter+lay_pick 变体）+ 西南农田 + 环边松林，用满 placements/grids/rows/
  alongs/scatters 全部规则，126 roots / 4290 nodes / 94464 tri / 0 warning）。

### 4.2 catalog.json（机器可读零件目录）

组合器与 LLM 的「零件菜单」。由工具自动生成（见 4.3），人工只补 `tags`/`desc`：

```json
{
  "kits": {
    "old_city": {
      "file": "lib/kit_old_city.scad", "prefix": "oc_", "scaleClass": "mid",
      "env": ["GZT", "WZT"],
      "modules": [
        { "name": "oc_bldg_house",   "cat": "bldg",   "params": {"seed":0,"L":9,"D":6.5},
          "footprint": [9, 6.5], "height": 7.2, "anchor": "base-frontY",
          "tags": ["house","residential","chinese"] },
        { "name": "oc_nature_tree",  "cat": "nature", "params": {"s":1.0,"i":0},
          "footprint": [2.4, 2.4], "height": 4.1, "anchor": "base",
          "tags": ["tree","green"] }
      ]
    }
  }
}
```

### 4.3 catalog 生成：`gnb scad catalog`（已落地）

实现为 console 工具 **`ScadCatalog`**（`src/Application/Util/ScadCatalog/`，Packager 模式，
无 GPU）+ gnb 包装（`tools/gnb/cmd/gnb/scad.go`）：

- 签名/参数/类别：与 ScadLibrary 共用同一扫描器（`Application/Editor/ScadLibrary/KitCatalog.cpp`
  直接编入本工具，单一事实来源）；`paramList` 按顶层逗号切出 name/default 对。
- `footprint/height/zMin/center/triangles/colors`：对每个 module 合成 `$fn=12; name();`，
  经 `LoadScadProgram`（解析 kit 的 use 闭包）+ `ScadEvaluator::Evaluate` 求 bbox——
  293 modules 全量求值秒级；必填参数模块（如 `oc_part_text_cn(label)`）记 `ok:false`（当前 3 个）。
- `gnb scad catalog`：跑工具写 `assets/scad/lib/catalog.json`（源码树），并镜像到
  `out/build/<preset>/assets/`，已构建的二进制无需重编即可读到新 catalog。
- `kit_layout.scad`（组合子规则库）被扫描器显式跳过，不进 catalog / 浏览器。
- 后续可加 `sourceHash`，kit 文件变更后 CI/pre-commit 提示重新生成。

---

## 5. L1：布局组合子库（`lib/kit_layout.scad`）

把 v2 总装里 ad-hoc 的模式抽成通用、带 `children()` 的 scad 组合子（全部在已支持的语法子集内）：

| 组合子 | 语义 | 现有原型 |
|--------|------|----------|
| `lay_grid(cols, rows, cw, ch)` + `$col/$row/$seed` | 网格瓦片放置（children 按格实例化） | v2 总装双层 for |
| `lay_row(n, dx)` / `lay_ring(n, r)` | 线阵 / 环阵 | office 工位排、airport 值机岛 |
| `lay_scatter(seed, n, x0,x1,y0,y1, minGap)` | 区域内确定性散布 | `nature_scatter` |
| `lay_along(pts, step)` | 折线路径撒点（路灯、围墙、车流） | v2 traffic / oc wall_run |
| `lay_block_frame(W, D, pad)` | 街区底板 + 边缘留白 + 四角 | `v2_block_pad`/`edge_props` |
| `lay_pick(seed, i)` + children(i) | 从候选 children 里按权重选一 | `v2_vehicle_pick` |

要点：
- 组合子只做**放置**（transform），不产几何；随机全部经 seed 派生，保证同 spec 同产物
  （可回归、可 baseline 截图对比）。
- 通过 `$col/$row/$idx/$seed/$t` 特殊变量把格位信息传给 children——**已验证**：loader 的
  动态作用域下，for 体内 `$var = ...;` 赋值与 `let($var=...)` 都能穿透 `children()`。
- **CSG 局部化守则**：`difference/intersection` 只允许出现在 kit 模块内部（件级），布局层一律
  union 拼接——避免 Manifold 在场景级大 union 上做布尔（性能悬崖）。

**已落地（`assets/scad/lib/kit_layout.scad`）**：`lay_grid` / `lay_row` / `lay_ring`（face 朝心/朝外/保持）/
`lay_scatter` / `lay_along`（折线撒点，$t 段参数）/ `lay_pick`（候选确定性选一）/ `lay_jitter`，
加 `lay_rand/randf/randi/randr`。demo：`assets/scad/layout_demo.scad`（民居网格+抖动、市场环阵、
沿线路灯、车位选型、树林散布，0 warning）。

**PRNG 教训**：scad 内伪随机**必须含平方项**（`lay_sq(x) = (x*x + x*587 + 41) % 65521`，
x<65521 时 x² < 2^53 double 精确）。任何线性同余的组合仍是线性——连续 seed 会让
`lay_scatter` 的点排成格线、`lay_pick` 选型全同（首版实测踩坑，echo 探针数值确认后修复）。

---

## 6. L2：Scene Spec 与生成器

### 6.1 Spec v1 schema（已落地，`tools/gnb/internal/scadcompose/`）

严格 JSON（无注释，未知字段报错）。字段：`name`（必填）、`fn`（默认 12）、`seed`、
`kits`（短名 `"old_city"` 或全名 `"kit_old_city"`）、`ground`（size/color/z/thickness 单块地板）、
以及任意条数的放置规则（module 调用写成 `"oc_prop_well"` 或 `{"module": "...", "args": "seed = $seed"}`，
children 多于一个自动包 `lay_pick`）：

| 字段 | 展开为 | 说明 |
|------|--------|------|
| `blockTypes` + `blockGrids` | 类型索引矩阵常量 + `<name>_block(t, seed)` 分发 module + `lay_grid` | V2_LAYOUT 模式；layout 矩阵尺寸即网格尺寸 |
| `placements` | `translate/rotate/scale` + 调用 | 显式地标（at/rot/scale/args） |
| `grids` | `lay_grid`（可选 `jitter` → `lay_jitter`） | cols/rows/cell/seed/center |
| `rows` / `rings` | `lay_row` / `lay_ring` | 沿街车位、市场环阵 |
| `scatters` | `lay_scatter` | region [x0,x1,y0,y1] + n + seed |
| `alongs` | `lay_along` | 折线 pts + step + offset |

样例：`assets/scad/specs/mixed_town_spec.json`（placements 复刻手摆版）、
`assets/scad/specs/port_mini.json`（blockGrid 3×2 用 v2 街区生成新迷你城市）。

### 6.2 生成器：`gnb scad compose --spec <x.json> [-o assets/scad/gen/<name>.scad]`（已落地）

薄模板展开，职责严格受限：

1. **校验**（这是 spec 相对手写 scad 的核心价值，全部对着 catalog.json）：kit 存在、module
   在 catalog 且属于已声明 kit（报错并提示补 kit）、layout 矩阵矩形且 blockType 引用闭合、
   scaleClass 混用告警（human×city）、catalog 标记 `ok:false` 的 module 无 args 调用时告警。
2. **展开**：输出顶层 .scad —— 头注释（spec 相对路径 + sha256 前 12 位；**无时间戳**，
   同 spec 字节级确定，git diff 干净）→ `$fn` → `use <../lib/...>`（kit_layout 仅在用到组合子时）→
   地面 → 矩阵常量 + 分发 module → 各规则的组合子调用。
3. **不做**：任何几何/布尔运算、任何随机决策（seed 全部下推给 scad 层）、任何引擎调用。

实现：`tools/gnb/internal/scadcompose/`（spec.go / catalog.go / compose.go + 12 个单测）；
命令写完源码树后自动镜像到 build assets，`gnb shot --scene assets/scad/gen/<name>.scad` 免重建直接验收。

**等价性验收（2026-07-11）**：`mixed_town_spec.json` compose 产物与手摆导出版逐字一致
（11 roots / 224 nodes / 6068 triangles / 0 warning）；`port_mini.json` 40 行 spec 生成
6 类街区 3×2 新城市（1445 nodes / 44388 triangles / 0 warning）。

产物特性：生成的 .scad 是一等资产——可提交、可手改微调（改完与 spec 脱钩，头注释注明）、
可 `gnb shot` 直接验收。

**v1 未覆盖（后续按需）**：`roadWidth` 自动路网（当前用 ground 色块或 kit 路模块摆）、
landmark `clearCells` 占格冲突检测、加权 pick、稀疏 layout（fill+override）、sea/sky 环境。

### 6.3 与 LLM 的衔接（经 ScadLibrary，不复用 ScadStudio）

**决策（2026-07-11）：不在 ScadStudio 上扩展**——它的定位是"agent 生成 scad 源码"。
kit 浏览与组合测试由独立应用 **ScadLibrary** 承载（见 §6.5）。

- catalog.json（或其摘要）注入 prompt context = LLM 的零件菜单；LLM 产出/修改的是 **spec**，
  不是 60 KB scad——小、结构化、可机器校验，失败可回喂（借鉴 ScadStudio M7 的修复回路模式）。
- 远期对话式场景生成挂在 ScadLibrary（或 gnb chat）上：对话 → spec diff → compose → 重载视口。

### 6.5 ScadLibrary 应用（已落地，target `ScadLibrary`）

`src/Application/Editor/ScadLibrary/`（KitCatalog + Interface + Main，复用 ScadStudio 的
三栏骨架与 ProfessionalUI，但独立 App）：

- **左栏 零件库**：扫描 `assets/scad/lib/kit_*.scad`（`KitCatalog::ScanKits`，纯文本签名解析），
  按 kit → 类别（名字第二段：bldg/block/furn/prop/nature/veh/…）→ module 树形展示，支持搜索；
  点击 module → 写 `<cwd>/scad_library/preview.scad`（`use` 绝对路径 + `module();`）→ 视口隔离预览。
- **右栏 组合台**：从浏览器 "+" 添加实例（自动网格散开），每件可调 位置/旋转/缩放/额外参数
  （如 `seed = 3`）；$fn、地板开关；自动刷新（拖拽释放后才重载）；生成 `bench.scad` 渲染。
- **导出**：组合台产物写 `assets/scad/gen/<名>.scad`，`use` 用 `../lib/` 相对路径，
  是可直接 `--load-scene` 的一等资产。首个跨 kit 验证样例：`assets/scad/gen/mixed_town_test.scad`
  （oc 城门楼/民居/牌坊 + hc 咖啡屋/车/树 + of 沙发，0 warning）。
- 视口相机沿用 ModelViewController 轨道相机，OnSceneLoaded 自动取景。

---

## 7. 演进路线（非本期，预留接口）

单文件全量求值在「每格都重新求值几何、无 mesh 去重」下终会到顶（v2 已 ~数十万三角形量级；
16 材质槽/节点上限靠 `__render` 分块兜底）。规模再上一个数量级时的路径：

1. **`ScadLoadOptions::rootModule`**：允许「加载某文件的某个 module 为根」——scad 片段即 prefab。
2. **SceneReferenceComponent 引用 scad**：现有场景引用机制（`gkSceneReference` extras）指向
   `kit_x.scad#module`，同 module 多实例共享 Model（真实例化），配合瓦片化按需加载。
3. **求值缓存**：同 (module, 参数, seed) 的几何 memo 化，先在 loader 内做即可收益。

Spec 层对此透明：同一份 spec，未来可由 compose 选择输出「单 .scad」或「宿主 gltf + scad 引用」。

## 8. 里程碑

| 里程碑 | 内容 | 验收 | 状态 |
|--------|------|------|------|
| **M0 Kit 化** | 五 kit 拆库（city_hd/city_blocks/old_city/office/airport）+ 命名空间前缀 + 常量函数化自洽；原场景文件改 `use` kit，玩法锚点（`*_NN`）保留在场景文件 | 四场景节点/三角形数与拆分前逐字一致（27361/665818、3362/90226、125/32250、413/119612），0 warning；AirportSim 51 POI / StudioSim 15 POI 解析正常 | ✅ 2026-07-11（工具 `tools/scadkit`） |
| **M1 跨 kit 验证 + ScadLibrary** | **ScadLibrary** 应用（kit 浏览/预览/组合台/导出，见 §6.5）；跨 kit 混合场景样例 | `gen/mixed_town_test.scad`（oc+hc+of 混摆）0 warning 出图；重名/env 两障碍已消，尺度混用如预期需 scaleClass 管理 | ✅ 2026-07-11 |
| **M2 catalog** | `ScadCatalog` 工具 + `gnb scad catalog`（签名 + bbox 求值 → catalog.json，含 scaleClass/footprint/paramList） | 293 modules 覆盖（290 ok / 3 必填参数），footprint 抽查正确（oc_bldg_house 10.6×8.1×h5.6）；ScadLibrary 优先读 catalog（日志确认）+ tooltip 尺寸 + 组合台按 footprint 自适应间距 | ✅ 2026-07-11 |
| **M2.5 layout 组合子** | `lib/kit_layout.scad`（lay_grid/row/ring/scatter/along/pick/jitter，§5） | `layout_demo.scad` 街区级 demo 0 warning 出图；$var 穿透 children 探针验证；PRNG 平方项修复格线伪影 | ✅ 2026-07-11 |
| **M3 compose** | spec v1 schema + `gnb scad compose` + catalog 校验器（`tools/gnb/internal/scadcompose/`，12 单测） | mixed_town spec 产物与手摆版逐字等价（11/224/6068）；port_mini 40 行 spec 生成 3×2 新城市 0 warning；scaleClass/未声明 kit/矩阵不齐 校验全部生效 | ✅ 2026-07-11 |
| **M4 LLM 场景生成** | ScadLibrary 对话出 spec / gnb chat 集成 | 对话生成一个新主题场景（如「机场旁的老城区」）| 待做 |

## 9. 风险与开放问题

- **kit 重命名的波及面**：ScadRig / 各游戏若引用了这些 module 名需同步（grep 确认后机械替换）；
  原文件保留为总装外观回归基准，风险可控。
- **颜色分桶爆炸**：多 kit 混用调色板并集变大 → Model section 增多、GPU 上传变慢。缓解：catalog
  记录 palette，compose 校验时报告颜色桶预估；必要时 kit 间共享基础色常量。
- **`$fn` 全局性**：顶层 `$fn=12` 动态作用域全场生效，室内 human 级件可能需要更高细分——
  kit 模块内局部覆盖 `$fn`，catalog 标注。
- **spec 表达力边界**：非网格布局（放射状古城、沿河带状）先靠 landmarks + scatter + 手改产物
  兜底，组合子库按需增补（`lay_ring`/`lay_along` 已列入）；不追求 spec 一次性覆盖所有形态。
- **office/airport 的定位**：室内 kit 在城市尺度场景中只作为「建筑内部」出现（远期 rootModule/
  引用机制的场景），本期它们 kit 化的直接收益是室内新场景（新办公室/新航站楼）的快速组合。
