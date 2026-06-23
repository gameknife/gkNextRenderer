#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Reflection/PropertyMeta.h"
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
            .func<&Node::GetComponentByTypeName>("GetComponent");
    }

    Component* Node::GetComponentByTypeName(const std::string& componentType) const
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
        if(parent_)
        {
            transform_ = parent_->transform_ * localTransform_;
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

    bool Node::TickVelocity(glm::mat4& combinedTS)
    {
        auto* physComp = physicsComponent_;
        if (physComp && physComp->GetMobility() == ENodeMobility::Dynamic)
        {
            auto body = NextEngine::GetInstance()->GetPhysicsEngine()->GetBody(physComp->GetPhysicsBody());
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
        
        combinedTS = prevTransform_ * glm::inverse(transform_);
        prevTransform_ = transform_;

        glm::vec3 newPos = combinedTS * glm::vec4(0,0,0,1);
        bool moving = glm::length(newPos) > 0.001;
        if (moving)
        {
            return true;
        }

        combinedTS = glm::mat4(1);
        return false;
    }

    void Node::SetParent(std::shared_ptr<Node> parent)
    {
        // remove form previous parent
        if(parent_)
        {
            parent_->RemoveChild( shared_from_this() );
        }
        parent_ = parent;
        parent_->AddChild( shared_from_this() );

        RecalcTransform();
    }

    void Node::ClearParent()
    {
        if (parent_)
        {
            parent_->RemoveChild(shared_from_this());
            parent_.reset();
            RecalcTransform();
        }
    }

    void Node::AddChild(std::shared_ptr<Node> child)
    {
        children_.insert(child);
    }

    void Node::RemoveChild(std::shared_ptr<Node> child)
    {
        children_.erase(child);
    }

    NodeProxy Node::GetNodeProxy() const
    {
        NodeProxy proxy;
        proxy.instanceId = instanceId_;
        proxy.worldTS = WorldTransform();
        proxy.reserved1 = 0;
        proxy.reserved2 = 0;
        
        auto renderComp = GetComponent<Runtime::RenderComponent>();
        if (renderComp)
        {
            proxy.modelId = renderComp->GetModelId();
            proxy.visible = renderComp->GetVisible() ? 1 : 0;
            const auto& mats = renderComp->GetMaterials();
            for ( int i = 0; i < 16; i++ )
            {
                proxy.matId[i] = mats[i];
            }
            proxy.skinId = renderComp->GetSkinIndex();
        }
        else
        {
             proxy.modelId = -1;
             proxy.visible = 0;
             for ( int i = 0; i < 16; i++ ) proxy.matId[i] = 0;
             proxy.skinId = -1;
        }

        proxy.jointMatrixOffset = 0; // Default
        proxy.nort = 0; // Default

        return proxy;
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
