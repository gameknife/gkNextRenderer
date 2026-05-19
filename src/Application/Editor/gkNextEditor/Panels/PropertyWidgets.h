#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Reflection/PropertyTypes.h"
#include "Engine/Runtime/Reflection/PropertyAccessor.h"
#include "Engine/Runtime/Command/CommandHistory.hpp"
#include "Engine/Assets/Core/Component.h"

#include <imgui.h>
#include <string>
#include <functional>
#include <entt/meta/meta.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cfloat>
#include <climits>

namespace Editor
{
    /**
     * Utility class for rendering property editor widgets based on reflection.
     * Automatically selects appropriate ImGui widgets based on property type.
     */
    class PropertyWidgets
    {
    public:
        /**
         * Configuration for property widget rendering.
         */
        struct WidgetConfig
        {
            float dragSpeed;
            float minValue;
            float maxValue;
            bool readOnly;
            const char* format;
            
            WidgetConfig()
                : dragSpeed(0.1f)
                , minValue(-FLT_MAX)
                , maxValue(FLT_MAX)
                , readOnly(false)
                , format(nullptr)
            {}
        };
        
        /**
         * Draw an appropriate widget for a property info.
         * Returns true if the value was changed.
         */
        static bool DrawProperty(
            const Reflection::PropertyInfo& propInfo,
            Assets::Component* component,
            Runtime::Command::CommandHistory* history = nullptr,
            WidgetConfig config = WidgetConfig(),
            const entt::meta_any* defaultInstance = nullptr
        );
        
        /**
         * Draw all properties of a component using reflection.
         * Returns true if any value was changed.
         * Pass an optional filter to show only matching properties.
         */
        static bool DrawComponentProperties(
            Assets::Component* component,
            Runtime::Command::CommandHistory* history = nullptr,
            WidgetConfig config = WidgetConfig(),
            ImGuiTextFilter* filter = nullptr
        );
        
        /**
         * Draw a widget for a specific value type.
         * These can be used directly for non-reflected properties.
         */
        static bool DrawBool(const char* label, bool& value, bool readOnly = false);
        static bool DrawInt(const char* label, int32_t& value, float speed = 0.1f, int min = INT_MIN, int max = INT_MAX, bool readOnly = false);
        static bool DrawUInt(const char* label, uint32_t& value, float speed = 0.1f, uint32_t min = 0, uint32_t max = UINT_MAX, bool readOnly = false);
        static bool DrawFloat(const char* label, float& value, float speed = 0.1f, float min = -FLT_MAX, float max = FLT_MAX, bool readOnly = false, const char* format = "%.3f");
        static bool DrawDouble(const char* label, double& value, float speed = 0.1f, double min = -DBL_MAX, double max = DBL_MAX, bool readOnly = false, const char* format = "%.6f");
        static bool DrawString(const char* label, std::string& value, bool readOnly = false);
        static bool DrawVec2(const char* label, glm::vec2& value, float speed = 0.1f, bool readOnly = false);
        static bool DrawVec3(const char* label, glm::vec3& value, float speed = 0.1f, bool readOnly = false);
        static bool DrawVec4(const char* label, glm::vec4& value, float speed = 0.1f, bool readOnly = false);
        static bool DrawQuat(const char* label, glm::quat& value, float speed = 0.1f, bool readOnly = false);
        static bool DrawColor3(const char* label, glm::vec3& value, bool readOnly = false);
        static bool DrawColor4(const char* label, glm::vec4& value, bool readOnly = false);
        
        /**
         * Draw an enum dropdown using reflection.
         */
        static bool DrawEnum(
            const char* label,
            entt::meta_any& value,
            entt::meta_type enumType,
            bool readOnly = false
        );
        
        /**
         * Draw an array/sequence container property.
         * Returns true if any element was changed.
         */
        static bool DrawArray(
            const char* label,
            const Reflection::PropertyInfo& propInfo,
            entt::meta_any& arrayValue,
            bool readOnly = false
        );
        
    private:
        // Internal helper to get enum value names
        static std::vector<std::pair<std::string, entt::meta_any>> GetEnumValues(entt::meta_type enumType);
        
        // Internal helper to draw a single array element
        static bool DrawArrayElement(
            size_t index,
            entt::meta_any& element,
            Reflection::PropertyType elementType,
            uint32_t enumTypeId,
            bool readOnly
        );
    };
}
