#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Loaders/FProcModel.h"
#include "Brotato3DAudio.hpp"
#include "Runtime/Scene/SceneBuilder.h"

using namespace Brotato3DUtil;

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
        const uint32_t materialId = SceneBuilder::AddDiffuseLightMaterial(materials, color, intensity);
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
        auto node = SceneBuilder::CreateRenderNode(name,
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
        characterMaterialIds_[character.id] = SceneBuilder::AddLambertianMaterial(materials, character.color);
    }
    const uint32_t playerMaterialId =
        characterMaterialIds_.contains(selectedCharacterId_) ? characterMaterialIds_[selectedCharacterId_] :
                                                               SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.20f, 0.75f, 0.30f));
    player_.bodyNode = SceneBuilder::CreateRenderNode("Brotato3D_Player", player_.worldPos, glm::vec3(1.0f),
                                        static_cast<uint32_t>(nodes.size()), playerModelId, playerMaterialId);
    nodes.push_back(player_.bodyNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.08f), glm::vec3(0.08f)));
    const uint32_t facingModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t facingMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f));
    player_.facingNode = SceneBuilder::CreateRenderNode("Brotato3D_PlayerFacing", glm::vec3(0.0f, 0.45f, -0.25f), glm::vec3(1.0f),
                                          static_cast<uint32_t>(nodes.size()), facingModelId, facingMaterialId);
    player_.facingNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.facingNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.06f, -0.05f, -0.24f), glm::vec3(0.06f, 0.05f, 0.24f)));
    const uint32_t smgWeaponModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t smgWeaponMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.85f, 0.2f));
    player_.smgWeaponNode = SceneBuilder::CreateRenderNode("Brotato3D_PlayerWeapon_SMG", glm::vec3(-0.18f, 0.42f, -0.54f),
                                             glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), smgWeaponModelId,
                                             smgWeaponMaterialId);
    player_.smgWeaponNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.smgWeaponNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.11f, -0.06f, -0.34f), glm::vec3(0.11f, 0.06f, 0.34f)));
    const uint32_t shotgunWeaponModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t shotgunWeaponMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.4f, 0.2f));
    player_.shotgunWeaponNode = SceneBuilder::CreateRenderNode("Brotato3D_PlayerWeapon_Shotgun", glm::vec3(0.24f, 0.43f, -0.62f),
                                                 glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()),
                                                 shotgunWeaponModelId, shotgunWeaponMaterialId);
    player_.shotgunWeaponNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.shotgunWeaponNode);

    enemyVisuals_.clear();
    for (const auto& [enemyId, def] : enemyDefs_)
    {
        models.push_back(Assets::FProcModel::CreateBox(-def.size * 0.5f, def.size * 0.5f));
        FEnemyVisualResource visual{};
        visual.modelId = static_cast<uint32_t>(models.size() - 1);
        visual.materialId = SceneBuilder::AddLambertianMaterial(materials, def.color);
        visual.darkMaterialId = SceneBuilder::AddLambertianMaterial(materials, def.color * 0.4f);
        visual.hitFlashMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f));
        visual.warningMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.08f, 0.06f));
        visual.phase2MaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.95f, 0.20f, 0.10f));
        visual.baseColor = def.color;
        enemyVisuals_[enemyId] = visual;
    }

    projectilePool_.clear();
    projectilePool_.reserve(weaponDefs_.size() * 128);
    for (const auto& [weaponId, weaponDef] : weaponDefs_)
    {
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), weaponDef.projectileSize));
        const uint32_t weaponProjectileModelId = static_cast<uint32_t>(models.size() - 1);
        const uint32_t weaponProjectileMaterialId = SceneBuilder::AddLambertianMaterial(materials, weaponDef.projectileColor);
        if (weaponId == "smg")
        {
            projectileModelId_ = weaponProjectileModelId;
            projectileMaterialId_ = weaponProjectileMaterialId;
        }

        for (int index = 0; index < 128; ++index)
        {
            auto node = SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Projectile_{}_{}", weaponId, index), HiddenPosition,
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
            enemyProjectileMaterialIds_[&enemyDef] = SceneBuilder::AddLambertianMaterial(materials, enemyDef.ranged.color);
        }
    }
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
    enemyProjectileModelId_ = static_cast<uint32_t>(models.size() - 1);
    enemyProjectileMaterialId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.3f, 0.95f, 0.2f));
    enemyProjectilePool_.clear();
    enemyProjectilePool_.reserve(128);
    for (int index = 0; index < 128; ++index)
    {
        auto node = SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_EnemyProjectile_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), enemyProjectileModelId_,
                                     enemyProjectileMaterialId_, false);
        nodes.push_back(node);
        Brotato3D::FEnemyProjectileRuntime projectile{};
        projectile.radius = 0.18f;
        projectile.node = node;
        enemyProjectilePool_.push_back(projectile);
    }

    BuildDebrisPool(models, materials, nodes);
    BuildKinematicCollisionBodies();

    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.12f));
    pickupXpModelId_ = static_cast<uint32_t>(models.size() - 1);
    pickupXpMaterialId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.2f, 1.0f, 0.35f));
    pickupPool_.clear();
    pickupPool_.reserve(128);
    for (int index = 0; index < 128; ++index)
    {
        auto node = SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Pickup_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), pickupXpModelId_, pickupXpMaterialId_, false);
        nodes.push_back(node);
        Brotato3D::FPickupRuntime pickup{};
        pickup.kind = Brotato3D::EPickupKind::XP;
        pickup.node = node;
        pickupPool_.push_back(pickup);
    }
    sceneReady_ = true;
    if (appState_ == Brotato3D::EAppState::MainMenu || appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        NodeUtils::SetVisible(player_.bodyNode, false);
    }
}

bool Brotato3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    glm::vec3 cameraPosition(0.0f, 18.0f, 11.0f);
    glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);
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

void Brotato3DGameInstance::UpdateCombatEffects(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
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
            NodeUtils::SetVisible(light.node, false);
            continue;
        }
        UpdateLightArea(light.lightIndex, light.worldPos, light.radiusMeters, intensityScale);
        light.node->SetTranslation(light.worldPos);
        light.node->SetScale(glm::vec3(std::max(0.05f, light.radiusMeters * intensityScale)));
    }
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

    auto& lights = engine_->GetScene().Lights();
    if (lightIt->lightIndex >= 0 && lightIt->lightIndex < static_cast<int>(lights.size()))
    {
        lights[static_cast<size_t>(lightIt->lightIndex)].lightMatIdx = EnsureLightMaterial(color);
    }
    NodeUtils::SetPrimaryMaterial(lightIt->node, EnsureLightMaterial(color));
    lightIt->node->SetTranslation(worldPos);
    lightIt->node->SetScale(glm::vec3(radiusMeters));
    NodeUtils::SetVisible(lightIt->node, true);
    UpdateLightArea(lightIt->lightIndex, worldPos, radiusMeters, 1.0f);
}

void Brotato3DGameInstance::UpdateLightArea(int lightIndex,
                                            const glm::vec3& worldPos,
                                            float radiusMeters,
                                            float intensityScale)
{
    if (!engine_)
    {
        return;
    }

    auto& lights = engine_->GetScene().Lights();
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

    const uint32_t materialId = SceneBuilder::AddDiffuseLightMaterialToScene(engine_->GetScene(), color, 650.0f);
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
    auto& envSettings = engine_->GetScene().GetEnvSettings();
    envSettings.HasSky = true;
    envSettings.HasSun = false;
    envSettings.SkyIntensity = 8.0f;
    engine_->GetScene().MarkEnvDirty();
}


