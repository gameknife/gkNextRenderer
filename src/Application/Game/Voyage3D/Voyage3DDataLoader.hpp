#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>

namespace Voyage3D
{
    struct FPortDef
    {
        std::string id;
        std::string name;
        float lon = 0.0f;
        float lat = 0.0f;
        std::string nation;
        std::string specialtyId;
        glm::vec3 color = glm::vec3(1.0f);
    };

    struct FGoodsDef
    {
        std::string id;
        std::string name;
        int basePrice = 0;
        std::vector<std::string> supplyPorts;
        std::vector<std::string> demandPorts;
    };

    struct FShipDef
    {
        std::string id;
        std::string name;
        int hp = 0;
        int cargoMax = 0;
        int cannonCount = 0;
        float speedKnots = 0.0f;
        int price = 0;
        glm::vec3 size = glm::vec3(1.0f);
        float sailHeight = 1.0f;
    };

    struct FLandmassBlock
    {
        std::string name;
        glm::vec3 min = glm::vec3(0.0f);
        glm::vec3 max = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
    };

    struct FEventDef
    {
        std::string id;
        int weight = 0;
        std::string effect;
        std::string enemyShip;
        int rewardGold = 0;
        std::string messageKey;
        std::string rewardCargoId;
        int rewardCargoQty = 0;
        int hpDamage = 0;
        int speedDebuffMs = 0;
    };

    std::vector<FPortDef> LoadPorts(const std::string& path);
    std::vector<FGoodsDef> LoadGoods(const std::string& path);
    std::vector<FShipDef> LoadShips(const std::string& path);
    std::vector<FLandmassBlock> LoadLandmass(const std::string& path);
    std::vector<FEventDef> LoadEvents(const std::string& path);
    std::map<std::string, std::string> LoadPortLore(const std::string& path);
}
