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

## 移动平台与安装

```bash
./gnb.sh android debug    # CMake 驱动构建、安装并启动
./gnb.sh android release  # CMake 驱动，仅生成 APK
./gnb.sh ios build
./gnb.sh install
./gnb.sh init
```

Android 的 Gradle 工程由 `tools/android/templates/` 生成到
`out/build/android-<variant>/gradle/`，固定 APK 位于
`out/build/android-<variant>/apk/gkNextRenderer-<variant>.apk`。Gradle 是内部打包后端，
日常命令和 CI 不应直接调用 `gradlew`。
driver 要求 JDK 17–23，以及 libc++ 提供 C++20 `<ranges>` 的当前 NDK；
不兼容的 NDK 会在 configure 阶段明确失败。

release 正式签名从仓库外的 `~/.gknext/android-signing.properties` 自动加载，也可通过
`GK_ANDROID_SIGNING_PROPERTIES` 环境变量或同名 CMake cache 参数指定。属性文件必须包含
`storeFile`、`storePassword`、`keyAlias` 和 `keyPassword`；未配置时 release 仍生成 unsigned
APK 供 CI 构建验证，但不能直接安装或发布。发布密钥必须长期备份，后续升级包必须使用同一密钥。

iOS 由根 CMake 工程生成 arm64 device `.app`；需要签名时通过 `--team-id` 提供仓库外 Team ID，
未提供时生成 CI 可验证的 unsigned bundle。项目不支持 iOS Simulator，也不提供 `ios run`；真机
安装、启动和完整 Vulkan/MoltenVK 渲染验证由 Xcode 或外部部署工具完成。

`init` 可在仓库外克隆新 checkout；其他大多数命令要求能发现 `gnb.toml`。也可通过 `--repo-root` 或 `GNB_REPO_ROOT` 明确仓库根。
