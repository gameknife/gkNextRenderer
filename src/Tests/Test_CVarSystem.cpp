#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include "Runtime/Config/CVarSystem.hpp"

TEST_CASE("CVar complete supports case-insensitive prefix and substring lookup", "[Unit][CVar]")
{
    NextCVar::FCVarSystem cvars;
    int32_t bloomQuality = 2;
    bool debugBloom = false;

    REQUIRE(cvars.RegisterInt("r.bloom.quality", bloomQuality, &bloomQuality,
                              NextCVar::ECVarFlags::None, "Bloom quality"));
    REQUIRE(cvars.RegisterBool("show.debugBloom", debugBloom, &debugBloom,
                               NextCVar::ECVarFlags::None, "Bloom debug"));
    REQUIRE(cvars.RegisterFloat("r.exposure", 1.0f, nullptr,
                                NextCVar::ECVarFlags::None, "Exposure"));

    size_t totalMatches = 0;
    auto prefixMatches = cvars.Match("R.BLOOM", {.prefixThenSubstring = true, .limit = 20}, &totalMatches);
    REQUIRE(totalMatches == 1);
    REQUIRE(prefixMatches == std::vector<std::string>{"r.bloom.quality"});

    auto substringMatches = cvars.Match("bloom", {.prefixThenSubstring = true, .limit = 20}, &totalMatches);
    REQUIRE(totalMatches == 2);
    REQUIRE(substringMatches == std::vector<std::string>{"r.bloom.quality", "show.debugBloom"});

    auto commandResult = cvars.ExecuteCommand("cvar.complete bloom");
    REQUIRE(commandResult.success);
    REQUIRE(commandResult.output.size() == 2);
    CHECK(commandResult.output[0] == "r.bloom.quality");
    CHECK(commandResult.output[1] == "show.debugBloom");
}

TEST_CASE("CVar complete reports no matches and respects limit", "[Unit][CVar]")
{
    NextCVar::FCVarSystem cvars;
    for (int32_t i = 0; i < 25; ++i)
    {
        REQUIRE(cvars.RegisterInt(fmt::format("r.test{:02}", i), i, nullptr,
                                  NextCVar::ECVarFlags::None, "Test cvar"));
    }

    auto noMatch = cvars.ExecuteCommand("cvar.complete missing");
    REQUIRE(noMatch.success);
    REQUIRE(noMatch.output == std::vector<std::string>{"(no matches)"});

    auto limited = cvars.ExecuteCommand("cvar.complete r.test");
    REQUIRE(limited.success);
    REQUIRE(limited.output.size() == 21);
    CHECK(limited.output.back() == "... (5 more, refine prefix)");
}
