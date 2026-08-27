---
title: Documentation Overview
description: Technical documentation, architecture design, and AI Agent guidelines for gkNextEngine
---

# gkNextEngine Documentation

Welcome to the **gkNextEngine** official documentation. gkNextEngine is a cross-platform 3D game engine and rendering playground built with modern C++20 and Vulkan, focusing on lean architecture, real-time path tracing, modern GPU pipelines, and AI Native automated verification.

---

## 🗺️ Documentation Roadmap

<div class="docs-grid">

<a href="/en/docs/getting-started" class="doc-card gk-card">
  <div class="doc-card-title">🚀 Getting Started</div>
  <div class="doc-card-desc">Prerequisites, unified gnb CLI toolchain, cross-platform build, run commands, and WebRTC Remote Play.</div>
  <div class="doc-card-link">Read Guide →</div>
</a>

<a href="/en/docs/architecture" class="doc-card gk-card">
  <div class="doc-card-title">⚡ Architecture &amp; Pipelines</div>
  <div class="doc-card-desc">Core modular architecture, fully bindless resource management, GPU-driven single draw culling, and multi-pipeline switching.</div>
  <div class="doc-card-link">Read Design →</div>
</a>

<a href="/en/docs/agent-guide" class="doc-card gk-card">
  <div class="doc-card-title">🤖 AI Agent &amp; Verification</div>
  <div class="doc-card-desc">Structured 3D assets (SCAD/LDraw), declarative test scripts, hidden-window fast validation, and local LLM loops.</div>
  <div class="doc-card-link">Read Guide →</div>
</a>

<a href="/en/docs/csharp-development" class="doc-card gk-card">
  <div class="doc-card-title">🎮 C# Managed Scripting</div>
  <div class="doc-card-desc">Dual backend architecture (CoreCLR hot reload + NativeAOT), ECS component access via reflection, and Play-in-Editor (PIE).</div>
  <div class="doc-card-link">Read Tutorial →</div>
</a>

<a href="/en/docs/scad-pipeline" class="doc-card gk-card">
  <div class="doc-card-title">📐 OpenSCAD Pipeline</div>
  <div class="doc-card-desc">Embedded OpenSCAD DSL parser, Manifold CSG geometry evaluation, and ScadRig procedural character rigging.</div>
  <div class="doc-card-link">Read Pipeline →</div>
</a>

<a href="/en/docs/subprojects" class="doc-card gk-card">
  <div class="doc-card-title">🧩 15+ Subprojects</div>
  <div class="doc-card-desc">Detailed overview of AirportSim, MagicaLego, Brotato3D, ScadStudio, gkNextEditor, and more.</div>
  <div class="doc-card-link">View List →</div>
</a>

</div>

---

## 💡 Quick Commands

```bash
# 1. Check environment and host tools
./gnb.sh doctor

# 2. Setup dependencies and SDKs (auto downloads Vulkan SDK + Slang + vcpkg)
./gnb.sh setup

# 3. Build core target (gkNextRenderer + gkNextUnitTests)
./gnb.sh build

# 4. Run main renderer or editor
./gnb.sh run gkNextRenderer
./gnb.sh run gkNextEditor
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
