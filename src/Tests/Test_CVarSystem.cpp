#include <catch2/catch_all.hpp>
#include <fmt/format.h>

#include "Engine/Runtime/Config/CVarSystem.hpp"

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

TEST_CASE("CVar metadata exposes type range and modified state", "[Unit][CVar]")
{
    NextCVar::FCVarSystem cvars;
    int32_t quality = 2;
    REQUIRE(cvars.RegisterInt("r.quality", quality, &quality, NextCVar::ECVarFlags::Archive,
                              "Quality", nullptr, 0, 4));

    NextCVar::FCVarInfo info;
    REQUIRE(cvars.TryGetInfo("r.quality", info));
    CHECK(info.type == NextCVar::ECVarType::Int);
    CHECK(info.isDefault);
    CHECK(info.minValue == 0.0);
    CHECK(info.maxValue == 4.0);

    std::string error;
    REQUIRE(cvars.SetValueFromString("r.quality", "9", NextCVar::ECVarSetBy::Console, &error));
    CHECK(quality == 4);
    REQUIRE(cvars.TryGetInfo("r.quality", info));
    CHECK_FALSE(info.isDefault);

    std::vector<std::string> names;
    cvars.ForEach([&](const NextCVar::FCVarInfo& entry) { names.push_back(entry.name); });
    CHECK(names == std::vector<std::string>{"r.quality"});
}

TEST_CASE("CVar change callback only runs for actual value changes", "[Unit][CVar]")
{
    NextCVar::FCVarSystem cvars;
    bool enabled = false;
    int callbackCount = 0;
    REQUIRE(cvars.RegisterBool("r.enabled", false, &enabled, NextCVar::ECVarFlags::None,
                               "Enabled", [&]() { ++callbackCount; }));

    std::string error;
    REQUIRE(cvars.SetValueFromString("r.enabled", "false", NextCVar::ECVarSetBy::Console, &error));
    CHECK(callbackCount == 0);
    REQUIRE(cvars.SetValueFromString("r.enabled", "true", NextCVar::ECVarSetBy::Console, &error));
    CHECK(callbackCount == 1);
    REQUIRE(cvars.SetValueFromString("r.enabled", "true", NextCVar::ECVarSetBy::Console, &error));
    CHECK(callbackCount == 1);
}
