# Phase 07 · Application 三分类 + src/Editor → Application/Core/Editor

> **目的：** 11 个并列子项目按角色分到 `Games/`/`Tools/`/`Core/`；`src/Editor/` 整体迁入 `Application/Core/Editor/`。
> **依赖：** phase-06 完成
> **范围：** `src/Application/`（11 子项目）+ `src/Editor/`（37 文件）
> **预计 diff：** ~120 文件移动；CMake target list 重排；改写 ~80 处 include

---

## 1. 当前状况

```
src/Application/
├─ Brotato3D/                    # game (无前缀)
├─ BrickPlayer/                  # game
├─ CharacterDemo/                # game
├─ Flappy/                       # game (含 FlappyCpp/ FlappyJs/ 两实现)
├─ KongLie3D/                    # game
├─ MagicaLego/                   # game
├─ Voyage3D/                     # game
├─ gkNextBenchmark/              # tool (gkNext 前缀)
├─ gkNextRenderer/               # core renderer (gkNext 前缀)
├─ gkNextVisualTest/             # tool
└─ Packager/                     # tool

src/Editor/                      # 编辑器主程序，独立顶层（37 文件）
```

11 个子项目平铺；命名前缀混乱；`src/Editor/` 与 `Application/` 平起平坐让人疑惑。

---

## 2. 目标结构

```
src/Application/
├─ Core/
│  ├─ Renderer/                  # ← gkNextRenderer/
│  └─ Editor/                    # ← src/Editor/ 整体迁入
├─ Tools/
│  ├─ Benchmark/
│  │  ├─ Common/                 # ← gkNextBenchmark/Common/
│  │  ├─ Still/                  # ← gkNextBenchmark/gkNextStillBenchmark/
│  │  └─ Motion/                 # ← gkNextBenchmark/gkNextMotionBenchmark/
│  ├─ VisualTest/                # ← gkNextVisualTest/
│  └─ Packager/
└─ Games/
   ├─ Brotato3D/
   ├─ BrickPlayer/
   ├─ CharacterDemo/
   ├─ Flappy/                    # 内含 Common + FlappyCpp + FlappyJs
   ├─ KongLie3D/
   ├─ MagicaLego/
   └─ Voyage3D/
```

> **重要不变量**：CMake target 名**全部保留**（README §7）：
> - `gkNextRenderer` `gkNextEditor` `gkNextStillBenchmark` `gkNextMotionBenchmark` `gkNextVisualTest`
> - `Brotato3D` `KongLie3D` `MagicaLego` `BrickPlayer` `CharacterDemo` `FlappyCpp` `FlappyJs` `Voyage3D` `Packager`
>
> 即源文件夹名 `Application/Games/Brotato3D/` 对应 target `Brotato3D`；`Application/Core/Renderer/` 对应 target `gkNextRenderer`。**target 名与源文件夹解耦**。

---

## 3. 步骤

### 3.1 创建三个分类目录

```bash
cd src/Application
mkdir -p Core Tools Games Tools/Benchmark
```

### 3.2 git mv 子项目（按表）

```bash
# Core
git mv gkNextRenderer    Core/Renderer

# Tools
git mv gkNextBenchmark/Common               Tools/Benchmark/Common
git mv gkNextBenchmark/gkNextStillBenchmark Tools/Benchmark/Still
git mv gkNextBenchmark/gkNextMotionBenchmark Tools/Benchmark/Motion
rmdir gkNextBenchmark
git mv gkNextVisualTest  Tools/VisualTest
git mv Packager          Tools/Packager

# Games
git mv Brotato3D        Games/Brotato3D
git mv BrickPlayer      Games/BrickPlayer
git mv CharacterDemo    Games/CharacterDemo
git mv Flappy           Games/Flappy
git mv KongLie3D        Games/KongLie3D
git mv MagicaLego       Games/MagicaLego
git mv Voyage3D         Games/Voyage3D
```

> Voyage3D：审计列表里出现，确认它存在 `ls src/Application/Voyage3D` 后再 mv；若不存在跳过。

### 3.3 迁移 src/Editor → Application/Core/Editor

```bash
git mv src/Editor src/Application/Core/Editor
```

> 这是单步 37 文件 rename。

### 3.4 改写 SourceFiles.cmake

`src/cmake/SourceFiles.cmake` 内 11 处硬编码路径需要全部改：

```cmake
# Old → New
"Application/MagicaLego/*"        → "Application/Games/MagicaLego/*"
"Application/BrickPlayer/*"       → "Application/Games/BrickPlayer/*"
"Application/gkNextRenderer/*"    → "Application/Core/Renderer/*"
"Application/gkNextBenchmark/Common/*"               → "Application/Tools/Benchmark/Common/*"
"Application/gkNextBenchmark/gkNextStillBenchmark/*" → "Application/Tools/Benchmark/Still/*"
"Application/gkNextBenchmark/gkNextMotionBenchmark/*"→ "Application/Tools/Benchmark/Motion/*"
"Application/gkNextVisualTest/*"  → "Application/Tools/VisualTest/*"
"Application/KongLie3D/*"         → "Application/Games/KongLie3D/*"
"Application/Brotato3D/*"         → "Application/Games/Brotato3D/*"
"Application/Flappy/*"            → "Application/Games/Flappy/*"
"Application/CharacterDemo/*"     → "Application/Games/CharacterDemo/*"
```

将 `Editor` 加入 SourceFiles.cmake：

```cmake
# 原 src/Editor 现在是 Application/Core/Editor
file(GLOB_RECURSE src_files_editor "Application/Core/Editor/*")
```

替换原第 71 行的 `file(GLOB_RECURSE src_files_editor "Editor/*")`。

### 3.5 改写 src/CMakeLists.txt

`src/CMakeLists.txt:151-152` 的 Packager 主入口路径：

```cmake
add_executable(Packager
    Application/Packager/PackagerMain.cpp  # 改为
    Application/Tools/Packager/PackagerMain.cpp
)
```

`UNIT_TEST_SOURCES`（`src/CMakeLists.txt:44, 48-50`）：

```cmake
Application/BrickPlayer/BrickPlayerSnapLogic.cpp        # → Application/Games/BrickPlayer/BrickPlayerSnapLogic.cpp
Application/MagicaLego/MagicaLegoStyle.cpp              # → Application/Games/MagicaLego/MagicaLegoStyle.cpp
Application/MagicaLego/MagicaLegoScriptParser.cpp       # → Application/Games/MagicaLego/MagicaLegoScriptParser.cpp
Application/MagicaLego/MagicaLegoPlacementRules.cpp     # → Application/Games/MagicaLego/MagicaLegoPlacementRules.cpp
```

### 3.6 改写所有 #include

App 内部互相不太交叉 include，但有几处会动：

- `Application/Core/Editor/` 内的 `#include "Editor/<...>"` 全部改成 `#include "Application/Core/Editor/<...>"` 或者**保留**？

**决策**：因为 `target_include_directories(${target} PRIVATE .)`（`src/CMakeLists.txt:238`），所有 include 路径都从 `src/` 起算。所以：

| 旧 | 新 |
| --- | --- |
| `"Editor/EditorMain.h"` | `"Application/Core/Editor/EditorMain.h"` |
| `"Editor/Panels/PropertyWidgets.h"` | `"Application/Core/Editor/Panels/PropertyWidgets.h"` |
| 所有 `"Editor/<X>"` | `"Application/Core/Editor/<X>"` |

```bash
declare -A subs=(
  ["Editor/"]="Application/Core/Editor/"
)
# 但要小心：Editor/ 路径太短可能误伤；先看会被 sed 覆盖的范围
grep -rn '"Editor/' src/ --include='*.cpp' --include='*.h'
# 仔细审视是否所有匹配都是预期的（Application/Core/Editor 与 Application/Games/Editor 不同——后者不存在）
```

**注意防误伤**：
- `Runtime/UI/` 内不再有 `Editor/` 路径（phase-04 已改）
- `NextGameplay/` 内不引用 `Editor/`
- `Vulkan/` `Rendering/` `Assets/` 不引用 `Editor/`

如确认所有 `"Editor/...` 都源自 `src/Editor/` 旧路径，可放心 sed。

### 3.7 改 Application/ 内部互相 include

`Application/Tools/Benchmark/Still/*` 引用 `Application/Tools/Benchmark/Common/*`：

```bash
declare -A app_subs=(
  ["Application/MagicaLego/"]="Application/Games/MagicaLego/"
  ["Application/BrickPlayer/"]="Application/Games/BrickPlayer/"
  ["Application/CharacterDemo/"]="Application/Games/CharacterDemo/"
  ["Application/Brotato3D/"]="Application/Games/Brotato3D/"
  ["Application/KongLie3D/"]="Application/Games/KongLie3D/"
  ["Application/Voyage3D/"]="Application/Games/Voyage3D/"
  ["Application/Flappy/"]="Application/Games/Flappy/"
  ["Application/gkNextRenderer/"]="Application/Core/Renderer/"
  ["Application/gkNextVisualTest/"]="Application/Tools/VisualTest/"
  ["Application/gkNextBenchmark/Common/"]="Application/Tools/Benchmark/Common/"
  ["Application/gkNextBenchmark/gkNextStillBenchmark/"]="Application/Tools/Benchmark/Still/"
  ["Application/gkNextBenchmark/gkNextMotionBenchmark/"]="Application/Tools/Benchmark/Motion/"
  ["Application/Packager/"]="Application/Tools/Packager/"
)
for old in "${!app_subs[@]}"; do
  new="${app_subs[$old]}"
  grep -rl "\"$old" src/ --include='*.cpp' --include='*.h' \
    | xargs sed -i.bak "s|\"$old|\"$new|g"
done
find src/ -name '*.bak' -delete
```

### 3.8 src/CMakeLists.txt target list 不动

target name 不变（README §7），所以 `AllTargets` 列表（`src/CMakeLists.txt:204-223`）**不需要改**。

但 target 内的 source list 变量名也都不变（`src_files_brotato3d` 等），所以 `add_executable(Brotato3D ${src_files_brotato3d} ...)` 也**不需要改**。

唯一需要改的：
- 12 个 GLOB 路径（§3.4）
- Packager 显式路径（§3.5）
- UNIT_TEST_SOURCES 4 处（§3.5）
- `src/CMakeLists.txt:71` 的 `src_files_editor` GLOB

### 3.9 Editor 目录内部不动

`src/Editor/` → `src/Application/Core/Editor/` 的迁移**只改路径**，子目录结构（`AI/` `Core/` `Nodes/` `Overlays/` `Panels/` 等）保持原样。

`src/Editor/Core/` 是个含糊命名（命中 `Editor/Core/EditorLayoutConstants.h` 等），**本 phase 不动**，留给 phase-12 收尾。

### 3.10 ImNodeFlow link

`src/CMakeLists.txt:415`：

```cmake
target_link_libraries(gkNextEditor PRIVATE ImNodeFlow)
```

target 名不变，无需改。

`src/CMakeLists.txt:420`：

```cmake
set_source_files_properties(${src_files_editor} PROPERTIES COMPILE_FLAGS "-w")
```

变量名不变，无需改。

### 3.11 文档更新

- `AGENTS.md` 第 80 行附近的目录树注释里包含 `Application/`，需同步更新
- `AGENT_GUIDE/MagicaLego.md` `BrickPlayer-LDraw-Technical-Summary*.md` 等如有路径引用，更新
- `docs/projects/brotato-3d/` `konglie-3d/` 等历史文档**不动**（它们是历史快照）
- `docs/CMAKE_STRUCTURE.md` 同步

---

## 4. 验收门

```bash
# 1. 旧顶层路径不存在
test ! -d src/Editor && \
test ! -d src/Application/gkNextRenderer && \
test ! -d src/Application/gkNextBenchmark && \
test ! -d src/Application/gkNextVisualTest && \
test ! -d src/Application/Brotato3D && \
test ! -d src/Application/Packager && echo OK

# 2. 新结构就绪
test -d src/Application/Core/Renderer && \
test -d src/Application/Core/Editor && \
test -d src/Application/Tools/Benchmark/Common && \
test -d src/Application/Tools/Benchmark/Still && \
test -d src/Application/Tools/Benchmark/Motion && \
test -d src/Application/Tools/VisualTest && \
test -d src/Application/Tools/Packager && \
test -d src/Application/Games/Brotato3D && echo OK

# 3. 旧 include 路径 0 残留
grep -rn '"\(Editor/\|Application/\(MagicaLego\|BrickPlayer\|gkNext\)\)' src/ --include='*.cpp' --include='*.h' | grep -v 'Application/Core/Editor/\|Application/Games/MagicaLego/\|Application/Games/BrickPlayer/'
# 期望: 0 行

# 4. 编译 + 单测（核心：所有 17 个 target 必须全部能配置）
./gnb build --reconfigure
./out/build/macos-arm64/bin/gkNextUnitTests
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] 所有 11 个子项目都按表移动
- [ ] `src/Editor/` 已不存在，全部迁到 `Application/Core/Editor/`
- [ ] `SourceFiles.cmake` 12 处 GLOB 路径已更新
- [ ] `src/CMakeLists.txt` 中 Packager 显式路径已更新
- [ ] `UNIT_TEST_SOURCES` 中 4 处 Application/ 路径已更新
- [ ] CMake target 名**一个不变**：`gkNextRenderer` `gkNextEditor` `gkNextStillBenchmark` `gkNextMotionBenchmark` `gkNextVisualTest` `Brotato3D` `KongLie3D` `MagicaLego` `BrickPlayer` `CharacterDemo` `FlappyCpp` `FlappyJs` `Packager` 都还在 `AllTargets`
- [ ] `./gnb build` 成功；`out/build/<preset>/bin/` 下 17 个二进制都生成
- [ ] `gkNextUnitTests` 全部通过
- [ ] AGENTS.md / AGENT_GUIDE 内提到旧路径的地方已更新
- [ ] PR 标题：`refactor(phase-07): Application 三分类 + Editor 迁入`

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| sed `"Editor/` 误伤了某些不该改的字符串字面量（如配置文件路径常量） | sed 前先 grep 一遍 `"Editor/` 看有无非 #include 出现；本工程检查后预计无误伤 |
| Voyage3D 没在 SourceFiles.cmake 注册（只在 §3.2 mv），导致仍然不能构建 | 检查 SourceFiles.cmake 是否本来就缺 Voyage3D 的 GLOB；若是历史问题，本 PR 顺手补上一条 |
| 某个游戏的 .cpp 通过相对路径 `#include "../Common/Foo.h"` 引用 Benchmark Common | grep 验证；若有，改为从 src/ 起算 |
| MagicaLego 的 ffmpeg 复制脚本 `src/CMakeLists.txt:397` 路径相对 `MagicaLego` target | target 名未变，逻辑不动 |

回退：`git revert` 单一 squash commit。

## 7. 关于 Flappy 内部结构

`Flappy/` 内部已是 `FlappyCpp/`/`FlappyJs/` + 顶层共享 `FlappyCommon.h FlappyConfig.{cpp,h}`。**本 phase 不动其内部**，整体迁到 `Application/Games/Flappy/`。

如果 codex 在 mv 时发现 `Flappy/` 路径里有更深的子目录耦合，按原样搬运，**不做内部重构**。
