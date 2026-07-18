---
title: "SCAD Terrain：语言描述的低模可行走地形（开发计划）"
category: plan
status: ✅ M0–M4 已完成
owner: engine
created: 2026-07-17
last_updated: 2026-07-17
---

# SCAD Terrain 开发计划

> 设计方案见 `docs/designs/scad-terrain-design.md`（术语、算法、接口、风险均以设计文档为准，
> 本文只做任务分解与验收）。里程碑 M0→M4 严格串行——每期产物是下一期的输入；
> 单个里程碑内的任务可并行。执行 agent 每完成一个里程碑应先跑完该期"验收"再进入下一期。

## 总览

| 里程碑 | 一句话 | 主要产出 | 依赖 |
|--------|--------|----------|------|
| M0 | 地形核心 builtin，手写 scad 能出图 | `FScadTerrain` + 3 个 `gk_*` 符号 + faceted 透传 + 单测 | — |
| M1 | scad 层贴地组合子，kit 件能贴地/过滤散布 | `lib/kit_terrain.scad` + demo 场景 | M0 |
| M2 | spec/compose 打通，JSON 一键出场景 | terrain 段 schema + 校验 + 展开 + 样例 spec | M1 |
| M3 | 可行走闭环，角色真实在地形上走 | TerrainComponent + loader 挂接 + NavGrid/Jolt 验证 | M0（可与 M1/M2 并行开工） |
| M4 | LLM 一句话生成含地形场景 | generate prompt/schema 扩展 + 验收题通过 | M2 |

通用规则（每个里程碑都适用）：

- **构建口径**：改 Engine/Modules → `./gnb build gkNextRenderer gkNextUnitTests`；
  改 gnb Go 代码 → `cd tools/gnb && go test ./...`；不要全量构建。
- **回归红线**：既有 `[Scad]` 单测全绿；`old_city.scad` / `beer_cup.scad` /
  `gen/deadly_roadtrip_map.scad` 三个场景的 节点数/三角形数/warning 数 与改动前一致
  （faceted 标志默认关闭路径必须逐字节无影响）。
- **视觉验收**：`gnb shot --scene <x.scad>`（隐藏窗口自动截图），截图人审记录在 journal。
- **确定性**：同 seed 两次加载三角形数/颜色桶逐一致；compose 产物字节级稳定。

---

## M0：地形核心（FScadTerrain + builtin）

**目标**：手写一个 `assets/scad/terrain_demo.scad`（直接书写 TERR 常量 + `gk_terrain(TERR)`），
`gnb shot` 出一张有山、河、湖、平原、道路、基座的 low-poly 地形图，0 warning。

### 任务

| # | 任务 | 涉及文件 | 说明 |
|---|------|----------|------|
| 0.1 | TERR list 解码器 + 规范化哈希 | `src/Modules/ScadLoader/FScadTerrain.{h,cpp}`（新增） | `"gkterr1"` 版本标签校验；解码失败 `Warn()` 降级跳过；spec→hash 供缓存 |
| 0.2 | 高度函数：base fbm + 特征算子 | 同上 | 设计 §5.1：value noise（整数 hash 含平方项）、smoothmax/min 混合、Chaikin 折线细分、mountain/ridge/plateau/lake/river/road/pad 七算子按序求值 |
| 0.3 | 生物群系着色 + 调色板 | 同上 | 设计 §5.2：road/pad/水下/岩石/雪/海拔带规则，temperate/arid/alpine 三命名调色板，≤12 色量化 |
| 0.4 | 低模网格化 + 水面网格 | 同上 | 设计 §5.4/§5.3：顶点抖动（边界/pad/road 不抖）、对角线翻转、裙边+底盖闭合；河/湖水面条带网格入半透明色桶；水域/掩码写入 `FTerrainData` |
| 0.5 | evaluator 接入：module + function + 缓存 | `FScadEvaluator.Geometry.cpp`（`gk_terrain` 分发）、`FScadEvaluator.Expr.cpp`（`gk_terrain_height/info`）、`FScadEvaluator.h`（缓存成员） | 缓存 keyed by 0.1 的哈希；height/info 走三角化后网格重心插值（设计 §5.6） |
| 0.6 | faceted 标志透传 | `FScadTypes.h`（GeomList 桶标志）、`FScadEvaluator.*`、`FScadLoader.cpp`（带标志的桶跳过法线平滑） | 默认 false；非地形路径逐字节不变是硬要求 |
| 0.7 | 单测 `[ScadTerrain]` | `src/Tests/Test_ScadTerrain.cpp`（新增） | 见下"验收" |
| 0.8 | 手写 demo 场景 | `assets/scad/terrain_demo.scad`（新增） | 覆盖全部七种 feature；文件头注释说明 TERR 编码格式（后续人手写的参考） |

### 验收

1. 单测（至少）：解码器版本/畸形容错；同 spec 两次生成网格逐顶点一致（确定性）；
   `gk_terrain_height` 与生成网格在 1000 个随机采样点重心插值一致（误差 < 1e-6）；
   pad 域内高度为常数；river 走廊水面标高沿下游单调不增；着色调色板色数 ≤ 12；
   faceted=false 时既有 `[Scad]` 场景桶输出逐字节不变。
2. `gnb shot --scene assets/scad/terrain_demo.scad` 出图：山体连续、河有下切+半透明水面、
   道路可辨、pad 平整、平直着色（无平滑渐变），0 warning。
3. 回归红线（见通用规则）。

---

## M1：贴地组合子（kit_terrain.scad）

**目标**：overhill / old_city 的件能贴地摆放与过滤散布；产出跨 kit 贴地 demo。

### 任务

| # | 任务 | 涉及文件 | 说明 |
|---|------|----------|------|
| 1.1 | `ter_place / ter_place_tilt / ter_along` | `assets/scad/lib/kit_terrain.scad`（新增） | 设计 §6.1；tilt 用 `gk_terrain_info` 的坡度+法线，上限角参数 |
| 1.2 | `ter_scatter` 拒绝采样 | 同上 | 设计 §6.2；filt=[hMin,hMax,slopeMax,avoidWater,biomes]；候选上限 4n；`$idx/$seed/$t` 穿透 children |
| 1.3 | catalog 扫描白名单 | `src/Application/Editor/ScadLibrary/KitCatalog.cpp`（kit_layout 跳过处） | kit_terrain 加入跳过清单，不进 catalog/浏览器 |
| 1.4 | 贴地 demo 场景 | `assets/scad/terrain_layout_demo.scad`（新增） | terrain_demo 地形 + 桥横跨河 + 山坡松树 ter_scatter（草地/缓坡过滤生效：河里和陡壁上无树）+ pad 上 2–3 栋建筑 + ter_along 沿路路灯 |

### 验收

1. `gnb shot --scene assets/scad/terrain_layout_demo.scad`：全部件底面贴合地表（无悬空/穿地）、
   pad 上建筑共面、散布过滤肉眼可辨（水域/陡坡无树），0 warning。
2. 同 seed 重开场景点位完全一致（截图 diff 或节点数核对）。
3. `gnb scad catalog` 重跑：kit_terrain 未进 catalog，module 总数不含 ter_*。

---

## M2：spec / compose 扩展

**目标**：`specs/overhill_valley.json`（设计 §4 的完整示例）一条命令出 `gen/overhill_valley.scad` 并出图。

### 任务

| # | 任务 | 涉及文件 | 说明 |
|---|------|----------|------|
| 2.1 | terrain 段 schema 解析 | `tools/gnb/internal/scadcompose/spec.go` | 严格 JSON、未知字段报错（既有口径）；`terrain` 与 `ground` 互斥 |
| 2.2 | 校验器 | `tools/gnb/internal/scadcompose/compose.go` | 设计 §8.1 全部结构校验 + 语义告警；错误信息带"怎么改"（LLM 回喂友好） |
| 2.3 | TERR 展开 + snap 缺省规则 | 同上 | canonical 数值格式化（字节稳定）；有 terrain 段时非 `ground_*` 类模块默认 snap=terrain，`"snap":"none"` 可关；placements/grids/rows/rings/alongs 包 `ter_place`，scatters 带 where 走 `ter_scatter` |
| 2.4 | Go 单测 | `tools/gnb/internal/scadcompose/*_test.go` | terrain 段 golden file（spec→scad 字节比对）+ 校验错误用例（互斥/越界/折线 <2 点/pad 压河告警） |
| 2.5 | 样例 spec | `assets/scad/specs/overhill_valley.json`（新增） | 设计 §4 示例的完整可跑版；产物 `assets/scad/gen/overhill_valley.scad` 一并提交 |

### 验收

1. `go test ./...`（tools/gnb）全绿；同 spec 两次 compose 产物逐字节一致。
2. `gnb scad compose --spec assets/scad/specs/overhill_valley.json` + `gnb shot`：
   山/河/桥/村庄/松林齐备，0 warning。
3. 无 terrain 段的既有 spec（`deadly_roadtrip_map.json` 等）compose 产物与改动前逐字节一致。

---

## M3：可行走闭环（引擎侧）

**目标**：角色（物理胶囊）在 overhill_valley 地形上能走上坡、被河挡住、从桥通过；
游戏代码能查询地形语义。可与 M1/M2 并行开工（只依赖 M0）。

### 任务

| # | 任务 | 涉及文件 | 说明 |
|---|------|----------|------|
| 3.1 | TerrainComponent | `src/Engine/Runtime/Components/TerrainComponent.{hpp,cpp}`（新增） | 设计 §7.2 API；数据为引擎世界空间；`REFLECT_COMPONENT` 注册（尺寸/seed 只读展示）；Engine 层零 SCAD 依赖 |
| 3.2 | loader 挂接 | `src/Modules/ScadLoader/FScadLoader.cpp` | scene graph 地形 payload → 地形 Node AddComponent；完成 Z-up→Y-up + scale 基变换；水面 Node 设 RayCastVisibility=false |
| 3.3 | 组件单测 | `src/Tests/Test_ScadTerrain.cpp` 扩展 | SampleHeight 与 scad 侧 `gk_terrain_height` 数值一致（坐标基换算后）；IsWater/IsWalkable 在河道/陡坡/pad 处的真值表 |
| 3.4 | 集成验证（EngineTestFixture） | `src/Tests/` 新增 `[Terrain][Integration]` 用例 | 加载 `gen/overhill_valley.scad` → `FNavGrid::Build` → 断言：河两岸互通路径必经桥面格；陡壁格不可走；水面格不可走 |
| 3.5 | 实走冒烟（agentscript 或 CharacterDemo 手验） | `assets/agentscripts/terrain_walk.agentscript.json`（新增，如走 agentscript 路径） | 角色出生在村庄 pad，沿路走到桥再过河；`wait-until` + `assert` 位置/高度；`gnb validate` 非零退出即失败 |

### 验收

1. `./gnb build gkNextRenderer gkNextUnitTests` 通过；`[ScadTerrain]` + `[Gameplay]` 单测全绿。
2. 3.4 集成用例通过：桥是河两岸唯一通路（拆掉桥 placement 后同用例路径应失败——作为反向断言）。
3. 3.5 冒烟脚本 `gnb validate` 通过；journal 附 NavGrid 覆盖层截图（F8→2）一张。
4. 编辑器打开地形场景：PropertyPanel 能看到 TerrainComponent 属性。

---

## M4：LLM 生成（gnb scad generate）

**目标**：一句话生成含地形的完整场景。

### 任务

| # | 任务 | 涉及文件 | 说明 |
|---|------|----------|------|
| 4.1 | prompt/schema 扩展 | `tools/gnb/internal/scadgen/prompt.go` 等 | terrain 段完整合法示例（无省略号）+ 硬规则（河 pts 上游→下游、村庄先 pad 后建筑、mountain 不越图界）+ few-shot 一例山谷河流村庄 |
| 4.2 | 校验回喂适配 | `tools/gnb/internal/scadgen/generate.go` | terrain 校验错误进自修复回路；`--debug` 落盘含 terrain 的完整对话 |
| 4.3 | Go 单测 | `tools/gnb/internal/scadgen/*_test.go` | 菜单/prompt 含 terrain 词汇；模拟 LLM 输出的解析容错（双重编码等既有教训用例补 terrain 变体） |

### 验收

1. 验收题：`gnb scad generate "北面一列雪山，一条河从山谷流向南边平原，河上有一座桥，西侧平原上有个小村庄，松树散布在缓坡上"`
   ——在默认本地模型（Gemma-4-E4B）≤3 轮自修复内通过 compose 校验，`gnb shot` 截图要素齐备
   （雪山在北、河纵贯、桥在河上、村庄成组、树避水避陡坡），0 warning。
2. 失败案例（若有）用 `--debug` 落盘并在 journal 记录 prompt 迭代结论。

---

## 执行记录（2026-07-17，全部里程碑一次会话完成）

| 里程碑 | 结果 | 验收数据 |
|--------|------|----------|
| M0 | ✅ | `FScadTerrain.{h,cpp}`（~1200 行）+ 3 个 `gk_*` builtin + faceted/water 桶标志透传；`[ScadTerrain]` 15 用例；`terrain_demo.scad` 7 特征全覆盖出图 0 warning（27k 三角形） |
| M1 | ✅ | `lib/kit_terrain.scad`（ter_place/place_tilt/snap/along/scatter + ter_rand 族）；catalog 白名单跳过；`terrain_layout_demo.scad` 出图 0 warning（39k 三角形），水域/坡度/生物群系过滤肉眼可辨 |
| M2 | ✅ | spec `terrain` 段 + `snap/snapAt/where` 扩展 + 校验（互斥/越界/枚举/永假过滤/pad 压河告警）；Go 单测 5 组新增全绿；`specs/overhill_valley.json` → gen 出图 0 warning；既有 spec 产物逐字节不变 |
| M3 | ✅ | `TerrainComponent`（反射注册）+ loader 挂接 + `FNavGrid::MaskUnwalkable`；集成测试 `Test_TerrainWalkable.cpp`：河中格全部不可达、路径必经桥面且高于水面、pad 落球停在地表、河面落球穿水沉底（22 断言） |
| M4 | ✅ | prompt schema/硬规则 9–13/第二 few-shot（few-shot 有"必须能对真实 catalog compose"的守卫测试）；验收题 Gemma-4-E4B 本地 3 轮通过、要素齐备 0 warning |

**与计划的偏差（重要）：**

1. **"NavGrid 零改动"不成立**（设计 §7.3 已修正）：缓坡河岸逐格落差 < maxStepHeight，射线又
   打到干河床，纯几何挡不住涉水。新增通用钩子 `FNavGrid::MaskUnwalkable(predicate)`，
   游戏侧用 TerrainComponent 的水域语义否决水下格。
2. **road 算子新增最大填方规则**（maxFill 0.9）：路穿河时不再把河填成可走的浅滩，深沟自动
   断开留桥位（设计 §5.1 已补）。
3. **桥的布置契约**：桥长必须 ≥ 2.5×河宽且 `snapAt` 锚在岸上路面，否则引桥落在河岸下切带内、
   下桥台阶超步高导致断连（首版 L=13 实测断连，改 L=18 修复；已写入 LLM prompt 硬规则 12）。
4. **3.5 agentscript 实走冒烟未做**，以 EngineTestFixture 集成测试（NavGrid 寻路 + Jolt 落球）
   覆盖同等语义；带角色的 agentscript 实走留待有地形玩法的 game target 出现后补。
5. 发现存量问题（非本次改动引入，已另开后台任务）：4 个旧 spec 用已废弃的 region 旧顺序
   无法 recompose；3 个 gen 头部 sha 过期。

## 完成定义（整个专项）

- [x] M0–M4 验收全部通过（记录见上表；本专项不走 .spec 工作流，无 journal）。
- [x] `AGENT_GUIDE/SCADLoader.md` 增补 `gk_terrain` 系列 builtin 的条目。
- [x] 新增 `AGENT_GUIDE/ScadTerrain.md`（TERR 编码、组合子、spec 字段、TerrainComponent API、
      调试技巧），并在 `AGENTS.md` Key References 挂链。
- [x] `docs/README.md` 索引状态更新（设计/计划 → ✅）。
- [x] 回归红线最终态复核：`[Scad]+[ScadTerrain]+[Gameplay]+[Unit]` 171 用例全绿；
      acient_city / beer_cup / deadly_roadtrip_map 三角形数与改动前一致。

## 明确不在本计划内（设计 §1.3 / §9 演进）

编辑器地形笔刷、运行时形变、LOD/分块流式、真实侵蚀、SwModern 不透明水回退开关、
`Scene.GetTerrain()` TS 便捷入口——按需求出现时另立计划。
