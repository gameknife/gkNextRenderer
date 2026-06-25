---
title: "gnb CLI 手册"
category: guide
status: 现行
owner: engine
created: 2026-05-09
last_updated: 2026-06-12
---

# gnb CLI 手册

`gnb` 是 gkNextEngine 的统一构建入口。没有安装系统级 `gnb` 时，Windows 使用 `./gnb.bat`，macOS/Linux 使用 `./gnb.sh`。

实现细节和架构说明见 [gnb-tech-stack.md](gnb-tech-stack.md)。

## 初始化

准备 vcpkg、外部 SDK、TypeScript 编译器、Slang 和可选资源：

```bash
./gnb.sh setup
```

跳过可选资源：

```bash
./gnb.sh setup --skip-paks
```

刷新 vcpkg：

```bash
./gnb.sh setup --refresh
```

## 构建

构建默认平台 preset：

```bash
./gnb.sh build
```

构建指定 target：

```bash
./gnb.sh build gkNextEditor
```

清理并重新 configure：

```bash
./gnb.sh build --clean --reconfigure
```

关闭 unity build 或启用 LTO：

```bash
./gnb.sh build --no-unity
./gnb.sh build --lto
```

## 运行

列出可运行应用：

```bash
./gnb.sh run
```

运行指定 target：

```bash
./gnb.sh run BrickPlayer
```

透传应用参数：

```bash
./gnb.sh run -- --scene=foo --present-mode=mailbox
```

终端 TUI 模式（隐藏窗口真实渲染，画面持续刷到当前终端）：

```bash
./gnb.sh tui --scene assets/models/playground.glb
./gnb.sh tui --target ScadStudio --scene assets/scad/beer_cup.scad
./gnb.sh tui --target gkNextRenderer --tui-fps 20
./gnb.sh tui --scene assets/models/playground.glb --tui-ssaa 2
```

## Dashboard 控制台

直接运行 `gnb` 或显式执行 dashboard 命令，会在 Wails 原生窗口中打开 dashboard：

```bash
./gnb.sh
./gnb.sh dashboard
```

Wails asset server 负责普通 htmx 请求。由于 WebView asset 响应不能增量 flush，构建日志 SSE 和流式 Chat 会走一个随机 loopback HTTP 端口。不需要外部浏览器。

兼容模式：

```bash
./gnb.sh dashboard --browser  # 在系统浏览器中打开
./gnb.sh dashboard --no-open  # 只启动 server
./gnb.sh dashboard --port 7788
```

Windows 使用 WebView2 runtime；release 和 shim 构建会内嵌它的 bootstrapper。Linux/macOS 使用 Wails 所需的平台 WebKit runtime。

## 测试与可视化

运行单元测试：

```bash
./gnb.sh test "[Unit]"
```

运行可视化测试：

```bash
./gnb.sh visual
```

## 移动端

Android：

```bash
./gnb.sh android release
```

iOS：

```bash
./gnb.sh ios              # 默认：跳过代码签名
./gnb.sh ios --codesign   # 启用代码签名
```

## Pak 资源

拉取全部可选资源：

```bash
./gnb.sh paks fetch
```

拉取指定资源组：

```bash
./gnb.sh paks fetch optional ldraw
```

列出状态：

```bash
./gnb.sh paks list
```

发布指定资源：

```bash
GITHUB_TOKEN=... ./gnb.sh paks publish optional
```

## 打包

创建桌面分发包：

```bash
./gnb.sh package linux --version v1.0.0
```

创建 MagicaLego 分发包：

```bash
./gnb.bat package magicalego --version v1.0.0
```

## 信息与诊断

打印环境信息：

```bash
./gnb.sh info
```

检查必要工具：

```bash
./gnb.sh doctor
```

## AVIF

AVIF 仍然是手动 CMake feature，不通过 `gnb build` flag 暴露：

```bash
cmake --preset windows -DENABLE_AVIF=ON -DVCPKG_MANIFEST_FEATURES=avif
./gnb.bat build
```

## 发布 gnb 二进制

`./gnb.bat` 和 `gnb.sh` 使用 `paks-latest` GitHub release 作为唯一 bootstrap 来源。推送到 `main` / `dev` 且修改了 `tools/gnb` Go 源码时，`.github/workflows/gnb-release.yml` 会自动重新构建并发布各平台二进制以及 `gnb-version.txt`。

shim 会把本地缓存版本与 `gnb-version.txt` 对比；当有更新 release 时，会自动刷新缓存的 bootstrap 二进制。如果本机安装了 Go，shim 仍会优先从 `tools/gnb` 本地重新构建。

在安装了 Go 和 GitHub CLI 的机器上手动发布或补发：

```powershell
gh auth login
.\scripts\publish-gnb.ps1
```

只预览本地构建和目标 release URL，不上传：

```powershell
.\scripts\publish-gnb.ps1 -DryRun
```

## TODO 工作流

列出并查看 `.spec/TODO.md` 任务：

```bash
./gnb.sh todo list
./gnb.sh todo show 00021
./gnb.sh todo next --json
./gnb.sh todo next --wait --timeout 590s --json
```

添加任务，可选择同时创建关联的 `specs/<id>.md` 背景文件：

```bash
./gnb.sh todo add -t feat -p P1 "重构材质缓存" --spec
./gnb.sh todo add -t feat "重构材质缓存" --spec-from docs/material-cache.md
```

维护 `下一步` 与 `待规划` 中的任务顺序：

```bash
./gnb.sh todo move 00021 --to next
./gnb.sh todo move 00021 --before 00018
./gnb.sh todo swap 00018 00021
```

删除任务（默认也删除 `specs/<id>.md`）：

```bash
./gnb.sh todo delete 00021          # dry-run：显示将删除的内容
./gnb.sh todo delete 00021 -y       # 实际删除任务行及其 spec
./gnb.sh todo delete 00021 -y --keep-spec
./gnb.sh todo delete 00021 -y --also-files   # 同时删除 journal/blocker
```
