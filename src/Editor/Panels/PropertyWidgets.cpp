#include "PropertyWidgets.h"
#include "Runtime/Command/PropertyCommand.hpp"
#include "Runtime/Reflection/ReflectionMacros.h"

#include <imgui_stdlib.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>
#include <map>
#include <array>
#include <vector>

namespace Editor
{
    using namespace Reflection;
    
    // ============================================================================
    // UI Helper functions for consistent property panel styling
    // ============================================================================
    
    // Begin a property row with label on left (50:50 ratio)
    static void BeginPropertyRow(const char* label)
    {
        float labelWidth = ImGui::GetContentRegionAvail().x * 0.5f; // 50% for label
        
        // Draw label
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        
        // Set width for the value widget
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    }
    
    // Wrapper for property drawing with label-value layout
    static bool DrawPropertyRow(const char* label, const std::function<bool()>& drawWidget)
    {
        BeginPropertyRow(label);
        ImGui::PushID(label);
        bool result = drawWidget();
        ImGui::PopID();
        return result;
    }
    
    // Draw a category header with distinctive style
    static bool DrawCategoryHeader(const char* category)
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        
        bool open = ImGui::CollapsingHeader(category, ImGuiTreeNodeFlags_DefaultOpen);
        
        ImGui::PopStyleColor(3);
        
        return open;
    }
    
    // ============================================================================
    // Main property drawing
    // ============================================================================
    
    bool PropertyWidgets::DrawProperty(
        const PropertyInfo& propInfo,
        Assets::Component* component,
        CommandHistory* history,
        WidgetConfig config
    )
    {
        if (!component)
        {
            return false;
        }
        
        bool changed = false;
        bool isReadOnly = config.readOnly || propInfo.meta.IsReadOnly();
        
        // Get current value
        auto metaType = component->GetMetaType();
        auto currentValue = PropertyAccessor::GetPropertyValue(metaType, component, propInfo.name);
        
        if (!currentValue)
        {
            ImGui::Text("%s: <unable to read>", propInfo.name.c_str());
            return false;
        }
        
        const char* label = propInfo.meta.displayName.empty() 
            ? propInfo.name.c_str() 
            : propInfo.meta.displayName.c_str();
        
        // Store old value for undo
        entt::meta_any oldValue = currentValue;
        
        switch (propInfo.type)
        {
            case PropertyType::Bool:
            {
                if (auto* ptr = currentValue.try_cast<bool>())
                {
                    bool val = *ptr;
                    if (DrawBool(label, val, isReadOnly))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::Int32:
            {
                if (auto* ptr = currentValue.try_cast<int32_t>())
                {
                    int32_t val = *ptr;
                    if (DrawInt(label, val, config.dragSpeed, 
                               static_cast<int>(config.minValue), 
                               static_cast<int>(config.maxValue), 
                               isReadOnly))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::UInt32:
            {
                if (auto* ptr = currentValue.try_cast<uint32_t>())
                {
                    uint32_t val = *ptr;
                    if (DrawUInt(label, val, config.dragSpeed,
                                static_cast<uint32_t>(std::max(0.0f, config.minValue)),
                                static_cast<uint32_t>(config.maxValue),
                                isReadOnly))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::Float:
            {
                if (auto* ptr = currentValue.try_cast<float>())
                {
                    float val = *ptr;
                    if (DrawFloat(label, val, config.dragSpeed, config.minValue, config.maxValue, 
                                 isReadOnly, config.format ? config.format : "%.3f"))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::Double:
            {
                if (auto* ptr = currentValue.try_cast<double>())
                {
                    double val = *ptr;
                    if (DrawDouble(label, val, config.dragSpeed,
                                  static_cast<double>(config.minValue),
                                  static_cast<double>(config.maxValue),
                                  isReadOnly, config.format ? config.format : "%.6f"))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::String:
            {
                if (auto* ptr = currentValue.try_cast<std::string>())
                {
                    std::string val = *ptr;
                    if (DrawString(label, val, isReadOnly))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::Vec2:
            {
                if (auto* ptr = currentValue.try_cast<glm::vec2>())
                {
                    glm::vec2 val = *ptr;
                    if (DrawVec2(label, val, config.dragSpeed, isReadOnly))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::Vec3:
            {
                if (auto* ptr = currentValue.try_cast<glm::vec3>())
                {
                    glm::vec3 val = *ptr;
                    // Check if this is a color property
                    if (propInfo.meta.category == "Color")
                    {
                        if (DrawColor3(label, val, isReadOnly))
                        {
                            changed = true;
                            currentValue = entt::meta_any{val};
                        }
                    }
                    else
                    {
                        if (DrawVec3(label, val, config.dragSpeed, isReadOnly))
                        {
                            changed = true;
                            currentValue = entt::meta_any{val};
                        }
                    }
                }
                break;
            }
            
            case PropertyType::Vec4:
            {
                if (auto* ptr = currentValue.try_cast<glm::vec4>())
                {
                    glm::vec4 val = *ptr;
                    // Check if this is a color property
                    if (propInfo.meta.category == "Color")
                    {
                        if (DrawColor4(label, val, isReadOnly))
                        {
                            changed = true;
                            currentValue = entt::meta_any{val};
                        }
                    }
                    else
                    {
                        if (DrawVec4(label, val, config.dragSpeed, isReadOnly))
                        {
                            changed = true;
                            currentValue = entt::meta_any{val};
                        }
                    }
                }
                break;
            }
            
            case PropertyType::Quat:
            {
                if (auto* ptr = currentValue.try_cast<glm::quat>())
                {
                    glm::quat val = *ptr;
                    if (DrawQuat(label, val, config.dragSpeed, isReadOnly))
                    {
                        changed = true;
                        currentValue = entt::meta_any{val};
                    }
                }
                break;
            }
            
            case PropertyType::Enum:
            {
                if (propInfo.enumTypeId != 0)
                {
                    auto enumType = entt::resolve(propInfo.enumTypeId);
                    if (enumType && DrawEnum(label, currentValue, enumType, isReadOnly))
                    {
                        changed = true;
                    }
                }
                else
                {
                    ImGui::Text("%s: <enum type unknown>", label);
                }
                break;
            }
            
            case PropertyType::Array:
            {
                if (DrawArray(label, propInfo, currentValue, isReadOnly))
                {
                    changed = true;
                }
                break;
            }
            
            default:
                ImGui::Text("%s: <unsupported type>", label);
                break;
        }
        
        // Apply change with command for undo
        if (changed && !isReadOnly)
        {
            if (history)
            {
                auto command = std::make_unique<PropertyCommand>(
                    component,
                    propInfo.name,
                    currentValue,
                    oldValue
                );
                history->Execute(std::move(command));
            }
            else
            {
                // Direct set without undo
                PropertyAccessor::SetPropertyValue(metaType, component, propInfo.name, currentValue);
            }
        }
        
        return changed;
    }
    
    bool PropertyWidgets::DrawComponentProperties(
        Assets::Component* component,
        CommandHistory* history,
        WidgetConfig config
    )
    {
        if (!component)
        {
            return false;
        }
        
        bool anyChanged = false;
        auto metaType = component->GetMetaType();
        auto properties = PropertyAccessor::GetProperties(metaType);
        
        // Group properties by category
        std::map<std::string, std::vector<PropertyInfo>> categorized;
        for (const auto& prop : properties)
        {
            std::string category = prop.meta.category.empty() ? "General" : prop.meta.category;
            categorized[category].push_back(prop);
        }
        
        // Reduce indent for property rows
        float indent = ImGui::GetStyle().IndentSpacing;
        ImGui::GetStyle().IndentSpacing = 8.0f;
        
        // Draw each category
        for (const auto& [category, props] : categorized)
        {
            if (DrawCategoryHeader(category.c_str()))
            {
                ImGui::Indent();
                for (const auto& prop : props)
                {
                    if (DrawProperty(prop, component, history, config))
                    {
                        anyChanged = true;
                    }
                }
                ImGui::Unindent();
            }
        }
        
        // Restore indent
        ImGui::GetStyle().IndentSpacing = indent;
        
        return anyChanged;
    }
    
    // Individual widget implementations - with property row layout
    
    bool PropertyWidgets::DrawBool(const char* label, bool& value, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                bool temp = value;
                ImGui::BeginDisabled();
                bool result = ImGui::Checkbox("##v", &temp);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::Checkbox("##v", &value);
        });
    }
    
    bool PropertyWidgets::DrawInt(const char* label, int32_t& value, float speed, int min, int max, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                int temp = value;
                ImGui::BeginDisabled();
                ImGui::DragInt("##v", &temp, speed, min, max);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::DragInt("##v", &value, speed, min, max);
        });
    }
    
    bool PropertyWidgets::DrawUInt(const char* label, uint32_t& value, float speed, uint32_t min, uint32_t max, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            int intVal = static_cast<int>(value);
            int intMin = static_cast<int>(std::min(min, static_cast<uint32_t>(INT_MAX)));
            int intMax = static_cast<int>(std::min(max, static_cast<uint32_t>(INT_MAX)));
            
            bool changed = false;
            if (readOnly)
            {
                ImGui::BeginDisabled();
                ImGui::DragInt("##v", &intVal, speed, intMin, intMax);
                ImGui::EndDisabled();
            }
            else
            {
                if (ImGui::DragInt("##v", &intVal, speed, intMin, intMax))
                {
                    value = static_cast<uint32_t>(std::max(0, intVal));
                    changed = true;
                }
            }
            return changed;
        });
    }
    
    bool PropertyWidgets::DrawFloat(const char* label, float& value, float speed, float min, float max, bool readOnly, const char* format)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                float temp = value;
                ImGui::BeginDisabled();
                ImGui::DragFloat("##v", &temp, speed, min, max, format);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::DragFloat("##v", &value, speed, min, max, format);
        });
    }
    
    bool PropertyWidgets::DrawDouble(const char* label, double& value, float speed, double min, double max, bool readOnly, const char* format)
    {
        float floatVal = static_cast<float>(value);
        bool changed = DrawFloat(label, floatVal, speed, static_cast<float>(min), static_cast<float>(max), readOnly, format);
        if (changed)
        {
            value = static_cast<double>(floatVal);
        }
        return changed;
    }
    
    bool PropertyWidgets::DrawString(const char* label, std::string& value, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                ImGui::BeginDisabled();
                ImGui::InputText("##v", &value, ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::InputText("##v", &value);
        });
    }
    
    bool PropertyWidgets::DrawVec2(const char* label, glm::vec2& value, float speed, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                glm::vec2 temp = value;
                ImGui::BeginDisabled();
                ImGui::DragFloat2("##v", &temp.x, speed);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::DragFloat2("##v", &value.x, speed);
        });
    }
    
    bool PropertyWidgets::DrawVec3(const char* label, glm::vec3& value, float speed, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                glm::vec3 temp = value;
                ImGui::BeginDisabled();
                ImGui::DragFloat3("##v", &temp.x, speed);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::DragFloat3("##v", &value.x, speed);
        });
    }
    
    bool PropertyWidgets::DrawVec4(const char* label, glm::vec4& value, float speed, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                glm::vec4 temp = value;
                ImGui::BeginDisabled();
                ImGui::DragFloat4("##v", &temp.x, speed);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::DragFloat4("##v", &value.x, speed);
        });
    }
    
    bool PropertyWidgets::DrawQuat(const char* label, glm::quat& value, float speed, bool readOnly)
    {
        // Convert to euler angles for editing
        glm::vec3 euler = glm::degrees(glm::eulerAngles(value));
        
        bool changed = DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                ImGui::BeginDisabled();
                ImGui::DragFloat3("##v", &euler.x, speed);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::DragFloat3("##v", &euler.x, speed);
        });
        
        if (changed)
        {
            value = glm::quat(glm::radians(euler));
        }
        return changed;
    }
    
    bool PropertyWidgets::DrawColor3(const char* label, glm::vec3& value, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                glm::vec3 temp = value;
                ImGui::BeginDisabled();
                ImGui::ColorEdit3("##v", &temp.x);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::ColorEdit3("##v", &value.x);
        });
    }
    
    bool PropertyWidgets::DrawColor4(const char* label, glm::vec4& value, bool readOnly)
    {
        return DrawPropertyRow(label, [&]() {
            if (readOnly)
            {
                glm::vec4 temp = value;
                ImGui::BeginDisabled();
                ImGui::ColorEdit4("##v", &temp.x);
                ImGui::EndDisabled();
                return false;
            }
            return ImGui::ColorEdit4("##v", &value.x);
        });
    }
    
    bool PropertyWidgets::DrawEnum(
        const char* label,
        entt::meta_any& value,
        entt::meta_type enumType,
        bool readOnly
    )
    {
        auto enumValues = GetEnumValues(enumType);
        if (enumValues.empty())
        {
            BeginPropertyRow(label);
            ImGui::Text("<no enum values>");
            return false;
        }
        
        // Find current selection
        int currentIndex = 0;
        for (size_t i = 0; i < enumValues.size(); ++i)
        {
            if (enumValues[i].second == value)
            {
                currentIndex = static_cast<int>(i);
                break;
            }
        }
        
        // Create combo items
        std::vector<const char*> items;
        items.reserve(enumValues.size());
        for (const auto& [name, _] : enumValues)
        {
            items.push_back(name.c_str());
        }
        
        return DrawPropertyRow(label, [&]() {
            bool changed = false;
            if (readOnly)
            {
                ImGui::BeginDisabled();
                ImGui::Combo("##v", &currentIndex, items.data(), static_cast<int>(items.size()));
                ImGui::EndDisabled();
            }
            else
            {
                if (ImGui::Combo("##v", &currentIndex, items.data(), static_cast<int>(items.size())))
                {
                    value = enumValues[currentIndex].second;
                    changed = true;
                }
            }
            return changed;
        });
    }
    
    std::vector<std::pair<std::string, entt::meta_any>> PropertyWidgets::GetEnumValues(entt::meta_type enumType)
    {
        std::vector<std::pair<std::string, entt::meta_any>> result;
        
        if (!enumType.is_enum())
        {
            return result;
        }
        
        // Simplified implementation - iterate over enum data members
        // In entt v3.x, we use .custom<>() for metadata, not .prop()
        for (auto&& [id, data] : enumType.data())
        {
            // Use the data name if available, otherwise use a generated name
            const char* name = data.name();
            if (name)
            {
                result.emplace_back(name, data.get({}));
            }
            else
            {
                // Generate a name from the id
                result.emplace_back(std::to_string(id), data.get({}));
            }
        }
        
        return result;
    }
    
    // ============================================================================
    // Array Drawing Helper
    // ============================================================================
    
    // Helper template to draw a container with known element type
    // Reduces code duplication for std::array and std::vector handling
    template<typename ContainerT, typename ElementT, typename DrawFunc>
    static bool DrawContainerElements(
        const char* label,
        ContainerT& container,
        size_t size,
        bool readOnly,
        DrawFunc drawElement
    )
    {
        bool changed = false;
        std::string headerLabel = std::string(label) + " [" + std::to_string(size) + "]";
        
        if (ImGui::TreeNodeEx(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < size; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                
                std::string indexLabel = "[" + std::to_string(i) + "]";
                ElementT val = container[i];
                
                if (drawElement(indexLabel.c_str(), val, readOnly))
                {
                    container[i] = val;
                    changed = true;
                }
                
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        return changed;
    }
    
    // RAII helper for indent management
    class ScopedIndent
    {
    public:
        explicit ScopedIndent(float newIndent)
            : oldIndent_(ImGui::GetStyle().IndentSpacing)
        {
            ImGui::GetStyle().IndentSpacing = newIndent;
        }
        
        ~ScopedIndent()
        {
            ImGui::GetStyle().IndentSpacing = oldIndent_;
        }
        
        ScopedIndent(const ScopedIndent&) = delete;
        ScopedIndent& operator=(const ScopedIndent&) = delete;
        
    private:
        float oldIndent_;
    };

    bool PropertyWidgets::DrawArray(
        const char* label,
        const PropertyInfo& propInfo,
        entt::meta_any& arrayValue,
        bool readOnly
    )
    {
        bool changed = false;
        
        // RAII indent management
        ScopedIndent indent(8.0f);
        
        // Try specific container types with their optimized handlers
        // std::array<uint32_t, 16> (for Materials)
        if (auto* arr = arrayValue.try_cast<std::array<uint32_t, 16>>())
        {
            return DrawContainerElements<std::array<uint32_t, 16>, uint32_t>(
                label, *arr, 16, readOnly,
                [](const char* lbl, uint32_t& val, bool ro) {
                    return DrawUInt(lbl, val, 1.0f, 0, UINT_MAX, ro);
                });
        }
        
        // std::vector<uint32_t>
        if (auto* vec = arrayValue.try_cast<std::vector<uint32_t>>())
        {
            return DrawContainerElements<std::vector<uint32_t>, uint32_t>(
                label, *vec, vec->size(), readOnly,
                [](const char* lbl, uint32_t& val, bool ro) {
                    return DrawUInt(lbl, val, 1.0f, 0, UINT_MAX, ro);
                });
        }
        
        // std::vector<int32_t>
        if (auto* vec = arrayValue.try_cast<std::vector<int32_t>>())
        {
            return DrawContainerElements<std::vector<int32_t>, int32_t>(
                label, *vec, vec->size(), readOnly,
                [](const char* lbl, int32_t& val, bool ro) {
                    return DrawInt(lbl, val, 1.0f, INT_MIN, INT_MAX, ro);
                });
        }
        
        // std::vector<float>
        if (auto* vec = arrayValue.try_cast<std::vector<float>>())
        {
            return DrawContainerElements<std::vector<float>, float>(
                label, *vec, vec->size(), readOnly,
                [](const char* lbl, float& val, bool ro) {
                    return DrawFloat(lbl, val, 0.1f, -FLT_MAX, FLT_MAX, ro);
                });
        }
        
        // std::vector<std::string>
        if (auto* vec = arrayValue.try_cast<std::vector<std::string>>())
        {
            return DrawContainerElements<std::vector<std::string>, std::string>(
                label, *vec, vec->size(), readOnly,
                [](const char* lbl, std::string& val, bool ro) {
                    return DrawString(lbl, val, ro);
                });
        }
        
        // Fallback: try using entt's sequence container API
        auto container = arrayValue.as_sequence_container();
        if (!container)
        {
            BeginPropertyRow(label);
            ImGui::Text("<not a sequence container>");
            return false;
        }
        
        size_t size = container.size();
        std::string headerLabel = std::string(label) + " [" + std::to_string(size) + "]";
        
        if (ImGui::TreeNodeEx(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (size_t i = 0; i < size; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                
                entt::meta_any element = container[i];
                
                if (DrawArrayElement(i, element, propInfo.elementType, propInfo.elementEnumTypeId, readOnly))
                {
                    changed = true;
                }
                
                ImGui::PopID();
            }
            
            ImGui::TreePop();
        }
        
        return changed;
    }
    
    bool PropertyWidgets::DrawArrayElement(
        size_t index,
        entt::meta_any& element,
        PropertyType elementType,
        uint32_t enumTypeId,
        bool readOnly
    )
    {
        std::string indexLabel = "[" + std::to_string(index) + "]";
        bool changed = false;
        
        switch (elementType)
        {
            case PropertyType::Bool:
            {
                if (auto* ptr = element.try_cast<bool>())
                {
                    bool val = *ptr;
                    if (DrawBool(indexLabel.c_str(), val, readOnly))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::Int32:
            {
                if (auto* ptr = element.try_cast<int32_t>())
                {
                    int32_t val = *ptr;
                    if (DrawInt(indexLabel.c_str(), val, 0.1f, INT_MIN, INT_MAX, readOnly))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::UInt32:
            {
                if (auto* ptr = element.try_cast<uint32_t>())
                {
                    uint32_t val = *ptr;
                    if (DrawUInt(indexLabel.c_str(), val, 0.1f, 0, UINT_MAX, readOnly))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::Float:
            {
                if (auto* ptr = element.try_cast<float>())
                {
                    float val = *ptr;
                    if (DrawFloat(indexLabel.c_str(), val, 0.1f, -FLT_MAX, FLT_MAX, readOnly, "%.3f"))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::Double:
            {
                if (auto* ptr = element.try_cast<double>())
                {
                    double val = *ptr;
                    if (DrawDouble(indexLabel.c_str(), val, 0.1f, -DBL_MAX, DBL_MAX, readOnly, "%.6f"))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::String:
            {
                if (auto* ptr = element.try_cast<std::string>())
                {
                    std::string val = *ptr;
                    if (DrawString(indexLabel.c_str(), val, readOnly))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::Vec3:
            {
                if (auto* ptr = element.try_cast<glm::vec3>())
                {
                    glm::vec3 val = *ptr;
                    if (DrawVec3(indexLabel.c_str(), val, 0.1f, readOnly))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::Vec4:
            {
                if (auto* ptr = element.try_cast<glm::vec4>())
                {
                    glm::vec4 val = *ptr;
                    if (DrawVec4(indexLabel.c_str(), val, 0.1f, readOnly))
                    {
                        *ptr = val;
                        changed = true;
                    }
                }
                break;
            }
            
            case PropertyType::Enum:
            {
                if (enumTypeId != 0)
                {
                    auto enumType = entt::resolve(enumTypeId);
                    if (enumType && DrawEnum(indexLabel.c_str(), element, enumType, readOnly))
                    {
                        changed = true;
                    }
                }
                else
                {
                    ImGui::Text("%s: <enum type unknown>", indexLabel.c_str());
                }
                break;
            }
            
            default:
                ImGui::Text("%s: <unsupported element type>", indexLabel.c_str());
                break;
        }
        
        return changed;
    }
}
