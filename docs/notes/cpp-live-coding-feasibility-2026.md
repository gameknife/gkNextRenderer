# gkNextEngine C++ Live Coding 可行性复调研

> 调研日期：2026-07-16
> 范围：开发期 C++ 函数级热更新；不讨论现有 TypeScript / Slang 热重载
> 结论状态：Windows 方案已按本文边界完成集成和 smoke test；暂不实施跨平台方案
> 实施入口：[`AGENT_GUIDE/HotReload.md`](../../AGENT_GUIDE/HotReload.md)

## 结论摘要

目前存在一个对引擎侵入较小、且与现有静态链接架构相容的可行方案：**Windows 开发环境下集成 Live++，直接修补已经链接进应用程序的 `.obj`，不把 `gkNextEngine` 或 Modules 改造成可卸载 DLL。**

推荐把第一阶段的支持合同严格限定为：

- 允许修改已有 `.cpp` 中、未内联的既有函数体，例如算法、分支、计算公式、局部变量和对已有函数的调用。
- 修改类布局、虚函数表、函数签名、头文件、反射声明/注册、全局或静态对象初始化、模块安装/卸载逻辑时，停止程序后正常构建并重启。
- 不做 Unreal 式对象重实例化，不做跨 DLL 状态迁移，也不承诺 Linux、macOS、Android 或 iOS 的 C++ live coding。

这条路线的关键不在于新 Modules 能否动态卸载，而在于 Live++ 官方支持修补 `.exe`、`.dll` 和静态 `.lib` 中的对象文件。现有 `gkNextEngine`、`NextGameplay` 及各 feature module 可以继续保持静态库形态。Modules 架构只负责承载可选集成和生命周期入口，不需要成为新的 ABI 边界。

有两个前置门槛：

1. Live++ 是商业、按席位授权的软件，需要团队接受许可模式；可以先使用完整功能试用版验证。
2. 本仓库当前生成器是 `Visual Studio 18 2026`。Live++ 2.11.3（2026-07-08）已明确附带 Visual Studio 2026 solutions/projects，这是很强的兼容信号；仍需用本仓库真实的 MSVC toolset、PDB、静态库和 unity 输出做一次 smoke test，不能只验证 vendor sample。

如果以上门槛通过，建议试点；如果试点要求先把引擎拆成 shared engine + host + plugin，应该立即停止该方向，因为那已经不再是“低侵入”方案。

## 目标与判断标准

这里的“低侵入”指：

- 不改变运行时对象所有权和内存分配边界。
- 不引入可卸载 game DLL、shadow copy、稳定 C++ ABI 或游戏状态序列化。
- 不要求把第三方库和现有静态 module 全部重新组织。
- 默认关闭，只影响明确启用的 Windows 开发配置，不影响 Shipping、CI、移动端和其他平台。
- 集成代码局限在构建开关、LiveCoding module 内部，以及一个明确的帧边界轮询点。

目标是把常见的“修改函数体 → 保存 → 数秒后看到新行为”循环变快，而不是允许任意 C++ 结构变化。

## 当前仓库的约束

### 架构现状

| 项目现状 | 对 C++ live coding 的影响 |
|---|---|
| `gkNextEngine` 和 `src/Modules/*` 均为静态库 | 适合对象代码修补；不适合直接套用可卸载 DLL 方案 |
| 应用通过 `DesktopMain.cpp` 创建引擎并静态安装 feature modules | 有清晰的集成入口，但不是动态 ABI |
| `Modules/LiveCoding` 当前只安装 Slang shader reloader | 可以作为 C++ live coding 的归属模块，但必须避免把两种机制混为一谈 |
| ReflectionRegistry 和各 module registry 显式、一次性注册 `entt::meta` | 热更反射布局或注册代码会留下旧 metadata 和旧对象，必须重启 |
| 引擎、物理、AI、远程服务等存在工作线程 | 在任意时刻换函数实现可能跨越任务执行边界；同步到帧边界只能降低风险，不能自动暂停所有 worker |
| Windows 默认 `RelWithDebInfo`：`/O2 /Ob1`、`/Zi`、`/MT` | Live++ 可覆盖这种优化与静态 CRT 配置；VS 原生 C++ Hot Reload 不适合该配置 |
| 已启用 `/INCREMENTAL /OPT:NOREF /OPT:NOICF` | 与 Live++ 建议一致 |
| Unity build 默认开启 | Live++ 有官方 unity/jumbo 文件拆分支持；第一次触碰某个 unity bucket 仍可能较慢 |
| LTO 默认关闭 | 符合 Live++ 要求；启用 LTCG/LTO 的目标不能进入支持范围 |
| 非 Windows 默认 `-fvisibility=hidden` | 会进一步限制实验性跨平台 patcher 的符号发现能力 |

### 新 Modules 架构带来的实际帮助

新架构确实比旧的应用层“大一统”入口更利于试点：

- `Modules::LiveCoding::Install(NextEngine&)` 已经是明确、可选的开发功能安装点。
- 可以只让选定 program 链接 C++ live coding SDK，不污染所有目标。
- module 的源文件边界便于设置 source allowlist 和统计 Engine / Module / Application 三类热更效果。

但它**没有**把 module 变成可安全卸载的插件：

- module 目前 `STATIC` 链接，并没有稳定的 C ABI。
- 如果简单改成 `SHARED` 且继续各自链接静态 `gkNextEngine`，可能复制引擎全局状态、反射 registry 和 singleton。
- Windows 当前使用 `/MT`；跨 DLL 传递由不同 CRT heap 分配的 C++ 对象会增加释放和异常边界风险。
- Linux 默认隐藏符号，Engine 的导出 API 也没有覆盖完整的 module 使用面。
- callback、线程、ECS component、renderer resource 和注册表都需要明确撤销后，DLL 才可能安全卸载。

因此，Modules 架构适合作为 **Live++ 集成边界**，并没有降低自研 DLL hot reload 所需的 ABI、状态和生命周期成本。

## 旧方案历史审计

Git 历史里有两次相邻变更：

- `8eb324da`（2026-05-07）：`Add shader and C++ hot reload workflow`
- `14bebe69`（2026-05-08）：`Remove C++ hot reload plugin path`

复核 `8eb324da` 的实际 tree 后，提交内容与当时文档描述并不一致：文档声称已经存在 `PluginLoader`、shadow copy、ABI version、状态保存、`gkNextEngineShared`、`gkNextHost` 和 `FlappyCppPlugin`，但对应源码、symbol 和计划文档均不在该提交中；CMake 还引用了不存在的 `Application/PluginEntry.cpp`。可复现检查方式：

```powershell
git show --name-status 8eb324da
git ls-tree -r --name-only 8eb324da
git grep -n "PluginLoader\|gkPluginAbiVersion\|HotReloadState" 8eb324da
```

所以那次提交应视为未闭环的 CMake / 文档骨架，不能作为“完整方案做过但失败”的实现证据。次日移除提交恢复了 monolithic executable + static engine。

不过，旧方向暴露出的成本判断仍然成立：为了 reload game DLL 而引入 shared engine、host、ABI、shadow copy 和状态迁移，改造层级远高于“只更新已有函数体”。本次不应沿原路径重做。

## 候选方案盘点

| 方案 | 机制与平台 | 能否覆盖现有静态 Engine / Modules | 侵入度 | 维护与风险 | 本项目判断 |
|---|---|---:|---:|---|---|
| **Live++** | Windows 进程内对象代码修补；另支持部分主机平台 | **官方支持 `.lib`** | 低 | 商业授权；当前工程组合待实测 | **唯一推荐进入正式试点** |
| Visual Studio C++ Hot Reload / Edit and Continue | MSVC 调试器和 IDE 工作流 | **不更新静态库代码** | 表面低，若迁就它改库形态则高 | 需 `/ZI`、非优化调试配置，限制较多 | 仅可作 Application `.cpp` 的免费窄域备选 |
| Blink | Windows 外部 PDB/COFF 函数 patcher | 理论上可尝试已链接对象 | 很低 | 最近代码活动停在 2023；新 MSVC、unity 和大型工程兼容性未知 | 可做零成本 smoke test，不作为主方案 |
| jet-live | Linux x86_64 / macOS arm64 函数 hooking | 依赖 compile database 和符号 | 中 | API 必须同线程调用；偏好 `-O0`、非 hidden symbols | 只适合隔离实验，不适合当前优化配置 |
| `cr.h` | host 动态加载 guest DLL / SO / dylib，C entry point | 否，需要重组为插件 | 高 | 跨平台、活跃，但复杂 C++ 和跨模块 heap 有额外风险 | 与旧 plugin 路线同类，不推荐 |
| RuntimeCompiledCPlusPlus | 运行时编译 + RuntimeObjectSystem + 状态序列化 | 不是透明覆盖，需要采用其对象模型 | 很高 | 跨平台，但长期绑定专用 runtime object 体系 | 不符合低侵入目标 |
| 自研 DLL / SO 插件 | 自己定义 ABI、卸载和状态迁移 | 否，需要 shared engine 或稳定 C ABI | 很高 | 生命周期、线程、资源、reflection、allocator 均由项目维护 | 暂不考虑；仅适合未来主动设计的窄 gameplay sandbox |
| LLVM ORC / Cling 类 JIT | 运行时编译和符号重绑定 | 不是现成的原生工程 patcher | 极高 | 改变编译、符号和状态模型 | 排除 |

没有发现一个同时满足“Windows + Linux + macOS、活跃维护、支持优化构建和多线程大型 C++、透明覆盖现有静态库、低侵入”的开源方案。跨平台如果成为硬要求，问题会升级为架构项目，而不是工具接入。

## 首选方案：Live++

### 为什么与本项目匹配

Live++ 的工作层级是编译单元和对象代码，而不是业务 plugin：

- 2.11.3 发布于 2026-07-08，官方示例已经覆盖 Visual Studio 2026，项目仍处于活跃维护状态。
- 官方文档明确支持 `.exe`、`.dll`、`.lib` 的任意组合，并能重新编译静态库里的单个对象文件。
- 支持 MSVC 和受支持版本的 Clang、优化构建、`/MT` 静态 CRT，不依赖 Visual Studio debugger。
- 官方支持 unity / jumbo / blob build：首次修改时拆出对应 `.cpp`，后续可只处理较小单元。
- Synchronized Agent 可以由程序在选定的时机检查并应用 patch，适合放在主循环帧边界。
- 编译失败时旧代码继续运行，符合交互式算法迭代需求。
- Unreal Engine 的 Live Coding 也是基于 Live++；Unreal 额外做的 Object Reinstancing 是其结构变更能力，不是函数体 patch 的必要条件。

因此无需为了工具把现有静态 module 改成 DLL，也无需先解决 C++ ABI 和对象迁移。

### 构建条件对照

| Live++ 条件 | 当前仓库 | 试点动作 |
|---|---|---|
| 完整 PDB，编译使用 `/Zi` 或 `/Z7` | 当前 `RelWithDebInfo` 使用 `/Zi` | 保持，并确认 PDB、`.obj` 和 source 在运行期间未被清理 |
| incremental link | 已有 `/INCREMENTAL` | 保持 |
| 不消除/折叠可 patch 函数 | 已有 `/OPT:NOREF /OPT:NOICF` | 保持 |
| function padding | x64 需要 linker `/FUNCTIONPADMIN` | 仅对启用 live coding 的应用目标增加 |
| 完整 link debug info | 当前没有显式保证 `/DEBUG:FULL` | 对试点目标显式增加 |
| function/data packaging | `/Gy /Gw` 为官方建议 | 对参与热更的 first-party targets 增加并实测构建影响 |
| 禁止 whole-program optimization | `ENABLE_LTO=OFF` | 开关启用时强制检查并拒绝 LTO/LTCG |
| 可重放原始编译命令 | VS CMake 工程和 PDB 可提供 | 用真实 unity object 验证，不只跑 vendor sample |
| 受支持的 compiler / linker | 当前 VS 18 2026 | 2.11.3 已提供 VS 2026 示例；仍需验证本仓库的精确 toolset/PDB/unity 组合 |

当前 `/O2 /Ob1` 不需要为了 Live++ 整体降为 Debug，但 live-coded 算法应定义在 `.cpp` 的非 `inline` 函数里。已经内联到调用者的代码不会因为只 patch 原函数就自动改写所有 caller。

### 建议的支持合同

第一阶段宁可少承诺，不引入对象迁移。

| 变更类型 | 等级 | 约定 |
|---|---:|---|
| 修改已有 `.cpp` 非内联函数体中的算法、分支、字面量 | 绿 | 正式支持 |
| 增删局部自动变量，调用已有 API | 绿 | 正式支持；不改变跨帧对象布局 |
| 修改 Engine、Gameplay、Module 或 Application 静态库中的已有函数体 | 绿 | 试点必须分别验证后才宣称支持 |
| 增加 free/helper function，或修改不影响布局的 `.cpp` 内部类型 | 黄 | Live++ 能力允许时可用，但不列入首期保证 |
| 修改 `.hpp/.h`、inline、template、`constexpr` | 黄 | 可能触发大范围依赖重编和旧实例不一致；首期统一要求重启 |
| 修改函数签名、成员字段、字段顺序、继承关系、enum underlying type | 红 | 正常构建并重启 |
| 新增、删除或修改 virtual function / virtual base | 红 | 会改变 vtable；正常构建并重启 |
| `REFLECT_COMPONENT`、`RegisterReflection()`、`entt::meta` 属性或 reflected component 布局 | 红 | 旧 metadata 与 ECS 实例无法自动迁移；正常构建并重启 |
| 全局、function-static、TLS 对象的初始化或生命周期 | 红 | patch 不等于重新初始化现有状态；正常构建并重启 |
| module `Install()`、registry/callback 所有权、线程启动停止 | 红 | 可能产生重复注册和悬空 callback；正常构建并重启 |
| 新源文件、CMake、依赖、编译选项、第三方代码 | 红 | 重新 configure/build/restart |

这里的“红”表示不进入本项目的 live coding 支持范围，不等于 vendor 绝对无法处理。Live++ 提供结构变化和对象迁移 hook，但在 gkNextEngine 中使用它们意味着要枚举并迁移 ECS component、scene node、renderer/service 对象以及各种缓存；其复杂度已经违背本次目标。

### 最小集成形态

建议后续实现时遵守以下边界：

1. 新增 `GK_ENABLE_CPP_LIVE_CODING`，默认 `OFF`，仅 Windows developer preset 可开。SDK 通过本地路径或环境变量发现，许可文件和 vendor binary 不提交到仓库。
2. 保持 `gkNextEngine`、`NextGameplay` 和所有 modules 为静态库。只对明确的应用 target 启用 Live++ agent，并让它覆盖链接进该 executable 的 first-party objects。
3. 在现有 `Modules/LiveCoding` 内增加独立的 `CppLiveCodingService`；shader reload 与 C++ patch 使用不同日志前缀、状态和开关。
4. 使用 Synchronized Agent，在 `SDL_AppIterate` 调用 `NextEngine::Tick()` 之前的外层帧边界检查并应用 reload。关闭顺序应先停 agent，再销毁 Engine。
5. 只启用当前主 `.exe`，不使用 `LPP_MODULES_OPTION_ALL_IMPORT_MODULES` 扫描所有导入 DLL。支持策略只承诺所选应用的 `src/Application`、`src/Engine`、`src/Gameplay`、`src/Modules`；`src/ThirdParty`、vcpkg、generated code 和外部 SDK 一律不在范围内。由于静态 third-party object 仍可能存在于主 executable 的 PDB 中，试点时还要确认 Live++ 是否能按 compiland/source path 过滤；不能把 module filter 误当成 source filter。
6. 首期不自动解析“某次改动是不是安全”。文档、日志和团队约定规定：触碰 header / reflection / layout 后由开发者停止并正常重建。
7. 不把同步帧边界描述成 worker quiescence。先选择不在长时间 worker 栈上执行的算法试点；若以后需要 patch 任务系统热点，再增加明确的 cooperative pause，而不是隐式假定安全。

预计代码侵入面应局限在一个 CMake helper、`Modules/LiveCoding` 内部服务和一个帧边界调用点。若实现过程中需要改动 Engine 对外对象模型或 module ABI，应视为方案偏离。

## 试点与验收

### Gate 0：工具链和许可

- 使用 Live++ 2.11.3 或更新版本的 30 天完整功能试用版，验证 `Visual Studio 18 2026`、当前 MSVC toolset、Visual Studio linker 和当前 CMake unity 输出。官方 VS 2026 example 只能证明工具链已进入支持面，不能替代真实工程验证。
- 若 VS 2026 不兼容，先尝试专门的 VS 2022 live-coding preset；不要因此直接启动 shared engine / plugin 重构。
- 确认团队能接受按席位商业授权和 SDK 分发约束。

### Gate 1：最小功能矩阵

建议先选 `FlappyCpp` 作为行为易观察的应用，再至少各验证一个真实对象来自 Engine 和 feature Module：

| 用例 | 验收点 |
|---|---|
| Application `.cpp` 中修改一个既有算法 | 不重启进程，下一帧行为改变 |
| `gkNextEngine.lib` 中修改一个既有函数体 | 证明不是只 patch 最终 executable 自身源码 |
| 某个静态 feature module 中修改一个既有函数体 | 证明新 Modules 架构下 `.lib` object 能被定位 |
| 制造一次 C++ 编译错误 | 运行中的旧实现保持可用；修正后可以继续 reload |
| Unity bucket 首次和第二次修改 | 记录编译、link/patch 总时间，确认后续循环确实缩短 |
| 连续 20 次函数体 reload | 无 crash、无明显资源/线程泄漏，调试断点仍可用 |
| 修改一个长时间调用链中的函数 | 确认旧栈帧退出、再次进入后才表现为新逻辑 |
| 触碰 reflection/header | 明确提示“不在支持范围”，执行正常 rebuild/restart 流程 |

建议把“保存到行为生效”的 p50 / p95、首次 unity 拆分耗时、内存增长和失败恢复次数写入试点记录。是否达标应以团队当前 `gnb build <target>` 的实际迭代时间为对照，而不是 vendor demo。

### Go / No-Go

满足下列条件才继续推广：

- VS 2026 或专用 VS 2022 preset 稳定可用。
- Application、Engine static lib、Module static lib 三层均能 patch。
- 连续 reload、编译失败恢复、debugger 和现有 shader hot reload 可以共存。
- 实测迭代时间显著优于 targeted incremental build + restart。
- 许可与本地 SDK 管理方式被团队接受。

出现下列任一情况则停止推广：

- vendor/toolchain 不能可靠处理当前 MSVC/PDB/unity objects。
- 为覆盖静态库而被迫引入 shared engine、game DLL 或状态序列化。
- 常见函数体修改仍频繁受线程执行、PDB 或 linker 问题影响。
- 团队实际修改以 component layout、reflection/header 和 template 为主，函数体热更收益不足。

## 其他方案的具体定位

### Visual Studio C++ Hot Reload

优点是免费且 CMake 项目受支持，但 Microsoft 的 supported code changes 文档明确列出优化代码和静态库限制；静态库中的变更不会被更新。当前关键代码恰好主要位于 `gkNextEngine.lib` 和 module `.lib`，默认又是 `/O2 /Zi`，而不是 Edit and Continue 使用的 `/ZI` 非优化配置。

可以增加一个 Debug + `/ZI` preset，给少量直接编进 application target 的 `.cpp` 使用，但它不能实现用户期望的“引擎算法也能 live coding”。为了迎合它而把静态库改成 DLL 或 OBJECT 聚合，侵入度不再低。

### Blink

Blink 的吸引力是外部 launch/attach、无需项目集成，适合花半天验证 PDB function patch 的上限。它公开支持 Windows x86/x64、MSVC、PDB/COFF，并保留 global state。

但官方仓库最后一次代码提交停留在 2023-12，README 没有对 VS 2026、现代 unity CMake 大项目或静态库对象重放给出明确承诺。可把它作为免费探索性基线，不能把生产工作流押在其上。

### jet-live

jet-live 仍有近期提交，支持 Linux x86_64 和 macOS arm64，也强调不需要特殊的业务代码组织。但其 README 明确说明 API 不是线程安全的，所有 library method 必须从同一线程调用；作者只在 `-O0`、不 strip、没有 `-fvisibility=hidden` 的配置下使用，并判断高度优化构建很可能无法工作。

调用线程约束本身可以由主线程遵守，但它没有解决 engine worker 正在执行被替换代码时的协作问题；更直接的阻碍是 gkNextEngine 当前的 `RelWithDebInfo` 和 Linux hidden symbols。除非做一个小 target 的隔离实验，否则不建议接入主引擎。

### `cr.h` 与 RuntimeCompiledCPlusPlus

`cr.h` 很小、跨平台且仍活跃，但本质是让 thin host 动态装载 guest library，要求稳定的 `cr_main` 边界和跨 reload state。它自己的文档也提醒复杂 C++ 场景更危险；Windows host/plugin 间共享 heap 还要求一致的动态 CRT 约定。对当前 `/MT`、静态 engine 架构而言，这就是旧 game plugin 路线的另一种实现。

RuntimeCompiledCPlusPlus 通过 RuntimeObjectSystem、runtime compiler 和 serialization 提供更深的对象替换能力。能力更强的代价是业务类型需要进入它的对象和状态模型，不是透明的函数 patch。它更适合从项目早期就围绕该框架设计的 engine，不适合当前“少改引擎”的目标。

## 最终建议

**建议做 Live++ 的 Windows-only、函数体-only 技术试点；不要恢复旧的 C++ game DLL hot reload 架构。**

新 Modules 架构使集成落点更干净，但真正降低侵入度的是 Live++ 能修补静态库 object。首期把反射、header、对象布局、module lifecycle 和静态状态全部划为 restart boundary，恰好符合可接受的 Unreal 式限制，并避免复制 Unreal 的对象重实例化复杂度。

Live++ 2.11.3 已经把 VS 2026 纳入官方 examples。后续集成使用本仓库 VS 18 / MSVC 14.51、`RelWithDebInfo`、静态 Engine/Modules 和 unity build 完成验证：Agent 能连接 Broker、识别 1957 个 translation units，并在同一 `gkNextRenderer` 进程中连续应用函数体 patch。实现保持默认关闭，没有引入 shared engine、plugin ABI 或对象迁移。

## 资料来源

本节只列官方文档、官方仓库和本仓库可复现证据；能力与维护状态截至 2026-07-16。

- Live++：[产品与平台](https://liveplusplus.tech/)、[完整文档](https://liveplusplus.tech/docs/documentation.html)、[Integration](https://liveplusplus.tech/integration.html)、[FAQ](https://liveplusplus.tech/faq.html)、[Release history](https://liveplusplus.tech/releases.html)、[Pricing / trial](https://liveplusplus.tech/pricing.html)、[EULA](https://liveplusplus.tech/downloads/LPP_EULA.pdf)
- Epic Games：[Live Coding in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-live-coding-to-recompile-unreal-engine-applications-at-runtime)
- Microsoft：[C++ Hot Reload](https://learn.microsoft.com/en-us/visualstudio/debugger/hot-reload?view=visualstudio)、[Supported C++ code changes](https://learn.microsoft.com/en-us/visualstudio/debugger/supported-code-changes-cpp?view=visualstudio)、[`/Z7`、`/Zi`、`/ZI`](https://learn.microsoft.com/en-us/cpp/build/reference/z7-zi-zi-debug-information-format?view=msvc-170)
- Blink：[官方 GitHub 仓库](https://github.com/crosire/blink)
- jet-live：[官方 GitHub 仓库](https://github.com/ddovod/jet-live)
- `cr.h`：[官方 GitHub 仓库](https://github.com/fungos/cr)
- RuntimeCompiledCPlusPlus：[官方 GitHub 仓库](https://github.com/RuntimeCompiledCPlusPlus/RuntimeCompiledCPlusPlus)
- 本仓库：[Modules CMake](../../src/Modules/CMakeLists.txt)、[TargetHelpers](../../src/cmake/TargetHelpers.cmake)、[当前 Hot Reload 说明](../../AGENT_GUIDE/HotReload.md)、[ReflectionRegistry](../../src/Engine/Runtime/Reflection/ReflectionRegistry.cpp)、[Reflection macros](../../src/Engine/Runtime/Reflection/ReflectionMacros.hpp)
