---
title: "Release 前代码修缮：书写规范、结构卫生与明显错误清理"
category: plan
status: 执行中（Phase 1-4 已完成，Phase 5 待跨平台彩排）
owner: engine
created: 2026-07-13
last_updated: 2026-07-13
---

# Release 前代码修缮：书写规范、结构卫生与明显错误清理

> 状态：Phase 1-4 已完成 / Phase 5 待执行 | 编写日期：2026-07-13
> 背景：项目即将进入对外发布节奏（系列文章深链源码 + README 导流 + 预编译 release）。多篇文章会把读者直接带到仓库的具体文件与行号，这批"公开检视路径"上的拼写错误、命名混乱与文档漂移会直接损伤可信度。本计划聚焦**书写规范、结构明显不合理、代码不优雅、明显错误**四类问题，不做大型架构重构。
>
> 与既有计划的边界：
> - 架构级拆分归 [engine-core-refactor-round5.md](engine-core-refactor-round5.md) 与 [vulkan-base-renderer-architecture-audit-fable.md](../notes/vulkan-base-renderer-architecture-audit-fable.md)，本计划不重复其条目；
> - StudioSim/AirportSim 公共层抽取归 [studiosim-refactor-simkit-plan.md](studiosim-refactor-simkit-plan.md)，本计划仅标注其发布相关性；
> - 本计划的全部任务均为**行为不变**的修缮：编译等价、渲染结果等价（用 `gnb shot` / VisualTest 验证）。

## 0. 执行进度（2026-07-13）

| 阶段 | 状态 | 落地提交 / 说明 |
| --- | --- | --- |
| Phase 1 | 完成 | `7c6588c7`：拼写、死代码、文档漂移、工具链；MIT LICENSE 同步落地 |
| Phase 2 | 完成 | `de2e839e`：深链文件精修 |
| Phase 3 | 完成 | `93f4447e`：ScadLoader 结构整理与物理崩溃保护 |
| Phase 4a | 完成 | `5aceac61`：Engine/Modules tab 缩进统一为 4 空格 |
| Phase 4b | 完成 | `cff3d43e`：Engine 头文件统一为 `.hpp`；全 target `--reconfigure` 构建通过 |
| Phase 4c | 完成 | `2e5e034c`：Engine 中文注释统一英译；171 个单测（49,525 条断言）通过 |
| 拼写防护补齐 | 完成 | `9ad37dc`：新增 `gnb typos`；全量 Go 测试与仓库拼写检查通过 |
| Phase 5 | 待执行 | Windows/Linux/macOS clean-clone 彩排、文章命令实测、预编译产物冒烟 |

当前已知验证阻塞：macOS 上 `gkNextVisualTest` 在首个场景截图时抛出
`screenshot capture was not recorded before present`，尚未进入 baseline diff。全量编译、单测与
拼写检查均已通过；该问题需按视觉测试截图时序单独诊断，不计作本轮机械修缮的像素回归。

---

## 1. 发布可见面（本次修缮的优先级依据）

以下文件/路径将被对外文章直接深链或在 README 导览中露出，属于修缮的最高优先级：

| 路径 | 露出场景 |
| --- | --- |
| `assets/shaders/common/BasicTypes.slang`（GPUScene / 128B push constant） | 零 Bind 文章深链 |
| `assets/shaders/common/BindlessTexture.slang` | 零 Bind 文章深链 |
| `src/Engine/Runtime/Editor/UserInterface.cpp` | ImGui backend 文章深链 |
| `src/Modules/ScadLoader/`、`src/Application/Editor/ScadStudio/` | SCAD 文章深链 + ScadStudio 预编译 release |
| `src/Application/Game/AirportSim/DecisionScheduler.*`、`StudioSim/DecisionScheduler.*` | 运行时 LLM 文章深链 |
| `AGENTS.md`、`AGENT_GUIDE/`、`docs/plans/engine-core-refactor*.md` | "仓库如何对 AI 友好"README 导览 |
| `README.md` / `README.en.md` quick start | "clone 即跑通"承诺 + 预编译 release |
| `src/Application/Render/gkNextBenchmark/` | benchmark 预编译 release |

---

## 2. 问题清单（按严重度）

### P0 — 发布阻塞

| # | 问题 | 证据 | 处置 |
| --- | --- | --- | --- |
| P0-1 | **仓库无 LICENSE 文件**，`vcpkg.json` 亦无 `license` 字段。以"完全开源"对外宣传前必须补齐 | 根目录无 `LICENSE*` | 决策项 D1，用户选定协议后一次性落地（根目录 LICENSE + vcpkg.json + README 徽章） |
| P0-2 | **深链 shader 的拼写错误**：`RT_ACCUMLATE_*`（应为 ACCUMULATE，48 处含 C++ 侧）、`RT_OBJEDCTID_*`（应为 OBJECTID，32 处）、`Material.Reserverd2/3`（4 处）、`RayCastContext.Hitted`（应为 Hit，19 处）、`RayCastContext.Reversed0/1`（应为 Reserved0/1，[BasicTypes.slang:359](../../assets/shaders/common/BasicTypes.slang:359)） | `grep -rn "ACCUMLATE\|OBJEDCTID\|Reserverd\|Hitted" src assets/shaders` | Phase 1 机械重命名，shader 与 C++ 同步改 |
| P0-3 | **AGENTS.md 架构树漂移**：[AGENTS.md:179](../../AGENTS.md) 写有 `src/Engine/NextGameplay/`（实际为顶层 `src/Gameplay/`）；`src/Modules/`（17 个模块、3.3 万行、占第一方代码 23.5%）在架构树中完全缺失。该文件将作为"agent 环境说明书"被公开导览 | 对照 `ls src/` | Phase 1 重写 AGENTS.md 架构节，并核对 AGENT_GUIDE 各篇的路径引用 |
| P0-4 | **README quick-start 未经三平台彩排**，发布承诺"clone 即跑通" | — | Phase 5 发布彩排（Windows/Linux/macOS 全流程 + 文章中出现的每条命令实测） |

### P1 — 深链路径上的书写规范与明显错误

| # | 问题 | 证据 | 处置 |
| --- | --- | --- | --- |
| P1-1 | GPUScene 统一 push-constant header 用 snake_case：`custom_data_0/1/2`（125 处，横跨 C++ 与全部 shader），与全仓 PascalCase/camelCase 约定冲突 | [BasicTypes.slang:433](../../assets/shaders/common/BasicTypes.slang:433) | 决策项 D2；若改名则 Phase 2 机械替换 |
| P1-2 | `UserInterface.cpp` include 卫生：`FileHelper.hpp` 重复 include（[11 行](../../src/Engine/Runtime/Editor/UserInterface.cpp:11) 与 [41 行](../../src/Engine/Runtime/Editor/UserInterface.cpp:41)）；include 分裂为两段无序块；`extern float GAndroidMagicScale;`、`extern ... GApplication;` 以裸 extern 出现在 .cpp | [UserInterface.cpp:47](../../src/Engine/Runtime/Editor/UserInterface.cpp:47) | Phase 2 |
| P1-3 | `GAndroidMagicScale` 全局变量：定义在 [SwapChain.cpp:16](../../src/Engine/Vulkan/SwapChain.cpp:16)，通过裸 extern 被 Engine/Application 两层引用，且非 Android 路径也在消费；名字自带 "Magic" | `grep -rn GAndroidMagicScale src` | Phase 2 收敛为 SwapChain/Window 的显式 DPI scale 接口 |
| P1-4 | `NodeProxy.nort` 密名字段（语义：排除主可见性/RT 参与），shader 与 C++ 双侧使用 | [BasicTypes.slang:190](../../assets/shaders/common/BasicTypes.slang:190)、[Scene.Update.cpp:352](../../src/Engine/Assets/Core/Scene.Update.cpp:352) | Phase 2 重命名（建议 `excludeFromTrace` 或对齐 RenderParticipation 位语义） |
| P1-5 | `BindlessTexture.slang` RT slot 编号乱序（18/19 后跳 24-30，20-23 插在后面），阅读困难；`kViewRtBankStride` 与 `RT_*`/`SM_*` 三种常量风格混用 | [BindlessTexture.slang:30](../../assets/shaders/common/BindlessTexture.slang:30) | Phase 2 按数值重排声明顺序 + 分组注释（不改数值，行为零风险） |
| P1-6 | 注释残留与死代码：`MAX_NODES = 65535; // 256;`（[BasicTypes.slang:9](../../assets/shaders/common/BasicTypes.slang:9)）；未使用变量 `hasRayTracing`（[VulkanBaseRenderer.cpp:90](../../src/Engine/Rendering/VulkanBaseRenderer.cpp:90)）；注释掉的调试输出（[Device.cpp:59](../../src/Engine/Vulkan/Device.cpp:59)、[FSceneLoader.cpp:1086](../../src/Modules/GltfLoader/FSceneLoader.cpp:1086)） | 见链接 | Phase 1 |
| P1-7 | 重复 include：`RenderingPipeline.cpp`、`SwapChain.cpp`、`TextureImage.cpp`、`CommandHistory.cpp`、`UserInterface.cpp` 等 5 处同文件重复 include 同一头 | `for f in $(find src -name "*.cpp"); do grep "^#include" $f | sort | uniq -d; done` | Phase 1 |
| P1-8 | "Global Vertice Buffer" 等注释拼写（Vertice→Vertex） | [BasicTypes.slang:582](../../assets/shaders/common/BasicTypes.slang:582) | Phase 1 顺手修 |

### P2 — 全仓书写规范一致性

| # | 问题 | 证据 | 处置 |
| --- | --- | --- | --- |
| P2-1 | **Tab 缩进**：Engine + Modules 共 57 个文件含 tab 缩进，违反"4 空格、无 tab"的声明式规范（集中在 `src/Engine/Vulkan/`、`Options.*`、`Engine.*` 等最老的文件） | `grep -rlP "^\t" src/Engine src/Modules --include="*.cpp" --include="*.hpp"` | Phase 4 独立 commit 一次性转换（避免与逻辑改动混在同一 diff） |
| P2-2 | Engine 层 `.h` / `.hpp` 混用（95 个 .hpp vs 32 个 .h，如 `Node.h`、`RenderComponent.h`、`SceneBuilder.h`） | `find src/Engine -name "*.h"` | 决策项 D3；若统一则 Phase 4 机械改名 + include 全量替换 |
| P2-3 | Engine 层 15 个文件含中文注释，Modules 层 0 个；与"引擎核心代码国际可读"的定位不一致 | `grep -rlP "//.*[\x{4e00}-\x{9fff}]" src/Engine` | 决策项 D4；若统一英文则 Phase 4 翻译（保语义，不改代码） |
| P2-4 | `namespace Assets::scad` 小写命名空间违反 PascalCase 约定（ScadLoader 内 18 处） | [FScadEvaluator.cpp:20](../../src/Modules/ScadLoader/FScadEvaluator.cpp:20) | Phase 3 改 `Assets::Scad` |
| P2-5 | `F` 前缀类名（UE 风格，约 40+ 个类：`FSceneLoader`、`FCVarSystem`…）与无前缀类（`Scene`、`Engine`、`VulkanBaseRenderer`）并存，规范文档未定义该前缀 | `grep -rEh "class F[A-Z]" src` | 不做批量改名；Phase 1 在 AGENT_GUIDE/coding-standards.md 补一条成文约定（何时用 F 前缀），消除"无规则感" |
| P2-6 | gnb Go 侧 3 个文件未过 gofmt（`cmd/gnb/init.go`、`internal/llm/paths.go`、`internal/spec/paths.go`）；`go vet` 干净 | `gofmt -l tools/gnb` | Phase 1 `gofmt -w` |
| P2-7 | 本地遗留空目录 `src/Rendering/`（仅 .DS_Store，git 未跟踪），易误导为顶层模块 | `find src/Rendering -type f` | Phase 1 本地删除（不涉及 git） |

### P3 — 结构与工具链

| # | 问题 | 证据 | 处置 |
| --- | --- | --- | --- |
| P3-1 | `FScadEvaluator.cpp` 1972 行单文件（内建函数求值 + 几何生成 + 文本/CSG 混在一个 TU），SCAD 文章将深链此模块 | `wc -l src/Modules/ScadLoader/FScadEvaluator.cpp` | Phase 3 按职责拆 2-3 个 TU（纯移动代码，不改逻辑） |
| P3-2 | StudioSim 与 AirportSim 的 `DecisionScheduler` / `PerceptionSystem` 双拷贝（约 1200 行，已分叉 ~60%） | `diff` 两侧文件 | 决策项 D5；默认**不动**（demo 独立性优先），仅在两份文件头部加一行注释互相声明"姊妹实现" |
| P3-3 | `StreamlineIntegration.cpp` 2074 行（Windows only，不在深链面上） | — | 挂账，不进本计划执行范围 |
| P3-4 | `.clang-tidy` 的 `HeaderFilterRegex` 使用负向前瞻 `(?!...)`，LLVM 正则引擎不支持 lookahead，疑似导致 header 命名检查被静默跳过（`run-naming.sh` 的默认 HEADER_FILTER 同样写法）。注：实测 Engine.cpp TU 用有效过滤器复查为 0 违规，命名现状本身健康 | `.clang-tidy` 第 6 行 | Phase 1 改为排除式正则（如按 `src/(Engine|Modules|Gameplay|Application|Tests)/` 白名单匹配），并全量重跑确认基线 |
| P3-5 | 拼写错误无工具防护（本次 P0-2 的 100+ 处即历史累积） | — | Phase 1 将 `typos`（或 codespell）接入 `gnb`（建议 `gnb loc` 同级命令或 CI step），配 `_typos.toml` 白名单 |
| P3-6 | 编译警告基线未知（无 -Wall 清零记录） | — | Phase 4 全 target 构建收集警告 → 清零或显式豁免 |

---

## 3. 决策项（执行前需用户拍板）

| # | 决策 | 选项 | 建议 |
| --- | --- | --- | --- |
| D1 | 开源协议 | MIT / Apache-2.0 / BSD-3 等 | 需用户选定；落地动作见 P0-1 |
| D2 | `custom_data_0/1/2` 是否改名 | a) 改 `CustomData0/1/2`（125 处机械替换）；b) 保留 + 注释说明是跨管线别名槽位 | 建议 a：该结构会被文章逐字段讲解 |
| D3 | 头文件扩展名 | a) 全部统一 `.hpp`；b) 维持现状、仅约定新文件用 `.hpp` | 建议 a：32 个文件的机械改名，一次做完永绝后患 |
| D4 | Engine 层中文注释 | a) 统一英文；b) 维持双语 | 建议 a：仅 15 个文件，工作量小 |
| D5 | Sim 双拷贝 | a) 抽 SimKit（走既有计划）；b) 保留双拷贝 | 建议 b（本轮）：发布叙事本就强调"每个 demo 独立可抛弃" |

---

## 4. 分期执行计划

> 每个 Phase 独立成 PR/commit 序列，可由不同 agent 认领。**通用验证**：按 AGENTS.md targeted build 规则构建受影响目标 + `gkNextUnitTests` 全绿；凡触碰 shader 或渲染路径，追加 `gnb shot --scene assets/models/playground.glb` 与 baseline 肉眼比对（改动应为像素级等价）。

### Phase 1 — 机械修缮（低风险，先行）

预计规模：1 个工作日。全部为文本级等价变换。

1. **拼写错误全量重命名**（P0-2、P1-8）：
   - `RT_ACCUMLATE_*` → `RT_ACCUMULATE_*`；`RT_OBJEDCTID_*` → `RT_OBJECTID_*`；`Reserverd2/3` → `Reserved2/3`；`Hitted` → `Hit`；`Reversed0/1` → `Reserved0/1`；注释中 `Vertice` → `Vertex`。
   - 操作：对每个词全仓 `grep -rn`（src + assets/shaders + assets/scripts + docs），一次性替换；shader 常量为符号引用，改名后重编译即等价。
   - 验证：`./gnb build gkNextRenderer gkNextUnitTests` + `gnb shot`。
2. **死代码与注释残留**（P1-6）：删 `hasRayTracing` 未用变量、`// 256;` 残留、两处注释掉的调试输出。
3. **重复 include 清理**（P1-7）：5 处。
4. **AGENTS.md 架构节重写**（P0-3）：架构树补 `src/Modules/`（17 模块逐个一行说明，可引用 `src/Modules/README.md`）、`src/Gameplay/`，删除 `NextGameplay`；同步核对 AGENT_GUIDE 各篇路径引用与 `docs/README.md` 索引。
5. **工具链**：`gofmt -w` 三个文件（P2-6）；`.clang-tidy` / `run-naming.sh` 正则修正并全量重跑记录基线（P3-4）;`typos` 接入 gnb 或 CI（P3-5）；本地删除空 `src/Rendering/`（P2-7）。
6. **coding-standards.md 补 F 前缀约定**（P2-5）。

### Phase 2 — 深链文件精修（中风险，逐文件验证）

预计规模：1-2 个工作日。

1. `BindlessTexture.slang`：slot 声明按数值重排 + 分组注释（screen-space bank 区 / 全局区 / swapchain 区），**不改任何数值**（P1-5）。
2. `BasicTypes.slang`：若 D2 通过，`custom_data_*` → `CustomData*`（连带 125 处）；`NodeProxy.nort` 语义化改名（P1-4）；成员命名风格在同一 struct 内统一（如 `SoftMeshShaderVisibleItem` 的 camelCase 与 `UniformBufferObject` 的 PascalCase 并存现状可保留，但同 struct 内不得混用，如 `AmbientCube.skyVisibility_*` 两个字段）。
3. `UserInterface.cpp`：include 合并为单块并按"自身头 → 引擎 → 第三方 → std"排序；裸 extern 收编（P1-2）。
4. `GAndroidMagicScale` 收敛（P1-3）：定义迁至合适持有者（SwapChain 成员 + 访问器，或 Window DPI 接口），三处消费方改走接口；保持行为逐帧等价（Android + 桌面各 `gnb shot` 一次）。
5. 验证：每文件独立 commit；`./gnb build gkNextRenderer gkNextUnitTests gkNextEditor` + `gnb shot`（含 `--ui` 一张验证 ImGui 路径）+ Android 目标编译通过（涉及 GAndroidMagicScale / BindedTLAS 的改动）。

### Phase 3 — 结构小改（中风险）

预计规模：1 个工作日。

1. `FScadEvaluator.cpp` 拆分（P3-1）：建议 `FScadEvaluator.cpp`（表达式/控制流求值）+ `FScadEvaluator.Builtins.cpp`（内建模块/函数表）+ 既有 `FScadCsg.cpp` 边界复核；纯代码移动。
2. `namespace Assets::scad` → `Assets::Scad`（18 处，P2-4）。
3. DecisionScheduler 姊妹注释（P3-2，若 D5 选 b）。
4. 验证：`./gnb build ScadStudio gkNextUnitTests` + `gnb shot --target ScadStudio --scene assets/scad/beer_cup.scad`；ScadLoader 相关单测全绿。

### Phase 4 — 全仓一致性（大面积、零逻辑改动，放在功能冻结点执行）

预计规模：1 个工作日。**必须与任何逻辑改动隔离成独立 commit，避免污染 blame 与 review。**

1. Tab → 4 空格：57 个文件一次性转换（P2-1），转换脚本先在 3 个文件试跑肉眼 diff 确认无字符串内误伤。
2. `.h` → `.hpp` 统一（P2-2，若 D3 通过）：32 个文件 `git mv` + include 全量替换 + CMake glob 确认（需 `--reconfigure`）。
3. Engine 中文注释英译（P2-3，若 D4 通过）：15 个文件。
4. 编译警告基线（P3-6）：`./gnb build --reconfigure` 全 target，收集并清零警告（无法清零的在计划附录登记豁免原因）。
5. 验证：全量构建 + 单测 + `gkNextVisualTest` 全场景与 baseline 对比（本 Phase 改动理论零像素差）。

### Phase 5 — 发布彩排（发布周前完成）

1. LICENSE 落地（D1 拍板后）：根 LICENSE + `vcpkg.json` license 字段 + 双语 README 徽章/声明（P0-1）。
2. README quick-start 三平台全流程彩排：干净 clone → `gnb setup` → `gnb build` → `gnb run`，记录首次构建时长与坑（P0-4）。
3. 对外文章中将出现的每条命令逐条实测：`gnb shot`、`gnb validate`、benchmark 运行命令、`gnb llm setup` 链路。
4. 预编译 release 产物冒烟：gkNextBenchmark / ScadStudio / AirportSim 在无开发环境的机器语义下能启动并出图。
5. `gnb loc` 刷新、`docs/README.md` 索引状态更新。

---

## 5. 验收标准

- [ ] 仓库根目录运行 `typos` 零输出（`_typos.toml` 已配置第一方范围;本文档保留错误原文作历史记录,已列入排除）。
- [ ] 第 1 节"发布可见面"清单内文件逐一人工通读签收（书写规范、命名一致、无死代码）。
- [ ] AGENTS.md 架构树与 `ls src/` 实际一致；AGENT_GUIDE 无失效路径。
- [ ] `grep -rlP "^\t" src/Engine src/Modules --include="*.cpp" --include="*.hpp"` 零命中。
- [ ] clang-tidy naming（修正后的过滤器）全量零违规;`gofmt -l tools/gnb` 空输出。
- [ ] 全 target `--reconfigure` 构建零警告（或豁免清单成文）。
- [ ] `gkNextUnitTests` 全绿；`gkNextVisualTest` 与 baseline 零差异。
- [ ] LICENSE 存在且三处（根/vcpkg/README）一致。
- [ ] 三平台 quick-start 彩排通过并留档。

## 6. 风险与回滚

- 全部任务设计为行为等价变换，单 Phase 单独可回滚；Phase 4 的大面积格式化 commit 若引发合流冲突，以"重放脚本"而非 cherry-pick 方式在新基线重做。
- shader 符号重命名遗漏会导致编译期失败（安全失败），不存在静默行为变化；唯一需肉眼验证的是 `GAndroidMagicScale` 收敛（触碰 DPI 逻辑）。
- 执行期间主线仍在开发：Phase 1-3 尽早合入；Phase 4 选择功能冻结点执行，避免长寿分支。
