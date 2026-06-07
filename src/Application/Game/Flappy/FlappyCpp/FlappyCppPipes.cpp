#include "FlappyCpp/FlappyCppPipes.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Scene/NodeUtils.h"

namespace
{
    constexpr float hiddenY = -40.0f;

    bool AabbOverlap(float minAx, float maxAx, float minAy, float maxAy,
                     float minBx, float maxBx, float minBy, float maxBy)
    {
        return minAx <= maxBx && maxAx >= minBx && minAy <= maxBy && maxAy >= minBy;
    }
}

namespace Flappy
{
    void FFlappyCppPipes::Reset(const FPipeConfig& config)
    {
        const size_t desiredSize = static_cast<size_t>(std::max(1, config.poolSize));
        if (pipes_.size() != desiredSize)
        {
            pipes_.resize(desiredSize);
        }
        spawnTimer_ = config.spawnInterval;
        for (FPipeRuntime& pipe : pipes_)
        {
            Hide(pipe);
        }
    }

    void FFlappyCppPipes::SetNodes(size_t index,
                                   std::shared_ptr<Assets::Node> topNode,
                                   std::shared_ptr<Assets::Node> bottomNode)
    {
        if (index >= pipes_.size())
        {
            pipes_.resize(index + 1);
        }
        pipes_[index].topNode = std::move(topNode);
        pipes_[index].bottomNode = std::move(bottomNode);
        Hide(pipes_[index]);
    }

    void FFlappyCppPipes::Update(float fixedDeltaSeconds,
                                 const FPipeConfig& config,
                                 const FWorldConfig& world,
                                 FXorShift32& rng)
    {
        spawnTimer_ -= fixedDeltaSeconds;
        if (spawnTimer_ <= 0.0f)
        {
            SpawnPipe(config, world, rng);
            spawnTimer_ += config.spawnInterval;
        }

        for (FPipeRuntime& pipe : pipes_)
        {
            if (!pipe.active)
            {
                continue;
            }

            pipe.x -= config.speed * fixedDeltaSeconds;
            if (pipe.x < config.destroyX)
            {
                Hide(pipe);
                continue;
            }

            SyncVisual(pipe, config, world);
        }
    }

    bool FFlappyCppPipes::CheckCollision(const glm::vec3& birdPosition, float birdRadius, const FPipeConfig& config) const
    {
        const float birdMinX = birdPosition.x - birdRadius;
        const float birdMaxX = birdPosition.x + birdRadius;
        const float birdMinY = birdPosition.y - birdRadius;
        const float birdMaxY = birdPosition.y + birdRadius;
        const float halfWidth = config.width * 0.5f;
        const float halfGap = config.gapHeight * 0.5f;

        for (const FPipeRuntime& pipe : pipes_)
        {
            if (!pipe.active)
            {
                continue;
            }

            const float pipeMinX = pipe.x - halfWidth;
            const float pipeMaxX = pipe.x + halfWidth;
            const bool hitsTop = AabbOverlap(birdMinX, birdMaxX, birdMinY, birdMaxY,
                                             pipeMinX, pipeMaxX, pipe.gapCenterY + halfGap, FLT_MAX);
            const bool hitsBottom = AabbOverlap(birdMinX, birdMaxX, birdMinY, birdMaxY,
                                                pipeMinX, pipeMaxX, -FLT_MAX, pipe.gapCenterY - halfGap);
            if (hitsTop || hitsBottom)
            {
                return true;
            }
        }
        return false;
    }

    int FFlappyCppPipes::ConsumeScoreEvents(float birdX)
    {
        int scoreEvents = 0;
        for (FPipeRuntime& pipe : pipes_)
        {
            if (pipe.active && !pipe.scored && birdX > pipe.x)
            {
                pipe.scored = true;
                ++scoreEvents;
            }
        }
        return scoreEvents;
    }

    void FFlappyCppPipes::SpawnPipe(const FPipeConfig& config, const FWorldConfig& world, FXorShift32& rng)
    {
        auto pipeIt = std::find_if(pipes_.begin(), pipes_.end(), [](const FPipeRuntime& pipe)
        {
            return !pipe.active;
        });
        if (pipeIt == pipes_.end())
        {
            return;
        }

        const float t = rng.NextFloat01();
        pipeIt->x = config.spawnX;
        pipeIt->gapCenterY = config.gapCenterMinY + (config.gapCenterMaxY - config.gapCenterMinY) * t;
        pipeIt->active = true;
        pipeIt->scored = false;
        Assets::NodeUtils::SetVisible(pipeIt->topNode, true);
        Assets::NodeUtils::SetVisible(pipeIt->bottomNode, true);
        SyncVisual(*pipeIt, config, world);
    }

    void FFlappyCppPipes::SyncVisual(FPipeRuntime& pipe, const FPipeConfig& config, const FWorldConfig& world) const
    {
        const float halfGap = config.gapHeight * 0.5f;
        const float topBottomY = pipe.gapCenterY + halfGap;
        const float bottomTopY = pipe.gapCenterY - halfGap;

        if (pipe.topNode)
        {
            const float topCenterY = (world.maxY + topBottomY) * 0.5f;
            const float topHeight = std::max(0.01f, world.maxY - topBottomY);
            pipe.topNode->SetTranslation(glm::vec3(pipe.x, topCenterY, world.gameplayZ));
            pipe.topNode->SetScale(glm::vec3(1.0f, topHeight, 1.0f));
            pipe.topNode->RecalcTransform(true);
        }

        if (pipe.bottomNode)
        {
            const float bottomCenterY = (world.minY + bottomTopY) * 0.5f;
            const float bottomHeight = std::max(0.01f, bottomTopY - world.minY);
            pipe.bottomNode->SetTranslation(glm::vec3(pipe.x, bottomCenterY, world.gameplayZ));
            pipe.bottomNode->SetScale(glm::vec3(1.0f, bottomHeight, 1.0f));
            pipe.bottomNode->RecalcTransform(true);
        }
    }

    void FFlappyCppPipes::Hide(FPipeRuntime& pipe) const
    {
        pipe.active = false;
        pipe.scored = false;
        pipe.x = 0.0f;
        pipe.gapCenterY = 0.0f;
        if (pipe.topNode)
        {
            pipe.topNode->SetTranslation(glm::vec3(0.0f, hiddenY, 0.0f));
            pipe.topNode->RecalcTransform(true);
            Assets::NodeUtils::SetVisible(pipe.topNode, false);
        }
        if (pipe.bottomNode)
        {
            pipe.bottomNode->SetTranslation(glm::vec3(0.0f, hiddenY, 0.0f));
            pipe.bottomNode->RecalcTransform(true);
            Assets::NodeUtils::SetVisible(pipe.bottomNode, false);
        }
    }
}
