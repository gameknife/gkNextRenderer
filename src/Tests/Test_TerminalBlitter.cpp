#include "Modules/NextTui/TerminalBlitter.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Terminal blitter emits half-block ANSI frame and diffs unchanged cells", "[Tui][TerminalBlitter]")
{
    Runtime::Tui::TerminalBlitter blitter;
    const std::vector<Runtime::Tui::FRgb8> pixels = {
        {255, 0, 0}, {0, 255, 0},
        {0, 0, 255}, {255, 255, 0},
    };

    const std::string first = blitter.EncodeFrame(pixels, 2, 2, 8, 2, "status");
    REQUIRE(first.find("\x1b[2J\x1b[H") != std::string::npos);
    REQUIRE(first.find("\xE2\x96\x80") != std::string::npos);
    REQUIRE(first.find("status") != std::string::npos);

    const std::string second = blitter.EncodeFrame(pixels, 2, 2, 8, 2, "status");
    CHECK(second.empty());

    auto changedPixels = pixels;
    changedPixels[0] = {1, 2, 3};
    const std::string third = blitter.EncodeFrame(changedPixels, 2, 2, 8, 2, "status");
    REQUIRE(third.find("\x1b[1;1H") != std::string::npos);
    REQUIRE(third.find("\xE2\x96\x80") != std::string::npos);
}

TEST_CASE("Terminal blitter truncates status line to terminal width", "[Tui][TerminalBlitter]")
{
    Runtime::Tui::TerminalBlitter blitter;
    const std::vector<Runtime::Tui::FRgb8> pixels = {
        {10, 20, 30}, {40, 50, 60},
        {70, 80, 90}, {100, 110, 120},
    };

    const std::string output = blitter.EncodeFrame(pixels, 2, 2, 4, 2, "abcdef");
    REQUIRE(output.find("abcd") != std::string::npos);
    CHECK(output.find("abcde") == std::string::npos);
}

TEST_CASE("Terminal blitter falls back to full redraw for heavy frame changes", "[Tui][TerminalBlitter]")
{
    Runtime::Tui::TerminalBlitter blitter;
    const std::vector<Runtime::Tui::FRgb8> pixelsA = {
        {255, 0, 0}, {0, 255, 0},
        {0, 0, 255}, {255, 255, 0},
    };
    const std::vector<Runtime::Tui::FRgb8> pixelsB = {
        {0, 0, 0}, {255, 255, 255},
        {255, 0, 255}, {0, 255, 255},
    };

    REQUIRE_FALSE(blitter.EncodeFrame(pixelsA, 2, 2, 8, 2, "status").empty());

    const std::string output = blitter.EncodeFrame(pixelsB, 2, 2, 8, 2, "status");
    REQUIRE(output.rfind("\x1b[H", 0) == 0);
    CHECK(output.find("\x1b[1;1H") == std::string::npos);
}

TEST_CASE("Terminal blitter encodes quadrant cells and reuses unchanged cells", "[Tui][TerminalBlitter]")
{
    Runtime::Tui::TerminalBlitter blitter({
        .CellMode = Runtime::Tui::ECellMode::Quadrant,
    });
    const std::vector<Runtime::Tui::FRgb8> pixels = {
        {255, 0, 0}, {0, 0, 255},
        {0, 0, 255}, {0, 0, 255},
    };

    const std::string first = blitter.EncodeFrame(pixels, 2, 2, 1, 2, "status");
    REQUIRE(first.find("\xE2\x96\x98") != std::string::npos);

    const std::string second = blitter.EncodeFrame(pixels, 2, 2, 1, 2, "status");
    CHECK(second.empty());
}

TEST_CASE("Terminal blitter reports quadrant source extent", "[Tui][TerminalBlitter]")
{
    Runtime::Tui::TerminalBlitter blitter({
        .MaxColumns = 10,
        .MaxRows = 5,
        .CellMode = Runtime::Tui::ECellMode::Quadrant,
    });

    const auto extent = blitter.GetSourceExtent(80, 24);
    CHECK(extent.Width == 20);
    CHECK(extent.Height == 8);

    const auto renderExtent = blitter.GetRenderExtent(80, 24);
    CHECK(renderExtent.Width == 20);
    CHECK(renderExtent.Height == 16);
}
