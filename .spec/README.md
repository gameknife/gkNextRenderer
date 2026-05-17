# .spec — 交互式工作流规范

gkNextRenderer 项目的 spec 驱动开发工作流目录。AGENT 在当前 session 内根据这些文件调度任务，**不调用其他 agent，不 sleep**。

## 文件结构

| 路径 | 用途 | 谁写 |
| --- | --- | --- |
| `TODO.md` | 活跃任务列表 | 用户（任务内容）+ AGENT（状态字符、journal 链接） |
| `ARCHIVE.md` | 归档的完成任务 | `gnb todo archive` 或用户 |
| `specs/<id>.md` | 复杂任务的详细规格，**仅 spec 类任务需要** | 用户 |
| `journal/<id>.md` | 任务完成报告，一任务一文件 | AGENT |
| `blockers/<id>.md` | AGENT 卡住时的提问，一任务一文件 | AGENT |

## TODO.md 格式

```markdown
# TODO

## Milestone: <名字>  <!-- status: active|done -->

### 下一步
- [ ] `#00018` [P0][BUG] 修复贴图采样越界
- [/] `#00019` [P1][FEAT] 体积雾 → specs/00019.md
- [!] `#00020` [SPIKE] work graphs (blockers/00020.md)

### 待规划
- [ ] `#00021` [IDEA] 试试 NRD 降噪

### 最近完成
- [x] `#00017` [BUG] 修复贴图过滤 → journal/00017.md (2026-05-13)
```

### 三个段落

- **下一步**：AGENT 只扫这一段，按从上到下顺序执行
- **待规划**：想法池/积压。AGENT **完全不动**，从待规划挪到下一步由用户操作
- **最近完成**：累积完成的任务，定期由 `gnb todo archive` 移到 ARCHIVE.md

### 状态字符

| 字符 | 含义 |
| --- | --- |
| `[ ]` | pending |
| `[/]` | doing（执行中，正常不持久化此状态，crash 恢复用） |
| `[x]` | done |
| `[!]` | blocked（对应 `blockers/<id>.md` 有说明） |

### ID

五位全局递增数字 `#00001` ~ `#99999`。新 ID 取当前所有任务（含 ARCHIVE）中最大 ID + 1。

### 内联标签

- 优先级：`[P0]` `[P1]` `[P2]`
- 类型：`[BUG]` `[FEAT]` `[IDEA]` `[SPIKE]` `[REFACTOR]` `[DOC]`

## journal/`<id>`.md 格式

```markdown
---
task: 00018
completed: 2026-05-14T15:30:00
build_ok: true
---

## 做了什么
…

## 改动文件
- `src/Rendering/VolumeFog.cpp`

## 风险/遗留
- ⚠️ 与 SSR 有交互问题，未处理
```

## blockers/`<id>`.md 格式

```markdown
---
task: 00018
blocked_at: 2026-05-14T15:30:00
---

## 歧义点
任务描述里说"修复采样越界"，但越界发生在两处：
1. `SampleEnvironment.hlsl:42` — 已知问题
2. `VolumeFog.cpp:128` — 看上去也有类似模式

## 候选方案
- A. 只修 1
- B. 修 1 + 2

等用户答复后继续。
```

## AGENT 行为边界

**可改**：
- TODO.md 中任务的状态字符（`[ ]` → `[x]` / `[!]`）
- TODO.md 中任务行末追加 journal 链接
- `journal/`、`blockers/` 下的文件

**不可改**：
- TODO.md 中任务标题、ID、优先级、类型、所属段落
- `specs/` 下的文件（用户写的需求）
- `ARCHIVE.md`（归档由工具或用户操作）
- "待规划"段任何任务
