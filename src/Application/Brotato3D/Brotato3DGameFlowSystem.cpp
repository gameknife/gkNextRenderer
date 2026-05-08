#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Assets/Core/Node.h"
#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

namespace
{
    constexpr const char* EmptySceneName = "Empty.proc";
}

void Brotato3DGameInstance::RestartGame()
{
    Brotato3D::PlayUiClickSfx();
    StartNewRun();
}

void Brotato3DGameInstance::StartNewRun()
{
    Brotato3D::StopBgm();
    ResetRuntimeState();
    ApplySelectedCharacter();
    if (player_.bodyNode)
    {
        player_.bodyNode->SetTranslation(player_.worldPos);
        NodeUtils::SetVisible(player_.bodyNode, true);
    }
    if (player_.facingNode)
    {
        player_.facingNode->SetTranslation(glm::vec3(0.0f, 0.62f, -0.45f));
        NodeUtils::SetVisible(player_.facingNode, true);
    }
    if (player_.smgWeaponNode)
    {
        player_.smgWeaponNode->SetTranslation(glm::vec3(-0.18f, 0.42f, -0.54f));
        NodeUtils::SetVisible(player_.smgWeaponNode,
                              !equippedWeapons_.empty() && equippedWeapons_.front().weaponId == "smg");
    }
    if (player_.shotgunWeaponNode)
    {
        player_.shotgunWeaponNode->SetTranslation(glm::vec3(0.24f, 0.43f, -0.62f));
        NodeUtils::SetVisible(player_.shotgunWeaponNode,
                              !equippedWeapons_.empty() && equippedWeapons_.front().weaponId == "shotgun");
    }
    cameraSmoothedTarget_ = glm::vec3(player_.worldPos.x, 0.0f, player_.worldPos.z);

    appState_ = Brotato3D::EAppState::Playing;
    SetWorldPhysicsPaused(false);
    if (sceneReady_)
    {
        waveSystem_.StartGame();
        BeginWaveBanner();
    }
    ClearMovementInput();
}

void Brotato3DGameInstance::GoToMainMenu()
{
    Brotato3D::PlayUiClickSfx();
    Brotato3D::StopBgm();
    ResetRuntimeState();
    appState_ = Brotato3D::EAppState::MainMenu;
    cameraSmoothedTarget_ = glm::vec3(0.0f);
    SetWorldPhysicsPaused(true);
    Brotato3D::StartBgm("calm");
}

void Brotato3DGameInstance::GoToCharacterSelect()
{
    Brotato3D::PlayUiClickSfx();
    appState_ = Brotato3D::EAppState::CharacterSelect;
    cameraSmoothedTarget_ = glm::vec3(0.0f);
    SetWorldPhysicsPaused(true);
    Brotato3D::StartBgm("calm");
    ClearMovementInput();
}

void Brotato3DGameInstance::SelectCharacter(const std::string& characterId)
{
    if (!FindCharacterDef(characterId))
    {
        return;
    }
    Brotato3D::PlayUiClickSfx();
    selectedCharacterId_ = characterId;
    StartNewRun();
}

void Brotato3DGameInstance::SelectArena(const std::string& arenaId)
{
    const auto it = std::find_if(arenaDefs_.begin(), arenaDefs_.end(), [&arenaId](const Brotato3D::FArenaDef& arena)
    {
        return arena.id == arenaId;
    });
    if (it == arenaDefs_.end())
    {
        return;
    }
    Brotato3D::PlayUiClickSfx();
    selectedArenaId_ = arenaId;
    ApplySelectedArena();
}

void Brotato3DGameInstance::PauseGame()
{
    if (appState_ != Brotato3D::EAppState::Playing)
    {
        return;
    }
    Brotato3D::PlayUiClickSfx();
    appState_ = Brotato3D::EAppState::Paused;
    SetWorldPhysicsPaused(true);
    ClearMovementInput();
}

void Brotato3DGameInstance::ResumeGame()
{
    if (appState_ != Brotato3D::EAppState::Paused)
    {
        return;
    }
    Brotato3D::PlayUiClickSfx();
    appState_ = Brotato3D::EAppState::Playing;
    SetWorldPhysicsPaused(false);
    ClearMovementInput();
}

void Brotato3DGameInstance::ExitGame()
{
    Brotato3D::PlayUiClickSfx();
    Brotato3D::StopBgm();
    engine_->RequestClose();
}

void Brotato3DGameInstance::BeginLevelUp()
{
    Brotato3D::PlayLevelUpSfx();
    appState_ = Brotato3D::EAppState::LevelUpPicking;
    SetWorldPhysicsPaused(true);
    ClearMovementInput();
    RollUpgradeChoices();
}

void Brotato3DGameInstance::RollUpgradeChoices()
{
    currentUpgradeChoices_.clear();
    std::vector<Brotato3D::FUpgradeCardDef> remaining = upgradeCards_;
    while (!remaining.empty() && currentUpgradeChoices_.size() < 3)
    {
        int totalWeight = 0;
        for (const auto& card : remaining)
        {
            totalWeight += std::max(1, card.weight);
        }
        std::uniform_int_distribution<int> dist(1, totalWeight);
        int pick = dist(rng_);
        size_t chosenIndex = 0;
        for (size_t index = 0; index < remaining.size(); ++index)
        {
            pick -= std::max(1, remaining[index].weight);
            if (pick <= 0)
            {
                chosenIndex = index;
                break;
            }
        }
        currentUpgradeChoices_.push_back(remaining[chosenIndex]);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
    }
}

void Brotato3DGameInstance::EnterResult(bool playerDead)
{
    Brotato3D::StopBgm();
    playerDead ? Brotato3D::PlayDefeatSfx() : Brotato3D::PlayVictorySfx();
    UpdateBestRecord(playerDead);
    playerDead_ = playerDead;
    bossVictoryDelayMs_ = 0.0f;
    appState_ = Brotato3D::EAppState::Result;
    SetWorldPhysicsPaused(true);
    ClearMovementInput();
    if (playerDead)
    {
        ClearAliveEnemies(false);
    }
}

void Brotato3DGameInstance::ApplySelectedArena()
{
    const Brotato3D::FArenaDef* arenaDef = FindArenaDef(selectedArenaId_);
    if (!arenaDef && !arenaDefs_.empty())
    {
        selectedArenaId_ = arenaDefs_.front().id;
        arenaDef = &arenaDefs_.front();
    }

    arenaHalfExtent_ = arenaDef ? arenaDef->halfExtent : glm::vec2(12.0f, 8.0f);
    waveSystem_.SetArenaHalfExtent(arenaHalfExtent_);

    if (sceneReady_)
    {
        engine_->RequestLoadScene({.filename = EmptySceneName});
    }
}

const Brotato3D::FCharacterDef* Brotato3DGameInstance::FindCharacterDef(const std::string& characterId) const
{
    const auto it = std::find_if(characterDefs_.begin(), characterDefs_.end(), [&characterId](const Brotato3D::FCharacterDef& character)
    {
        return character.id == characterId;
    });
    return it != characterDefs_.end() ? &(*it) : nullptr;
}

const Brotato3D::FArenaDef* Brotato3DGameInstance::FindArenaDef(const std::string& arenaId) const
{
    const auto it = std::find_if(arenaDefs_.begin(), arenaDefs_.end(), [&arenaId](const Brotato3D::FArenaDef& arena)
    {
        return arena.id == arenaId;
    });
    return it != arenaDefs_.end() ? &(*it) : nullptr;
}

