#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Core/Component.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"

#include "glm/ext.hpp"

#include <limits>
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
        
        void SetTranslation(glm::vec3 translation);
        void SetRotation(glm::quat rotation);
        void SetScale(glm::vec3 scale);

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
        void SetName(std::string name);
        const std::string& GetTag() const { return tag_; }
        void SetTag(std::string tag) { tag_ = std::move(tag); }
        const std::string& GetLayer() const { return layer_; }
        void SetLayer(std::string layer) { layer_ = std::move(layer); }

        bool IsSceneReferenceInternal() const { return sceneReferenceOwnerProxyId_ != invalidNodeId; }
        uint32_t GetSceneReferenceOwnerProxyId() const { return sceneReferenceOwnerProxyId_; }
        void SetSceneReferenceOwnerProxyId(uint32_t id) { sceneReferenceOwnerProxyId_ = id; }
        void ClearSceneReferenceOwner() { sceneReferenceOwnerProxyId_ = invalidNodeId; }

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
            const auto componentTypeId = ComponentTypeId<T>();

            if constexpr (std::is_same_v<T, Runtime::PhysicsComponent>)
            {
                physicsComponent_ = nullptr;
            }
            
            // Remove existing component of same type
            for (auto it = components_.begin(); it != components_.end(); )
            {
                if ((*it)->GetTypeId() == componentTypeId)
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
                componentTypeMask_ |= ComponentTypeMask<T>();

                if constexpr (std::is_same_v<T, Runtime::PhysicsComponent>)
                {
                    physicsComponent_ = component.get();
                }
            }
        }

        template <typename T>
        T* GetComponentPtr() const
        {
            static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
            if (!MayHaveComponent<T>())
            {
                return nullptr;
            }
            const auto componentTypeId = ComponentTypeId<T>();

            for (const auto& comp : components_)
            {
                if (comp->GetTypeId() == componentTypeId)
                {
                    return static_cast<T*>(comp.get());
                }
            }
            return nullptr;
        }

        template <typename T>
        std::shared_ptr<T> GetComponent() const
        {
            static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
            if (!MayHaveComponent<T>())
            {
                return nullptr;
            }
            const auto componentTypeId = ComponentTypeId<T>();
            
            for (const auto& comp : components_)
            {
                if (comp->GetTypeId() == componentTypeId)
                {
                    return std::static_pointer_cast<T>(comp);
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
        template <typename T>
        static constexpr uint64_t ComponentTypeMask()
        {
            return 1ull << (ComponentTypeId<T>() & 63u);
        }

        template <typename T>
        bool MayHaveComponent() const
        {
            return (componentTypeMask_ & ComponentTypeMask<T>()) != 0;
        }

        std::string name_;
        std::string tag_ = "Untagged";
        std::string layer_ = "Default";

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
        uint64_t componentTypeMask_ = 0;
        Runtime::PhysicsComponent* physicsComponent_ = nullptr;
        static constexpr uint32_t invalidNodeId = std::numeric_limits<uint32_t>::max();
        uint32_t sceneReferenceOwnerProxyId_ = invalidNodeId;
    };
}
