#pragma once
#include "Common/CoreMinimal.hpp"
#include "Assets/GPU/UniformBuffer.hpp"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Assets/Core/Component.h"
#include "Runtime/Components/PhysicsComponent.h"

#include "glm/ext.hpp"

#include <vector>
#include <memory>
#include <type_traits>

namespace Runtime { class SkinnedMeshComponent; }

namespace Assets
{
    class Node : public std::enable_shared_from_this<Node>
    {
    public:
        static void RegisterReflection();
        using ENodeMobility = Runtime::ENodeMobility;
        
        static std::shared_ptr<Node> CreateNode(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t instanceId = 0);
        Node(std::string name,  glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t instanceId);
        
        void SetTranslation( glm::vec3 translation );
        void SetRotation( glm::quat rotation );
        void SetScale( glm::vec3 scale );

        glm::vec3& Translation() const { return translation_; }
        glm::quat& Rotation() const { return rotation_; }
        glm::vec3& Scale() const { return scaling_; }

        void RecalcLocalTransform();
        void RecalcTransform(bool full = true);
        const glm::mat4& WorldTransform() const { return transform_; }
        glm::vec3 WorldTranslation() const;
        glm::quat WorldRotation() const;
        glm::vec3 WorldScale() const;
        
        const std::string& GetName() const {return name_; }

        Component* GetComponentByTypeName(const std::string& componentType) const;

        uint32_t GetInstanceId() const { return instanceId_; }
        void SetInstanceId(uint32_t id) { instanceId_ = id; }
        bool TickVelocity(glm::mat4& combinedTS);

        void SetParent(std::shared_ptr<Node> parent);
        void ClearParent();
        Node* GetParent() { return parent_.get(); }

        void AddChild(std::shared_ptr<Node> child);
        void RemoveChild(std::shared_ptr<Node> child);

        const std::set< std::shared_ptr<Node> >& Children() const { return children_; }

        NodeProxy GetNodeProxy() const;

        // New Component System
        template <typename T>
        void AddComponent(std::shared_ptr<T> component)
        {
            static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
            
            // Remove existing component of same type
            for (auto it = components_.begin(); it != components_.end(); )
            {
                if (std::dynamic_pointer_cast<T>(*it))
                {
                    it = components_.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            
            if (component)
            {
                component->SetOwner(this);
                components_.push_back(component);
            }
        }

        template <typename T>
        std::shared_ptr<T> GetComponent() const
        {
            static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
            
            for (const auto& comp : components_)
            {
                auto casted = std::dynamic_pointer_cast<T>(comp);
                if (casted)
                {
                    return casted;
                }
            }
            return nullptr;
        }

        /**
         * Get all components attached to this node.
         * Useful for reflection-based iteration.
         */
        const std::vector<std::shared_ptr<Component>>& GetComponents() const
        {
            return components_;
        }

    private:
        std::string name_;

        mutable glm::vec3 translation_;
        mutable glm::quat rotation_;
        mutable glm::vec3 scaling_;
        
        glm::mat4 localTransform_;
        glm::mat4 transform_;
        glm::mat4 prevTransform_;

        uint32_t instanceId_;

        std::shared_ptr<Node> parent_;
        std::set< std::shared_ptr<Node> > children_;

        std::vector<std::shared_ptr<Component>> components_;
    };
}
