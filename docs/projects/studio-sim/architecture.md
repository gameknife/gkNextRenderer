---
title: "StudioSim 架构与 AI 边界"
category: project
status: 现行
owner: StudioSim
created: 2026-07-17
last_updated: 2026-07-17
---

# StudioSim 架构与 AI 边界

StudioSim 是可在无 LLM 情况下完整推进的工作室经营/办公室模拟。旧 MVP、production refinement、game-project iteration 和 Sim Kit 重构计划已经落地；本文件只保留当前系统所有权与 AI 不能越过的边界。

## 核心状态与流程

- `FWorldState` / `DayClock`：Briefing → Working → Review，工作日 09:00–18:00；只有 Working 且未暂停/阻塞时推进。
- `FGameProject` / `FCompanyState`：类型、题材、规模、工期、预算、highlight、跨日进度和结算。
- `GoalSystem`：晨会候选、玩家选择/自定义、职位任务分解和日终总结。
- `ProductionSystem`：员工在岗产出 tech/design/art/polish，推进 Production → Polish → Done，并维护 bug/进度/浮字事件。
- `EmployeeSystem`：日程、移动、工位和可选决策 override。
- `EventSystem` / `PerceptionSystem`：玩家事件与可观察状态进入决策上下文。
- `GatheringSystem`：会议/茶水间参与者、对白、可确认的群体决定和散场。

Office SCAD 的具名 node 由 `OfficeMap`/Sim Kit anchor 解析；角色池、NavGrid 和 ScadRig visual 来自 `src/Gameplay/Sim/`。产品进度、事件、目标与会议不能下沉到 Sim Kit。

## 无 LLM 也必须闭环

时钟、项目 meter、stage、bug、deadline、上线质量、销量/资金和角色移动由本地代码拥有。LLM 不生成数值积分，也不决定 tick 是否推进。脚本日程、模板目标/对白/会议决定和本地结算是 fallback，保证服务离线时仍能从立项走到上线与下一项目。

启用 LLM 后结果会影响允许的任务、情绪、台词或玩家可确认的聚焦决定，因此不能把整个产品称为网络级 deterministic simulation；正确表述是“规则/生产闭环不依赖 LLM，所有 AI 输出受产品 DTO 和 fallback 约束”。

## AI 调度与权限

`DecisionScheduler` 对员工决策保持最多一个在途请求，并公平轮转候选。Goal、Gathering 和 summary 各有产品专用 prompt/parser；不同阶段通过 GameInstance gate，避免把普通员工决策与玩家决策流混在一起。

worker callback 只解析并入队；所有 employee、world、production、office/Scene 改动在主线程 Tick apply。reset 增加 generation，丢弃旧天/旧项目的迟到结果。服务失败、格式错误或不可用时回退本地规则。

LLM 只能返回产品定义的结构化选择或文案。它不能：

- 直接改 meter、资金、时钟、project stage 或 Scene component；
- 发任意 shell/repo/Scene tool call；
- 绕过玩家对会议决定、立项或目标的确认；
- 让一个 callback 持有 employee/node 裸指针跨越 reset/unload。

通用 AI 边界见 [NextAI 产品化设计](../../designs/nextai-product-focused-architecture.md)。AirportSim 有相似 scheduler，但两个 demo 刻意保持 application-local；只有多个 consumer 的真实契约收敛后才抽公共调度器。

## 修改护栏

- 新生产机制先进入本地 `FProjectState`/结算和 UI，再决定是否把只读摘要喂给 LLM；不能以 prompt 文本代替权威状态。
- 所有 AI action 做 allowlist、范围钳制和 entity/session still-valid 检查。
- 使用游戏分钟安排生产/日程/气泡，steady clock 只用于请求 latency/timeout 观测。
- 会议决定对 production 的影响必须经过显式 accept/reject，且应用为有限、可解释的 focus boost/任务改派。
- agent validation 传 null AI 并自动选择确定性路径；测试不能依赖本地模型正好给出某句话。

构建和可视验证：

```bash
./gnb.sh build StudioSim
./gnb.sh shot --target StudioSim --ui --frames 300
```

至少覆盖无 LLM 多日推进、项目跨日不重置、deadline/上线结算、事件触发会议、accept/reject、AI callback 迟到与 scene reload。
