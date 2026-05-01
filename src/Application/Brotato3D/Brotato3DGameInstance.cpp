#include "Brotato3DGameInstance.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FProcModel.h"
#include "Brotato3DUI.hpp"
#include "Runtime/Components/RenderComponent.h"

namespace
{
    constexpr const char* EnemiesConfigPath = "assets/configs/brotato3d/enemies.json";
    constexpr const char* WeaponsConfigPath = "assets/configs/brotato3d/weapons.json";
    constexpr const char* UpgradesConfigPath = "assets/configs/brotato3d/upgrades.json";
    constexpr const char* WavesConfigPath = "assets/configs/brotato3d/waves.json";
    constexpr const char* ShopItemsConfigPath = "assets/configs/brotato3d/shop_items.json";
    constexpr float ArenaHalfWidth = 12.0f;
    constexpr float ArenaHalfDepth = 8.0f;
    constexpr float PlayerBaseSpeed = 5.0f;
    constexpr float PickupBaseRadius = 1.6f;
    constexpr glm::vec3 HiddenPosition(0.0f, -100.0f, 0.0f);

    uint32_t AddLambertMaterial(std::vector<Assets::FMaterial>& materials, const glm::vec3& color)
    {
        materials.push_back({Assets::Material::Lambertian(glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)))});
        return static_cast<uint32_t>(materials.size() - 1);
    }

    std::shared_ptr<Assets::Node> CreateRenderNode(const std::string& name,
                                                   const glm::vec3& translation,
                                                   const glm::vec3& scale,
                                                   uint32_t instanceId,
                                                   uint32_t modelId,
                                                   uint32_t materialId,
                                                   bool visible = true)
    {
        auto node = Assets::Node::CreateNode(name, translation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), scale, instanceId);
        auto renderComponent = std::make_shared<Runtime::RenderComponent>();
        renderComponent->SetModelId(modelId);
        renderComponent->SetMaterial({materialId});
        renderComponent->SetVisible(visible);
        node->AddComponent(renderComponent);
        return node;
    }

    glm::vec3 ClampToArena(const glm::vec3& pos, float radius)
    {
        return glm::vec3(std::clamp(pos.x, -ArenaHalfWidth + radius, ArenaHalfWidth - radius), pos.y,
                         std::clamp(pos.z, -ArenaHalfDepth + radius, ArenaHalfDepth - radius));
    }

    float DistanceXZ(const glm::vec3& a, const glm::vec3& b)
    {
        const glm::vec2 delta(a.x - b.x, a.z - b.z);
        return glm::length(delta);
    }

    glm::vec3 RotateY(const glm::vec3& dir, float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return glm::normalize(glm::vec3(dir.x * c - dir.z * s, 0.0f, dir.x * s + dir.z * c));
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<Brotato3DGameInstance>(config, options, engine);
}

Brotato3DGameInstance::Brotato3DGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine),
    engine_(engine)
{
    config.Title = "Brotato3D";
    config.Width = 1920;
    config.Height = 1080;
    options.Width = 1920;
    options.Height = 1080;
}

void Brotato3DGameInstance::OnInit()
{
    if (!Brotato3D::LoadEnemies(EnemiesConfigPath, enemyDefs_) ||
        !Brotato3D::LoadWeapons(WeaponsConfigPath, weaponDefs_) ||
        !Brotato3D::LoadUpgrades(UpgradesConfigPath, upgradeCards_) ||
        !Brotato3D::LoadWaves(WavesConfigPath, waveDefs_) ||
        !Brotato3D::LoadShopItems(ShopItemsConfigPath, shopItems_))
    {
        throw std::runtime_error("Brotato3D failed to load required data");
    }

    shop_.SetItems(shopItems_);
    waveSystem_.LoadWaves(waveDefs_);
    ResetRuntimeState();
    engine_->SetGraphicsDebugPanelVisible(false);
    engine_->GetUserSettings().ShowOverlay = false;
    engine_->RequestLoadScene("Empty.proc");
}

void Brotato3DGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
}

void Brotato3DGameInstance::OnTick(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    screenShakeMs_ = std::max(0.0f, screenShakeMs_ - deltaMs);
    damageFlashMs_ = std::max(0.0f, damageFlashMs_ - deltaMs);

    if (appState_ == Brotato3D::EAppState::Playing)
    {
        runElapsedSec_ += static_cast<float>(deltaSeconds);
        UpdatePlayer(deltaSeconds);
        UpdateWeapons(deltaSeconds);
        UpdateProjectiles(deltaSeconds);
        UpdateEnemies(deltaSeconds);
        UpdatePickups(deltaSeconds);
        UpdateImpactDebris(deltaSeconds);

        waveSystem_.Update(deltaSeconds, [this](const std::string& enemyId, glm::vec3 pos)
        {
            SpawnEnemy(enemyId, pos);
        });
        if (waveSystem_.ConsumeWaveEnded())
        {
            ClearAliveEnemies(false);
        }
        if (waveSystem_.ConsumeIntermissionStarted())
        {
            StartShopping();
        }
        if (waveSystem_.ConsumeVictory())
        {
            EnterResult(false);
        }
    }
    else if (appState_ == Brotato3D::EAppState::Hitstop)
    {
        hitStopMs_ -= deltaMs;
        if (hitStopMs_ <= 0.0f)
        {
            hitStopMs_ = 0.0f;
            appState_ = Brotato3D::EAppState::Playing;
        }
    }

    UpdateFloatingTexts(deltaSeconds);
    engine_->GetScene().MarkTransformDirty();
}

void Brotato3DGameInstance::OnDestroy()
{
    enemies_.clear();
    projectilePool_.clear();
    impactDebrisPool_.clear();
    pickupPool_.clear();
    floatingTexts_.clear();
}

bool Brotato3DGameInstance::OnRenderUI()
{
    ImGui::Begin("Brotato3D");
    ImGui::Text("Brotato3D MVP - bootstrap OK");
    ImGui::End();

    Brotato3D::RenderHUD(*this);
    if (appState_ == Brotato3D::EAppState::LevelUpPicking)
    {
        Brotato3D::RenderUpgradeModal(*this);
    }
    else if (appState_ == Brotato3D::EAppState::Shopping)
    {
        Brotato3D::RenderShopModal(*this);
    }
    else if (appState_ == Brotato3D::EAppState::Result)
    {
        Brotato3D::RenderResultModal(*this);
    }
    return false;
}

bool Brotato3DGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }

    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
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
#if !defined(NDEBUG)
        if (pressed)
        {
            SpawnEnemy("rat", RandomDebugSpawnPosition());
        }
#endif
        return true;
    default:
        break;
    }
    return false;
}

void Brotato3DGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& lights,
                                               std::vector<Assets::AnimationTrack>& tracks)
{
    (void)lights;
    (void)tracks;
    sceneReady_ = false;
    Brotato3D::BuildArena(models, materials, nodes, arenaResources_);

    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), player_.radius));
    const uint32_t playerModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t playerMaterialId = AddLambertMaterial(materials, glm::vec3(0.20f, 0.75f, 0.30f));
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

    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.04f), glm::vec3(0.04f)));
    impactDebrisModelId_ = static_cast<uint32_t>(models.size() - 1);
    impactDebrisMaterialId_ = AddLambertMaterial(materials, glm::vec3(1.0f, 0.18f, 0.08f));
    impactDebrisPool_.clear();
    impactDebrisPool_.reserve(60);
    for (int index = 0; index < 60; ++index)
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
    if (appState_ == Brotato3D::EAppState::Playing && waveSystem_.GetState() == Brotato3D::EWaveState::Idle)
    {
        waveSystem_.StartGame();
    }
}

bool Brotato3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    glm::vec3 cameraPosition(0.0f, 18.0f, 11.0f);
    glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);
    if (screenShakeMs_ > 0.0f)
    {
        const float strength = std::min(0.13f, screenShakeMs_ / 950.0f);
        const float t = runElapsedSec_ * 37.0f;
        const glm::vec3 jitter(std::sin(t), 0.0f, std::cos(t * 1.37f));
        cameraPosition += jitter * strength;
        cameraTarget += jitter * (strength * 0.5f);
    }
    outRenderCamera.ModelView = glm::lookAtRH(cameraPosition, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    outRenderCamera.FieldOfView = 50.0f;
    return true;
}

int Brotato3DGameInstance::GetXpToNextLevel() const
{
    return 5 + player_.level * 4;
}

bool Brotato3DGameInstance::WorldToScreen(const glm::vec3& world, ImVec2& outScreen) const
{
    const auto& ubo = engine_->GetUniformBufferObject();
    glm::vec4 clip = ubo.ViewProjection * glm::vec4(world, 1.0f);
    if (clip.w <= 0.001f)
    {
        return false;
    }
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < 0.0f || ndc.z > 1.0f)
    {
        return false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    outScreen.x = viewport->Pos.x + (ndc.x * 0.5f + 0.5f) * viewport->Size.x;
    outScreen.y = viewport->Pos.y + (ndc.y * 0.5f + 0.5f) * viewport->Size.y;
    return true;
}

void Brotato3DGameInstance::SelectUpgrade(size_t choiceIndex)
{
    if (choiceIndex >= currentUpgradeChoices_.size())
    {
        return;
    }
    ApplyUpgrade(currentUpgradeChoices_[choiceIndex].stat, currentUpgradeChoices_[choiceIndex].delta);
    --player_.pendingLevelUps;
    if (player_.pendingLevelUps > 0)
    {
        RollUpgradeChoices();
        appState_ = Brotato3D::EAppState::LevelUpPicking;
        ClearMovementInput();
    }
    else
    {
        currentUpgradeChoices_.clear();
        appState_ = Brotato3D::EAppState::Playing;
        ClearMovementInput();
    }
}

void Brotato3DGameInstance::BuyShopItem(size_t slotIndex)
{
    if (slotIndex >= shopOffers_.size())
    {
        return;
    }
    const Brotato3D::FShopItemDef item = shopOffers_[slotIndex];
    if (player_.materials < item.cost)
    {
        return;
    }
    player_.materials -= item.cost;
    ApplyShopItem(item);
    shopOffers_.erase(shopOffers_.begin() + static_cast<std::ptrdiff_t>(slotIndex));
}

void Brotato3DGameInstance::RerollShop()
{
    if (shop_.Reroll(player_.materials, shopOffers_))
    {
        spdlog::info("[Brotato3D] Shop rerolled, materials={}", player_.materials);
    }
}

void Brotato3DGameInstance::ContinueFromShop()
{
    appState_ = Brotato3D::EAppState::Playing;
    ClearMovementInput();
    waveSystem_.EndIntermissionAndAdvance();
    if (waveSystem_.ConsumeVictory())
    {
        EnterResult(false);
    }
}

void Brotato3DGameInstance::RestartGame()
{
    ResetRuntimeState();
    if (player_.bodyNode)
    {
        SetNodeTranslation(player_.bodyNode, player_.worldPos);
    }
    if (player_.facingNode)
    {
        SetNodeTranslation(player_.facingNode, glm::vec3(0.0f, 0.62f, -0.45f));
    }
    if (player_.smgWeaponNode)
    {
        SetNodeTranslation(player_.smgWeaponNode, glm::vec3(-0.18f, 0.42f, -0.54f));
    }
    if (player_.shotgunWeaponNode)
    {
        SetNodeTranslation(player_.shotgunWeaponNode, glm::vec3(0.24f, 0.43f, -0.62f));
    }
}

void Brotato3DGameInstance::ExitGame()
{
    engine_->RequestClose();
}

void Brotato3DGameInstance::SpawnEnemy(const std::string& enemyId, const glm::vec3& worldPos)
{
    const auto defIt = enemyDefs_.find(enemyId);
    const auto visualIt = enemyVisuals_.find(enemyId);
    if (defIt == enemyDefs_.end() || visualIt == enemyVisuals_.end())
    {
        spdlog::warn("[Brotato3D] Unknown enemy id '{}'", enemyId);
        return;
    }

    const Brotato3D::FEnemyDef& def = defIt->second;
    const FEnemyVisualResource& visual = visualIt->second;
    const glm::vec3 spawnPos(worldPos.x, def.size.y * 0.5f, worldPos.z);
    auto reusableEnemy = std::find_if(enemies_.begin(), enemies_.end(),
                                      [&def](const Brotato3D::FEnemyRuntime& enemy)
                                      {
                                          return !enemy.alive && !enemy.fading && enemy.def == &def && enemy.node;
                                      });

    Brotato3D::FEnemyRuntime enemy{};
    enemy.def = &def;
    enemy.worldPos = spawnPos;
    enemy.radius = std::max(def.size.x, def.size.z) * 0.5f;
    enemy.currentHp = def.hp;
    enemy.maxHp = def.hp;
    enemy.alive = true;
    enemy.modelId = visual.modelId;
    enemy.materialId = visual.materialId;
    enemy.darkMaterialId = visual.darkMaterialId;
    enemy.hitFlashMaterialId = visual.hitFlashMaterialId;
    if (reusableEnemy != enemies_.end())
    {
        enemy.node = reusableEnemy->node;
        *reusableEnemy = enemy;
        SetNodeMaterial(reusableEnemy->node, reusableEnemy->materialId);
        SetNodeTranslation(reusableEnemy->node, reusableEnemy->worldPos);
        ShowNode(reusableEnemy->node);
        return;
    }

    enemy.node = CreateRenderNode(fmt::format("Brotato3D_Enemy_{}_{}", enemyId, enemies_.size()), spawnPos, glm::vec3(1.0f),
                                  engine_->GetScene().GenerateInstanceId(), visual.modelId, visual.materialId);
    engine_->GetScene().AddNode(enemy.node);
    engine_->GetScene().MarkDirty();
    enemies_.push_back(enemy);
}

void Brotato3DGameInstance::SpawnPickup(int value, Brotato3D::EPickupKind kind, const glm::vec3& worldPos)
{
    if (value <= 0 || pickupPool_.empty())
    {
        return;
    }

    Brotato3D::FPickupRuntime* slot = nullptr;
    for (auto& pickup : pickupPool_)
    {
        if (!pickup.active && pickup.kind == kind)
        {
            slot = &pickup;
            break;
        }
    }
    if (!slot)
    {
        slot = &*std::max_element(pickupPool_.begin(), pickupPool_.end(),
                                  [this](const Brotato3D::FPickupRuntime& lhs,
                                         const Brotato3D::FPickupRuntime& rhs)
                                  {
                                      return DistanceXZ(lhs.worldPos, player_.worldPos) <
                                             DistanceXZ(rhs.worldPos, player_.worldPos);
                                  });
        HideNode(slot->node);
    }

    slot->kind = kind;
    slot->worldPos = glm::vec3(worldPos.x, 0.15f, worldPos.z);
    slot->value = value;
    slot->active = true;
    slot->magnetized = false;
    if (slot->node)
    {
        const uint32_t modelId = kind == Brotato3D::EPickupKind::XP ? pickupXpModelId_ : pickupMaterialModelId_;
        const uint32_t materialId =
            kind == Brotato3D::EPickupKind::XP ? pickupXpMaterialId_ : pickupMaterialMaterialId_;
        if (auto render = slot->node->GetComponent<Runtime::RenderComponent>())
        {
            render->SetModelId(modelId);
            render->SetMaterial({materialId});
        }
        SetNodeTranslation(slot->node, slot->worldPos);
        ShowNode(slot->node);
    }
}

void Brotato3DGameInstance::UpdatePlayer(double deltaSeconds)
{
    glm::vec3 inputDir(static_cast<float>(keyD_) - static_cast<float>(keyA_), 0.0f,
                       static_cast<float>(keyS_) - static_cast<float>(keyW_));
    if (glm::length(inputDir) > 0.001f)
    {
        inputDir = glm::normalize(inputDir);
        if (equippedWeapons_.empty())
        {
            player_.facingDir = inputDir;
        }
    }

    const float speed = PlayerBaseSpeed * (1.0f + player_.stats.moveSpeedPct);
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
    if (player_.smgWeaponNode)
    {
        const glm::vec3 smgOffset = forward * 0.54f - right * 0.18f;
        SetNodeTranslation(player_.smgWeaponNode, glm::vec3(smgOffset.x, 0.42f, smgOffset.z));
        SetNodeRotation(player_.smgWeaponNode, weaponRotation);
    }
    if (player_.shotgunWeaponNode)
    {
        const glm::vec3 shotgunOffset = forward * 0.62f + right * 0.24f;
        SetNodeTranslation(player_.shotgunWeaponNode, glm::vec3(shotgunOffset.x, 0.43f, shotgunOffset.z));
        SetNodeRotation(player_.shotgunWeaponNode, weaponRotation);
    }
}

void Brotato3DGameInstance::UpdateEnemies(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& enemy : enemies_)
    {
        if (!enemy.alive)
        {
            if (enemy.fading)
            {
                enemy.deathFadeMs -= deltaMs;
                const float alpha = std::clamp(enemy.deathFadeMs / 500.0f, 0.0f, 1.0f);
                SetNodeTranslation(enemy.node, enemy.worldPos + glm::vec3(0.0f, -0.3f * (1.0f - alpha), 0.0f));
                if (enemy.deathFadeMs <= 0.0f)
                {
                    enemy.fading = false;
                    HideNode(enemy.node);
                }
            }
            continue;
        }

        enemy.contactCooldownMs = std::max(0.0f, enemy.contactCooldownMs - deltaMs);
        enemy.hitFlashRemainingMs = std::max(0.0f, enemy.hitFlashRemainingMs - deltaMs);
        SetNodeMaterial(enemy.node, enemy.hitFlashRemainingMs > 0.0f ? enemy.hitFlashMaterialId : enemy.materialId);

        glm::vec3 toPlayer = player_.worldPos - enemy.worldPos;
        toPlayer.y = 0.0f;
        if (glm::length(toPlayer) > 0.001f)
        {
            enemy.worldPos += glm::normalize(toPlayer) * enemy.def->moveSpeed * static_cast<float>(deltaSeconds);
            enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius);
            enemy.worldPos.y = enemy.def->size.y * 0.5f;
            SetNodeTranslation(enemy.node, enemy.worldPos);
        }

        if (DistanceXZ(enemy.worldPos, player_.worldPos) < enemy.radius + player_.radius && enemy.contactCooldownMs <= 0.0f)
        {
            player_.currentHp -= enemy.def->contactDamage;
            enemy.contactCooldownMs = 600.0f;
            screenShakeMs_ = 150.0f;
            damageFlashMs_ = 180.0f;
            if (enemy.def->name == "Brute")
            {
                hitStopMs_ = 80.0f;
                appState_ = Brotato3D::EAppState::Hitstop;
            }
            PushFloatingText(player_.worldPos + glm::vec3(0.0f, 1.0f, 0.0f), fmt::format("-{}", enemy.def->contactDamage),
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
                return;
            }
        }
    }
}

void Brotato3DGameInstance::UpdateWeapons(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& weapon : equippedWeapons_)
    {
        if (!weapon.def)
        {
            continue;
        }
        weapon.cooldownMs -= deltaMs;
        if (weapon.cooldownMs > 0.0f)
        {
            continue;
        }

        Brotato3D::FEnemyRuntime* target = nullptr;
        float bestDistance = FLT_MAX;
        const float range = weapon.def->rangeMeters * (1.0f + player_.stats.rangePct);
        for (auto& enemy : enemies_)
        {
            if (!enemy.alive)
            {
                continue;
            }
            const float distance = DistanceXZ(enemy.worldPos, player_.worldPos);
            if (distance <= range && distance < bestDistance)
            {
                bestDistance = distance;
                target = &enemy;
            }
        }
        if (!target)
        {
            continue;
        }

        glm::vec3 dir = target->worldPos - player_.worldPos;
        dir.y = 0.0f;
        dir = glm::normalize(dir);
        player_.facingDir = dir;

        std::uniform_real_distribution<float> spreadDist(-weapon.def->spreadDeg * 0.5f, weapon.def->spreadDeg * 0.5f);
        const int pelletCount = std::max(1, weapon.def->pellets);
        for (int pelletIndex = 0; pelletIndex < pelletCount; ++pelletIndex)
        {
            const float spreadRadians = glm::radians(spreadDist(rng_));
            const glm::vec3 projectileDir = RotateY(dir, spreadRadians);
            for (auto& projectile : projectilePool_)
            {
                if (projectile.active || projectile.weaponId != weapon.weaponId)
                {
                    continue;
                }
                projectile.active = true;
                projectile.worldPos = player_.worldPos + projectileDir * 0.5f;
                projectile.worldPos.y = 0.5f;
                projectile.velocity = projectileDir * weapon.def->projectileSpeed;
                projectile.remainingLifetimeMs = weapon.def->projectileLifetimeMs;
                projectile.damage = static_cast<int>(
                    std::round(weapon.def->damage * (1.0f + player_.stats.damagePct) + player_.stats.damageFlat));
                projectile.radius = weapon.def->projectileSize;
                SetNodeTranslation(projectile.node, projectile.worldPos);
                ShowNode(projectile.node);
                break;
            }
        }

        weapon.cooldownMs = 1000.0f / std::max(0.01f, weapon.def->atkSpeedHz * (1.0f + player_.stats.atkSpeedPct));
    }
}

void Brotato3DGameInstance::UpdateProjectiles(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& projectile : projectilePool_)
    {
        if (!projectile.active)
        {
            continue;
        }

        projectile.worldPos += projectile.velocity * static_cast<float>(deltaSeconds);
        projectile.remainingLifetimeMs -= deltaMs;
        bool deactivate = projectile.remainingLifetimeMs <= 0.0f || std::abs(projectile.worldPos.x) > ArenaHalfWidth + 1.0f ||
                          std::abs(projectile.worldPos.z) > ArenaHalfDepth + 1.0f;

        if (!deactivate)
        {
            for (auto& enemy : enemies_)
            {
                if (!enemy.alive)
                {
                    continue;
                }
                if (DistanceXZ(projectile.worldPos, enemy.worldPos) < enemy.radius + projectile.radius)
                {
                    enemy.currentHp -= projectile.damage;
                    enemy.hitFlashRemainingMs = 80.0f;
                    SetNodeMaterial(enemy.node, enemy.hitFlashMaterialId);
                    SpawnImpactDebris(projectile.worldPos);
                    PushFloatingText(enemy.worldPos + glm::vec3(0.0f, 0.8f, 0.0f), fmt::format("-{}", projectile.damage),
                                     glm::vec4(1.0f, 0.25f, 0.18f, 1.0f), 600.0f);
                    deactivate = true;
                    if (enemy.currentHp <= 0)
                    {
                        KillEnemy(enemy, true);
                    }
                    break;
                }
            }
        }

        if (deactivate)
        {
            projectile.active = false;
            projectile.worldPos = HiddenPosition;
            SetNodeTranslation(projectile.node, HiddenPosition);
            HideNode(projectile.node);
        }
        else
        {
            SetNodeTranslation(projectile.node, projectile.worldPos);
        }
    }
}

void Brotato3DGameInstance::UpdatePickups(double deltaSeconds)
{
    for (auto& pickup : pickupPool_)
    {
        if (!pickup.active)
        {
            continue;
        }
        const float pickupRadius = PickupBaseRadius * (1.0f + player_.stats.pickupRadiusPct);
        if (DistanceXZ(pickup.worldPos, player_.worldPos) < pickupRadius)
        {
            pickup.magnetized = true;
        }
        if (pickup.magnetized)
        {
            pickup.worldPos = glm::mix(pickup.worldPos, player_.worldPos, std::min(1.0f, 8.0f * static_cast<float>(deltaSeconds)));
            pickup.worldPos.y = 0.15f;
            SetNodeTranslation(pickup.node, pickup.worldPos);
        }
        if (DistanceXZ(pickup.worldPos, player_.worldPos) < 0.4f)
        {
            if (pickup.kind == Brotato3D::EPickupKind::XP)
            {
                player_.currentXp += pickup.value;
                PushFloatingText(player_.worldPos, fmt::format("+{} XP", pickup.value), glm::vec4(0.2f, 1.0f, 0.35f, 1.0f),
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
            else
            {
                player_.materials += pickup.value;
                totalMaterialsGained_ += pickup.value;
                PushFloatingText(player_.worldPos, fmt::format("+{} MAT", pickup.value), glm::vec4(1.0f, 0.85f, 0.15f, 1.0f),
                                 500.0f);
                spdlog::info("[Brotato3D] Materials {}", player_.materials);
            }
            pickup.active = false;
            pickup.magnetized = false;
            HideNode(pickup.node);
        }
    }
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

        debris.velocity.y -= 7.5f * static_cast<float>(deltaSeconds);
        debris.worldPos += debris.velocity * static_cast<float>(deltaSeconds);
        SetNodeTranslation(debris.node, debris.worldPos);
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

void Brotato3DGameInstance::SpawnImpactDebris(const glm::vec3& worldPos)
{
    std::uniform_real_distribution<float> horizontalDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> speedDist(1.2f, 3.0f);
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

void Brotato3DGameInstance::KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot)
{
    if (!enemy.alive)
    {
        return;
    }
    enemy.alive = false;
    enemy.fading = true;
    enemy.deathFadeMs = dropLoot ? 500.0f : 400.0f;
    SetNodeMaterial(enemy.node, enemy.darkMaterialId);
    if (dropLoot)
    {
        ++killCount_;
        SpawnPickup(enemy.def->xpDrop, Brotato3D::EPickupKind::XP, enemy.worldPos);
        SpawnPickup(enemy.def->materialDrop, Brotato3D::EPickupKind::Material, enemy.worldPos);
    }
}

void Brotato3DGameInstance::ClearAliveEnemies(bool dropLoot)
{
    for (auto& enemy : enemies_)
    {
        if (enemy.alive)
        {
            KillEnemy(enemy, dropLoot);
        }
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

void Brotato3DGameInstance::PushFloatingText(const glm::vec3& worldPos, std::string text, const glm::vec4& color, float lifeMs)
{
    floatingTexts_.push_back({worldPos, std::move(text), color, lifeMs, lifeMs});
}

void Brotato3DGameInstance::BeginLevelUp()
{
    appState_ = Brotato3D::EAppState::LevelUpPicking;
    ClearMovementInput();
    RollUpgradeChoices();
}

void Brotato3DGameInstance::RollUpgradeChoices()
{
    currentUpgradeChoices_.clear();
    std::vector<Brotato3D::FUpgradeCardDef> remaining = upgradeCards_;
    while (!remaining.empty() && currentUpgradeChoices_.size() < 3)
    {
        int totalWeight = 0;
        for (const auto& card : remaining)
        {
            totalWeight += std::max(1, card.weight);
        }
        std::uniform_int_distribution<int> dist(1, totalWeight);
        int pick = dist(rng_);
        size_t chosenIndex = 0;
        for (size_t index = 0; index < remaining.size(); ++index)
        {
            pick -= std::max(1, remaining[index].weight);
            if (pick <= 0)
            {
                chosenIndex = index;
                break;
            }
        }
        currentUpgradeChoices_.push_back(remaining[chosenIndex]);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(chosenIndex));
    }
}

void Brotato3DGameInstance::ApplyUpgrade(const std::string& stat, float delta)
{
    Brotato3D::FShopItemDef item{};
    item.stat = stat;
    item.delta = delta;
    ApplyShopItem(item);
}

void Brotato3DGameInstance::ApplyShopItem(const Brotato3D::FShopItemDef& item)
{
    if (item.stat == "damagePct")
    {
        player_.stats.damagePct += item.delta;
    }
    else if (item.stat == "damageFlat")
    {
        player_.stats.damageFlat += item.delta;
    }
    else if (item.stat == "atkSpeedPct")
    {
        player_.stats.atkSpeedPct += item.delta;
    }
    else if (item.stat == "rangePct")
    {
        player_.stats.rangePct += item.delta;
    }
    else if (item.stat == "moveSpeedPct")
    {
        player_.stats.moveSpeedPct += item.delta;
    }
    else if (item.stat == "pickupRadiusPct")
    {
        player_.stats.pickupRadiusPct += item.delta;
    }
    else if (item.stat == "maxHpFlat")
    {
        player_.stats.maxHpFlatBonus += static_cast<int>(std::round(item.delta));
        player_.maxHp = static_cast<int>(player_.stats.maxHpFlat) + player_.stats.maxHpFlatBonus;
        player_.currentHp = std::min(player_.maxHp, player_.currentHp + static_cast<int>(std::round(item.delta)));
    }
    else if (item.stat == "healPct")
    {
        player_.currentHp = std::min(player_.maxHp, player_.currentHp + static_cast<int>(std::round(player_.maxHp * item.delta)));
    }
}

void Brotato3DGameInstance::StartShopping()
{
    shop_.SetWaveIndex(waveSystem_.GetCurrentWaveIndex());
    shop_.Roll(4, shopOffers_);
    appState_ = Brotato3D::EAppState::Shopping;
    ClearMovementInput();
}

void Brotato3DGameInstance::EnterResult(bool playerDead)
{
    playerDead_ = playerDead;
    appState_ = Brotato3D::EAppState::Result;
    ClearMovementInput();
    if (playerDead)
    {
        ClearAliveEnemies(false);
    }
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
        HideNode(enemy.node);
    }
    player_ = Brotato3D::FPlayerRuntime{};
    player_.bodyNode = playerBodyNode;
    player_.facingNode = playerFacingNode;
    player_.smgWeaponNode = playerSmgWeaponNode;
    player_.shotgunWeaponNode = playerShotgunWeaponNode;
    for (auto& projectile : projectilePool_)
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
    currentUpgradeChoices_.clear();
    shopOffers_.clear();
    equippedWeapons_.clear();
    if (auto weaponIt = weaponDefs_.find("smg"); weaponIt != weaponDefs_.end())
    {
        equippedWeapons_.push_back({"smg", &weaponIt->second, 0.0f});
    }
    if (auto weaponIt = weaponDefs_.find("shotgun"); weaponIt != weaponDefs_.end())
    {
        equippedWeapons_.push_back({"shotgun", &weaponIt->second, 0.0f});
    }
    waveSystem_.LoadWaves(waveDefs_);
    if (sceneReady_)
    {
        waveSystem_.StartGame();
    }
    appState_ = Brotato3D::EAppState::Playing;
    screenShakeMs_ = 0.0f;
    damageFlashMs_ = 0.0f;
    hitStopMs_ = 0.0f;
    runElapsedSec_ = 0.0f;
    killCount_ = 0;
    totalMaterialsGained_ = 0;
    playerDead_ = false;
    ClearMovementInput();
}

void Brotato3DGameInstance::ClearMovementInput()
{
    keyW_ = false;
    keyA_ = false;
    keyS_ = false;
    keyD_ = false;
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
