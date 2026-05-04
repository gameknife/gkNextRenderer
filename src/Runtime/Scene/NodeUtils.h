#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

namespace Assets
{
    class Node;
}

namespace NodeUtils
{
    void SetVisible(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetRayCastVisible(const std::shared_ptr<Assets::Node>& node, bool visible);
    void SetRayCastVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible);

    void SetPrimaryMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId);
    void SetAllMaterials(const std::shared_ptr<Assets::Node>& node, const std::array<uint32_t, 16>& materialIds);
    void SetMaterialRecursive(const std::shared_ptr<Assets::Node>& node, uint32_t materialId);
}
