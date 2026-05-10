# Phase 02 · Vulkan 子目录化与 helpers 拆分

> **目的：** Vulkan 模块从扁平堆叠改为 5 个职能子目录；同时拆分 3 个"And"-style god-helper 文件。
> **依赖：** phase-01 完成（所有头已是 `.h`）
> **范围：** `src/Vulkan/`（不动 `src/Vulkan/RayTracing/`，它已经是子目录且语义独立）
> **预计 diff：** ~25 文件移动 + 3 处源文件拆分；改写 ~200 处 include

---

## 1. 当前状况

`src/Vulkan/` 是 14 个文件平摊的扁平结构 + 1 个 `RayTracing/` 子目录。问题（来自 phase 0 调研）：

- `MemoryAndShader.cpp` 把 `DeviceMemory` 与 `ShaderModule` 拼在一起 → 命名诚实但职责分离
- `SyncAndTiming.h` 509 LoC，混 Fence/Semaphore + GPU/CPU 计时
- `GpuResources.h` 487 LoC，混 Image/ImageView/Buffer/Sampler/BufferView
- `DebugUtilities.{cpp,h}` 命名模糊，实际是 ValidationLayer + DebugMarker

---

## 2. 目标结构

```
src/Vulkan/
├─ Core/
│  ├─ Device.{cpp,h}
│  ├─ Instance.{cpp,h}
│  ├─ SwapChain.{cpp,h}
│  └─ WindowSurface.{cpp,h}
├─ Memory/
│  ├─ DeviceMemory.{cpp,h}      # 从 MemoryAndShader 拆出
│  ├─ Buffer.{cpp,h}            # 从 GpuResources 拆出
│  ├─ BufferUtil.h              # 现有头-only
│  ├─ Image.{cpp,h}             # 从 GpuResources 拆出（含 ImageView 头）
│  ├─ ImageView.{cpp,h}         # 从 GpuResources 拆出
│  └─ Sampler.{cpp,h}           # 从 GpuResources 拆出
├─ Pipeline/
│  ├─ RenderingPipeline.{cpp,h}
│  ├─ DescriptorSystem.{cpp,h}
│  ├─ ShaderModule.{cpp,h}      # 从 MemoryAndShader 拆出
│  ├─ ShaderHotReloader.{cpp,h}
│  └─ CommandExecution.{cpp,h}
├─ Sync/
│  ├─ Synchronization.{cpp,h}   # 从 SyncAndTiming 拆出（Fence/Semaphore）
│  └─ GpuTimer.{cpp,h}          # 从 SyncAndTiming 拆出（Timer/Metrics）
├─ Debug/
│  └─ Validation.{cpp,h}        # 即原 DebugUtilities
└─ RayTracing/                   # 不动
```

> 14 个原顶层文件 → 21 个新文件（净 +7，因为 3 处拆分各 +2~3，命名拆分各 +1）

---

## 3. 步骤

### 3.1 创建子目录

```bash
mkdir -p src/Vulkan/Core src/Vulkan/Memory src/Vulkan/Pipeline src/Vulkan/Sync src/Vulkan/Debug
```

### 3.2 git mv 直接挪动的 13 个文件

| 原路径 | 新路径 |
| --- | --- |
| `src/Vulkan/Device.{cpp,h}` | `src/Vulkan/Core/Device.{cpp,h}` |
| `src/Vulkan/Instance.{cpp,h}` | `src/Vulkan/Core/Instance.{cpp,h}` |
| `src/Vulkan/SwapChain.{cpp,h}` | `src/Vulkan/Core/SwapChain.{cpp,h}` |
| `src/Vulkan/WindowSurface.{cpp,h}` | `src/Vulkan/Core/WindowSurface.{cpp,h}` |
| `src/Vulkan/BufferUtil.h` | `src/Vulkan/Memory/BufferUtil.h` |
| `src/Vulkan/RenderingPipeline.{cpp,h}` | `src/Vulkan/Pipeline/RenderingPipeline.{cpp,h}` |
| `src/Vulkan/DescriptorSystem.{cpp,h}` | `src/Vulkan/Pipeline/DescriptorSystem.{cpp,h}` |
| `src/Vulkan/ShaderHotReloader.{cpp,h}` | `src/Vulkan/Pipeline/ShaderHotReloader.{cpp,h}` |
| `src/Vulkan/CommandExecution.{cpp,h}` | `src/Vulkan/Pipeline/CommandExecution.{cpp,h}` |
| `src/Vulkan/DebugUtilities.{cpp,h}` | `src/Vulkan/Debug/Validation.{cpp,h}` ⚠️ 文件**也改名** |

### 3.3 拆分 god-helpers

#### 3.3.1 MemoryAndShader → DeviceMemory + ShaderModule

`src/Vulkan/MemoryAndShader.h` 当前定义两个类。**拆分规则**：

1. 新建 `src/Vulkan/Memory/DeviceMemory.h`，把 `class DeviceMemory` 及其方法搬入
2. 新建 `src/Vulkan/Pipeline/ShaderModule.h`，把 `class ShaderModule` 及其方法搬入
3. `src/Vulkan/MemoryAndShader.cpp` 同步拆为 `Memory/DeviceMemory.cpp` + `Pipeline/ShaderModule.cpp`
4. 删除原 `MemoryAndShader.{cpp,h}`
5. 全工程 grep `#include "Vulkan/MemoryAndShader.h"`，按需替换为 `Vulkan/Memory/DeviceMemory.h` 或 `Vulkan/Pipeline/ShaderModule.h`（可能两个都需要）

#### 3.3.2 SyncAndTiming → Synchronization + GpuTimer

`src/Vulkan/SyncAndTiming.h`（509 LoC）实际包含：

- `Fence` `Semaphore` 类（同步原语）
- `GpuTimer` `ScopedTimer` 类（计时 + 性能 metrics）

**拆分规则**：

1. 新建 `src/Vulkan/Sync/Synchronization.h`，搬入 `Fence` `Semaphore`
2. 新建 `src/Vulkan/Sync/GpuTimer.h`，搬入 `GpuTimer` `ScopedTimer`
3. `SyncAndTiming.cpp` 拆为 `Synchronization.cpp` + `GpuTimer.cpp`
4. 删除原 `SyncAndTiming.{cpp,h}`
5. grep 替换：调用方按需 include 一个或两个新头

#### 3.3.3 GpuResources → Buffer + Image + ImageView + Sampler

`src/Vulkan/GpuResources.h`（487 LoC）实际包含 5 个类：`Image`、`ImageView`、`Buffer`、`Sampler`、`BufferView`。

**拆分规则**：

1. `src/Vulkan/Memory/Image.h` ← `class Image` + `class BufferView`（ImageView 单拆）
2. `src/Vulkan/Memory/ImageView.h` ← `class ImageView`
3. `src/Vulkan/Memory/Buffer.h` ← `class Buffer`
4. `src/Vulkan/Memory/Sampler.h` ← `class Sampler`
5. `GpuResources.cpp` 拆为对应 4 个 cpp
6. 删除原 `GpuResources.{cpp,h}`
7. grep 替换：原 `#include "Vulkan/GpuResources.h"` 按需替换为对应 1~4 个新头

> ⚠️ **拆分策略原则**：拆完后**类内成员、类签名、命名空间一概不动**。仅是物理拆分。包括 `friend` 关系、`forward declaration` 顺序，全部保留。

### 3.4 改写所有 #include

```bash
# 收集所有要替换的旧路径
# Vulkan 内部移动 + 拆分共影响 14 个旧 include 路径
# 用 sed 批量替换（一次一个旧路径，避免误伤）
```

替换映射表（codex 必须按表跑 sed）：

| 旧 include | 新 include |
| --- | --- |
| `"Vulkan/Device.h"` | `"Vulkan/Core/Device.h"` |
| `"Vulkan/Instance.h"` | `"Vulkan/Core/Instance.h"` |
| `"Vulkan/SwapChain.h"` | `"Vulkan/Core/SwapChain.h"` |
| `"Vulkan/WindowSurface.h"` | `"Vulkan/Core/WindowSurface.h"` |
| `"Vulkan/BufferUtil.h"` | `"Vulkan/Memory/BufferUtil.h"` |
| `"Vulkan/RenderingPipeline.h"` | `"Vulkan/Pipeline/RenderingPipeline.h"` |
| `"Vulkan/DescriptorSystem.h"` | `"Vulkan/Pipeline/DescriptorSystem.h"` |
| `"Vulkan/ShaderHotReloader.h"` | `"Vulkan/Pipeline/ShaderHotReloader.h"` |
| `"Vulkan/CommandExecution.h"` | `"Vulkan/Pipeline/CommandExecution.h"` |
| `"Vulkan/DebugUtilities.h"` | `"Vulkan/Debug/Validation.h"` |
| `"Vulkan/MemoryAndShader.h"` | （手工：替换为 `"Vulkan/Memory/DeviceMemory.h"` 或 `"Vulkan/Pipeline/ShaderModule.h"` 或两者） |
| `"Vulkan/SyncAndTiming.h"` | （手工：替换为 `"Vulkan/Sync/Synchronization.h"` 或 `"Vulkan/Sync/GpuTimer.h"` 或两者） |
| `"Vulkan/GpuResources.h"` | （手工：替换为 `"Vulkan/Memory/Image.h"` / `Buffer.h` / `Sampler.h` / `ImageView.h` 中需要的） |

**手工替换策略**：codex 对每个引用 `MemoryAndShader.h` / `SyncAndTiming.h` / `GpuResources.h` 的文件**单独处理**——

- 用 `grep -l '<old_header>' src/` 列出引用方
- 对每个引用方读其 .cpp，看用了哪些类
- 在 `#include` 处替换为对应新头（可能需要列出多个）

### 3.5 改 SourceFiles.cmake

`SourceFiles.cmake` 中 Vulkan 的 GLOB：

```cmake
file(GLOB_RECURSE src_files_vulkan
    "Vulkan/*.cpp"
    "Vulkan/*.hpp"  # 已无 .hpp，但保留通配符
)
```

`GLOB_RECURSE` 自动包含子目录，**无需修改**。

但 `src/CMakeLists.txt` 第 274 行 UNITY 分组：

```cmake
set_source_files_properties(${src_files_vulkan} PROPERTIES UNITY_GROUP "vulkan")
```

仍然按变量分组，**无需修改**。

### 3.6 检查跨模块 include 顺序

phase-00 宪法规定 include 顺序为：CoreMinimal → 同模块 → 其他模块 → 第三方 → 标准库。本 phase 移动 Vulkan 文件后，部分文件的"同模块"边界改了（比如原本 `MemoryAndShader.cpp` 的 include 块组现在归 `Memory/DeviceMemory.cpp`，它的"同模块"是 `Memory/`）。**本 phase 不强制重排 include 块**，phase-11 统一处理。

---

## 4. 验收门

```bash
# 1. Vulkan 顶层不再有源文件（仅有子目录）
ls src/Vulkan/*.cpp src/Vulkan/*.h 2>/dev/null
# 期望: 输出为空

# 2. 没有引用旧路径
grep -rn '"Vulkan/\(Device\|Instance\|SwapChain\|WindowSurface\|BufferUtil\|RenderingPipeline\|DescriptorSystem\|ShaderHotReloader\|CommandExecution\|DebugUtilities\|MemoryAndShader\|SyncAndTiming\|GpuResources\)\.h"' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行

# 3. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain  # 仅 staged 的本 PR 改动
```

---

## 5. 自我审查清单

- [ ] `src/Vulkan/` 顶层只有 5 个子目录 + RayTracing/，无 .cpp/.h
- [ ] 5 个新子目录文件数：Core 8、Memory 9、Pipeline 10、Sync 4、Debug 2（cpp + h 计数）
- [ ] `MemoryAndShader.{cpp,h}` `SyncAndTiming.{cpp,h}` `GpuResources.{cpp,h}` `DebugUtilities.{cpp,h}` 已不存在
- [ ] git diff 中所有 `R` (rename) 行都符合预期映射表；没有意外的 `D`+`A`（应当全部识别为 rename，得益于 git mv）
- [ ] 全工程 grep 旧路径返回 0
- [ ] 编译 + 单测通过
- [ ] UNITY_BUILD 仍生效（构建日志含 `Building CXX object .../Unity_*.cxx`）
- [ ] PR 标题：`refactor(phase-02): Vulkan 子目录化与 helpers 拆分`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| 拆分 god-helper 时漏掉某个 forward declaration / friend 关系 | 拆分前先列出原文件的所有类与互相依赖；拆完后逐一对照 |
| `RayTracing/` 内部仍 include `"Vulkan/SyncAndTiming.h"`（审计提到了） | 全工程 grep 时不会漏；按映射表替换 |
| 某个 cpp 同时用 `Image` 和 `Buffer`，拆分后需要 include 两个头 | 这是预期行为；codex 不要试图合并回去 |
| 移动后 `friend class` 找不到对方 | 这种情况意味着原文件类间耦合；记录在 PR 描述但不修，phase-11 整理命名空间时再处理 |

回退：`git revert` 单一 squash commit。
