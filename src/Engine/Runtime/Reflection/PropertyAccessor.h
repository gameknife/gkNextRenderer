#pragma once
#include "Engine/Assets/AssetsFwd.hpp"
#include "PropertyTypes.h"
#include "PropertyMeta.h"
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <vector>
#include <string>
#include <utility>

namespace Reflection
{
    class PropertyAccessor
    {
    public:
        // Get all properties of a component type
        static std::vector<PropertyInfo> GetProperties(entt::meta_type type);
        
        // Get a property value from a component
        static entt::meta_any GetPropertyValue(entt::meta_type type, void* instance, const std::string& propName);
        
        // Set a property value on a component
        static bool SetPropertyValue(entt::meta_type type, void* instance, const std::string& propName, const entt::meta_any& value);
        
        // Deduce PropertyType from entt::meta_type
        static PropertyType DeducePropertyType(entt::meta_type type);
        
        // Get array element type info (returns {elementType, enumTypeId})
        static std::pair<PropertyType, uint32_t> GetArrayElementType(entt::meta_type containerType);
    };
}
