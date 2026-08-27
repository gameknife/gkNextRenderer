export interface I18nContent {
  hero: {
    title1: string
    title2: string
    subtitle: string
    quickstart: string
    github: string
    docs: string
    scrollHint: string
    scenes: Array<{
      name: string
      title: string
      desc: string
      image: string
      fps: string
      pipeline: string
      vram: string
    }>
  }
  features: {
    badge: string
    title: string
    desc: string
    items: Array<{
      title: string
      summary: string
      accentColor: string
      glowColor: string
      points: string[]
    }>
  }
  showcase: {
    badge: string
    title: string
    desc: string
    categories: Array<{ id: string; name: string }>
    projects: Array<{
      id: string
      name: string
      category: string
      categoryName: string
      role: string
      desc: string
      image: string
      tags: string[]
      target: string
    }>
  }
  benchmark: {
    badge: string
    title: string
    desc: string
    metrics: Array<{
      val: string
      unit: string
      label: string
      sub: string
      colorClass: string
    }>
    tableTitle: string
    tableEnv: string
    tableHeaders: string[]
    reproduceLabel: string
  }
  quickstart: {
    badge: string
    title: string
    desc: string
    copied: string
    copyBtn: string
    platforms: Array<{ id: string; name: string; icon: string }>
    footerTip: string
  }
  ecosystem: {
    badge: string
    title: string
    desc: string
    licenseTitle: string
    licenseDesc: string
    starBtn: string
    groups: Array<{
      category: string
      items: Array<{ name: string; role: string }>
    }>
  }
}

export const zhCN: I18nContent = {
  hero: {
    title1: '轻量、现代、极速光追。',
    title2: '自由开源的跨平台 3D 游戏引擎',
    subtitle: '基于 C++20 与 Vulkan，为实时路径追踪、游戏原型与 AI Native 工作流打造。',
    quickstart: '快速上手 (gnb)',
    github: 'GitHub 仓库',
    docs: '文档手册 →',
    scrollHint: '向下滚动探索更多',
    scenes: [
      {
        name: 'Still 渲染',
        title: 'gkNextRenderer · still.glb',
        desc: '室外自然采光 · 物理 PBR 材质 · 1/2spp 硬件路径追踪',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/still.webp',
        fps: '427 FPS (2.34ms)',
        pipeline: 'PathTracing + SHARC',
        vram: 'VRAM: 978 MiB'
      },
      {
        name: 'Luxball 材质',
        title: 'gkNextRenderer · luxball.glb',
        desc: '金属 / 高透玻璃 / 塑料 · 多光源高动态反射',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/luxball.webp',
        fps: '353 FPS (2.83ms)',
        pipeline: 'PathTracing + ReSTIR',
        vram: 'VRAM: 978 MiB'
      },
      {
        name: 'Playground 几何',
        title: 'gkNextRenderer · playground.glb',
        desc: '复杂体素几何 · 全局漫反射 · 实时 CSM 太阳阴影',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/playground.webp',
        fps: '606 FPS (1.65ms)',
        pipeline: 'Hybrid Rendering',
        vram: '925 MiB'
      },
      {
        name: '会议室 室内光',
        title: 'gkNextRenderer · confroom.glb',
        desc: '高反弹室内光照 · 空间音频与物理碰撞调试视窗',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/confroom.webp',
        fps: '1,614 FPS (0.62ms)',
        pipeline: 'SoftwareModern',
        vram: '925 MiB'
      }
    ]
  },
  features: {
    badge: 'Core Architecture',
    title: '为什么选择 gkNextEngine？',
    desc: '不设商业妥协的个人 R&D 引擎实验场。以现代图形学技术与工程约束为导向，打造极致性能与 AI 友好的下一代基础设施。',
    items: [
      {
        title: '实时路径追踪与 Hybrid 渲染',
        summary: '面向真实运行时的 1/2spp 路径追踪，追求极致帧率与画质的平衡。',
        accentColor: '#00d2ff',
        glowColor: 'rgba(0, 210, 255, 0.1)',
        points: [
          'SHARC 世界辐射缓存复用与 ReSTIR DI 直接光重采样',
          'NVIDIA DLSS / DLSS-RR / FSR / SGSR2 多档上采样与重建',
          '多渲染器热切换：PathTracing 与延迟光栅共享同一资产',
          'PlayCanvas 高斯溅射（Gaussian Splat）与 Mesh 在同一帧共存'
        ]
      },
      {
        title: '现代 GPU 架构与克制代码量',
        summary: '以运行时性能为约束，第一方 Core 严格控制在 50k LOC 以内。',
        accentColor: '#ff7a00',
        glowColor: 'rgba(255, 122, 0, 0.1)',
        points: [
          '全 Bindless 资源索引，单帧数万 Draw Call 单 Draw 提交',
          'Visibility Buffer 与 Soft Mesh Shader 几何流水线',
          'entt ECS + 反射层：一次注册通吃编辑器、脚本与序列化',
          'Jolt Physics 物理引擎深度整合，支撑碰撞、刚体与角色移动'
        ]
      },
      {
        title: 'AI Native 内容与自动化闭环',
        summary: '让 AI 面对可解析、可修改、可验证的结构化 3D 资产与运行时。',
        accentColor: '#10b981',
        glowColor: 'rgba(16, 185, 129, 0.1)',
        points: [
          '原生解析 OpenSCAD DSL、LDraw（乐高）与 glTF 2.0',
          '声明式输入驱动脚本（.agentscript.json），支持自动判定断言',
          '无窗口（Hidden Window）快速截图与视觉回归测试（gnb shot）',
          '集成本地 llama.cpp / Gemma 推理服务，驱动游戏 NPC 决策'
        ]
      },
      {
        title: '统一工具链与 Remote Play',
        summary: '单一入口掌控全生命周期，打破平台与运行环境限制。',
        accentColor: '#a855f7',
        glowColor: 'rgba(168, 85, 247, 0.1)',
        points: [
          '统一 gnb CLI：一体化接管构建、运行、测试与资产打包',
          '多平台口径一致：Windows, Linux, macOS, Android, iOS',
          'WebRTC Remote Play：浏览器免安装 60FPS 低延迟游玩',
          'Tracy CPU/GPU 时间线与 Superluminal 深度性能剖析'
        ]
      }
    ]
  },
  showcase: {
    badge: 'Prototypes & Ecosystem',
    title: '15+ 子项目与玩法原型展厅',
    desc: 'gkNextEngine 不只是渲染管线，更是丰富玩法原型与内容管线的实验场。所有子项目共享统一的 ECS、物理与渲染基座。',
    categories: [
      { id: 'all', name: '全部原型' },
      { id: 'game', name: '玩法与模拟' },
      { id: 'pipeline', name: '内容管线 & DSL' },
      { id: 'tool', name: '编辑器与工具' }
    ],
    projects: [
      {
        id: 'airportsim',
        name: '✈️ AirportSim',
        category: 'game',
        categoryName: '生态模拟',
        role: '机场生态与 Agent 决策',
        desc: '验证 SCAD 描述的 POI 兴趣点、旅客队列、A* 寻路网格与本地 LLM 智能体自主行为决策。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/airportsim.webp',
        tags: ['A* 寻路', 'LLM Agent', 'SCAD POI', '队列模拟'],
        target: 'AirportSim'
      },
      {
        id: 'magicalego',
        name: '🧱 MagicaLego',
        category: 'game',
        categoryName: '体素搭建',
        role: '数字积木物理搭建',
        desc: '体素与乐高风格场景搭建与物理玩法实验场，支持射线抓取、积木连接点判定与物理受力模拟。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/magicalego.webp',
        tags: ['Jolt 物理', '体素搭建', 'LGEO 材质', '射线交互'],
        target: 'MagicaLego'
      },
      {
        id: 'brotato3d',
        name: '🥔 Brotato3D',
        category: 'game',
        categoryName: '生存射击',
        role: '3D 割草生存原型',
        desc: '俯视角 3D 生存射击原型，验证海量怪潮波次生成、高性能对象池、武器技能羁绊与 Jolt 物理碰撞。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brotato3d.webp',
        tags: ['怪潮对象池', '技能系统', 'Jolt 物理', 'C# 脚本'],
        target: 'Brotato3D'
      },
      {
        id: 'brickplayer',
        name: '🧩 BrickPlayer',
        category: 'pipeline',
        categoryName: 'LDraw 乐高',
        role: 'LDraw 标准交互原型',
        desc: '基于 LDraw 官方规范的数字乐高积木资产解析器，官方色表映射 PBR 材质与零件拼搭语义。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brickplayer.webp',
        tags: ['LDraw 标准', 'PBR 色表', '零件连接', '程序化拼装'],
        target: 'BrickPlayer'
      },
      {
        id: 'scadstudio',
        name: '📐 ScadStudio',
        category: 'pipeline',
        categoryName: 'OpenSCAD',
        role: '程序化 DSL 建模求值',
        desc: '内置 OpenSCAD DSL 解析与求值器，几何走 Manifold CSG 运算，支持 ScadRig 刚体骨骼绑定。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/procscad.webp',
        tags: ['OpenSCAD', 'Manifold CSG', 'ScadRig 骨骼', 'FreeType 文字'],
        target: 'ScadStudio'
      },
      {
        id: 'gknexteditor',
        name: '🛠️ gkNextEditor',
        category: 'tool',
        categoryName: '综合编辑器',
        role: '节点式材质与场景调试',
        desc: '基于 ImGui 与 entt::meta 反射的综合可视化编辑器，支持节点材质图、cvar 实时调优与 Play-in-Editor。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/gknexteditor.webp',
        tags: ['ImGui 编辑器', '材质节点图', 'PIE 模式', 'entt 反射'],
        target: 'gkNextEditor'
      },
      {
        id: 'studiosim',
        name: '🏢 StudioSim / CitySolSim',
        category: 'game',
        categoryName: '模拟经营',
        role: '工作室经营与城市模拟',
        desc: '验证本地 LLM 驱动的随机事件、员工日常行为目标、SCAD 办公室生成与职业配色体系。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/citysim.webp',
        tags: ['LLM 事件', '城市模拟', 'ScadRig 职业', 'AI 状态机'],
        target: 'StudioSim'
      },
      {
        id: 'nextdayz',
        name: '🧟 NextDayZ',
        category: 'game',
        categoryName: '生存战斗',
        role: '生存探索与 AI 行为树',
        desc: '开放世界角色移动控制、NavGrid A* 寻路、敌对 AI 行为树决策与第三人称射击/战斗交互原型。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nextdayz.webp',
        tags: ['AI 行为树', 'NavGrid A*', '角色控制', '战斗系统'],
        target: 'NextDayz'
      },
      {
        id: 'nexttotalwar',
        name: '⚔️ NextTotalWar / NextRA',
        category: 'game',
        categoryName: '军团 RTS',
        role: '大规模战术与确定性同步',
        desc: '大规模军团方阵战术模拟与 Lockstep 确定性帧同步验证，支持百万级单位高效批处理。',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nexttotalwar.webp',
        tags: ['Lockstep 帧同步', '军团方阵', '确定性重放', 'GPU-Driven'],
        target: 'NextTotalwar'
      }
    ]
  },
  benchmark: {
    badge: 'Performance & Efficiency',
    title: '性能参考与 GPU 吞吐指标',
    desc: '性能是核心设计约束之一。通过全 Bindless、GPU-Driven 单 Draw 剔除、辐射缓存复用与稀疏驻留，在极小显存预算下爆发极致吞吐。',
    metrics: [
      { val: '420+', unit: 'FPS', label: '1/2spp 硬件路径追踪', sub: 'RTX 5070 Ti · 720p 物理材质测试', colorClass: 'text-cyan' },
      { val: '1,600+', unit: 'FPS', label: '延迟光栅化渲染', sub: 'SoftwareModern 极速无环境光管线', colorClass: 'text-amber' },
      { val: '< 1,000', unit: 'MiB', label: '显存常驻控制', sub: '稀疏显存布局与辐射缓存按需复用', colorClass: 'text-green' },
      { val: '34,000+', unit: 'Draws', label: '海量小行星带 GPU 单 Draw', sub: '540万+ 视锥三角形 GPU 硬件级剔除', colorClass: 'text-purple' }
    ],
    tableTitle: '典型场景 Benchmark 数据（RTX 5070 Ti / 720p 基准）',
    tableEnv: 'Vulkan 1.4 · Driver 596.49 · 3s 预热 + 3s 采样统计',
    tableHeaders: ['场景', '渲染管线', '帧时间', 'FPS', '显存占用', 'Draw Call (剔除后/总数)', '三角形 (剔除后/总数)'],
    reproduceLabel: '复现命令：'
  },
  quickstart: {
    badge: 'One Command to Rule',
    title: '单命令工具链，几分钟内启动',
    desc: '所有平台统一由 gnb CLI 调度。环境检测、依赖安装、构建编译、单元测试与资产打包，一行搞定。',
    copied: '已复制到剪贴板',
    copyBtn: '复制命令',
    platforms: [
      { id: 'windows', name: 'Windows (VS2022 / Ninja)', icon: '🪟' },
      { id: 'linux', name: 'Linux (Ubuntu / Arch)', icon: '🐧' },
      { id: 'macos', name: 'macOS (Apple Silicon)', icon: '🍎' },
      { id: 'remote', name: 'WebRTC 远程游玩', icon: '🌐' }
    ],
    footerTip: '不需要繁琐的手动配置，gnb setup 会自动拉取匹配版本的 LunarG Vulkan SDK、Slang 编译器与 vcpkg 依赖。'
  },
  ecosystem: {
    badge: 'Ecosystem & Tech Stack',
    title: '站在卓越图形开源生态之上',
    desc: '精选行业前沿、高内聚、现代标准的 C++20 / 图形库体系，构建稳固可靠的基础设施。',
    licenseTitle: '开源许可与自由共享',
    licenseDesc: 'gkNextEngine 整体代码以 MIT 协议 开源发布，欢迎自由研究、原型验证与二次创作。',
    starBtn: 'Star on GitHub',
    groups: [
      {
        category: 'Graphics & Hardware (图形与底层)',
        items: [
          { name: 'Vulkan 1.4', role: '现代跨平台底层 API' },
          { name: 'Slang', role: '模块化着色器与光线查询' },
          { name: 'SDL3', role: '窗口、事件与跨平台上下文' },
          { name: 'GLM / MeshOpt', role: '数学运算与网格优化' }
        ]
      },
      {
        category: 'Architecture & Physics (架构与物理)',
        items: [
          { name: 'entt', role: '高性能 ECS 与反射系统' },
          { name: 'Jolt Physics', role: '3D 刚体与角色物理模拟' },
          { name: 'ImGui', role: '综合编辑器与材质节点图' },
          { name: 'Tracy', role: '纳秒级 CPU/GPU 性能剖析' }
        ]
      },
      {
        category: 'Content Pipelines (结构化内容管线)',
        items: [
          { name: 'OpenSCAD / Manifold', role: 'DSL 程序化 CSG 几何' },
          { name: 'LDraw / LGEO', role: '数字乐高标准与真实 PBR' },
          { name: 'Gaussian Splatting', role: 'PlayCanvas SOG 共存渲染' },
          { name: 'TinyGLTF / Draco', role: 'glTF 2.0 场景模型导入' }
        ]
      },
      {
        category: 'Scripting & AI (脚本与大模型)',
        items: [
          { name: 'C# (.NET CoreCLR/AOT)', role: '双后端高性能托管脚本' },
          { name: 'llama.cpp / Gemma', role: '本地大模型推理与决策' },
          { name: 'RmlUi', role: 'HTML/CSS 界面引擎' },
          { name: 'WebRTC / Vulkan Video', role: 'Remote Play 远程云游玩' }
        ]
      }
    ]
  }
}

export const enUS: I18nContent = {
  hero: {
    title1: 'Lightweight, modern, and ray-traced.',
    title2: 'Free & open source cross-platform 3D engine',
    subtitle: 'Built with C++20 and Vulkan for real-time path tracing, game prototypes, and AI-native workflows.',
    quickstart: 'Quick Start (gnb)',
    github: 'GitHub Repo',
    docs: 'Documentation →',
    scrollHint: 'Scroll to explore more',
    scenes: [
      {
        name: 'Still Scene',
        title: 'gkNextRenderer · still.glb',
        desc: 'Outdoor natural lighting · Physical PBR · 1/2spp hardware path tracing',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/still.webp',
        fps: '427 FPS (2.34ms)',
        pipeline: 'PathTracing + SHARC',
        vram: 'VRAM: 978 MiB'
      },
      {
        name: 'Luxball Material',
        title: 'gkNextRenderer · luxball.glb',
        desc: 'Metals / Glass / Plastics · High dynamic multi-light reflections',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/luxball.webp',
        fps: '353 FPS (2.83ms)',
        pipeline: 'PathTracing + ReSTIR',
        vram: 'VRAM: 978 MiB'
      },
      {
        name: 'Playground',
        title: 'gkNextRenderer · playground.glb',
        desc: 'Voxel geometry · Global diffuse illumination · Real-time CSM shadows',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/playground.webp',
        fps: '606 FPS (1.65ms)',
        pipeline: 'Hybrid Rendering',
        vram: '925 MiB'
      },
      {
        name: 'Conference Room',
        title: 'gkNextRenderer · confroom.glb',
        desc: 'High bounce indoor lighting · Spatial audio & physics debug viewport',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/confroom.webp',
        fps: '1,614 FPS (0.62ms)',
        pipeline: 'SoftwareModern',
        vram: '925 MiB'
      }
    ]
  },
  features: {
    badge: 'Core Architecture',
    title: 'Why choose gkNextEngine?',
    desc: 'An uncompromising personal R&D playground. Guided by modern graphics standards and runtime performance constraints to deliver an AI-friendly next-gen engine.',
    items: [
      {
        title: 'Real-Time Path Tracing & Hybrid',
        summary: 'Targeted at true runtime 1/2spp path tracing with the perfect balance of fidelity and frame rates.',
        accentColor: '#00d2ff',
        glowColor: 'rgba(0, 210, 255, 0.1)',
        points: [
          'SHARC world radiance cache & ReSTIR DI direct lighting resampling',
          'NVIDIA DLSS / DLSS-RR / FSR / SGSR2 upscaling and reconstruction',
          'Seamless pipeline hot-switching: PathTracing & deferred rasterizer',
          'PlayCanvas Gaussian Splatting (SOG) coexists with meshes in one frame'
        ]
      },
      {
        title: 'Modern GPU Architecture & Lean Core',
        summary: 'Strictly constrained first-party Core under 50k LOC for maximum runtime throughput.',
        accentColor: '#ff7a00',
        glowColor: 'rgba(255, 122, 0, 0.1)',
        points: [
          'Fully bindless resource indexing with tens of thousands of instances in a single Draw',
          'Visibility Buffer and soft Mesh Shader geometry pipeline',
          'entt ECS + Reflection layer: unified runtime, editor properties, and scripts',
          'Deep Jolt Physics integration for collision, rigid bodies, and character actors'
        ]
      },
      {
        title: 'AI Native Structured Assets & Verification',
        summary: 'Empower AI with parseable, editable, and automatable 3D assets and runtime feedback.',
        accentColor: '#10b981',
        glowColor: 'rgba(16, 185, 129, 0.1)',
        points: [
          'Native OpenSCAD DSL, LDraw (LEGO), and glTF 2.0 parsing',
          'Declarative input driver scripts (.agentscript.json) with automated assertions',
          'Hidden window fast snapshot & visual regression testing (gnb shot)',
          'Integrated local llama.cpp / Gemma inference service for in-game NPC decisions'
        ]
      },
      {
        title: 'Unified Toolchain & Remote Play',
        summary: 'Single entry point for full lifecycle across desktop and mobile platforms.',
        accentColor: '#a855f7',
        glowColor: 'rgba(168, 85, 247, 0.1)',
        points: [
          'Unified gnb CLI: manages setup, build, run, test, and packaging',
          'Cross-platform parity: Windows, Linux, macOS, Android, and iOS',
          'WebRTC Remote Play: zero-install 60FPS streaming in browser',
          'Tracy CPU/GPU timeline & Superluminal profiling integration'
        ]
      }
    ]
  },
  showcase: {
    badge: 'Prototypes & Ecosystem',
    title: '15+ Subprojects & Gameplay Prototypes',
    desc: 'More than a rendering engine: a rich testbed for game mechanics and content pipelines, sharing unified ECS, physics, and rendering foundations.',
    categories: [
      { id: 'all', name: 'All Prototypes' },
      { id: 'game', name: 'Games & Simulation' },
      { id: 'pipeline', name: 'Content & DSL' },
      { id: 'tool', name: 'Editor & Tools' }
    ],
    projects: [
      {
        id: 'airportsim',
        name: '✈️ AirportSim',
        category: 'game',
        categoryName: 'Simulation',
        role: 'Airport Ecology & Agent Decisions',
        desc: 'Validates SCAD-described POIs, passenger queues, NavGrid A* pathfinding, and local LLM agent decisions.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/airportsim.webp',
        tags: ['A* Pathfinding', 'LLM Agent', 'SCAD POI', 'Queue Sim'],
        target: 'AirportSim'
      },
      {
        id: 'magicalego',
        name: '🧱 MagicaLego',
        category: 'game',
        categoryName: 'Voxel Assembly',
        role: 'Digital Brick Physics Playground',
        desc: 'Voxel and LEGO style scene building and physics mechanics with raycast grabbing and stud snapping.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/magicalego.webp',
        tags: ['Jolt Physics', 'Voxel Building', 'LGEO Material', 'Raycast'],
        target: 'MagicaLego'
      },
      {
        id: 'brotato3d',
        name: '🥔 Brotato3D',
        category: 'game',
        categoryName: 'Survival Shooter',
        role: 'Top-Down 3D Horde Shooter',
        desc: 'Top-down horde survival shooter validating massive wave spawns, high-performance object pools, and weapon skills.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brotato3d.webp',
        tags: ['Horde Pooling', 'Skill System', 'Jolt Physics', 'C# Script'],
        target: 'Brotato3D'
      },
      {
        id: 'brickplayer',
        name: '🧩 BrickPlayer',
        category: 'pipeline',
        categoryName: 'LDraw LEGO',
        role: 'LDraw Standard Prototype',
        desc: 'Digital LEGO brick parser conforming to official LDraw specs, mapping colors to PBR materials with assembly semantics.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/brickplayer.webp',
        tags: ['LDraw Spec', 'PBR Palette', 'Part Snapping', 'Procedural'],
        target: 'BrickPlayer'
      },
      {
        id: 'scadstudio',
        name: '📐 ScadStudio',
        category: 'pipeline',
        categoryName: 'OpenSCAD',
        role: 'Procedural DSL Modeling & Eval',
        desc: 'Embedded OpenSCAD DSL parser and evaluator, powered by Manifold CSG operations and ScadRig bone hierarchies.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/procscad.webp',
        tags: ['OpenSCAD', 'Manifold CSG', 'ScadRig', 'FreeType Text'],
        target: 'ScadStudio'
      },
      {
        id: 'gknexteditor',
        name: '🛠️ gkNextEditor',
        category: 'tool',
        categoryName: 'Comprehensive Editor',
        role: 'Material Node Graph & Debugging',
        desc: 'ImGui and entt::meta reflection editor supporting material node authoring, real-time cvar tuning, and Play-in-Editor.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/gknexteditor.webp',
        tags: ['ImGui Editor', 'Material Graphs', 'PIE Mode', 'entt Meta'],
        target: 'gkNextEditor'
      },
      {
        id: 'studiosim',
        name: '🏢 StudioSim / CitySolSim',
        category: 'game',
        categoryName: 'Sim Strategy',
        role: 'Studio & City Simulation',
        desc: 'Validates local LLM event generation, employee daily routines, SCAD procedural offices, and career palettes.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/citysim.webp',
        tags: ['LLM Events', 'City Sim', 'ScadRig Careers', 'AI FSM'],
        target: 'StudioSim'
      },
      {
        id: 'nextdayz',
        name: '🧟 NextDayZ',
        category: 'game',
        categoryName: 'Survival Combat',
        role: 'Exploration & AI Behavior Trees',
        desc: 'Character locomotion, NavGrid A* pathfinding, hostile AI behavior trees, and third-person combat prototypes.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nextdayz.webp',
        tags: ['Behavior Tree', 'NavGrid A*', 'Locomotion', 'Combat'],
        target: 'NextDayz'
      },
      {
        id: 'nexttotalwar',
        name: '⚔️ NextTotalWar / NextRA',
        category: 'game',
        categoryName: 'Legion RTS',
        role: 'Massive Tactics & Lockstep Sync',
        desc: 'Massive army formation tactics simulation and deterministic Lockstep replay validation with millions of units.',
        image: 'https://github.com/gameknife/gkNextEngine/releases/download/readme-assets-v1/nexttotalwar.webp',
        tags: ['Lockstep Sync', 'Legion Formations', 'Replay Parity', 'GPU-Driven'],
        target: 'NextTotalwar'
      }
    ]
  },
  benchmark: {
    badge: 'Performance & Efficiency',
    title: 'Performance & GPU Throughput',
    desc: 'Performance is a primary constraint. Leveraging Bindless, GPU-Driven single Draw culling, radiance caching, and sparse residency for extreme throughput within tight VRAM budgets.',
    metrics: [
      { val: '420+', unit: 'FPS', label: '1/2spp Hardware Path Tracing', sub: 'RTX 5070 Ti · 720p PBR Material Benchmark', colorClass: 'text-cyan' },
      { val: '1,600+', unit: 'FPS', label: 'Deferred Rasterization', sub: 'SoftwareModern high-speed ambient-free pipeline', colorClass: 'text-amber' },
      { val: '< 1,000', unit: 'MiB', label: 'VRAM Footprint Control', sub: 'Sparse residency & on-demand radiance caching', colorClass: 'text-green' },
      { val: '34,000+', unit: 'Draws', label: 'Massive Asteroid GPU Single Draw', sub: '5.4M+ frustum triangles culled on GPU hardware', colorClass: 'text-purple' }
    ],
    tableTitle: 'Representative Benchmark Data (RTX 5070 Ti / 720p Baseline)',
    tableEnv: 'Vulkan 1.4 · Driver 596.49 · 3s warmup + 3s sampling',
    tableHeaders: ['Scene', 'Pipeline', 'Frame Time', 'FPS', 'VRAM', 'Draws (Culled / Total)', 'Triangles (Culled / Total)'],
    reproduceLabel: 'Reproduce with:'
  },
  quickstart: {
    badge: 'One Command to Rule',
    title: 'Single-Command Toolchain, Ready in Minutes',
    desc: 'All platforms orchestrated uniformly by gnb CLI. Environment diagnostics, dependency setup, compilation, tests, and packaging in one line.',
    copied: 'Copied to clipboard',
    copyBtn: 'Copy Command',
    platforms: [
      { id: 'windows', name: 'Windows (VS2022 / Ninja)', icon: '🪟' },
      { id: 'linux', name: 'Linux (Ubuntu / Arch)', icon: '🐧' },
      { id: 'macos', name: 'macOS (Apple Silicon)', icon: '🍎' },
      { id: 'remote', name: 'WebRTC Remote Play', icon: '🌐' }
    ],
    footerTip: 'No tedious manual configuration needed. gnb setup automatically downloads matching LunarG Vulkan SDK, Slang compiler, and vcpkg dependencies.'
  },
  ecosystem: {
    badge: 'Ecosystem & Tech Stack',
    title: 'Standing on the Shoulders of Great Graphics OSS',
    desc: 'Carefully curated industry-leading, modular, modern C++20 and graphics libraries.',
    licenseTitle: 'Open Source & Freedom to Create',
    licenseDesc: 'gkNextEngine codebase is released under the MIT License. Free for research, prototyping, and creative derivative works.',
    starBtn: 'Star on GitHub',
    groups: [
      {
        category: 'Graphics & Hardware',
        items: [
          { name: 'Vulkan 1.4', role: 'Modern cross-platform graphics API' },
          { name: 'Slang', role: 'Modular shaders & Ray Query' },
          { name: 'SDL3', role: 'Windowing, events & contexts' },
          { name: 'GLM / MeshOpt', role: 'Math routines & mesh optimization' }
        ]
      },
      {
        category: 'Architecture & Physics',
        items: [
          { name: 'entt', role: 'High-performance ECS & reflection' },
          { name: 'Jolt Physics', role: 'Rigid body & character simulation' },
          { name: 'ImGui', role: 'Integrated editor & node graphs' },
          { name: 'Tracy', role: 'Nanosecond CPU/GPU profiling' }
        ]
      },
      {
        category: 'Content Pipelines',
        items: [
          { name: 'OpenSCAD / Manifold', role: 'DSL procedural CSG geometry' },
          { name: 'LDraw / LGEO', role: 'Digital LEGO spec & physical PBR' },
          { name: 'Gaussian Splatting', role: 'PlayCanvas SOG co-rendering' },
          { name: 'TinyGLTF / Draco', role: 'glTF 2.0 scene importing' }
        ]
      },
      {
        category: 'Scripting & AI',
        items: [
          { name: 'C# (.NET CoreCLR/AOT)', role: 'Dual-backend high performance scripting' },
          { name: 'llama.cpp / Gemma', role: 'Local LLM inference & agent decisions' },
          { name: 'RmlUi', role: 'HTML/CSS UI engine' },
          { name: 'WebRTC / Vulkan Video', role: 'Remote Play in browser' }
        ]
      }
    ]
  }
}
