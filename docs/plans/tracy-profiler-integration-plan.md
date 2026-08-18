---
title: "Tracy Profiler 接入与 CPU/GPU 计时设施重构计划"
category: plan
status: 已完成
owner: engine
created: 2026-08-17
last_updated: 2026-08-17
---

# Tracy Profiler 接入与 CPU/GPU 计时设施重构计划

## 结论

1. **Tracy 以「第二个 sink」的身份接入，不取代现有 `FrameProfiler`。**
   现有 `SCOPED_CPU_TIMER` / `SCOPED_GPU_TIMER` 宏是全仓 92 处调用点的唯一入口，宏名与语义保持不变；
   把宏体从「一个实现」改成「向 N 个 sink 扇出」：ImGui overlay 聚合器（现有 `FrameProfiler`）、
   Superluminal（现有）、Tracy（新增）。**不做任何调用点的批量改写。**
2. **GPU 侧走 `IGpuProfilerBackend` 组合器，Tracy 用自己的 `VkCtx` 与 query pool，不复用我们的时间戳。**
   Tracy 的 Vulkan 上下文自带 query pool、collect 与时钟校准；试图把 `GpuQueryTimer` 的时间戳"喂"给 Tracy
   需要手工构造 GPU zone 序列化，脆弱且无收益。新增 `FCompositeGpuProfilerBackend` 把 Begin/End 扇出到
   `GpuQueryTimer`（overlay 用）与 `TracyGpuProfilerBackend`（Tracy 用）两个后端。
3. **接入 Tracy 前必须先修一处现有缺陷：`GpuQueryTimer::EndFrame` 用 `VK_QUERY_RESULT_WAIT_BIT` 同步读回
   （`src/Engine/Vulkan/GpuQueryTimer.cpp:170-178`）。** 它在 `DrawFrame` 开头、fence 等待之前阻塞主线程等 GPU
   查询完成。接入 Tracy 后时间线上会明确看到这段 stall，并且会污染所有 CPU zone 的归因。这是本次"整理重构"的
   第一优先项，且它本身就是一个可独立验证的性能修复。
4. **依赖用 vcpkg 端口 `tracy 0.13.1`（仓库 registry 已有），构建开关用 CMake option `GK_ENABLE_TRACY`，
   默认 dev 开、release 关。** Server（Tracy GUI）不走 vcpkg 的 `gui-tools` feature（会拖进第二份 imgui、
   capstone、glfw、freetype 等），改用 `gnb tracy fetch` 下载官方同版本预编译包到 `external/tracy/`，
   与 `external/llm`、`external/VulkanSDK` 的既有模式一致。**client 与 server 版本必须严格一致**，
   Tracy 协议版本不匹配会直接拒绝连接。
5. **Android 实时连接走 `adb forward tcp:8086`，不依赖 UDP 广播发现。**
   需要给 APK 加 `android.permission.INTERNET`，并用 `relwithdebinfo` 变体保留符号。工具链入口复制
   `gnb android` 里 RenderDoc 的既有做法（`tools/gnb/internal/android/android.go:76` `OpenRenderDoc`）。
6. **推进顺序：M0 依赖与开关 → M1 CPU 接入 → M2 GPU 接入 → M3 现有设施重构 → M4 Android 实时连接 →
   M5 数据增强与工具化。** M0–M2 结束就已经能在 Windows 上实时连接看 CPU+GPU 时间线；M3 是必须做但可以
   与 M1/M2 并行验证的清理；M4 交付 Android；M5 是 plot / message / headless capture 等增益项。

## 当前实现盘点

### 前端：两个宏，一个全局 active profiler

`src/Engine/Runtime/Profiling/FrameProfiler.hpp:12-14`：

```cpp
#define SCOPED_GPU_TIMER_CMD(cmd, name) Runtime::ScopedGpuProfileScope ...(cmd, FrameProfiler::GetActiveProfiler(), name)
#define SCOPED_GPU_TIMER(name)          SCOPED_GPU_TIMER_CMD(commandBuffer, name)
#define SCOPED_CPU_TIMER(name)          PERFORMANCEAPI_INSTRUMENT_DATA(name, ""); Runtime::ScopedCpuProfileScope ...
```

- 全仓 92 处使用，分布在 19 个文件（Engine/Rendering 为主，另有 Scene、Engine::Tick、SplatLoader、DevTools）。
- `SCOPED_CPU_TIMER` 已经是"双 sink"结构：Superluminal 宏 + `FrameProfiler`。Tracy 是把 2 变成 3，形状不变。
- **绝大多数调用点传字符串字面量**；只有两处传运行时字符串：
  `src/Engine/Rendering/VulkanBaseRenderer.cpp:1807`（`view.DebugName()`）与
  `src/Engine/Rendering/VulkanBaseRenderer.GiBake.cpp:198`（`timerName`）。这决定了 Tracy zone 必须支持
  运行时名字（transient / alloc'd source location），不能只用 `ZoneScopedN` 这种编译期字面量形式。
- active profiler 是静态单例指针（`FrameProfiler.hpp:99`），由 `VulkanBaseRenderer` 在
  `src/Engine/Rendering/VulkanBaseRenderer.cpp:931-932` 构造并自动置位。

### CPU 聚合：帧内 vector + 路径式 stableKey

`FrameProfiler.cpp:126-186`：`BeginCpuFrame` 清空记录，`BeginCpuScope` 压栈并生成
`/engine#0/scene tick#0` 形式的 stableKey，`EndCpuFrame` 把非零耗时记录快照给 overlay。

- 帧边界在 `src/Engine/Runtime/Engine.cpp:717-720` 与 `:908-911`（`NextEngine::Tick` 首尾）。
- **该结构完全不是线程安全的**：`cpuTimerRecords_`、`cpuActiveStack_` 都是裸成员。当前所有
  `SCOPED_CPU_TIMER` 都在主线程；`Scene::StartUpdateNodes` 派发到 `TaskCoordinator` 的工作线程里
  **没有**计时宏。也就是说：多线程部分目前是 profiling 盲区，而这恰好是 Tracy 相对现有 overlay 的最大增量。
- 消费端只有 ImGui overlay：`src/Modules/DevTools/ProfileDebugOverlay.cpp:90-150`，`FetchCpuTimes(3)` 取前 3 层。

### GPU 聚合：单缓冲 query pool + 阻塞读回

`src/Engine/Vulkan/GpuQueryTimer.cpp`：

- 200 个 timestamp 的单个 `VkQueryPool`（`VulkanBaseRenderer.cpp:932` 传入 `totalCount=200`）。
- `BeginFrame`（`:141-154`）在当前 command buffer 里 `vkCmdResetQueryPool` 整池，然后结算上一帧统计。
- `EndFrame`（`:156-207`）**在 CPU 上以 `VK_QUERY_RESULT_WAIT_BIT` 读回**。它被调用于
  `VulkanBaseRenderer.cpp:1986-1989`，即 `DrawFrame` 开头、`FrameSubmission::WaitAndAcquire` 之前，
  且自身被 `SCOPED_CPU_TIMER("hwquery")` 包着——这段等待现在被记成 CPU 耗时，实际是等 GPU。
- `BeginScope` 同时下 `vkCmdWriteTimestamp` 与 debug marker（`:229-230`），marker 与计时是耦合的。
- 逐帧只有一份 pool，没有按 frame-in-flight 分 bank；正确性靠"读回时阻塞等完"兜底。

### Superluminal 与线程命名

- `src/Engine/Common/CoreMinimal.hpp:68-156`：`WITH_SUPERLUMINAL` 打开时用真 API，否则一整套空宏 + 空函数。
- `src/Engine/Runtime/Subsystems/TaskCoordinator.cpp:20-25`：工作线程启动时命名（仅 Superluminal 分支）。
- 构建开关在 `cmake/SetupDependencies.cmake:30-41`（探测安装路径）与
  `src/cmake/TargetHelpers.cmake:290-300`（`gk_link_engine_feature_dependencies`）。**这是本次新增 Tracy 开关
  应当照抄的模式。**
- ⚠️ `README.md:129` / `README.en.md:131` 声称"GPU 事件经独立回放线程标注到 Superluminal 时间线"，
  但当前代码里**没有**这条路径（全仓无 `PerformanceAPI::BeginEvent` 的 GPU 回放调用）。文档与实现不一致，
  M5 顺手修正。

### 重复代码与测试现状

- `BuildStableKey` / `ResetTimerRecords` / `PopActiveTimer` / `FilterTimerStats` 在
  `FrameProfiler.cpp:5-68` 与 `GpuQueryTimer.cpp:8-55` **各有一份近乎逐字相同的副本**。
- `src/Tests/Test_GpuTimerLogic.cpp` 又在测试文件里**第三次**手抄了同一套 stableKey 逻辑（`TimerPathBuilder`），
  测的是抄本而不是产品代码。

## 目标与非目标

**目标**

- 在 Windows / Linux / macOS / Android 上，运行中的引擎可被 Tracy Profiler 实时连接，看到：
  逐帧 CPU zone 树（含工作线程）、GPU zone（与现有 pass 命名一致）、帧边界、关键计数器。
- 现有 ImGui overlay 与 Superluminal 行为不回退。
- 调用点零改写；新增 zone 只需继续用现有宏。
- Release 构建默认不含 Tracy client（无监听端口、无额外线程）。

**非目标（本计划不做）**

- 不接 Tracy 的 memory profiling（`TracyAlloc`/`TracyFree` 全局 new/delete hook）与 lock profiling——
  留作 M5 之后的独立议题，二者对帧时间与二进制体积影响明显，需要单独度量。
- 不做 fiber 支持（`TRACY_FIBERS`）：`TaskCoordinator` 是普通 std::thread 池，不需要。
- 不替换 Superluminal，也不删除 ImGui overlay。overlay 是"随时能看的粗粒度"，Tracy 是"要连接的细粒度"，
  两者定位不同。
- 不在 iOS 上启用（M0 起就把 iOS 显式关掉，等桌面/Android 稳定后再评估）。

## 目标架构

### 分层

```
调用点   SCOPED_CPU_TIMER("scene tick") / SCOPED_GPU_TIMER("[render]")
             │
前端     Engine/Runtime/Profiling/ProfilerMacros.hpp   ← 宏在这里扇出，唯一改动面
             ├── Superluminal  (PERFORMANCEAPI_*，现有，不动)
             ├── Tracy CPU     (GK_TRACY_ZONE，新增，GK_TRACY_ENABLED=0 时整体消失)
             └── FrameProfiler (现有聚合器，overlay 消费)
                     └── IGpuProfilerBackend
                             └── FCompositeGpuProfilerBackend  ← 新增
                                     ├── Vulkan::GpuQueryTimer          (现有，overlay 数据源)
                                     └── Vulkan::TracyGpuProfilerBackend(新增，tracy::VkCtx)
```

### 文件落位

| 文件 | 状态 | 职责 |
|---|---|---|
| `src/Engine/Runtime/Profiling/ProfilerMacros.hpp` | 新增 | 唯一对外宏定义，从 `FrameProfiler.hpp` 拆出 |
| `src/Engine/Runtime/Profiling/TracyIntegration.hpp/.cpp` | 新增 | Tracy 头文件封装、`GK_TRACY_ENABLED` 契约、`SetThreadName`/`FrameMark`/`Plot`/`Message` 薄封装 |
| `src/Engine/Runtime/Profiling/ProfileScopeTree.hpp/.cpp` | 新增 | 抽出 stableKey/栈/过滤逻辑，供 CPU 与 GPU 两侧共用（消除三份抄写） |
| `src/Engine/Runtime/Profiling/FrameProfiler.hpp/.cpp` | 改造 | 保留聚合器职责，改用 `ProfileScopeTree`，明确主线程契约 |
| `src/Engine/Runtime/Profiling/CompositeGpuProfilerBackend.hpp/.cpp` | 新增 | N 后端扇出 + scope id 映射 |
| `src/Engine/Vulkan/GpuQueryTimer.hpp/.cpp` | 改造 | 去重复代码、按 frame-in-flight 分 bank、非阻塞读回 |
| `src/Engine/Vulkan/TracyGpuProfilerBackend.hpp/.cpp` | 新增 | `tracy::VkCtx` 生命周期、transient GPU zone、`TracyVkCollect` |

源文件是 `file(GLOB_RECURSE ...)` 收集的（`src/cmake/SourceFiles.cmake:24,52`），新增文件不需要改 CMake 列表，
但首次构建要带 `--reconfigure`。

### CPU 侧宏扇出

```cpp
// ProfilerMacros.hpp（示意）
#if GK_TRACY_ENABLED
  #define GK_TRACY_CPU_ZONE(name) \
      tracy::ScopedZone GK_CONCAT(gkTracyZone_, __LINE__)( \
          __LINE__, __FILE__, sizeof(__FILE__) - 1, __FUNCTION__, sizeof(__FUNCTION__) - 1, \
          (name), GkProfiling::ZoneNameLength(name), true)
#else
  #define GK_TRACY_CPU_ZONE(name)
#endif

#define SCOPED_CPU_TIMER(name)                       \
    PERFORMANCEAPI_INSTRUMENT_DATA(name, "");        \
    GK_TRACY_CPU_ZONE(name);                         \
    Runtime::ScopedCpuProfileScope GK_CONCAT(scopedCpuTimer_, __LINE__)( \
        Runtime::FrameProfiler::GetActiveProfiler(), name)
```

要点：

- 用 `tracy::ScopedZone` 的**运行时 name 重载**而不是 `ZoneScopedN`，这样字面量与运行时字符串统一处理，
  也不用为两处运行时 GPU 名字开特例。Tracy 会拷贝 name，指针生命周期无要求。
- 代价是每个 zone 多一次 name 长度计算与一次动态 source location 分配。若后续实测显示高频 zone
  （例如 `Scene.Update` 内每节点级别）开销可见，再补一个 `SCOPED_CPU_TIMER_LITERAL` 走静态 srcloc 的快路径；
  当前 92 处调用点都是 per-pass 粒度，不预先优化。
- `GK_TRACY_ENABLED` 关闭时整行消失，与 `WITH_SUPERLUMINAL=0` 的既有做法对齐。

### 帧边界与线程

- `FrameMark` 放在 `NextEngine::Tick` 末尾，紧贴 `Profiler()->EndCpuFrame()`（`Engine.cpp:908-911`）。
  Tracy 的帧边界必须与 overlay 的 CPU 帧边界是同一个点，否则两边读数无法互相印证。
- `tracy::SetThreadName` 加在 `TaskCoordinator.cpp:15-25` 线程入口，与现有 Superluminal 命名并列；
  主线程在引擎初始化时命名为 `Main`。
- **工作线程从此可以安全使用 `SCOPED_CPU_TIMER`**：Tracy 天然按线程分离，而 `FrameProfiler` 侧在 M3
  加主线程断言后会对非主线程直接 no-op（而不是像现在这样静默破坏记录树）。这条是本次接入的实质收益之一。

### GPU 侧

`FCompositeGpuProfilerBackend` 实现 `IGpuProfilerBackend`，内部持有 `std::vector<std::unique_ptr<IGpuProfilerBackend>>`：

- `BeginScope` 返回**自己的** id，内部表记录每个子后端返回的 id；`EndScope` 按表反查逐个转发。
- `GetTime` / `FetchTimes` 只转发给"主后端"（`GpuQueryTimer`），overlay 数据来源不变。
- 子后端返回 `invalidTimerId` 时该子后端在这一 scope 上跳过，不影响其他后端。

`Vulkan::TracyGpuProfilerBackend`：

- 构造：`TracyVkContextCalibrated(physicalDevice, device, graphicsQueue, cmdBuffer, vkGetPhysicalDeviceCalibrateableTimeDomainsEXT, vkGetCalibratedTimestampsEXT)`；
  设备不支持 `VK_EXT_calibrated_timestamps` 时退回 `TracyVkContext(...)`。需要在
  `src/Engine/Rendering/VulkanBaseRenderer.cpp:407-411` 的 `requiredExtensions` 之外增加一条**可选**扩展探测
  （现有代码只有 required 列表，需要补一个 optional 探测分支）。校准需要一次性提交，用现成的
  `Vulkan::SingleTimeCommands`（`src/Engine/Vulkan/CommandExecution.hpp:68`）。
- `BeginScope` 用 transient zone（运行时名字），把 `tracy::VkCtxScope` 放进 `std::deque<std::optional<tracy::VkCtxScope>>`；
  `EndScope` 对应槽位 `reset()`。`VkCtxScope` 不可拷贝不可移动，`optional::emplace` 原地构造可行；
  我们的 scope 严格 LIFO 嵌套，析构顺序满足要求。
- `EndFrame` 里调用 `TracyVkCollect(ctx, cmdBuffer)`。注意 collect 需要一个**正在录制**的 command buffer，
  而现有 `EndGpuFrame` 拿到的 buffer 此刻并未 begin（`VulkanBaseRenderer.cpp:1988` 传的是上一帧的 buffer）。
  因此 Tracy 的 collect 落在 `BeginGpuFrame`（`VulkanBaseRenderer.cpp:2031`，此时 buffer 刚 `Begin`）之后的
  第一件事，而不是照搬 `GpuQueryTimer` 的时序。**这个时序差异是 M2 最容易出错的点，要在 journal 里写清。**
- 一个 `VkCtx` 对应一个 queue；当前引擎只在 graphics queue 上录制主渲染，满足 Tracy 的单线程录制约束。

### 计数器与事件（M5）

- `TracyPlot`：fps / draw call / triangle / VRAM budget，数据源直接用
  `ProfileDebugOverlay.cpp` 已在读的 `Scene::GetGpuDrivenStat()` 与 allocator 统计。
- `TracyMessage`：场景加载完成（`committed scene [...]`）、renderer 热切换（`Engine.cpp:743-746`）、
  shader/script 热重载。
- `TracyAppInfo`：启动时写入 target 名、renderer type、GPU 名、构建配置。

## 依赖与构建开关

### vcpkg

`vcpkg.json` 新增（仓库 registry 已含 `tracy 0.13.1`，见 `.vcpkg/ports/tracy/vcpkg.json`）：

```json
{
  "name": "tracy",
  "default-features": false,
  "features": ["on-demand"],
  "platform": "!ios"
}
```

- **`on-demand` 必须开。** 不开的话 client 从进程启动就开始采集并在内存里堆积事件直到有 profiler 连上，
  长时间运行会持续涨内存；用户要的是"运行时随时连"，这正是 `TRACY_ON_DEMAND` 的场景。
- **`crash-handler` 显式关掉**（`default-features: false`）。引擎已有 `cpptrace`
  （`src/Engine/CMakeLists.txt:88-90`）与自有崩溃路径，两个 handler 抢注册只会让崩溃现场更难读。
- `gui-tools` / `cli-tools` 不在 manifest 里；GUI 走 `gnb tracy fetch`，`tracy-capture` 若 M5 需要再单独评估。
- 端口在 android triplet 上的可构建性是 M0 的第一个验收点（port 的 `supports` 只排除了 windows-arm/uwp，
  但实际 arm64-android 构建未在本仓验证过）。

### CMake

照抄 Superluminal 的形状：

- `cmake/SetupDependencies.cmake`：新增 `GK_ENABLE_TRACY` option 与 `find_package(Tracy CONFIG)`，
  失败时降级为 OFF 并 `message(STATUS ...)`，**不 FATAL**（与 RenderDoc/Superluminal 的"没装就跳过"一致）。
- `src/cmake/TargetHelpers.cmake` 的 `gk_link_engine_feature_dependencies`（`:290`）里加：
  ```cmake
  if(GK_ENABLE_TRACY)
      target_compile_definitions(${target} PUBLIC GK_TRACY_ENABLED=1)
      target_link_libraries(${target} PRIVATE Tracy::TracyClient)
  else()
      target_compile_definitions(${target} PUBLIC GK_TRACY_ENABLED=0)
  endif()
  ```
  该函数同时被 `gkNextEngine`（`src/Engine/CMakeLists.txt:108`）与 Android runtime 目标
  （`gk_configure_android_runtime`，`TargetHelpers.cmake:362`）调用，一处改动覆盖桌面与 Android。
- 默认值建议：`GK_ENABLE_TRACY` 默认 **ON**（dev 体验优先，on-demand 下不连接时开销极小），
  由 release 打包路径显式传 `-DGK_ENABLE_TRACY=OFF`。这条要与 `docs/guides/release-process.md` 对齐，
  M0 一并改该文档，否则会悄悄把监听端口带进发布包。
- iOS 分支强制 OFF。

### 宏契约

`GK_TRACY_ENABLED` 是**引擎侧**开关（0/1 都定义，禁止用 `#ifdef`，与 `AGENTS.md` 的 `#if ANDROID` 约定一致）；
`TRACY_ENABLE` 是 Tracy 自己的开关，由 `Tracy::TracyClient` 的 INTERFACE 定义带入，**我们不手写**。

## 运行时连接工作流

### Windows / Linux / macOS 本机

1. `gnb tracy fetch` → 下载 `tracy-<version>-<platform>` 官方预编译包到 `external/tracy/`（沿用
   `tools/gnb/internal/fetcher` 的下载+校验+解压模式）。版本号写在 `gnb.toml`，与 vcpkg 端口版本
   （0.13.1）**同一个来源**，不允许两处各写各的。
2. `gnb tracy` → 启动 Tracy GUI；再正常 `gnb run <target>`，GUI 的 discovery 列表里会出现进程，点击连接。
3. 断开后进程继续跑（on-demand 语义），可反复连接。

### Android

1. `gnb android relwithdebinfo`（**不要用 release**，需要符号）。
2. `gnb tracy --android [--serial <s>]` 做三件事：
   - `adb forward tcp:8086 tcp:8086`
   - 启动 APK（复用 `android.go` 的 `launchInstalled`）
   - 启动本机 Tracy GUI，并提示"Connect to 127.0.0.1"
3. UDP 广播发现不穿 adb，必须手工填 `127.0.0.1:8086`；这一步要写进 guide，否则第一次用的人会以为没接上。
4. `tools/android/templates/app/src/main/AndroidManifest.xml` 需要新增
   `<uses-permission android:name="android.permission.INTERNET" />`（当前只有 VIBRATE，`:9`）。
   该模板由 `tools/android/CMakeLists.txt:265-269` `configure_file` 到 gradle 目录。
5. 符号：Tracy 需要未 strip 的 `.so` 才能解调用栈采样；`relwithdebinfo` 变体已具备，release 不支持。

### 环境变量（无需重编译的现场调参）

Tracy client 读取 `TRACY_PORT`、`TRACY_NO_BROADCAST`、`TRACY_ONLY_LOCALHOST` 等环境变量。
Android 侧端口冲突或多设备并行时用 `TRACY_PORT` 区分，`gnb tracy --android --port` 透传即可，
不需要引擎侧配置项。

## 里程碑

### M0 — 依赖、开关与空接入（不改任何行为）

任务：

- `vcpkg.json` 增加 `tracy`（`default-features: false` + `on-demand`，排除 ios）。
- `cmake/SetupDependencies.cmake` + `src/cmake/TargetHelpers.cmake` 增加 `GK_ENABLE_TRACY` 与链接。
- 新增 `Engine/Runtime/Profiling/TracyIntegration.hpp`：包含 Tracy 头、定义空封装，`GK_TRACY_ENABLED=0`
  时提供同名空实现。
- 把宏从 `FrameProfiler.hpp` 拆到 `ProfilerMacros.hpp`，`FrameProfiler.hpp` 转为 include 它（保持所有
  现有 include 可用，零调用点改动）。
- 更新 `docs/guides/release-process.md`：发布构建显式 `-DGK_ENABLE_TRACY=OFF`。

验收：

- `gnb build --reconfigure`（Windows）通过；`GK_ENABLE_TRACY=OFF` 与 `ON` 两种配置都能编译链接。
- `gnb android relwithdebinfo` 通过 —— **这是 M0 的真正 gate**，若 tracy 端口在 arm64-android 上不可构建，
  在这里就必须决定是 vendored 客户端源码（Tracy client 是单 TU，可直接加进 `src/ThirdParty/`）还是放弃
  Android 目标。不要把这个不确定性带进 M1。
- `gkNextUnitTests` 通过。

### M1 — CPU zone、帧标记与线程命名

任务：

- `SCOPED_CPU_TIMER` 扇出 Tracy zone（`tracy::ScopedZone` 运行时 name 形式）。
- `NextEngine::Tick` 末尾 `FrameMark`。
- `TaskCoordinator` 线程命名 + 主线程命名 + `TracyAppInfo`（target/renderer/GPU/config）。

验收：

- Windows 上 `gnb run gkNextRenderer`，Tracy GUI 连接后可见逐帧 zone 树，层级与 overlay 的
  `engine → scene tick / game tick / draw frame` 一致；断开重连正常。
- Tracy 里 `engine` zone 的耗时与 overlay 表格的 `engine` 行读数在同一量级（差异应 < 5%，
  差异更大说明帧边界对齐错了）。
- `gnb shot --scene assets/models/playground.glb` 仍能出图（确认 Tracy 未破坏隐藏窗口/退出路径）。

### M2 — GPU zone

任务：

- 新增 `FCompositeGpuProfilerBackend` + 单元测试（scope id 映射、子后端返回 invalid 的降级）。
- 新增 `Vulkan::TracyGpuProfilerBackend`；`VulkanBaseRenderer.cpp:931-932` 处改为按开关组合两个后端。
- `VK_EXT_calibrated_timestamps` 可选探测（`VulkanBaseRenderer.cpp:407-411` 附近新增 optional 扩展分支），
  不可用时退回非校准上下文。
- `TracyVkCollect` 时序：放在 `BeginGpuFrame` 之后（command buffer 已 Begin），**不要**跟着
  `EndGpuFrame` 走。

验收：

- Tracy GUI 中出现 GPU 时间线，zone 名与现有 pass 名一致（`[gpu]` / `[pre-render]` / `[render]` / `[post-render]`）。
- 两处运行时名字（`view.DebugName()`、GiBake 的 `timerName`）在 Tracy 中显示为真实名字而非空/乱码。
- overlay 的 GPU 表格数值不变（说明 `GpuQueryTimer` 未被组合器影响）。
- macOS/MoltenVK 上若 timestamp query 不可用，Tracy GPU 上下文创建失败要走降级日志而不是崩溃。

### M3 — 现有计时设施重构

任务（可与 M1/M2 并行开发，但必须在 M4 前合入）：

1. **非阻塞 GPU query 读回**：`GpuQueryTimer` 按 frame-in-flight 分 query bank，去掉
   `VK_QUERY_RESULT_WAIT_BIT`，改为"上上帧结果就绪才读"，未就绪则沿用上次统计。
   `SCOPED_CPU_TIMER("hwquery")` 这段应从可见的毫秒级 stall 降到接近 0。
2. **抽出 `ProfileScopeTree`**：`FrameProfiler.cpp:5-68` 与 `GpuQueryTimer.cpp:8-55` 的四个重复函数合并；
   `src/Tests/Test_GpuTimerLogic.cpp` 改为直接测这个共享实现，删掉 `TimerPathBuilder` 抄本。
3. **主线程契约**：`FrameProfiler` 记录创建线程 id，非该线程调用 `BeginCpuScope` 直接返回 `invalidTimerId`
   并只走 Tracy 路径（debug 下 `SPDLOG_WARN` 一次）。这样工作线程可以放心加 `SCOPED_CPU_TIMER`。
4. **debug marker 与计时解耦**：`GpuQueryTimer::BeginScope` 里的 `DebugUtils().BeginMarker` 移出到独立的
   `ScopedGpuMarker`，使得"关掉 `GOption->HardwareQuery` 时仍有 RenderDoc marker"成立
   （当前 `HardwareQuery=false` 会连 marker 一起丢，直接影响 RenderDoc 抓帧的可读性——
   这和刚落地的 RenderDoc 服务是同一条工作流）。

验收：

- `gkNextUnitTests` 全绿，新增共享 scope-tree 测试覆盖 stableKey 冲突、深度过滤、栈错配。
- 用 Tracy 对比重构前后：`hwquery` zone 的耗时显著下降；帧时间不回退。
- `GOption->HardwareQuery=false` 时 RenderDoc 抓帧仍有 pass marker。

### M4 — Android 实时连接

任务：

- AndroidManifest 增加 `INTERNET` 权限。
- `tools/gnb/internal/tracy/`（新包）+ `gnb tracy` / `gnb tracy fetch` / `gnb tracy --android`，
  android 部分复用 `internal/android` 的 adb 发现、设备选择、启动逻辑。
- 新增 `docs/guides/tracy-profiling.md`：桌面与 Android 两条连接路径、版本一致性要求、
  on-demand 语义、常见连不上的原因（端口占用 / 未 forward / 版本不匹配 / release 构建关了开关）。

验收：

- 真机（arm64）`gnb android relwithdebinfo` + `gnb tracy --android`，GUI 能连上并持续出帧。
- 断开 GUI 后 app 继续正常运行，再次连接可恢复。
- 记录一份基线：同一场景 Android 上 CPU/GPU zone 的前 10 项耗时，写进 journal，作为后续优化的对照。

### M5 — 数据增强与文档收口

任务：

- `TracyPlot`：fps、draw call、triangle、VRAM。
- `TracyMessage`：场景加载、renderer 切换、shader/script 热重载。
- 修正 `README.md:127-129` / `README.en.md:129-131` 关于 Superluminal GPU 回放线程的过时描述，
  并补上 Tracy 段落。
- 评估（不一定实施）：`tracy-capture` 无头抓取接入 `gkNextMotionBenchmark`，让 benchmark 产出可归档的 trace。

验收：

- Tracy 中 plot 曲线与 overlay 数值一致。
- `docs/README.md` 索引更新；本 plan 状态改为已完成并按文档生命周期规则退出现行面（耐久内容进
  `docs/guides/tracy-profiling.md`）。

## 风险与回退

| 风险 | 影响 | 处置 |
|---|---|---|
| tracy 端口在 arm64-android triplet 构建失败 | M4 无法交付 | M0 就验证；失败则把 Tracy client 单 TU vendored 进 `src/ThirdParty/tracy`，仅 Android 走 vendored 路径 |
| client/server 版本不匹配 | 连接被拒，且报错不直观 | 版本号单一来源写在 `gnb.toml`；`gnb tracy` 启动前比对 vcpkg 已安装版本与 `external/tracy/` 版本，不一致直接报错 |
| 未开 `on-demand` 导致长跑内存增长 | 长时间运行 OOM，Android 尤甚 | manifest 里显式 feature，M0 验收时用 `gnb dashboard`/日志观察 30 分钟空跑内存曲线 |
| Tracy crash handler 与 cpptrace 冲突 | 崩溃现场丢失 | `default-features: false` 关掉；若将来要开，必须先决定谁是唯一 handler |
| Release 包误带 Tracy | 发布产物监听 8086，安全与体积问题 | M0 同步改 `release-process.md`；CI 打包路径显式 `-DGK_ENABLE_TRACY=OFF`，并在 release 验收清单加一条端口检查 |
| `TracyVkCollect` 时序放错（沿用 `EndGpuFrame`） | GPU zone 缺失或 validation error | M2 明确写在实现注释里；用 validation layer 跑一遍 `gnb shot` |
| MoltenVK / 部分 Android GPU 不支持 timestamp query 或 calibrated timestamps | GPU 上下文创建失败 | 双重降级：先退非校准，再退"仅 CPU zone"，任何一级失败都只 `SPDLOG_WARN` |
| zone 名字动态分配带来的开销 | 高频 zone 下 CPU 反噬 | 当前均为 per-pass 粒度可接受；若 M5 度量显示可见，补静态 srcloc 快路径宏 |
| Streamline/DLSS 与 Tracy 同时启用的交互 | 未知 | Tracy 不做全局 Vulkan hook，理论无冲突；M2 验收时在开 DLSS 的正常窗口路径跑一次确认（注意 `gnb shot` 会强制禁用 Streamline，验证不了这条） |

## 验证方式汇总

- **构建**：`gnb build --reconfigure`（Windows，ON/OFF 两种）、`gnb android relwithdebinfo`、
  macOS/Linux 各一次；大改动才需要 `--all`。
- **单测**：`gkNextUnitTests`，新增 composite backend 与 scope-tree 用例。
- **渲染无回归**：`gnb shot --scene assets/models/playground.glb`，与 M0 前的图对比。
- **交互**：Tracy GUI 手工连接（桌面 + Android 真机），断开重连各一次。
- **性能**：M3 前后各跑一次 `gkNextMotionBenchmark`，确认 `hwquery` stall 消失且帧时间不回退。

## 待确认的决策点

以下三项按上文推荐值执行；用户若有不同偏好，在 M0 开工前提出即可，都不影响后续里程碑结构：

1. `GK_ENABLE_TRACY` 默认值 —— 推荐 **dev 默认 ON**（release 显式 OFF）。若倾向"默认 OFF、需要时
   `gnb build --tracy`"，M0 只需改一行默认值与 gnb 的一个 flag。
2. Tracy GUI 的获取方式 —— 推荐 **`gnb tracy fetch` 下官方预编译包**。若坚持全部走 vcpkg（`gui-tools`），
   代价是首次 setup 显著变慢并引入第二份 imgui/capstone/glfw。
3. Superluminal 的去留 —— 本计划保留现状。Tracy 落地后 Superluminal 的增量主要是 Windows 采样剖析；
   若之后确认不再使用，删除它是一次独立且干净的清理（`CoreMinimal.hpp:68-156` + 两处 CMake + 一处线程命名）。
