# Windows Dozen Vulkan 后端：启动与黑屏问题根因复盘

本文记录 gkNextEngine 在 Windows 上通过 Mesa Dozen 将 Vulkan 转译到 Direct3D 12 时遇到的关键问题、真正根因、当前实现约束和排障方法。

Dozen 不是一个可以静态链接进引擎的单一库。它由 Vulkan ICD manifest、Dozen DLL 以及相关运行时文件组成；引擎仍然调用 Vulkan API，由 Dozen ICD 在底层转译为 D3D12。

## 结论先行

这次黑屏的最终根因不是场景加载、交换链 present 或 GPU culling 算法本身，而是 GPUScene 使用的 Vulkan Buffer Device Address 全部为零：

1. Dozen 当前报告的 Vulkan API 版本低于 1.2，但实际通过 `VK_KHR_buffer_device_address` 提供 BDA 能力。
2. 引擎只查询了核心入口 `vkGetBufferDeviceAddress`，没有回退到 `vkGetBufferDeviceAddressKHR`。
3. VMA 创建时没有启用 `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`。
4. GPUScene、顶点/索引缓冲和 soft-mesh culling shader 收到零地址后，GPU-driven 路径无法读取场景数据，最终表现为 UI 正常但场景全黑。

修复必须同时包含入口函数回退和 VMA BDA 标志；只修其中一项仍可能得到零地址或不稳定结果。

## 故障链

| 现象 | 根因 | 当前处理 |
| --- | --- | --- |
| 启动时报 `No app-local Dozen ICD manifest was found` | Vulkan loader 找不到 `dzn_icd*.json`，而 Dozen 的 DLL 也不一定在进程搜索路径中 | `gnb` 自动准备依赖；构建时把 runtime 复制到 `out/build/windows/bin/dozen`；运行时设置 `GK_NEXT_DOZEN_ICD`、`VK_DRIVER_FILES`、`VK_ICD_FILENAMES` 和 DLL 搜索路径 |
| `CreateInstance` 失败或实例后的查询异常 | Dozen/Windows loader 对标准 Vulkan loader、SDL Vulkan 加载和重复扩展查询的兼容性有限 | Dozen 模式直接从 `vulkan-1.dll` 获取实例入口；跳过实例创建后的 layer/extension 二次枚举；物理设备枚举也走 loader 入口 |
| `vkCreateDevice` 报扩展或 feature chain 错误 | Dozen 可以报告完整 feature chain，但当前实现创建完整链时会出现 `VK_ERROR_EXTENSION_NOT_PRESENT` | Dozen 使用最小 device feature chain，仅保留 swapchain、BDA、descriptor indexing，并显式启用必要的 core/feature 位 |
| 普通 `gnb run ... --vulkan-driver dozen` 在 device 创建前以 `0xc0000005` 退出 | 普通 run 仍注册 FidelityFX 的 Vulkan device augmenter；该模块按 native Vulkan 假设枚举扩展、追加 feature/queue 配置，与 Dozen 的 device dispatch/最小 feature chain 不兼容 | Dozen 运行必须禁用 FidelityFX；`shot/validate` 已因 agent validation 自动禁用，普通 run 需要显式 `--disable-fidelityfx`，最终应由 Dozen driver mode 自动设置 |
| 设备创建成功但场景黑屏 | GPUScene 的 BDA 为零；Dozen 实际使用 KHR BDA 入口而引擎只查核心入口 | 启用 VMA BDA 标志，并从 `vkGetBufferDeviceAddressKHR` 回退获取地址 |
| 交换链创建成功但无法直接写出场景 | Dozen swapchain 不支持引擎原有的 storage image 写入路径 | 场景先写中间 render target，再通过 `vkCmdBlitImage` 复制到 swapchain |
| 截图请求一直等待 | `vkGetImageSubresourceLayout` 直接调用在 Dozen dispatch 下不可用 | 通过 renderer device proc table 动态获取该入口 |

## 1. 运行时和 ICD manifest

### 1.1 Dozen 的实际部署形态

Dozen 的可运行文件至少包括：

- `dzn_icd.x86_64.json` 或同类 ICD manifest；
- manifest 引用的 `vulkan_dzn.dll`；
- Dozen 依赖的其他 DLL 和运行时文件。

manifest 不是可选的配置文件。Vulkan loader 先读取 manifest，再根据 manifest 加载 ICD DLL。因此只复制 DLL、只把 Dozen 目录放进 PATH，仍可能无法启动。

当前构建流程通过 [`gk_stage_dozen_runtime`](../../src/cmake/TargetHelpers.cmake) 和 [`CopyDozen.cmake`](../../cmake/CopyDozen.cmake) 将 `external/Dozen/x64` 复制到：

```text
out/build/windows/bin/dozen/
```

### 1.2 gnb 的自动配置

Dozen 依赖由 [`gnb.toml`](../../gnb.toml) 的 `[external.dozen]` 段定义。首次准备或刷新依赖：

```powershell
gnb.bat setup --dozen
```

使用 gnb 启动、验证或截图时传入：

```powershell
gnb.bat shot --target gkNextRenderer `
    --scene assets/models/playground.glb `
    --vulkan-driver dozen
```

gnb 会检查 manifest，设置子进程环境，并把 Dozen DLL 所在目录加入 PATH。引擎自身的 [`WindowSurface.cpp`](../../src/Engine/Vulkan/WindowSurface.cpp) 还会按以下顺序寻找 app-local manifest：

1. `GK_NEXT_DOZEN_ICD` 指定的路径；
2. 可执行文件目录；
3. 可执行文件目录下的 `dozen`；
4. runtime root 及其 Dozen/Vulkan 子目录。

若使用自定义 Dozen 安装，可显式指定：

```powershell
$env:GK_NEXT_DOZEN_ICD = 'D:\path\to\dzn_icd.x86_64.json'
```

### 1.3 loader 配置原则

找到 manifest 后，Dozen 模式设置：

- `VK_DRIVER_FILES=<manifest>`；
- `VK_ICD_FILENAMES=<manifest>`；
- 清除 `VK_LOADER_DRIVERS_SELECT`；
- 将 manifest 所在目录和 `vulkan_dzn.dll` 所在目录加入 PATH。

找不到 app-local manifest 时才回退到系统 loader 的 `dzn*` 选择器。开发和打包环境应优先使用 app-local manifest，避免机器上安装的其他 ICD 改变选择结果。

## 2. 实例创建与 Vulkan 入口分发

### 2.1 不要把 Dozen 路径和 Streamline interposer 叠加

native Vulkan 路径可以由 SDL 加载 Streamline interposer，再由 interposer 转发到 Vulkan loader。Dozen 选择的是另一套 ICD，Streamline 的 Vulkan interposer 预期 native vendor driver，不能稳定地和 Dozen ICD 组合。

因此 Dozen 模式：

- 禁用 Streamline；
- 不调用 `SDL_Vulkan_LoadLibrary`；
- 直接使用系统 `vulkan-1.dll` 的 loader 入口；
- SDL 仍负责窗口和 surface 所需的 Win32 集成。

### 2.2 直接从 loader 获取入口

Dozen 当前对全局 Vulkan 符号和实例/设备 dispatch 的表现不完全等同于 native ICD。关键入口在 Dozen 模式应按层级获取：

- 全局/实例入口：从 `vulkan-1.dll` 的 `vkGetInstanceProcAddr` 获取；
- 设备入口：从 `vkGetDeviceProcAddr` 或引擎的 device procedure table 获取；
- command buffer 命令：不要假设链接库中的全局函数指针在 Dozen 下已经正确绑定。

这也是为什么 Dozen 路径对 command、同步、截图、pipeline 和资源命令做了动态 proc lookup。这里的目的不是绕过 Vulkan API，而是确保函数指针属于当前 Dozen device dispatch。

### 2.3 实例后的枚举要克制

Dozen 当前实现对重复的 layer/extension 查询较敏感。实例创建成功后，引擎只执行必要的物理设备枚举，跳过 native 路径的实例 layer/extension 二次枚举。

## 3. 最小 device feature chain

Dozen 当前 Windows ICD 能枚举较完整的 feature chain，但创建 logical device 时消费完整链会失败。因此不能简单地把 native Vulkan 的全部 feature 和扩展照搬过来。

Dozen 最小路径只保留：

- `VK_KHR_swapchain`；
- `VK_KHR_buffer_device_address`；
- `VK_EXT_descriptor_indexing`。

同时启用当前 GPU-driven 路径必需的能力：

- `robustBufferAccess`；
- `shaderInt64`；
- `runtimeDescriptorArray`；
- shader sampled/storage image 的非 uniform indexing；
- `bufferDeviceAddress`。

以下能力在 Dozen 模式不作为启动前提：

- ray tracing / ray query；
- shader clock instrumentation；
- native upscaler；
- atmosphere device resources；
- 初始 HDR 环境纹理预加载。

Dozen 没有 dedicated transfer-only queue 时，上传和 transfer command 使用 graphics queue。这是队列能力差异的兼容处理，不是渲染错误。

## 4. FidelityFX 必须与 Dozen 隔离

普通 `run` 与 `shot/validate` 的一个重要差异是：agent validation 会自动设置 `DisableFidelityFX`，而普通 `run` 默认仍会安装 FidelityFX FSR 3.1 provider。

FidelityFX 安装时会注册：

- Vulkan swapchain interposer；
- logical device 创建前的 `IDeviceCreationAugmenter`；
- FidelityFX upscaler factory。

在 `SetPhysicalDeviceImpl()` 中，所有 device augmenter 都会在 Dozen 最小 feature chain 处理前运行。FidelityFX augmenter 使用 native Vulkan 的全局 `vkEnumerateDeviceExtensionProperties`，并追加 timeline semaphore、扩展和队列规划。Dozen 模式下这条路径不满足当前 loader/ICD 的 dispatch 和队列契约，普通 run 会在输出以下日志之后、Dozen feature-chain 日志之前直接发生访问冲突：

```text
FidelityFX FSR 3.1 Vulkan provider installed alongside other upscalers
...
Soft-mesh GPU cull: subgroup size 0, wave fast-path disabled (LDS fallback)
exit status 0xc0000005
```

禁用 FidelityFX 后，日志会变为：

```text
FidelityFX FSR plugins disabled for this application
Dozen: skipping optional physical-device feature-chain query
Creating Vulkan logical device with 3 extensions
...
---- Next Engine Started
```

当前临时绕过方式：

```powershell
gnb.bat run gkNextRenderer --vulkan-driver dozen --disable-fidelityfx
```

Dozen 模式的正确长期策略是和 Streamline 一样，在解析 `--vulkan-driver dozen` 时自动设置 `DisableFidelityFX = true`。这样普通 `run`、`shot`、`validate` 和其他 gnb 启动入口的行为一致，不依赖用户记住额外参数。

## 5. 真正导致黑屏的 BDA 根因

### 5.1 现象定位

黑屏期间 UI 可以正常绘制，说明以下链路已经工作：

- SDL 窗口创建；
- Vulkan instance/device 创建；
- swapchain acquire/present；
- ImGui/UI command 提交。

因此问题集中在场景 GPU 数据或场景 render pass，而不是“整个 Dozen 没有工作”。

GPU-driven 统计最终确认：CPU 场景节点、模型和 batch 均存在，但 GPUScene 地址为零，GPU culling 无法读取场景 buffer。

### 5.2 为什么只查核心入口会失败

Dozen 当前报告的 API 版本低于 Vulkan 1.2，但它通过 `VK_KHR_buffer_device_address` 扩展提供 BDA。此时：

```cpp
vkGetBufferDeviceAddress
```

可能为空，而：

```cpp
vkGetBufferDeviceAddressKHR
```

可用。

当前 [`Buffer::GetDeviceAddress`](../../src/Engine/Vulkan/GpuResources.cpp) 的规则是：

1. 先查询核心 `vkGetBufferDeviceAddress`；
2. 查询失败时回退到 `vkGetBufferDeviceAddressKHR`；
3. 两者都不存在时返回零地址。

这里不能根据 API version 简单判断“核心入口一定存在”，也不能因为扩展名和核心功能语义相同就省略 KHR 入口。

### 5.3 VMA 也必须显式启用 BDA

VMA allocator 创建时必须带：

```cpp
VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
```

否则由 VMA 创建的 buffer allocation 不保证满足 BDA 使用契约。Dozen 路径使用 Vulkan 1.1 兼容配置，但仍启用 BDA allocator flag；native 路径保留原有 memory budget 和 BDA 标志。

### 5.4 为什么 shaderInt64 也属于必要条件

GPUScene 和 soft-mesh culling shader 把 buffer device address 作为 64 位 shader 值使用。Dozen 的最小 feature chain 不消费完整 feature query，因此必须显式启用 `shaderInt64`，否则即使 CPU 侧拿到了非零 BDA，shader 仍可能无法正确执行。

## 6. swapchain storage 限制

Dozen 的 swapchain surface 当前不支持引擎 native 路径所依赖的 `VK_IMAGE_USAGE_STORAGE_BIT`。继续把场景颜色直接作为 storage image 写入 swapchain，会在 surface 创建或后处理阶段失败。

当前处理方式：

1. 场景始终渲染到中间 scene-color render target；
2. Dozen swapchain 只申请 color/transfer 相关 usage；
3. tone mapping 或输出阶段使用 `vkCmdBlitImage` 把中间图像复制到 swapchain；
4. 完成 layout barrier 后再 present。

这条 fallback 也解释了日志中的：

```text
Swapchain STORAGE usage is unavailable; using intermediate render target + blit fallback
```

该警告是 Dozen 的已知能力差异，不代表当前运行失败。

## 7. 截图和验证链路

截图路径也必须使用当前 device 的动态入口。Dozen 下 `vkGetImageSubresourceLayout` 不能假设链接库的全局符号已绑定，因此截图模块通过 renderer device proc 获取该函数。

推荐验证顺序：

```powershell
# 编译受影响的核心目标
gnb.bat build gkNextRenderer gkNextUnitTests

# 运行自动化输入和截图验证
gnb.bat validate `
    --script assets/agentscripts/smoke.agentscript.json `
    --vulkan-driver dozen `
    --report dozen-final.json

# 运行较长帧数的实际场景验证
gnb.bat shot `
    --target gkNextRenderer `
    --scene assets/models/playground.glb `
    --frames 120 `
    --vulkan-driver dozen
```

成功标准：

- 日志显示已选择 Dozen ICD；
- logical device 创建成功；
- scene load/commit 成功；
- smoke 报告 `passed: true`；
- 截图中有完整场景，而不是只有 UI 或纯黑；
- 进程能自动退出且返回码为 0。

本次最终验证结果为 `301` 个测试、`59123` 个断言全部通过，Dozen smoke 和 120 帧 playground 截图均成功。

## 8. 已知限制与不要误判的日志

Dozen 是 Vulkan-on-D3D12 的兼容/测试实现，当前不应按 native Vulkan 的能力集合判断成功与否：

- `dzn is not a conformant Vulkan implementation, testing use only` 是 Dozen 自身的状态警告；
- Dozen 路径关闭硬件 ray tracing，使用 `SoftwareModernNoAmbient`；
- atmosphere、ambient-cube 资源和 native upscaler 在 Dozen 下跳过；
- Dozen 的物理设备属性/内存报告不完整，日志中的零 heap budget 不能直接等同于真实 D3D12 显存为零；
- 当前目标是稳定验证 Vulkan 调用转译和基本渲染，不是实现 native Vulkan 的完整画质与特性 parity。

## 9. 排障检查表

遇到新的 Dozen 问题时，按以下顺序检查，避免把多个层级的问题混在一起：

1. **manifest**：确认 `GK_NEXT_DOZEN_ICD` 或 app-local `dzn_icd*.json` 存在；确认 manifest 引用的 DLL 能被 PATH 找到。
2. **模块隔离**：确认日志显示 `FidelityFX FSR plugins disabled for this application`，并确认没有注册 FidelityFX device augmenter。
3. **loader**：确认日志显示 `Vulkan driver mode: Dozen`，并确认没有继续加载 Streamline Vulkan interposer。
4. **instance**：确认 Dozen instance extension 列表只包含实际可用扩展，且 physical-device enumerate 入口可用。
5. **device**：确认只启用最小扩展集和最小 feature chain；不要恢复完整 native feature 查询。
6. **BDA**：如果 UI 正常但场景黑屏，优先检查 `GetDeviceAddress()` 是否走了 KHR fallback，以及 VMA allocator 是否带 BDA flag。
7. **GPU culling**：确认 GPUScene、顶点、索引和 soft-mesh buffer 地址非零；确认 culling 使用当前帧 `ViewProjectionUnJit`。
8. **swapchain**：确认 Dozen 下没有要求 swapchain storage，且 scene-color blit fallback 被执行。
9. **截图**：如果渲染正常但截图超时，检查 screenshot device proc，而不是重复检查场景渲染。

相关实现入口：

- [`WindowSurface.cpp`](../../src/Engine/Vulkan/WindowSurface.cpp)：Dozen manifest、loader 环境和 SDL 集成；
- [`Instance.cpp`](../../src/Engine/Vulkan/Instance.cpp)：Dozen instance 创建和物理设备枚举；
- [`Device.cpp`](../../src/Engine/Vulkan/Device.cpp)：队列选择、device proc 和 logical device 创建；
- [`VulkanBaseRenderer.cpp`](../../src/Engine/Rendering/VulkanBaseRenderer.cpp)：Dozen feature chain、渲染 fallback 和输出；
- [`GpuResources.cpp`](../../src/Engine/Vulkan/GpuResources.cpp)：BDA KHR fallback；
- [`Allocator.cpp`](../../src/Engine/Vulkan/Allocator.cpp)：VMA BDA allocator 配置；
- [`fetcher.go`](../../tools/gnb/internal/fetcher/fetcher.go)：gnb Dozen 下载、manifest 查找和运行环境。
