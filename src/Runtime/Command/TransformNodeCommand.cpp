#include "Runtime/Command/TransformNodeCommand.hpp"

#include "Assets/Node.h"
#include "Assets/Scene.hpp"

#include <cmath>

TransformNodeCommand::TransformNodeCommand(Assets::Scene& scene, uint32_t instanceId, const TransformSnapshot& before, const TransformSnapshot& after)
    : scene_(&scene)
    , instanceId_(instanceId)
    , before_(before)
    , after_(after)
{
}

bool TransformNodeCommand::Execute()
{
    Apply(after_);
    return true;
}

bool TransformNodeCommand::Undo()
{
    Apply(before_);
    return true;
}

bool TransformNodeCommand::IsDifferent(const TransformSnapshot& before, const TransformSnapshot& after)
{
    constexpr float kEpsilon = 1e-4f;
    const float translationDelta = glm::length(before.translation - after.translation);
    const float scaleDelta = glm::length(before.scale - after.scale);
    const float rotationDot = std::abs(glm::dot(before.rotation, after.rotation));
    const float rotationDelta = 1.0f - rotationDot;

    return translationDelta > kEpsilon || scaleDelta > kEpsilon || rotationDelta > kEpsilon;
}

void TransformNodeCommand::Apply(const TransformSnapshot& snapshot)
{
    if (scene_ == nullptr)
    {
        return;
    }

    Assets::Node* node = scene_->GetNodeByInstanceId(instanceId_);
    if (node == nullptr)
    {
        return;
    }

    node->SetTranslation(snapshot.translation);
    node->SetRotation(snapshot.rotation);
    node->SetScale(snapshot.scale);
    node->RecalcTransform(true);
    scene_->MarkDirty();
    scene_->GetCPUAccelerationStructure().UpdateBVH(*scene_);
}
