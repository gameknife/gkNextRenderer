# LDraw 文件加载器

本文记录当前 LDraw 导入实现。实现已经从 Engine 核心迁到可选模块 `src/Modules/LDrawLoader/`；不要再从旧的 `src/Engine/Assets/Loaders/FLDraw*` 路径寻找代码，也不要沿用旧文档中“MPD、直接几何和磁盘缓存尚未实现”的结论。

## 接入边界

- `LDrawModule.cpp` 通过 `Assets::FLoaderRegistry` 注册 `.ldr` / `.mpd`，应用必须链接 `LDrawLoader` 并调用 `Modules::LDraw::Register()`。
- 当前调用者包括 `gkNextRenderer`、`gkNextEditor`、`gkNextVisualTest`、两个 benchmark 和 `BrickPlayer`；Engine 核心不依赖该模块。
- 标准零件库由可选包 `assets/paks/ldraw.pak` 提供，使用 `./gnb.sh paks fetch ldraw` 获取。只含内嵌直接几何的 MPD 可以用 `LDrawLoadOptions::useLibraryPak = false` 加载。
- SceneList 仍扫描运行时 `assets/omr/` 中的 `.ldr` 场景；这些示例可能来自 optional pak，不应假定 source tree 一定存在。

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `LDrawModule.*` | loader 注册和 Engine 用户设置接线 |
| `FLDrawConfig.*` | `LDConfig.ldr`、真实色彩覆盖、pak 挂载和大小写无关文件索引 |
| `FLDrawParser.*` | type 1/3/4、BFC、MPD 内嵌文件、内存/磁盘模板缓存 |
| `FLDrawGeometry.*` | section 分组、坐标转换、三角网格和 45° 面积加权平滑法线 |
| `FLDrawLoader.*` | MPD 层级、STEP 元数据、材质/Model/Node 创建、相机初始化 |
| `FLDrawConnectivity.*` | BrickPlayer 使用的连接点数据解析 |

## 当前数据流

1. `LDrawModule::Register` 把扩展名交给 loader registry。
2. Loader 挂载 `ldraw.pak`，解析颜色表并建立标准库索引。
3. MPD 的 `0 FILE` / `0 NOFILE` 被拆成内嵌文件；非 `.dat` 子模型保留 Node 父子层级。
4. Parser 递归处理 type-1 引用，解析 type-3 三角形和 type-4 四边形，并应用 `BFC CERTIFY/CW/CCW/INVERTNEXT/CLIP/NOCLIP` 与镜像变换。
5. Geometry 按颜色建立最多 16 个 material section，生成硬边/平滑法线网格；每个放置创建一个 Node。
6. Loader 记录 Node 对应的 build step 和 part filename，供 BrickPlayer 回放/编辑使用，然后用 `AutoFocusCamera` 生成默认相机。

线段 type 2 和 conditional line type 5 仍被跳过。Loader 不会自动添加展示地板；需要地板的产品应在自己的场景或游戏层创建。

## 坐标与尺度

LDraw 到 Engine 的基变换是 `F = diag(-1, -1, 1)`：几何顶点先翻转 X/Y，Node 变换使用 `F * T * F`，平移也乘 `lduToWorldScale`。不要恢复旧文档中的“只翻 Y”公式。

尺度有两个容易混淆的默认值：

- 正常 Engine 路径通过 `sys.ldrawLduToWorldScale` 绑定用户设置，当前注册默认值为 `0.02`。
- 直接调用低层 API、未传 options 或传入非法值时，`defaultLDrawLduToWorldScale` 回退为 `0.001`。

测试自定义尺度时显式填写 `LDrawLoadOptions::lduToWorldScale`，不要依赖调用环境恰好加载了哪个用户配置。

## 材质、缓存与确定性

- 颜色 16 继承父颜色；颜色 24 目前也映射到 inherited section。
- section 使用有序颜色分组，保证 `Vertex::MaterialIndex` 与 Node material array 顺序一致。
- `LDConfig.ldr` 颜色会叠加内建 realistic overrides，并按 Solid、Transparent、Chrome、Pearlescent、Rubber、Matte Metallic、Glitter、Speckle 构建近似 PBR 材质。
- 当前仍为整张颜色表创建材质，并为每个放置创建 Node；没有按实际用色延迟创建，也没有 per-part GPU instancing。
- 文件系统零件模板会写入 CookHelper 管理的 `ldpart` / `ldpartd` 压缩缓存，缓存包含依赖文件签名并在依赖变化时失效。pak 内条目没有文件系统签名，只使用进程内模板缓存。

## 验证

修改模块后至少运行：

```bash
./gnb.sh build gkNextRenderer gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[Unit][LDraw]"
./out/build/<preset>/bin/gkNextUnitTests "[Unit][LDrawConn]"
```

主要回归文件是 `src/Tests/Test_LDrawParser.cpp`、`Test_LDrawGeometry.cpp`、`Test_LDrawLoader.cpp`、`Test_LDrawConfig.cpp` 和 `Test_LDrawConnectivity.cpp`。有可用 OMR 场景时，再用 `./gnb.sh shot --scene <scene.ldr>` 做视觉检查。

调试日志统一使用 `LDraw:` 前缀。零件缺失先检查 `assets/paks/ldraw.pak` 是否已获取和挂载；朝向错误检查 BFC 与 `F * T * F`；材质错位检查 sectionColors 与 material array 的同序关系。
