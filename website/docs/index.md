---
title: 文档手册总览
description: gkNextEngine 官方技术文档、架构设计与 AI Agent 开发指引
---

# gkNextEngine 文档手册

欢迎阅读 **gkNextEngine** 官方技术文档。gkNextEngine 是一个基于现代 C++20 与 Vulkan 的跨平台 3D 游戏引擎 / 渲染实验场，以极简轻量、实时路径追踪、现代 GPU 架构与 AI Native 自动化闭环为核心。

---

## 🗺️ 文档导航地图

<div class="docs-grid">

<a href="/docs/getting-started" class="doc-card gk-card">
  <div class="doc-card-title">🚀 快速起步指南</div>
  <div class="doc-card-desc">环境准备、统一 gnb 命令行工具使用、跨平台构建、运行与 WebRTC Remote Play 体验。</div>
  <div class="doc-card-link">阅读指南 →</div>
</a>

<a href="/docs/architecture" class="doc-card gk-card">
  <div class="doc-card-title">⚡ 架构与渲染管线</div>
  <div class="doc-card-desc">核心架构设计、全 Bindless 资源管理、GPU-Driven 单 Draw 提交、多管线热切换机制。</div>
  <div class="doc-card-link">阅读设计 →</div>
</a>

<a href="/docs/agent-guide" class="doc-card gk-card">
  <div class="doc-card-title">🤖 AI Agent 与自动化验证</div>
  <div class="doc-card-desc">结构化 3D 资产（SCAD/LDraw）、确定性输入驱动脚本、秒级无窗口截图验证与 LLM 闭环。</div>
  <div class="doc-card-link">阅读指引 →</div>
</a>

<a href="/docs/csharp-development" class="doc-card gk-card">
  <div class="doc-card-title">🎮 C# 托管脚本开发</div>
  <div class="doc-card-desc">CoreCLR 热重载与 NativeAOT 双后端架构、ECS 组件反射访问与 Play-in-Editor (PIE)。</div>
  <div class="doc-card-link">阅读教程 →</div>
</a>

<a href="/docs/scad-pipeline" class="doc-card gk-card">
  <div class="doc-card-title">📐 OpenSCAD 程序化管线</div>
  <div class="doc-card-desc">OpenSCAD DSL 原生解析器、Manifold CSG 几何布尔运算与 ScadRig 刚体骨骼绑定。</div>
  <div class="doc-card-link">阅读管线 →</div>
</a>

<a href="/docs/subprojects" class="doc-card gk-card">
  <div class="doc-card-title">🧩 15+ 子项目工程清单</div>
  <div class="doc-card-desc">AirportSim、MagicaLego、Brotato3D、ScadStudio、gkNextEditor 等全量子项目详解。</div>
  <div class="doc-card-link">查看清单 →</div>
</a>

</div>

---

## 💡 常用命令速查

```bash
# 1. 检查环境与工具链
./gnb.bat doctor

# 2. 准备依赖与 SDK (自动下载 Vulkan SDK + Slang + vcpkg)
./gnb.bat setup

# 3. 极速增量构建核心目标
./gnb.bat build

# 4. 运行主渲染器或编辑器
./gnb.bat run gkNextRenderer
./gnb.bat run gkNextEditor
```

<style>
.docs-grid {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
  margin: 24px 0 36px;
}

.doc-card {
  padding: 20px;
  display: flex;
  flex-direction: column;
  text-decoration: none !important;
  color: inherit !important;
}

.doc-card-title {
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--vp-c-text-1);
  margin-bottom: 8px;
}

.doc-card-desc {
  font-size: 0.88rem;
  color: var(--vp-c-text-2);
  line-height: 1.5;
  margin-bottom: 14px;
  flex-grow: 1;
}

.doc-card-link {
  font-size: 0.82rem;
  font-weight: 600;
  color: var(--gk-accent-cyan);
}

@media (max-width: 640px) {
  .docs-grid {
    grid-template-columns: 1fr;
  }
}
</style>
