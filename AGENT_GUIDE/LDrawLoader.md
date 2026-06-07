# LDraw 文件加载器

本文档记录 LDraw 数字 LEGO 标准加载功能的实现细节、架构决策和已知问题，便于后续改进。

## 功能概览

加载 `.ldr` 模型文件，递归解析零件引用、生成几何体和材质，渲染完整的 LEGO 模型。

- **支持格式**: `.ldr`, `.mpd`
- **资源依赖**: `assets/ldraw/`（LDraw Library）, `assets/omr/`（测试模型）
- **验证模型**: `assets/omr/102.ldr`（micro cart，43 零件，27 唯一模型）
- **导入附加内容**: 自动在模型下方补一个白色展示地板，尺寸明显大于模型投影，厚度至少 0.5 米，顶面贴齐模型包围盒最低点；默认相机仍只按 LEGO 模型本体自动对焦；默认开启 sky、关闭 sun light

## 文件结构

```
src/Engine/Assets/Loaders/
├── FLDrawTypes.h             # 共享类型与 LDrawLoadOptions（含 lduToWorldScale）
├── FLDrawConfig.h/.cpp       # 颜色表解析 + 资源/路径解析（含 ldraw.pak 挂载）
├── FLDrawParser.h/.cpp       # 递归 .dat/.ldr 解析器 + BFC 状态机
├── FLDrawGeometry.h/.cpp     # 几何组装（按颜色分组 + 法线平滑）
├── FLDrawConnectivity.h/.cpp # 连接点语义（shadow library / connector 抽象）
└── FLDrawLoader.h/.cpp       # 场景组装（材质/Model/Node 创建）
```

修改的现有文件:
- `Model.hpp`: `friend class FLDrawLoader;`（访问私有构造函数）
- `SceneList.cpp`: `.ldr/.mpd` 扩展名分发 + `assets/omr/` 目录扫描
- `assets/CMakeLists.txt`: `omr` + 条件性 `ldraw` 目录复制

## 架构设计

### 数据流

```
.ldr 文件 → 逐行解析 type-1 引用（颜色+变换+零件名）
                    ↓
         对每个唯一零件调用 Parser
                    ↓
    Parser 递归展开 .dat 文件 → 展平的三角形列表（局部坐标）
                    ↓
    Loader 按颜色分组 + 共享顶点法线平滑 → Vertex(MaterialIndex) + 索引
                    ↓
         构建 Model（局部坐标，Y-flip + lduToWorldScale 已烘焙）
                    ↓
    每个放置 → Node（变换 = F * T_ldraw * F 共轭 + lduToWorldScale）
                    ↓
       计算模型包围盒 → 追加白色地板（不参与默认相机 autofocus）
```

### 坐标系转换

| 属性 | LDraw | 引擎 | 转换方式 |
|------|-------|------|----------|
| Y 轴 | 向下为正 | 向上为正 | 顶点 `y *= -1` |
| 单位 | LDU（≈0.4mm） | 米 | `× lduToWorldScale`，默认 `0.001` |
| 面朝向 | CCW（BFC 默认） | CCW | Y 翻转后 swap winding 补偿 |

**Node 变换推导:**
- 模型顶点 = `F * v_local * lduToWorldScale`（Y-flip + 缩放烘焙到几何中）
- Node 变换 = `F * T_ldraw * F`（Y-flip 共轭，平移也乘以 `lduToWorldScale`）
- 最终位置 = `NodeTransform * ModelVertex = lduToWorldScale * F * T_ldraw * v_local`

其中 `F = diag(1, -1, 1, 1)`，glm column-major 下的具体计算:
```cpp
tfm[0][1] = -ldrawMat[0][1];  // col 0, row 1
tfm[1][0] = -ldrawMat[1][0];  // col 1, row 0
tfm[1][2] = -ldrawMat[1][2];  // col 1, row 2
tfm[2][1] = -ldrawMat[2][1];  // col 2, row 1
tfm[3][1] = -ldrawMat[3][1] * lduToWorldScale;  // 平移 Y 取反 + 缩放
```

### 缩放配置

- LDraw 导入支持 `LDrawLoadOptions::lduToWorldScale`
- 引擎默认通过 CVar `sys.ldrawLduToWorldScale` 提供该值，并持久化到 `assets/configs/cvar_user.json`
- 默认值仍为 `0.001`，旧场景行为保持兼容；调大后可直接与引擎现有世界尺度对齐

### 材质映射

- 颜色表从 `LDConfig.ldr` 解析（322 种颜色）
- 默认使用一层 realistic color 覆盖，将 `ImportLDraw`/LGEO 的实物色值覆盖到 `LDConfig.ldr` 对应颜色
- 每种颜色 → 一个 `FMaterial`，按 Finish 类型分配更接近实物的 PBR 参数:
  - **Solid**: `Mixture(roughness=0.1, metalness=0.0, ior=1.45)`
  - **Transparent**: `Dielectric(roughness=0.05, ior=1.585)` + `Diffuse.a = alpha`
  - **Chrome**: `Metallic(roughness=0.0, ior=2.4)`，颜色轻微提亮
  - **Pearlescent**: `Mixture(roughness=0.2, metalness=0.5, ior=1.6)`
  - **Matte Metallic**: `Mixture(roughness=0.2, metalness=0.8, ior=1.45)`
  - **Rubber**: `Mixture(roughness=0.4, metalness=0.0, ior=1.45)`；半透明时退化为 rough dielectric
  - **Glitter/Speckle**: 解析 `MATERIAL ... VALUE #RRGGBB` 的 secondary color，并用 5% 混合近似 ImportLDraw 的颗粒层
- 颜色继承: LDraw 颜色 16 = 继承父级颜色 → `kLDrawColorInherit (-1)`
- Section 机制: 同一零件中不同颜色的面用不同 `MaterialIndex`，Node 的材质数组映射到实际材质

### 多材质 Section 排序

使用 `std::map<int, ...>`（有序映射）确保 section 颜色排序确定性。`kLDrawColorInherit = -1` 自然排在最前，对应 MaterialIndex 0。这一点很关键——之前用 `std::unordered_map` 导致 Model 构建和 Node 材质数组的排序不一致。

### 法线生成

- 几何构建阶段会按共享顶点位置收集相邻三角形，使用面积加权法线做平滑
- 平滑仅在相邻面法线夹角不超过 45° 时生效，避免把砖块硬边抹平
- 结果是 stud、圆柱、圆角等曲面会得到连续法线，而 90° 折边仍保持硬边

### BFC（Back Face Culling）处理

Parser 维护 `BFCState` 逐文件传递:
- `CERTIFY CCW/CW`: 设定文件的默认 winding
- `INVERTNEXT`: 下一个 type-1 引用翻转 winding
- 负行列式变换（镜像）: 自动翻转 winding
- 子文件从父文件继承 winding 方向（通过 `childBfc.windingCCW`）

### 缓存策略

- **Parser 模板缓存**: `templateCache_[lowercase_filename]` → 零件展平后的面列表（color 16 保留为 `kLDrawColorInherit`）
- **文件索引**: `LDrawFileResolver` 在首次加载时扫描 `parts/`, `p/`, `parts/s/`, `p/48/`, `p/8/` 建立 lowercase→路径 映射
- **Static lazy init**: `colorTable`, `fileResolver` 为 static 变量，首次调用时初始化

## 性能数据（102.ldr, RTX 5070 Ti）

| 阶段 | 耗时 |
|------|------|
| 颜色解析 | <1ms |
| 文件索引（44590 文件） | ~45ms |
| 零件解析（27 唯一零件） | ~400ms |
| GPU 上传 | ~80ms |
| **总计** | **~530ms** |

## 已知限制和后续改进方向

### 当前限制

1. **无磁盘缓存**: 每次启动都重新解析零件文本文件。计划中的二进制缓存（`.cache/<part>.bin`）未实现。
2. **无 per-part instancing**: 每个放置创建独立 Node。相同零件+相同颜色的多个放置理论上可以合并为 GPU instancing。
3. **仅支持顶层 type-1 引用**: `.ldr` 文件中的 type-3/type-4 直接几何被忽略（只处理 type-1 零件引用）。`.mpd` 多模型文件的 `FILE/NOFILE` 指令未处理。
4. **322 个材质全部创建**: 即使场景只用了 5 种颜色，也会创建所有 322 种材质。
5. **线段（type 2/5）被跳过**: 光追渲染不需要边线，但某些零件可能缺少视觉细节。

### 推荐的改进优先级

**P0 - 正确性:**
- [ ] `.mpd` 文件支持（`0 FILE` / `0 NOFILE` 指令），实现多模型内联
- [ ] 顶层 .ldr 文件中的 type-3/type-4 直接几何支持

**P1 - 性能:**
- [ ] 磁盘二进制缓存（见原计划中的 `.cache/` 方案）
- [ ] 延迟材质创建：只创建场景实际使用的颜色对应的材质
- [ ] 文件索引缓存到磁盘（避免每次扫描 44000+ 文件）

**P2 - 视觉质量:**
- [ ] 更精确的材质：Glitter/Speckle 目前仍是无纹理近似，而不是程序化颗粒层
- [ ] 边缘检测/描边效果：模拟 LDraw 的 edge lines

**P3 - 功能扩展:**
- [ ] STEP 指令支持（构建步骤动画）
- [ ] 与 MagicaLego 集成（导入 LDraw 零件库作为方块刷子）
- [ ] 大型模型支持（LOD/流式加载）

## 关键复用点

| 现有功能 | 文件 | 用途 |
|---------|------|------|
| `FSceneLoader::AutoFocusCamera()` | `FSceneLoader.h` | 自动对焦相机到场景包围盒 |
| `Material::Lambertian/Metallic/Dielectric` | `Material.hpp` | 材质工厂方法 |
| `Node::CreateNode()` | `Node.h` | 创建场景节点 |
| `RenderComponent` | `RenderComponent.h` | 模型/材质绑定，材质数组最多 16 个 slot |
| `Model(name, vertices, indices, genTSpace)` | `Model.hpp:155` | 私有构造，需 friend |

## 调试技巧

- 所有日志前缀 `LDraw:`，可用 `grep "LDraw"` 过滤
- 启动参数: `--load-scene "assets/omr/102.ldr"`
- 如果颜色不对，检查 `sectionColors` 排序是否与 `MaterialIndex` 一致
- 如果零件缺失，检查 `LDrawFileResolver::Resolve()` 的 warn 日志
- 如果面朝向错误（黑面），检查 BFC winding + Y-flip winding 补偿
