# SCAD 资产生成实战手册（Kit → 场景 → 地形开放地图）

本文是一次完整、顺畅的生成流程复盘：kit_coldwar（112 模块）→ 8 个主题场景 → riverland_1km
开放地图，全程零返工大坑。适用于任何"为新题材造 SCAD 组件库 + 场景 + 地图"的任务。
语言面以 [SCADLoader.md](SCADLoader.md) 为准，地形以 [ScadTerrain.md](ScadTerrain.md) 为准；
本文只讲**流程与经验**，与上述两文冲突时以它们为准。

## 0. 开工前侦查（先读后写，10 分钟）

1. 读 `AGENT_GUIDE/SCADLoader.md`；要做地形地图再读 `AGENT_GUIDE/ScadTerrain.md`。
2. **打开最近一个 kit 当格式模板**（`assets/scad/lib/kit_coldwar.scad` / `kit_deadly.scad`），
   不要自创格式。头注释、配色段、PRNG、工具模块、类别分段的组织方式照抄。
3. 看 `assets/scad/lib/catalog.json` 里任一 kit 的模块 schema：category 由模块名第二段
   （`cw_bldg_barn` → `bldg`）推导，footprint/zMin/triangles 由工具自动求值。
4. 场景组织参考对应 showcase（`assets/scad/coldwar_showcase.scad`）与
   `assets/scad/coldwar/*.scad`；布局组合子见 `lib/kit_layout.scad`，贴地组合子见 `lib/kit_terrain.scad`。

## 1. 组件库（kit）编写规范

### 结构契约

- 纯零件库：只含 `module`/`function`，**无顶层几何**；文件放 `assets/scad/lib/kit_<theme>.scad`。
- 命名空间前缀（2 字母 + 下划线，如 `cw_`）全库唯一；模块名第二段就是 catalog 类别：
  `ground` / `bldg` / `nature` / `prop` / `veh` / `wpn` / `item`（可按题材增减，保持这套词汇）。
- 常量一律**零参 function**（`function cw_ASPH() = [0.29, 0.29, 0.31];`）——`use` 语义不传播
  顶层赋值，变量形式在调用方一律 undef。
- 确定性伪随机**必须含平方项**（线性同余的组合仍是线性，连续 seed 会出等差伪影）：

```scad
function cw_sq(x) = (x * x + x * 601 + 37) % 65521;
function cw_rnd(s, m) = cw_sq(cw_sq(((s % 65521) + 65521) % 65521) + 11) % m;
```

- 每个 kit 换一组加法常数，避免跨 kit 相同 seed 输出相同序列。

### 放置契约（全库统一，场景侧才能盲拼）

| 类别 | 契约 |
|---|---|
| 所有落地件 | 底面 z=0 |
| 带朝向件（门面/取货面/开口） | front = -y |
| 载具 | 车头朝 +x，底面 z=0 |
| 武器 loot | 平躺地面、枪口朝 +x、厚度 z 0.05~0.08，**顶视 profile 可读**（俯视角游戏里远景不糊） |
| 桥 | 沿 x 跨越，锚点 = 引桥端路面高度，桥墩向下延伸没入河床（zMin 为负是预期） |

### 配色（PT 管线特性决定）

- 路径追踪强日光下 albedo 会整体提亮：**0.5 就是白色**，深色必须压到 0.1~0.3。
- 全库配色集中在文件头一段零参 function，низ饱和、偏深；变体色用调色板 function
  （`function cw_car_c(i) = [[...],[...],...][cw_rnd(i, 6)];` —— 列表字面量直接下标没问题，
  但**不要对函数调用结果直接下标**，先赋给局部变量再 `c[0]`）。
- 半透明可用：alpha 0.45~0.5 的薄板（0.02 厚）渲染成"网面"，铁丝网/购物车筐/电话亭玻璃
  用它效果好且不爆噪。alpha < 0.99 走透明 Dielectric 路径。

### 几何技巧与反例

- 坡屋顶用验证过的 polyhedron 模板（`cw_part_roof`：双坡/四坡/攒尖一个模块搞定），别手搓面序。
- 拱顶（机库/卡车篷布）= `scale + rotate + cylinder` 再 `difference` 切掉地下半。
- `difference` 只准出现在零件内部（女儿墙、轮胎、钢盔、井圈）；布局层一律 union。
- **交叉杆件（拒马类）反例**：以原点为中心 rotate 建模会有一半埋进地下，必须整体
  `translate([0,0,h])` 抬到交点离地高度。写完在心里过一遍"最低点在哪"。
- 复用 `boxc`/`slab` 两个工具模块（居中 cube / 底面 z=0 平板），全库几何 90% 由它们拼出。
- 单模块三角预算：小道具 <300，载具 <1100，大建筑 <2500（catalog 会算，超了就减窗格数）。

## 2. 验证闭环（每步都截图，别攒到最后）

```bash
gnb scad catalog                                   # 1. 求值全库：0 bad / 0 warning 才继续
cp assets/scad/lib/kit_X.scad out/build/<preset>/assets/scad/lib/   # 2. 手动镜像 build assets（必须！）
cp assets/scad/<scene>.scad     out/build/<preset>/assets/scad/...
gnb shot --scene assets/scad/xxx.scad              # 3. 截图肉眼验收（读 agent_validation.jpg）
```

- catalog 除了语法，还要扫一眼 JSON 里的 `footprint`/`zMin`/`triangles`：0 三角 = 空几何，
  footprint 异常大 = 有件飞了，zMin 异常负 = 穿地（桥除外）。
- **顺序**：kit → showcase → loot 特写 → 各场景。问题在最小上下文里暴露。
- 小物件（武器/物资）必须建一个专门的**特写场景**（十几件摆在 13×8 台上）验收；
  在全景 showcase 里它们只有几个像素，看不出问题。本次拒马半埋就是特写抓出来的。
- 1km 大图 `gnb shot` 相机拉得很远，用 Python PIL 裁剪放大关键区域检查
  （桥、据点入口、道路衔接）：`img.crop(box).resize(4x).save(...)` 后再看图。

## 3. showcase 与主题场景

- **showcase 先行**：按类别排行陈列全部模块（参考 `coldwar_showcase.scad` 的行分段注释），
  它同时是验收场、选型目录和回归基准。
- 场景基底：**绿色系 [0.36, 0.41, 0.24] 级别**。灰度 > 0.38 的基底在 PT 日光下整体发白
  （本次超市/医院两个场景都因此返工）。工业场景可用灰但要铺大块 `ground_dirt/grass` 打散。
- 场景尺寸 90×80 左右、20~35k 三角是舒适区；`$fn = 12` 场景级设置一次。
- 布局先在脑内画格子（北排大建筑/中部道路/南排民房…）再写坐标，避免穿插；
  front=-y 的件要面向北侧道路时 `rotate([0, 0, 180])`。
- 植被/杂物用 `lay_scatter + lay_pick` 撒，房屋等结构件手摆坐标——散布交给组合子，
  结构不要交给随机。
- 叙事细节让场景"活"：弃车长龙、洗劫翻倒的货架、检疫封锁沙袋、幸存者篝火+铺盖卷。
  每场景 1~2 个叙事焦点即可。

## 4. 地形开放地图（1km 级）

### 地形参数

- **cells ≤ 176×176**：>180² 单 Model 索引超限，引擎跳过 MeshShape，玩家没法走。
  1000m / 176 ≈ 5.7m 网格，低模风格够用。
- TERR features **按序作用**：山/脊/台地 → 湖 → 河 → 路 → pad（最后压平）。
- 基础起伏 `[0, 2.6, 0.5]` 量级；山高 40~55、脊 24、台地 12 在 1km 图上比例舒服。

### 路网与桥（血泪契约，照抄别改）

- 主路折线**直接穿过河**：路算子对填方 >0.9 的深沟留空，桥沟自动出现。
- 路-河交点要**手算**（两段线求交），桥摆在交点上、按路段方向旋转。
- 桥长 ≥ 2.5×河宽（河岸下切带宽 = 2.2×半河宽，引桥必须落在带外）；
  锚点高度取**下切带外的路面点**：`translate([cx, cy, gk_terrain_height(TERR, ax, ay)])`，
  (ax,ay) 离河中心线 ≥ 1.3×河宽。
- 支路止于 pad 边缘（差 3~5m 的土缝可接受，不要伸进 pad 深处）。

### 据点（POI）

- **每个需要平地的据点给一块 pad**，尺寸按内容定（村庄 46×32、军事基地 90×64、
  哨点 16×12）；pad 会把地表压平并染成 pad 生物群系（视觉即"夯土场院"）。
- 据点内容整体用 `ter_place(TERR, cx, cy) { ...局部坐标... }` 包裹：pad 是平的，
  子件全部用局部坐标，和平地场景写法完全一致，可以整段搬运已验收的场景片段。
- 内部街道/停机坪等平板 ground 件**只能放在 pad 上**；pad 外的坡地上放平板会悬空/穿地。
- 生存玩法按风险/回报梯度布点：村庄（低危食物）→ 加油站/工厂（工具油料）→
  小镇（医疗）→ 桥头咽喉（弹药+交火点）→ 军事基地/坠机点（高危军火）。
  制高点（通信站）给狙击视野，河与桥制造地形咽喉。

### 植被与公路道具

- `ter_scatter` 按生物群系过滤：树只长缓坡草地
  `[0.5, 40, 26, 3, ["grass", "grass_dark"]]`（海拔带 / 坡度≤26° / 避水 3m）；
  岩块只在 `["rock", "rock_high"]`；枯草带放灌木枯树。
- **密度基准：1km² 约 700~900 棵**（首版 320 棵太稀，一眼荒芜）。树要放大：
  s = 1.5~2.9（8~17m 高），showcase 用的 s=1 在大地图上是灌木。
- 河谷两岸单独补一层疏林（渡河接近路线的掩体），北坡补密林带。
- 电力杆用 `ter_along` 沿路撒：step 85、横向 offset 5.5（半路宽 + 1.5，杆基不悬空）。
- 弃车直接 `ter_place` 在路折线顶点上（顶点必在压平路面上），按路段方向 ±20° 旋转。

### 预算

地形 62k + 植被 ~800 件 × 90 ≈ 70k + 8 据点 ~100k ≈ **350k 三角**，加载 <1s，渲染无压力。
`ter_scatter` 候选是 4n 次地形查询，千级实例无感。

## 5. 交付前清单

- [ ] `gnb scad catalog`：新 kit 全模块 ok、0 warning；抽查 footprint/zMin/triangles
- [ ] kit + 全部场景已 cp 到 `out/build/<preset>/assets/scad/`（两处不同步 = 白改）
- [ ] showcase、loot 特写、每个场景、大地图各一张 `gnb shot`，肉眼过：
      比例 / 朝向（门面向路）/ 穿插 / 悬空 / 发白 / 黑面
- [ ] 大地图：树密度、桥两端衔接、支路-pad 衔接、据点梯度，PIL 裁剪放大复核
- [ ] 若 kit 供 compose/generate 管线使用：模块默认参数必须能出几何（catalog 会标记）
