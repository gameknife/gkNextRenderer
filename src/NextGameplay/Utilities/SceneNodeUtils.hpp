#pragma once

#include "Common/CoreMinimal.hpp"

class NextPhysics;

namespace Assets
{
    class Node;
    class Scene;
}

namespace Runtime
{
    class SkinnedMeshComponent;
}

namespace NextGameplay
{

    bool HasSkinnedMeshInHierarchy(const std::shared_ptr<Assets::Node>& node);
    std::shared_ptr<Assets::Node> FindAppendedCharacterRoot(const Assets::Scene& scene, const std::string& baseName,
                                                            size_t ordinal,
                                                            const std::shared_ptr<Assets::Node>& exclude = nullptr);
    std::vector<Runtime::SkinnedMeshComponent*> CollectSkinnedMeshComponents(
        const std::shared_ptr<Assets::Node>& root, Runtime::SkinnedMeshComponent** outPrimary = nullptr);
    void SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetNodeRayCastVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void DisableNodePhysicsRecursive(const std::shared_ptr<Assets::Node>& node, NextPhysics* physicsEngine);
}
