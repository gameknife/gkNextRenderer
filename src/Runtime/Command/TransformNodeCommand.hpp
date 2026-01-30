#pragma once

#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/ICommand.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

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
    bool Undo() override;
    std::string GetDescription() const override { return "Transform Node"; }

    static bool IsDifferent(const TransformSnapshot& before, const TransformSnapshot& after);

private:
    void Apply(const TransformSnapshot& snapshot);

    Assets::Scene* scene_ = nullptr;
    uint32_t instanceId_ = 0;
    TransformSnapshot before_{};
    TransformSnapshot after_{};
};
