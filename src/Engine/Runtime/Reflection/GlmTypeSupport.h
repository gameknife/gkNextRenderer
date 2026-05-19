#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/meta/factory.hpp>
#include <entt/core/hashed_string.hpp>
#include <array>
#include <vector>

namespace Reflection
{
    inline void RegisterGlmTypes()
    {
        using namespace entt::literals;
        
        // Register glm::vec2
        entt::meta_factory<glm::vec2>()
            .type("vec2")
            .data<&glm::vec2::x>("x")
            .data<&glm::vec2::y>("y");
        
        // Register glm::vec3
        entt::meta_factory<glm::vec3>()
            .type("vec3")
            .data<&glm::vec3::x>("x")
            .data<&glm::vec3::y>("y")
            .data<&glm::vec3::z>("z");
        
        // Register glm::vec4
        entt::meta_factory<glm::vec4>()
            .type("vec4")
            .data<&glm::vec4::x>("x")
            .data<&glm::vec4::y>("y")
            .data<&glm::vec4::z>("z")
            .data<&glm::vec4::w>("w");
        
        // Register glm::quat
        entt::meta_factory<glm::quat>()
            .type("quat")
            .data<&glm::quat::x>("x")
            .data<&glm::quat::y>("y")
            .data<&glm::quat::z>("z")
            .data<&glm::quat::w>("w");
        
        // Register glm::mat4 (only type for simplicity)
        entt::meta_factory<glm::mat4>()
            .type("mat4");
    }
    
    // Register common container types for sequence container reflection
    inline void RegisterContainerTypes()
    {
        using namespace entt::literals;
        
        // Register std::array<uint32_t, 16> for RenderComponent::Materials
        entt::meta_factory<std::array<uint32_t, 16>>()
            .type("array_uint32_16");
        
        // Register common vector types that might be used
        entt::meta_factory<std::vector<uint32_t>>()
            .type("vector_uint32");
        
        entt::meta_factory<std::vector<int32_t>>()
            .type("vector_int32");
        
        entt::meta_factory<std::vector<float>>()
            .type("vector_float");
        
        entt::meta_factory<std::vector<std::string>>()
            .type("vector_string");
    }
}
