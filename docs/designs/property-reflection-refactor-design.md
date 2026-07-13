---
title: "编辑器 Property 反射机制改进 — 研究报告与改进方案"
category: design
status: 📝 草案（待讨论）
owner: engine
created: 2026-06-25
last_updated: 2026-06-25
---

# 编辑器 Property 反射机制改进 — 研究报告与改进方案

> 目标：在**保留 entt::meta 作为运行时反射底座**的前提下，把 `RegisterReflection()` 的注册写法从「逐属性手写 DSL + 多处平行硬编码表」收敛成「单一来源、声明式、可自注册」的形式，降低新增组件/属性的样板量与出错面。
>
> 非目标：不替换 entt（编辑器 Property 面板与 QuickJS 绑定**共用**同一套 entt::meta，替换成本与风险过高，见 §4.5）；不改动 ImGui 控件层的视觉风格；本轮不引入 C++26 静态反射。
>
> 本文供后续细节讨论与接手实现。所有源码引用为 `文件:行号`。

---

## 0. 结论（TL;DR）

当前反射「繁杂」的根因不在 entt 本身，而在于工程在 entt 之上**重复实现了 entt 已经提供的能力**，并把同一份信息分散到多个必须手工保持一致的地方：

1. **逐属性的注册 DSL 重复且字符串冗余** —— 每个属性都要写一行 `.data<&Set, &Get>("Name").custom<PropertyMeta>(PropertyPresets::Editable("Name", "Cat", "tip"))`，名字常常重复 2~3 次（标识名 / displayName / 分类），`RenderComponent` 甚至因此手写出 `RayCastVisible` / `RaycastVisible` 两条几乎相同的别名注册（`RenderComponent.cpp:18-21`）。
2. **平行硬编码表，必须双向同步** —— 容器类型在 `GlmTypeSupport.h` 的 `RegisterContainerTypes()`（`GlmTypeSupport.h:49`）注册一次，又在 `PropertyAccessor.cpp` 的 `FindContainerTypeInfo()`（`PropertyAccessor.cpp:30`）硬编码一张并行表；二者任意一侧漏改，属性就会静默退化成 `Unknown`。
3. **中心化注册清单，漏写无报错** —— 每个组件都要在 `ReflectionRegistry.cpp:29-36` 或 `GameplayReflectionRegistry.cpp` 里手工添一行 `T::RegisterReflection()`，漏写则该组件在编辑器里**静默没有任何属性**，且编译期不报错。
4. **类型系统是封闭枚举 + 巨型 switch** —— `PropertyType` 枚举（`PropertyTypes.h`）配合三处 switch（`PropertyWidgets.cpp:146`、`:958`、`PropertyAccessor.cpp:201`）；每新增一种可编辑类型都要改这几处。
5. **枚举逐项手写** —— 每个枚举值都要 `.data<E::Value>("Value")` 手列一遍（如 `PhysicsComponent.cpp`、`AIAgentComponent.cpp`），与枚举定义两地维护。

**推荐路线（分三阶段，渐进、可回退）**：

- **阶段 1（低风险、收益最大）**：引入一层薄包装 `Reflect::Class<T>` 流式 Builder + 改进宏，把「标识名 / 显示名 / 分类 / tooltip / range / 读写性」收敛到一行声明，显示名缺省自动由标识名推导；先迁移 1 个组件作样例。
- **阶段 2（去重去散）**：用 entt 原生 `is_sequence_container()` / `value_type()` 与枚举 `data()` 迭代，**删除** `FindContainerTypeInfo` 与 `RegisterContainerTypes` 的平行表；引入自注册（static initializer）干掉中心清单。
- **阶段 3（可选，开放扩展）**：把封闭的 `PropertyType` + switch 换成「按 meta_type 注册控件处理器」的开放注册表，新增类型从「改 N 处 switch」变成「注册 1 个 handler」。

下文 §1 现状、§2 痛点量化、§3 设计目标、§4 方案对比、§5 推荐方案与代码草图、§6 分阶段计划、§7 风险、§8 待讨论问题。

---

## 1. 现状梳理

### 1.1 链路全景

反射数据有**一个生产侧**和**两个消费侧**，这点对方案选择至关重要：

```
                         ┌────────────────────────────────────────┐
  生产侧（注册）          │   entt::meta 全局类型库（entt::resolve） │
  T::RegisterReflection() ─▶│   meta_factory<T>().data<>().custom<>() │
                         └───────────────┬────────────────────────┘
                                         │
                  ┌──────────────────────┴───────────────────────┐
   消费侧 A（编辑器）                                 消费侧 B（脚本）
   PropertyAccessor::GetProperties()                 QuickJSReflectionBridge
   → PropertyWidgets::DrawComponentProperties()      → AutoBindComponent / TS 定义生成
   （PropertyWidgets.cpp）                            （QuickJSReflectionBridge.hpp）
```

- **生产侧**：每个组件类用 `REFLECT_COMPONENT(ClassName)` 宏（`ReflectionMacros.h:5`）在头里声明 `GetTypeName/GetTypeId/GetMetaType/RegisterReflection`，并在 `.cpp` 手写 `RegisterReflection()`。
- **统一入口**：`Reflection::RegisterAllReflection()`（`ReflectionRegistry.cpp`）+ `NextGameplay::RegisterGameplayReflection()`（`GameplayReflectionRegistry.cpp`）各自维护一份组件清单并带一次性初始化布尔量。
- **消费侧 A（编辑器）**：`PropertiesPanel.cpp:426-448` 拿 `component->GetMetaType()`，交给 `PropertyWidgets::DrawComponentProperties()` 遍历属性、按 `PropertyType` 出对应 ImGui 控件。
- **消费侧 B（脚本）**：`QuickJSReflectionBridge.hpp` 复用 `PropertyAccessor` 把同一份 meta 自动绑定到 JS，并生成 TypeScript 定义。

> 关键结论：entt::meta 是**两个子系统的共享底座**，不是编辑器私有。任何「换掉 entt / 自己撸一套反射」的方案都要同时改造 JS 桥，成本与风险成倍放大（详见 §4.5）。

### 1.2 一个属性当前要写什么

以 `RenderComponent.cpp:14-40` 为例，注册一个布尔属性的最小单位是：

```cpp
entt::meta_factory<RenderComponent>()
    .type("RenderComponent"_hs)
    .data<&RenderComponent::SetVisible, &RenderComponent::GetVisible>("Visible")
        .custom<PropertyMeta>(PropertyPresets::Editable("Visible", "Rendering",
                              "Whether the object is visible"))
    // ... 每个属性重复上面两行
    .func<&RenderComponent::ToggleVisible>("ToggleVisible");
```

读写性通过模板参数表达：可写 `.data<&Set,&Get>`，只读 `.data<nullptr,&Get>`（如 `AIAgentComponent.cpp` 的状态量）。元数据通过位置参数构造 `PropertyMeta(display, cat, tip, flags, min, max)`（`PropertyMeta.h:46`）。

### 1.3 枚举与容器的额外注册

- **枚举**：在组件的 `RegisterReflection()` 里先把枚举逐值注册一遍，例如 `PhysicsComponent.cpp` 的 `ENodeMobility`、`AIAgentComponent.cpp` 的 `EAIAgentState/EBehaviorDebugState`。
- **容器**：必须先在 `GlmTypeSupport.h:49 RegisterContainerTypes()` 用 `.type("array_uint32_16")` 之类登记，**并且**在 `PropertyAccessor.cpp:30` 的 `containerTypes[]` 再硬编码一份「typeId → 元素类型」映射，两处都改才生效。

---

## 2. 痛点量化

| # | 痛点 | 证据 | 影响 |
| --- | --- | --- | --- |
| P1 | 逐属性 DSL 啰嗦、名字重复 2~3 次 | `RenderComponent.cpp:14-40`；`AIAgentComponent.cpp` 全文 | 每属性 2 行；新增/改名易漏改，可读性差 |
| P2 | 别名注册靠手抄 | `RenderComponent.cpp:18` 与 `:20` 的 `RayCastVisible`/`RaycastVisible` 两条重复 | 复制粘贴味、易腐化（本质是缺「别名」一等支持） |
| P3 | 容器类型平行双表，必须同步 | `GlmTypeSupport.h:49` ↔ `PropertyAccessor.cpp:30` | 漏一侧 → 属性静默变 `Unknown`，无编译报错 |
| P4 | 中心化注册清单，漏写无报错 | `ReflectionRegistry.cpp:29-36`、`GameplayReflectionRegistry.cpp` | 新组件忘登记 → 编辑器静默无属性 |
| P5 | 封闭 `PropertyType` + 三处 switch | `PropertyTypes.h`；`PropertyWidgets.cpp:146/958`；`PropertyAccessor.cpp:201` | 新增类型要改 3+ 处，扩展成本高 |
| P6 | 枚举值两地维护 | `PhysicsComponent.cpp`、`AIAgentComponent.cpp` | 枚举增删值要记得改注册，易漏 |
| P7 | 元数据位置参数易错位 | `PropertyMeta.h:46` 的 `(display, cat, tip, flags, min, max)` | 参数顺序记忆负担，Range 的 min/max 容易传反 |
| P8 | 一次性初始化样板重复 | 两个 Registry 各有 `sXxxInitialized` 守卫 | 模式重复，多模块时继续膨胀 |

> 注：P3/P4/P6 的共同特征是「**同一事实存在于多处、且不一致时静默失败**」——这是最危险的一类，应优先消除。

---

## 3. 设计目标

1. **单一事实来源（SSOT）**：属性名、显示名、分类、范围、读写性只写一次；容器/枚举信息从类型本身推导，删除平行表。
2. **声明式、低样板**：新增一个属性 ≈ 一行；缺省值智能推导（显示名由标识名 humanize，分类可继承「上一条」或类级默认）。
3. **漏配即报错或自愈**：组件忘记注册应在编译期/链接期暴露，或通过自注册根本不需要中心清单。
4. **开放扩展**：新增可编辑类型/自定义控件，不必修改既有 switch。
5. **零破坏迁移**：保持 entt::meta 底座与 `PropertyAccessor` 对外签名不变，JS 桥与编辑器消费侧无需大改；可逐组件灰度迁移。

---

## 4. 方案对比

### 4.1 方案 A — entt 之上的流式 Builder + 改进宏（推荐基底）

在 `meta_factory` 之上加一层 `Reflect::Class<T>`，把「读写访问 + 元数据」收敛到链式调用，显示名缺省自动推导：

```cpp
void RenderComponent::RegisterReflection()
{
    Reflect::Class<RenderComponent>("RenderComponent")
        .Property(&RenderComponent::visible_, "Visible").Category("Rendering")
            .Tooltip("Whether the object is visible")
        .Property(&RenderComponent::rayCastVisible_, "Raycast Visible").Category("Rendering")
            .Alias("RayCastVisible", "RaycastVisible")          // 别名一等支持，替代 P2
        .ReadOnlyProperty(&RenderComponent::modelId_, "Model ID").Category("Mesh")
        .Range(&RenderComponent::layerMask_, "Layer Mask", 0, 0xFFFFFFFF)
        .Method(&RenderComponent::ToggleVisible, "ToggleVisible");
}
```

- **优点**：增量、纯加法；底层仍是 entt（`.data<>()/.custom<>()`），消费侧零改动；可一次解决 P1/P2/P7；Builder 内部统一 humanize 显示名、统一构造 `PropertyMeta`，把位置参数错位（P7）挡在一个地方。
- **代价**：要写并维护 Builder 模板（一次性）；对私有成员需暴露访问（用成员指针时 entt 支持直接 `.data<&T::member>`，但工程现多为私有 + Get/Set，需保留 `.Property(&Set,&Get,...)` 重载）。
- **风险**：低。

### 4.2 方案 B — 自注册，消灭中心清单（解决 P4/P8）

每个组件在自己的 `.cpp` 用一个宏挂一个静态初始化器，把 `RegisterReflection` 自动登记进全局表：

```cpp
// 放在组件 .cpp 末尾
REGISTER_COMPONENT_REFLECTION(RenderComponent);
// 展开 ≈ 命名空间内 static 对象，ctor 调用 ReflectionRegistry::Add(&RenderComponent::RegisterReflection)
```

- **优点**：删除 `ReflectionRegistry.cpp`/`GameplayReflectionRegistry.cpp` 的人工清单与初始化守卫（P4/P8）；新增组件「就地」登记，认知负担最低。
- **代价/风险**：**链接器死代码消除**——若组件所在 TU 没有被显式引用，静态对象可能被裁掉导致「又静默没属性」。需要约定：把组件编进会被强制链接的静态库目标，或用 `/WHOLEARCHIVE`(MSVC) / `--whole-archive`(gcc/ld) / CMake `target_link_libraries(... -Wl,--whole-archive ...)`，或保留一个「touch list」兜底。静态初始化顺序也要注意（GLM/容器基础类型需先于组件注册——可用「基础类型在 Add 时惰性确保」规避）。
- **建议**：阶段 2 引入，并配套构建期约定 + 一条「已注册组件数」启动日志做自检。

### 4.3 方案 C — 复用 entt 原生能力，删平行表（解决 P3/P6）

entt::meta 本身支持序列容器与枚举的内省，工程已在 JS 桥 `GetEnumValues` 里用 `metaType.data()` 迭代枚举值（`QuickJSReflectionBridge.hpp:193`），但编辑器侧却另起了硬编码表。统一到原生 API 后：

- **容器**：仅保留 `.type()` 注册（让 entt 知道该容器），元素类型改用 `meta_type::is_sequence_container()` + `as_sequence_container().value_type()` 推导（`PropertyAccessor.cpp:258-272` 已有 fallback 实现，把它扶正为主路径）→ **删除** `FindContainerTypeInfo` 与 `containerTypes[]`（P3）。
- **枚举**：提供 `Reflect::Enum<E>()` helper 或宏统一注册；值名可由一处列出。注：自动「枚举值 ↔ 名字」目前 C++ 无内建，仍需列举一次，但可集中到枚举定义旁（X-macro 或 `magic_enum` 可进一步免列举，见 §4.6）。
- **优点**：消除最危险的「双表静默失败」；与 JS 桥行为一致。
- **风险**：低~中。需确认目标 entt 版本的 sequence-container traits 对 `std::array<T,N>` 的支持（工程已 `.type("array_uint32_16")` 注册并有 fallback 代码，基本可用；迁移时加单测覆盖）。

### 4.4 方案 D — 开放式控件注册表，替换封闭 switch（解决 P5）

把 `PropertyType` 封闭枚举 + 三处 switch，改成「按 `entt::id_type`(类型) 注册一个绘制/读写 handler」的开放表：

```cpp
PropertyWidgetRegistry::Register<glm::vec3>(
    [](const char* label, void* p, bool ro){ return DrawVec3(label, *static_cast<glm::vec3*>(p), 0.1f, ro); });
```

- **优点**：新增可编辑类型 = 注册一个 handler，不动既有代码（开闭原则）；自定义业务类型（如 `AssetRef`、颜色）可插拔。
- **代价**：改动消费侧 `PropertyWidgets` 的派发核心，影响面比 A/B/C 大；需要保留对 GLM/数组/枚举的内置 handler。
- **建议**：列为阶段 3 可选，先用 A/B/C 把样板降下来，确认收益后再决定是否做。

### 4.5 方案 E — 替换 entt / 自研反射（不推荐）

候选：RTTR、refl-cpp、visit_struct、boost.pfr、C++26 静态反射。

- **boost.pfr / 聚合反射**：只适用于聚合体（无私有成员、无自定义访问器），而本工程组件普遍是「私有字段 + Get/Set + 继承 `Component`」，**不满足聚合要求**，直接出局。
- **RTTR/refl-cpp**：能力够，但意味着**整套换底座**——`PropertyAccessor`、`QuickJSReflectionBridge`、`meta_any` 互转、TS 生成全部重写，且失去 entt 与 ECS 生态的一致性。
- **C++26 静态反射**：方向正确但当前工具链（C++20，见 `cmake/SetupPlatform.cmake`）不可用。
- **结论**：**不替换**。entt 已是编辑器 + 脚本双消费的共享底座，问题出在「用法」而非「底座」，包装即可。

### 4.6 方案 F — `magic_enum` 免列举枚举（小增强，可选）

引入 `magic_enum` 后，`Reflect::Enum<E>()` 可自动枚举所有值与名字，彻底消除 P6 的手列举。

- **优点**：枚举增删值零维护。
- **代价**：新增三方依赖；有编译期开销与值范围上限约束（默认 `[-128,127]`，可配）。
- **建议**：作为方案 C 的可选增强，视团队对依赖的接受度定。

### 4.7 方案小结

| 方案 | 解决痛点 | 改动面 | 风险 | 阶段 |
| --- | --- | --- | --- | --- |
| A 流式 Builder + 宏 | P1/P2/P7 | 生产侧+新增 Builder | 低 | 1（基底） |
| B 自注册 | P4/P8 | 生产侧+构建约定 | 中（链接裁剪） | 2 |
| C 复用 entt 原生 | P3/P6 | PropertyAccessor | 低~中 | 2 |
| D 控件注册表 | P5 | 消费侧派发核心 | 中 | 3（可选） |
| E 换库/自研 | （全部） | 全链路重写 | 高 | 否决 |
| F magic_enum | P6 | 枚举注册 | 低 | 2 可选 |

---

## 5. 推荐方案与代码草图

总体：**A 为基底 + C 去平行表 + B 去中心清单**，D/F 视收益再定。下面给出关键接口草图（仅示意，细节待讨论）。

### 5.1 `Reflect::Class<T>` Builder（方案 A）

```cpp
namespace Reflect
{
    template <typename T>
    class Class
    {
    public:
        explicit Class(const char* typeName)
            : factory_(entt::meta_factory<T>().type(entt::hashed_string{typeName})) {}

        // 直接成员指针（公有成员或允许暴露时）
        template <auto Member>
        Class& Property(const char* name) { /* .data<Member>(name) + 暂存当前 prop 以便链式补元数据 */ }

        // Get/Set 访问器对（兼容现有私有成员风格）
        template <auto Setter, auto Getter>
        Class& Property(const char* name) { /* .data<Setter,Getter>(name) */ }

        template <auto Getter>
        Class& ReadOnlyProperty(const char* name) { /* .data<nullptr,Getter>(name) */ }

        // 链式补充元数据（作用于「上一条 Property」）
        Class& Category(const char* c);
        Class& Tooltip(const char* t);
        Class& DisplayName(const char* d);     // 不调则由 name 自动 humanize
        Class& ReadOnly();
        Class& Range(float lo, float hi);
        Class& Alias(std::initializer_list<const char*> names);  // 替代 P2 手抄

        template <auto Fn>
        Class& Method(const char* name) { /* .func<Fn>(name) */ }

    private:
        // 在 commit 当前 prop 时统一构造 PropertyMeta 并 .custom<PropertyMeta>(...)
    };
}
```

要点：
- 显示名缺省：`"rayCastVisible_"`/`"RayCastVisible"` → `"Raycast Visible"` 的 humanize 规则集中在 Builder。
- `PropertyMeta` 的构造从「位置参数」改为「Builder 内部具名设置」，根除 P7。
- 别名 `Alias` 内部就是多注册几条 `.data<>`，但只写一次意图。

### 5.2 容器与枚举走原生（方案 C）

```cpp
// PropertyAccessor：删除 FindContainerTypeInfo / containerTypes[]，主路径走 entt 原生
PropertyType PropertyAccessor::DeducePropertyType(entt::meta_type type)
{
    // ... 基础类型与 GLM 保留 ...
    if (type.is_enum()) return PropertyType::Enum;
    if (type.is_sequence_container()) return PropertyType::Array;   // 不再查硬编码表
    return PropertyType::Unknown;
}

// 枚举集中注册 helper
template <typename E>
void Reflect::Enum(std::initializer_list<std::pair<E, const char*>> values); // 或 magic_enum 自动
```

> 迁移护栏：为 `std::array<uint32_t,16>` 与各 `std::vector<...>` 补单测，断言迁移前后 `DeducePropertyType` / `GetArrayElementType` 结果一致，再删旧表。

### 5.3 自注册（方案 B）

```cpp
// ReflectionRegistry：清单改为运行时聚合
namespace Reflection {
    void AddReflectionRegistrar(void(*fn)());     // 由静态初始化器调用
    void RegisterAllReflection();                 // 遍历已聚合的 registrar，幂等
}

#define REGISTER_COMPONENT_REFLECTION(T)                                   \
    namespace { struct T##_AutoReg {                                       \
        T##_AutoReg(){ ::Reflection::AddReflectionRegistrar(&T::RegisterReflection); } \
    } g_##T##_AutoReg; }
```

配套：构建侧对组件静态库启用 whole-archive，避免被裁剪；启动时打印「已注册组件数 / 属性数」自检日志。

### 5.4 迁移前后对比（直观收益）

```cpp
// 之前（每属性 2 行 + 名字重复 + 位置参数）
.data<&PhysicsComponent::SetLinearDamping, &PhysicsComponent::GetLinearDamping>("LinearDamping")
    .custom<PropertyMeta>(PropertyPresets::Editable("Linear Damping", "Physics", "Linear velocity damping"))

// 之后（一行声明，显示名自动推导，分类沿用）
.Property(&PhysicsComponent::SetLinearDamping, &PhysicsComponent::GetLinearDamping, "LinearDamping")
    .Tooltip("Linear velocity damping")     // Category("Physics") 可类级默认
```

---

## 6. 分阶段实施计划

**阶段 1（基底，~低风险）**
1. 实现 `Reflect::Class<T>` Builder + humanize + 类级默认分类。
2. 迁移 1 个代表组件（建议 `RenderComponent`，顺手修掉 P2 的别名重复）作为范式，保留旧写法可共存。
3. 加最小单测：迁移前后 `GetProperties()` 输出（名/类型/读写/元数据）逐字段一致。

**阶段 2（去重去散）**
4. 用 entt 原生容器/枚举内省替换平行表（方案 C），删 `FindContainerTypeInfo`/`RegisterContainerTypes` 重复，补容器单测。
5. 引入自注册（方案 B）+ 构建期 whole-archive 约定 + 启动自检日志，删中心清单。
6. （可选）引入 `magic_enum` 免列举枚举（方案 F）。
7. 批量迁移其余组件（共约 11 个 `RegisterReflection`）。

**阶段 3（可选，开放扩展）**
8. 评估方案 D：把 `PropertyType` + switch 改为按类型注册的控件 handler 表，支持自定义业务类型（`AssetRef`/颜色等）插拔。

每阶段独立可交付、可回退；阶段 1 不动消费侧，阶段 2 才碰 `PropertyAccessor`，阶段 3 才碰 `PropertyWidgets` 派发核心。

---

## 7. 风险与缓解

| 风险 | 说明 | 缓解 |
| --- | --- | --- |
| 链接器裁剪自注册对象 | 方案 B 静态初始化器被 DCE | whole-archive / 强制引用 / 启动数量自检 |
| 静态初始化顺序 | 基础类型（GLM/容器）需先注册 | `AddReflectionRegistrar` 内惰性确保基础类型；或 registrar 排序 |
| entt 版本容器 traits 差异 | `std::array` 序列容器支持 | 迁移加单测；保留 fallback 一版观察 |
| JS 桥行为漂移 | 消费侧 B 依赖同一 meta | 阶段 1/2 不改 `PropertyAccessor` 对外签名；加 TS 生成快照对比 |
| Builder 抽象漏特性 | 现有 `.func`、只读、range 等需全覆盖 | 以「能 1:1 翻译现有所有注册」为验收门槛 |

---

## 8. 待讨论问题（细节对齐用）

1. **私有成员 vs 访问器**：是否愿意对部分简单字段开放「直接成员指针注册」（少写 Get/Set），还是统一保留访问器风格？这决定 Builder 的主推 API 形态。
2. **自注册的链接策略**：构建系统能否接受对组件静态库统一 whole-archive？还是更倾向保留一份显式 touch list（更可控但仍是清单）？
3. **枚举免列举**：是否接受引入 `magic_enum` 依赖换取 P6 的彻底消除？
4. **是否做阶段 3（方案 D）**：当前 `PropertyType` 封闭枚举是否已经够用？近期是否有自定义可编辑类型（AssetRef/颜色/曲线等）的需求驱动开放注册表？
5. **迁移节奏**：一次性迁全部 11 个组件，还是新组件用新写法、旧的随手改？
6. **元数据扩展**：`PropertyMeta` 未来是否要加 step/单位/枚举 UI 提示等字段？若是，Builder 的具名 setter 形态会比位置参数更扛得住扩展。

---

## 附录 A：关键源码索引

| 角色 | 文件:行号 |
| --- | --- |
| 组件声明宏 | `src/Engine/Runtime/Reflection/ReflectionMacros.hpp:5` |
| 元数据结构/预设 | `src/Engine/Runtime/Reflection/PropertyMeta.hpp:46` |
| 类型枚举/PropertyInfo | `src/Engine/Runtime/Reflection/PropertyTypes.h` |
| 属性访问/类型推导 | `src/Engine/Runtime/Reflection/PropertyAccessor.cpp:201`、`:30` |
| GLM/容器注册 | `src/Engine/Runtime/Reflection/GlmTypeSupport.hpp:10`、`:49` |
| 中心注册清单 | `src/Engine/Runtime/Reflection/ReflectionRegistry.cpp:29`；`src/Gameplay/Reflection/GameplayReflectionRegistry.cpp` |
| 注册样例（繁杂代表） | `src/Engine/Runtime/Components/RenderComponent.cpp:14`、`src/Gameplay/Components/AIAgentComponent.cpp` |
| 编辑器消费/派发 switch | `src/Application/Editor/gkNextEditor/Panels/PropertiesPanel.cpp:426`；`Panels/PropertyWidgets.cpp:146`、`:958` |
| 脚本消费（第二消费者） | `src/Modules/NextQuickJS/Reflection/QuickJSReflectionBridge.hpp:193` |
