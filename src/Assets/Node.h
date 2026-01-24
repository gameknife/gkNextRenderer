#pragma once
#include "Common/CoreMinimal.hpp"
#include "UniformBuffer.hpp"
#include "Runtime/NextPhysics.h"
#include "Component.h"
#include "Runtime/Components/PhysicsComponent.h"

#include "glm/ext.hpp"

#include <unordered_map>
#include <typeindex>
#include <vector>
#include <memory>
#include <type_traits>

namespace Runtime { class SkinnedMeshComponent; }

namespace Assets
{
    class Node : public std::enable_shared_from_this<Node>
    {
    public:
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

        uint32_t GetInstanceId() const { return instanceId_; }
        bool TickVelocity(glm::mat4& combinedTS);

        void SetParent(std::shared_ptr<Node> parent);
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
            
            if (component)
            {
                component->SetOwner(this);
                components_[std::type_index(typeid(T))] = component;
            }
            else
            {
                components_.erase(std::type_index(typeid(T)));
            }
        }

        template <typename T>
        std::shared_ptr<T> GetComponent() const
        {
            static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
            
            auto it = components_.find(std::type_index(typeid(T)));
            if (it != components_.end())
            {
                return std::static_pointer_cast<T>(it->second);
            }
            return nullptr;
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

        std::unordered_map<std::type_index, std::shared_ptr<Component>> components_;
    };
}
