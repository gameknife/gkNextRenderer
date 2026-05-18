#pragma once

#include "Application/Game/Flappy/FlappyCommon.hpp"

namespace Assets
{
    class Node;
}

namespace Flappy
{
    class FFlappyCppBird final
    {
    public:
        void Reset(const FBirdConfig& config);
        void Flap(const FBirdConfig& config);
        void Update(float fixedDeltaSeconds, const FBirdConfig& config);
        void SyncVisual() const;

        const glm::vec3& GetPosition() const { return position_; }
        float GetVelocityY() const { return velocityY_; }
        void SetNode(std::shared_ptr<Assets::Node> node) { node_ = std::move(node); }

    private:
        glm::vec3 position_{-3.0f, 0.0f, 0.0f};
        float velocityY_ = 0.0f;
        std::shared_ptr<Assets::Node> node_;
    };
}
