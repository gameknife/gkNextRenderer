#include <catch2/catch_test_macros.hpp>

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Rendering/Atmosphere/SkyIrradianceProjector.hpp"

#include <cstddef>

TEST_CASE("Atmosphere parameter layout matches the Slang contract", "[Unit][Atmosphere]")
{
    STATIC_REQUIRE(alignof(Assets::FAtmosphereParams) == 16);
    STATIC_REQUIRE(sizeof(Assets::FAtmosphereParams) == 160);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, RayleighScattering) == 0);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, MieScattering) == 16);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, OzoneAbsorption) == 48);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, BottomRadius) == 80);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, TransmittanceLutId) == 96);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, AerialPerspectiveMaxDistance) == 112);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, FogInscatteringColor) == 128);
    STATIC_REQUIRE(offsetof(Assets::FAtmosphereParams, FogHeightFalloff) == 144);

    STATIC_REQUIRE(offsetof(Assets::UniformBufferObject, AtmosphereReserved0) ==
                   offsetof(Assets::UniformBufferObject, AtmosphereParams) + sizeof(uint64_t));
    STATIC_REQUIRE(sizeof(Assets::UniformBufferObject) % 16 == 0);
}

TEST_CASE("Atmosphere SH projection has positive finite L0", "[Unit][Atmosphere]")
{
    Assets::AtmosphereSetting source;
    Assets::FAtmosphereParams params{};
    params.RayleighScattering = source.RayleighScattering;
    params.RayleighDensityH = source.RayleighDensityH;
    params.MieScattering = source.MieScattering;
    params.MieDensityH = source.MieDensityH;
    params.MieAbsorption = source.MieAbsorption;
    params.MiePhaseG = source.MiePhaseG;
    params.OzoneAbsorption = source.OzoneAbsorption;
    params.OzoneWidth = source.OzoneWidth;
    params.BottomRadius = source.BottomRadius;
    params.TopRadius = source.TopRadius;
    params.GroundAlbedo = source.GroundAlbedo;
    params.SkyLuminanceScale = 1.0f;

    const auto sh = Rendering::Atmosphere::SkyIrradianceProjector::Project(
        params, glm::normalize(glm::vec3(0.2f, 0.8f, 0.4f)), glm::vec3(500.0f), 0.0f);
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        REQUIRE(std::isfinite(sh.coefficients[channel][0]));
        REQUIRE(sh.coefficients[channel][0] > 0.0f);
    }
}

TEST_CASE("Environment atmosphere features are disabled by default", "[Unit][Atmosphere]")
{
    const Assets::EnvironmentSetting environment;

    REQUIRE_FALSE(environment.AtmosphereEnabled);
    REQUIRE_FALSE(environment.AerialPerspectiveEnabled);
    REQUIRE_FALSE(environment.HeightFogEnabled);
}
