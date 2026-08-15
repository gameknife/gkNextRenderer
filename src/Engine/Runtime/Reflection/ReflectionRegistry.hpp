#pragma once
#include <entt/meta/meta.hpp>
#include <string>
#include <vector>

namespace Reflection
{
    // Register all reflection metadata
    // Must be called once at engine initialization
    void RegisterAllReflection();

    // Check if reflection has been initialized
    bool IsReflectionInitialized();

    /// One type that RegisterAllReflection registered, in registration order.
    struct FReflectedType
    {
        std::string name;       // The name the meta type was registered under, e.g. "RenderComponent"
        entt::meta_type meta;
        bool isComponent;       // false for Assets::Node, which is a node rather than a component
    };

    /// The registered types, for consumers that need to walk the whole reflection surface rather
    /// than resolve one type they already know: the editor's type pickers and the
    /// `--dump-reflection` manifest that generates the C# component wrappers.
    ///
    /// This list is produced by RegisterAllReflection itself, so it cannot drift from what was
    /// actually registered — a second hand-maintained list is how the binding surface and the
    /// reflection surface came apart the last time (design 4.4).
    const std::vector<FReflectedType>& GetReflectedTypes();
}
