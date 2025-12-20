#include "Node.h"

#include "Runtime/Engine.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

namespace Assets
{
    std::shared_ptr<Node> Node::CreateNode(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t id, uint32_t instanceId, bool replace)
    {
        return std::make_shared<Node>(name, translation, rotation, scale, id, instanceId, replace);
    }

    void Node::SetTranslation(glm::vec3 translation)
    {
        translation_ = translation;
    }

    void Node::SetRotation(glm::quat rotation)
    {
        rotation_ = rotation;
    }

    void Node::SetScale(glm::vec3 scale)
    {
        scaling_ = scale;
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
        if (mobility_ == ENodeMobility::Dynamic)
        {
            auto body = NextEngine::GetInstance()->GetPhysicsEngine()->GetBody(physicsBodyTemp_);
            if (body != nullptr)
            {
                // Physics body position is the center of mass in world space.
                // Node translation is the mesh pivot in world space.
                // We need to calculate: Translation = BodyPos - Rotated(Scaled(Offset))
                
                glm::vec3 scaledOffset = physicsOffset_ * scaling_;
                glm::vec3 rotatedOffset = body->rotation * scaledOffset;
                glm::vec3 newTranslation = body->position - rotatedOffset;

                SetTranslation(newTranslation);
                SetRotation(body->rotation);
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

    void Node::AddChild(std::shared_ptr<Node> child)
    {
        children_.insert(child);
    }

    void Node::RemoveChild(std::shared_ptr<Node> child)
    {
        children_.erase(child);
    }

    void Node::SetMaterial(const std::array<uint32_t, 16>& materials)
    {
        materialIdx_ = materials;
    }

    NodeProxy Node::GetNodeProxy() const
    {
        NodeProxy proxy;
        proxy.instanceId = instanceId_;
        proxy.modelId = modelId_;
        proxy.worldTS = WorldTransform();
        proxy.visible = visible_ ? 1 : 0;
        for ( int i = 0; i < materialIdx_.size(); i++ )
        {
            if (i < 16)
            {
                proxy.matId[i] = materialIdx_[i];
            }
        }

        return proxy;
    }

    Node::Node(std::string name, glm::vec3 translation, glm::quat rotation, glm::vec3 scale, uint32_t id, uint32_t instanceId, bool replace):
    name_(name),
    translation_(translation), rotation_(rotation), scaling_(scale), 
    modelId_(id), instanceId_(instanceId), visible_(false), rayCastVisible_(true), mobility_(ENodeMobility::Static)
    {
        RecalcLocalTransform();
        RecalcTransform();
        if(replace)
        {
            prevTransform_ = transform_;
        }
        else
        {
            prevTransform_ = translate(glm::mat4(1), glm::vec3(0,-100,0));
        }
    }
}
