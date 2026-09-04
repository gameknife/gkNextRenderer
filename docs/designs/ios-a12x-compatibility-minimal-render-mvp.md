---
title: "iOS A12X Compatibility Minimal Render MVP"
category: design
status: M0–M3 已实施（mini G-buffer + compute Lambert），贴图与 CSM 待实施
owner: engine
created: 2026-08-22
last_updated: 2026-09-04
---

# iOS A12X Compatibility Minimal Render MVP

## 0. 实施现状（2026-09-04）

M0 的骨架已落地，形态与本文 §8 的描述一致，但把"兼容"表达成了两个数据决策点而不是一组条件分支：

- **`Assets::FBindlessProfile`**（`Engine/Assets/GPU/Texture.hpp`）描述 descriptor 数组容量。
  `Full()` 来自 `BindlessTexture.slang` 的槽位注册表；`Compatibility()` 是 8 个 sampled、
  0 storage/shadow/volume。**同一个 struct** 同时驱动 descriptor layout、`RegisterTexture` 的容量
  上限、以及设备探测的阈值——三者错开正是之前越界写 descriptor 的成因。
- **`ERT_Compatibility`**（`Engine/Rendering/Compatibility/`）是一个 contract 全 `None` 的
  logic renderer。`outputs == None` 就是"没有 screen-space 链"的判据：`CreateSwapChain`、
  `RefreshSceneSwapChainResources`、`BeginSceneFrame`、`Render` 各自据此跳过 RT bank、visibility、
  共享 compute pipeline。约束设备上只注册这一个 renderer，其它类型自然走"未注册"分支。
- 触发降级的条件有两个，任一成立即降级：撑不住 bindless descriptor 数组，**或**没有
  `bufferDeviceAddress`（见下）。

能力探测读的是 **update-after-bind** 上限（`VkPhysicalDeviceDescriptorIndexingProperties`），
不是 `VkPhysicalDeviceLimits`：本引擎的 bindless set 全部带
`VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`，规范把这类 binding 排除在基础 per-stage 上限之外。
§2 记录的 A12X "per-stage 96/96/16、descriptor-set 480/480/80" 是**基础上限**，不是真正的判据——
按基础上限判会把每一台 Apple GPU 都降级（M3 Max 报 `maxPerStageDescriptorSamplers = 16`，却能正常
创建 17k descriptor 的完整 layout）。重新按 A12X 的 update-after-bind 上限核对本文结论时要注意这点。

兼容模式照常提交并绘制真实场景（`conf_room.glb` / `playground.glb` 都能 commit）。场景的 CPU/GPU
数据、节点树、UI、输入全部可用，`Scene` 也会依据已注册 renderer 的 requirements 自动把 ambient arena
收缩到 1 个 cascade；不过本 renderer 不会创建完整 RT bank。

### A12X 的第二条硬约束：没有 bufferDeviceAddress

真机验证推翻了本文原先的一个隐含假设。**A12X 不支持 `VK_KHR_buffer_device_address`**，而
`GPUScene` push constant 里每一个字段都是 buffer device address。因此兼容路径不仅不能用 bindless
descriptor 数组，**也完全不能用 GPUScene**。

引擎侧按"能力缺失即降级"处理，收口在三个点，而不是给 ~50 个 `GetDeviceAddress()` 调用点各加判断：

- `Device::SupportsBufferDeviceAddress()` 读的是 `vkCreateDevice` 实际启用的 feature（走 pNext 链）；
- `Buffer` 构造时把 `VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT` 抹掉，`GetDeviceAddress()` 返回 0；
- VMA allocator 不再无条件带 `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`（否则每次分配都会
  带上非法的 `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT`）。

`ProbeBindlessProfile` 把"没有 BDA"和"撑不住 descriptor 数组"视作同一个结论：都只能跑
`ERT_Compatibility`。并且**兼容 profile 下即使设备支持 BDA 也不启用它**——这样在桌面 GPU 上加
`--force-compatibility-renderer` 就能精确复现真机，而不是只复现一半。

### 绘制与 compute 着色（M1–M3）

`CompatibilityRenderer` 现在真的画场景，但**没有走 §5.2 的 Soft Mesh GPU-driven 链路**：
cull / finalize / expand 三个 compute pass 都是 `ZeroBindPipeline`，其 layout 里带着
`GlobalTexturePool` 的 descriptor set，兼容 profile 建不出来。改用最短的等价路径：

- raster 自带一个 5 个 storage buffer 的 descriptor set（Nodes / VertexWords / Indices / Offsets /
  Materials）；compute 再自带一个 2 sampled + 1 storage image 的 descriptor set（Albedo / Normal /
  SceneColor）。二者都不碰 bindless set；shader 只用显式 binding，**不 import `Shader.Bindless`**。
- 用 storage buffer 而不是 uniform buffer：std430 的 array stride 与 CPU 结构体一致，
  std140 会把 `NodeProxy::matId[16]` 从 4 字节/元素撑到 16。已核对 SPIR-V 的 `ArrayStride`：
  NodeProxy 224、ModelData 64、Material 64、uint 4，与 `GPU_SCENE_*_SIZE` 逐个吻合。
- raster push constant 是 `float4x4 ViewProjection + uint ProxyIndex`；compute push constant 是太阳/
  天空 `float4` × 3。二者都省掉 camera descriptor 与 GPUScene address。
- 每个 render proxy 一次 `vkCmdPushConstants` + `vkCmdDraw(model.indexCount)`；
  vertex shader 用 `SV_VertexID` 做 vertex pulling，没有 vertex input binding。
- raster pass 写两张 `R8G8B8A8_UNORM` attachment：`Material.Diffuse.rgb` 与编码 world normal；
  normal alpha 同时是 coverage sentinel。它们在 graphics→compute barrier 后以 sampled image 读取。
- compute 写独立的 `R8G8B8A8_UNORM` SceneColor storage image，之后 transfer blit 到 swapchain；
  base renderer 照常负责 `PRESENT_SRC` 和 UI 的 `LOAD + STORE` pass。

### 光照：预览量级，不是 radiance

顶点法线从 vertex buffer 的 word 2..3 解出，用 `worldTS` 的左上 3x3 变到世界空间（不做
inverse-transpose：场景节点通常是刚体+均匀缩放，非均匀缩放只会让明暗过渡略微偏斜）。
compute shader 做一个太阳 Lambert 项 + 一个半球天空补光。这样照明脱离 geometry draw，后续的
compatibility-only 屏幕空间效果可以消费同一对固定 descriptor 输入，而无需引入完整 Primary Surface。

**关键：不能直接用 `SunIntensity` / `SkyIntensity`。** 这两个量是相对**天空 IBL 贴图**标定的——
典型场景 `SkyIntensity = 100`、`SunColor = 500`，因为 `SampleIBL()` 返回的是很小的数值。
兼容 profile 采样不到那张贴图，照搬量级会直接全屏过曝（实测确认过，Reinhard 和引擎的
Uncharted2 曲线都压不住）。

所以这里**只取光的色调**，用固定预览权重，并按"场景实际有哪些光"归一化：

```
lighting = (sunTint·NoL·0.75·hasSun + skyTint·hemisphere·0.55·hasSky)
         / (0.75·hasSun + 0.55·hasSky)
```

`hasSun` / `hasSky` 由 host 塞进 `SunColor.w` / `SkyColor.w`，shader 不需要分支。两者都没有时
退回平铺 base color（即加光照之前的行为），不会出现黑屏。

这样任何场景下满照面都落在 1 附近，曝光稳定、不需要 tonemap。代价是**不跟随场景光强**——
这是预览取向的取舍，不是要对齐完整渲染器。`playground.glb` 恰好是 `HasSun = 0` 的纯天空场景，
是这条归一化路径的实测用例。

**顶点缓冲按裸 uint 读，不按 `GPUVertex` 读。** `GPUVertex` 是 3 个 `half4`；只要 shader 里出现
16-bit 类型，SPIR-V 就会声明 `UniformAndStorageBuffer16BitAccess`（引擎从未启用），而
`f16tof32` 也会 lower 成经由 `half` 的 bitcast、引入 `Float16` capability。改成读 6 个 uint、
用纯整数运算解 IEEE binary16 之后，这个 module 只剩 `Shader` 和 `DrawParameters` 两个 capability。

`Offsets` 用 **encoded model-section id**（`NodeProxy::modelId` 本身）索引，不要在这里
`DecodeModelIndex`——那个函数是给 BLAS 用的。shader 侧同理。

自检手段（改 shader 后建议都跑一遍）：

```bash
spirv-dis <out>/assets/shaders/Rast.CompatibilityAlbedo.vert.slang.spv | grep -E "OpCapability|DescriptorSet"
spirv-dis <out>/assets/shaders/Core.CompatibilityShade.comp.slang.spv | grep -E "OpCapability|DescriptorSet|OpTypeImage"
```

vertex 应当只看到 `Shader` / `DrawParameters`，compute 应当只有 `Shader` / `ImageQuery` 和三个
set 0 image binding；SceneColor 的 `OpTypeImage` 必须是 `Rgba8`，不能是 `Unknown`，否则会重新要求
`shaderStorageImage*WithoutFormat`。任一 shader 出现 `PhysicalStorageBufferAddresses`、`Float16` 或
`UniformAndStorageBuffer16BitAccess` 即为回归。

已验证：`playground.glb` 几何、变换、深度、逐材质颜色、compute 着色与 UI 正常；相机可自由移动。
`compatibility-renderer-smoke` 在 `--force-compatibility-renderer` 下通过，截图确认 mini G-buffer
compute 结果可正常 present。

### 兼容 profile 不绑定场景纹理

兼容 renderer 只读材质常量，不采样任何纹理。但场景加载器照样会为每张贴图注册 slot——
`conf_room.glb` 有 11 张，而兼容 profile 的 sampled 数组只有 8 个。因此
`FBindlessProfile::bindsSceneTextures = false`：纹理照常加载上传（材质 id 保持有意义），
**超出数组的部分不绑定**，只 warn 一次。

顺带修掉一个既有漏洞：`RequestNewTextureMemAsync`（场景纹理真正走的那条路）自己分配 index，
**完全绕过了 `RegisterTexture` 的容量检查**。在完整 profile 下因为数组够大一直没暴露；
在 8 槽的兼容 profile 下就是直接越界写 descriptor——这正是真机之外的第二个崩溃源。
现在容量检查在两条路径上都有，并且 `BindSampleTexture` / `BindStorageTexture` / `BindShadowMap`
都加了最后一道越界防线。

### 验证入口

`--force-compatibility-renderer` 在有能力的桌面 GPU 上强制走这条路径。仓库里的回归脚本
`assets/agentscripts/compatibility-renderer-smoke.agentscript.json` 已把该 flag 写进脚本的
`args` 字段，直接跑即可：

```bash
gnb validate --script assets/agentscripts/compatibility-renderer-smoke.agentscript.json
```

### 尚未实施

显式 albedo texture（要动 sampled 数组，得先拿到 A12X 的 update-after-bind 上限）、CSM、
高光。另外这一版**不做骨骼蒙皮**（skinning compute
属于被跳过的 scene-frame prepass，shader 读 bind pose）、**不做视锥剔除**（逐 proxy 全量提交）、
**不做 alpha 测试 / 混合**。这些都是有意的 MVP 边界，不是遗漏。

## 1. 目标

当前目标不是实现一套完整的 non-bindless renderer，而是在 A12X 这类 constrained bindless 设备上，先建立一条最短、稳定、可继续扩展的兼容绘制路径：

1. 窗体、swapchain、present 和 ImGui/UI 正常工作；
2. 场景通过显式的 CPU per-proxy `vkCmdDraw` 路径绘制；
3. 不生成 visibility ID、不建立完整 G-buffer、不运行 Surface Build；
4. raster 只输出独立 mini G-buffer，compute shader 输出简化 scene color；
5. 先能看到场景几何、窗体和 UI，再逐步增加材质、阴影和调试能力。

这里的“mesh shader”指仓库现有的 Soft Mesh Shader 路径：GPU cull / finalize / expand 生成 indirect draw argument，随后使用 raster vertex/fragment pass 绘制展开后的三角形。第一版不引入或依赖 `VK_EXT_mesh_shader` 硬件 mesh shader。

## 2. 核心结论

兼容模式只需要一个简化的 scene-color renderer，不需要把整个 gkNextEngine 改造成第二套完整渲染架构：

```text
现有 WindowSurface / SwapChain / Present
        ↓
显式 descriptor set / CPU per-proxy draw
        ↓
Compatibility mini G-buffer raster pass
        ↓
Compatibility compute shade
        ↓
Intermediate Scene Color
        ↓ blit/copy
Swapchain
        ↓
现有 ImGui/UI LOAD + STORE pass
        ↓
Present
```

兼容设备只替换中间的 scene rendering 部分。窗口、swapchain、UI、输入、菜单和退出流程尽量复用当前路径，不在本 MVP 中重新设计。

## 3. MVP 的明确边界

### 3.1 必须支持

- `gkNextRenderer` 能在 iPad Pro A12X 上启动；
- `conf_room.glb` 等普通场景能完成 scene commit；
- 场景中有实际可见几何，而不是纯色清屏；
- swapchain 能正常 present；
- ImGui 和 Debug Layer UI 能正常绘制；
- `View → Debug Layer` / `F2` 仍然可以打开调试面板；
- visual debug 开关不再依赖完整 G-buffer，至少能控制兼容路径的 debug color 或简单 overlay；
- 连续运行一段时间不出现 pipeline creation error、`VK_ERROR_DEVICE_LOST` 或黑屏。

### 3.2 第一版不支持

- visibility instance/triangle ID；
- `R32_UINT` visibility attachments；
- Surface Build、visibility decode 和 shading scheduler；
- 桌面 Primary Surface 的完整 G-buffer（motion、object ID、BSDF、history 与 bindless albedo storage）；
- PathTracing、SoftwareTracing、AmbientCube 和硬件 ray tracing；
- temporal accumulation、DLSS、Native TAAU、SGSR2 和 frame generation；
- 完整材质纹理 bindless；
- 多 RenderView、thumbnail、offscreen material preview；
- 与桌面版完全一致的 Visual Debug 多格子资源浏览器。

这些功能不是永久禁止，而是必须在 MVP 的“窗口/UI + mini G-buffer + compute scene color”稳定之后逐项加入。

## 4. 资源模型

### 4.1 只保留 MVP 必需资源

兼容路径只创建和使用：

- push constant 中的 camera / light 数据；
- scene 的 node、mesh、vertex、index、material storage buffer；
- 一个 depth attachment；
- 两张 `R8G8B8A8_UNORM` mini G-buffer attachment（albedo、normal + coverage）；
- 一个 `R8G8B8A8_UNORM` SceneColor storage / transfer-source image；
- UI 自己需要的字体/atlas 资源。

不要为兼容路径创建完整 renderer 的 storage image bank。尤其不要因为未来可能需要 visibility、G-buffer 或 temporal，就提前创建一套会触发 MoltenVK typed resource array 编译的资源。

### 4.2 固定 descriptor 的边界

兼容路径只使用两个独立、固定大小的 descriptor set：raster pass 的五个 scene SSBO，以及
compute pass 的两张 sampled mini G-buffer image 和一张 storage SceneColor image。它不依赖：

- `GPUScene` 或 buffer device address；
- 大型全局 sampled texture array；
- 大型全局 storage image array；
- 多个 `RWTexture2D<T>` 通过同一全局数组动态转换；
- 完整 renderer 的所有 RT bank 和 pass resource contract。

因此这是资源和 shader 契约都独立的 compatibility path；没有把整个引擎重新实现成严格
non-bindless 模式，但也不会暗中回退到 full renderer 的 bindless/GPUScene 依赖。

### 4.3 scene color 的推荐格式

当前实现使用 `R8G8B8A8_UNORM`：预览光照已归一化到 LDR 范围，并将 compute output 的 SPIR-V image
format 固定为 `Rgba8`，避免要求 A12X 不保证提供的 `shaderStorageImage*WithoutFormat` feature。

当前实现固定采用 storage SceneColor：compute shader 只访问一个显式的 `float4` 输出资源，再以
`vkCmdBlitImage` 复制到 swapchain。mini G-buffer 仍是 raster color attachment，因此 graphics、compute
与 UI 的职责和资源状态保持清晰。

## 5. 最简绘制流程

### 5.1 CPU/scene 准备

复用现有 scene commit 和 scene storage buffer 构建：

1. 解析 glTF/场景；
2. 创建 NodeProxy、ModelData、vertex/index/material buffers；
3. 上传 node / vertex / index / model-offset / material buffer。

这一步不需要 visibility buffer 或 GPUScene address。兼容路径只读取位置、索引、变换和最小材质颜色信息。

### 5.2 显式 draw 提交

兼容 profile 不复用 Soft Mesh GPU-driven 的 cull / finalize / expand 链路：三个 compute pass 都绑定
`GlobalTexturePool` 且依赖 GPUScene address。它直接按 scene proxy 提交：

```text
for each visible proxy
    ↓
push ViewProjection + ProxyIndex
    ↓
vkCmdDraw(model.indexCount)
```

这是可预期的保守实现：不做 frustum / occlusion culling、skinning 或 alpha test；正确性和描述符契约优先。

### 5.3 Compatibility mini G-buffer + compute shade

新增一个双输出 raster pass。它复用 compatibility vertex pulling 的 mesh / transform 解码，但不输出
visibility ID：

```slang
struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation uint materialId : MATERIAL_ID;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 albedo : SV_Target0;
    [[vk::location(1)]] float4 normal : SV_Target1;
};
```

fragment shader 写 material 的 `BaseColorFactor` 与编码 normal；独立 compute pass 读取它们，写
SceneColor：

```slang
FragmentOutput main(FragmentInput input)
{
    output.albedo = float4(material.Diffuse.rgb, 1.0f);
    output.normal = float4(normalize(input.worldNormal) * 0.5f + 0.5f, 1.0f);
}
```

这里的 albedo color 指材质常量中的 base-color factor，不要求第一版采样 albedo texture。这样可以先验证：

- 显式 mesh 提交正确；
- vertex/index/transform 正确；
- material ID 到 material constant 的访问正确；
- depth test/write 正常；
- graphics→compute barrier、SceneColor blit、present 与 UI LOAD 都正确。

这两个 G-buffer image 是 renderer 私有的固定 descriptor；它们不注册到 `GlobalTexturePool`，也不扩张
compatibility profile 的 storage array。

### 5.4 Depth

使用现有 depth buffer，开启：

- depth test：`GREATER`（reverse-Z）；
- depth write：`true`；
- clear depth：`0.0`。

不把 depth 转成 storage image，不建立额外 depth history，不依赖 temporal pass。

## 6. Window、Swapchain 和 UI 不变量

这是本 MVP 的第一验收层，场景绘制失败时也不能破坏它。

### 6.1 swapchain 输出

- Apple 平台继续使用当前 Immediate present workaround；
- 若 swapchain 不支持 storage usage，继续使用 intermediate scene color + blit；
- scene pass 只写 intermediate color；
- blit 后再进入现有 UI render pass；
- swapchain image 不进入长期存在的全局 bindless storage descriptor。

### 6.2 UI

- ImGui 继续使用当前 framebuffer/render pass；
- scene color 到 UI pass 之间必须有覆盖真实 producer 的 color/transfer dependency；
- UI 使用 `LOAD + STORE`，不能因为兼容路径清理了 scene color 而丢失 UI；
- Debug Layer、F2 菜单项、Visual Debug checkbox 和退出操作不依赖 scene G-buffer。

### 6.3 验收顺序

每次修改兼容路径都按以下顺序验证：

1. 窗口启动；
2. UI/菜单可见；
3. swapchain 能 present；
4. 空场景/清屏颜色正确；
5. 单 mesh 颜色正确；
6. 完整 `conf_room.glb` 场景正确；
7. 连续帧稳定。

不要在第 5 步失败时直接加入 visibility、G-buffer 或 upscaler，因为那会掩盖最基本的 framebuffer/present/descriptor 问题。

## 7. Visual Debug 的 MVP 形态

第一版不恢复完整 Visual Debug 的多资源缩略图布局，只提供一个与兼容 color pass 对应的简单开关：

- 默认：输出 `BaseColorFactor` 或稳定 material color；
- Visual Debug 开启：可以输出 material ID hash、primitive hash 或纯色诊断模式；
- 输出仍然是 scene color，不读取 visibility image；
- 不新增额外 uint image、完整 G-buffer 或全局 storage array。

这样可以验证“Debug Layer 开关确实影响了兼容渲染”，同时不把 debug 工具重新绑定到完整 bindless 资源模型。

## 8. 分阶段实施

### M0：窗口/UI 基线

- compatibility mode 只创建私有 mini G-buffer、scene color 和 depth；
- 绘制固定颜色 quad/triangle；
- 验证 swapchain、blit、ImGui 和 present；
- `gnb ios run --device 2` 连续运行 60 秒。

### M1：显式 raster 几何

- 按 render proxy 直接提交 draw；
- vertex shader 解码真实 scene mesh；
- fragment shader 输出固定颜色；
- 验证 `conf_room.glb` 有完整几何且无 device lost。

### M2：mini G-buffer

- 输出 material `BaseColorFactor` 与编码 normal；
- 暂不采样 albedo texture；
- 没有 material constant 时使用白色 fallback；
- 以 coverage alpha 区分背景。

### M3：基础兼容视觉质量

- 固定 descriptor compute pass 做 Lambert / 半球天空光照，并 blit SceneColor 到 swapchain；
- 评估是否加入单张或固定数量的显式 albedo texture；
- 评估 CSM，但仍不引入完整 G-buffer/visibility storage 链路。

### M4：再决定是否需要 visibility

只有当 MVP 已稳定、且后续功能确实需要逐像素 primitive/instance ID 时，才重新评估 visibility buffer：

- 优先使用两个显式 `R32_UINT` color attachments；
- 不用 float16 保存 ID；
- 不把 uint/float typed resources 混入同一个全局 storage array；
- 如果只是 debug，优先沿用当前 fragment 直接输出 debug color 的方式。

## 9. 明确不做的事情

- 不为兼容设备复制完整桌面 renderer 的所有 renderer contract；
- 不为支持一个 debug panel 预先创建所有 G-buffer；
- 不把 `R32_UINT` 改成 float 以规避 shader 编译问题；
- 不在每帧同时运行 gradient color、visibility prepass、visual debugger 和完整 post chain；
- 不把硬件 mesh shader 和现有 Soft Mesh Shader GPU-driven 路径混为同一个 MVP；
- 不因为当前 sampled texture 数量不足，就把 texture fallback 当成最终材质方案；
- 不在设备初始化完成后再隐式切换 renderer 的资源模型。

## 10. 验收标准

### A12X 实机

- `gnb ios run --device 2` 可安装并启动；
- 能看到窗体和 UI；
- `conf_room.glb` 提交后能看到几何；
- 场景颜色至少正确显示固定颜色或 base-color factor；
- Visual Debug 开关能切换简单诊断颜色；
- 运行 60 秒无 pipeline creation error、`VK_ERROR_DEVICE_LOST`、黑屏或 UI 丢失；
- 设备不需要创建完整 bindless RT bank。

### Desktop 回归

- full bindless 路径行为不变；
- 兼容 shader 不被桌面默认 renderer 误选；
- 普通 visibility、Surface Build、Visual Test 不受 MVP 代码影响。

## 11. 后续优先级

故障复盘中的有效结论已并入本文：兼容设备在物理设备选择阶段确定独立 profile，不创建完整 bindless
RT bank，不用 float16 保存 visibility ID，也不混用 typed storage array。当前优先级是贴图与 CSM，
之后再按实机收益决定是否需要真正的 visibility ID 和更完整的兼容资源模型。
