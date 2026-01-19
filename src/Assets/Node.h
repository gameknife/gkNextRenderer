#pragma once
#include "Common/CoreMinimal.hpp"
#include "UniformBuffer.hpp"
#include "Runtime/NextPhysics.h"
#include "Component.h"

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
        enum class ENodeMobility
        {
            Static,
            Dynamic,
            Kinematic
        };
        
        static std::shared_ptr<Node> CreateNode(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t modelId, uint32_t instanceId, bool replace);
        Node(std::string name,  glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t id, uint32_t instanceId, bool replace);
        
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
        
        uint32_t GetModel() const { return modelId_; }
        const std::string& GetName() const {return name_; }

        void SetVisible(bool visible);
        void SetRayCastVisible(bool visible) { rayCastVisible_ = visible; }
        bool IsVisible() const { return visible_; }
        bool IsRayCastVisible() const { return rayCastVisible_; }
        bool IsDrawable() const { return modelId_ != -1; }

        uint32_t GetInstanceId() const { return instanceId_; }
        bool TickVelocity(glm::mat4& combinedTS);

        void SetParent(std::shared_ptr<Node> parent);
        Node* GetParent() { return parent_.get(); }

        void AddChild(std::shared_ptr<Node> child);
        void RemoveChild(std::shared_ptr<Node> child);

        const std::set< std::shared_ptr<Node> >& Children() const { return children_; }

        void SetMaterial(const std::array<uint32_t, 16>& materials);
        void SetModelId(uint32_t modelId) { modelId_ = modelId; }
        std::array<uint32_t, 16>& Materials() { return materialIdx_; }
        NodeProxy GetNodeProxy() const;

        void BindPhysicsBody(NextBodyID bodyId) { physicsBodyTemp_ = bodyId; }

        void SetMobility(ENodeMobility staticType) { mobility_ = staticType; }
        ENodeMobility GetMobility() const { return mobility_; }

        const NextBodyID& GetPhysicsBody() const { return physicsBodyTemp_; }

        void SetPhysicsOffset(const glm::vec3& offset) { physicsOffset_ = offset; }
        const glm::vec3& GetPhysicsOffset() const { return physicsOffset_; }
        
        void SetSkin(int32_t skinIndex) { skinIndex_ = skinIndex; }
        int32_t GetSkin() const { return skinIndex_; }

        void SetSkinnedMesh(std::shared_ptr<Runtime::SkinnedMeshComponent> skinnedMesh) { skinnedMesh_ = skinnedMesh; }
        Runtime::SkinnedMeshComponent* GetSkinnedMesh() const { return skinnedMesh_.get(); }

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
        std::string name_;

        mutable glm::vec3 translation_;
        mutable glm::quat rotation_;
        mutable glm::vec3 scaling_;

        glm::vec3 physicsOffset_ = glm::vec3(0.0f);
        glm::mat4 localTransform_;
        glm::mat4 transform_;
        glm::mat4 prevTransform_;
        uint32_t modelId_;
        int32_t skinIndex_ = -1;
        std::shared_ptr<Runtime::SkinnedMeshComponent> skinnedMesh_;
        uint32_t instanceId_;
        bool visible_;
        bool rayCastVisible_;

        std::shared_ptr<Node> parent_;
        std::set< std::shared_ptr<Node> > children_;
        std::array<uint32_t, 16> materialIdx_;
        NextBodyID physicsBodyTemp_;
        ENodeMobility mobility_;

        std::vector<std::shared_ptr<Component>> components_;
    };
}