#pragma once

#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/ICommand.hpp"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Assets/Core/Component.h"
#include "Assets/Core/Node.h"

#include <entt/meta/meta.hpp>
#include <functional>
#include <string>
#include <string_view>
#include <spdlog/fmt/fmt.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

/**
 * Command for modifying a property on a component through reflection.
 * Supports undo/redo and command merging for continuous edits.
 */
class PropertyCommand : public ICommand
{
public:
    /**
     * Create a property command.
     * @param component The component to modify
     * @param propertyName Name of the property to modify
     * @param newValue The new value to set
     * @param oldValue The old value (for undo)
     */
    PropertyCommand(
        Assets::Component* component,
        const std::string& propertyName,
        entt::meta_any newValue,
        entt::meta_any oldValue)
        : component_(component)
        , propertyName_(propertyName)
        , newValue_(std::move(newValue))
        , oldValue_(std::move(oldValue))
    {
    }

    bool Execute() override
    {
        if (!component_)
        {
            return false;
        }

        auto metaType = component_->GetMetaType();
        return Reflection::PropertyAccessor::SetPropertyValue(
            metaType,
            component_,
            propertyName_,
            newValue_);
    }

    bool Undo() override
    {
        if (!component_)
        {
            return false;
        }

        auto metaType = component_->GetMetaType();
        return Reflection::PropertyAccessor::SetPropertyValue(
            metaType,
            component_,
            propertyName_,
            oldValue_);
    }

    std::string GetDescription() const override
    {
        return fmt::format("Set {} on {}", propertyName_, component_ ? component_->GetTypeName() : "null");
    }

    bool CanMergeWith(const ICommand* other) const override
    {
        auto* otherCmd = dynamic_cast<const PropertyCommand*>(other);
        if (!otherCmd)
        {
            return false;
        }

        const auto type = newValue_.type();
        if (!type)
        {
            return false;
        }

        if (type == entt::resolve<bool>() || type.is_enum() || type.is_sequence_container())
        {
            return false;
        }

        if (type == entt::resolve<std::string>() ||
            type == entt::resolve<std::string_view>() ||
            type == entt::resolve<const char*>())
        {
            return false;
        }

        const bool isNumeric =
            type == entt::resolve<int8_t>() ||
            type == entt::resolve<uint8_t>() ||
            type == entt::resolve<int16_t>() ||
            type == entt::resolve<uint16_t>() ||
            type == entt::resolve<int32_t>() ||
            type == entt::resolve<uint32_t>() ||
            type == entt::resolve<int64_t>() ||
            type == entt::resolve<uint64_t>() ||
            type == entt::resolve<float>() ||
            type == entt::resolve<double>() ||
            type == entt::resolve<glm::vec2>() ||
            type == entt::resolve<glm::vec3>() ||
            type == entt::resolve<glm::vec4>() ||
            type == entt::resolve<glm::quat>();

        if (!isNumeric)
        {
            return false;
        }

        return component_ == otherCmd->component_ && propertyName_ == otherCmd->propertyName_;
    }

    void MergeWith(const ICommand* other) override
    {
        auto* otherCmd = dynamic_cast<const PropertyCommand*>(other);
        if (otherCmd)
        {
            newValue_ = otherCmd->newValue_;
        }
    }

    Assets::Component* GetComponent() const { return component_; }
    const std::string& GetPropertyName() const { return propertyName_; }

private:
    Assets::Component* component_ = nullptr;
    std::string propertyName_;
    entt::meta_any newValue_;
    entt::meta_any oldValue_;
};
