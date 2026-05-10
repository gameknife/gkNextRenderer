# Phase 01 · 头文件扩展名统一为 .h

> **目的：** 把 `src/` 下所有一手 C++ 头从 `.hpp` 改名为 `.h`，并改写所有 `#include`。
> **依赖：** phase-00 完成（约定文件已落地）
> **范围：** `src/` 全部一手代码；不动 `src/ThirdParty/`、`external/`、`assets/shaders/*.slang`。
> **预计 diff：** ~153 文件改名 + ~1500~2000 处 include 改写

---

## 1. 为什么把 `.hpp` 改成 `.h`（而不是反过来）

宪法 D5 已敲定 `.h`。理由记录在这里供后续追溯：

1. 跨平台 IDE / clangd / vscode 对 `.h` 的 C++ 识别是默认行为
2. 与项目已大量存在的 `.h`（57 个）保持一致而非反向（153 个）—— 改名总数虽多，但分布更均匀，验证 grep 更彻底
3. 与 SDL3 / spdlog 等第三方依赖的命名一致

---

## 2. 改名清单（自动生成方式）

```bash
# 列出所有需要改名的文件（不含 ThirdParty）
find src -type f -name "*.hpp" -not -path "src/ThirdParty/*"
```

执行结果应该约 153 个文件。**保存这个列表**作为本 phase 的"改名清单"，附在 PR 描述里。

---

## 3. 步骤

### 3.1 重命名文件

```bash
# 在 src/ 下，所有 .hpp 改为 .h（用 git mv 保留 history）
cd src
find . -type f -name "*.hpp" -not -path "./ThirdParty/*" | while read f; do
  git mv "$f" "${f%.hpp}.h"
done
```

> 注意：`Common/CoreMinimal.hpp` 也改名为 `Common/CoreMinimal.h`。这个改名会引发**几乎所有文件的 include 改写**，是本 phase 最大头的一处。

### 3.2 改写所有 #include 引用

```bash
# 在仓库根执行；只改非 ThirdParty 的源码 + 改 .slang 文件如果有的话
# 假设没有 .hpp 进 shader，只处理 cpp/h/hpp（hpp 改完后剩 0 个）
cd src
grep -rl '\.hpp"' . \
  --include='*.cpp' --include='*.h' --include='*.hpp' \
  --exclude-dir='ThirdParty' \
| while read f; do
  # 使用 sed 把所有 #include "...hpp" 改为 .h
  sed -i.bak -E 's/(#include "[A-Za-z0-9_/.-]+)\.hpp"/\1.h"/g' "$f"
  rm "${f}.bak"
done
```

⚠️ macOS / BSD sed 与 GNU sed 行为不同；codex 在 macOS 上必须用 `sed -i.bak ... && rm *.bak` 这种形式。Linux 上可以直接 `sed -i`。

### 3.3 改写 SourceFiles.cmake

`src/cmake/SourceFiles.cmake` 中 GLOB 模式包含 `*.hpp`。**保留这些模式**（防止漏改时还能继续编译），但**追加注释**说明项目已无 `.hpp`：

```cmake
# 注：本工程一手源码已统一为 .h；保留 .hpp 通配符仅为兼容 ThirdParty 与
# 极个别遗漏文件；若 grep 返回 0 行可安全删除。
```

### 3.4 改写 src/CMakeLists.txt 中的硬编码路径

```cmake
# Line 22 (原):
set_source_files_properties(Runtime/Subsystems/NextAudio.cpp PROPERTIES LANGUAGE "OBJCXX")
```
该行不含 `.hpp`，无需改。

```cmake
# Line 278-279 (原):
set_source_files_properties(Assets/Loaders/FSceneLoader.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties(Assets/Savers/FSceneSaver.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
```
不含 `.hpp`，无需改。

```cmake
# UNIT_TEST_SOURCES（Line 26-52）全部是 .cpp；无需改。
```

`SourceFiles.cmake` 的 GLOB 模式中**已经同时包含 `.h` 和 `.hpp`**（Line 8-12, 15-19, 53-61, 64-68, 75-77 等），所以即便文件改名也不会因为 GLOB 失配而漏挂载。

### 3.5 平台特例

- `assets/CMakeLists.txt`：是 shader build 配置，不动。
- `android/` `ios/`：扫一下是否有引用 `.hpp` 的 manifest / plist；没有的话不动。
- `tools/`：脚本里如果出现 `*.hpp` 字面量，比如 `clang-tools/run-clang-tidy.py`，需要更新。

```bash
grep -rn '\.hpp' tools/ android/ ios/ scripts/ 2>/dev/null | grep -v 'ThirdParty'
```
如果结果非空，逐条检查并改写。

### 3.6 防回潮

在 `.gitignore` 同级位置（暂不引入 git hook，那超范围），但**在本 PR 描述里写明：未来新增 .hpp 的 PR 必须 reject**。

---

## 4. 验收门

```bash
# 1. 工程内不再有一手 .hpp
find src -type f -name "*.hpp" -not -path "src/ThirdParty/*" | wc -l
# 期望: 0

# 2. include 中不再引用 .hpp（ThirdParty 除外）
grep -rn '#include "[^"]*\.hpp"' src/ \
  --include='*.cpp' --include='*.h' --include='*.hpp' \
  | grep -v 'ThirdParty' | wc -l
# 期望: 0

# 3. 编译
./gnb build --reconfigure

# 4. 单测
./out/build/macos-arm64/bin/gkNextUnitTests
# 期望: All tests passed

# 5. 没有未提交残留
git status --porcelain
```

---

## 5. 自我审查清单

- [ ] `find src -name '*.hpp' -not -path 'src/ThirdParty/*'` 返回 0 行
- [ ] `grep -rn '\.hpp"' src/ --exclude-dir=ThirdParty` 返回 0 行
- [ ] `git diff --stat -M` 中所有 rename 都是 `.hpp → .h`，没有意外新增/删除
- [ ] CMake `UNITY_BUILD` 仍生效（构建日志含 `Unity` 字样）
- [ ] iOS preset 仍能配置（`cmake --preset ios` 不报错；不需要真编 iOS）
- [ ] Android preset 仍能配置（`./gnb android` 跑到 `cmake configure` 阶段不报错）
- [ ] 单测全部通过
- [ ] PR 标题：`refactor(phase-01): 头文件扩展名统一为 .h`
- [ ] PR 描述附改名清单（153 行左右）

## 6. 风险与回退

| 风险 | 应对 |
| --- | --- |
| 漏改某个 include 路径，编译失败 | 编译错误会报到具体文件，按报错改即可 |
| sed 误伤了字符串字面量里的 `.hpp"`（极少见） | 在 sed 替换前先 grep 一次：`grep -rn '\.hpp"' src/ --include='*.cpp' \| grep -v '#include'`，结果为 0 才安全 |
| `Common/CoreMinimal.hpp` 改名后仍有大量 include `<...CoreMinimal.hpp>` 残留 | 上述全局 sed 会一并改写，但要单独再 grep 一次 `CoreMinimal\.hpp` 验证 |
| 重命名后 git history 看不到 rename | 使用 `git mv` 而非 `mv`；`git diff -M` 默认能识别 |

回退：单一 `git revert` 该阶段的 squash commit。

## 7. 提示给 Codex

- 这是**最机械**的一个 phase，但也是**改动 diff 最大**的。建议 codex 一次性跑完整套 sed + git mv，不要分小步提交。
- `git mv` 与 `sed` 的顺序：**先 git mv 文件，后 sed 改 include**。反过来 sed 找不到目标文件。
- macOS 上 `find ... | while read f` 在含空格路径上会出问题，但 src/ 下确认无空格路径，可放心。
