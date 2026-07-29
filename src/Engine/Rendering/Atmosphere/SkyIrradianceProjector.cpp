#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Atmosphere/SkyIrradianceProjector.hpp"

namespace Rendering::Atmosphere
{
    namespace
    {
        constexpr float pi = glm::pi<float>();
        constexpr float goldenAngle = 2.39996322972865332f;
        constexpr uint32_t integrationStepCount = 16;

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

        float RaySphereNearest(glm::vec3 origin, glm::vec3 direction, float radius)
        {
            const float b = glm::dot(origin, direction);
            const float c = glm::dot(origin, origin) - radius * radius;
            const float discriminant = b * b - c;
            if (discriminant < 0.0f)
            {
                return -1.0f;
            }
            const float root = std::sqrt(std::max(discriminant, 0.0f));
            const float nearDistance = -b - root;
            const float farDistance = -b + root;
            return nearDistance >= 0.0f ? nearDistance : farDistance;
        }

        float OzoneDensity(const Assets::FAtmosphereParams& params, float altitude)
        {
            return std::clamp(
                1.0f - std::abs(altitude - params.OzoneCenterAltitude) /
                    std::max(params.OzoneWidth, 0.001f),
                0.0f, 1.0f);
        }

        glm::vec3 MediumExtinction(const Assets::FAtmosphereParams& params, float altitude)
        {
            altitude = std::max(altitude, 0.0f);
            const float rayleighDensity =
                std::exp(-altitude / std::max(params.RayleighDensityH, 0.001f));
            const float mieDensity =
                std::exp(-altitude / std::max(params.MieDensityH, 0.001f));
            return params.RayleighScattering * rayleighDensity +
                (params.MieScattering + params.MieAbsorption) * mieDensity +
                params.OzoneAbsorption * OzoneDensity(params, altitude);
        }

        glm::vec3 IntegrateTransmittance(
            const Assets::FAtmosphereParams& params,
            glm::vec3 origin,
            glm::vec3 direction,
            float distance)
        {
            const float stepLength = std::max(distance, 0.0f) /
                static_cast<float>(integrationStepCount);
            glm::vec3 opticalDepth(0.0f);
            for (uint32_t step = 0; step < integrationStepCount; ++step)
            {
                const float t = (static_cast<float>(step) + 0.5f) * stepLength;
                const float altitude =
                    glm::length(origin + direction * t) - params.BottomRadius;
                opticalDepth += MediumExtinction(params, altitude) * stepLength;
            }
            return glm::exp(-opticalDepth);
        }

        glm::vec3 UnitRadiance(
            const Assets::FAtmosphereParams& params,
            glm::vec3 direction,
            glm::vec3 sunDirection,
            float cameraAltitudeKm)
        {
            direction = glm::normalize(direction);
            sunDirection = glm::normalize(sunDirection);
            const glm::vec3 origin(
                0.0f,
                params.BottomRadius + std::max(cameraAltitudeKm, 0.001f),
                0.0f);
            const float atmosphereDistance =
                RaySphereNearest(origin, direction, params.TopRadius);
            const float groundDistance =
                RaySphereNearest(origin, direction, params.BottomRadius);
            const bool hitsGround =
                groundDistance > 0.0f &&
                (atmosphereDistance < 0.0f || groundDistance < atmosphereDistance);
            const float viewDistance = hitsGround ? groundDistance : atmosphereDistance;
            if (viewDistance <= 0.0f)
            {
                return glm::vec3(0.0f);
            }

            const float stepLength =
                viewDistance / static_cast<float>(integrationStepCount);
            glm::vec3 transmittance(1.0f);
            glm::vec3 radiance(0.0f);
            const float cosine = glm::dot(direction, sunDirection);
            const float rayleighPhase = RayleighPhase(cosine);
            const float miePhase = MiePhase(cosine, params.MiePhaseG);
            for (uint32_t step = 0; step < integrationStepCount; ++step)
            {
                const float t = (static_cast<float>(step) + 0.5f) * stepLength;
                const glm::vec3 samplePosition = origin + direction * t;
                const float altitude =
                    std::max(glm::length(samplePosition) - params.BottomRadius, 0.0f);
                const float rayleighDensity =
                    std::exp(-altitude / std::max(params.RayleighDensityH, 0.001f));
                const float mieDensity =
                    std::exp(-altitude / std::max(params.MieDensityH, 0.001f));
                const float sunAtmosphereDistance =
                    RaySphereNearest(samplePosition, sunDirection, params.TopRadius);
                const float sunGroundDistance =
                    RaySphereNearest(samplePosition, sunDirection, params.BottomRadius);
                const bool sunOccluded =
                    sunGroundDistance > 0.0f &&
                    (sunAtmosphereDistance < 0.0f ||
                     sunGroundDistance < sunAtmosphereDistance);
                const glm::vec3 sunTransmittance =
                    !sunOccluded && sunAtmosphereDistance > 0.0f
                    ? IntegrateTransmittance(
                        params, samplePosition, sunDirection, sunAtmosphereDistance)
                    : glm::vec3(0.0f);
                const glm::vec3 source =
                    params.RayleighScattering * rayleighDensity * rayleighPhase +
                    params.MieScattering * mieDensity * miePhase;
                radiance += transmittance * sunTransmittance * source * stepLength;
                transmittance *= glm::exp(-MediumExtinction(params, altitude) * stepLength);
            }

            if (hitsGround && sunDirection.y > 0.0f)
            {
                const glm::vec3 surfacePosition = origin + direction * viewDistance;
                const glm::vec3 groundPosition =
                    glm::normalize(surfacePosition) * (params.BottomRadius + 0.001f);
                const float sunDistance =
                    RaySphereNearest(groundPosition, sunDirection, params.TopRadius);
                if (sunDistance > 0.0f)
                {
                    radiance += transmittance * params.GroundAlbedo *
                        IntegrateTransmittance(
                            params, groundPosition, sunDirection, sunDistance) *
                        sunDirection.y / pi;
                }
            }
            return radiance;
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
