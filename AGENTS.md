# 🤖 Agent 指引

## 📍 核心指引位置

**完整分层指引**: `AGENT_GUIDE/README.md`

### 按任务类型快速导航

| 任务类型 | 阅读文件 | 预估时间 |
|---------|---------|---------|
| **简单任务** | `AGENT_GUIDE/core-patterns.md` | 30秒 |
| **复杂开发** | `AGENT_GUIDE/core-patterns.md` → `AGENT_GUIDE/contextual-rules.md` | 2.5分钟 |
| **紧急修复** | `AGENT_GUIDE/quick-commands.md` | 15秒 |

## ⚡ 立即行动清单

1. **阅读核心模式**: `AGENT_GUIDE/core-patterns.md`
2. **使用中文交流**: 所有回复使用中文
3. **使用TodoWrite**: 开始任务前创建任务清单
4. **遵循架构**: 使用 `CoreMinimal.hpp` 和 `PlatformCommon.h`
5. **验证构建**: 每次修改后运行 `./build.[bat|sh] [platform]`

## 🎯 核心命令

```bash
# 构建（选择你的平台）
./build.sh macos       # macOS
./build.sh linux       # Linux
./build.bat windows    # Windows
./build.bat android    # Android

# 快速验证
cd build/[platform]/bin && ./gkNextRenderer
```

## ✅ 成功标准

看到日志: `uploaded scene [CornellBox.proc] to gpu`

## 🏗️ 项目结构（补充）

- `src/` 核心代码：`Application/` 启动程序，`Rendering/`+`Vulkan/`+`Runtime/` 为引擎主体，`Assets/` 负责资源桥接
- `assets/` 存放资源与着色器，`tools/` 保存辅助脚本与文档
- `AGENT_GUIDE/` Agent专用分层指引目录

## 💡 关键历史经验

### 构建系统优化
- **统一脚本**: 优先使用 `build.bat [platform]` 而非分散的脚本
- **自动清理**: 必要时删除整个 `build/` 目录重新构建
- **包大小优化**: 生产构建可减少90%体积（2.2GB→216MB）

### 架构重构经验
- **平台抽象**: 使用 `PlatformCommon.h` 避免条件包含，提高可维护性
- **统一头文件**: `CoreMinimal.hpp` 集成所有通用功能，简化维护
- **编译期控制**: 使用 `#if ANDROID` 而非 `#ifdef ANDROID`

### 工作流程验证
- **多平台验证**: 各平台构建验证正常，统一脚本工作可靠
- **依赖管理**: vcpkg统一管理，支持缓存共享
- **功能验证**: 每次修改后必须构建+运行验证

---

**记住**: 始终从这里开始，然后按照任务复杂度选择阅读深度。这些经验来自实际项目重构，可直接应用到你的任务中。
