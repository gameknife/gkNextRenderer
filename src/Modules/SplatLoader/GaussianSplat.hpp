#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

namespace Assets
{
    struct alignas(16) FGaussianSplatGpu
    {
        glm::vec4 positionOpacity{};
        glm::vec4 covariance0{};
        glm::vec4 covariance1{};
        glm::vec4 sh0{};
        glm::uvec4 metadata{};
    };

    struct FGaussianSplatData
    {
        std::string name;
        std::vector<FGaussianSplatGpu> splats;
        std::vector<glm::vec4> shPalette;
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        uint32_t shBands = 0;
        bool antialias = false;
        bool shBasisFlipXY = false;
    };
}
