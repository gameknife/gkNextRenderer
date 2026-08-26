# Architecture & Pipelines

gkNextEngine adheres to a lean, modular design. The first-party Engine Core codebase is strictly constrained below **50k LOC**, prioritizing clear data flows and maximum runtime throughput.

---

## 🏛️ System Architecture

```
src/
├── Engine/                  # Core engine library (gkNextEngine.lib, zero module deps)
│   ├── Common/              # CoreMinimal.hpp & platform abstractions
│   ├── Runtime/             # ECS (entt), reflection (entt::meta), cvars
│   ├── Assets/              # Scene graph, GPU resource pools, CPU acceleration
│   ├── Vulkan/              # Vulkan 1.4 backend & hardware Ray Query
│   └── Rendering/           # Multi-pipeline renderers
│       ├── PathTracing/     # 1/2spp path tracing + SHARC radiance cache
│       ├── SoftwareModern/  # Visibility buffer deferred rasterizer + CSM
│       ├── SoftwareTracing/ # Software DDA voxel tracing
│       └── Upscaler/        # DLSS / FSR / SGSR2 abstractions
├── Modules/                 # 18 linkable modules (NextDotNet, NextPhysics, NextAI, etc.)
├── Gameplay/                # Shared gameplay mechanics (Actor, NavGrid A*, AI)
└── Application/             # 15+ Subproject applications
```

---

## ⚡ Modern GPU Architecture Highlights

### 1. Fully Bindless Resource Indexing
All textures, buffers, and samplers are bound into a global descriptor array at startup. Shaders access resources via integer indices, eliminating CPU-side descriptor binding overhead.

### 2. Visibility Buffer & GPU-Driven Single Draw
Compute shaders execute frustum and occlusion culling directly on the GPU. Visible geometry clusters are submitted via `vkCmdDrawIndexedIndirect` in a single command, reducing CPU scheduling overhead to near zero.

### 3. Multi-Pipeline Hot Switching
Seamlessly switch between multiple rendering pipelines on the same committed scene:
- **PathTracing**: Hardware Ray Query with SHARC radiance caching and ReSTIR DI.
- **SoftwareModern**: Modern deferred rasterization with 4-cascade GPU CSM.
- **SoftwareModernNoAmbient**: High-speed rasterization for extreme framerate benchmarking.
