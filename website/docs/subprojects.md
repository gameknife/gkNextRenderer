# 15+ 子项目与工程分类清单 (Subprojects)

gkNextEngine 下拥有 15+ 独立的子项目工程，分别承担渲染实验、可视化工具、玩法原型与基准测试等角色。

---

## 🎨 渲染与可视化工具 (Render & Editor)

- **`gkNextRenderer`**：主渲染器，支持实时路径追踪 / Hybrid Rendering / 降噪与多管线对比。
- **`gkNextEditor`**：ImGui 综合编辑器，面向场景编辑、材质节点工作流（Material Node Editor）与运行时 cvar 调优。
- **`ScadStudio`**：OpenSCAD 程序化 DSL 建模求值、场景生成与 ScadRig 角色绑定实验场。
- **`RmlUiDemo`**：RmlUi 运行时 HTML/CSS UI 引擎集成与交互验证 Demo。

---

## 🕹️ 玩法与生态模拟 (Game & Simulation)

- **`AirportSim`**：机场生态模拟，验证 SCAD POI、角色队列、A* 寻路、LLM 智能决策与 ScadRig 角色。
- **`StudioSim`**：工作室经营模拟，验证本地 LLM 随机事件、员工日常行为目标、SCAD 办公室与职业配色。
- **`MagicaLego`**：体素 / 乐高风格玩法原型与场景物理搭建。
- **`BrickPlayer`**：基于 LDraw 官方规范的数字乐高积木交互与拼装原型。
- **`Brotato3D`**：俯视角 3D 生存射击原型，验证技能波次、怪物 AI、对象池与 Jolt 物理。
- **`NextDayz` / `CharacterDemo`**：角色控制、NavGrid A* 寻路、AI 行为树与生存战斗原型。
- **`NextTotalWar` / `NextRA`**：大规模军团方阵战术模拟与 Lockstep 确定性 RTS 验证。

---

## ⚡ 基准测试与自动化工具 (Benchmark & Tools)

- **`gkNextStillBenchmark`**：静态场景帧率与画质基准测试。
- **`gkNextMotionBenchmark`**：动态镜头 / 多场景渲染性能基准，自动输出 CSV 报告。
- **`gkNextVisualTest`**：自动化视觉回归测试，渲染场景并生成对比截图报告。
- **`Packager`**：资产打包工具，将场景与纹理打包为 `.pkg` 归档文件。
