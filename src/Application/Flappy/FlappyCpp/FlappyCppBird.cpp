#include "Application/Flappy/FlappyCpp/FlappyCppBird.hpp"

#include "Assets/Core/Node.h"

namespace Flappy
{
    void FFlappyCppBird::Reset(const FBirdConfig& config)
    {
        position_ = config.initialPosition;
        velocityY_ = 0.0f;
        SyncVisual();
    }

    void FFlappyCppBird::Flap(const FBirdConfig& config)
    {
        velocityY_ = config.flapVelocity;
    }

    void FFlappyCppBird::Update(float fixedDeltaSeconds, const FBirdConfig& config)
    {
        velocityY_ = std::clamp(velocityY_ + config.gravity * fixedDeltaSeconds,
                                config.minVelocity,
                                config.maxVelocity);
        position_.y += velocityY_ * fixedDeltaSeconds;
        SyncVisual();
    }

    void FFlappyCppBird::RestoreRuntime(const glm::vec3& position, float velocityY)
    {
        position_ = position;
        velocityY_ = velocityY;
        SyncVisual();
    }

    void FFlappyCppBird::SyncVisual() const
    {
        if (!node_)
        {
            return;
        }
        node_->SetTranslation(position_);
        node_->RecalcTransform(true);
    }
}
