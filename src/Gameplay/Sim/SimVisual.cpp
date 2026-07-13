#include "SimVisual.h"

#include "Engine/Assets/Core/Node.hpp"

#include <glm/gtc/quaternion.hpp>

namespace NextGameplay::Sim
{
    FGeometryVisual::FGeometryVisual(std::shared_ptr<Assets::Node> node, glm::vec3 parkedPosition)
        : node_(std::move(node)), parkedPosition_(parkedPosition)
    {
    }

    void FGeometryVisual::SetWorldTransform(const glm::vec3& position, float yaw)
    {
        node_->SetTranslation(position);
        node_->SetRotation(glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
        node_->RecalcTransform();
    }

    void FGeometryVisual::SetAnimHint(EAnimHint hint)
    {
        if (hint == hint_)
        {
            return;
        }
        hint_ = hint;
        node_->SetScale(glm::vec3(1.0f, hint == EAnimHint::Sit ? 0.55f : 1.0f, 1.0f));
        node_->RecalcTransform();
    }

    void FGeometryVisual::SetVisible(bool visible)
    {
        if (!visible)
        {
            node_->SetTranslation(parkedPosition_);
            node_->RecalcTransform();
        }
    }
}
