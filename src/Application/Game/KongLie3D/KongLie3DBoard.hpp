#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Material.hpp"

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
