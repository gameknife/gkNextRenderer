#include <catch2/catch_all.hpp>

#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include <glm/gtc/matrix_transform.hpp>

TEST_CASE("Upscaler mode mapping is centralized", "[Unit][Upscaler]")
{
    using namespace Rendering::Upscaler;

    CHECK(GetUpscaleModeInfo(0).mode == EUpscaleMode::Quality);
    CHECK(GetUpscaleModeInfo(1).mode == EUpscaleMode::Balanced);
    CHECK(GetUpscaleModeInfo(2).mode == EUpscaleMode::Performance);
    CHECK(GetUpscaleModeInfo(3).mode == EUpscaleMode::UltraPerformance);
    CHECK(GetUpscaleModeInfo(4).mode == EUpscaleMode::Native);
    CHECK(GetUpscaleModeInfo(5).mode == EUpscaleMode::Auto);
    CHECK(GetUpscaleModeInfo(99).mode == EUpscaleMode::Quality);
}

TEST_CASE("Upscaler auto mode favors DLAA at Full HD", "[Unit][Upscaler]")
{
    using namespace Rendering::Upscaler;

    const auto fullHd = ResolveUpscaleMode(5, {1920, 1080});
    CHECK(fullHd.enabled);
    CHECK(fullHd.mode == static_cast<uint32_t>(EUpscaleMode::Native));

    const auto smallWindow = ResolveUpscaleMode(5, {1600, 900});
    CHECK(smallWindow.enabled);
    CHECK(smallWindow.mode == static_cast<uint32_t>(EUpscaleMode::Native));

    const auto ultrawideBelowFullHdPixels = ResolveUpscaleMode(5, {2560, 720});
    CHECK(ultrawideBelowFullHdPixels.enabled);
    CHECK(ultrawideBelowFullHdPixels.mode == static_cast<uint32_t>(EUpscaleMode::Native));

    const auto quadHd = ResolveUpscaleMode(5, {2560, 1440});
    CHECK(quadHd.enabled);
    CHECK(quadHd.mode == static_cast<uint32_t>(EUpscaleMode::Quality));

    const auto explicitQuality = ResolveUpscaleMode(0, {1280, 720});
    CHECK(explicitQuality.enabled);
    CHECK(explicitQuality.mode == static_cast<uint32_t>(EUpscaleMode::Quality));
}

TEST_CASE("Upscaler fallback render extent stays valid", "[Unit][Upscaler]")
{
    using namespace Rendering::Upscaler;

    CHECK(ScaleExtent({1920, 1080}, GetUpscaleModeInfo(0).fallbackScale).width == 1280);
    CHECK(ScaleExtent({1920, 1080}, GetUpscaleModeInfo(2).fallbackScale).height == 540);
    CHECK(ScaleExtent({1, 1}, GetUpscaleModeInfo(3).fallbackScale).width == 1);
    CHECK(ScaleExtent({1, 1}, GetUpscaleModeInfo(3).fallbackScale).height == 1);
}

namespace
{
    void CheckIdentity(const glm::mat4& matrix, float margin = 0.0001f)
    {
        for (int column = 0; column < 4; ++column)
        {
            for (int row = 0; row < 4; ++row)
            {
                CHECK(matrix[column][row] == Catch::Approx(column == row ? 1.0f : 0.0f).margin(margin));
            }
        }
    }
}

TEST_CASE("Upscaler reprojection transforms are inverse pairs", "[Unit][Upscaler]")
{
    using namespace Rendering::Upscaler;

    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    SECTION("stationary camera")
    {
        const glm::mat4 view = glm::lookAtRH(
            glm::vec3(0.0f, 2.0f, 5.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const auto transforms = CalculateReprojectionTransforms(projection, view, projection * view);

        CheckIdentity(transforms.clipToPrevClip);
        CheckIdentity(transforms.prevClipToClip);
    }

    SECTION("translated and rotated camera")
    {
        const glm::mat4 previousView = glm::lookAtRH(
            glm::vec3(-2.0f, 1.0f, 6.0f),
            glm::vec3(0.0f, 0.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 currentView = glm::lookAtRH(
            glm::vec3(1.0f, 3.0f, 4.0f),
            glm::vec3(1.0f, 0.5f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f));
        const auto transforms =
            CalculateReprojectionTransforms(projection, currentView, projection * previousView);

        CheckIdentity(transforms.prevClipToClip * transforms.clipToPrevClip, 0.002f);
        CheckIdentity(transforms.clipToPrevClip * transforms.prevClipToClip, 0.002f);
    }
}

TEST_CASE("Upscaler motion vectors use pixel-space normalization", "[Unit][Upscaler]")
{
    using namespace Rendering::Upscaler;

    const glm::vec2 scale = CalculateMotionVectorScale({1280, 720});
    CHECK(scale.x == Catch::Approx(1.0f / 1280.0f));
    CHECK(scale.y == Catch::Approx(1.0f / 720.0f));

    const glm::vec2 clampedScale = CalculateMotionVectorScale({0, 0});
    CHECK(clampedScale.x == 1.0f);
    CHECK(clampedScale.y == 1.0f);
}
TEST_CASE("Upscaler types have one stable ordered selection", "[Unit][Upscaler]")
{
    using namespace Rendering::Upscaler;

    CHECK(GetUpscalerTypeInfo(0).type == EUpscalerType::None);
    CHECK(GetUpscalerTypeInfo(1).type == EUpscalerType::DLSS);
    CHECK(GetUpscalerTypeInfo(2).type == EUpscalerType::DLSSRayReconstruction);
    CHECK(GetUpscalerTypeInfo(3).type == EUpscalerType::FidelityFXFSR);
    CHECK(GetUpscalerTypeInfo(4).type == EUpscalerType::NativeTAAU);
    CHECK(GetUpscalerTypeInfo(5).type == EUpscalerType::SnapdragonGSR2);
    CHECK(GetUpscalerTypeInfo(99).type == EUpscalerType::None);
    CHECK(std::string_view(GetUpscalerTypeInfo(4).stableId) == "native-taau");
    CHECK(std::string_view(GetUpscaleModeInfo(5).stableId) == "auto");

    const FUpscalerTypeMask nativeTypes =
        UpscalerTypeBit(EUpscalerType::NativeTAAU) |
        UpscalerTypeBit(EUpscalerType::SnapdragonGSR2);
    CHECK(SupportsUpscalerType(nativeTypes, EUpscalerType::NativeTAAU));
    CHECK(SupportsUpscalerType(nativeTypes, EUpscalerType::SnapdragonGSR2));
    CHECK_FALSE(SupportsUpscalerType(nativeTypes, EUpscalerType::FidelityFXFSR));
}
