#include "Engine/Common/CoreMinimal.hpp"
#include "Application/Game/AirportSim/StructuredDecisionContract.hpp"
#include "Application/Game/StudioSim/StructuredDecisionContract.hpp"

#include <catch2/catch_all.hpp>

TEST_CASE("StudioSim structured decisions reject invalid output deterministically", "[Unit][AI][StudioSim]")
{
    CHECK_FALSE(StudioSim::ParseStructuredDecision("not json").valid);
    CHECK_FALSE(StudioSim::ParseStructuredDecision(
                    R"json({"action":"DELETE","target_poi":"desk","target_employee":"","dialogue":"","mood":"calm","duration_minutes":30})json")
                    .valid);

    const auto valid = StudioSim::ParseStructuredDecision(
        R"json({"action":"WORK","target_poi":"desk_engineer_01","target_employee":"","dialogue":"","mood":"focused","duration_minutes":90})json");
    REQUIRE(valid.valid);
    CHECK(valid.durationMinutes == 60);

    const std::string fifteenChineseCharacters = "一二三四五六七八九十一二三四五";
    const auto chineseDialogue = StudioSim::ParseStructuredDecision(fmt::format(
        R"json({{"action":"TALK","target_poi":"desk","target_employee":"小王","dialogue":"{}","mood":"calm","duration_minutes":30}})json",
        fifteenChineseCharacters));
    CHECK(chineseDialogue.valid);
    CHECK_FALSE(StudioSim::ParseStructuredDecision(fmt::format(
                    R"json({{"action":"TALK","target_poi":"desk","target_employee":"小王","dialogue":"{}六","mood":"calm","duration_minutes":30}})json",
                    fifteenChineseCharacters))
                    .valid);
}

TEST_CASE("AirportSim structured decisions reject invalid output deterministically", "[Unit][AI][AirportSim]")
{
    CHECK_FALSE(AirportSim::ParseStructuredDecision("{broken").valid);
    CHECK_FALSE(AirportSim::ParseStructuredDecision(
                    R"json({"action":"teleport","target":"gate_01","say":"","mood":"neutral"})json")
                    .valid);
    CHECK_FALSE(AirportSim::ParseStructuredDecision(
                    R"json({"action":"idle","target":"","say":"","mood":"worried"})json")
                    .valid);
    CHECK_FALSE(AirportSim::ParseStructuredDecision(
                    R"json({"action":"goto","target":"gate_01","say":"this message exceeds twenty characters","mood":"neutral"})json")
                    .valid);

    const auto valid = AirportSim::ParseStructuredDecision(
        R"json({"action":"goto","target":"gate_01","say":"登机去","mood":"excited"})json");
    REQUIRE(valid.valid);
    CHECK(valid.target == "gate_01");

    const std::string twentyChineseCharacters = "一二三四五六七八九十一二三四五六七八九十";
    const auto chineseDialogue = AirportSim::ParseStructuredDecision(fmt::format(
        R"json({{"action":"say_to","target":"王安检","say":"{}","mood":"annoyed"}})json",
        twentyChineseCharacters));
    REQUIRE(chineseDialogue.valid);
    CHECK(chineseDialogue.target == "王安检");
    CHECK(chineseDialogue.mood == AirportSim::EMood::Annoyed);
    CHECK_FALSE(AirportSim::ParseStructuredDecision(fmt::format(
                    R"json({{"action":"say_to","target":"王安检","say":"{}一","mood":"anxious"}})json",
                    twentyChineseCharacters))
                    .valid);
}
