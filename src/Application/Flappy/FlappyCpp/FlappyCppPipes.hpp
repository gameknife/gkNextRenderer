#pragma once

#include "Application/Flappy/FlappyCommon.hpp"
#include "Application/Flappy/FlappyCpp/FlappyCppRng.hpp"

namespace Assets
{
    class Node;
}

namespace Flappy
{
    struct FPipeRuntime
    {
        float x = 0.0f;
        float gapCenterY = 0.0f;
        bool active = false;
        bool scored = false;
        std::shared_ptr<Assets::Node> topNode;
        std::shared_ptr<Assets::Node> bottomNode;
    };

    class FFlappyCppPipes final
    {
    public:
        void Reset(const FPipeConfig& config);
        void SetNodes(size_t index, std::shared_ptr<Assets::Node> topNode, std::shared_ptr<Assets::Node> bottomNode);
        void Update(float fixedDeltaSeconds, const FPipeConfig& config, const FWorldConfig& world, FXorShift32& rng);
        bool CheckCollision(const glm::vec3& birdPosition, float birdRadius, const FPipeConfig& config) const;
        int ConsumeScoreEvents(float birdX);

        const std::vector<FPipeRuntime>& GetPipes() const { return pipes_; }

    private:
        void SpawnPipe(const FPipeConfig& config, const FWorldConfig& world, FXorShift32& rng);
        void SyncVisual(FPipeRuntime& pipe, const FPipeConfig& config, const FWorldConfig& world) const;
        void Hide(FPipeRuntime& pipe) const;

        std::vector<FPipeRuntime> pipes_;
        float spawnTimer_ = 0.0f;
    };
}
