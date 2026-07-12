# gnb Agent Bridge Protocol v1

本文件定格 M0 的兼容边界。传输采用 UTF-8 NDJSON，每行一个 JSON-RPC 2.0 对象；stdout 只允许协议帧，诊断写 stderr。共享交互夹具位于 `tests/fixtures/gnb-agent-protocol/v1/`。

## 兼容规则

- 首帧必须是 `initialize`，双方明确交换整数 `protocolVersion`；版本不匹配时立即返回 `protocol_version` 并关闭连接。
- v1 内可新增可选字段和新事件类型；接收方必须忽略未知可选字段与未知事件。
- 不得删除字段、改变字段类型或改变既有错误语义；这类变更必须提升协议主版本。
- 请求 `id` 在连接内唯一；通知不带 `id`。所有流事件携带 `runId` 和单调递增的 `sequence`。
- `tool.execute` 以 `callId` 去重。修改型工具结果不确定时返回 `outcome_unknown`，调用方不得自动重放。
- 凭据、provider 原始响应和 reasoning 原文不得进入协议错误或 transcript。

## 错误码

| Code | Category | 含义 | 默认可重试 |
| ---: | --- | --- | --- |
| -32700 | `protocol` | JSON 解析失败 | 否 |
| -32600 | `protocol` | 非法 JSON-RPC 请求 | 否 |
| -32601 | `protocol` | 未知方法 | 否 |
| -32602 | `invalid_argument` | 参数缺失、类型或 schema 错误 | 否 |
| -32603 | `internal` | 未分类内部错误 | 否 |
| -32001 | `protocol_version` | 协议版本不兼容 | 否 |
| -32002 | `not_configured` | profile/provider/凭据未配置 | 否 |
| -32003 | `unavailable` | provider、bridge 或工具不可用 | 是 |
| -32004 | `deadline_exceeded` | run 或工具超时 | 是（仅无副作用时） |
| -32005 | `cancelled` | 调用方取消 | 否 |
| -32006 | `budget_exceeded` | steps/tool calls/context 超限 | 否 |
| -32007 | `tool_error` | 工具明确失败 | 由工具声明 |
| -32008 | `outcome_unknown` | 修改型工具执行结果未知 | 否 |
| -32009 | `model_busy` | LocalLlama 模型租约冲突 | 是 |
| -32010 | `frame_too_large` | 单帧超过协商上限 | 否 |

错误对象的 `data` 至少包含稳定的 `category` 和 `retryable`；面向用户的 `message` 可演进，不作为程序分支依据。

## C++ → Go 行为迁移清单

`Test_AIChatProtocol.cpp`：OpenAI 消息、tool schema、assistant tool call/tool result 往返；字符串/对象形式 arguments；content、finish reason、usage 与 provider error；Gemini system instruction、functionCall 和合成 call id；Ollama prompt flatten。

`Test_AgentLoop.cpp`：tool registry/schema；单工具完整链与 transcript/event；unknown tool 和 exception 作为 observation；max steps；可配置 grounding retry；步骤间取消；JSON fence、裸对象和数组 fallback tool calls；主线程 dispatcher。M2 在 Go 中逐项建立对应测试后，才允许 M6 删除这些 C++ 测试与实现。
