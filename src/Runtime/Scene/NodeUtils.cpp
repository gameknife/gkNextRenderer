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

    void SetMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId)
    {
        if (!node)
        {
            return;
        }

        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetMaterial({materialId});
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
            auto materials = render->Materials();
            materials[0] = materialId;
            render->SetMaterial(materials);
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

    void SetTranslation(const std::shared_ptr<Assets::Node>& node, const glm::vec3& translation)
    {
        if (!node)
        {
            return;
        }

        node->SetTranslation(translation);
        node->RecalcTransform(true);
    }

    void SetRotation(const std::shared_ptr<Assets::Node>& node, const glm::quat& rotation)
    {
        if (!node)
        {
            return;
        }

        node->SetRotation(rotation);
        node->RecalcTransform(true);
    }

    void SetScale(const std::shared_ptr<Assets::Node>& node, const glm::vec3& scale)
    {
        if (!node)
        {
            return;
        }

        node->SetScale(scale);
        node->RecalcTransform(true);
    }
}
