---
title: "src/Engine 核心层精炼 Round 3：模块归位 + SkinnedMesh 瘦身"
category: plan
status: 草案
owner: engine
created: 2026-06-14
last_updated: 2026-06-14
---

# src/Engine 核心层精炼 Round 3：模块归位 + SkinnedMesh 瘦身

> 状态：分析/待讨论 | 编写日期：2026-06-14 | 前置：[engine-core-refactor.md](engine-core-refactor.md)（Round 1，已完成）、[engine-core-refactor-round2.md](engine-core-refactor-round2.md)（Round 2，god class 拆解，草案）
>
> 本轮主题：**把"不属于核心层"的代码挪出去 + 砍掉效果不佳的重资产**。原则仍是 KISS：核心层只留"所有 program 都要用"的东西；游戏/编辑器专属的逻辑下沉到对应库；维护成本高、当前效果有问题的功能（SkinnedMesh IK）直接删，而不是继续修。
>
> 三项改动彼此独立，可分开提交、分开 review。三项全做后预计核心层净减 **~1.8k LOC**（IK + 多视口后端占大头）。
>
> **决策状态：四个待确认项均已拍板（见 §8），本文档已据此从"方案对比"收敛为"既定方案"。**

---

## 1. 现状快照（2026-06-14）

- 统计口径：`src/Engine/**/*.{cpp,hpp,h,c}` 非空行（含注释），ripgrep。
- 核心层非空行：**32,182**（Round 2 草案口径为 35,476；近期已下降，距 30k 目标约剩 2.2k）。
- 本轮三个目标文件/系统：

| 系统 | 当前位置（库） | 文件 | 原始行数 |
|---|---|---|---:|
| 角色控制器 | `gkNextEngine`（核心层） | `Runtime/Subsystems/NextCharacterController.{h,cpp}` | 65 + 212 = 277 |
| 多视口 UI 后端 | `gkNextEngine`（核心层） | `Runtime/Editor/UserInterface.ViewportBackend.cpp` | 865 |
| 蒙皮网格组件 | `gkNextEngine`（核心层） | `Runtime/Components/SkinnedMeshComponent.{h,cpp,IK.cpp,Internal.hpp}` | 149 + 496 + 594 + 53 = 1,292 |

三项改动定位不同：**改动一是纯搬家（零行为变化）**，**改动二是删功能（有行为变化）**，**改动三是抽接缝（需动 UI 类的对外面，最重）**。建议按 1 → 2 → 3 的顺序推进。

### 关键基建事实（决定改动难易）

- `src/cmake/SourceFiles.cmake` 用 **GLOB_RECURSE** 按目录划库：`src_files_engine` 收 `Engine/Runtime/*`（→ `gkNextEngine`），`src_files_nextgameplay` 收 `Gameplay/*`（→ `NextGameplay`）。**因此"换库"= 把文件挪到另一个目录**，绝大多数情况无需手改 CMake 文件清单（`src/cmake/SourceFiles.cmake:49`、`:60`）。
- `NextGameplay` 已 `target_link_libraries(... PRIVATE gkNextEngine)`（`src/CMakeLists.txt:487`）。
- per-target 配置循环对**所有** target（含 `NextGameplay`）统一打开 `WITH_PHYSIC=1` + 链接 `Jolt::Jolt`、`WITH_OZZ=1` + 链接 `ozz`（`src/CMakeLists.txt:537-553`）。这意味着把依赖 Jolt/ozz 的代码搬进 `NextGameplay` 不会缺定义、不会缺头文件。

---

## 2. 改动总览

| # | 改动 | 目标 | 行为变化 | 风险 | 核心层 LOC 变化 |
|---|---|---|:--:|:--:|---:|
| 1 | `NextCharacterController` → `NextGameplay` | 角色控制器从核心层下沉到游戏层 | 无 | 低 | −277 |
| 2 | `SkinnedMeshComponent` 砍 IK | 删除脚步 IK / 两骨 IK，核心只留"采样+混合+蒙皮" | 有（脚步 IK 消失） | 中 | −700 ~ −760 |
| 3 | `UserInterface.ViewportBackend` 移出核心 | 多视口平台后端真抽离到**编辑器 app 公共层**（provider 注入，方案 B） | 无（仅编辑器类 target 受影响） | 高 | −865（+ 核心接口 ~40） |

> 注：三项 LOC 均为既定方案口径。改动 3 已定为"真抽离"（方案 B），落点为新建的编辑器 app 公共目录 `src/Application/Editor/Common/`（见 §5）。

---

## 3. 改动一：NextCharacterController → NextGameplay（低风险，先做）

### 3.1 为什么可以搬

`NextCharacterController` 是"角色"概念，本质属于 gameplay，不是渲染/资产/Vulkan 这类所有 program 都依赖的基础设施。当前它待在 `Engine/Runtime/Subsystems/` 纯属历史位置。验证依赖方向：

- **核心层不反向依赖它**：全核心层只有它自己 include 自己的头（`grep "NextCharacterController.h" src/Engine` 仅命中 `NextCharacterController.cpp:1`）。`NextPhysics.cpp` 里出现的 "NextCharacterController" 只是注释（`NextPhysics.cpp:422`、`:434`），不是真引用。**因此不存在"引擎依赖角色控制器"的环**，可以安全下沉。
- **它对引擎的依赖是单向、且已具备**：
  - 头文件只 include `Engine/Runtime/RuntimeFwd.hpp` + `Engine/Runtime/Subsystems/NextPhysicsTypes.h`（都是引擎公共头），`NextCharacterController.h:7-8`。
  - `.cpp` 通过 `extern JPH::PhysicsSystem* GetJoltPhysicsSystem(NextPhysics*)` 拿 Jolt 内部句柄（`NextCharacterController.cpp:19-20`），该自由函数由引擎侧 `NextPhysics.cpp:448` 导出；`NextGameplay` 链接 `gkNextEngine`，符号在链接期可解析。
  - `.cpp` 需要 Jolt 头与 `WITH_PHYSIC` —— `NextGameplay` 已具备（§1 基建事实）。
- **现有消费者本就在游戏层/应用层**：`Gameplay/Character/CharacterActor.h:10`（已在 NextGameplay 内）、`Application/Game/CharacterDemo/CharacterDemoGameInstance.hpp`、`Modules/DevTools/PhysicsDebugOverlay.{hpp,cpp}`。前两者搬家后改 include 路径即可；DevTools 这处按 §3.3（方案 i）把调试绘制函数一并迁出，不是简单改路径。

### 3.2 执行步骤

1. `git mv src/Engine/Runtime/Subsystems/NextCharacterController.h  src/Gameplay/Character/NextCharacterController.h`
2. `git mv src/Engine/Runtime/Subsystems/NextCharacterController.cpp src/Gameplay/Character/NextCharacterController.cpp`
3. 全局替换 include 路径：`Engine/Runtime/Subsystems/NextCharacterController.h` → `Gameplay/Character/NextCharacterController.h`。涉及：
   - `src/Gameplay/Character/CharacterActor.h:10`
   - `src/Application/Game/CharacterDemo/CharacterDemoGameInstance.hpp`
   - 移动后的 `NextCharacterController.cpp` 自身第 1 行
   - `src/Modules/DevTools/PhysicsDebugOverlay.{hpp,cpp}` 不在此处改路径——其角色调试绘制按 §3.3 迁往 NextGameplay 后，DevTools 不再 include 该头。
4. 头内部 include（`RuntimeFwd.hpp` / `NextPhysicsTypes.h`）保持 `Engine/...` 绝对前缀不变，无需改。
5. 增量构建验证：`./gnb build NextGameplay CharacterDemo gkNextRenderer gkNextUnitTests`。
   - `gkNextRenderer` 不该再引用该头（确认核心层干净）；`CharacterDemo` 验证消费者；`NextGameplay` 验证新归属编译通过。
6. 若新增目录 `src/Gameplay/Character/` 已存在（CharacterActor 就在那），GLOB 自动收录，**无需 `--reconfigure`**；若 CMake 没识别到新文件再加 `--reconfigure`。

### 3.3 唯一的连带：DevTools 的角色控制器调试绘制

`Modules/DevTools` 真的用到了它——已确认 `PhysicsDebugOverlay.cpp:297` 的 `DrawCharacterControllerDebugOverlay(const NextCharacterController&, ...)` 会读取控制器（调用其取值方法），`PhysicsDebugOverlay.cpp:15` include 了该头。而 DevTools 当前链接 `gkNextEngine` 但**不**链接 `NextGameplay`，且 DevTools 被**每个 program** 链接（`src/CMakeLists.txt:498`）。

**已定（方案 i）：把这支"角色控制器专属"的调试绘制从 DevTools 挪进 NextGameplay。** 它本就是 gameplay 类型的可视化，归位后 DevTools 不再 include 角色头，依赖图保持 `DevTools → gkNextEngine` 干净，只有真正用到角色的 program 才拉 NextGameplay。具体操作：

1. 把 `PhysicsDebugOverlay.cpp` 中的 `DrawCharacterControllerDebugOverlay` 函数（及其在 `PhysicsDebugOverlay.hpp:11` 的声明、`.hpp:6` 的前置声明）整体迁到 NextGameplay，例如新增 `src/Gameplay/Character/CharacterControllerDebugDraw.{hpp,cpp}`（GLOB 自动收进 NextGameplay）。
2. DevTools 删去该函数与对 `NextCharacterController.h` 的 include（`PhysicsDebugOverlay.cpp:15`）。
3. 原先在 DevTools 调用该 overlay 的地方（若有），改为调用 NextGameplay 侧的新入口；这些调用点只会出现在链接了 NextGameplay 的角色类 program 中。

> 备选（不采用）：给 DevTools 补 `link NextGameplay`——因 DevTools 被所有 program 链接，会让 NextGameplay 成为隐式全局构建依赖，与 KISS 相悖，故弃用。

回滚：`git revert` 单个提交即可，无数据/格式迁移。

---

## 4. 改动二：SkinnedMeshComponent 砍 IK（中风险，收益最大）

### 4.1 现状：核心动画 vs IK 的边界很清楚

`SkinnedMeshComponent` 干两件事，耦合度低、切得干净：

- **核心动画（保留）**：ozz 采样 `SampleOzz` + 混合 `FinalizePose` + 软件层级蒙皮 `UpdateJoints` + 播放状态机 `AdvanceAnimationState`/`PlayAnimation`/`StopAnimation`。这是所有蒙皮角色都要的基础能力，`SkinnedMeshComponent.cpp:200`（`Update`）是入口。
- **脚步 IK（删除）**：`SkinnedMeshComponent.IK.cpp` 整文件（594 行）—— foot placement chain 解析、两骨 IK、脚趾对地、地面采样、调试绘制。`Update()` 末尾一行 `ApplyFootPlacementIK(deltaTime)`（`SkinnedMeshComponent.cpp:231`）是它唯一的 hook。

IK 当前是"占 LOC 大头且效果有问题"的功能，符合"删而不修"的判断。

### 4.2 删除清单（按文件）

**① `SkinnedMeshComponent.IK.cpp` —— 整文件删除（−594）。**

**② `SkinnedMeshComponent.cpp`（−~60）：**
- 删 `Update()` 中的 `ApplyFootPlacementIK(deltaTime);` 调用（`:231`）。
- 删 `FindJointIndex`（`:449`）、`ExtractJointGlobalRotation`（`:487`）、匿名命名空间内的 `NormalizeJointName`（`:39`）——已用 grep 确认这三个**仅被 IK.cpp 调用**，删 IK 后即成死代码。

**③ `SkinnedMeshComponent.h`（−~65）：** 删除全部 IK 对外/对内面：
- `struct FootPlacementIKSettings`（`:18-37`）与 `struct FootPlacementChain`（`:114-133`）。
- IK 公有方法：`SetFootPlacementIKSettings` / `GetFootPlacementIKSettings` / `SetFootPlacementIKEnabled` / `SetFootPlacementIKWeight`（`:58-61`）。
- IK 私有方法声明：`ApplyFootPlacementIK` / `ResolveFootPlacementChains` / `SampleGroundHeight` / `SolveTwoBoneIK` / `DrawFootPlacementDebug` / `AlignToeToGround` / `MakeRotationBetween` / `FindJointIndex` / `ExtractJointGlobalRotation`（`:98`、`:105-112`、`:134-135`）。
- IK 成员：`footPlacementIKSettings_` / `leftFootPlacementChain_` / `rightFootPlacementChain_` / `hipsJointIndex_` / `footPlacementChainsResolved_` / `footPlacementChainsValid_` / `footPlacementBlendWeight_` / `pelvisOffset_`（`:140-147`）。

**④ `SkinnedMeshComponent.Internal.hpp`（−少量）：** 删 ozz 的 IK job 头：`ik_aim_job.h`、`ik_two_bone_job.h`（`:16-17`）。其余 ozz state 保留（采样/混合仍用）。注意 `SkinnedMeshComponent.cpp:21-22` 也 include 了这两个头，一并删。

> 反射不受影响：`RegisterReflection`（`SkinnedMeshComponent.cpp:56`）只注册 `PlaySpeed/IsPlaying/CurrentAnimation/PlayAnimation/StopAnimation/GetAnimationNames`，从不暴露 IK 字段；`ReflectionRegistry.cpp:30` 的注册调用也无需改。

### 4.3 连带：拆掉跨层的 IK 调用链（NextGameplay + 1 个 app）

IK 的配置入口是一条贯穿 gameplay 的管线，删组件能力后必须同步拆掉，否则编译失败：

| 文件 | 现状 | 处理 |
|---|---|---|
| `Gameplay/Character/CharacterActor.{h,cpp}` | `ConfigureFootPlacementIK(settings)` → `SetFootPlacementIKSettings`（`CharacterActor.cpp:250-260`、`.h:55`） | 删方法 |
| `Gameplay/Components/CharacterGameplayComponent.{h,cpp}` | `ConfigureFootPlacementIK(enabled,debugDraw,weight)`（`.cpp:77-90`、`.h:40`） | 删方法 |
| `Gameplay/Components/CharacterAnimationComponent.cpp` | 调 `gameplayComponent.ConfigureFootPlacementIK(...)`（`:357`） | 删调用 |
| `Application/Game/CharacterDemo/CharacterDemoGameInstance.cpp` | 构造 `FootPlacementIKSettings` 并调 `ConfigureFootPlacementIK`（`:976-987`、`:1097-1108`） | 删这两段 |

这部分改动落在 `NextGameplay` 库与 `CharacterDemo`，**不在核心层**，但属于同一逻辑改动，应在同一 PR 完成。

### 4.4 执行顺序与验证

1. 先删 app/gameplay 侧调用（④表自底向上：CharacterDemo → CharacterAnimationComponent → CharacterGameplayComponent → CharacterActor），再删组件能力（①②③④），避免中间态出现"调用已删方法"。
2. 构建：`./gnb build gkNextRenderer gkNextUnitTests CharacterDemo`。
3. 动画回归：`gnb shot --target CharacterDemo --scene <带蒙皮角色的场景>`，肉眼确认走/跑/待机动画与混合正常（IK 没了，脚不再贴地是**预期**变化）。
4. 单测：`Tests/Test_GltfSkinning.cpp` 必须仍通过。已确认该测试无任何 IK 引用（不触达 foot placement / 两骨 IK 路径），删 IK 不需要改测试。

### 4.5 风险

- **行为变化是有意为之**：删 IK 后角色脚步不再随地形贴合。需确认 CharacterDemo / AirportSim 等没有把"脚贴地"当作不可退化的卖点。
- **可逆性**：IK 代码在 git 历史中保留，未来若要做"对的 IK"可参考但建议重写（当前实现按 joint 名字猜测骨链 `FindJointIndex`，本就脆弱）。
- 若希望降低一次性冲击，可中间态保留 `ConfigureFootPlacementIK` 为**空实现 stub** 一两个版本再删——但这与 KISS 相悖，**默认直接删干净**。

---

## 5. 改动三：UserInterface.ViewportBackend 移出核心（高风险，需设计决策）

### 5.1 直觉对：这段确实只服务编辑器

`UserInterface.ViewportBackend.cpp`（865 行）是 ImGui **多视口（multi-viewport / 可拖出主窗口的浮动面板）** 的平台+渲染后端：每视口独立 swapchain、`Renderer_*`/`Platform_*` 回调。它只在开启 `ImGuiConfigFlags_ViewportsEnable` 时才跑，而开这个 flag 的只有编辑器类 target：

- `Application/Editor/gkNextEditor/EditorInterface.cpp:73-74` 打开 Docking + Viewports；
- `Application/Editor/ScadStudio/ScadStudioInterface.cpp:509` 反而**关掉** Docking；
- 文件自身全程被 `if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)` 包住（`UserInterface.ViewportBackend.cpp:476`、`UserInterface.cpp:793`）。

游戏（MagicaLego/Brotato3D/BrickPlayer 等）用 ImGui 只画 HUD/简单面板，**从不开多视口**。所以这 865 行对每个非编辑器 target 都是"编译进来但永不执行"的死重量。把它移出核心，方向正确。

### 5.2 但"直接把 .cpp 挪进编辑器 app"行不通

这是本轮唯一一处不能机械搬家的改动，原因是**类成员耦合**：

- `ViewportBackend.cpp` 实现的是 `NextUI::UserInterface` 的**私有方法**：`CreatePlatformViewportWindow` / `DestroyPlatformViewportWindow` / `ResizePlatformViewportWindow` / `RenderPlatformViewportWindow` / `SwapPlatformViewportBuffers` 及其 5 个 static 回调、`GetOrCreatePlatformViewportPipeline`、`PrunePlatformViewportRenderBuffers`、`GetRendererBackendOwner`（声明见 `UserInterface.hpp:114-126`）。
- 它还读写 `UserInterface` 私有成员：`uiPlatformViewportPipeline_`、`uiPlatformViewportRenderPass_`、`platformUiRenderBuffers_`（`UserInterface.hpp:145-149`），并复用 `RenderDrawData`（`:127`）。
- `UserInterface` 类本体在核心层、被**所有** program 使用（游戏也要它画 HUD）。若把这些方法的**定义**放到只有编辑器才链接的库，则任何"调用了这些方法但没链接编辑器后端"的 target 会在链接期报 undefined symbol——运行期的 `ViewportsEnable` 开关挡不住链接期。

**结论**：必须先"抽接缝"，把多视口后端从 `UserInterface` 类里剥出来，才能搬走。

### 5.3 方案对比

**已定：方案 B —— 真抽离为自注册 provider，落点 = 编辑器 app 公共层。** 引擎已有 `DebugUiProvider.hpp` 注入先例：`DevTools` 在 `DesktopMain` 里向引擎注册自己。多视口后端照此办理，但实现体放进**编辑器 app 公共目录**而非核心层、也不做成 Module。

> 落点说明：当前没有"编辑器公共层"目录——`src_files_editor` 只 GLOB `Application/Editor/gkNextEditor/*`（`src/cmake/SourceFiles.cmake:90`），`src_files_scadstudio` 只 GLOB `Application/Editor/ScadStudio/*`（`:93`）。因此方案 B 需**新建** `src/Application/Editor/Common/`，作为编辑器类 app 共享的源目录。

步骤：

1. **核心层留最小接口（~40 行）**：把多视口相关的私有成员（`uiPlatformViewportPipeline_`、`uiPlatformViewportRenderPass_`、`platformUiRenderBuffers_`，`UserInterface.hpp:145-149`）与私有方法（`UserInterface.hpp:114-126`）从 `UserInterface` 收拢，定义一个纯虚接口 `NextUI::IMultiViewportBackend`（`Initialize(device,...)` / `Shutdown()` / `OnPostRender(...)`），留在核心层。`UserInterface` 改为持 `std::unique_ptr<IMultiViewportBackend> viewportBackend_`（默认空），在 `PostRender` 的 `ViewportsEnable` 分支（`UserInterface.cpp:793`）里 `if (viewportBackend_) viewportBackend_->OnPostRender(...)`，并新增一个注册入口 `SetMultiViewportBackend(...)`。后端干活所需的少量句柄（device、`RenderDrawData` 入口）由接口参数/回调传入，避免反向依赖。
2. **实现体下沉到编辑器 app 公共层**：把现 `UserInterface.ViewportBackend.cpp`（865 行）改写为 `src/Application/Editor/Common/MultiViewportBackend.{hpp,cpp}`，实现 `IMultiViewportBackend`。删除核心层的 `Engine/Runtime/Editor/UserInterface.ViewportBackend.cpp`。
3. **编辑器 app 注册**：`gkNextEditor`（及将来想要浮动面板的 ScadStudio）在 UI 初始化处（`EditorInterface.cpp:73-74` 打开 `ViewportsEnable` 的同一位置）`SetMultiViewportBackend(std::make_unique<MultiViewportBackend>(...))`。游戏 target 不编译该公共层、不注册，`viewportBackend_` 恒空，多视口路径彻底不进二进制。
4. **CMake 接线**（因落点是 app 源目录而非 Module，需手改，不能纯靠 GLOB 自动建库）：
   - 在 `src/cmake/SourceFiles.cmake` 新增 `file(GLOB_RECURSE src_files_editorcommon "Application/Editor/Common/*")`。
   - 在 `src/CMakeLists.txt` 的 `add_executable(gkNextEditor ...)`（`:156`）源列表加入 `${src_files_editorcommon}`；ScadStudio（`:163`）按需加入。
   - `IMultiViewportBackend` 接口头随核心层走，无需额外配置。

效果：核心层 −865（+ 接口 ~40），且消除"游戏携带编辑器死代码"。代价：要动 `UserInterface` 的对外面（新增注册 hook）、把 3 个私有成员搬进新类，并新增一个编辑器公共源目录 + CMake 接线——本轮最重、最需要单独 review 的改动。

> 已否决的方案 A（仅运行期开关）：保持文件在核心层不动、仅靠 `ViewportsEnable` 守卫。改动量≈0 但核心层 LOC 不减，不满足"移出核心"的目标，故不采用。

### 5.4 推进要点

- 改动 3 **与 1、2 解耦，单独排期、单独 PR**。先合 1、2 拿到稳定的 LOC 收益，再单独做 3 的接缝设计。
- 接缝设计的关键约束：`IMultiViewportBackend` 接口必须把后端对 `UserInterface` 私有态的访问收敛成显式参数/回调，不能让核心层反向 include 编辑器公共层（否则又成环）。
- 验证：`./gnb build gkNextEditor ScadStudio MagicaLego gkNextRenderer`——编辑器多视口（浮动面板可拖出主窗口）正常；`MagicaLego`/`gkNextRenderer` 链接通过且 HUD 正常，证明多视口已不在其链路。

---

## 6. 总执行顺序

1. **PR-1 改动一**（搬 NextCharacterController）：纯搬家，先合，零行为风险。
2. **PR-2 改动二**（砍 IK）：核心收益最大（−~700）；含 gameplay 调用链清理；需动画肉眼回归。
3. **PR-3 改动三**（多视口后端外迁，方案 B）：需先设计接缝，独立 review。

每个 PR 的最小验证集：

| PR | 构建目标 | 额外验证 |
|---|---|---|
| 1 | `NextGameplay CharacterDemo gkNextRenderer gkNextUnitTests` | DevTools 角色调试绘制已迁入 NextGameplay（方案 i）；确认 DevTools 不再 include 角色头、依赖图无环 |
| 2 | `gkNextRenderer gkNextUnitTests CharacterDemo` | `Test_GltfSkinning` 通过；`gnb shot --target CharacterDemo` 动画正常 |
| 3 | `gkNextEditor ScadStudio MagicaLego gkNextRenderer` | 编辑器浮动面板正常；游戏链接通过 |

全部合并后跑一次全量 `./gnb build --reconfigure` 确认所有 program 编译。

---

## 7. LOC 账本

| 改动 | 核心层（gkNextEngine）变化 | 其他库变化 | 说明 |
|---|---:|---:|---|
| 一 NextCharacterController 下沉 | −277 | NextGameplay +277 | 等量平移，核心层净减 |
| 二 砍 SkinnedMesh IK | −700 ~ −760 | NextGameplay/CharacterDemo 另减 ~100 | IK.cpp 594 + 头/.cpp 残骸 ~120 |
| 三 多视口后端外迁（方案 B） | −865（+ 接口 ~40） | 编辑器 app 公共层 +865 | 净减约 −825 |
| **合计（1+2）** | **约 −1,000** | | 稳态、低争议 |
| **合计（1+2+3）** | **约 −1,825** | | 含编辑器接缝改造 |

核心层从 32,182 起算，仅做 1+2 即落到 **~31.2k**；三项全做落到 **~30.4k**，逼近 30k 目标。

> 与 Round 2 的关系：Round 2 主攻"god class 拆解 + include 卫生"（同库内拆文件，LOC 净减有限）；Round 3 主攻"跨库归位 + 删重资产"（LOC 净减明确）。两轮互补，可并行推进，互不冲突（改动文件无重叠）。

---

## 8. 决策记录（已拍板，2026-06-14）

1. **改动 1 / DevTools overlay**：✅ 采用**方案 i**——把 `DrawCharacterControllerDebugOverlay` 挪进 NextGameplay（`src/Gameplay/Character/CharacterControllerDebugDraw.{hpp,cpp}`），DevTools 不再依赖角色类型。不给 DevTools 补链接 NextGameplay。
2. **改动 2 / 脚步 IK**：✅ **接受视觉退化，直接删除**。删 IK 后角色脚步不再贴合地形，视为可接受的取舍。
3. **改动 2 / 测试**：✅ `Test_GltfSkinning` 已确认无 IK 引用，**直接删除组件 IK 能力，无需改测试**。
4. **改动 3 / 多视口后端**：✅ 采用**方案 B（真抽离）**，落点 = **编辑器 app 公共层**（新建 `src/Application/Editor/Common/`），不做成 Module、不放核心层。CMake 需新增 `src_files_editorcommon` GLOB 并接入编辑器 target（见 §5.3 步骤 4）。
