---
title: "Steam Deck / Arch Linux 部署注意事项"
category: note
status: 现行环境说明
owner: build
created: 2026-05-31
last_updated: 2026-07-17
---

# Steam Deck / Arch Linux 部署注意事项

Steam Deck 桌面模式按 Linux x86_64 路径构建。旧记录中某次 LDraw 单测失败和手工修补依赖的步骤属于当时快照，不能作为当前基线；遇到失败应以当前代码、日志和测试重新判断。

## 建议流程

```bash
./gnb.sh doctor
./gnb.sh setup
./gnb.sh build gkNextRenderer gkNextUnitTests
./out/build/linux/bin/gkNextUnitTests
./gnb.sh shot --scene assets/models/playground.glb
```

`gnb setup` 负责项目管理的 vcpkg、Slang/工具链和可选依赖，不要把个人机器上的绝对 SDK 路径写进 CMake。Arch/SteamOS 系统升级后若编译器、Vulkan loader 或 driver 改变，先重新跑 `doctor`/`setup`，必要时对受影响 target 使用 `--clean`。

## 平台差异

- 需要可用的 Vulkan driver；Steam Deck 通常走 Mesa RADV。用 `vulkaninfo` 和应用启动日志确认实际 GPU/driver，不凭旧机器记录推断。
- Linux gnb 不构建 Wails 原生窗口，裸 `gnb`/`gnb dashboard` 会打开浏览器；`gnb dashboard --no-open` 是 server-only 模式。
- Windows-only Streamline/DLSS 在 Linux 关闭。不要把其缺失误判为构建损坏。
- Remote Play 还要求 driver 暴露 Vulkan Video H.264 encode；不支持时目前没有软件 fallback。
- 游戏模式的只读文件系统、Flatpak 权限或外接存储挂载可能影响写入；开发和首次 setup 建议在桌面模式、可写 checkout 中完成。

## 验收口径

构建成功不等于运行成功。至少确认日志出现 `uploaded scene [...] to gpu`，运行相关单测，并用 `gnb shot` 检查截图。若特定 loader 失败，记录当前输入文件、测试名和 commit；不要把临时失败写成永久平台限制。
