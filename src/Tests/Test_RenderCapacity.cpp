#include "Engine/Options.hpp"

#include <catch2/catch_test_macros.hpp>

using Runtime::Config::ERenderCapacityMode;
using Runtime::Config::FRenderCapacityLimits;

TEST_CASE("Render capacity contracts", "[Unit][RenderCapacity]")
{
    const auto defaults = FRenderCapacityLimits::FromMode(ERenderCapacityMode::Default);
    REQUIRE(defaults.renderProxyCapacity == 65535);
    REQUIRE(defaults.visibilityProxyCapacity == 32767);
    REQUIRE(defaults.primitiveWordCount == 1);
    REQUIRE_FALSE(defaults.IsValidOneBasedProxySlot(0));
    REQUIRE(defaults.IsValidOneBasedProxySlot(32767));
    REQUIRE_FALSE(defaults.IsValidOneBasedProxySlot(32768));

    const auto massive = FRenderCapacityLimits::FromMode(ERenderCapacityMode::Massive);
    REQUIRE(massive.renderProxyCapacity == 262140);
    REQUIRE(massive.visibilityProxyCapacity == 262140);
    REQUIRE(massive.primitiveWordCount == 2);
    REQUIRE(massive.IsValidOneBasedProxySlot(262140));
    REQUIRE_FALSE(massive.IsValidOneBasedProxySlot(262141));
}

TEST_CASE("Render capacity checked byte sizes", "[Unit][RenderCapacity]")
{
    const auto bytes = FRenderCapacityLimits::CheckedByteSize(262140, 224, 1);
    REQUIRE(bytes.has_value());
    REQUIRE(*bytes == 58719360);

    REQUIRE_FALSE(FRenderCapacityLimits::CheckedByteSize(
        std::numeric_limits<uint64_t>::max(), 2, 1).has_value());
    REQUIRE_FALSE(FRenderCapacityLimits::CheckedByteSize(
        std::numeric_limits<uint64_t>::max() / 2, 1, 3).has_value());
}
