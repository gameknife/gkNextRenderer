#include "Brotato3DGameInstance.hpp"
#include "Brotato3DAssetPaths.hpp"
#include "Brotato3DAudio.hpp"
#include "Brotato3DCommon.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Brotato3DUI.hpp"
#include "Engine/Runtime/Editor/FontLoader.h"
#include "Engine/Runtime/Platform/UserPaths.h"
#include "Engine/Runtime/Subsystems/NextLocalization.h"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Utilities/FileHelper.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

using namespace Brotato3DUtil;

namespace
{
    constexpr const char* ArenasConfigPath = "assets/configs/brotato3d/arenas.json";
    constexpr const char* I18nConfigPath = "assets/configs/brotato3d/i18n.json";
    constexpr float CameraFollowSharpness = 8.0f;
    constexpr float CameraClampHalfViewX = 12.0f;
    constexpr float CameraClampHalfViewZ = 8.0f;

    std::filesystem::path GetBestRecordPath()
    {
        return NextPlatform::UserPaths::EnsureUserFile("Brotato3D", "best.json");
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    return std::make_unique<Brotato3DGameInstance>(config, options, engine);
}

Brotato3DGameInstance::Brotato3DGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "Brotato3D", 1920, 1080, true);
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
    if (NextLocalization* localization = GetEngine().GetLocalization())
    {
        localization->LoadFromJson(I18nConfigPath, "zh");
    }
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
    ApplySelectedArena();
    appState_ = Brotato3D::EAppState::MainMenu;
    SetWorldPhysicsPaused(true);
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetUserSettings().ShowOverlay = false;
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});

    // Mount brotato3d.pak (SFX + UI icons). Missing is non-fatal: FPackageFileSystem
    // will fall back to the on-disk copies under assets/sounds/brotato3d and
    // assets/textures/brotato3d.
    const std::string brotato3dPakPath =
        Utilities::FileHelper::GetPlatformFilePath("assets/paks/brotato3d.pak");
    if (std::filesystem::exists(brotato3dPakPath))
    {
        GetEngine().GetPakSystem().MountPak(brotato3dPakPath);
    }

    Brotato3D::StartBgm("calm");

    // SFX/icons have been absorbed into formal asset paths, but the underlying binary
    // content is still proprietary Brotato reference material until the planned refresh.
    if (Utilities::FileHelper::IsAssetAvailable(Brotato3D::Assets::Sfx("fire_smg_01.wav")))
    {
        spdlog::warn("[PLACEHOLDER ASSETS] Brotato vendor reference assets detected — DO NOT DISTRIBUTE");
    }
}

void Brotato3DGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
    if (!bigFont_)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        const std::string placeholderUiFont = Brotato3D::PlaceholderAssets::Font("NotoSansSC-Medium.otf");
        const std::string fallbackChineseFont = "assets/fonts/DroidSansFallback.ttf";
        const std::string fallbackLatinFont = "assets/fonts/Roboto-BoldCondensed.ttf";
        const std::string placeholderBigFont = Brotato3D::PlaceholderAssets::Font("Anybody-Medium.ttf");

        std::string displayFont = fallbackLatinFont;
        bool displayIncludesChinese = false;
        if (Utilities::FileHelper::IsAssetAvailable(placeholderUiFont))
        {
            displayFont = placeholderUiFont;
            displayIncludesChinese = true;
        }
        else if (Utilities::FileHelper::IsAssetAvailable(fallbackChineseFont))
        {
            displayFont = fallbackChineseFont;
            displayIncludesChinese = true;
        }

        ImFont* uiFont = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = displayFont,
            .pixelSize = 16.0f,
            .includeChineseFull = displayIncludesChinese,
            .setAsDefault = true,
        });

        const bool hasPlaceholderBigFont = Utilities::FileHelper::IsAssetAvailable(placeholderBigFont);
        const std::string bigFontPath = hasPlaceholderBigFont ? placeholderBigFont : displayFont;
        bigFont_ = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = bigFontPath,
            .pixelSize = 32.0f,
            .includeChineseFull = false,
        });
        if (!bigFont_)
        {
            bigFont_ = uiFont;
        }
    }
}

void Brotato3DGameInstance::OnTick(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    SetWorldPhysicsPaused(ShouldPauseWorldPhysics());
    if (appState_ == Brotato3D::EAppState::Paused ||
        appState_ == Brotato3D::EAppState::MainMenu ||
        appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        GetEngine().GetScene().MarkTransformDirty();
        return;
    }

    screenShakeMs_ = std::max(0.0f, screenShakeMs_ - deltaMs);
    if (screenShakeMs_ <= 0.0f)
    {
        screenShakeIntensity_ = 0.0f;
    }
    damageFlashMs_ = std::max(0.0f, damageFlashMs_ - deltaMs);
    bossKillFlashMs_ = std::max(0.0f, bossKillFlashMs_ - deltaMs);
    weaponMergeBannerMs_ = std::max(0.0f, weaponMergeBannerMs_ - deltaMs);
    UpdateSkyTransition(deltaSeconds);
    UpdateWaveBanner(deltaSeconds);
    UpdateCameraTracking(deltaSeconds);

    if (timeScaleRecoveryMs_ > 0.0f)
    {
        timeScaleRecoveryMs_ = std::max(0.0f, timeScaleRecoveryMs_ - deltaMs);
        globalTimeScale_ = glm::mix(0.4f, 1.0f, 1.0f - timeScaleRecoveryMs_ / 1200.0f);
    }
    else
    {
        globalTimeScale_ = 1.0f;
    }
    const double effectiveDt = deltaSeconds * static_cast<double>(globalTimeScale_);

    if (appState_ == Brotato3D::EAppState::Playing)
    {
        if (bossVictoryDelayMs_ > 0.0f)
        {
            bossVictoryDelayMs_ = std::max(0.0f, bossVictoryDelayMs_ - deltaMs);
            if (bossVictoryDelayMs_ <= 0.0f)
            {
                EnterResult(false);
            }
        }

        if (appState_ == Brotato3D::EAppState::Playing)
        {
            runElapsedSec_ += static_cast<float>(deltaSeconds);
            UpdatePlayer(deltaSeconds);
            UpdateWeapons(deltaSeconds);
            UpdateProjectiles(effectiveDt);
            UpdateEnemies(effectiveDt);
            UpdateEnemyProjectiles(effectiveDt);
            ProcessItemTriggers(effectiveDt);
            UpdateExtractionVehicle(deltaSeconds);

            const double waveDt = bossVictoryDelayMs_ <= 0.0f ? deltaSeconds : 0.0;
            waveSystem_.Update(waveDt, [this](const std::string& enemyId, glm::vec3 pos)
            {
                SpawnEnemy(enemyId, pos);
            });

            if (waveSystem_.ConsumeDuskBegan())
            {
                BeginDuskSurge();
            }
            if (waveSystem_.ConsumeExtractionCompleted())
            {
                SetExtractionVehicleVisible(false);
                SetSkyIntensityTarget(30.0f, 800.0f);
                Brotato3D::PlayWaveStartSfx(waveSystem_.GetCurrentWaveIndex());
            }
            if (waveSystem_.ConsumeWaveEnded())
            {
                ClearAliveEnemies(false);
                ClearAllDebris(false);
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
    }
    else if (appState_ == Brotato3D::EAppState::Hitstop)
    {
        hitStopMs_ -= deltaMs;
        if (hitStopMs_ <= 0.0f)
        {
            hitStopMs_ = 0.0f;
            appState_ = Brotato3D::EAppState::Playing;
            SetWorldPhysicsPaused(false);
        }
    }

    if (appState_ == Brotato3D::EAppState::Playing)
    {
        UpdateDebris(effectiveDt);
        UpdateCombatEffects(effectiveDt);
    }
    UpdateFloatingTexts(deltaSeconds);
    GetEngine().GetScene().MarkTransformDirty();
}

void Brotato3DGameInstance::OnDestroy()
{
    SetWorldPhysicsPaused(false);
    ClearArenaPropBodies();
    ClearArenaWallBodies();
    enemies_.clear();
    projectilePool_.clear();
    enemyProjectilePool_.clear();
    debrisPool_.clear();
    floatingTexts_.clear();
    muzzleFlashes_.clear();
    explosionRings_.clear();
    laserBeams_.clear();
    groundIndicators_.clear();
    ResetExtractionVehicle();
    playerLightNode_.reset();
    tempLightPool_.clear();
    enemyKinematicBodyPools_.clear();
}

void Brotato3DGameInstance::UpdateCameraTracking(double deltaSeconds)
{
    if (appState_ != Brotato3D::EAppState::Playing &&
        appState_ != Brotato3D::EAppState::Hitstop &&
        appState_ != Brotato3D::EAppState::LevelUpPicking &&
        appState_ != Brotato3D::EAppState::Shopping)
    {
        return;
    }

    const glm::vec3 desired(player_.worldPos.x, 0.0f, player_.worldPos.z);
    const float lerpK = 1.0f - std::exp(-CameraFollowSharpness * static_cast<float>(deltaSeconds));
    cameraSmoothedTarget_ = glm::mix(cameraSmoothedTarget_, desired, lerpK);

    const float marginX = std::max(0.0f, arenaHalfExtent_.x - CameraClampHalfViewX);
    const float marginZ = std::max(0.0f, arenaHalfExtent_.y - CameraClampHalfViewZ);
    cameraSmoothedTarget_.x = std::clamp(cameraSmoothedTarget_.x, -marginX, marginX);
    cameraSmoothedTarget_.y = 0.0f;
    cameraSmoothedTarget_.z = std::clamp(cameraSmoothedTarget_.z, -marginZ, marginZ);
}

bool Brotato3DGameInstance::ShouldPauseWorldPhysics() const
{
    return appState_ != Brotato3D::EAppState::Playing;
}

void Brotato3DGameInstance::SetWorldPhysicsPaused(bool paused)
{
    if (NextPhysics* physics = GetEngine().GetPhysicsEngine())
    {
        physics->SetPaused(paused);
    }
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

