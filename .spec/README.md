# .spec — 交互式工作流规范

gkNextRenderer 项目的 spec 驱动开发工作流目录。只有用户明确要求“启动交互式工作流”时，AGENT 才根据这些文件持续调度任务；普通开发请求不会自动执行 `TODO.md`。工作流内 **不调用其他 agent，不自行 sleep**。

## 权威性与生命周期

`.spec` 同时包含当前任务和历史记录，不能把整个目录当成当前实现说明：

1. `AGENTS.md`、代码、构建配置和测试描述当前仓库事实。
2. `TODO.md` 中未完成的任务，以及它显式链接的 `specs/<id>.md`，只描述尚未实现的用户意图。
3. `journal/` 和 `ARCHIVE.md` 是历史快照；其中的文件路径、方案和限制可能已被后续提交替换，不可据此覆盖当前代码。
4. 任务完成、放弃或被后续实现取代后，应从活动任务面移出；不再承担当前约束的 spec 可以删除，历史结论由 journal、archive 和 Git 保存。

执行任务前必须重新核对当前代码和提交记录。若历史记录与当前实现冲突，以当前实现为准，并在新任务中重新写明验收标准。

## 文件结构

| 路径 | 用途 | 谁写 |
| --- | --- | --- |
| `TODO.md` | 活跃任务列表 | 用户（任务内容）+ AGENT（状态字符、journal 链接） |
| `ARCHIVE.md` | 历史任务索引（非当前实现说明） | `gnb todo archive` 或用户 |
| `specs/<id>.md` | 活跃复杂任务的详细规格，**仅 spec 类任务需要** | 用户 |
| `journal/<id>.md` | 任务完成时的历史报告，一任务一文件 | AGENT |
| `blockers/<id>.md` | AGENT 卡住时的提问，一任务一文件 | AGENT |

## TODO.md 格式

```markdown
# TODO

## Milestone: <名字>  <!-- status: active|done -->

### 下一步
- [ ] `#NNNNN` [P0][BUG] 修复贴图采样越界
- [/] `#NNNNN` [P1][FEAT] 体积雾 → specs/NNNNN.md
- [!] `#NNNNN` [SPIKE] work graphs (blockers/NNNNN.md)

### 待规划
- [ ] `#NNNNN` [IDEA] 试试 NRD 降噪

### 最近完成
- [x] `#NNNNN` [BUG] 修复贴图过滤 → journal/NNNNN.md (YYYY-MM-DD)
```

`NNNNN` 表示同一个五位任务 ID；示例只说明语法，不对应仓库中的实际任务文件。

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
- `src/Engine/Runtime/Engine.cpp`

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

以上边界约束的是普通交互式任务执行。若用户明确授权仓库级文档治理或 `.spec` 清洁，AGENT 可以核对代码和 Git 后归档失效任务、删除废弃 spec、修正规范说明；不得借此臆造新需求或悄悄改变仍有效的用户目标。

## AGENT 调度规则

AGENT 不自己实现 TODO 读取、mtime 判断或 sleep 等待；这些逻辑统一交给 `gnb`：

```bash
gnb todo next --wait --timeout 590s --json
```

命令语义：
- 如果"下一步"段已有 `[ ]` 任务，立即返回第一个任务
- 如果当前没有任务，等待 `.spec/TODO.md` 修改；590 秒内出现任务就立即返回
- 如果等待 590 秒仍没有任务，返回 `found: false` 并退出；AGENT 必须立即再次调用同一命令继续等待
- 如果返回 `milestone_status: "done"`，AGENT 退出交互式工作流

取到任务后，AGENT 若发现 `.spec/specs/<id>.md` 存在，先读规格再执行。任务完成后把 TODO 行标为 `[x]`，追加 `→ journal/<id>.md (YYYY-MM-DD)`，并写对应 journal。任务歧义无法判断时，写 `blockers/<id>.md`，把任务标为 `[!]`，然后继续调用命令取下一个任务。
