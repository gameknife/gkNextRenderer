---
title: Flappy Bird Parity 项目介绍
status: 现行
owner: docs
last_updated: 2026-05-18
---

# Flappy Bird Parity 项目介绍

Flappy Bird Parity 是 gkNextEngine 里的双实现玩法样例：同一套 Flappy Bird 规则同时由 C++ 原生目标 `FlappyCpp` 和 QuickJS / TypeScript 目标 `FlappyJs` 实现，并通过确定性 replay trace 做逐帧对比。

它的重点不是复刻一个复杂游戏，而是用一个规则极小、反馈直接、容易验证的玩法，把脚本绑定链路压到可回归的程度：输入、生命周期、场景构建、相机覆盖、UI 绘制、音频、文件读写、JSON 配置和 ESM 模块加载都要能被 TypeScript 端完整使用，并且行为与 C++ baseline 一致。

## 当前形态

- C++ 目标：`FlappyCpp`
- TypeScript 目标：`FlappyJs`
- 共享 C++ 代码：`src/Application/Game/Flappy/`
- TS 源码：`assets/typescript/flappy/`
- TS 编译输出：`assets/scripts/flappy/`
- 配置目录：`assets/configs/flappy/`
- trace 对比工具：`tools/flappy/diff_traces.py`
- 当前对齐报告：`docs/projects/flappy-bird-parity/parity-report.md`

两个目标都使用同一份 `assets/configs/flappy/gameplay.json` 作为数值来源，包含世界边界、鸟的重力与 flap 速度、管道生成参数、固定步长和 RNG seed。Replay 模式使用 `assets/configs/flappy/replay.json` 中的 flap 帧序列，分别输出 `out/flappy_cpp_trace.json` 和 `out/flappy_js_trace.json`，最后由 `diff_traces.py` 检查逐帧状态。

## 为什么做双实现

`FlappyCpp` 是行为基准。它是标准 `NextGameInstanceBase` 子类，直接在 C++ 中实现状态机、固定步长、鸟、管道、碰撞、UI、音效和 trace 输出。

`FlappyJs` 是脚本绑定验收目标。宿主侧只有一个很薄的 C++ 壳，负责设置 QuickJS 入口并把生命周期转发给脚本；玩法逻辑、场景构建、输入处理、相机覆盖、UI 和 replay 写文件都在 TypeScript 中完成。

这能回答一个很具体的问题：如果同一份规则和同一份输入序列在 C++ 与 TypeScript 中得到完全相同的 trace，那么 QuickJS 绑定至少覆盖了一个完整小游戏所需的关键能力，并且具备可重复验证的回归信号。

## 玩法规则

当前玩法保持极简：

- 状态机：`Ready → Playing → Dead`
- 输入：Space、鼠标左键、手柄南键触发 flap；死亡后等待短暂 hit-stop 再输入重开。
- 鸟：固定 X 坐标，Y 轴受重力和 flap velocity 影响，速度有上下限。
- 管道：按固定间隔从右侧生成，使用 xorshift32 生成 gap center，向左匀速移动。
- 碰撞：鸟碰到世界上下边界或管道矩形即死亡。
- 计分：鸟越过管道后加分。
- 渲染：程序化球体、box 管道、floor / ceiling / background；透视相机模拟侧视。
- UI：中央 Ready / Game Over 面板，顶部 score。

这套规则刻意避免依赖复杂资产和物理系统，目的是让差异主要来自脚本绑定、数值读取、浮点顺序和固定步长实现，而不是内容本身。

## 代码组织

| 区域 | 文件 | 职责 |
| --- | --- | --- |
| 共享类型 | `FlappyCommon.hpp` | C++ 侧状态、配置结构、默认数值 |
| 配置加载 | `FlappyConfig.cpp/.hpp` | 读取 `gameplay.json` 和 `replay.json` |
| C++ 入口 | `FlappyCpp/FlappyCppGameInstance.cpp/.hpp` | 原生玩法主循环、UI、输入、相机、replay |
| C++ 鸟 | `FlappyCpp/FlappyCppBird.cpp/.hpp` | 位置、速度、flap、视觉同步 |
| C++ 管道 | `FlappyCpp/FlappyCppPipes.cpp/.hpp` | 管道池、生成、移动、碰撞、计分 |
| C++ RNG | `FlappyCpp/FlappyCppRng.hpp` | xorshift32，与 TS 端逐位对齐 |
| JS 宿主 | `FlappyJs/FlappyJsGameInstance.cpp/.hpp` | 设置 `QuickJSEntry`，转发生命周期和场景构建 |
| TS 入口 | `assets/typescript/flappy/FlappyJs/FlappyJsGameInstance.ts` | TypeScript 版完整玩法 |
| TS 模块 | `FlappyJsBird.ts`, `FlappyJsPipes.ts`, `FlappyJsRng.ts` | 对齐 C++ 端鸟、管道和 RNG |
| 对比工具 | `tools/flappy/diff_traces.py` | 比较 C++ / JS trace 并生成报告 |

## 验证方式

常规运行：

```powershell
gnb.bat run FlappyCpp
gnb.bat run FlappyJs
```

确定性 replay 对比：

```powershell
.\out\build\windows\bin\FlappyCpp.exe --flappy-replay
.\out\build\windows\bin\FlappyJs.exe --flappy-replay
python tools\flappy\diff_traces.py --report docs\projects\flappy-bird-parity\parity-report.md
```

当前 `parity-report.md` 记录的状态为 PASS：C++ 和 JS 都跑 720 帧，死亡帧一致，`birdY`、`birdVelocityY`、score 和 state 均无差异。

## 它验证了哪些引擎能力

- QuickJS 生命周期：`OnInit`、`OnTick`、`OnRenderUI`、`BeforeSceneRebuild`、`OnSceneLoaded`。
- TypeScript ESM：同目录和上级目录模块 import 能从编译输出目录正确解析。
- 场景构建绑定：TS 能创建程序化模型、材质和 RenderNode。
- 输入桥接：键盘、鼠标、手柄按钮能进入脚本层统一处理。
- 相机覆盖：TS 能返回 camera override，宿主填充 `Assets::Camera`。
- UI 绘制：TS 能绘制文字、矩形、面板并查询屏幕尺寸。
- 音频：TS 能请求播放 SFX，缺失资源不阻断玩法。
- 文件与 JSON：TS 能读取配置、写 replay trace。
- 确定性：固定步长、RNG、浮点处理和状态输出可以与 C++ baseline 对齐。

## 推荐阅读顺序

1. 先读本文，理解为什么 Flappy 有两个目标。
2. 再读 `docs/typescript-integration.md`，理解 TypeScript 源码、编译输出和 QuickJS 入口。
3. 然后读 `AGENT_GUIDE/QuickJSBindings.md`，了解绑定规则和 Flappy replay 回归命令。
4. 若要追溯开发计划，读 `plan.md`；若要看当前验证结果，读 `parity-report.md`。

