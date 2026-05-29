#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Brotato3DAudio.hpp"
#include "Brotato3DPcgGenerator.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.h"

using namespace Brotato3DUtil;

namespace
{
    constexpr float ExtractionVehicleObstacleHalfX = 1.45f;
    constexpr float ExtractionVehicleObstacleHalfZ = 0.78f;
    constexpr float SegmentEpsilon = 0.0001f;

    bool SegmentIntersectsAabb2D(const glm::vec2& from,
                                 const glm::vec2& to,
                                 const glm::vec2& minBounds,
                                 const glm::vec2& maxBounds)
    {
        const glm::vec2 delta = to - from;
        float tMin = 0.0f;
        float tMax = 1.0f;

        auto clipAxis = [&](float start, float dir, float minValue, float maxValue) -> bool
        {
            if (std::abs(dir) < SegmentEpsilon)
            {
                return start >= minValue && start <= maxValue;
            }

            float enter = (minValue - start) / dir;
            float exit = (maxValue - start) / dir;
            if (enter > exit)
            {
                std::swap(enter, exit);
            }
            tMin = std::max(tMin, enter);
            tMax = std::min(tMax, exit);
            return tMin <= tMax;
        };

        return clipAxis(from.x, delta.x, minBounds.x, maxBounds.x) &&
               clipAxis(from.y, delta.y, minBounds.y, maxBounds.y);
    }

    glm::vec2 BoxAabbHalfExtentXZ(const Brotato3D::FArenaResources::FBoxCollider& collider)
    {
        const glm::mat3 rotationMatrix = glm::mat3_cast(collider.rotation);
        const glm::vec3 halfExtent = collider.extent * 0.5f;
        return glm::vec2(std::abs(rotationMatrix[0][0]) * halfExtent.x +
                             std::abs(rotationMatrix[1][0]) * halfExtent.y +
                             std::abs(rotationMatrix[2][0]) * halfExtent.z,
                         std::abs(rotationMatrix[0][2]) * halfExtent.x +
                             std::abs(rotationMatrix[1][2]) * halfExtent.y +
                             std::abs(rotationMatrix[2][2]) * halfExtent.z);
    }

    glm::vec3 ResolveAabbCollisionXZ(const glm::vec3& pos,
                                     float radius,
                                     const glm::vec3& center,
                                     const glm::vec2& halfExtentXZ)
    {
        const float paddedHalfX = halfExtentXZ.x + std::max(0.0f, radius);
        const float paddedHalfZ = halfExtentXZ.y + std::max(0.0f, radius);
        const glm::vec2 delta(pos.x - center.x, pos.z - center.z);
        if (std::abs(delta.x) > paddedHalfX || std::abs(delta.y) > paddedHalfZ)
        {
            return pos;
        }

        glm::vec3 resolved = pos;
        const float overlapX = paddedHalfX - std::abs(delta.x);
        const float overlapZ = paddedHalfZ - std::abs(delta.y);
        if (overlapX < overlapZ)
        {
            const float sign = delta.x < 0.0f ? -1.0f : 1.0f;
            resolved.x = center.x + sign * paddedHalfX;
        }
        else
        {
            const float sign = delta.y < 0.0f ? -1.0f : 1.0f;
            resolved.z = center.z + sign * paddedHalfZ;
        }
        return resolved;
    }

    Brotato3D::FArenaResources::FBoxCollider GetCurrentPropCollider(const Brotato3D::FArenaResources::FBoxCollider& source,
                                                                    NextPhysics* physics,
                                                                    const std::vector<NextBodyID>& bodyIds,
                                                                    size_t index)
    {
        Brotato3D::FArenaResources::FBoxCollider collider = source;
        if (physics && index < bodyIds.size())
        {
            if (const FNextPhysicsBody* body = physics->GetBody(bodyIds[index]))
            {
                collider.center = body->position;
                collider.rotation = body->rotation;
            }
        }
        return collider;
    }
}

void Brotato3DGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& lights,
                                               std::vector<Assets::AnimationTrack>& tracks)
{
    (void)tracks;
    sceneReady_ = false;
    ApplyLightingSettings();
    Brotato3D::BuildArena(models, materials, nodes, arenaResources_, arenaDefs_, selectedArenaId_);
    lightMaterialIds_.clear();
    tempLightPool_.clear();
    playerLightIndex_ = -1;
    playerLightNode_.reset();

    auto addLightMaterial = [&materials, this](const glm::vec3& color, float intensity) -> uint32_t
    {
        const std::string key = fmt::format("{}_{:.2f}", LightMaterialKey(color), intensity);
        if (const auto it = lightMaterialIds_.find(key); it != lightMaterialIds_.end())
        {
            return it->second;
        }
        const uint32_t materialId = Assets::SceneBuilder::AddDiffuseLightMaterial(materials, color, intensity);
        lightMaterialIds_[key] = materialId;
        return materialId;
    };
    auto addAreaLight = [&models, &nodes, &lights](const std::string& name,
                                                   const glm::vec3& center,
                                                   float radius,
                                                   uint32_t materialId,
                                                   bool visible) -> std::pair<int, std::shared_ptr<Assets::Node>>
    {
        const glm::vec3 right(radius, 0.0f, 0.0f);
        const glm::vec3 up(0.0f, 0.0f, radius);
        std::vector<Assets::LightObject> createdLights;
        models.push_back(Assets::FProcModel::CreateAreaLight(name,
                                                             -right * 0.5f - up * 0.5f,
                                                             right,
                                                             up,
                                                             materialId,
                                                             createdLights));
        if (createdLights.empty())
        {
            return {-1, nullptr};
        }

        lights.push_back(createdLights.front());
        const int lightIndex = static_cast<int>(lights.size() - 1);
        auto node = Assets::SceneBuilder::CreateRenderNode(name,
                                     center,
                                     glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()),
                                     static_cast<uint32_t>(models.size() - 1),
                                     materialId,
                                     visible);
        nodes.push_back(node);
        return {lightIndex, node};
    };

    const auto playerLight = addAreaLight("Brotato3D_PlayerEmissiveLight",
                                          player_.worldPos + glm::vec3(0.0f, 3.2f, 0.0f),
                                          6.0f,
                                          addLightMaterial(glm::vec3(1.0f, 0.86f, 0.62f), 180.0f),
                                          true);
    playerLightIndex_ = playerLight.first;
    playerLightNode_ = playerLight.second;
    tempLightPool_.reserve(32);
    for (int index = 0; index < 32; ++index)
    {
        const auto tempLight = addAreaLight(fmt::format("Brotato3D_TempEmissiveLight_{}", index),
                                            HiddenPosition,
                                            1.0f,
                                            addLightMaterial(glm::vec3(1.0f), 0.0f),
                                            false);
        tempLightPool_.push_back({.lightIndex = tempLight.first, .node = tempLight.second});
    }

    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), player_.radius));
    const uint32_t playerModelId = static_cast<uint32_t>(models.size() - 1);
    characterMaterialIds_.clear();
    for (const Brotato3D::FCharacterDef& character : characterDefs_)
    {
        characterMaterialIds_[character.id] = Assets::SceneBuilder::AddLambertianMaterial(materials, character.color);
    }
    const uint32_t playerMaterialId =
        characterMaterialIds_.contains(selectedCharacterId_) ? characterMaterialIds_[selectedCharacterId_] :
                                                               Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.20f, 0.75f, 0.30f));
    player_.bodyNode = Assets::SceneBuilder::CreateRenderNode("Brotato3D_Player", player_.worldPos, glm::vec3(1.0f),
                                        static_cast<uint32_t>(nodes.size()), playerModelId, playerMaterialId);
    nodes.push_back(player_.bodyNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.08f), glm::vec3(0.08f)));
    const uint32_t facingModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t facingMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f));
    player_.facingNode = Assets::SceneBuilder::CreateRenderNode("Brotato3D_PlayerFacing", glm::vec3(0.0f, 0.45f, -0.25f), glm::vec3(1.0f),
                                          static_cast<uint32_t>(nodes.size()), facingModelId, facingMaterialId);
    player_.facingNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.facingNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.06f, -0.05f, -0.24f), glm::vec3(0.06f, 0.05f, 0.24f)));
    const uint32_t smgWeaponModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t smgWeaponMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.85f, 0.2f));
    player_.smgWeaponNode = Assets::SceneBuilder::CreateRenderNode("Brotato3D_PlayerWeapon_SMG", glm::vec3(-0.18f, 0.42f, -0.54f),
                                             glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), smgWeaponModelId,
                                             smgWeaponMaterialId);
    player_.smgWeaponNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.smgWeaponNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.11f, -0.06f, -0.34f), glm::vec3(0.11f, 0.06f, 0.34f)));
    const uint32_t shotgunWeaponModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t shotgunWeaponMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.4f, 0.2f));
    player_.shotgunWeaponNode = Assets::SceneBuilder::CreateRenderNode("Brotato3D_PlayerWeapon_Shotgun", glm::vec3(0.24f, 0.43f, -0.62f),
                                                 glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()),
                                                 shotgunWeaponModelId, shotgunWeaponMaterialId);
    player_.shotgunWeaponNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.shotgunWeaponNode);

    extractionVehicleNodes_.clear();
    extractionVehicleRootNode_.reset();
    extractionVehicleMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.18f, 0.22f, 0.25f));
    extractionVehicleActiveMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.85f, 0.68f, 0.25f));
    const uint32_t extractionWheelMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.04f, 0.04f, 0.05f));
    auto addTruckPart = [&models, &nodes, this](const std::string& name,
                                                const glm::vec3& min,
                                                const glm::vec3& max,
                                                const glm::vec3& localPos,
                                                uint32_t materialId,
                                                bool root) -> std::shared_ptr<Assets::Node>
    {
        models.push_back(Assets::FProcModel::CreateBox(min, max));
        auto node = Assets::SceneBuilder::CreateRenderNode(name,
                                                   root ? HiddenPosition : localPos,
                                                   glm::vec3(1.0f),
                                                   static_cast<uint32_t>(nodes.size()),
                                                   static_cast<uint32_t>(models.size() - 1),
                                                   materialId,
                                                   false);
        if (!root && extractionVehicleRootNode_)
        {
            node->SetParent(extractionVehicleRootNode_);
        }
        nodes.push_back(node);
        extractionVehicleNodes_.push_back(node);
        return node;
    };
    extractionVehicleRootNode_ = addTruckPart("Brotato3D_ExtractionTruck_Body",
                                              glm::vec3(-1.3f, 0.0f, -0.55f),
                                              glm::vec3(1.3f, 0.55f, 0.55f),
                                              glm::vec3(0.0f),
                                              extractionVehicleMaterialId_,
                                              true);
    addTruckPart("Brotato3D_ExtractionTruck_Cab",
                 glm::vec3(-0.45f, 0.0f, -0.48f),
                 glm::vec3(0.45f, 0.72f, 0.48f),
                 glm::vec3(0.72f, 0.36f, 0.0f),
                 extractionVehicleMaterialId_,
                 false);
    addTruckPart("Brotato3D_ExtractionTruck_Beacon",
                 glm::vec3(-0.16f, 0.0f, -0.16f),
                 glm::vec3(0.16f, 0.18f, 0.16f),
                 glm::vec3(0.72f, 1.0f, 0.0f),
                 extractionVehicleActiveMaterialId_,
                 false);
    addTruckPart("Brotato3D_ExtractionTruck_WheelA",
                 glm::vec3(-0.22f, -0.22f, -0.08f),
                 glm::vec3(0.22f, 0.22f, 0.08f),
                 glm::vec3(-0.85f, 0.04f, -0.62f),
                 extractionWheelMaterialId,
                 false);
    addTruckPart("Brotato3D_ExtractionTruck_WheelB",
                 glm::vec3(-0.22f, -0.22f, -0.08f),
                 glm::vec3(0.22f, 0.22f, 0.08f),
                 glm::vec3(0.85f, 0.04f, -0.62f),
                 extractionWheelMaterialId,
                 false);
    addTruckPart("Brotato3D_ExtractionTruck_WheelC",
                 glm::vec3(-0.22f, -0.22f, -0.08f),
                 glm::vec3(0.22f, 0.22f, 0.08f),
                 glm::vec3(-0.85f, 0.04f, 0.62f),
                 extractionWheelMaterialId,
                 false);
    addTruckPart("Brotato3D_ExtractionTruck_WheelD",
                 glm::vec3(-0.22f, -0.22f, -0.08f),
                 glm::vec3(0.22f, 0.22f, 0.08f),
                 glm::vec3(0.85f, 0.04f, 0.62f),
                 extractionWheelMaterialId,
                 false);

    enemyVisuals_.clear();
    for (const auto& [enemyId, def] : enemyDefs_)
    {
        models.push_back(Assets::FProcModel::CreateBox(-def.size * 0.5f, def.size * 0.5f));
        FEnemyVisualResource visual{};
        visual.modelId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f), glm::vec3(0.5f)));
        visual.bodyBlockModelId = static_cast<uint32_t>(models.size() - 1);
        visual.materialId = Assets::SceneBuilder::AddLambertianMaterial(materials, def.color);
        visual.darkMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, def.color * 0.4f);
        visual.hitFlashMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f));
        visual.warningMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.08f, 0.06f));
        visual.phase2MaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.95f, 0.20f, 0.10f));
        visual.baseColor = def.color;
        enemyVisuals_[enemyId] = visual;
    }

    projectilePool_.clear();
    projectilePool_.reserve(weaponDefs_.size() * 128);
    for (const auto& [weaponId, weaponDef] : weaponDefs_)
    {
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), weaponDef.projectileSize));
        const uint32_t weaponProjectileModelId = static_cast<uint32_t>(models.size() - 1);
        const uint32_t weaponProjectileMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, weaponDef.projectileColor);
        if (weaponId == "smg")
        {
            projectileModelId_ = weaponProjectileModelId;
            projectileMaterialId_ = weaponProjectileMaterialId;
        }

        for (int index = 0; index < 128; ++index)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Projectile_{}_{}", weaponId, index), HiddenPosition,
                                         glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), weaponProjectileModelId,
                                         weaponProjectileMaterialId, false);
            nodes.push_back(node);
            Brotato3D::FProjectileRuntime projectile{};
            projectile.weaponId = weaponId;
            projectile.modelId = weaponProjectileModelId;
            projectile.materialId = weaponProjectileMaterialId;
            projectile.radius = weaponDef.projectileSize;
            projectile.node = node;
            projectilePool_.push_back(projectile);
        }
    }

    enemyProjectileMaterialIds_.clear();
    for (const auto& [enemyId, enemyDef] : enemyDefs_)
    {
        (void)enemyId;
        if (enemyDef.ranged.enabled)
        {
            enemyProjectileMaterialIds_[&enemyDef] = Assets::SceneBuilder::AddLambertianMaterial(materials, enemyDef.ranged.color);
        }
    }
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
    enemyProjectileModelId_ = static_cast<uint32_t>(models.size() - 1);
    enemyProjectileMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.3f, 0.95f, 0.2f));
    enemyProjectilePool_.clear();
    enemyProjectilePool_.reserve(128);
    for (int index = 0; index < 128; ++index)
    {
        auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_EnemyProjectile_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), enemyProjectileModelId_,
                                     enemyProjectileMaterialId_, false);
        Assets::NodeUtils::SetOutlineFlags(node, Runtime::RenderOutlineFlags::danger);
        nodes.push_back(node);
        Brotato3D::FEnemyProjectileRuntime projectile{};
        projectile.radius = 0.18f;
        projectile.node = node;
        enemyProjectilePool_.push_back(projectile);
    }

    BuildDebrisPool(models, materials, nodes);
    BuildArenaWallBodies();
    BuildKinematicCollisionBodies(models, materials, nodes);

    sceneReady_ = true;
    if (appState_ == Brotato3D::EAppState::MainMenu || appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        Assets::NodeUtils::SetVisible(player_.bodyNode, false);
    }
}

bool Brotato3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    glm::vec3 cameraTarget = cameraSmoothedTarget_;
    glm::vec3 cameraPosition = cameraTarget + glm::vec3(0.0f, 10.8f, 6.6f);
    if (Brotato3D::ScreenShakeEnabled && screenShakeMs_ > 0.0f)
    {
        const float strength = std::min(0.25f, screenShakeIntensity_ * 0.05f);
        const float t = runElapsedSec_ * 37.0f;
        const glm::vec3 jitter(std::sin(t), 0.0f, std::cos(t * 1.37f));
        cameraPosition += jitter * strength;
        cameraTarget += jitter * (strength * 0.5f);
    }
    outRenderCamera.ModelView = glm::lookAtRH(cameraPosition, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    outRenderCamera.FieldOfView = 50.0f;
    return true;
}

void Brotato3DGameInstance::OnSceneLoaded()
{
    ApplyLightingSettings();
}

void Brotato3DGameInstance::SetExtractionVehicleVisible(bool visible)
{
    extractionVehicleVisible_ = visible;
    for (const auto& node : extractionVehicleNodes_)
    {
        if (node)
        {
            Assets::NodeUtils::SetVisible(node, visible);
        }
    }
    if (!visible && extractionVehicleRootNode_)
    {
        extractionVehicleRootNode_->SetTranslation(HiddenPosition);
    }
}

void Brotato3DGameInstance::ResetExtractionVehicle()
{
    extractionVehiclePos_ = HiddenPosition;
    extractionVehicleStartPos_ = HiddenPosition;
    extractionVehicleTargetPos_ = HiddenPosition;
    extractionVehicleAnimMs_ = 0.0f;
    extractionVehicleAnimTotalMs_ = 0.0f;
    SetExtractionVehicleVisible(false);
}

void Brotato3DGameInstance::BeginDuskSurge()
{
    const int side = std::uniform_int_distribution<int>(0, 3)(rng_);
    const float anchorX = side < 2 ? 0.0f : (side == 2 ? -arenaHalfExtent_.x : arenaHalfExtent_.x);
    const float anchorZ = side < 2 ? (side == 0 ? -arenaHalfExtent_.y : arenaHalfExtent_.y) : 0.0f;
    glm::vec3 inward(-anchorX, 0.0f, -anchorZ);
    if (glm::length(inward) < 0.001f)
    {
        inward = glm::vec3(0.0f, 0.0f, side == 0 ? 1.0f : -1.0f);
    }
    inward = glm::normalize(inward);

    const glm::vec3 anchor(anchorX, 0.0f, anchorZ);
    extractionVehicleStartPos_ = anchor - inward * 4.0f + glm::vec3(0.0f, 0.02f, 0.0f);
    extractionVehicleTargetPos_ = anchor + inward * std::min(arenaHalfExtent_.x, arenaHalfExtent_.y) * 0.42f + glm::vec3(0.0f, 0.02f, 0.0f);
    extractionVehiclePos_ = extractionVehicleStartPos_;
    extractionVehicleAnimTotalMs_ = 1500.0f;
    extractionVehicleAnimMs_ = extractionVehicleAnimTotalMs_;
    if (extractionVehicleRootNode_)
    {
        extractionVehicleRootNode_->SetTranslation(extractionVehiclePos_);
    }
    SetExtractionVehicleVisible(true);
    SetSkyIntensityTarget(5.0f, 1500.0f);
    waveBannerText_ = "NIGHTFALL";
    waveBannerMs_ = 1500.0f;
    StartScreenShake(260.0f, 2.2f);
    Brotato3D::StartBgm("battle");
}

void Brotato3DGameInstance::UpdateExtractionVehicle(double deltaSeconds)
{
    if (waveSystem_.GetState() != Brotato3D::EWaveState::DuskSurge)
    {
        waveSystem_.NotifyPlayerInExtractionZone(false);
        return;
    }

    if (extractionVehicleAnimMs_ > 0.0f)
    {
        extractionVehicleAnimMs_ = std::max(0.0f, extractionVehicleAnimMs_ - static_cast<float>(deltaSeconds * 1000.0));
        const float t = 1.0f - extractionVehicleAnimMs_ / std::max(1.0f, extractionVehicleAnimTotalMs_);
        const float eased = t * t * (3.0f - 2.0f * t);
        extractionVehiclePos_ = glm::mix(extractionVehicleStartPos_, extractionVehicleTargetPos_, eased);
        if (extractionVehicleRootNode_)
        {
            extractionVehicleRootNode_->SetTranslation(extractionVehiclePos_);
        }
    }
    else
    {
        extractionVehiclePos_ = extractionVehicleTargetPos_;
    }

    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    const float radius = waveDef ? waveDef->extractionRadiusM : 2.5f;
    const bool inZone = extractionVehicleVisible_ &&
                        DistanceXZ(player_.worldPos, extractionVehiclePos_) <= radius;
    waveSystem_.NotifyPlayerInExtractionZone(inZone);
    const uint32_t materialId = inZone ? extractionVehicleActiveMaterialId_ : extractionVehicleMaterialId_;
    if (extractionVehicleRootNode_ && materialId != 0)
    {
        Assets::NodeUtils::SetPrimaryMaterial(extractionVehicleRootNode_, materialId);
    }
}

bool Brotato3DGameInstance::IsExtractionVehicleObstacleActive() const
{
    return extractionVehicleVisible_;
}

glm::vec3 Brotato3DGameInstance::ResolveExtractionVehicleCollision(const glm::vec3& pos, float radius) const
{
    glm::vec3 resolved = pos;
    if (IsExtractionVehicleObstacleActive())
    {
        resolved = ResolveAabbCollisionXZ(resolved,
                                          radius,
                                          extractionVehiclePos_,
                                          glm::vec2(ExtractionVehicleObstacleHalfX, ExtractionVehicleObstacleHalfZ));
    }

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    for (size_t index = 0; index < arenaResources_.propColliders.size(); ++index)
    {
        const Brotato3D::FArenaResources::FBoxCollider collider =
            GetCurrentPropCollider(arenaResources_.propColliders[index], physics, arenaPropBodyIds_, index);
        resolved = ResolveAabbCollisionXZ(resolved, radius, collider.center, BoxAabbHalfExtentXZ(collider));
    }
    return resolved;
}

glm::vec3 Brotato3DGameInstance::ResolvePlayerObstacleCollision(const glm::vec3& pos, float radius)
{
    glm::vec3 resolved = pos;
    if (IsExtractionVehicleObstacleActive())
    {
        resolved = ResolveAabbCollisionXZ(resolved,
                                          radius,
                                          extractionVehiclePos_,
                                          glm::vec2(ExtractionVehicleObstacleHalfX, ExtractionVehicleObstacleHalfZ));
    }

    return resolved;
}

bool Brotato3DGameInstance::IsSegmentBlockedByExtractionVehicle(const glm::vec3& from,
                                                                const glm::vec3& to,
                                                                float radius) const
{
    const float padding = std::max(0.0f, radius);
    const glm::vec2 fromXZ(from.x, from.z);
    const glm::vec2 toXZ(to.x, to.z);
    if (IsExtractionVehicleObstacleActive())
    {
        const glm::vec2 minBounds(extractionVehiclePos_.x - ExtractionVehicleObstacleHalfX - padding,
                                  extractionVehiclePos_.z - ExtractionVehicleObstacleHalfZ - padding);
        const glm::vec2 maxBounds(extractionVehiclePos_.x + ExtractionVehicleObstacleHalfX + padding,
                                  extractionVehiclePos_.z + ExtractionVehicleObstacleHalfZ + padding);
        if (SegmentIntersectsAabb2D(fromXZ, toXZ, minBounds, maxBounds))
        {
            return true;
        }
    }

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    for (size_t index = 0; index < arenaResources_.propColliders.size(); ++index)
    {
        const Brotato3D::FArenaResources::FBoxCollider collider =
            GetCurrentPropCollider(arenaResources_.propColliders[index], physics, arenaPropBodyIds_, index);
        const glm::vec2 halfExtent = BoxAabbHalfExtentXZ(collider);
        const glm::vec2 minBounds(collider.center.x - halfExtent.x - padding,
                                  collider.center.z - halfExtent.y - padding);
        const glm::vec2 maxBounds(collider.center.x + halfExtent.x + padding,
                                  collider.center.z + halfExtent.y + padding);
        if (SegmentIntersectsAabb2D(fromXZ, toXZ, minBounds, maxBounds))
        {
            return true;
        }
    }
    return false;
}

float Brotato3DGameInstance::SampleArenaGroundY(const glm::vec3& worldPos) const
{
    if (!arenaResources_.terrainHeight.enabled)
    {
        return 0.0f;
    }

    return Brotato3D::Pcg::SampleVisualHeight(arenaResources_.terrainHeight.rootSeed,
                                             arenaResources_.terrainHeight.vertexJitterAmplitude,
                                             arenaResources_.terrainHeight.vertexJitterFrequency,
                                             glm::vec2(worldPos.x, worldPos.z));
}

glm::vec3 Brotato3DGameInstance::ResolveEnemyGroundedPosition(const Brotato3D::FEnemyRuntime& enemy, glm::vec3 candidate) const
{
    candidate = ClampToArena(candidate, enemy.radius, arenaHalfExtent_);
    candidate = ResolveExtractionVehicleCollision(candidate, enemy.radius);
    candidate = ClampToArena(candidate, enemy.radius, arenaHalfExtent_);
    if (enemy.def)
    {
        candidate.y = enemy.def->size.y * 0.5f + SampleArenaGroundY(candidate);
    }
    return candidate;
}

void Brotato3DGameInstance::UpdateCombatEffects(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    UpdateGroundIndicators(deltaSeconds);
    for (auto& ring : explosionRings_)
    {
        ring.remainingMs -= deltaMs;
    }
    explosionRings_.erase(std::remove_if(explosionRings_.begin(), explosionRings_.end(),
                                         [](const Brotato3D::FExpandingRing& ring)
                                         {
                                             return ring.remainingMs <= 0.0f;
                                         }),
                          explosionRings_.end());

    for (auto& beam : laserBeams_)
    {
        beam.remainingMs -= deltaMs;
    }
    laserBeams_.erase(std::remove_if(laserBeams_.begin(), laserBeams_.end(),
                                     [](const Brotato3D::FLaserBeam& beam)
                                     {
                                         return beam.remainingMs <= 0.0f;
                                     }),
                      laserBeams_.end());

    for (auto& flash : muzzleFlashes_)
    {
        flash.remainingMs -= deltaMs;
    }
    muzzleFlashes_.erase(std::remove_if(muzzleFlashes_.begin(), muzzleFlashes_.end(),
                                        [](const Brotato3D::FMuzzleFlash& flash)
                                        {
                                            return flash.remainingMs <= 0.0f;
                                        }),
                         muzzleFlashes_.end());

    const glm::vec3 playerLightPos = player_.worldPos + glm::vec3(0.0f, 3.2f, 0.0f);
    UpdateLightArea(playerLightIndex_, playerLightPos, 6.0f, 1.0f);
    playerLightNode_->SetTranslation(playerLightPos);
    for (auto& light : tempLightPool_)
    {
        if (!light.active)
        {
            continue;
        }

        light.remainingMs -= deltaMs;
        const float intensityScale = light.durationMs > 0.0f ? std::clamp(light.remainingMs / light.durationMs, 0.0f, 1.0f) : 0.0f;
        if (light.remainingMs <= 0.0f)
        {
            light.active = false;
            light.remainingMs = 0.0f;
            UpdateLightArea(light.lightIndex, HiddenPosition, 0.01f, 0.0f);
            light.node->SetTranslation(HiddenPosition);
            Assets::NodeUtils::SetVisible(light.node, false);
            continue;
        }
        UpdateLightArea(light.lightIndex, light.worldPos, light.radiusMeters, intensityScale);
        light.node->SetTranslation(light.worldPos);
        light.node->SetScale(glm::vec3(std::max(0.05f, light.radiusMeters * intensityScale)));
    }
}

void Brotato3DGameInstance::UpdateGroundIndicators(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& indicator : groundIndicators_)
    {
        indicator.remainingMs -= deltaMs;
    }
    groundIndicators_.erase(std::remove_if(groundIndicators_.begin(), groundIndicators_.end(),
                                           [](const Brotato3D::FGroundIndicator& indicator)
                                           {
                                               return indicator.remainingMs <= 0.0f;
                                           }),
                            groundIndicators_.end());
}

void Brotato3DGameInstance::UpdateWaveBanner(double deltaSeconds)
{
    if (waveBannerMs_ > 0.0f)
    {
        waveBannerMs_ = std::max(0.0f, waveBannerMs_ - static_cast<float>(deltaSeconds * 1000.0));
    }
}

void Brotato3DGameInstance::UpdateFloatingTexts(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& text : floatingTexts_)
    {
        text.remainingMs -= deltaMs;
    }
    floatingTexts_.erase(std::remove_if(floatingTexts_.begin(), floatingTexts_.end(),
                                        [](const Brotato3D::FFloatingText& text)
                                        {
                                            return text.remainingMs <= 0.0f;
                                        }),
                         floatingTexts_.end());
}

void Brotato3DGameInstance::PushExplosionRing(const glm::vec3& worldPos, const glm::vec4& color, float maxRadius)
{
    explosionRings_.push_back({worldPos, color, 300.0f, 300.0f, maxRadius});
}

void Brotato3DGameInstance::PushLaserBeam(const glm::vec3& from,
                                          const glm::vec3& to,
                                          const glm::vec4& color,
                                          float durationMs,
                                          float width)
{
    laserBeams_.push_back({from, to, color, durationMs, durationMs, width});
}

void Brotato3DGameInstance::PushGroundIndicator(const Brotato3D::FGroundIndicator& indicator)
{
    if (indicator.enemyTag != 0)
    {
        CancelGroundIndicatorsForEnemy(indicator.enemyTag);
    }
    if (groundIndicators_.size() >= 64)
    {
        groundIndicators_.erase(groundIndicators_.begin());
    }
    groundIndicators_.push_back(indicator);
}

void Brotato3DGameInstance::CancelGroundIndicatorsForEnemy(uint32_t enemyTag)
{
    if (enemyTag == 0)
    {
        return;
    }
    groundIndicators_.erase(std::remove_if(groundIndicators_.begin(), groundIndicators_.end(),
                                           [enemyTag](const Brotato3D::FGroundIndicator& indicator)
                                           {
                                               return indicator.enemyTag == enemyTag;
                                           }),
                            groundIndicators_.end());
}

void Brotato3DGameInstance::PushMuzzleFlash(const glm::vec3& worldPos, const glm::vec3& color)
{
    muzzleFlashes_.push_back({worldPos, color, 80.0f, 80.0f});
}

void Brotato3DGameInstance::SpawnTempLight(const glm::vec3& worldPos,
                                           const glm::vec3& color,
                                           float radiusMeters,
                                           float durationMs)
{
    if (tempLightPool_.empty())
    {
        return;
    }

    auto lightIt = std::find_if(tempLightPool_.begin(), tempLightPool_.end(),
                                [](const FTempLightRuntime& light)
                                {
                                    return !light.active;
                                });
    if (lightIt == tempLightPool_.end())
    {
        lightIt = std::min_element(tempLightPool_.begin(), tempLightPool_.end(),
                                   [](const FTempLightRuntime& lhs, const FTempLightRuntime& rhs)
                                   {
                                       return lhs.remainingMs < rhs.remainingMs;
                                   });
    }
    if (lightIt == tempLightPool_.end())
    {
        return;
    }

    lightIt->active = true;
    lightIt->worldPos = worldPos;
    lightIt->radiusMeters = radiusMeters;
    lightIt->durationMs = durationMs;
    lightIt->remainingMs = durationMs;

    auto& lights = GetEngine().GetScene().Lights();
    if (lightIt->lightIndex >= 0 && lightIt->lightIndex < static_cast<int>(lights.size()))
    {
        lights[static_cast<size_t>(lightIt->lightIndex)].lightMatIdx = EnsureLightMaterial(color);
    }
    Assets::NodeUtils::SetPrimaryMaterial(lightIt->node, EnsureLightMaterial(color));
    lightIt->node->SetTranslation(worldPos);
    lightIt->node->SetScale(glm::vec3(radiusMeters));
    Assets::NodeUtils::SetVisible(lightIt->node, true);
    UpdateLightArea(lightIt->lightIndex, worldPos, radiusMeters, 1.0f);
}

void Brotato3DGameInstance::UpdateLightArea(int lightIndex,
                                            const glm::vec3& worldPos,
                                            float radiusMeters,
                                            float intensityScale)
{
    auto& lights = GetEngine().GetScene().Lights();
    if (lightIndex < 0 || lightIndex >= static_cast<int>(lights.size()))
    {
        return;
    }

    const float radius = std::max(0.01f, radiusMeters);
    const glm::vec3 right(radius, 0.0f, 0.0f);
    const glm::vec3 up(0.0f, 0.0f, radius);
    auto& light = lights[static_cast<size_t>(lightIndex)];
    light.p0 = glm::vec4(worldPos - right * 0.5f - up * 0.5f, 1.0f);
    light.p1 = glm::vec4(worldPos - right * 0.5f + up * 0.5f, 1.0f);
    light.p3 = glm::vec4(worldPos + right * 0.5f - up * 0.5f, 1.0f);
    light.normal_area = glm::vec4(0.0f, -1.0f, 0.0f, radius * radius * std::clamp(intensityScale, 0.0f, 1.0f));
}

uint32_t Brotato3DGameInstance::EnsureLightMaterial(const glm::vec3& color)
{
    const std::string key = LightMaterialKey(color);
    if (const auto it = lightMaterialIds_.find(key); it != lightMaterialIds_.end())
    {
        return it->second;
    }

    const uint32_t materialId = Assets::SceneBuilder::AddDiffuseLightMaterialToScene(GetEngine().GetScene(), color, 650.0f);
    lightMaterialIds_[key] = materialId;
    return materialId;
}

void Brotato3DGameInstance::StartScreenShake(float durationMs, float intensity)
{
    if (!Brotato3D::ScreenShakeEnabled)
    {
        return;
    }
    screenShakeMs_ = std::max(screenShakeMs_, durationMs);
    screenShakeIntensity_ = std::max(screenShakeIntensity_, intensity);
}

void Brotato3DGameInstance::BeginWaveBanner()
{
    if (waveSystem_.GetState() != Brotato3D::EWaveState::Active)
    {
        return;
    }

    const int waveIndex = waveSystem_.GetCurrentWaveIndex();
    const int waveCount = waveSystem_.GetWaveCount();
    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    const bool bossWave = waveDef && waveDef->bgmCue == "boss";
    Brotato3D::PlayWaveStartSfx(waveIndex);
    Brotato3D::StartBgm(waveDef ? waveDef->bgmCue : std::string("calm"));
    waveBannerText_ = bossWave ? "BOSS INCOMING" : fmt::format("WAVE {} / {}", waveIndex + 1, waveCount);
    waveBannerMs_ = 1000.0f;
    if (bossWave)
    {
        StartScreenShake(250.0f, 2.0f);
    }
}

void Brotato3DGameInstance::ApplyLightingSettings()
{
    auto& envSettings = GetEngine().GetScene().GetEnvSettings();
    envSettings.HasSky = true;
    envSettings.HasSun = false;
    envSettings.SkyIntensity = currentSkyIntensity_;
    GetEngine().GetScene().MarkEnvDirty();
}

void Brotato3DGameInstance::SetSkyIntensityTarget(float target, float transitionMs)
{
    skyTransitionStartIntensity_ = currentSkyIntensity_;
    targetSkyIntensity_ = target;
    skyTransitionTotalMs_ = std::max(1.0f, transitionMs);
    skyTransitionRemainingMs_ = skyTransitionTotalMs_;
    if (transitionMs <= 0.0f)
    {
        currentSkyIntensity_ = targetSkyIntensity_;
        skyTransitionRemainingMs_ = 0.0f;
        ApplyLightingSettings();
    }
}

void Brotato3DGameInstance::UpdateSkyTransition(double deltaSeconds)
{
    if (skyTransitionRemainingMs_ <= 0.0f)
    {
        return;
    }

    skyTransitionRemainingMs_ = std::max(0.0f, skyTransitionRemainingMs_ - static_cast<float>(deltaSeconds * 1000.0));
    const float t = 1.0f - skyTransitionRemainingMs_ / std::max(1.0f, skyTransitionTotalMs_);
    currentSkyIntensity_ = glm::mix(skyTransitionStartIntensity_, targetSkyIntensity_, std::clamp(t, 0.0f, 1.0f));
    ApplyLightingSettings();
}

bool Brotato3DGameInstance::IsDuskSurgeActive() const
{
    return waveSystem_.GetState() == Brotato3D::EWaveState::DuskSurge;
}

float Brotato3DGameInstance::GetExtractionRadius() const
{
    const Brotato3D::FWaveDef* waveDef = waveSystem_.GetCurrentWaveDef();
    return waveDef ? waveDef->extractionRadiusM : 2.5f;
}


