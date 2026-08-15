# VITURE Carina AR（可选模块）

`NextViture` 将 VITURE Carina（Luma Ultra）的 3DOF / 6DOF 姿态接入
`gkNextRenderer` 与 `gkNextEditor`。它仅服务于 macOS arm64 桌面构建，默认关闭。

## 默认行为

默认配置中，CMake 只检查仓库内的 SDK 压缩包；未放置时不会解压、不创建
`NextViture` 静态库，
`gkNextRenderer` 和 `gkNextEditor` 也不会链接该模块或分发任何 VITURE
dylib。AR 专用命令行参数也不会注册；传入 `--ar` 会按未知选项报错，而不会
静默降级。

## 启用

SDK 不随仓库分发。将官方 macOS arm64 压缩包放到以下固定位置：

```text
external/viture/VITURE_XR_Glasses_SDK_for_MacOS_arm64.zip
```

下次 CMake 配置会自动以压缩包 SHA-256 判断是否需要解压，并将 SDK 解压到
当前构建目录的 `external/viture-sdk/`。在 macOS arm64 上直接构建即可：

```bash
./gnb.sh build gkNextRenderer gkNextEditor
```

压缩包不存在、目标不是 macOS arm64 desktop 时，模块保持关闭，不需要额外
选项或环境变量。

启用后，构建系统只将 `libglasses.dylib` 和 `libcarina_vio.dylib` 复制到链接
`NextViture` 的应用输出目录，并对 `libglasses.dylib` 做本地 ad-hoc 重签名，
避免 macOS 在加载官方 SDK 的无效内嵌签名时终止应用。要恢复默认构建，移除该
压缩包后重新配置即可。

## 运行

```bash
./gnb.sh run gkNextRenderer --ar --load-scene assets/models/playground.glb
./gnb.sh run gkNextEditor --ar --load-scene assets/models/playground.glb
```

`--ar` 默认使用 6DOF；`--ar-dof 3` 使用仅朝向追踪。首个有效姿态会成为软件
原点，按 `R` 可重新居中。物理平移以米为单位，使用
`--ar-world-units-per-meter <scale>` 适配场景单位；
`--ar-smoothing-hz <hz>` 控制平滑（`0` 关闭）。

模块只保留设备发现、SDK 生命周期和姿态读取。双目相机预览、GPU 纹理上传、
ImGui 调试面板与专用测试代码不属于正式集成，未被编译或分发。
