#include "Engine/Rendering/AmbientBakeScheduler.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Ambient bake scheduler scales groups from total frame time", "[Unit][AmbientBake]")
{
    SECTION("under target frame time increases the next batch")
    {
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(4, 1.0 / 120.0, 60) > 4u);
    }

    SECTION("over target frame time decreases the next batch")
    {
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(16, 1.0 / 30.0, 60) < 16u);
    }

    SECTION("correction is bounded and never stalls baking")
    {
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(1, 1.0, 60) == 1u);
        CHECK(Vulkan::AmbientBake::PlanNextDispatchGroups(100, 1.0 / 1000.0, 60) == 200u);
    }

    SECTION("target FPS controls the desired frame budget")
    {
        const uint32_t atTenFps = Vulkan::AmbientBake::PlanNextDispatchGroups(8, 1.0 / 60.0, 10);
        const uint32_t atSixtyFps = Vulkan::AmbientBake::PlanNextDispatchGroups(8, 1.0 / 60.0, 60);
        CHECK(atTenFps > atSixtyFps);
    }
}
