# src/Modules — 可选引擎模块

每个子目录是一个独立静态库（`add_library(<Name> STATIC ...)`），由需要它的
Application 显式链接；核心层 `src/Engine` 不得反向依赖本目录。

构建机制见 `src/cmake/SourceFiles.cmake`（`GK_MODULE_NAMES` / `src_files_module_*`）
与 `src/Modules/CMakeLists.txt`：模块目录为空时自动跳过，
Android 平台模块源直接并入单一 SHARED target；`NextTui` 仅桌面 + `GK_WITH_TUI`，
`NextQuickJS` 在 Android 之外的平台可用。

当前模块（16 个）：

| 模块 | 职责 |
|---|---|
| GltfLoader | glTF/GLB 场景与动画加载（`FSceneLoader`） |
| LDrawLoader | LDraw 乐高模型加载（MagicaLego / BrickPlayer） |
| ScadLoader | OpenSCAD DSL 解析/求值/CSG（Manifold）与 ScadRig 骨骼 |
| SplatLoader | Gaussian Splat / SOG 加载与渲染 pass |
| SceneExport | 场景保存/导出（`FSceneSaver`） |
| NextQuickJS | QuickJS runtime、TypeScript 热重载、反射脚本绑定 |
| NextPhysics | Jolt 物理后端 |
| NextAudio | miniaudio 音频后端 |
| NextAI | 轻量 LLM Chat / Structured Output 客户端（`FAIService`、`GnbAIClient`）；provider 路由与凭据由 gnb 管理 |
| NextRmlUi | RmlUi 文档 UI 系统 |
| NextRemote | Remote Play：视频编码、WebRTC、远程输入 |
| NextStreamline | DLSS / Streamline 集成（仅 Windows） |
| NextTui | 终端 TUI 渲染模式 |
| RenderViews | 离屏 RenderView 控制器（缩略图 / 多视口） |
| DevTools | 调试面板、AuxDraw、ProfessionalUI、CVar 编辑器等 |
| LiveCoding | shader 热重载 watcher / `slangc` 增量编译 |

当前添加/链接模块的方法见 `docs/guides/cmake-structure.md`；历史拆分过程只在 Git 提交记录中保留。

`NextQuickJS` 由应用通过 `Modules::NextQuickJS::Install()` 显式安装。目前
`FlappyJs` 和 `gkNextEditor` 链接该模块；普通 renderer 与其他 program
不会创建 JavaScript runtime，也不会执行默认测试脚本。

`LiveCoding` 也通过 `Modules::LiveCoding::Install()` 显式安装；核心层只保留
`IShaderHotReloader` 抽象与 factory，不再直接持有具体 `slangc` watcher 实现。
