#pragma once

#include "Engine/Assets/AssetsFwd.hpp"

namespace Runtime { class GaussianSplatComponent; }

namespace Assets
{
    void BuildGaussianSplatProxy(const std::shared_ptr<Node>& sourceNode,
                                 const std::shared_ptr<Runtime::GaussianSplatComponent>& component,
                                 std::vector<std::shared_ptr<Node>>& nodes,
                                 std::vector<Model>& models,
                                 std::vector<FMaterial>& materials);
}
