# Hot Reload Cookbook (C++ + Slang)

> **状态：** 已落地基础闭环（2026-05-07）。
> **配套规划：** [docs/plans/2026-05/cpp-shader-hot-reload-plan.md](../docs/plans/2026-05/cpp-shader-hot-reload-plan.md)
>
> 本文档记录当前真实实现。若继续扩展，不要按旧计划里的 `ShaderRegistry` 假设写代码；当前 shader reload 走全量 shader 编译 + renderer pipeline 重建。

---

## 总览

| 链路 | 入口 | 触发 | 当前行为 |
|---|---|---|---|
| TypeScript -> QuickJS | `QuickJSEngine::TickHotReload()` | 0.5 s 轮询 | 已有实现，失败保留旧脚本 |
| Slang -> SPIR-V -> Vulkan pipeline | `Vulkan::ShaderHotReloader` | 0.5 s 默认轮询或 Editor 手动触发 | 编译变更 `.slang`，`common` 变更触发全量重编，成功后 `VulkanBaseRenderer::ReloadShaders()` |
| C++ game plugin reload | `PluginLoader` + `gkNextHost` | 0.5 s 默认轮询或 Editor 手动触发 | Host 加载 `<Game>Plugin.dll` 的 shadow copy，mtime 变化后销毁旧实例、加载新 DLL、恢复状态 |
| Editor/CVar | `Hot Reload` 面板 + CVar console | 手动 | 可开关 plugin/shader reload，调整轮询间隔，手动触发 reload |

移动端（Android/iOS）不启用 C++/shader hot reload 路径。

---

## 构建与启动

full preset 会构建 host、shared engine 和 FlappyCpp plugin：

```powershell
.\build.bat --preset full-windows --reconfigure
```

运行 plugin host：

```powershell
.\out\build\full-windows\bin\gkNextHost.exe --game=FlappyCpp
```

重新构建 plugin 后，host 会在下一次轮询加载新 DLL：

```powershell
cmake --build out/build/full-windows --target FlappyCppPlugin
```

常用开关：

```powershell
--game=FlappyCpp
--no-hot-reload
--no-plugin-hotreload
--no-shader-hotreload
--plugin-hotreload-interval=0.5
--shader-hotreload-interval=0.5
```

对应 CVar：

```text
g.plugin.hot_reload
g.plugin.hot_reload_interval
r.shader.hot_reload
r.shader.hot_reload_interval
```

---

## Shader 热重载

实现文件：

- `src/Vulkan/ShaderHotReloader.hpp`
- `src/Vulkan/ShaderHotReloader.cpp`
- `src/Rendering/VulkanBaseRenderer::ReloadShaders()`

当前设计：

- source root 优先使用 `GK_NEXT_SOURCE_DIR/assets/shaders`。
- output root 使用运行时 assets 路径：`out/build/<preset>/assets/shaders`。
- bundled `slangc` 由 CMake 复制到 `out/build/<preset>/tools/slang`。
- watcher 扫描 `assets/shaders/**/*.slang`。
- 普通 shader 比对应 `.spv` 新时重编。
- `assets/shaders/common` 下文件更新时，触发所有 source shader 重编。
- 全部编译成功后调用 `renderer.ReloadShaders()`，内部走 `WaitIdle -> DeleteSwapChain -> CreateSwapChain`。
- 任一 shader 编译失败时保留旧 SPIR-V 和旧 pipeline，并打印 `[HotReload] slangc failed...`。

当前没有 per-pipeline `ShaderRegistry`。这是有意的保守实现：管线依赖关系还没有统一登记，先使用全量 swapchain/pipeline 重建保证正确性。

---

## C++ Plugin 热重载

实现文件：

- `src/Common/PluginExport.hpp`
- `src/Application/PluginEntry.cpp`
- `src/Runtime/Plugin/PluginLoader.hpp`
- `src/Runtime/Plugin/PluginLoader.cpp`
- `src/Runtime/Plugin/HotReloadState.hpp`
- `src/Runtime/Plugin/HotReloadState.cpp`
- `src/Runtime/Plugin/PluginUi.hpp`
- `src/Runtime/Plugin/PluginUi.cpp`

构建目标：

- `gkNextEngine`：保持 static，不影响现有 executable。
- `gkNextEngineShared`：供 host 和 plugin 共用的 shared engine。
- `gkNextHost`：统一 host，可通过 `--game=<Name>` 加载 plugin。
- `FlappyCppPlugin`：当前已迁移的 C++ game plugin demo。

Plugin 必须导出：

```cpp
extern "C" GK_PLUGIN_EXPORT NextGameInstanceBase* gkCreateGameInstance(
    Vulkan::WindowConfig* config, Options* options, NextEngine* engine);
extern "C" GK_PLUGIN_EXPORT void gkDestroyGameInstance(NextGameInstanceBase* instance);
extern "C" GK_PLUGIN_EXPORT uint32_t gkPluginAbiVersion();
```

加载流程：

1. `gkNextHost --game=FlappyCpp` 解析 `FlappyCppPlugin.dll`。
2. `PluginLoader` 拷贝到 `bin/_hot/<plugin>.<pid>.<counter>.dll`。
3. Windows 下加载 shadow copy 前临时把 DLL search path 指向 `bin`，确保能找到 `gkNextEngineShared.dll`。
4. 校验 `gkPluginAbiVersion() == GetEngineHotReloadAbiVersion()`，该值包含固定 ABI 版本、关键类大小与 Debug/Release 标识。
5. 调用 `gkCreateGameInstance()` 创建游戏实例。

Windows full preset 使用静态 SDL。`gkNextHost.exe` 与 `gkNextEngineShared.dll` 会各自带一份 SDL 状态；窗口由 shared engine 创建，因此 shared engine 目标带 `GK_ENGINE_OWNS_SDL_EVENT_PUMP=1`，在 `NextEngine::Tick()` 内用同一份 SDL 状态 pump window/input event。

Reload 流程：

1. 轮询发现 plugin mtime 更新，先加载新 shadow copy。
2. 加载成功后调用旧实例 `SaveHotReloadState()`。
3. 旧实例 `OnSceneUnloaded()`、`OnDestroy()`、`gkDestroyGameInstance()`。
4. 提交新 DLL，创建新实例。
5. 新实例 `OnInit()` 后调用 `LoadHotReloadState()`。
6. 失败时不替换，旧实例继续运行。

---

## 状态保留

`NextGameInstanceBase` 新增默认空实现：

```cpp
virtual void SaveHotReloadState(FHotReloadState& state) const;
virtual void LoadHotReloadState(const FHotReloadState& state);
```

`FHotReloadState` 是 `nlohmann::json` 的薄封装，适合保存纯 gameplay 状态。FlappyCpp 当前保存：

- score / game state / fixed accumulator / dead timer / pending flap
- RNG 状态
- bird position / velocity
- pipe runtime 列表、spawn timer、config

FlappyCpp plugin 版通过 `Runtime::PluginUi` 绘制 HUD。plugin 不直接链接/调用 ImGui 符号，避免产生第二份 ImGui context；实际绘制在 engine/shared DLL 的 ImGui context 中执行。

---

## Editor 面板

`gkNextEditor` 的 Tools 菜单新增 `Hot Reload` 面板：

- 开关 `g.plugin.hot_reload`
- 开关 `r.shader.hot_reload`
- 调整 plugin/shader 轮询间隔
- 手动 `Reload plugin now`
- 手动 `Rebuild shaders now`
- 显示 shader source/output/slangc 路径与 plugin source/shadow 路径

面板文件：`src/Editor/Panels/HotReloadPanel.cpp`。

---

## 已验证

Windows full preset：

```powershell
.\build.bat --preset full-windows --reconfigure
.\build.bat --preset full-windows
```

启动与 replay：

```powershell
.\out\build\full-windows\bin\gkNextHost.exe --game=FlappyCpp --flappy-replay --width=800 --height=600 --no-shader-hotreload --fastexit=false
.\out\build\full-windows\bin\FlappyCpp.exe --flappy-replay --width=800 --height=600 --fastexit=false
.\out\build\full-windows\bin\gkNextUnitTests.exe "[FlappyRng]"
```

全量 `gkNextUnitTests.exe` 当前仍有既有 LDraw 失败：

- `Test_LDrawParser.cpp`: `FaceNormal(tmpl.faces[0]).z < 0.0f` 失败，实际为 `1.0f`
- `Test_LDrawLoader.cpp`: `LDraw loader applies configurable LDU scale...` 中 SIGSEGV

这些失败与 hot reload 改动无直接关联，且在 hot reload 改动前后表现一致。

---

## 限制与后续扩展

- 目前只有 FlappyCpp 迁成 plugin；Brotato3D、KongLie3D、MagicaLego、Voyage3D 等仍是普通 executable。
- shader reload 使用全量重建，不做 per-pipeline dependency graph。
- plugin reload 是被动模式：开发者手动构建 `FlappyCppPlugin`，host 发现 DLL 写盘后 reload。
- 自动 `cmake --build --target <Game>Plugin` 触发还未实现。
- Blink/Live++ 函数级 patch 是可选 H3 加速器，当前未集成。
- plugin 里不要持久保存 host 不知道如何序列化的 gameplay 状态；跨 reload 必须走 `FHotReloadState`。
