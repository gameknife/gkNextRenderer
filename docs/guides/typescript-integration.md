---
title: "TypeScript / NextQuickJS 整合"
category: guide
status: 现行
owner: NextQuickJS
created: 2026-05-14
last_updated: 2026-07-17
---

# TypeScript / NextQuickJS 整合

TypeScript 是可选的游戏/编辑器脚本层，不是全局 Web 工具链。应用必须链接 `NextQuickJS` 并显式调用 `Modules::NextQuickJS::Install()`；未安装的 target 不创建 QuickJS runtime，也不触发 TS 编译。

## 目录与生成物

- `assets/typescript/`：人工维护的 `.ts`、`tsconfig.json` 和生成的 API 声明 `Engine.d.ts`。
- `assets/scripts/`：仓库中人工维护的普通 JS/MLS scripts，会被 CMake 复制到 runtime assets；不要把整个目录都视为 TS 生成物。
- `out/build/<preset>/assets/scripts/`：桌面运行时的 TS 编译输出和 `.tsc.stamp`。Android/iOS 使用各自打包布局。
- `tools/tsc/tsc[.exe]`：`gnb setup` 准备的 bundled compiler；CMake 同时复制到 `out/build/<preset>/tools/tsc/`。

`QuickJSEngine::CompileTypeScriptSources()` 会读取 source-tree `assets/typescript/tsconfig.json`，但通过 `--outDir` 把结果写到运行时 assets，不在正常运行时改写 source-tree `assets/scripts`。`UpdateTypeScriptDefinitions()` 会按当前 C++ bindings 更新 `assets/typescript/Engine.d.ts`，这份声明应随绑定改动提交。

## 生命周期

安装配置的关键字段：

- `entryScript`：运行时 asset path，例如 FlappyJs 的 `assets/scripts/flappy/FlappyJs/FlappyJsGameInstance.js`。
- `compileTypeScript`：初始化前检查/编译 TS。
- `enableHotReload`：桌面端约每 0.5 秒检查源文件；成功后重建 QuickJS context，失败保留已有 JS。

脚本游戏继承 `assets/typescript/NextGameInstanceBase.ts` 并调用 `RunGameInstance()`。常用 hook：`OnInit`、`BeforeSceneRebuild`、`OnSceneLoaded`、`OnTick`、`OnRenderUI`、`OnInputEvent`、`OverrideRenderCamera`、`OnDestroy`。

移动端当前不走桌面 TS hot reload；发布资产必须在打包流程中已有可运行 JS。

## ES module

内建模块名为 `Engine`。Loader 也把 `./Engine`、`../Engine` 和规范化后的 `assets/scripts/Engine` 映射回内建模块；普通相对 import 从 runtime `assets/scripts` 树解析，缺少 `.js` 时自动补扩展。

```ts
import * as NE from "../Engine";
import { NextGameInstanceBase, RunGameInstance } from "../../NextGameInstanceBase";
```

实际相对层级取决于编译后 module 位置，不要复制另一个目录的 import 而不核对输出树。

## Binding 与类型声明

API 有两类：

- entt reflection 自动暴露的 Scene/Node/component 属性；
- `QuickJSBindings*.cpp` 手写的 Global、Input、Audio、UI、SceneBuild、文件/JSON、camera 等函数。

新增 binding 时：

1. 在 `src/Modules/NextQuickJS/` 注册实现。
2. 在 `BuildTypeScriptDefinitions()` 增加匹配声明。
3. 在 `assets/typescript/test.ts` 或专用 host 脚本加最小调用。
4. 构建实际使用它的 target，并检查生成后的 `Engine.d.ts` diff。

对象形参数、可选参数、JSON 和临时对象优先用原生 QuickJS C API。简单稳定类可用 quickjspp。`SceneBuild.*` 只在 `BeforeSceneRebuild` 期间有效；运行时节点修改用 Scene/Node API。

向量属性读取会得到临时 JS 对象，必须整体赋回：

```ts
node.Translation = { x, y, z };
node.RecalcTransform(true);
```

`node.Translation.x = x` 不会写回 C++。

## 验证

只做类型检查且不写生成 JS：

```bash
tools/tsc/tsc --noEmit -p assets/typescript/tsconfig.json
```

验证 runtime 编译、加载和 Flappy bindings：

```bash
./gnb.sh build FlappyJs
NEXTENGINE_FORCE_TSC=1 ./gnb.sh run FlappyJs --flappy-replay
```

绑定变化需要 C++ baseline 对比时：

```bash
./gnb.sh build FlappyCpp FlappyJs
./gnb.sh run FlappyCpp --flappy-replay
./gnb.sh run FlappyJs --flappy-replay
python3 tools/flappy/diff_traces.py
```

不要引入 Node/npm/global `tsc` 前提，也不要为普通 binding 改动做全量 `--reconfigure`。更多 API 细节见 `AGENT_GUIDE/QuickJSBindings.md`，hot reload 边界见 `AGENT_GUIDE/HotReload.md`。
