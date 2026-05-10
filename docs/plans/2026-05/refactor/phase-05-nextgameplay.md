# Phase 05 · NextGameplay 子文件夹消歧

> **目的：** `NextGameplay/Components` `NextGameplay/Reflection` `NextGameplay/Utilities` 与 Runtime 同名子文件夹引发的视觉混淆 → 三个子文件夹改名。
> **依赖：** phase-04 完成
> **范围：** `src/NextGameplay/`（16 文件级别）+ 所有引用方
> **预计 diff：** ~14 文件移动 + ~30 处 include 改写

---

## 1. 当前状况

```
src/NextGameplay/
├─ AI/                    # NavGrid, PathFollower（游戏级寻路；与 Runtime/Subsystems/AI/AIService 不同义）
├─ Character/             # CharacterActor
├─ Components/            # CharacterAnimationComponent, CharacterControlComponent, ...
├─ Gameplay/              # GameplayMath.h, GameplayTypes.h
├─ Reflection/            # GameplayReflectionRegistry.{cpp,h}（仅注册器）
└─ Utilities/             # SceneNodeUtils.{cpp,h}（150L）
```

子文件夹 `Components`/`Reflection`/`Utilities` 和 `Runtime/` 同名，但：
- `NextGameplay/Components/` 是**游戏级 ECS 组件**（角色控制、AI 状态机），不是引擎组件
- `NextGameplay/Reflection/` 仅是**反射注册器**（40 LoC），不是反射框架本身
- `NextGameplay/Utilities/` 是 **场景节点 helper**，与 `Utilities/` 不同义

审计结论：无代码重复，但**视觉上让人误以为是重复**。

---

## 2. 目标结构

```
src/NextGameplay/
├─ AI/                            # 不动
├─ Character/                     # 不动
├─ Gameplay/                      # 不动（GameplayMath/GameplayTypes 是该模块的公共类型）
├─ GameComponents/                # ← Components 改名
├─ GameplayReflection/            # ← Reflection 改名
└─ Helpers/                       # ← Utilities 改名（见 §3.3 决策）
```

> 说明：
> - `GameComponents/` 而不是 `Components/`，让"游戏级组件"与"引擎组件"在文件路径上一目了然
> - `GameplayReflection/` 不叫 `ReflectionRegistry/`：保留 "Gameplay" 前缀强调它是该模块的注册器
> - `Helpers/` 而不是 `Utilities/`：避免与顶层 `Utilities/` 视觉重复；语义"helper"更轻

---

## 3. 步骤

### 3.1 git mv 子文件夹

```bash
cd src/NextGameplay
git mv Components GameComponents
git mv Reflection GameplayReflection
git mv Utilities Helpers
```

> git 应识别为 16 个文件的 rename。

### 3.2 改写 include

| 旧 | 新 |
| --- | --- |
| `"NextGameplay/Components/AIAgentComponent.h"` | `"NextGameplay/GameComponents/AIAgentComponent.h"` |
| `"NextGameplay/Components/CharacterAnimationComponent.h"` | `"NextGameplay/GameComponents/CharacterAnimationComponent.h"` |
| `"NextGameplay/Components/CharacterControlComponent.h"` | `"NextGameplay/GameComponents/CharacterControlComponent.h"` |
| `"NextGameplay/Components/CharacterGameplayComponent.h"` | `"NextGameplay/GameComponents/CharacterGameplayComponent.h"` |
| `"NextGameplay/Reflection/GameplayReflectionRegistry.h"` | `"NextGameplay/GameplayReflection/GameplayReflectionRegistry.h"` |
| `"NextGameplay/Utilities/SceneNodeUtils.h"` | `"NextGameplay/Helpers/SceneNodeUtils.h"` |

```bash
declare -A subs=(
  ["NextGameplay/Components/"]="NextGameplay/GameComponents/"
  ["NextGameplay/Reflection/"]="NextGameplay/GameplayReflection/"
  ["NextGameplay/Utilities/"]="NextGameplay/Helpers/"
)
for old in "${!subs[@]}"; do
  new="${subs[$old]}"
  grep -rl "\"$old" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|\"$old|\"$new|g"
done
find src/ -name '*.bak' -delete
```

> 注意：`grep -rl '"NextGameplay/Components/'` 会**精确匹配**前缀字符串，不会误伤 `Runtime/Components/` 等。

### 3.3 §3.3 决策提示：`Helpers/` vs `Scene/`

`SceneNodeUtils.cpp` 实际是"游戏侧场景查询"，更准确的命名可能是：

- **选项 A**：`NextGameplay/Helpers/SceneNodeUtils.h`（按 §2 计划，强调"杂项 helper"）
- **选项 B**：`NextGameplay/Scene/SceneNodeUtils.h`（强调"场景"）

按 §2 默认走 A。codex 若发现 SceneNodeUtils 内还有 1~2 个非 scene 相关函数，就保留 A；如果纯是场景遍历，可以**自由切换到 B** 并在 PR 描述说明。

### 3.4 更新反射注册调用点

`GameplayReflectionRegistry::RegisterGameplayReflection()` 在 `NextEngine::RegisterReflection()` 里调用（看 `Engine.cpp:72`）。本 phase **类名不变**，只是路径变。include 改写已覆盖。

### 3.5 SourceFiles.cmake

`src_files_nextgameplay` GLOB_RECURSE `NextGameplay/*.cpp NextGameplay/*.hpp NextGameplay/*.h`，自动覆盖。无需改。

### 3.6 文档更新

- `AGENTS.md` 第 100 行附近：「Common components: ...」段落不变（那是引擎组件）
- `AGENT_GUIDE/QuickJSBindings.md`：检查是否提到 `NextGameplay/Components/`
- `AGENT_GUIDE/PrefabSceneWorkflow.md`：同上

---

## 4. 验收门

```bash
# 1. 旧子文件夹不存在
test ! -d src/NextGameplay/Components && \
test ! -d src/NextGameplay/Reflection && \
test ! -d src/NextGameplay/Utilities && echo OK

# 2. 新子文件夹就绪
ls src/NextGameplay/GameComponents src/NextGameplay/GameplayReflection src/NextGameplay/Helpers >/dev/null && echo OK

# 3. 0 残留旧路径
grep -rn '"NextGameplay/\(Components\|Reflection\|Utilities\)/' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行

# 4. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] 三个子文件夹改名完成
- [ ] `Tests/Test_GameplayComponents.cpp` 仍编译通过（include 路径已更新）
- [ ] `CharacterDemo` target 仍编译并跑通其基线测试
- [ ] AGENT_GUIDE 内提到这些路径的地方已更新
- [ ] PR 标题：`refactor(phase-05): NextGameplay 子文件夹消歧`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| `Tests/Test_GameplayComponents.cpp` 直接 include `NextGameplay/Components/...` | sed 已覆盖；编译会发现漏改 |
| TypeScript binding 里的注册名未变（如 `CharacterAnimationComponent`），运行时反射查表不受影响 | 不需要改 d.ts |
| 某些 `Application/Games/*` 直接 include `NextGameplay/Components/...` | sed 全工程覆盖，但要 PR 描述里写 grep 验证结果 |

回退：`git revert` 单一 squash commit。
