# Agent 自动验证（输入驱动 + 断言）系统 — 设计与开发计划

> 状态：设计完成，待实现（设计稿，供后续 agent 接手开发）。
> 目标：在现有 `--agent-validation`（渲染到稳定帧 → 截一张图 → 自动退出）基础上，增加一套**可向运行中进程下达指令、模拟键盘 / 鼠标操作并断言运行时状态**的自动验证系统。让 agent 不必"临时改代码把启动状态硬切到要验证的位置"，而是用**外部脚本 / 实时命令**驱动程序走到任意交互状态后验证。
> 交互模型（已与用户确认）：**声明式验证脚本回放为主**（确定性、可回归、CI 友好）+ **实时命令通道为辅**（loopback socket，交互式探索与读回结果）。
> 验证深度（已与用户确认）：**输入模拟 + 截图 + 状态查询 / 断言**——验证可自动判定 pass/fail（非零退出码），适合无人值守与 CI。
> 非目标（v1）：录制真人操作回放、跨进程多窗口编排、网络远端验证（那是 WebRTC RemotePlay 的范畴，见第 1.3 节）、模糊 / 随机 fuzz 测试、像素级 golden 全量比对（沿用 `gkNextVisualTest`）。
> 日期：2026-06-08
> 关联代码：`src/DesktopMain.cpp`、`src/Engine/Runtime/Engine.{hpp,cpp}`、`src/Engine/Runtime/GameInstance.hpp`、`src/Engine/Runtime/Config/CVarSystem.{hpp,cpp}`、`src/Engine/Vulkan/WindowSurface.cpp`、`src/Engine/Options.{hpp,cpp}`、`tools/gnb/cmd/gnb/main.go`、`tools/gnb/internal/runner/runner.go`、`docs/WebRTC-RemotePlay-Design.md`。

---

## 0. 结论（先读这一段）

可行性**高**。引擎现有的四块基础设施刚好覆盖了"自动验证"的全部难点，几乎不需要动核心架构：

| 难点 | 引擎现成能力 | 落点 |
|---|---|---|
| **注入键鼠操作** | 所有输入统一走 `SDL_Event` → `NextEngine::HandleEvent()`（唯一分发中枢）；`SDL_PushEvent` 注入合成事件**已是仓库内既有写法**（`Window::Close()`，`WindowSurface.cpp`） | 把高层步骤翻译成合成 `SDL_Event` 注入，与 WebRTC `InputRouter` 共享一套 `SyntheticInput` 合成层 |
| **下达 / 接收指令** | CVar 控制台 `FCVarSystem::ExecuteCommand(line)` 已有**文本命令 + 结果协议** `FConsoleResult{success, message, output}` | 注册 `agent.*` 命令族复用该协议；脚本与实时通道共用同一套命令词汇 |
| **无头运行 + 截图 + 自动退出** | `--agent-validation` 已实现隐藏窗口真实渲染、定帧截图到固定路径、自动退出的状态机（`FAgentValidationState` / `TickAgentValidation()`） | 把单步状态机泛化为"可执行多步脚本"的驱动器；复用隐藏窗口与退出语义 |
| **读取运行时状态做断言** | 引擎暴露 `GetTotalFrames/GetFrameRate/GetEngineStatus/GetScene/GetCVarSystem`；CVar 可读写；ECS/反射可枚举组件 | 新增 `QueryProvider` 抽象 + `GameInstance` 可选钩子，把"可断言状态"对外暴露（替代散落的 `if(GOption->AgentValidation)` 硬分支） |

技术选型：

- **主路径 = 声明式验证脚本**：一个 JSON 文件描述有序步骤（按键 / 点击 / 拖拽 / 等待 / 等到条件 / 截图 / 断言 / 退出）。引擎以 `--agent-script=<path>` 启动，逐步回放，跑完写 report 并按断言结果决定退出码。确定性、可 diff、可进 CI。
- **辅路径 = 实时命令通道**：`--agent-control[=port]` 让引擎在 `127.0.0.1` 起一个极小的 loopback TCP 行协议服务，agent 实时发命令、读回结果（含截图路径 / 查询值 / 断言结果）。用于交互式探索；命令词汇与脚本步骤同源。
- **输入注入层 = `SyntheticInput`**：纯函数式地把高层意图（"按下 W"、"在 (0.5,0.5) 点左键"）构造成 `SDL_Event` 并 `SDL_PushEvent`。**与 `docs/WebRTC-RemotePlay-Design.md` 的 `InputRouter` 是同一层**——两者都把"某种来源的输入"译成 SDL 事件，应抽到 `Runtime/Input/` 共享，避免重复实现。
- **驱动器入口 = `gnb`**：新增 `gnb validate --script <path>`（镜像现有 `gnb shot`，阻塞运行、转发退出码、打印 report 路径）；实时模式 `gnb drive`（phase 2）。

整体落成一个新模块 `src/Engine/Runtime/AgentDriver/`，用 `GK_WITH_AGENT_DRIVER` CMake 开关守卫，仅在 `--agent-script` / `--agent-control` 时实例化。对引擎其余部分**低侵入**：`Engine.cpp` 加一个成员 `agentDriver_`（与既有 `remoteServer_` 并列）、一处 `Tick()` 推进、若干 `Inject*` 公共入口。

---

## 1. 背景与目标

### 1.1 现状痛点

现有 `--agent-validation` 只能做一件事：**把场景渲染到稳定帧，截一张图，退出**。这对"看一眼渲染对不对"足够（`gnb shot`），但对**需要交互才能到达的状态**无能为力：晨会要点"确认"、编辑器要拖控件、放置类游戏要点格子、菜单要逐级进入……

agent 目前的变通办法是**临时改代码**，把游戏初始状态硬切到要验证的位置，或在游戏逻辑里塞 `if(GOption->AgentValidation)` 分支自动驱动。`StudioSim` 就是活样本——`StudioSimGameInstance.cpp` 里散布着十余处：

```cpp
if (GOption->AgentValidation) { StartProjectPitch(...); }              // 自动发起立项
goalSystem_.Tick(GOption->AgentValidation ? nullptr : GetAIService(), ...);  // 跳过 LLM
if (GOption->AgentValidation && goalSystem_.State()==AwaitingChoice) goalSystem_.ChooseGoal(0,...); // 自动选目标
if (GOption->AgentValidation) { /* auto-accept gatherings */ }         // 自动接受会议
worldState_.timeScale = 240.0f;                                        // 验证时 240x 加速
```

问题正如用户所述：**不直观，且做不到所有验证**——验证逻辑和游戏逻辑耦合、每加一个验证点就改一次游戏代码、分支只能写死一条"happy path"、无法覆盖"点错了会怎样"。

### 1.2 目标 / 非目标

**目标：**

1. agent 以验证模式启动进程后，能**模拟键盘 / 鼠标 / 滚轮 / 文本输入**，把程序驱动到任意交互状态。
2. 能在指定时机**截图**，并能**查询运行时状态**（cvar、帧状态、场景选中、游戏自定义状态）做**断言**。
3. 主路径是**声明式脚本**：可版本化、可 review、可在 CI 里回归、断言失败返回非零退出码。
4. 辅路径是**实时通道**：交互式发命令、读回结果，供探索性验证。
5. 给游戏 / 编辑器一条**对外暴露可断言状态的标准钩子**，逐步替换 `if(GOption->AgentValidation)` 硬分支。

**非目标（v1）：**

- 录制真人操作生成脚本（v1 脚本由 agent / 人手写或由 agent 生成）。
- 网络 / 远端验证、浏览器端控制（属 WebRTC RemotePlay）。
- 全量像素 golden 比对（沿用 `gkNextVisualTest` 的 baseline 流程）。
- 随机 fuzz、压力测试。

### 1.3 与 WebRTC RemotePlay 的关系（务必对齐，避免重复造轮子）

`docs/WebRTC-RemotePlay-Design.md` 已经把"**任何来源的输入都译成 `SDL_Event` 经 `SDL_PushEvent` 注入 `HandleEvent`**"这条路径论证清楚，并规划了 `InputRouter` / `InjectRelativeMouse` / SDL 虚拟手柄。本系统与它**共享输入注入底座**，区别只在"输入从哪来、为什么注入"：

| | 输入来源 | 触发者 | 目的 | 读回 |
|---|---|---|---|---|
| **AgentDriver（本系统）** | 本地脚本 / loopback 命令 | 自动验证 agent | 把程序驱动到某状态并**断言** | 截图 + 状态查询 + 退出码 |
| **RemotePlay（另一篇）** | 浏览器 DataChannel | 远端真人 | 实时**游玩 / 调试手感** | 实时视频流 |

**强约束：** 两者的"高层意图 → `SDL_Event`"代码必须是同一份 `SyntheticInput`（见第 6 节）。谁先落地谁就把它抽到 `src/Engine/Runtime/Input/`，另一方复用。`InjectRelativeMouse(dx,dy)` 这个薄封装两篇都需要，定义一次。

> 现状（2026-06-08）：`src/Engine/Runtime/Remote/` 已落地**视频侧**（`FrameSource`、`OpenH264Encoder`、`RemoteSession`、`SignalingServer`、`RemoteServer`，已在 `Engine.cpp` 实例化并 `Tick`），**输入侧（`InputRouter` / `SyntheticInput` / `InjectRelativeMouse`）尚未实现**。因此本系统 M1 很可能是**第一个**创建 `SyntheticInput` 的人——请直接建在 `Runtime/Input/` 公共位置，让 Remote 的输入里程碑日后复用，不要塞进 `AgentDriver/` 私有目录。

---

## 2. 现状调研：引擎集成点（给接手 agent 的地图）

> 以下行号基于 2026-06-08 的 `master`，仅作定位提示，接手时**以实际代码为准**（仓库有活跃改动，行号会漂）。

### 2.1 入口与主循环（SDL3 callbacks）

`src/DesktopMain.cpp` 用 `#define SDL_MAIN_USE_CALLBACKS`：

- `SDL_AppInit` 解析 `Options` → `new NextEngine(*GOption)` → `Start()`。
- `SDL_AppIterate` → `GApplication->Tick()`（**持续 game loop，不阻塞等事件**）。
- `SDL_AppEvent` → `GApplication->HandleEvent(*event)`。
- `SDL_AppQuit` → `End()`，`FastExit` 时 `std::quick_exit(0)`。

**含义：** 因为是持续 iterate，`SDL_PushEvent` 注入的合成事件会在下一轮被 `SDL_AppEvent` 正常分发——无需额外唤醒，注入即生效。

### 2.2 输入分发中枢：`HandleEvent`

`NextEngine::HandleEvent(SDL_Event&)`（`Engine.cpp`）是**唯一分发中枢**，顺序为：

```
userInterface_->HandleEvent(&event)         // ImGui
→ rmlUi_->HandleEvent(event)                // RmlUi（返回 consumed）
→ quickJSEngine->HandleInputEvent(event)    // 脚本（键/鼠/手柄按钮，未被 RmlUi 吃掉时）
→ switch(event.type):
     KEY_DOWN/UP, GAMEPAD_BUTTON → OnKey(event)
     MOUSE_BUTTON_DOWN/UP        → OnMouseButton(event)
     MOUSE_MOTION                → OnCursorPosition(rel 或 abs)
     MOUSE_WHEEL                 → OnScroll(x, y)
     WINDOW_CLOSE_REQUESTED      → return true（退出）
```

`OnKey` / `OnMouseButton` / `OnCursorPosition` / `OnScroll` 内部再分别转给 `gameInstance_->OnXxx(...)`（`GameInstance.hpp:47-50` 的虚函数，默认 return false）。**任何来源的 `SDL_Event` 都会流过完整输入栈**（编辑器 UI、脚本、相机、玩法），这正是我们想要的：注入的事件和真人输入走同一条路。

### 2.3 注入先例 + 键盘是事件驱动

`Window::Close()`（`WindowSurface.cpp`）已经在用 `SDL_PushEvent` 构造合成事件：

```cpp
SDL_Event e{};
e.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
e.window.windowID = SDL_GetWindowID(window_);
SDL_PushEvent(&e);
```

`SDL_PushEvent` **线程安全**，可从任意线程（含命令通道线程）直接调。
键盘是**事件驱动**——全仓库**没有** `SDL_GetKeyboardState` 轮询（`OnKey` 读 `event.key.key` / `event.key.scancode`），所以合成 `SDL_EVENT_KEY_DOWN/UP` 完全够用，**无需维护影子键盘态**。

### 2.4 鼠标有两处轮询点 + 相对模式

与键盘不同，鼠标**位置**有两处直接读 OS 态而非跟踪事件：

- `NextEngine::GetMousePos()`（`Engine.cpp:903`）：`SDL_GetMouseState(&fx,&fy)`。
- `EditorMain.cpp:250`：`SDL_GetMouseState(nullptr,nullptr)` 取按键位。

**含义：** 只 `SDL_PushEvent` 一个 `MOUSE_MOTION` **不会**更新 `SDL_GetMouseState` 的返回值。要让轮询点也看到，需要 `SDL_WarpMouseInWindow(window, x, y)` 真正移动 OS 光标（隐藏窗口下仍可 warp），或在注入层维护影子坐标并提供 `InjectRelativeMouse(dx,dy)` 薄封装。`OnCursorPosition` 还区分相对模式（`SDL_GetWindowRelativeMouseMode`）——隐藏窗口下相对模式为 false，FPS 类相机若依赖相对位移，需要直注入 `OnCursorPosition(dx,dy)`（见第 6.2 / 7.x 与 WebRTC 文档同款处理）。

### 2.5 命令协议已存在：CVar 控制台

`FCVarSystem::ExecuteCommand(const std::string& line)`（`CVarSystem.hpp:102`）返回：

```cpp
struct FConsoleResult { bool success; std::string message; std::vector<std::string> output; };
```

已被引擎用于 `cvar.toggle sys.fullscreen` 这类命令。**这就是现成的"文本命令进、结构化结果出"协议**。本系统的命令词汇应当注册成 `agent.*` 命令族（`agent.key`、`agent.click`、`agent.wait`、`agent.shot`、`agent.assert` …），让脚本步骤、实时通道、甚至编辑器控制台三处共用同一套解释器与结果结构。

### 2.6 现有 agent-validation 状态机 + gnb shot

`Options`（`Options.hpp/cpp`）已有：`AgentValidation`、`AgentValidationFrames{90}`、`AgentValidationOutput{"screenshots/agent_validation"}`、`HiddenWindow`、`FastExit`。
`Engine.cpp` 里：构造时 `agentValidation_.{active,waitFrames,outputPath}` 赋值；`windowConfig.HiddenWindow = AgentValidation || HiddenWindow`；present mode 在验证时切 Immediate；`Tick()` 末尾 `if(agentValidation_.active) TickAgentValidation();`。
`TickAgentValidation()`（`Engine.cpp`）状态机：等 `status_==Running` 且 `GetTotalFrames()>=waitFrames` → `RequestScreenShot` → 标记 captured → 再过 3 帧 → `RequestClose()`。
`gnb shot`（`main.go` `newShotCommand`）拼 `--agent-validation [--agent-validation-frames=N] --load-scene=...`，经 `runner.Run`（`exec.Command` + `cmd.Run()` 阻塞、继承 stdio）跑完后打印固定截图路径。

**复用策略：** AgentDriver 是这套状态机的**超集**——把"等帧→截图→退出"泛化成"执行脚本步骤序列→收尾退出"。隐藏窗口、Immediate present、自动退出语义直接沿用。

### 2.7 截图与状态 API

- `NextEngine::RequestScreenShot(FScreenShotSpec{.filename=...})`（`Engine.hpp:149`）：异步，在下一帧 `Tick` 顶部 flush 落盘；支持 `accumulateFrames` 做高质量累积截图。
- 帧 / 状态：`GetTotalFrames()`、`GetFrameRate()`、`GetTime()`、`GetEngineStatus()`、`GetScene()`、`GetCVarSystem()`、`GetUserInterface()`。
- 场景选中：`Scene::GetSelectedId()/GetSelectedIds()`；ECS + `entt::meta` 反射可枚举组件（见 `AGENT_GUIDE/ReflectionSystem.md`）——断言层可据此读任意反射属性。

---

## 3. 总体架构

```text
                       ┌──────────────────────────── 开发 / CI 机 ────────────────────────────┐
  gnb validate         │                                                                      │
  --script X    ─────► │  gkNextRenderer --agent-script=X   (隐藏窗口、Immediate、自动退出)     │
  (阻塞, 转发退出码)     │     │                                                                │
                       │     ▼                                                                │
                       │  ┌───────────────── AgentDriver（新模块）─────────────────┐           │
   gnb drive           │  │  ScriptPlayer   ── 解析 .agentscript.json，按帧推进步骤  │           │
   (实时, phase2) ────► │  │  ControlChannel ── loopback TCP 行协议（127.0.0.1）      │          │
   socket 命令/读回      │  │        └──────────────┬───────────────────────────────┘          │
                       │  │                        ▼                                          │
                       │  │   CommandInterpreter（agent.* 命令族，复用 FConsoleResult）        │
                       │  │        │                 │                  │                      │
                       │  │        ▼                 ▼                  ▼                      │
                       │  │  SyntheticInput     Query/Assert        Screenshot                 │
                       │  │  （键鼠→SDL_Event） （读 cvar/状态/像素）  （RequestScreenShot）       │
                       │  └────────│─────────────────│──────────────────│────────────────────┘ │
                       │           ▼                 ▼                  ▼                        │
                       │   SDL_PushEvent ─► HandleEvent ─► OnKey/OnMouse/...  ─► gameInstance_   │
                       │                                                                        │
                       │   收尾：写 report（JSON）+ 退出码（断言全过=0，否则非0）                   │
                       └────────────────────────────────────────────────────────────────────────┘
```

**线程模型：**

- `ScriptPlayer` 完全在**主线程**（`Tick()` 内）推进——天然与渲染 / 游戏逻辑同步，确定性最好。
- `ControlChannel` 在**独立 IO 线程** accept/recv；收到命令后：
  - 纯输入注入（`SDL_PushEvent`）→ **线程安全，可直接在 IO 线程做**。
  - 需要读引擎状态 / 截图 / 触碰非线程安全对象 → **投递到主线程任务队列**（`AddTickedTask` / 一个专用 MPSC 队列），主线程 `Tick` 取出执行，结果回填，IO 线程再응答。
- 模块落点：`src/Engine/Runtime/AgentDriver/`，与 `Engine.cpp` 里既有的 `remoteServer_` 成员并列一个 `agentDriver_`。

---

## 4. 验证脚本格式（声明式，主路径）

### 4.1 文件与外形

后缀 `.agentscript.json`，放 `assets/agentscripts/` 或随测试就近。顶层：

```json
{
  "name": "studiosim-morning-meeting",
  "target": "StudioSim",
  "scene": "assets/scad/office.scad",
  "defaults": { "waitFrames": 2, "stepTimeoutMs": 8000 },
  "viewport": { "width": 1920, "height": 1080 },
  "steps": [ /* 见 4.2 */ ]
}
```

`target` / `scene` / `viewport` 可被 `gnb validate` 的命令行覆盖。`defaults` 给每步兜底超时与节拍。

### 4.2 步骤类型（v1）

每个 step 是 `{ "type": "...", ...args, "comment": "..." }`。`comment` 进 report 便于读。

| type | 参数 | 语义 |
|---|---|---|
| `key` | `code`(如 `"W"`/`"RETURN"`/`"F2"`), `action`("down"/"up"/"press"), `mods`(["ctrl","shift"]) | 合成 `KEY_DOWN/UP`；`press`=down+up |
| `text` | `value`(string) | 合成 `SDL_EVENT_TEXT_INPUT`（IME / 输入框，见 6.1） |
| `mouse-move` | `to`(锚点，见 4.3), `relative`(bool) | warp + 合成 `MOUSE_MOTION` |
| `click` | `button`("left"/"right"/"middle"), `at`(锚点，可省=当前), `count`(1/2) | move→`BUTTON_DOWN`→`BUTTON_UP`，`count:2` 双击 |
| `drag` | `from`, `to`, `button` | down@from → 多帧 move → up@to |
| `scroll` | `x`,`y` | 合成 `MOUSE_WHEEL` |
| `wait-frames` | `n` | 推进 n 帧 |
| `wait-ms` | `ms` | 按 wall-clock 等待（隐藏窗口 Immediate 下很快） |
| `wait-until` | `query`(见 5/7), `op`, `value`, `timeoutMs` | **轮询**直到条件成立或超时（超时=失败） |
| `cvar` | `set`/`get` + `name` + `value` | 读写 cvar（`set` 走 `ExecuteCommand`） |
| `exec` | `line`(string) | 直接执行一条控制台命令行 |
| `screenshot` | `out`(路径,可省), `accumulate`(n) | `RequestScreenShot`；路径默认 `screenshots/<name>_<idx>.jpg` |
| `assert` | `query`, `op`, `value`, `message` | 求值并记录 pass/fail（不立即退出，除非 `fatal:true`） |
| `log` | `message` | 写一行到 report / stdout |
| `quit` | — | 主动结束（脚本末尾隐式 quit） |

`op` 集合：`eq` / `ne` / `gt` / `ge` / `lt` / `le` / `contains` / `exists` / `truthy`。

### 4.3 锚点（坐标）语义

为抗分辨率 / DPI 漂移，坐标支持三种写法，统一在注入层解析成**窗口逻辑像素**：

- 绝对像素：`{ "px": [960, 540] }`
- 归一化比例：`{ "norm": [0.5, 0.5] }`（× 当前窗口逻辑尺寸）
- 具名目标（可选，phase 3）：`{ "ui": "BriefingPanel/ConfirmButton" }` —— 由 UI 层注册可点击元素的命名矩形（ImGui/RmlUi 元素），注入层取其中心。最稳，但要 UI 配合暴露。

> v1 先支持 `px` / `norm`；`ui` 具名目标作为增量（第 11 节 M3+）。

### 4.4 时序语义

- 每步在 `Tick()` 边界推进；同步步骤（key/click/cvar/log）执行后默认再等 `defaults.waitFrames` 帧让效果落地。
- `wait-until` / `assert` 的 `query` 每帧求值一次，直到成立或 `timeoutMs`。
- 全脚本有总超时（`gnb validate --timeout`，默认 60s）防卡死。

### 4.5 示例：StudioSim 晨会流程（替代硬分支）

```json
{
  "name": "studiosim-morning-meeting", "target": "StudioSim",
  "scene": "assets/scad/office.scad",
  "steps": [
    { "type": "wait-until", "query": "game.phase", "op": "eq", "value": "Briefing", "timeoutMs": 10000,
      "comment": "等进入晨会" },
    { "type": "screenshot", "out": "screenshots/ss_briefing.jpg" },
    { "type": "assert", "query": "game.hasActiveProject", "op": "eq", "value": false,
      "message": "晨会开始时不应有进行中项目" },
    { "type": "click", "at": { "norm": [0.5, 0.82] }, "button": "left", "comment": "点『发起立项』" },
    { "type": "wait-until", "query": "game.goalState", "op": "eq", "value": "AwaitingChoice", "timeoutMs": 8000 },
    { "type": "key", "code": "1", "action": "press", "comment": "选第一个目标" },
    { "type": "wait-until", "query": "game.phase", "op": "eq", "value": "Working", "timeoutMs": 8000 },
    { "type": "screenshot", "out": "screenshots/ss_working.jpg" },
    { "type": "assert", "query": "game.employeeCount", "op": "ge", "value": 1, "message": "应有在岗员工" },
    { "type": "quit" }
  ]
}
```

这版脚本把 §1.1 列出的硬分支（自动立项、自动选目标、自动接受）**全部外置**：游戏逻辑回归"正常按键鼠走"，验证意图写在脚本里、可 review、可加分支（比如再写一个"点错按钮应弹确认框"的脚本）。

---

## 5. 命令词汇（文本协议，脚本与实时通道共用）

脚本步骤（第 4 节）是**结构化 JSON**，实时通道（第 8 节）是**单行文本**——两者最终都汇聚到同一个 `CommandInterpreter`。为统一，定义一套 `agent.*` 控制台命令，结构化步骤即其语法糖。

| 命令行 | 对应步骤 | 返回（`FConsoleResult.output`） |
|---|---|---|
| `agent.key <code> <down\|up\|press> [mods]` | key | ok |
| `agent.text <string>` | text | ok |
| `agent.move <px x y \| norm a b> [--rel]` | mouse-move | ok |
| `agent.click <left\|right\|middle> [at...] [--double]` | click | ok |
| `agent.drag <from...> <to...> <button>` | drag | ok |
| `agent.scroll <x> <y>` | scroll | ok |
| `agent.wait frames <n> \| ms <n> \| until <query> <op> <value> [timeout]` | wait-* | ok / timeout |
| `agent.shot [out] [--accumulate n]` | screenshot | **截图绝对路径** |
| `agent.query <query>` | — | **查询到的值**（字符串化） |
| `agent.assert <query> <op> <value> [msg]` | assert | pass / fail + 实际值 |
| `cvar.set/get/toggle ...` | cvar | 复用现有 cvar 命令 |

**结果协议**：直接复用 `FConsoleResult{success,message,output}`。实时通道把它序列化成一行 JSON 回应（见 8.3）；脚本回放把它累加进 report。

实现选择（二选一，建议 A）：

- **A. 注册到 `FCVarSystem`**：新增 `agent.*` 命令处理器进 `ExecuteCommand` 的分发。好处：编辑器控制台、脚本、实时通道、QuickJS 全都能调，统一；坏处：命令需要触达 `NextEngine`（注入 / 截图），要给 CVarSystem 一个回引擎的弱引用或回调表。
- **B. AgentDriver 内置独立 interpreter**：只在验证模式存在，直接持有 `NextEngine*`。好处：零侵入 CVarSystem；坏处：与 cvar 命令两套解析。

> 建议 A：把 `agent.*` 作为引擎级控制台命令注册（处理器内 `NextEngine::GetInstance()` 拿引擎），让能力对编辑器/脚本都可用；AgentDriver 只负责"从脚本/socket 把命令行喂进来 + 收集结果"。

---

## 6. 输入注入层 `SyntheticInput`（与 WebRTC 共享）

落点：`src/Engine/Runtime/Input/SyntheticInput.{hpp,cpp}`（新建共享目录）。纯逻辑、不持有引擎大对象，依赖窗口尺寸 / 句柄通过参数传入。

### 6.1 键盘 / 文本

```cpp
namespace Input::Synthetic {
  // code: 接受 SDL keycode 名（"W","RETURN","F2","KP_ENTER"）或 scancode；内部用查表 + SDL_GetKeyFromName。
  void PushKey(SDL_Keycode key, SDL_Scancode sc, SDL_Keymod mods, bool down, SDL_WindowID win);
  void PushKeyPress(...);            // down + up
  void PushText(const char* utf8, SDL_WindowID win);  // SDL_EVENT_TEXT_INPUT，喂 ImGui/RmlUi 输入框
}
```

- 填 `event.key.{key,scancode,mod,down,repeat=false}`，`windowID` 用 `SDL_GetWindowID`。
- `mods` 影响快捷键判定（`OnKey` 读 `SDL_GetModState()`——注意：`SDL_GetModState()` 反映的是真实修饰键态，合成事件不一定改它。若快捷键依赖 `GetModState`，需要在注入前后 `SDL_SetModState(mods)` 或改走事件里的 `key.mod`。**接手时实测 Ctrl+Z 这类组合**，必要时用 `SDL_SetModState`）。
- 文本输入（聊天框、命名、`customGoalBuf_`）走 `TEXT_INPUT` 而非逐字 keydown。

### 6.2 鼠标 / 滚轮

```cpp
  void WarpAndMove(SDL_Window* win, float x, float y);  // SDL_WarpMouseInWindow + 合成 MOUSE_MOTION(abs)
  void PushButton(float x, float y, Uint8 button, bool down, SDL_WindowID win);
  void PushWheel(float x, float y, SDL_WindowID win);
  void InjectRelativeMouse(float dx, float dy);         // 直调 NextEngine::OnCursorPosition(dx,dy) 薄封装
```

- **绝对移动**先 `SDL_WarpMouseInWindow`（让 §2.4 的 `SDL_GetMouseState` 轮询点同步），再 push `MOUSE_MOTION` 带 `motion.x/y`。
- **相对移动**（FPS 相机、隐藏窗口相对模式为 false）：经 `NextEngine::InjectRelativeMouse(dx,dy)` 直接喂 `OnCursorPosition`，绕开窗口相对态——**这个薄封装 WebRTC 文档也要，定义一次，两边共用**。
- 点击 = move(可选) + `BUTTON_DOWN` + 隔 1 帧 + `BUTTON_UP`；双击靠 `button.clicks=2` 或两次快速点击。

### 6.3 坐标系与 DPI

- 锚点（4.3）→ 注入层用**当前窗口逻辑尺寸**（`SDL_GetWindowSize`）解析 `norm`；高 DPI 下 SDL 事件坐标是逻辑像素，渲染是像素，注意别混。
- viewport 在脚本里声明，`gnb validate` 用 `--width/--height` 传给进程，保证脚本里 `px` 坐标可复现。

### 6.4 UI 焦点 / 事件被谁消费

`HandleEvent` 链路里 ImGui(`WantsToCaptureKeyboard/Mouse`)、RmlUi(`WantsToCaptureKeyboard/Mouse`) 会**优先吃掉**事件（见 `OnCursorPosition` 开头的 capture 检查）。这是对的：要点 ImGui 按钮，事件本就该进 ImGui。注入层**不做特判**，让事件自然流过——脚本作者只需把坐标对准目标控件。`ui` 具名锚点（4.3，phase3）能进一步消除"对没对准"的不确定性。

### 6.5 隐藏窗口注意点

- 隐藏窗口（`SDL_WINDOW_HIDDEN`）仍可 warp、仍收合成事件；但**不抢焦点**、相对鼠标模式默认 false → 相对位移走 6.2 的直注入。
- 若某验证必须真实可见窗口（极少数，如平台级拖拽），脚本可声明 `"window": "visible"`，由 `gnb validate` 去掉 `--hidden-window`（但 `--agent-script` 默认隐藏）。

---

## 7. 状态查询与断言层

### 7.1 查询命名空间（`query` 语法：`<域>.<键>[.<子键>]`）

| 域 | 例 | 来源 |
|---|---|---|
| `cvar.` | `cvar.r.shadowResolution` | `FCVarSystem::GetValueString` |
| `engine.` | `engine.totalFrames` / `engine.frameRate` / `engine.status` | `NextEngine` getter |
| `scene.` | `scene.selectedId` / `scene.nodeCount` / `scene.node(<id>).component(Render).visible` | `Scene` + `entt::meta` 反射 |
| `pixel.` | `pixel.at(0.5,0.5)` / `pixel.regionMean(x,y,w,h)` | 采样最近一次截图 / swapchain 回读 |
| `game.` | `game.phase` / `game.employeeCount` / 任意游戏自定义键 | **GameInstance QueryProvider（见 7.2）** |

`pixel.*` 让断言能"看图"做粗判（如"屏幕中心不是纯黑/未渲染"），不替代 `gkNextVisualTest` 的精细 baseline。

### 7.2 GameInstance 可断言钩子（替代硬分支的关键）

给 `NextGameInstanceBase` 加**可选**虚函数，让每个游戏把"可断言状态"对外暴露：

```cpp
// GameInstance.hpp，默认空实现，旧游戏零改动
virtual void RegisterAgentQueries(AgentQueryRegistry& reg) {}
```

`StudioSim` 实现示例：

```cpp
void RegisterAgentQueries(AgentQueryRegistry& reg) override {
  reg.Add("phase",            [this]{ return ToString(worldState_.phase); });
  reg.Add("hasActiveProject", [this]{ return HasActiveGameProject(); });
  reg.Add("goalState",        [this]{ return ToString(goalSystem_.State()); });
  reg.Add("employeeCount",    [this]{ return (int)employeeSystem_.Employees().size(); });
}
```

`AgentQueryRegistry` 把闭包返回值统一成 `variant<bool,int64,double,string>` 供断言算子比较。**这条钩子是把 §1.1 硬分支"翻译"过来的归宿**：原本 `if(AgentValidation){自动做X}` 变成"脚本点按钮做 X + `assert game.* `验证 X 发生"。

### 7.3 断言算子与失败处理

- 算子见 §4.2 `op`。类型按目标值推断（脚本 `value` 是 JSON 类型）。
- `assert` 默认**记录但不中断**（收集全部结果，跑完一并报告）；`"fatal": true` 则立即停并退出。
- `wait-until` 超时 = 失败。
- **退出码**：全部 assert pass 且无超时 → `0`；任一 fail / 超时 / 脚本错误 → 非零（`1` 断言失败，`2` 超时，`3` 脚本/IO 错误）。`gnb validate` 透传退出码，CI 可直接用。

### 7.4 失败现场

任一 assert 失败时**自动截一张图**（`screenshots/<name>_FAIL_<step>.jpg`）并把当时所有 `query` 域快照写进 report，方便 agent 事后定位。

### 7.5 Report 格式

收尾写 `--agent-report=<path>`（默认 `out/build/<preset>/agent_reports/<name>.json`）：

```json
{
  "name": "...", "target": "...", "passed": false,
  "durationMs": 4231, "framesRendered": 540,
  "steps": [ { "idx": 4, "type": "assert", "query": "game.hasActiveProject",
               "op": "eq", "expected": false, "actual": true, "passed": false,
               "screenshot": "screenshots/..._FAIL_4.jpg" } ],
  "screenshots": [ "..." ], "exitCode": 1
}
```

---

## 8. 实时命令通道（次要路径，phase 2）

### 8.1 传输选型

- **推荐：loopback TCP 行协议**，仅 `bind 127.0.0.1`，端口写入 `out/build/<preset>/run/agent_control.port`（沿用仓库 `run/<x>.pid` 约定，见 AGENTS.md 的 llm server）。跨平台、支持请求/响应、`gnb` 易连。
- 备选：**监听命令文件**（agent 往 `agent_control.in` 追加行，引擎写 `agent_control.out`）。零 socket、最简，但请求/响应配对与并发更糙。v1 若想最省事可先做文件版，TCP 作为正式版。

> 决策：**phase 2 直接做 loopback TCP**；文件版仅作降级备选不投入。

### 8.2 线程安全

- IO 线程收命令 → 输入类（`SDL_PushEvent`）直接做（线程安全）。
- 查询 / 截图 / 触引擎对象 → 封成 task 投递主线程队列，主线程 `Tick` 执行后把 `FConsoleResult` 回填，IO 线程应答。绝不在 IO 线程读 `Scene` / 渲染对象。

### 8.3 协议（行 JSON）

```
→ {"id":7,"cmd":"agent.click left norm 0.5 0.82"}
← {"id":7,"ok":true,"output":[],"message":""}
→ {"id":8,"cmd":"agent.shot"}
← {"id":8,"ok":true,"output":["/abs/.../screenshots/agent_validation.jpg"]}
→ {"id":9,"cmd":"agent.query game.phase"}
← {"id":9,"ok":true,"output":["Working"]}
```

### 8.4 安全

仅绑 `127.0.0.1`，不监听公网；无鉴权（本机信任边界，与 RemotePlay 的公网场景不同）。`--agent-control` 不传端口则取 0 让 OS 分配，写进 `.port` 文件。

---

## 9. 引擎集成点改动清单

| 文件 | 改动 |
|---|---|
| `src/Engine/Options.{hpp,cpp}` | 新增 `AgentScript`(string)、`AgentControl`(bool/port)、`AgentReport`(string)；`--agent-script` 隐含 `HiddenWindow`、Immediate present、`FastExit`（沿用 AgentValidation 那套），并隐含 `AgentValidation` 的"无 LLM/确定性"语义（见 9.1） |
| `src/Engine/Runtime/Engine.hpp` | 加成员 `std::unique_ptr<AgentDriver> agentDriver_;`（`GK_WITH_AGENT_DRIVER` 守卫）；公开 `InjectRelativeMouse(float,float)`、（可选）`InjectSyntheticEvent` 入口 |
| `src/Engine/Runtime/Engine.cpp` | `Start()` 末按 Options 实例化 `agentDriver_`；`Tick()` 末 `if(agentDriver_) agentDriver_->Tick(...)`（在 `TickAgentValidation` 同位置）；`End()` `agentDriver_.reset()` |
| `src/Engine/Runtime/Input/SyntheticInput.{hpp,cpp}` | **新建**，第 6 节合成层（WebRTC 复用） |
| `src/Engine/Runtime/AgentDriver/AgentDriver.{hpp,cpp}` | **新建**，持有 `ScriptPlayer` / `ControlChannel` / `CommandInterpreter` / `AgentQueryRegistry` / report writer |
| `src/Engine/Runtime/AgentDriver/ScriptPlayer.{hpp,cpp}` | **新建**，JSON 解析 + 步骤状态机 |
| `src/Engine/Runtime/AgentDriver/ControlChannel.{hpp,cpp}` | **新建**（phase2），loopback TCP |
| `src/Engine/Runtime/Config/CVarSystem.{hpp,cpp}` | 注册 `agent.*` 命令族（方案 A），或留出 interpreter 钩子 |
| `src/Engine/Runtime/GameInstance.hpp` | 加 `virtual void RegisterAgentQueries(AgentQueryRegistry&) {}`（默认空，旧游戏零改） |
| `src/CMakeLists.txt` / 相关 | `GK_WITH_AGENT_DRIVER` 开关（默认 ON for desktop dev，可关）；JSON 解析用 **`nlohmann-json`**（已在 `vcpkg.json` 依赖中，直接 `#include <nlohmann/json.hpp>`，无需新增依赖） |

### 9.1 与 AgentValidation 语义的关系

`--agent-script` 应**自动带上现有 `AgentValidation` 的"确定性"副作用**（隐藏窗口、跳过 LLM 异步、加速时钟等），否则脚本回放会被真实 LLM 延迟 / 真实时钟拖垮。最干净的做法：`--agent-script` 隐含 `AgentValidation=true`，但把"到点自动截图退出"的单步逻辑交给 ScriptPlayer 接管（脚本里显式 `screenshot`/`quit`）。即 `TickAgentValidation()` 在 `agentDriver_` 存在时让位。

---

## 10. gnb 集成

| 命令 | 行为 | 实现 |
|---|---|---|
| `gnb validate --script <path> [--target T] [--scene S] [--width/-h] [--timeout S] [--report P]` | 阻塞跑脚本，转发进程退出码，结束打印 report + 失败截图路径 | 镜像 `newShotCommand`：拼 `--agent-script=...` 经 `runner.Run`；`runner.Run` 已继承 stdio 并返回 `cmd.Run()` err（含退出码） |
| `gnb drive [--target T] [--scene S]` (phase2) | 起进程（`--agent-control`）+ 读 `.port` + 连 TCP + 进 REPL/转发 stdin 命令、打印响应 | 新增；复用 control 协议（8.3） |

`runner.Options` 已支持 `Target/Preset/Scenes/Args`；`validate` 仅需把脚本路径作为 `--agent-script=` 追加，并在 `cmd.Run()` 返回后据 `exec.ExitError` 取退出码。

文档：更新 `AGENTS.md` "Agent Visual Validation" 段，增"Agent Interactive Validation"小节，并在 `docs/gnb-cli.md` 补 `validate`/`drive`。

---

## 11. 开发阶段拆分（里程碑）

> 每个里程碑独立可验证、可合并。构建按 AGENTS.md "Targeted builds"：动 Engine 层 → `./gnb build gkNextRenderer gkNextUnitTests`。

### M1 — 输入注入底座 `SyntheticInput` + 引擎入口
- 任务：新建 `Runtime/Input/SyntheticInput.{hpp,cpp}`（键/文本/鼠标/滚轮/相对）；`NextEngine::InjectRelativeMouse`；最小 `--agent-control` 不做也行，先用单测驱动。
- 改动：`SyntheticInput.*`、`Engine.{hpp,cpp}`、`CMakeLists`。
- 验收：新增 `gkNextUnitTests` 用例——构造合成 `KEY_DOWN("W")` / `MOUSE_BUTTON` push 后，用一个测试用 `GameInstance` 在 `OnKey/OnMouseButton` 里捕获，断言收到正确 key/button/坐标；`InjectRelativeMouse` 能驱动 `OnCursorPosition`。（`EngineTestFixture` 已支持隐藏窗口真实渲染。）
- 命令：`./gnb build gkNextRenderer gkNextUnitTests` → `gkNextUnitTests "[Unit][AgentInput]"`。

### M2 — ScriptPlayer + 基础步骤 + `gnb validate`
- 任务：`AgentDriver` + `ScriptPlayer`；解析 JSON；落地 `key/text/mouse-move/click/drag/scroll/wait-frames/wait-ms/screenshot/quit`；`--agent-script` 接管 `TickAgentValidation`；`gnb validate`。
- 改动：`AgentDriver/*`、`Options.*`、`Engine.cpp`(Tick 接管)、`main.go`(validate)、`runner` 退出码透传。
- 验收：写一条 `playground` 脚本（移动相机几下 + 截图 + quit），`gnb validate --script X` 跑通、自动退出、截图落盘、退出码 0；故意写错步骤 → 非零退出。
- 命令：`gnb build gkNextRenderer` → `gnb validate --script assets/agentscripts/smoke.agentscript.json`。

### M3 — 查询 + 断言 + report + 退出码
- 任务：`AgentQueryRegistry`；`cvar./engine./scene./pixel.` 内建查询；`GameInstance::RegisterAgentQueries`；`wait-until`/`assert`/`cvar`/`exec`/`log`；失败截图 + report JSON + 退出码语义（§7.3）。
- 改动：`AgentDriver/*`、`GameInstance.hpp`、`CVarSystem`(agent.* 命令)、Engine 查询暴露。
- 验收：脚本含 `assert engine.totalFrames ge 1`（pass）与一条故意 fail；report 正确标 pass/fail、退出码非零、失败截图存在。
- 命令：同上 + 检查 `agent_reports/*.json`。

### M4 — 实时命令通道 + `gnb drive`
- 任务：`ControlChannel`（loopback TCP，行 JSON，§8）；主线程任务队列回填；`.port` 文件；`gnb drive` REPL。
- 改动：`AgentDriver/ControlChannel.*`、`Options.*`、`main.go`(drive)。
- 验收：`gnb drive --scene playground` 后在 REPL 发 `agent.query engine.frameRate`、`agent.shot` 能即时拿到值/路径；多命令顺序正确、无崩溃；端口仅本机可连。

### M5 — 迁移 StudioSim + 文档
- 任务：用脚本复现 StudioSim 一天流程，**删除/收敛 `if(GOption->AgentValidation)` 硬分支**（保留必要的"无 LLM/加速"开关，由 `--agent-script` 语义统一驱动）；实现其 `RegisterAgentQueries`；写 2~3 条代表脚本（晨会、生产、结算）。
- 改动：`StudioSimGameInstance.{hpp,cpp}`、`assets/agentscripts/*`、`AGENTS.md`、`docs/gnb-cli.md`。
- 验收：三条脚本在 `gnb validate` 全绿；StudioSim 代码里 `AgentValidation` 硬分支显著减少；其他游戏不受影响（抽样 `gnb shot` 各 program 仍 OK）。

---

## 12. 风险与对策

| 风险 | 说明 | 对策 |
|---|---|---|
| **修饰键判定** | `OnKey` 读 `SDL_GetModState()`，合成事件可能不改它 → Ctrl+Z 等组合失效 | 注入组合键前 `SDL_SetModState(mods)`，事后还原；M1 单测覆盖 Ctrl 组合 |
| **鼠标轮询点** | `GetMousePos`/Editor 读 `SDL_GetMouseState` 不随合成 motion 更新 | 绝对移动一律先 `SDL_WarpMouseInWindow`；相对走 `InjectRelativeMouse` |
| **UI 抢事件** | ImGui/RmlUi capture 导致点击没落到预期层 | 锚点对准目标控件；phase3 `ui` 具名锚点；report 记录"事件是否被 UI 吃掉"便于排查 |
| **DPI / 分辨率漂移** | `px` 坐标换分辨率失效 | 优先 `norm`；脚本声明 viewport，`gnb validate` 固定 `--width/--height` |
| **时序非确定性** | 真实 LLM / 真实时钟让"等到某状态"不稳 | `--agent-script` 隐含确定性语义（无 LLM、加速时钟）；一切等待走 `wait-until` 条件而非固定帧 |
| **线程安全** | IO 线程碰引擎对象 | 输入注入线程安全直接做；其余投递主线程队列回填 |
| **隐藏窗口相对鼠标** | 相对模式 false，FPS 相机收不到位移 | `InjectRelativeMouse` 直喂 `OnCursorPosition`，不依赖窗口态 |
| **与 WebRTC 重复** | 两套输入注入分叉 | 强制共享 `SyntheticInput` / `InjectRelativeMouse`；本文件 §1.3、§6 与 RemotePlay 文档交叉引用 |
| **JSON 依赖** | — | 已确认 `vcpkg.json` 含 `nlohmann-json`，脚本/报告解析直接用，无新增依赖 |
| **跨平台** | warp/相对模式 Linux/mac 行为差异 | M1~M3 Windows 先行，CI 加 Linux 冒烟；平台差异在 `SyntheticInput` 内用 `PlatformCommon` 收口 |

---

## 13. 目录结构 & 新增文件清单

```
src/Engine/Runtime/
├── Input/
│   └── SyntheticInput.{hpp,cpp}        # 新：高层意图→SDL_Event（WebRTC 共享）
└── AgentDriver/
    ├── AgentDriver.{hpp,cpp}           # 新：驱动器门面（持 player/channel/interpreter/registry）
    ├── ScriptPlayer.{hpp,cpp}          # 新：脚本解析 + 步骤状态机
    ├── CommandInterpreter.{hpp,cpp}    # 新：agent.* 命令解析（或并入 CVarSystem）
    ├── AgentQueryRegistry.{hpp,cpp}    # 新：查询/断言注册表
    ├── AgentReport.{hpp,cpp}           # 新：report JSON 写出
    └── ControlChannel.{hpp,cpp}        # 新（phase2）：loopback TCP

assets/agentscripts/
├── smoke.agentscript.json              # 新：冒烟
├── studiosim-morning-meeting.agentscript.json
├── studiosim-production.agentscript.json
└── studiosim-settlement.agentscript.json

tools/gnb/cmd/gnb/main.go               # 改：newValidateCommand / newDriveCommand
docs/
├── AgentValidation-InputDriver-Design.md  # 本文件
└── gnb-cli.md                          # 改：补 validate/drive
AGENTS.md                               # 改：Agent Interactive Validation 段
```

---

## 14. 附录

### 14.1 命令速查（给写脚本的 agent）

```bash
# 跑一条验证脚本（阻塞、转发退出码、打印 report）
gnb validate --script assets/agentscripts/studiosim-morning-meeting.agentscript.json

# 指定 target / 场景 / 分辨率 / 超时
gnb validate --script X.json --target StudioSim --scene assets/scad/office.scad --width 1920 --height 1080 --timeout 60

# 交互式探索（phase2）
gnb drive --target MagicaLego --scene assets/models/playground.glb
> agent.move norm 0.5 0.5
> agent.click left
> agent.shot
> agent.query engine.frameRate
```

### 14.2 给后续 agent 的接手须知

1. **先读 `docs/WebRTC-RemotePlay-Design.md` 的 §2.2/§2.3/§7.4**——输入注入的论证与 `InjectRelativeMouse` 设计在那已有，本系统复用，不要另起炉灶。
2. 行号会漂，**以函数名定位**：`NextEngine::HandleEvent` / `OnKey` / `OnMouseButton` / `OnCursorPosition` / `TickAgentValidation` / `RequestScreenShot` / `FCVarSystem::ExecuteCommand` / `Window::Close`（PushEvent 先例）。
3. **按里程碑提交**，M1 的单测是整个系统的地基，务必先绿。
4. 迁移 StudioSim（M5）时**保留**"无 LLM / 加速时钟"这类确定性开关，只删"自动点按钮/自动选项"这类**本该由脚本驱动**的硬分支。
5. 不确定的歧义按 `.spec/` 工作流写 blocker，别瞎猜。
