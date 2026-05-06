# C++ & Slang 热编译/热加载开发计划

> **作者：** 调研 Agent · 2026-05-07
> **范围：** `src/Application/<Game>/`（游戏逻辑层，可热替换的核心目标）+ `assets/shaders/*.slang`（GPU 管线）+ 必要的 `gkNextEngine` 改造点
> **目的：** 在已落地的 TypeScript 热重载（`QuickJSEngine::TickHotReload`）之外，让大部分 C++ 游戏代码与 Slang 着色器代码在运行时修改后立即生效，缩短迭代循环到秒级。
> **执行约定：** 单 PR 可独立交付，按阶段 H0 → H4 推进；每个阶段都给出验收命令；除非显式标记，否则只在 `default-*` 桌面 preset 上启用，移动端不引入。

---

## TL;DR

| 阶段 | 目标 | 主方案 | 启用平台 | 风险 |
|---|---|---|---|---|
| **H0** | 背景与契约梳理 | 文档 + 接口审计 | 全平台 | 低 |
| **H1** | Slang shader 热编译 + 管线重建 | `slangc` + 文件 watcher + 管线 invalidate | Win/Linux/macOS 桌面 | 低（已有离线流水线） |
| **H2** | C++ 游戏模块 DLL 热重载 | "Game-as-Plugin" 模式：把 `src/Application/<Game>` 编译为共享库，宿主 watch + reload | Win/Linux/macOS 桌面 | 中（状态分层、ABI 边界） |
| **H3** | 函数级 in-place patch（可选） | Blink（Win）/ Live++ 试评估 / RCC++ POC | Win 优先 | 中-高，仅作为加速器 |
| **H4** | 编辑器集成 + Hot Reload UI | `gkNextEditor` 面板 + CVar + Trace | Win/Linux/macOS 桌面 | 低 |

---

## 1. 现状盘点

### 1.1 已有热重载基线（TypeScript）

- `src/Runtime/Subsystems/QuickJSEngine.cpp:2312` 实现 `TickHotReload`：每 0.5 s 轮询，调用打包好的 `tools/tsc/tsc[.exe]` 把 `assets/typescript/` 编译到 `assets/scripts/`，输出比 `.tsc.stamp` 新就 `ResetContextAndLoadScript()`。
- 设计要点（C++ 方案需要复刻）：
  1. 编译器是项目自带工具，不依赖用户全局环境。
  2. 增量判断走文件 mtime。
  3. 失败 = 不切换，旧脚本继续跑，主循环不中断。
  4. 移动端禁用（`#if ANDROID || IOS`）。

### 1.2 Shader 离线流水线

- `assets/CMakeLists.txt:76-113` 用 `add_custom_command` 调用 `Vulkan_SLANGC` 把 `assets/shaders/*.{comp,vert,frag}.slang` 编译成 `.spv`，放进 `out/build/<preset>/bin/assets/shaders/`。
- 共享头：`assets/shaders/common/*.slang` 通过 CMake `DEPENDS` 触发依赖文件全量重编。
- 运行时由 `Vulkan::ShaderModule(device, "assets/shaders/Foo.comp.slang.spv")` 读 SPIR-V（`src/Vulkan/MemoryAndShader.hpp:92`）。
- 管线持有方：
  - `Rendering/PipelineCommon/CommonComputePipeline.{hpp,cpp}` 提供 `ZeroBindPipeline` / `ZeroBindCustomPushConstantPipeline`，构造时传入着色器路径，析构时释放 `VkPipeline` + `PipelineLayout` + `DescriptorSetManager`。
  - `Rendering/{PathTracing, SoftwareTracing, SoftwareModern}/*Renderer.cpp` 在初始化时 `pipeline.reset(new ...)`。
  - 需要重建管线时已有先例：`VulkanBaseRenderer::RecreateSwapChain()`（`VulkanBaseRenderer.cpp:682`）走 `WaitIdle → DeleteSwapChain → CreateSwapChain` 全量重建。

### 1.3 游戏模块结构

- `src/CMakeLists.txt:85-156` 每个游戏（`gkNextRenderer`、`Brotato3D`、`KongLie3D`、`MagicaLego`、`FlappyCpp`、`FlappyJs`、`BrickPlayer`、`CharacterDemo`、`Voyage3D`、`gkNextVisualTest`、`gkNextBenchmark`）都是 **独立 executable**，链接静态库 `gkNextEngine`。
- 每个游戏通过自由函数 `extern std::unique_ptr<NextGameInstanceBase> CreateGameInstance(...)` 注入到引擎（`Engine.hpp:112`）。
- 引擎在 `NextEngine::Start()` 调用 `gameInstance_ = CreateGameInstance(...)`（`Engine.cpp:272`），后续生命周期钩子 `OnInit / OnTick / OnDestroy / OnRenderUI / BeforeSceneRebuild / OnSceneLoaded / OnKey / OnMouseButton / OnGamepadInput / OverrideRenderCamera` 全部走 `gameInstance_->...`。
- 这是一个 **天然的 v-table 边界**：游戏侧没有静态全局状态被引擎直接读写，所有交互都过 `NextGameInstanceBase` 或 `NextEngine` 公共 API。该边界正是 DLL 热重载的最佳切线。

---

## 2. 技术调研：主流 C++ 热重载方案

下表只列在桌面端可用、维护活跃、规模可控的方案。重内核 LLVM REPL（Cling、cppyy、clang-repl）因依赖 200+ MB LLVM 运行时与 GPL/LGPL 风险，不在本计划候选内。

| 方案 | 原理 | 平台 | 集成成本 | 限制 | 许可 |
|---|---|---|---|---|---|
| **DIY Plugin DLL Reload**（"Handmade Hero" 模式） | 游戏逻辑编译为 `.dll/.so/.dylib`，宿主 watch 文件→`FreeLibrary`→拷贝→`LoadLibrary`→重建 GameInstance | 全桌面 | 低（仅需 CMake + 一段 loader） | ABI 必须只通过纯虚 + POD；DLL 内不能持久化非 POD 状态；vtable 地址变化需要保护 | 自写 |
| **[Blink](https://github.com/crosire/blink)** | 读 PDB diff，在已加载 PE 镜像里替换函数体（call-site 改写） | **Windows only** | 极低（无侵入，`blink.exe game.exe`） | 不支持改类布局/虚表/全局变量类型；只能改函数体；MSVC `/Z7 /Od` 推荐 | BSD-3 |
| **[RuntimeCompiledCPlusPlus (RCC++)](https://github.com/RuntimeCompiledCPlusPlus/RuntimeCompiledCPlusPlus)** | 通过 `IObject` 基类做对象交换；监听单个 `.cpp`，调用 cl/clang 编成临时 dll，把旧实例数据拷到新实例 | Win/Linux/macOS | 中（需要把热替换类继承 `IObject` 并注册 `RUNTIME_MODIFIABLE_INTERFACE`） | 引入侵入式基类；要求所有可热替换类型走 RCC++ 工厂；C++20 module/概念兼容性需自测 | Zlib |
| **[Jet Live](https://github.com/ddovod/jet-live)** | ELF/Mach-O 动态符号 patcher：watch 源文件 → 调 cmake 增量构建 → 把新 `.o` 的符号映射进运行进程 | Linux/macOS | 中（CMake 注入 + 启动时 attach） | 当前不支持 Windows（PDB 路径单独）；社区维护，提交频率不高；需 `-fPIC -fno-omit-frame-pointer` 等编译开关 | MIT |
| **Live++**（Molecular Matters，商业） | PDB-based 二进制 patch + 全量替换工具链支持 | Win/PS5/Xbox/Linux | 低-中（链接 lib + `lpp::lppCreateAgent`） | 收费（席位制）；闭源；Linux 端依赖 DWARF；与 vcpkg 集成需手工 | 商业 |

### 评估结论

- **能力曲线** 由低到高：Blink（仅函数体）< Plugin DLL（整模块替换）< RCC++/Jet Live（对象级替换）< Live++（接近 IDE 改即生效）。
- **侵入性** 由小到大：Blink ≪ Plugin DLL ≪ RCC++ ≪ Jet Live ≪ Live++。
- **"轻量化"诉求最佳平衡点：Plugin DLL Reload。**
  - 仓库已经按"游戏 = 独立可执行"组织，只要把 executable 切成 host + plugin shared lib，零外部依赖就能跑通。
  - 能覆盖 90% 的玩法迭代（参数、关卡、AI、UI 文案、动画时长）。
  - 与 TS 热重载形态一致：宿主 watch → 编译器（CMake/MSBuild/ninja）→ 输出新文件 → 重建实例。
- **加速器：Blink** 作为 Windows-only 的可选附加层，让"改一个函数"不必走完整 DLL 重链；它**不是必需**，但接入成本极低（一条命令行），值得做 H3 评估。
- **不选择的方案：**
  - RCC++ 与 Jet Live 都强制把所有可热替换类型纳入它们的工厂体系，会跟现有 entt/`Assets::Component` 反射体系冲突，迁移成本高。
  - Live++ 收费且不开源，与本项目"vcpkg 自举、零商业依赖"的工程哲学冲突，建议仅在生产团队需要时单独评估。

---

## 3. 推荐方案

### 3.1 总体架构

```
┌──────────────────────────────────────────────────────────────────┐
│  host_executable (e.g. gkNextHost.exe)                           │
│  ─────────────────────────────────────────────                   │
│  • DesktopMain.cpp (SDL_AppInit/Iterate/Quit)                    │
│  • gkNextEngine static lib (Vulkan, ECS, scene, QuickJS, audio)  │
│  • PluginLoader (this plan, H2)                                  │
│  • ShaderHotReloader (this plan, H1)                             │
└────────────────────────────────────────┬─────────────────────────┘
                                         │ dlopen / LoadLibrary
                                         ▼
                           ┌──────────────────────────────┐
                           │ <Game>.dll / .so / .dylib    │
                           │ ─────────────────────────    │
                           │ • CreateGameInstance()       │
                           │ • DestroyGameInstance()      │
                           │ • <Game>GameInstance (vtbl)  │
                           └──────────────────────────────┘
```

- **Plugin DLL** 内只放游戏逻辑（`src/Application/<Game>`）和私有数据；不含 Vulkan、Scene、引擎子系统的实现，仅链接它们的头与导入符号。
- **Host** 持有所有 Vulkan 资源、Scene、ECS world、CVar 系统、Audio、Input；游戏只通过 `NextEngine*` 与 `Assets::Scene*` 这类已有指针访问。
- **状态边界规则**（H2 必须遵守）：
  1. 所有要跨 reload 保留的状态 **必须存在于 host**（即 `gkNextEngine` 静态库或 host 主控对象里）。
  2. Plugin 里的对象寿命 ≤ 一次 reload 周期；reload 时把"业务状态"序列化成 JSON / blob 由 host 暂存，新实例 `OnInit` 后再恢复（H2.5 任务）。
  3. Plugin 不允许在 `extern "C"` 边界外暴露内联模板/STL 容器对象，避免 MSVC 与 libc++ 不同 STL 实例之间的 ODR 冲突。

### 3.2 Slang 热重载（H1）

```
.slang change → ShaderHotReloader::Tick()
              → slangc <file> -o <stage>.spv
              → 比对 mtime 找出受影响 ShaderModule
              → device.WaitIdle()
              → 重建 ShaderModule、PipelineLayout、Pipeline（按持有方分类）
              → 下一帧用新管线
```

- 关键点：把现在散在 `*Renderer.cpp` 里的 `pipeline.reset(new ...(... shaderfile))` 路径**登记到一个集中的 ShaderRegistry**，让 watcher 知道"shader X 变了 → 这些 Pipeline 要 invalidate"。
- 失败回退：`slangc` 退出码非零 → 保留旧 SPIR-V + 旧管线，`SPDLOG_WARN` 打印 stderr，**不**让玩家看到崩溃。

---

## 4. 阶段化路线图

### H0 · 接口审计与契约固化（前置）

**目标：** 让 H1/H2 可以独立 PR 推进，不与历史代码相互绊脚。

| 任务 | 输出 | 验收 |
|---|---|---|
| H0.1 整理 `NextGameInstanceBase` 当前所有虚函数 + 成员可见性，落到 `AGENT_GUIDE/HotReload.md`（H4 阶段创建） | 接口表 | 文档 PR |
| H0.2 在 `NextEngine` 找出 plugin DLL 不能直接持有的子系统指针（Audio/Renderer/Scene/CVar/Physics），列出"可序列化"和"不可序列化"的成员 | 边界清单 | 文档 PR |
| H0.3 列出现有所有管线对象 → 着色器路径的映射；标记每个管线属于哪种生命周期（swapchain-bound、scene-bound、永久） | 表格 | 文档 PR |

> 这一步纯调研无代码改动，可由文档 Agent 独立完成，**它的产出物即 H1/H2 的输入**。

---

### H1 · Slang 着色器热重载（P0，先做）

**输入：** 现有 `Vulkan_SLANGC`、`assets/CMakeLists.txt:76-113`、`Vulkan::ShaderModule`、`PipelineBase`。

#### H1.1 ShaderRegistry：登记着色器与管线的依赖关系

新增 `src/Vulkan/ShaderRegistry.{hpp,cpp}`：

```cpp
namespace Vulkan
{
    class ShaderRegistry final
    {
    public:
        using PipelineRebuildFn = std::function<void()>;

        // 由 PipelineBase 子类构造时调用：声明本管线依赖的 .slang 源
        void Register(const std::string& slangSourcePath,
                      const std::string& spvPath,
                      PipelineRebuildFn rebuild);

        void Unregister(const std::string& spvPath); // 析构时

        // 由 ShaderHotReloader 调用
        void NotifySpvUpdated(const std::string& spvPath);

    private:
        struct FEntry { std::string slangSource; std::vector<PipelineRebuildFn> rebuilders; };
        std::unordered_map<std::string, FEntry> bySpv_;
    };
}
```

- `PipelineBase` 子类（如 `ZeroBindPipeline`）在构造时记录 `(slangPath, spvPath, [this]{ /* recreate pipeline */ })`，析构时反注册。
- `gkNextEngine` 持有单例 `ShaderRegistry`（放进 `NextEngine` 或 `Vulkan::Device` 旁边）。

**验收：** 打开 `gkNextRenderer`，启动后从 registry dump 出 ~30 条登记记录，与 `*Renderer.cpp` 实际构造数一致。

#### H1.2 ShaderHotReloader：watcher + slangc 调用 + invalidate

新增 `src/Vulkan/ShaderHotReloader.{hpp,cpp}`：

- `Initialize(Device&, ShaderRegistry&, sourceRoot, outputRoot)`。
- `Tick(double dt)`：每 0.5 s 扫一遍 `assets/shaders/**/*.slang` 的 mtime，比 `outputRoot/<file>.spv` 新就：
  1. 把 `slangc` 路径在启动时缓存（同 `QuickJSEngine::ResolveBundledTscExecutable` 做法，CMake 把 slangc.exe 拷到 `out/build/<preset>/tools/slang/` 或直接走 `Vulkan_SLANGC`）。
  2. 子进程调用 `slangc <slang> -o <spv> -entry main -target spirv [defines]`。
  3. 退出码 0 → `device.WaitIdle()` → 把 `bySpv_[spvPath].rebuilders` 全部跑一遍。
  4. 退出码非零 → spdlog warn + 保留旧 spv。
- `*.slang` 共享头（`shaders/common/*.slang`）改动：触发依赖它的所有目标重编（先全量重编简单稳妥；后续可上 dep cache）。

**集成点：** 在 `NextEngine::Tick()` 早期、`gameInstance_->OnTick` 之前调用 `shaderHotReloader_->Tick(dt)`。

**CMake：** 在 `assets/CMakeLists.txt` 增加把 `Vulkan_SLANGC` 拷贝到 `out/build/<preset>/tools/slang/` 的自定义命令（仿 `tsc-tool` 段），保证打包后的运行时也能 reload。

**验收：**
1. 修改 `assets/shaders/Core.PathTracing.comp.slang` 任意一行，2 秒内日志输出 `Shader rebuilt: ...`，画面更新。
2. 改一个共享头 `Common.slang` 中的 `#define`，所有依赖管线都重建（用 `RenderDoc` 抓帧验证）。
3. 故意写错语法 → 打印编译错误 → 旧画面继续，无崩溃。
4. 用 `--no-shader-hotreload` CLI flag 关掉时，与改造前完全等价。

**风险：** descriptor set 复用、PipelineCache 失效。先做 `WaitIdle` 全量重建，性能不是瓶颈（迭代时机才触发）。

---

### H2 · C++ 游戏模块 DLL 热重载（P0，主菜）

#### H2.1 CMake 拆分：游戏 = 共享库

- 引入 CMake 选项 `GK_ENABLE_HOT_RELOAD`（默认在 `default-*` / `full-*` 桌面 preset 上 ON，移动端强制 OFF）。
- 当 `GK_ENABLE_HOT_RELOAD=ON` 时：
  - `add_executable(<Game> ...)` → `add_library(<Game>Plugin SHARED ...)`。
  - 新增统一宿主 `add_executable(gkNextHost DesktopMain.cpp src/Runtime/Plugin/PluginLoader.cpp)`。
  - 通过 `--game=<name>` CLI 选择启动哪个 plugin（host 启动时映射到 `<bin>/<Name>Plugin.<dll|so|dylib>`）。
- 当 `GK_ENABLE_HOT_RELOAD=OFF`：保留现有 executable 行为（移动端、CI、Release 包）。
- 共享库导出 C 入口：

```cpp
// 每个 plugin 的 GameInstance.cpp 末尾追加
extern "C" GK_PLUGIN_EXPORT NextGameInstanceBase* gkCreateGameInstance(
    Vulkan::WindowConfig* config, Options* options, NextEngine* engine);
extern "C" GK_PLUGIN_EXPORT void gkDestroyGameInstance(NextGameInstanceBase* instance);
extern "C" GK_PLUGIN_EXPORT uint32_t gkPluginAbiVersion();
```

`GK_PLUGIN_EXPORT` 在 `Common/PluginExport.hpp` 里按平台展开 `__declspec(dllexport)` / `__attribute__((visibility("default")))`。

**验收：** Win/Linux/macOS 上 `cmake --build out/build/<preset> --target FlappyCppPlugin` 能产出共享库；`gkNextHost --game=FlappyCpp` 行为与现有 `FlappyCpp.exe` 一致（`--flappy-replay` trace 哈希相同）。

#### H2.2 PluginLoader：加载、卸载、重载

新增 `src/Runtime/Plugin/PluginLoader.{hpp,cpp}`：

```cpp
class PluginLoader final
{
public:
    bool Load(const std::filesystem::path& dllPath);  // 拷贝到临时副本再 LoadLibrary，避免占用源文件
    void Unload();
    bool Tick(double dt);  // 检测 mtime 变化，若变化则触发 Reload
    NextGameInstanceBase* Create(Vulkan::WindowConfig& cfg, Options& opt, NextEngine* eng);
    void Destroy(NextGameInstanceBase* inst);
private:
    using CreateFn = NextGameInstanceBase* (*)(Vulkan::WindowConfig*, Options*, NextEngine*);
    using DestroyFn = void (*)(NextGameInstanceBase*);
    using AbiVersionFn = uint32_t (*)();

    void* handle_ = nullptr;
    CreateFn create_ = nullptr;
    DestroyFn destroy_ = nullptr;
    std::filesystem::path sourcePath_;
    std::filesystem::path shadowPath_;
    std::filesystem::file_time_type lastWriteTime_{};
};
```

实现要点：
- **影子拷贝**：把 `<Name>Plugin.dll` 拷到 `<bin>/_hot/<Name>.<pid>.<n>.dll` 再加载。Windows 锁住正在加载的 DLL，影子拷贝才能让 ninja/MSBuild 同步覆盖原文件。
- **卸载顺序**：`Destroy(instance) → FreeLibrary → 删旧影子文件`。
- **ABI 版本号** `gkPluginAbiVersion()` 返回一个由 `gkNextEngine` 头里的 `GK_ENGINE_ABI_VERSION` 宏拼出来的整数，host 加载时校验，不匹配直接拒绝并提示用户重启。

#### H2.3 触发与重启：Reload Flow

- `NextEngine::Tick()` 进入：`pluginLoader_.Tick(dt)`。
- 检测到 DLL 变更：
  1. 调用游戏当前的 `OnSceneUnloaded`（已经存在的钩子）。
  2. **状态保留**：调用新增的 `gameInstance_->SaveHotReloadState(json)`（默认空实现，游戏可选 override）。
  3. `Destroy(instance)` → `FreeLibrary` → `Load(newPath)` → `Create(...)` → `LoadHotReloadState(json)` → `OnInit()` → `OnSceneLoaded()`。
  4. 不重新创建 Vulkan 设备、Scene、CVar、Physics、Audio。
- 失败回退：新 DLL 加载失败或 ABI 不匹配 → 保留旧实例继续运行，UI 弹窗提示。

#### H2.4 文件 watcher 与构建触发

- **被动模式**（默认）：watcher 只监控 `<bin>/<Name>Plugin.<ext>` 的 mtime。开发者自己在 IDE 里按 Ctrl+B / `cmake --build --target <Name>Plugin`，写盘后 host 自动 pickup。
- **主动模式**（可选 H2.6）：watcher 监控 `src/Application/<Name>/**.cpp` 的 mtime → 启子进程 `cmake --build out/build/<preset> --target <Name>Plugin`。仅在 dev preset 启用，避免 CI 误触。

#### H2.5 状态序列化辅助

新增 `Runtime/Plugin/HotReloadState.{hpp,cpp}`，提供：

```cpp
class FHotReloadState
{
public:
    template <typename T> void Set(std::string_view key, const T& value);   // 走 nlohmann/json
    template <typename T> bool Get(std::string_view key, T& outValue) const;
    nlohmann::json& Raw();
};
```

`NextGameInstanceBase` 增加：

```cpp
virtual void SaveHotReloadState(FHotReloadState& state) const {}
virtual void LoadHotReloadState(const FHotReloadState& state) {}
```

游戏侧用法（以 FlappyCpp 为例）：

```cpp
void FlappyCppGameInstance::SaveHotReloadState(FHotReloadState& s) const {
    s.Set("score", score_);
    s.Set("birdY", bird_.Position().y);
    s.Set("state", static_cast<int>(state_));
}
void FlappyCppGameInstance::LoadHotReloadState(const FHotReloadState& s) {
    s.Get("score", score_);
    glm::vec3 pos = bird_.Position(); s.Get("birdY", pos.y); bird_.SetPosition(pos);
    int st{}; if (s.Get("state", st)) state_ = static_cast<EGameState>(st);
}
```

**验收：**
1. `gkNextHost --game=FlappyCpp` 启动后游玩中，改 `FlappyConfig.cpp` 的重力常数，`cmake --build --target FlappyCppPlugin`；写盘后 1 秒内画面行为变化，分数与位置不丢。
2. 改 `FlappyCppGameInstance.cpp` 的 `OnTick` 加一行 spdlog → reload 后立刻看到日志输出。
3. 改一个**新增字段**到 `FlappyCppGameInstance`（更改类布局）→ ABI 版本不变情况下 reload 成功（因为 host 不持有 plugin 内部对象的旧布局指针）。
4. 改 `NextGameInstanceBase` 接口（增加纯虚方法）→ ABI version 自动 bump，host 拒绝旧 plugin 并打印对齐建议。
5. `--no-hot-reload` CLI flag 关掉时表现与 statically-linked 版本一致。

#### H2.6（可选） 主动构建触发

如果 H2.5 验收通过，再增加：开发模式下 watcher 直接调用 cmake 子进程，参考 `QuickJSEngine::CompileTypeScriptSources` 实现 `BuildPluginAsync()`。同样保留失败回退。

---

### H3 · 函数级 in-place patch（P2，可选加速器）

只评估、不强推。在 H2 稳定后做一周 spike：

- **Blink 集成**（Win 优先）：写一个 `tools/blink/`，提供 `blink_attach.bat <pid>`。Blink 不需要任何代码改动，但要把 `gkNextHost` 编译加 `/Z7 /Od` 的 dev 配置（CMake 增 `default-windows-fastdev` preset）。
- **POC 标的**：在 Brotato3D 改一个 enemy AI 的 `MoveTowards` 函数体，blink 直接 patch 而无须 reload DLL。
- **决策点**：如果"改函数体"占迭代场景 > 50%，再扩大投入。否则停在评估阶段，把这条作为可选工具记录在 `AGENT_GUIDE/HotReload.md`。

---

### H4 · 编辑器集成与可观测性（P1）

- `gkNextEditor` 新增 **Hot Reload** 面板（`src/Editor/Panels/HotReloadPanel.{hpp,cpp}`）：
  - 列表：所有受监控的 `.slang` 与 plugin DLL，最后修改时间、最后 reload 结果。
  - 按钮：手动 trigger reload、清空 shader cache、查看 slangc/cmake stderr。
- CVar：
  - `r.shader.hot_reload`（bool, 默认 1）
  - `r.shader.hot_reload_interval`（float, 秒, 默认 0.5）
  - `g.plugin.hot_reload`（bool, 默认 1）
  - `g.plugin.auto_build`（bool, 默认 0）
- spdlog 日志统一前缀 `[HotReload]`。
- `Trace::Frame` 中输出 reload 耗时（已有 `spdlog::stopwatch` 模式可参考 `QuickJSEngine::CompileTypeScriptSources`）。

---

## 5. 给后续 Agent 的实现指引

### 5.1 文件落点速查

| 模块 | 新文件 |
|---|---|
| Shader registry | `src/Vulkan/ShaderRegistry.{hpp,cpp}` |
| Shader hot reloader | `src/Vulkan/ShaderHotReloader.{hpp,cpp}` |
| Plugin export 宏 | `src/Common/PluginExport.hpp` |
| Plugin loader | `src/Runtime/Plugin/PluginLoader.{hpp,cpp}` |
| Hot reload state 工具 | `src/Runtime/Plugin/HotReloadState.{hpp,cpp}` |
| Host 入口 | `src/HostMain.cpp`（替代 `DesktopMain.cpp` 在 host 目标里的角色） |
| Editor panel | `src/Editor/Panels/HotReloadPanel.{hpp,cpp}` |
| 长期参考 | `AGENT_GUIDE/HotReload.md`（H4 创建） |

### 5.2 命名与代码风格

- 严格遵循 `AGENTS.md` "Code Style" 一节：类型 PascalCase、成员 `camelCase_`、第一个 include 必须是 `Common/CoreMinimal.hpp`。
- 平台分支用 `#if WIN32` / `#if __APPLE__` / `#if __linux__`，不引入 `#ifdef _WIN32` 风格；Plugin 平台扩展集中在 `PluginExport.hpp` 一处。
- 不写多行注释；只在 ABI 边界、PE 锁定、descriptor pool 失效等"读者会被坑"的地方写**单行** WHY。

### 5.3 建议 PR 拆分

每个 PR 自包含、独立可验收：

1. **PR-H0** 文档：本计划 + `AGENT_GUIDE/HotReload.md` 骨架。
2. **PR-H1.1** `ShaderRegistry` + 把所有 `PipelineBase` 子类改造成构造时登记。**纯重构，行为不变**。
3. **PR-H1.2** `ShaderHotReloader` + CMake 拷贝 slangc 到 `tools/slang/`。**默认开启**。
4. **PR-H2.1** CMake 加 `GK_ENABLE_HOT_RELOAD` 选项 + 把 FlappyCpp 一个游戏拆成 plugin shared lib + 引入 `gkNextHost`。其它游戏暂保留 executable 形态。
5. **PR-H2.2** PluginLoader（被动模式）+ FlappyCpp reload demo + 单测（Catch2）。
6. **PR-H2.3** `SaveHotReloadState`/`LoadHotReloadState` + FlappyCpp 实例覆写 + replay trace 跨 reload 一致性测试。
7. **PR-H2.4** 把 Brotato3D / KongLie3D / MagicaLego / Voyage3D 等剩余游戏迁到 plugin 形态。Voyage3D 与 KongLie3D 由它们的开发者签字。
8. **PR-H2.5**（可选）主动 cmake build 触发。
9. **PR-H3**（可选）Blink dev preset。
10. **PR-H4** Editor panel + CVar + 长期文档定稿。

### 5.4 验收脚本草图

新增 `tools/hotreload/`：
- `bench-shader-reload.ps1` / `.sh`：连续 10 次修改 shader，统计平均 reload 时间，目标 < 800ms。
- `bench-plugin-reload.ps1` / `.sh`：自动改 FlappyCpp 一个浮点常数，触发 reload，验证 trace 哈希。
- `flappy-reload-replay.ps1`：H2.3 验收用，跑两遍 `--flappy-replay` 中间穿插一次 reload，断言 trace 数据一致。

### 5.5 不要做的事

- **不要** 把 Vulkan 资源（ShaderModule、Pipeline、Buffer）放到 plugin DLL 内构造，即使这些类的实现来自 `gkNextEngine`。一旦 host 与 plugin 各自实例化了同一个模板，FreeLibrary 会触发析构 UB。游戏侧持有的应该都是 host 已经创建好的引用/指针。
- **不要** 在 plugin 里使用全局静态对象做单例（spdlog 之类已有 sink 例外，由 gkNextEngine 暴露统一接口）。
- **不要** 把 `entt::registry` 实例放进 plugin；ECS world 留在 host，plugin 通过引用拿到。
- **不要** 在 `assets/shaders/common/*.slang` 改动后只重编单个 spv，必须重编所有依赖；否则会出现 binding offset 不匹配。
- **不要** 在 H1 阶段就尝试做 PipelineCache 复用，先跑通正确性再谈性能。

---

## 6. 风险与限制

| 风险 | 严重度 | 缓解 |
|---|---|---|
| Plugin DLL 持有引擎 STL 对象触发 ODR 违规 | 高 | API 边界纯 C 入口；引擎 STL 对象只通过 host 暴露的非 inline 函数返回引用 |
| `device.WaitIdle()` 卡顿在大场景下肉眼可见 | 中 | dev 模式可见，不进入 release；大场景下 reload 帧可短暂掉到 0 fps，可接受 |
| MSVC Debug iterator 与 plugin Release 不兼容 | 中 | host 与 plugin 强制相同 `CMAKE_BUILD_TYPE`；ABI version 字符串包含 `Debug/Release` 标识 |
| macOS arm64 dlopen 需要签名 | 中 | dev 用 ad-hoc 签名 `codesign -s -`，已有 `tools/prepare.sh` 流程可扩展 |
| Slang 版本升级 SPIR-V 不兼容 | 低 | host 启动时 dump `slangc --version` 到日志；CI 锁版本 |
| 文件 watch 在 Windows 上对网络盘表现不稳 | 低 | mtime 轮询 + 重试；不依赖 ReadDirectoryChangesW |
| Vulkan validation layer 在 reload 帧报 stale handle | 低 | 在 reload 时临时 mute 一帧 validation，或纯走 `WaitIdle` 后再重建 |

---

## 7. 验收标准（最终）

完成 H1 + H2 + H4 后，下述场景全部通过：

1. **Shader 闭环**：在 `default-windows` preset 下启动 `gkNextHost --game=Brotato3D`，编辑 `Core.PathTracing.comp.slang` 的 lighting 常数，**< 1 s** 内画面更新；连续 10 次 reload 不泄漏 GPU 内存（用 `vkmemoryview` 或 RenderDoc 验证）。
2. **C++ 闭环**：在 `gkNextHost --game=FlappyCpp` 游玩中改重力，`cmake --build --target FlappyCppPlugin` 后 **< 2 s** 内 reload 完成；分数、Bird 位置、随机数种子保留；replay trace（`tools/flappy/diff_traces.py`）跨 reload 仍可匹配。
3. **健壮性**：在游玩中给 plugin 引入编译错误，host 不崩溃，UI 弹窗提示构建失败；恢复编译后下一次写盘 reload 成功。
4. **不退化**：移动端（Android/iOS）build 不受影响，shader 仍然走 CMake 离线编译；CI 上 `gkNextUnitTests` 与 `gkNextVisualTest` 全绿。
5. **可关闭**：所有 hot reload 路径在 `--no-hot-reload` / CVar 关闭后零额外开销，行为退化为现状。

---

## 8. 参考文献与外部资源

- Casey Muratori, *Handmade Hero* — Day 21–25：DLL 热重载基本模式（YouTube/Twitch 存档）。
- crosire, [Blink](https://github.com/crosire/blink) — Windows PDB-based 函数体 patch。
- Doug Binks, [RuntimeCompiledCPlusPlus](https://github.com/RuntimeCompiledCPlusPlus/RuntimeCompiledCPlusPlus) — `IObject` 对象交换模型。
- Denys Dovod, [Jet Live](https://github.com/ddovod/jet-live) — Linux/macOS ELF/Mach-O patcher。
- Molecular Matters, [Live++](https://liveplusplus.tech/) — 商业方案，能力上限参考。
- The Khronos Group, *Vulkan Pipeline Cache* spec — 重建管线时的最佳实践，H1.2 可参考但**不在 MVP 范围**。
- 本仓库现有：
  - `src/Runtime/Subsystems/QuickJSEngine.cpp`（TS 热重载实现，H1/H2 watcher 直接复用其轮询模式）
  - `src/Rendering/VulkanBaseRenderer.cpp:682`（`RecreateSwapChain` 全量重建样板）
  - `src/Application/Flappy/`（最简洁的 NextGameInstanceBase 示例，H2 demo 首选）

---

> **下一步：** 由文档 Agent 推进 H0 三个子任务并交付 `AGENT_GUIDE/HotReload.md` 骨架；之后按 PR-H1.1 → PR-H1.2 → PR-H2.1 顺序进入实现。每个 PR 务必带验收数据（reload 平均耗时、trace 哈希、`vkmemoryview` 截图任一）。
