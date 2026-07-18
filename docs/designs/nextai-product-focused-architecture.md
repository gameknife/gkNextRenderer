---
title: "NextAI 产品化边界"
category: design
status: 现行
owner: engine/tools/applications
created: 2026-07-15
last_updated: 2026-07-17
---

# NextAI 产品化边界

NextAI 是轻量 LLM 接入模块，不是通用 Agent SDK。2026-07-15 的重构已删除 Editor 场景 Agent、通用 Agent Loop/Tool Registry、repo/Git/Shell 工具与 Dashboard coding-agent 路线；不要根据已归档的 `#00017` 至 `#00023` 阶段记录恢复它们。

## 当前职责

`src/Modules/NextAI/` 只提供：

- `FAIService` 的 Chat、stream、文本与结构化输出接口；
- `GnbAIClient`/`GnbAIProcess` 对 gnb bridge 的进程与协议封装；
- provider/profile/model 选择、session、cancel、usage 等轻量能力。

`tools/gnb/internal/ai/` 拥有 provider adapters、router、session store 和有名字的业务 workflow。Bridge v2 见 [协议文档](gnb-ai-bridge-protocol-v2.md)。正式 CLI 是 `gnb ai doctor` / `gnb ai bridge --stdio`；`gnb agent` 仅为隐藏兼容别名，`agent run` 已删除。

## 产品模式

1. 场景/资产生成：ScadStudio、`gnb scad generate`、MagicaLego 生成明确领域产物，由产品代码解析、校验、最多有限修复，再预览/应用。
2. 游戏决策：StudioSim、AirportSim 等发送只读状态快照并请求结构化 DTO；游戏自己做 allowlist、范围钳制和 deterministic fallback。`--agent-validation` 不调用真实 LLM。
3. Dashboard：默认普通 Chat，不附加 repo/Git/Shell/Scene tools。显式 Tool Call Smoke 只用于 provider conformance 的固定内存 fixture，不是开放工具注册表。
4. gkNextEditor：保留用户显式操作的 Automation/Script Console；它不连接模型，也不展示 Agent 状态。

## 固定依赖方向

```text
application-owned prompt / DTO / validator / fallback
                    ↓
          NextAI lightweight facade
                    ↓
      gnb bridge / provider router / profiles
```

NextAI 不知道 SCAD、积木、机场 NPC、Scene/ECS 或 Editor 命令。领域规则留在产品；凭据、本地 llama-server 生命周期和 provider 路由留在 gnb。

## 重新引入 Tool Call 的门槛

只有当任务确实需要模型根据中间结果自主选择下一步、固定 workflow 无法表达、且有明确的工具 allowlist/预算/取消/验证边界时，才考虑产品内有限 Agent。即使满足，也不得把工具放进 NextAI 全局 registry，不能重新开放 repo shell 或任意场景写入。

术语上，`gnb validate` 的 agent control、游戏里的 `AgentSystem` 与 LLM Agent 无关，不应因此引入通用 Agent 抽象。
