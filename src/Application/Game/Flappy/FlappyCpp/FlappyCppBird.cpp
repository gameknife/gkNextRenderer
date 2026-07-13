#include "FlappyCpp/FlappyCppBird.hpp"

#include "Engine/Assets/Core/Node.hpp"

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
