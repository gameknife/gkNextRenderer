#include "Modules/DevTools/Command/TransformNodesCommand.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include <algorithm>
#include <cmath>

namespace Runtime::Command
{

TransformNodesCommand::TransformNodesCommand(Assets::Scene& scene, std::vector<uint32_t> instanceIds,
                                             std::vector<TransformSnapshot> before,
                                             std::vector<TransformSnapshot> after)
    : scene_(&scene)
    , instanceIds_(std::move(instanceIds))
    , before_(std::move(before))
    , after_(std::move(after))
{
}

bool TransformNodesCommand::Execute()
{
    Apply(after_);
    return true;
}

bool TransformNodesCommand::Undo()
{
    Apply(before_);
    return true;
}

bool TransformNodesCommand::IsDifferent(const TransformSnapshot& before, const TransformSnapshot& after)
{
    constexpr float kEpsilon = 1e-4f;
    const float translationDelta = glm::length(before.translation - after.translation);
    const float scaleDelta = glm::length(before.scale - after.scale);
    const float rotationDot = std::abs(glm::dot(before.rotation, after.rotation));
    const float rotationDelta = 1.0f - rotationDot;

    return translationDelta > kEpsilon || scaleDelta > kEpsilon || rotationDelta > kEpsilon;
}

bool TransformNodesCommand::IsDifferent(const std::vector<TransformSnapshot>& before,
                                        const std::vector<TransformSnapshot>& after)
{
    if (before.size() != after.size())
    {
        return true;
    }

    for (size_t i = 0; i < before.size(); ++i)
    {
        if (IsDifferent(before[i], after[i]))
        {
            return true;
        }
    }
    return false;
}

void TransformNodesCommand::Apply(const std::vector<TransformSnapshot>& snapshots)
{
    if (scene_ == nullptr)
    {
        return;
    }

    const size_t count = std::min(instanceIds_.size(), snapshots.size());
    bool changed = false;
    for (size_t i = 0; i < count; ++i)
    {
        Assets::Node* node = scene_->GetNodeByInstanceId(instanceIds_[i]);
        if (node == nullptr)
        {
            continue;
        }

        const TransformSnapshot& snapshot = snapshots[i];
        node->SetTranslation(snapshot.translation);
        node->SetRotation(snapshot.rotation);
        node->SetScale(snapshot.scale);
        node->RecalcTransform(true);
        changed = true;
    }

    if (changed)
    {
        scene_->MarkDirty();
    }
}

}
