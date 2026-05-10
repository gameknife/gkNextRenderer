# Phase 06 · Rendering 重组 + VulkanBaseRenderer 拆分

> **目的：** Rendering 顶层从 2 base + 4 子目录的扁平摆放 → `Base/` + `Common/` + `Pipelines/`；同时拆分 1943 LoC 的 `VulkanBaseRenderer.cpp`。
> **依赖：** phase-05 完成
> **范围：** `src/Rendering/`
> **预计 diff：** ~10 文件移动 + `VulkanBaseRenderer.cpp` 拆为 4~5 个 cpp（header 不动）

---

## 1. 当前状况

```
src/Rendering/
├─ RayTraceBaseRenderer.{cpp,h}   # 477 / -
├─ VulkanBaseRenderer.{cpp,h}     # 1943 / 大头文件
├─ PathTracing/                   # 2 文件
├─ PipelineCommon/                # 2 文件
├─ SoftwareModern/                # 2 文件
└─ SoftwareTracing/               # 2 文件
```

问题：
- 基类与具体管线平起平坐
- `VulkanBaseRenderer.cpp` 是 1943 LoC god-file
- `SoftwareModern` / `SoftwareTracing` 命名分层不清

---

## 2. 目标结构

```
src/Rendering/
├─ Base/
│  ├─ VulkanBaseRenderer.{cpp,h}                   # header 不动
│  ├─ VulkanBaseRenderer_Init.cpp                  # ← 拆出
│  ├─ VulkanBaseRenderer_FrameLoop.cpp             # ← 拆出
│  ├─ VulkanBaseRenderer_Resources.cpp             # ← 拆出
│  ├─ VulkanBaseRenderer_Streamline.cpp            # ← 拆出（Streamline / DLSS）
│  ├─ RayTraceBaseRenderer.{cpp,h}
│  └─ RayTraceBaseRenderer_AS.cpp                  # ← 加速结构相关，可选拆
├─ Common/                          # ← 即原 PipelineCommon
└─ Pipelines/
   ├─ PathTracing/
   ├─ SoftwareModern/
   └─ SoftwareTracing/
```

> 命名收益：
> - `Base/` 一目了然这是基础设施
> - `Pipelines/` 表明这是可插拔的渲染管线
> - `Common/` 比 `PipelineCommon/` 更短、与 `Base/` `Pipelines/` 同级整齐

---

## 3. 步骤

### 3.1 创建 Base/ + 移动基类

```bash
cd src/Rendering
mkdir -p Base
git mv VulkanBaseRenderer.cpp     Base/
git mv VulkanBaseRenderer.h       Base/  # phase-01 已是 .h
git mv RayTraceBaseRenderer.cpp   Base/
git mv RayTraceBaseRenderer.h     Base/
```

### 3.2 改名 PipelineCommon → Common

```bash
git mv PipelineCommon Common
```

### 3.3 创建 Pipelines/ 与移入 3 个管线

```bash
mkdir -p Pipelines
git mv PathTracing      Pipelines/
git mv SoftwareModern   Pipelines/
git mv SoftwareTracing  Pipelines/
```

### 3.4 改写 include 映射表

| 旧 | 新 |
| --- | --- |
| `"Rendering/VulkanBaseRenderer.h"` | `"Rendering/Base/VulkanBaseRenderer.h"` |
| `"Rendering/RayTraceBaseRenderer.h"` | `"Rendering/Base/RayTraceBaseRenderer.h"` |
| `"Rendering/PipelineCommon/<X>.h"` | `"Rendering/Common/<X>.h"` |
| `"Rendering/PathTracing/<X>.h"` | `"Rendering/Pipelines/PathTracing/<X>.h"` |
| `"Rendering/SoftwareModern/<X>.h"` | `"Rendering/Pipelines/SoftwareModern/<X>.h"` |
| `"Rendering/SoftwareTracing/<X>.h"` | `"Rendering/Pipelines/SoftwareTracing/<X>.h"` |

```bash
declare -A subs=(
  ["Rendering/VulkanBaseRenderer.h"]="Rendering/Base/VulkanBaseRenderer.h"
  ["Rendering/RayTraceBaseRenderer.h"]="Rendering/Base/RayTraceBaseRenderer.h"
  ["Rendering/PipelineCommon/"]="Rendering/Common/"
  ["Rendering/PathTracing/"]="Rendering/Pipelines/PathTracing/"
  ["Rendering/SoftwareModern/"]="Rendering/Pipelines/SoftwareModern/"
  ["Rendering/SoftwareTracing/"]="Rendering/Pipelines/SoftwareTracing/"
)
for old in "${!subs[@]}"; do
  new="${subs[$old]}"
  grep -rl "\"$old" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|\"$old|\"$new|g"
done
find src/ -name '*.bak' -delete
```

### 3.5 拆分 VulkanBaseRenderer.cpp

**原则：仅按"自然方法分组"拆，header 一行不动**。

读 `Rendering/Base/VulkanBaseRenderer.cpp`，按方法名分配到 4 个伴生 cpp：

| 伴生 cpp | 包含的方法（按方法名前缀分类） |
| --- | --- |
| `VulkanBaseRenderer.cpp` | 构造/析构、`Run()`、各 setter / getter、其他不归类的小函数 |
| `VulkanBaseRenderer_Init.cpp` | `CreateXxx*()` `Setup*()`（设备 / 实例 / swapchain 初始化） |
| `VulkanBaseRenderer_FrameLoop.cpp` | `DrawFrame()` `RenderFrame*()` `Submit*()` `Present*()` |
| `VulkanBaseRenderer_Resources.cpp` | `LoadScene()` 资源上传 / 描述符更新 / 缓冲创建 |
| `VulkanBaseRenderer_Streamline.cpp` | 所有 `#if WITH_STREAMLINE` 包裹的方法（DLSS / Reflex 集成） |

**实施步骤**（严格按顺序）：

1. **读** `Rendering/Base/VulkanBaseRenderer.cpp`，列出每个方法签名 + 其行号区间
2. **分类**：codex 自己判断每个方法归哪个伴生 cpp
3. **新建** 4 个伴生 cpp，每个文件顶部写：
   ```cpp
   #include "Common/CoreMinimal.h"
   #include "Rendering/Base/VulkanBaseRenderer.h"
   // 该文件相关 Vulkan 头
   ```
4. **剪切粘贴**方法实现（`Vulkan::VulkanBaseRenderer::XxxMethod()` 这样的定义）
5. 原 `VulkanBaseRenderer.cpp` 仅保留构造/析构 + 不归类的小函数，**净剩 ≤ 500 LoC**
6. 任何**匿名命名空间内的辅助函数 / 静态变量** → 跟随主用方调用方一起搬过去；如果被多个文件用，就**搬到 header 的 detail 命名空间或 .cpp 都共享的 namespace**

> ⚠️ **不要修改方法体**！只剪切粘贴。任何"顺手优化"立即停止。

### 3.6 关于 RayTraceBaseRenderer

`RayTraceBaseRenderer.cpp` 477 LoC，**不拆**。本 phase 只移动到 `Base/`。

### 3.7 SourceFiles.cmake

`src_files_rendering` GLOB `Rendering/*.cpp Rendering/*.hpp`，自动覆盖子目录新文件。无需改。

但 UNITY_BUILD 中（`src/CMakeLists.txt:274`）：

```cmake
set_source_files_properties(${src_files_rendering} PROPERTIES UNITY_GROUP "rendering")
```

`src_files_rendering` 仍是 GLOB 结果，自动包含新加的 4 个伴生 cpp。无需改。

但 unity batch 大小可能需要调整（拆分后 rendering 文件数从 6 → 14）。**不动**，让 cmake 自动批次化。

### 3.8 Streamline 平台条件

`src/CMakeLists.txt` 第 302 行附近：

```cmake
if ( WITH_STREAMLINE )
    target_compile_definitions(${target} PUBLIC WITH_STREAMLINE=1)
    target_include_directories(${target} PRIVATE ${STREAMLINE_INCLUDE_DIR})
    target_link_directories(${target} PRIVATE ${STREAMLINE_LIB_DIR})
    target_link_libraries(${target} PRIVATE sl.interposer)
endif()
```

`VulkanBaseRenderer_Streamline.cpp` 内的代码全部包在 `#if WITH_STREAMLINE` 内即可，与之前一致。

### 3.9 检查 Application 内对 Rendering 的引用

应用层（`Application/gkNextRenderer/` `Application/gkNextEditor/` 等）有引用 `Rendering/VulkanBaseRenderer.h`，phase 通用 sed 已覆盖。但还要 grep 一下 `Application/` 目录确认。

```bash
grep -rn '"Rendering/' src/Application/ --include='*.cpp' --include='*.h'
```

确认所有引用已更新。

---

## 4. 验收门

```bash
# 1. 顶层 Rendering 仅含子目录
ls src/Rendering/*.cpp src/Rendering/*.h 2>/dev/null  # 期望: 输出为空

# 2. 4 个新拆分文件存在
for f in VulkanBaseRenderer_Init.cpp VulkanBaseRenderer_FrameLoop.cpp \
         VulkanBaseRenderer_Resources.cpp VulkanBaseRenderer_Streamline.cpp; do
  test -f src/Rendering/Base/$f || echo "MISSING: $f"
done
# 期望: 无 MISSING

# 3. VulkanBaseRenderer.cpp 净大小 ≤ 500 LoC
wc -l src/Rendering/Base/VulkanBaseRenderer.cpp
# 期望: ≤ 500

# 4. 旧路径 0 残留
grep -rn '"Rendering/\(VulkanBaseRenderer\|RayTraceBaseRenderer\|PipelineCommon\|PathTracing\|SoftwareModern\|SoftwareTracing\)\b' src/ --include='*.cpp' --include='*.h' | grep -v '/Base/\|/Common/\|/Pipelines/'
# 期望: 0 行

# 5. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] `Rendering/Base/` `Rendering/Common/` `Rendering/Pipelines/` 三个子目录就绪
- [ ] `VulkanBaseRenderer.cpp` 已拆为 5 个 cpp
- [ ] `VulkanBaseRenderer.h` 一行未改
- [ ] 拆分后所有方法仍是 `Vulkan::VulkanBaseRenderer::X` 完整定义（grep `^Vulkan::VulkanBaseRenderer::` 数量 = 拆分前数量）
- [ ] `#if WITH_STREAMLINE` 仅出现在 `_Streamline.cpp` 内
- [ ] Streamline preset 仍能配置（Windows 上）
- [ ] PR 标题：`refactor(phase-06): Rendering 重组 + VulkanBaseRenderer 拆分`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| 拆分时漏掉某个静态变量，导致重定义错误 | 编译会立即报错；移到 anonymous namespace 或 header 解决 |
| 匿名命名空间函数被多文件复用 | 把它移到 .h 的 `namespace detail` 或保留在主 cpp 让别人 extern |
| Unity Build 在拆分后批次过大引起 OOM（rendering 14 文件全合并） | unity_batch_size 已设 12（`src/CMakeLists.txt:269`），保持；如果 OOM 调小 |
| 切换到 Linux/Windows 编译时方法分配不同（如 `_Streamline` 在非 Win 时为空文件） | 空文件没问题；保留即可 |

回退：`git revert` 单一 squash commit。

## 7. 关于命名

> 审计提到 `SoftwareModern`（命名空间 `Vulkan::LegacyDeferred`）/ `SoftwareTracing`（命名空间 `Vulkan::ModernDeferred`）的命名空间与文件夹名"反向"。

**本 phase 不动这个**。命名空间统一在 phase-11 处理。这里只动文件夹路径。
