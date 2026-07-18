---
title: "AirportSim 架构与确定性边界"
category: project
status: 现行
owner: AirportSim
created: 2026-07-17
last_updated: 2026-07-17
---

# AirportSim 架构与确定性边界

AirportSim 是以游戏分钟驱动的机场生态 demo。旧 MVP plan 的任务拆分已经完成，但 Layer 0/Layer 1 边界、线程纪律和 SCAD 点位契约仍是当前实现的核心。

## 系统分层

```text
airport.scad named nodes
  → AirportMap / Sim Kit anchors + NavGrid
  → Time / FlightBoard / Journey / Queue / Agent（Layer 0）
  → Perception events
  → DecisionScheduler + NextAI（可选 Layer 1）
  → allowlisted result，主线程 apply；失败走规则 fallback
```

共享移动、角色池和 anchor 基础设施来自 `src/Gameplay/Sim/`，边界见 `AGENT_GUIDE/SimKit.md`。机场领域状态仍留在 application；不要把航班、旅客旅程或排队语义抽进 Sim Kit。

## Layer 0 必须独立成立

`TimeSystem` 是所有系统的时间事实源：跨天累计 `GameMinutes()`，当日时间由 `DayMinutes()` 派生。航班、班次、排队服务、停留、决策 cooldown 和气泡期限都使用游戏分钟；真实秒只在时钟/移动入口转换，不能在各系统混用两套时基。

`JourneySystem` 拥有旅客从 spawn、值机、安检、空侧活动、登机到 despawn 的刚性主线，以及员工通勤/在岗/离岗日程。`FlightBoard`、`QueueSystem` 和 Journey 即使没有 NextAI 也必须让机场持续运转。seeded `mt19937` 与规则 fallback 提供可复现基线，但这不是网络 lockstep 承诺。

SCAD 具名 node 是 POI contract。`AirportMap` 通过 Sim Kit `FAnchorMap` 解析 name/category/world transform，front direction 遵循 SCAD 局部 `-Y` 到引擎 `+Z` 的约定。服务点、座位和队列必须通过 claim/release API 管理，不能直接改 `occupiedBy` 绕过所有权。

## Layer 1 的权限上限

`DecisionScheduler` 每次最多一个在途员工/旅客决策，并轮转 scan cursor，避免低 index 角色垄断本地模型。结构化输出只允许：

- action：`idle`、`goto`、`use_poi`、`say_to`；
- mood：固定六值枚举；
- target 最多 64 UTF-8 code points，say 最多 20。

LLM 可以改变 mood/台词、触发有界对话，并为处于空侧弹性活动的旅客选择合法 POI；它不能跳过值机/安检/登机、改航班时间、凭空占用 POI 或直接操作 Scene。目标还要经过 `JourneySystem::ApplyAirsideChoice` 的类别/当前状态校验。

NextAI 不可用、agent validation、解析失败或超时都走规则 fallback。迟到 callback 由 generation 丢弃，不得覆盖已经 fallback 的状态。

## 线程纪律

NextAI callback 在 worker 线程只做 response 解析并在 mutex 下写 `completed_`。`DecisionScheduler::Tick()` 在主线程 drain、查找仍存活的 agent 并 apply；Scene、agents、Journey、POI 和 ImGui 只能在主线程访问。

session reset/unload 要增加 generation，使旧 callback 无效。不要为了“减少延迟”从 callback 直接移动角色或写气泡；这会引入 use-after-free 和跨线程 Scene 竞态。

## 生命周期与验证

应用遵循 `BeforeSceneRebuild` 注入资产 → `OnSceneUnloaded` 清运行时指针 → `OnSceneLoaded` 解析 POI/初始化池。共享 ScadRig visual 的注入数据不能在 unload 阶段提前销毁。

构建/截图：

```bash
./gnb.sh build AirportSim
./gnb.sh shot --target AirportSim --ui --frames 300
```

验证 Layer 0 时关闭 LLM，观察跨班次、队列前移、旅客完整旅程和 pool 回收；验证 Layer 1 时再检查 schema 拒绝、超时、迟到 callback、fallback 和 scan fairness。不能把一段自然语言对话当成机场流程正确性的证据。
