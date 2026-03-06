# LDraw 文件加载器

本文档记录 LDraw 数字 LEGO 标准加载功能的实现细节、架构决策和已知问题，便于后续改进。

## 功能概览

加载 `.ldr` 模型文件，递归解析零件引用、生成几何体和材质，渲染完整的 LEGO 模型。

- **支持格式**: `.ldr`, `.mpd`
- **资源依赖**: `assets/ldraw/`（LDraw Library）, `assets/omr/`（测试模型）
- **验证模型**: `assets/omr/102.ldr`（micro cart，43 零件，27 唯一模型）

## 文件结构

```
src/Assets/Loaders/
├── FLDrawConfig.h/.cpp    # 颜色表解析 + 文件路径解析器
├── FLDrawParser.h/.cpp    # 递归 .dat/.ldr 解析器 + BFC 状态机
└── FLDrawLoader.h/.cpp    # 场景组装（材质/Model/Node 创建）
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
    Loader 按颜色分组 → Vertex(MaterialIndex) + 索引
                    ↓
         构建 Model（局部坐标，Y-flip + scale 已烘焙）
                    ↓
    每个放置 → Node（变换 = F * T_ldraw * F 共轭 + scale）
```

### 坐标系转换

| 属性 | LDraw | 引擎 | 转换方式 |
|------|-------|------|----------|
| Y 轴 | 向下为正 | 向上为正 | 顶点 `y *= -1` |
| 单位 | LDU（≈0.4mm） | 米 | `× 0.001` |
| 面朝向 | CCW（BFC 默认） | CCW | Y 翻转后 swap winding 补偿 |

**Node 变换推导:**
- 模型顶点 = `F * v_local * scale`（Y-flip + 缩放烘焙到几何中）
- Node 变换 = `F * T_ldraw * F`（Y-flip 共轭，平移也乘以 scale）
- 最终位置 = `NodeTransform * ModelVertex = scale * F * T_ldraw * v_local`

其中 `F = diag(1, -1, 1, 1)`，glm column-major 下的具体计算:
```cpp
tfm[0][1] = -ldrawMat[0][1];  // col 0, row 1
tfm[1][0] = -ldrawMat[1][0];  // col 1, row 0
tfm[1][2] = -ldrawMat[1][2];  // col 1, row 2
tfm[2][1] = -ldrawMat[2][1];  // col 2, row 1
tfm[3][1] = -ldrawMat[3][1] * kLDrawScale;  // 平移 Y 取反 + 缩放
```

### 材质映射

- 颜色表从 `LDConfig.ldr` 解析（322 种颜色）
- 每种颜色 → 一个 `FMaterial`，按 Finish 类型分配材质模型:
  - **Solid/Rubber**: `Material::Lambertian(linearColor)`
  - **Chrome**: `Material::Metallic(linearColor, 0.01f)`
  - **Pearlescent**: `Material::Metallic(linearColor, 0.15f)`
  - **Transparent** (alpha < 1): `Material::Dielectric(1.5f, 0.01f)` + `Diffuse.a = alpha`
- 颜色继承: LDraw 颜色 16 = 继承父级颜色 → `kLDrawColorInherit (-1)`
- Section 机制: 同一零件中不同颜色的面用不同 `MaterialIndex`，Node 的材质数组映射到实际材质

### 多材质 Section 排序

使用 `std::map<int, ...>`（有序映射）确保 section 颜色排序确定性。`kLDrawColorInherit = -1` 自然排在最前，对应 MaterialIndex 0。这一点很关键——之前用 `std::unordered_map` 导致 Model 构建和 Node 材质数组的排序不一致。

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
4. **无平滑法线**: 所有面使用 flat shading。圆柱形零件（如 stud）会有明显的棱角。
5. **322 个材质全部创建**: 即使场景只用了 5 种颜色，也会创建所有 322 种材质。
6. **线段（type 2/5）被跳过**: 光追渲染不需要边线，但某些零件可能缺少视觉细节。

### 推荐的改进优先级

**P0 - 正确性:**
- [ ] `.mpd` 文件支持（`0 FILE` / `0 NOFILE` 指令），实现多模型内联
- [ ] 顶层 .ldr 文件中的 type-3/type-4 直接几何支持

**P1 - 性能:**
- [ ] 磁盘二进制缓存（见原计划中的 `.cache/` 方案）
- [ ] 延迟材质创建：只创建场景实际使用的颜色对应的材质
- [ ] 文件索引缓存到磁盘（避免每次扫描 44000+ 文件）

**P2 - 视觉质量:**
- [ ] 平滑法线：对 stud/cylinder 等圆柱形原语使用角度阈值的法线平滑
- [ ] 更精确的材质：Glitter/Speckle 材质目前退化为 Lambertian
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
