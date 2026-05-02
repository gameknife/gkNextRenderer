#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Assets/Loaders/FProcModel.h"
#include "Brotato3DAudio.hpp"

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
        materials.push_back({Assets::Material::DiffuseLight(color * intensity)});
        const uint32_t materialId = static_cast<uint32_t>(materials.size() - 1);
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
        auto node = CreateRenderNode(name,
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
        characterMaterialIds_[character.id] = AddLambertMaterial(materials, character.color);
    }
    const uint32_t playerMaterialId =
        characterMaterialIds_.contains(selectedCharacterId_) ? characterMaterialIds_[selectedCharacterId_] :
                                                               AddLambertMaterial(materials, glm::vec3(0.20f, 0.75f, 0.30f));
    player_.bodyNode = CreateRenderNode("Brotato3D_Player", player_.worldPos, glm::vec3(1.0f),
                                        static_cast<uint32_t>(nodes.size()), playerModelId, playerMaterialId);
    nodes.push_back(player_.bodyNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.08f), glm::vec3(0.08f)));
    const uint32_t facingModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t facingMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f));
    player_.facingNode = CreateRenderNode("Brotato3D_PlayerFacing", glm::vec3(0.0f, 0.45f, -0.25f), glm::vec3(1.0f),
                                          static_cast<uint32_t>(nodes.size()), facingModelId, facingMaterialId);
    player_.facingNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.facingNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.06f, -0.05f, -0.24f), glm::vec3(0.06f, 0.05f, 0.24f)));
    const uint32_t smgWeaponModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t smgWeaponMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f, 0.85f, 0.2f));
    player_.smgWeaponNode = CreateRenderNode("Brotato3D_PlayerWeapon_SMG", glm::vec3(-0.18f, 0.42f, -0.54f),
                                             glm::vec3(1.0f), static_cast<uint32_t>(nodes.size()), smgWeaponModelId,
                                             smgWeaponMaterialId);
    player_.smgWeaponNode->SetParent(player_.bodyNode);
    nodes.push_back(player_.smgWeaponNode);

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.11f, -0.06f, -0.34f), glm::vec3(0.11f, 0.06f, 0.34f)));
    const uint32_t shotgunWeaponModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t shotgunWeaponMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f, 0.4f, 0.2f));
    player_.shotgunWeaponNode = CreateRenderNode("Brotato3D_PlayerWeapon_Shotgun", glm::vec3(0.24f, 0.43f, -0.62f),
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
        visual.materialId = AddLambertMaterial(materials, def.color);
        visual.darkMaterialId = AddLambertMaterial(materials, def.color * 0.4f);
        visual.hitFlashMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f));
        visual.warningMaterialId = AddLambertMaterial(materials, glm::vec3(1.0f, 0.08f, 0.06f));
        visual.phase2MaterialId = AddLambertMaterial(materials, glm::vec3(0.95f, 0.20f, 0.10f));
        visual.baseColor = def.color;
        enemyVisuals_[enemyId] = visual;
    }

    projectilePool_.clear();
    projectilePool_.reserve(weaponDefs_.size() * 128);
    for (const auto& [weaponId, weaponDef] : weaponDefs_)
    {
        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), weaponDef.projectileSize));
        const uint32_t weaponProjectileModelId = static_cast<uint32_t>(models.size() - 1);
        const uint32_t weaponProjectileMaterialId = AddLambertMaterial(materials, weaponDef.projectileColor);
        if (weaponId == "smg")
        {
            projectileModelId_ = weaponProjectileModelId;
            projectileMaterialId_ = weaponProjectileMaterialId;
        }

        for (int index = 0; index < 128; ++index)
        {
            auto node = CreateRenderNode(fmt::format("Brotato3D_Projectile_{}_{}", weaponId, index), HiddenPosition,
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
            enemyProjectileMaterialIds_[&enemyDef] = AddLambertMaterial(materials, enemyDef.ranged.color);
        }
    }
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
    enemyProjectileModelId_ = static_cast<uint32_t>(models.size() - 1);
    enemyProjectileMaterialId_ = AddLambertMaterial(materials, glm::vec3(0.3f, 0.95f, 0.2f));
    enemyProjectilePool_.clear();
    enemyProjectilePool_.reserve(128);
    for (int index = 0; index < 128; ++index)
    {
        auto node = CreateRenderNode(fmt::format("Brotato3D_EnemyProjectile_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), enemyProjectileModelId_,
                                     enemyProjectileMaterialId_, false);
        nodes.push_back(node);
        Brotato3D::FEnemyProjectileRuntime projectile{};
        projectile.radius = 0.18f;
        projectile.node = node;
        enemyProjectilePool_.push_back(projectile);
    }

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.04f), glm::vec3(0.04f)));
    impactDebrisModelId_ = static_cast<uint32_t>(models.size() - 1);
    impactDebrisMaterialId_ = AddLambertMaterial(materials, glm::vec3(1.0f, 0.18f, 0.08f));
    impactDebrisPool_.clear();
    impactDebrisPool_.reserve(160);
    for (int index = 0; index < 160; ++index)
    {
        auto node = CreateRenderNode(fmt::format("Brotato3D_ImpactDebris_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), impactDebrisModelId_, impactDebrisMaterialId_, false);
        nodes.push_back(node);
        Brotato3D::FImpactDebrisRuntime debris{};
        debris.node = node;
        impactDebrisPool_.push_back(debris);
    }

    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.12f));
    pickupXpModelId_ = static_cast<uint32_t>(models.size() - 1);
    pickupXpMaterialId_ = AddLambertMaterial(materials, glm::vec3(0.2f, 1.0f, 0.35f));
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.10f));
    pickupMaterialModelId_ = static_cast<uint32_t>(models.size() - 1);
    pickupMaterialMaterialId_ = AddLambertMaterial(materials, glm::vec3(1.0f, 0.85f, 0.15f));
    pickupPool_.clear();
    pickupPool_.reserve(256);
    for (int index = 0; index < 256; ++index)
    {
        const bool xpSlot = index < 128;
        auto node = CreateRenderNode(fmt::format("Brotato3D_Pickup_{}", index), HiddenPosition, glm::vec3(1.0f),
                                     static_cast<uint32_t>(nodes.size()), xpSlot ? pickupXpModelId_ : pickupMaterialModelId_,
                                     xpSlot ? pickupXpMaterialId_ : pickupMaterialMaterialId_, false);
        nodes.push_back(node);
        Brotato3D::FPickupRuntime pickup{};
        pickup.kind = xpSlot ? Brotato3D::EPickupKind::XP : Brotato3D::EPickupKind::Material;
        pickup.node = node;
        pickupPool_.push_back(pickup);
    }
    sceneReady_ = true;
    if (appState_ == Brotato3D::EAppState::MainMenu || appState_ == Brotato3D::EAppState::CharacterSelect)
    {
        HideNode(player_.bodyNode);
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

void Brotato3DGameInstance::UpdateImpactDebris(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& debris : impactDebrisPool_)
    {
        if (!debris.active)
        {
            continue;
        }

        debris.remainingMs -= deltaMs;
        if (debris.remainingMs <= 0.0f)
        {
            debris.active = false;
            SetNodeTranslation(debris.node, HiddenPosition);
            HideNode(debris.node);
            continue;
        }

        debris.velocity.y -= 9.8f * static_cast<float>(deltaSeconds);
        debris.worldPos += debris.velocity * static_cast<float>(deltaSeconds);
        SetNodeTranslation(debris.node, debris.worldPos);
    }
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
    SetNodeTranslation(playerLightNode_, playerLightPos);
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
            SetNodeTranslation(light.node, HiddenPosition);
            HideNode(light.node);
            continue;
        }
        UpdateLightArea(light.lightIndex, light.worldPos, light.radiusMeters, intensityScale);
        SetNodeTranslation(light.node, light.worldPos);
        SetNodeScale(light.node, glm::vec3(std::max(0.05f, light.radiusMeters * intensityScale)));
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
    SetNodeMaterial(lightIt->node, EnsureLightMaterial(color));
    SetNodeTranslation(lightIt->node, worldPos);
    SetNodeScale(lightIt->node, glm::vec3(radiusMeters));
    ShowNode(lightIt->node);
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

    const uint32_t materialId = engine_->GetScene().AddMaterial({Assets::Material::DiffuseLight(color * 650.0f)});
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

void Brotato3DGameInstance::SpawnImpactDebris(const glm::vec3& worldPos)
{
    std::uniform_real_distribution<float> horizontalDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> speedDist(2.0f, 4.0f);
    for (int count = 0; count < 3; ++count)
    {
        auto it = std::find_if(impactDebrisPool_.begin(), impactDebrisPool_.end(),
                               [](const Brotato3D::FImpactDebrisRuntime& debris)
                               {
                                   return !debris.active;
                               });
        if (it == impactDebrisPool_.end())
        {
            return;
        }

        glm::vec3 dir(horizontalDist(rng_), 0.8f, horizontalDist(rng_));
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        dir = glm::normalize(dir);
        it->active = true;
        it->worldPos = worldPos;
        it->worldPos.y = std::max(0.35f, it->worldPos.y);
        it->velocity = dir * speedDist(rng_);
        it->lifeMs = 400.0f;
        it->remainingMs = it->lifeMs;
        SetNodeTranslation(it->node, it->worldPos);
        ShowNode(it->node);
    }
}

void Brotato3DGameInstance::HideNode(const std::shared_ptr<Assets::Node>& node)
{
    if (node)
    {
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetVisible(false);
        }
    }
}

void Brotato3DGameInstance::ShowNode(const std::shared_ptr<Assets::Node>& node)
{
    if (node)
    {
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetVisible(true);
        }
    }
}

void Brotato3DGameInstance::SetNodeMaterial(const std::shared_ptr<Assets::Node>& node, uint32_t materialId)
{
    if (node)
    {
        if (auto render = node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetMaterial({materialId});
        }
    }
}

void Brotato3DGameInstance::SetNodeTranslation(const std::shared_ptr<Assets::Node>& node, const glm::vec3& translation)
{
    if (node)
    {
        node->SetTranslation(translation);
        node->RecalcTransform(true);
    }
}

void Brotato3DGameInstance::SetNodeRotation(const std::shared_ptr<Assets::Node>& node, const glm::quat& rotation)
{
    if (node)
    {
        node->SetRotation(rotation);
        node->RecalcTransform(true);
    }
}

void Brotato3DGameInstance::SetNodeScale(const std::shared_ptr<Assets::Node>& node, const glm::vec3& scale)
{
    if (node)
    {
        node->SetScale(scale);
        node->RecalcTransform(true);
    }
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

