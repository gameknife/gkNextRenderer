# Archive

> 历史任务索引，不是当前实现说明。旧 journal 中的路径、架构和验收结论可能已被后续提交替换；继续工作前应以 `AGENTS.md`、当前代码、测试和 `docs/README.md` 为准。

## 2026-07 文档清理结论

以下任务原先仍显示在活动任务面，但核对代码与提交记录后已不应继续作为待办：

| ID | 结论 |
| --- | --- |
| `#00012` | WebGPU 浏览器后端未形成当前路线，旧 spec 已放弃并删除；若重启需重新立项。 |
| `#00051` | Wails 原生 Dashboard 已实现；Linux 按设计回退浏览器模式。 |
| `#00054` | ScadRig 与 AirportSim 的替换工作已落地，旧设计文档已由当前 `AGENT_GUIDE/ScadRig.md` 取代。 |
| `#00055` | NextRemote 已使用 Vulkan Video H.264 硬件编码；旧分阶段计划不再描述当前代码。 |
| `#00056` | “优化 ModernNoAmbient”没有可验证范围，且旧 spec 已失效；需要时应按具体缺陷重新立项。 |
| `#00060` | SHARC 调试色彩、层级选择及默认场景尺度问题已在当前实现中处理。 |
| `#00063` | 第一方代码中的旧 ImGui 版本分支已清除；剩余宏位于第三方代码。 |
| `#00064` | 已由 `#00066` 的 AmbientCube 命中驱动驻留实现覆盖。 |
| `#00067` | “提交目前所有修改”是过期的一次性操作，不作为长期项目任务保留。 |

## 2026-06

- [x] `#00007` vcpkg 基线升级 → journal/00007.md (2026-06-08)
- [x] `#00028` StudioSim ProductionSystem → journal/00028.md (2026-06-08)
- [x] `#00029` StudioSim 量化表现层 → journal/00029.md (2026-06-08)
- [x] `#00030` StudioSim 进度感知决策 → journal/00030.md (2026-06-08)
- [x] `#00031` StudioSim 对话节流与连续性 → journal/00031.md (2026-06-08)
- [x] `#00032` StudioSim GatheringSystem → journal/00032.md (2026-06-08)
- [x] `#00033` StudioSim 结算与多天钩子 → journal/00033.md (2026-06-08)
- [x] `#00034` StudioSim 玩家决策时暂停进度 → journal/00034.md (2026-06-08)
- [x] `#00035` StudioSim 独立决策弹窗 → journal/00035.md (2026-06-08)
- [x] `#00036` StudioSim 弹窗布局修复 → journal/00036.md (2026-06-08)
- [x] `#00037` StudioSim HUD 拆分 → journal/00037.md (2026-06-08)
- [x] `#00038` StudioSim 迭代 → journal/00038.md (2026-06-08)
- [x] `#00039` StudioSim overlay 层级修复 → journal/00039.md (2026-06-08)
- [x] `#00040` StudioSim 迭代 → journal/00040.md (2026-06-08)
- [x] `#00041` StudioSim 迭代 → journal/00041.md (2026-06-08)
- [x] `#00042` StudioSim 迭代 → journal/00042.md (2026-06-08)
- [x] `#00043` StudioSim 迭代 → journal/00043.md (2026-06-08)
- [x] `#00044` WebRTC Remote Play 初始实现 → journal/00044.md (2026-06-08)
- [x] `#00045` TODO 修改监听 → journal/00045.md (2026-06-12)
- [x] `#00046` WebRTC Remote Play 迭代 → journal/00046.md (2026-06-08)
- [x] `#00047` WebRTC Remote Play 迭代 → journal/00047.md (2026-06-08)
- [x] `#00048` `gnb todo next --wait` → journal/00048.md (2026-06-08)
- [x] `#00050` WebRTC Remote Play 迭代 → journal/00050.md (2026-06-08)
- [x] `#00053` QuickJS 模块迁移方案 → journal/00053.md (2026-06-12)
- [x] `#00057` SHARC 官方实现整合 → journal/00057.md (2026-06-16)
- [x] `#00058` SHARC 过亮修复 → journal/00058.md (2026-06-16)
- [x] `#00059` SHARC Radiance Cache 调试显示 → journal/00059.md (2026-06-16)
- [x] `#00061` SwModernNoAmbient 天空遮蔽 → journal/00061.md (2026-06-21)
- [x] `#00062` Editor 图标字体清理 → journal/00062.md (2026-06-22)
- [x] `#00065` Editor CVar/Settings 面板改造 → journal/00065.md (2026-06-24)
- [x] `#00066` AmbientCube 命中驱动驻留 → journal/00066.md (2026-06-24)

## 2026-05

- [x] `#00001` gnb 技术栈说明 → journal/00001.md (2026-05-14)
- [x] `#00002` TypeScript 集成说明 → journal/00002.md (2026-05-14)
- [x] `#00003` Brotato3D 项目说明 → journal/00003.md (2026-05-18)
- [x] `#00004` Flappy 项目说明 → journal/00004.md (2026-05-18)
- [x] `#00005` Windows DPI awareness 调整 → journal/00005.md (2026-05-18)
- [x] `#00008` TODO 任务顺序维护（历史完成，无 journal）
- [x] `#00009` TODO spec 文件支持（历史完成，无 journal）
- [x] `#00013` SwModernNoAmbient 渲染流程重构 → journal/00013.md (2026-05-20)
- [x] `#00014` Dashboard LOC 页面 → journal/00014.md (2026-05-26)
- [x] `#00016` AGENTS 文档维护 → journal/00016.md (2026-05-26)
- [x] `#00017` 至 `#00023` AIService Agent 化历史阶段（2026-05-27；该通用 Agent 架构后来已整体移除，旧 journal 只在 Git 历史中保留）
- [x] `#00026` SCAD 结构化生成 → journal/00026.md (2026-05-31)
- [x] `#00027` SCAD Studio 交互探索（历史上阻塞，后续实现已取代；旧 spec/blocker 不再保留）

## Pre-workflow（2026-05-14 之前）

从旧版根目录 `TODO.md` 迁移。无 ID、无 journal，仅保留原始描述；这些条目同样只作历史记录。

- [x] 提交当前修改
- [x] 确认 AmbientCube 的改造，目前 Voxel 的更新和 GPU 读取是正确的，但 AmbientCube 感觉没有工作。但 hwlightbake 在执行
- [x] 提交目前的修改
- [x] 清理上下文
- [x] 恢复 wireframe 的工作，这里 wireframePipeline_，可考虑直接写在 imgui 绘制前，直接绘制在最终输出之上。不要像之前一样尝试写在 RT_DENOISED 上
- [x] Options 下的 bool HotReload{true} 已经废弃，移除整个选项以及相关无用的逻辑
- [x] wireframe 的修改有些问题，只有当 scale 为 native 的时候，绘制正常。当使用 quality 等缩放模式的时候，线框会位于画面的左上角。修复这个问题
- [x] 提交目前的修改
- [x] 把本地 feature/productive-ui-refactor 分支关于 ui 的重构以及前面的 Brotato3D Tweaks 的提交合并到本分支，并运行验证通过
- [x] 彻底解决 LDraw 的那个单元测试错误，如果是因为 Optional 资源问题，实在不行，干掉它
- [x] 目前 `.\gnb.bat run xxxx` 无法带上 target 本身支持的命令行参数，很不方便，请改造
- [x] Brotato3D 动态对象不再触发不必要的 BVH 与 voxel 重建
