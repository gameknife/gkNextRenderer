# Hot Reload Cookbook (C++ + Slang)

> **状态：** 骨架（2026-05-07） · 详细实现细节随 PR-H1/H2/H3/H4 落地补全。
> **配套规划：** [docs/plans/2026-05/cpp-shader-hot-reload-plan.md](../docs/plans/2026-05/cpp-shader-hot-reload-plan.md)
>
> 本文档面向后续维护与改进 Hot Reload 链路的 Agent。配合 `QuickJSBindings.md` 阅读：TS 热重载是 C++/Shader 热重载的样板。

---

## 总览

引擎里有三条互不相干的热重载链路：

| 链路 | 编译器 | Watcher 周期 | 触发动作 | 已落地？ |
|---|---|---|---|---|
| TypeScript → QuickJS | `tools/tsc/tsc[.exe]` | 0.5 s | `ResetContextAndLoadScript()` | ✅ |
| Slang → SPIR-V → Vulkan Pipeline | `tools/slang/slangc[.exe]` | 0.5 s（计划） | `WaitIdle` + 管线 invalidate | 🚧 H1 |
| Game C++ → 共享库 → Plugin Reload | CMake + 系统编译器 | mtime 轮询（计划） | `Destroy → FreeLibrary → Load → Create → Restore` | 🚧 H2 |

**核心原则：** 三条链路全部走"轮询 + 旁路编译器 + 失败回退保留旧产物"模型。这与 `QuickJSEngine::TickHotReload`（`src/Runtime/Subsystems/QuickJSEngine.cpp:2312`）的形态保持一致。

---

## Slang 着色器热重载

> 详细方案与文件落点见 plan 文档 §3.2 与 §4-H1。本节只给后续维护要点。

### 关键不变量

- 每个 `PipelineBase` 子类构造时**必须**通过 `ShaderRegistry::Register(slangPath, spvPath, rebuildLambda)` 登记自己；析构时反注册。
- Watcher 检测到 `.slang` 写入 → `slangc` 编译 → `device.WaitIdle()` → 调用所有 rebuild lambda → 下一帧用新管线。
- 共享头 `assets/shaders/common/*.slang` 改动 → 全量重编（暂不做依赖图）。

### 失败回退

- `slangc` 退出码非零 → 保留旧 SPIR-V 与旧管线，`SPDLOG_WARN` 把 stderr 全文输出。
- Pipeline 重建时 Vulkan 报错 → 旧 pipeline 仍生效（rebuild lambda 内部用 try-catch 兜底，析构旧 pipeline 在新 pipeline 创建成功后才执行）。

### 检查清单（PR 自检）

- [ ] 新加的管线在 ctor 里调了 `Register`。
- [ ] 修改 shader 后 < 1 s 内画面更新。
- [ ] 故意写错 shader 不崩溃，旧画面继续。
- [ ] 移动端构建不受影响（`#if !ANDROID && !IOS`）。
- [ ] `--no-shader-hotreload` 关闭后路径与 H1 之前完全一致。

---

## C++ 游戏插件热重载

> 详细方案与文件落点见 plan 文档 §4-H2。本节只给契约与红线。

### Plugin 边界契约

每个 `src/Application/<Game>` 在 `GK_ENABLE_HOT_RELOAD=ON` 时编译为 `<Game>Plugin.<dll|so|dylib>`，必须导出：

```cpp
extern "C" GK_PLUGIN_EXPORT NextGameInstanceBase* gkCreateGameInstance(
    Vulkan::WindowConfig* config, Options* options, NextEngine* engine);
extern "C" GK_PLUGIN_EXPORT void gkDestroyGameInstance(NextGameInstanceBase*);
extern "C" GK_PLUGIN_EXPORT uint32_t gkPluginAbiVersion();
```

ABI version 由 `gkNextEngine` 头部宏 `GK_ENGINE_ABI_VERSION` 计算，包含：

- 引擎接口版本号（开发者手动 bump）
- `sizeof(NextGameInstanceBase)`、`sizeof(NextEngine)` 等关键尺寸的低 16 位
- `Debug` / `Release` 标识

### 红线（不允许做的事）

| 红线 | 原因 |
|---|---|
| Plugin 内 `new` 一个 `Vulkan::ShaderModule` / `Vulkan::Pipeline*` / `entt::registry` | host 与 plugin 各自实例化模板 → ODR/析构 UB |
| Plugin 内放 `static` 单例对象 | reload 时构造/析构序列与 host 错位 |
| Plugin 暴露非 `extern "C"` 的内联模板符号给 host | MSVC/libc++ STL 不同实例之间 ABI 不兼容 |
| 在 plugin 头里放 `inline` 全局变量 | 同上 |
| reload 后假设旧函数指针仍可用 | 旧 DLL 已 FreeLibrary，老地址会段错误 |

### 状态保留

跨 reload 要保留的状态由游戏侧 override：

```cpp
void OnGame::SaveHotReloadState(FHotReloadState& s) const override;
void OnGame::LoadHotReloadState(const FHotReloadState& s) override;
```

底层是 `nlohmann::json`。host 在 reload 期间持有 `FHotReloadState` 实例，旧 plugin 析构后再喂给新 plugin。

### 手动触发与自动触发

- **手动**（默认）：开发者在 IDE 按 build；host 检测到 plugin 文件 mtime 变化自动 reload。
- **自动**（可选 H2.6）：host 监控 `src/Application/<Name>/**.cpp` 的 mtime，自动调 `cmake --build --target <Name>Plugin`。仅 dev preset 启用。

### 检查清单（PR 自检）

- [ ] Plugin 不引入新的全局 static 对象。
- [ ] `SaveHotReloadState` / `LoadHotReloadState` 覆盖所有想保留的成员。
- [ ] reload 在 < 2 s 内完成（dev 机器，FlappyCpp 量级）。
- [ ] replay trace（`tools/flappy/diff_traces.py` 类似流程）跨 reload 一致。
- [ ] ABI version mismatch 时 host 拒绝并打印诊断。
- [ ] 移动端（Android/iOS）构建不受影响（仍是 statically linked executable）。

---

## Blink 函数级 patch（Windows 可选加速器）

> 详见 plan 文档 §4-H3。这是**可选**工具链，不在主路径上。

适用场景：只改一个函数体、不改类布局、不增删字段。

```powershell
# Build host with /Z7 /Od (dev preset)
.\build.bat --preset default-windows-fastdev

# 启动 host
.\out\build\default-windows-fastdev\bin\gkNextHost.exe --game=Brotato3D

# 在另一个终端 attach blink
.\tools\blink\blink.exe (Get-Process gkNextHost).Id
```

之后改 cpp + 在 IDE 里 build → blink patch 进进程，无 reload 成本。

不适用：改类布局、改虚函数表、改全局类型。退回去用 H2 plugin reload。

---

## 调试与可观测

- 日志统一前缀 `[HotReload]`，分 SubSystem：`shader` / `plugin` / `state`。
- CVar：
  - `r.shader.hot_reload`（bool）
  - `r.shader.hot_reload_interval`（秒）
  - `g.plugin.hot_reload`（bool）
  - `g.plugin.auto_build`（bool）
- Editor 面板：`gkNextEditor` 的 **Hot Reload** 面板可手动触发、查看上一次 reload 的耗时与 stderr。

---

## 验收脚本（计划落点）

- `tools/hotreload/bench-shader-reload.{ps1,sh}`：连续修改 shader，统计平均 reload 时间。
- `tools/hotreload/bench-plugin-reload.{ps1,sh}`：自动改 FlappyCpp 一个常数，触发 reload，验证 trace 哈希。
- `tools/hotreload/flappy-reload-replay.{ps1,sh}`：跑两遍 `--flappy-replay`，中间穿插一次 reload，断言 trace 一致。

---

## 与 TS 热重载的关系

- 三条链路独立轮询，互不阻塞。
- 三者都在 `NextEngine::Tick()` 早期、`gameInstance_->OnTick()` 之前完成；游戏看到的永远是最新一致的代码 + shader + script。
- 失败语义统一：保留旧产物 + spdlog warn + UI 提示。

---

## TODO（随实现补全）

- [ ] H1.1 PR 落地后：补 `ShaderRegistry` API 表与登记示例。
- [ ] H1.2 PR 落地后：补 `slangc` 调用参数、共享头依赖处理示例。
- [ ] H2.2 PR 落地后：补 PluginLoader 完整 API 与影子拷贝路径约定。
- [ ] H2.5 PR 落地后：补 `FHotReloadState` 完整 schema 示例（FlappyCpp / Brotato3D）。
- [ ] H4 PR 落地后：补 Editor 面板截图（用户确认开启 emoji/截图后）与 CVar 默认值表。
