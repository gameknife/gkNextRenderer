#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/CommandSystem.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Assets
{
    class Scene;
}

struct TransformSnapshot
{
    glm::vec3 translation{};
    glm::quat rotation{};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

class TransformNodeCommand final : public ICommand
{
public:
    TransformNodeCommand(Assets::Scene& scene, uint32_t instanceId, const TransformSnapshot& before, const TransformSnapshot& after);

    bool Execute() override;
    void Undo() override;
    void Redo() override;
    const char* Name() const override { return "TransformNode"; }

    static bool IsDifferent(const TransformSnapshot& before, const TransformSnapshot& after);

private:
    void Apply(const TransformSnapshot& snapshot);

    Assets::Scene* scene_ = nullptr;
    uint32_t instanceId_ = 0;
    TransformSnapshot before_{};
    TransformSnapshot after_{};
};
