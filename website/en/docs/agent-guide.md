# AI Agent & Automated Verification

gkNextEngine is designed from the ground up for **AI-Native Workflows** and **Autonomous Agent Verification**. Structured machine-readable assets and deterministic input driving form an efficient loop: "Generate → Run → Verify → Iterate".

---

## 🤖 Why AI-Native?

1. **Structured 3D Assets (SCAD / LDraw / Splats)**: AI models can directly write code-based OpenSCAD DSL scripts or LEGO brick definitions instead of wrestling with opaque binary 3D formats.
2. **entt::meta Full Reflection System**: All component properties are exposed via reflection metadata to C# scripting and autonomous inspection.
3. **Deterministic Input Driving & Assertions**: JSON scripts drive mouse/keyboard events and assert framerates, nodes, and rendering states.
4. **Fast Hidden-Window Screenshots**: Capture stable frames in seconds without popping windows or stealing OS focus via `gnb shot`.

---

## 📸 Agent Visual Validation

```bash
# Capture image of a scene to a fixed path and exit immediately
gnb shot --scene assets/models/playground.glb

# Validate procedural SCAD model
gnb shot --target ScadStudio --scene assets/scad/source/beer_cup.scad --frames 60

# Capture with ImGui UI overlay
gnb shot --target AirportSim --ui
```

Output screenshot is saved to `out/build/<preset>/screenshots/agent_validation.jpg`.
