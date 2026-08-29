# 移动端 application 目标：注册表 + 统一 `gk_add_application`

状态：现行。Android 与 iOS 可以打包 `src/Application/` 下任意一个登记过的 application，不再只有
写死的 `gkNextRenderer`（Android 上曾额外硬编码一个 `FlappyCSharp`）。

## 1. 问题

改动前，移动端能打哪些程序是分散在四处的硬编码：

| 位置 | 硬编码内容 |
| --- | --- |
| `src/Application/CMakeLists.txt` | `if(NOT ANDROID AND NOT IOS)` 把 Editor/Game/Util 整片排除，再单独 `add_subdirectory(Game/Flappy/FlappyCSharp)` |
| `src/Application/Render/gkNextRenderer/CMakeLists.txt` | 一大段只属于它的 iOS bundle 逻辑（Info.plist、bundle id、frameworks、资源拷贝、manifest 生成） |
| `tools/android/CMakeLists.txt` | `if(GK_ANDROID_APP STREQUAL ...)` 手写 applicationId / label |
| `tools/gnb/internal/android/android.go` | `androidApps` map，注释里写着"keep this in sync" |

再加上每个 application 的 `CMakeLists.txt` 都是 `add_executable` + `gk_configure_application`
的桌面写法，Android 需要的是"把整份引擎编进 .so 的 `add_library(SHARED)`"，两者对不上。于是新增
一个移动端程序 = 改四处 + 给那个程序手写一段 Android 分支。

## 2. 结构

### 2.1 注册表

[`src/Application/MobileApplications.json`](../../src/Application/MobileApplications.json) 是唯一
事实来源。每条记录声明目录、显示名、支持的平台、Android applicationId、iOS bundle id、可选的启动
场景，以及是否需要 .NET。**不在表里 = 仅桌面**，没有别的标记方式。

三个消费者读同一份文件：

- `src/cmake/MobileApplications.cmake` —— 引擎 configure 决定加哪个目录
- `tools/android/CMakeLists.txt` —— APK 的 applicationId 与 launcher label
- `tools/gnb/internal/mobileapps` —— `gnb android --app` / `gnb ios --app` 的取值与校验

`tools/gnb/internal/mobileapps/mobileapps_test.go` 守住表本身：目录存在、id 不重名、C# 游戏不出现
在 iOS 列表里。

### 2.2 统一入口 `gk_add_application`

`src/cmake/TargetHelpers.cmake` 里的 `gk_add_application` 是创建 application 的唯一方式：

```cmake
gk_add_application(<target>
    SOURCES  <application sources>
    [MODULES <runtime modules>]    # gk_target_runtime_modules，三平台通用
    [LINK    <libraries>]          # NextGameplay 这类引擎库；Android 直接编进 .so，跳过链接
    [DEFINES <definitions>]
    [INCLUDES <directories>]
    [NO_DEFAULT_MAIN]              # 自带 SDL 入口的程序
    [CORE_ONLY] [NO_UNITY] [NO_FAST_LINK] [MINIMAL_LINK])
```

它按平台分派：

- **Android** → `gk_add_android_application`：`add_library(SHARED)`，引擎/模块/Gameplay 源码全部编进
  这一个 .so，入口是 `AndroidMain.cpp`
- **iOS** → `add_executable(MACOSX_BUNDLE)` + `gk_configure_application` +
  `gk_configure_ios_application`（Info.plist、签名、frameworks、资源拷贝、`ios-app-<Config>.json`）
- **桌面** → `add_executable` + `gk_configure_application`

`LINK` 与 `MODULES` 的区别是这里唯一需要记住的事：`MODULES` 是可选引擎模块，`gk_target_runtime_modules`
在桌面链接静态库、在 Android 打开 `GK_MODULE_*` 宏；`LINK` 是 `NextGameplay` 这种引擎库，Android
把同样的源码直接编进 application，所以那里没有东西可链。

### 2.3 一次配置一个 application

移动端 configure **只 `add_subdirectory` 被选中的那一个 application 目录**。Android 上这是硬性
要求：每个 application 目标都要重复一遍完整引擎源码列表，声明 25 个就是 25 份编译规则。iOS 沿用
同样的形状，好处是 `GK_IOS_BUNDLE_ID` 这类"当前 app"语义不会含糊。

选谁由 cache 变量 `GK_MOBILE_APP` 决定：

- Android：`gnb` 把 `--app` 传给 `tools/android` 驱动的 `GK_ANDROID_APP`，驱动再通过
  `templates/app/build.gradle.in` 的 `arguments '-DGK_MOBILE_APP=@GK_ANDROID_APP@'` 传进引擎
  configure——Gradle 的 `targets '<name>'` 只有在 configure 也知道这个 application 时才找得到。
- iOS：`gnb ios build --app` 直接传 `-DGK_MOBILE_APP=`。换 app 会触发 reconfigure。
- 空值表示"注册表里该平台的第一条"，这就是默认值不写死在代码里的原因。

**因此每个 application 目录必须自洽**：它上面的 `CMakeLists.txt` 在移动端根本不会执行。这条约束
是 benchmark / editor / flappy 的 common 源码 glob 从父目录下沉到各自叶子目录的原因。

### 2.4 移动端没有命令行

Android 与 iOS 都没有命令行，只有 application identity 会编进启动参数：

- `GK_APPLICATION_NAME` —— argv[0]，即 `NextRenderer::SetApplicationIdentity` 看到的名字

初始场景不再从移动端注册表注入；每个 application 自行负责其场景加载策略。

`AndroidMain.cpp` 的模块安装现在和 `DesktopMain.cpp` 一样按 `#if GK_MODULE_*` 分支。Android 把所有
模块源码都编进 .so，所以过去无条件安装也能链上，但那样一来 application 声明的模块集合在 Android 上
形同虚设——benchmark 会拿到它没要的 NextAudio 与 GltfLoader。

## 3. 加一个移动端 application

1. 在 `MobileApplications.json` 里加一条：target、directory、label、platforms、androidId、
   iosBundleId，需要的话再加 requiresDotNet。
2. 确认该 application 的 `CMakeLists.txt` 走的是 `gk_add_application`，且不依赖父目录变量。
3. `gnb android build debug --app <target>` / `gnb ios build --app <target> --team-id <TEAM_ID>`。

不用改任何 CMake 逻辑、不用改 gnb、不用改 Gradle 模板。

## 4. 已知边界

- **iOS 没有 C#**：`cmake/SetupDotNet.cmake` 在 iOS 上直接关掉托管层，所以 `FlappyCSharp`、
  `Brotato3DCSharp`、`DotNetSandbox` 只登记 `android`。选中一个 `requiresDotNet` 的 application 而
  托管层不可用时，configure 会带着原因直接失败，而不是打出一个没有游戏逻辑的包。
- **登记 ≠ 已验证**：注册表说的是"这个 application 可以被移动端配置"。桌面代码里用到的桌面专有 API
  会在真正构建它时暴露出来——那是需要修的可移植性问题，不是注册表的问题。
- **Android 每个 app 一个 build 目录**（`out/build/android-<variant>[-<app>]`）：整份引擎都编进
  application，共用目录意味着每次换 app 全量重编。默认 app 保留历史路径，CI 产物路径不变。
