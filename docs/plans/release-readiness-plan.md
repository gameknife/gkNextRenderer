# 阶段性 Release 准备计划

面向"对外发布 Release 版本 + 推广引擎"这一目标的现状盘点与执行计划。
发布物范围：**gkNextRenderer / gkNextEditor / gkNextMotionBenchmark** 三个桌面 target，
平台优先级 Windows > Linux > macOS。

本轮**不新增引擎功能**，只做发布相关的收口：能跑通的发布流程、能看的首屏体验、
能对外的仓库合规、以及和代码一致的文档。

> 排查基线：`dev` @ `7754a8b8`，Windows / RTX 5070 Ti / 显示缩放 125%。
> 下列结论均已在本机实测或定位到具体代码行；未验证的推测会显式标注"待确认"。

---

## 0. 现状快照

| 项 | 现状 |
|---|---|
| 首方代码规模 | 930 文件 / 183,146 行（Engine 36,193；Modules 39,055；Application 93,710；Gameplay 5,009；Tests 9,179） |
| 单元测试 | 301 cases，**300 通过 / 1 失败** |
| CI | `desktop.yml` 三平台只 build 不 test；`release.yml` 只有 android/linux/windows，无 macOS |
| Release 打包 | `gnb package <variant>` 整目录打包 `bin/`（本机 13 GB，含 20+ 子项目 exe 与 pdb/ilk） |
| 构建配置 | 所有 preset 都是 `RelWithDebInfo`，没有真正的 `Release` 配置 |
| 最近 tag | `v0.1.1.0` |
| 首方 TODO/FIXME | 约 15 处（ThirdParty 除外），密度健康，非本轮重点 |

**结论：代码本身的整洁度不是瓶颈，瓶颈在"发布这条链路"和"用户第一次打开看到什么"。**

---

## 1. 阻断级问题（不修就不该发）

### R-1 发布流水线根本没有构建要发的 target

`release.yml` 的 linux/windows job 都执行 `gnb build`（无参数）。而 `gnb build` 无参数时
只构建 `gkNextRenderer` + `gkNextUnitTests`：

- `tools/gnb/cmd/gnb/main.go:332`
- `.github/workflows/release.yml:66`（linux）、`.github/workflows/release.yml:88`（windows）

**即：本次要发布的 gkNextEditor 和 gkNextMotionBenchmark 从来没进过 release 包。**
对照 `release_magicalego.yml:35` 是显式 `gnb build MagicaLego`，模式是现成的，主流程没用上。

### R-2 打包清单不可控，且缺运行期必需资产

`tools/gnb/internal/packager/packager.go:24-34` 把整个 `bin/` 目录塞进 zip：
所有子项目 exe、`.pdb`、`.ilk` 全都在内。同时资产白名单只有
`assets/{brand,locale,shaders,textures,fonts,models,paks}`，**漏掉了运行期真实依赖的**：

| 缺失目录 | 谁在读 |
|---|---|
| `assets/configs` | `Engine.cpp:321`（cvar_default.json）、`SettingsPanel.cpp:135`（Editor 设置面板 schema）、`EditorMain.cpp:87` |
| `assets/scripts` | MagicaLego `.mlscript`（脚本运行时已随 QuickJS 删除，见 `docs/plans/dotnet-scripting-plan.md`） |
| `assets/remote` | Remote Play 浏览器客户端，缺了 `--remote` 直接 404 |
| `assets/sounds` / `assets/anims` | NextAudio / 动画资产 |

### R-3 Linux 包用 `-march=native` 编译

`src/cmake/TargetHelpers.cmake:138-140`：

```cmake
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_compile_options(${target} PRIVATE -march=native -mavx)
endif()
```

在 GitHub Actions runner 上编译 = 按 runner 的 CPU 指令集编译。用户 CPU 较旧时
**直接 SIGILL 崩溃**，且崩得毫无线索。发布构建必须固定一个基线指令集。

### R-4 启动期异常没有兜底，任何失败都是硬崩

`src/DesktopMain.cpp` 的 `SDL_AppInit` 没有 try/catch，而 `Throw()`
（`src/Engine/Utilities/Exception.hpp`）打完栈就往外抛 → `std::terminate` → Windows 崩溃框。
讽刺的是 `DesktopMain.cpp:76-96` 有一段 `std::set_terminate` 的实现，**整段被注释掉了**。

会走到这条路径的真实场景：缺 Vulkan 扩展（`Device.cpp:301`）、找不到 present queue
（`Device.cpp:103`）、显存不足、资产缺失。对"随便什么显卡都可能有人下载"的公开 Release，
这是最高优先级的一条。

### R-5 资产缺失时的降级路径本身会崩（已实测复现）

`src/Engine/Assets/GPU/Texture.cpp:109-134`，HDR 贴图找不到时走"占位环境"分支，
但传下去的是 `nullptr, 0`：

```cpp
return GetInstance()->RequestNewTextureMemAsync(
    filename, "image/hdr", true, nullptr, 0, false, ETextureLifetime::ETL_Persistent);
```

**实测**：按当前 `gnb package windows` 的资产白名单搭一个 staging 目录跑 `gkNextRenderer`，
9 张 HDR 落到占位分支后立刻：

```
[warning] HDR texture 'assets/textures/canary_wharf_1k.hdr' is unavailable; using a placeholder environment.
...
[error] Exception: failed to allocate VMA image memory (VkResult(-3))
```

随后进程异常退出。同一二进制在完整 `out/build/windows` 目录下运行正常 —— 说明
**这是降级路径的 bug，不是环境问题**。下载不完整、解压漏文件的用户会撞上它。

### R-6 有一个单元测试是挂的，而 CI 从不跑测试

`src/Tests/Test_GnbAIClient.cpp:20`：

```cpp
const auto fixtureDir = SourceRoot() / "tests" / "fixtures" / "gnb-ai-protocol" / "v2";
```

fixture 实际在 `tools/tests/fixtures/gnb-ai-protocol/v2/`（5 个 `.ndjson` 都在），
路径少了一层 `tools`。`desktop.yml` 只有 setup + build，没有 `gnb test`，所以一直没人发现。

---

## 2. 首次体验与 UI（推广的第一印象）

### U-1 高 DPI 下工具栏和面板被裁切

125% 缩放是 Windows 笔记本的出厂默认，绝大多数下载者会命中。仓库已有
`UiScale()` 基础设施（`AirportSim`/`Brotato3D`/`ScadLibrary` 在用），但两个发布 target
的视口 UI 全是**未经缩放的硬编码像素**：

| 位置 | 代码 | 现象 |
|---|---|---|
| Renderer 顶部工具栏 | `gkNextRenderer.cpp:1910-1917`（88/108/154/150/188px 固定宽） | "Realtime" 按钮文字被裁 |
| Renderer 快捷键面板 | `gkNextRenderer.cpp:2175`（`panelHeight = 450.0f`） | 最后一行 "Q / Toggle Local·World" 被从中间切断，且无滚动条 |
| Editor 视口工具栏 | `ViewportOverlay.cpp:189`（`std::min(700.0f, ...)`，内容实际 >738px） | 最右侧 Camera 下拉被裁成 "Ca…" |

### U-2 Renderer 首屏是信箱化的近正方形画面

默认场景 `CornellBox.proc`（`gkNextRenderer.cpp:581`）渲染出来只占窗口约 54% 宽度，
左右各一大块深灰空白。作为"打开就截图发推"的第一帧，观感明显吃亏。
需要决定：换默认场景（playground / conf_room 展示力更强），或让 proc 场景跟随窗口宽高比。

### U-3 Editor 首次启动是空场景

`EditorMain.cpp:203` 只在 `--load-scene` 有值时加载。Release 用户双击 `gkNextEditor.exe`
看到的是**全黑视口 + Outliner 里只有一个 Environment 节点**。需要默认场景或欢迎页。

### U-4 Editor 顶部工具栏大部分是装饰件

`EditorInterface.cpp:212-267`：

- `##BackendSelector` 列出 `Vulkan / Metal / DirectX 12` —— 引擎只有 Vulkan，**对外是误导**
- `##ProjectSelector`（RayQuery/Playground）、`##PlatformSelector`（Desktop/Android/iOS）、
  `##BuildConfigSelector`（Development/Debug/Shipping）：全仓库检索确认**只写不读**
- 右上角 "GK" 头像圆圈 + tooltip "User"：纯占位
- Play 按钮：`std::system(cmdline)` 同步阻塞（Editor 卡死到子进程退出），
  路径未加引号（带空格的安装路径会断），且拼的是 `gkNextRenderer` 无扩展名

### U-5 Editor 视口工具栏同样有 5 个装饰件

`ViewportOverlay.cpp:233-255` 的 `projectionMode` / `displayMode` / `cameraIndex` /
`angleSnap` / `distanceSnap`，在 `EditorUiState.hpp:86-90` 定义，全仓库只有写入没有读取。
注意 View 菜单里有**真正可用**的 Viewport Display Mode（`TitleBarOverlay.cpp:255-280`），
两套 UI 语义重复且其中一套是假的。

### U-6 Editor Build 菜单三项全灰

`TitleBarOverlay.cpp:320-328`：Cook Assets / Package Project / Launch Renderer 都是
`MenuItem(..., false, false)`。

### U-7 Help 菜单前后不一致且有模板残留

- Renderer：`gkNextRenderer.cpp:2365-2367` 的 Documentation 和 About 都是禁用状态
- Editor About：`EditorUtils.cpp:303-323` 标题写成 **"gkNextRenderer"**（应为 gkNextEditor），
  内容只有 ImGui / Fmt 版本号
- Editor Help > Resources：`EditorUtils.cpp:195-300` 是**整页 Dear ImGui 生态链接**
  （awesome-dear-imgui、ImFileDialog、hello_imgui…）+ 彩虹色 header + 一个永不编译的
  `__EMSCRIPTEN__` 分支。这是从 ImGui 模板抄来的残留，对外发布时是明显的减分项

### U-8 Editor 视口常驻内部调试读数

`ViewportOverlay.cpp:338` 的 `AC Bricks: 0/10368` 一直挂在视口右上角。属于引擎内部的
AmbientCube 驻留统计，对普通用户是噪音。

### U-9 视频录制功能在任何 Release 里都不可用

`src/cmake/TargetHelpers.cmake:185-191` 从 `src/ThirdParty/ffmpeg/bin/ffmpeg.exe` 拷贝，
而 `.gitignore:409` 忽略 `src/ThirdParty/*/bin/` —— **CI 上这个文件永远不存在**。
`ScreenShotService.cpp:109` 找不到就只写一行 error log，但 Screenshot 菜单里的录制项照常可点。
另外该路径硬编码 `.exe`，Linux/macOS 从未支持过。

### U-10 本地化只有骨架

`assets/locale/`：en 53 行、zhCN 100 行、RU 27 行，而 UI 里绝大多数字符串是硬编码英文。
RU 基本是空壳。发布前需要决定：明确对外声明"UI 为英文"，还是收敛掉半成品语言。

---

## 3. 仓库与发布物合规

### C-1 README 里有 VPN 推荐和个人邀请码

`README.md:174-178` / `README.en.md:176-179`，在"快速开始"的第一屏：

```
[带邀请码链接](https://nxonearth.com/signupbyemail.aspx?MemberCode=93e1edc9...)
```

英文 README 也带同一条。对以海外读者为主的 star 增长来说，这在技术文档里出现会被读成推广内容。

### C-2 缺第三方 attribution 清单

`LICENSE` 只有一句"third-party libraries ... are subject to their respective licenses"，
但 `README.md:416` 写的是"第三方库的源代码详见 LICENSE 中的第三方声明"——实际没有声明。
需要清单的至少有：随包分发的 7 个字体（Roboto / Cousine / DroidSansFallback 为 Apache-2.0，
FontAwesome 为 OFL + CC BY）、NVIDIA Streamline / DLSS（`bin/*.license.txt` 已随包，
但目录一旦收敛就要显式保留）、以及 vcpkg 依赖与 `src/ThirdParty/` 各库。

### C-3 iOS 构建写死个人签名身份

`src/Application/Render/gkNextRenderer/CMakeLists.txt`：
`XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "LWJL2YZH4A"`、
`XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "nz.new.dev.wildcard"`。
公开仓库里的个人开发者信息，应改为变量或缓存项。

### C-4 解压包里没有任何说明

zip 解出来是 `bin/` + `assets/`，没有 README、没有 LICENSE、没有启动脚本。
`bin/` 里如果还是当前的 100 个文件，用户根本不知道点哪个。

### C-5 写入目录 = 安装目录

`FileHelper.hpp:34-43` 的 `GetWritableRuntimeRoot()` 桌面端返回 exe 的上级目录，
`imgui.ini` / `cvar_user.json` / `screenshots/` / `cooked/` / `recent_scenes.txt` 全写在那。
装到 `C:\Program Files` 或只读介质下会静默失败。

### C-6 没有文件日志

桌面端只有 console sink（`Engine.cpp:267`），只有 TUI 模式落盘
（`NextTuiModule.cpp:92` → `logs/tui.log`）。发布后用户报 bug 无 log 可附。

### C-7 包命名与平台覆盖不一致

`packager.go:24` Windows 输出 `gkNextRenderer-windows.zip`（**无版本号**），
linux/macos 则带版本号。`release.yml` 有 macOS 的打包分支代码但没有 macOS job。

### C-8 `--help` 是开发者视角

`src/Engine/Options.cpp` 把 `--agent-validation` / `--agent-control-token` / `--flappy-replay` /
`--update-baseline` / `--test-gltf` / 全套 `--tui-*` 和用户参数平铺在一起。另外：

- `Options.cpp:41` `--hwquery` 的说明文案是从 `--forcenort` 复制来的（"Forcing hardware raytracing not supported."），描述错误
- `Options.cpp:201` 参数解析失败后 `exit(0)`，应返回非 0
- `DesktopMain.cpp:187-190` `--test-gltf` 在桌面 target 上只打一行 warning，是死的 CLI 面

---

## 4. 文档一致性

| 问题 | 位置 |
|---|---|
| 模块数三个版本不一致：AGENTS.md 写 16、`src/Modules/README.md` 写 17、实际 18 个目录 | `AGENTS.md`、`src/Modules/README.md:12` |
| preset 名写错：AGENTS.md 写 `windows-vs`，实际是 `windows-vcproj` | `AGENTS.md`、`CMakePresets.json:42` |
| LOC 数字过期：AGENTS.md 记 Engine 31k / 合计 141,421，实际 36,193 / 183,146 | `AGENTS.md` |
| 子项目清单缺 NextDayz / NextTotalwar / CitySolSim / Voyage3D，`gkNextBenchmark` 目录名与文中 target 名对不上 | `AGENTS.md` |
| `docs/README.md` 索引漏 9 个文档（7 个 plan + `scad-terrain-design.md` + `dozen-vulkan-backend-troubleshooting.md`） | `docs/README.md` |
| README 性能表来自本机 `motion_benchmark_report.csv`，未标注为可复现的固定基线 | `README.md:82-101` |
| 没有 Release 流程文档（打 tag、验证清单、发什么、release notes 模板） | 缺失 |

---

## 5. 执行计划

任务按批次组织，**每个任务都小到可以独立完成、独立验证、独立提交**。
批次内任务大多可并行；批次之间建议按顺序推进。

### 批次 A：发布流水线打通（先做，后面所有验证都依赖它）

| ID | 任务 | 涉及 | 验收 |
|---|---|---|---|
| A-1 | `gnb package` 改为按显式 target 清单打包，不再整目录塞 `bin/`；排除 `.pdb` / `.ilk` | `packager.go` | 包内只有 3 个 exe + 必要 DLL/license |
| A-2 | 打包资产白名单补 `configs` / `scripts` / `remote` / `sounds` / `anims` | `packager.go` | 见 A-6 冒烟 |
| A-3 | `release.yml` 显式构建三个 target（照抄 `release_magicalego.yml` 的写法） | `.github/workflows/release.yml` | CI 产物里三个 exe 都在 |
| A-4 | Windows 包名补版本号；补 macOS release job | `packager.go`、`release.yml` | 三平台命名一致 |
| A-5 | Linux 发布构建去掉 `-march=native`，改固定基线（如 `x86-64-v2`），保留本地开发可选开关 | `TargetHelpers.cmake` | `objdump` 确认无越界指令集 |
| A-6 | 新增"解压即用"冒烟脚本：从干净目录解压 → 启动三个 target → 校验退出码与关键日志 | 新增 `gnb` 子命令或 CI step | 三平台通过 |
| A-7 | zip 内补 `README.txt`（启动方式 / 系统要求 / 已知问题）、`LICENSE`、`THIRD-PARTY-NOTICES` | `packager.go` | 解压后自解释 |

### 批次 B：崩溃与降级兜底（决定"能不能给陌生人用"）

| ID | 任务 | 涉及 | 验收 |
|---|---|---|---|
| B-1 | `SDL_AppInit` 包 try/catch，启动失败弹 `SDL_ShowSimpleMessageBox` 说明原因 + 日志路径，非 0 退出 | `DesktopMain.cpp` | 手动断掉 Vulkan 扩展验证 |
| B-2 | 启用（或删除）注释掉的 `std::set_terminate`，二选一，不留死代码 | `DesktopMain.cpp:76-96` | — |
| B-3 | 修 HDR 占位贴图路径：生成真实 1×1 或 4×4 占位数据，不再传 `nullptr, 0` | `Texture.cpp:109-134` | 删掉 `assets/paks` 后能正常启动 |
| B-4 | 设备能力不满足时给出可读提示（缺哪个扩展 / 建议驱动版本），而非 `Throw` | `Device.cpp:301`、`VulkanBaseRenderer.cpp` | — |
| B-5 | 加桌面文件日志 sink（`logs/<app>.log`，带轮转），About 对话框显示日志路径 | `Engine.cpp:267` 附近 | 运行后有日志文件 |
| B-6 | 修 `Test_GnbAIClient.cpp:20` 的 fixture 路径 | `src/Tests/` | `gnb test` 全绿 |
| B-7 | `desktop.yml` 增加 `gnb test` step | `.github/workflows/desktop.yml` | CI 跑测试 |
| B-8 | 可写目录改为用户目录（`SDL_GetPrefPath`），保留 portable 模式回退 | `FileHelper.hpp:34-43` | 只读安装目录下可运行 |

### 批次 C：UI 收口（推广观感）

| ID | 任务 | 涉及 | 验收 |
|---|---|---|---|
| C-1 | Renderer 顶部工具栏所有硬编码宽度接入 `UiScale()` | `gkNextRenderer.cpp:1910-1917` | 100/125/150/200% 各截一张图 |
| C-2 | 快捷键面板高度改为自适应 + 溢出滚动 | `gkNextRenderer.cpp:2164-2264` | 同上 |
| C-3 | Editor 视口工具栏宽度接入 `UiScale()`，或溢出折叠进"更多" | `ViewportOverlay.cpp:189` | 同上 |
| C-4 | 删除 Editor 顶部工具栏的装饰件（Backend / Platform / BuildConfig / 头像），保留真实可用项 | `EditorInterface.cpp:212-267` | 无只写不读的状态 |
| C-5 | Play 按钮改异步启动 + 路径加引号 + 带平台扩展名；失败时给提示 | `EditorInterface.cpp:226-231` | 带空格路径下可用 |
| C-6 | 删除 Editor 视口工具栏 5 个装饰件（与 View 菜单里的真实实现重复） | `ViewportOverlay.cpp:233-255`、`EditorUiState.hpp:86-90` | — |
| C-7 | Build 菜单：要么实现，要么整个隐藏 | `TitleBarOverlay.cpp:320-328` | 无常灰菜单 |
| C-8 | 统一 About 对话框：正确 app 名、引擎版本、构建日期、GPU/驱动、渲染器、日志路径、License 与项目链接；Renderer 侧接上 Documentation | `EditorUtils.cpp:303-323`、`gkNextRenderer.cpp:2365-2367` | 两个 target 一致 |
| C-9 | 删除 Help > Resources 的 ImGui 模板残留（含 `__EMSCRIPTEN__` 死分支），换成项目自己的链接 | `EditorUtils.cpp:195-300` | — |
| C-10 | `AC Bricks` 等内部读数移到 Stats / 调试开关后 | `ViewportOverlay.cpp:309-345` | 默认视口干净 |
| C-11 | 决定 Renderer 默认场景与画面比例（换场景 or proc 场景跟随窗口比例） | `gkNextRenderer.cpp:581` | 首帧铺满窗口 |
| C-12 | Editor 首次启动加载默认场景或欢迎页 | `EditorMain.cpp:203` | 首屏非空 |
| C-13 | ffmpeg：由 `gnb setup/deps` 统一提供，或在缺失时禁用录制菜单并给出说明 | `TargetHelpers.cmake:185-191`、`ScreenShotService.cpp:109` | Release 包里行为明确 |
| C-14 | 整理 `--help`：分组（常用 / 渲染 / 诊断 / 自动化），修 `--hwquery` 描述，解析失败返回非 0，移除死的 `--test-gltf` | `Options.cpp`、`DesktopMain.cpp:187-190` | `--help` 可直接贴进 README |
| C-15 | 本地化决策：明确对外语言，收敛 RU/zhCN 半成品或补齐 | `assets/locale/` | 无混排 |

### 批次 D：合规与文档

| ID | 任务 | 涉及 | 验收 |
|---|---|---|---|
| D-1 | 移除 README（中英）里的 VPN 推荐与邀请码，改为中性的网络前置条件说明 | `README.md:174-178`、`README.en.md:176-179` | — |
| D-2 | 新增 `THIRD-PARTY-NOTICES.md`（字体 / Streamline·DLSS / vcpkg 依赖 / `src/ThirdParty`），README 指向它 | 新增 | 随包分发 |
| D-3 | iOS 签名身份改为 CMake 缓存变量，默认空 | `gkNextRenderer/CMakeLists.txt` | 公开仓库无个人身份 |
| D-4 | 新增 `docs/guides/release-process.md`：打 tag → CI → 验证清单 → 产物 → release notes 模板 → 回滚 | 新增 | 下次发布照做即可 |
| D-5 | 同步 AGENTS.md：模块数、preset 名、LOC、子项目清单、benchmark target 名 | `AGENTS.md`、`src/Modules/README.md` | 与代码一致 |
| D-6 | `docs/README.md` 补齐 9 个未索引文档，或按生命周期规则归档 | `docs/README.md` | 无游离文档 |
| D-7 | 在固定硬件重跑 `gkNextMotionBenchmark`，更新 README 性能表并注明硬件/驱动/版本 | `README.md:82-101` | 数据可复现 |
| D-8 | 清理 `.gitignore` 里已移除组件的残留条目（`src/ThirdParty/oidn/` 等） | `.gitignore:409-411` | — |

### 批次 E：发布执行

| ID | 任务 | 验收 |
|---|---|---|
| E-1 | 三平台干净机器验收：解压 → 启动三个 target → 切换全部渲染器 → 截图 → 退出，全程无崩溃无报错 | 验收记录归档 |
| E-2 | 高 DPI 矩阵验收：100 / 125 / 150 / 200%，两个 GUI target 无裁切 | 截图对比 |
| E-3 | 无硬件光追设备上验证 `ResolveRendererType` 回退链路 | 日志确认 |
| E-4 | 跑一遍 `gkNextVisualTest`，确认无视觉回归 | report 通过 |
| E-5 | 写 release notes（亮点 / 已知问题 / 系统要求 / 反馈入口），打 tag 发布 | — |

---

## 6. 建议的推进顺序

```
A-1..A-3  ──►  B-1,B-3,B-6,B-7  ──►  A-6,A-7  ──►  C-1..C-3  ──►  C-4..C-14
   打通                兜底              冒烟           DPI            UI 收口
                                                                        │
                                          D-1..D-8 ◄────────────────────┘
                                          （可全程并行）
                                              │
                                              ▼
                                          E-1..E-5 发布
```

**最小可发布集**（时间紧时的下限）：`A-1 A-2 A-3 A-4 A-5 A-6 A-7` + `B-1 B-3 B-5 B-6 B-7`
+ `C-1 C-2 C-3 C-4 C-8 C-9 C-12` + `D-1 D-2 D-4`。
其余项可以放到 Release 之后的补丁版本。

---

## 7. 验收口径

发布前逐条确认：

- [ ] 三个 target 都在包里，包体不含 `.pdb` / `.ilk` / 无关子项目
- [ ] 干净机器解压即可运行，无需 clone 仓库、无需 `gnb`
- [ ] 缺资产 / 缺扩展 / 显存不足时给可读提示并正常退出，不弹系统崩溃框
- [ ] 崩溃或异常时用户能找到日志文件
- [ ] 125% / 150% DPI 下两个 GUI 无文字裁切
- [ ] UI 上不存在只写不读的假控件和常灰菜单
- [ ] About 显示正确的 app 名、版本、硬件与 License 信息
- [ ] `gnb test` 全绿，且 CI 会跑
- [ ] README / AGENTS.md / docs 索引与代码一致
- [ ] 仓库内无个人推广链接、无个人签名身份，第三方 attribution 完整
