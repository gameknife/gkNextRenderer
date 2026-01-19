#pragma once
#include "Common/CoreMinimal.hpp"
#include "UniformBuffer.hpp"
#include "Runtime/NextPhysics.h"
#include "Component.h"
#include "PhysicsComponent.h"

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
        // Use enum from PhysicsComponent to maintain compatibility, but eventually we should use Assets::ENodeMobility directly
        using ENodeMobility = Assets::ENodeMobility;
        
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

        // Render properties moved to RenderComponent
        // Physics properties moved to PhysicsComponent

        uint32_t GetInstanceId() const { return instanceId_; }
        bool TickVelocity(glm::mat4& combinedTS);

        void SetParent(std::shared_ptr<Node> parent);
        Node* GetParent() { return parent_.get(); }

        void AddChild(std::shared_ptr<Node> child);
        void RemoveChild(std::shared_ptr<Node> child);

        const std::set< std::shared_ptr<Node> >& Children() const { return children_; }

        NodeProxy GetNodeProxy() const;

        void BindPhysicsBody(NextBodyID bodyId);

        void SetMobility(ENodeMobility staticType);
        ENodeMobility GetMobility() const;

        const NextBodyID& GetPhysicsBody() const;

        void SetPhysicsOffset(const glm::vec3& offset);
        const glm::vec3& GetPhysicsOffset() const;
        
        void SetSkin(int32_t skinIndex);
        int32_t GetSkin() const;

        void SetSkinnedMesh(std::shared_ptr<Runtime::SkinnedMeshComponent> skinnedMesh);
        Runtime::SkinnedMeshComponent* GetSkinnedMesh() const;

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

    private:
        std::shared_ptr<PhysicsComponent> GetOrAddPhysicsComponent();
        std::shared_ptr<PhysicsComponent> GetPhysicsComponent() const;

        std::string name_;

        mutable glm::vec3 translation_;
        mutable glm::quat rotation_;
        mutable glm::vec3 scaling_;

        // glm::vec3 physicsOffset_ = glm::vec3(0.0f); // Moved
        glm::mat4 localTransform_;
        glm::mat4 transform_;
        glm::mat4 prevTransform_;
        // uint32_t modelId_; // Moved
        // int32_t skinIndex_ = -1; // Moved
        // std::shared_ptr<Runtime::SkinnedMeshComponent> skinnedMesh_; // Moved
        uint32_t instanceId_;
        // bool visible_; // Moved
        // bool rayCastVisible_; // Moved

        std::shared_ptr<Node> parent_;
        std::set< std::shared_ptr<Node> > children_;
        // std::array<uint32_t, 16> materialIdx_; // Moved
        // NextBodyID physicsBodyTemp_; // Moved
        // ENodeMobility mobility_; // Moved

        std::vector<std::shared_ptr<Component>> components_;
    };
}
