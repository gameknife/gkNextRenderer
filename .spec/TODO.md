# TODO

## Milestone: 工作流落地  <!-- status: active -->

里程碑目标：完成 spec 工作流的三步落地（规范 + gnb todo + dashboard）。

### 下一步

- [x] `#00001` [IDEA] 介绍gnb技术栈  → journal/00001.md (2026-05-14)
- [x] `#00002` [IDEA] 介绍typescript整合  → journal/00002.md (2026-05-14)
- [x] `#00003` 介绍brotato3D  → journal/00003.md (2026-05-18)
- [x] `#00004` [IDEA] 介绍flappybird  → journal/00004.md (2026-05-18)
- [x] `#00005` [FEAT] 在windows下，设置 SDL 的 Windows DPI awareness 为 unaware, 使用系统拉伸的方式适配DPI缩放  → journal/00005.md (2026-05-18)
- [x] `#00008` [IDEA] gnb的todolist要增加维护功能，可以调换add的任务顺序。
- [x] `#00009` [IDEA] gnb需要能够给add的task增加spec文件，写明执行的详细背景信息
- [x] `#00013` [REFACTOR] 重构 SwModernNoAmbient 渲染流程：Lambert+IBL + CSM 阴影 → journal/00013.md (2026-05-20)
- [x] `#00014` [FEAT] 把 `gnb loc` 接到 dashboard（分类表格 + 柱图），同步关闭 #00010 → journal/00014.md (2026-05-26)
- [x] `#00016` [DOCS] AGENTS.md 维护：对照当前项目结构清理过时信息（含已删/重命名的模块、命令、特性），补全新增子项目与工具（dashboard chat、loc、CSM 等），控制总篇幅 → journal/00016.md (2026-05-26)
- [x] `#00017` [REFACTOR] AIService Agent 化 Phase 1：LLM 协议升级（tools schema + provider Chat 接口） → journal/00017.md (2026-05-27)
- [x] `#00018` [FEAT] AIService Agent 化 Phase 2：本地 llama provider（复用 gnb llama-server，PID 自动发现） → journal/00018.md (2026-05-27)
- [x] `#00019` [FEAT] AIService Agent 化 Phase 3：Agent Loop + Tool Registry + 主线程 dispatcher → journal/00019.md (2026-05-27)
- [x] `#00020` [FEAT] AIService Agent 化 Phase 4：通用知识工具（list_dir/find_symbol/read_file/git_log 等，C++ 移植 gnb） → journal/00020.md (2026-05-27)
- [x] `#00021` [FEAT] AIService Agent 化 Phase 5：编辑器工具集（包装 EditorScriptExecutor + DEFERRED high-risk） → journal/00021.md (2026-05-27)
- [x] `#00022` [FEAT] AIService Agent 化 Phase 6：AI Panel UI 升级（步骤可视化 + 停止 + 模式开关） → journal/00022.md (2026-05-27)
- [x] `#00023` [REFACTOR] AIService Agent 化 Phase 7：FEditorAIService 主路径切换到 agent，砍掉大 system prompt → journal/00023.md (2026-05-27)
- [x] `#00026` [IDEA] 让SCAD的LLM生成结果更加结构化，按module拆分文件 → specs/00026.md → journal/00026.md (2026-05-31)
- [!] `#00027` [IDEA] 优化SCAD Studio的交互逻辑 → specs/00027.md (blockers/00027.md)

### 待规划
- [ ] `#00024` [FEAT] AIService Agent 化 Phase 8：MagicaLego AI 接入 agent loop → specs/00024.md
- [ ] `#00025` [DOC] AIService Agent 化 Phase 9：端到端集成测试 + AGENT_GUIDE/AIAgentSystem.md → specs/00025.md
- [ ] `#00007` [FEAT] 目前的vcpkg是固定在一个老版本的，把项目依赖升级到最新的vcpkg版本，2026.04.27。并绑定版本，解决各种编译和运行问题。
- [ ] `#00011` [IDEA] gnb在gkmini上host一个持久的todolist，作为一个“服务器”同步，可以随时随地进行任务管理
- [ ] `#00012` [FEAT] 让 gkNextRenderer 主管线原生运行在浏览器（WebGPU 后端） → specs/00012.md

### 最近完成

(暂无)
