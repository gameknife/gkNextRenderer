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
    constexpr float BorderHeight = 0.05f;

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

    uint32_t AddLambert(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back({Assets::Material::Lambertian(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)))});
        return static_cast<uint32_t>(materials.size() - 1);
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

        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-4.0f, -0.5f, -5.0f), glm::vec3(4.0f, -0.05f, 5.0f)));
        const uint32_t baseModelId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-1.6f, -0.18f, -0.55f), glm::vec3(1.6f, -0.02f, 0.55f)));
        const uint32_t benchPlatformModelId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.1f, -BorderHeight, -4.1f), glm::vec3(0.1f, 0.0f, 4.1f)));
        const uint32_t borderLongModelId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-3.6f, -BorderHeight, -0.1f), glm::vec3(3.6f, 0.0f, 0.1f)));
        const uint32_t borderWideModelId = static_cast<uint32_t>(models.size() - 1);

        const uint32_t enemyLightCellMaterialId = AddLambert(materials, glm::vec3(0.40f, 0.34f, 0.48f));
        const uint32_t enemyDarkCellMaterialId = AddLambert(materials, glm::vec3(0.24f, 0.20f, 0.34f));
        const uint32_t playerLightCellMaterialId = AddLambert(materials, glm::vec3(0.58f, 0.56f, 0.54f));
        const uint32_t playerDarkCellMaterialId = AddLambert(materials, glm::vec3(0.33f, 0.32f, 0.31f));
        const uint32_t midlineMaterialId = AddLambert(materials, glm::vec3(0.32f, 0.82f, 1.0f));
        const uint32_t borderMaterialId = AddLambert(materials, glm::vec3(0.18f, 0.19f, 0.22f));
        const uint32_t baseMaterialId = AddLambert(materials, glm::vec3(0.12f, 0.13f, 0.16f));
        const uint32_t benchPlatformMaterialId = AddLambert(materials, glm::vec3(0.22f, 0.23f, 0.28f));

        nodes.push_back(CreateBoardCell("BoardBase",
                                        glm::vec3(3.0f, 0.0f, 4.0f),
                                        static_cast<uint32_t>(nodes.size()),
                                        baseModelId,
                                        baseMaterialId));

        nodes.push_back(CreateBoardCell("BenchPlatform",
                                        glm::vec3(1.0f, 0.0f, 8.5f),
                                        static_cast<uint32_t>(nodes.size()),
                                        benchPlatformModelId,
                                        benchPlatformMaterialId));

        nodes.push_back(CreateBoardCell("BoardBorder_West",
                                        glm::vec3(-0.60f, 0.0f, 3.5f),
                                        static_cast<uint32_t>(nodes.size()),
                                        borderLongModelId,
                                        borderMaterialId));
        nodes.push_back(CreateBoardCell("BoardBorder_East",
                                        glm::vec3(6.60f, 0.0f, 3.5f),
                                        static_cast<uint32_t>(nodes.size()),
                                        borderLongModelId,
                                        borderMaterialId));
        nodes.push_back(CreateBoardCell("BoardBorder_North",
                                        glm::vec3(3.0f, 0.0f, -0.60f),
                                        static_cast<uint32_t>(nodes.size()),
                                        borderWideModelId,
                                        borderMaterialId));
        nodes.push_back(CreateBoardCell("BoardBorder_South",
                                        glm::vec3(3.0f, 0.0f, 7.60f),
                                        static_cast<uint32_t>(nodes.size()),
                                        borderWideModelId,
                                        borderMaterialId));

        for (int row = 0; row < BoardRows; ++row)
        {
            for (int col = 0; col < BoardCols; ++col)
            {
                uint32_t materialId = ((row + col) % 2 == 0)
                                          ? (row <= 3 ? enemyLightCellMaterialId : playerLightCellMaterialId)
                                          : (row <= 3 ? enemyDarkCellMaterialId : playerDarkCellMaterialId);
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
