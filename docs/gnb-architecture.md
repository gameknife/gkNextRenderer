# gnb 架构与代码导览

本文面向**要改 `gnb` 源码的人**，讲清楚 `tools/gnb/` 这套 Go 代码是怎么组织的、一次命令从输入到执行经过哪些环节、每个包负责什么，以及如何安全地新增功能。

与现有文档的分工：

- 想知道**怎么用命令** → [`gnb-cli.md`](gnb-cli.md)
- 想要**技术栈/分层概览** → [`gnb-tech-stack.md`](gnb-tech-stack.md)
- 想**读懂并修改源码**（本文）→ 继续往下看
- 本地开发入口 → [`tools/gnb/README.md`](../tools/gnb/README.md)

---

## 1. 一句话定位

`gnb` 是一个**独立的 Go 命令行工具**，不链接引擎本体，职责是统一驱动「构建前准备 → 构建 → 运行 / 测试 / 打包」的全流程，并附带一个本地 Web 控制台（`.spec` 工作流 + 构建/运行/测试/Git/本地 LLM 聊天）。

- 模块路径：`github.com/gameknife/gknextrenderer/tools/gnb`
- Go 版本：1.22（用到 `net/http` 的方法路由 `mux.HandleFunc("GET /path/{id}", ...)`）
- 第三方依赖只有两个：`spf13/cobra`（CLI 框架）、`BurntSushi/toml`（解析 `gnb.toml`）

## 2. 目录结构

```
tools/gnb/
├── cmd/gnb/              # 程序入口 + 所有 cobra 子命令的“装配”
│   ├── main.go           # main()、appContext、各 newXxxCommand 构造器
│   ├── dashboard.go      # dashboard 子命令 + 启动逻辑
│   ├── git.go            # git 子命令（commit-msg / ai-commit 等）
│   ├── todo.go           # todo 子命令族（list/show/next/add/move/...）
│   ├── llm.go            # llm 子命令（setup/serve/status/chat/...）
│   └── init.go           # init 子命令（clone 一个仓库）
└── internal/             # 实际干活的功能包，按职责拆分
    ├── config/           # gnb.toml 解析 + 仓库根查找 + 缓存键
    ├── platform/         # 主机识别、preset、bin 目录、可执行扩展名
    ├── console/          # 统一的 [gnb] 风格终端输出
    ├── cmakerun/         # CMake configure / build / clean
    ├── vcpkg/            # vcpkg bootstrap + 工具链路径 + 内置 cmake/ninja
    ├── fetcher/          # 下载并解包外部工具链（Slang/Streamline/TSC/MoltenVK/Vulkan SDK）
    ├── runner/           # 定位并启动 out/build/<preset>/bin/ 下的可执行文件
    ├── paks/             # 可选 pak 资源的 fetch / publish / list
    ├── packager/         # 打分发包（zip）
    ├── android/, ios/    # 移动端构建入口
    ├── loc/              # 源码行数统计
    ├── spec/             # .spec/ 工作流文件（TODO.md 等）的解析与回写
    ├── gitops/           # 对 git 命令的类型化封装
    ├── llm/              # 本地 llama-server 生命周期 + OpenAI 兼容客户端
    └── dashboard/        # 本地 Web 控制台（HTTP + 内嵌模板 + SSE + 聊天）
```

**心智模型**：`cmd/gnb` 只负责「定义命令、解析参数、拼装上下文」，真正的行为全部下沉到 `internal/*`。新增功能时保持这个边界——别把业务逻辑写进命令定义里。

## 3. 一次命令的生命周期

以 `gnb build gkNextEditor --clean` 为例：

1. **`main()`**（`cmd/gnb/main.go`）启动。
2. `config.FindRepoRoot(".")` 从当前目录向上找，第一个含 `gnb.toml` 或 `.git` 的目录就是仓库根。
3. `config.Load(repoRoot)` 解析 `gnb.toml` 得到 `config.Config`，并补上默认值（Vulkan SDK 版本、LLM 模型清单等）。
4. `cmakerun.DefaultPreset()` → `platform.Detect()` 得到当前平台的 CMake preset（windows / linux / macos-arm64）。
5. 三者打包进 `appContext{repoRoot, cfg, preset}`，传给每个 `newXxxCommand(ctx)`。
6. cobra 解析命令行，匹配到 `build` 子命令；`PersistentPreRunE` 先做一道检查：**没找到仓库根**时，除 `init/help/version/completion` 外的命令都会带提示快速失败。
7. `build` 的 `RunE` 调用 `internal` 各包：必要时 `vcpkg.Ensure` + `fetcher.EnsureExternal` 自举依赖，再 `cmakerun.BuildWithCMake(...)` 执行 configure + build。
8. 错误一路 `return` 回 `main()`，由 `fatal()` 统一打印并 `os.Exit(1)`。

**裸 `gnb`（无子命令）** 走 `root.RunE`，直接启动 dashboard。

## 4. CLI 命令层（`cmd/gnb`）

### 命令是怎么注册的

`main.go` 里每个命令都是一个 `newXxxCommand(ctx appContext) *cobra.Command` 构造器，集中在 `main()` 末尾 `root.AddCommand(...)`。要加新命令：写一个 `newFooCommand`，在 `main()` 里 `AddCommand` 一行即可。

命令实现里**只做三件事**：读 flag → 调 `internal` 包 → 返回 error。例如 `newCleanCommand` 全身就是把 `args[0]` 转成 target 再调 `cmakerun.Clean`。

### `run` 的特殊参数解析

`gnb run` 需要把 target 之后的参数**原样透传**给目标程序（`gnb run gkNextRenderer --help` 要把 `--help` 给引擎而不是 cobra）。所以它设了 `DisableFlagParsing: true`，由 `parseRunArgs` 手写解析：遇到第一个非 `-` 开头的参数当作 target，其余全部进 `opts.Args`；`--` 之后的也全部透传。改 `run` 的参数行为时改这个函数。

### 子命令族

`todo`、`git`、`llm`、`paks` 是「命令族」——一个父命令挂多个子命令。`todo.go` 最典型：`newTodoCommand` 下挂 `list/show/next/add/move/swap/done/delete/block/archive`，每个子命令一个 `newTodoXxxCommand`，共享 `spec` 包做 TODO.md 读写。

## 5. 配置层（`internal/config`）

`config.Config` 是整个 `gnb` 的中心数据结构，一一对应 `gnb.toml` 的各段：

| 字段 | TOML 段 | 用途 |
|------|---------|------|
| `GNB.MinVersion` | `[gnb]` | 最小版本约束 |
| `Vcpkg` | `[vcpkg]` | vcpkg ref / 本地根 / binary cache 目录 |
| `External` | `[external.*]` | Streamline / TSC / MoltenVK / Vulkan SDK / LLM 的下载与配置 |
| `Paks` | `[paks]` | 可选资源仓库、release tag、每个资源落盘位置 |
| `Targets` | `[targets]` | 默认启动目标 + 可运行目标白名单 |

要点：

- `Load()` 解析后会**补默认值**（`applyLLMDefaults` 内置了 Gemma 模型清单、server host/port、idle 等），所以即便 `gnb.toml` 没写 `[external.llm]` 也能跑。
- `FindRepoRoot` 是「向上找 `gnb.toml`/`.git`」，所以桌面命令可以在任意子目录运行。
- `BinCacheKey` 用 `vcpkg.json` 的 sha256 + vcpkg ref + OS 拼出 CI 二进制缓存键。

## 6. 各功能包速查

| 包 | 关键类型 / 入口 | 说明 |
|----|----------------|------|
| `platform` | `Detect()`、`BinDir`、`ExecutablePath`、`CommandExists` | 平台分支的唯一收口处。**不要**把 `runtime.GOOS` 判断散落到别处，优先放这里 |
| `cmakerun` | `BuildOptions`、`BuildWithCMake`、`Clean` | configure（`--preset`）+ build（`--build --preset --target`）。带「是否需要重新 configure」的启发式判断 |
| `vcpkg` | `Ensure`、`Toolchain`、`EnsureBundledCMake/Ninja` | 首次构建自举 vcpkg；Linux/macOS 还会下发内置 cmake/ninja |
| `fetcher` | `EnsureExternal`、`EnsureNamedExternal`、`Download`/`Unzip`/`Untar` | 外部工具链下载解包；含 sha256 校验、Vulkan SDK 平台化安装 |
| `runner` | `Options`、`Run` | 拼出 bin 路径并 `exec`，把 `--present-mode` / `--scene` 转成引擎参数 |
| `paks` | `Fetch`、`Publish`、`List` | 通过 GitHub Release 拉取/发布可选资源 |
| `packager` | `Package` | 把产物打成分发 zip |
| `loc` | `Run(Options)` | `src/` 的源码行数统计，按类别/子项目分组 |
| `spec` | `Parse`、`Document`、`FormatLine` | `.spec/TODO.md` 等工作流文件的解析与回写（见 §8） |
| `gitops` | `GetStatus`、`Branches`、`Log`、`StashAction`… | 对 `git` 命令的薄封装，返回类型化结构体（见 §7） |
| `llm` | `Client`、`Server`、`commitmsg` | 本地 llama-server 生命周期 + OpenAI 兼容聊天客户端 |
| `console` | `Info`/`Success`/`Warn`/`Error`/`Label`/`CommandLine` | 统一 `[gnb]` 风格输出，命令回显也走这里 |
| `dashboard` | `Server`、`routes()` | 本地 Web 控制台（见 §9） |

## 7. gitops：git 的类型化封装

`gitops` 把零散的 `git` 子进程调用收敛成一组返回结构体的函数：`Status` / `Branch` / `RemoteBranch` / `Commit` / `Stash` / `FileChange`。所有调用最终都走私有的 `run(dir, args...)`，统一处理工作目录和错误。

dashboard 的 Git 标签页、`gnb git` 命令都复用它——想改 git 行为，改这里一处即可，不要在 handler 里直接拼 `git` 命令。

## 8. spec：TODO.md 的可往返编辑

`.spec/` 工作流（见仓库根 `.spec/README.md`）的所有文件读写都在 `spec` 包：

- `Parse(path)` → `*Document`：解析 `TODO.md`，**同时保留原始行缓冲**，这样编辑后 `Save()` 能最小改动地写回（保留无关行的原样）。
- `Task` 是 TODO.md 里的一行；`FormatLine(t)` 生成它的规范文本。
- 三个区段：`SectionNext`（下一步）、`SectionBacklog`（待规划）、`SectionRecent`（最近完成）。
- 编辑操作：`AppendTask` / `EditTask` / `MarkStatus` / `MoveTask` / `SwapTasks` / `DeleteTask`，都走「改行缓冲 → 必要时 `reloadFromLines` 重解析」。
- `archive.go`、`journal.go`、`paths.go` 分别管归档、完成报告 stub、各种路径常量。

`gnb todo *` 命令和 dashboard 的 TODO 标签页是同一套 `spec` API 的两个前端。

## 9. dashboard：本地 Web 控制台

这是代码量最大的包，`gnb`（裸命令）或 `gnb dashboard` 启动它。

### 9.1 启动与请求生命周期

- `Server`（`server.go`）持有 4 样东西：`opts`、`tpl`（解析好的模板）、`jobs`（任务管理器）、`chats`（聊天会话存储）。
- 模板用 `//go:embed templates/*.html` 内嵌进二进制，`New()` 里**一次性**解析（解析失败在绑定端口前就报错）。
- `Run()` 监听 `127.0.0.1:<port>`（默认 7777），用 `routes()` 返回的 mux 提供服务，并尽力打开浏览器。
- `routes()` 是**唯一的路由表**：用 Go 1.22 的方法路由把每条 `METHOD /path` 映射到一个 handler，最外面包一层 `logRequests` 打访问日志。

前端是 htmx 驱动的服务端渲染：大多数交互是 POST 一个动作 → 服务端改文件/起任务 → 返回一段 HTML 局部，由 htmx 换入页面。

### 9.2 handler 是怎么分文件的（重构后）

原来所有 handler 挤在一个 1700 行的 `handlers.go` 里。现已按**功能域**拆开，同包多文件：

| 文件 | 内容 |
|------|------|
| `handlers.go` | 路由表 `routes()`、`logRequests`、所有视图模型（`indexVM` 等 `xxxVM` 结构体）、首页与 Tab 分发（`handleIndex`/`handleTab`）、各 Tab 的 VM 构造器、共享 helper（`render`/`httpError`/`parsePathID`/`relativeTime`） |
| `handlers_todo.go` | TODO/任务标签页：面板渲染 + 对 TODO.md 的增删改/标记。含 `loadTODO` helper |
| `handlers_chat.go` | 本地 LLM 聊天 UI：聊天视图模型、会话生命周期、发送/流式接口 |
| `handlers_git.go` | Git 标签页：状态、分支、暂存/提交、stash、commit message 生成 |
| `handlers_jobs.go` | 构建/运行/测试任务的启动、取消、SSE 流式日志，以及 JobSpec 构造器 |

每个文件顶部都有一行注释说明职责。`chat_tools.go`（聊天的工具调用循环）、`jobs.go`（任务执行/缓冲）、`chat.go`（会话持久化）、`tests.go`、`ansi.go`、`activation.go` 按机制单独成文件。

### 9.3 视图模型与渲染

每个标签页对应一个 `xxxVM` 结构体（`indexVM`/`buildVM`/`gitVM`/`chatVM`/`locVM`…），handler 把数据塞进 VM，再调 `s.render(w, "模板名", vm)` 执行对应模板。出错统一 `httpError(w, err)`（写 500）。模板里要用的小函数（图标、时间格式、千分位、git 状态色等）集中在 `server.go` 的 `templateFuncs()`。

### 9.4 任务与 SSE（`jobs.go` + `handlers_jobs.go`）

构建/运行/测试是长任务，用「任务 + 服务端推送」模型：

- `JobManager.Start(JobSpec)` 起一个子进程，`pumpLines` 把 stdout/stderr 逐行读进**行缓冲**，同时把 `JobEvent` 广播给所有订阅者。
- 浏览器通过 `GET /jobs/{id}/stream`（`handleJobStream`）建立 SSE，实时追加日志行；`JobSnapshot` 用于首屏把已有缓冲渲染出来。
- `buildJobSpec` / `runJobSpec` / `testJobSpec` 负责把一个 target 翻译成具体命令行。

### 9.5 聊天与工具调用（`chat.go` + `chat_tools.go`）

- `ChatStore`（`chat.go`）管理多会话，持久化到一个 JSON 文件（`chatStorePath`）。
- `runChatToolLoop`（`chat_tools.go`）是**本地 LLM 智能体**的核心：多轮地让模型产出「工具调用 JSON」，执行**只读**工具（列目录、找文件、搜文本、找符号、读文件、跑只读命令），把结果回灌给模型，直到模型给出最终回答。`validateReadOnlyCommand` 保证 `run_cmd` 工具只能跑安全命令。
- 引擎层和 dashboard 复用同一个 `llama-server`，host/port/model 通过 `external/llm/run/server.pid` 自动发现。

## 10. 实操指南

### 新增一个 CLI 子命令

1. 在 `cmd/gnb/` 写 `newFooCommand(ctx appContext) *cobra.Command`，`RunE` 里只解析 flag 并调用 `internal` 包。
2. 在 `main.go` 的 `main()` 里 `root.AddCommand(newFooCommand(ctx))`。
3. 真正的逻辑放进（或新建）一个 `internal/foo` 包，保持命令层薄。
4. 若命令无需仓库根（像 `init`），把它加进 `repolessCommands`。

### 新增一个 dashboard 接口

1. 在 `routes()`（`handlers.go`）加一行 `mux.HandleFunc("POST /foo", s.handleFoo)`。
2. 把 `handleFoo` 写进**对应功能域**的文件（git 相关进 `handlers_git.go`，依此类推）；纯新功能域就新建 `handlers_foo.go` 并在文件头加一行职责注释。
3. 需要新数据就加一个 `fooVM` 结构体（放 `handlers.go` 的视图模型区或功能文件里），用 `s.render(w, "tab_foo", vm)` 渲染；模板加在 `templates/*.html`。
4. 出错走 `httpError(w, err)`；解析 TODO.md 用 `s.loadTODO(w)`。

### 扩展本地 LLM 工具

在 `chat_tools.go` 的 `executeChatTool` 分发里加一个 case，实现一个 `toolXxx(...)`。**务必保持只读**，并在 `validateReadOnlyCommand` / 工具说明里同步约束，避免让模型获得写权限。

## 11. 构建、测试与验证 gnb 自身

```bash
cd tools/gnb
go build ./...        # 编译全部包
go vet ./...          # 静态检查
go test ./...         # 跑单测（Catch2 之外，这里是 gnb 自己的 Go 测试）
```

- 单测覆盖了 `spec` / `dashboard` / `llm` / `fetcher` / `cmakerun` / `platform` / `ios` / `cmd/gnb` 等关键包。
- **已知例外**：Windows 上 `fetcher` 的 `TestUntarPreservesSymlink` 会因「创建符号链接需要权限」失败，这是宿主环境限制（需要开发者模式/管理员），与逻辑无关。
- 发布：根目录 `gnb.bat` / `gnb.sh` 是很薄的 shim——本机有 Go 就从 `tools/gnb` 重新编译，否则用 `tools/gnb-bin/` 缓存或从 GitHub Release（`paks-latest`）下载，机制见 `gnb-cli.md` 的「Publish gnb Binaries」。

## 12. 代码约定

- 命名遵循 Go 习惯（导出 `PascalCase`、包内 `camelCase`），平台分支只在 `internal/platform` 出现。
- 子进程调用统一经过各包的 `run(...)` 私有函数，便于统一处理工作目录与错误回显。
- 终端输出一律走 `console`，不要散用 `fmt.Println` 拼前缀。
- 同包拆多文件时，按**功能域**而非「函数/类型」机械切分；单一职责、已内聚的文件（如 `gitops.go`、`chat_tools.go`）不必为了行数硬拆。

---

> 维护提示：本文描述的是当前结构。若新增/重命名了 `internal` 包或 dashboard handler 文件，请同步更新 §2、§6、§9.2 的对照表。
