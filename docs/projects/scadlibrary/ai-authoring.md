---
title: "ScadLibrary AI 创作实现"
category: project
status: 现行
owner: ScadLibrary/NextAI
created: 2026-07-28
last_updated: 2026-07-28
---

# ScadLibrary AI 创作实现

ScadLibrary 已内建基于 NextAI 的受约束创作链。入口位于右侧 Inspector 的 `AI` 页签和标题栏
`AI` 按钮；Kit 浏览器模块的右键菜单还提供“AI 编辑此模块”。

AI 面板不再提供手动“编辑源”选择器。上下文由资产类别唯一决定：

- `assets/scad/evaluated/` → `SceneObjects`，可携带 viewport/对象列表中的精确实例选择；
- `assets/scad/source/` → `SceneSource`，保留程序结构，不伪装成可寻址实例；
- `assets/scad/proc/` → `TerrainProcess`，编辑 feature/rule；
- 左侧预览 Kit module → `KitModule`；重新打开场景即返回该场景类别的 adapter。

Source → Evaluated 必须由用户点击“转换为 Evaluated 副本”显式触发。转换结果写入
`assets/scad/evaluated/<原名>_evaluated.scad`，原 Source 不会被求值结果覆盖。

## 已实现链路

- Kit module：模型返回一个完整且同名、同签名的 module definition；本地通过 parser-aware
  byte span 替换到 Kit clone。应用后只进入 Kit 草稿。显式保存时加载完整依赖闭包、用真实
  evaluator 重算目标 module metrics，并原子提交 Kit 与 `catalog.json`。
- 场景源码：模型返回完整 candidate source；本地 lexer/parser 校验后可预览、应用到
  `assemblySource_`、撤销，再走原有显式场景保存。
- 结构化场景：模型只返回 `add/update/remove/duplicate/reorder` 操作。操作引用 snapshot id，
  module 受当前 catalog allowlist 限制，arguments 必须仍解析为单一 module call。
- 过程场景：模型只返回 Terrain/feature/rule typed operation；`rN` 会保留原 rule source span，
  删除规则会走 `removed`，因此注释、自由 SCAD 和未识别内容仍由
  `FTerrainProcessDocument` byte-preserving 地保留。
- Rig 动作：模型只返回 clip/channel/key operation；bone 和 channel 受本地 allowlist 约束。
  `FCharacterWorkbench::ApplyToAsset()` 已改为按当前 DTO 安全重建 clip，支持新增动作。

所有链路都遵循：

```text
authoritative snapshot + target + revision
  → stateless structured request
  → strict JSON/domain validation（最多一次 repair）
  → candidate clone
  → preview
  → explicit apply to dirty in-memory state
  → existing explicit save
```

AI 不获得文件、shell、Scene 或通用 tool 权限。请求发出后 target/revision 固定；只要用户切换
目标或手工修改内容，旧 proposal 就变为 stale，不能应用。

### 选中实例定向编辑

仅在 Evaluated 场景中，用户在对象列表或 viewport 中选中的 Kit 实例会成为显式 AI focus。请求
snapshot 同时包含 `selectedId`、完整 `selectedObject`（Kit、module、transform、arguments、
color）和 `selectionScope=selected_instance`；面板目标栏也显示具体 `Kit/module`。模型默认
以该实例为主要修改对象，只有用户指令明确要求整体场景调整时才扩大范围。未选择实例时
`selectionScope=scene`，面板会明确提示 AI 将编辑整体场景。

### 原案 / 提案 A/B 对比

通过本地校验的 proposal 提供“预览候选”和“对比原案”两个相邻按钮。两者复用同一个 viewport、
相机、渲染器和 SCAD 坐标轴，可反复切换进行 A/B 确认；面板显示 viewport 当前状态。切换目标、
重新生成、拒绝或撤销时会结束候选预览并恢复当前原案。预览不修改 authoritative document，
只有“应用到草稿”会进入 dirty state。

## Provider 结构化输出兼容性

完整场景源码和单个 Kit module 的固定字段 artifact 使用 strict JSON Schema。结构化场景、
过程场景和 Rig clip 的 operation payload 是按 operation type 稀疏变化的开放对象，因此使用
provider 的非 strict JSON Schema 生成，再由本地 typed validator 执行严格字段、ID、类型、
allowlist 和 SCAD 语法校验。不能把这些 operation schema 以 `strict=true` 发送给 OpenAI：
OpenAI strict mode 要求每个 object 都设置 `additionalProperties:false`，且 `properties` 中
所有字段都必须出现在 `required` 中；不满足时会在生成前返回 HTTP 400。

高级推理模型的单次创作 deadline 为 300 秒，OpenAI-compatible HTTP transport 的硬上限为
10 分钟。用户仍可随时通过 AI 面板取消当前 run。

## 运行与离线行为

ScadLibrary 链接 `NextAI`，使用 `gnb.toml` 的 `scad-authoring` profile。AI 面板顶部提供
Provider 和模型选择器，数据直接来自 gnb provider catalog；未配置或不可用的 Provider 会显示但
不可选择，生成期间会锁定选择器。切换会重建当前 `scad-authoring` session，后续请求立即使用
新 Provider/模型。AI service 在首次打开 AI 面板或提交请求时初始化；Bridge/provider 不可用
不会影响任何手工编辑功能，面板会保留错误信息和重试入口。

`--agent-validation` 会注入 `FFixtureScadAITransport`，生成确定性的 schema-valid proposal，
不会访问本地或远程模型。正常运行使用 `FNextAIScadTransport`；显式 `runId`、deadline 和
`run.cancel` 贯穿到 Bridge v2。

中央 viewport 左下角常驻显示随相机旋转的 SCAD 坐标轴：X 红、Y 绿、Z 蓝。该标识使用
OpenSCAD 右手坐标系，便于在自然语言指令中明确表达沿轴移动和绕轴旋转，而不是暴露引擎内部
坐标转换。

本地有限 history 位于 ScadLibrary user-data 的 `ai/` 目录，只记录自然语言、摘要和
accepted/rejected outcome；请求仍固定 `stateless=true`，不会叠加 Bridge session history。

## ScadStudio 迁移状态

AI 页签可只读扫描进程工作目录下旧 `scad_studio/sessions.json` 与 session JSON。安全相对路径
校验和 SCAD parser 通过后，选中文件可导入为一个无路径、未保存的场景草稿；不会复制 archived
chat、删除旧 session 或覆盖现有资产。

ScadStudio target 目前仍保留。退役仍需完成更广的真实 provider 质量观察和四类 UI
agentscript 覆盖；在这些门槛满足前，不应删除 standalone 应用。

## 关键代码

- `src/Application/Editor/ScadLibrary/AI/ScadAIContracts.*`
- `src/Application/Editor/ScadLibrary/AI/ScadAIController.*`
- `src/Application/Editor/ScadLibrary/AI/Adapters/`
- `src/Modules/ScadLoader/FScadSourceIndex.*`
- `src/Application/Editor/ScadLibrary/CharacterWorkbench.*`

验证：

```bash
./gnb.sh build ScadLibrary gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[AI][ScadLibrary]"
./out/build/<preset>/bin/gkNextUnitTests "[ScadLibrary]"
./gnb.sh validate --script assets/agentscripts/scadlibrary-ai-terrain.agentscript.json
```
