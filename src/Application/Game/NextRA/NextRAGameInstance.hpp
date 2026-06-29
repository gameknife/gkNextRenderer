#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "Net/OrderManager.h"
#include "NextRAConfig.hpp"
#include "Render/RenderProxySystem.h"
#include "Render/RtsCamera.h"
#include "Sim/PathfindGrid.h"
#include "Sim/SimWorld.h"

#include <glm/vec2.hpp>

class NextRAGameInstance : public NextGameInstanceBase
{
public:
    NextRAGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~NextRAGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    void ResetSim();
    void StepSim(double deltaSeconds);
    void RebindRenderNodes();
    glm::vec3 GetFirstActorRenderNodePos() const;
    void CreateRenderNodeForActor(NextRA::Sim::FActorId actor);
    void SyncSpawnedRenderNodes();
    bool TryGetGroundHit(const glm::dvec2& screenPos, NextRA::Sim::WPos& outPos) const;
    void SelectAt(const glm::dvec2& screenPos, bool additive);
    void SelectInRect(const glm::dvec2& a, const glm::dvec2& b, bool additive);
    void IssueMoveCommand(const NextRA::Sim::WPos& targetPos);
    void IssueAttackCommand(NextRA::Sim::FActorId targetActor);
    void IssueProduceCommand(uint16_t produceTypeId);
    void SubmitLocalOrderWithLog(NextRA::Net::FOrder order, const char* label);
    void SubmitAIOrderWithInjection(NextRA::Net::FOrder order);
    void DrainInjectedAIOrders();
    void SubmitAIOrders();
    NextRA::Sim::FActorId FindBaseForPlayer(uint8_t playerId) const;
    NextRA::Sim::FActorId FindSelectedProductionActor() const;
    NextRA::Sim::FActorId TryPickEnemyAt(const glm::dvec2& screenPos) const;
    NextRA::Sim::FActorId TryPickOwnedActorAt(const glm::dvec2& screenPos) const;
    void HideDestroyedRenderNodes();
    bool IsSelected(NextRA::Sim::FActorId actor) const;
    void DrawSelectionOverlay() const;
    void DrawDebugGridOverlay() const;
    void DrawMinimap() const;
    void DrawVictoryBanner() const;

    struct FOrderLogEntry
    {
        uint32_t tick = 0;
        uint8_t playerId = 0;
        NextRA::Net::EOrderType type = NextRA::Net::EOrderType::Move;
        uint16_t actorCount = 0;
    };

    struct FInjectedOrder
    {
        uint32_t deliverTick = 0;
        NextRA::Net::FOrder order;
    };

    NextRA::Sim::FSimWorld simWorld_;
    NextRA::Sim::FPathfindGrid pathGrid_{48, 48, NextRA::Sim::CPos{-24, -24}};
    NextRA::Net::FOrderManager orderManager_;
    NextRA::FRenderProxySystem renderProxy_;
    NextRA::FRtsCamera camera_;
    std::vector<std::shared_ptr<Assets::Node>> actorNodes_;
    std::vector<NextRA::Sim::FActorId> selectedActors_;
    std::vector<FOrderLogEntry> orderLog_;
    std::vector<FInjectedOrder> injectedAIOrders_;
    std::vector<NextRA::Sim::CPos> debugPath_;
    glm::dvec2 mousePos_{0.0, 0.0};
    glm::dvec2 dragStart_{0.0, 0.0};
    bool hasMousePos_ = false;
    bool leftMouseDown_ = false;
    double accumulator_ = 0.0;
    float renderAlpha_ = 0.0f;
    uint32_t nextTick_ = 0;
    uint32_t aiNextProduceTick_ = 80;
    uint32_t aiNextAttackTick_ = 140;
    int orderLatencyTicks_ = 0;
    int artificialDelayTicks_ = 0;
    int dropEveryNthPacket_ = 0;
    int reorderEveryNthPacket_ = 0;
    uint32_t injectedOrderCounter_ = 0;
    bool aiEnabled_ = true;
    bool showDebugGrid_ = true;
    bool showMinimap_ = true;
    bool showOrderLog_ = true;
    bool sceneInjected_ = false;
    uint32_t infantryModelId_ = 0;
    uint32_t tankModelId_ = 0;
    uint32_t barracksModelId_ = 0;
    uint32_t baseModelId_ = 0;
    uint32_t playerMatId_ = 0;
    uint32_t enemyMatId_ = 0;
};
