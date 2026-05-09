# gkNextRenderer 构建系统重构执行计划

> 目标：用一个独立、跨平台、轻量的 CLI 二进制 `gnb` 取代当前散落的 `build.{bat,sh}` / `run.{bat,sh}` / `scripts/*.{bat,ps1,sh}`，并把 CMake 预设矩阵收敛成单一配置。
>
> 本文档面向 codex，所有决策已落定，按 §3 分阶段直接执行。

---

## 0. 现状盘点（实施前必须理解的工程拓扑）

### 0.1 用户入口
- `build.bat` → 调 `scripts\build.ps1`（Windows）。
- `build.sh` → 自带逻辑（Linux/macOS）。
- `run.bat` → 调 `scripts\run.ps1`。
- `run.sh` → 自带逻辑。

参数空间（`build.{sh,bat}`）：
| 参数 | 说明 |
| --- | --- |
| `--preset <name>` | 必填，传给 CMake Preset |
| `--clean` | 删除 `out/build/<preset>` |
| `--reconfigure` | 强制 cmake configure |
| `--android` | 改走 `android/gradlew build` |
| `--avif`（仅 ps1） | 注入 `-DENABLE_AVIF=ON` + vcpkg manifest feature `avif` |
| `-- <args>` | 透传 cmake 配置参数（含 `-DENABLE_*=ON/OFF`） |

`run.{sh,bat}` 提供 `--target / --preset / --bin-dir / --present-mode / --scene / --list / --dry-run` 与透传。

### 0.2 依赖准备脚本（位于 `scripts/`）
| 脚本 | 职责 |
| --- | --- |
| `vcpkg.{sh,bat,ps1}` | clone vcpkg 到 `.vcpkg`，checkout `2025.12.12`，`bootstrap-vcpkg`，准备 `.vcpkg_bincache` |
| `fetch-paks.{sh,bat,ps1}` | 从 GitHub Release 拉取 `ldraw.pak / optional.pak / sfx / ffmpeg.exe` |
| `publish-paks.{sh,bat,ps1}` | 反向上传到 GitHub Release |
| `package.bat` | Windows 打包 `gkNextRenderer-windows.zip` 与 `MagicaLego_*.zip` |
| `import_brotato_placeholder.py` | 资产辅助脚本（与构建无关，保留） |
| `tools/fetch_slang_linux.sh` | Linux 下没有 `slangc` 时拉取 |

### 0.3 CMake 部分
- `CMakeLists.txt` 顶层定义 9 个 option：`ENABLE_AVIF / ENABLE_DLSS / ENABLE_KTX2 / ENABLE_PHYSIC / ENABLE_AUDIO / ENABLE_OIDN / ENABLE_QUICKJS / ENABLE_WHISPERCPP / GK_ENABLE_HOT_RELOAD`，并映射到 `WITH_*`。
- `cmake/SetupExternalLibs.cmake`：用 `FetchContent` 拉 Streamline / OIDN / TSC / MoltenVK，缓存目录 `external/`。
- `cmake/SetupDependencies.cmake`：聚合 `find_package`，并查找 `slangc`（默认从 `external/slang*`、`external/slang`、`SLANG_ROOT`、`VULKAN_SDK` 等）。
- `CMakePresets.json`：5 个平台 base × 3 个 feature 集（minimal/default/full）= **15 configure preset + 15 build preset**。
- `vcpkg.json`：单一 manifest feature `avif`（其他依赖均为非可选）。

### 0.4 GitHub Actions（`.github/workflows/`）
- `windows.yml / linux.yml / macos.yml / ios.yml / android.yml / windows_self.yml / android_self.yml / release.yml / release_magicalego.yml`。
- 共同模式：装 SDK → `scripts/vcpkg.{sh,bat}` → `actions/cache` 缓存 `.vcpkg / .vcpkg_bincache` → `build.{sh,bat} --preset default-<platform>`。
- 缓存 key 形如 `${{ runner.os }}-desktop-bincache`，**专为 CI 编译时长服务**，本地用户无收益。

### 0.5 关键潜规则（必须保留语义）
- 第一次构建会自动 bootstrap vcpkg（由 build script 调 vcpkg script 完成）。
- `out/build/<preset>/bin/` 为运行目录；非 bin 目录运行 `gkNextRenderer` 也能用（已经做了 cwd 解耦）。
- Linux 上自动检测 `xrandr / wayland-protocols / xkbcommon` 缺失并打印 apt/pacman 安装提示。
- macOS / Linux 上自动拉取 slangc 到 `external/slang-*`。
- iOS 上自动拉 MoltenVK 到 `external/moltenvk-*`。
- Windows + DLSS 自动拉 Streamline 到 `external/streamline-*`，cmake 把 `bin/x64/*` 拷到 runtime dir。
- Android 走 `android/gradlew build`，Gradle 会自己再调 cmake；SDL3 的 AAR 由 `android/app/build.gradle` 自下载，不归这里管。
- vcpkg binary cache `.vcpkg_bincache` 在 CI 上做缓存，本地构建只是顺手命中。

---

## 1. 目标架构

### 1.1 单一 CLI：`gnb`

`gnb` = **g**k**N**ext**B**uilder。命名短、易输入；放在 PATH 或仓库根目录均可。

**技术栈：Go 1.23 + spf13/cobra。**
- 单文件静态二进制，5–8 MB stripped。
- cobra 自带子命令、help、shell 补全。
- 一条 `GOOS=… GOARCH=… go build` 跨编译 4 平台。
- 不依赖项目本身（gkNextEngine 不出现在 gnb 的 import 链）。

**目录约定：**
```
tools/gnb/
├── go.mod
├── cmd/
│   └── gnb/main.go            # cobra root + 子命令注册
├── internal/
│   ├── platform/              # 平台检测、triplet、shell 抽象
│   ├── vcpkg/                 # clone/bootstrap/install 逻辑
│   ├── fetcher/               # http 下载 + zip/tar 解包，复用给 streamline/tsc/moltenvk/slang/paks
│   ├── cmakerun/              # 调 cmake configure/build/list-presets
│   ├── runner/                # 启动 exe（含 --present-mode/--scene 透传）
│   ├── packager/              # zip 打包（替换 package.bat）
│   ├── android/               # 调 ./gradlew
│   ├── ios/                   # 调 xcodebuild
│   └── config/                # 解析 gnb.toml
└── README.md
```

`gnb.toml` 放在仓库根目录。

### 1.2 命令矩阵

```
gnb                        # 显示总览：检测平台、关键路径、可用命令；不做任何修改
gnb help [cmd]             # cobra 自带

gnb setup                  # 一键准备：vcpkg + binary cache + external 三方包 + paks（默认 group）
gnb setup --skip-paks
gnb setup --vcpkg-only
gnb setup --refresh        # 等价旧的 vcpkg.sh --update

gnb build                  # 根据当前平台选 preset，复用增量 configure
gnb build <target>         # 仅构建指定 target
gnb build --clean          # 删除 out/build/<preset>
gnb build --reconfigure
gnb build --jobs N
gnb build --no-unity       # -DENABLE_UNITY_BUILD=OFF
gnb build --lto            # -DENABLE_LTO=ON
gnb build --print-cmd      # 仅打印 cmake 命令，不执行

gnb run [target]           # 默认 gkNextRenderer
gnb run -- --scene=foo --present-mode=bar  # 透传
gnb run --bin-dir <path>
gnb run --list

gnb test [filter]          # 跑 gkNextUnitTests
gnb visual                 # 跑 gkNextVisualTest
gnb editor                 # 等价 gnb run gkNextEditor

gnb android [debug|release]   # ./gradlew installAndLaunch / build
gnb ios                       # xcodebuild

gnb paks fetch [groups...]    # 默认 all
gnb paks publish [groups...]  # 用户自带的 GitHub token
gnb paks list                 # 显示 manifest 状态

gnb package <variant>         # variant ∈ {windows, linux, macos, magicalego}
gnb clean [target]            # 默认清 out/，带 target 时仅 cmake --build --target clean
gnb info                      # 打印：vcpkg 路径/版本、external/ 内容、preset、bin 目录、git 版本
gnb doctor                    # 自检（VulkanSDK / slangc / git / cmake / Visual Studio / Xcode / NDK），缺啥提示啥
gnb install                   # 把当前 gnb 二进制 cp 到 ~/.local/bin（Unix）或 %USERPROFILE%\bin（Win）
```

> gnb **不暴露** AVIF 开关。AVIF 是手动启用的能力（见 §1.3），用户需要时直接 `cmake --preset windows -DENABLE_AVIF=ON` 后再 `gnb build`，或临时编辑 `CMakePresets.json` 加 `cacheVariables.ENABLE_AVIF = ON`。

**自解释行为（核心要求）：**
- 单独 `gnb` → 等价 `gnb help`，并附 "尝试 `gnb setup` / `gnb build` / `gnb doctor`" 引导。
- `gnb build` 时若发现 `.vcpkg/` 不存在 → 直接顺手跑 setup，并打印 `[gnb] 首次构建：自动执行 setup（如需跳过用 --skip-setup）`。
- `gnb build` 在 Linux 缺包时复用旧 `show_linux_dependency_hint_and_exit`，列出 apt/pacman 命令。
- `gnb run` 没有 `out/build/<preset>/bin/<target>` 时，提示 "请先运行 `gnb build <target>`"。
- 任何子命令缺必填参数时，cobra 自动报 usage；额外补一行 "示例: gnb build Brotato3D"。

### 1.3 CMake 预设收敛

把 15 + 15 个 preset 收敛到 **4 个 configure + 4 个 build**（去掉 mingw）：

```
configurePresets:  windows / linux / macos-arm64 / ios
buildPresets:      windows / linux / macos-arm64 / ios
```

特性集所有 `ENABLE_*` 选项 **从 CMake 中删除**，依赖一致化：
- `WITH_KTX2` 始终 ON。
- `WITH_PHYSIC` 始终 ON。
- `WITH_AUDIO` 始终 ON。
- `WITH_QUICKJS` 始终 ON（源码在 `src/ThirdParty/quickjs-ng/`）。
- `WITH_WHISPERCPP` 桌面始终 ON，移动端依旧 force-OFF（vcpkg manifest 已按平台 gate）。
- `WITH_STREAMLINE` 仅 Windows ON，其它平台 force-OFF（DLSS 只支持 Win，不可跨平台统一）。
- **`WITH_OIDN` 彻底移除**。涉及代码（`SetupExternalLibs.cmake` 中 OIDN 块、`src/CMakeLists.txt` 中 `if (WITH_OIDN)` 分支、`src/` 中 `#if WITH_OIDN` 宏路径）一并 `git rm` 干净；vcpkg manifest 中无 OIDN 依赖，无需改。
- **`WITH_AVIF` 保留为可选能力**：
  - `option(ENABLE_AVIF "Enable AVIF support" OFF)` 留在 `CMakeLists.txt` 中，**默认 OFF**。
  - `vcpkg.json` 保留 `features.avif`（仅当用户传 `-DVCPKG_MANIFEST_FEATURES=avif` 时拉 `libavif`）。
  - `cmake/SetupDependencies.cmake` 中 `if (WITH_AVIF) find_package(libavif)` 保留。
  - `src/CMakeLists.txt` 中 `if (WITH_AVIF)` 分支保留。
  - `src/` 中 `#if WITH_AVIF` 代码不动。
  - gnb 不暴露开关；启用方式见 §1.2 提示框。

**MinGW 整体下线**：
- `CMakePresets.json` 中 `mingw-base / default-mingw / minimal-mingw / full-mingw` 全部删除。
- `cmake/ProjectOptions.cmake` 中 `if (MINGW) ... endif()` 整段删除。
- `src/CMakeLists.txt` 中所有 `if (MINGW)` / `elseif (MINGW)` / `(UNIX OR MINGW)` 分支按"非 MINGW 路径"展平：
  - `if (NOT MINGW) find_package(Catch2 3 REQUIRED) add_executable(gkNextUnitTests ...) endif()` → 直接保留 `find_package(Catch2)` 与 unit test target。
  - `elseif (MINGW) set(AllTargets …短名单…)` 整段删，统一走桌面长名单。
  - `if (MINGW) target_link_libraries(${target} PRIVATE gdi32) endif()` 删。
  - `if ((UNIX OR MINGW) AND NOT APPLE AND NOT ANDROID)` → `if (UNIX AND NOT APPLE AND NOT ANDROID)`。
- `git grep MINGW` 应当只剩第三方代码或注释，无项目代码。

`ENABLE_LTO` / `ENABLE_UNITY_BUILD` 这两个调优 option **保留**，gnb 通过 `--lto` / `--no-unity` 透传。

`GK_ENABLE_HOT_RELOAD` 保留（语义清晰：移动端 force-OFF，桌面 ON）。

### 1.4 vcpkg manifest 简化

- `vcpkg.json` 保留 `features.avif`（不主动安装，需用户显式启用）。
- `dependencies` 保持现状（包括 `whisper-cpp` 的平台 gate、Linux 的 `vulkan-loader[xcb,xlib,wayland]`）。
- 删 `vcpkg.json` 中的 mingw 相关 platform expression（如有）。
- 移除 ps1 里 `Normalize-AvifArgs` 这种 manifest features 手术逻辑（随 ps1 一起删）。

### 1.5 数据驱动配置 `gnb.toml`

把所有"魔法数"、URL、版本号集中到一个地方，方便升级：

```toml
[gnb]
min_version = "0.1.0"     # gnb 二进制启动时检查，老二进制配新仓库直接报错

[vcpkg]
ref = "2025.12.12"
binary_cache = ".vcpkg_bincache"
root = ".vcpkg"

[external.streamline]
when = "windows"
url = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.0/streamline-sdk-v2.10.0.zip"

[external.tsc]
version = "v2025.5.23"
windows = "https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-windows-amd64.exe"
linux   = "https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-linux-amd64"
macos_arm64 = "https://github.com/rxliuli/tsgo-npm-release/releases/download/v2025.5.23/tsgo-darwin-arm64"

[external.moltenvk]
when = "ios"
url  = "https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.0/MoltenVK-ios.tar"

[external.slang]
linux = "https://github.com/shader-slang/slang/releases/download/v2025.6.1/slang-2025.6.1-linux-x86_64.zip"
macos_arm64 = "https://github.com/shader-slang/slang/releases/download/v2025.6.1/slang-2025.6.1-macos-aarch64.zip"
# Windows 上由 VulkanSDK 自带，不下载

[paks]
repo = "gameknife/gkNextEngine"
release_tag = "paks-latest"

[[paks.assets]]
id = "ldraw"
name = "ldraw.pak"
dest = "assets/paks/ldraw.pak"

[[paks.assets]]
id = "optional"
name = "optional.pak"
dest = "assets/paks/optional.pak"

# sfx/ffmpeg 同样照抄旧 manifest

[targets]
default = "gkNextRenderer"
all = [
  "gkNextRenderer", "gkNextEditor", "gkNextStillBenchmark", "gkNextMotionBenchmark",
  "gkNextVisualTest", "MagicaLego", "KongLie3D", "Brotato3D", "FlappyCpp", "FlappyJs",
  "BrickPlayer", "CharacterDemo", "Packager", "gkNextUnitTests"
]
```

CMake 不直接消费这个 toml；gnb 在 `setup` 阶段把外部包准备到 `external/`，然后 `cmake/SetupExternalLibs.cmake` 改为 **只检测路径不下载**（`FetchContent` 替换为 `set + 路径校验`）。这样 cmake 不再依赖网络。

### 1.6 二进制分发策略

- **CI 产物**：在仓库新增 `.github/workflows/gnb-release.yml`，编译 `gnb-windows-amd64.exe / gnb-linux-amd64 / gnb-macos-arm64 / gnb-macos-amd64` 并上传到 release tag `tools-gnb-vX.Y.Z`。
- **本地 bootstrap**：仓库根目录留 **极薄** 的 `gnb.bat` / `gnb.sh`：
  - 检查 `tools/gnb-bin/<platform>/gnb`，没有则 curl 下载到该目录。
  - 然后转发所有参数。
  - 这层 shim 文件 < 30 行，不再做业务。
- **进阶**：用户也可以 `go install` 一次性装到 PATH，或运行 `gnb install`。

---

## 2. 删除 / 保留 / 新增清单

### 2.1 删除（实施完毕后仓库不再保留）

| 路径 / 内容 | 备注 |
| --- | --- |
| `build.bat` / `build.sh` | gnb 完整接管；不留过渡 shim |
| `run.bat` / `run.sh` | gnb run 替代 |
| `scripts/build.ps1` | 业务进 gnb |
| `scripts/run.ps1` | 同上 |
| `scripts/vcpkg.{sh,bat,ps1}` | 同上 |
| `scripts/fetch-paks.{sh,bat,ps1}` | 同上 |
| `scripts/publish-paks.{sh,bat,ps1}` | 同上 |
| `scripts/package.bat` | gnb package 替代 |
| `tools/fetch_slang_linux.sh` | gnb setup 替代 |
| `CMakePresets.json` 中 `*-minimal` / `*-full` / `features-*` | preset 矩阵收敛 |
| `CMakePresets.json` 中 `mingw-base` / `*-mingw` | MinGW 整体下线 |
| `cmake/SetupExternalLibs.cmake` 中所有 `FetchContent_*` 块 | 改为路径校验 |
| `cmake/SetupExternalLibs.cmake` 中 OIDN 整段 | OIDN 完全移除 |
| `CMakeLists.txt` 中 `option(ENABLE_DLSS / ENABLE_KTX2 / ENABLE_PHYSIC / ENABLE_AUDIO / ENABLE_OIDN / ENABLE_QUICKJS / ENABLE_WHISPERCPP)` | 7 个 option 静态化或删除（`ENABLE_AVIF` 保留） |
| `cmake/ProjectOptions.cmake` 中 `if (MINGW) ... endif()` 整段 | MinGW 下线 |
| `src/CMakeLists.txt` 中 `if (WITH_OIDN)` 分支与 `WITH_OIDN` 引用 | OIDN 死代码 |
| `src/CMakeLists.txt` 中 `if (MINGW) / elseif (MINGW)` 分支、`(UNIX OR MINGW)` 表达式 | MinGW 下线 |
| `src/` 中 `#if WITH_OIDN` 代码路径 | `git rm` 干净，不留 `#if 0` |

### 2.2 保留

- `cmake/` 目录、`CMakePresets.json`（瘦身后）— gnb 仍走 preset。
- `scripts/import_brotato_placeholder.py` — 资产工具，与构建无关。
- `android/gradlew*`、`android/app/build.gradle` 中 SDL3 自动下载逻辑 — 不归本计划。
- `external/` 目录（不再 commit 内容，已在 `.gitignore` 中）。
- `ENABLE_LTO` / `ENABLE_UNITY_BUILD` / `GK_ENABLE_HOT_RELOAD` 三个 option（gnb 透传）。
- **AVIF 整套**：`ENABLE_AVIF` option（默认 OFF）、`vcpkg.json` 中 `features.avif`、`cmake/SetupDependencies.cmake` 中 `find_package(libavif)`、`src/CMakeLists.txt` 中 `if (WITH_AVIF)` 分支、`src/` 中 `#if WITH_AVIF` 宏路径。gnb 不暴露开关，需要时手工 `-DENABLE_AVIF=ON`。

### 2.3 新增

- `tools/gnb/` 整个 Go 模块。
- `gnb.toml`（仓库根目录）。
- `gnb.bat` + `gnb.sh`（仓库根目录的 bootstrap shim，< 30 行）。
- `.github/workflows/gnb-release.yml`（编译 + 发布二进制）。
- `doc/build-system-rework.md` — 即本文档。
- `doc/gnb-cli.md` — 用户手册（在 §3 Phase 7 阶段补）。

---

## 3. 分阶段实施步骤

### Phase 0 — 环境准备
1. 在 `dev` 分支基础上开 `feature/gnb-build-system`。
2. 确认仓库 clean（无未提交修改）。
3. 在本文档末尾 `## Status` 段勾选 Phase 0 完成。

### Phase 1 — gnb 项目骨架（无功能但可跑）
1. `tools/gnb/go.mod` 初始化（`module github.com/gameknife/gknextrenderer/tools/gnb`），`go 1.23`。
2. 引入 `github.com/spf13/cobra`、`github.com/BurntSushi/toml`、`github.com/schollz/progressbar/v3`。
3. 实现 `gnb` / `gnb help` / `gnb info` / `gnb doctor`（doctor 仅做版本检查，不修复）。
4. 写 `tools/gnb/README.md`：怎么 `go build -o ../../gnb`。
5. 准备 `gnb.toml` 雏形（仅 `[gnb] min_version` + `[vcpkg]` 段）。
6. CI：`.github/workflows/gnb-release.yml` 第一版只做 `go build` + 上传 artifact，**先不发 release**。

**验收**：`go run ./cmd/gnb info` 能正确打印仓库根目录、平台、git commit。

### Phase 2 — 复刻 setup（vcpkg + external）
1. 把 `scripts/vcpkg.*` 的逻辑搬进 `internal/vcpkg/`：clone / fetch tag / bootstrap / 缓存目录。
2. 实现 `internal/fetcher`：HTTPS GET（带进度条）、自动选 zip/tar/裸文件、SHA-256 校验（可选）。
3. 把 `cmake/SetupExternalLibs.cmake` 的 streamline / tsc / moltenvk / slang 下载迁到 gnb：
   - 路径仍写到 `external/<name>-<version>/`。
   - cmake 只读路径，不下载。
4. 复刻 Linux 缺包检查（`xrandr/wayland-protocols/xkbcommon`）。
5. 子命令：`gnb setup`、`gnb setup --vcpkg-only`、`gnb setup --refresh`。

**验收**：在 3 个桌面平台 clean clone 后只跑 `gnb setup` 应该把 `.vcpkg/`、`.vcpkg_bincache/`、`external/` 全建好，且 `external/streamline-*/include/sl.h` 等关键文件存在。

### Phase 3 — 构建/运行/测试
1. `gnb build [target] [--clean] [--reconfigure] [--jobs N] [--no-unity] [--lto] [--print-cmd]`。
   - 内部走 `cmake --preset <auto>` + `cmake --build --preset <auto> [--target ...]`。
   - 复用 build.sh 的"是否需要 reconfigure"启发：缓存文件存在 + 无 `-D` 增删 → 跳过。
2. `gnb run [target] [-- ...]`，复刻 run.sh 行为。
3. `gnb test [filter]` → `gkNextUnitTests <filter>`；支持 `--list-tests / --list-tags`（透传）。
4. `gnb visual` → `gkNextVisualTest`。
5. **此阶段 `cmake --preset` 仍用旧 default-* 名字**（preset 收敛在 Phase 5）。

**验收**：在 3 个桌面平台上 `gnb build && gnb run` 启动到"uploaded scene [...] to gpu"。

### Phase 4 — paks / package / 移动端
1. `gnb paks fetch / publish / list`：迁 `scripts/fetch-paks.*`、`scripts/publish-paks.*`，原 `gh` 调用 → 直接调 GitHub API（用 `GITHUB_TOKEN` env 或用户手动指定 token；`gh auth status` 仅作可选辅助）。
2. `gnb package <variant>`：替换 `scripts/package.bat`，跨平台 zip 打包，复用 `archive/zip`。
3. `gnb android` / `gnb ios`：包装 `./gradlew installAndLaunch`、`xcodebuild -project ios/...`。
4. `gnb install`：把当前二进制 cp 到 `~/.local/bin`（Unix）或 `%USERPROFILE%\bin`（Win），自动建目录并提示加 PATH。

**验收**：Windows 上 `gnb package magicalego --version v1.0.0` 产出与旧 `scripts/package.bat` 一致的 zip；Android 上 `gnb android` 能装包到设备。

### Phase 5 — CMake 瘦身
1. `CMakePresets.json` 改写为 4 + 4：
   ```jsonc
   "configurePresets": [
     { "name": "base", ...同今天的 base },
     { "name": "windows",     "inherits": "base", "generator": "Visual Studio 17 2022", "architecture": "x64",
       "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-windows-static", "CMAKE_BUILD_TYPE": "RelWithDebInfo" } },
     { "name": "linux",       "inherits": "base", "generator": "Ninja",
       "cacheVariables": { "VCPKG_TARGET_TRIPLET": "x64-linux", "CMAKE_BUILD_TYPE": "RelWithDebInfo" } },
     { "name": "macos-arm64", "inherits": "base", "generator": "Ninja",
       "cacheVariables": { "VCPKG_TARGET_TRIPLET": "arm64-osx", "CMAKE_BUILD_TYPE": "RelWithDebInfo" } },
     { "name": "ios",         "inherits": "base", "generator": "Xcode",
       "cacheVariables": { "CMAKE_SYSTEM_NAME": "iOS", "VCPKG_TARGET_TRIPLET": "arm64-ios",
                          "IOS_SKIP_CODE_SIGN": "ON", "CMAKE_BUILD_TYPE": "RelWithDebInfo" } }
   ]
   ```
   每个 configure preset 各对应一个同名 build preset。**没有 mingw 项**。
2. `CMakeLists.txt`：
   - 删除 `option(ENABLE_DLSS / ENABLE_KTX2 / ENABLE_PHYSIC / ENABLE_AUDIO / ENABLE_OIDN / ENABLE_QUICKJS / ENABLE_WHISPERCPP)` 7 个块。
   - 直接 `set(WITH_KTX2 ON)` / `set(WITH_PHYSIC ON)` / `set(WITH_AUDIO ON)` / `set(WITH_QUICKJS ON)`。
   - `WITH_WHISPERCPP`：桌面 ON，移动端 force-OFF。
   - `WITH_STREAMLINE`：`if (WIN32) set(WITH_STREAMLINE ON) else() set(WITH_STREAMLINE OFF) endif()`。
   - **`option(ENABLE_AVIF "Enable AVIF support" OFF)` 与下面的 `if (ENABLE_AVIF) set(WITH_AVIF ON ...) endif()` 保留不动**。
   - **OIDN 相关块全部删除**，包括 `if (WITH_OIDN)` 引用 `find_package` 的部分。
3. `vcpkg.json`：
   - dependencies 保持。
   - **保留 `features.avif`**。
4. `cmake/SetupExternalLibs.cmake` 改为：
   ```cmake
   # 仅校验，不下载。下载由 `gnb setup` 完成。
   if (WIN32)  # WITH_STREAMLINE 在 CMakeLists.txt 中已强制设
     file(GLOB STREAMLINE_DIRS "${CMAKE_SOURCE_DIR}/external/streamline-*")
     if (NOT STREAMLINE_DIRS)
       message(FATAL_ERROR "Streamline missing under external/. Run `gnb setup` first.")
     endif()
     list(GET STREAMLINE_DIRS 0 STREAMLINE_ROOT)
     ...
   endif()
   # tsc / moltenvk / slang 同模式
   # OIDN 块整段删除
   ```
5. `cmake/ProjectOptions.cmake`：
   - 删除 `if (MINGW) target_link_options(... -municode) ... endif()` 整段。
6. `src/CMakeLists.txt`：
   - 移除 `if (WITH_OIDN)` 分支。
   - 移除所有 `if (MINGW)` / `elseif (MINGW)` 分支：
     - `if (NOT MINGW) find_package(Catch2 ...) add_executable(gkNextUnitTests ...) endif()` 改为去掉 `if (NOT MINGW)` 包裹。
     - `elseif (MINGW) set(AllTargets <短名单>)` 删，仅保留桌面长名单。
     - `if (MINGW) target_link_libraries(${target} PRIVATE gdi32) endif()` 删。
     - `if ((UNIX OR MINGW) AND NOT APPLE AND NOT ANDROID)` → `if (UNIX AND NOT APPLE AND NOT ANDROID)`。
   - 保留 `if (WITH_AVIF)` 分支。
7. C++ 源码内 `#if WITH_OIDN` 路径一并清除（用 `git grep WITH_OIDN` 找全）。`#if WITH_AVIF` 不动。

**验收**：
- `cmake --list-presets=configure` 仅返回 4 项。
- `cmake --preset windows` 不带任何 `-D` 参数即可 configure 通过，AVIF 默认 OFF。
- `cmake --preset windows -DENABLE_AVIF=ON` 仍能拉 `libavif` 并编译通过（手动 smoke test）。
- `gnb build` 在三平台跑通；产物功能与旧 `full-windows` / `default-linux` / `default-macos-arm64` 等价（OIDN 缺失符合预期）。

### Phase 6 — CI 切换
1. 替换所有 `.github/workflows/*.yml`：
   - 删除 `Compile vcpkg dependencies` 步骤（gnb setup 顺手做）。
   - `actions/cache` 对 `.vcpkg / .vcpkg_bincache` 的逻辑保留，cache key 由 gnb 计算（`os + vcpkg_ref + vcpkg.json hash`），通过 `gnb info --bincache-key` 输出。
   - 改为：
     ```yaml
     - run: ./gnb setup
     - run: ./gnb build
     ```
2. `gnb-release.yml` 升级为 tag 触发 + 发布到 `tools-gnb-vX.Y.Z`。
3. `release.yml` / `release_magicalego.yml` 中 `tar / zip` 自定义命令改为 `gnb package <variant>`。
4. 把 `windows_self.yml`、`android_self.yml` 同步迁移。
5. 在 `gnb.bat` / `gnb.sh` shim 第一次运行时若没有 `tools/gnb-bin/<platform>/gnb`，自动从最新 `tools-gnb-v*` release 拉取。CI 环境下也走这条路径，无需特殊处理。

**验收**：所有 CI 绿；`.vcpkg_bincache` 缓存命中率不降。

### Phase 7 — 文档
1. 更新 `README.md` / `README.en.md`：移除 preset 矩阵，改为：
   ```bash
   ./gnb setup        # 一次
   ./gnb build        # 日常
   ./gnb run          # 跑起来
   ```
   并在 README 加一行 "AVIF / 其他可选能力请见 `doc/gnb-cli.md`"。
2. 更新 `AGENTS.md`（`## Build Commands`、`## Run Commands`、`## Verification After Changes`）：去掉 preset 概念。
3. 更新 `AGENT_GUIDE/quick-commands.md`、`core-patterns.md`。
4. 新增 `doc/gnb-cli.md`：完整命令手册，每条带 1 个示例；末尾附 "AVIF 手动启用" 章节。

**验收**：新人按 README 0 干预完成 build & run。

### Phase 8 — Legacy 收尾
1. `git rm` 掉 §2.1 列出的所有路径与代码块。
2. 同步 `.clang-tidy`、`tools/clang-tools/run-clang-tidy.py` 等仍引用旧路径的工具。
3. 更新 `.gitignore`：忽略 `tools/gnb-bin/`。
4. 在 `doc/build-system-rework.md` `## Status` 中勾选 Phase 8 完成。

### Phase 9 — Release & Verification
1. 在每个桌面平台手测：
   - `git clean -ffdx` → `./gnb setup` → `./gnb build` → `./gnb run` 全过程。
   - `./gnb test`、`./gnb visual`。
   - `./gnb package <platform>`，解压后能直接运行。
   - **AVIF smoke test**：`cmake --preset windows -DENABLE_AVIF=ON` → `gnb build` 通过。
2. 移动端：
   - Android: `./gnb android` 能装包并跑通。
   - iOS: `./gnb ios` xcodebuild 通过（CI 用 `--skip-codesign`）。
3. 用 `du -h gnb*` 检查二进制大小（目标 < 10 MB stripped）。
4. 打 tag `tools-gnb-v0.1.0` 触发首次正式发布。

---

## 4. 验收标准（可勾选）

- [ ] 仓库根目录可执行入口仅 `gnb.bat / gnb.sh`（< 30 行）+ 一条 `gnb` 二进制 bootstrap 链路。
- [ ] `cmake --list-presets=configure` 只返回 4 行（windows / linux / macos-arm64 / ios）。
- [ ] `vcpkg.json` 仅保留 `features.avif`，其它 feature 字段不存在。
- [ ] `CMakeLists.txt` 中 `option(ENABLE_*)` 仅剩 `ENABLE_AVIF / ENABLE_LTO / ENABLE_UNITY_BUILD`。
- [ ] `git grep WITH_OIDN` 与 `git grep MINGW`（项目代码部分）均无结果。
- [ ] `git grep WITH_AVIF` 仍能命中（保留）。
- [ ] `scripts/` 仅剩 `import_brotato_placeholder.py`。
- [ ] CI 全部 green，且 `.vcpkg_bincache` 缓存命中。
- [ ] 二进制 `gnb` 在三个桌面平台 `< 10 MB`（stripped）。
- [ ] `./gnb` 单命令输出引导帮助，新人 < 5 分钟跑出场景。
- [ ] `cmake --preset windows -DENABLE_AVIF=ON` + `gnb build` 一次性通过（AVIF 手动通路有效）。
- [ ] `doc/gnb-cli.md` 覆盖全部子命令，每条带 1 个示例；含 AVIF 手动启用说明。

---

## 5. 风险 & 应对

| 风险 | 影响 | 应对 |
| --- | --- | --- |
| Go 工具链对部分用户陌生 | 用户改 gnb 时需要装 go | 把 `gnb` 当作"编译产物 + 二进制"分发，绝大多数用户不需要装 go |
| FetchContent 退出 → 构建期网络断了 | configure 失败而非 setup 失败 | gnb setup 必须把 external 下完整；cmake 仅校验路径，给出清晰错误提示"请运行 gnb setup" |
| Windows 上 PowerShell 执行策略 | shim `gnb.bat` 不再依赖 PowerShell | 直接用 cmd + curl/iwr fallback，保持 Win10+ 兼容 |
| 二进制版本与仓库 cmake 不兼容 | gnb 升级后老分支用不了 | `gnb.toml [gnb] min_version`，二进制启动检查不达标即报错 |
| CI 缓存 key 变化 | 第一次 CI 走全量编译 ~30min | 接受一次性损耗；之后 key 用 `os + vcpkg_ref + manifest hash` 与现状等价 |
| 删除 OIDN / MinGW 后用户回滚需求 | 短期内无需求；长期需重新加 | 重新加时按"新增第三方依赖 / 平台"流程：往 vcpkg.json 加，往 `gnb.toml` 加 fetcher，往 cmake 加 `find_package` / preset |
| AVIF 手动通路被遗忘 | 用户不知道怎么开 | `doc/gnb-cli.md` 末尾固定章节给一行示例 `cmake --preset windows -DENABLE_AVIF=ON && ./gnb build` |

---

## 6. 给 codex 的执行约束

1. **每个 Phase 单独 PR**。Phase 1/2/3 串行；Phase 4 与 Phase 5 可并行；Phase 6 必须等 1–5 全部合入。
2. 每个 PR 都要在本文件 `## Status` 段勾选对应 Phase。
3. 任何对 CMake/preset 的改动都要在 `full-windows` / `default-linux` / `default-macos-arm64` 三个旧 preset 下做完整 build 验证（直到 Phase 5 完成才允许换名）。
4. gnb 二进制在 Phase 1 ~ Phase 4 阶段允许通过 `go run ./cmd/gnb` 直接跑；Phase 5 起必须支持 `./gnb` 直接调用（即仓库根有可用的 `gnb.bat` / `gnb.sh`）。
5. 不要在迁移过程中调整 `src/`、`assets/` 内的功能代码；仅 build 系统、被 OIDN/MinGW 引用的死代码、以及 build 文档允许变更。**AVIF 相关代码一律不动。**
6. 删除文件时一律 `git rm`，不要留 `.deleted` / `.bak`。
7. 新代码沿用项目根 `AGENTS.md` 的命名规范（gnb 自身是 Go，不受 C++ 规范约束，遵循 Go 社区惯例 + `gofmt`）。
8. **不留过渡 shim**。Phase 8 一次性删掉旧 `build.{bat,sh}` / `run.{bat,sh}` / `scripts/*`。

---

## 7. 时间估算（参考，不强制）

| Phase | 估时（人日） |
| --- | --- |
| 0 环境准备 | 0.2 |
| 1 骨架 | 1 |
| 2 setup | 2 |
| 3 build/run/test | 1.5 |
| 4 paks/package/移动端 | 1.5 |
| 5 CMake 瘦身 | 1 |
| 6 CI 切换 | 1 |
| 7 文档 | 0.5 |
| 8 Legacy 收尾 | 0.5 |
| 9 验收 + release | 1 |
| **合计** | **~10.2 人日** |

---

## 8. 旧命令 → 新命令对照表

| 旧命令 | 新命令 |
| --- | --- |
| `./build.sh --preset default-linux` | `./gnb build` |
| `./build.bat --preset full-windows` | `./gnb build` |
| `./build.bat --preset default-windows -- -DENABLE_AVIF=ON` | `cmake --preset windows -DENABLE_AVIF=ON` 后再 `./gnb build`（gnb 不直接暴露 AVIF 开关） |
| `./build.sh --preset default-macos-arm64 --clean` | `./gnb build --clean` |
| `./build.sh --preset default-linux --reconfigure` | `./gnb build --reconfigure` |
| `./build.sh --android` | `./gnb android` |
| `./build.{bat,sh} --preset *-mingw` | 已删除支持（MinGW 整体下线） |
| `scripts/vcpkg.sh --update` | `./gnb setup --refresh` |
| `scripts/fetch-paks.sh --all` | `./gnb paks fetch` |
| `scripts/publish-paks.sh --all` | `./gnb paks publish` |
| `scripts/package.bat local` | `./gnb package windows` |
| `scripts/package.bat magicalego v1.0.0` | `./gnb package magicalego --version v1.0.0` |
| `./run.sh --preset default-linux --target gkNextEditor` | `./gnb run gkNextEditor` |
| `./run.bat --preset full-windows -- --scene=foo` | `./gnb run -- --scene=foo` |
| `./out/build/<preset>/bin/gkNextUnitTests "[Unit]"` | `./gnb test "[Unit]"` |
| `./out/build/<preset>/bin/gkNextVisualTest` | `./gnb visual` |
| `cmake --list-presets=configure` | `./gnb info` |

---

## Status

- [x] Phase 0 — 环境准备
- [x] Phase 1 — gnb 骨架
- [x] Phase 2 — setup
- [x] Phase 3 — build/run/test
- [x] Phase 4 — paks/package/移动端
- [x] Phase 5 — CMake 瘦身
- [x] Phase 6 — CI 切换
- [x] Phase 7 — 文档
- [x] Phase 8 — Legacy 收尾
- [ ] Phase 9 — 验收 & release
