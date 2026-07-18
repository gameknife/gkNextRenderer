---
title: "Flappy C++ / TypeScript Parity"
category: project
status: 现行
owner: Flappy/NextQuickJS
created: 2026-05-18
last_updated: 2026-07-17
---

# Flappy C++ / TypeScript Parity

`FlappyCpp` 与 `FlappyJs` 用同一份数值和输入 replay 实现同一个极小游戏。它们的用途是回归 NextQuickJS 的生命周期、ES module、场景构建、输入、相机、UI、文件/JSON 与浮点确定性，不是维护一份人工“当前 PASS”报告。

## 代码与数据

- 共享 C++ 配置/类型：`src/Application/Game/Flappy/`
- C++ 实现：`src/Application/Game/Flappy/FlappyCpp/`
- JS host：`src/Application/Game/Flappy/FlappyJs/`
- TypeScript 实现：`assets/typescript/flappy/`
- 运行时编译输出：`out/build/<preset>/assets/scripts/flappy/`
- 玩法/replay 配置：`assets/configs/flappy/gameplay.json`、`replay.json`
- 比较器：`tools/flappy/diff_traces.py`

两端以 `gameplay.json` 的 fixed step 和 RNG seed 运行，以 `replay.json` 的 flap frame 序列输入。输出分别是 `out/flappy_cpp_trace.json` 和 `out/flappy_js_trace.json`。

## 验证

```bash
./gnb.sh build FlappyCpp FlappyJs
./gnb.sh run FlappyCpp --flappy-replay
./gnb.sh run FlappyJs --flappy-replay
python3 tools/flappy/diff_traces.py
```

比较器核对 fixed delta、seed、death frame、frame count，以及每帧的 score/state/birdY/birdVelocityY；有任一差异返回非零。需要保存某次 CI/调查证据时可以显式传 `--report <path>`，但仓库不保留会迅速过时的常驻 parity report。

普通交互运行：

```bash
./gnb.sh run FlappyCpp
./gnb.sh run FlappyJs
```

## 修改规则

- C++ 是行为 baseline；若规则有意改变，两端和共享配置应在同一改动中更新。
- TypeScript 端用 `Math.fround`/等价 `f32` 保持 C++ float 舍入点，不随意改表达式结合顺序。
- RNG 算法、固定步长、碰撞边界和状态切换顺序必须逐端一致。
- `FlappyJs` host 保持薄，只负责安装 NextQuickJS 和指定入口；玩法不要回流到 host C++。
- TypeScript 源在 `assets/typescript`；运行时编译输出在 build assets，不要把 source-tree `assets/scripts` 当成 TS 源码目录。

推荐继续阅读 [TypeScript 整合](../../guides/typescript-integration.md) 和 `AGENT_GUIDE/QuickJSBindings.md`。当前结果必须由上述命令重新生成，不能从历史 journal 或旧报告推断。
