#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Loaders/FProcModel.h"
#include "Brotato3DAudio.hpp"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Scene/SceneBuilder.h"

#include <spdlog/spdlog.h>

using namespace Brotato3DUtil;

namespace
{
    constexpr int TinyDebrisCount = 800;
    constexpr int ChunkDebrisCount = 480;
    constexpr int BossChunkDebrisCount = 80;
    constexpr int RatKinematicBodyCount = 96;
    constexpr int EnemyKinematicBodyCount = 32;
    constexpr int BossKinematicBodyCount = 4;
    // TODO: drop EnableKinematicDebrisPush guard once the kinematic body engine API stabilizes.
    constexpr bool EnableKinematicDebrisPush = true;

    constexpr glm::vec3 TinyHalfExtent(0.08f);
    constexpr glm::vec3 ChunkHalfExtent(0.18f);
    constexpr glm::vec3 BossChunkHalfExtent(0.36f);
    constexpr float DebrisSpawnOffsetJitter = 0.08f;
    constexpr float DebrisSpeedJitterMin = 0.7f;
    constexpr float DebrisSpeedJitterMax = 1.3f;
    constexpr float DebrisLiftMin = 0.25f;
    constexpr float DebrisLiftMax = 0.65f;
    constexpr float DebrisAngularSpeedMin = 8.0f;
    constexpr float DebrisAngularSpeedMax = 16.0f;
    constexpr float DebrisLengthEpsilon = 0.001f;
    constexpr float DebrisPickablePhysicsMs = 1000.0f;
    constexpr float DebrisMagneticHeightOffset = 0.15f;
    constexpr float DebrisMagneticProgressSpeed = 5.0f;
    constexpr int MaxXpChunksPerHit = 12;
    constexpr float HitXpSurfaceOffset = 0.2f;
    constexpr float HitXpSpeed = 5.0f;
    constexpr float HitXpConeRad = 0.6f;
    constexpr float KinematicMoveStepSeconds = 1.0f / 60.0f;
    constexpr float ArenaWallHeight = 5.0f;
    constexpr float ArenaWallThickness = 0.5f;
    constexpr float ArenaWallHalfHeight = ArenaWallHeight * 0.5f;
    constexpr float ArenaWallHalfThickness = ArenaWallThickness * 0.5f;

    glm::quat RandomRotation(std::mt19937& rng)
    {
        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> axisDist(-1.0f, 1.0f);
        glm::vec3 axis(axisDist(rng), axisDist(rng), axisDist(rng));
        if (glm::length(axis) < DebrisLengthEpsilon)
        {
            axis = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        return glm::angleAxis(angleDist(rng), glm::normalize(axis));
    }

    glm::vec3 SampleConeDirection(const glm::vec3& centerDir, float angleConeRad, std::mt19937& rng)
    {
        glm::vec3 center = centerDir;
        if (glm::length(center) < DebrisLengthEpsilon)
        {
            center = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        center = glm::normalize(center);

        if (angleConeRad <= DebrisLengthEpsilon)
        {
            return center;
        }

        const glm::vec3 helper = std::abs(center.y) > 0.92f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 tangent = glm::normalize(glm::cross(helper, center));
        const glm::vec3 bitangent = glm::normalize(glm::cross(center, tangent));

        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
        std::uniform_real_distribution<float> phiDist(0.0f, glm::two_pi<float>());
        const float cosMin = std::cos(angleConeRad);
        const float cosTheta = glm::mix(cosMin, 1.0f, std::sqrt(unitDist(rng)));
        const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        const float phi = phiDist(rng);
        return glm::normalize(center * cosTheta + (std::cos(phi) * tangent + std::sin(phi) * bitangent) * sinTheta);
    }

    bool SameBody(const NextBodyID& lhs, const NextBodyID& rhs)
    {
        return !lhs.IsInvalid() && !rhs.IsInvalid() && lhs == rhs;
    }
}

void Brotato3DGameInstance::BuildDebrisPool(std::vector<Assets::Model>& models,
                                            std::vector<Assets::FMaterial>& materials,
                                            std::vector<std::shared_ptr<Assets::Node>>& nodes)
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (physics)
    {
        for (const Brotato3D::FDebrisRuntime& slot : debrisPool_)
        {
            if (!slot.bodyId.IsInvalid() && physics->GetBody(slot.bodyId))
            {
                physics->RemoveBody(slot.bodyId);
            }
        }
    }
    debrisPool_.clear();
    debrisTickCounter_ = 0;

    models.push_back(Assets::FProcModel::CreateBox(-TinyHalfExtent, TinyHalfExtent));
    debrisTinyModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(-ChunkHalfExtent, ChunkHalfExtent));
    debrisChunkModelId_ = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(-BossChunkHalfExtent, BossChunkHalfExtent));
    debrisBossChunkModelId_ = static_cast<uint32_t>(models.size() - 1);

    debrisFallbackTinyMatId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.18f, 0.08f));
    debrisFallbackChunkMatId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.5f));
    materialDebrisMatId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.85f, 0.15f));
    xpDebrisMatId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.2f, 1.0f, 0.35f));

    const Brotato3D::FCharacterDef* selectedCharacter = FindCharacterDef(selectedCharacterId_);
    const glm::vec3 playerColor = selectedCharacter ? selectedCharacter->color :
                                      (characterDefs_.empty() ? glm::vec3(0.20f, 0.75f, 0.30f) : characterDefs_.front().color);
    playerDebrisMatId_ = SceneBuilder::AddLambertianMaterial(materials, playerColor * 0.6f + glm::vec3(0.4f));

    auto appendPool = [&](Brotato3D::EDebrisKind kind,
                          int count,
                          uint32_t modelId,
                          uint32_t materialId,
                          const glm::vec3& halfExtent,
                          const char* namePrefix)
    {
        debrisPool_.reserve(debrisPool_.size() + static_cast<size_t>(count));
        const glm::vec3 fullExtent = halfExtent * 2.0f;
        for (int index = 0; index < count; ++index)
        {
            auto node = SceneBuilder::CreateRenderNode(fmt::format("{}_{}", namePrefix, index),
                                                       HiddenPosition,
                                                       glm::vec3(1.0f),
                                                       static_cast<uint32_t>(nodes.size()),
                                                       modelId,
                                                       materialId,
                                                       false);
            auto physicsComponent = std::make_shared<Runtime::PhysicsComponent>();
            physicsComponent->SetMobility(Runtime::ENodeMobility::Dynamic);
            NextBodyID bodyId{};
            if (physics)
            {
                bodyId = physics->CreateBoxBody(HiddenPosition, fullExtent, NextMotionType::Dynamic);
                physics->SetBodyTransform(bodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
                physics->SetBodyVelocity(bodyId, glm::vec3(0.0f), glm::vec3(0.0f));
                physics->SetBodyActive(bodyId, false);
                physicsComponent->BindPhysicsBody(bodyId);
            }
            node->AddComponent(physicsComponent);
            NodeUtils::SetVisible(node, false);
            nodes.push_back(node);

            Brotato3D::FDebrisRuntime slot{};
            slot.kind = kind;
            slot.bodyId = bodyId;
            slot.node = node;
            slot.baseModelId = modelId;
            slot.currentMaterialId = materialId;
            debrisPool_.push_back(slot);
        }
    };

    appendPool(Brotato3D::EDebrisKind::Tiny, TinyDebrisCount, debrisTinyModelId_, debrisFallbackTinyMatId_,
               TinyHalfExtent, "Brotato3D_DebrisTiny");
    appendPool(Brotato3D::EDebrisKind::Chunk, ChunkDebrisCount, debrisChunkModelId_, debrisFallbackChunkMatId_,
               ChunkHalfExtent, "Brotato3D_DebrisChunk");
    appendPool(Brotato3D::EDebrisKind::BossChunk, BossChunkDebrisCount, debrisBossChunkModelId_, debrisFallbackChunkMatId_,
               BossChunkHalfExtent, "Brotato3D_DebrisBossChunk");

    spdlog::info("[Brotato3D] debris pool created: {} dynamic bodies", debrisPool_.size());
}

void Brotato3DGameInstance::BuildKinematicCollisionBodies(std::vector<Assets::Model>& models,
                                                          std::vector<Assets::FMaterial>& materials,
                                                          std::vector<std::shared_ptr<Assets::Node>>& nodes)
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics)
    {
        return;
    }

    if (!playerKinematicBodyId_.IsInvalid() && physics->GetBody(playerKinematicBodyId_))
    {
        physics->RemoveBody(playerKinematicBodyId_);
    }
    for (const auto& [enemyId, bodies] : enemyKinematicBodyPools_)
    {
        (void)enemyId;
        for (const NextBodyID& bodyId : bodies)
        {
            if (!bodyId.IsInvalid() && physics->GetBody(bodyId))
            {
                physics->RemoveBody(bodyId);
            }
        }
    }
    enemyKinematicBodyPools_.clear();
    playerKinematicBodyId_ = {};
    playerKinematicBodyActive_ = false;

    if (!EnableKinematicDebrisPush)
    {
        spdlog::warn("[Brotato3D] kinematic debris push disabled; MoveKinematicBody needs investigation");
        return;
    }

    const uint32_t proxyMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.0f));
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), std::max(0.35f, player_.radius)));
    const uint32_t playerProxyModelId = static_cast<uint32_t>(models.size() - 1);
    // HACK: Scene::RebuildMeshBuffer auto-promotes manually-created primitive bodies to mesh bodies
    // when it sees a Node without a PhysicsComponent that owns the body. Workaround: attach a hidden
    // proxy Node with a PhysicsComponent (mobility tagged Dynamic to opt out of mesh promotion) that
    // claims ownership of the kinematic body. The body itself stays Kinematic at the physics layer.
    // TODO: replace with a `PhysicsComponent::IsManuallyBound()` flag in the engine layer.
    auto attachPhysicsProxyNode = [&nodes, proxyMaterialId](const std::string& name, uint32_t modelId, const NextBodyID& bodyId)
    {
        auto node = SceneBuilder::CreateRenderNode(name,
                                                   HiddenPosition,
                                                   glm::vec3(1.0f),
                                                   static_cast<uint32_t>(nodes.size()),
                                                   modelId,
                                                   proxyMaterialId,
                                                   false,
                                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                   false);
        auto physicsComponent = std::make_shared<Runtime::PhysicsComponent>();
        physicsComponent->SetMobility(Runtime::ENodeMobility::Dynamic);
        physicsComponent->BindPhysicsBody(bodyId);
        node->AddComponent(physicsComponent);
        nodes.push_back(node);
    };

    playerKinematicBodyId_ = physics->CreateSphereBody(HiddenPosition, std::max(0.35f, player_.radius), NextMotionType::Kinematic);
    physics->SetBodyTransform(playerKinematicBodyId_, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    physics->SetBodyActive(playerKinematicBodyId_, false);
    attachPhysicsProxyNode("Brotato3D_PlayerKinematicPushProxy", playerProxyModelId, playerKinematicBodyId_);

    size_t bodyCount = 1;
    for (const auto& [enemyId, def] : enemyDefs_)
    {
        const int count = def.boss.enabled ? BossKinematicBodyCount :
                          (def.name == "Rat" ? RatKinematicBodyCount : EnemyKinematicBodyCount);
        std::vector<NextBodyID>& pool = enemyKinematicBodyPools_[enemyId];
        pool.reserve(static_cast<size_t>(count));
        const glm::vec3 extent = glm::max(def.size, glm::vec3(0.2f));
        const uint32_t proxyModelId = enemyVisuals_.contains(enemyId) ? enemyVisuals_[enemyId].modelId : playerProxyModelId;
        for (int index = 0; index < count; ++index)
        {
            const NextBodyID bodyId = physics->CreateBoxBody(HiddenPosition, extent, NextMotionType::Kinematic);
            physics->SetBodyTransform(bodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            physics->SetBodyActive(bodyId, false);
            attachPhysicsProxyNode(fmt::format("Brotato3D_EnemyKinematicPushProxy_{}_{}", enemyId, index), proxyModelId, bodyId);
            pool.push_back(bodyId);
            ++bodyCount;
        }
    }

    spdlog::info("[Brotato3D] kinematic debris push bodies created: {}", bodyCount);
}

void Brotato3DGameInstance::BuildArenaWallBodies()
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics)
    {
        return;
    }

    ClearArenaWallBodies();
    const glm::vec3 northSouthExtent(arenaHalfExtent_.x * 2.0f + ArenaWallThickness,
                                     ArenaWallHeight,
                                     ArenaWallThickness);
    const glm::vec3 eastWestExtent(ArenaWallThickness,
                                   ArenaWallHeight,
                                   arenaHalfExtent_.y * 2.0f + ArenaWallThickness);

    arenaWallBodyIds_[0] = physics->CreateBoxBody(glm::vec3(0.0f, ArenaWallHalfHeight, -(arenaHalfExtent_.y + ArenaWallHalfThickness)),
                                                  northSouthExtent,
                                                  NextMotionType::Static);
    arenaWallBodyIds_[1] = physics->CreateBoxBody(glm::vec3(0.0f, ArenaWallHalfHeight, arenaHalfExtent_.y + ArenaWallHalfThickness),
                                                  northSouthExtent,
                                                  NextMotionType::Static);
    arenaWallBodyIds_[2] = physics->CreateBoxBody(glm::vec3(-(arenaHalfExtent_.x + ArenaWallHalfThickness), ArenaWallHalfHeight, 0.0f),
                                                  eastWestExtent,
                                                  NextMotionType::Static);
    arenaWallBodyIds_[3] = physics->CreateBoxBody(glm::vec3(arenaHalfExtent_.x + ArenaWallHalfThickness, ArenaWallHalfHeight, 0.0f),
                                                  eastWestExtent,
                                                  NextMotionType::Static);
    BuildArenaPropBodies();
}

void Brotato3DGameInstance::ClearArenaWallBodies()
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics)
    {
        arenaWallBodyIds_ = {};
        return;
    }

    for (NextBodyID& bodyId : arenaWallBodyIds_)
    {
        if (!bodyId.IsInvalid() && physics->GetBody(bodyId))
        {
            physics->RemoveBody(bodyId);
        }
        bodyId = {};
    }
    ClearArenaPropBodies();
}

void Brotato3DGameInstance::BuildArenaPropBodies()
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics)
    {
        return;
    }

    ClearArenaPropBodies();
    arenaPropBodyIds_.reserve(arenaResources_.propColliders.size());
    for (size_t index = 0; index < arenaResources_.propColliders.size(); ++index)
    {
        const Brotato3D::FArenaResources::FBoxCollider& collider = arenaResources_.propColliders[index];
        if (glm::length(collider.extent) <= 0.001f)
        {
            arenaPropBodyIds_.push_back({});
            continue;
        }
        const NextBodyID bodyId = physics->CreateBoxBody(collider.center, collider.rotation, collider.extent, NextMotionType::Dynamic);
        arenaPropBodyIds_.push_back(bodyId);
        if (index < arenaResources_.propNodes.size())
        {
            const std::shared_ptr<Assets::Node>& node = arenaResources_.propNodes[index];
            if (node)
            {
                auto physicsComponent = node->GetComponent<Runtime::PhysicsComponent>();
                if (!physicsComponent)
                {
                    physicsComponent = std::make_shared<Runtime::PhysicsComponent>();
                    node->AddComponent(physicsComponent);
                }
                physicsComponent->SetMobility(Runtime::ENodeMobility::Dynamic);
                physicsComponent->SetPhysicsOffset(glm::vec3(0.0f, collider.extent.y * 0.5f, 0.0f));
                physicsComponent->BindPhysicsBody(bodyId);
            }
        }
    }
    if (!arenaPropBodyIds_.empty())
    {
        spdlog::info("[Brotato3DPcg] dynamic prop bodies created: {}", arenaPropBodyIds_.size());
    }
}

void Brotato3DGameInstance::ClearArenaPropBodies()
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics)
    {
        arenaPropBodyIds_.clear();
        return;
    }

    for (NextBodyID& bodyId : arenaPropBodyIds_)
    {
        if (!bodyId.IsInvalid() && physics->GetBody(bodyId))
        {
            physics->RemoveBody(bodyId);
        }
    }
    arenaPropBodyIds_.clear();
}

void Brotato3DGameInstance::SpawnDebris(Brotato3D::EDebrisKind kind,
                                        const glm::vec3& worldPos,
                                        const glm::vec3& impulseDir,
                                        float speed,
                                        uint32_t materialId,
                                        int count,
                                        float angleConeRad,
                                        Brotato3D::EDebrisPayload payload,
                                        int payloadValuePerSlot,
                                        float lifetimeMs)
{
    if (count <= 0 || debrisPool_.empty())
    {
        return;
    }

    const bool pickable = payload != Brotato3D::EDebrisPayload::None;
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    std::vector<size_t> selectedSlots;
    selectedSlots.reserve(static_cast<size_t>(count));

    auto alreadySelected = [&selectedSlots](size_t index)
    {
        return std::find(selectedSlots.begin(), selectedSlots.end(), index) != selectedSlots.end();
    };

    for (int emitted = 0; emitted < count; ++emitted)
    {
        size_t selected = std::numeric_limits<size_t>::max();
        for (size_t index = 0; index < debrisPool_.size(); ++index)
        {
            if (!alreadySelected(index) && debrisPool_[index].kind == kind && !debrisPool_[index].active)
            {
                selected = index;
                break;
            }
        }
        if (selected == std::numeric_limits<size_t>::max())
        {
            uint64_t oldestTick = std::numeric_limits<uint64_t>::max();
            for (size_t index = 0; index < debrisPool_.size(); ++index)
            {
                const Brotato3D::FDebrisRuntime& slot = debrisPool_[index];
                if (!alreadySelected(index) && slot.kind == kind && slot.activatedTickId < oldestTick)
                {
                    oldestTick = slot.activatedTickId;
                    selected = index;
                }
            }
        }
        if (selected == std::numeric_limits<size_t>::max())
        {
            break;
        }
        selectedSlots.push_back(selected);
    }

    std::uniform_real_distribution<float> offsetDist(-DebrisSpawnOffsetJitter, DebrisSpawnOffsetJitter);
    std::uniform_real_distribution<float> speedDist(DebrisSpeedJitterMin, DebrisSpeedJitterMax);
    std::uniform_real_distribution<float> liftDist(DebrisLiftMin, DebrisLiftMax);
    std::uniform_real_distribution<float> angularDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> angularSpeedDist(DebrisAngularSpeedMin, DebrisAngularSpeedMax);

    glm::vec3 centerDir = impulseDir;
    if (glm::length(centerDir) < DebrisLengthEpsilon)
    {
        centerDir = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    centerDir = glm::normalize(centerDir);

    for (size_t selected : selectedSlots)
    {
        Brotato3D::FDebrisRuntime& slot = debrisPool_[selected];
        const glm::vec3 spawnPos = worldPos + glm::vec3(offsetDist(rng_), offsetDist(rng_), offsetDist(rng_));
        glm::vec3 direction = SampleConeDirection(centerDir, angleConeRad, rng_);
        direction = glm::normalize(direction + glm::vec3(0.0f, liftDist(rng_), 0.0f));
        glm::vec3 angularVelocity(angularDist(rng_), angularDist(rng_), angularDist(rng_));
        if (glm::length(angularVelocity) < DebrisLengthEpsilon)
        {
            angularVelocity = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        angularVelocity = glm::normalize(angularVelocity) * angularSpeedDist(rng_);
        const glm::quat rotation = RandomRotation(rng_);

        if (physics && !slot.bodyId.IsInvalid())
        {
            physics->SetBodyTransform(slot.bodyId, spawnPos, rotation, true);
            physics->SetBodyVelocity(slot.bodyId, direction * speed * speedDist(rng_), angularVelocity);
            physics->SetBodyActive(slot.bodyId, true);
        }
        if (slot.node)
        {
            slot.node->SetTranslation(spawnPos);
            slot.node->SetRotation(rotation);
            slot.node->SetScale(glm::vec3(1.0f));
            NodeUtils::SetPrimaryMaterial(slot.node, materialId);
            NodeUtils::SetOutlineFlags(slot.node, Runtime::RenderOutlineFlags::none);
            NodeUtils::SetVisible(slot.node, true);
        }

        slot.currentMaterialId = materialId;
        slot.activatedTickId = ++debrisTickCounter_;
        slot.active = true;
        slot.payload = payload;
        slot.pickupState = pickable ? Brotato3D::EPickupState::Physics : Brotato3D::EPickupState::None;
        slot.payloadValue = pickable ? payloadValuePerSlot : 0;
        slot.settleTimerMs = pickable ? DebrisPickablePhysicsMs : 0.0f;
        slot.magneticLerpProgress = 0.0f;
        slot.magneticPos = spawnPos;
        slot.remainingMs = pickable ? 0.0f : lifetimeMs;
    }
}

void Brotato3DGameInstance::UpdateDebris(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    const bool pickupEnabled = appState_ == Brotato3D::EAppState::Playing;
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    const float pickupRadius = PickupBaseRadius * (1.0f + effectiveStats.pickupRadiusPct);
    NextPhysics* physics = GetEngine().GetPhysicsEngine();

    auto deactivateSlot = [physics](Brotato3D::FDebrisRuntime& slot)
    {
        if (physics && !slot.bodyId.IsInvalid())
        {
            physics->SetBodyTransform(slot.bodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            physics->SetBodyVelocity(slot.bodyId, glm::vec3(0.0f), glm::vec3(0.0f));
            physics->SetBodyActive(slot.bodyId, false);
        }
        if (slot.node)
        {
            NodeUtils::SetOutlineFlags(slot.node, Runtime::RenderOutlineFlags::none);
            slot.node->SetTranslation(HiddenPosition);
            NodeUtils::SetVisible(slot.node, false);
        }
        slot.active = false;
        slot.payload = Brotato3D::EDebrisPayload::None;
        slot.pickupState = Brotato3D::EPickupState::None;
        slot.payloadValue = 0;
        slot.settleTimerMs = 0.0f;
        slot.magneticLerpProgress = 0.0f;
        slot.remainingMs = 0.0f;
    };

    for (Brotato3D::FDebrisRuntime& slot : debrisPool_)
    {
        if (!slot.active)
        {
            continue;
        }

        if (slot.payload == Brotato3D::EDebrisPayload::None)
        {
            if (slot.remainingMs > 0.0f)
            {
                slot.remainingMs -= deltaMs;
                if (slot.remainingMs <= 0.0f)
                {
                    deactivateSlot(slot);
                }
            }
            continue;
        }

        if (!pickupEnabled)
        {
            continue;
        }

        glm::vec3 currentPos = slot.magneticPos;
        if (physics && !slot.bodyId.IsInvalid())
        {
            if (const FNextPhysicsBody* body = physics->GetBody(slot.bodyId))
            {
                currentPos = body->position;
            }
        }
        else if (slot.node)
        {
            currentPos = slot.node->WorldTranslation();
        }

        if (slot.pickupState == Brotato3D::EPickupState::Physics)
        {
            slot.settleTimerMs = std::max(0.0f, slot.settleTimerMs - deltaMs);
        }

        if (slot.pickupState != Brotato3D::EPickupState::Magnetic &&
            DistanceXZ(currentPos, player_.worldPos) < pickupRadius)
        {
            slot.pickupState = Brotato3D::EPickupState::Magnetic;
            slot.magneticPos = currentPos;
            slot.magneticLerpProgress = 0.0f;
            if (physics && !slot.bodyId.IsInvalid())
            {
                physics->SetBodyVelocity(slot.bodyId, glm::vec3(0.0f), glm::vec3(0.0f));
                physics->SetBodyActive(slot.bodyId, false);
            }
            if (slot.node)
            {
                slot.node->SetTranslation(slot.magneticPos);
            }
        }

        if (slot.pickupState != Brotato3D::EPickupState::Magnetic)
        {
            continue;
        }

        const glm::vec3 targetPos = player_.worldPos + glm::vec3(0.0f, DebrisMagneticHeightOffset, 0.0f);
        slot.magneticLerpProgress =
            std::min(1.0f, slot.magneticLerpProgress + static_cast<float>(deltaSeconds) * DebrisMagneticProgressSpeed);
        const float magnetSpeed = slot.payload == Brotato3D::EDebrisPayload::Xp ? MagnetLerpSpeedXp : MagnetLerpSpeedMaterial;
        slot.magneticPos = glm::mix(slot.magneticPos, targetPos,
                                    std::min(1.0f, magnetSpeed * static_cast<float>(deltaSeconds)));
        if (slot.node)
        {
            slot.node->SetTranslation(slot.magneticPos);
        }
        if (physics && !slot.bodyId.IsInvalid())
        {
            physics->SetBodyTransform(slot.bodyId, slot.magneticPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        }

        if (DistanceXZ(slot.magneticPos, player_.worldPos) < PickupClaimDistance)
        {
            if (slot.payload == Brotato3D::EDebrisPayload::Material)
            {
                Brotato3D::PlayPickupMaterialSfx();
                player_.materials += slot.payloadValue;
                totalMaterialsGained_ += slot.payloadValue;
                PushFloatingText(player_.worldPos + glm::vec3(0.0f, 0.2f, 0.0f),
                                 fmt::format("+{} MAT", slot.payloadValue),
                                 glm::vec4(1.0f, 0.85f, 0.15f, 1.0f),
                                 500.0f);
                spdlog::info("[Brotato3D] Materials {}", player_.materials);
            }
            else if (slot.payload == Brotato3D::EDebrisPayload::Xp)
            {
                Brotato3D::PlayPickupXpSfx();
                player_.currentXp += slot.payloadValue;
                PushFloatingText(player_.worldPos + glm::vec3(0.0f, 0.6f, 0.0f),
                                 fmt::format("+{} XP", slot.payloadValue),
                                 glm::vec4(0.2f, 1.0f, 0.35f, 1.0f),
                                 500.0f);
                while (player_.currentXp >= GetXpToNextLevel())
                {
                    player_.currentXp -= GetXpToNextLevel();
                    ++player_.level;
                    ++player_.pendingLevelUps;
                }
                if (player_.pendingLevelUps > 0)
                {
                    BeginLevelUp();
                }
            }
            deactivateSlot(slot);
        }
    }
}

void Brotato3DGameInstance::ClearAllDebris(bool keepPickable)
{
    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    for (Brotato3D::FDebrisRuntime& slot : debrisPool_)
    {
        if (keepPickable && slot.payload != Brotato3D::EDebrisPayload::None && slot.active)
        {
            continue;
        }
        if (physics && !slot.bodyId.IsInvalid())
        {
            physics->SetBodyTransform(slot.bodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            physics->SetBodyVelocity(slot.bodyId, glm::vec3(0.0f), glm::vec3(0.0f));
            physics->SetBodyActive(slot.bodyId, false);
        }
        if (slot.node)
        {
            NodeUtils::SetOutlineFlags(slot.node, Runtime::RenderOutlineFlags::none);
            slot.node->SetTranslation(HiddenPosition);
            slot.node->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            NodeUtils::SetVisible(slot.node, false);
        }
        slot.active = false;
        slot.payload = Brotato3D::EDebrisPayload::None;
        slot.pickupState = Brotato3D::EPickupState::None;
        slot.payloadValue = 0;
        slot.settleTimerMs = 0.0f;
        slot.magneticLerpProgress = 0.0f;
        slot.remainingMs = 0.0f;
    }
}

void Brotato3DGameInstance::SpawnHitXpDebris(const glm::vec3& worldPos, const glm::vec3& projectileDir, int damage)
{
    if (damage <= 0)
    {
        return;
    }

    glm::vec3 impulseDir(projectileDir.x, 0.0f, projectileDir.z);
    if (glm::length(impulseDir) < DebrisLengthEpsilon)
    {
        impulseDir = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    impulseDir = glm::normalize(impulseDir);

    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    const float duskXpMult = IsDuskSurgeActive() && waveDef ? std::max(1.0f, waveDef->duskBonusXpMult) : 1.0f;
    const int count = std::clamp(static_cast<int>(std::ceil(static_cast<float>(damage) * duskXpMult)), 1, MaxXpChunksPerHit);
    const glm::vec3 spawnPos = worldPos + impulseDir * HitXpSurfaceOffset;
    SpawnDebris(Brotato3D::EDebrisKind::Tiny,
                glm::vec3(spawnPos.x, std::max(0.25f, spawnPos.y), spawnPos.z),
                impulseDir,
                HitXpSpeed,
                xpDebrisMatId_ != 0 ? xpDebrisMatId_ : debrisFallbackTinyMatId_,
                count,
                HitXpConeRad,
                Brotato3D::EDebrisPayload::Xp,
                1);
}

void Brotato3DGameInstance::SpawnPlayerDamageDebris(int damage)
{
    const int count = std::clamp(4 + damage / 5, 4, 10);
    const float speed = damage < 8 ? 2.5f : 3.5f;
    SpawnDebris(Brotato3D::EDebrisKind::Tiny,
                player_.worldPos + glm::vec3(0.0f, 0.4f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f),
                speed,
                playerDebrisMatId_ != 0 ? playerDebrisMatId_ : debrisFallbackTinyMatId_,
                count,
                glm::half_pi<float>(),
                Brotato3D::EDebrisPayload::None,
                0,
                1500.0f);
}

NextBodyID Brotato3DGameInstance::AcquireEnemyKinematicBody(const std::string& enemyId) const
{
    const auto poolIt = enemyKinematicBodyPools_.find(enemyId);
    if (poolIt == enemyKinematicBodyPools_.end())
    {
        return {};
    }

    for (const NextBodyID& candidate : poolIt->second)
    {
        bool used = false;
        for (const Brotato3D::FEnemyRuntime& enemy : enemies_)
        {
            if ((enemy.alive || enemy.fading) && SameBody(enemy.kinematicBodyId, candidate))
            {
                used = true;
                break;
            }
        }
        if (!used)
        {
            return candidate;
        }
    }
    return {};
}

void Brotato3DGameInstance::SyncPlayerKinematicBody(double deltaSeconds)
{
    (void)deltaSeconds;

    if (!EnableKinematicDebrisPush)
    {
        return;
    }

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics || playerKinematicBodyId_.IsInvalid())
    {
        return;
    }

    if (appState_ != Brotato3D::EAppState::Playing)
    {
        if (playerKinematicBodyActive_)
        {
            physics->SetBodyActive(playerKinematicBodyId_, false);
            physics->SetBodyTransform(playerKinematicBodyId_, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            playerKinematicBodyActive_ = false;
        }
        return;
    }

    const glm::vec3 targetPos = player_.worldPos;
    const glm::quat targetRot(1.0f, 0.0f, 0.0f, 0.0f);
    if (!playerKinematicBodyActive_)
    {
        physics->SetBodyTransform(playerKinematicBodyId_, targetPos, targetRot, true);
        physics->SetBodyActive(playerKinematicBodyId_, true);
        playerKinematicBodyActive_ = true;
        return;
    }

    // MoveKinematic's dt must match the next fixed Jolt step. Render-frame dt can be smaller than
    // the fixed step, especially on frames where Physics::Tick skips, and that can create velocity spikes.
    physics->MoveKinematicBody(playerKinematicBodyId_, targetPos, targetRot, KinematicMoveStepSeconds);
}

void Brotato3DGameInstance::SyncEnemyKinematicBody(Brotato3D::FEnemyRuntime& enemy, double deltaSeconds)
{
    (void)deltaSeconds;

    if (!EnableKinematicDebrisPush)
    {
        return;
    }

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics || enemy.kinematicBodyId.IsInvalid() || !enemy.alive || !enemy.def)
    {
        return;
    }

    if (!arenaResources_.propColliders.empty())
    {
        if (enemy.kinematicBodyActive)
        {
            physics->SetBodyActive(enemy.kinematicBodyId, false);
            physics->SetBodyTransform(enemy.kinematicBodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
            enemy.kinematicBodyActive = false;
        }
        return;
    }

    const glm::quat targetRot = enemy.node ? enemy.node->WorldRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (!enemy.kinematicBodyActive)
    {
        physics->SetBodyTransform(enemy.kinematicBodyId, enemy.worldPos, targetRot, true);
        physics->SetBodyActive(enemy.kinematicBodyId, true);
        enemy.kinematicBodyActive = true;
        return;
    }

    // MoveKinematic's dt must match the next fixed Jolt step. Render-frame dt can be smaller than
    // the fixed step, especially on frames where Physics::Tick skips, and that can create velocity spikes.
    physics->MoveKinematicBody(enemy.kinematicBodyId, enemy.worldPos, targetRot, KinematicMoveStepSeconds);
}

void Brotato3DGameInstance::DeactivateEnemyKinematicBody(Brotato3D::FEnemyRuntime& enemy)
{
    if (!EnableKinematicDebrisPush)
    {
        enemy.kinematicBodyId = {};
        enemy.kinematicBodyActive = false;
        return;
    }

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    if (!physics || enemy.kinematicBodyId.IsInvalid())
    {
        return;
    }

    physics->SetBodyActive(enemy.kinematicBodyId, false);
    physics->SetBodyTransform(enemy.kinematicBodyId, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
    enemy.kinematicBodyActive = false;
}
