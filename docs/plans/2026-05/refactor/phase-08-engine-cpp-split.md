# Phase 08 · Engine.cpp 拆分

> **目的：** 把 1692 LoC 的 `Runtime/Engine.cpp` 按职能拆为 6 个伴生 cpp，header 不动。
> **依赖：** phase-07 完成
> **范围：** `src/Runtime/Engine.cpp`（仅 1 文件，但是大头）
> **预计 diff：** 1 cpp 拆为 6 cpp；`Engine.h` 一行未改

---

## 1. 当前状况

`Engine.cpp` 1692 LoC，类 `NextEngine` 的所有实现都堆在这里。从 phase-00 调研得到的方法清单（行号是当前主干）：

| 行 | 方法 | 性质 |
| --- | --- | --- |
| 72 | `RegisterReflection` | Lifecycle |
| 292 | `TickHotReload` | HotReload |
| 331 | `RequestShaderHotReload` | HotReload |
| 360 | `Start` | Lifecycle |
| 679 | `End` | Lifecycle |
| 717 | `RegisterJSCallback` | Lifecycle/Glue |
| 725 | `AddTimerTask` | Glue |
| 734 | `RequestClose` | Window |
| 742 | `RequestMinimize` | Window |
| 781 | `ConfigureCustomTitleBarDrag` | Window |
| 797 | `ToggleMaximize` | Window |
| 809 | `RequestScreenShot` | ScreenShot |
| 923 | `RayCastGPU` | Renderer/RT |
| 931 | `SetProgressiveRendering` | Renderer |
| 953 | `TryGetGPUAccelerationStructureAddress` | Renderer/RT |
| 965 | `TryGetGPUAccelerationStructureHandle` | Renderer/RT |
| 1112 | `OnRendererDeviceSet` | Renderer Callback |
| 1149 | `OnRendererCreateSwapChain` | Renderer Callback |
| 1160 | `OnRendererDeleteSwapChain` | Renderer Callback |
| 1168 | `OnRendererPostRender` | Renderer Callback |
| 1247 | `OnKey` | Input |
| 1466 | `OnTouch` | Input |
| 1471 | `OnTouchMove` | Input |
| 1473 | `OnCursorPosition` | Input |
| 1487 | `OnMouseButton` | Input |
| 1500 | `OnScroll` | Input |
| 1510 | `OnDropFile` | Input |
| 1530 | `TickGamepadInput` | Input |
| 1550 | `OnRendererBeforeNextFrame` | Renderer Callback |
| 1556 | `RequestLoadScene` | SceneLoad |
| 1571 | `LaunchLoadSceneTask` | SceneLoad |
| 1636 | `LoadScene` | SceneLoad |
| 1692 | `InitPhysics` | Lifecycle (空实现) |

---

## 2. 目标拆分

```
src/Runtime/
├─ Engine.h                       # 不动
├─ Engine.cpp                     # 残留 ≤ 250 LoC：Tick 主循环 + Glue（构造/析构/RegisterJSCallback/AddTimerTask）
├─ Engine_Lifecycle.cpp           # RegisterReflection / Start / End / InitPhysics
├─ Engine_Window.cpp              # RequestClose / RequestMinimize / ConfigureCustomTitleBarDrag / ToggleMaximize
├─ Engine_ScreenShot.cpp          # RequestScreenShot
├─ Engine_Renderer.cpp            # RayCastGPU / SetProgressiveRendering / TryGetGPUAccelerationStructure* / OnRendererDeviceSet / OnRendererCreateSwapChain / OnRendererDeleteSwapChain / OnRendererPostRender / OnRendererBeforeNextFrame
├─ Engine_Input.cpp               # OnKey / OnTouch / OnTouchMove / OnCursorPosition / OnMouseButton / OnScroll / OnDropFile / TickGamepadInput
├─ Engine_HotReload.cpp           # TickHotReload / RequestShaderHotReload
└─ Engine_SceneLoad.cpp           # RequestLoadScene / LaunchLoadSceneTask / LoadScene
```

> 8 个 cpp，包括残留 `Engine.cpp`。每个文件 100~400 LoC。

---

## 3. 步骤

### 3.1 拆分原则

1. **类成员定义保持 `void NextEngine::XxxMethod()` 完整签名**，不改一字
2. **匿名命名空间**：原 `Engine.cpp` 顶部 / 中部如有匿名 namespace 中的 helper，按"被谁调用"决定归属：
   - 仅被 1 个新 cpp 用：搬过去
   - 被多个新 cpp 用：保留在残留 `Engine.cpp` + 在 `Engine.h` 加 `namespace NextEngineDetail { ... }` 共享，**或**搬到一个新文件 `Engine_Internal.h`（不暴露到 public）
3. **静态变量**：原 `Engine.cpp` 顶部 `static int gXxx = 0` 等，按调用方归属同上
4. **include**：每个新 cpp 顶部固定写：
   ```cpp
   #include "Common/CoreMinimal.h"
   #include "Runtime/Engine.h"
   // 其它本文件实际用到的头
   ```
5. **避免重复 include**：原 `Engine.cpp` 头部 30+ include 不要简单复制到每个新 cpp；每个新 cpp 只 include 自己的方法实际需要的头

### 3.2 操作步骤

```bash
cd src/Runtime
# 1. 备份 Engine.cpp 内容供参考（不进 git）
cp Engine.cpp /tmp/Engine.cpp.bak

# 2. 创建 7 个新 cpp（先空文件，逐步剪贴）
touch Engine_Lifecycle.cpp Engine_Window.cpp Engine_ScreenShot.cpp \
      Engine_Renderer.cpp Engine_Input.cpp Engine_HotReload.cpp Engine_SceneLoad.cpp

# 3. 按 §1 表格逐方法剪切粘贴到对应文件
#    建议工作流：
#    - 打开 Engine.cpp 找到方法体
#    - 复制到 Engine_<Group>.cpp
#    - 把方法体所需的 include 加到新 cpp 顶部
#    - 从 Engine.cpp 删掉

# 4. 检查残留 Engine.cpp 仅含 §2 中规定的内容
wc -l Engine.cpp  # 期望 ≤ 250
```

### 3.3 各文件 include 模板

#### Engine_Lifecycle.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include "Runtime/Reflection/ReflectionRegistry.h"
#include "NextGameplay/GameplayReflection/GameplayReflectionRegistry.h"
#include "Runtime/Subsystems/AI/AIService.h"
#include "Runtime/Subsystems/Audio/Audio.h"
#include "Runtime/Subsystems/Physics/Physics.h"
#include "Runtime/Subsystems/Animation/Animation.h"
#include "Runtime/Subsystems/Scripting/QuickJSEngine.h"
#include "Runtime/Subsystems/Localization/LocalizationService.h"
#include "Runtime/Subsystems/Voice/VoiceInputService.h"
#include "Runtime/Subsystems/Tasks/TaskCoordinator.h"
#include "Runtime/Config/CVarSystem.h"
#include "Runtime/Config/EngineCVars.h"
#include "Vulkan/Core/Device.h"
#include "Vulkan/Core/Instance.h"
#include "Vulkan/Core/SwapChain.h"
#include "Vulkan/Core/WindowSurface.h"
#include "Rendering/Base/VulkanBaseRenderer.h"
// ... 等
```

#### Engine_Window.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include "Vulkan/Core/WindowSurface.h"
// 极少其它依赖
```

#### Engine_ScreenShot.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include "Runtime/ScreenShot.h"
#include "Vulkan/Core/SwapChain.h"
#include "Rendering/Base/VulkanBaseRenderer.h"
```

#### Engine_Renderer.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include "Rendering/Base/VulkanBaseRenderer.h"
#include "Rendering/Base/RayTraceBaseRenderer.h"
#include "Vulkan/Core/Device.h"
#include "Vulkan/Pipeline/ShaderHotReloader.h"
#include "Vulkan/Sync/GpuTimer.h"
#include "Vulkan/RayTracing/...h"  // 按需
```

#### Engine_Input.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include <SDL3/SDL.h>
#include "Runtime/UI/UserInterface.h"  // ImGui IO
```

#### Engine_HotReload.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include "Vulkan/Pipeline/ShaderHotReloader.h"
#include "Rendering/Base/VulkanBaseRenderer.h"
```

#### Engine_SceneLoad.cpp
```cpp
#include "Common/CoreMinimal.h"
#include "Runtime/Engine.h"
#include "Runtime/Scene/SceneList.h"
#include "Runtime/Scene/SceneBuilder.h"
#include "Runtime/Subsystems/Tasks/TaskCoordinator.h"
#include "Assets/Core/Scene.h"
#include "Assets/Loaders/FSceneLoader.h"
```

### 3.4 残留 Engine.cpp

剩余内容应包含：
- 构造 `NextEngine::NextEngine(...)` / 析构 `~NextEngine()`
- `Tick()` 主循环（如果有）
- `RegisterJSCallback` / `AddTimerTask` 这类小 glue
- 静态全局 `NextEngine::GetInstance()` 实现（如果在 cpp 里）
- 不归任何分组的 1-3 个方法

### 3.5 SourceFiles.cmake / UNITY_BUILD

`src_files_engine` GLOB 自动包含 7 个新 cpp。无需改 cmake。

但 UNITY_BUILD 可能把 7 个 Engine_*.cpp 合并到同一 unity batch，导致 anonymous namespace 冲突或多重定义。**测试一下**：

```bash
./gnb build --reconfigure
# 如果遇到 multiple definition / redeclaration in unity batch
# 解决方案：
# 1) 把所有 Engine_*.cpp 设置 SKIP_UNITY_BUILD_INCLUSION
set_source_files_properties(
    Runtime/Engine.cpp
    Runtime/Engine_Lifecycle.cpp
    Runtime/Engine_Window.cpp
    Runtime/Engine_ScreenShot.cpp
    Runtime/Engine_Renderer.cpp
    Runtime/Engine_Input.cpp
    Runtime/Engine_HotReload.cpp
    Runtime/Engine_SceneLoad.cpp
    PROPERTIES UNITY_BUILD_BATCH_SIZE 1)
# 或更激进：SKIP_UNITY_BUILD_INCLUSION ON
```

加在 `src/CMakeLists.txt` 的 UNITY_GROUP 设置之后。

### 3.6 不动 Engine.h

`Engine.h` **一行不改**。这是本 phase 的硬性约束。如果 codex 觉得"为了 helper 函数得加 friend / namespace"，**停下来 PR 描述里说明**，由人类决定。

---

## 4. 验收门

```bash
# 1. 8 个 cpp 都存在
for f in Engine.cpp Engine_Lifecycle.cpp Engine_Window.cpp Engine_ScreenShot.cpp \
         Engine_Renderer.cpp Engine_Input.cpp Engine_HotReload.cpp Engine_SceneLoad.cpp; do
  test -f src/Runtime/$f || echo "MISSING $f"
done

# 2. 残留 Engine.cpp 净大小
wc -l src/Runtime/Engine.cpp
# 期望: ≤ 250

# 3. 所有 NextEngine:: 方法定义总数不变
grep -c '^[a-zA-Z].*NextEngine::' src/Runtime/Engine*.cpp
# 期望: 与拆分前相同（约 32 个方法）

# 4. Engine.h 未改动
git diff --stat src/Runtime/Engine.h
# 期望: 0 行变更

# 5. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] 8 个 cpp 文件都存在且各 100~400 LoC（残留 Engine.cpp ≤ 250）
- [ ] `Engine.h` 一字未改
- [ ] 全工程 `grep '^void NextEngine::\|^bool NextEngine::\|^int NextEngine::'` 在 Engine*.cpp 中的总匹配数 = 拆分前
- [ ] 没有任何 Engine_X.cpp 重复定义同一方法
- [ ] 匿名 namespace helper 已合理归位，无未引用的死代码
- [ ] 静态变量唯一性保证（grep `^static \w` 在 Engine*.cpp 数量合理）
- [ ] UNITY_BUILD 仍能跑（即便要把 Engine_*.cpp 排除出 unity）
- [ ] PR 标题：`refactor(phase-08): Engine.cpp 拆分为 6 个伴生 cpp`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| Unity Build 把 7 个伴生 cpp 合并 → anonymous namespace 冲突 | §3.5 提供 SKIP_UNITY_BUILD 方案 |
| 某个静态变量 `gXxx` 被多个原本属于不同分组的方法用 → 拆分后失联 | 把该静态变量保留在残留 `Engine.cpp`，在新 cpp 用 `extern` 声明（仅工程内部，不暴露到头） |
| 一个方法跨多个分组职责（如 `OnRendererPostRender` 既是 Input 又是 Renderer） | 按"主要职责"归类（PostRender 主要是 Renderer 回调），在 PR 描述里说明决策 |
| 编译错误不易定位（拆分太多，每个 cpp 缺 include） | 编译 + 修 include 的循环；耐心，每个方法看其原本用了什么 |

回退：`git revert` 单一 squash commit。

## 7. 关于"为什么不动 Engine.h"

`Engine.h` 393 LoC，公共 API 已被全工程引用。本 phase 目标是降低**实现文件的体积与复杂度**，而非重新设计接口。接口收敛留给 [docs/plans/2026-05/engine-cleanup-and-unification.md](../../engine-cleanup-and-unification.md) 处理。

避免在本 phase 内同时改接口 + 拆实现，否则验收门难以满足且难以 review。
