# 引擎架构与管线设计 (Architecture)

gkNextEngine 采用克制、高内聚的模块化架构，第一方 Engine Core 代码规模严格控制在 **50k LOC** 以内，追求清晰的数据流与极高的运行时吞吐。

---

## 🏛️ 系统架构全览

```
src/
├── Engine/                  # 核心引擎层 (gkNextEngine.lib，无外部业务模块依赖)
│   ├── Common/              # CoreMinimal.hpp 与跨平台基础抽象
│   ├── Runtime/             # ECS (entt)、反射系统 (entt::meta)、Cvar、配置
│   ├── Assets/              # 场景树、GPU 资源池、CPU 加速结构
│   ├── Vulkan/              # Vulkan 1.4 后端驱动与 HW RT 射线查询
│   └── Rendering/           # 多管线渲染器实现
│       ├── PathTracing/     # 1/2spp 路径追踪 + SHARC 辐射缓存
│       ├── SoftwareModern/  # Visibility Buffer 延迟光栅 + CSM
│       ├── SoftwareTracing/ # CPU/GPU 软件体素 DDA
│       └── Upscaler/        # DLSS / FSR / SGSR2 上采样统一抽象
├── Modules/                 # 18 个按需链接的引擎模块
│   ├── NextDotNet/          # C# CoreCLR / NativeAOT 双后端运行时
│   ├── NextPhysics/         # Jolt Physics 物理引擎封装
│   ├── NextAI/              # 本地大模型 (llama.cpp) 推理服务
│   ├── ScadLoader/          # OpenSCAD DSL 解析与 Manifold CSG 几何生成
│   ├── LDrawLoader/         # 数字乐高标准与 LGEO PBR 材质映射
│   ├── SplatLoader/         # PlayCanvas 高斯溅射 (Gaussian Splatting)
│   └── NextRemote/          # WebRTC 浏览器零安装推流服务
├── Gameplay/                # 共享玩法机制 (角色 Actor, NavGrid A*, AI 行为树)
└── Application/             # 15+ 子项目入口 (Render, Editor, Game, Util)
```

---

## ⚡ 现代 GPU 架构特色

### 1. 全 Bindless 资源管理
所有 Texture、Buffer、Sampler 均在启动时绑定到全局 Bindless Descriptor Set。着色器通过无符号整型索引访问资源，彻底消除 CPU 端的 Descriptor 绑定与切换瓶颈。

### 2. Visibility Buffer 与 GPU-Driven 单 Draw 提交
- 场景中所有几何体与网格簇（Meshlet）由 Compute Shader 在 GPU 端进行视锥剔除（Frustum Culling）和遮挡剔除（Occlusion Culling）。
- 剔除后的可见列表直接通过 `vkCmdDrawIndexedIndirect` 单次提交数万实例，CPU Draw Call 调度开销接近 0。

### 3. 多渲染管线热切换
引擎支持共享同一场景状态在以下渲染器间无缝热切换：
- **PathTracing**：硬件光线查询（Ray Query），结合 SHARC 世界辐射缓存与 ReSTIR DI。
- **SoftwareModern**：现代 G-Buffer 延迟光栅化 + GPU CSM 太阳阴影（4 cascades）。
- **SoftwareModernNoAmbient**：极限性能光栅化模式，专为超高帧率基准测试设计。
