#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"

#include "glm/ext.hpp"

#include <limits>
#include <type_traits>

namespace Runtime
{
    class PhysicsComponent;
    class RenderComponent;
}

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
        /// Updates the complete local transform with a single hierarchy recalculation. Gameplay
        /// object pools should prefer this over three independent setters.
        void SetTransform(glm::vec3 translation, glm::quat rotation, glm::vec3 scale);

        glm::vec3& Translation() const { return translation_; }
        glm::quat& Rotation() const { return rotation_; }
        glm::vec3& Scale() const { return scaling_; }

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
        /// Loader-authored key/value payload in "k=v;k=v" form. The SCAD loader writes the
        /// evaluated named parameters of a user-module call here so gameplay can recover the
        /// authoring intent (rail length, swing period, button index). Not reflected, not shown
        /// in the outliner, never serialized: it is rebuilt on every load.
        const std::string& GetMetadata() const { return metadata_; }
        void SetMetadata(std::string metadata) { metadata_ = std::move(metadata); }

        bool IsSceneReferenceInternal() const { return sceneReferenceOwnerProxyId_ != invalidNodeId; }
        uint32_t GetSceneReferenceOwnerProxyId() const { return sceneReferenceOwnerProxyId_; }
        void SetSceneReferenceOwnerProxyId(uint32_t id) { sceneReferenceOwnerProxyId_ = id; }

        Component* GetComponent(const std::string& componentType) const;

        uint32_t GetInstanceId() const { return instanceId_; }
        void SetInstanceId(uint32_t id) { instanceId_ = id; }
        void SyncPhysics();
        bool TickVelocity();

        void SetParent(const std::shared_ptr<Node>& parent);
        void ClearParent();
        Node* GetParent() { return parent_.lock().get(); }
        const Node* GetParent() const { return parent_.lock().get(); }

        const std::set< std::shared_ptr<Node> >& Children() const { return children_; }

        void GetNodeProxy(NodeProxy& proxy) const;

        template <typename T>
        void AddComponent(std::shared_ptr<T> component)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
            SetComponent(ComponentTypeId<T>(), std::move(component));
        }

        template <typename T>
        void RemoveComponent()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
            SetComponent(ComponentTypeId<T>(), {});
        }

        template <typename T>
        T* GetComponent() const
        {
            static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
            return static_cast<T*>(FindComponent(ComponentTypeId<T>()));
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
        friend class Scene;

        void RecalcLocalTransform();
        void SetComponent(entt::id_type componentTypeId, std::shared_ptr<Component> component);
        Component* FindComponent(entt::id_type componentTypeId) const;

        std::string name_;
        std::string tag_ = "Untagged";
        std::string layer_ = "Default";
        std::string metadata_;

        mutable glm::vec3 translation_;
        mutable glm::quat rotation_;
        mutable glm::vec3 scaling_;
        
        glm::mat4 localTransform_;
        glm::mat4 transform_;
        glm::mat4 prevTransform_;
        glm::mat4 combinedPrevTransform_;

        uint32_t instanceId_;

        std::weak_ptr<Node> parent_;
        std::set< std::shared_ptr<Node> > children_;

        std::vector<std::shared_ptr<Component>> components_;
        Scene* scene_ = nullptr;
        Runtime::PhysicsComponent* physicsComponent_ = nullptr;
        Runtime::RenderComponent* renderComponent_ = nullptr;
        static constexpr uint32_t invalidNodeId = std::numeric_limits<uint32_t>::max();
        uint32_t sceneReferenceOwnerProxyId_ = invalidNodeId;
    };
}
