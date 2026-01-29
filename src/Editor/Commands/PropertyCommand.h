#pragma once

#include "ICommand.h"
#include "Runtime/Reflection/PropertyAccessor.h"
#include "Assets/Component.h"
#include "Assets/Node.h"
#include <entt/meta/meta.hpp>
#include <functional>
#include <string>
#include <spdlog/fmt/fmt.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Editor
{
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
            entt::meta_any oldValue
        )
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
                newValue_
            );
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
                oldValue_
            );
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
            
            // Merge if same component and same property
            return component_ == otherCmd->component_ && 
                   propertyName_ == otherCmd->propertyName_;
        }
        
        void MergeWith(const ICommand* other) override
        {
            auto* otherCmd = dynamic_cast<const PropertyCommand*>(other);
            if (otherCmd)
            {
                // Keep our old value (from the original change), update new value
                newValue_ = otherCmd->newValue_;
            }
        }
        
        // Accessors for inspection
        Assets::Component* GetComponent() const { return component_; }
        const std::string& GetPropertyName() const { return propertyName_; }
        
    private:
        Assets::Component* component_ = nullptr;
        std::string propertyName_;
        entt::meta_any newValue_;
        entt::meta_any oldValue_;
    };

    /**
     * Command for modifying Node transform (position, rotation, scale).
     * Separate from PropertyCommand as transforms are not component properties.
     */
    class TransformCommand : public ICommand
    {
    public:
        enum class TransformType
        {
            Translation,
            Rotation,
            Scale
        };
        
        TransformCommand(
            Assets::Node* node,
            TransformType type,
            glm::vec3 newValue,
            glm::vec3 oldValue
        )
            : node_(node)
            , type_(type)
            , newValue_(newValue)
            , oldValue_(oldValue)
        {
        }
        
        TransformCommand(
            Assets::Node* node,
            glm::quat newRotation,
            glm::quat oldRotation
        )
            : node_(node)
            , type_(TransformType::Rotation)
            , newRotation_(newRotation)
            , oldRotation_(oldRotation)
            , isQuat_(true)
        {
        }
        
        bool Execute() override;
        bool Undo() override;
        std::string GetDescription() const override;
        
        bool CanMergeWith(const ICommand* other) const override
        {
            auto* otherCmd = dynamic_cast<const TransformCommand*>(other);
            if (!otherCmd)
            {
                return false;
            }
            return node_ == otherCmd->node_ && type_ == otherCmd->type_;
        }
        
        void MergeWith(const ICommand* other) override
        {
            auto* otherCmd = dynamic_cast<const TransformCommand*>(other);
            if (otherCmd)
            {
                if (isQuat_)
                {
                    newRotation_ = otherCmd->newRotation_;
                }
                else
                {
                    newValue_ = otherCmd->newValue_;
                }
            }
        }
        
    private:
        Assets::Node* node_ = nullptr;
        TransformType type_;
        glm::vec3 newValue_{0.0f};
        glm::vec3 oldValue_{0.0f};
        glm::quat newRotation_{1.0f, 0.0f, 0.0f, 0.0f};
        glm::quat oldRotation_{1.0f, 0.0f, 0.0f, 0.0f};
        bool isQuat_ = false;
    };
}
