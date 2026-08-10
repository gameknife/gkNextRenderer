# 发布流程（Release Process）

本文描述 gkNextEngine 的完整发布流程。同一个 `v*` Release 发布标准 `default` preset
（**gkNextRenderer / gkNextEditor / gkNextMotionBenchmark**），平台优先级 Windows > Linux > macOS。

按本文逐步执行即可完成一次发布，不需要额外的口头约定。

---

## 1. 发布前检查（Release Checklist）

在打 tag 之前，逐条确认：

- [ ] `gnb test` 全绿（本机跑全量；CI 只跑 `~[GPU]` 子集）
- [ ] `gnb visual` 无视觉回归
- [ ] 三个 target 在本机能正常启动、切换全部渲染器、截图、正常退出
- [ ] MagicaLego 能加载 `legobricks.glb`、显示缩略图、播放放置音效并读写示例存档
- [ ] 100 / 125 / 150 / 200% DPI 下两个 GUI target 无文字裁切
- [ ] 无硬件光追的设备上（或 `--forcenort`）回退链路正常
- [ ] `README.md` / `README.en.md` / `AGENTS.md` / `docs/README.md` 与代码一致
- [ ] `THIRD-PARTY-NOTICES.md` 覆盖所有随包分发的第三方组件
- [ ] 仓库内无个人推广链接、无个人签名身份
- [ ] `src/build.version` 会由 CI 覆盖，不需要手工改

### 本地预演打包

CI 出问题时排查成本高，建议先在本机跑一遍完整链路：

```bash
gnb build gkNextRenderer gkNextEditor gkNextMotionBenchmark MagicaLego Packager
```

```bash
gnb package windows --package-preset default --trace-assets --version v0.1.2.0
gnb package windows --package-preset magicalego --trace-assets --version v0.1.2.0
```

```bash
gnb smoke gknextrenderer_win64_v0.1.2.0.7z --launch
gnb smoke MagicaLego_win64_v0.1.2.0.7z --launch
```

`gnb smoke` 会把 7z 解压到一个干净目录并验证：包内没有 `.pdb` / `.ilk` 等构建产物、
必需资产齐全、package manifest 中声明的可执行文件都能启动（`--help`）。加 `--launch` 时还会真实运行每个 target
并等待引擎日志里的 `committed scene`（需要可用的 Vulkan 设备）。

---

## 2. 打 tag 触发 CI

发布由 tag 触发（`.github/workflows/release.yml`，匹配 `v*`）：

```bash
git tag v0.1.2.0 && git push origin v0.1.2.0
```

CI 会依次执行：

1. `make-release` — 创建 GitHub Release
2. `linux-build` — 写入版本、构建桌面目标，并在 `Xvfb + Lavapipe` 软件 Vulkan 环境中对
   `default` 运行 `--trace-assets`。它发布 Linux 的 `default` 包，并上传平台无关的完整精确资产包
   （`runtime.pak`、资产列表、manifest）供后续 job 使用。
3. `windows-build` / `macos-build` — 在 Linux 资产追踪完成后构建各自的可执行文件，下载对应
   preset 的精确资产包并通过 `--runtime-pak` 组装归档；不在无 GPU runner 上运行 Vulkan 追踪：
   - 写入 `src/build.version`
   - `gnb setup`
   - `gnb build` 所有该归档需要的 target
   - Windows 与 macOS 均打包 `default`
   - `gnb smoke <7z>`（结构冒烟；runner 无 GPU，不带 `--launch`）
   - 上传产物到 Release

三个桌面 Release job 与 `desktop.yml` 使用相同 runner：`ubuntu-24.04`、`windows-latest`、
`macos-latest`。它们也使用相同的 `gnb info --bincache-key` 和 `.vcpkg` / `.vcpkg_bincache`
缓存路径，因此 Release 可以直接复用日常 Desktop CI 已生成的 vcpkg 二进制缓存。Windows 的
`default` 与 `magicalego` 位于同一个 job，共享同一台 runner、依赖目录和 CMake build tree。
Android 当前不属于发布支持平台，也不会产生 Release 产物。

### 产物命名

| 平台 | 文件名 |
|---|---|
| Windows | `gknextrenderer_win64_<tag>.7z` |
| Linux | `gknextrenderer_linux64_<tag>.7z` |
| macOS | `gknextrenderer_macos_<tag>.7z` |

### 包内结构

```
bin/          当前 package preset 的 exe + 运行时 DLL/共享库 + 厂商 license 文本
assets/paks/  runtime.pak + 可审计的运行时资产清单（精确模式）
package.manifest.json  preset、平台、版本和目标清单
README.txt    启动方式 / 系统要求 / 已知问题 / 反馈入口
LICENSE
THIRD-PARTY-NOTICES.md
```

默认发布包不携带 `gnb` 命令行工具。仅在需要随包提供 AI agent sidecar 时显式传入
`--include-gnb`；打包器届时会加入 `bin/gnb[.exe]` 和 `bin/gnb-agent-manifest.json`。

打包目标由 `gnb.toml` 的 `[package.presets.<name>]` 配置；`default` 是标准三程序发布，
`magicalego` 是单程序发布。通过 `--package-preset <name>` 选择，省略时使用
`[package].default_preset`。所有 preset 共用同一套 trace、runtime.pak、sidecar、文档和 7z 流程。

推荐的精确模式 `--trace-assets` 会依次以隐藏 Agent Validation 模式运行
`gkNextRenderer`、`gkNextEditor`、`gkNextMotionBenchmark`，合并 `FileHelper` 成功解析的磁盘文件与
实际命中的 Pak 条目，排序去重后生成单一 `assets/paks/runtime.pak`。发布 7z 同时携带
`runtime-assets.txt` 和 `runtime.manifest.json`，便于审计文件名与压缩后大小。已有的多轮覆盖清单
可用 `--asset-trace <path>` 直接复用；这适合把代表性场景和交互脚本的结果合并后交给无 GPU 的 CI。
完整的 `runtime.pak`、资产列表与 manifest 也可通过 `--runtime-pak <目录>` 原样复用；Release CI
用 Linux 的 Lavapipe 生成它们，因此 Windows/macOS 只需组装，不依赖各自 runner 的 Vulkan 设备。
Packager 会从原始资产目录或已有 Pak 中提取每个命中项，不会把整个可选 Pak 嵌套进新 Pak。
场景列表、内容浏览器、`IsAssetAvailable` 和 Pak 条目枚举只属于发现/存在性探测，不计入覆盖；
只有具体磁盘文件路径被解析或 `LoadFile` / `LoadMountedFile` 成功读取时才记录。
编译后的 `assets/shaders/**/*.spv` 是例外：渲染器可在运行时切换，采样期间未启用的渲染器仍是
发布功能，因此精确打包会无条件合并全部 SPIR-V 文件。场景扫描还会用当前安装目录和已挂载 Pak
再次校验逻辑资产路径，避免旧的按需解包缓存让未随当前版本发布的场景重新出现在选择器中。
其他必须进入精确包、但不保证在采样流程中加载的资产，分别配置在各 preset 的
`always_include_assets`。当前 `default` 固定包含 `conf_room.glb`、`pbr.glb` 和 `playground.glb`；
`magicalego` 不继承这三个场景，而是独立补齐交互阶段才读取的音效和示例 `.mls` 存档。

精确包只保证覆盖采样时实际走到的功能。发布前应以代表性的场景与交互流程扩充清单，并始终执行
`gnb smoke <7z> --launch`。不传上述两个参数时仍保留目录白名单模式，作为 CI 与问题排查的保守回退。

桌面发布归档使用 7z/LZMA2 最高压缩级别、128 MiB 字典和 solid 模式。gnb 依次查找
`GNB_7Z`、PATH 中的 `7zz` / `7z` / `7za`，Windows 还会查找标准的 `Program Files/7-Zip`。
`gnb smoke` 仍可读取旧 `.zip`，但新的桌面与 MagicaLego 发布物统一输出 `.7z`。

---

## 3. 发布后验收

在**干净机器**（不含仓库、不含开发工具）上，三平台各做一次：

1. 解压 → 双击 `bin/gkNextRenderer`
2. 依次切换全部渲染器
3. 截图一张，确认输出落在用户目录（不是安装目录）
4. 打开 `gkNextEditor`，确认首屏加载了默认场景
5. 运行 `gkNextMotionBenchmark`，确认自动跑完并退出
6. 全程无崩溃框、无红色 error 日志

验收记录（截图 + 日志）归档到该次 Release 的说明或 issue 里。

---

## 4. Release Notes 模板

```markdown
## gkNextRenderer <version>

### 亮点
- （3–5 条，面向用户，不写内部重构）

### 变更
- （按 renderer / editor / benchmark / 工具链分组）

### 已知问题
- （已知但本次不修的问题，附 issue 链接）

### 系统要求
- Windows 10/11 x64 · Linux x86_64 (glibc 2.35+) · macOS arm64
- 支持 Vulkan 1.3 的 GPU；硬件光追（RTX 20 系 / RX 6000 及以上）可启用 PathTracing，
  其余设备自动回退到软件渲染器
- 请更新到显卡厂商的最新驱动

### 文件写入位置
日志、设置、截图与烘焙缓存写入用户数据目录（Windows: `%APPDATA%\gkNext\<app>`）。
需要绿色版时，在 exe 上级目录放一个 `portable.txt`，所有写入会回到安装目录。

### 反馈
https://github.com/gameknife/gkNextRenderer/issues
```

---

## 5. 回滚

Release 出现阻断问题时：

1. 在 GitHub 上把该 Release 标为 **pre-release** 或直接删除，避免继续被下载
2. **不要删除 tag**（已有人 clone/引用时会造成更混乱的状态）；在下一个 patch 版本修复
3. 修复后发新 tag（`v0.1.2.1`），在新 Release Notes 里说明上一版的问题
4. 把导致回滚的问题补成一条冒烟检查（优先加进 `gnb smoke`），避免同类问题再次流出

---

## 6. 相关文件

| 文件 | 作用 |
|---|---|
| `.github/workflows/release.yml` | tag 触发的发布流水线 |
| `.github/workflows/desktop.yml` | PR / main 的构建 + 测试 |
| `tools/gnb/internal/packager/packager.go` | 打包清单、资产白名单、包内 README |
| `tools/gnb/internal/packager/smoke.go` | 解压即用冒烟校验 |
| `THIRD-PARTY-NOTICES.md` | 第三方 attribution，随包分发 |
| `docs/plans/release-readiness-plan.md` | 本轮发布准备的问题清单与批次任务 |
