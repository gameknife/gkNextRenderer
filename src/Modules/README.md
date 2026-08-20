# src/Modules — 可选引擎模块

每个子目录是一个独立静态库（`add_library(<Name> STATIC ...)`），由需要它的
Application 显式链接；核心层 `src/Engine` 不得反向依赖本目录。

构建机制见 `src/cmake/SourceFiles.cmake`（`GK_MODULE_NAMES` / `src_files_module_*`）
与 `src/Modules/CMakeLists.txt`：模块目录为空时自动跳过，
Android 平台模块源直接并入单一 SHARED target；`NextTui` 仅桌面 + `GK_WITH_TUI`，
`NextDotNet` 需要 CMake 找到 .NET 工具链（`GK_DOTNET_ENABLED`），移动端不构建。

当前模块（最多 23 个；部分受平台/工具链开关控制）：

| 模块 | 职责 |
|---|---|
| GltfLoader | glTF/GLB 场景与动画加载（`FSceneLoader`） |
| LDrawLoader | LDraw 乐高模型加载（MagicaLego / BrickPlayer） |
| ScadLoader | OpenSCAD DSL 解析/求值/CSG（Manifold）与 ScadRig 骨骼 |
| SplatLoader | Gaussian Splat / SOG 加载与渲染 pass |
| SceneContent | 场景扫描、scene reference 装配与递归引用解析 |
| SceneExport | 场景保存/导出（`FSceneSaver`） |
| NextDotNet | C# 脚本运行时：CoreCLR/NativeAOT 双后端宿主、EngineApi 绑定表 |
| NextPhysics | Jolt 物理后端 |
| NextAudio | miniaudio 音频后端 |
| NextAI | 轻量 LLM Chat / Structured Output 客户端（`FAIService`、`GnbAIClient`）；provider 路由与凭据由 gnb 管理 |
| NextRmlUi | RmlUi 文档 UI 系统 |
| NextUI | ImGui/Vulkan backend、字体缩放、纹理解析与 Desktop UI Foundation |
| NextCapture | 截图编码、异步导出与视频录制 |
| NextValidation | `gnb shot/validate` loopback 控制、查询与 SDL 合成输入 |
| NextRemote | Remote Play：视频编码、WebRTC、远程输入 |
| NextStreamline | DLSS / Streamline 集成（仅 Windows） |
| NextFidelityFX | FidelityFX FSR 3.1 upscale / Frame Generation Vulkan 集成（仅 Windows） |
| NextTemporalUpscaler | Native TAAU + Snapdragon GSR 2 2-pass compute（全 Vulkan 平台，见 `AGENT_GUIDE/NativeTemporalUpscaler.md`） |
| NextTui | 终端 TUI 渲染模式 |
| NextViture | VITURE XR 设备集成（macOS arm64，可选 SDK） |
| RenderViews | 离屏 RenderView 控制器（缩略图 / 多视口） |
| DevTools | 调试面板、AuxDraw、DeveloperStatusBar、CVar 编辑器等 |
| LiveCoding | shader 热重载 watcher / `slangc` 增量编译 |

当前添加/链接模块的方法见 `docs/guides/cmake-structure.md`；历史拆分过程只在 Git 提交记录中保留。

`NextDotNet` 由应用通过 `Modules::NextDotNet::Install()` 显式安装，实现
`Runtime::IScriptRuntime`。后端由 CMake option `GK_DOTNET_BACKEND=CoreCLR|AOT` 选择，
托管代码两种后端完全相同；绑定面的唯一事实来源是 `EngineApi.def.h`，托管侧包装层由
`gnb csharpgen` 生成。目前 `DotNetSandbox` 与 `FlappyCSharp` 链接该模块。见
`docs/designs/dotnet-scripting-design.md`。

`LiveCoding` 也通过 `Modules::LiveCoding::Install()` 显式安装；核心层只保留
`IShaderHotReloader` 抽象与 factory，不再直接持有具体 `slangc` watcher 实现。
