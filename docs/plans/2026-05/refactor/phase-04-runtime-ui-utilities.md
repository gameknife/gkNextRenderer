# Phase 04 · Runtime/Editor → Runtime/UI；Utilities 上移与拆分

> **目的：** 把"通用 ImGui 工具层"从 `Runtime/Editor/` 改名 `Runtime/UI/`；把 `Runtime/Utilities/` 内容按性质分流到 `Utilities/` 与 `Runtime/UI/Overlays/`。
> **依赖：** phase-03 完成
> **范围：** `src/Runtime/Editor/` `src/Runtime/Utilities/` `src/Utilities/`
> **预计 diff：** ~20 文件移动；改写 ~80 处 include

---

## 1. 当前状况

### 1.1 `src/Runtime/Editor/` ── 14 文件

```
ConsoleLogBuffer.{cpp,h}
FontLoader.{cpp,h}
GizmoController.{cpp,h}
ImGuiPainter.{cpp,h}
ImGuiScaling.{cpp,h}
NotificationCenter.{cpp,h}
UserInterface.{cpp,h}
```

实际是**通用的 ImGui 工具与运行时 UI 基础**，不是编辑器主程序。命名误导。

### 1.2 `src/Runtime/Utilities/` ── 9 文件

```
NextEngineHelper.{cpp,h}        # 投影/反投影、ImGui 集成
JsonHelpers.{cpp,h}             # JSON 序列化 helper
GraphicsDebugPanel.{cpp,h}      # 图形调试 ImGui 面板
PhysicsDebugOverlay.{cpp,h}     # 物理调试叠层
ProfileDebugOverlay.{cpp,h}     # 性能 profile 叠层
```

混了 2 类东西：**helpers**（不依赖 ImGui） 与 **debug overlay**（依赖 ImGui + 引擎）。

### 1.3 `src/Utilities/` ── 11 文件

```
Exception.{cpp,h}
FileHelper.{cpp,h}
Glm.h                           # 已是 .h
ImGui.h                         # 已是 .h
Localization.{cpp,h}
Math.h
StbImage.{cpp,h}
```

底层工具；无引擎依赖。

---

## 2. 目标结构

```
src/Runtime/UI/                  # 原 Runtime/Editor 改名
├─ ConsoleLogBuffer.{cpp,h}
├─ FontLoader.{cpp,h}
├─ GizmoController.{cpp,h}
├─ ImGuiPainter.{cpp,h}
├─ ImGuiScaling.{cpp,h}
├─ NotificationCenter.{cpp,h}
├─ UserInterface.{cpp,h}
└─ Overlays/                     # 原 Runtime/Utilities 中的 debug overlay
   ├─ GraphicsDebugPanel.{cpp,h}
   ├─ PhysicsDebugOverlay.{cpp,h}
   └─ ProfileDebugOverlay.{cpp,h}

src/Utilities/                   # 通用工具，吸收两个 helper
├─ Exception.{cpp,h}
├─ FileHelper.{cpp,h}
├─ Glm.h
├─ ImGui.h
├─ JsonHelpers.{cpp,h}           # ← Runtime/Utilities 迁入
├─ Localization.{cpp,h}
├─ Math.h
├─ NextEngineHelper.{cpp,h}      # ← Runtime/Utilities 迁入
└─ StbImage.{cpp,h}

src/Runtime/Utilities/           # ❌ 整个目录删除
src/Runtime/Editor/              # ❌ 整个目录删除
```

---

## 3. 步骤

### 3.1 迁移 Runtime/Editor → Runtime/UI

```bash
git mv src/Runtime/Editor src/Runtime/UI
```

> ⚠️ git 会把整个目录的所有文件识别为 rename。检查 `git status` 应该看到 14 个 R 行。

### 3.2 创建 UI/Overlays 并迁入 debug overlay

```bash
mkdir -p src/Runtime/UI/Overlays
git mv src/Runtime/Utilities/GraphicsDebugPanel.cpp     src/Runtime/UI/Overlays/
git mv src/Runtime/Utilities/GraphicsDebugPanel.h       src/Runtime/UI/Overlays/
git mv src/Runtime/Utilities/PhysicsDebugOverlay.cpp    src/Runtime/UI/Overlays/
git mv src/Runtime/Utilities/PhysicsDebugOverlay.h      src/Runtime/UI/Overlays/
git mv src/Runtime/Utilities/ProfileDebugOverlay.cpp    src/Runtime/UI/Overlays/
git mv src/Runtime/Utilities/ProfileDebugOverlay.h      src/Runtime/UI/Overlays/
```

### 3.3 迁移 helpers 到顶层 Utilities

```bash
git mv src/Runtime/Utilities/JsonHelpers.cpp       src/Utilities/
git mv src/Runtime/Utilities/JsonHelpers.h         src/Utilities/
git mv src/Runtime/Utilities/NextEngineHelper.cpp  src/Utilities/
git mv src/Runtime/Utilities/NextEngineHelper.h    src/Utilities/
```

### 3.4 删除空目录

```bash
rmdir src/Runtime/Utilities  # 应该已空
```

如果 `rmdir` 失败说明还有未迁文件，停下来检查。

### 3.5 改写 include 映射表

| 旧 include | 新 include |
| --- | --- |
| `"Runtime/Editor/ConsoleLogBuffer.h"` | `"Runtime/UI/ConsoleLogBuffer.h"` |
| `"Runtime/Editor/FontLoader.h"` | `"Runtime/UI/FontLoader.h"` |
| `"Runtime/Editor/GizmoController.h"` | `"Runtime/UI/GizmoController.h"` |
| `"Runtime/Editor/ImGuiPainter.h"` | `"Runtime/UI/ImGuiPainter.h"` |
| `"Runtime/Editor/ImGuiScaling.h"` | `"Runtime/UI/ImGuiScaling.h"` |
| `"Runtime/Editor/NotificationCenter.h"` | `"Runtime/UI/NotificationCenter.h"` |
| `"Runtime/Editor/UserInterface.h"` | `"Runtime/UI/UserInterface.h"` |
| `"Runtime/Utilities/GraphicsDebugPanel.h"` | `"Runtime/UI/Overlays/GraphicsDebugPanel.h"` |
| `"Runtime/Utilities/PhysicsDebugOverlay.h"` | `"Runtime/UI/Overlays/PhysicsDebugOverlay.h"` |
| `"Runtime/Utilities/ProfileDebugOverlay.h"` | `"Runtime/UI/Overlays/ProfileDebugOverlay.h"` |
| `"Runtime/Utilities/JsonHelpers.h"` | `"Utilities/JsonHelpers.h"` |
| `"Runtime/Utilities/NextEngineHelper.h"` | `"Utilities/NextEngineHelper.h"` |

```bash
declare -A subs=(
  ["Runtime/Editor/ConsoleLogBuffer.h"]="Runtime/UI/ConsoleLogBuffer.h"
  ["Runtime/Editor/FontLoader.h"]="Runtime/UI/FontLoader.h"
  ["Runtime/Editor/GizmoController.h"]="Runtime/UI/GizmoController.h"
  ["Runtime/Editor/ImGuiPainter.h"]="Runtime/UI/ImGuiPainter.h"
  ["Runtime/Editor/ImGuiScaling.h"]="Runtime/UI/ImGuiScaling.h"
  ["Runtime/Editor/NotificationCenter.h"]="Runtime/UI/NotificationCenter.h"
  ["Runtime/Editor/UserInterface.h"]="Runtime/UI/UserInterface.h"
  ["Runtime/Utilities/GraphicsDebugPanel.h"]="Runtime/UI/Overlays/GraphicsDebugPanel.h"
  ["Runtime/Utilities/PhysicsDebugOverlay.h"]="Runtime/UI/Overlays/PhysicsDebugOverlay.h"
  ["Runtime/Utilities/ProfileDebugOverlay.h"]="Runtime/UI/Overlays/ProfileDebugOverlay.h"
  ["Runtime/Utilities/JsonHelpers.h"]="Utilities/JsonHelpers.h"
  ["Runtime/Utilities/NextEngineHelper.h"]="Utilities/NextEngineHelper.h"
)
for old in "${!subs[@]}"; do
  new="${subs[$old]}"
  grep -rl "\"$old\"" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|\"$old\"|\"$new\"|g"
done
find src/ -name '*.bak' -delete
```

### 3.6 重点改写：Engine.cpp / Engine.h

`Engine.cpp` include 头部包含：

```cpp
#include "Runtime/Editor/UserInterface.hpp"          // → Runtime/UI/UserInterface.h
#include "Runtime/Editor/ConsoleLogBuffer.hpp"       // → Runtime/UI/ConsoleLogBuffer.h
#include "Runtime/Utilities/GraphicsDebugPanel.hpp"  // → Runtime/UI/Overlays/...
#include "Runtime/Utilities/PhysicsDebugOverlay.hpp" // 同上
#include "Runtime/Utilities/ProfileDebugOverlay.hpp" // 同上
```

phase-01 已经把 `.hpp` 全部改 `.h`，这里统一映射后：

```cpp
#include "Runtime/UI/UserInterface.h"
#include "Runtime/UI/ConsoleLogBuffer.h"
#include "Runtime/UI/Overlays/GraphicsDebugPanel.h"
#include "Runtime/UI/Overlays/PhysicsDebugOverlay.h"
#include "Runtime/UI/Overlays/ProfileDebugOverlay.h"
```

### 3.7 更新 src/Editor 内的 include

`src/Editor/` 与 `src/Runtime/Editor/` 是不同模块。`src/Editor/` 仍保留（phase-07 才会迁移到 `Application/Core/Editor/`）。`src/Editor/` 内部目前也 include 了 `Runtime/Editor/UserInterface.hpp`，本 phase 一并改写为 `Runtime/UI/UserInterface.h`。

### 3.8 SourceFiles.cmake

`src_files_engine` GLOB_RECURSE `Runtime/*.cpp Runtime/*.hpp Runtime/*.h` 自动覆盖新路径。无需改。

`src_files_utilities` GLOB `Utilities/*.cpp ...` 自动覆盖新进入的 4 个文件。无需改。

但 `src/Runtime/Utilities/` 已删除，`src/Runtime/Editor/` 重命名为 `UI/`，**确认 GLOB 不会因为目录消失出错**。CMake 重新配置时 GLOB 会重新扫描，应该 OK。

### 3.9 检查命名冲突

`src/Editor/EditorUi.hpp` ≠ `src/Runtime/Editor/UserInterface.hpp`，是不同文件。

但 `src/Editor/EditorInterface.hpp` 与原 `src/Runtime/Editor/UserInterface.hpp` 命名相近，**不会冲突**（路径不同）。

新 `Runtime/UI/UserInterface.h` 仍可能与未来某个 `Application/Core/Editor/UserInterface.h` 重名，但路径不同所以编译器不混淆。

### 3.10 文档更新

- `AGENTS.md` 第 70 行附近的目录树注释需要同步更新（如果列了 `Editor/`）。grep 一下。
- `AGENT_GUIDE/` 内任何提到 `Runtime/Editor/` 的地方按需替换。

---

## 4. 验收门

```bash
# 1. Runtime/Editor 与 Runtime/Utilities 不存在
test ! -d src/Runtime/Editor && test ! -d src/Runtime/Utilities && echo OK
# 期望: OK

# 2. UI/Overlays 各有 3 个 cpp + 3 个 h
ls src/Runtime/UI/Overlays/*.cpp | wc -l  # 期望 3
ls src/Runtime/UI/Overlays/*.h | wc -l    # 期望 3

# 3. Utilities 含新增 4 文件
ls src/Utilities/JsonHelpers.* src/Utilities/NextEngineHelper.* | wc -l  # 期望 4

# 4. 旧路径 0 引用
grep -rn '"Runtime/Editor/\|"Runtime/Utilities/' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行

# 5. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] `src/Runtime/Editor/` 和 `src/Runtime/Utilities/` 已不存在
- [ ] `src/Runtime/UI/` 含 14 个文件 + `Overlays/` 子目录
- [ ] `src/Utilities/` 共有 13 个文件（原 11 + 新 2 类 4 文件 = 15 ... 数实际）
- [ ] `Engine.cpp` 顶部 5 处旧 include 全部更新
- [ ] `src/Editor/` 内引用 `Runtime/Editor/` 的地方已更新
- [ ] `Tests/` 内若有引用旧路径已更新
- [ ] PR 标题：`refactor(phase-04): Runtime/Editor → Runtime/UI；Utilities 上移与拆分`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| 把 `NextEngineHelper.h` 上移到 `Utilities/` 后，其内部如果 include `Runtime/Engine.h` 形成循环风险 | 检查 NextEngineHelper.h 的 include；若它仅依赖 ImGui + glm，可以放心；若它需要 NextEngine 类型，保留在 `Runtime/Utilities/` 下层级，重新评估 |
| `Runtime/UI/UserInterface.h` 与 `src/Editor/EditorInterface.hpp` 类名混淆 | 不同类名，不会冲突 |
| AGENTS.md 里的目录树过期 | grep `Runtime/Editor` `Runtime/Utilities` 在 AGENTS.md / AGENT_GUIDE 里出现的地方，逐处更新 |

回退：`git revert` 单一 squash commit。

## 7. 决策提示

⚠️ §3.3 把 `NextEngineHelper.h` 移到 `Utilities/` 是**对的吗？**

它依赖 `ImGui.h` + 投影矩阵 + `NextEngine` 反向引用，现实里它属于"Engine 层的辅助函数"。如果 codex 在执行中发现 `NextEngineHelper.h` 实际依赖 `NextEngine`（即上游），把它放到 `Utilities/` 会形成循环，应当**保留在 Runtime/UI/Overlays/ 同级或单独 Runtime/Helpers/**。

**指令给 codex**：执行 §3.3 前，先 `cat src/Runtime/Utilities/NextEngineHelper.h` 检查依赖。若只依赖 GLM/ImGui，按计划上移；若依赖 NextEngine，**改放到 `src/Runtime/EngineHelpers.{cpp,h}`**（顶层）并在 PR 描述里说明偏离原因。
