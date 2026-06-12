# TODO

## Milestone: StudioSim 打磨  <!-- status: active -->

### 下一步
- [ ] `#00051` [FEAT] 用https://wails.io/的方案包裹gnb，单可执行文件直接启动，不再依赖浏览器
- [ ] `#00052` [FEAT] 优化gnb dashboard的git页

### 待规划
- [ ] `#00011` [IDEA] gnb在gkmini上host一个持久的todolist，作为一个“服务器”同步，可以随时随地进行任务管理
- [ ] `#00012` [FEAT] 让 gkNextRenderer 主管线原生运行在浏览器（WebGPU 后端） → specs/00012.md
- [ ] `#00045` [FEAT] gnb需要及时响应TODO文件的修改

### 最近完成
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
- [x] `#00027` [IDEA] 优化SCAD Studio的交互逻辑 → specs/00027.md (blockers/00027.md)
- [x] `#00028` [P1][FEAT] StudioSim R1：产出状态机 ProductionSystem（FProjectState 四仪表 tech/design/art/polish + 工序阶段 + bug 循环，纯确定性不依赖 LLM；pm/boss 只做系数加成不产点） → docs/StudioSim-Production-Model-Refinement.md §6 R1 / §3.1-3.2 → journal/00028.md (2026-06-08)
- [x] `#00029` [P2][FEAT] StudioSim R2：量化表现层（HUD 总进度条 + 四仪表 + 阶段徽章 + bug 计数；头顶 +N 浮字粒子） → docs/StudioSim-Production-Model-Refinement.md §6 R2 / §3.5 → journal/00029.md (2026-06-08)
- [x] `#00030` [P1][FEAT] StudioSim R3：进度感知决策（BuildPrompt 注入项目进度 + 短板提示 + shortMemory，决策优先补最低仪表 / Polish 修 bug） → docs/StudioSim-Production-Model-Refinement.md §6 R3 / §3.3 → journal/00030.md (2026-06-08)
- [x] `#00031` [P1][FEAT] StudioSim R4：对话节流 + 连续性（dialogue 改可选 + 发言闸门、shortMemory 环、气泡自动淡出、chatterBudget 限流） → docs/StudioSim-Production-Model-Refinement.md §6 R4 / §3.4 → journal/00031.md (2026-06-08)
- [x] `#00032` [P1][REFACTOR] StudioSim R5：GatheringSystem 群体聚集与决策（替换单例 meeting_；并发会议/茶水间、按项目状态自然触发、多人对白、群体决策玩家【采纳/否决】确认制回灌、茶水间回 mood） → docs/StudioSim-Production-Model-Refinement.md §6 R5 / §3.7 → journal/00032.md (2026-06-08)
- [x] `#00033` [P2][FEAT] StudioSim R6：真实结算 + 多天钩子（Summarize 本地算分 + LLM 仅点评、结算面板、残留写入次日 Briefing） → docs/StudioSim-Production-Model-Refinement.md §6 R6 / §3.6 → journal/00033.md (2026-06-08)
- [x] `#00007` [FEAT] 目前的vcpkg是固定在一个老版本的，把项目依赖升级到最新的vcpkg版本，2026.04.27。并绑定版本，解决各种编译和运行问题。 → journal/00007.md (2026-06-08)
- [x] `#00034` [P0][FEAT] StudioSim有需要玩家判断的时候，要把游戏进度停下来，目前开会需要玩家选择的时候，有可能等到当天直接结束。 → journal/00034.md (2026-06-08)
- [x] `#00035` [FEAT] StudioSim每天的目标选择，开会时的决策选择，以及当天结束的复盘并进入下一天。需要单独弹出对话框让玩家来选，不要留在总体面板里 → journal/00035.md (2026-06-08)
- [x] `#00036` [BUG] StudioSim的弹出对话框尺寸太小了，文字换行很多。整体修复一下。 → journal/00036.md (2026-06-08)
- [x] `#00037` [FEAT] 把进度，随机时间，员工状态现在集中于调试面板的信息，都分拆出来，像游戏界面一样分成多个子界面分布于HUD上。 → journal/00037.md (2026-06-08)
- [x] `#00038` [FEAT] 根据docs/StudioSim-GameProject-Iteration-Plan.md的迭代规划，继续迭代StudioSim项目 → journal/00038.md (2026-06-08)
- [x] `#00039` [BUG] 目前员工头上的overlay层级太高，会遮挡其他面板 → journal/00039.md (2026-06-08)
- [x] `#00040` [FEAT] 继续完成docs/StudioSim-GameProject-Iteration-Plan.md规划的剩余未做的项目 → journal/00040.md (2026-06-08)
- [x] `#00041` [FEAT] 继续完成docs/StudioSim-GameProject-Iteration-Plan.md规划的剩余未做的项目 → journal/00041.md (2026-06-08)
- [x] `#00042` [FEAT] 继续完成docs/StudioSim-GameProject-Iteration-Plan.md规划的剩余未做的项目 → journal/00042.md (2026-06-08)
- [x] `#00043` [FEAT] 继续完成docs/StudioSim-GameProject-Iteration-Plan.md规划的剩余未做的项目 → journal/00043.md (2026-06-08)
- [x] `#00044` [FEAT] 根据docs\WebRTC-RemotePlay-Design.md的开发计划，实现引擎的webrtc远程游戏功能 → journal/00044.md (2026-06-08)
- [x] `#00046` [FEAT] 根据docs\WebRTC-RemotePlay-Design.md的开发计划，继续推进 → journal/00046.md (2026-06-08)
- [x] `#00048` [IDEA] todo任务的获取和等待，在gnb上做一个命令来实现。llm有时会思考出错误的指令导致交互式工作流终端 → journal/00048.md (2026-06-08)
- [x] `#00047` [FEAT] 根据docs\WebRTC-RemotePlay-Design.md的开发计划，继续推进到结束 → journal/00047.md (2026-06-08)
- [x] `#00050` [FEAT] 根据docs\WebRTC-RemotePlay-Design.md的开发计划，继续推进 → journal/00050.md (2026-06-08)
