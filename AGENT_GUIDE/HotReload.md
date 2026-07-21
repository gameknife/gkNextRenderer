# Hot Reload Cookbook (C++ / Slang)

> **状态：** 当前保留 TypeScript 与 Slang shader hot reload，并可在 Windows 开发构建中选择启用 Live++ C++ 函数级 live coding。旧 C++ game plugin / DLL hot reload 仍已移除，桌面程序保持 monolithic executable + static `gkNextEngine` 架构。
>
> Shader reload 走增量 Slang 编译 + renderer pipeline 重建；C++ live coding 直接 patch 已链接进 executable 的静态库 object。不要按旧计划里的 `ShaderRegistry` 或 game plugin ABI 假设写代码。

---

## 总览

| 链路 | 入口 | 触发 | 当前行为 |
|---|---|---|---|
| TypeScript -> QuickJS | `Modules/NextQuickJS/QuickJSEngine::TickHotReload()` | 安装模块且启用配置后 0.5 s 轮询 | 失败保留旧脚本 |
| Slang -> SPIR-V -> Vulkan pipeline | `Modules::LiveCoding::ShaderHotReloader` | 安装 `LiveCoding` 模块后 0.5 s 默认轮询或 Editor 手动触发 | 编译变更 `.slang`，`common` 变更触发全量重编，成功后 `VulkanBaseRenderer::ReloadShaders()` |
| C++ -> Live++ patch | `Modules::LiveCoding::CppLiveCoding` | Windows 开发构建显式启用后，按 `Ctrl+Alt+F11` 或 Broker 的 `Tools -> Hot-Reload changes` | Broker 后台编译，主线程在下一帧开始前同步应用 patch |
| Editor/CVar | `Hot Reload` 面板 + CVar console | 手动 | 可开关 shader reload，调整轮询间隔，手动触发 shader rebuild |

移动端（Android/iOS）不启用 shader 或 C++ live coding 路径。未链接并安装 `NextQuickJS` 的 program 也不启用 TypeScript 热重载。

---

## 构建与启动

普通 preset 仍然构建普通 executable，不构建 shared engine、host 或 game plugin。Slang hot reload 默认开启；Live++ 默认关闭。

### 启用 Live++

Live++ SDK 不提交到仓库。`GK_LIVEPP_ROOT` 可以指向包含 `API/`、`Agent/` 的 `LivePP` 目录，也可以指向它的父目录。推荐直接使用独立的 No-Unity preset，避免 Live++ 首次拆分 unity bucket：

```powershell
cmake --preset windows-no-unity `
  -DGK_ENABLE_CPP_LIVE_CODING=ON `
  -DGK_LIVEPP_ROOT=P:/tools/LPP_2_11_3
cmake --build --preset windows-no-unity --target gkNextRenderer --parallel
./out/build/windows-no-unity/bin/gkNextRenderer.exe
```

也可以在首次 configure 前设置 `LIVEPP_ROOT` 环境变量。构建开关会验证 SDK header、x64 Agent、LTO 设置，并向 first-party targets 增加 Live++ 所需的 `/Zi /Gm- /Gy /Gw`；链接 LiveCoding module 的 executable 额外使用 `/FUNCTIONPADMIN /DEBUG:FULL /INCREMENTAL /OPT:NOREF /OPT:NOICF`。

启动后应看到：

```text
[LiveCoding] Live++ Broker connected
[LiveCoding] C++ Live++ enabled for [gkNextRenderer.exe] ...
```

修改受支持的 `.cpp` 函数体并保存，然后按 `Ctrl+Alt+F11`。如果修改发生在当前调用栈上，新实现要等函数退出并再次进入后才生效。若使用普通 Unity preset，第一次修改 unity bucket 时 Live++ 会先拆分 unity file，通常更慢且更容易暴露隐式 include；推荐日常 Live++ 开发使用 `windows-no-unity`。

### No-Unity 独立编译检查

Live++ 拆分 unity bucket 后，每个 `.cpp` 必须能够作为独立 translation unit 编译。仓库提供独立输出目录的 `windows-no-unity` preset，用于提前发现被 unity include 顺序掩盖的头文件依赖：

```powershell
cmake --preset windows-no-unity
cmake --build --preset windows-no-unity --parallel
```

如需在完全关闭 Unity Build 的配置中直接使用 Live++：

```powershell
cmake --preset windows-no-unity `
  -DGK_ENABLE_CPP_LIVE_CODING=ON `
  -DGK_LIVEPP_ROOT=P:/tools/LPP_2_11_3
cmake --build --preset windows-no-unity --target gkNextRenderer --parallel
```

该 preset 使用 `out/build/windows-no-unity`，不会覆盖日常 `out/build/windows` 配置。`.github/workflows/no-unity.yml` 每周一执行一次 Windows 全目标 No-Unity 构建，也支持手动触发。CI 只检查独立 TU 编译，不启用需要本地商业 SDK 的 Live++ Agent。

常用运行时开关：

```powershell
--no-hot-reload
--no-shader-hotreload
--shader-hotreload-interval=0.5
--no-cpp-live-coding
```

`gnb shot` / `gnb validate` 使用的 `--agent-validation` 会自动禁用 C++ live coding，避免 Broker 和 patch 时机影响确定性验证。

### C++ 支持边界

首期只正式支持修改已有 `.cpp` 中的非内联函数体：算法、分支、字面量、局部自动变量和对已有 API 的调用。Engine、Gameplay、Application 和静态 Modules 中的 object 都可由最终 executable 的 PDB 定位。

以下修改完成后必须停止程序，正常 build 并重启：

- `.hpp/.h`、inline、template、`constexpr` 和函数签名。
- class/struct 字段、字段顺序、继承关系、virtual function 或 vtable。
- `REFLECT_COMPONENT`、`RegisterReflection()`、`entt::meta` 注册和 reflected component 布局。
- global、function-static、TLS 对象的初始化和生命周期。
- module `Install()`、callback/registry 所有权、线程启动停止。
- 新源文件、CMake、依赖、编译选项和第三方代码。

不要启用 Live++ 的结构迁移 hook 来绕过以上边界；本项目没有 Unreal 式对象 reinstancing。历史可行性评估已归档，当前支持范围以本节和 `CppLiveCodingService` 为准。

热重载开关和轮询间隔属于 `Runtime::Config::Options`，不是 CVar；Editor 面板直接修改这两个 option。

---

## Shader 热重载

实现文件：

- `src/Modules/LiveCoding/ShaderHotReloader.hpp`
- `src/Modules/LiveCoding/ShaderHotReloader.cpp`
- `src/Modules/LiveCoding/LiveCodingModule.cpp`
- `src/Engine/Rendering/VulkanBaseRenderer::ReloadShaders()`

当前设计：

- source root 优先使用 `GK_NEXT_SOURCE_DIR/assets/shaders`。
- output root 使用运行时 assets 路径：`out/build/<preset>/assets/shaders`。
- bundled `slangc` 由 CMake 复制到 `out/build/<preset>/tools/slang`。
- app 入口先调用 `Modules::LiveCoding::Install(engine)`，由模块向 core 注入 factory。
- watcher 扫描 `assets/shaders/**/*.slang`。
- 普通 shader 比对应 `.spv` 新时重编。
- `assets/shaders/common` 下文件更新时，触发所有 source shader 重编。
- 全部编译成功后调用 `renderer.ReloadShaders()`，内部走 `WaitIdle -> DeleteSwapChain -> CreateSwapChain`。
- 任一 shader 编译失败时保留旧 SPIR-V 和旧 pipeline，并打印 `[HotReload] slangc failed...`。

当前没有 per-pipeline `ShaderRegistry`。这是有意的保守实现：管线依赖关系还没有统一登记，先使用全量 swapchain/pipeline 重建保证正确性。

---

## Editor 面板

`gkNextEditor` 的 Tools 菜单有 `Hot Reload` 面板：

- 开关 shader hot reload option
- 调整 shader 轮询间隔
- 用魔棒图标手动重编 shader
- 用锤子图标请求 Live++ 编译 C++ 变更（Agent 不可用时禁用）
- 显示 shader source/output/slangc 路径

面板文件：`src/Application/Editor/gkNextEditor/Panels/HotReloadPanel.cpp`。

---

## 架构约束

- 不构建 `gkNextEngineShared`、`gkNextHost`、`FlappyCppPlugin` 等 C++ hot reload 目标。
- 不在 `NextEngine` 生命周期中引入 plugin loader、shadow copy、ABI 版本检查或跨 DLL 状态保存。
- Live++ 只 enable 当前主 executable，不自动 enable import DLL；静态 Engine / Module object 由 executable 的 PDB 覆盖。
- C++ patch 由 Synchronized Agent 在 `SDL_AppIterate` 调用 `NextEngine::Tick()` 前应用。这个边界不会自动暂停 engine worker；不要 live patch 正在长时间 worker 栈上执行的函数。
- FlappyCpp 和其他桌面游戏继续走 `CreateGameInstance()` 静态链接路径。
