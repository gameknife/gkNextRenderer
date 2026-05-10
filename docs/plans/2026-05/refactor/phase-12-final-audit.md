# Phase 12 · 最终审计 + 文档同步 + 完整 visual test

> **目的：** 收尾。清扫死代码、同步 AGENTS.md / AGENT_GUIDE，跑一次完整 visual test 视觉对比，关闭整个重构窗口。
> **依赖：** phase-11 完成
> **范围：** 全工程；以查为主、改为辅
> **预计 diff：** 文档更新为主；少量死代码删除

---

## 1. 任务清单

### 1.1 全面静态扫描

```bash
# 1) 找出"似乎不再被引用"的头文件
for h in $(find src -name '*.h' -not -path 'src/ThirdParty/*'); do
  base=$(basename "$h" .h)
  # 该头是否还被任何 cpp/h include
  refs=$(grep -rln "\"$h\"\|/$base\.h\"" src/ --include='*.cpp' --include='*.h' | grep -v "$h" | wc -l)
  if [ "$refs" -eq 0 ]; then
    echo "ORPHAN: $h"
  fi
done | tee /tmp/orphan_headers.txt
```

人工 review `/tmp/orphan_headers.txt`，对每个孤儿头：
- **真孤儿** → `git rm`
- **属于公共 API 但用户在外部引用** → 保留并记录
- **是 phase 拆分时遗留的中间产物** → 删除并补 PR 描述

### 1.2 死代码扫描

```bash
# 找出未被调用的 static / 内部函数
# 借助 -Wunused-function 编译警告
./gnb build --reconfigure 2>&1 | grep -E 'unused (function|variable)' > /tmp/unused.txt
```

> 项目用 `-Werror`（`src/CMakeLists.txt:382`），所以 unused 警告其实会变 error。这一步如果没东西可看，说明编译已干净，good。

### 1.3 命名空间最终验证

```bash
# 列出工程内所有顶层命名空间，确认仅含规划的几个
grep -rh '^namespace [A-Z]' src/ --include='*.h' --include='*.cpp' \
  | grep -v 'ThirdParty' \
  | sed 's/{//' | sort -u
```

期望仅看到（最多）：
```
namespace Application
namespace Assets
namespace NextGameplay
namespace Runtime
namespace Utilities
namespace Vulkan
```

加上 anonymous `namespace { ... }` 不会被这个 grep 捕获。

### 1.4 头扩展名兜底

```bash
find src -name '*.hpp' -not -path 'src/ThirdParty/*' | wc -l
# 期望: 0
```

### 1.5 include 路径兜底

```bash
# 不应再有任何相对路径 ../ include
grep -rn '#include "[.][.]/' src/ --include='*.cpp' --include='*.h' \
  | grep -v 'ThirdParty'
# 期望: 0 行
```

### 1.6 CMake 健康度

```bash
# SourceFiles.cmake 中的 GLOB 模式仍然合法
grep -E '\*.hpp' src/cmake/SourceFiles.cmake
# 仍存在但不应再匹配任何文件，可以决定是否清理
```

如确认 0 hpp 引用，可在本 phase **删除** SourceFiles.cmake 中所有 `"*.hpp"` 模式条目。

### 1.7 文档同步

#### AGENTS.md

更新以下章节：

- **Architecture Overview** 段（第 70 行附近）：目录树整体替换为 README.md §3 的目标结构
- **Subsystems** 列表：列出新的 9 个子目录而非旧的扁平文件
- **Key References** 段：保留指向 AGENT_GUIDE 的链接

#### AGENT_GUIDE/

按目录扫描，更新以下文件中所有过期路径引用：

```bash
grep -rln 'Runtime/Editor\|Runtime/Utilities\|Vulkan/MemoryAndShader\|Vulkan/SyncAndTiming\|Vulkan/GpuResources\|src/Editor/\|Application/MagicaLego\|Application/Brotato3D\|Application/gkNextRenderer\|Application/gkNextBenchmark\|Application/gkNextVisualTest\|NextGameplay/Components\|NextGameplay/Reflection\|NextGameplay/Utilities\|Subsystems/NextAudio\|Subsystems/NextPhysics\|Subsystems/NextAnimation\|Subsystems/NextLocalization\|Subsystems/NextCharacterController\|Subsystems/NextPhysicsTypes' AGENT_GUIDE/ docs/ README.en.md README.md
```

逐一更新或在 PR 描述里说明为什么不改（历史快照类文档可不动）。

#### docs/CMAKE_STRUCTURE.md

按新结构整体更新。

### 1.8 把 refactor-conventions.md 并入 coding-standards.md

phase-00 创建的 `AGENT_GUIDE/refactor-conventions.md` 是临时文件。本 phase 的最后一步：

1. 把其内容并入 `AGENT_GUIDE/coding-standards.md`（追加到合适章节）
2. `git rm AGENT_GUIDE/refactor-conventions.md`

### 1.9 完整 visual test 对比

```bash
./out/build/macos-arm64/bin/gkNextVisualTest
# 检查 output 截图是否与基线一致（人类辅助）
```

如出现回归（比如 phase-08 拆分 Engine.cpp 时漏移了某个静态变量），定位并修复。

### 1.10 跑一次实际 game / editor

```bash
./gnb run                       # gkNextRenderer 主程序
./gnb editor                    # gkNextEditor
./out/build/macos-arm64/bin/Brotato3D
./out/build/macos-arm64/bin/MagicaLego
```

人工目视确认：
- 无窗口创建失败
- 主菜单 / 场景加载正常
- ImGui 编辑器面板可点击 / Gizmo 能拖拽

---

## 2. 不在本 phase 范围内（但应记录到下一波计划）

- 应用侧 god-UI 拆分（Brotato3DUI.cpp 2039L 等）—— 留给游戏作者
- Vulkan::LegacyDeferred / ModernDeferred 命名空间方向纠正 —— 需要业务讨论
- 真正的 RHI 抽象基类（D3D12 / Metal 后端）—— 长期工作
- 引擎层 API 净化（删 façade、收敛重载）—— 由 [`engine-cleanup-and-unification.md`](../engine-cleanup-and-unification.md) 接续
- per-module CMake 拆分 —— D6 已决定不做；如未来需要，由 [`build-system-rework.md`](../build-system-rework.md) 接手

把上述列入 **`docs/plans/2026-05/refactor/FOLLOWUP.md`**（phase 12 的第二个产出文件），作为重构窗口关闭后的"待办"。

---

## 3. 验收门

```bash
# 1. 0 .hpp / 0 相对 include / 0 项目方言命名空间
find src -name '*.hpp' -not -path 'src/ThirdParty/*' | wc -l        # 0
grep -rn '#include "[.][.]/' src/ --include='*.cpp' --include='*.h' \
  | grep -v 'ThirdParty' | wc -l                                    # 0
grep -rn 'namespace \(NextCVar\|NextAI\|NextUI\|NextPlatform\|NextJson\|NextRenderer\)\b' src/ \
  --include='*.cpp' --include='*.h' | wc -l                         # 0

# 2. 编译 + 单测 + visual test
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
./out/build/macos-arm64/bin/gkNextVisualTest

# 3. 手动启动验证
./gnb run
./gnb editor
# 人类目视：编辑器主面板 / 场景视口正常

# 4. 文档同步
# - AGENTS.md 目录树为新结构
# - AGENT_GUIDE/* 无过期路径
# - docs/CMAKE_STRUCTURE.md 同步
# - AGENT_GUIDE/refactor-conventions.md 已合并到 coding-standards.md 并删除

# 5. FOLLOWUP.md 已生成
test -f docs/plans/2026-05/refactor/FOLLOWUP.md
```

---

## 4. 自我审查清单

- [ ] 孤儿头文件全部决策（删 / 留 / 标注）
- [ ] AGENTS.md 目录树更新
- [ ] AGENT_GUIDE 内 0 过期路径
- [ ] docs/CMAKE_STRUCTURE.md 同步
- [ ] refactor-conventions.md 已并入 coding-standards.md 并删除
- [ ] visual test 截图对比无回归
- [ ] 4 个手动启动 (gkNextRenderer / gkNextEditor / Brotato3D / MagicaLego) 均正常
- [ ] FOLLOWUP.md 已生成，列出后续工作
- [ ] PR 标题：`refactor(phase-12): 最终审计 + 文档同步 + 关闭重构窗口`

## 5. 风险与回退

| 风险 | 应对 |
| --- | --- |
| visual test 发现细微渲染差异（光照、相机偏移） | 优先怀疑 phase-08 Engine.cpp 拆分时静态变量没正确归位；定位修复 |
| 编辑器某面板崩溃 | 怀疑 phase-10 RHI 门面遗漏某个 hook；定位修复 |
| 文档同步过程中误删了仍有效的指引 | 文档改动**单独 commit**，便于精细 revert |
| FOLLOWUP.md 写得太散漫 | 限制 ≤ 200 LoC，分 5 个清晰子项 |

回退：`git revert` 本 phase 单一 squash commit；前 11 个 phase 的成果不受影响。

---

## 6. 关闭重构窗口

PR 合入 main 后：

1. 在 README.md 第 9 节"启动检查"上方加一段："**重构于 2026-XX-XX 完成**"
2. 把整个 `docs/plans/2026-05/refactor/` 标记为已完成（不删除，作为历史快照）
3. 通知所有正在或将要提 PR 的人**新结构生效**，并发链接到 `AGENT_GUIDE/coding-standards.md` 的更新章节
4. 庆祝：91→13 个目录、3 个 god-file 拆完、2 个模块边界澄清，下一个新人能 1 小时摸到代码骨架而不是 1 周

---

## 7. 给 codex 的最后提示

本 phase 是"以查为主、改为辅"的收尾，与前 11 个 phase 的工作量相当或更小。但**最不可省略**：前面任何遗漏在这里都会被暴露出来。

如果某个验收命令报错，**不要硬塞 SUCCESS 给人类**。停下来，把错误贴出来请求决策。
