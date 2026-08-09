---
title: "Bindless 3D 纹理支持（前置开发计划）"
category: plan
status: 已完成
owner: engine/rendering
created: 2026-07-29
last_updated: 2026-07-29
---

> **已完成（2026-07-29）**。P0/P1/P2 全部交付并验收通过。
> 现行事实见 [渲染运行时架构 · Bindless 资源维度](../designs/rendering-runtime-architecture.md#bindless-资源维度)；
> 本文件保留为实施记录。

# Bindless 3D 纹理支持（前置计划）

> 本计划是 [大气散射与高度雾开发计划](atmosphere-and-height-fog-plan.md) 的**前置依赖**：
> 大气透视需要一张 32×32×32 的 froxel volume，而当前 bindless 体系只能承载 2D 图像。
> 本计划独立可交付、独立可验收，不含任何大气逻辑。
>
> 完成后同时惠及：体积雾 froxel、3D LUT（色彩分级 / LUT-based tonemap）、voxel 场可视化。

## 现状核实

改动前的事实（均已在代码中核实，行号为撰写时快照，实施前用 `rg` 复核）：

| 位置 | 现状 | 是否需改 |
|---|---|---|
| `Engine/Vulkan/GpuResources.cpp:70` | `imageInfo.imageType = VK_IMAGE_TYPE_2D` 硬编码，`extent.depth = 1` | **需改** |
| `Engine/Vulkan/GpuResources.cpp:292` | `createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D` 硬编码 | **需改** |
| `Engine/Vulkan/GpuResources.hpp:53` | `Image::extent_` 是 `const VkExtent2D` | **需改** |
| `Engine/Vulkan/GpuResources.cpp:481` | `RenderImage` 构造只走 2D 路径 | **需改** |
| `assets/shaders/common/BindlessTexture.slang:88-101` | 只有 `.as<Sampler2D>()` / `.as<RWTexture2D<T>>()` | **需改** |
| `Assets/GPU/Texture.cpp:209` `BindSampleTexture` | 只接受 `ImageView&` + `Sampler&`，**维度无关** | 不用改 |
| `Assets/GPU/Texture.cpp:225` `BindStorageTexture` | 只接受 `ImageView&`，**维度无关** | 不用改 |
| `Assets/GPU/Texture.cpp:174-176` 描述符布局 | `COMBINED_IMAGE_SAMPLER` / `STORAGE_IMAGE` 数组，**类型不编码维度** | 不用改 |
| `PipelineCommon/ResourceStateTracker.hpp` | `FImageUse::range` 是通用 `VkImageSubresourceRange`，3D 图像 `layerCount=1` 天然成立 | 不用改 |
| `MemoryAndShader.hpp:69` `SamplerConfig::AddressModeW` | 已存在，默认 `CLAMP_TO_EDGE` | 不用改，但需专用配置 |

**结论**：Vulkan 侧描述符不编码图像维度，写入一个 3D image view 到既有 bindless 数组是合法的。
唯一的真实不确定性在 Slang 端 —— `__DynamicResource.as<RWTexture3D<T>>()` / `.as<Sampler3D>()`
是否能正确生成 SPIR-V。**P0 阶段的唯一目的就是提前证实或证伪这一点。**

## 里程碑总览

| 阶段 | 一句话 | 依赖 |
|---|---|---|
| P0 | 探针：证实 slangc 能从 `__DynamicResource` 转出 3D 资源类型 | — |
| P1 | `Image` / `ImageView` / `RenderImage` 支持 3D | P0 通过 |
| P2 | Slang 访问器 + 运行时正确性测试 + 文档收口 | P1 |

**P0 不通过则整个计划终止**，改由大气计划走 2D tile atlas 退化路径（见末节）。

构建口径：全部改动在 Engine 层与 shader，用 `./gnb.sh build gkNextRenderer gkNextUnitTests`。
新增 shader 文件的那一次需 `--reconfigure`。

---

## P0 — 可行性探针

**目标：在写任何生产代码之前，用最小成本确定 slangc 是否支持。**

### 任务

1. **编译期探针 shader**
   新建 `assets/shaders/Util.Bindless3DCompileTest.comp.slang`，内容仅需：
   - 从 `StorageTextureArray[NonUniformResourceIndex(i)].as<RWTexture3D<float4>>()` 写一个 texel；
   - 从 `SampleTextureArray[NonUniformResourceIndex(j)].as<Sampler3D>()` 做一次 `SampleLevel(float3 uvw, 0)`；
   - 调一次 `GetDimensions(w, h, d)`。
2. **纳入测试 shader 门控**
   在 `assets/cmake/SlangShaders.cmake` 的 `shader_test_files` 列表里加入该文件
   （与既有 `Util.SharcCompileTest.comp.slang` 同法，受 `GK_BUILD_SHADER_TESTS` 控制，
   默认不进正式构建）。
3. **编译并检查产物**
   ```bash
   cmake --preset windows -DGK_BUILD_SHADER_TESTS=ON
   ```
   然后构建。确认 slangc 无错误，并用 `spirv-dis` 检查 `.spv`：
   同一 `(set=0, binding=0)` / `(set=0, binding=1)` 上应出现 `Dim2D` 与 `Dim3D` 两种
   `OpTypeImage` 变量别名，且带 `NonUniformEXT` 修饰。
4. **记录 slangc 版本**
   `SlangShaders.cmake` 已经在配置期打印版本，把实测版本号记进本文档的"探针结论"小节。

### 验收

- `GK_BUILD_SHADER_TESTS=ON` 时探针 shader 编译通过，`spirv-val` 无错。
- 本文档补上"探针结论"小节：slangc 版本 + 通过/不通过 + 若不通过的完整报错。
- 常规构建（`GK_BUILD_SHADER_TESTS=OFF`）不受影响。

### 探针结论 — **通过**

- **slangc 版本**：`2026.1-52-gc8ddf20bb`（`external/VulkanSDK/1.4.341.1/Bin/slangc.exe`）
- **结果**：`__DynamicResource.as<RWTexture3D<T>>()` 与 `.as<Sampler3D>()` 均正确降级为 SPIR-V，
  `spirv-val` 零错误。`assets/shaders/Util.Bindless3DCompileTest.comp.slang` 反汇编中出现四个
  `OpTypeImage` 别名，正是预期形态：

  ```
  OpTypeImage %float 3D 2 0 0 2 Unknown   // binding 1, RWTexture3D
  OpTypeImage %float 3D 2 0 0 1 Unknown   // binding 0, Sampler3D
  OpTypeImage %float 2D 2 0 0 2 Unknown   // binding 1, RWTexture2D（同一 binding 的 2D 别名）
  OpTypeImage %float 2D 2 0 0 1 Unknown   // binding 0, Sampler2D
  ```

  变量全部带 `NonUniform` 修饰，capability 仅需既有的
  `RuntimeDescriptorArray` / `ShaderNonUniform` / `ImageQuery` /
  `StorageImageRead|WriteWithoutFormat`，**无新增 capability 或扩展**。
- **`GetDimensions(w, h, d)`** 在 3D 上正常工作。
- 常规构建不受影响：`GK_BUILD_SHADER_TESTS=OFF` 时探针不进 ninja 构建图（已核对 `build.ninja`）。

结论：计划继续，AP volume 用真 3D 图像，2D tile atlas 退化路径作废。

---

## P1 — 核心 3D 图像支持

### 任务

1. **`Vulkan::Image` 支持 3D**
   - `extent_` 由 `const VkExtent2D` 改为 `const VkExtent3D`，新增 `imageType_` 成员。
   - 新增构造重载：`Image(const Device&, VkExtent3D extent, VkImageType type, uint32_t miplevel, VkFormat, VkImageTiling, VkImageUsageFlags, bool useForExternal = false)`。
   - 现有两个 2D 构造保持签名不变，内部转发（`{w, h, 1}` + `VK_IMAGE_TYPE_2D`），**调用方零改动**。
   - `Extent()` 保持返回 `VkExtent2D` 语义不变；新增 `Extent3D()` 与 `ImageType()`。
   - `TransitionImageLayout` / `CopyFrom` 的 subresource range 对 3D 无需改动（3D 图像 `layerCount` 恒为 1）。
2. **`Vulkan::ImageView` 支持 3D**
   新增 `VkImageViewType viewType` 参数，默认 `VK_IMAGE_VIEW_TYPE_2D`，放在 `miplevel` 之后、
   `components` 之前需注意默认参数顺序 —— 建议放到参数列表末尾以免破坏既有调用点。
3. **`Vulkan::RenderImage` 支持 3D**
   - 新增构造重载接受 `VkExtent3D` + `VkImageType`，内部按类型选 `VkImageViewType`。
   - 新增可选的 `SamplerConfig` 参数（默认沿用现有 `SamplerConfig()`），
     供 3D LUT 传入专用配置：`AnisotropyEnable = false`、三轴全 `CLAMP_TO_EDGE`、
     `MagFilter/MinFilter = LINEAR`、`MipmapMode = NEAREST`、`MaxLod = 0`。
     ⚠️ 现有默认 `AnisotropyEnable = true` 对 3D LUT 是错的，会让边界采样跑偏。
   - `InsertBarrier` 无需改动。
4. **`VulkanBaseRenderer::CreateStorageImage3D`**
   新增 `CreateStorageImage3D(uint32_t bindlessIdx, VkExtent3D extent, VkFormat, VkImageTiling, VkImageUsageFlags, const char* debugName)`，
   复用既有 `bindless_.images`（`vector<unique_ptr<RenderImage>>`）的生命周期管理与
   `BindStorageTexture` 绑定路径。
5. **格式能力检查**
   创建前用 `vkGetPhysicalDeviceImageFormatProperties(VK_IMAGE_TYPE_3D, ...)` 校验
   `STORAGE_IMAGE_BIT | SAMPLED_IMAGE_BIT`，失败时明确 `Throw` 并打印格式与用途，
   **不允许静默降级**（符合 `AGENTS.md` 的"no silent failures in init/resource loading paths"）。
6. **Bindless 槽位登记**
   3D LUT 需要固定的高位 sample/storage 槽。当前高位槽已被占用：
   `AssetThumbnailRenderer` 的 63200（mesh，512 槽）、64000（material，512 槽）、64600（preview）。
   建议为 volume 资源保留 `62900..62931`（32 槽），并在
   `assets/shaders/common/BindlessTexture.slang` 的槽位注释区增加一段集中登记表，
   把上述所有已占用区间一并写清楚 —— 目前这些常量散落在各自的 header 里，容易撞车。

   > 实际做得更彻底：登记表不只是注释，而是所有 owner 引用的常量推导链，
   > 并顺带压缩了整个地址空间。见文末"追加：槽位重叠修复与 bindless 瘦身"。

### 验收

- `./gnb.sh build gkNextRenderer gkNextUnitTests --reconfigure` 通过。
- **既有全部 2D 调用点未改动一行**（`git diff` 中不应出现 `RenderImage(` 调用侧的改动）。
- `gnb shot --scene assets/models/playground.glb` 与改动前逐位一致。
- Vulkan validation layer 全程无 warning/error（用 debug 构建跑一次 `gnb shot`）。

---

## P2 — Slang 访问器与运行时验证

### 任务

1. **`common/BindlessTexture.slang` 新增访问器**
   ```slang
   public Sampler3D GetSampleTexture3D(int index);
   public RWTexture3D<T> GetStorageTexture3D<T : ITexelElement>(int index);
   public uint3 GetStorageTexture3DDimensions<T : ITexelElement>(int index);
   ```
   与既有 2D 版本同结构，同样带 `NonUniformResourceIndex`。
   **不要**给 3D 加 `ViewRT()` 重映射 —— volume 资源是全局的，不属于任何 RenderView bank，
   这与既有"全局槽位不得经 ViewRT 重映射"的规则一致。
2. **运行时正确性测试** `src/Tests/Test_Bindless3DTexture.cpp`（基于 `EngineTestFixture`）：
   - 创建 8×8×8 `VK_FORMAT_R32_SFLOAT` storage 3D image，绑到保留槽。
   - compute pass A：写入 `value = x + y*8 + z*64`。
   - compute pass B：用 `Sampler3D` 分别在 (a) 若干 voxel 中心、(b) 两个相邻 voxel 的正中点采样，
     结果写进 host-visible buffer。
   - 断言：(a) 精确等于写入值；(b) 等于两邻值的算术平均 —— 这同时验证了
     **写路径、读路径、三线性过滤、以及 W 轴寻址**四件事。
   - 标签 `[Unit][Bindless3D]`。
3. **探针 shader 处置**
   P0 的 `Util.Bindless3DCompileTest.comp.slang` 保留在 `shader_test_files` 中作为回归护栏，
   不要删除（与 `Util.SharcCompileTest.comp.slang` 同定位）。
4. **文档收口**
   - 在 `docs/designs/rendering-runtime-architecture.md` 的"资源状态规则"之后补一小节
     "Bindless 资源维度"，写清：描述符不编码维度、3D 视图可写入既有数组、volume 槽位不经 ViewRT。
   - 更新 [大气设计文档](../designs/atmosphere-and-height-fog-design.md) 的"已知限制"，
     删除 3D bindless 的不确定性条目，改为陈述已支持。
   - 本计划状态改为完成并从 `docs/README.md` 现行面移除。

### 验收

- `./out/build/windows/bin/gkNextUnitTests "[Unit][Bindless3D]"` 通过。
- 既有全部单测绿。
- `gkNextVisualTest` 与改动前一致（本计划不改变任何现有画面）。

---

## 对大气计划的影响

本计划完成后：

- 大气计划 **M0 的任务 7（3D bindless 可行性验证）删除**，改为"依赖本前置计划已完成"。
- 大气计划 **M2 的 AP volume 直接用真 3D 图像**，不再保留 2D tile atlas 分支。
- 大气设计文档"已知限制"中的 3D bindless 条目删除。

若 **P0 证伪**（slangc 不支持），本计划终止，并且：

- 在本文档"探针结论"里记录完整报错与所用 slangc 版本。
- 大气计划 M2 改走 **32 × 1024 的 2D tile atlas**：32 个深度片沿 V 轴排布，
  采样端手动做片间 lerp。该退化不影响任何对外契约，只增加约 10 行采样代码。
- 大气设计文档"已知限制"中该条目改为陈述结论与退化形态。

## 实施记录与偏差（2026-07-29）

实际交付与计划一致，有三处经权衡的偏差，都朝更安全的方向：

1. **volume 资源不放进 `bindless_.images`**（偏离 P1 任务 4 的字面要求）。
   核实后发现 `DeleteSwapChain()` 会遍历 `bindless_.images` 逐个 `reset()`，
   而 `CreateRenderImages()` 又把它 `resize(RT_COUNT)` 缩回 128。volume 存在里面会在每次窗口
   resize 时被销毁且无人重建，留下悬挂的 bindless 描述符。改为独立的
   `bindless_.volumeImages`（`unordered_map<slot, unique_ptr<RenderImage>>`），
   在 `End()` 与析构中先于 device 释放。这也顺带避免了把 vector 撑到 62901 个元素。
   对外 API 仍是计划要求的 `CreateStorageImage3D(bindlessIdx, extent, ...)`。
2. **`RenderImage` 3D 构造不带 `external` 参数**。外部内存共享是 2D 输出/编码路径的需求，
   volume 用不上，加进来只是无法验证的死参数。
3. **运行时测试用两个真 shader 而非一个**。`Sampler3D` 描述符声明
   `SHADER_READ_ONLY_OPTIMAL`、storage 描述符声明 `GENERAL`，单 shader 双分支会让其中一个
   描述符在 dispatch 时 layout 不匹配。拆成 `Util.Bindless3DWrite` / `Util.Bindless3DSample`
   两个 pass 加一次 layout 转换，既 validation 干净，也正好是 M2 的 AP volume 要走的形态。
   这两个 shader 进常规构建，因此 3D bindless 的编译回归由默认构建直接兜住，
   P0 探针（`GK_BUILD_SHADER_TESTS` 门控）作为额外的 2D/3D 别名护栏保留。

验收实测：

- `gnb.bat build gkNextRenderer gkNextUnitTests --reconfigure` 通过。
- `gkNextUnitTests "[Unit][Bindless3D]"` 通过（13 assertions）；全量 241 test cases /
  54518 assertions 全绿。
- 临时给 `EngineTestFixture` 加 `--validation` 跑该用例：**零 validation error**
  （唯一一条 PERFORMANCE warning 来自既有 graphics pipeline 的 vs/fs interface，与本改动无关）。
  验证后已还原 `TestCommon.cpp`。
- `gnb shot --scene assets/models/playground.glb` 画面正常；2D 路径逐行核对为等价改写
  （2D 构造转发后 `imageInfo.extent` 仍是 `{w, h, 1}`，`ImageView` 的 `viewType` 默认
  `VK_IMAGE_VIEW_TYPE_2D`，`Extent()` 语义不变），无调用方改动。

## 顺带发现（不属本计划范围）

`Engine/Vulkan/GpuResources.cpp:105` 的 `Image::Image(Image&& other)` 移动构造
**漏拷了 `mipLevel_` 与 `external_`**：

```cpp
Image::Image(Image&& other) noexcept :
    device_(other.device_), extent_(other.extent_), format_(other.format_),
    imageLayout_(other.imageLayout_), image_(other.image_)   // mipLevel_ / external_ 未初始化
```

移动后的对象调用 `TransitionImageLayout` 会把未初始化的 `mipLevel_` 写进
`subresourceRange.levelCount`。当前是否有真实触发路径未核实（`Image` 的移动构造调用点需另行排查）。
P1 会改动这个构造函数，届时应顺手补全这两个成员的拷贝，但**根因排查与影响面评估另开任务**。

**已处置（2026-07-29）**：P1 改动该构造函数时补齐了 `mipLevel_` / `external_` / `imageType_` 的拷贝。
同时核实：全仓**没有任何 `Vulkan::Image` 的移动构造调用点**（`std::move` 只作用于
`TextureImage` 等持有者，不作用于 `Image` 本身），所以此前没有真实触发路径，属预防性修复。

## 追加：槽位重叠修复与 bindless 瘦身（2026-07-29）

建立槽位登记表时发现一处**既有**重叠：`AssetThumbnailRenderer::kMaterialThumbnailSampleSlotBase`
占 64000..64511（512 槽），而 `RemoteServer.cpp` 的 `remoteCompositeBindlessBase = 64500`
占 64500..64502。两者在 64500..64502 撞车，编辑器与 Remote Play 同进程时描述符互相覆盖。

根因是**每个 owner 自建 base**。修复不是挪一个数字，而是把整个地址空间收进登记表：

- 高位区间改成**推导链**（`RES_VIEW_SAMPLE_BASE` → `RES_REMOTE_COMPOSITE_BASE` →
  `RES_REMOTE_ENCODE_BASE` → `RES_VOLUME_BASE` → `RES_MATERIAL_PREVIEW` →
  `RES_MESH_THUMBNAIL_BASE` → `RES_MATERIAL_THUMBNAIL_BASE`），手改单个 base 不可能再造成重叠。
- `AssetThumbnailRenderer` / `RemoteServer` / `OffscreenRenderViewController` / `FBankAllocator`
  全部改为引用登记表常量，仓内已无 bindless 魔法数字。
- C++ 侧对整个分区加 `static_assert`；新增 `[Unit][Bindless]` 运行时用例校验分区并在每个区间首尾槽位
  实打实写描述符（validation layer 下可捕获越界）。

顺带的瘦身（用户口径：场景贴图上限取 16384）：

| | 改动前 | 改动后 |
|---|---|---|
| 每数组容量 | 65535 | 17481 |
| 描述符总数 | 131086 | 34978（**-73%**） |
| volume 预留 | 32 槽 | 8 槽 |
| 场景贴图超限行为 | 静默踩坏 63200 起的高位描述符 | `Throw` 并提示要调哪个常量 |

两个数组本来就按声明数**全量**分配——`Texture.cpp` 里那条
"DescriptorSetLayout puts VARIABLE_DESCRIPTOR_COUNT_BIT on the last binding" 的注释早已失效，
代码里没有该机制。注释一并更正。

## 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| slangc 不支持 `.as<RWTexture3D>` / `.as<Sampler3D>` | 整个计划终止 | P0 用最小探针提前证伪，成本 < 半天 |
| `Image::extent_` 改类型波及面超预期 | P1 工期膨胀 | 保留 `Extent()` 的 2D 语义，2D 构造签名不变，调用方零改动 |
| 高位 bindless 槽位与缩略图系统撞车 | 运行时描述符被覆盖，症状诡异 | P1 任务 6 建立集中槽位登记表 |
| 3D 采样器沿用默认各向异性配置 | LUT 边界采样偏移，大气出接缝 | P1 任务 3 显式要求专用 SamplerConfig |
| 3D storage image 的格式支持因驱动而异 | 少数设备创建失败 | P1 任务 5 显式能力检查 + 明确报错 |
