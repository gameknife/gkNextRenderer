#pragma once

// CPU sky projection used by the optional atmosphere implementation.

#include "Engine/Assets/GPU/UniformBuffer.hpp"

namespace Rendering::Atmosphere
{
    class SkyIrradianceProjector final
    {
    public:
        static Assets::SphericalHarmonics Project(
            const Assets::FAtmosphereParams& params,
            const glm::vec3& sunDirection,
            const glm::vec3& sunIrradiance,
            float cameraAltitudeKm,
            uint32_t directionCount = 128);
    };
}
