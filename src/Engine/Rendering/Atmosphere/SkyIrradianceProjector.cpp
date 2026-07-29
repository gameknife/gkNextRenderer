#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Atmosphere/SkyIrradianceProjector.hpp"

namespace Rendering::Atmosphere
{
    namespace
    {
        constexpr float pi = glm::pi<float>();
        constexpr float goldenAngle = 2.39996322972865332f;

        float RayleighPhase(float cosine)
        {
            return 3.0f * (1.0f + cosine * cosine) / (16.0f * pi);
        }

        float MiePhase(float cosine, float g)
        {
            const float g2 = g * g;
            return (1.0f - g2) /
                (4.0f * pi * std::max(std::pow(1.0f + g2 - 2.0f * g * cosine, 1.5f), 1.0e-4f));
        }

        glm::vec3 UnitRadiance(
            const Assets::FAtmosphereParams& params,
            glm::vec3 direction,
            glm::vec3 sunDirection,
            float cameraAltitudeKm)
        {
            if (direction.y < 0.0f)
            {
                direction.y = -direction.y;
                return params.GroundAlbedo * UnitRadiance(
                    params, direction, sunDirection, cameraAltitudeKm) / pi;
            }

            const float atmosphereHeight = std::max(
                params.TopRadius - params.BottomRadius - std::max(cameraAltitudeKm, 0.0f), 0.001f);
            const float pathLength = atmosphereHeight / std::max(direction.y, 0.05f);
            const float rayleighDensity =
                std::exp(-std::max(cameraAltitudeKm, 0.0f) / std::max(params.RayleighDensityH, 0.001f));
            const float mieDensity =
                std::exp(-std::max(cameraAltitudeKm, 0.0f) / std::max(params.MieDensityH, 0.001f));
            const glm::vec3 scattering =
                params.RayleighScattering * rayleighDensity +
                params.MieScattering * mieDensity;
            const glm::vec3 extinction = scattering +
                params.MieAbsorption * mieDensity + params.OzoneAbsorption;
            const glm::vec3 viewTransmittance = glm::exp(-extinction * pathLength);

            const float sunAirMass = 1.0f / std::max(sunDirection.y, 0.05f);
            const glm::vec3 sunOpticalDepth =
                params.RayleighScattering * params.RayleighDensityH * rayleighDensity +
                (params.MieScattering + params.MieAbsorption) * params.MieDensityH * mieDensity +
                params.OzoneAbsorption * params.OzoneWidth;
            const glm::vec3 sunTransmittance = sunDirection.y > -0.05f
                ? glm::exp(-sunOpticalDepth * sunAirMass)
                : glm::vec3(0.0f);
            const float cosine = glm::dot(direction, sunDirection);
            const glm::vec3 phaseScattering =
                params.RayleighScattering * rayleighDensity * RayleighPhase(cosine) +
                params.MieScattering * mieDensity * MiePhase(cosine, params.MiePhaseG);
            return (glm::vec3(1.0f) - viewTransmittance) *
                phaseScattering / glm::max(extinction, glm::vec3(1.0e-4f)) * sunTransmittance;
        }

        std::array<float, 9> Basis(glm::vec3 direction)
        {
            const float x = direction.x;
            const float y = direction.y;
            const float z = direction.z;
            return {
                0.2820947918f,
                0.4886025119f * y,
                0.4886025119f * z,
                0.4886025119f * x,
                1.0925484306f * x * y,
                1.0925484306f * y * z,
                0.3153915653f * (3.0f * z * z - 1.0f),
                1.0925484306f * x * z,
                0.5462742153f * (x * x - y * y),
            };
        }
    }

    Assets::SphericalHarmonics SkyIrradianceProjector::Project(
        const Assets::FAtmosphereParams& params,
        const glm::vec3& sunDirection,
        const glm::vec3& sunIrradiance,
        float cameraAltitudeKm,
        uint32_t directionCount)
    {
        Assets::SphericalHarmonics result{};
        directionCount = std::max(directionCount, 1u);
        const glm::vec3 normalizedSun = glm::normalize(sunDirection);
        const float sampleWeight = 4.0f * pi / static_cast<float>(directionCount);
        for (uint32_t i = 0; i < directionCount; ++i)
        {
            const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) /
                static_cast<float>(directionCount);
            const float radius = std::sqrt(std::max(1.0f - y * y, 0.0f));
            const float azimuth = goldenAngle * static_cast<float>(i);
            const glm::vec3 direction(radius * std::cos(azimuth), y, radius * std::sin(azimuth));
            const glm::vec3 radiance =
                UnitRadiance(params, direction, normalizedSun, cameraAltitudeKm) *
                sunIrradiance * params.SkyLuminanceScale;
            const auto basis = Basis(direction);
            for (uint32_t coefficient = 0; coefficient < basis.size(); ++coefficient)
            {
                for (uint32_t channel = 0; channel < 3; ++channel)
                {
                    result.coefficients[channel][coefficient] +=
                        radiance[channel] * basis[coefficient] * sampleWeight;
                }
            }
        }
        return result;
    }
}
