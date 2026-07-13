#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "Engine/Runtime/Reflection/PropertyMeta.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <vector>
#include <entt/core/hashed_string.hpp>
#include <spdlog/spdlog.h>

namespace Reflection
{
    using namespace entt::literals;
    
    // ============================================================================
    // Container Type Registry
    // ============================================================================
    
    // Centralized container type information to avoid duplication
    // between DeducePropertyType() and GetArrayElementType()
    struct ContainerTypeInfo
    {
        entt::id_type typeId;
        PropertyType elementType;
    };
    
    // Returns container type info if typeId matches a known container type
    // Returns nullptr if not a known container type
    static const ContainerTypeInfo* FindContainerTypeInfo(entt::id_type typeId)
    {
        // Static registry of known container types
        static const ContainerTypeInfo containerTypes[] = {
            { entt::resolve<std::array<uint32_t, 16>>().id(), PropertyType::UInt32 },
            { entt::resolve<std::vector<uint32_t>>().id(),    PropertyType::UInt32 },
            { entt::resolve<std::vector<int32_t>>().id(),     PropertyType::Int32 },
            { entt::resolve<std::vector<float>>().id(),       PropertyType::Float },
            { entt::resolve<std::vector<std::string>>().id(), PropertyType::String },
        };
        
        for (const auto& info : containerTypes)
        {
            if (info.typeId == typeId)
            {
                return &info;
            }
        }
        return nullptr;
    }
    
    // ============================================================================
    // PropertyAccessor Implementation
    // ============================================================================
    
    std::vector<PropertyInfo> PropertyAccessor::GetProperties(entt::meta_type type)
    {
        std::vector<PropertyInfo> result;
        
        if (!type)
        {
            return result;
        }
        
        // Iterate over all data members registered with entt::meta
        for (auto&& [id, data] : type.data())
        {
            PropertyInfo info;
            info.propId = id;
            
            // Get property name
            const char* name = data.name();
            info.name = name ? name : std::to_string(id);
            
            // Get property type
            auto propType = data.type();
            info.type = DeducePropertyType(propType);
            
            // Get custom metadata if available
            auto custom = data.custom();
            if (PropertyMeta* meta = custom)
            {
                info.meta = *meta;
            }
            else
            {
                // Default metadata
                info.meta.displayName = info.name;
                info.meta.category = "General";
            }
            
            // Check for enum type
            if (info.type == PropertyType::Enum)
            {
                info.enumTypeId = propType.id();
            }
            
            // Check for array type - get element type info
            if (info.type == PropertyType::Array && propType.is_sequence_container())
            {
                // Create a temporary to get the value_type
                // We need to check the template argument type
                // For now, use a workaround by checking common element types
                auto valueType = GetArrayElementType(propType);
                info.elementType = valueType.first;
                info.elementEnumTypeId = valueType.second;
            }
            
            // Set read-only based on const-ness
            if (data.is_const())
            {
                info.meta.flags |= PropertyFlags::ReadOnly;
            }
            
            result.push_back(info);
        }
        
        return result;
    }
    
    entt::meta_any PropertyAccessor::GetPropertyValue(entt::meta_type type, void* instance, const std::string& propName)
    {
        if (!instance || !type)
        {
            return {};
        }
        
        // Wrap the void* instance using meta_type::from_void
        entt::meta_any instanceAny = type.from_void(instance);
        if (!instanceAny)
        {
            spdlog::warn("PropertyAccessor::GetPropertyValue: failed to wrap instance");
            return {};
        }
        
        // Find property by name - iterate through data and check names
        for (auto&& [id, data] : type.data())
        {
            const char* name = data.name();
            if (name && propName == name)
            {
                return data.get(instanceAny);
            }
        }
        
        // Try by hashed name
        auto hashedName = entt::hashed_string::value(propName.c_str());
        auto data = type.data(hashedName);
        if (data)
        {
            return data.get(instanceAny);
        }
        
        spdlog::warn("PropertyAccessor::GetPropertyValue: property '{}' not found", propName);
        return {};
    }
    
    bool PropertyAccessor::SetPropertyValue(entt::meta_type type, void* instance, const std::string& propName, const entt::meta_any& value)
    {
        if (!instance || !type || !value)
        {
            return false;
        }
        
        // Wrap the void* instance using meta_type::from_void
        entt::meta_any instanceAny = type.from_void(instance);
        if (!instanceAny)
        {
            spdlog::warn("PropertyAccessor::SetPropertyValue: failed to wrap instance");
            return false;
        }
        
        // Find property by name - iterate through data and check names
        for (auto&& [id, data] : type.data())
        {
            const char* name = data.name();
            if (name && propName == name)
            {
                if (data.is_const())
                {
                    spdlog::warn("PropertyAccessor::SetPropertyValue: property '{}' is read-only", propName);
                    return false;
                }
                return data.set(instanceAny, value);
            }
        }
        
        // Try by hashed name
        auto hashedName = entt::hashed_string::value(propName.c_str());
        auto data = type.data(hashedName);
        if (data)
        {
            if (data.is_const())
            {
                spdlog::warn("PropertyAccessor::SetPropertyValue: property '{}' is read-only", propName);
                return false;
            }
            return data.set(instanceAny, value);
        }
        
        spdlog::warn("PropertyAccessor::SetPropertyValue: property '{}' not found", propName);
        return false;
    }

    PropertyType PropertyAccessor::DeducePropertyType(entt::meta_type type)
    {
        auto typeId = type.id();
        
        // Check basic types
        if (typeId == entt::resolve<bool>().id())
            return PropertyType::Bool;
        if (typeId == entt::resolve<int32_t>().id())
            return PropertyType::Int32;
        if (typeId == entt::resolve<uint32_t>().id())
            return PropertyType::UInt32;
        if (typeId == entt::resolve<float>().id())
            return PropertyType::Float;
        if (typeId == entt::resolve<double>().id())
            return PropertyType::Double;
        if (typeId == entt::resolve<std::string>().id())
            return PropertyType::String;
        
        // Check GLM types
        if (typeId == entt::resolve<glm::vec2>().id())
            return PropertyType::Vec2;
        if (typeId == entt::resolve<glm::vec3>().id())
            return PropertyType::Vec3;
        if (typeId == entt::resolve<glm::vec4>().id())
            return PropertyType::Vec4;
        if (typeId == entt::resolve<glm::quat>().id())
            return PropertyType::Quat;
        if (typeId == entt::resolve<glm::mat4>().id())
            return PropertyType::Mat4;
        
        // Check if it's an enum
        if (type.is_enum())
            return PropertyType::Enum;
        
        // Check for known container types using centralized registry
        if (FindContainerTypeInfo(typeId) != nullptr)
            return PropertyType::Array;
        
        // Fallback: check if it's a sequence container (for any other registered types)
        if (type.is_sequence_container())
            return PropertyType::Array;
        
        return PropertyType::Unknown;
    }
    
    std::pair<PropertyType, uint32_t> PropertyAccessor::GetArrayElementType(entt::meta_type containerType)
    {
        auto typeId = containerType.id();
        
        // Check known container types using centralized registry
        if (const auto* info = FindContainerTypeInfo(typeId))
        {
            return {info->elementType, 0};
        }
        
        // Fallback: try using entt's sequence container API
        if (containerType.is_sequence_container())
        {
            auto defaultValue = containerType.construct();
            if (defaultValue)
            {
                auto container = defaultValue.as_sequence_container();
                if (container)
                {
                    auto valueType = container.value_type();
                    if (valueType)
                    {
                        PropertyType elemType = DeducePropertyType(valueType);
                        uint32_t enumId = 0;
                        if (elemType == PropertyType::Enum)
                        {
                            enumId = valueType.id();
                        }
                        return {elemType, enumId};
                    }
                }
            }
        }
        
        spdlog::warn("GetArrayElementType: unknown container type");
        return {PropertyType::Unknown, 0};
    }
}
