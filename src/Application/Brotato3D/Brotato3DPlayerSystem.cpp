#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

bool Brotato3DGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }

    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key)
    {
    case SDLK_ESCAPE:
        if (pressed && appState_ == Brotato3D::EAppState::Playing)
        {
            PauseGame();
        }
        else if (pressed && appState_ == Brotato3D::EAppState::Paused)
        {
            ResumeGame();
        }
        return true;
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
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    glm::vec3 keyboardDir(static_cast<float>(keyD_) - static_cast<float>(keyA_), 0.0f,
                          static_cast<float>(keyS_) - static_cast<float>(keyW_));
    if (glm::length(keyboardDir) > 0.001f)
    {
        keyboardDir = glm::normalize(keyboardDir);
    }

    glm::vec3 inputDir = keyboardDir + glm::vec3(gamepadMoveInput_.x, 0.0f, gamepadMoveInput_.y);
    if (glm::length(inputDir) > 1.0f)
    {
        inputDir = glm::normalize(inputDir);
    }

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
        if (distance <= bestRange && distance < bestDistance)
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
    player_.worldPos = ClampToArena(player_.worldPos + inputDir * speed * static_cast<float>(deltaSeconds), player_.radius);
    if (player_.bodyNode)
    {
        SetNodeTranslation(player_.bodyNode, player_.worldPos);
    }
    if (player_.facingNode)
    {
        const glm::vec3 localOffset = glm::vec3(player_.facingDir.x, 0.0f, player_.facingDir.z) * 0.45f;
        SetNodeTranslation(player_.facingNode, glm::vec3(localOffset.x, 0.62f, localOffset.z));
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
        SetNodeTranslation(player_.smgWeaponNode, glm::vec3(smgOffset.x, 0.42f, smgOffset.z));
        SetNodeRotation(player_.smgWeaponNode, weaponRotation);
        hasSmg ? ShowNode(player_.smgWeaponNode) : HideNode(player_.smgWeaponNode);
    }
    if (player_.shotgunWeaponNode)
    {
        const glm::vec3 shotgunOffset = forward * 0.62f + right * 0.24f;
        SetNodeTranslation(player_.shotgunWeaponNode, glm::vec3(shotgunOffset.x, 0.43f, shotgunOffset.z));
        SetNodeRotation(player_.shotgunWeaponNode, weaponRotation);
        hasShotgun ? ShowNode(player_.shotgunWeaponNode) : HideNode(player_.shotgunWeaponNode);
    }
}

void Brotato3DGameInstance::DamagePlayer(int damage, float shakeMs, float flashMs)
{
    if (damage <= 0 || appState_ == Brotato3D::EAppState::Result)
    {
        return;
    }

    player_.currentHp -= damage;
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
    return 5 + player_.level * 4;
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
        HideNode(enemy.node);
        SetNodeScale(enemy.node, glm::vec3(1.0f));
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
        HideNode(projectile.node);
        SetNodeTranslation(projectile.node, HiddenPosition);
    }
    for (auto& projectile : enemyProjectilePool_)
    {
        projectile.active = false;
        HideNode(projectile.node);
        SetNodeTranslation(projectile.node, HiddenPosition);
    }
    for (auto& debris : impactDebrisPool_)
    {
        debris.active = false;
        HideNode(debris.node);
        SetNodeTranslation(debris.node, HiddenPosition);
    }
    for (auto& pickup : pickupPool_)
    {
        pickup.active = false;
        pickup.magnetized = false;
        HideNode(pickup.node);
        SetNodeTranslation(pickup.node, HiddenPosition);
    }
    floatingTexts_.clear();
    muzzleFlashes_.clear();
    explosionRings_.clear();
    laserBeams_.clear();
    for (auto& light : tempLightPool_)
    {
        light.active = false;
        light.remainingMs = 0.0f;
        UpdateLightArea(light.lightIndex, HiddenPosition, 0.01f, 0.0f);
        SetNodeTranslation(light.node, HiddenPosition);
        HideNode(light.node);
    }
    currentUpgradeChoices_.clear();
    shopOffers_.clear();
    equippedWeapons_.clear();
    waveSystem_.LoadWaves(waveDefs_);
    screenShakeMs_ = 0.0f;
    screenShakeIntensity_ = 0.0f;
    damageFlashMs_ = 0.0f;
    hitStopMs_ = 0.0f;
    waveBannerMs_ = 0.0f;
    waveBannerText_.clear();
    weaponMergeBannerMs_ = 0.0f;
    weaponMergeBannerText_.clear();
    runElapsedSec_ = 0.0f;
    killCount_ = 0;
    totalMaterialsGained_ = 0;
    playerDead_ = false;
    dynamicStatBuffs_.damagePct = 0.0f;
    dynamicStatBuffs_.damageFlat = 0.0f;
    dynamicStatBuffs_.atkSpeedPct = 0.0f;
    dynamicStatBuffs_.rangePct = 0.0f;
    dynamicStatBuffs_.moveSpeedPct = 0.0f;
    dynamicStatBuffs_.pickupRadiusPct = 0.0f;
    dynamicStatBuffs_.critChancePct = 0.0f;
    itemTickAccumMs_ = 0.0f;
    itemHealRemainder_ = 0.0f;
    ClearMovementInput();
    HideNode(player_.bodyNode);
    HideNode(player_.facingNode);
    HideNode(player_.smgWeaponNode);
    HideNode(player_.shotgunWeaponNode);
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
    player_.maxHp = static_cast<int>(std::round(player_.stats.maxHpFlat)) + player_.stats.maxHpFlatBonus;
    player_.currentHp = player_.maxHp;

    equippedWeapons_.clear();
    Brotato3D::FWeaponRuntime startingWeapon = CreateWeaponRuntime(character->startWeapon, 1);
    if (startingWeapon.def)
    {
        equippedWeapons_.push_back(startingWeapon);
        NormalizeWeaponDefPointers();
    }

    if (const auto materialIt = characterMaterialIds_.find(character->id); materialIt != characterMaterialIds_.end())
    {
        SetNodeMaterial(player_.bodyNode, materialIt->second);
    }
}

void Brotato3DGameInstance::ClearMovementInput()
{
    keyW_ = false;
    keyA_ = false;
    keyS_ = false;
    keyD_ = false;
    gamepadMoveInput_ = glm::vec2(0.0f);
}

glm::vec3 Brotato3DGameInstance::RandomDebugSpawnPosition()
{
    std::uniform_int_distribution<int> sideDist(0, 3);
    std::uniform_real_distribution<float> xDist(-ArenaHalfWidth, ArenaHalfWidth);
    std::uniform_real_distribution<float> zDist(-ArenaHalfDepth, ArenaHalfDepth);
    switch (sideDist(rng_))
    {
    case 0:
        return glm::vec3(xDist(rng_), 0.3f, -ArenaHalfDepth);
    case 1:
        return glm::vec3(xDist(rng_), 0.3f, ArenaHalfDepth);
    case 2:
        return glm::vec3(-ArenaHalfWidth, 0.3f, zDist(rng_));
    default:
        return glm::vec3(ArenaHalfWidth, 0.3f, zDist(rng_));
    }
}

