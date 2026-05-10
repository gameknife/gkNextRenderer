# Phase 10 · Runtime/RHI 门面：Editor / Subsystems 不再直接 include Vulkan

> **目的：** 引入 `Runtime/RHI/` 薄门面层，让 `Application/Core/Editor/` 与 `Runtime/Subsystems/` 不再 `#include "Vulkan/..."`。
> **依赖：** phase-09 完成
> **范围：** 新增 `Runtime/RHI/`；移除 ~30 处 Vulkan 直接 include
> **预计 diff：** 新增 2~3 文件；改写 ~30 处 include + 少量类型转发

---

## 1. 当前耦合现状

审计发现的"非法穿透"include：

| 模块 | 文件 | 直 include 的 Vulkan 头 |
| --- | --- | --- |
| Runtime/Subsystems | `Audio/Audio.cpp` | `Vulkan/Debug/Validation.h`（旧名 DebugUtilities） |
| Runtime/Subsystems | `Animation/Animation.cpp` | `Vulkan/Debug/Validation.h` |
| Runtime/Subsystems | `Physics/Physics.cpp` | `Vulkan/Debug/Validation.h` |
| Runtime/Camera | `ModelViewController.cpp` | `Vulkan/Debug/Validation.h`（**为什么模型视图控制器需要 Vulkan 调试？**） |
| Application/Core/Editor | `Panels/PropertiesPanel.cpp` 等 | 多处 ImGui Vulkan 后端、SwapChain、Device |
| Application/Core/Editor | `EditorMain.cpp` | `Vulkan/Core/Device.h` `Vulkan/Pipeline/...` |

> Subsystems 那边明显是历史 debug 残留（include 了不用），可以**直接删 include**。
> Editor 那边是真依赖（ImGui Vulkan 后端、上传字体纹理、抓取 swapchain image），需要**门面**。

按 README §2 D7：**仅 Editor / Runtime/Subsystems 强制走门面**；Rendering 仍可裸套 Vulkan。

---

## 2. 目标：Runtime/RHI 门面

```
src/Runtime/RHI/
├─ RenderContext.{cpp,h}     # 暴露 Editor 真正需要的 5~10 个能力
└─ ImGuiBackend.{cpp,h}      # 把 ImGui_ImplVulkan_* 调用包进去
```

### 2.1 `Runtime/RHI/RenderContext.h` 接口草图

```cpp
#pragma once
#include "Common/CoreMinimal.h"

// 仅前向声明 Vulkan 类型，避免对外暴露 Vulkan/* 头
namespace Vulkan {
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace Runtime {
namespace RHI {

class RenderContext {
public:
    // === Editor 需要的能力 ===
    // 用 opaque 类型 / VkXxx 透传（不暴露 Vulkan/Core 头）
    VkDevice GetDevice() const;
    VkPhysicalDevice GetPhysicalDevice() const;
    VkInstance GetInstance() const;
    VkQueue GetGraphicsQueue() const;
    uint32_t GetGraphicsQueueFamily() const;
    VkRenderPass GetMainRenderPass() const;
    uint32_t GetSwapChainImageCount() const;
    VkExtent2D GetSwapChainExtent() const;
    VkFormat GetSwapChainFormat() const;

    // === ImGui 后端常用 ===
    void UploadFontTexture(/* font atlas */);
    void RecreateForSwapchain();

    // === 截图 / 抓帧（编辑器用） ===
    bool BlitSwapChainTo(VkImage dst);

private:
    Vulkan::Device* device_;
    Vulkan::SwapChain* swapChain_;
    // ...
    friend class NextEngine;  // Engine 内部 ctor 注入
};

}}  // namespace
```

注意：**接口里仍出现 VkDevice / VkRenderPass 等 Vulkan handle**。因为 ImGui Vulkan 后端就是吃这些 handle。**门面的目的不是隐藏 Vulkan 类型本身**，而是：

1. 收敛唯一访问点 —— Editor 不再东拉一个 `Device::Handle()` 西拉一个 `SwapChain::ImageCount()`
2. 隔离声明依赖 —— Editor 只 include `Runtime/RHI/RenderContext.h`，前向声明屏蔽 `Vulkan/*` 头
3. 留出未来替换后端的可能 —— 想换 D3D12 时，只要换 `RenderContext` 的实现

### 2.2 `Runtime/RHI/ImGuiBackend.{cpp,h}`

把 `ImGui_ImplVulkan_Init` `ImGui_ImplVulkan_NewFrame` `ImGui_ImplVulkan_RenderDrawData` 等调用整体打包：

```cpp
namespace Runtime { namespace RHI {

class ImGuiBackend {
public:
    void Init(RenderContext* ctx);
    void Shutdown();
    void BeginFrame();
    void Render(VkCommandBuffer cmd);
    // ImGui_ImplVulkan_AddTexture / RemoveTexture 等
};

}}
```

Editor 调用 `imguiBackend.Render(cmd)`，不再调 `ImGui_ImplVulkan_RenderDrawData`。

---

## 3. 步骤

### 3.1 删除 Subsystems 的"借用 include"

```bash
# Audio.cpp / Physics.cpp / Animation.cpp / Camera/ModelViewController.cpp
# 这些里 include 了 Vulkan/Debug/Validation.h 但实际不用
# grep 找出后删除
for f in src/Runtime/Subsystems/Audio/Audio.cpp \
         src/Runtime/Subsystems/Physics/Physics.cpp \
         src/Runtime/Subsystems/Animation/Animation.cpp \
         src/Runtime/Camera/ModelViewController.cpp; do
  sed -i.bak '/#include "Vulkan\/Debug\/Validation.h"/d' "$f"
done
find src/ -name '*.bak' -delete
```

如果删除后编译失败（说明实际有用），那就把该 include 替换为 `Runtime/RHI/RenderContext.h`，并把代码里直接调用的 `Vulkan::ValidateXxx` 改为通过 RenderContext 的 method。

### 3.2 创建 RenderContext

```bash
mkdir -p src/Runtime/RHI
# Write RenderContext.h / RenderContext.cpp / ImGuiBackend.h / ImGuiBackend.cpp
```

#### 3.2.1 RenderContext 实现要点

- 由 `NextEngine` 在 `Start()` 里持有并构造（注入 Device / SwapChain 等）
- 提供 getter 给 Editor 使用
- 不持有所有权（指针引用 NextEngine 已有的资源）
- 在 swapchain 重建时被通知更新内部缓存

#### 3.2.2 NextEngine 持有 RenderContext

`Engine.h` 已存在，本 phase **允许在 Engine.h 加 1~2 行**：

```cpp
class NextEngine {
    // ... 既有
    Runtime::RHI::RenderContext* GetRenderContext();  // 新增

private:
    std::unique_ptr<Runtime::RHI::RenderContext> renderContext_;  // 新增
};
```

> **特例豁免**：phase-08 规定 Engine.h 不动；本 phase 因为是引入门面**必须**改 Engine.h。请在 PR 描述里明示这一豁免。

### 3.3 改 Editor 的 include

`Application/Core/Editor/` 内所有 `#include "Vulkan/..."`：

```bash
grep -rn '"Vulkan/' src/Application/Core/Editor/ --include='*.cpp' --include='*.h'
```

对每个引用：

| 原来用 Vulkan/X 拿 | 改为 |
| --- | --- |
| `device_->Handle()` | `engine.GetRenderContext()->GetDevice()` |
| `swapChain_->ImageCount()` | `engine.GetRenderContext()->GetSwapChainImageCount()` |
| `ImGui_ImplVulkan_RenderDrawData(...)` | `engine.GetImGuiBackend()->Render(cmd)` |
| ... | ... |

把 Editor 内所有 `#include "Vulkan/Core/Device.h"` 等替换为 `#include "Runtime/RHI/RenderContext.h"`。

### 3.4 改 Subsystems 的 include（如果有真用）

理论上 §3.1 后 Subsystems 不再 include Vulkan。如果 §3.1 删完编译过了，本步无事可做。

如果某个 Subsystem 真的需要 GPU resource（罕见，可能只有 Animation 用 GPU skinning），同样走 RenderContext。

### 3.5 SourceFiles.cmake

`src_files_engine` 已 GLOB `Runtime/*.cpp`，自动包含 `Runtime/RHI/*.cpp`。无需改。

### 3.6 验证 Editor 不再依赖 Vulkan/

```bash
# 应该 0 行
grep -rn '#include "Vulkan/' src/Application/Core/Editor/ --include='*.cpp' --include='*.h'

# Subsystems 也应该 0 行（或仅留下确实需要的）
grep -rn '#include "Vulkan/' src/Runtime/Subsystems/ --include='*.cpp' --include='*.h'
```

---

## 4. 验收门

```bash
# 1. RHI 文件就绪
test -f src/Runtime/RHI/RenderContext.h && \
test -f src/Runtime/RHI/RenderContext.cpp && \
test -f src/Runtime/RHI/ImGuiBackend.h && \
test -f src/Runtime/RHI/ImGuiBackend.cpp && echo OK

# 2. Editor 0 直接 include Vulkan/
grep -c '#include "Vulkan/' src/Application/Core/Editor/ -r
# 期望: 0

# 3. Subsystems 0 直接 include Vulkan/
grep -c '#include "Vulkan/' src/Runtime/Subsystems/ -r
# 期望: 0

# 4. Camera 0 直接 include Vulkan/Debug/
grep -n '#include "Vulkan/' src/Runtime/Camera/ -r
# 期望: 0

# 5. 编译 + 单测 + 编辑器能启动（手动一次）
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
./gnb editor  # 手动确认编辑器正常打开（人类辅助验收）
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] `Runtime/RHI/` 含 4 文件（2 cpp + 2 h）
- [ ] Editor / Subsystems / Camera 0 直接 include Vulkan/
- [ ] Engine.h 仅新增 1 个 getter + 1 个 member（在 PR 描述明示）
- [ ] RenderContext 是非拥有性引用（不要 `unique_ptr<Device>`）
- [ ] 编辑器手动启动一次，菜单 / 面板 / Gizmo 都正常（visual sanity）
- [ ] 单元测试通过
- [ ] PR 标题：`refactor(phase-10): 引入 Runtime/RHI 门面`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| 过度设计 RenderContext 接口 | 按"Editor 实际用什么就暴露什么"，不预先暴露未来可能用的；后续按需扩展 |
| Editor 改 include 后大量 build 错误 | 一个 panel 一个 panel 改，不要一次性改完整个 Editor |
| ImGui Vulkan 后端的 hook 函数（`g_VulkanInitInfo` 等）需要 Editor 直接持有 | 把 hook 信息封装在 RHI::ImGuiBackend 内 |
| RenderContext 与 NextEngine 双向引用循环 | RenderContext 持有 Vulkan::Device* 等指针；不持有 NextEngine。NextEngine 持有 RenderContext unique_ptr。单向 |

回退：`git revert` 单一 squash commit。

## 7. 后续扩展（不在本 phase 范围）

- 把 `Vulkan::DescriptorSystem` 也包到 `RHI::DescriptorAllocator`
- 把 `Rendering/Base/` 也走 RHI（D7 决策已排除）
- 引入 `RHI::IBackend` 抽象基类 → 真正支持替换为 D3D12 / Metal

这些都是未来工作。本 phase 仅完成"Editor / Subsystems 不再裸 include Vulkan"。
