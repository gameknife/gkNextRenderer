---
title: "gnb CLI 速查"
category: guide
status: 现行
owner: tools
created: 2026-06-24
last_updated: 2026-07-17
---

# gnb CLI 速查

以下只描述稳定入口；完整参数始终以当前源码的 `./gnb.sh <command> --help`（Windows 为 `gnb.bat`）为准。开发 gnb 自身时不要依赖可能过期的仓库根二进制。

## 环境与构建

```bash
./gnb.sh doctor
./gnb.sh setup
./gnb.sh info
./gnb.sh build gkNextRenderer gkNextUnitTests
./gnb.sh build NextRA
./gnb.sh clean
```

`setup` 负责 vcpkg 和项目管理的外部 SDK；`build` 会按需 configure。默认优先构建受影响 target，只有 CMake/target 结构变化等情况才加 `--reconfigure`。

## 运行与验证

```bash
./gnb.sh run                         # 列目标或运行默认目标
./gnb.sh run gkNextEditor
./gnb.sh editor
./gnb.sh test
./gnb.sh visual
./gnb.sh shot --scene assets/models/playground.glb
./gnb.sh validate --script assets/agentscripts/smoke.agentscript.json
./gnb.sh tui --scene assets/models/playground.glb
./gnb.sh remote --scene assets/models/playground.glb
```

`shot` 是快速肉眼验证；`validate` 用输入脚本驱动并断言；`visual` 才是多场景 baseline 回归。Remote Play 的安全与能力边界见 [当前设计](../designs/webrtc-remoteplay-design.md)。

## 项目工具

```bash
./gnb.sh dashboard
./gnb.sh todo list
./gnb.sh loc
./gnb.sh graph
./gnb.sh paks list
./gnb.sh package <windows|linux|macos>
# 使用 gnb.toml 的 default package preset，隐藏运行其目标并生成单一 runtime.pak
./gnb.sh package windows --trace-assets --version v0.1.2.0
# 使用独立的 MagicaLego preset，但复用完全相同的 trace / runtime.pak / 7z 流程
./gnb.sh package windows --package-preset magicalego --trace-assets --version m0.1.0
# 复用多轮运行合并出的覆盖清单（也适合无 GPU / 跨平台打包环境）
./gnb.sh package windows --asset-trace out/build/windows/asset-traces/release-assets.txt --version v0.1.2.0
# 复用 Linux 追踪作业已生成的完整精确资产包（无需在本机运行 Vulkan 或构建 Packager）
./gnb.sh package windows --runtime-pak release-paks/default --version v0.1.2.0
# 可选：显式携带 gnb agent sidecar（默认不打包）
./gnb.sh package windows --include-gnb --version v0.1.2.0
./gnb.sh git status
./gnb.sh typos
```

package preset 配置在 `gnb.toml` 的 `[package.presets.<name>]`，可独立声明 `targets`、
`archive_name`、`always_include_assets` 和 `extra_files`。精确包会合并当前 preset 的
`always_include_assets`；新增产品只需增加 preset，无需修改 packager 分支。

`--runtime-pak <目录>` 接收包含 `runtime.pak`、`runtime-assets.txt` 与
`runtime.manifest.json` 的完整精确资产包；它不能与 `--trace-assets` 或 `--asset-trace`
同时使用。Release CI 在 Linux/Lavapipe 上生成这两个 preset 的资产包，再由 Windows 与 macOS
直接装配到各自的归档中。

裸 `gnb` 启动 Dashboard。Windows/macOS 使用 Wails 原生窗口；Linux build 回退浏览器，`dashboard --no-open` 为 server-only。

## AI、LLM 与 SCAD

```bash
./gnb.sh ai doctor
./gnb.sh ai bridge --help
./gnb.sh llm models
./gnb.sh llm serve
./gnb.sh llm chat "你好"
./gnb.sh scad catalog
./gnb.sh scad compose --spec assets/scad/specs/deadly_roadtrip_map.json
./gnb.sh scad generate "一个港口旁的小镇"
```

`gnb ai` 是 provider/Bridge 正式入口；旧 `agent run` 已删除。`llm` 管理本地 llama-server。SCAD 的生成物与源数据规则见 [Scene Compose](../designs/scad-scene-compose-design.md)。

## 真实地理数据生成城市关卡

```bash
./gnb.sh geo make --name hk_victoria --at 22.2855,114.1580 --size 1000 --profile hongkong
./gnb.sh shot --scene assets/geo/hk_victoria/hk_victoria.scad
./gnb.sh geo pak                 # assets/geo/** -> assets/paks/geo.pak
./gnb.sh paks fetch geo          # 反向：取别人发布的 tile
```

`fetch`（SRTM 高程 + Overpass 矢量，缓存到 gitignore 的 `external/geocache/`）、`build`
（归一化 IR + `terrain.hmap` + `poi.json`）、`scad`（发射场景）三步也可单独跑；`make` 串联。加
`--debug-images` 会导出 DEM 各阶段灰度图。详见
[地理城市生成](../designs/geo-city-generation-design.md)。

**原始下载与中间 IR 是 ODbL 衍生数据库，不入库。产物也不入库**：一个 tile 的四件产物
（`<tile>.scad` / `terrain.hmap` / `poi.json` / `ATTRIBUTION.md`）落在 gitignore 的
`assets/geo/<tile>/`，由 `gnb geo pak` 打成 `assets/paks/geo.pak` 分发，引擎启动时自动挂载。
署名随 `.scad` 头注释与 `ATTRIBUTION.md` 一起进 pak。

## 移动平台与安装

```bash
./gnb.sh android build    # CMake 驱动构建 release APK
./gnb.sh android run      # 安装并启动已构建的 APK
./gnb.sh android devices  # 列出 adb 已连接设备及其状态
./gnb.sh android connect 192.168.1.100:5555  # 通过 adb 连接远程调试设备
./gnb.sh android build relwithdebinfo  # 可选：构建带原生调试符号的 APK
./gnb.sh ios build
./gnb.sh ios run
./gnb.sh install
./gnb.sh init
```

Android 的 Gradle 工程由 `tools/android/templates/` 生成到
`out/build/android-<variant>/gradle/`，固定 APK 位于
`out/build/android-<variant>/apk/gkNextRenderer-<variant>.apk`。默认 variant 是
`release`。如需保留 CMake 的 `RelWithDebInfo` 及 Android debug 签名以便安装和原生调试，使用
`gnb android build relwithdebinfo`。
`gnb android run` 优先安装到当前第一个在线 adb 设备；没有在线设备时，会启动本机第一个 AVD（可用
`--avd <name>` 指定）并等待其完成启动，再安装运行。Gradle 是内部打包后端，
日常命令和 CI 不应直接调用 `gradlew`。
`gnb android devices` 等同于以详细格式执行 `adb devices -l`，因此也会显示 offline 和
unauthorized 设备，便于诊断连接问题。
`gnb android connect <host>:<port>` 将目标转发给 SDK 中的 `adb connect`，连接成功后可直接
使用 `gnb android run --serial <host>:<port>` 安装并启动 APK。

### Tracy

`gnb tracy fetch` 下载与 vcpkg client 匹配的官方 Tracy GUI，`gnb tracy` 启动它；开发构建可用
`gnb build --tracy=on` 或默认配置启用 client，发布构建使用 `gnb build --tracy=off`。Android 先运行
`gnb android build relwithdebinfo`，再执行 `gnb tracy --android --serial <serial>`，然后在 GUI 中连接
`127.0.0.1:8086`（`--port` 可改本机端口）。完整排障说明见 [Tracy Profiling](tracy-profiling.md)。

### Rider

`gnb rider` 每次启动前都会删除仓库根目录的 `.idea`，然后启动 Rider 并直接打开
`CMakeLists.txt`，以规避当前 Rider 的 CMake 构建问题。命令会优先使用 `PATH` 中的
`rider64` / `rider`，也会探测常见的 JetBrains Toolbox 和系统安装目录。

### Visual Studio

Windows 上运行 `gnb visualstudio` 会使用 `windows-vcproj` preset 重新生成
`out/build/windows-vcproj/gkNextRenderer.slnx`（或 `.sln`），然后用 Visual Studio 打开它。
可加 `--skip-setup` 跳过依赖检查。
driver 要求 JDK 17–23，以及 libc++ 提供 C++20 `<ranges>` 的当前 NDK；
不兼容的 NDK 会在 configure 阶段明确失败。

release 正式签名从仓库外的 `~/.gknext/android-signing.properties` 自动加载，也可通过
`GK_ANDROID_SIGNING_PROPERTIES` 环境变量或同名 CMake cache 参数指定。属性文件必须包含
`storeFile`、`storePassword`、`keyAlias` 和 `keyPassword`；未配置时 release 仍生成 unsigned
APK 供 CI 构建验证，但不能直接安装或发布。发布密钥必须长期备份，后续升级包必须使用同一密钥。

iOS 由根 CMake 工程生成 arm64 device `.app`；需要签名时通过 `--team-id` 提供仓库外 Team ID，
未提供时禁用签名并生成 CI 可验证的 unsigned bundle（该 bundle 无法启动，`gnb ios build` 此时也不会
生成下面的 wrapper）。可用的 Team ID 通过 `gnb ios teams` 从本机 provisioning profile 列出。项目
不支持 iOS Simulator。

已签名的 device bundle 可在 Apple Silicon Mac 上通过 `gnb ios run` 直接启动（Mac Designed for iPad），
无需 Xcode 参与。macOS 只能通过 Launch Services 启动 Designed-for-iPad 的 wrapper 布局，直接运行
iPhoneOS 可执行文件会被内核以 code signing 错误杀掉（SIGKILL，退出码 9）。因此 `gnb ios build` /
`gnb ios run` 会把签名后的 bundle 镜像成 `bin/DesignedForIpad/<App>.app/Wrapper/<App>.app`（用
`ditto` 保留签名所封的扩展属性），再用 `open` 交给 Launch Services。

wrapper 仅在 bundle 的 CDHash 变化时重建：app 每获得一个新的磁盘身份，Gatekeeper 就会重新要求批准
开发者证书（app 用 Apple Development 证书签名，未经公证）。因此每编译出一个新版本，首次启动需要在
弹窗中批准一次，否则 app 会一直挂起；之后重复 `gnb ios run` 不再询问。

可运行设备通过 CoreDevice 的 `devicectl` 发现和部署：

```bash
./gnb.sh ios device
./gnb.sh ios run
./gnb.sh ios run --device <设备 ID、UDID 或名称>
```

`ios device` 列出本机 Mac Designed for iPad 和已配对、可连接的物理 iOS/iPadOS 设备。
`ios run` 只有一个可选设备时自动使用它，设备多于一个时显示交互式选择；脚本或 CI 应传
`--device <编号>`（也支持设备 ID、UDID 或名称）。列表编号可直接用于 `ios run --device`，物理设备使用 `devicectl device install app` 和 `devicectl device process launch`，
因此需要设备已配对并开启 Developer Mode；Mac 目标仍使用上面的 Designed-for-iPad wrapper。
真机调试和断点仍可由 Xcode 接管。

`init` 可在仓库外克隆新 checkout；其他大多数命令要求能发现 `gnb.toml`。也可通过 `--repo-root` 或 `GNB_REPO_ROOT` 明确仓库根。
