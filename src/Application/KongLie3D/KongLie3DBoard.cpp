#include "KongLie3DBoard.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Loaders/FProcModel.h"
#include "Runtime/Components/RenderComponent.h"

namespace
{
    constexpr int BoardCols = 7;
    constexpr int BoardRows = 8;
    constexpr float CellHalfSize = 0.475f;
    constexpr float CellHeight = 0.05f;

    std::shared_ptr<Assets::Node> CreateBoardCell(const std::string& name,
                                                  const glm::vec3& position,
                                                  uint32_t instanceId,
                                                  uint32_t modelId,
                                                  uint32_t materialId)
    {
        auto node = Assets::Node::CreateNode(name, position, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterial({materialId});
        renderComponent->SetVisible(true);
        node->AddComponent(renderComponent);
        return node;
    }
}

namespace KongLie3D
{
    void BuildBoard(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        models.push_back(Assets::FProcModel::CreateBox(
            glm::vec3(-CellHalfSize, -CellHeight, -CellHalfSize),
            glm::vec3(CellHalfSize, 0.0f, CellHalfSize)));
        const uint32_t cellModelId = static_cast<uint32_t>(models.size() - 1);

        materials.push_back({Assets::Material::Lambertian(glm::vec3(0.78f, 0.80f, 0.84f))});
        const uint32_t lightCellMaterialId = static_cast<uint32_t>(materials.size() - 1);

        materials.push_back({Assets::Material::Lambertian(glm::vec3(0.42f, 0.45f, 0.50f))});
        const uint32_t darkCellMaterialId = static_cast<uint32_t>(materials.size() - 1);

        materials.push_back({Assets::Material::Lambertian(glm::vec3(0.42f, 0.35f, 0.74f))});
        const uint32_t midlineMaterialId = static_cast<uint32_t>(materials.size() - 1);

        for (int row = 0; row < BoardRows; ++row)
        {
            for (int col = 0; col < BoardCols; ++col)
            {
                uint32_t materialId = ((row + col) % 2 == 0) ? lightCellMaterialId : darkCellMaterialId;
                if (row == 4)
                {
                    materialId = midlineMaterialId;
                }

                nodes.push_back(CreateBoardCell(
                    fmt::format("BoardCell_{}_{}", col, row),
                    glm::vec3(static_cast<float>(col), 0.0f, static_cast<float>(row)),
                    static_cast<uint32_t>(nodes.size()),
                    cellModelId,
                    materialId));
            }
        }
    }
}
