#include "Engine/Runtime/Scene/NodeUtils.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"

namespace Assets::NodeUtils
{
    namespace
    {
        // A model with no vertices keeps the inverted sentinel bounds Model::CalcAABB starts
        // from, and folding those in would blow the union up to +-999999.
        void ExpandByNodeModel(const Assets::Scene& scene, const Assets::Node& node, glm::vec3& outMin,
                               glm::vec3& outMax, bool& outAny)
        {
            const auto* render = node.GetComponent<Runtime::RenderComponent>();
            if (!render)
            {
                return;
            }
            const Assets::Model* model = scene.GetModel(render->GetModelId());
            if (!model)
            {
                return;
            }
            const glm::vec3 localMin = model->GetLocalAABBMin();
            const glm::vec3 localMax = model->GetLocalAABBMax();
            if (glm::any(glm::greaterThan(localMin, localMax)))
            {
                return;
            }

            const glm::mat4& world = node.WorldTransform();
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local((corner & 1) ? localMax.x : localMin.x,
                                      (corner & 2) ? localMax.y : localMin.y,
                                      (corner & 4) ? localMax.z : localMin.z);
                const glm::vec3 worldCorner(world * glm::vec4(local, 1.0f));
                outMin = glm::min(outMin, worldCorner);
                outMax = glm::max(outMax, worldCorner);
            }
            outAny = true;
        }

        void GatherSubtreeWorldBounds(const Assets::Scene& scene, const Assets::Node& node, glm::vec3& outMin,
                                      glm::vec3& outMax, bool& outAny)
        {
            ExpandByNodeModel(scene, node, outMin, outMax, outAny);
            for (const std::shared_ptr<Assets::Node>& child : node.Children())
            {
                if (child)
                {
                    GatherSubtreeWorldBounds(scene, *child, outMin, outMax, outAny);
                }
            }
        }
    }

    void SetVisible(Assets::Node* node, bool visible)
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

    void SetVisibleRecursive(Assets::Node* node, bool visible)
    {
        if (!node)
        {
            return;
        }

        SetVisible(node, visible);
        for (const auto& child : node->Children())
        {
            SetVisibleRecursive(child.get(), visible);
        }
    }

    void SetRayCastVisible(Assets::Node* node, bool visible)
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

    void SetRayCastVisibleRecursive(Assets::Node* node, bool visible)
    {
        if (!node)
        {
            return;
        }

        SetRayCastVisible(node, visible);
        for (const auto& child : node->Children())
        {
            SetRayCastVisibleRecursive(child.get(), visible);
        }
    }

    void SetVisible(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        SetVisible(node.get(), visible);
    }

    void SetVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        SetVisibleRecursive(node.get(), visible);
    }

    void SetRayCastVisible(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        SetRayCastVisible(node.get(), visible);
    }

    void SetRayCastVisibleRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        SetRayCastVisibleRecursive(node.get(), visible);
    }

    bool GetSubtreeWorldBounds(const Assets::Scene& scene, const Assets::Node* node, glm::vec3& outMin,
                               glm::vec3& outMax)
    {
        if (!node)
        {
            return false;
        }

        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
        bool any = false;
        GatherSubtreeWorldBounds(scene, *node, boundsMin, boundsMax, any);
        if (!any)
        {
            return false;
        }

        outMin = boundsMin;
        outMax = boundsMax;
        return true;
    }

    void SetOutlineFlags(const std::shared_ptr<Assets::Node>& node, uint32_t outlineFlags)
    {
        if (!node)
        {
            return;
        }

        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetOutlineFlags(outlineFlags);
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
