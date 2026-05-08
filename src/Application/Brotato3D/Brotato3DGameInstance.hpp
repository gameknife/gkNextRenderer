#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Brotato3DArena.hpp"
#include "Brotato3DDataLoader.hpp"
#include "Brotato3DDebris.hpp"
#include "Brotato3DEnemy.hpp"
#include "Brotato3DPickup.hpp"
#include "Brotato3DPlayer.hpp"
#include "Brotato3DProjectile.hpp"
#include "Brotato3DShop.hpp"
#include "Brotato3DWaveSystem.hpp"
#include "Brotato3DWeapon.hpp"

#include <glm/ext.hpp>
#include <random>

struct ImVec2;
struct ImFont;

namespace Brotato3D
{
    enum class EAppState : uint8_t
    {
        MainMenu,
        CharacterSelect,
        Playing,
        Hitstop,
        Paused,
        LevelUpPicking,
        Shopping,
        Result,
    };

    struct FFloatingText
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        std::string text;
        glm::vec4 color = glm::vec4(1.0f);
        float lifeMs = 0.0f;
        float remainingMs = 0.0f;
        float fontScale = 1.0f;
    };

    struct FMuzzleFlash
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f);
        float lifeMs = 80.0f;
        float remainingMs = 0.0f;
    };

    struct FBestRecord
    {
        int totalWins = 0;
        int totalKills = 0;
        float fastestCompletionSec = 0.0f;
        std::map<std::string, int> characterWins;
    };
}

class Brotato3DGameInstance : public NextGameInstanceBase
{
public:
    Brotato3DGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~Brotato3DGameInstance() override = default;

    void OnInit() override;
    void OnInitUI() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    void OnSceneLoaded() override;
    bool OnGamepadInput(int16_t leftStickX,
                        int16_t leftStickY,
                        int16_t rightStickX,
                        int16_t rightStickY,
                        int16_t leftTrigger,
                        int16_t rightTrigger) override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;

    NextEngine& GetEngine() const { return *engine_; }
    Brotato3D::FPlayerRuntime& GetPlayer() { return player_; }
    const Brotato3D::FPlayerRuntime& GetPlayer() const { return player_; }
    const std::vector<Brotato3D::FEnemyRuntime>& GetEnemies() const { return enemies_; }
    const std::vector<Brotato3D::FWeaponRuntime>& GetWeapons() const { return equippedWeapons_; }
    const std::vector<Brotato3D::FProjectileRuntime>& GetProjectiles() const { return projectilePool_; }
    const std::vector<Brotato3D::FFloatingText>& GetFloatingTexts() const { return floatingTexts_; }
    const std::vector<Brotato3D::FMuzzleFlash>& GetMuzzleFlashes() const { return muzzleFlashes_; }
    const std::vector<Brotato3D::FExpandingRing>& GetExplosionRings() const { return explosionRings_; }
    const std::vector<Brotato3D::FLaserBeam>& GetLaserBeams() const { return laserBeams_; }
    const Brotato3D::FWaveSystem& GetWaveSystem() const { return waveSystem_; }
    Brotato3D::EAppState GetAppState() const { return appState_; }
    int GetXpToNextLevel() const;
    int GetKillCount() const { return killCount_; }
    int GetTotalMaterialsGained() const { return totalMaterialsGained_; }
    float GetRunElapsedSec() const { return runElapsedSec_; }
    float GetDamageFlashMs() const { return damageFlashMs_; }
    float GetWaveBannerMs() const { return waveBannerMs_; }
    const std::string& GetWaveBannerText() const { return waveBannerText_; }
    float GetWeaponMergeBannerMs() const { return weaponMergeBannerMs_; }
    const std::string& GetWeaponMergeBannerText() const { return weaponMergeBannerText_; }
    ImFont* GetBigFont() const { return bigFont_; }
    bool IsPlayerDead() const { return playerDead_; }
    const std::vector<Brotato3D::FUpgradeCardDef>& GetCurrentUpgradeChoices() const { return currentUpgradeChoices_; }
    const std::vector<Brotato3D::FShopItemDef>& GetShopOffers() const { return shopOffers_; }
    const std::vector<std::string>& GetOwnedItemIds() const { return player_.ownedItemIds; }
    int GetRerollCost() const { return shop_.GetRerollCost(); }
    const Brotato3D::FItemDef* GetItemDef(const std::string& itemId) const;
    const std::vector<Brotato3D::FCharacterDef>& GetCharacterDefs() const { return characterDefs_; }
    const Brotato3D::FCharacterDef* GetSelectedCharacterDef() const;
    const std::string& GetSelectedCharacterId() const { return selectedCharacterId_; }
    const std::vector<Brotato3D::FArenaDef>& GetArenaDefs() const { return arenaDefs_; }
    const std::string& GetSelectedArenaId() const { return selectedArenaId_; }
    const Brotato3D::FBestRecord& GetBestRecord() const { return bestRecord_; }
    Brotato3D::FPlayerStats GetEffectivePlayerStats() const;
    bool CanBuyShopOffer(size_t slotIndex) const;
    std::string GetShopOfferUnavailableReason(size_t slotIndex) const;
    void SelectUpgrade(size_t choiceIndex);
    void BuyShopItem(size_t slotIndex);
    void RerollShop();
    void ContinueFromShop();
    void RestartGame();
    void StartNewRun();
    void GoToMainMenu();
    void GoToCharacterSelect();
    void SelectCharacter(const std::string& characterId);
    void SelectArena(const std::string& arenaId);
    void ResumeGame();
    void PauseGame();
    void ExitGame();

private:
    struct FEnemyVisualResource
    {
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        uint32_t hitFlashMaterialId = 0;
        uint32_t warningMaterialId = 0;
        uint32_t phase2MaterialId = 0;
        glm::vec3 baseColor = glm::vec3(1.0f);
    };

    void SpawnEnemy(const std::string& enemyId, const glm::vec3& worldPos);
    void SpawnPickup(int value, Brotato3D::EPickupKind kind, const glm::vec3& worldPos);
    void UpdatePlayer(double deltaSeconds);
    void UpdateEnemies(double deltaSeconds);
    void UpdateWeapons(double deltaSeconds);
    void UpdateProjectiles(double deltaSeconds);
    void UpdateEnemyProjectiles(double deltaSeconds);
    void UpdatePickups(double deltaSeconds);
    void UpdateDebris(double deltaSeconds);
    void UpdateCombatEffects(double deltaSeconds);
    void UpdateWaveBanner(double deltaSeconds);
    void UpdateFloatingTexts(double deltaSeconds);
    void SpawnEnemyProjectile(const Brotato3D::FEnemyRuntime& enemy, const glm::vec3& dir);
    void KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot);
    int CalculateWeaponDamage(const Brotato3D::FWeaponDef& weaponDef, bool& outIsCrit);
    void ApplyDamageToEnemy(Brotato3D::FEnemyRuntime& enemy, int damage, bool isCrit);
    void ApplyWeaponKnockback(Brotato3D::FEnemyRuntime& enemy, const glm::vec3& direction, float knockbackMeters);
    void DamagePlayer(int damage, float shakeMs, float flashMs);
    void BuildDebrisPool(std::vector<Assets::Model>& models,
                         std::vector<Assets::FMaterial>& materials,
                         std::vector<std::shared_ptr<Assets::Node>>& nodes);
    void BuildKinematicCollisionBodies(std::vector<Assets::Model>& models,
                                       std::vector<Assets::FMaterial>& materials,
                                       std::vector<std::shared_ptr<Assets::Node>>& nodes);
    void SpawnDebris(Brotato3D::EDebrisKind kind,
                     const glm::vec3& worldPos,
                     const glm::vec3& impulseDir,
                     float speed,
                     uint32_t materialId,
                     int count,
                     float angleConeRad,
                     bool pickable = false,
                     int materialValuePerSlot = 0,
                     float lifetimeMs = 0.0f);
    void ClearAllDebris(bool keepPickable);
    void SpawnImpactDebris(const glm::vec3& worldPos,
                           const glm::vec3& projectileVelocity,
                           const glm::vec3& weaponColor,
                           const glm::vec3& enemyColor,
                           bool isCrit,
                           const std::string& enemyName);
    void SpawnDeathDebris(const Brotato3D::FEnemyRuntime& enemy);
    void SpawnPlayerDamageDebris(int damage);
    uint32_t EnsureHitDebrisMaterial(const glm::vec3& weaponColor, const glm::vec3& enemyColor);
    void PushMuzzleFlash(const glm::vec3& worldPos, const glm::vec3& color);
    void SpawnTempLight(const glm::vec3& worldPos, const glm::vec3& color, float radiusMeters, float durationMs);
    void UpdateLightArea(int lightIndex, const glm::vec3& worldPos, float radiusMeters, float intensityScale);
    uint32_t EnsureLightMaterial(const glm::vec3& color);
    void StartScreenShake(float durationMs, float intensity);
    void PushExplosionRing(const glm::vec3& worldPos, const glm::vec4& color, float maxRadius);
    void PushLaserBeam(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, float durationMs, float width);
    void ClearAliveEnemies(bool dropLoot);
    void PushFloatingText(const glm::vec3& worldPos,
                          std::string text,
                          const glm::vec4& color,
                          float lifeMs,
                          float fontScale = 1.0f);
    void BeginLevelUp();
    void RollUpgradeChoices();
    void ApplyUpgrade(const std::string& stat, float delta);
    void ApplyShopItem(const Brotato3D::FShopItemDef& item);
    bool BuyPassiveItem(const Brotato3D::FShopItemDef& item);
    void ApplyPassiveItemStats(const Brotato3D::FItemDef& item);
    Brotato3D::FPlayerStats GetEffectiveStats() const;
    Brotato3D::FWeaponRuntime CreateWeaponRuntime(const std::string& weaponId, int tier) const;
    Brotato3D::FWeaponDef CreateTier2Weapon(const Brotato3D::FWeaponDef& base) const;
    void NormalizeWeaponDefPointers();
    bool CanBuyWeaponCard(const std::string& weaponId) const;
    bool BuyWeaponCard(const Brotato3D::FShopItemDef& item);
    void TryMergeWeapons(const std::string& weaponId);
    void ProcessItemTriggers(double deltaSeconds);
    void ProcessTickItemTriggers(float deltaMs);
    void ProcessLowHpItemBuffs();
    void ProcessOnKillTriggers(const glm::vec3& worldPos);
    void ApplyItemExplosionDamage(const glm::vec3& worldPos, float radius, int damage);
    void StartShopping();
    void EnterResult(bool playerDead);
    void ResetRuntimeState();
    void ApplySelectedCharacter();
    void ApplySelectedArena();
    void LoadBestRecord();
    void SaveBestRecord() const;
    void UpdateBestRecord(bool playerDead);
    const Brotato3D::FCharacterDef* FindCharacterDef(const std::string& characterId) const;
    void BeginWaveBanner();
    void SetDebugSingleWeapon(const std::string& weaponId);
    void ApplyLightingSettings();
    void ClearMovementInput();
    bool ShouldPauseWorldPhysics() const;
    void SetWorldPhysicsPaused(bool paused);
    glm::vec3 RandomDebugSpawnPosition();
    NextBodyID AcquireEnemyKinematicBody(const std::string& enemyId) const;
    void SyncPlayerKinematicBody(double deltaSeconds);
    void SyncEnemyKinematicBody(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds);
    void DeactivateEnemyKinematicBody(Brotato3D::FEnemyRuntime& enemy);

    NextEngine* engine_ = nullptr;
    Brotato3D::EAppState appState_ = Brotato3D::EAppState::MainMenu;
    Brotato3D::FArenaResources arenaResources_{};
    Brotato3D::FPlayerRuntime player_{};
    std::map<std::string, Brotato3D::FEnemyDef> enemyDefs_;
    std::map<std::string, Brotato3D::FWeaponDef> weaponDefs_;
    std::vector<Brotato3D::FUpgradeCardDef> upgradeCards_;
    std::vector<Brotato3D::FShopItemDef> shopItems_;
    std::vector<Brotato3D::FItemDef> itemDefs_;
    std::map<std::string, Brotato3D::FItemDef> itemDefsById_;
    std::vector<Brotato3D::FCharacterDef> characterDefs_;
    std::vector<Brotato3D::FArenaDef> arenaDefs_;
    std::string selectedCharacterId_ = "soldier";
    std::string selectedArenaId_ = "grassland";
    Brotato3D::FBestRecord bestRecord_{};
    std::vector<Brotato3D::FWaveDef> waveDefs_;
    std::map<std::string, FEnemyVisualResource> enemyVisuals_;
    std::vector<Brotato3D::FEnemyRuntime> enemies_;
    std::vector<Brotato3D::FWeaponRuntime> equippedWeapons_;
    std::vector<Brotato3D::FProjectileRuntime> projectilePool_;
    std::vector<Brotato3D::FEnemyProjectileRuntime> enemyProjectilePool_;
    std::vector<Brotato3D::FDebrisRuntime> debrisPool_;
    std::vector<Brotato3D::FPickupRuntime> pickupPool_;
    std::vector<Brotato3D::FFloatingText> floatingTexts_;
    std::vector<Brotato3D::FMuzzleFlash> muzzleFlashes_;
    std::vector<Brotato3D::FExpandingRing> explosionRings_;
    std::vector<Brotato3D::FLaserBeam> laserBeams_;
    Brotato3D::FWaveSystem waveSystem_;
    Brotato3D::FShop shop_;
    std::vector<Brotato3D::FUpgradeCardDef> currentUpgradeChoices_;
    std::vector<Brotato3D::FShopItemDef> shopOffers_;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMaterialId_ = 0;
    uint32_t enemyProjectileModelId_ = 0;
    uint32_t enemyProjectileMaterialId_ = 0;
    std::map<const Brotato3D::FEnemyDef*, uint32_t> enemyProjectileMaterialIds_;
    std::map<std::string, uint32_t> lightMaterialIds_;
    struct FTempLightRuntime
    {
        int lightIndex = -1;
        glm::vec3 worldPos = glm::vec3(0.0f);
        float radiusMeters = 3.0f;
        float durationMs = 120.0f;
        float remainingMs = 0.0f;
        std::shared_ptr<Assets::Node> node;
        bool active = false;
    };
    int playerLightIndex_ = -1;
    std::shared_ptr<Assets::Node> playerLightNode_;
    std::vector<FTempLightRuntime> tempLightPool_;
    uint32_t debrisTinyModelId_ = 0;
    uint32_t debrisChunkModelId_ = 0;
    uint32_t debrisBossChunkModelId_ = 0;
    uint32_t debrisFallbackTinyMatId_ = 0;
    uint32_t debrisFallbackChunkMatId_ = 0;
    uint32_t materialDebrisMatId_ = 0;
    uint32_t playerDebrisMatId_ = 0;
    uint64_t debrisTickCounter_ = 0;
    NextBodyID playerKinematicBodyId_{};
    bool playerKinematicBodyActive_ = false;
    std::map<std::string, std::vector<NextBodyID>> enemyKinematicBodyPools_;
    std::unordered_map<uint64_t, uint32_t> hitDebrisMaterialIds_;
    uint32_t pickupXpModelId_ = 0;
    uint32_t pickupXpMaterialId_ = 0;
    std::map<std::string, uint32_t> characterMaterialIds_;
    bool keyW_ = false;
    bool keyA_ = false;
    bool keyS_ = false;
    bool keyD_ = false;
    glm::vec2 gamepadMoveInput_ = glm::vec2(0.0f);
    bool sceneReady_ = false;
    std::mt19937 rng_{std::random_device{}()};
    float screenShakeMs_ = 0.0f;
    float screenShakeIntensity_ = 0.0f;
    float damageFlashMs_ = 0.0f;
    float hitStopMs_ = 0.0f;
    float globalTimeScale_ = 1.0f;
    float bossKillFlashMs_ = 0.0f;
    float timeScaleRecoveryMs_ = 0.0f;
    float bossVictoryDelayMs_ = 0.0f;
    float waveBannerMs_ = 0.0f;
    std::string waveBannerText_;
    float runElapsedSec_ = 0.0f;
    int killCount_ = 0;
    int totalMaterialsGained_ = 0;
    bool playerDead_ = false;
    Brotato3D::FPlayerStats dynamicStatBuffs_{};
    float itemTickAccumMs_ = 0.0f;
    float itemHealRemainder_ = 0.0f;
    float weaponMergeBannerMs_ = 0.0f;
    std::string weaponMergeBannerText_;
    ImFont* bigFont_ = nullptr;
};
