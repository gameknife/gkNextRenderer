# AI Agent 与自动化验证 (Agent Guide)

gkNextEngine 是首批专为 **AI Native 工作流** 与 **自动化 Agent 验证** 设计的跨平台 3D 引擎之一。通过机器可读的结构化内容、无窗口确定性驱动与自动化断言，形成“生成 → 运行 → 验证 → 迭代”的闭环。

---

## 🤖 为什么是 AI Native 架构？

传统游戏引擎充斥着非文本化、专有格式的二进制资产与难以自动验证的复杂交互。gkNextEngine 通过四大支柱提供 AI 友好能力：

1. **结构化 3D 资产（SCAD / LDraw / Splat）**：AI 可以直接编写 OpenSCAD DSL 代码或 LDraw 积木脚本生成 3D 几何，无需复杂手工建模软件。
2. **entt::meta 全反射组件系统**：所有属性暴露于反射元数据与 C# 脚本，AI 可直接理解并精准修改场景属性。
3. **确定性输入驱动与断言**：通过 JSON 脚本驱动事件，自动判断帧率、节点数与渲染状态。
4. **极速无窗口截图验证**：提供秒级无弹窗截图能力（`gnb shot`），方便 AI 视觉模型快速肉眼质检。

---

## 📸 快速视觉验证 (Agent Visual Validation)

当需要确认渲染、着色或场景修改是否正确时，首选 `gnb shot`：

```bash
# 渲染指定场景到稳定帧 → 截取图像到固定路径 → 自动退出 (几秒完成，不抢焦点)
gnb shot --scene assets/models/playground.glb

# 验证 SCAD 程序化模型
gnb shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60

# 包含 ImGui UI 截图
gnb shot --target AirportSim --ui
```

截图将覆盖输出至 `out/build/<preset>/screenshots/agent_validation.jpg`。

---

## 🎮 声明式自动化脚本验证 (Agent Interactive Validation)

通过 JSON 声明式脚本驱动游戏行为并进行断言判定：

```bash
gnb validate --script assets/agentscripts/smoke.agentscript.json
```

支持步骤：`key` / `text` / `mouse-move` / `click` / `drag` / `scroll` / `wait-frames` / `cvar` / `assert` / `screenshot` / `quit`。
断言失败时返回非零退出码，无缝接入 CI 自动化流水线。
