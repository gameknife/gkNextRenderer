# Phase 11 · 命名空间收敛 + CoreMinimal 强制

> **目的：** 把 7+ 套并存的命名空间方言（`NextRenderer::` `NextCVar::` `NextAI::` `NextUI::Painter` `NextPlatform::` 等）收敛到模块级；强制每个 .cpp / .h 首 include 是 `Common/CoreMinimal.h`。
> **依赖：** phase-10 完成
> **范围：** 全工程跨切面；约 400 文件首行检查 + ~15 个命名空间收敛
> **预计 diff：** ~250 文件 include 重排 + ~80 处命名空间改名

---

## 1. 命名空间方言现状

审计提到的非模块名命名空间：

| 命名空间 | 出现位置 | 应当归入 |
| --- | --- | --- |
| `NextRenderer::` | Application/Core/Renderer | `Application::Renderer::`（或直接删除，应用级不必命名空间） |
| `NextCVar::` | Runtime/Config | `Runtime::Config::` |
| `NextAI::` | Runtime/Subsystems/AI | `Runtime::Subsystems::AI::` |
| `NextUI::Scaling` | Runtime/UI（原 Editor） | `Runtime::UI::` |
| `NextUI::Painter` | Runtime/UI | `Runtime::UI::` |
| `NextUI::` | Runtime/UI 多处 | `Runtime::UI::` |
| `NextPlatform::` | Runtime/Platform | `Runtime::Platform::` |
| `NextEngineHelper`（无命名空间，全局函数） | Utilities/ | `Utilities::EngineHelper::` |
| `NextJson` | Utilities/JsonHelpers | `Utilities::Json::` |
| `Runtime::Command::SelectionUtils` | Runtime/Command | 可保留（`Runtime::Command::` 是合理嵌套） |
| `Runtime::Editor::` | 旧 Runtime/Editor → 已挪到 Runtime/UI | 改为 `Runtime::UI::` |
| **`Vulkan::LegacyDeferred`** | SoftwareModern 文件夹 | 应改为 `Vulkan::Pipelines::LegacyDeferred` 或 `Rendering::Pipelines::SoftwareModern`（语义疑问：审计提到命名空间方向反了） |
| **`Vulkan::ModernDeferred`** | SoftwareTracing 文件夹 | 同上方向疑问 |

> ⚠️ Vulkan::LegacyDeferred / ModernDeferred 与文件夹名"反向"是历史遗留。**本 phase 不强行修正命名空间**，因为可能涉及业务语义混淆。仅记录在 PR 描述里供后续决策。

---

## 2. 收敛规则

按 phase-00 conventions §5：

```
模块名作为顶层命名空间：Vulkan / Assets / Runtime / NextGameplay / Utilities / Application
子模块用嵌套命名空间：
  Vulkan::Memory        Vulkan::RayTracing
  Runtime::Subsystems::Audio   Runtime::UI
  Utilities::Json
项目方言前缀（NextXxx::）全部删除。
.cpp 内辅助函数放进匿名 namespace { ... }。
```

---

## 3. 步骤

### 3.1 命名空间改名映射

| 旧 namespace | 新 namespace |
| --- | --- |
| `namespace NextCVar` | `namespace Runtime::Config` |
| `namespace NextAI` | `namespace Runtime::Subsystems::AI` |
| `namespace NextUI` | `namespace Runtime::UI` |
| `namespace NextUI::Scaling` | `namespace Runtime::UI` |
| `namespace NextUI::Painter` | `namespace Runtime::UI` |
| `namespace NextPlatform` | `namespace Runtime::Platform` |
| `namespace NextJson` | `namespace Utilities::Json` |
| `namespace NextRenderer` | （应用级；删除命名空间，类放裸） |
| `namespace Runtime::Editor` | `namespace Runtime::UI` |

```bash
declare -A ns_subs=(
  ["namespace NextCVar"]="namespace Runtime::Config"
  ["namespace NextAI"]="namespace Runtime::Subsystems::AI"
  ["namespace NextUI::Scaling"]="namespace Runtime::UI"
  ["namespace NextUI::Painter"]="namespace Runtime::UI"
  ["namespace NextUI"]="namespace Runtime::UI"
  ["namespace NextPlatform"]="namespace Runtime::Platform"
  ["namespace NextJson"]="namespace Utilities::Json"
  ["namespace Runtime::Editor"]="namespace Runtime::UI"
)
for old in "${!ns_subs[@]}"; do
  new="${ns_subs[$old]}"
  grep -rl "$old" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|$old|$new|g"
done
find src/ -name '*.bak' -delete
```

### 3.2 限定符调用点改写

声明改完后，**调用点也要改**。例如 `NextUI::DrawSomething()` 改为 `Runtime::UI::DrawSomething()`：

```bash
declare -A use_subs=(
  ["NextCVar::"]="Runtime::Config::"
  ["NextAI::"]="Runtime::Subsystems::AI::"
  ["NextUI::"]="Runtime::UI::"
  ["NextPlatform::"]="Runtime::Platform::"
  ["NextJson::"]="Utilities::Json::"
)
for old in "${!use_subs[@]}"; do
  new="${use_subs[$old]}"
  grep -rl "$old" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|\b$old|$new|g"
done
find src/ -name '*.bak' -delete
```

> ⚠️ `NextEngine`（**类名**，非命名空间）**不改**。它是 NextEngine 这个类的实际名字。

### 3.3 NextRenderer:: 处理

`NextRenderer::` 多半是 `Application/Core/Renderer/` 内部包裹某些工具函数的命名空间。**删除该命名空间**，函数移到顶层（应用层不强求 namespace）。

- 找出 `namespace NextRenderer { ... }` 块
- 把内部内容提到外层文件作用域
- 更新调用点 `NextRenderer::Foo()` → `Foo()`（但要确保不与全局符号冲突）

如发现 NextRenderer 内有 ≥10 个函数（说明它确实是个工具空间），保留为 `Renderer` 命名空间（去掉"Next"）。

### 3.4 强制 CoreMinimal 首 include

**目标**：每个 `.cpp` / `.h` 在 `#pragma once`（仅 .h）后的第一个非空非注释行是 `#include "Common/CoreMinimal.h"`。

**例外**：
- `Common/CoreMinimal.h` 自身（不能 include 自己）
- `ThirdParty/` 不动

**实施**：

```bash
# 1) 列出所有 src/ 一手 cpp/h
find src -type f \( -name '*.cpp' -o -name '*.h' \) -not -path 'src/ThirdParty/*' > /tmp/files_to_check.txt

# 2) 写一个 Python 脚本检查每个文件，自动修复缺失
cat > /tmp/fix_coreminimal.py <<'PY'
import sys, re
from pathlib import Path
files = [Path(l.strip()) for l in open('/tmp/files_to_check.txt')]
for f in files:
    if f.name == 'CoreMinimal.h':
        continue
    text = f.read_text()
    lines = text.split('\n')
    # find first non-blank, non-comment, non-pragma line
    insert_at = 0
    in_block_comment = False
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith('//'):
            continue
        if stripped.startswith('/*'):
            in_block_comment = True
            if '*/' in stripped:
                in_block_comment = False
            continue
        if in_block_comment:
            if '*/' in stripped:
                in_block_comment = False
            continue
        if stripped.startswith('#pragma'):
            insert_at = i + 1
            continue
        # First substantive line
        insert_at = i
        break
    # Check if first include after insert_at is CoreMinimal
    if insert_at < len(lines) and 'CoreMinimal.h' in lines[insert_at]:
        continue
    # Insert
    lines.insert(insert_at, '#include "Common/CoreMinimal.h"')
    f.write_text('\n'.join(lines))
    print(f'fixed: {f}')
PY
python3 /tmp/fix_coreminimal.py
```

> 该脚本是 best-effort。复杂文件（多重 license 注释、跨平台条件 include）可能误判，**手工 review** PR 中所有"自动加入 CoreMinimal.h"的文件。

### 3.5 检查 include 顺序

phase-00 §4 规定 include 顺序：CoreMinimal → 同模块 → 其他模块 → 第三方 → 标准库。本 phase 不做严格顺序整理，**仅保证 CoreMinimal 是第一个**。完整顺序整理留给 phase-12。

### 3.6 SourceFiles.cmake / 建造系统

无变更。

### 3.7 文档更新

- `AGENT_GUIDE/refactor-conventions.md` 已规定（phase-00），无需改
- `AGENTS.md` Code Style 段落（namespace 部分）：检查现有描述与新规则一致

---

## 4. 验收门

```bash
# 1. 项目方言命名空间已清空
grep -rn 'namespace \(NextCVar\|NextAI\|NextUI\|NextPlatform\|NextJson\|NextRenderer\|Runtime::Editor\)\b' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行

# 2. 调用点也清空
grep -rn '\(NextCVar\|NextAI\|NextUI\|NextPlatform\|NextJson\)::' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行

# 3. CoreMinimal 首 include 比率
python3 << 'PY'
from pathlib import Path
files = [p for p in Path('src').rglob('*.cpp')] + \
        [p for p in Path('src').rglob('*.h') if p.name != 'CoreMinimal.h']
files = [p for p in files if 'ThirdParty' not in str(p)]
ok = bad = 0
for f in files:
    text = f.read_text()
    # 简化检查：CoreMinimal.h 在前 30 行内
    head = '\n'.join(text.split('\n')[:30])
    if 'CoreMinimal.h' in head:
        ok += 1
    else:
        bad += 1
        print(f'MISSING: {f}')
print(f'compliance: {ok}/{ok+bad} = {ok/(ok+bad)*100:.1f}%')
PY
# 期望: compliance ≥ 99%

# 4. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] 7 个项目方言命名空间已删除：grep 验证 0 行
- [ ] 调用点限定符全部更新
- [ ] CoreMinimal 首 include 合规率 ≥ 99%（剩余 1% 由 PR review 人工补救）
- [ ] 编译无 ambiguous reference 错误
- [ ] 单元测试通过
- [ ] PR 标题：`refactor(phase-11): 命名空间收敛 + CoreMinimal 强制`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| 命名空间改名后，标识符与模块内已有同名标识符冲突 | 编译器立即报错；按报错添加显式限定 `Runtime::Config::Foo` |
| `using namespace NextUI;` 这种放在 .cpp 顶部的 directive 漏改 | grep `using namespace Next` 一并改写 |
| 自动加 CoreMinimal 的脚本插入位置错误（如插到了类定义中间） | review PR diff，确认每个文件 CoreMinimal 在文件顶部、`#pragma once` 之后 |
| Vulkan::LegacyDeferred / ModernDeferred 命名方向问题 | **本 phase 不动**，记录在 PR 描述供未来评估 |
| TypeScript binding 里使用 `NextCVar::`（C++→JS 注册时拼字符串） | 检查 `Runtime/Subsystems/Scripting/QuickJSEngine_Bindings.cpp`；如有硬编码命名空间字符串，同步更新 |

回退：`git revert` 单一 squash commit。

## 7. 关于 NextEngine / NextGameplay 名字

- `class NextEngine` ── **类名**，不是命名空间，**不改**
- `namespace NextGameplay` ── **模块顶层命名空间**，按 phase-00 规则属于"模块名"。**保留**

这两个 "Next" 前缀**都不动**，这是产品 brand。仅"项目方言子命名空间"那种 `NextCVar::` `NextUI::` 才删。
