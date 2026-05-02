#include "Brotato3DArena.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FProcModel.h"
#include "Brotato3DDataLoader.hpp"
#include "Runtime/Components/RenderComponent.h"

namespace
{
    uint32_t AddLambertMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back({Assets::Material::Lambertian(color)});
        return static_cast<uint32_t>(materials.size() - 1);
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(const std::string& name,
                                                   const glm::vec3& translation,
                                                   uint32_t instanceId,
                                                   uint32_t modelId,
                                                   uint32_t materialId)
    {
        auto node = Assets::Node::CreateNode(name, translation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
                                             instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterial({materialId});
        renderComponent->SetVisible(true);
        node->AddComponent(renderComponent);
        return node;
    }
}

namespace Brotato3D
{
    void BuildArena(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    FArenaResources& outResources,
                    const std::vector<FArenaDef>& arenaDefs,
                    const std::string& selectedArenaId)
    {
        outResources = {};
        const std::vector<FArenaDef> fallbackArenas =
            arenaDefs.empty() ? std::vector<FArenaDef>{{.id = "grassland", .name = "绿野"}} : arenaDefs;
        for (const FArenaDef& arena : fallbackArenas)
        {
            outResources.groundMaterialIds[arena.id] = AddLambertMaterial(materials, arena.groundColor);
            outResources.borderMaterialIds[arena.id] = AddLambertMaterial(materials, arena.borderColor);
        }
        const std::string selectedId =
            outResources.groundMaterialIds.contains(selectedArenaId) ? selectedArenaId : fallbackArenas.front().id;

        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-12.0f, -0.05f, -8.0f),
                                                       glm::vec3(12.0f, 0.0f, 8.0f)));
        outResources.groundModelId = static_cast<uint32_t>(models.size() - 1);
        outResources.groundMaterialId = outResources.groundMaterialIds[selectedId];
        outResources.borderMaterialId = outResources.borderMaterialIds[selectedId];
        outResources.groundNode = CreateRenderNode("Brotato3D_Ground", glm::vec3(0.0f), static_cast<uint32_t>(nodes.size()),
                                                   outResources.groundModelId, outResources.groundMaterialId);
        nodes.push_back(outResources.groundNode);

        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-12.2f, 0.0f, -0.1f), glm::vec3(12.2f, 0.4f, 0.1f)));
        const uint32_t horizontalBoundaryModelId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.1f, 0.0f, -8.2f), glm::vec3(0.1f, 0.4f, 8.2f)));
        const uint32_t verticalBoundaryModelId = static_cast<uint32_t>(models.size() - 1);
        const uint32_t boundaryMaterialId = outResources.borderMaterialId;

        outResources.borderNodes.push_back(CreateRenderNode("Brotato3D_Boundary_N", glm::vec3(0.0f, 0.0f, -8.0f),
                                                            static_cast<uint32_t>(nodes.size()), horizontalBoundaryModelId,
                                                            boundaryMaterialId));
        nodes.push_back(outResources.borderNodes.back());
        outResources.borderNodes.push_back(CreateRenderNode("Brotato3D_Boundary_S", glm::vec3(0.0f, 0.0f, 8.0f),
                                                            static_cast<uint32_t>(nodes.size()), horizontalBoundaryModelId,
                                                            boundaryMaterialId));
        nodes.push_back(outResources.borderNodes.back());
        outResources.borderNodes.push_back(CreateRenderNode("Brotato3D_Boundary_W", glm::vec3(-12.0f, 0.0f, 0.0f),
                                                            static_cast<uint32_t>(nodes.size()), verticalBoundaryModelId,
                                                            boundaryMaterialId));
        nodes.push_back(outResources.borderNodes.back());
        outResources.borderNodes.push_back(CreateRenderNode("Brotato3D_Boundary_E", glm::vec3(12.0f, 0.0f, 0.0f),
                                                            static_cast<uint32_t>(nodes.size()), verticalBoundaryModelId,
                                                            boundaryMaterialId));
        nodes.push_back(outResources.borderNodes.back());
    }
}
