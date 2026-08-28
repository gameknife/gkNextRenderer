#include "Engine/Assets/Data/RigAsset.hpp"
#include "Modules/NextDotNet/ManagedGameTemplate.hpp"
#include "Modules/ScadLoader/FScadRig.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// The "tps" template plays clips on two shipped rigs by name, from C#. Nothing checks that coupling
// at build time: a renamed or removed clip does not fail to compile, it animates the character to
// its bind pose at runtime, which reads as a broken rig rather than as a missing clip.
//
// This is that check. It reads the names out of the generated sources rather than repeating them,
// so a clip the template stops using stops being required here too.

using namespace Modules::NextDotNet;

namespace
{
    std::string ReadTemplateFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    /// Every string literal passed to a Rig.PlayClip / Rig.HasClip call in the template's sources.
    std::vector<std::string> CollectClipNames(const std::string& source)
    {
        std::vector<std::string> names;
        for (const char* call : {"Rig.PlayClip(", "Rig.HasClip("})
        {
            size_t at = source.find(call);
            while (at != std::string::npos)
            {
                // The clip is the first quoted argument after the call; a call that passes a
                // variable simply has no quote before the closing parenthesis and is skipped.
                const size_t end = source.find(')', at);
                const size_t open = source.find('"', at);
                if (end == std::string::npos || open == std::string::npos || open > end)
                {
                    at = source.find(call, at + 1);
                    continue;
                }
                const size_t close = source.find('"', open + 1);
                if (close != std::string::npos && close < end)
                {
                    names.push_back(source.substr(open + 1, close - open - 1));
                }
                at = source.find(call, at + 1);
            }
        }
        return names;
    }
}

TEST_CASE("the tps template only plays clips its rigs actually have", "[Unit][DotNet][Template][ScadRig]")
{
    const std::vector<FGameTemplate> templates = ScanGameTemplates();
    const auto tps = std::find_if(templates.begin(), templates.end(),
                                  [](const FGameTemplate& t) { return t.id == "tps"; });
    REQUIRE(tps != templates.end());

    // The rigs the template names. Kept here rather than parsed out of the source: these two paths
    // are also what the template's manifest requires ScadLoader for, so they are the contract.
    const std::string playerRigPath = "assets/scad/characters/nextdayz_survivor.scad";
    const std::string enemyRigPath = "assets/scad/characters/nextdayz_infected.scad";

    const std::string gameSource = ReadTemplateFile(tps->directory / "files" / "__ProjectName__Game.cs");
    const std::string squadSource = ReadTemplateFile(tps->directory / "files" / "EnemySquad.cs");
    REQUIRE_FALSE(gameSource.empty());
    REQUIRE_FALSE(squadSource.empty());
    CHECK(gameSource.find(playerRigPath) != std::string::npos);
    CHECK(gameSource.find(enemyRigPath) != std::string::npos);

    Assets::FRigAsset playerRig;
    Assets::FRigAsset enemyRig;
    std::string error;
    REQUIRE(Assets::FScadRigLoader::LoadRig(playerRigPath, {}, playerRig, error));
    INFO(error);
    REQUIRE(Assets::FScadRigLoader::LoadRig(enemyRigPath, {}, enemyRig, error));
    INFO(error);

    // A rig with no parts instantiates as an invisible character that still moves and collides —
    // the failure mode that is hardest to recognise from a screenshot.
    CHECK_FALSE(playerRig.parts.empty());
    CHECK_FALSE(enemyRig.parts.empty());
    CHECK_FALSE(playerRig.bones.empty());
    CHECK_FALSE(enemyRig.bones.empty());

    for (const std::string& clip : CollectClipNames(gameSource))
    {
        INFO("player clip " << clip);
        CHECK(playerRig.FindClip(clip) != nullptr);
    }
    for (const std::string& clip : CollectClipNames(squadSource))
    {
        INFO("enemy clip " << clip);
        CHECK(enemyRig.FindClip(clip) != nullptr);
    }
}
