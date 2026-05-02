#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Brotato3DUI.hpp"
#include "Utilities/FileHelper.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace Brotato3DUtil;

namespace
{
    constexpr const char* ArenasConfigPath = "assets/configs/brotato3d/arenas.json";
    constexpr const char* I18nConfigPath = "assets/configs/brotato3d/i18n.json";

    std::filesystem::path GetBestRecordPath()
    {
        if (const char* appData = std::getenv("APPDATA"))
        {
            return std::filesystem::path(appData) / "Brotato3D" / "best.json";
        }
        return std::filesystem::current_path() / "Brotato3D" / "best.json";
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<Brotato3DGameInstance>(config, options, engine);
}

Brotato3DGameInstance::Brotato3DGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine),
    engine_(engine)
{
    config.Title = "Brotato3D";
    config.Width = 1920;
    config.Height = 1080;
    config.ForceSDR = true;
    options.Width = 1920;
    options.Height = 1080;
    options.ForceSDR = true;
}

void Brotato3DGameInstance::OnInit()
{
    if (!Brotato3D::LoadEnemies(EnemiesConfigPath, enemyDefs_) ||
        !Brotato3D::LoadWeapons(WeaponsConfigPath, weaponDefs_) ||
        !Brotato3D::LoadUpgrades(UpgradesConfigPath, upgradeCards_) ||
        !Brotato3D::LoadWaves(WavesConfigPath, waveDefs_) ||
        !Brotato3D::LoadShopItems(ShopItemsConfigPath, shopItems_) ||
        !Brotato3D::LoadItems(ItemsConfigPath, itemDefs_) ||
        !Brotato3D::LoadCharacters(CharactersConfigPath, characterDefs_) ||
        !Brotato3D::LoadArenas(ArenasConfigPath, arenaDefs_))
    {
        throw std::runtime_error("Brotato3D failed to load required data");
    }
    Brotato3D::LoadI18n(I18nConfigPath, i18nTexts_);
    LoadBestRecord();

    itemDefsById_.clear();
    for (const Brotato3D::FItemDef& item : itemDefs_)
    {
        itemDefsById_.emplace(item.id, item);
    }
    shop_.SetItems(shopItems_);
    shop_.SetPassiveItems(itemDefs_);
    shop_.SetWeapons(weaponDefs_);
    waveSystem_.LoadWaves(waveDefs_);
    ResetRuntimeState();
    appState_ = Brotato3D::EAppState::MainMenu;
    engine_->SetGraphicsDebugPanelVisible(false);
    engine_->GetUserSettings().ShowOverlay = false;
    engine_->RequestLoadScene("Empty.proc");
}

void Brotato3DGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
    if (!bigFont_)
    {
        const ImWchar* glyphRange = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
        const std::string chineseFont = Utilities::FileHelper::GetPlatformFilePath("assets/fonts/DroidSansFallback.ttf");
        const std::string displayFont = std::filesystem::exists(chineseFont) ?
            chineseFont :
            Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-BoldCondensed.ttf");
        glyphRange = displayFont == chineseFont ? ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon() : glyphRange;
        ImFont* uiFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(displayFont.c_str(), 16, nullptr, glyphRange);
        if (uiFont)
        {
            ImGui::GetIO().FontDefault = uiFont;
        }
        bigFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(displayFont.c_str(), 32, nullptr, glyphRange);
    }
}

void Brotato3DGameInstance::OnTick(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    if (appState_ == Brotato3D::EAppState::Paused ||
        appState_ == Brotato3D::EAppState::MainMenu ||
        appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        engine_->GetScene().MarkTransformDirty();
        return;
    }

    screenShakeMs_ = std::max(0.0f, screenShakeMs_ - deltaMs);
    if (screenShakeMs_ <= 0.0f)
    {
        screenShakeIntensity_ = 0.0f;
    }
    damageFlashMs_ = std::max(0.0f, damageFlashMs_ - deltaMs);
    weaponMergeBannerMs_ = std::max(0.0f, weaponMergeBannerMs_ - deltaMs);
    UpdateWaveBanner(deltaSeconds);

    if (appState_ == Brotato3D::EAppState::Playing)
    {
        runElapsedSec_ += static_cast<float>(deltaSeconds);
        UpdatePlayer(deltaSeconds);
        UpdateWeapons(deltaSeconds);
        UpdateProjectiles(deltaSeconds);
        UpdateEnemies(deltaSeconds);
        UpdateEnemyProjectiles(deltaSeconds);
        UpdatePickups(deltaSeconds);
        ProcessItemTriggers(deltaSeconds);

        waveSystem_.Update(deltaSeconds, [this](const std::string& enemyId, glm::vec3 pos)
        {
            SpawnEnemy(enemyId, pos);
        });
        if (waveSystem_.ConsumeWaveEnded())
        {
            ClearAliveEnemies(false);
        }
        if (waveSystem_.ConsumeIntermissionStarted())
        {
            StartShopping();
        }
        if (waveSystem_.ConsumeVictory())
        {
            EnterResult(false);
        }
    }
    else if (appState_ == Brotato3D::EAppState::Hitstop)
    {
        hitStopMs_ -= deltaMs;
        if (hitStopMs_ <= 0.0f)
        {
            hitStopMs_ = 0.0f;
            appState_ = Brotato3D::EAppState::Playing;
        }
    }

    UpdateImpactDebris(deltaSeconds);
    UpdateCombatEffects(deltaSeconds);
    UpdateFloatingTexts(deltaSeconds);
    engine_->GetScene().MarkTransformDirty();
}

void Brotato3DGameInstance::OnDestroy()
{
    enemies_.clear();
    projectilePool_.clear();
    enemyProjectilePool_.clear();
    impactDebrisPool_.clear();
    pickupPool_.clear();
    floatingTexts_.clear();
    muzzleFlashes_.clear();
    explosionRings_.clear();
    laserBeams_.clear();
    playerLightNode_.reset();
    tempLightPool_.clear();
}

const Brotato3D::FItemDef* Brotato3DGameInstance::GetItemDef(const std::string& itemId) const
{
    const auto it = itemDefsById_.find(itemId);
    return it != itemDefsById_.end() ? &it->second : nullptr;
}

const Brotato3D::FCharacterDef* Brotato3DGameInstance::GetSelectedCharacterDef() const
{
    return FindCharacterDef(selectedCharacterId_);
}

Brotato3D::FPlayerStats Brotato3DGameInstance::GetEffectivePlayerStats() const
{
    return GetEffectiveStats();
}

std::string Brotato3DGameInstance::Localize(const std::string& key, const std::string& fallback) const
{
    const auto it = i18nTexts_.find(key);
    if (it != i18nTexts_.end())
    {
        return it->second;
    }
    return fallback.empty() ? key : fallback;
}

void Brotato3DGameInstance::LoadBestRecord()
{
    bestRecord_ = {};
    const std::filesystem::path path = GetBestRecordPath();
    if (!std::filesystem::exists(path))
    {
        return;
    }

    try
    {
        std::ifstream input(path);
        nlohmann::json document;
        input >> document;
        bestRecord_.totalWins = document.value("totalWins", 0);
        bestRecord_.totalKills = document.value("totalKills", 0);
        bestRecord_.fastestCompletionSec = document.value("fastestCompletionSec", 0.0f);
        if (document.contains("characterWins") && document.at("characterWins").is_object())
        {
            for (const auto& [characterId, value] : document.at("characterWins").items())
            {
                bestRecord_.characterWins[characterId] = value.get<int>();
            }
        }
    }
    catch (const std::exception& exception)
    {
        SPDLOG_WARN("[Brotato3D] failed to load best record: {}", exception.what());
        bestRecord_ = {};
    }
}

void Brotato3DGameInstance::SaveBestRecord() const
{
    try
    {
        const std::filesystem::path path = GetBestRecordPath();
        std::filesystem::create_directories(path.parent_path());
        nlohmann::json document;
        document["totalWins"] = bestRecord_.totalWins;
        document["totalKills"] = bestRecord_.totalKills;
        document["fastestCompletionSec"] = bestRecord_.fastestCompletionSec;
        document["characterWins"] = bestRecord_.characterWins;
        std::ofstream output(path);
        output << document.dump(2);
    }
    catch (const std::exception& exception)
    {
        SPDLOG_WARN("[Brotato3D] failed to save best record: {}", exception.what());
    }
}

void Brotato3DGameInstance::UpdateBestRecord(bool playerDead)
{
    bestRecord_.totalKills += killCount_;
    if (!playerDead)
    {
        ++bestRecord_.totalWins;
        ++bestRecord_.characterWins[selectedCharacterId_];
        if (bestRecord_.fastestCompletionSec <= 0.0f || runElapsedSec_ < bestRecord_.fastestCompletionSec)
        {
            bestRecord_.fastestCompletionSec = runElapsedSec_;
        }
    }
    SaveBestRecord();
}

bool Brotato3DGameInstance::CanBuyShopOffer(size_t slotIndex) const
{
    if (slotIndex >= shopOffers_.size())
    {
        return false;
    }

    const Brotato3D::FShopItemDef& item = shopOffers_[slotIndex];
    if (player_.materials < item.cost)
    {
        return false;
    }
    if (item.isPassiveItem)
    {
        return player_.ownedItemIds.size() < 6 &&
               std::find(player_.ownedItemIds.begin(), player_.ownedItemIds.end(), item.id) == player_.ownedItemIds.end();
    }
    if (item.isWeaponCard)
    {
        return CanBuyWeaponCard(item.weaponId);
    }
    return true;
}

std::string Brotato3DGameInstance::GetShopOfferUnavailableReason(size_t slotIndex) const
{
    if (slotIndex >= shopOffers_.size())
    {
        return {};
    }

    const Brotato3D::FShopItemDef& item = shopOffers_[slotIndex];
    if (player_.materials < item.cost)
    {
        return "材料不足";
    }
    if (item.isPassiveItem && player_.ownedItemIds.size() >= 6)
    {
        return "Item 已满";
    }
    if (item.isWeaponCard && !CanBuyWeaponCard(item.weaponId))
    {
        return "槽位已满";
    }
    return {};
}

bool Brotato3DGameInstance::OnRenderUI()
{
    ImGui::Begin("Brotato3D");
    ImGui::Text("Brotato3D MVP - bootstrap OK");
    ImGui::End();

    if (appState_ == Brotato3D::EAppState::MainMenu)
    {
        Brotato3D::RenderMainMenu(*this);
    }
    else if (appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        Brotato3D::RenderCharacterSelect(*this);
    }
    else
    {
        Brotato3D::RenderHUD(*this);
    }

    if (appState_ == Brotato3D::EAppState::LevelUpPicking)
    {
        Brotato3D::RenderUpgradeModal(*this);
    }
    else if (appState_ == Brotato3D::EAppState::Shopping)
    {
        Brotato3D::RenderShopModal(*this);
    }
    else if (appState_ == Brotato3D::EAppState::Paused)
    {
        Brotato3D::RenderPauseModal(*this);
    }
    else if (appState_ == Brotato3D::EAppState::Result)
    {
        Brotato3D::RenderResultModal(*this);
    }
    return false;
}

