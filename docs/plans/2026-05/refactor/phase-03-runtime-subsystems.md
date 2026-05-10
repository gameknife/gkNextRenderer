# Phase 03 · Runtime/Subsystems 按领域拆子目录 + 去 Next 前缀

> **目的：** `Runtime/Subsystems/` 19 个异质文件平摊 → 9 个领域子目录；同时所有 `NextXxx` 前缀去掉。
> **依赖：** phase-02 完成
> **范围：** `src/Runtime/Subsystems/`（19 文件）+ 所有引用方
> **预计 diff：** ~14 文件移动 + 改名；改写 ~150 处 include + ~50 处 `Next*` 类型引用（如果有）

---

## 1. 当前状况

```
src/Runtime/Subsystems/
├─ AIService.{cpp,h}              # AI / LLM 服务
├─ NextAnimation.{cpp,h}          # ozz 动画
├─ NextAudio.{cpp,h}              # miniaudio
├─ NextCharacterController.{cpp,h}# 角色控制
├─ NextLocalization.{cpp,h}       # 本地化
├─ NextPhysics.{cpp,h}            # Jolt 物理
├─ NextPhysicsTypes.h             # 物理类型定义
├─ QuickJSEngine.{cpp,h}          # 脚本引擎
├─ TaskCoordinator.{cpp,h}        # 后台任务
└─ VoiceInputService.{cpp,h}      # 语音输入 (whisper)
```

> 注：审计原本说 19 文件，含 .h/.cpp 各算独立，实际 10 类（17 个文件，加 `NextPhysicsTypes.h` 共 11 头 + 8 cpp = 19）。

---

## 2. 目标结构

```
src/Runtime/Subsystems/
├─ AI/
│  └─ AIService.{cpp,h}
├─ Animation/
│  └─ Animation.{cpp,h}           # ← NextAnimation
├─ Audio/
│  └─ Audio.{cpp,h}               # ← NextAudio
├─ Character/
│  └─ CharacterController.{cpp,h} # ← NextCharacterController
├─ Localization/
│  └─ Localization.{cpp,h}        # ← NextLocalization
│  ⚠️ 注意：与 src/Utilities/Localization.{cpp,h} 重名！见 §3.4
├─ Physics/
│  ├─ Physics.{cpp,h}             # ← NextPhysics
│  └─ PhysicsTypes.h              # ← NextPhysicsTypes
├─ Scripting/
│  └─ QuickJSEngine.{cpp,h}
├─ Tasks/
│  └─ TaskCoordinator.{cpp,h}
└─ Voice/
   └─ VoiceInputService.{cpp,h}
```

> 命名收益：每个子目录恰好 1 个领域 1 个类，自带语义标签。`Audio.h` `Physics.h` 等极清楚。

---

## 3. 步骤

### 3.1 创建子目录 + 文件 git mv 与改名

```bash
cd src/Runtime/Subsystems
mkdir -p AI Animation Audio Character Localization Physics Scripting Tasks Voice

git mv AIService.cpp                AI/AIService.cpp
git mv AIService.h                  AI/AIService.h
git mv NextAnimation.cpp            Animation/Animation.cpp
git mv NextAnimation.h              Animation/Animation.h
git mv NextAudio.cpp                Audio/Audio.cpp
git mv NextAudio.h                  Audio/Audio.h
git mv NextCharacterController.cpp  Character/CharacterController.cpp
git mv NextCharacterController.h    Character/CharacterController.h
git mv NextLocalization.cpp         Localization/Localization.cpp
git mv NextLocalization.h           Localization/Localization.h
git mv NextPhysics.cpp              Physics/Physics.cpp
git mv NextPhysics.h                Physics/Physics.h
git mv NextPhysicsTypes.h           Physics/PhysicsTypes.h
git mv QuickJSEngine.cpp            Scripting/QuickJSEngine.cpp
git mv QuickJSEngine.h              Scripting/QuickJSEngine.h
git mv TaskCoordinator.cpp          Tasks/TaskCoordinator.cpp
git mv TaskCoordinator.h            Tasks/TaskCoordinator.h
git mv VoiceInputService.cpp        Voice/VoiceInputService.cpp
git mv VoiceInputService.h          Voice/VoiceInputService.h
```

### 3.2 改写 include 路径（grep + sed 映射表）

| 旧 include | 新 include |
| --- | --- |
| `"Runtime/Subsystems/AIService.h"` | `"Runtime/Subsystems/AI/AIService.h"` |
| `"Runtime/Subsystems/NextAnimation.h"` | `"Runtime/Subsystems/Animation/Animation.h"` |
| `"Runtime/Subsystems/NextAudio.h"` | `"Runtime/Subsystems/Audio/Audio.h"` |
| `"Runtime/Subsystems/NextCharacterController.h"` | `"Runtime/Subsystems/Character/CharacterController.h"` |
| `"Runtime/Subsystems/NextLocalization.h"` | `"Runtime/Subsystems/Localization/Localization.h"` |
| `"Runtime/Subsystems/NextPhysics.h"` | `"Runtime/Subsystems/Physics/Physics.h"` |
| `"Runtime/Subsystems/NextPhysicsTypes.h"` | `"Runtime/Subsystems/Physics/PhysicsTypes.h"` |
| `"Runtime/Subsystems/QuickJSEngine.h"` | `"Runtime/Subsystems/Scripting/QuickJSEngine.h"` |
| `"Runtime/Subsystems/TaskCoordinator.h"` | `"Runtime/Subsystems/Tasks/TaskCoordinator.h"` |
| `"Runtime/Subsystems/VoiceInputService.h"` | `"Runtime/Subsystems/Voice/VoiceInputService.h"` |

```bash
# 一次性脚本（macOS 兼容）
declare -A subs=(
  ["Runtime/Subsystems/AIService.h"]="Runtime/Subsystems/AI/AIService.h"
  ["Runtime/Subsystems/NextAnimation.h"]="Runtime/Subsystems/Animation/Animation.h"
  ["Runtime/Subsystems/NextAudio.h"]="Runtime/Subsystems/Audio/Audio.h"
  ["Runtime/Subsystems/NextCharacterController.h"]="Runtime/Subsystems/Character/CharacterController.h"
  ["Runtime/Subsystems/NextLocalization.h"]="Runtime/Subsystems/Localization/Localization.h"
  ["Runtime/Subsystems/NextPhysics.h"]="Runtime/Subsystems/Physics/Physics.h"
  ["Runtime/Subsystems/NextPhysicsTypes.h"]="Runtime/Subsystems/Physics/PhysicsTypes.h"
  ["Runtime/Subsystems/QuickJSEngine.h"]="Runtime/Subsystems/Scripting/QuickJSEngine.h"
  ["Runtime/Subsystems/TaskCoordinator.h"]="Runtime/Subsystems/Tasks/TaskCoordinator.h"
  ["Runtime/Subsystems/VoiceInputService.h"]="Runtime/Subsystems/Voice/VoiceInputService.h"
)
for old in "${!subs[@]}"; do
  new="${subs[$old]}"
  grep -rl "\"$old\"" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|\"$old\"|\"$new\"|g"
done
find src/ -name '*.bak' -delete
```

### 3.3 改写 NextXxx 类型/命名空间引用

只改路径不改类名一般够用，但如果代码里有形如 `class NextAudio` `namespace NextAudio` 这样的命名空间，要查清楚：

```bash
grep -rn 'namespace NextAudio\|namespace NextPhysics\|namespace NextAnimation\|namespace NextLocalization\|namespace NextCharacterController\|namespace NextPlatform' src/ --include='*.cpp' --include='*.h'
```

如果发现：
- 命名空间 `NextAudio` → 改为 `Runtime::Subsystems::Audio`（在 phase-11 集中处理）。**本 phase 暂不动命名空间**，只动文件路径与文件名。
- 类名 `class NextAudio` → 改名 `class Audio`。**注意类名调整**！

> ⚠️ **本 phase 必做的类名改名**：因为 `NextAudio.h` 现在叫 `Audio.h`，类名 `NextAudio` 也应同步去前缀。grep 搜：
>
> ```bash
> grep -rn 'NextAudio\|NextPhysics\|NextAnimation\|NextLocalization\|NextCharacterController\|VoiceInputService' src/ --include='*.cpp' --include='*.h'
> ```
>
> 对每个类名引用，按以下表格替换（**仅改类名 / 变量类型，不改 API 行为**）：
>
> | 旧符号 | 新符号 | 备注 |
> | --- | --- | --- |
> | `class NextAudio` | `class Audio` | header + cpp |
> | `class NextPhysics` | `class Physics` | |
> | `class NextAnimation` | `class Animation` | |
> | `class NextLocalization` | `class Localization` | ⚠️ 与 `Utilities::Localization` 同名，见 §3.4 |
> | `class NextCharacterController` | `class CharacterController` | |
> | 局部变量类型 `NextAudio*` | `Audio*` | 用 grep 替换 |
> | `NextEngine::GetAudio()` 返回 `NextAudio*` | 改为返回 `Subsystems::Audio::Audio*`？**否，本 phase 不引入命名空间**；返回类型改为 `Audio*` | |

### 3.4 关于 Localization 重名

`src/Utilities/Localization.{cpp,h}` 是通用 `LOCTEXT()` API。`src/Runtime/Subsystems/NextLocalization.{cpp,h}` 是引擎运行时的 i18n 配置子系统。

**问题**：去前缀后会出现两个 `Localization.h`，路径不同但 IDE / clangd 可能混淆。

**决策**：
- 保留 `Utilities/Localization.{cpp,h}` 不动（它是底层 API）
- 子系统改名为 `LocalizationService.{cpp,h}`（更准确反映它是"服务"）
- 类名也改：`NextLocalization` → `LocalizationService`
- 路径：`Runtime/Subsystems/Localization/LocalizationService.{cpp,h}`

更新映射表（覆盖 §3.1 的 mv）：

```bash
git mv NextLocalization.cpp  Localization/LocalizationService.cpp
git mv NextLocalization.h    Localization/LocalizationService.h
```

include 替换映射表对应改：

| 旧 | 新 |
| --- | --- |
| `"Runtime/Subsystems/NextLocalization.h"` | `"Runtime/Subsystems/Localization/LocalizationService.h"` |

### 3.5 iOS OBJCXX 设置

`src/CMakeLists.txt:22`：

```cmake
if (IOS)
    set_source_files_properties(Runtime/Subsystems/NextAudio.cpp PROPERTIES LANGUAGE "OBJCXX")
endif()
```

改为：

```cmake
if (IOS)
    set_source_files_properties(Runtime/Subsystems/Audio/Audio.cpp PROPERTIES LANGUAGE "OBJCXX")
endif()
```

### 3.6 SourceFiles.cmake

`src_files_engine` 用 `Runtime/*.cpp` GLOB_RECURSE，**已自动覆盖子目录**，无需改。

### 3.7 更新 AGENTS.md 引用

`AGENTS.md` 第 71 行附近列出的"Common components: RenderComponent, PhysicsComponent, SkinnedMeshComponent" 不涉及本 phase 范围（那是 Runtime/Components/，本 phase 不动）。

但 AGENTS.md 第 76 行附近 "Subsystems" 列表如果有提到旧名字，要同步更新。grep 一下 `AGENTS.md` 里 `NextAudio` `NextPhysics` `Subsystems/` 的引用，必要时修。

---

## 4. 验收门

```bash
# 1. Subsystems 顶层无 .cpp/.h
ls src/Runtime/Subsystems/*.cpp src/Runtime/Subsystems/*.h 2>/dev/null
# 期望: 输出为空

# 2. 没有 Next 前缀的 Subsystems 文件名
find src/Runtime/Subsystems -name 'Next*' -o -name 'AIService*' | grep -v '/AI/AIService' | wc -l
# 期望: 0（AIService 在 AI/ 子目录里是预期的）

# 3. 没有引用旧路径
grep -rn '"Runtime/Subsystems/\(NextAudio\|NextPhysics\|NextAnimation\|NextLocalization\|NextCharacterController\|NextPhysicsTypes\|AIService\|QuickJSEngine\|TaskCoordinator\|VoiceInputService\)\.h"' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行

# 4. 类名引用全部更新
grep -rn '\bNextAudio\b\|\bNextPhysics\b\|\bNextAnimation\b\|\bNextLocalization\b\|\bNextCharacterController\b' src/ --include='*.cpp' --include='*.h'
# 期望: 0 行（注意 NextEngine、NextGameplay 不算前缀，是项目内固有名）

# 5. 编译 + 单测
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] `Subsystems/` 下 9 个子目录就绪
- [ ] 所有 `NextXxx` 前缀（除 NextEngine / NextGameplay 外）已去除
- [ ] `LocalizationService` 与 `Utilities::Localization` 不冲突（IDE 不报二义）
- [ ] iOS preset 配置中 OBJCXX 路径已更新
- [ ] `Tests/Test_PhysicsSync.cpp` 等用到 Physics 的测试仍编译通过
- [ ] `Tests/Test_PhysicsComponent.cpp` 已通过
- [ ] PR 标题：`refactor(phase-03): Subsystems 按领域拆子目录 + 去 Next 前缀`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| `NextAudio` 类名被 SDL3/miniaudio 平台头里某个宏命中 | grep 替换前先检查 `grep -rn 'define.*Audio\b' src/ThirdParty` 看有无冲突 |
| QuickJS TypeScript 绑定 (`Engine.d.ts`) 内的 C++ 类型映射写死了 `NextAudio` | 检查 `assets/typescript/Engine.d.ts`；按 README §7.3 "TypeScript 绑定 API 不动"原则，**绑定层暴露的 JS 类名不变**。如果 d.ts 里写的是 `NextAudio` 这种 C++ 类名暴露给 JS，要么不改、要么同步改 d.ts 与 binding 注册码。**默认 d.ts 不动**，binding 代码里如果出现 `NextAudio` 字面量，要单独评估 |
| `LocalizationService` 与 `Utilities::Localization` 在某文件同时被 include 时编译器混淆 | 不同命名空间不会真正冲突；如真有问题，把 `Utilities` 命名空间显式加上 |

回退：`git revert` 单一 squash commit。
