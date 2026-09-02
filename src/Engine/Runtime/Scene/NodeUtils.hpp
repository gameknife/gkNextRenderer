#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

namespace Assets::NodeUtils
{
    // A loader is free to spread one authored object over a whole subtree: the SCAD loader
    // turns every user-module call into its own node, so a pickup's geometry usually lives
    // in children rather than on the node gameplay indexed. Anything that hides, unhides or
    // measures such an object has to walk the subtree, not just touch the root.
    void SetVisible(Assets::Node* node, bool visible);
    void SetVisibleRecursive(Assets::Node* node, bool visible);
    void SetRayCastVisible(Assets::Node* node, bool visible);
    void SetRayCastVisibleRecursive(Assets::Node* node, bool visible);

    void SetVisible(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetRayCastVisible(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetRayCastVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetOutlineFlags(const std::shared_ptr<Assets::Node>& node, uint32_t outlineFlags);

    /// World-space AABB over every renderable model in the subtree rooted at `node`.
    /// Follows the child pointers instead of Scene::GetNodeBounds' instance-id round trip,
    /// so a loader that hands out colliding ids cannot pull a foreign node into the bounds.
    /// False when nothing in the subtree draws; the outputs are then left untouched.
    bool GetSubtreeWorldBounds(const Assets::Scene& scene, const Assets::Node* node, glm::vec3& outMin,
                               glm::vec3& outMax);

    void SetPrimaryMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId);
    void SetAllMaterials(const std::shared_ptr<Assets::Node>& node, const std::array<uint32_t, 16>& materialIds);
    void SetMaterialRecursive(const std::shared_ptr<Assets::Node>& node, uint32_t materialId);
}
