#pragma once

#include "NextTotalwarTypes.h"

#include <array>
#include <string>
#include <vector>

namespace NextTotalwar
{
    struct FRegimentDeployment
    {
        int faction = 0;
        size_t unitIndex = 0;
        glm::vec3 position{};
        float facing = 0.0f;
    };

    struct FBattleScenario
    {
        std::string id;
        std::string scene;
        uint64_t seed = 1337;
        int playerFaction = 0;
        int aiFaction = 1;
        std::string aiDifficulty = "normal";
        std::string victoryRule = "eliminate-or-rout";
        int soldiersPerRegiment = 100;
        std::vector<FRegimentDeployment> regiments;
    };

    FBattleScenario LoadBattleProductData(std::array<FUnitDef, 3>& unitDefs,
        const std::string& unitsPath = "assets/configs/nexttotalwar/units.json",
        const std::string& battlePath = "assets/configs/nexttotalwar/battles/greenfield.json");
}
