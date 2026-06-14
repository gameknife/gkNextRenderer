---
title: "TypeScript 整合说明"
category: guide
status: 现行
owner: engine
created: 2026-05-14
last_updated: 2026-06-12
---

# TypeScript 整合说明

gkNextEngine 的 TypeScript 链路不是独立的 Web 工具链，而是服务于引擎运行时的脚本层：开发者在 `assets/typescript` 编写 TypeScript，运行时使用仓库内置的 `tools/tsc/tsc[.exe]` 编译到 `assets/scripts`，然后由 QuickJS 以 ES module 形式加载。这个设计让脚本热重载、编辑器验证和玩法原型都能在不依赖 Node/npm/全局 `tsc` 的前提下运行。

## 一句话概览

- 源码层：TypeScript 源文件位于 `assets/typescript`
- 编译层：`assets/typescript/tsconfig.json` 输出 JavaScript 到 `assets/scripts`
- 工具链层：`gnb setup` 准备 `tools/tsc/tsc[.exe]`，CMake 把它复制到运行时 `tools/tsc`
- 运行层：应用显式安装 `NextQuickJS` 后，`QuickJSEngine` 启动时按配置编译 TypeScript、重建上下文并加载入口脚本
- 热重载层：桌面端每 0.5 秒检查一次源文件变化，编译成功后重载 QuickJS 上下文，编译失败时保留旧脚本
- 绑定层：C++ 反射与手写 QuickJS API 共同生成 `Engine.d.ts` 和运行时 `Engine` 模块

## 文件布局

```text
assets/
├── typescript/
│   ├── tsconfig.json
│   ├── Engine.d.ts
│   ├── NextGameInstanceBase.ts
│   ├── test.ts
│   └── flappy/
└── scripts/
    ├── .tsc.stamp
    ├── Engine.js / Engine.d.ts 映射入口由 QuickJS 特判
    └── 编译后的 .js / .map 文件

tools/
└── tsc/
    └── tsc.exe 或 tsc

src/Modules/NextQuickJS/
├── NextQuickJSModule.hpp/.cpp
├── QuickJSEngine.hpp/.cpp
├── QuickJSBindings*.cpp
└── Reflection/
```

`assets/typescript` 是人工维护的脚本源码目录。`assets/scripts` 是运行时读取的编译输出目录，里面的 `.tsc.stamp` 用于避免每帧重复编译。核心引擎没有默认脚本入口；应用通过 `Modules::NextQuickJS::Install()` 的 `FConfig::entryScript` 显式指定，例如 `FlappyJs` 使用 `assets/scripts/flappy/FlappyJs/FlappyJsGameInstance.js`。

## 构建和工具链准备

`gnb.toml` 的 `[external.tsc]` 记录了跨平台 TypeScript 编译器下载地址。执行 `gnb setup` 或首次构建时，`gnb` 会把对应平台的编译器准备到 `tools/tsc`。

构建资源时，`assets/CMakeLists.txt` 会把 `tools/tsc` 复制到构建输出旁边：

```text
out/build/<preset>/tools/tsc/tsc[.exe]
```

运行时查找顺序覆盖了几种常见场景：

- 从构建输出目录直接运行可执行文件
- 通过仓库根目录的 `gnb run` 启动
- 从源码树或复制后的运行时布局启动

因此桌面二进制不要求当前工作目录必须是 `out/build/<preset>/bin`。

## 运行时编译流程

当 `FConfig::compileTypeScript` 为 true 时，`QuickJSEngine::Initialize()` 会先调用 `CompileTypeScriptSources()`，再调用 `ResetContextAndLoadScript()`。

编译流程的核心步骤是：

1. `ResolveTypeScriptPaths()` 定位 `assets/typescript/tsconfig.json` 和 `assets/scripts`
2. `UpdateTypeScriptDefinitions()` 根据当前 C++ 绑定生成 `assets/typescript/Engine.d.ts`
3. 检查 `assets/typescript/**/*.ts`、`tsconfig.json` 与 `assets/scripts/.tsc.stamp` 的时间戳
4. 有变化时执行 bundled `tsc -p assets/typescript/tsconfig.json`
5. 编译成功后刷新 `.tsc.stamp`
6. 编译失败时打印警告，并继续使用已有 JavaScript 输出

这个策略把失败影响限制在脚本层：TypeScript 写错不会直接让引擎初始化路径崩溃，但旧脚本是否还能满足当前逻辑仍需要开发者自行验证。

## QuickJS 模块加载

编译后的脚本以 ES module 形式运行。`QuickJSEngine` 的 module loader 会处理三类路径：

- 内建模块 `Engine`
- 指向 `Engine` 的相对路径，如 `./Engine`、`../Engine`
- 编译输出树里的普通相对导入，如 `./helper`、`../FlappyCommon`

TypeScript 侧通常这样导入引擎 API：

```ts
import * as NE from "./Engine";
```

不同目录深度的脚本也可以使用 `../Engine` 或直接使用 `Engine`，loader 会把这些形式映射回内建 `Engine` 模块。

## 生命周期和游戏实例

脚本化游戏建议继承 `NextGameInstanceBase`：

```ts
import { NextGameInstanceBase, RunGameInstance } from "../NextGameInstanceBase";

class MyGameInstance extends NextGameInstanceBase {
    OnInit(): void {}
    OnTick(deltaSeconds: number): void {}
    OnSceneLoaded(): void {}
}

RunGameInstance(new MyGameInstance());
```

`RunGameInstance()` 会把 TypeScript 类方法注册到 QuickJS 生命周期钩子，并用模块函数 `RegisterTickCallback()` 接入每帧 tick。当前常用生命周期包括：

- `OnInit`
- `BeforeSceneRebuild`
- `OnSceneLoaded`
- `OnTick`
- `OnRenderUI`
- `OnInputEvent`
- `OverrideRenderCamera`
- `OnDestroy`

这条路径让脚本游戏和原生 `NextGameInstanceBase` 保持类似职责边界，便于做 Flappy 这类 C++/TypeScript 行为一致性验证；Flappy 双实现的项目定位见 [docs/projects/flappy-bird-parity/introduction.md](../projects/flappy-bird-parity/introduction.md)。

## 绑定和类型定义

TypeScript API 的来源分两类：

- 反射生成：`Node`、`Scene`、`RenderComponent`、`PhysicsComponent`、`SkinnedMeshComponent` 等反射类型
- 手写绑定：`Global`、`Input`、`Audio`、`UI`、`SceneBuild`、`LoadJson()`、`RequestLoadScene()` 等运行时函数

`BuildTypeScriptDefinitions()` 会把这些 API 写入 `assets/typescript/Engine.d.ts`。这份 `.d.ts` 是脚本编译期类型检查的入口，也是 C++ 绑定变化是否暴露给 TypeScript 的主要检查点。

新增绑定时，需要同步做三件事：

1. 在 `src/Modules/NextQuickJS/` 中注册 QuickJS 函数或反射类型
2. 在 `BuildTypeScriptDefinitions()` 中补齐 TypeScript 声明
3. 在 `assets/typescript/test.ts` 或对应示例脚本里加最小调用验证

如果是对象形参数、JSON、可选参数或临时返回对象，优先使用原生 QuickJS C API。简单 C++ 类和稳定签名可以继续使用 `quickjspp`。

## 热重载行为

桌面端安装 `NextQuickJS` 且启用 `compileTypeScript`、`enableHotReload` 后，`QuickJSEngine::TickHotReload()` 以 0.5 秒间隔调用 `CompileTypeScriptSources()`。如果编译输出更新成功，运行时会打印 TypeScript 输出更新日志，并重置 QuickJS context 重新加载入口脚本。

移动端当前不走这条热重载路径。Android/iOS 更偏向打包后的稳定运行布局，脚本变更需要通过正常资源构建或打包流程进入应用。

## 验证方式

只改 TypeScript 时，可以先直接运行 bundled compiler：

```powershell
tools\tsc\tsc.exe -p assets\typescript\tsconfig.json
```

跨平台写法：

```shell
tools/tsc/tsc -p assets/typescript/tsconfig.json
```

涉及 C++ 绑定、资源复制或运行时加载路径时，使用项目默认构建验证：

```powershell
./gnb.bat build --reconfigure
./gnb.bat run gkNextRenderer
```

启动成功后，日志应能到达：

```text
uploaded scene [...] to gpu
```

如果改动影响 Flappy 脚本绑定或输入同步，还应运行 C++/TypeScript replay 对比：

```powershell
.\out\build\windows\bin\FlappyCpp.exe --flappy-replay
.\out\build\windows\bin\FlappyJs.exe --flappy-replay
python tools\flappy\diff_traces.py
```

## 维护建议

- 不要引入 Node/npm/global `tsc` 作为运行时前提；项目约束是 bundled `tsc`
- 新增脚本入口时，同步更新 `assets/typescript/tsconfig.json` 的 `files`
- 不要手改 `assets/scripts` 当成源码；它是编译输出
- 修改反射组件后，确认 `Engine.d.ts` 中的类型随运行时生成逻辑同步更新
- 脚本里修改 `Node` 的向量属性时，赋回整个对象，例如 `node.Translation = { x, y, z }`
- 不要修改 `node.Translation.x` 这类临时对象字段，它不会写回 C++ 节点

## 相关文档

- QuickJS 绑定细节：`AGENT_GUIDE/QuickJSBindings.md`
- 热重载总览：`AGENT_GUIDE/HotReload.md`
- TypeScript 代码规范检查项：`AGENT_GUIDE/coding-standards.md`
