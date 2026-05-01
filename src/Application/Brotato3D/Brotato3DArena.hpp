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
    struct FArenaResources
    {
        uint32_t groundModelId = 0;
        uint32_t groundMaterialId = 0;
    };

    void BuildArena(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    FArenaResources& outResources);
}
