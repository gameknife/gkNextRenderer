#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/Command/ICommand.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace Runtime::Command
{

struct TransformSnapshot
{
    glm::vec3 translation{};
    glm::quat rotation{};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
};

class TransformNodesCommand final : public ICommand
{
public:
    TransformNodesCommand(Assets::Scene& scene, std::vector<uint32_t> instanceIds,
                          std::vector<TransformSnapshot> before, std::vector<TransformSnapshot> after);

    bool Execute() override;
    bool Undo() override;
    std::string GetDescription() const override { return "Transform Nodes"; }

    static bool IsDifferent(const TransformSnapshot& before, const TransformSnapshot& after);
    static bool IsDifferent(const std::vector<TransformSnapshot>& before, const std::vector<TransformSnapshot>& after);

private:
    void Apply(const std::vector<TransformSnapshot>& snapshots);

    Assets::Scene* scene_ = nullptr;
    std::vector<uint32_t> instanceIds_;
    std::vector<TransformSnapshot> before_;
    std::vector<TransformSnapshot> after_;
};

}
