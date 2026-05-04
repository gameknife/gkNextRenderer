#include "Runtime/Scene/NodeUtils.h"

#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"

namespace NodeUtils
{
    void SetVisible(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        if (!node)
        {
            return;
        }

        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetVisible(visible);
        }
    }

    void SetVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        if (!node)
        {
            return;
        }

        SetVisible(node, visible);
        for (const auto& child : node->Children())
        {
            SetVisibleRecursive(child, visible);
        }
    }

    void SetRayCastVisible(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        if (!node)
        {
            return;
        }

        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetRayCastVisible(visible);
        }
    }

    void SetRayCastVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        if (!node)
        {
            return;
        }

        SetRayCastVisible(node, visible);
        for (const auto& child : node->Children())
        {
            SetRayCastVisibleRecursive(child, visible);
        }
    }

    void SetPrimaryMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId)
    {
        if (!node)
        {
            return;
        }

        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            auto materials = render->GetMaterials();
            materials[0] = materialId;
            render->SetMaterials(materials);
        }
    }

    void SetAllMaterials(const std::shared_ptr<Assets::Node>& node, const std::array<uint32_t, 16>& materialIds)
    {
        if (!node)
        {
            return;
        }

        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetMaterials(materialIds);
        }
    }

    void SetMaterialRecursive(const std::shared_ptr<Assets::Node>& node, uint32_t materialId)
    {
        if (!node)
        {
            return;
        }

        SetPrimaryMaterial(node, materialId);
        for (const auto& child : node->Children())
        {
            SetMaterialRecursive(child, materialId);
        }
    }

}
