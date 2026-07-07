---
title: "Git 历史资产瘦身方案"
category: plan
status: 草案
owner: engine
created: 2026-07-07
last_updated: 2026-07-07
related:
  - AGENTS.md
  - docs/guides/gnb-cli.md
---

# Git 历史资产瘦身方案

> 本文规划一次破坏性 Git 历史重写，用于移除早期误提交到仓库历史里的大型资产 blob。仓库允许 force push，执行后要求所有贡献者重新 clone。本方案优先清理 `assets/` 下已经不在当前 `HEAD` 的模型、纹理、字体和音频等历史残留；非 `assets/` 的旧二进制历史作为可选二期处理。

## 0. 结论摘要

当前 `dev` 的工作树已经很瘦：`assets/` 在 `HEAD` 中只有约 8.17 MiB 未压缩、4.42 MiB pack 实占。但历史中 `assets/` 曾提交过大量 OBJ/GLB/HDR/PNG/JPG/MP3/TTF，导致 clone 仍需下载旧 blob。

建议执行 **Phase A：删除当前 `HEAD` 已不存在的 `assets/` 历史路径**，而不是按扩展名粗暴删除所有资产。原因：

1. 当前远端分支 tip 的资产树保持不变，`refs/heads/main^{tree}` 和 `refs/heads/dev^{tree}` 应该在重写前后完全一致。
2. 统计显示当前资产路径上没有 pack 实占超过 1 MiB 的历史大 blob；历史大 blob 基本都来自已删除路径。
3. 单独清理这些 stale asset paths 就能回收约 204.36 MiB pack 实占，覆盖主要问题。

按 2026-07-07 本地审计结果估算。这里区分两个口径：`git count-objects` 看到的本地 pack 包含不可达垃圾对象；fresh clone 更接近 `git rev-list --objects --all` 的可达对象集合。

| 项目 | pack 实占 |
| --- | ---: |
| 当前本地 `.git` pack | 476.74 MiB |
| 当前可达对象 pack 估算 | 401.71 MiB |
| 本地不可达 blob 垃圾 | 72.55 MiB |
| `assets/` 全历史 blob | 209.33 MiB |
| `assets/` 当前 `HEAD` blob | 4.42 MiB |
| `assets/` 历史残留 blob | 204.91 MiB |
| 当前 `HEAD` 已不存在的 `assets/` 历史路径 | 204.36 MiB |
| Phase A 后可达 pack 预估 | 约 197 MiB |

如果后续执行 **Phase B：清理非 `assets/` 的已删除历史二进制路径**，还能额外回收约 179.73 MiB。若 Phase B 按“当前 `HEAD` 已不存在的非 `assets/` 路径”完整执行，最终可达 pack 估计约 18-22 MiB，而不是 90-100 MiB。Phase B 涉及 `art/`、旧 `src/ThirdParty/*/bin`、旧 `gallery/`、旧 `android/app/libs/*.aar` 等路径，需要单独确认范围。

## 1. 审计结果

本次审计在 `P:\github\gkNextEngine`、`dev` 分支上执行。基础数据：

```powershell
git count-objects -vH
# size-pack: 476.74 MiB

Get-ChildItem -Recurse -Force .git | Measure-Object -Property Length -Sum
# .git 目录约 478.83 MiB
```

其中约 72.55 MiB 是本地不可达 blob 垃圾，`git fsck --full --unreachable --no-reflogs` 可看到 1877 个 unreachable blob。它们不应计入 fresh clone 的远端下载尺寸。按可达对象估算，当前 clone-relevant pack 约为 401.71 MiB。

`assets/` 历史与当前占用：

| 范围 | blob 数 | 未压缩大小 | pack 实占 |
| --- | ---: | ---: | ---: |
| `assets/` 全历史 | 2441 | 468.47 MiB | 209.33 MiB |
| `assets/` 当前 `HEAD` | 180 | 8.17 MiB | 4.42 MiB |
| `assets/` 仅历史残留 | 2261 | 460.29 MiB | 204.91 MiB |
| 当前 `HEAD` 已不存在的 `assets/` 历史路径 | 1443 blob / 264 path | 458.92 MiB | 204.36 MiB |

按扩展名看，主要浪费来自模型和纹理：

| 扩展名 | 历史残留 pack 实占 |
| --- | ---: |
| `.glb` | 94.45 MiB |
| `.obj` | 63.63 MiB |
| `.hdr` | 11.77 MiB |
| `.png` | 10.52 MiB |
| `.jpg` | 8.95 MiB |
| `.ttf` | 8.59 MiB |
| `.mp3` | 6.09 MiB |

单路径主要嫌疑：

| 路径 | 版本数 | 未压缩大小 | pack 实占 |
| --- | ---: | ---: | ---: |
| `assets/models/luxball.obj` | 2 | 139.20 MiB | 32.45 MiB |
| `assets/models/livingroom.glb` | 3 | 30.47 MiB | 19.45 MiB |
| `assets/models/still.glb` | 1 | 19.35 MiB | 18.32 MiB |
| `assets/models/qx50.glb` | 8 | 23.10 MiB | 17.45 MiB |
| `assets/models/luxball.glb` | 8 | 17.62 MiB | 13.11 MiB |
| `assets/models/livingroom.obj` | 2 | 61.78 MiB | 12.22 MiB |
| `assets/models/lucy.obj` | 1 | 30.88 MiB | 11.25 MiB |
| `assets/fonts/MicrosoftYaHeiMono.ttf` | 1 | 14.27 MiB | 8.59 MiB |
| `assets/models/kitchen.obj` | 1 | 34.01 MiB | 7.72 MiB |
| `assets/models/moderndepart.glb` | 1 | 10.37 MiB | 7.65 MiB |
| `assets/models/kitchen.glb` | 4 | 6.45 MiB | 5.35 MiB |
| `assets/textures/land_ocean_ice_cloud_2048.png` | 1 | 3.53 MiB | 3.52 MiB |
| `assets/sfx/bgm2.mp3` | 1 | 3.53 MiB | 3.51 MiB |
| `assets/sfx/bgm.mp3` | 1 | 2.54 MiB | 2.53 MiB |

非 `assets/` 的可选二期历史残留也很明显：

| 路径 | pack 实占 |
| --- | ---: |
| `src/ThirdParty/oidn/bin/OpenImageDenoise_core.dll` | 44.84 MiB |
| `art/livingroom.blend` | 20.71 MiB |
| `art/simple.blend` | 18.74 MiB |
| `android/app/libs/SDL3-3.2.22.aar` | 13.34 MiB |
| `art/LuxBall/luxball.blend` | 12.43 MiB |
| `gallery/Showcase_h264.mp4` | 11.86 MiB |
| `art/complex.blend` | 8.40 MiB |
| `src/ThirdParty/ffmpeg/bin/ffmpeg.exe` | 6.84 MiB |

## 2. 推荐方案

### Phase A：只清理 stale assets

范围：

- 删除所有曾经在历史中出现、但当前 `refs/heads/main` 和 `refs/heads/dev` 都已不存在的 `assets/**` 路径。
- 不删除当前 `main` / `dev` tip 中的 `assets/**` 文件。
- 不处理源码、文档、工具链历史。

成功标准：

1. `refs/heads/main^{tree}` 和 `refs/heads/dev^{tree}` 重写前后完全相同。
2. 当前 fresh clone 的工作树不变。
3. `assets/models/luxball.obj`、`assets/models/livingroom.glb`、`assets/models/still.glb`、`assets/models/qx50.glb` 等 stale paths 不再出现在 `git rev-list --objects --all`。
4. fresh clone 的可达 pack 从约 401.71 MiB 降到约 197 MiB。若直接观察本地 `.git`，需先排除不可达垃圾对象，否则会高估 clone 体积。

### Phase B：可选清理非 assets 历史二进制

范围候选：

- `art/**` 中已删除的 `.blend` 设计源文件。
- `gallery/**` 中已删除的大图和视频。
- `src/ThirdParty/**/bin/**` 中已删除的旧二进制依赖。
- `android/app/libs/*.aar` 中已删除的旧 AAR。
- `tools/typescript/lib/*.js` 中已删除的 bundled 大文件版本。

成功标准：

1. `refs/heads/main^{tree}` 和 `refs/heads/dev^{tree}` 仍保持不变。
2. fresh clone pack 进一步下降，完整清理当前 `HEAD` 已不存在的非 `assets/` 路径后，目标约 18-22 MiB。
3. 不移除当前 `HEAD` 仍需要的第三方源码、文档 gallery、Android wrapper 等文件。

Phase B 建议在 Phase A 合并、团队重新 clone 后再评估，避免一次 force push 范围过大。

### Phase A+B 后剩余构成

若 Phase A 和 Phase B 都按“当前 `HEAD` 已不存在的路径全部清理”执行，剩余可达 pack 估计约 17.62 MiB，其中：

| 剩余类别 | pack 实占 |
| --- | ---: |
| 当前 `HEAD` blob | 13.11 MiB |
| 当前路径的历史版本 blob | 2.85 MiB |
| commit / tree / tag | 1.66 MiB |

这已经不是“大二进制历史”问题。剩余主要是当前仓库内容本身：

| 当前 `HEAD` 顶层目录 | pack 实占 |
| --- | ---: |
| `docs/` | 6.10 MiB |
| `assets/` | 4.42 MiB |
| `src/` | 2.12 MiB |
| `tools/` | 0.20 MiB |
| `android/` | 0.10 MiB |

其中较大的当前文件包括 `docs/gallery/*.avif`、`assets/fonts/DroidSansFallback.ttf`、`assets/paks/thumbs.pak`、`src/ThirdParty/miniaudio/miniaudio.h`、`src/ThirdParty/quickjs-ng/quickjs/quickjs.c`。这些是当前 `HEAD` 仍在使用或展示的文件，继续瘦身就不再是 history rewrite，而是产品取舍：

1. 把 `docs/gallery/*.avif` 改成更小的缩略图或外链，可回收约 5.5 MiB。
2. 把 `assets/paks/thumbs.pak` 移到 release 下载，可回收约 1.7 MiB。
3. 替换或外置 `assets/fonts/DroidSansFallback.ttf`，可回收约 1.75 MiB pack / 3.76 MiB 工作树。
4. 把 vendored 第三方单文件源码改为 submodule、包管理或 fetch 脚本，最多回收约 1 MiB 级别，但会增加构建复杂度。

当前路径的历史版本 blob 只有约 2.85 MiB，基本就是正常文本和小资源的 diff；不值得为了这部分再牺牲历史可读性。

## 3. 执行流程

以下命令应在临时目录的新 mirror clone 中执行，不要直接在日常工作目录里重写历史。

### 3.1 冻结与备份

1. 通知团队进入短暂提交冻结窗口。
2. 关闭或暂停需要基于旧 history rebase 的 PR。
3. 在远端外部保留一份只读备份，例如压缩一个 mirror clone 到本地 NAS 或对象存储。不要把备份 tag 或 backup branch 推回同一个 GitHub remote，否则旧对象仍会被远端引用，clone 体积不会下降。

```powershell
$repo = "https://github.com/gameknife/gkNextEngine.git"
$rewrite = "P:\tmp\gkNextEngine-history-rewrite.git"

git clone --mirror $repo $rewrite
Set-Location $rewrite
git remote update --prune

$beforeTrees = @{}
foreach ($branch in @("main", "dev")) {
    $beforeTrees[$branch] = git rev-parse "refs/heads/$branch^{tree}"
}
git count-objects -vH
```

### 3.2 准备 git-filter-repo

`git filter-repo` 当前不在本机 Git 命令中，需要执行者先安装：

```powershell
py -m pip install --user git-filter-repo
git filter-repo --version
```

也可以用 `pipx install git-filter-repo`。不要使用 `git filter-branch`；它慢、容易留下 refs/original，并且更容易误推旧引用。

### 3.3 生成 Phase A 删除列表

以当前 `main` 和 `dev` 的资产树并集作为保留集合，生成“历史出现过但当前两个分支 tip 都已不存在”的 `assets/**` 路径列表：

```powershell
$headAssets = @{}
foreach ($branch in @("main", "dev")) {
    git ls-tree -r --name-only "refs/heads/$branch" assets | ForEach-Object {
        $headAssets[$_] = $true
    }
}

git log --all --pretty=format: --name-only -- assets |
    Where-Object { $_ -like "assets/*" -and -not $headAssets.ContainsKey($_) } |
    Sort-Object -Unique |
    Set-Content -Encoding ascii rewrite-remove-assets.txt

(Get-Content rewrite-remove-assets.txt).Count
Select-String -Path rewrite-remove-assets.txt -Pattern "luxball.obj|livingroom.glb|still.glb|qx50.glb|MicrosoftYaHeiMono"
```

预期数量约为 264 条路径。若明显偏离，应停止并重新审计。

可选：确认删除列表没有包含当前 `main` / `dev` tip 资产：

```powershell
$remove = Get-Content rewrite-remove-assets.txt
$stillInHead = $remove | Where-Object { $headAssets.ContainsKey($_) }
if ($stillInHead.Count -ne 0) {
    $stillInHead
    throw "delete list contains current HEAD assets"
}
```

### 3.4 重写历史

```powershell
git filter-repo --paths-from-file rewrite-remove-assets.txt --invert-paths --force
```

`git filter-repo` 通常会移除 `origin` remote，防止误推。重写完成后重新设置：

```powershell
git remote add origin $repo
```

若 `origin` 仍存在，则改用：

```powershell
git remote set-url origin $repo
```

### 3.5 验证重写结果

当前 `main` 和 `dev` 的 tree id 必须不变：

```powershell
foreach ($branch in @("main", "dev")) {
    $afterTree = git rev-parse "refs/heads/$branch^{tree}"
    if ($afterTree -ne $beforeTrees[$branch]) {
        throw "$branch tree changed: before=$($beforeTrees[$branch]) after=$afterTree"
    }
}
```

检查主要 stale paths 已消失：

```powershell
git rev-list --objects --all |
    Select-String -Pattern "assets/models/luxball.obj|assets/models/livingroom.glb|assets/models/still.glb|assets/models/qx50.glb|assets/fonts/MicrosoftYaHeiMono.ttf"
```

上述命令应无输出。

重新统计 pack：

```powershell
git count-objects -vH
```

本地 mirror 的 pack 估计应接近 272-275 MiB。GitHub 远端重新 pack 后可能有 5-10% 浮动。

### 3.6 推送

只有 repo maintainer 在确认冻结窗口、备份、验证都完成后才执行：

```powershell
git push --force --all origin
git push --force --tags origin
```

如果需要同步删除远端废弃分支或标签，应先人工列出并确认，再单独删除。不要在没有核对的情况下使用 `git push --mirror`，因为它会把本地 refs 形状强制同步到远端。

推送后：

1. 在 GitHub 上确认默认分支仍是 `dev`。
2. 删除或重建所有基于旧 history 的 PR 分支。
3. 等待 GitHub 后台 GC。若 fresh clone 体积长时间没有下降，检查是否还有 tag、branch、PR ref 或 release source archive 引用旧对象；必要时联系 GitHub 支持清理 cached views。

### 3.7 Fresh clone 验证

```powershell
$check = "P:\tmp\gkNextEngine-check"
git clone $repo $check
Set-Location $check
git checkout dev
git count-objects -vH
git rev-parse "HEAD^{tree}"
```

如果只做历史重写，当前 tree 未变，不需要 C++ 构建作为必要 gate。若希望验证新 clone 的工作流，可按常规 targeted build：

```powershell
.\gnb.bat build gkNextRenderer gkNextUnitTests
```

## 4. 贡献者迁移说明

执行 force push 后，默认要求所有贡献者重新 clone。Windows 贡献者使用 PowerShell 脚本：

```powershell
# 在旧仓库内或任意目录均可运行。默认迁移当前 Git 仓库，重新 clone dev 分支。
.\scripts\reclone-after-history-rewrite.ps1
```

如果旧 clone 里还没有这个脚本，可以在 force push 完成后从重写后的 `dev` 下载到临时目录再执行：

```powershell
$script = "$env:TEMP\reclone-after-history-rewrite.ps1"
Invoke-WebRequest `
    -Uri "https://raw.githubusercontent.com/gameknife/gkNextEngine/dev/scripts/reclone-after-history-rewrite.ps1" `
    -OutFile $script

powershell -ExecutionPolicy Bypass -File $script `
    -RepoPath P:\github\gkNextEngine `
    -RemoteUrl https://github.com/gameknife/gkNextEngine.git `
    -Branch dev
```

常用参数：

```powershell
# 如果需要把未提交改动自动尝试应用到新 clone，加 -ApplyPatches。
powershell -ExecutionPolicy Bypass -File .\scripts\reclone-after-history-rewrite.ps1 `
    -RepoPath P:\github\gkNextEngine `
    -ApplyPatches
```

macOS / Linux 贡献者使用 Bash 脚本：

```bash
# 在旧仓库内运行。默认迁移当前 Git 仓库，重新 clone dev 分支。
bash scripts/reclone-after-history-rewrite.sh

# 如果从旧仓库外运行，显式指定路径。
bash /path/to/gkNextEngine/scripts/reclone-after-history-rewrite.sh \
    --repo-path /path/to/gkNextEngine \
    --remote-url https://github.com/gameknife/gkNextEngine.git \
    --branch dev

# 如果需要把未提交改动自动尝试应用到新 clone，加 --apply-patches。
bash scripts/reclone-after-history-rewrite.sh --apply-patches
```

如果旧 clone 里还没有 Bash 脚本，可以在 force push 完成后从重写后的 `dev` 下载到临时目录再执行：

```bash
script="/tmp/reclone-after-history-rewrite.sh"
curl -fsSL \
    https://raw.githubusercontent.com/gameknife/gkNextEngine/dev/scripts/reclone-after-history-rewrite.sh \
    -o "$script"

bash "$script" \
    --repo-path /path/to/gkNextEngine \
    --remote-url https://github.com/gameknife/gkNextEngine.git \
    --branch dev
```

脚本行为：

1. 读取旧仓库的 `origin` URL、当前 `HEAD`、`git status`。
2. 备份未提交改动：`working-tree.patch`、`index.patch`、未跟踪文件归档（PowerShell 为 zip，Bash 为 tar.gz）。
3. 把旧目录重命名为 `gkNextEngine.old-history-YYYYMMDD-HHMMSS`。
4. 从远端重新 clone 指定分支到原路径。
5. 传入 `-ApplyPatches` 时，尝试把 patch 和未跟踪文件恢复到新 clone。

手工流程如下：

```powershell
Rename-Item P:\github\gkNextEngine gkNextEngine.old
git clone https://github.com/gameknife/gkNextEngine.git P:\github\gkNextEngine
```

如果有人有未提交本地修改，应先在旧仓库导出 patch：

```powershell
git diff > ..\gkNextEngine-working.patch
git diff --cached > ..\gkNextEngine-index.patch
```

新 clone 后人工检查并应用：

```powershell
git apply ..\gkNextEngine-working.patch
```

旧 clone 不建议继续使用 `fetch + reset` 复用，因为本地 reflog 和旧 pack 仍会保留大对象，容易造成误判和后续推送事故。

## 5. 后续体积规则

从本方案合并起，仓库采用以下规则，规则编号 `repo-size-001`：

1. `assets/` 只保留运行 smoke test、默认 demo、编辑器基础 UI 所必需的小资产。
2. `assets/` 下单个新增或修改的二进制文件超过 1 MiB，需要在 PR 中说明用途、来源、许可证、是否可由 `gnb paks fetch` 或 release asset 下载。
3. `assets/` 下单个二进制文件超过 5 MiB 默认禁止提交。确需例外时，需要先把大资产放到 GitHub Release 或其他 release artifact，再提交下载脚本、manifest 或缩略代理资产。
4. `.obj`、`.fbx`、`.blend`、`.exr`、`.hdr`、`.png`、`.jpg`、`.glb`、`.gltf`、`.mp3`、`.wav`、`.mp4`、`.zip`、`.7z`、`.pak` 等格式按二进制资产处理。
5. `src/ThirdParty/**/bin/**`、`art/**`、`gallery/**`、`android/app/libs/**` 的大文件同样适用 1 MiB 说明、5 MiB 禁止规则。
6. Release 级资产应走现有模式：放在 GitHub Release 上，通过 `gnb paks fetch`、manifest 或专门脚本下载，不进入 Git history。
7. 不在仓库中新增 pre-commit hook 或 scheduled task；PR reviewer 使用下面的 size gate 命令检查，后续若要强制化，应把同等逻辑放入 GitHub Actions。

PR 本地检查命令：

```powershell
$softLimit = 1MB
$hardLimit = 5MB
$binaryExts = @(
    ".obj", ".fbx", ".blend", ".exr", ".hdr", ".png", ".jpg", ".jpeg",
    ".glb", ".gltf", ".mp3", ".wav", ".mp4", ".zip", ".7z", ".pak",
    ".aar", ".dll", ".exe", ".dylib", ".so"
)

$changed = git diff --name-only --diff-filter=AM origin/dev...HEAD
$violations = foreach ($path in $changed) {
    if (-not (Test-Path $path)) { continue }
    $item = Get-Item $path
    $ext = [IO.Path]::GetExtension($path).ToLowerInvariant()
    $isBinaryAsset = $binaryExts -contains $ext
    $isWatchedPath = (
        $path -like "assets/*" -or
        $path -like "art/*" -or
        $path -like "gallery/*" -or
        $path -like "android/app/libs/*" -or
        $path -like "src/ThirdParty/*/bin/*"
    )

    if ($item.Length -gt $hardLimit -and ($isBinaryAsset -or $isWatchedPath)) {
        [pscustomobject]@{ Level="ERROR"; MiB=[math]::Round($item.Length / 1MB, 2); Path=$path }
    } elseif ($item.Length -gt $softLimit -and ($isBinaryAsset -or $isWatchedPath)) {
        [pscustomobject]@{ Level="WARN"; MiB=[math]::Round($item.Length / 1MB, 2); Path=$path }
    }
}

$violations | Format-Table -AutoSize
if (@($violations | Where-Object { $_.Level -eq "ERROR" }).Count -gt 0) {
    throw "repo-size-001 violation: move large assets to release artifacts"
}
```

Reviewer 处理规则：

- `ERROR` 必须退回，除非 maintainer 明确批准并记录原因。
- `WARN` 可以合并，但 PR 描述必须说明为什么不能走 release asset 下载。
- 新增大型示例场景时，仓库只放低分辨率代理、缩略图或 manifest；完整 payload 走 release。

## 6. 回滚策略

历史重写 push 后不要试图在同一个 remote 上保留旧 history 引用。真正需要回滚时：

1. 停止所有推送。
2. 从外部备份 mirror 恢复。
3. 再次 force push 旧 refs。
4. 通知所有人重新 clone。

如果只是发现少量资产误删，但当前 `HEAD` tree 没变，则优先新提交恢复需要的文件，不要把旧 history 重新引入。
