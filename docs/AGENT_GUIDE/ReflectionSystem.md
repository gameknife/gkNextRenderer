# Reflection System

当前反射层用 `entt::meta` 同时服务编辑器 PropertyWidgets 和属性命令历史。实现位于 `src/Engine/Runtime/Reflection/`；不要按已删除的“自注册 builder”设计工作，那套方案没有落地。

反射层有三个消费者：编辑器 PropertyWidgets、属性命令历史，以及 C# 脚本绑定。第三个是从反射清单
自动生成的强类型 component wrapper，见下文“脚本绑定”与
[.NET Bindings](DotNetBindings.md)。

## 当前结构

```text
ReflectionMacros.hpp      component identity + RegisterReflection 声明
ReflectionRegistry.*      Engine 内建类型的集中注册入口
GlmTypeSupport.hpp        GLM 与已知容器 meta type
PropertyMeta.hpp          UI/JS flags、range、tooltip
PropertyTypes.hpp         封闭的 widget type enum
PropertyAccessor.*        meta data 枚举、类型推断、get/set

Editor/PropertyWidgets.*  ImGui widget + PropertyCommand
```

`REFLECT_COMPONENT(ClassName)` 只生成 `GetTypeName/GetTypeId/GetMetaType` 和 `static RegisterReflection()` 声明。它不是 fluent registration macro，也不会触发静态初始化。

## 注册生命周期

Engine 初始化时 `NextEngine` 调用一次 `Reflection::RegisterAllReflection()`。该函数先注册 GLM/container，再显式调用 Engine 内建 component、Node、Scene 和 Engine 的 `RegisterReflection()`。

因此新增 Engine 内建 component 要做三件事：

1. class 继承 `Assets::Component`，在 public 区写 `REFLECT_COMPONENT(MyComponent)`；
2. 在 `.cpp` 实现 `MyComponent::RegisterReflection()`；
3. 在 `ReflectionRegistry.cpp::RegisterAllReflection()` 增加显式调用。

可选模块不能把自己的类型塞进 Engine registry，因为 Engine 不得依赖 Modules。模块在自己的 `Install()` 中注册，例如 `SplatModule::Install()` 调 `GaussianSplatComponent::RegisterReflection()`。注册必须早于属性查询/JS binding，并由装配代码保证只发生一次。

## Component 示例

```cpp
// MyComponent.hpp
class MyComponent final : public Assets::Component
{
public:
    REFLECT_COMPONENT(MyComponent)

    void SetStrength(float value) { strength_ = value; }
    float GetStrength() const { return strength_; }

private:
    float strength_ = 1.0f;
};

// MyComponent.cpp
void MyComponent::RegisterReflection()
{
    using namespace entt::literals;
    using namespace Reflection;

    entt::meta_factory<MyComponent>()
        .type("MyComponent"_hs)
        .data<&MyComponent::SetStrength, &MyComponent::GetStrength>("Strength")
            .custom<PropertyMeta>(
                PropertyPresets::Range("Strength", "General", 0.0f, 4.0f,
                                       "Runtime strength multiplier"));
}
```

优先注册 setter/getter，而不是直接暴露 private field。read-only 属性使用 `.data<nullptr, &Getter>()`；可调用脚本 method 使用 `.func<&Method>("Method")`。property name 是编辑器命令与脚本绑定共同使用的稳定 API，改名需要同步兼容处理。

## PropertyMeta

`PropertyPresets` 提供 Editable、ReadOnly、Range、Hidden、Transient。默认 metadata 带 `ScriptExposed`；如果属性只供编辑器使用，要显式去掉该 flag——默认值会让它出现在生成的 C# wrapper 里。

- `ReadOnly`：PropertyWidgets 和脚本对象不提供 setter。
- `Hidden`：编辑器隐藏；不要把它当序列化策略。
- `ScriptExposed`：脚本绑定包含该属性，即 `Components.g.cs` 会为它生成 C# 属性。旧名 `JSExposed` 保留为 deprecated 别名一个版本周期。
- `Transient`：意图为不持久化；当前 SceneExport 并不会自动序列化所有反射属性。
- `HasRange`：numeric widget 使用 min/max。

缺少 custom metadata 时，`PropertyAccessor` 使用 property name 和 `General` category；这会默认 script exposed，敏感/内部状态不要依赖默认值。

## 类型与容器限制

`PropertyAccessor::DeducePropertyType()` 当前识别 Bool、Int32、UInt32、Float、Double、String、Vec2/3/4、Quat、Mat4、Enum 和 Array。编辑器有常用 scalar/vector/quat/enum/array widget；Mat4 虽能识别，但当前没有专用 PropertyWidgets case。`AssetRef` enum 值只是预留，当前没有推断路径。

已知 container registry 只有：

- `std::array<uint32_t, 16>`；
- `std::vector<uint32_t/int32_t/float/std::string>`。

添加容器类型必须同步 `RegisterContainerTypes()`、`FindContainerTypeInfo()` 和 `PropertyWidgets::DrawArray()` 的实际 element handling。仅让 `entt` 报告 `is_sequence_container()` 不保证编辑器能写元素。

Enum 要先以 `entt::meta_factory<Enum>()` 注册所有 value，再把 enum property 注册到 component；widget 通过 meta data 生成下拉项。不要引入未注册 enum 并期待反射自动枚举 C++ 定义。

## 编辑器与 undo/redo

`PropertiesPanel` 调 `PropertyWidgets::DrawComponentProperties(component, &engine.GetCommandHistory())`。widget 先取 old value；变化后创建 DevTools 的 `Runtime::Command::PropertyCommand`，由 Engine-owned `CommandHistory` 执行。没有 history 参数时才直接 `PropertyAccessor::SetPropertyValue()`。

因此 editor property side effect 应放在 setter/command 可重复执行的路径，保证 execute/undo/redo 一致。不要在 ImGui 绘制分支中额外修改 Scene，否则 redo 无法重放。

## 脚本绑定

C# 侧的 `assets/csharp/GkNext.Engine/Components.g.cs` 由 `gnb csharpgen` 从提交的反射清单
`src/Modules/NextDotNet/ReflectionManifest.json` 生成，清单本身由 `gkNextRenderer --dump-reflection`
导出。生成器消费的正是 `PropertyAccessor::GetProperties()` 过滤 `ScriptExposed` 后的集合。

**改了反射注册就要 `gnb csharpgen --refresh` 并提交清单与生成文件。**
`Test_ReflectionManifest.cpp` 会拿提交的清单和实时反射逐项比对，忘了刷新是测试失败而不是静默漂移。

property name 与 propId（name 的 entt hash）是生成 C# 的编译期常量，改名等于改脚本侧 API。

新增 `PropertyType` 时要审计五处：

- `PropertyAccessor::DeducePropertyType`；
- `PropertyWidgets`；
- container/enum handling；
- undo/redo 的 `meta_any` copy 语义；
- 脚本侧类型映射：`tools/gnb/internal/csharpgen/components.go` 的 `accessors`（绑定它）或
  `skipReasons`（明确声明还不绑定，理由会写进生成文件）。生成器对未知类型直接报错，不会静默跳过。

## 验证

Engine 内建反射改动构建 `gkNextRenderer gkNextUnitTests`；模块类型还构建一个安装该模块的 consumer。运行对应 unit tag，并在 Editor 中验证显示、范围、undo/redo。只验证 `entt::resolve<T>()` 非空不足以证明 UI 与命令两条链都可用；P5 之后还要加上 C# wrapper 一条。
