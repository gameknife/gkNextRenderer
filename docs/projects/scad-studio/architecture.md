---
title: "ScadStudio 会话、生成与预览架构"
category: project
status: 现行
owner: ScadStudio
created: 2026-07-17
last_updated: 2026-07-28
---

# ScadStudio 会话、生成与预览架构

ScadStudio 是 Sessions | Viewport | Chat 三栏的 SCAD authoring application，实现在 `src/Application/Editor/ScadStudio/`。旧 model-generator plan 已完成；本文记录仍决定会话一致性、AI 安全和 live preview 的契约。

## 已确认的迁移方向

ScadStudio 的自然语言创作能力将迁入 ScadLibrary，按 Kit module、场景、Terrain 过程节点和
Rig 动作分别生成受约束 proposal；设计与开发顺序见
[ScadLibrary AI 融合创作架构](../../designs/scadlibrary-ai-authoring-integration.md) 和
[开发计划](../../plans/scadlibrary-ai-authoring-plan.md)。

本文件描述的 standalone 实现目前仍是现行代码。只有 ScadLibrary 达到设计中的功能等价、
旧 session 可导入且四类 adapter 均通过验收后，才能退役 ScadStudio；迁移期间不得把旧
session workspace 当成 ScadLibrary 资产的事实来源。

## 权威状态

`FScadSession` 是产品事实源：id/title、chat turns、单文件 `currentSource` 或多文件 `files`、active file/module scope、outline 和 scene path。当前工作区是进程 current working directory 下的 `scad_studio/`：

- `sessions.json` 保存可见 session 顺序/归档状态；
- `<id>.json` 保存 turns、当前 source/files 和 UI 状态；
- 单文件兼容路径 `<id>.scad`，多文件项目写到 `<id>/main.scad` 等相对路径。

AI conversation 只保留普通 user/assistant 文本；每次请求重新注入 session 的最新权威 source/project files。不要把旧 source 重复塞进每条 history，否则 token 会膨胀且模型可能编辑过期版本。

## 生成与线程模型

`ScadAIService` 在主线程组装 SCAD 专用 system prompt、live source、edit scope 和 instruction，然后在 `std::jthread` 中调用 NextAI。streaming text 和完成结果通过 mutex/atomic handoff；UI `PollAI()` 在主线程按 `pendingSessionId` 路由结果。用户切换 session 不会把结果写到当前选中项，session 已删除则丢弃结果。

worker 不得写 session、文件、Scene 或 ImGui。后续若支持取消/并发，也必须保留“结果属于发起它的 session + edit scope”这一身份检查。

## Artifact 与校验

模型可以返回一个完整 `scad` fenced block，或带安全相对路径的 `scad-project` 多文件 artifact。输出先由 `BuildScadOutline` 做 lexer/parser 级校验；它验证语法和结构，不等价于完整 evaluator/Manifold 几何成功。

修复预算是有界的：service 对首次格式/parse 失败最多做一次协议修复；UI 的 auto-fix 当前还允许最多两轮基于本地 parse error 的重新提交。不得改成无上限 agent loop。repair 期间保留最后一份已渲染 scene；预算耗尽的错误 source 会标错并停止 reload。

所有项目路径都要经过 sanitise，不能允许绝对路径或 `..` 逃出 session workspace。完整 artifact 成功后才替换对应 file/currentSource、持久化并请求预览。

## Outline、scope 与 live preview

`BuildScadOutline` 复用 ScadLoader lexer/parser，去除 `use/include` directive 后生成只读 module/call structure。active file 与 focused module 会进入 prompt，决定默认编辑目标；module preview 可生成临时 wrapper，只展示选中 module。

预览闭环是：

```text
validated session source/files
  → 写 workspace
  → engine.RequestLoadScene(scenePath)
  → ScadLoader rebuild Scene
  → OnSceneLoaded 重设 orbit target/focus
```

文件写入与 Scene reload 只在主线程发生。`currentSource/files` 是权威状态，workspace `.scad` 是 loader 消费的投影；不要反向扫描临时生成文件来覆盖 session。

## 边界与验证

- ScadStudio 不是通用 coding agent，不开放 repo/shell/Scene tool registry。
- parser outline 成功不保证 CSG 性能、字体/Manifold capability 或 0 warning；最终仍以 ScadLoader 日志和渲染为准。
- session chat 的 restore-to-turn 会替换 source/files 并 reload；新增字段时需保持 JSON 向后兼容。
- `ConfigureCVars` 只给迭代预览设低 samples/motion-history 默认值，不应改用户全局配置文件。

```bash
./gnb.sh build ScadStudio
./gnb.sh shot --target ScadStudio --scene assets/scad/beer_cup.scad --frames 60
```

修改生成链时还要测试：生成期间切换/删除 session、单文件与多文件、非法路径、parse repair、restore turn、focused module preview，以及 valid parse 但 evaluator warning 的可见错误反馈。
