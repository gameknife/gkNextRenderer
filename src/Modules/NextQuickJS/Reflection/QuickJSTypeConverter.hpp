#pragma once

#if WITH_QUICKJS
#include "Engine/Runtime/Reflection/PropertyTypes.h"
#include <entt/meta/meta.hpp>
#include <ThirdParty/quickjs-ng/quickjspp.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace Reflection
{
    class QuickJSTypeConverter
    {
    public:
        // Convert entt::meta_any to JSValue
        static JSValue ToJSValue(JSContext* ctx, const entt::meta_any& value);
        
        // Convert JSValue to entt::meta_any of the specified type
        static entt::meta_any FromJSValue(JSContext* ctx, JSValue jsValue, entt::meta_type targetType);
        
        // Convert PropertyType to TypeScript type string
        static std::string ToTypeScriptType(PropertyType type);
        
        // Check if a type is supported for JS conversion
        static bool IsTypeSupported(entt::meta_type type);
        
    private:
        // Helper functions for specific types
        static JSValue BoolToJS(JSContext* ctx, bool value);
        static JSValue IntToJS(JSContext* ctx, int32_t value);
        static JSValue UIntToJS(JSContext* ctx, uint32_t value);
        static JSValue FloatToJS(JSContext* ctx, float value);
        static JSValue DoubleToJS(JSContext* ctx, double value);
        static JSValue StringToJS(JSContext* ctx, const std::string& value);
        static JSValue Vec2ToJS(JSContext* ctx, const glm::vec2& value);
        static JSValue Vec3ToJS(JSContext* ctx, const glm::vec3& value);
        static JSValue Vec4ToJS(JSContext* ctx, const glm::vec4& value);
        static JSValue QuatToJS(JSContext* ctx, const glm::quat& value);
        static JSValue EnumToJS(JSContext* ctx, const entt::meta_any& value, entt::meta_type type);
        
        static bool JSTooBool(JSContext* ctx, JSValue value);
        static int32_t JSToInt(JSContext* ctx, JSValue value);
        static uint32_t JSToUInt(JSContext* ctx, JSValue value);
        static float JSToFloat(JSContext* ctx, JSValue value);
        static double JSToDouble(JSContext* ctx, JSValue value);
        static std::string JSToString(JSContext* ctx, JSValue value);
        static glm::vec2 JSToVec2(JSContext* ctx, JSValue value);
        static glm::vec3 JSToVec3(JSContext* ctx, JSValue value);
        static glm::vec4 JSToVec4(JSContext* ctx, JSValue value);
        static glm::quat JSToQuat(JSContext* ctx, JSValue value);
        static entt::meta_any JSToEnum(JSContext* ctx, JSValue value, entt::meta_type type);
    };
}
#endif // WITH_QUICKJS
