---
title: "Dozen Vulkan 后端排障"
category: guide
status: 现行
owner: rendering
last_updated: 2026-08-31
---

# Dozen Vulkan 后端排障

Dozen 是 Windows 上基于 D3D12 的 Vulkan 兼容后端，主要用于软件兼容性验证，不是正式发布驱动。仓库不会下载或分发 Dozen；使用者需自行准备 Mesa Dozen 的 ICD manifest 与其引用的 DLL。

## 驱动放置与启动

引擎支持的常见 manifest 位置包括：

1. 环境变量 `GK_NEXT_DOZEN_ICD` 指向的 manifest；
2. `external/Dozen/x64/`；
3. 可执行文件附近的 `dozen/` 或 `Vulkan/` 目录。

manifest 中引用的 DLL 必须与 manifest 保持相对路径关系。启动时使用统一驱动选择参数：

```powershell
.\gnb.bat shot --target gkNextRenderer --scene assets/models/playground.glb --vulkan-driver dozen
```

`--vulkan-driver dozen` 会自动禁用 Streamline 和 FidelityFX，不需要再传禁用参数。引擎会在创建 Vulkan instance 前设置 `VK_DRIVER_FILES`、`VK_ICD_FILENAMES` 和 DLL 搜索路径，并清除会干扰显式 ICD 选择的隐式 layer/驱动选择环境。

## 预期行为

- Dozen 只在 Windows 可选；其他平台会明确报错。
- 软件/翻译 ICD 不经过 Streamline interposer。
- 设备能力以实际枚举结果为准，不能假定硬件光追、swapchain storage image 或厂商扩展可用。
- swapchain 不支持 storage usage 时，引擎使用中间图像并在 present 前复制/转换。
- 成功启动的统一标志是日志出现 `committed scene [...]`。

## 排障顺序

1. 确认 manifest 与 DLL 位数均为 x64，manifest 内 library 路径有效。
2. 用 `GK_NEXT_DOZEN_ICD` 指定绝对 manifest 路径，排除自动发现问题。
3. 检查首段日志是否明确选择 Dozen，以及是否枚举到 Microsoft/Dozen adapter。
4. 若 instance 创建失败，先移除外部 `VK_LAYER_PATH`、`VK_INSTANCE_LAYERS` 等注入环境后重试。
5. 若 device 创建失败，以日志中的缺失 feature/extension 为准；不要通过伪报能力绕过检查。
6. 若画面提交失败，检查 swapchain usage、格式和 copy/blit 路径，而不是假定 present image 可直接作为 storage image。

相关实现：[`Options.cpp`](../../src/Engine/Options.cpp)、[`WindowSurface.cpp`](../../src/Engine/Vulkan/WindowSurface.cpp) 和 [`VulkanLoaderBypass.cpp`](../../src/Engine/Vulkan/VulkanLoaderBypass.cpp)。
