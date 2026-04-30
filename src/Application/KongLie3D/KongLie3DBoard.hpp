#pragma once

#include "Common/CoreMinimal.hpp"
#include "Assets/Core/Model.hpp"
#include "Assets/Data/Material.hpp"

namespace Assets
{
    class Node;
    struct FMaterial;
}

namespace KongLie3D
{
    void BuildBoard(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes);
}
