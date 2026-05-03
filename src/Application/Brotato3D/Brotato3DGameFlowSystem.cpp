#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <imgui.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

bool Brotato3DGameInstance::WorldToScreen(const glm::vec3& world, ImVec2& outScreen) const
{
    const auto& ubo = engine_->GetUniformBufferObject();
    glm::vec4 clip = ubo.ViewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= 0.001f)
    {
        return false;
    }
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    outScreen.x = viewport->Pos.x + (ndc.x * 0.5f + 0.5f) * viewport->Size.x;
    outScreen.y = viewport->Pos.y + (ndc.y * 0.5f + 0.5f) * viewport->Size.y;
    return true;
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
        SetNodeTranslation(player_.bodyNode, player_.worldPos);
        ShowNode(player_.bodyNode);
    }
    if (player_.facingNode)
    {
        SetNodeTranslation(player_.facingNode, glm::vec3(0.0f, 0.62f, -0.45f));
        ShowNode(player_.facingNode);
    }
    if (player_.smgWeaponNode)
    {
        SetNodeTranslation(player_.smgWeaponNode, glm::vec3(-0.18f, 0.42f, -0.54f));
        equippedWeapons_.empty() || equippedWeapons_.front().weaponId != "smg" ? HideNode(player_.smgWeaponNode) :
                                                                                  ShowNode(player_.smgWeaponNode);
    }
    if (player_.shotgunWeaponNode)
    {
        SetNodeTranslation(player_.shotgunWeaponNode, glm::vec3(0.24f, 0.43f, -0.62f));
        equippedWeapons_.empty() || equippedWeapons_.front().weaponId != "shotgun" ? HideNode(player_.shotgunWeaponNode) :
                                                                                     ShowNode(player_.shotgunWeaponNode);
    }

    appState_ = Brotato3D::EAppState::Playing;
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
    Brotato3D::StartBgm("calm");
}

void Brotato3DGameInstance::GoToCharacterSelect()
{
    Brotato3D::PlayUiClickSfx();
    appState_ = Brotato3D::EAppState::CharacterSelect;
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
    appState_ = Brotato3D::EAppState::Result;
    ClearMovementInput();
    if (playerDead)
    {
        ClearAliveEnemies(false);
    }
}

void Brotato3DGameInstance::ApplySelectedArena()
{
    const auto groundIt = arenaResources_.groundMaterialIds.find(selectedArenaId_);
    const auto borderIt = arenaResources_.borderMaterialIds.find(selectedArenaId_);
    if (groundIt == arenaResources_.groundMaterialIds.end() || borderIt == arenaResources_.borderMaterialIds.end())
    {
        return;
    }

    arenaResources_.groundMaterialId = groundIt->second;
    arenaResources_.borderMaterialId = borderIt->second;
    SetNodeMaterial(arenaResources_.groundNode, arenaResources_.groundMaterialId);
    for (const std::shared_ptr<Assets::Node>& node : arenaResources_.borderNodes)
    {
        SetNodeMaterial(node, arenaResources_.borderMaterialId);
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

