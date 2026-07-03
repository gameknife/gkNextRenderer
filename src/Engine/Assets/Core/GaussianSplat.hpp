#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <limits>

namespace Assets
{
    struct alignas(16) FGaussianSplatGpu
    {
        glm::vec4 positionOpacity{};
        glm::vec4 covariance0{}; // xx, xy, xz, yy
        glm::vec4 covariance1{}; // yz, zz, unused, unused
        glm::vec4 sh0{};         // SH DC coefficients
        glm::uvec4 metadata{};   // palette label, bands, reserved, reserved
    };

    struct FGaussianSplatData
    {
        std::string name;
        std::vector<FGaussianSplatGpu> splats;
        std::vector<glm::vec4> shPalette; // palette-major, RGB coefficient per vec4
        glm::vec3 aabbMin{0.0f};
        glm::vec3 aabbMax{0.0f};
        uint32_t nodeInstanceId = std::numeric_limits<uint32_t>::max();
        uint32_t shBands = 0;
        bool antialias = false;
        bool shBasisFlipXY = false;
    };
}
