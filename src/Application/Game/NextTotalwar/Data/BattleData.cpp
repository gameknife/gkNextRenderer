#include "Data/BattleData.h"

#include "Engine/Runtime/Utilities/JsonHelpers.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>

namespace NextTotalwar
{
    namespace
    {
        size_t UnitIndex(std::string_view id)
        {
            if (id == "spearman") return 0;
            if (id == "swordsman") return 1;
            if (id == "archer") return 2;
            throw std::runtime_error(fmt::format("NextTotalwar: unknown unit id '{}'", id));
        }

        const nlohmann::json& Required(const nlohmann::json& object, const char* key,
                                       const std::string& path)
        {
            if (!object.contains(key))
                throw std::runtime_error(fmt::format("NextTotalwar: '{}' missing '{}'", path, key));
            return object.at(key);
        }
    }

    FBattleScenario LoadBattleProductData(std::array<FUnitDef, 3>& unitDefs,
                                          const std::string& unitsPath,
                                          const std::string& battlePath)
    {
        const nlohmann::json units = NextJson::LoadFile(unitsPath);
        const auto& unitArray = Required(units, "units", unitsPath);
        if (!unitArray.is_array() || unitArray.size() != unitDefs.size())
            throw std::runtime_error("NextTotalwar: units.json must contain exactly three unit definitions");
        std::array<bool, 3> seen{};
        for (const auto& entry : unitArray)
        {
            const std::string id = Required(entry, "id", unitsPath).get<std::string>();
            const size_t index = UnitIndex(id);
            if (seen[index]) throw std::runtime_error("NextTotalwar: duplicate unit definition " + id);
            seen[index] = true;
            FUnitDef& definition = unitDefs[index];
            definition.marchSpeed = Required(entry, "marchSpeed", unitsPath).get<float>();
            definition.catchUpFactor = Required(entry, "catchUpFactor", unitsPath).get<float>();
            definition.defaultRanks = Required(entry, "defaultRanks", unitsPath).get<int>();
            definition.fileSpacing = Required(entry, "fileSpacing", unitsPath).get<float>();
            definition.rankSpacing = Required(entry, "rankSpacing", unitsPath).get<float>();
            definition.baseMorale = Required(entry, "baseMorale", unitsPath).get<float>();
            definition.canRangedAttack = entry.value("canRangedAttack", false);
            definition.rangedRange = entry.value("rangedRange", 0.0f);
            definition.rangedMinRange = entry.value("rangedMinRange", 0.0f);
            definition.volleyInterval = entry.value("volleyInterval", 0.0f);
            definition.startingAmmo = entry.value("startingAmmo", 0);
            definition.rangedAccuracy = entry.value("rangedAccuracy", 0.0f);
            definition.rangedDamage = entry.value("rangedDamage", 0.0f);
            if (definition.defaultRanks < 4 || definition.marchSpeed <= 0.0f ||
                (definition.canRangedAttack &&
                 (definition.rangedRange <= definition.rangedMinRange ||
                  definition.startingAmmo <= 0 || definition.volleyInterval <= 0.0f)))
                throw std::runtime_error("NextTotalwar: invalid unit tuning for " + id);
        }

        const nlohmann::json battle = NextJson::LoadFile(battlePath);
        FBattleScenario scenario;
        scenario.id = Required(battle, "battle", battlePath).get<std::string>();
        scenario.scene = Required(battle, "scene", battlePath).get<std::string>();
        scenario.seed = Required(battle, "seed", battlePath).get<uint64_t>();
        scenario.playerFaction = Required(battle, "playerFaction", battlePath).get<int>();
        scenario.aiFaction = Required(battle, "aiFaction", battlePath).get<int>();
        scenario.aiDifficulty = Required(battle, "aiDifficulty", battlePath).get<std::string>();
        scenario.victoryRule = Required(battle, "victoryRule", battlePath).get<std::string>();
        scenario.soldiersPerRegiment = Required(battle, "soldiersPerRegiment", battlePath).get<int>();
        const auto& deployments = Required(battle, "regiments", battlePath);
        if (!deployments.is_array() || deployments.empty() || scenario.soldiersPerRegiment <= 0 ||
            scenario.playerFaction == scenario.aiFaction || scenario.aiDifficulty != "normal" ||
            scenario.victoryRule != "eliminate-or-rout")
            throw std::runtime_error("NextTotalwar: battle scenario has no valid regiments");
        scenario.regiments.reserve(deployments.size());
        for (const auto& entry : deployments)
        {
            const auto& position = Required(entry, "position", battlePath);
            if (!position.is_array() || position.size() != 2)
                throw std::runtime_error("NextTotalwar: regiment position must be [x,z]");
            FRegimentDeployment deployment;
            deployment.faction = Required(entry, "faction", battlePath).get<int>();
            deployment.unitIndex = UnitIndex(Required(entry, "unit", battlePath).get<std::string>());
            deployment.position = {position[0].get<float>(), 0.0f, position[1].get<float>()};
            deployment.facing = glm::radians(Required(entry, "facingDegrees", battlePath).get<float>());
            scenario.regiments.push_back(deployment);
        }
        return scenario;
    }
}
