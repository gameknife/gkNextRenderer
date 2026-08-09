#include "NextRAGameInstance.hpp"

#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Sim/SyncHash.h"
#include "Sim/Systems/OrderApplySystem.h"

#include <algorithm>
#include <cmath>

namespace
{
    glm::vec3 ToRenderVec3(const NextRA::Sim::WPos& pos)
    {
        constexpr float renderScale = 1.0f / static_cast<float>(NextRA::Sim::cellSubUnits);
        return glm::vec3(pos.x.ToFloat() * renderScale, pos.y.ToFloat() * renderScale, pos.z.ToFloat() * renderScale);
    }

    NextRA::Sim::WPos FromRenderVec3(const glm::vec3& pos)
    {
        constexpr float simScale = static_cast<float>(NextRA::Sim::cellSubUnits);
        return NextRA::Sim::WPos{
            NextRA::Sim::FFixed::FromRaw(static_cast<int64_t>(std::llround(pos.x * simScale * NextRA::Sim::FFixed::oneRaw))),
            NextRA::Sim::FFixed::FromInt(0),
            NextRA::Sim::FFixed::FromRaw(static_cast<int64_t>(std::llround(pos.z * simScale * NextRA::Sim::FFixed::oneRaw))),
        };
    }

    double DistanceSquared(const glm::dvec2& a, const glm::dvec2& b)
    {
        const glm::dvec2 delta = a - b;
        return glm::dot(delta, delta);
    }

    const char* OrderTypeName(NextRA::Net::EOrderType type)
    {
        switch (type)
        {
        case NextRA::Net::EOrderType::Move:
            return "Move";
        case NextRA::Net::EOrderType::AttackMove:
            return "AttackMove";
        case NextRA::Net::EOrderType::Attack:
            return "Attack";
        case NextRA::Net::EOrderType::Produce:
            return "Produce";
        }
        return "Unknown";
    }

    const char* UnitTypeName(uint16_t typeId)
    {
        return NextRA::UnitDef(typeId).name;
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<NextRAGameInstance>(config, options, engine);
}

NextRAGameInstance::NextRAGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "NextRA", 1600, 900, true);
}

void NextRAGameInstance::OnInit()
{
    ResetSim();
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void NextRAGameInstance::OnTick(double deltaSeconds)
{
    camera_.Tick(deltaSeconds);
    StepSim(deltaSeconds);
    SyncSpawnedRenderNodes();
    HideDestroyedRenderNodes();
    renderProxy_.Sync(simWorld_, renderAlpha_);
    GetEngine().GetScene().MarkTransformDirty();
}

void NextRAGameInstance::OnDestroy()
{
}

bool NextRAGameInstance::OnRenderUI()
{
    ImGui::SetNextWindowPos(ImVec2(12, 12), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);
    ImGui::Begin("NextRA", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Tick: %u", simWorld_.CurrentTick());
    ImGui::Text("Actors: %zu", simWorld_.Actors().size());
    ImGui::Text("Selected: %zu", selectedActors_.size());
    ImGui::Text("Orders: %zu", orderManager_.PendingOrderCount());
    ImGui::Text("Injected: %zu", injectedAIOrders_.size());
    ImGui::Text("Alpha: %.2f", renderAlpha_);
    ImGui::Text("Hash: %016llx", static_cast<unsigned long long>(NextRA::Sim::ComputeSyncHash(simWorld_)));
    ImGui::Text("Peer 0: %016llx", static_cast<unsigned long long>(NextRA::Sim::ComputeSyncHash(simWorld_)));
    ImGui::Separator();
    bool latencyChanged = false;
    latencyChanged |= ImGui::SliderInt("Order latency", &orderLatencyTicks_, 0, 8);
    ImGui::SliderInt("Net delay", &artificialDelayTicks_, 0, 12);
    ImGui::SliderInt("Drop every N", &dropEveryNthPacket_, 0, 20);
    ImGui::SliderInt("Reorder every N", &reorderEveryNthPacket_, 0, 20);
    ImGui::Checkbox("AI", &aiEnabled_);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showDebugGrid_);
    ImGui::SameLine();
    ImGui::Checkbox("Mini", &showMinimap_);
    if (latencyChanged)
    {
        orderManager_.SetLockstepConfig(1, static_cast<uint32_t>(orderLatencyTicks_));
    }
    if (simWorld_.WinnerPlayerId() >= 0)
    {
        ImGui::Separator();
        ImGui::TextUnformatted(simWorld_.WinnerPlayerId() == 0 ? "Victory" : "Defeat");
    }
    if (!selectedActors_.empty())
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Selection");
        for (NextRA::Sim::FActorId actor : selectedActors_)
        {
            const NextRA::Sim::FUnitType* type = simWorld_.TryGetUnitType(actor);
            const NextRA::Sim::FHealth* health = simWorld_.TryGetHealth(actor);
            const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
            if (!type || !health || !transform)
            {
                continue;
            }
            const glm::vec3 pos = ToRenderVec3(transform->pos);
            ImGui::Text("#%u %s  HP %d/%d  %.1f, %.1f",
                        actor,
                        UnitTypeName(type->typeId),
                        health->hp,
                        health->maxHp,
                        pos.x,
                        pos.z);
        }
    }
    if (const NextRA::Sim::FActorId productionActor = FindSelectedProductionActor();
        productionActor != static_cast<NextRA::Sim::FActorId>(-1))
    {
        const NextRA::Sim::FProduction* production = simWorld_.TryGetProduction(productionActor);
        ImGui::Separator();
        if (production && production->queuedTypeId != 0)
        {
            ImGui::Text("Producing: %s (%d)",
                        UnitTypeName(production->queuedTypeId),
                        production->progressLeft);
        }
        else
        {
            if (ImGui::Button("Infantry"))
            {
                IssueProduceCommand(NextRA::infantryTypeId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Rocketeer"))
            {
                IssueProduceCommand(NextRA::rocketeerTypeId);
            }
            ImGui::SameLine();
            if (ImGui::Button("Tank"))
            {
                IssueProduceCommand(NextRA::tankTypeId);
            }
        }
    }
    if (showOrderLog_)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Order log");
        const size_t first = orderLog_.size() > 8 ? orderLog_.size() - 8 : 0;
        for (size_t index = first; index < orderLog_.size(); ++index)
        {
            const FOrderLogEntry& entry = orderLog_[index];
            ImGui::Text("%04u P%u %-10s x%u",
                        entry.tick,
                        entry.playerId,
                        OrderTypeName(entry.type),
                        entry.actorCount);
        }
    }
    ImGui::End();
    DrawSelectionOverlay();
    DrawDebugGridOverlay();
    DrawMinimap();
    DrawVictoryBanner();
    return true;
}

bool NextRAGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    return camera_.OverrideRenderCamera(outRenderCamera);
}

void NextRAGameInstance::BeforeSceneRebuild(
    std::vector<std::shared_ptr<Assets::Node>>& nodes,
    std::vector<Assets::Model>& models,
    std::vector<Assets::FMaterial>& materials,
    std::vector<Assets::LightObject>& lights,
    std::vector<Assets::AnimationTrack>& tracks)
{
    (void)lights;
    (void)tracks;

    if (sceneInjected_)
    {
        return;
    }

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-20.0f, -0.08f, -20.0f), glm::vec3(20.0f, 0.0f, 20.0f)));
    const uint32_t groundModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.45f, 0.0f, -0.45f), glm::vec3(0.45f, 0.65f, 0.45f)));
    infantryModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.38f, 0.0f, -0.38f), glm::vec3(0.38f, 0.78f, 0.38f)));
    rocketeerModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.65f, 0.0f, -0.85f), glm::vec3(0.65f, 0.45f, 0.85f)));
    tankBodyModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.36f, -0.10f, -0.36f), glm::vec3(0.36f, 0.16f, 0.36f)));
    tankTurretModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.07f, -0.06f, -0.05f), glm::vec3(0.07f, 0.06f, 0.82f)));
    tankBarrelModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.8f, 0.0f, -0.8f), glm::vec3(0.8f, 1.0f, 0.8f)));
    barracksModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-1.05f, 0.0f, -1.05f), glm::vec3(1.05f, 1.25f, 1.05f)));
    baseModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.52f, 0.0f, -0.52f), glm::vec3(0.52f, 0.42f, 0.52f)));
    turretBaseModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.34f, -0.09f, -0.34f), glm::vec3(0.34f, 0.15f, 0.34f)));
    turretHeadModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.06f, -0.05f, -0.04f), glm::vec3(0.06f, 0.05f, 0.95f)));
    turretBarrelModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.55f, 0.0f, -0.16f), glm::vec3(0.55f, 0.72f, 0.16f)));
    wallModelId_ = static_cast<uint32_t>(models.size() - 1);

    const uint32_t groundMatId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.23f, 0.30f, 0.23f));
    playerMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.18f, 0.42f, 0.92f));
    enemyMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.92f, 0.28f, 0.18f));
    playerRocketMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.18f, 0.70f, 0.82f));
    enemyRocketMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.92f, 0.55f, 0.18f));
    metalMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.16f, 0.18f, 0.19f));
    wallMatId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.42f, 0.45f, 0.46f));
    const uint32_t neutralMatId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.65f, 0.68f, 0.72f));

    nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
        "NextRA_Ground", glm::vec3(0.0f), glm::vec3(1.0f), 9000, groundModelId, groundMatId, true));

    actorNodes_.clear();
    renderProxy_.Clear();
    uint32_t nextSceneNodeId = 9100;
    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        const NextRA::Sim::FUnitType* unitType = simWorld_.TryGetUnitType(actor);
        if (!transform || !owner || !unitType)
        {
            continue;
        }

        const uint32_t renderNodeId = nextSceneNodeId++;
        uint32_t turretNodeId = 0;
        CreateActorRenderNode(actor, *transform, *owner, *unitType, renderNodeId, &nodes, nextSceneNodeId, turretNodeId);
        simWorld_.SetRenderLink(actor, renderNodeId, turretNodeId);
    }

    nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
        "NextRA_NeutralBlock", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f), 9202, barracksModelId_, neutralMatId, true));

    sceneInjected_ = true;
}

void NextRAGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();
    RebindRenderNodes();
}

void NextRAGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();
    actorNodes_.clear();
    renderProxy_.Clear();
    sceneInjected_ = false;
}

bool NextRAGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }

    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key)
    {
    case SDLK_W:
        camera_.SetMoveForward(pressed);
        return true;
    case SDLK_S:
        camera_.SetMoveBack(pressed);
        return true;
    case SDLK_A:
        camera_.SetMoveLeft(pressed);
        return true;
    case SDLK_D:
        camera_.SetMoveRight(pressed);
        return true;
    default:
        return false;
    }
}

bool NextRAGameInstance::OnCursorPosition(double xpos, double ypos)
{
    mousePos_ = glm::dvec2(xpos, ypos);
    hasMousePos_ = true;
    return leftMouseDown_;
}

bool NextRAGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP)
    {
        return false;
    }

    mousePos_ = glm::dvec2(event.button.x, event.button.y);
    hasMousePos_ = true;

    if (event.button.button == SDL_BUTTON_LEFT)
    {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            leftMouseDown_ = true;
            dragStart_ = mousePos_;
            return true;
        }

        leftMouseDown_ = false;
        const bool additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (DistanceSquared(dragStart_, mousePos_) > 64.0)
        {
            SelectInRect(dragStart_, mousePos_, additive);
        }
        else
        {
            SelectAt(mousePos_, additive);
        }
        return true;
    }

    if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        NextRA::Sim::WPos target;
        if (TryGetGroundHit(mousePos_, target))
        {
            const NextRA::Sim::FActorId enemy = TryPickEnemyAt(mousePos_);
            if (enemy != static_cast<NextRA::Sim::FActorId>(-1))
            {
                IssueAttackCommand(enemy);
            }
            else
            {
                IssueMoveCommand(target);
            }
        }
        return true;
    }

    return false;
}

bool NextRAGameInstance::OnScroll(double xoffset, double yoffset)
{
    (void)xoffset;
    camera_.AddZoom(static_cast<float>(yoffset));
    return true;
}

void NextRAGameInstance::ResetSim()
{
    simWorld_ = NextRA::Sim::FSimWorld{};
    pathGrid_ = NextRA::Sim::FPathfindGrid{48, 48, NextRA::Sim::CPos{-24, -24}};
    pathGrid_.SetBlocked(NextRA::Sim::CPos{0, 0}, true);
    simWorld_.SetPathGrid(&pathGrid_);
    orderManager_.Clear();
    orderManager_.SetLockstepConfig(1, static_cast<uint32_t>(orderLatencyTicks_));
    selectedActors_.clear();
    orderLog_.clear();
    injectedAIOrders_.clear();
    debugPath_.clear();
    injectedOrderCounter_ = 0;
    accumulator_ = 0.0;
    renderAlpha_ = 0.0f;
    nextTick_ = 0;
    aiNextProduceTick_ = 80;
    aiNextAttackTick_ = 140;

    simWorld_.SpawnBuilding(
        0,
        NextRA::baseTypeId,
        NextRA::Sim::WPos::FromCells(-8, -7),
        NextRA::BaseMaxHp(),
        true,
        false,
        NextRA::Sim::WPos::FromCells(-6, -7));
    simWorld_.SpawnBuilding(
        0,
        NextRA::barracksTypeId,
        NextRA::Sim::WPos::FromCells(-7, -4),
        NextRA::BarracksMaxHp(),
        false,
        true,
        NextRA::Sim::WPos::FromCells(-5, -4));
    simWorld_.SpawnMobile(
        0,
        NextRA::infantryTypeId,
        NextRA::Sim::WPos::FromCells(-4, -1),
        NextRA::Sim::WPos::FromCells(-4, -1),
        NextRA::UnitSpeedPerTick(NextRA::infantryTypeId),
        false);
    simWorld_.SpawnMobile(
        0,
        NextRA::rocketeerTypeId,
        NextRA::Sim::WPos::FromCells(-5, 1),
        NextRA::Sim::WPos::FromCells(-5, 1),
        NextRA::UnitSpeedPerTick(NextRA::rocketeerTypeId),
        false);
    simWorld_.SpawnMobile(
        0,
        NextRA::tankTypeId,
        NextRA::Sim::WPos::FromCells(-4, 3),
        NextRA::Sim::WPos::FromCells(-4, 3),
        NextRA::UnitSpeedPerTick(NextRA::tankTypeId),
        false);
    simWorld_.SpawnMobile(
        0,
        NextRA::infantryTypeId,
        NextRA::Sim::WPos::FromCells(-4, 1),
        NextRA::Sim::WPos::FromCells(-4, 1),
        NextRA::UnitSpeedPerTick(NextRA::infantryTypeId),
        false);
    simWorld_.SpawnBuilding(
        0,
        NextRA::turretTypeId,
        NextRA::Sim::WPos::FromCells(-4, -5),
        NextRA::UnitMaxHp(NextRA::turretTypeId),
        false,
        false,
        NextRA::Sim::WPos::FromCells(-4, -5));
    for (int32_t z = -6; z <= -4; ++z)
    {
        simWorld_.SpawnBuilding(
            0,
            NextRA::wallTypeId,
            NextRA::Sim::WPos::FromCells(-2, z),
            NextRA::UnitMaxHp(NextRA::wallTypeId),
            false,
            false,
            NextRA::Sim::WPos::FromCells(-2, z));
    }
    simWorld_.SpawnBuilding(
        1,
        NextRA::baseTypeId,
        NextRA::Sim::WPos::FromCells(8, 7),
        NextRA::BaseMaxHp(),
        true,
        false,
        NextRA::Sim::WPos::FromCells(6, 7));
    simWorld_.SpawnBuilding(
        1,
        NextRA::barracksTypeId,
        NextRA::Sim::WPos::FromCells(7, 4),
        NextRA::BarracksMaxHp(),
        false,
        true,
        NextRA::Sim::WPos::FromCells(5, 4));
    simWorld_.SpawnMobile(
        1,
        NextRA::tankTypeId,
        NextRA::Sim::WPos::FromCells(4, 1),
        NextRA::Sim::WPos::FromCells(4, 1),
        NextRA::UnitSpeedPerTick(NextRA::tankTypeId),
        false);
    simWorld_.SpawnMobile(
        1,
        NextRA::rocketeerTypeId,
        NextRA::Sim::WPos::FromCells(5, -1),
        NextRA::Sim::WPos::FromCells(5, -1),
        NextRA::UnitSpeedPerTick(NextRA::rocketeerTypeId),
        false);
    simWorld_.SpawnBuilding(
        1,
        NextRA::turretTypeId,
        NextRA::Sim::WPos::FromCells(4, 5),
        NextRA::UnitMaxHp(NextRA::turretTypeId),
        false,
        false,
        NextRA::Sim::WPos::FromCells(4, 5));
    for (int32_t z = 4; z <= 6; ++z)
    {
        simWorld_.SpawnBuilding(
            1,
            NextRA::wallTypeId,
            NextRA::Sim::WPos::FromCells(2, z),
            NextRA::UnitMaxHp(NextRA::wallTypeId),
            false,
            false,
            NextRA::Sim::WPos::FromCells(2, z));
    }

}

void NextRAGameInstance::StepSim(double deltaSeconds)
{
    accumulator_ += deltaSeconds;
    int catchupTicks = 0;
    while (accumulator_ >= NextRA::simStepSeconds && catchupTicks < NextRA::maxCatchupTicksPerFrame)
    {
        orderManager_.ReceiveOrders(0, nextTick_, {});
        DrainInjectedAIOrders();
        SubmitAIOrders();
        if (!orderManager_.CanAdvance(nextTick_))
        {
            break;
        }
        const std::vector<NextRA::Net::FOrder> orders = orderManager_.ConsumeExecOrders(nextTick_);
        NextRA::Sim::ApplyOrders(simWorld_, pathGrid_, orders);
        simWorld_.Step(nextTick_);
        ++nextTick_;
        accumulator_ -= NextRA::simStepSeconds;
        ++catchupTicks;
    }

    if (catchupTicks == NextRA::maxCatchupTicksPerFrame && accumulator_ >= NextRA::simStepSeconds)
    {
        accumulator_ = NextRA::simStepSeconds;
    }

    renderAlpha_ = static_cast<float>(glm::clamp(accumulator_ / NextRA::simStepSeconds, 0.0, 1.0));
}

void NextRAGameInstance::RebindRenderNodes()
{
    actorNodes_.clear();
    renderProxy_.Clear();

    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FRenderLink* renderLink = simWorld_.TryGetRenderLink(actor);
        if (!renderLink)
        {
            continue;
        }

        std::shared_ptr<Assets::Node> node = GetEngine().GetScene().GetNodeSharedByInstanceId(renderLink->renderNodeId);
        if (!node)
        {
            SPDLOG_WARN("NextRA: render node {} for actor {} was not found after scene load",
                        renderLink->renderNodeId, actor);
            continue;
        }

        std::shared_ptr<Assets::Node> turretNode;
        if (renderLink->turretNodeId != 0)
        {
            turretNode = GetEngine().GetScene().GetNodeSharedByInstanceId(renderLink->turretNodeId);
        }
        renderProxy_.BindNode(renderLink->renderNodeId, node, turretNode);
        actorNodes_.push_back(node);
    }
}

std::shared_ptr<Assets::Node> NextRAGameInstance::CreateActorRenderNode(
    NextRA::Sim::FActorId actor,
    const NextRA::Sim::FSimTransform& transform,
    const NextRA::Sim::FOwner& owner,
    const NextRA::Sim::FUnitType& unitType,
    uint32_t renderNodeId,
    std::vector<std::shared_ptr<Assets::Node>>* sceneNodes,
    uint32_t& nextSceneNodeId,
    uint32_t& outTurretNodeId)
{
    outTurretNodeId = 0;

    auto allocateNodeId = [&]() -> uint32_t {
        if (sceneNodes)
        {
            return nextSceneNodeId++;
        }
        return GetEngine().GetScene().GenerateInstanceId();
    };
    auto addNode = [&](const std::shared_ptr<Assets::Node>& node) {
        if (sceneNodes)
        {
            sceneNodes->push_back(node);
        }
        else
        {
            GetEngine().GetScene().AddNode(node);
        }
    };

    const bool playerOwned = owner.playerId == 0;
    const uint32_t ownerMatId = playerOwned ? playerMatId_ : enemyMatId_;
    const uint32_t rocketMatId = playerOwned ? playerRocketMatId_ : enemyRocketMatId_;
    uint32_t modelId = infantryModelId_;
    uint32_t materialId = ownerMatId;
    if (unitType.typeId == NextRA::rocketeerTypeId)
    {
        modelId = rocketeerModelId_;
        materialId = rocketMatId;
    }
    else if (unitType.typeId == NextRA::tankTypeId)
    {
        modelId = tankBodyModelId_;
    }
    else if (unitType.typeId == NextRA::barracksTypeId)
    {
        modelId = barracksModelId_;
    }
    else if (unitType.typeId == NextRA::baseTypeId)
    {
        modelId = baseModelId_;
    }
    else if (unitType.typeId == NextRA::turretTypeId)
    {
        modelId = turretBaseModelId_;
    }
    else if (unitType.typeId == NextRA::wallTypeId)
    {
        modelId = wallModelId_;
        materialId = wallMatId_;
    }

    auto root = Assets::SceneBuilder::CreateRenderNode(
        fmt::format("NextRA_Actor_{}", actor),
        ToRenderVec3(transform.pos),
        glm::vec3(1.0f),
        renderNodeId,
        modelId,
        materialId,
        true);
    addNode(root);

    auto addChild = [&](const char* suffix,
                        uint32_t childModelId,
                        uint32_t childMaterialId,
                        const glm::vec3& localPos,
                        const std::shared_ptr<Assets::Node>& parent) -> std::shared_ptr<Assets::Node> {
        const uint32_t childNodeId = allocateNodeId();
        auto child = Assets::SceneBuilder::CreateRenderNode(
            fmt::format("NextRA_Actor_{}_{}", actor, suffix),
            localPos,
            glm::vec3(1.0f),
            childNodeId,
            childModelId,
            childMaterialId,
            true);
        child->SetParent(parent);
        addNode(child);
        return child;
    };

    if (unitType.typeId == NextRA::tankTypeId)
    {
        auto turret = addChild("Turret", tankTurretModelId_, ownerMatId, glm::vec3(0.0f, 0.55f, 0.0f), root);
        outTurretNodeId = turret->GetInstanceId();
        addChild("Barrel", tankBarrelModelId_, metalMatId_, glm::vec3(0.0f, 0.08f, 0.16f), turret);
    }
    else if (unitType.typeId == NextRA::turretTypeId)
    {
        auto turret = addChild("Turret", turretHeadModelId_, ownerMatId, glm::vec3(0.0f, 0.54f, 0.0f), root);
        outTurretNodeId = turret->GetInstanceId();
        addChild("Barrel", turretBarrelModelId_, metalMatId_, glm::vec3(0.0f, 0.07f, 0.10f), turret);
    }

    return root;
}

void NextRAGameInstance::CreateRenderNodeForActor(NextRA::Sim::FActorId actor)
{
    if (simWorld_.TryGetRenderLink(actor))
    {
        return;
    }

    const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
    const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
    const NextRA::Sim::FUnitType* unitType = simWorld_.TryGetUnitType(actor);
    if (!transform || !owner || !unitType)
    {
        return;
    }

    const uint32_t renderNodeId = GetEngine().GetScene().GenerateInstanceId();
    uint32_t nextSceneNodeId = 0;
    uint32_t turretNodeId = 0;
    std::shared_ptr<Assets::Node> node =
        CreateActorRenderNode(actor, *transform, *owner, *unitType, renderNodeId, nullptr, nextSceneNodeId, turretNodeId);
    simWorld_.SetRenderLink(actor, renderNodeId, turretNodeId);
    std::shared_ptr<Assets::Node> turretNode;
    if (turretNodeId != 0)
    {
        turretNode = GetEngine().GetScene().GetNodeSharedByInstanceId(turretNodeId);
    }
    renderProxy_.BindNode(renderNodeId, node, turretNode);
    actorNodes_.push_back(std::move(node));
}

void NextRAGameInstance::SyncSpawnedRenderNodes()
{
    for (NextRA::Sim::FActorId actor : simWorld_.ConsumeSpawnedActorIds())
    {
        CreateRenderNodeForActor(actor);
    }
}

glm::vec3 NextRAGameInstance::GetFirstActorRenderNodePos() const
{
    if (actorNodes_.empty() || !actorNodes_.front())
    {
        return glm::vec3(0.0f);
    }
    return actorNodes_.front()->WorldTranslation();
}

bool NextRAGameInstance::TryGetGroundHit(const glm::dvec2& screenPos, NextRA::Sim::WPos& outPos) const
{
    glm::vec3 rayOrigin{0.0f};
    glm::vec3 rayDir{0.0f};
    Runtime::EngineHelper::GetScreenToWorldRay(glm::vec2(screenPos), rayOrigin, rayDir);
    if (std::abs(rayDir.y) < 0.0001f)
    {
        return false;
    }

    const float t = -rayOrigin.y / rayDir.y;
    if (t < 0.0f)
    {
        return false;
    }

    const glm::vec3 hit = rayOrigin + rayDir * t;
    outPos = FromRenderVec3(hit);
    return true;
}

void NextRAGameInstance::SelectAt(const glm::dvec2& screenPos, bool additive)
{
    const NextRA::Sim::FActorId bestActor = TryPickOwnedActorAt(screenPos);

    if (!additive)
    {
        selectedActors_.clear();
    }
    if (bestActor != static_cast<NextRA::Sim::FActorId>(-1) && !IsSelected(bestActor))
    {
        selectedActors_.push_back(bestActor);
    }
}

void NextRAGameInstance::SelectInRect(const glm::dvec2& a, const glm::dvec2& b, bool additive)
{
    const double minX = std::min(a.x, b.x);
    const double maxX = std::max(a.x, b.x);
    const double minY = std::min(a.y, b.y);
    const double maxY = std::max(a.y, b.y);

    if (!additive)
    {
        selectedActors_.clear();
    }

    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        if (!owner || owner->playerId != 0 || !transform || IsSelected(actor))
        {
            continue;
        }

        ImVec2 screen;
        if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(transform->pos), screen))
        {
            continue;
        }
        if (screen.x >= minX && screen.x <= maxX && screen.y >= minY && screen.y <= maxY)
        {
            selectedActors_.push_back(actor);
        }
    }
}

void NextRAGameInstance::IssueMoveCommand(const NextRA::Sim::WPos& targetPos)
{
    if (selectedActors_.empty())
    {
        return;
    }

    for (size_t i = 0; i < selectedActors_.size(); ++i)
    {
        NextRA::Net::FOrder order;
        const bool attackMove = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        order.type = attackMove ? NextRA::Net::EOrderType::AttackMove : NextRA::Net::EOrderType::Move;
        order.playerId = 0;
        order.issueTick = nextTick_;
        order.actorIds.push_back(selectedActors_[i]);

        const int32_t row = static_cast<int32_t>(i / 3);
        const int32_t col = static_cast<int32_t>(i % 3);
        order.targetPos = targetPos;
        order.targetPos.x += NextRA::Sim::FFixed::FromInt((col - 1) * NextRA::Sim::cellSubUnits);
        order.targetPos.z += NextRA::Sim::FFixed::FromInt(row * NextRA::Sim::cellSubUnits);
        if (i == 0)
        {
            if (const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(selectedActors_[i]))
            {
                debugPath_ = pathGrid_.FindPath(transform->pos.ToCell(), order.targetPos.ToCell());
            }
        }
        SubmitLocalOrderWithLog(std::move(order), attackMove ? "AttackMove" : "Move");
    }
}

void NextRAGameInstance::IssueAttackCommand(NextRA::Sim::FActorId targetActor)
{
    if (selectedActors_.empty())
    {
        return;
    }

    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Attack;
    order.playerId = 0;
    order.issueTick = nextTick_;
    order.actorIds = selectedActors_;
    order.targetActor = targetActor;
    if (const NextRA::Sim::FSimTransform* targetTransform = simWorld_.TryGetTransform(targetActor);
        targetTransform && !selectedActors_.empty())
    {
        if (const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(selectedActors_.front()))
        {
            debugPath_ = pathGrid_.FindPath(transform->pos.ToCell(), targetTransform->pos.ToCell());
        }
    }
    SubmitLocalOrderWithLog(std::move(order), "Attack");
}

void NextRAGameInstance::IssueProduceCommand(uint16_t produceTypeId)
{
    const NextRA::Sim::FActorId productionActor = FindSelectedProductionActor();
    if (productionActor == static_cast<NextRA::Sim::FActorId>(-1))
    {
        return;
    }

    NextRA::Net::FOrder order;
    order.type = NextRA::Net::EOrderType::Produce;
    order.playerId = 0;
    order.issueTick = nextTick_;
    order.actorIds = {productionActor};
    order.produceTypeId = produceTypeId;
    SubmitLocalOrderWithLog(std::move(order), "Produce");
}

void NextRAGameInstance::SubmitLocalOrderWithLog(NextRA::Net::FOrder order, const char* label)
{
    (void)label;
    orderLog_.push_back(FOrderLogEntry{
        order.issueTick,
        order.playerId,
        order.type,
        static_cast<uint16_t>(std::min<size_t>(order.actorIds.size(), 0xffffu)),
    });
    if (orderLog_.size() > 64)
    {
        orderLog_.erase(orderLog_.begin(), orderLog_.begin() + static_cast<std::ptrdiff_t>(orderLog_.size() - 64));
    }
    orderManager_.SubmitLocalOrder(std::move(order));
}

void NextRAGameInstance::SubmitAIOrderWithInjection(NextRA::Net::FOrder order)
{
    ++injectedOrderCounter_;
    if (dropEveryNthPacket_ > 0 && injectedOrderCounter_ % static_cast<uint32_t>(dropEveryNthPacket_) == 0)
    {
        return;
    }

    uint32_t delay = static_cast<uint32_t>(std::max(0, artificialDelayTicks_));
    if (reorderEveryNthPacket_ > 0 && injectedOrderCounter_ % static_cast<uint32_t>(reorderEveryNthPacket_) == 0)
    {
        delay += 5;
    }

    if (delay == 0)
    {
        SubmitLocalOrderWithLog(std::move(order), "AI");
        return;
    }

    injectedAIOrders_.push_back(FInjectedOrder{nextTick_ + delay, std::move(order)});
}

void NextRAGameInstance::DrainInjectedAIOrders()
{
    for (auto it = injectedAIOrders_.begin(); it != injectedAIOrders_.end();)
    {
        if (it->deliverTick > nextTick_)
        {
            ++it;
            continue;
        }

        it->order.issueTick = nextTick_;
        SubmitLocalOrderWithLog(std::move(it->order), "AI Delayed");
        it = injectedAIOrders_.erase(it);
    }
}

void NextRAGameInstance::SubmitAIOrders()
{
    if (!aiEnabled_ || simWorld_.WinnerPlayerId() >= 0)
    {
        return;
    }

    if (nextTick_ >= aiNextProduceTick_)
    {
        aiNextProduceTick_ = nextTick_ + 120;
        for (NextRA::Sim::FActorId actor : simWorld_.Actors())
        {
            const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
            const NextRA::Sim::FProduction* production = simWorld_.TryGetProduction(actor);
            if (!owner || owner->playerId != 1 || !production || production->queuedTypeId != 0)
            {
                continue;
            }

            NextRA::Net::FOrder order;
            order.type = NextRA::Net::EOrderType::Produce;
            order.playerId = 1;
            order.issueTick = nextTick_;
            order.actorIds = {actor};
            const uint32_t wave = (nextTick_ / 120) % 3;
            order.produceTypeId =
                wave == 0 ? NextRA::infantryTypeId : (wave == 1 ? NextRA::rocketeerTypeId : NextRA::tankTypeId);
            SubmitAIOrderWithInjection(std::move(order));
            break;
        }
    }

    if (nextTick_ < aiNextAttackTick_)
    {
        return;
    }

    aiNextAttackTick_ = nextTick_ + 180;
    const NextRA::Sim::FActorId playerBase = FindBaseForPlayer(0);
    const NextRA::Sim::FSimTransform* baseTransform = simWorld_.TryGetTransform(playerBase);
    if (!baseTransform)
    {
        return;
    }

    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        if (!owner || owner->playerId != 1 || !simWorld_.TryGetMobile(actor))
        {
            continue;
        }

        NextRA::Net::FOrder order;
        order.type = NextRA::Net::EOrderType::AttackMove;
        order.playerId = 1;
        order.issueTick = nextTick_;
        order.actorIds = {actor};
        order.targetPos = baseTransform->pos;
        SubmitAIOrderWithInjection(std::move(order));
    }
}

NextRA::Sim::FActorId NextRAGameInstance::FindBaseForPlayer(uint8_t playerId) const
{
    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        if (owner && owner->playerId == playerId && simWorld_.IsBase(actor))
        {
            return actor;
        }
    }
    return static_cast<NextRA::Sim::FActorId>(-1);
}

NextRA::Sim::FActorId NextRAGameInstance::FindSelectedProductionActor() const
{
    for (NextRA::Sim::FActorId actor : selectedActors_)
    {
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        if (owner && owner->playerId == 0 && simWorld_.TryGetProduction(actor))
        {
            return actor;
        }
    }
    return static_cast<NextRA::Sim::FActorId>(-1);
}

NextRA::Sim::FActorId NextRAGameInstance::TryPickEnemyAt(const glm::dvec2& screenPos) const
{
    NextRA::Sim::WPos hit;
    if (!TryGetGroundHit(screenPos, hit))
    {
        return static_cast<NextRA::Sim::FActorId>(-1);
    }

    constexpr NextRA::Sim::FFixed selectRadius = NextRA::Sim::FFixed::FromInt(900);
    NextRA::Sim::FActorId bestActor = static_cast<NextRA::Sim::FActorId>(-1);
    NextRA::Sim::FFixed bestDistance = NextRA::Sim::FFixed::FromInt(999999);
    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        const NextRA::Sim::FHealth* health = simWorld_.TryGetHealth(actor);
        if (!owner || owner->playerId == 0 || !transform || !health || health->hp <= 0)
        {
            continue;
        }

        const NextRA::Sim::FFixed distance = NextRA::Sim::Length2D(transform->pos - hit);
        if (distance <= selectRadius && (bestActor == static_cast<NextRA::Sim::FActorId>(-1) || distance < bestDistance))
        {
            bestDistance = distance;
            bestActor = actor;
        }
    }
    return bestActor;
}

NextRA::Sim::FActorId NextRAGameInstance::TryPickOwnedActorAt(const glm::dvec2& screenPos) const
{
    NextRA::Sim::WPos hit;
    if (!TryGetGroundHit(screenPos, hit))
    {
        return static_cast<NextRA::Sim::FActorId>(-1);
    }

    constexpr NextRA::Sim::FFixed selectRadius = NextRA::Sim::FFixed::FromInt(900);
    NextRA::Sim::FActorId bestActor = static_cast<NextRA::Sim::FActorId>(-1);
    NextRA::Sim::FFixed bestDistance = NextRA::Sim::FFixed::FromInt(999999);
    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        const NextRA::Sim::FHealth* health = simWorld_.TryGetHealth(actor);
        if (!owner || owner->playerId != 0 || !transform || !health || health->hp <= 0)
        {
            continue;
        }

        const NextRA::Sim::FFixed distance = NextRA::Sim::Length2D(transform->pos - hit);
        if (distance <= selectRadius && (bestActor == static_cast<NextRA::Sim::FActorId>(-1) || distance < bestDistance))
        {
            bestDistance = distance;
            bestActor = actor;
        }
    }
    return bestActor;
}

void NextRAGameInstance::HideDestroyedRenderNodes()
{
    for (uint32_t renderNodeId : simWorld_.ConsumeDestroyedRenderNodeIds())
    {
        std::shared_ptr<Assets::Node> node = GetEngine().GetScene().GetNodeSharedByInstanceId(renderNodeId);
        if (node)
        {
            node->SetTranslation(glm::vec3(0.0f, -1000.0f, 0.0f));
        }
    }
}

bool NextRAGameInstance::IsSelected(NextRA::Sim::FActorId actor) const
{
    return std::find(selectedActors_.begin(), selectedActors_.end(), actor) != selectedActors_.end();
}

void NextRAGameInstance::DrawSelectionOverlay() const
{
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
    {
        return;
    }

    for (NextRA::Sim::FActorId actor : selectedActors_)
    {
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        if (!transform)
        {
            continue;
        }

        ImVec2 center;
        ImVec2 edge;
        const glm::vec3 world = ToRenderVec3(transform->pos);
        if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, center) &&
            Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world + glm::vec3(0.75f, 0.0f, 0.0f), edge))
        {
            drawList->AddCircle(center, std::max(8.0f, std::abs(edge.x - center.x)), IM_COL32(80, 220, 255, 230), 32, 2.0f);
        }
    }

    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        const NextRA::Sim::FHealth* health = simWorld_.TryGetHealth(actor);
        if (!transform || !health || health->hp <= 0)
        {
            continue;
        }

        ImVec2 center;
        if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(transform->pos) + glm::vec3(0.0f, 0.9f, 0.0f), center))
        {
            continue;
        }

        const float width = 34.0f;
        const float height = 4.0f;
        const float hpRatio = glm::clamp(static_cast<float>(health->hp) / static_cast<float>(std::max(1, health->maxHp)), 0.0f, 1.0f);
        const ImVec2 min(center.x - width * 0.5f, center.y - height * 0.5f);
        const ImVec2 max(center.x + width * 0.5f, center.y + height * 0.5f);
        drawList->AddRectFilled(min, max, IM_COL32(35, 35, 35, 220));
        drawList->AddRectFilled(min, ImVec2(min.x + width * hpRatio, max.y), IM_COL32(80, 220, 90, 230));
    }

    if (leftMouseDown_ && hasMousePos_ && DistanceSquared(dragStart_, mousePos_) > 64.0)
    {
        const ImVec2 a(static_cast<float>(dragStart_.x), static_cast<float>(dragStart_.y));
        const ImVec2 b(static_cast<float>(mousePos_.x), static_cast<float>(mousePos_.y));
        drawList->AddRect(a, b, IM_COL32(80, 220, 255, 220), 0.0f, 0, 1.5f);
        drawList->AddRectFilled(a, b, IM_COL32(80, 220, 255, 35));
    }
}

void NextRAGameInstance::DrawDebugGridOverlay() const
{
    if (!showDebugGrid_)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
    {
        return;
    }

    const NextRA::Sim::CPos origin = pathGrid_.Origin();
    const int32_t width = pathGrid_.Width();
    const int32_t height = pathGrid_.Height();
    for (int32_t z = origin.z; z <= origin.z + height; z += 4)
    {
        ImVec2 a;
        ImVec2 b;
        if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(origin.x, z)), a) &&
            Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(origin.x + width, z)), b))
        {
            drawList->AddLine(a, b, IM_COL32(255, 255, 255, 32), 1.0f);
        }
    }
    for (int32_t x = origin.x; x <= origin.x + width; x += 4)
    {
        ImVec2 a;
        ImVec2 b;
        if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(x, origin.z)), a) &&
            Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(x, origin.z + height)), b))
        {
            drawList->AddLine(a, b, IM_COL32(255, 255, 255, 32), 1.0f);
        }
    }

    for (int32_t z = origin.z; z < origin.z + height; ++z)
    {
        for (int32_t x = origin.x; x < origin.x + width; ++x)
        {
            const NextRA::Sim::CPos cell{x, z};
            if (pathGrid_.IsPassable(cell))
            {
                continue;
            }

            ImVec2 center;
            if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(x, z)), center))
            {
                drawList->AddCircleFilled(center, 5.0f, IM_COL32(255, 220, 80, 170), 12);
            }
        }
    }

    for (size_t index = 1; index < debugPath_.size(); ++index)
    {
        ImVec2 a;
        ImVec2 b;
        if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(debugPath_[index - 1].x, debugPath_[index - 1].z)), a) &&
            Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, ToRenderVec3(NextRA::Sim::WPos::FromCells(debugPath_[index].x, debugPath_[index].z)), b))
        {
            drawList->AddLine(a, b, IM_COL32(80, 240, 255, 220), 3.0f);
        }
    }
}

void NextRAGameInstance::DrawMinimap() const
{
    if (!showMinimap_)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 size(172.0f, 172.0f);
    const ImVec2 min(viewport->WorkPos.x + viewport->WorkSize.x - size.x - 16.0f,
                     viewport->WorkPos.y + viewport->WorkSize.y - size.y - 16.0f);
    const ImVec2 max(min.x + size.x, min.y + size.y);
    drawList->AddRectFilled(min, max, IM_COL32(12, 18, 18, 190));
    drawList->AddRect(min, max, IM_COL32(180, 210, 210, 210), 0.0f, 0, 1.0f);

    auto toMini = [&](const NextRA::Sim::WPos& pos) {
        const float x = (pos.x.ToFloat() / static_cast<float>(NextRA::Sim::cellSubUnits) + 20.0f) / 40.0f;
        const float z = (pos.z.ToFloat() / static_cast<float>(NextRA::Sim::cellSubUnits) + 20.0f) / 40.0f;
        return ImVec2(min.x + glm::clamp(x, 0.0f, 1.0f) * size.x,
                      min.y + glm::clamp(z, 0.0f, 1.0f) * size.y);
    };

    for (NextRA::Sim::FActorId actor : simWorld_.Actors())
    {
        const NextRA::Sim::FSimTransform* transform = simWorld_.TryGetTransform(actor);
        const NextRA::Sim::FOwner* owner = simWorld_.TryGetOwner(actor);
        const NextRA::Sim::FHealth* health = simWorld_.TryGetHealth(actor);
        if (!transform || !owner || !health || health->hp <= 0)
        {
            continue;
        }

        const ImU32 color = owner->playerId == 0 ? IM_COL32(80, 160, 255, 240) : IM_COL32(255, 100, 70, 240);
        drawList->AddCircleFilled(toMini(transform->pos), simWorld_.IsBase(actor) ? 5.0f : 3.0f, color, 12);
    }
}

void NextRAGameInstance::DrawVictoryBanner() const
{
    if (simWorld_.WinnerPlayerId() < 0)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (!drawList)
    {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const char* text = simWorld_.WinnerPlayerId() == 0 ? "Victory" : "Defeat";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 center(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                        viewport->WorkPos.y + viewport->WorkSize.y * 0.18f);
    const ImVec2 min(center.x - textSize.x * 0.5f - 24.0f, center.y - 20.0f);
    const ImVec2 max(center.x + textSize.x * 0.5f + 24.0f, center.y + 28.0f);
    drawList->AddRectFilled(min, max, IM_COL32(12, 18, 18, 220));
    drawList->AddRect(min, max, IM_COL32(255, 255, 255, 210), 0.0f, 0, 1.0f);
    drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f), IM_COL32(245, 245, 245, 255), text);
}
