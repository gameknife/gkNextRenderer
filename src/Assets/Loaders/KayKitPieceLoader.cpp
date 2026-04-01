#include "Assets/Loaders/KayKitPieceLoader.h"
#include "Assets/Loaders/FSceneLoader.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Core/Node.h"
#include "Assets/Data/Skeleton.hpp"

namespace Assets
{
    std::string KayKitPieceLoader::BuildPath(const std::string& pieceName, const std::string& color)
    {
        // neutral pieces have no color suffix in filename
        if (color == "neutral")
        {
            return fmt::format("assets/models/KayKit_Platformer_Pack/Assets/gltf/neutral/{}.gltf", pieceName);
        }
        return fmt::format("assets/models/KayKit_Platformer_Pack/Assets/gltf/{}/{}_{}.gltf", color, pieceName, color);
    }

    int KayKitPieceLoader::LoadPiece(const std::string& pieceName,
                                     const std::string& color,
                                     std::vector<Model>& models,
                                     std::vector<FMaterial>& materials)
    {
        const std::string path = BuildPath(pieceName, color);
        const uint32_t modelOffset = static_cast<uint32_t>(models.size());
        const uint32_t materialOffset = static_cast<uint32_t>(materials.size());

        // Load into temporary vectors for nodes/lights/tracks/skeletons
        // We only want model and material data
        std::vector<std::shared_ptr<Node>> tmpNodes;
        std::vector<LightObject> tmpLights;
        std::vector<AnimationTrack> tmpTracks;
        std::vector<Skeleton> tmpSkeletons;
        EnvironmentSetting tmpCamera;

        // LoadGLTFScene appends models and materials to the provided vectors directly
        // We pass our real models/materials vectors so the data lands there
        if (!FSceneLoader::LoadGLTFScene(path, tmpCamera, tmpNodes, models, materials,
                                         tmpLights, tmpTracks, tmpSkeletons))
        {
            SPDLOG_ERROR("KayKitPieceLoader: failed to load piece '{}' from '{}'", pieceName, path);
            return -1;
        }

        // Verify we got at least one model
        if (models.size() <= modelOffset)
        {
            SPDLOG_ERROR("KayKitPieceLoader: no model data loaded for piece '{}'", pieceName);
            return -1;
        }

        uint32_t modelId = modelOffset;
        uint32_t materialId = materialOffset;

        // Material deduplication: if we already loaded a material for this color,
        // reuse it and remove the duplicate that was just added
        auto it = colorToMaterialId_.find(color);
        if (it != colorToMaterialId_.end())
        {
            materialId = it->second;
            // Remove the duplicate materials that LoadGLTFScene just added
            if (materials.size() > materialOffset)
            {
                materials.erase(materials.begin() + materialOffset, materials.end());
            }
        }
        else
        {
            // First time loading this color - record the material ID
            colorToMaterialId_[color] = materialId;
        }

        // Get AABB from the loaded model
        glm::vec3 aabbMin = models[modelId].GetLocalAABBMin();
        glm::vec3 aabbMax = models[modelId].GetLocalAABBMax();

        KayKitPiece piece;
        piece.name = pieceName;
        piece.modelId = modelId;
        piece.materialId = materialId;
        piece.aabbMin = aabbMin;
        piece.aabbMax = aabbMax;

        int index = static_cast<int>(pieces_.size());
        pieces_.push_back(std::move(piece));

        SPDLOG_INFO("KayKitPieceLoader: loaded '{}' ({}), model={}, material={}, aabb=({},{},{})~({},{},{})",
                    pieceName, color, modelId, materialId,
                    aabbMin.x, aabbMin.y, aabbMin.z,
                    aabbMax.x, aabbMax.y, aabbMax.z);

        return index;
    }

    glm::vec3 KayKitPieceLoader::GetCollisionExtent(int index) const
    {
        const auto& p = pieces_[index];
        return (p.aabbMax - p.aabbMin);
    }

    glm::vec3 KayKitPieceLoader::GetCollisionOffset(int index) const
    {
        const auto& p = pieces_[index];
        return (p.aabbMax + p.aabbMin) * 0.5f;
    }
}
