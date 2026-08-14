---
title: "Flappy C++ / C# Parity"
category: project
status: 现行
owner: Flappy/engine-scripting
created: 2026-05-18
last_updated: 2026-08-15
---

# Flappy C++ / C# Parity

`FlappyCpp` 与 `FlappyCSharp` 用同一份数值和输入 replay 实现同一个极小游戏。C++ 是行为基线；
C# 侧存在的意义是回归**脚本绑定层**——生命周期时序、调用次数与顺序、场景构建、输入、相机、UI
与配置读取。绑定漏调一次、多调一次或调错帧，都会体现在得分或死亡帧上，而这两项是严格比较的。

它不是一份人工维护的"当前 PASS"报告，结果必须由下面的命令重新生成。

## 代码与数据

- 共享 C++ 配置/类型：`src/Application/Game/Flappy/`
- C++ 实现：`src/Application/Game/Flappy/FlappyCpp/`
- C# host（约 90 行，无玩法）：`src/Application/Game/Flappy/FlappyCSharp/`
- C# 实现：`assets/csharp/Flappy/FlappyCSharp/`
- 玩法/replay 配置（两端共享同一份）：`assets/configs/flappy/gameplay.json`、`replay.json`
- 比较器：`tools/flappy/diff_traces.py`

两端以 `gameplay.json` 的 fixed step 和 RNG seed 运行，以 `replay.json` 的 flap frame 序列输入，
分别产出 `out/flappy_cpp_trace.json` 与 `out/flappy_cs_trace.json`。

## 验证

```bash
./gnb.sh run FlappyCpp --flappy-replay
./gnb.sh run FlappyCSharp --flappy-replay
python3 tools/flappy/diff_traces.py
```

C# 侧要在**两种后端下各跑一次**（`gnb dotnet ci` 会把两种后端都构建出来）。两份 C# trace 必须
一致——这是双后端语义等价的硬证据，也是这套 parity 相对单后端方案的额外价值。

门槛（设计第 9 节）：

| 字段 | 判据 |
|---|---|
| `score` / `state` / `frame` / `deathFrame` / 帧数 | 严格相等 |
| `birdY` / `birdVelocityY` | 容差 1e-3 |

`fixedDeltaSeconds` 按 float32 比较：两端都存 float32，只是 nlohmann 提升到 double 打印、
`Utf8JsonWriter` 打印 float 的最短往返，文本不同而值相同。

**实测（2026-08-15）**：两种后端下 0 violations，`deathFrame` 均为 126，720/720 帧的 C# trace
在两种后端间**逐字段完全相同**，与 C++ 的最大偏差 2.4e-7——比门槛小四个数量级。

## 修改规则

- C++ 是行为 baseline；若规则有意改变，两端和共享配置应在同一改动中更新。
- RNG 算法、固定步长、碰撞边界和状态切换顺序必须逐端一致。**RNG 抽取的时机也算顺序**：
  C# 侧刻意在找到空闲 pipe 槽位之后才抽随机数，与 C++ 一致；提前抽会在对象池占满的那一刻
  让整个 replay 错位。
- C# 侧刻意写得"不够地道"——它是 C++ 的逐句翻译。两边能不同的地方就按 C++ 写，否则这套回归
  的价值就没了。
- 视差层未移植：它只移动背景节点、不参与模拟，因此不影响 parity。
