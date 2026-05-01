#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Brotato3DArena.hpp"
#include "Brotato3DDataLoader.hpp"
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

namespace Brotato3D
{
    enum class EAppState : uint8_t
    {
        Playing,
        Hitstop,
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
    const std::vector<Brotato3D::FFloatingText>& GetFloatingTexts() const { return floatingTexts_; }
    const Brotato3D::FWaveSystem& GetWaveSystem() const { return waveSystem_; }
    Brotato3D::EAppState GetAppState() const { return appState_; }
    int GetXpToNextLevel() const;
    int GetKillCount() const { return killCount_; }
    int GetTotalMaterialsGained() const { return totalMaterialsGained_; }
    float GetRunElapsedSec() const { return runElapsedSec_; }
    float GetDamageFlashMs() const { return damageFlashMs_; }
    bool IsPlayerDead() const { return playerDead_; }
    const std::vector<Brotato3D::FUpgradeCardDef>& GetCurrentUpgradeChoices() const { return currentUpgradeChoices_; }
    const std::vector<Brotato3D::FShopItemDef>& GetShopOffers() const { return shopOffers_; }
    int GetRerollCost() const { return shop_.GetRerollCost(); }
    bool WorldToScreen(const glm::vec3& world, ImVec2& outScreen) const;
    void SelectUpgrade(size_t choiceIndex);
    void BuyShopItem(size_t slotIndex);
    void RerollShop();
    void ContinueFromShop();
    void RestartGame();
    void ExitGame();

private:
    struct FEnemyVisualResource
    {
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        uint32_t darkMaterialId = 0;
        uint32_t hitFlashMaterialId = 0;
        glm::vec3 baseColor = glm::vec3(1.0f);
    };

    void SpawnEnemy(const std::string& enemyId, const glm::vec3& worldPos);
    void SpawnPickup(int value, Brotato3D::EPickupKind kind, const glm::vec3& worldPos);
    void UpdatePlayer(double deltaSeconds);
    void UpdateEnemies(double deltaSeconds);
    void UpdateWeapons(double deltaSeconds);
    void UpdateProjectiles(double deltaSeconds);
    void UpdatePickups(double deltaSeconds);
    void UpdateImpactDebris(double deltaSeconds);
    void UpdateFloatingTexts(double deltaSeconds);
    void KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot);
    void SpawnImpactDebris(const glm::vec3& worldPos);
    void ClearAliveEnemies(bool dropLoot);
    void HideNode(const std::shared_ptr<Assets::Node>& node);
    void ShowNode(const std::shared_ptr<Assets::Node>& node);
    void SetNodeMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId);
    void SetNodeTranslation(const std::shared_ptr<Assets::Node>& node, const glm::vec3& translation);
    void SetNodeRotation(const std::shared_ptr<Assets::Node>& node, const glm::quat& rotation);
    void PushFloatingText(const glm::vec3& worldPos, std::string text, const glm::vec4& color, float lifeMs);
    void BeginLevelUp();
    void RollUpgradeChoices();
    void ApplyUpgrade(const std::string& stat, float delta);
    void ApplyShopItem(const Brotato3D::FShopItemDef& item);
    void StartShopping();
    void EnterResult(bool playerDead);
    void ResetRuntimeState();
    void ClearMovementInput();
    glm::vec3 RandomDebugSpawnPosition();

    NextEngine* engine_ = nullptr;
    Brotato3D::EAppState appState_ = Brotato3D::EAppState::Playing;
    Brotato3D::FArenaResources arenaResources_{};
    Brotato3D::FPlayerRuntime player_{};
    std::map<std::string, Brotato3D::FEnemyDef> enemyDefs_;
    std::map<std::string, Brotato3D::FWeaponDef> weaponDefs_;
    std::vector<Brotato3D::FUpgradeCardDef> upgradeCards_;
    std::vector<Brotato3D::FShopItemDef> shopItems_;
    std::vector<Brotato3D::FWaveDef> waveDefs_;
    std::map<std::string, FEnemyVisualResource> enemyVisuals_;
    std::vector<Brotato3D::FEnemyRuntime> enemies_;
    std::vector<Brotato3D::FWeaponRuntime> equippedWeapons_;
    std::vector<Brotato3D::FProjectileRuntime> projectilePool_;
    std::vector<Brotato3D::FImpactDebrisRuntime> impactDebrisPool_;
    std::vector<Brotato3D::FPickupRuntime> pickupPool_;
    std::vector<Brotato3D::FFloatingText> floatingTexts_;
    Brotato3D::FWaveSystem waveSystem_;
    Brotato3D::FShop shop_;
    std::vector<Brotato3D::FUpgradeCardDef> currentUpgradeChoices_;
    std::vector<Brotato3D::FShopItemDef> shopOffers_;
    uint32_t projectileModelId_ = 0;
    uint32_t projectileMaterialId_ = 0;
    uint32_t impactDebrisModelId_ = 0;
    uint32_t impactDebrisMaterialId_ = 0;
    uint32_t pickupXpModelId_ = 0;
    uint32_t pickupXpMaterialId_ = 0;
    uint32_t pickupMaterialModelId_ = 0;
    uint32_t pickupMaterialMaterialId_ = 0;
    bool keyW_ = false;
    bool keyA_ = false;
    bool keyS_ = false;
    bool keyD_ = false;
    bool sceneReady_ = false;
    std::mt19937 rng_{std::random_device{}()};
    float screenShakeMs_ = 0.0f;
    float damageFlashMs_ = 0.0f;
    float hitStopMs_ = 0.0f;
    float runElapsedSec_ = 0.0f;
    int killCount_ = 0;
    int totalMaterialsGained_ = 0;
    bool playerDead_ = false;
};
