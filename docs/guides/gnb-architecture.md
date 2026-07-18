---
title: "gnb 架构与代码导览"
category: guide
status: 现行
owner: tools
created: 2026-06-24
last_updated: 2026-07-17
---

# gnb 架构与代码导览

`gnb` 是仓库内的 Go 编排器：统一 setup、CMake build/run/test、资源、移动平台、验证、Dashboard、AI provider 和本地 LLM 生命周期。命令入口在 `tools/gnb/cmd/gnb/`，可复用实现放在 `tools/gnb/internal/`。

修改 gnb 时在 macOS/Linux 使用 `./gnb.sh`，Windows 使用 `gnb.bat`。包装脚本会在源码变化后重建；直接运行仓库根目录旧 `gnb` 二进制可能看到已删除的命令或参数。

## 主要包

| 包 | 职责 |
| --- | --- |
| `config`, `platform`, `cmakerun`, `runner` | 仓库配置、平台路径、CMake 和进程运行 |
| `vcpkg`, `fetcher`, `paks`, `packager` | 依赖、可选资产与发布包 |
| `android`, `ios` | 移动平台入口 |
| `spec` | `.spec` TODO/archive/journal 解析与更新 |
| `dashboard` | TODO、Build/Run/Test、Git、Chat、LOC 等本地 UI 与 streaming jobs |
| `validate` | `.agentscript.json` 编排、断言和报告 |
| `remoteplay` | `gnb remote` 参数与访问 URL |
| `scadcompose`, `scadgen` | SCAD catalog/spec 生成管线 |
| `ai` | provider、profile、router、session、Bridge v2 与命名 workflow |
| `llm` | llama.cpp/Gemma 下载、server 生命周期与兼容 client |
| `gitops`, `loc`, `targetgraph` | Git、LOC 和 target graph 功能 |

## Dashboard

裸 `gnb` 等价于启动 Dashboard。Windows/macOS 的 desktop build 使用 Wails 原生窗口：普通 htmx 请求由 Wails AssetServer 处理，需要增量 flush 的 build/run/test 日志与 Chat stream 使用随机 loopback HTTP server。Linux 不编译 Wails desktop 支持，会回退到系统浏览器；`gnb dashboard --no-open` 只启动 `127.0.0.1` server。

Dashboard Chat 是普通对话界面，不给普通请求附加 repo/Git/Shell/Scene tools。显式 Tool Call Smoke 只验证 provider 的固定内存 fixture；旧 `chat_tools.go` 和通用 coding-agent 工具链已经删除，不应恢复。

## AI 边界

正式命令是 `gnb ai doctor` 和 `gnb ai bridge`。`internal/ai` 负责 provider/profile 路由和命名业务 workflow；`internal/llm` 负责本地模型进程。Engine 的 NextAI 只通过 Bridge 使用这些能力，边界见 [NextAI 产品化架构](../designs/nextai-product-focused-architecture.md) 与 [Bridge v2](../designs/gnb-ai-bridge-protocol-v2.md)。

## 修改与测试

命令层只负责解析参数、输出和调用 internal package；可测试逻辑不要堆进 `cmd/gnb`。路径必须从 repo root/config/platform helper 解析，不写死本机目录。

```bash
cd tools/gnb
go test ./...
go vet ./...
```

涉及 dashboard desktop build 时再按 `tools/gnb/README.md` 的 build tags 构建。普通 Go 改动不需要 C++ build。
