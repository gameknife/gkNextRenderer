# Engine + Application 大型结构重构计划

> **作者：** Claude (Opus 4.7) · 2026-05-10
> **范围：** `src/` 全部一手代码（不含 `ThirdParty/` 与 `external/`）；约 414 文件 / 94k LoC
> **目标：** 把模块边界、文件夹层次、文件命名、命名空间、include 风格统一到一个一致、可读、低耦合的目标层次
> **执行者：** Codex (按本目录下的 `phase-XX-*.md` 顺序执行)
> **执行约定：** 每个 phase 一个 PR；阶段与阶段之间**严格串行**；每个 PR 必须满足"验收门"才能进入下一阶段

---

## 0. 阅读顺序

1. 本 README（背景、原则、阶段索引、验收门）
2. `phase-00-conventions.md` —— 本次重构的"宪法"，所有阶段都引用它
3. `phase-01-*.md` ~ `phase-12-*.md` —— 按编号顺序执行，**不允许跳号**

---

## 1. 重构动机 ── 当前结构的核心问题

下面 6 条问题是把 414 个文件读完后归纳出的 root cause，每个 phase 都对应到其中 1~3 条。

| # | 问题 | 证据 | 影响 |
| --- | --- | --- | --- |
| **R1** | 头扩展名混用 | `.h` 57 个 / `.hpp` 153 个；同模块同类型混用（`Components/` 全 `.h`、`Editor/` 全 `.hpp`、`Reflection/` 混用） | 心智成本；grep / IDE 跳转混乱 |
| **R2** | 命名空间方言不一致 | `Vulkan::` / `Assets::` / `Runtime::Command::` / `NextRenderer::` / `NextCVar::` / `NextAI::` / `NextUI::Painter` 等 7+ 套并存；部分 `.h` 文件无命名空间 | 全局符号污染；移动文件时极易冲突 |
| **R3** | "And"/"Junk" 命名 + god-file | `Vulkan/MemoryAndShader.cpp` `SyncAndTiming.hpp` `GpuResources.hpp`；`Engine.cpp 1692L` `VulkanBaseRenderer.cpp 1943L` `QuickJSEngine.cpp 2650L` `SceneList.cpp 1765L` | 文件即上帝类；新人无从下手 |
| **R4** | 模块边界模糊 + 双份目录 | `src/Editor/` 与 `src/Runtime/Editor/` 并存；`src/Utilities/` 与 `src/Runtime/Utilities/` 并存；`Editor/AI/` 与 `NextGameplay/AI/` 同名不同义；`Common/` 仅含 1 个文件 | 不知道新代码该放哪 |
| **R5** | 跨层强耦合 | Runtime 直接 `#include "Vulkan/*.hpp"` 12+ 处；`Camera/ModelViewController.cpp` `Subsystems/NextAudio.cpp` 都包含了 `Vulkan/DebugUtilities.hpp`；`Editor/UserInterface.cpp` 包含 7 个 Vulkan 头 | 没法替换后端、没法离屏跑 Editor |
| **R6** | Application 命名/分类不一致 | `Brotato3D/`/`KongLie3D/` 无前缀，`gkNextRenderer/`/`gkNextBenchmark/` 带 `gkNext` 前缀；游戏 / 工具 / 核心 11 个项目平铺一层；`MagicaLego/` 既是游戏又被测试 import | 子项目角色不清；`Application/` 像杂物间 |

---

## 2. 顶层决策（与人类对齐结果）

> 这些决策由人类在 2026-05-10 拍板，所有 phase 必须遵循。Codex 在执行中**不得偏离**；如遇必须偏离，需停下来在 PR 描述里写明并请求确认。

| # | 决策 | 选项 |
| --- | --- | --- |
| **D1** | 兼容策略 | **全破坏式**：直接改路径/命名/命名空间，不留 deprecated 别名、不留转发头 |
| **D2** | NextGameplay 边界 | **保留独立模块**，重命名 `Components`/`Reflection`/`Utilities` 子文件夹消歧 |
| **D3** | Editor 分层 | `Runtime/Editor/` → `Runtime/UI/`；`src/Editor/` → `Application/Core/Editor/` |
| **D4** | App 命名 | **按子文件夹分类**（`Games/`、`Tools/`、`Core/`），文件夹名**不强制 gkNext 前缀**；CMake target 名**保留 gkNext 前缀** |
| **D5** | 头文件扩展名 | **全部统一为 `.h`**（57 + 153 = 210 个文件参与重命名） |
| **D6** | CMake 组织 | **保持集中**（`src/cmake/SourceFiles.cmake` 单文件），仅重排列表分区与注释 |
| **D7** | Vulkan 隔离 | **混合策略**：仅对 Editor / Runtime/Subsystems 强制走门面（新增 `Runtime/RHI/`），Rendering 仍可裸套 Vulkan |
| **D8** | god-file 拆分 | **核心引擎侧**（Engine.cpp / VulkanBaseRenderer.cpp / SceneList.cpp / QuickJSEngine.cpp）拆分；**应用侧 UI 不动** |
| **D9** | 文档形式 | 主 README + 阶段子计划，便于 codex 分阶段拉取 |
| **D10** | 验收门 | **每阶段：编译通过 + 全量单元测试通过** |
| **D11** | App 分类 | `Application/Games/{...}` + `Application/Tools/{Benchmark,VisualTest,Packager}` + `Application/Core/{Renderer,Editor}` |
| **D12** | Subsystems 拆分 | 按领域拆子目录：`Audio/` `Physics/` `Animation/` `AI/` `Scripting/` `Localization/` `Voice/` `Tasks/` `Character/` |

---

## 3. 目标结构（重构完成后的"北极星"）

```
src/
├─ Common/
│  └─ CoreMinimal.h            # 唯一全工程必含头（保留）
│
├─ Utilities/                  # 通用工具（无引擎依赖）
│  ├─ Exception.{cpp,h}
│  ├─ FileHelper.{cpp,h}
│  ├─ Glm.h                    # 即原 Glm.hpp
│  ├─ ImGui.h                  # 即原 ImGui.hpp
│  ├─ Localization.{cpp,h}
│  ├─ Math.h
│  ├─ StbImage.{cpp,h}
│  ├─ JsonHelpers.{cpp,h}      # 从 Runtime/Utilities 迁入
│  └─ NextEngineHelper.{cpp,h} # 从 Runtime/Utilities 迁入（仅依赖 ImGui）
│
├─ Vulkan/                     # 直接 Vulkan 后端
│  ├─ Core/                    # 设备、实例、交换链、表面
│  │  ├─ Device.{cpp,h}
│  │  ├─ Instance.{cpp,h}
│  │  ├─ SwapChain.{cpp,h}
│  │  └─ WindowSurface.{cpp,h}
│  ├─ Memory/                  # 缓冲、镜像、采样器、设备内存
│  │  ├─ DeviceMemory.{cpp,h}  # 从 MemoryAndShader 拆出
│  │  ├─ Buffer.{cpp,h}        # 从 GpuResources 拆出
│  │  ├─ BufferUtil.h
│  │  ├─ Image.{cpp,h}         # 从 GpuResources 拆出
│  │  ├─ ImageView.{cpp,h}
│  │  └─ Sampler.{cpp,h}
│  ├─ Pipeline/                # 管线、描述符、着色器、命令
│  │  ├─ RenderingPipeline.{cpp,h}
│  │  ├─ DescriptorSystem.{cpp,h}
│  │  ├─ ShaderModule.{cpp,h}  # 从 MemoryAndShader 拆出
│  │  ├─ ShaderHotReloader.{cpp,h}
│  │  └─ CommandExecution.{cpp,h}
│  ├─ Sync/                    # 栅栏、信号量、计时
│  │  ├─ Synchronization.{cpp,h}  # 从 SyncAndTiming 拆出
│  │  └─ GpuTimer.{cpp,h}         # 从 SyncAndTiming 拆出
│  ├─ Debug/
│  │  └─ Validation.{cpp,h}    # 即原 DebugUtilities
│  └─ RayTracing/              # （保持现状内部结构）
│
├─ Rendering/
│  ├─ Base/
│  │  ├─ VulkanBaseRenderer.{cpp,h}     # 拆为 4~5 个 .cpp
│  │  └─ RayTraceBaseRenderer.{cpp,h}
│  ├─ Common/                  # 即原 PipelineCommon
│  └─ Pipelines/
│     ├─ PathTracing/
│     ├─ SoftwareModern/
│     └─ SoftwareTracing/
│
├─ Assets/                     # 现有结构基本保留
│  ├─ Core/                    # Scene/Node/Model/Component
│  ├─ Data/                    # Animation/Material/Skeleton/Vertex
│  ├─ GPU/                     # Texture/UniformBuffer
│  ├─ Loaders/                 # 按格式分组（gltf/ldraw/kaykit/...）
│  ├─ Savers/
│  └─ Acceleration/
│
├─ Runtime/                    # 引擎运行时（新增 RHI 与 UI；Editor → UI；Utilities 拆走）
│  ├─ Engine.{cpp,h}           # 主体；按职能拆 5~6 个伴生 cpp（见 phase-08）
│  ├─ Engine_Lifecycle.cpp     # Start / End / Reflection 注册
│  ├─ Engine_Input.cpp         # OnKey / OnMouse / OnTouch / OnScroll / Gamepad
│  ├─ Engine_Renderer.cpp      # OnRendererXxx 回调
│  ├─ Engine_SceneLoad.cpp     # RequestLoadScene / LaunchLoadSceneTask
│  ├─ Engine_Window.cpp        # 窗口控制 / 截图
│  ├─ ScreenShot.{cpp,h}
│  │
│  ├─ RHI/                     # 新增：仅 Editor / Subsystems 走的 Vulkan 门面
│  │  ├─ RenderContext.{cpp,h} # 拥有 Device/Queue/Swapchain 引用，对外暴露最小集
│  │  └─ ImGuiBackend.{cpp,h}  # 把 ImGui_ImplVulkan_* 包进去
│  │
│  ├─ Camera/
│  ├─ Command/
│  ├─ Components/              # 引擎级 ECS 组件（Render/Physics/SkinnedMesh）
│  ├─ Config/                  # CVar / EngineCVars / ShowFlags / UserSettings
│  ├─ Platform/                # 平台抽象
│  ├─ Reflection/              # 反射框架
│  ├─ Scene/
│  │  ├─ SceneList.{cpp,h}     # 入口 + Discovery（从原 1765L 拆出）
│  │  ├─ SceneBuilders/        # 各场景 builder
│  │  └─ NodeUtils.{cpp,h}
│  ├─ Subsystems/              # 按领域分子目录
│  │  ├─ Audio/                # 原 NextAudio.* → Audio.{cpp,h}
│  │  ├─ Physics/              # 原 NextPhysics.* + NextPhysicsTypes.h
│  │  ├─ Animation/            # 原 NextAnimation.*
│  │  ├─ AI/                   # 原 AIService.*
│  │  ├─ Scripting/            # QuickJSEngine 拆为多文件
│  │  ├─ Localization/         # 原 NextLocalization.*
│  │  ├─ Voice/                # 原 VoiceInputService.*
│  │  ├─ Tasks/                # 原 TaskCoordinator.*
│  │  └─ Character/            # 原 NextCharacterController.*
│  └─ UI/                      # 原 Runtime/Editor + 原 Runtime/Utilities 中的 debug overlay
│     ├─ UserInterface.{cpp,h}
│     ├─ ConsoleLogBuffer.{cpp,h}
│     ├─ FontLoader.{cpp,h}
│     ├─ GizmoController.{cpp,h}
│     ├─ ImGuiPainter.{cpp,h}
│     ├─ ImGuiScaling.{cpp,h}
│     ├─ NotificationCenter.{cpp,h}
│     └─ Overlays/             # 原 Runtime/Utilities/{Graphics,Physics,Profile}DebugOverlay
│
├─ NextGameplay/               # 仍是独立模块；子文件夹消歧
│  ├─ AI/                      # NavGrid / PathFollower（游戏级寻路）
│  ├─ Character/               # CharacterActor
│  ├─ Gameplay/                # GameplayMath / GameplayTypes
│  ├─ GameComponents/          # ← 原 Components（消歧）
│  ├─ GameplayReflection/      # ← 原 Reflection（消歧）
│  └─ Helpers/                 # ← 原 Utilities（消歧）
│
├─ Tests/                      # 不变
│
└─ Application/                # 三分类
   ├─ Core/
   │  ├─ Renderer/             # 原 gkNextRenderer/
   │  └─ Editor/               # ← src/Editor/ 整体迁入
   ├─ Tools/
   │  ├─ Benchmark/
   │  │  ├─ Common/
   │  │  ├─ Still/             # 原 gkNextStillBenchmark/
   │  │  └─ Motion/            # 原 gkNextMotionBenchmark/
   │  ├─ VisualTest/           # 原 gkNextVisualTest/
   │  └─ Packager/
   └─ Games/
      ├─ Brotato3D/
      ├─ BrickPlayer/
      ├─ CharacterDemo/
      ├─ Flappy/
      ├─ KongLie3D/
      ├─ MagicaLego/
      └─ Voyage3D/
```

> CMake target 名（`gkNextEngine` / `gkNextRenderer` / `gkNextEditor` / `gkNextBenchmark*` / `gkNextVisualTest` / `gkNextUnitTests`）**保持不变**，避免破坏外部脚本和 CI；仅源文件夹改名。

---

## 4. 阶段索引

阶段顺序经过依赖分析，**不可乱序**：每个 phase 都假设前序 phase 已落地。

| Phase | 文件 | 标题 | 大致 diff 规模 | 关键风险 |
| --- | --- | --- | --- | --- |
| **00** | [phase-00-conventions.md](phase-00-conventions.md) | 制定重构宪法（命名、include、命名空间） | 1 文件新增；0 代码改动 | 无 |
| **01** | [phase-01-header-extension.md](phase-01-header-extension.md) | `.hpp` → `.h` 全量改名 | ~210 文件改名 + ~1500 include 改写 | 漏改 include；shader 文件名误伤 |
| **02** | [phase-02-vulkan-internal.md](phase-02-vulkan-internal.md) | Vulkan 子目录化 + 拆 god-helpers | ~25 文件移动；3 处 god-file 拆分 | UNITY_GROUP 路径失效 |
| **03** | [phase-03-runtime-subsystems.md](phase-03-runtime-subsystems.md) | Subsystems 按领域拆子目录 + 去 Next 前缀 | ~14 文件移动 + 改名 | iOS OBJCXX 设置漏改 |
| **04** | [phase-04-runtime-ui-utilities.md](phase-04-runtime-ui-utilities.md) | `Runtime/Editor` → `Runtime/UI`；Utilities 上移/拆分 | ~20 文件移动 | Engine.cpp 顶部 include 大改 |
| **05** | [phase-05-nextgameplay.md](phase-05-nextgameplay.md) | NextGameplay 子文件夹消歧 | ~16 文件移动 | 反射注册路径 |
| **06** | [phase-06-rendering.md](phase-06-rendering.md) | Rendering Base/ + Pipelines/；VulkanBaseRenderer 拆 4 个 cpp | ~10 文件移动 + 拆 1943L 文件 | UNITY 分组、Streamline include |
| **07** | [phase-07-application.md](phase-07-application.md) | Application Games/Tools/Core 三分；`src/Editor` → `Application/Core/Editor` | ~120 文件移动 | CMake target 与 SourceFiles 路径联动 |
| **08** | [phase-08-engine-cpp-split.md](phase-08-engine-cpp-split.md) | `Engine.cpp` 1692L → 6 个伴生 cpp | 拆 1 文件为 6；header 不变 | UNITY；同名静态变量泄漏 |
| **09** | [phase-09-scenelist-quickjs-split.md](phase-09-scenelist-quickjs-split.md) | `SceneList.cpp` / `QuickJSEngine.cpp` 拆分 | 拆 2 文件 → ~10 个 | JS 模块加载逻辑容易回归 |
| **10** | [phase-10-vulkan-facade.md](phase-10-vulkan-facade.md) | `Runtime/RHI/` 门面；Editor/Subsystems 不再直接 include `Vulkan/*` | 新增 2~3 文件；改 ~30 处 include | Editor ImGui Vulkan 后端 |
| **11** | [phase-11-namespaces-and-coreminimal.md](phase-11-namespaces-and-coreminimal.md) | 命名空间收敛到模块级；强制 CoreMinimal 首 include | 跨全工程 ~400 文件首行检查 | 隐式名字解析破坏 |
| **12** | [phase-12-final-audit.md](phase-12-final-audit.md) | 死代码清扫 + 文档同步 + 一次完整 visual test | 删除 + 文档更新 | 无 |

---

## 5. 验收门（每个 phase 必须满足）

每个 phase 完成时 codex 必须连贯执行以下命令并全部通过，才能进入下一 phase：

```bash
# 1) 重新配置 + 编译
./gnb build --reconfigure

# 2) 全量单元测试
./out/build/macos-arm64/bin/gkNextUnitTests
# 或对应平台路径

# 3) 检查工作区是否还有未提交改动
git status --porcelain
```

**额外要求：**

1. **PR 标题格式：** `refactor(phase-XX): <title>`，例如 `refactor(phase-02): Vulkan 子目录化与 helpers 拆分`
2. **PR 描述必含：**
   - 文件移动 / 改名清单（`git diff --stat -M`）
   - 改动后净 LoC 变化
   - 验收门 3 个命令的输出截图或日志末尾几行
   - 自我审查清单（在每个 phase 文档末尾，逐条勾掉）
3. **任何 phase 不得绕过验收门进入下一阶段**。如编译失败但 codex 觉得无关，必须先回报人类。
4. **不得在重构 PR 中夹带功能改动**。任何"顺手修个 bug"都应该独立成 PR 在重构序列之外。

---

## 6. 自我审查机制

每个 phase 文档末尾都有一个 `## 自我审查清单`，至少包含以下 5 条（具体 phase 会扩展）：

- [ ] 所有目标文件已按计划移动/改名
- [ ] `grep -r "<old_path>" src/ --include='*.h' --include='*.hpp' --include='*.cpp'` 返回 0 行
- [ ] `./gnb build --reconfigure` 成功
- [ ] `gkNextUnitTests` 100% 通过
- [ ] `git status` 干净（无遗留未提交临时文件、无 `.bak`/`.orig`）

Codex 完成阶段时**必须**在 PR 描述里逐条勾选。任何一条没勾，回退或继续修复，不允许提交。

---

## 7. 跨阶段不变量

下面是从 phase 0 到 phase 12 始终保持的不变量。Codex 在任何阶段意外破坏其中之一，立即停下。

1. **CMake target 名不变**：`gkNextEngine` / `gkNextRenderer` / `gkNextEditor` / `gkNextStillBenchmark` / `gkNextMotionBenchmark` / `gkNextVisualTest` / `gkNextUnitTests` / `MagicaLego` / `Brotato3D` / `KongLie3D` / `BrickPlayer` / `CharacterDemo` / `FlappyCpp` / `FlappyJs` / `Packager` / `NextGameplay` / `Assets` —— 17 个 target 一个不动。
2. **assets/ 目录结构不动**：着色器 / 配置 / glTF 路径都保持原样。脚本字面量里的 `assets/...` 路径不需要改。
3. **TypeScript 绑定 API 不动**：`Engine.d.ts` 暴露的 `Global.GetEngine()` / `Scene.*` / `SceneBuild.*` 等接口签名不变。反射注册的组件名（`CharacterAnimationComponent` 等）不变。
4. **公共 C++ API**：`NextEngine` 类的 public 方法名签名不动（除 phase-04 删 Localization 的旧 façade 等明确写出的项以外）。
5. **配置文件**：`assets/configs/visual_test.json` `ai_config.json` 不动。
6. **gnb CLI**：`./gnb build` `./gnb run <target>` 行为不变。

---

## 8. 关于 Brotato3D / KongLie3D / MagicaLego 等 god-UI 文件

审计发现 `Brotato3DUI.cpp 2039L` `MagicaLegoUserInterface.cpp 1972L` `KongLie3DUI.cpp 1971L` 都是 god-file，但**本次重构按 D8 不动应用侧**。原因：

- 应用侧 UI 是业务代码，重构需要熟悉游戏逻辑，由游戏作者后续单独发起
- 本次目标是把"骨架"摆正，不是把所有肉都重做一遍
- 这些文件会在 phase-07 跟随文件夹移动一并被搬到 `Application/Games/`，但**内容不动**

如果将来要拆，可以在 `docs/plans/<future>/games-ui-split/` 里另开一个迷你重构计划。

---

## 9. 估算与节奏建议

| 维度 | 估算 |
| --- | --- |
| 阶段总数 | 13（phase-00 ~ phase-12） |
| 累计 PR 数 | 13（每阶段 1 PR；不允许拆得更细，否则验收门管理太散） |
| 累计预计 diff | ~600 文件改动 / ~3000 include 改写 / ~1.5w LoC 净移动（不增不减大头） |
| 单阶段 Codex 实测时间 | 视 phase 大小 30 分钟 ~ 4 小时 |
| 全程顺利情况 | 1~2 周连贯滚完 |
| 风险中断点 | phase-01（最大批量改名）、phase-07（最多文件移动）、phase-10（语义改造） |

---

## 10. 出错回退策略

**不写 shim** 意味着每个阶段一旦合入主干，前一阶段的所有路径都不复存在。所以出错回退必须：

1. **首选：在阶段内修正后重新提交，不回退主干。** 单元测试是网，编译失败也是网，一般能在 PR review 阶段拦住。
2. **若已合并后才发现严重问题：** `git revert` 整个 phase 的合并 commit。所有 phase commit message 都打 `refactor(phase-XX):` 前缀，方便定位。
3. **若多个 phase 已叠加难以单独回退：** 这就是为什么 phase 间要严格串行 + 等单元测试通过。如果走到这一步，说明流程没遵守。

---

## 11. 与现有计划的关系

- `docs/plans/2026-05/engine-cleanup-and-unification.md` —— 引擎层 API 净化（删 façade、收敛重载）。**与本计划正交**，可以在本计划之前或之后跑，但建议**先跑本计划再跑 cleanup**：本计划把骨架理顺后，cleanup 的 grep 路径才稳定。
- `docs/plans/2026-05/build-system-rework.md` —— 已存在的构建系统计划。本计划的 D6 决定**不改 CMake 组织**，因此与之兼容；若 build-system-rework 后续要把 CMake 拆模块，可以在 phase-12 之后启动。
- `docs/plans/2026-05/engine-uplift-from-brotato-konglie.md` —— 从游戏中提炼引擎能力。**先于本计划完成**，本计划假设其结果已落地。

---

## 12. 启动检查（Codex 第一次拉取本目录时）

在执行 phase-00 之前，codex 必须确认：

- [ ] 当前分支是 `dev` 或为重构开的专用分支
- [ ] `git status --porcelain` 干净
- [ ] `./gnb build --reconfigure` 在重构前能跑通（基线必须绿）
- [ ] `./out/build/<preset>/bin/gkNextUnitTests` 在重构前 100% 通过（基线）
- [ ] 已读完本 README 与 phase-00

任何一条不满足，停下来报告人类。
