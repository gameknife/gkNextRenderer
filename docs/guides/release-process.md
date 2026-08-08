# 发布流程（Release Process）

本文描述 gkNextRenderer 桌面版的完整发布流程。发布物为三个 target：
**gkNextRenderer / gkNextEditor / gkNextMotionBenchmark**，平台优先级 Windows > Linux > macOS。

按本文逐步执行即可完成一次发布，不需要额外的口头约定。

---

## 1. 发布前检查（Release Checklist）

在打 tag 之前，逐条确认：

- [ ] `gnb test` 全绿（本机跑全量；CI 只跑 `~[GPU]` 子集）
- [ ] `gnb visual` 无视觉回归
- [ ] 三个 target 在本机能正常启动、切换全部渲染器、截图、正常退出
- [ ] 100 / 125 / 150 / 200% DPI 下两个 GUI target 无文字裁切
- [ ] 无硬件光追的设备上（或 `--forcenort`）回退链路正常
- [ ] `README.md` / `README.en.md` / `AGENTS.md` / `docs/README.md` 与代码一致
- [ ] `THIRD-PARTY-NOTICES.md` 覆盖所有随包分发的第三方组件
- [ ] 仓库内无个人推广链接、无个人签名身份
- [ ] `src/build.version` 会由 CI 覆盖，不需要手工改

### 本地预演打包

CI 出问题时排查成本高，建议先在本机跑一遍完整链路：

```bash
gnb build gkNextRenderer gkNextEditor gkNextMotionBenchmark
```

```bash
gnb package windows --version v0.1.2.0
```

```bash
gnb smoke gknextrenderer_win64_v0.1.2.0.zip --launch
```

`gnb smoke` 会把 zip 解压到一个干净目录并验证：包内没有 `.pdb` / `.ilk` 等构建产物、
必需资产齐全、三个可执行文件都能启动（`--help`）。加 `--launch` 时还会真实运行每个 target
并等待引擎日志里的 `committed scene`（需要可用的 Vulkan 设备）。

---

## 2. 打 tag 触发 CI

发布由 tag 触发（`.github/workflows/release.yml`，匹配 `v*`）：

```bash
git tag v0.1.2.0 && git push origin v0.1.2.0
```

CI 会依次执行：

1. `make-release` — 创建 GitHub Release
2. `android-build` / `linux-build` / `windows-build` / `macos-build` — 各平台：
   - 写入 `src/build.version`
   - `gnb setup`
   - `gnb build gkNextRenderer gkNextEditor gkNextMotionBenchmark`
   - `gnb package <platform> --version <tag>`
   - `gnb smoke <zip>`（结构冒烟；runner 无 GPU，不带 `--launch`）
   - 上传产物到 Release

### 产物命名

| 平台 | 文件名 |
|---|---|
| Windows | `gknextrenderer_win64_<tag>.zip` |
| Linux | `gknextrenderer_linux64_<tag>.zip` |
| macOS | `gknextrenderer_macos_<tag>.zip` |
| Android | `gknextrenderer_android_<tag>.zip` |

### 包内结构

```
bin/          三个 exe + 运行时 DLL/共享库 + 厂商 license 文本
assets/       anims brand configs fonts locale models paks remote scripts shaders sounds textures
README.txt    启动方式 / 系统要求 / 已知问题 / 反馈入口
LICENSE
THIRD-PARTY-NOTICES.md
```

打包清单由 `tools/gnb/internal/packager/packager.go` 的 `releaseTargets` 与
`releaseAssetDirs` 决定。**新增运行期读取的资产目录时必须同步 `releaseAssetDirs`**，
否则用户解压后会缺资产。

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
