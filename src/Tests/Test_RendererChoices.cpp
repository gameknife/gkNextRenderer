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

    // Every catalog entry builds the full bindless set, so a constrained device offers none of
    // them -- the picker must show an empty list rather than a renderer that cannot start.
    CHECK(AvailableRendererChoices({true, true, false}).empty());
    CHECK_FALSE(IsRendererChoiceAvailable(Vulkan::ERT_SoftwareModernNoAmbient, {true, true, false}));
    // Compatibility is a device verdict, not a user-selectable quality level.
    CHECK(FindRendererChoice(Vulkan::ERT_Compatibility) == nullptr);
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

TEST_CASE("A missing bindless budget resolves to the compatibility renderer",
          "[Unit][Rendering][RendererChoices]")
{
    using namespace Rendering;
    // Outranks everything else: no catalog entry can create its resources on such a device, so
    // the request is irrelevant -- including a request for ray tracing or full ambient cubes.
    CHECK(ResolveRendererChoice(Vulkan::ERT_PathTracing, {true, true, false}) == Vulkan::ERT_Compatibility);
    CHECK(ResolveRendererChoice(Vulkan::ERT_SoftwareModernNoAmbient, {false, false, false}) ==
          Vulkan::ERT_Compatibility);
    CHECK(ResolveRendererChoiceDetailed(Vulkan::ERT_SoftwareModern, {true, true, false}).reason ==
          ERendererFallbackReason::BindlessBudgetUnavailable);
    // Already on Compatibility for the right reason: not reported as a fallback.
    CHECK(ResolveRendererChoiceDetailed(Vulkan::ERT_Compatibility, {true, true, false}).reason ==
          ERendererFallbackReason::None);

    // A capable device never stays on Compatibility, since it is not in the catalog.
    CHECK(ResolveRendererChoice(Vulkan::ERT_Compatibility, {true, true, true}) == Vulkan::ERT_SoftwareTracing);
    CHECK(ResolveRendererChoice(Vulkan::ERT_Compatibility, {false, false, true}) ==
          Vulkan::ERT_SoftwareModernNoAmbient);

    // The default keeps existing two-field call sites meaning "full budget".
    CHECK(FRendererChoiceCapabilities{}.hasFullBindlessBudget);
}
