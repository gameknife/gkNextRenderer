---
title: "gnb AI Bridge Protocol v2"
category: design
status: 已实现
last_updated: 2026-07-17
---

# gnb AI Bridge Protocol v2

AI bridge 使用 stdin/stdout 上的 NDJSON JSON-RPC 2.0。它是 NextAI 的轻量模型接入边界，不是通用 Agent 或 Tool Registry。

支持的方法：

- `initialize`
- `providers.list`、`profiles.list`
- `session.create`、`session.reset`、`session.close`
- `llm.chat`
- `workflow.run`
- `run.cancel`
- `shutdown`

`llm.chat` 可携带 `responseFormat`：`mode=json` 请求 JSON mode；`mode=schema` 还携带 `name`、`schema`、`strict`。返回值用 `structuredOutputMode` 明示 `native_schema`、`native_json` 或 `prompt_only` 降级。`enableThinking` 显式控制本地 llama.cpp 的推理模式；`deadlineMs` 控制单次请求期限。产品运行时的只读状态快照请求使用 `stateless=true`，不读取也不写入 Chat session 历史。

流式增量以 `run.event` 通知返回。普通 Chat 请求不附加 tools。Bridge 不支持 `agent.run`、远程工具注册或反向工具执行。

共享协议夹具位于 `tests/fixtures/gnb-ai-protocol/v2/`，由 Go bridge tests 和 C++ NextAI tests 共同校验。
