# Hot Reload Cookbook (Slang)

> **状态：** 当前保留 TypeScript 与 Slang shader hot reload。C++ game plugin / DLL hot reload 已移除，桌面程序回到 monolithic executable + static `gkNextEngine` 架构。
>
> Shader reload 走全量 shader 编译 + renderer pipeline 重建；不要按旧计划里的 `ShaderRegistry` 或 game plugin ABI 假设写代码。

---

## 总览

| 链路 | 入口 | 触发 | 当前行为 |
|---|---|---|---|
| TypeScript -> QuickJS | `QuickJSEngine::TickHotReload()` | 0.5 s 轮询 | 失败保留旧脚本 |
| Slang -> SPIR-V -> Vulkan pipeline | `Vulkan::ShaderHotReloader` | 0.5 s 默认轮询或 Editor 手动触发 | 编译变更 `.slang`，`common` 变更触发全量重编，成功后 `VulkanBaseRenderer::ReloadShaders()` |
| Editor/CVar | `Hot Reload` 面板 + CVar console | 手动 | 可开关 shader reload，调整轮询间隔，手动触发 shader rebuild |

移动端（Android/iOS）不启用 shader hot reload 路径。

---

## 构建与启动

full preset 仍然构建普通 executable，不构建 shared engine、host 或 game plugin：

```powershell
gnb.bat build --reconfigure
```

常用开关：

```powershell
--no-hot-reload
--no-shader-hotreload
--shader-hotreload-interval=0.5
```

对应 CVar：

```text
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

## Editor 面板

`gkNextEditor` 的 Tools 菜单有 `Hot Reload` 面板：

- 开关 `r.shader.hot_reload`
- 调整 shader 轮询间隔
- 手动 `Rebuild shaders now`
- 显示 shader source/output/slangc 路径

面板文件：`src/Editor/Panels/HotReloadPanel.cpp`。

---

## 架构约束

- 不构建 `gkNextEngineShared`、`gkNextHost`、`FlappyCppPlugin` 等 C++ hot reload 目标。
- 不在 `NextEngine` 生命周期中引入 plugin loader、shadow copy、ABI 版本检查或跨 DLL 状态保存。
- FlappyCpp 和其他桌面游戏继续走 `CreateGameInstance()` 静态链接路径。
