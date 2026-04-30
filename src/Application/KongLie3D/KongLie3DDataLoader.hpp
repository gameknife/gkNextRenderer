#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/ext.hpp>

namespace KongLie3D
{
    struct FPieceDef
    {
        std::string name;
        std::string team;
        std::string role;
        std::string attackType;
        int hp = 0;
        int atk = 0;
        float atkSpeed = 0.0f;
        int range = 0;
        float moveSpeed = 0.0f;
        int maxMana = 0;
        int manaPerAtk = 0;
        int healAmount = 0;
        int healInterval = 0;
        glm::vec3 color = glm::vec3(1.0f);
        bool isHero = false;
        std::string skillWName;
        std::string skillW;
        int skillWCooldownMs = 0;
        std::string skillUltimateName;
        std::string skillUltimate;
    };

    struct FPlacementEntry
    {
        std::string pieceId;
        int col = 0;
        int row = 0;
    };

    struct FPlacementData
    {
        std::vector<FPlacementEntry> player;
        std::vector<FPlacementEntry> enemy;
        std::vector<std::string> bench;
    };

    struct FRelicDef
    {
        std::string id;
        std::string name;
        std::string icon;
        std::vector<std::string> buffs;
        std::string statKey;
        float statVal = 0.0f;
        glm::vec3 color = glm::vec3(1.0f);
    };

    std::map<std::string, FPieceDef> LoadPieces(const std::string& path);
    FPlacementData LoadPlacement(const std::string& path);
    std::vector<FRelicDef> LoadRelics(const std::string& path);
}
