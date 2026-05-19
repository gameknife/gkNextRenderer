#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Voyage3DCombat.hpp"
#include "Voyage3DCommon.hpp"
#include "Voyage3DDataLoader.hpp"
#include "Voyage3DPort.hpp"
#include "Voyage3DShip.hpp"

#include <random>

struct ImFont;

class Voyage3DGameInstance : public NextGameInstanceBase
{
public:
    Voyage3DGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~Voyage3DGameInstance() override = default;

    void OnInit() override;
    void OnInitUI() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    void OnSceneLoaded() override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;

    Voyage3D::EAppState GetAppState() const { return appState_; }
    const Voyage3D::FInputState& GetInputState() const { return input_; }
    Voyage3D::FShipRuntime& GetPlayerShip() { return playerShip_; }
    const Voyage3D::FShipRuntime& GetPlayerShip() const { return playerShip_; }
    std::vector<Voyage3D::FShipRuntime>& GetEnemyShips() { return enemyShips_; }
    const std::vector<Voyage3D::FShipRuntime>& GetEnemyShips() const { return enemyShips_; }
    std::vector<Voyage3D::FProjectileRuntime>& GetProjectiles() { return projectiles_; }
    const std::vector<Voyage3D::FProjectileRuntime>& GetProjectiles() const { return projectiles_; }
    std::vector<Voyage3D::FPortRuntime>& GetPorts() { return ports_; }
    const std::vector<Voyage3D::FPortRuntime>& GetPorts() const { return ports_; }
    const std::vector<Voyage3D::FGoodsDef>& GetGoodsDefs() const { return goodsDefs_; }
    const std::vector<Voyage3D::FShipDef>& GetShipDefs() const { return shipDefs_; }
    const std::vector<Voyage3D::FEventDef>& GetEventDefs() const { return eventDefs_; }
    const std::vector<Voyage3D::FLandmassBlock>& GetLandBlocks() const { return landBlocks_; }
    std::mt19937& GetRng() { return rng_; }
    int GetGold() const { return gold_; }
    int GetGameDayCounter() const { return gameDayCounter_; }
    int GetVisitedPortCount() const;
    int GetCombatWins() const { return combatWins_; }
    int GetSailingDays() const { return gameDayCounter_; }
    const Voyage3D::FPortRuntime* GetNearestPort() const { return nearestPort_; }
    Voyage3D::FPortRuntime* GetCurrentPort() { return currentPort_; }
    const Voyage3D::FPortRuntime* GetCurrentPort() const { return currentPort_; }
    const std::vector<Voyage3D::FFloatingText>& GetFloatingTexts() const { return floatingTexts_; }
    const std::vector<Voyage3D::FMuzzleFlash>& GetMuzzleFlashes() const { return muzzleFlashes_; }
    const std::vector<Voyage3D::FExpandingRing>& GetExplosionRings() const { return explosionRings_; }
    const std::string& GetToastText() const { return toastText_; }
    float GetToastMs() const { return toastMs_; }
    const std::vector<std::string>& GetEventLog() const { return eventLog_; }
    bool IsEventLogOpen() const { return eventLogOpen_; }
    bool IsPirateEncounterPending() const { return pirateEncounterPending_; }
    const std::string& GetTradeMessage() const { return tradeMessage_; }
    float GetTradeMessageMs() const { return tradeMessageMs_; }
    const std::map<std::string, std::string>& GetPortLore() const { return portLore_; }
    ImFont* GetTitleFont() const { return titleFont_; }

    std::string FormatGameDate() const;
    const Voyage3D::FGoodsDef* FindGoodsDef(const std::string& goodId) const;
    const Voyage3D::FShipDef* FindShipDef(const std::string& shipId) const;
    Voyage3D::FPortRuntime* FindPort(const std::string& portId);
    const Voyage3D::FPortRuntime* FindPort(const std::string& portId) const;

    void StartNewGame();
    void ReturnToMainMenu();
    void EnterPort(Voyage3D::FPortRuntime& port);
    void LeavePort();
    void ReturnToPortMenu();
    void OpenTrade();
    void OpenShipUpgrade();
    bool TryBuyGood(const std::string& goodId, int qty);
    bool TrySellGood(const std::string& goodId, int qty);
    bool TryBuyShip(const std::string& shipId);
    void BeginPirateEncounter(const std::string& enemyShipId);
    void StartPirateCombat();
    void TryFleePirates();
    void ForcePirateDebug();
    void EndCombatVictory();
    void EndCombatDefeat();
    int AddCargo(const std::string& goodId, int qty);
    void AddGold(int amount);
    void DamagePlayer(int damage);
    void ApplyStormDebuff(float durationMs);
    void GrantIntelDiscount(const std::string& portId, const std::string& goodId, float factor);
    void PushToast(std::string text, float durationMs = 2600.0f);
    void AddEventLog(std::string text);
    void PushFloatingText(const glm::vec3& worldPos, std::string text, const glm::vec4& color, float lifeMs, float fontScale = 1.0f);
    void PushMuzzleFlash(const glm::vec3& worldPos, const glm::vec3& color);
    void PushExplosionRing(const glm::vec3& worldPos, const glm::vec4& color, float maxRadius);
    void StartScreenShake(float durationMs, float intensity);
    void UpdateShipNodeTransform(Voyage3D::FShipRuntime& ship);
    void SpawnEnemyShip(const std::string& shipId, const glm::vec3& spawnPos);
    void FireBroadside(Voyage3D::FShipRuntime& ship, bool leftSide, bool fromPlayer);
    void SpawnProjectile(const glm::vec3& worldPos, const glm::vec3& velocity, bool fromPlayer, int damage);

private:
    void ResetRuntimeState();
    void LoadGameData();
    void BuildPorts(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials);
    void BuildShipVisual(Voyage3D::FShipRuntime& ship,
                         std::string_view namePrefix,
                         const glm::vec3& sailColor,
                         std::vector<std::shared_ptr<Assets::Node>>& nodes,
                         std::vector<Assets::Model>& models,
                         std::vector<Assets::FMaterial>& materials,
                         bool visible);
    void BuildProjectilePool(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             std::vector<Assets::Model>& models,
                             std::vector<Assets::FMaterial>& materials);
    void RebuildPlayerShipVisual();
    void UpdateNearestPort();
    void UpdateRuntimeEffects(double deltaSeconds);
    void UpdateVisitedAnchor(Voyage3D::FPortRuntime& port);
    void SetRuntimeVisible(Voyage3D::FShipRuntime& ship, bool visible);
    void UpdateBgm();
    void ApplyLightingSettings();
    void SetTradeMessage(std::string message);
    void DropOverflowCargo();
    void CheckLossConditions();

    Voyage3D::EAppState appState_ = Voyage3D::EAppState::MainMenu;
    Voyage3D::FInputState input_{};
    std::vector<Voyage3D::FPortDef> portDefs_;
    std::vector<Voyage3D::FGoodsDef> goodsDefs_;
    std::vector<Voyage3D::FShipDef> shipDefs_;
    std::vector<Voyage3D::FLandmassBlock> landBlocks_;
    std::vector<Voyage3D::FEventDef> eventDefs_;
    std::map<std::string, std::string> portLore_;
    std::vector<Voyage3D::FPortRuntime> ports_;
    Voyage3D::FShipRuntime playerShip_{};
    std::vector<Voyage3D::FShipRuntime> enemyShips_;
    std::vector<Voyage3D::FProjectileRuntime> projectiles_;
    Voyage3D::FPortRuntime* nearestPort_ = nullptr;
    Voyage3D::FPortRuntime* currentPort_ = nullptr;
    std::vector<Voyage3D::FFloatingText> floatingTexts_;
    std::vector<Voyage3D::FMuzzleFlash> muzzleFlashes_;
    std::vector<Voyage3D::FExpandingRing> explosionRings_;
    std::vector<std::string> eventLog_;
    std::string pendingPirateShipId_ = "sloop";
    std::string toastText_;
    std::string tradeMessage_;
    std::string resultReason_ = "defeat";
    std::string currentBgmId_;
    std::mt19937 rng_{std::random_device{}()};
    ImFont* titleFont_ = nullptr;
    int gold_ = 1000;
    int gameDayCounter_ = 0;
    int combatWins_ = 0;
    float eventCheckTimerSec_ = 0.0f;
    float gameDateAccumSec_ = 0.0f;
    float stormDebuffMs_ = 0.0f;
    float toastMs_ = 0.0f;
    float tradeMessageMs_ = 0.0f;
    float retreatHoldMs_ = 0.0f;
    float screenShakeMs_ = 0.0f;
    float screenShakeIntensity_ = 0.0f;
    bool sceneReady_ = false;
    bool pirateEncounterPending_ = false;
    bool eventLogOpen_ = false;
    uint32_t visitedAnchorMaterialId_ = 0;
    uint32_t unvisitedAnchorMaterialId_ = 0;
};
