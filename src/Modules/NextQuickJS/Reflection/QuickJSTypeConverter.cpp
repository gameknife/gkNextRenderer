#include "Modules/NextQuickJS/Reflection/QuickJSTypeConverter.hpp"

#if WITH_QUICKJS
#include "Engine/Runtime/Reflection/PropertyAccessor.h"
#include "Engine/Runtime/Reflection/ReflectionMacros.h"
#include <entt/meta/resolve.hpp>

namespace Reflection
{
    using namespace entt::literals;

    JSValue QuickJSTypeConverter::ToJSValue(JSContext* ctx, const entt::meta_any& value)
    {
        if (!value)
        {
            return JS_UNDEFINED;
        }
        
        auto type = value.type();
        auto typeId = type.id();
        
        // Basic types
        if (typeId == entt::resolve<bool>().id())
        {
            return BoolToJS(ctx, value.cast<bool>());
        }
        if (typeId == entt::resolve<int32_t>().id())
        {
            return IntToJS(ctx, value.cast<int32_t>());
        }
        if (typeId == entt::resolve<uint32_t>().id())
        {
            return UIntToJS(ctx, value.cast<uint32_t>());
        }
        if (typeId == entt::resolve<float>().id())
        {
            return FloatToJS(ctx, value.cast<float>());
        }
        if (typeId == entt::resolve<double>().id())
        {
            return DoubleToJS(ctx, value.cast<double>());
        }
        if (typeId == entt::resolve<std::string>().id())
        {
            return StringToJS(ctx, value.cast<std::string>());
        }
        
        // GLM types
        if (typeId == entt::resolve<glm::vec2>().id())
        {
            return Vec2ToJS(ctx, value.cast<glm::vec2>());
        }
        if (typeId == entt::resolve<glm::vec3>().id())
        {
            return Vec3ToJS(ctx, value.cast<glm::vec3>());
        }
        if (typeId == entt::resolve<glm::vec4>().id())
        {
            return Vec4ToJS(ctx, value.cast<glm::vec4>());
        }
        if (typeId == entt::resolve<glm::quat>().id())
        {
            return QuatToJS(ctx, value.cast<glm::quat>());
        }
        
        // Enum types
        if (type.is_enum())
        {
            return EnumToJS(ctx, value, type);
        }
        
        return JS_UNDEFINED;
    }

    entt::meta_any QuickJSTypeConverter::FromJSValue(JSContext* ctx, JSValue jsValue, entt::meta_type targetType)
    {
        auto typeId = targetType.id();
        
        // Basic types
        if (typeId == entt::resolve<bool>().id())
        {
            return entt::meta_any{JSTooBool(ctx, jsValue)};
        }
        if (typeId == entt::resolve<int32_t>().id())
        {
            return entt::meta_any{JSToInt(ctx, jsValue)};
        }
        if (typeId == entt::resolve<uint32_t>().id())
        {
            return entt::meta_any{JSToUInt(ctx, jsValue)};
        }
        if (typeId == entt::resolve<float>().id())
        {
            return entt::meta_any{JSToFloat(ctx, jsValue)};
        }
        if (typeId == entt::resolve<double>().id())
        {
            return entt::meta_any{JSToDouble(ctx, jsValue)};
        }
        if (typeId == entt::resolve<std::string>().id())
        {
            return entt::meta_any{JSToString(ctx, jsValue)};
        }
        
        // GLM types
        if (typeId == entt::resolve<glm::vec2>().id())
        {
            return entt::meta_any{JSToVec2(ctx, jsValue)};
        }
        if (typeId == entt::resolve<glm::vec3>().id())
        {
            return entt::meta_any{JSToVec3(ctx, jsValue)};
        }
        if (typeId == entt::resolve<glm::vec4>().id())
        {
            return entt::meta_any{JSToVec4(ctx, jsValue)};
        }
        if (typeId == entt::resolve<glm::quat>().id())
        {
            return entt::meta_any{JSToQuat(ctx, jsValue)};
        }
        
        // Enum types
        if (targetType.is_enum())
        {
            return JSToEnum(ctx, jsValue, targetType);
        }
        
        return {};
    }

    std::string QuickJSTypeConverter::ToTypeScriptType(PropertyType type)
    {
        switch (type)
        {
            case PropertyType::Bool: return "boolean";
            case PropertyType::Int32:
            case PropertyType::UInt32:
            case PropertyType::Float:
            case PropertyType::Double: return "number";
            case PropertyType::String: return "string";
            case PropertyType::Vec2: return "Vec2";
            case PropertyType::Vec3: return "Vec3";
            case PropertyType::Vec4: return "Vec4";
            case PropertyType::Quat: return "Quat";
            case PropertyType::Mat4: return "Mat4";
            case PropertyType::Enum: return "string";  // Enums are strings in TS
            case PropertyType::Array: return "number[]";  // Simplified
            default: return "any";
        }
    }

    bool QuickJSTypeConverter::IsTypeSupported(entt::meta_type type)
    {
        auto typeId = type.id();
        
        return typeId == entt::resolve<bool>().id() ||
               typeId == entt::resolve<int32_t>().id() ||
               typeId == entt::resolve<uint32_t>().id() ||
               typeId == entt::resolve<float>().id() ||
               typeId == entt::resolve<double>().id() ||
               typeId == entt::resolve<std::string>().id() ||
               typeId == entt::resolve<glm::vec2>().id() ||
               typeId == entt::resolve<glm::vec3>().id() ||
               typeId == entt::resolve<glm::vec4>().id() ||
               typeId == entt::resolve<glm::quat>().id() ||
               type.is_enum();
    }

    // Helper implementations
    JSValue QuickJSTypeConverter::BoolToJS(JSContext* ctx, bool value)
    {
        return JS_NewBool(ctx, value);
    }

    JSValue QuickJSTypeConverter::IntToJS(JSContext* ctx, int32_t value)
    {
        return JS_NewInt32(ctx, value);
    }

    JSValue QuickJSTypeConverter::UIntToJS(JSContext* ctx, uint32_t value)
    {
        return JS_NewUint32(ctx, value);
    }

    JSValue QuickJSTypeConverter::FloatToJS(JSContext* ctx, float value)
    {
        return JS_NewFloat64(ctx, static_cast<double>(value));
    }

    JSValue QuickJSTypeConverter::DoubleToJS(JSContext* ctx, double value)
    {
        return JS_NewFloat64(ctx, value);
    }

    JSValue QuickJSTypeConverter::StringToJS(JSContext* ctx, const std::string& value)
    {
        return JS_NewStringLen(ctx, value.c_str(), value.length());
    }

    JSValue QuickJSTypeConverter::Vec2ToJS(JSContext* ctx, const glm::vec2& value)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, value.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, value.y));
        return obj;
    }

    JSValue QuickJSTypeConverter::Vec3ToJS(JSContext* ctx, const glm::vec3& value)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, value.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, value.y));
        JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, value.z));
        return obj;
    }

    JSValue QuickJSTypeConverter::Vec4ToJS(JSContext* ctx, const glm::vec4& value)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, value.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, value.y));
        JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, value.z));
        JS_SetPropertyStr(ctx, obj, "w", JS_NewFloat64(ctx, value.w));
        return obj;
    }

    JSValue QuickJSTypeConverter::QuatToJS(JSContext* ctx, const glm::quat& value)
    {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, value.x));
        JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, value.y));
        JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, value.z));
        JS_SetPropertyStr(ctx, obj, "w", JS_NewFloat64(ctx, value.w));
        return obj;
    }

    JSValue QuickJSTypeConverter::EnumToJS(JSContext* ctx, const entt::meta_any& value, entt::meta_type type)
    {
        // Try to find the enum value name
        for (auto&& [id, data] : type.data())
        {
            auto enumVal = data.get({});
            if (enumVal == value)
            {
                // Use data.name() instead of .prop() which doesn't exist in entt v3.x
                const char* name = data.name();
                if (name)
                {
                    return JS_NewString(ctx, name);
                }
            }
        }
        return JS_UNDEFINED;
    }

    bool QuickJSTypeConverter::JSTooBool(JSContext* ctx, JSValue value)
    {
        return JS_ToBool(ctx, value) != 0;
    }

    int32_t QuickJSTypeConverter::JSToInt(JSContext* ctx, JSValue value)
    {
        int32_t result = 0;
        JS_ToInt32(ctx, &result, value);
        return result;
    }

    uint32_t QuickJSTypeConverter::JSToUInt(JSContext* ctx, JSValue value)
    {
        uint32_t result = 0;
        JS_ToUint32(ctx, &result, value);
        return result;
    }

    float QuickJSTypeConverter::JSToFloat(JSContext* ctx, JSValue value)
    {
        double result = 0;
        JS_ToFloat64(ctx, &result, value);
        return static_cast<float>(result);
    }

    double QuickJSTypeConverter::JSToDouble(JSContext* ctx, JSValue value)
    {
        double result = 0;
        JS_ToFloat64(ctx, &result, value);
        return result;
    }

    std::string QuickJSTypeConverter::JSToString(JSContext* ctx, JSValue value)
    {
        const char* str = JS_ToCString(ctx, value);
        if (str)
        {
            std::string result(str);
            JS_FreeCString(ctx, str);
            return result;
        }
        return {};
    }

    glm::vec2 QuickJSTypeConverter::JSToVec2(JSContext* ctx, JSValue value)
    {
        glm::vec2 result(0.0f);
        JSValue x = JS_GetPropertyStr(ctx, value, "x");
        JSValue y = JS_GetPropertyStr(ctx, value, "y");
        JS_ToFloat64(ctx, reinterpret_cast<double*>(&result.x), x);
        JS_ToFloat64(ctx, reinterpret_cast<double*>(&result.y), y);
        // Convert from double
        double dx, dy;
        JS_ToFloat64(ctx, &dx, x);
        JS_ToFloat64(ctx, &dy, y);
        result.x = static_cast<float>(dx);
        result.y = static_cast<float>(dy);
        JS_FreeValue(ctx, x);
        JS_FreeValue(ctx, y);
        return result;
    }

    glm::vec3 QuickJSTypeConverter::JSToVec3(JSContext* ctx, JSValue value)
    {
        glm::vec3 result(0.0f);
        JSValue x = JS_GetPropertyStr(ctx, value, "x");
        JSValue y = JS_GetPropertyStr(ctx, value, "y");
        JSValue z = JS_GetPropertyStr(ctx, value, "z");
        double dx, dy, dz;
        JS_ToFloat64(ctx, &dx, x);
        JS_ToFloat64(ctx, &dy, y);
        JS_ToFloat64(ctx, &dz, z);
        result.x = static_cast<float>(dx);
        result.y = static_cast<float>(dy);
        result.z = static_cast<float>(dz);
        JS_FreeValue(ctx, x);
        JS_FreeValue(ctx, y);
        JS_FreeValue(ctx, z);
        return result;
    }

    glm::vec4 QuickJSTypeConverter::JSToVec4(JSContext* ctx, JSValue value)
    {
        glm::vec4 result(0.0f);
        JSValue x = JS_GetPropertyStr(ctx, value, "x");
        JSValue y = JS_GetPropertyStr(ctx, value, "y");
        JSValue z = JS_GetPropertyStr(ctx, value, "z");
        JSValue w = JS_GetPropertyStr(ctx, value, "w");
        double dx, dy, dz, dw;
        JS_ToFloat64(ctx, &dx, x);
        JS_ToFloat64(ctx, &dy, y);
        JS_ToFloat64(ctx, &dz, z);
        JS_ToFloat64(ctx, &dw, w);
        result.x = static_cast<float>(dx);
        result.y = static_cast<float>(dy);
        result.z = static_cast<float>(dz);
        result.w = static_cast<float>(dw);
        JS_FreeValue(ctx, x);
        JS_FreeValue(ctx, y);
        JS_FreeValue(ctx, z);
        JS_FreeValue(ctx, w);
        return result;
    }

    glm::quat QuickJSTypeConverter::JSToQuat(JSContext* ctx, JSValue value)
    {
        glm::quat result(1.0f, 0.0f, 0.0f, 0.0f);  // w, x, y, z
        JSValue x = JS_GetPropertyStr(ctx, value, "x");
        JSValue y = JS_GetPropertyStr(ctx, value, "y");
        JSValue z = JS_GetPropertyStr(ctx, value, "z");
        JSValue w = JS_GetPropertyStr(ctx, value, "w");
        double dx, dy, dz, dw;
        JS_ToFloat64(ctx, &dx, x);
        JS_ToFloat64(ctx, &dy, y);
        JS_ToFloat64(ctx, &dz, z);
        JS_ToFloat64(ctx, &dw, w);
        result.x = static_cast<float>(dx);
        result.y = static_cast<float>(dy);
        result.z = static_cast<float>(dz);
        result.w = static_cast<float>(dw);
        JS_FreeValue(ctx, x);
        JS_FreeValue(ctx, y);
        JS_FreeValue(ctx, z);
        JS_FreeValue(ctx, w);
        return result;
    }

    entt::meta_any QuickJSTypeConverter::JSToEnum(JSContext* ctx, JSValue value, entt::meta_type type)
    {
        std::string enumName = JSToString(ctx, value);
        
        // Find matching enum value by name
        for (auto&& [id, data] : type.data())
        {
            // Use data.name() instead of .prop() which doesn't exist in entt v3.x
            const char* name = data.name();
            if (name && enumName == name)
            {
                return data.get({});
            }
        }
        return {};
    }
}
#endif // WITH_QUICKJS
