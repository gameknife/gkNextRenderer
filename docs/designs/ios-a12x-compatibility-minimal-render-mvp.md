---
title: "iOS A12X Compatibility Minimal Render MVP"
category: design
status: 待实施
owner: engine
created: 2026-08-22
last_updated: 2026-08-22
related_design: ios-a12x-non-bindless-compatibility.md
---

# iOS A12X Compatibility Minimal Render MVP

## 1. 目标

当前目标不是实现一套完整的 non-bindless renderer，而是在 A12X 这类 constrained bindless 设备上，先建立一条最短、稳定、可继续扩展的兼容绘制路径：

1. 窗体、swapchain、present 和 ImGui/UI 正常工作；
2. 场景通过现有 Soft Mesh GPU-driven 提交路径绘制；
3. 不生成 visibility ID、不建立完整 G-buffer、不运行 Surface Build；
4. fragment shader 直接输出一个简化的 albedo color；
5. 先能看到场景几何、窗体和 UI，再逐步增加材质、阴影和调试能力。

这里的“mesh shader”指仓库现有的 Soft Mesh Shader 路径：GPU cull / finalize / expand 生成 indirect draw argument，随后使用 raster vertex/fragment pass 绘制展开后的三角形。第一版不引入或依赖 `VK_EXT_mesh_shader` 硬件 mesh shader。

## 2. 核心结论

兼容模式只需要一个简化的 scene-color renderer，不需要把整个 gkNextEngine 改造成第二套完整渲染架构：

```text
现有 WindowSurface / SwapChain / Present
        ↓
现有 GPUScene / Soft Mesh GPU-driven submit
        ↓
Compatibility Color Pass
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
- 完整 G-buffer、normal、motion、object ID、albedo storage images；
- PathTracing、SoftwareTracing、AmbientCube 和硬件 ray tracing；
- temporal accumulation、DLSS、Native TAAU、SGSR2 和 frame generation；
- 完整材质纹理 bindless；
- 多 RenderView、thumbnail、offscreen material preview；
- 与桌面版完全一致的 Visual Debug 多格子资源浏览器。

这些功能不是永久禁止，而是必须在 MVP 的“窗口/UI + 直接颜色场景”稳定之后逐项加入。

## 4. 资源模型

### 4.1 只保留 MVP 必需资源

兼容路径只创建和使用：

- Frame/camera UBO；
- GPUScene 中的 node、mesh、vertex、index、material 常量和 GPU-driven buffers；
- 一个 depth attachment；
- 一个 scene-color attachment 或 intermediate image；
- UI 自己需要的字体/atlas 资源。

不要为兼容路径创建完整 renderer 的 storage image bank。尤其不要因为未来可能需要 visibility、G-buffer 或 temporal，就提前创建一套会触发 MoltenVK typed resource array 编译的资源。

### 4.2 可以继续复用现有小规模 descriptor 能力

这个 MVP 不要求“完全没有 descriptor indexing”。如果现有 GPUScene/scene buffer descriptor 在 A12X 上可以稳定使用，可以继续复用；但兼容路径不应依赖：

- 大型全局 sampled texture array；
- 大型全局 storage image array；
- 多个 `RWTexture2D<T>` 通过同一全局数组动态转换；
- 完整 renderer 的所有 RT bank 和 pass resource contract。

因此这是一条“最小资源契约的 compatibility path”，不是把整个引擎重新实现成严格 non-bindless 模式。

### 4.3 scene color 的推荐格式

优先使用现有 swapchain/intermediate 支持的 float color 格式，例如 `R16G16B16A16_SFLOAT`。它只保存颜色，不保存 ID，所以不会有 float16 精度保存整数的问题。

scene color 可以有两种实现：

1. 直接作为 graphics color attachment，完成后 transfer 到 swapchain；
2. 使用一个固定 storage color image，但 shader 中只访问单一的 `float4` 输出资源。

第一版优先方案 1，因为它和 visibility/debug 的整数资源无关，且最容易与 UI 的 `LOAD + STORE` 流程衔接。

## 5. 最简绘制流程

### 5.1 CPU/scene 准备

复用现有 scene commit 和 GPUScene 构建：

1. 解析 glTF/场景；
2. 创建 NodeProxy、ModelData、vertex/index/material buffers；
3. 更新 GPUScene；
4. 确保 Soft Mesh primitive stream 和 indirect draw argument buffer 已准备完成。

这一步不需要 visibility buffer。GPUScene 仍然可以保存现有字段，但兼容 color pass 只读取位置、索引、变换和最小材质颜色信息。

### 5.2 GPU-driven 提交

复用当前 Soft Mesh GPU-driven 的最短链路：

```text
GPU cull / compact
    ↓
Finalize draw arguments
    ↓
Expand primitive stream
    ↓
vkCmdDrawIndirect
```

第一版可以采用兼容设备上已经验证过的保守行为：只要 render proxy 有三角形就生成 draw entry，不加入复杂的屏幕图像依赖、深度测试反馈或 subgroup fast path。正确性稳定后再恢复真正的 frustum/occlusion culling。

### 5.3 Compatibility Color Pass

新增一个只输出颜色的 raster pass。它可以复用现有 visibility vertex shader 的大部分 mesh/transform 解码逻辑，但必须去掉 visibility ID 输出：

```slang
struct VertexOutput
{
    float4 position : SV_Position;
    nointerpolation uint materialId : MATERIAL_ID;
};

struct FragmentOutput
{
    [[vk::location(0)]] float4 color : SV_Target0;
};
```

fragment shader 第一版直接输出 material 的 `BaseColorFactor`：

```slang
FragmentOutput main(FragmentInput input)
{
    const MaterialData material = FetchMaterial(input.materialId);
    return FragmentOutput(float4(material.BaseColorFactor.rgb, 1.0f));
}
```

这里的 albedo color 指材质常量中的 base-color factor，不要求第一版采样 albedo texture。这样可以先验证：

- GPU-driven mesh 提交正确；
- vertex/index/transform 正确；
- material ID 到 material constant 的访问正确；
- depth test/write 正常；
- scene color 能被 present 和 UI 继续加载。

如果材质常量访问本身在 A12X 上仍然依赖过大的 bindless buffer array，则第一版可以先输出一个按 `materialId` hash 得到的稳定颜色，随后再接入 base-color factor。这比回退到 float ID 或创建完整 G-buffer 更安全。

### 5.4 Depth

使用现有 depth buffer，开启：

- depth test：`LESS`；
- depth write：`true`；
- clear depth：`1.0`。

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
- 不新增 uint image、不新增 G-buffer、不新增 storage array。

这样可以验证“Debug Layer 开关确实影响了兼容渲染”，同时不把 debug 工具重新绑定到完整 bindless 资源模型。

## 8. 分阶段实施

### M0：窗口/UI 基线

- compatibility mode 只创建 scene color 和 depth；
- 绘制固定颜色 quad/triangle；
- 验证 swapchain、blit、ImGui 和 present；
- `gnb ios run --device 2` 连续运行 60 秒。

### M1：Soft Mesh GPU-driven 几何

- 接入 GPU cull / finalize / expand / indirect draw；
- vertex shader 解码真实 scene mesh；
- fragment shader 输出固定颜色；
- 验证 `conf_room.glb` 有完整几何且无 device lost。

### M2：最小 albedo color

- 输出 material `BaseColorFactor`；
- 暂不采样 albedo texture；
- 没有 material constant 时使用白色 fallback；
- 增加 material ID hash debug mode。

### M3：基础兼容视觉质量

- 加入明确的法线/简单 Lambert 计算；
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

## 11. 与前一份设计的关系

前一份 [iOS A12X Non-Bindless 兼容路径：适配复盘与重构设计](ios-a12x-non-bindless-compatibility.md) 仍然保留故障根因、格式边界和 MoltenVK typed array 的排障记录，但其中“实现完整 non-bindless backend”的目标被本文件的最小兼容 MVP 取代。

当前优先级应是：窗口/UI 不变量 → Soft Mesh 几何 → 直接 albedo color → 基础材质，再决定是否需要真正的 visibility ID 和更完整的兼容资源模型。

