#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Rendering/RendererChoices.hpp"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>

TEST_CASE("Renderer choices have one stable capability-filtered catalog", "[Unit][Rendering][RendererChoices]")
{
    using namespace Rendering;
    const auto catalog = RendererChoiceCatalog();
    REQUIRE(catalog.size() == 6);
    CHECK(catalog.front().type == Vulkan::ERT_SoftwareTracing);
    CHECK(catalog.back().type == Vulkan::ERT_PathTracingLite);

    const auto full = AvailableRendererChoices({true, true});
    CHECK(full.size() == catalog.size());

    const auto noRayTracing = AvailableRendererChoices({false, true});
    CHECK(noRayTracing.size() == 4);
    CHECK_FALSE(std::any_of(noRayTracing.begin(), noRayTracing.end(), [](const FRendererChoice* choice)
                            { return choice->type == Vulkan::ERT_PathTracing; }));
    CHECK_FALSE(std::any_of(noRayTracing.begin(), noRayTracing.end(), [](const FRendererChoice* choice)
                            { return choice->type == Vulkan::ERT_PathTracingLite; }));

    const auto lowBudget = AvailableRendererChoices({true, false});
    REQUIRE(lowBudget.size() == 1);
    CHECK(lowBudget.front()->type == Vulkan::ERT_SoftwareModernNoAmbient);
}

TEST_CASE("Renderer choice fallback is deterministic", "[Unit][Rendering][RendererChoices]")
{
    using namespace Rendering;
    CHECK(ResolveRendererChoice(Vulkan::ERT_PathTracing, {false, true}) == Vulkan::ERT_SoftwareTracing);
    CHECK(ResolveRendererChoice(Vulkan::ERT_PathTracingLite, {false, true}) == Vulkan::ERT_SoftwareTracing);
    CHECK(ResolveRendererChoice(Vulkan::ERT_PathTracing, {true, false}) == Vulkan::ERT_SoftwareModernNoAmbient);
    CHECK(ResolveRendererChoice(static_cast<Vulkan::ERendererType>(99), {true, true}) ==
          Vulkan::ERT_SoftwareTracing);

    CHECK(ResolveRendererChoiceDetailed(Vulkan::ERT_PathTracing, {false, true}).reason ==
          ERendererFallbackReason::RayTracingUnavailable);
    CHECK(ResolveRendererChoiceDetailed(Vulkan::ERT_SoftwareModern, {true, false}).reason ==
          ERendererFallbackReason::AmbientCubeBudgetUnavailable);
    CHECK(ResolveRendererChoiceDetailed(static_cast<Vulkan::ERendererType>(99), {true, true}).reason ==
          ERendererFallbackReason::UnknownRenderer);
}
