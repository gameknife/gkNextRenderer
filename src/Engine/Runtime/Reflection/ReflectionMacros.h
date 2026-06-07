#pragma once
#include <entt/meta/factory.hpp>

// Macro to define component type name and meta type accessor
#define REFLECT_COMPONENT(ClassName)                                             \
    std::string_view GetTypeName() const override { return #ClassName; }         \
    entt::id_type GetTypeId() const override                                     \
    {                                                                            \
        return Assets::ComponentTypeId<ClassName>();                             \
    }                                                                            \
    entt::meta_type GetMetaType() const override                                 \
    {                                                                            \
        return entt::resolve<ClassName>();                                       \
    }                                                                            \
    static void RegisterReflection();

// Hash literal for string to entt::id_type conversion
using namespace entt::literals;

// Property name storage key
constexpr auto kPropertyNameKey = "prop_name"_hs;
constexpr auto kPropertyMetaKey = "prop_meta"_hs;

