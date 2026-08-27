---
title: "src/Engine 核心精炼 Round 5：可选能力拆分与核心边界修订"
category: plan
status: 已完成
owner: engine
created: 2026-08-20
last_updated: 2026-08-20
---

# src/Engine 核心精炼 Round 5

> 2026-08-20 架构修订：29,771 行的阶段结果把基础 renderer 与 scene acceleration 也作为
> provider 模块迁出，导致 `gkNextMinimalRenderer` 不能只依赖 `gkNextEngine`。最终边界改为：
> UI、捕获、validation、scene catalog、upscaler 保持可选；基础 renderer、CPU scene acceleration
> 与 procedural primitive 归属核心。30k 验收线因此被核心完整性约束取代。

## 目标与基线

- 统计口径：`gnb loc` 的 C/C++ 代码行（排除空行与纯注释行）。
- 当前基线：`src/Engine` **40,862 行 / 279 文件**。
- 历史最低点：2026-07-10 `3a5e7707` 的 **30,324 行 / 219 文件**。
- 本轮验收：**≤ 29,800 行**，为 30k 红线保留至少 200 行演进余量；禁止删注释、压多语句到单行或机械合并文件凑数。

从历史最低点到当前，Engine 净增长 10,538 行：Runtime +4,353、Rendering +3,429、Assets +1,168、
Vulkan +868、Utilities +637。Runtime 增长中 `Runtime/Editor` 从 1,178 增至 3,238；Rendering 新增了
Atmosphere、surface scheduler、ReSTIR、RenderView 和更多后处理编排。这些增长既包含合理的新能力，也暴露出
“实现先落核心、边界稍后再补”的所有权反弹。

## 前几轮经验

1. **先建窄接缝，再移动实现。** LoaderRegistry、外部 render pass、upscaler/interposer registry 的成功说明，
   核心只应保留稳定契约；模块通过显式 `Install`/`Register` 装配。
2. **按能力所有权拆，不按文件大小拆。** Gameplay、AI、Remote、Streamline、SceneExport 的迁移都降低了依赖面；
   单纯把 god class 拆成多个 `.cpp` 只改善导航，不降低核心职责或 LOC。
3. **模块必须可缺省。** 最小 target 应能不链接 UI、捕获、诊断和高级渲染 feature；核心不得反向 include
   `src/Modules`，也不得靠未声明的静态库循环依赖补符号。
4. **写法压缩必须提升内聚。** 之前 CVar variant dispatch、截图像素循环和 pipeline 表驱动有效；以宏或单行化
   换 LOC 会降低维护性，本轮明确不采用。

## 执行批次与预算

| 批次 | 边界 | 预计 Engine 净减 | 核心保留内容 |
|---|---|---:|---|
| A | `NextUI`：ImGui/Vulkan backend、Desktop UI Foundation、字体/缩放/纹理实现 | 2,900–3,050 | `UserInterface` 抽象、UI frame/type、multi-viewport 契约、factory |
| B | `NextCapture`：截图编码、异步视频导出与文件输出 | 850–1,000 | capture request/state、renderer readback 契约 |
| C | `NextRenderFeatures`：具体 logic renderer、Atmosphere、Shadow feature | 1,500–1,850 | `LogicRendererBase`、factory registry、资源/帧生命周期契约 |
| D | 开发与内容能力归位：agent validation、RenderDoc、reflection dump、scene catalog、proc/ozz builder、pak authoring | 1,600–2,000 | 输入/反射/loader/package 的运行期窄接口 |
| E | Ambient GI 与 CPU BVH 解耦：probe bake/brick residency 从 raycast/nav BVH 拆出 | 900–1,200 | snapshot、raycast、dirty bounds、通用 CPU TLAS |
| F | 核心去重：renderer resource/post chain、Scene collect/upload、Vulkan feature chain、CVar/FileHelper | 2,000–2,500 | 等价行为与现有生命周期不变量 |

保守预算为 9,750 行，仍不足以越过 30k；因此每批完成后重新测量，优先从 F 的候选池补足，最终以 29,800
硬闸门为准。若某项接缝导致核心新增量超过预算，不用“移动更多必需代码”掩盖，而是回看接口形状。

## 依赖规则

- `gkNextEngine` 不依赖任何 `Modules/*` target；模块只依赖核心或明确的更底层模块。
- 每个可选能力必须有无模块实现时的确定性行为（无 UI、无捕获、无大气等），不得空指针崩溃或静默选择错误路径。
- 标准 desktop runtime 通过 `GK_STANDARD_RUNTIME_MODULES` 安装常用能力；`gkNextMinimalRenderer` 只显式安装它真正需要的模块。
- Android 仍可把模块源并入单一 shared target，但源码所有权和注册顺序必须与 desktop 一致。

## 验证闸门

每批至少执行：

1. `gnb build gkNextRenderer gkNextUnitTests`；改到单个 program 时另构建该 target。
2. 核心单测；迁移到模块的测试必须显式链接该模块，不能因链接偶然性通过。
3. 渲染路径改动用 `gnb shot --scene assets/models/playground.glb` 肉眼检查；UI 改动加 `--ui`。
4. 批次收尾记录 `gnb loc`，最终必须 ≤29,800。
5. 涉及广泛 public header/ABI 的最终收尾执行一次 `gnb build --all --reconfigure`。

## 实际落地（2026-08-20）

`gnb loc` 曾测得阶段结果 `src/Engine` 29,771 行 / 205 文件；按上述架构修订将基础能力归位后，
最终为 **35,702 行 / 248 文件**。相对 40,862 行 / 279 文件的基线仍净减
**5,160 行（12.6%）/ 31 文件**。

| 新边界 | 迁出实现 | 核心保留 |
|---|---|---|
| `NextUI` | ImGui backend、字体/缩放/纹理、Desktop UI Foundation | `IUserInterface`、frame/multi-viewport 契约 |
| `NextCapture` | 图像编码、视频导出、异步文件输出 | 捕获请求状态与 GPU readback |
| `NextValidation` | loopback server、命令解释、SDL 合成输入 | `IAgentControlService`、query registry |
| `SceneContent` | 场景扫描、scene reference 构造与递归引用解析 | 已注册 loader/`.proc` 的核心直达路径 |
| `DevTools` | RenderDoc 与 reflection manifest dump | 反射注册与 Vulkan debug 基础契约 |

六种 logic renderer、大气、GPU-driven、CSM、硬件 RT、CPU BVH、probe/brick bake 与 procedural mesh
构造最终归回 `gkNextEngine`。`gkNextMinimalRenderer` 使用 `CORE_ONLY`，不链接任何 `Modules/*` target；
其他应用仍通过 `GK_STANDARD_RUNTIME_MODULES` 装配 UI、捕获、validation、SceneContent 与 upscaler 等可选能力。

最终验证覆盖全目标重配置构建、336 个单元测试（79,108 条断言）、标准渲染器 playground 截图，
以及最小渲染器 CornellBox 启动/渲染/干净退出。验证期间同时修正了关闭顺序：任务协调器现在于
Scene 与 renderer 释放后销毁，避免 Scene 析构阶段重新创建协调器造成退出竞态。
