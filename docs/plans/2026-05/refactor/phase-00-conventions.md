# Phase 00 · 重构宪法

> **目的：** 在动任何代码之前，把命名/include/命名空间规则定死。后续每个 phase 都引用本文件。本 phase 不动代码，只新增 1 份文档。
> **输入：** `README.md` 第 2 节顶层决策 D1~D12
> **输出：** `AGENT_GUIDE/refactor-conventions.md` 新文档；本 phase 文档底部勾完审查清单
> **预计 diff：** 0 source change；2 文档新增（本文 + AGENT_GUIDE 内）
> **预计耗时：** Codex 30 分钟

---

## 1. 任务

新建 `AGENT_GUIDE/refactor-conventions.md`，内容是本计划期间的所有约定的浓缩版（人类与 codex 都会查它）。该文件比本 phase 文档**更精炼**、不带阶段流程，仅是"规则"。

下面是该文件的**完整内容模板**，codex 直接 copy 到 `AGENT_GUIDE/refactor-conventions.md`：

```markdown
# Refactor Conventions (2026-05 大重构期间)

> 本文件由 `docs/plans/2026-05/refactor/` 大重构产出，作为重构期间所有源码改动的统一约束。
> 重构完成后并入 `AGENT_GUIDE/coding-standards.md`，本文件归档。

## 1. 头文件扩展名
- 所有 C++ 头一律 **`.h`**。不允许 `.hpp` / `.hh` / `.hxx`。
- 唯一例外：`Common/CoreMinimal.h`（已统一）；`ThirdParty/` 内文件按上游格式不动。

## 2. 文件命名
- 文件名 **PascalCase**，与主类同名（`SwapChain.h` 内含 `class SwapChain`）。
- 一个文件**不超过两个公共类**；超过就拆。
- 不允许"And"-style 多职责文件名：禁止 `MemoryAndShader.cpp` `SyncAndTiming.h` 这类命名。

## 3. 文件夹命名
- 顶层模块文件夹：**PascalCase**（`Vulkan/` `Runtime/` `Application/` 等）。
- 模块内子文件夹：**PascalCase**（`Subsystems/Audio/`）。
- `cmake/` `assets/` `tools/` `docs/` 等非源码目录维持小写，不动。

## 4. include 风格
- 第一行（紧跟 license 注释或 `#pragma once` 之后）必须是 `#include "Common/CoreMinimal.h"`。
- 头路径**必须从 `src/` 起算**：`#include "Vulkan/Core/Device.h"` ✅；禁止相对路径 `#include "../Device.h"` ❌。
- 第三方头用尖括号：`#include <SDL3/SDL.h>` ✅。
- include 顺序：CoreMinimal → 同模块头 → 其他模块头 → 第三方 → 标准库。组与组之间空一行。

## 5. 命名空间
- 模块名作为顶层命名空间：`Vulkan`、`Assets`、`Runtime`、`NextGameplay`。
- 子模块用嵌套命名空间：`Vulkan::Memory`、`Vulkan::RayTracing`、`Runtime::Subsystems::Audio`。
- 禁止"项目方言"前缀作为命名空间：`NextRenderer::` `NextCVar::` `NextAI::` `NextUI::` 全部删除，移到对应模块命名空间内。
- 工具命名空间允许：`Utilities::FileHelper`、`Utilities::Math`。
- `.cpp` 内部辅助函数放进匿名命名空间 `namespace { ... }`，不污染模块命名空间。

## 6. 类内成员命名（不在本次重构范围）
- 现有 `.clang-tidy` 规则继续生效（`私有成员_` 后缀、PascalCase 函数名等）。
- 重构期间发现的违反 `.clang-tidy` 的存量代码**不修**，单独开 task。

## 7. 修改边界
- 重构 PR 不允许夹带功能改动、性能优化、bug 修复。
- 例外：纯路径/命名引发的"必要 grep 替换"算重构本身。
- 任何超出阶段文档描述的改动，必须在 PR 描述里单独列出并说明为什么避免不了。

## 8. 验收门
每个 phase 必须满足：
1. `./gnb build --reconfigure` 通过
2. `gkNextUnitTests` 100% 通过
3. `git status --porcelain` 干净
不达标不进入下一 phase。
```

---

## 2. 步骤

1. `mkdir -p AGENT_GUIDE`（已存在，等价 noop）
2. `Write` 创建 `AGENT_GUIDE/refactor-conventions.md`，内容如上。
3. **不修改任何代码**。

## 3. 验收门

- [ ] `AGENT_GUIDE/refactor-conventions.md` 存在且与上方模板内容一致
- [ ] `./gnb build --reconfigure` 通过（基线绿）
- [ ] `gkNextUnitTests` 100% 通过（基线绿）
- [ ] `git status --porcelain` 仅显示新增的 `AGENT_GUIDE/refactor-conventions.md` 一行（外加本 PR 修改的本计划文档）

## 4. 自我审查清单

- [ ] 是否新增了任何源码？应当**没有**。
- [ ] `refactor-conventions.md` 是否包含 8 个规则区段？
- [ ] 是否把规则文档放在 `AGENT_GUIDE/` 而不是 `docs/`？（位置很重要：`AGENT_GUIDE/` 是 codex 长程引用的地方）
- [ ] 编译 + 单测通过？
- [ ] PR 标题格式是否为 `refactor(phase-00): 制定重构宪法`？

## 5. 风险与回退

无风险。回退即 `git revert` 单一 commit。
