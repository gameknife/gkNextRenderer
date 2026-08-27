#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Reflection/PropertyMeta.hpp"
#include "Engine/Utilities/Exception.hpp"
#include <entt/meta/factory.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

namespace Assets
{
    void Node::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<Node>()
            .type("Node"_hs)
            .data<nullptr, &Node::GetName>("Name")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Name", "Transform", "Node name"))
            .data<nullptr, &Node::GetInstanceId>("InstanceId")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("InstanceId", "Transform", "Node instance id"))
            .data<&Node::SetTag, &Node::GetTag>("Tag")
                .custom<PropertyMeta>(PropertyPresets::Editable("Tag", "Metadata", "Node tag"))
            .data<&Node::SetLayer, &Node::GetLayer>("Layer")
                .custom<PropertyMeta>(PropertyPresets::Editable("Layer", "Metadata", "Node layer"))
            .data<&Node::SetTranslation, &Node::Translation>("Translation")
                .custom<PropertyMeta>(PropertyPresets::Editable("Translation", "Transform", "Local translation"))
            .data<&Node::SetRotation, &Node::Rotation>("Rotation")
                .custom<PropertyMeta>(PropertyPresets::Editable("Rotation", "Transform", "Local rotation"))
            .data<&Node::SetScale, &Node::Scale>("Scale")
                .custom<PropertyMeta>(PropertyPresets::Editable("Scale", "Transform", "Local scale"))
            .func<&Node::GetName>("GetName")
            .func<&Node::GetInstanceId>("GetInstanceId")
            .func<static_cast<Component* (Node::*)(const std::string&) const>(&Node::GetComponent)>("GetComponent");
    }

    Component* Node::GetComponent(const std::string& componentType) const
    {
        for (const auto& component : components_)
        {
            if (!component)
            {
                continue;
            }

            if (component->GetTypeName() == componentType)
            {
                return component.get();
            }
        }

        return nullptr;
    }

    Component* Node::FindComponent(entt::id_type componentTypeId) const
    {
        if (componentTypeId == ComponentTypeId<Runtime::RenderComponent>())
        {
            return renderComponent_;
        }
        if (componentTypeId == ComponentTypeId<Runtime::PhysicsComponent>())
        {
            return physicsComponent_;
        }

        const auto component = std::ranges::find_if(components_, [componentTypeId](const auto& candidate)
        {
            return candidate->GetTypeId() == componentTypeId;
        });
        return component != components_.end() ? component->get() : nullptr;
    }

    void Node::SetComponent(entt::id_type componentTypeId, std::shared_ptr<Component> component)
    {
        if (component && component->GetTypeId() != componentTypeId)
        {
            Throw(std::logic_error("Component static and dynamic types do not match"));
        }
        if (component && component->GetOwner() && component->GetOwner() != this)
        {
            Throw(std::logic_error("Component is already attached to another node"));
        }

        const auto existing = std::ranges::find_if(components_, [componentTypeId](const auto& candidate)
        {
            return candidate->GetTypeId() == componentTypeId;
        });
        if (existing == components_.end() && !component)
        {
            return;
        }
        if (existing != components_.end() && existing->get() == component.get())
        {
            return;
        }

        std::shared_ptr<Component> replaced;
        if (existing != components_.end())
        {
            replaced = std::move(*existing);
            if (component)
            {
                *existing = component;
            }
            else
            {
                components_.erase(existing);
            }
        }
        else
        {
            components_.push_back(component);
        }

        if (component)
        {
            component->SetOwner(this);
        }
        if (componentTypeId == ComponentTypeId<Runtime::RenderComponent>())
        {
            renderComponent_ = static_cast<Runtime::RenderComponent*>(component.get());
        }
        else if (componentTypeId == ComponentTypeId<Runtime::PhysicsComponent>())
        {
            physicsComponent_ = static_cast<Runtime::PhysicsComponent*>(component.get());
        }

        if (scene_)
        {
            scene_->OnNodeComponentChanged(*this, componentTypeId, component.get());
        }
        if (replaced)
        {
            replaced->SetOwner(nullptr);
        }
    }

    std::shared_ptr<Node> Node::CreateNode(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t instanceId)
    {
        return std::make_shared<Node>(name, translation, rotation, scale, instanceId);
    }

    void Node::SetTranslation(glm::vec3 translation)
    {
        translation_ = translation;
        RecalcTransform(true);
    }

    void Node::SetRotation(glm::quat rotation)
    {
        rotation_ = rotation;
        RecalcTransform(true);
    }

    void Node::SetScale(glm::vec3 scale)
    {
        scaling_ = scale;
        RecalcTransform(true);
    }

    void Node::SetTransform(glm::vec3 translation, glm::quat rotation, glm::vec3 scale)
    {
        translation_ = translation;
        rotation_ = rotation;
        scaling_ = scale;
        RecalcTransform(true);
    }

    void Node::SetName(std::string name)
    {
        name_ = std::move(name);
    }

    void Node::RecalcLocalTransform()
    {
        localTransform_ = glm::translate(glm::mat4(1), translation_) * glm::mat4_cast(rotation_) * glm::scale(glm::mat4(1), scaling_);
    }

    void Node::RecalcTransform(bool full)
    {
        RecalcLocalTransform();
        if (const auto parent = parent_.lock())
        {
            transform_ = parent->transform_ * localTransform_;
        }
        else
        {
            transform_ = localTransform_;
        }

        // update children
        if (full)
        {
            for(auto& child : children_)
            {
                child->RecalcTransform(full);
            }
        }
    }

    glm::vec3 Node::WorldTranslation() const
    {
        return glm::vec3(transform_[3]);
    }
    
    glm::quat Node::WorldRotation() const
    {
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        
        glm::decompose(transform_, scale, rotation, translation, skew, perspective);
        return rotation;
    }
    
    glm::vec3 Node::WorldScale() const
    {
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        
        glm::decompose(transform_, scale, rotation, translation, skew, perspective);
        return scale;
    }

    void Node::SyncPhysics()
    {
        auto* physComp = physicsComponent_;
        if (physComp && physComp->GetMobility() == ENodeMobility::Dynamic)
        {
            NextPhysics* physics = NextEngine::GetInstance()->GetPhysicsEngine();
            auto body = physics ? physics->GetBody(physComp->GetPhysicsBody()) : nullptr;
            if (body != nullptr)
            {
                // Physics body position is the center of mass in world space.
                // Node translation is the mesh pivot in world space.
                // We need to calculate: Translation = BodyPos - Rotated(Scaled(Offset))
                
                glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * scaling_;
                glm::vec3 rotatedOffset = body->rotation * scaledOffset;
                glm::vec3 newTranslation = body->position - rotatedOffset;

                translation_ = newTranslation;
                rotation_ = body->rotation;
                RecalcTransform(true);
            }
        }
    }

    bool Node::TickVelocity()
    {
        combinedPrevTransform_ = prevTransform_ * glm::inverse(transform_);
        prevTransform_ = transform_;

        constexpr float transformEpsilon = 0.001f;
        const glm::mat4 identity(1.0f);
        bool moving = false;
        for (glm::length_t column = 0; column < 4 && !moving; ++column)
        {
            for (glm::length_t row = 0; row < 4; ++row)
            {
                if (glm::abs(combinedPrevTransform_[column][row] - identity[column][row]) > transformEpsilon)
                {
                    moving = true;
                    break;
                }
            }
        }
        if (moving)
        {
            return true;
        }

        combinedPrevTransform_ = identity;
        return false;
    }

    void Node::SetParent(const std::shared_ptr<Node>& parent)
    {
        if (!parent)
        {
            ClearParent();
            return;
        }
        for (const Node* ancestor = parent.get(); ancestor; ancestor = ancestor->GetParent())
        {
            if (ancestor == this)
            {
                Throw(std::logic_error("Node hierarchy cannot contain a cycle"));
            }
        }

        if (const auto previousParent = parent_.lock())
        {
            previousParent->children_.erase(shared_from_this());
        }
        parent_ = parent;
        parent->children_.insert(shared_from_this());

        RecalcTransform();
    }

    void Node::ClearParent()
    {
        if (const auto parent = parent_.lock())
        {
            parent->children_.erase(shared_from_this());
        }
        parent_.reset();
        RecalcTransform();
    }

    void Node::GetNodeProxy(NodeProxy& proxy) const
    {
        proxy.instanceId = instanceId_;
        proxy.worldTS = transform_;
        proxy.combinedPrevTS = combinedPrevTransform_;
        proxy.reserved1 = 0;
        proxy.reserved2 = 0;
        
        if (renderComponent_)
        {
            proxy.modelId = renderComponent_->GetModelId();
            proxy.visible = renderComponent_->GetRenderParticipationMask();
            const auto& mats = renderComponent_->GetMaterials();
            for ( int i = 0; i < 16; i++ )
            {
                proxy.matId[i] = mats[i];
            }
            proxy.skinId = renderComponent_->GetSkinIndex();
        }
        else
        {
             proxy.modelId = -1;
             proxy.visible = 0;
             for ( int i = 0; i < 16; i++ ) proxy.matId[i] = 0;
             proxy.skinId = -1;
        }
        proxy.jointMatrixOffset = 0; // Default
        proxy.excludeFromAS = 0; // Default
    }

    Node::Node(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t instanceId):
    name_(name),
    translation_(translation), rotation_(rotation), scaling_(scale), 
    instanceId_(instanceId)
    {
        RecalcLocalTransform();
        RecalcTransform();
        prevTransform_ = transform_;
    }
}
