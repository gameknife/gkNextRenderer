#pragma once

#include "Common/CoreMinimal.hpp"
#include "Assets/Core/Model.hpp"

namespace Assets
{
    class Node;
    struct FMaterial;
}

namespace Brotato3D
{
    struct FArenaDef;

    struct FArenaResources
    {
        uint32_t groundModelId = 0;
        uint32_t groundMaterialId = 0;
        uint32_t borderMaterialId = 0;
        std::shared_ptr<Assets::Node> groundNode;
        std::vector<std::shared_ptr<Assets::Node>> borderNodes;
        std::map<std::string, uint32_t> groundMaterialIds;
        std::map<std::string, uint32_t> borderMaterialIds;
    };

    void BuildArena(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    FArenaResources& outResources,
                    const std::vector<FArenaDef>& arenaDefs,
                    const std::string& selectedArenaId);
}
