---
title: "SCAD Model Generator（SCAD Studio）设计与开发计划"
category: design
status: 已完成
owner: engine
created: 2026-05-30
last_updated: 2026-06-07
---

# SCAD Model Generator（SCAD Studio）设计与开发计划

> 本文是 **SCAD Model Generator**（产品名 *SCAD Studio*，CMake target **`ScadStudio`**）的整体设计与分阶段开发计划。
>
> 前置：OpenSCAD `.scad` loader 已接入（见 `AGENT_GUIDE/SCADLoader.md`、`docs/designs/scad-loader-design.md`）。本工具在其之上构建一个「对话式 SCAD 生成器」。
>
> **实现状态（2026-05-30）：M0–M7 全部完成**。源码 `src/Application/Editor/ScadStudio/`（11 文件）。
> - M0–M4 + 视口相机：脚手架 / 三栏 / 视口+SCAD 重载 / AI 多轮 / 生成→渲染闭环。
> - M5 持久化：`ScadSessionStore`（`scad_studio/sessions.json` + 每会话 `<id>.json`），启动恢复、重命名/删除、自动存盘。已验证：种入 JSON 会话后重启渲染的是该会话模型而非示例。
> - M6 结构树：`ScadOutline`（复用 `ScadLexer`+`ScadParser` → `Scope` AST → 左栏树形大纲，module/instance/assign 着色 + 行号）。
> - M7 校验+修复+打磨：写盘前用 `BuildScadOutline` 做语法校验；解析报错自动回喂 AI 修复（上限 2 次，可开关）；导出 `.scad`；状态行红/绿；会话右键菜单；in-flight 结果按 `pendingSessionId_` 路由（修了「生成中切 session」的问题）。
> - 实测坑：引擎截图只抓 3D swapchain、不含 ImGui overlay，故面板内容截图看不到（需交互验收）。

---

## 1. 目标与范围

### 1.1 一句话定位
一个**对话式 3D 建模工作台**：用自然语言多轮对话，让引擎内置 AIProvider 生成 / 修改 OpenSCAD（`.scad`）源码，实时解析渲染到视口。形态对标 Claude Desktop / Codex App —— **现代简约三栏布局**，不堆功能。

### 1.2 三栏布局（对标 Codex App）
```
┌────────────┬───────────────────────────┬──────────────────┐
│  左栏       │         中栏 视口          │     右栏 Chat     │
│            │                           │                  │
│ Session列表 │   渲染当前 .scad 场景      │  多轮对话消息流    │
│ + New      │   (PathTracing/SwModern)  │  (流式/打字机)    │
│ ─────────  │                           │                  │
│ 结构树      │   顶部细工具条             │  ──────────────  │
│ (AST大纲)   │   状态行(解析/生成/报错)   │  输入框 + 发送     │
└────────────┴───────────────────────────┴──────────────────┘
```

### 1.3 In Scope（v1）
- 三栏 ImGui docking 布局，简约主题。
- 多会话（session）管理：新建 / 切换 / 重命名 / 删除，磁盘持久化。
- 右栏多轮对话：复用 `NextAI::FAIService`，异步生成，流式或整段返回。
- AI 输出 `.scad` 源码 → 写盘 → 触发场景重载 → 中栏视口更新（核心闭环）。
- 左栏结构树：解析当前 `.scad` 源码的 **AST 大纲**（模块/顶层实例/变换-CSG 层级）。
- 生成结果校验 + 一键「修复」回路（把解析 warning/error 回喂 AI）。
- 导出当前 `.scad` 源码到任意路径。

### 1.4 Out of Scope（v1，列入后续）
- 移动端（AI / FreeType / Manifold 仅桌面）。仅 Windows / Linux / macOS。
- 部件级选中 / 编辑（受 loader 限制 #8：一种颜色 = 一个大 Model，无 per-instance 节点）。
- `.scad` 源码内联文本编辑器（v1 只读展示；编辑靠对话）。后续可加只读代码视图。
- 资产打包 / 导出为 glTF/STL。
- 多模型同场景拼装。

### 1.5 非目标 / 设计取舍
- **不做完整编辑器**。复用 `gkNextEditor` 的 UI 基础设施，但**不是**编辑器的一个面板，而是独立的精简 App，自有三栏布局。
- **整文件再生成优于增量 patch**（v1）。loader 以整文件为单位求值，AI 每轮返回**完整新源码**，多轮靠「带上当前源码 + 历史」实现「改高一点 / 加个屋顶」。

---

## 2. 现有可复用基础（已盘点确认）

### 2.1 SCAD Loader（`src/Engine/Assets/Loaders/FScad*`）
- 入口：`SceneList`（`src/Engine/Runtime/Scene/SceneList.*`）按 `.scad` 扩展名分发。命令行 `--load-scene "assets/scad/x.scad"`。
- 管线：`ExtractDirectives → FScadLexer → FScadParser(AST: FScadTypes.h Expr/Stmt) → FScadEvaluator → 颜色桶 → FScadLoader 组装 Model/Material/Node`。
- 坐标：OpenSCAD Z-up → 引擎 Y-up（绕 X −90°）。缩放 CVar `sys.scadToWorldScale`。
- 性能：解析 ~40–100ms，GPU 上传 ~150–250ms；**无磁盘缓存**（每次重解析，对话迭代足够快）。
- 关键限制（影响本工具设计）：
  - **#8 无 per-instance 节点**：一种颜色 → 一个 Model。⇒ 结构树用 **AST 源码大纲**，不用场景节点树。
  - **#2 `resize` no-op、#3 `offset`/`projection` 未实现、#4 `minkowski` 近似、#5 `import`/`surface` 未实现**。⇒ system prompt 必须把「已支持子集」喂给模型，约束它别用不支持特性。
- 已实现子集（写进 system prompt，见 §6.2）：图元 cube/sphere/cylinder/polyhedron；2D circle/square/polygon/text(CJK)；变换 translate/rotate/scale/mirror/multmatrix/color；CSG union/difference/intersection/hull；linear/rotate_extrude；for/if/let/list-comprehension；module/function；常用内置函数（三角为角度制）。
- 调试：日志前缀 `SCAD:`（warning）、`ECHO:`（echo()）；warning 数 > 0 表示触发了降级。`gkNextUnitTests "[Scad]"` 覆盖全管线。

### 2.2 引擎 AI 服务（`src/Modules/NextAI/AIService.*`）
- 取得：`engine.GetAIService()` → `NextAI::FAIService*`（已确认）。
- 周边：`AI/AIChat.{hpp,cpp}`（多轮消息/工具结构 + provider 序列化）、`AI/IAITool.{hpp,cpp}`（工具/`FAgentLoop`）、`AI/LlamaPidFile.{hpp,cpp}`（本地 llama-server PID 发现）；配置常量 `src/Engine/Runtime/Config/AISettings.hpp`。
- **已确认 API**（`AIService.hpp`）：
  - `LoadConfig()` / `IsConfigured()` / `GetStatus()`→`NextAI::EAIStatus{NotConfigured,Ready,Generating,Error}` / `GetStatusMessage()`
  - 单发：`GenerateText(prompt)` → `FAIResponse{ bool success; std::string text; std::string message; }`；异步 `GenerateTextAsync(prompt, cb)`
  - **多轮：`FChatResponse Chat(const FChatRequest& request)`** + `SupportsTools()` —— 本工具核心用它。
    - `FChatRequest{ std::vector<FChatMessage> messages; std::vector<FToolSchema> tools; std::string model; float temperature=0.7; int maxTokens=0; bool enableThinking=false; }`
    - `FChatMessage` 工厂：`System(text) / User(text) / Assistant(text) / ToolResult(...)`，角色 `EChatRole{System,User,Assistant,Tool}`
    - `FChatResponse{ bool success; std::string content; std::vector<FToolCall> toolCalls; std::string finishReason; std::string errorMessage; FChatUsage usage; }`
  - provider：`GetProviderName()` / `GetProviderType()`→`NextAI::EAIProviderType{Gemini,Ollama,Zhipu,DeepSeek,LocalLlama}` / `SwitchProvider(type)` / `IsProviderConfigured(type)`；static `GetAvailableProviders()` / `ProviderTypeToString()` / `StringToProviderType()`
- **无流式（streaming delta）接口** —— `Chat`/`GenerateText` 都是阻塞整段返回。⇒ ChatPanel 生成中用 spinner，不做打字机（除非后续新增流式）。
- 配置文件：`assets/configs/ai_config.json`（已存在）。`LocalLlama` provider 复用 `gnb llm serve` 起的本地 llama-server（PID 自动发现），**可离线**，适合做默认 provider，开发期零成本。
- **多轮策略（已定）**：用 `Chat()`：`messages = [System(systemPrompt), 历史若干轮 User/Assistant, User(当前源码 + 本轮指令)]`。`Chat` 阻塞，故在后台线程调用、主线程轮询（仿 MagicaLego）。不依赖工具调用（`IAITool`），保持简单。
- **编辑器已有先例可抄**：`gkNextEditor` 的 `AI/EditorAIService.cpp` + `Panels/AIPanel.cpp` + `AI/EditorTools.cpp` 已实现「引擎内 AI 对话 + 工具」，是 `ScadAIService` / `ChatPanel` 的直接参考。

### 2.3 异步范式（`MagicaLegoAIService` 已验证）
后台 `std::thread` 调 `GenerateText` → `std::mutex` + `std::atomic<bool> hasPendingResult_` 存结果 → 主线程每帧 `HasPendingResult()` / `GetPendingResult()` 取出。**场景重载必须在主/渲染线程**，所以「取出结果 → 写盘 → 触发重载」全在主线程做。代码块提取：找 ```` ```lang ```` 围栏取内容（见 `ExtractScriptFromResponse`）。

### 2.4 编辑器 UI 基础设施（复用，不改）
- App：`gkNextEditor`（ImGui docking）。Panels/（Outliner/Properties/MaterialEditor/ConsoleLog/CommandHistory/HotReload/ViewportOverlay）、Core/（EditorUiState/EditorLayoutConstants/RecentScenes）、AI/EditorScriptExecutor、EditorInterface/EditorContext。
- 引擎侧 UI 工具：`src/Engine/Runtime/Editor/`：`ImGuiPainter`、`ImGuiScaling`（DPI）、`NotificationCenter`（toast）、`ProfessionalUI`（统一主题/样式）、`GizmoController`、`ConsoleLogBuffer`。
- 其它：`Camera/ModelViewController`（视口相机）、`Command/CommandHistory`（撤销/重做，可选用于「回退到上一版」）、`Utilities/JsonHelpers`（会话序列化）、`Platform/UserPaths`（用户数据目录）、`Subsystems/TaskCoordinator`（任务/线程，可替代裸 std::thread）。

### 2.5 App 入口范式（`src/Engine/Runtime/GameInstance.hpp`，已确认）
子项目位于 `src/Application/<role>/`（Render/Editor/Game/Util）。每个是独立 CMake target，**继承 `NextGameInstanceBase`** 并提供全局工厂 `std::unique_ptr<NextGameInstanceBase> CreateGameInstance(WindowConfig&, Options&, NextEngine*)`（引擎在 `main` 里调用它）。可重写钩子：`OnInit / OnTick / OnDestroy / OnRenderUI / OnInitUI / OnPreConfigUI / BeforeSceneRebuild / OnSceneLoaded / OnSceneUnloaded / OverrideRenderCamera / ApplyDefaultCVars / OnKey/OnMouseButton/OnScroll/...`。`OnRenderUI()` 返回 bool（是否吞掉默认 UI），是画三栏的入口。参考 `gkNextEditor` 的 `EditorMain.cpp`（入口/工厂）+ `EditorInterface.cpp`（实例）。本工具作为 **Editor role** 下的新 target。

---

## 3. 整体架构

### 3.1 目录与 target
```
src/Application/Editor/ScadStudio/
├── ScadStudioMain.cpp              # 入口（main / 平台 bootstrap，仿 gkNextEditor）
├── ScadStudioInstance.hpp/.cpp     # NextGameInstance 子类：生命周期 + 各子系统组合
├── ScadStudioContext.hpp           # 跨面板共享状态（当前 session、UI 状态、待处理动作）
├── CMakeLists.txt                  # 链接 gkNextEngine、注册资源
│
├── Session/
│   ├── ScadSession.hpp/.cpp        # 单会话数据模型（消息、源码、元数据）
│   └── ScadSessionStore.hpp/.cpp   # 会话集合：加载/保存/新建/删除（JSON 持久化）
│
├── AI/
│   └── ScadAIService.hpp/.cpp      # 包装 NextAI::FAIService：异步、流式、prompt 构建、代码块提取、修复回路
│
├── Scad/
│   ├── ScadOutline.hpp/.cpp        # 源码 → AST → 结构树大纲（复用 FScadParser）
│   └── ScadValidator.hpp/.cpp      # headless 解析/求值校验，收集 warning/error
│
└── UI/
    ├── ScadStudioLayout.cpp        # docking 布局 + 主题
    ├── SessionPanel.cpp            # 左栏：会话列表 + 结构树
    ├── ViewportPanel.cpp           # 中栏：视口 + 工具条 + 状态行
    └── ChatPanel.cpp               # 右栏：消息流 + 输入框
```

### 3.2 数据流（核心闭环）
```
用户在 ChatPanel 输入指令
   → ScadSession 追加 user message
   → ScadAIService.SubmitAsync(session)        [后台线程]
       构建 FChatRequest = [System(systemPrompt), 历史, User(当前源码 + 用户指令)]
       FAIService::Chat() → 从 response.content 提取 ```scad 代码块
   → (主线程轮询) PollResult()
       ├─ 写 session.currentSource → <workspace>/<id>.scad
       ├─ 触发引擎重载该 .scad（主/渲染线程）→ 中栏视口刷新
       ├─ ScadOutline 解析源码 → 左栏结构树刷新
       ├─ ScadValidator 校验 → 状态行显示 warning/error
       │     若有 error 且开启自动修复 → 回喂 AI 一轮（最多 N 次）
       └─ ScadSession 追加 assistant message + 持久化
```

### 3.3 线程模型
- **主/渲染线程**：所有 ImGui、场景加载/重载、视口渲染、session 读写。
- **后台线程**（裸 `std::thread` 或 `TaskCoordinator`）：仅 AI 网络/推理调用与代码块提取；结果经 mutex+atomic 回主线程。
- 结构树 AST 解析很快（~毫秒），可主线程内做；headless 校验若慢可放后台。

### 3.4 状态机（每个 session 的生成状态）
`Idle → Generating（禁用输入，显示 spinner/流式）→ Applying（写盘+重载+解析）→ Idle`；异常分支 `→ Error（状态行红字 + 「修复」按钮）`。

---

## 4. UI / UX 设计（简约，对标 Codex）

### 4.1 全局
- 复用 `ProfessionalUI` 主题：扁平、少边框、留白充足、单一强调色。`ImGuiScaling` 处理 DPI。
- 顶部无传统菜单栏；仅一条极简标题区（产品名 + provider 状态点）。无多余工具按钮。
- 三栏用 `ImGui` DockBuilder 预设布局：左 ~260px、右 ~360px、中间自适应；可拖拽、可折叠左/右栏。

### 4.2 左栏 SessionPanel
- 顶部：`+ New Model` 主按钮。
- 会话列表：每行「标题（可双击重命名）+ 相对时间」，当前项高亮；右键菜单：重命名 / 删除 / 复制。
- 分隔线下：**结构树**（当前 session 的 AST 大纲）：
  - 节点：顶层 `module` 定义、顶层实例化调用、`translate/rotate/color/union/difference...` 嵌套，叶子是图元（cube/sphere/...）。
  - 显示统计：颜色组数、三角形数（来自上次加载）。
  - v1 只读；点击节点可滚动高亮（后续可联动视口）。

### 4.3 中栏 ViewportPanel
- 主体：引擎渲染输出（当前 `.scad` 场景）。鼠标轨道/缩放/平移（`ModelViewController`）。
- 顶部细工具条：模型名（内联可编辑）｜渲染模式下拉（`PathTracing` / `SwModern` / `SwModernNoAmbient`）｜重置相机｜重新生成（重跑上一条指令）｜导出 `.scad`。
- 底部状态行：`解析中… / 生成中… / ✓ 24 色组 · 154k 三角 · 0 warning / ✗ 第 12 行: 未知模块 foo()`。
- 空态（无生成内容）：居中提示「在右侧描述你想要的模型，例如『一个带把手的啤酒杯』」。

### 4.4 右栏 ChatPanel
- 消息流：user 右对齐、assistant 左对齐；代码用等宽折叠块（默认折叠，显示「已应用到视口 ✓」）。
- 生成中：spinner（引擎 AI 无流式接口，整段返回；不做打字机）。
- 输入区：多行输入框 + `Send`；`Enter` 发送、`Shift+Enter` 换行；生成中禁用并显示「停止」（best-effort）。
- 底部：provider 指示（名称 + 已配置/未配置点），点击可切换（`SwitchProvider`）。

### 4.5 交互细节
- 新建 session 自动聚焦输入框。
- 每轮完成后自动持久化（无需手动保存）。
- AI 返回非法/空源码：保留上一版可渲染场景，状态行报错，提供「修复」。

---

## 5. 数据模型与持久化

### 5.1 工作区位置（确认点 B）
优先用 `Platform/UserPaths` 的用户数据目录：`<userdata>/ScadStudio/`；若约定放仓库内则 `assets/scad/generated/`。结构：
```
<workspace>/
├── sessions.json            # 会话索引（id, title, updatedAt, 排序）
├── <sessionId>.json         # 单会话完整数据（含消息历史 + 当前源码快照）
└── <sessionId>.scad         # 当前源码（loader 直接加载的文件）
```
> `.scad` 与 `.json` 并存：`.scad` 给 loader 加载/导出；`.json` 保存对话与历史版本。

### 5.2 结构（用 `JsonHelpers` 序列化）
```cpp
struct ScadMessage {
    std::string role;        // "user" | "assistant"
    std::string content;     // 完整文本（assistant 含代码块）
    std::string scadSource;  // 该轮提取出的源码（assistant 才有，便于回退）
    int64_t     timestamp;
};
struct ScadSession {
    std::string id;                 // uuid / 时间戳
    std::string title;              // 默认取首条 user 消息前 N 字，可改
    std::string currentSource;      // 最新可用源码（= 渲染中的内容）
    std::vector<ScadMessage> messages;
    int64_t createdAt, updatedAt;
    // 运行期（不持久化）：lastWarnings/lastErrors、triCount、colorGroups、status
};
```

### 5.3 版本回退（轻量）
每条 assistant 消息存了 `scadSource`，「回退到此版本」= 把该 source 设为 `currentSource` 并重载。无需独立 undo 栈（如需更强可接 `CommandHistory`）。

---

## 6. AI 集成设计

### 6.1 ScadAIService 职责
封装 `NextAI::FAIService`，对 UI 暴露：
```cpp
bool   IsConfigured() const;
void   SubmitAsync(const ScadSession& session, const std::string& userInstruction);
bool   HasPendingResult() const;
struct Result { bool success; std::string assistantText; std::string scadSource; std::string error; };
Result TakePendingResult();
// provider
std::string CurrentProviderName() const;
bool SwitchProvider(NextAI::EAIProviderType);
```
内部：`BuildSystemPrompt()`、`BuildUserTurn(session, instruction)`、`ExtractScadBlock(text)`、后台线程 + mutex/atomic（仿 MagicaLego）。

### 6.2 System Prompt 契约（关键）
必须把 loader **已支持子集**与**禁用特性**写死进去，否则模型会用 `import/offset/projection/resize` 等不支持特性。要点：
1. 角色：资深 OpenSCAD 建模师，输出可被「本引擎 SCAD 子集」直接渲染的 `.scad`。
2. 坐标/单位：OpenSCAD Z-up，引擎会自动转 Y-up；建议模型尺度 ~1–100 单位（配合 `sys.scadToWorldScale`）。
3. **允许清单**：枚举 §2.1 已实现子集（图元/2D/变换/CSG/extrude/控制流/module/内置函数，三角函数角度制）。
4. **禁用清单**：`import`、`surface`、`projection`、`offset` 不支持；`resize` 为 no-op；`minkowski` 仅近似 union——避免使用。
5. 颜色：多用 `color([r,g,b])` / 命名色给部件上色（loader 按颜色分组，颜色 = 视觉部件划分）；alpha<0.99 会变玻璃/液体材质。
6. 用 `module` 组织部件，顶层实例化，便于结构树展示。
7. **输出格式**：只返回**一个** ```` ```scad ```` 代码块（完整文件），块外可附一句话说明。修改请求时返回**完整修订后的整文件**，不要只给片段。
8. few-shot：附 1–2 个精简范例（可借 `assets/scad/beer_cup.scad`、`acient_city.scad` 的风格）。

### 6.3 多轮编辑（用 `FAIService::Chat`）
构造 `FChatRequest.messages`：
```
[ FChatMessage::System(BuildSystemPrompt()),
  // 可选：最近 K 轮历史（节省 token 可省略，下行已带权威源码）
  FChatMessage::User(prevUserText), FChatMessage::Assistant(prevAssistantText), ...
  FChatMessage::User("当前模型源码:\n```scad\n{currentSource}\n```\n\n用户指令: {instruction}") ]
```
**始终把当前完整源码作为权威状态**喂入（即使省略长历史也能准确），保证「再高一点 / 把屋顶换成红色」生效。首轮无 currentSource 则走「从零创建」。`Chat` 阻塞 → 放后台线程；`request.tools` 留空，`temperature` 适中（~0.4 利于稳定语法）。

### 6.4 校验 + 修复回路
`ScadValidator` headless 跑 Lexer/Parser（必要时 Evaluator）→ 收集 `SCAD:` warning / parse error / 几何为空。若 `error` 或 `三角形=0`：
- 自动修复（可配置开关，默认开，上限 2 次）：构造修复 prompt =「上次源码 + 报错信息 + 请修正」再发一轮。
- 或在状态行给「修复」按钮手动触发。
> 复用 loader 现有诊断（warning 计数、`SCAD:` 日志）。**确认点 C**：是否有「仅解析校验、不建场景」的轻量入口；若无，新增一个 `FScadValidate(source) → {errors, warnings, triCount}` 薄封装（复用 Parser/Evaluator，不走 GPU 上传）。

---

## 7. SCAD 生成 → 重载 → 结构树 闭环实现

### 7.1 运行时重载（已确认）
loader 以**文件**为单位。实现：把 `currentSource` 写到 `<workspace>/<id>.scad`，调用
`engine.RequestLoadScene({ .filename = scadPath })`（`NextEngine::RequestLoadScene(FSceneLoadRequest)`，见 `Engine.hpp:151`）。
- `RequestLoadScene` 内部排队，引擎在合适帧执行 `LoadScene`（`LaunchLoadSceneTask`），**天然主/渲染线程时机安全**——直接在主线程轮询回调里调即可。
- loader 无缓存，覆盖同名文件再 `RequestLoadScene` 即拿到新内容；若发现路径级缓存，用「带递增后缀的临时文件名」绕过。
- `OnSceneLoaded()` 钩子可用来重置相机 / 刷新统计。

### 7.2 结构树（AST 大纲）（已确认入口）
- 复用 loader 的公开解析入口（引擎 lib 内，App 可链接）：先词法 `FScadLexer`，再
  `Assets::scad::ScadParser::Parse(tokens, Scope& outScope, std::string& outError)` → 得到 `Scope`（AST）。
  注意：`Parse` 期望 `use`/`include` 已被剥离（loader 的 `ExtractDirectives`）；生成的 scad 一般不含 include，必要时先剥离。
- `ScadOutline` 把 `Scope`（`FScadTypes.h` 的 `Stmt/Expr`）降维成树节点（名称 + 类型 + 子节点 + 源码行号）。
- `Parse` 返回 false + `outError` 即语法错误，可直接复用于 §6.4 的语法校验（见确认点 C）。

### 7.3 渲染模式
SCAD 多为静态场景：默认 `SwModernNoAmbient`（快、稳，适合迭代）；提供切到 `PathTracing`（漂亮预览）。运行时成功指示：日志 `uploaded scene [...] to gpu`。

---

## 8. 关键技术决策与风险

| 议题 | 决策 / 缓解 |
|------|------------|
| 结构树用什么 | **AST 源码大纲**（非场景节点树）—— 绕开 loader 限制 #8（颜色桶无 per-part 节点） |
| 增量 vs 整文件 | v1 **整文件再生成**，多轮靠「喂入当前完整源码」；简单、契合 loader 整文件求值 |
| 多轮 API | `FAIService::Chat(FChatRequest)`（已确认，messages+system） |
| 运行时重载 | 写盘 + `engine.RequestLoadScene({.filename=path})`（已确认，帧安全） |
| 模型乱用不支持特性 | system prompt 强约束「允许/禁用清单」+ 校验修复回路 |
| AI 离线/无 key | 默认 `localllm`（`gnb llm serve`）；未配置时 UI 明确提示并禁用发送 |
| 线程安全 | 仅 AI 调用在后台；写盘/重载/解析/渲染全主线程；mutex+atomic 交接（仿 MagicaLego） |
| 重载卡顿 | 解析 40–100ms + 上传 150–250ms 会有可感停顿；状态行给 spinner，等空闲帧切换 |
| 大场景性能 | 限制/提示模型控制三角面数；状态行显示 tri 数预警 |
| 平台 | 仅桌面（AI/FreeType/Manifold 限制）；CMake 里非桌面不构建该 target |

---

## 9. 分阶段开发计划（里程碑）

> 每个里程碑可独立验收。每完成一项跑 `gnb build --reconfigure`（Windows: `./gnb.bat build gkScadStudio`），并启动到日志 `uploaded scene [...] to gpu`。

### M0 — 脚手架与 target（0.5d）
- 建 `src/Application/Editor/ScadStudio/` 目录、`CMakeLists.txt`、`ScadStudioMain.cpp`、`ScadStudioInstance`（继承 `NextGameInstance`，空实现）。
- 注册到上层 CMake（仿 `gkNextEditor`），加平台 guard（仅桌面）。
- **验收**：`gnb build gkScadStudio` 通过，能启动出一个空窗口并加载内置 `assets/scad/beer_cup.scad`，日志 `uploaded scene ... to gpu`。
- 文件：新增上述；改 `src/Application/.../CMakeLists.txt`。

### M1 — 三栏布局外壳（1d）
- `ScadStudioLayout`：DockBuilder 三栏预设 + `ProfessionalUI` 主题。
- 三个空面板（SessionPanel/ViewportPanel/ChatPanel）占位，可折叠左右栏。
- **验收**：三栏稳定显示、可拖拽、DPI 正常；中栏接入引擎视口渲染当前场景。

### M2 — 视口 + SCAD 加载/重载管线（1d）
- ViewportPanel：相机控制（`ModelViewController`）、渲染模式下拉、重置相机、状态行。
- 实现「写 `.scad` 文件 → 主线程按路径重载」工具函数（确认点 D），用一个写死源码字符串验证闭环（按钮：写盘+重载）。
- **验收**：点按钮把一段硬编码 `.scad` 写盘并重载，视口实时变化；状态行显示色组/三角数。

### M3 — AI 接入（多轮，先不接视口）（1.5d）
- `ScadAIService`：包装 `FAIService`，`SubmitAsync/HasPendingResult/TakePendingResult`，后台线程 + mutex/atomic。
- `BuildSystemPrompt`（§6.2 完整契约）、`BuildUserTurn`、`ExtractScadBlock`。
- ChatPanel：消息流 + 输入框 + 发送 + provider 指示/切换；生成中禁用、spinner。
- **验收**：输入「一个啤酒杯」，右栏出现 assistant 回复且能提取出 ```scad``` 源码（先打印到日志/控制台，暂不渲染）；`localllm` 与至少一个云 provider 均可跑通；未配置时优雅提示。

### M4 — 闭环打通：生成→写盘→重载→视口（1d）
- 主线程 `PollResult`：取结果 → 写 `<id>.scad` → 重载 → 视口更新；assistant 消息入库。
- 多轮：每轮喂入 `currentSource`（§6.3）。
- **验收**：「做个啤酒杯」→ 视口出现杯子；「把杯子做高一点」「把泡沫改成白色」→ 视口正确更新。

### M5 — 会话管理 + 持久化（1d）
- `ScadSession` / `ScadSessionStore`：JSON（`JsonHelpers`）读写、新建/切换/重命名/删除、自动保存。
- SessionPanel 列表 UI；启动恢复上次会话列表。
- **验收**：建多个会话各自独立；重启后历史/源码/视口可恢复；切换会话正确加载对应 `.scad`。

### M6 — 结构树（AST 大纲）（1d）
- `ScadOutline`：复用 `FScadParser`（确认点 E）source→AST→树；SessionPanel 渲染只读树 + 统计。
- 每次重载后刷新。
- **验收**：杯子场景结构树显示 module/把手/泡沫/杯体等层级，行号正确，与源码一致。

### M7 — 校验 + 修复回路 + 打磨（1d）
- `ScadValidator`（确认点 C）：headless 解析校验，warning/error/triCount → 状态行。
- 自动修复回路（默认开，上限 2）+ 手动「修复」按钮；导出 `.scad`；空态/错误态/toast（`NotificationCenter`）。
- 主题细节、键位（Enter 发送等）、版本回退（点 assistant 消息回退）。
- **验收**：故意让模型产出非法源码能被检出并自动修复；导出文件可被 `--load-scene` 正常加载；整体观感简约一致。

**总估**：约 8–9 人日（单 agent 串行）。M0–M4 是 MVP（能对话生成并看到模型），M5–M7 是完善。

---

## 10. 实现确认点

### 10.1 已核实（可直接照用）
| # | 结论 |
|---|------|
| A 多轮 | `FAIService::Chat(FChatRequest)` 提供 messages+system+tools 多轮，本工具直接用（见 §2.2 / §6.3） |
| D 重载 | `NextEngine::RequestLoadScene({.filename=path})`（`Engine.hpp:151`），排队、帧安全（见 §7.1） |
| E AST | `Assets::scad::ScadParser::Parse(tokens, Scope&, error)`（`FScadParser.h`）公开静态；先 `FScadLexer`（见 §7.2） |
| F 流式 | 引擎 AI **无**流式接口；ChatPanel 用 spinner |
| 入口 | `NextGameInstanceBase` + 全局 `CreateGameInstance(...)` 工厂；参考 `EditorMain.cpp` |

### 10.2 待确认（实现前核实）
| # | 确认内容 | 入手位置 |
|---|---------|---------|
| B | 用户工作区目录 API（决定 §5.1 落盘位置） | `src/Engine/Runtime/Platform/UserPaths.h` |
| C | 几何级校验（warning/三角=0，非仅语法）的最轻入口；语法校验已可用 `ScadParser::Parse` | `FScadEvaluator` / `FScadLoader` |
| G | 新 App target 的 CMake 注册方式与平台 guard（`gkNextEditor` 的 CMake 在上层目录，非 target 同级） | `src/Application/CMakeLists.txt` 或上层 |
| H | 中栏视口如何嵌入/渲染当前场景（直接复用引擎全屏渲染 vs 渲染到纹理 + ImGui Image） | `gkNextEditor` 视口实现、`Runtime/Editor/*`、`Camera/ModelViewController` |

---

## 11. 测试与验收

- **构建**：`gnb build --reconfigure`（或 `./gnb.bat build gkScadStudio`），零编译错误。
- **运行**：启动到 `uploaded scene [...] to gpu`。
- **端到端**：
  1. 「一个带把手的啤酒杯」→ 视口出现杯子，结构树有层级，0 warning。
  2. 多轮：「加高」「把手加粗」「泡沫改白」逐步正确更新。
  3. 新建第二个会话「一座小城堡」，互不干扰；重启后两个会话均恢复。
  4. 导出 `.scad`，用 `--load-scene` 单独加载一致。
  5. 断网 / 用 `localllm` 全流程可跑。
- **回归**：不得改动 loader 既有行为；`gkNextUnitTests "[Scad]"` 全绿。
- **视觉**：可选跑 `gkNextVisualTest` 或 `--agent-validation` 截图核对。

---

## 12. 后续方向（v2+）
- 只读/可编辑源码视图（双向：编辑源码即时重载）。
- 部件级选中（需 loader 改「顶层模块实例 + 颜色」分组，呼应限制 #8）。
- 工具调用（`IAITool`）：让 AI 直接调引擎能力（设缩放、切渲染模式、加灯光）。
- 导出 glTF/STL；缩略图（每会话存一张视口截图作列表预览）。
- 参数化面板：从 AST 抽出顶层变量，生成滑杆实时调参。
