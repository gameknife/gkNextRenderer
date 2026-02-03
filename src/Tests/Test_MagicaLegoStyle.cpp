#include <catch2/catch_test_macros.hpp>
#include "Application/MagicaLego/MagicaLegoStyle.hpp"
#include <imgui.h>

TEST_CASE("MagicaLego Style Definitions", "[Style]") {
    // Verify constants exist and are correct types
    REQUIRE(MagicaLego::Style::LegoYellow.w == 1.0f);
    REQUIRE(MagicaLego::Style::GlassBackground.w < 1.0f); // Should be transparent
    REQUIRE(MagicaLego::Style::WindowRounding > 0.0f);
}
