# Reflection System Documentation

This document describes the unified property reflection system using **entt::meta** for gkNextRenderer.

## Overview

The reflection system provides:
1. **Editor PropertyPanel** - Auto-generate property editing UI
2. **QuickJS JavaScript bindings** - Auto-expose component properties to JS
3. **Undo/Redo system** - Property modification history with Ctrl+Z/Ctrl+Y

## Architecture

```
src/Runtime/Reflection/
├── PropertyTypes.h         # PropertyType enum and PropertyInfo struct
├── PropertyMeta.h          # PropertyMeta (displayName, category, flags)
├── PropertyAccessor.h/cpp  # Get/set properties via reflection
├── GlmTypeSupport.h        # GLM types + container types registration
├── ReflectionMacros.h      # REFLECT_COMPONENT macro
└── ReflectionRegistry.h/cpp # Central registration

src/Editor/Panels/
├── PropertyWidgets.h/cpp   # ImGui widgets based on PropertyType

src/Editor/Commands/
├── CommandHistory.h/cpp    # Undo/redo stack
└── PropertyCommand.h/cpp   # Property modification command
```

## Supported Property Types

| PropertyType | C++ Type | ImGui Widget |
|--------------|----------|--------------|
| Bool | `bool` | Checkbox |
| Int32 | `int32_t` | DragInt |
| UInt32 | `uint32_t` | DragInt (clamped) |
| Float | `float` | DragFloat |
| Double | `double` | DragFloat |
| String | `std::string` | InputText |
| Vec2 | `glm::vec2` | DragFloat2 |
| Vec3 | `glm::vec3` | DragFloat3 / ColorEdit3 |
| Vec4 | `glm::vec4` | DragFloat4 / ColorEdit4 |
| Quat | `glm::quat` | DragFloat3 (euler) |
| Enum | Any registered enum | Combo dropdown |
| Array | std::array/std::vector | TreeNode with elements |

## How to Add New Component Properties

### 1. Register the Component

In your component's `.cpp` file, add registration in an anonymous namespace:

```cpp
#include "Runtime/Reflection/ReflectionRegistry.h"
#include "Runtime/Reflection/ReflectionMacros.h"

namespace
{
    struct MyComponentReflection
    {
        MyComponentReflection()
        {
            REFLECT_COMPONENT(MyComponent)
                .data<&MyComponent::myProperty>("MyProperty"_hs)
                    .custom<Reflection::PropertyMeta>("My Property", "General")
                .data<&MyComponent::anotherProp>("AnotherProp"_hs)
                    .custom<Reflection::PropertyMeta>("Another Prop", "Settings");
        }
    };
    
    static MyComponentReflection registration;
}
```

### 2. Key Points

- Use `.data<&Class::member>("Name"_hs)` - the string literal sets both ID and name
- Add `.custom<Reflection::PropertyMeta>(displayName, category)` for UI metadata
- Registration happens automatically at static initialization time
- The component must inherit from `Assets::Component` and implement `GetMetaType()`

### 3. PropertyMeta Options

```cpp
PropertyMeta meta;
meta.displayName = "Display Name";  // Shown in UI
meta.category = "Category";         // Groups properties under headers
meta.flags = PropertyFlags::ReadOnly;  // Make read-only
meta.flags = PropertyFlags::Hidden;    // Hide from UI
```

## How to Add New Property Types

### 1. Add to PropertyType Enum

In `PropertyTypes.h`:
```cpp
enum class PropertyType
{
    // ... existing types ...
    NewType,
};
```

### 2. Register the Type in GlmTypeSupport.h

```cpp
inline void RegisterContainerTypes()
{
    entt::meta<NewCppType>()
        .type("NewCppType"_hs);
}
```

### 3. Update DeducePropertyType()

In `PropertyAccessor.cpp`:
```cpp
if (typeId == entt::resolve<NewCppType>().id())
    return PropertyType::NewType;
```

### 4. Add Widget Drawing

In `PropertyWidgets.cpp`:
```cpp
case PropertyType::NewType:
{
    if (auto* ptr = currentValue.try_cast<NewCppType>())
    {
        NewCppType val = *ptr;
        if (DrawNewType(label, val, isReadOnly))
        {
            changed = true;
            currentValue = entt::meta_any{val};
        }
    }
    break;
}
```

## Array/Container Support

### Supported Container Types

The system explicitly supports:
- `std::array<uint32_t, 16>` (for Materials)
- `std::vector<uint32_t>`
- `std::vector<int32_t>`
- `std::vector<float>`
- `std::vector<std::string>`

### Adding New Container Types

1. Register in `GlmTypeSupport.h`:
```cpp
entt::meta<std::vector<MyType>>()
    .type("std::vector<MyType>"_hs);
```

2. Add to container registry in `PropertyAccessor.cpp`:
```cpp
static const ContainerTypeInfo containerTypes[] = {
    // ... existing types ...
    { entt::resolve<std::vector<MyType>>().id(), PropertyType::MyType },
};
```

3. Add handling in `PropertyWidgets.cpp::DrawArray()`:
```cpp
if (auto* vec = arrayValue.try_cast<std::vector<MyType>>())
{
    return DrawContainerElements<std::vector<MyType>, MyType>(
        label, *vec, vec->size(), readOnly,
        [](const char* lbl, MyType& val, bool ro) {
            return DrawMyType(lbl, val, ro);
        });
}
```

## Enum Registration

### Registering an Enum

```cpp
entt::meta<EMyEnum>()
    .type("EMyEnum"_hs)
    .data<EMyEnum::Value1>("Value1"_hs)
    .data<EMyEnum::Value2>("Value2"_hs);
```

### Using in Components

```cpp
REFLECT_COMPONENT(MyComponent)
    .data<&MyComponent::myEnum>("MyEnum"_hs)
        .custom<Reflection::PropertyMeta>("My Enum", "Settings");
```

The enum will automatically render as a dropdown in the property panel.

## Undo/Redo System

### How It Works

1. `PropertyWidgets::DrawProperty()` captures old value before drawing
2. If value changes, creates a `PropertyCommand` with old and new values
3. `CommandHistory::Execute()` runs the command and pushes to undo stack
4. Ctrl+Z calls `CommandHistory::Undo()` to restore previous value
5. Ctrl+Y calls `CommandHistory::Redo()` to reapply the change

### Using in Editor

```cpp
// In your panel/editor code:
static CommandHistory history;

// Draw properties with undo support
PropertyWidgets::DrawComponentProperties(component, &history);

// Handle hotkeys
if (ImGui::IsKeyPressed(ImGuiKey_Z) && ImGui::GetIO().KeyCtrl)
    history.Undo();
if (ImGui::IsKeyPressed(ImGuiKey_Y) && ImGui::GetIO().KeyCtrl)
    history.Redo();
```

## Technical Notes

### entt v3.x API

- Use string literals `.data<>("Name")` to set both ID and name
- Access metadata via `.custom<T>()` (not `.prop()`)
- Use `meta_type::from_void(void*)` to wrap raw pointers

### std::array Limitation

`std::array` doesn't work automatically with `as_sequence_container()` - the system uses explicit `try_cast<>()` for known array types.

### Unity Build

The project uses unity builds. New `.cpp` files are auto-detected via `GLOB_RECURSE` in CMake.

## Example: Complete Component Registration

```cpp
// PhysicsComponent.cpp
#include "PhysicsComponent.h"
#include "Runtime/Reflection/ReflectionRegistry.h"
#include "Runtime/Reflection/ReflectionMacros.h"

namespace
{
    struct PhysicsComponentReflection
    {
        PhysicsComponentReflection()
        {
            // Register the enum first
            entt::meta<ENodeMobility>()
                .type("ENodeMobility"_hs)
                .data<ENodeMobility::Static>("Static"_hs)
                .data<ENodeMobility::Movable>("Movable"_hs);
            
            // Register the component
            REFLECT_COMPONENT(PhysicsComponent)
                .data<&PhysicsComponent::Mobility>("Mobility"_hs)
                    .custom<Reflection::PropertyMeta>("Mobility", "Physics")
                .data<&PhysicsComponent::PhysicsOffset>("PhysicsOffset"_hs)
                    .custom<Reflection::PropertyMeta>("Physics Offset", "Physics");
        }
    };
    
    static PhysicsComponentReflection registration;
}
```
