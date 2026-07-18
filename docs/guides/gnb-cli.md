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
./gnb.sh package <windows|linux|macos|magicalego>
./gnb.sh git status
./gnb.sh typos
```

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
./gnb.sh android
./gnb.sh ios
./gnb.sh install
./gnb.sh init
```

`init` 可在仓库外克隆新 checkout；其他大多数命令要求能发现 `gnb.toml`。也可通过 `--repo-root` 或 `GNB_REPO_ROOT` 明确仓库根。
