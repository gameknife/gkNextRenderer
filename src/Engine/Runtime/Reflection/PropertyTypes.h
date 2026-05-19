#pragma once
#include "PropertyMeta.h"
#include <string>
#include <cstdint>

namespace Reflection
{
    // Property type enumeration for UI widget selection
    enum class PropertyType
    {
        Unknown,
        Bool,
        Int32,
        UInt32,
        Float,
        Double,
        String,
        Vec2,
        Vec3,
        Vec4,
        Quat,
        Mat4,
        Enum,
        Array,
        AssetRef
    };

    // Property information at runtime
    struct PropertyInfo
    {
        std::string name;           // Property name (identifier)
        PropertyType type;          // Deduced property type
        PropertyMeta meta;          // Property metadata
        uint32_t propId;            // entt hash id for get/set operations
        uint32_t enumTypeId;        // For enum types, the type id for lookup (0 otherwise)
        PropertyType elementType;   // For array types, the type of elements
        uint32_t elementEnumTypeId; // For array of enums, the enum type id
        
        PropertyInfo() 
            : type(PropertyType::Unknown)
            , propId(0)
            , enumTypeId(0)
            , elementType(PropertyType::Unknown)
            , elementEnumTypeId(0) 
        {}
        
        PropertyInfo(const std::string& n, PropertyType t, const PropertyMeta& m, uint32_t id, uint32_t eTypeId = 0)
            : name(n), type(t), meta(m), propId(id), enumTypeId(eTypeId)
            , elementType(PropertyType::Unknown), elementEnumTypeId(0)
        {
        }
    };

    // Convert PropertyType to string for debugging
    inline const char* PropertyTypeToString(PropertyType type)
    {
        switch (type)
        {
            case PropertyType::Bool: return "Bool";
            case PropertyType::Int32: return "Int32";
            case PropertyType::UInt32: return "UInt32";
            case PropertyType::Float: return "Float";
            case PropertyType::Double: return "Double";
            case PropertyType::String: return "String";
            case PropertyType::Vec2: return "Vec2";
            case PropertyType::Vec3: return "Vec3";
            case PropertyType::Vec4: return "Vec4";
            case PropertyType::Quat: return "Quat";
            case PropertyType::Mat4: return "Mat4";
            case PropertyType::Enum: return "Enum";
            case PropertyType::Array: return "Array";
            case PropertyType::AssetRef: return "AssetRef";
            default: return "Unknown";
        }
    }
}
