#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Assets/Core/Node.h"
#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

namespace
{
    constexpr float PlayerDashSpeed = 26.0f;
    constexpr float PlayerDashDurationMs = 140.0f;
    constexpr float PlayerDashCooldownMs = 1700.0f;
    constexpr float PlayerDashInputEpsilon = 0.001f;

    glm::vec3 ResolveInputDirection(bool keyW, bool keyA, bool keyS, bool keyD, const glm::vec2& gamepadMoveInput)
    {
        glm::vec3 keyboardDir(static_cast<float>(keyD) - static_cast<float>(keyA), 0.0f,
                              static_cast<float>(keyS) - static_cast<float>(keyW));
        if (glm::length(keyboardDir) > PlayerDashInputEpsilon)
        {
            keyboardDir = glm::normalize(keyboardDir);
        }
        glm::vec3 inputDir = keyboardDir + glm::vec3(gamepadMoveInput.x, 0.0f, gamepadMoveInput.y);
        if (glm::length(inputDir) > 1.0f)
        {
            inputDir = glm::normalize(inputDir);
        }
        return inputDir;
    }
}

bool Brotato3DGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP &&
        event.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN && event.type != SDL_EVENT_GAMEPAD_BUTTON_UP)
    {
        return false;
    }

    const bool pressed = (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
    const bool isEscape = (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && event.key.key == SDLK_ESCAPE;
    const bool isStartButton = (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) && event.gbutton.button == SDL_GAMEPAD_BUTTON_START;
    const bool isDashButton = ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                               (event.key.key == SDLK_LSHIFT || event.key.key == SDLK_RSHIFT)) ||
                              ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
                               event.gbutton.button == SDL_GAMEPAD_BUTTON_WEST);

    if (isDashButton)
    {
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            TryStartDash();
        }
        return true;
    }

    if (isEscape || isStartButton)
    {
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            PauseGame();
        }
        else if (pressed && appState_ == Brotato3D::EAppState::Paused)
        {
            ResumeGame();
        }
        return true;
    }

    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
    {
        switch (event.key.key)
        {
        case SDLK_W:
            keyW_ = pressed;
            return true;
        case SDLK_A:
            keyA_ = pressed;
            return true;
        case SDLK_S:
            keyS_ = pressed;
            return true;
        case SDLK_D:
            keyD_ = pressed;
            return true;
        case SDLK_K:
#if DEV_MODE
            if (pressed && appState_ == Brotato3D::EAppState::Playing)
            {
                SpawnEnemy("rat", RandomDebugSpawnPosition());
            }
#endif
            return true;
#if DEV_MODE
    case SDLK_1:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            SetDebugSingleWeapon("smg");
        }
        return true;
    case SDLK_2:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            SetDebugSingleWeapon("shotgun");
        }
        return true;
    case SDLK_3:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            SetDebugSingleWeapon("sniper");
        }
        return true;
    case SDLK_4:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            SetDebugSingleWeapon("flamethrower");
        }
        return true;
    case SDLK_5:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            SetDebugSingleWeapon("rocket");
        }
        return true;
    case SDLK_6:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            SetDebugSingleWeapon("laser");
        }
        return true;
#endif
    default:
        break;
    }
    }
    return false;
}

bool Brotato3DGameInstance::OnGamepadInput(int16_t leftStickX,
                                           int16_t leftStickY,
                                           int16_t rightStickX,
                                           int16_t rightStickY,
                                           int16_t leftTrigger,
                                           int16_t rightTrigger)
{
    (void)rightStickX;
    (void)rightStickY;
    (void)leftTrigger;
    (void)rightTrigger;

    constexpr float stickSensitivity = 1.0f / 32767.0f;
    constexpr int16_t deadZone = 3000;
    glm::vec2 rawInput(static_cast<float>(leftStickX), static_cast<float>(leftStickY));
    const float rawLength = glm::length(rawInput);
    if (rawLength <= static_cast<float>(deadZone))
    {
        gamepadMoveInput_ = glm::vec2(0.0f);
        return false;
    }

    gamepadMoveInput_ = rawInput * stickSensitivity;
    if (glm::length(gamepadMoveInput_) > 1.0f)
    {
        gamepadMoveInput_ = glm::normalize(gamepadMoveInput_);
    }
    return true;
}

void Brotato3DGameInstance::UpdatePlayer(double deltaSeconds)
{
    const glm::vec3 previousPlayerPos = player_.worldPos;
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    const int dashMaxCharges = GetDashMaxCharges();
    player_.dashCharges = std::clamp(player_.dashCharges, 0, dashMaxCharges);
    if (player_.dashCharges < dashMaxCharges)
    {
        player_.dashCooldownMs -= static_cast<float>(deltaSeconds * 1000.0);
        while (player_.dashCooldownMs <= 0.0f && player_.dashCharges < dashMaxCharges)
        {
            ++player_.dashCharges;
            if (player_.dashCharges < dashMaxCharges)
            {
                player_.dashCooldownMs += PlayerDashCooldownMs;
            }
            else
            {
                player_.dashCooldownMs = 0.0f;
            }
        }
    }
    else
    {
        player_.dashCooldownMs = 0.0f;
    }
    glm::vec3 inputDir = ResolveInputDirection(keyW_, keyA_, keyS_, keyD_, gamepadMoveInput_);

    if (glm::length(inputDir) > 0.001f)
    {
        if (equippedWeapons_.empty())
        {
            player_.facingDir = glm::normalize(inputDir);
        }
    }

    Brotato3D::FEnemyRuntime* aimTarget = nullptr;
    float bestDistance = FLT_MAX;
    float bestRange = 0.0f;
    for (const auto& weapon : equippedWeapons_)
    {
        if (weapon.def)
        {
            bestRange = std::max(bestRange, weapon.def->rangeMeters * (1.0f + effectiveStats.rangePct));
        }
    }
    for (auto& enemy : enemies_)
    {
        if (!enemy.alive)
        {
            continue;
        }
        const float distance = DistanceXZ(enemy.worldPos, player_.worldPos);
        if (distance <= bestRange &&
            distance < bestDistance &&
            !IsSegmentBlockedByExtractionVehicle(player_.worldPos, enemy.worldPos, 0.05f))
        {
            bestDistance = distance;
            aimTarget = &enemy;
        }
    }
    if (aimTarget)
    {
        glm::vec3 targetDir = aimTarget->worldPos - player_.worldPos;
        targetDir.y = 0.0f;
        if (glm::length(targetDir) > 0.001f)
        {
            const float aimLerp = 1.0f - std::exp(-14.0f * static_cast<float>(deltaSeconds));
            player_.facingDir = glm::normalize(glm::mix(player_.facingDir, glm::normalize(targetDir), aimLerp));
        }
    }

    const float speed = PlayerBaseSpeed * (1.0f + effectiveStats.moveSpeedPct);
    if (player_.dashRemainingMs > 0.0f)
    {
        const float dashStepMs = std::min(player_.dashRemainingMs, static_cast<float>(deltaSeconds * 1000.0));
        player_.dashRemainingMs = std::max(0.0f, player_.dashRemainingMs - static_cast<float>(deltaSeconds * 1000.0));
        player_.worldPos = ClampToArena(player_.worldPos + player_.dashDir * PlayerDashSpeed * (dashStepMs / 1000.0f),
                                        player_.radius,
                                        arenaHalfExtent_);
        player_.worldPos = ResolvePlayerObstacleCollision(player_.worldPos, player_.radius);
        player_.worldPos = ClampToArena(player_.worldPos, player_.radius, arenaHalfExtent_);
        if (player_.dashRemainingMs <= 0.0f)
        {
            FinishDash();
        }
    }
    else
    {
        player_.worldPos = ClampToArena(player_.worldPos + inputDir * speed * static_cast<float>(deltaSeconds),
                                        player_.radius,
                                        arenaHalfExtent_);
        player_.worldPos = ResolvePlayerObstacleCollision(player_.worldPos, player_.radius);
        player_.worldPos = ClampToArena(player_.worldPos, player_.radius, arenaHalfExtent_);
    }
    player_.worldPos.y = 0.5f + SampleArenaGroundY(player_.worldPos);
    playerVelocity_ = deltaSeconds > 0.0 ? (player_.worldPos - previousPlayerPos) / static_cast<float>(deltaSeconds) :
                                           glm::vec3(0.0f);
    if (player_.bodyNode)
    {
        player_.bodyNode->SetTranslation(player_.worldPos);
    }
    SyncPlayerKinematicBody(deltaSeconds);
    if (player_.facingNode)
    {
        const glm::vec3 localOffset = glm::vec3(player_.facingDir.x, 0.0f, player_.facingDir.z) * 0.45f;
        player_.facingNode->SetTranslation(glm::vec3(localOffset.x, 0.62f, localOffset.z));
    }

    const glm::vec3 forward = glm::normalize(glm::vec3(player_.facingDir.x, 0.0f, player_.facingDir.z));
    const glm::vec3 right = glm::normalize(glm::vec3(forward.z, 0.0f, -forward.x));
    const glm::quat weaponRotation = glm::angleAxis(std::atan2(-forward.x, -forward.z), glm::vec3(0.0f, 1.0f, 0.0f));
    const bool hasSmg = std::any_of(equippedWeapons_.begin(), equippedWeapons_.end(), [](const Brotato3D::FWeaponRuntime& weapon)
    {
        return weapon.weaponId == "smg";
    });
    const bool hasShotgun = std::any_of(equippedWeapons_.begin(), equippedWeapons_.end(), [](const Brotato3D::FWeaponRuntime& weapon)
    {
        return weapon.weaponId == "shotgun";
    });
    if (player_.smgWeaponNode)
    {
        const glm::vec3 smgOffset = forward * 0.54f - right * 0.18f;
        player_.smgWeaponNode->SetTranslation(glm::vec3(smgOffset.x, 0.42f, smgOffset.z));
        player_.smgWeaponNode->SetRotation(weaponRotation);
        NodeUtils::SetVisible(player_.smgWeaponNode, hasSmg);
    }
    if (player_.shotgunWeaponNode)
    {
        const glm::vec3 shotgunOffset = forward * 0.62f + right * 0.24f;
        player_.shotgunWeaponNode->SetTranslation(glm::vec3(shotgunOffset.x, 0.43f, shotgunOffset.z));
        player_.shotgunWeaponNode->SetRotation(weaponRotation);
        NodeUtils::SetVisible(player_.shotgunWeaponNode, hasShotgun);
    }

    if (player_.dashRemainingMs > 0.0f)
    {
        PushLaserBeam(previousPlayerPos + glm::vec3(0.0f, 0.18f, 0.0f),
                      player_.worldPos + glm::vec3(0.0f, 0.18f, 0.0f),
                      glm::vec4(0.55f, 0.95f, 1.0f, 0.55f),
                      90.0f,
                      0.10f);
    }
}

void Brotato3DGameInstance::TryStartDash()
{
    if (player_.dashCharges <= 0 || player_.dashRemainingMs > 0.0f)
    {
        return;
    }

    glm::vec3 dashDir = ResolveInputDirection(keyW_, keyA_, keyS_, keyD_, gamepadMoveInput_);
    if (glm::length(dashDir) <= PlayerDashInputEpsilon)
    {
        dashDir = glm::vec3(player_.facingDir.x, 0.0f, player_.facingDir.z);
    }
    if (glm::length(dashDir) <= PlayerDashInputEpsilon)
    {
        dashDir = glm::vec3(0.0f, 0.0f, -1.0f);
    }

    player_.dashDir = glm::normalize(dashDir);
    player_.dashDurationMs = PlayerDashDurationMs;
    player_.dashRemainingMs = PlayerDashDurationMs;
    --player_.dashCharges;
    if (player_.dashCharges < GetDashMaxCharges() && player_.dashCooldownMs <= 0.0f)
    {
        player_.dashCooldownMs = PlayerDashCooldownMs;
    }
    StartScreenShake(80.0f, 0.9f);
    SpawnTempLight(player_.worldPos + glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.42f, 0.9f, 1.0f), 2.2f, 120.0f);
}

void Brotato3DGameInstance::FinishDash()
{
    player_.dashRemainingMs = 0.0f;
    player_.dashDurationMs = 0.0f;
    ProcessDashEndItemTriggers();
}

void Brotato3DGameInstance::DamagePlayer(int damage, float shakeMs, float flashMs)
{
    if (damage <= 0 || appState_ == Brotato3D::EAppState::Result || player_.dashRemainingMs > 0.0f)
    {
        return;
    }

    player_.currentHp -= damage;
    SpawnPlayerDamageDebris(damage);
    Brotato3D::PlayPlayerHurtSfx();
    const float shakeIntensity = damage < 5 ? 1.0f : (damage <= 15 ? 2.0f : 3.5f);
    StartScreenShake(shakeMs, shakeIntensity);
    damageFlashMs_ = std::max(damageFlashMs_, flashMs);
    PushFloatingText(player_.worldPos + glm::vec3(0.0f, 1.0f, 0.0f), fmt::format("-{}", damage),
                     glm::vec4(0.8f, 0.35f, 1.0f, 1.0f), 700.0f);
    spdlog::info("[Brotato3D] player HP {}", player_.currentHp);
    if (player_.currentHp <= 0)
    {
        player_.currentHp = 0;
        playerDead_ = true;
        spdlog::info("[Brotato3D] [player dead]");
        spdlog::info("[Brotato3D] [defeat]");
        waveSystem_.ForceAllCleared();
        EnterResult(true);
    }
}

int Brotato3DGameInstance::GetXpToNextLevel() const
{
    const int level = std::max(1, player_.level);
    return 20 + level * 10 + level * level * 2;
}

void Brotato3DGameInstance::ApplyUpgrade(const std::string& stat, float delta)
{
    Brotato3D::FShopItemDef item{};
    item.stat = stat;
    item.delta = delta;
    ApplyShopItem(item);
}

void Brotato3DGameInstance::ResetRuntimeState()
{
    const std::shared_ptr<Assets::Node> playerBodyNode = player_.bodyNode;
    const std::shared_ptr<Assets::Node> playerFacingNode = player_.facingNode;
    const std::shared_ptr<Assets::Node> playerSmgWeaponNode = player_.smgWeaponNode;
    const std::shared_ptr<Assets::Node> playerShotgunWeaponNode = player_.shotgunWeaponNode;
    for (auto& enemy : enemies_)
    {
        DeactivateEnemyKinematicBody(enemy);
        enemy.alive = false;
        enemy.fading = false;
        enemy.hitFlashRemainingMs = 0.0f;
        enemy.deathFadeMs = 0.0f;
        enemy.contactCooldownMs = 0.0f;
        enemy.rangedFireCooldownMs = 0.0f;
        enemy.chargeRampMs = 0.0f;
        enemy.chargeCooldownMs = 0.0f;
        enemy.charging = false;
        enemy.bombFuseMs = -1.0f;
        enemy.healIntervalMs = 0.0f;
        enemy.bossPhase2Active = false;
        enemy.mortarFireCooldownMs = 0.0f;
        enemy.mortarTelegraphRemainingMs = 0.0f;
        enemy.mortarTargetPos = glm::vec3(0.0f);
        enemy.lanceState = Brotato3D::ELanceState::Idle;
        enemy.lanceStateMs = 0.0f;
        enemy.lanceCooldownMs = 0.0f;
        enemy.lanceDashDir = glm::vec3(0.0f);
        enemy.lanceDashStartPos = glm::vec3(0.0f);
        enemy.lanceDashRemainingDist = 0.0f;
        SetEnemyVisualVisible(enemy, false);
        enemy.node->SetScale(glm::vec3(1.0f));
    }
    player_ = Brotato3D::FPlayerRuntime{};
    player_.bodyNode = playerBodyNode;
    player_.facingNode = playerFacingNode;
    player_.smgWeaponNode = playerSmgWeaponNode;
    player_.shotgunWeaponNode = playerShotgunWeaponNode;
    for (auto& projectile : projectilePool_)
    {
        projectile.active = false;
        projectile.hitEnemyIndices.clear();
        NodeUtils::SetVisible(projectile.node, false);
        projectile.node->SetTranslation(HiddenPosition);
    }
    for (auto& projectile : enemyProjectilePool_)
    {
        projectile.active = false;
        NodeUtils::SetVisible(projectile.node, false);
        projectile.node->SetTranslation(HiddenPosition);
    }
    ClearAllDebris(false);
    floatingTexts_.clear();
    muzzleFlashes_.clear();
    explosionRings_.clear();
    laserBeams_.clear();
    groundIndicators_.clear();
    for (auto& light : tempLightPool_)
    {
        light.active = false;
        light.remainingMs = 0.0f;
        UpdateLightArea(light.lightIndex, HiddenPosition, 0.01f, 0.0f);
        light.node->SetTranslation(HiddenPosition);
        NodeUtils::SetVisible(light.node, false);
    }
    currentUpgradeChoices_.clear();
    shopOffers_.clear();
    equippedWeapons_.clear();
    waveSystem_.LoadWaves(waveDefs_);
    waveSystem_.SetArenaHalfExtent(arenaHalfExtent_);
    screenShakeMs_ = 0.0f;
    screenShakeIntensity_ = 0.0f;
    damageFlashMs_ = 0.0f;
    hitStopMs_ = 0.0f;
    globalTimeScale_ = 1.0f;
    bossKillFlashMs_ = 0.0f;
    timeScaleRecoveryMs_ = 0.0f;
    bossVictoryDelayMs_ = 0.0f;
    waveBannerMs_ = 0.0f;
    waveBannerText_.clear();
    ResetExtractionVehicle();
    currentSkyIntensity_ = 30.0f;
    skyTransitionStartIntensity_ = 30.0f;
    targetSkyIntensity_ = 30.0f;
    skyTransitionTotalMs_ = 0.0f;
    skyTransitionRemainingMs_ = 0.0f;
    ApplyLightingSettings();
    weaponMergeBannerMs_ = 0.0f;
    weaponMergeBannerText_.clear();
    runElapsedSec_ = 0.0f;
    killCount_ = 0;
    totalMaterialsGained_ = 0;
    playerDead_ = false;
    playerVelocity_ = glm::vec3(0.0f);
    player_.dashCooldownMs = 0.0f;
    player_.dashRemainingMs = 0.0f;
    player_.dashDurationMs = 0.0f;
    player_.dashCharges = 3;
    player_.dashDir = glm::vec3(0.0f);
    dynamicStatBuffs_.damagePct = 0.0f;
    dynamicStatBuffs_.damageFlat = 0.0f;
    dynamicStatBuffs_.atkSpeedPct = 0.0f;
    dynamicStatBuffs_.rangePct = 0.0f;
    dynamicStatBuffs_.moveSpeedPct = 0.0f;
    dynamicStatBuffs_.pickupRadiusPct = 0.0f;
    dynamicStatBuffs_.critChancePct = 0.0f;
    itemTickAccumMs_ = 0.0f;
    itemHealRemainder_ = 0.0f;
    if (NextPhysics* physics = GetEngine().GetPhysicsEngine();
        physics && !playerKinematicBodyId_.IsInvalid())
    {
        physics->SetBodyActive(playerKinematicBodyId_, false);
        physics->SetBodyTransform(playerKinematicBodyId_, HiddenPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
        playerKinematicBodyActive_ = false;
    }
    ClearMovementInput();
    NodeUtils::SetVisible(player_.bodyNode, false);
    NodeUtils::SetVisible(player_.facingNode, false);
    NodeUtils::SetVisible(player_.smgWeaponNode, false);
    NodeUtils::SetVisible(player_.shotgunWeaponNode, false);
}

void Brotato3DGameInstance::ApplySelectedCharacter()
{
    const Brotato3D::FCharacterDef* character = FindCharacterDef(selectedCharacterId_);
    if (!character && !characterDefs_.empty())
    {
        selectedCharacterId_ = characterDefs_.front().id;
        character = &characterDefs_.front();
    }
    if (!character)
    {
        return;
    }

    player_.stats = Brotato3D::FPlayerStats{};
    player_.stats.maxHpFlat = character->startStats.maxHpFlat;
    player_.stats.damagePct += character->startStats.damagePct;
    player_.stats.damageFlat += character->startStats.damageFlat;
    player_.stats.atkSpeedPct += character->startStats.atkSpeedPct;
    player_.stats.rangePct += character->startStats.rangePct;
    player_.stats.moveSpeedPct += character->startStats.moveSpeedPct;
    player_.stats.pickupRadiusPct += character->startStats.pickupRadiusPct;
    player_.stats.critChancePct += character->startStats.critChancePct;
    player_.stats.critMultiplier += character->startStats.critMultiplier;
    player_.stats.dashChargeBonus += character->startStats.dashChargeBonus;
    player_.maxHp = static_cast<int>(std::round(player_.stats.maxHpFlat)) + player_.stats.maxHpFlatBonus;
    player_.currentHp = player_.maxHp;
    player_.dashCharges = GetDashMaxCharges();

    equippedWeapons_.clear();
    Brotato3D::FWeaponRuntime startingWeapon = CreateWeaponRuntime(character->startWeapon, 1);
    if (startingWeapon.def)
    {
        equippedWeapons_.push_back(startingWeapon);
        NormalizeWeaponDefPointers();
    }

    if (const auto materialIt = characterMaterialIds_.find(character->id); materialIt != characterMaterialIds_.end())
    {
        NodeUtils::SetPrimaryMaterial(player_.bodyNode, materialIt->second);
    }
    playerDebrisMatId_ = SceneBuilder::AddLambertianMaterialToScene(GetEngine().GetScene(),
                                                                    character->color * 0.6f + glm::vec3(0.4f));
}

void Brotato3DGameInstance::ClearMovementInput()
{
    keyW_ = false;
    keyA_ = false;
    keyS_ = false;
    keyD_ = false;
    gamepadMoveInput_ = glm::vec2(0.0f);
}

float Brotato3DGameInstance::GetDashCooldownRatio() const
{
    if (player_.dashRemainingMs > 0.0f)
    {
        return 1.0f;
    }
    if (player_.dashCharges >= GetDashMaxCharges())
    {
        return 1.0f;
    }
    return 1.0f - std::clamp(player_.dashCooldownMs / PlayerDashCooldownMs, 0.0f, 1.0f);
}

int Brotato3DGameInstance::GetDashMaxCharges() const
{
    return std::max(1, 3 + player_.stats.dashChargeBonus);
}

glm::vec3 Brotato3DGameInstance::RandomDebugSpawnPosition()
{
    std::uniform_int_distribution<int> sideDist(0, 3);
    std::uniform_real_distribution<float> xDist(-arenaHalfExtent_.x, arenaHalfExtent_.x);
    std::uniform_real_distribution<float> zDist(-arenaHalfExtent_.y, arenaHalfExtent_.y);
    switch (sideDist(rng_))
    {
    case 0:
        return glm::vec3(xDist(rng_), 0.3f, -arenaHalfExtent_.y);
    case 1:
        return glm::vec3(xDist(rng_), 0.3f, arenaHalfExtent_.y);
    case 2:
        return glm::vec3(-arenaHalfExtent_.x, 0.3f, zDist(rng_));
    default:
        return glm::vec3(arenaHalfExtent_.x, 0.3f, zDist(rng_));
    }
}


