---
title: "ScadLibrary AI 创作收口计划"
category: plan
status: M0–M4 已完成；仅剩 ScadStudio 退役门槛
owner: ScadLibrary/NextAI
last_updated: 2026-08-31
design: ../projects/scadlibrary/ai-authoring.md
---

# ScadLibrary AI 创作收口计划

统一 proposal/revision/validation 基座，以及 SceneSource、SceneObjects、KitModule、TerrainProcess、
RigClip 五类 adapter 均已落地。当前实现、运行方式和 provider 约束见
[ScadLibrary AI 创作实现](../projects/scadlibrary/ai-authoring.md)；本文件不再重复已完成任务，只跟踪
ScadStudio standalone target 的退役门槛。

## 当前状态

- 公共 `ScadAIContracts` / controller、显式 run/cancel、stateless request、一次 repair：已完成。
- 预览候选、原案/提案 A/B、显式 apply/undo/save、stale proposal 防护：已完成。
- 统一 SCAD 文档中的对象、源码/结构和 terrain process 编辑：已完成。
- Kit module parser-aware 替换、依赖闭包验证与 catalog 原子保存：已完成。
- Rig clip typed operation 与 DTO 重建：已完成。
- 旧 ScadStudio session 安全导入为未保存草稿：已完成。
- ScadStudio standalone target：保留，直到下列门槛全部满足。

## 剩余退役门槛

1. 为 SceneSource、SceneObjects、KitModule、TerrainProcess、RigClip 各保留至少一条稳定的 UI
   agentscript，覆盖生成、预览、应用、撤销与保存/拒绝；fixture transport 不访问真实 provider。
2. 使用至少一个真实 OpenAI-compatible provider 对五类 adapter 各完成一轮质量观察，记录 schema
   拒绝、repair、超时和取消结果；不得把一次成功当作稳定性结论。
3. 核对 ScadStudio 仍独有的入口、session 数据和用户操作，确认都能导入或由 ScadLibrary 等价完成。
4. 更新入口文档、target 列表和自动化脚本，不再把 ScadStudio 当作首选创作入口。
5. 最后才删除 ScadStudio application/CMake、专属测试和不再读取的 session 代码；迁移器至少保留一个
   发布周期，避免旧数据立即失去入口。

## 退役验收

```bash
./gnb.sh build ScadLibrary gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[AI][ScadLibrary]"
./out/build/<preset>/bin/gkNextUnitTests "[ScadLibrary]"
./gnb.sh validate --script assets/agentscripts/scadlibrary-ai-terrain.agentscript.json
./gnb.sh validate --script assets/agentscripts/scadlibrary-assembly.agentscript.json
./gnb.sh validate --script assets/agentscripts/scadlibrary-designer.agentscript.json
```

删除 target 属于跨 application/CMake 改动，退役提交需额外构建直接受影响目标并检查全仓链接。门槛未
全部通过时，ScadStudio 保持可构建；本计划不授权提前删除。
