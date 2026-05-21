#include "Engine/Runtime/GameInstance.hpp"
#include "Voyage3DGameInstance.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Runtime/Editor/FontLoader.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/NodeUtils.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Runtime/Subsystems/NextAudio.h"
#include "Voyage3DEvent.hpp"
#include "Voyage3DSailing.hpp"
#include "Voyage3DTrade.hpp"
#include "Voyage3DUI.hpp"
#include "Voyage3DWorldMap.hpp"

#include <filesystem>

namespace
{
    constexpr const char* PortsConfigPath = "assets/configs/voyage3d/ports.json";
    constexpr const char* GoodsConfigPath = "assets/configs/voyage3d/goods.json";
    constexpr const char* ShipsConfigPath = "assets/configs/voyage3d/ships.json";
    constexpr const char* EventsConfigPath = "assets/configs/voyage3d/events.json";
    constexpr const char* LandmassConfigPath = "assets/configs/voyage3d/landmass.json";
    constexpr const char* PortLoreConfigPath = "assets/configs/voyage3d/port_lore.json";
    constexpr const char* EmptySceneName = "Empty.proc";
    constexpr float PortEnterDistance = 4.0f;

    glm::quat YawRotation(float yaw)
    {
        return glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::vec3 ShipForward(float yaw)
    {
        return glm::vec3(std::cos(yaw), 0.0f, std::sin(yaw));
    }

    int RandInt(std::mt19937& rng, int minValue, int maxValue)
    {
        std::uniform_int_distribution<int> dist(minValue, maxValue);
        return dist(rng);
    }

    std::string BgmPathForState(Voyage3D::EAppState state)
    {
        if (state == Voyage3D::EAppState::NavalCombat)
        {
            return "assets/sounds/voyage3d/battle_bgm.ogg";
        }
        if (state == Voyage3D::EAppState::InPort ||
            state == Voyage3D::EAppState::Trading ||
            state == Voyage3D::EAppState::ShipUpgrade)
        {
            return "assets/sounds/voyage3d/port_bgm.ogg";
        }
        return "assets/sounds/voyage3d/sailing_bgm.ogg";
    }

    std::string BgmIdForState(Voyage3D::EAppState state)
    {
        if (state == Voyage3D::EAppState::NavalCombat)
        {
            return "battle";
        }
        if (state == Voyage3D::EAppState::InPort ||
            state == Voyage3D::EAppState::Trading ||
            state == Voyage3D::EAppState::ShipUpgrade)
        {
            return "port";
        }
        return "sailing";
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    return std::make_unique<Voyage3DGameInstance>(config, options, engine);
}

Voyage3DGameInstance::Voyage3DGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "Voyage3D", 1280, 720, true);
}

void Voyage3DGameInstance::OnInit()
{
    LoadGameData();
    ResetRuntimeState();
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetUserSettings().ShowOverlay = false;
    GetEngine().RequestLoadScene({.filename = EmptySceneName});
}

void Voyage3DGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFont* bodyFont = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/DroidSansFallback.ttf",
        .pixelSize = 17.0f,
        .includeChineseFull = true,
        .extraGlyphsUtf8 = Voyage3D::U8Text(u8"✓"),
        .setAsDefault = true,
    });
    titleFont_ = NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/DroidSansFallback.ttf",
        .pixelSize = 34.0f,
        .includeChineseFull = true,
        .extraGlyphsUtf8 = Voyage3D::U8Text(u8"✓"),
    });
    if (!titleFont_)
    {
        titleFont_ = bodyFont;
    }
}

void Voyage3DGameInstance::OnTick(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    input_.stormDebuffMs = stormDebuffMs_;

    if (appState_ == Voyage3D::EAppState::Sailing && !pirateEncounterPending_)
    {
        Voyage3D::UpdatePlayerShip(playerShip_, deltaSeconds, input_, landBlocks_, ports_);
        UpdateShipNodeTransform(playerShip_);
        UpdateNearestPort();

        eventCheckTimerSec_ += static_cast<float>(deltaSeconds);
        if (eventCheckTimerSec_ > 8.0f)
        {
            eventCheckTimerSec_ = 0.0f;
            Voyage3D::RollEvent(*this);
        }
    }
    else if (appState_ == Voyage3D::EAppState::NavalCombat)
    {
        Voyage3D::UpdateCombat(*this, deltaSeconds);
        if (input_.keyShift)
        {
            retreatHoldMs_ += deltaMs;
            if (retreatHoldMs_ >= 3000.0f)
            {
                retreatHoldMs_ = 0.0f;
                if (RandInt(rng_, 0, 99) < 50)
                {
                    PushToast(Voyage3D::U8Text(u8"脱战成功，回到航线。"));
                    for (Voyage3D::FShipRuntime& enemy : enemyShips_)
                    {
                        enemy.active = false;
                        SetRuntimeVisible(enemy, false);
                    }
                    appState_ = Voyage3D::EAppState::Sailing;
                }
                else
                {
                    PushToast(Voyage3D::U8Text(u8"脱战失败，继续战斗！"));
                }
            }
        }
        else
        {
            retreatHoldMs_ = 0.0f;
        }
    }

    if (appState_ == Voyage3D::EAppState::Sailing ||
        appState_ == Voyage3D::EAppState::NavalCombat)
    {
        gameDateAccumSec_ += static_cast<float>(deltaSeconds);
        while (gameDateAccumSec_ >= 2.0f)
        {
            gameDateAccumSec_ -= 2.0f;
            ++gameDayCounter_;
        }
    }

    UpdateRuntimeEffects(deltaSeconds);
    UpdateBgm();
    CheckLossConditions();
    GetEngine().GetScene().MarkTransformDirty();
}

void Voyage3DGameInstance::OnDestroy()
{
    ports_.clear();
    enemyShips_.clear();
    projectiles_.clear();
    floatingTexts_.clear();
    muzzleFlashes_.clear();
    explosionRings_.clear();
}

bool Voyage3DGameInstance::OnRenderUI()
{
    Voyage3D::Render(*this);
    return false;
}

bool Voyage3DGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }

    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key)
    {
    case SDLK_W: input_.keyW = pressed; return true;
    case SDLK_A: input_.keyA = pressed; return true;
    case SDLK_S: input_.keyS = pressed; return true;
    case SDLK_D: input_.keyD = pressed; return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        input_.keyShift = pressed;
        return true;
    default:
        break;
    }

    if (!pressed || event.key.repeat)
    {
        return false;
    }

    switch (event.key.key)
    {
    case SDLK_SPACE:
        if (appState_ == Voyage3D::EAppState::Sailing && nearestPort_)
        {
            EnterPort(*nearestPort_);
            return true;
        }
        break;
    case SDLK_ESCAPE:
        if (appState_ == Voyage3D::EAppState::Trading || appState_ == Voyage3D::EAppState::ShipUpgrade)
        {
            appState_ = Voyage3D::EAppState::InPort;
            return true;
        }
        if (appState_ == Voyage3D::EAppState::InPort)
        {
            LeavePort();
            return true;
        }
        break;
    case SDLK_Q:
        if (appState_ == Voyage3D::EAppState::NavalCombat)
        {
            FireBroadside(playerShip_, true, true);
            return true;
        }
        break;
    case SDLK_E:
        if (appState_ == Voyage3D::EAppState::NavalCombat)
        {
            FireBroadside(playerShip_, false, true);
            return true;
        }
        break;
    case SDLK_F5:
        ForcePirateDebug();
        return true;
    case SDLK_J:
        eventLogOpen_ = !eventLogOpen_;
        return true;
    default:
        break;
    }
    return false;
}

bool Voyage3DGameInstance::OnMouseButton(SDL_Event& event)
{
    (void)event;
    return false;
}

bool Voyage3DGameInstance::OnCursorPosition(double xpos, double ypos)
{
    (void)xpos;
    (void)ypos;
    return false;
}

void Voyage3DGameInstance::OnSceneLoaded()
{
    ApplyLightingSettings();
}

void Voyage3DGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& lights,
                                              std::vector<Assets::AnimationTrack>& tracks)
{
    (void)lights;
    (void)tracks;
    sceneReady_ = false;
    Voyage3D::WorldMap::BuildOcean(models, materials, nodes);
    Voyage3D::WorldMap::BuildLandmass(landBlocks_, models, materials, nodes);
    BuildPorts(nodes, models, materials);
    BuildShipVisual(playerShip_, "Voyage3D_PlayerShip", glm::vec3(0.95f, 0.92f, 0.85f), nodes, models, materials, true);
    enemyShips_.clear();
    enemyShips_.resize(1);
    if (const Voyage3D::FShipDef* sloop = FindShipDef("sloop"))
    {
        enemyShips_.front().def = *sloop;
        enemyShips_.front().currentHp = sloop->hp;
    }
    enemyShips_.front().enemy = true;
    enemyShips_.front().active = false;
    BuildShipVisual(enemyShips_.front(), "Voyage3D_EnemyShip", glm::vec3(0.60f, 0.10f, 0.10f), nodes, models, materials, false);
    BuildProjectilePool(nodes, models, materials);
    sceneReady_ = true;
}

bool Voyage3DGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    glm::vec3 cameraPosition(20.0f, 25.0f, 0.0f);
    glm::vec3 cameraTarget(20.0f, 0.0f, -30.0f);

    if (appState_ == Voyage3D::EAppState::NavalCombat && !enemyShips_.empty() && enemyShips_.front().active)
    {
        const glm::vec3 mid = (playerShip_.worldPos + enemyShips_.front().worldPos) * 0.5f;
        cameraPosition = mid + glm::vec3(0.0f, 8.0f, 10.0f);
        cameraTarget = mid;
    }
    else if (appState_ != Voyage3D::EAppState::MainMenu)
    {
        cameraPosition = playerShip_.worldPos + glm::vec3(0.0f, 18.0f, 14.0f);
        cameraTarget = playerShip_.worldPos;
    }

    if (screenShakeMs_ > 0.0f)
    {
        const float strength = std::min(0.35f, screenShakeIntensity_ * 0.06f);
        const float t = static_cast<float>(GetEngine().GetTime()) * 41.0f;
        const glm::vec3 jitter(std::sin(t), 0.0f, std::cos(t * 1.37f));
        cameraPosition += jitter * strength;
        cameraTarget += jitter * strength * 0.35f;
    }

    outRenderCamera.ModelView = glm::lookAtRH(cameraPosition, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    outRenderCamera.FieldOfView = appState_ == Voyage3D::EAppState::NavalCombat ? 48.0f : 50.0f;
    return true;
}

int Voyage3DGameInstance::GetVisitedPortCount() const
{
    return static_cast<int>(std::count_if(ports_.begin(), ports_.end(), [](const Voyage3D::FPortRuntime& port)
    {
        return port.visited;
    }));
}

std::string Voyage3DGameInstance::FormatGameDate() const
{
    const int year = 1499 + gameDayCounter_ / 365;
    const int month = ((gameDayCounter_ / 30) % 12) + 1;
    return fmt::format(fmt::runtime(Voyage3D::U8Text(u8"{} 年 {} 月")), year, month);
}

const Voyage3D::FGoodsDef* Voyage3DGameInstance::FindGoodsDef(const std::string& goodId) const
{
    const auto it = std::find_if(goodsDefs_.begin(), goodsDefs_.end(), [&goodId](const Voyage3D::FGoodsDef& good)
    {
        return good.id == goodId;
    });
    return it == goodsDefs_.end() ? nullptr : &*it;
}

const Voyage3D::FShipDef* Voyage3DGameInstance::FindShipDef(const std::string& shipId) const
{
    const auto it = std::find_if(shipDefs_.begin(), shipDefs_.end(), [&shipId](const Voyage3D::FShipDef& ship)
    {
        return ship.id == shipId;
    });
    return it == shipDefs_.end() ? nullptr : &*it;
}

Voyage3D::FPortRuntime* Voyage3DGameInstance::FindPort(const std::string& portId)
{
    const auto it = std::find_if(ports_.begin(), ports_.end(), [&portId](const Voyage3D::FPortRuntime& port)
    {
        return port.def.id == portId;
    });
    return it == ports_.end() ? nullptr : &*it;
}

const Voyage3D::FPortRuntime* Voyage3DGameInstance::FindPort(const std::string& portId) const
{
    const auto it = std::find_if(ports_.begin(), ports_.end(), [&portId](const Voyage3D::FPortRuntime& port)
    {
        return port.def.id == portId;
    });
    return it == ports_.end() ? nullptr : &*it;
}

void Voyage3DGameInstance::StartNewGame()
{
    ResetRuntimeState();
    appState_ = Voyage3D::EAppState::Sailing;
    GetEngine().RequestLoadScene({.filename = EmptySceneName});
}

void Voyage3DGameInstance::ReturnToMainMenu()
{
    appState_ = Voyage3D::EAppState::MainMenu;
    pirateEncounterPending_ = false;
}

void Voyage3DGameInstance::EnterPort(Voyage3D::FPortRuntime& port)
{
    currentPort_ = &port;
    nearestPort_ = &port;
    currentPort_->visited = true;
    UpdateVisitedAnchor(*currentPort_);
    Voyage3D::RefreshPortPrices(*currentPort_, goodsDefs_, rng_);
    appState_ = Voyage3D::EAppState::InPort;
    AddEventLog(fmt::format(fmt::runtime(Voyage3D::U8Text(u8"抵达 {}")), currentPort_->def.name));
}

void Voyage3DGameInstance::LeavePort()
{
    currentPort_ = nullptr;
    appState_ = Voyage3D::EAppState::Sailing;
}

void Voyage3DGameInstance::ReturnToPortMenu()
{
    if (currentPort_)
    {
        appState_ = Voyage3D::EAppState::InPort;
    }
}

void Voyage3DGameInstance::OpenTrade()
{
    if (currentPort_)
    {
        appState_ = Voyage3D::EAppState::Trading;
    }
}

void Voyage3DGameInstance::OpenShipUpgrade()
{
    if (currentPort_)
    {
        appState_ = Voyage3D::EAppState::ShipUpgrade;
    }
}

bool Voyage3DGameInstance::TryBuyGood(const std::string& goodId, int qty)
{
    if (!currentPort_)
    {
        return false;
    }
    if (qty <= 0)
    {
        SetTradeMessage(Voyage3D::U8Text(u8"数量无效"));
        return false;
    }
    const int price = currentPort_->currentPrices[goodId] * qty;
    if (playerShip_.cargoUsed + qty > playerShip_.def.cargoMax)
    {
        SetTradeMessage(Voyage3D::U8Text(u8"货舱已满"));
        return false;
    }
    if (gold_ < price)
    {
        SetTradeMessage(Voyage3D::U8Text(u8"金币不足"));
        return false;
    }
    if (!Voyage3D::BuyGood(*currentPort_, playerShip_, gold_, goodId, qty))
    {
        SetTradeMessage(Voyage3D::U8Text(u8"交易失败"));
        return false;
    }
    SetTradeMessage(Voyage3D::U8Text(u8"买入完成"));
    return true;
}

bool Voyage3DGameInstance::TrySellGood(const std::string& goodId, int qty)
{
    if (!currentPort_)
    {
        return false;
    }
    if (qty <= 0)
    {
        SetTradeMessage(Voyage3D::U8Text(u8"数量无效"));
        return false;
    }
    const auto cargoIt = playerShip_.cargo.find(goodId);
    if (cargoIt == playerShip_.cargo.end() || cargoIt->second < qty)
    {
        SetTradeMessage(Voyage3D::U8Text(u8"货物不足"));
        return false;
    }
    if (!Voyage3D::SellGood(*currentPort_, playerShip_, gold_, goodId, qty))
    {
        SetTradeMessage(Voyage3D::U8Text(u8"交易失败"));
        return false;
    }
    SetTradeMessage(Voyage3D::U8Text(u8"卖出完成"));
    return true;
}

bool Voyage3DGameInstance::TryBuyShip(const std::string& shipId)
{
    const Voyage3D::FShipDef* shipDef = FindShipDef(shipId);
    if (!shipDef || shipDef->id == playerShip_.def.id || gold_ < shipDef->price)
    {
        return false;
    }

    gold_ -= shipDef->price;
    playerShip_.def = *shipDef;
    playerShip_.currentHp = shipDef->hp;
    DropOverflowCargo();
    RebuildPlayerShipVisual();
    UpdateShipNodeTransform(playerShip_);
    PushToast(fmt::format(fmt::runtime(Voyage3D::U8Text(u8"购入 {}")), shipDef->name));
    return true;
}

void Voyage3DGameInstance::BeginPirateEncounter(const std::string& enemyShipId)
{
    if (appState_ != Voyage3D::EAppState::Sailing || pirateEncounterPending_)
    {
        return;
    }
    pendingPirateShipId_ = enemyShipId.empty() ? "sloop" : enemyShipId;
    pirateEncounterPending_ = true;
    PushToast(Voyage3D::U8Text(u8"海盗来袭！"));
    AddEventLog(Voyage3D::U8Text(u8"海盗来袭"));
}

void Voyage3DGameInstance::StartPirateCombat()
{
    pirateEncounterPending_ = false;
    appState_ = Voyage3D::EAppState::NavalCombat;
    SpawnEnemyShip(pendingPirateShipId_, playerShip_.worldPos + glm::vec3(0.0f, 0.0f, 12.0f));
}

void Voyage3DGameInstance::TryFleePirates()
{
    pirateEncounterPending_ = false;
    if (RandInt(rng_, 0, 99) < 75)
    {
        PushToast(Voyage3D::U8Text(u8"成功甩开海盗。"));
        AddEventLog(Voyage3D::U8Text(u8"逃离海盗"));
        return;
    }
    PushToast(Voyage3D::U8Text(u8"逃跑失败，被迫开战！"));
    StartPirateCombat();
}

void Voyage3DGameInstance::ForcePirateDebug()
{
    if (appState_ == Voyage3D::EAppState::Sailing)
    {
        BeginPirateEncounter("sloop");
    }
}

void Voyage3DGameInstance::EndCombatVictory()
{
    for (Voyage3D::FShipRuntime& enemy : enemyShips_)
    {
        enemy.active = false;
        SetRuntimeVisible(enemy, false);
    }
    for (Voyage3D::FProjectileRuntime& projectile : projectiles_)
    {
        projectile.active = false;
        if (projectile.node)
        {
            projectile.node->SetTranslation(Voyage3D::HiddenPosition);
            Assets::NodeUtils::SetVisible(projectile.node, false);
        }
    }

    const int reward = RandInt(rng_, 150, 400);
    gold_ += reward;
    ++combatWins_;
    if (!goodsDefs_.empty() && RandInt(rng_, 0, 99) < 50)
    {
        const Voyage3D::FGoodsDef& good = goodsDefs_[static_cast<size_t>(RandInt(rng_, 0, static_cast<int>(goodsDefs_.size() - 1)))];
        const int qty = RandInt(rng_, 2, 5);
        const int added = AddCargo(good.id, qty);
        PushToast(fmt::format(fmt::runtime(Voyage3D::U8Text(u8"击沉海盗，获得 {} 金币 + {} {}")), reward, added, good.name));
    }
    else
    {
        PushToast(fmt::format(fmt::runtime(Voyage3D::U8Text(u8"击沉海盗，获得 {} 金币")), reward));
    }
    AddEventLog(Voyage3D::U8Text(u8"海战胜利"));
    appState_ = Voyage3D::EAppState::Sailing;
}

void Voyage3DGameInstance::EndCombatDefeat()
{
    resultReason_ = "defeat";
    appState_ = Voyage3D::EAppState::Result;
    PushToast(Voyage3D::U8Text(u8"全军覆没"));
}

int Voyage3DGameInstance::AddCargo(const std::string& goodId, int qty)
{
    const int room = std::max(0, playerShip_.def.cargoMax - playerShip_.cargoUsed);
    const int added = std::clamp(qty, 0, room);
    if (added > 0)
    {
        playerShip_.cargo[goodId] += added;
        playerShip_.cargoUsed += added;
    }
    return added;
}

void Voyage3DGameInstance::AddGold(int amount)
{
    gold_ += amount;
}

void Voyage3DGameInstance::DamagePlayer(int damage)
{
    playerShip_.currentHp = std::max(0, playerShip_.currentHp - damage);
    PushFloatingText(playerShip_.worldPos + glm::vec3(0.0f, 1.8f, 0.0f),
                     fmt::format("-{}", damage),
                     glm::vec4(1.0f, 0.25f, 0.15f, 1.0f),
                     900.0f,
                     1.1f);
    StartScreenShake(180.0f, 1.8f);
}

void Voyage3DGameInstance::ApplyStormDebuff(float durationMs)
{
    stormDebuffMs_ = std::max(stormDebuffMs_, durationMs);
}

void Voyage3DGameInstance::GrantIntelDiscount(const std::string& portId, const std::string& goodId, float factor)
{
    if (Voyage3D::FPortRuntime* port = FindPort(portId))
    {
        port->nextVisitDiscountFactor[goodId] = factor;
    }
}

void Voyage3DGameInstance::PushToast(std::string text, float durationMs)
{
    toastText_ = std::move(text);
    toastMs_ = durationMs;
}

void Voyage3DGameInstance::AddEventLog(std::string text)
{
    eventLog_.push_back(std::move(text));
    if (eventLog_.size() > 10)
    {
        eventLog_.erase(eventLog_.begin());
    }
}

void Voyage3DGameInstance::PushFloatingText(const glm::vec3& worldPos,
                                            std::string text,
                                            const glm::vec4& color,
                                            float lifeMs,
                                            float fontScale)
{
    floatingTexts_.push_back({worldPos, std::move(text), color, lifeMs, lifeMs, fontScale});
}

void Voyage3DGameInstance::PushMuzzleFlash(const glm::vec3& worldPos, const glm::vec3& color)
{
    muzzleFlashes_.push_back({worldPos, color, 90.0f, 90.0f});
}

void Voyage3DGameInstance::PushExplosionRing(const glm::vec3& worldPos, const glm::vec4& color, float maxRadius)
{
    explosionRings_.push_back({worldPos, color, 450.0f, 450.0f, maxRadius});
}

void Voyage3DGameInstance::StartScreenShake(float durationMs, float intensity)
{
    screenShakeMs_ = std::max(screenShakeMs_, durationMs);
    screenShakeIntensity_ = std::max(screenShakeIntensity_, intensity);
}

void Voyage3DGameInstance::UpdateShipNodeTransform(Voyage3D::FShipRuntime& ship)
{
    if (!ship.rootNode)
    {
        return;
    }
    ship.rootNode->SetTranslation(ship.worldPos);
    ship.rootNode->SetRotation(YawRotation(ship.yaw));
}

void Voyage3DGameInstance::SpawnEnemyShip(const std::string& shipId, const glm::vec3& spawnPos)
{
    if (enemyShips_.empty())
    {
        return;
    }
    Voyage3D::FShipRuntime& enemy = enemyShips_.front();
    if (const Voyage3D::FShipDef* shipDef = FindShipDef(shipId))
    {
        enemy.def = *shipDef;
    }
    enemy.currentHp = enemy.def.hp;
    enemy.worldPos = glm::vec3(std::clamp(spawnPos.x, -30.0f, 85.0f), 0.0f, std::clamp(spawnPos.z, -90.0f, 20.0f));
    enemy.previousWorldPos = enemy.worldPos;
    enemy.yaw = std::uniform_real_distribution<float>(-glm::pi<float>(), glm::pi<float>())(rng_);
    enemy.currentSpeed = 0.0f;
    enemy.leftBroadsideCooldownMs = 1000.0f;
    enemy.rightBroadsideCooldownMs = 1000.0f;
    enemy.aiFireCooldownMs = 1200.0f;
    enemy.active = true;
    RebuildPlayerShipVisual();
    SetRuntimeVisible(enemy, true);
    UpdateShipNodeTransform(enemy);
}

void Voyage3DGameInstance::FireBroadside(Voyage3D::FShipRuntime& ship, bool leftSide, bool fromPlayer)
{
    float& cooldown = leftSide ? ship.leftBroadsideCooldownMs : ship.rightBroadsideCooldownMs;
    if (cooldown > 0.0f || !ship.active)
    {
        return;
    }

    const float sideAngle = ship.yaw + (leftSide ? glm::half_pi<float>() : -glm::half_pi<float>());
    const glm::vec3 sideDir(std::cos(sideAngle), 0.0f, std::sin(sideAngle));
    const glm::vec3 forward = ShipForward(ship.yaw);
    const int cannonCount = std::max(1, ship.def.cannonCount / 2);
    for (int index = 0; index < cannonCount; ++index)
    {
        const float lineT = cannonCount == 1 ? 0.0f : (static_cast<float>(index) / static_cast<float>(cannonCount - 1) - 0.5f);
        const glm::vec3 spawn = ship.worldPos + forward * lineT * ship.def.size.x * 0.75f + sideDir * ship.def.size.z * 0.65f + glm::vec3(0.0f, 0.45f, 0.0f);
        SpawnProjectile(spawn, sideDir * 18.0f, fromPlayer, 8);
        PushMuzzleFlash(spawn, fromPlayer ? glm::vec3(1.0f, 0.78f, 0.25f) : glm::vec3(1.0f, 0.25f, 0.12f));
    }
    cooldown = 1500.0f;
    StartScreenShake(fromPlayer ? 90.0f : 120.0f, fromPlayer ? 0.8f : 1.2f);
}

void Voyage3DGameInstance::SpawnProjectile(const glm::vec3& worldPos, const glm::vec3& velocity, bool fromPlayer, int damage)
{
    auto projectileIt = std::find_if(projectiles_.begin(), projectiles_.end(), [](const Voyage3D::FProjectileRuntime& projectile)
    {
        return !projectile.active;
    });
    if (projectileIt == projectiles_.end())
    {
        projectileIt = projectiles_.begin();
    }
    if (projectileIt == projectiles_.end())
    {
        return;
    }

    projectileIt->active = true;
    projectileIt->worldPos = worldPos;
    projectileIt->velocity = velocity;
    projectileIt->fromPlayer = fromPlayer;
    projectileIt->damage = damage;
    projectileIt->lifetimeMs = 800.0f;
    if (projectileIt->node)
    {
        projectileIt->node->SetTranslation(worldPos);
        Assets::NodeUtils::SetVisible(projectileIt->node, true);
    }
}

void Voyage3DGameInstance::ResetRuntimeState()
{
    ports_.clear();
    ports_.reserve(portDefs_.size());
    for (const Voyage3D::FPortDef& portDef : portDefs_)
    {
        Voyage3D::FPortRuntime port;
        port.def = portDef;
        port.worldPos = Voyage3D::WorldMap::GeoToWorld(portDef.lon, portDef.lat);
        ports_.push_back(port);
    }

    const Voyage3D::FShipDef* sloop = FindShipDef("sloop");
    playerShip_ = {};
    if (sloop)
    {
        playerShip_.def = *sloop;
        playerShip_.currentHp = sloop->hp;
    }
    const Voyage3D::FPortRuntime* venice = FindPort("venice");
    playerShip_.worldPos = venice ? venice->worldPos + glm::vec3(0.0f, 0.0f, 5.0f) : glm::vec3(20.0f, 0.0f, 0.0f);
    playerShip_.worldPos.z = std::clamp(playerShip_.worldPos.z, -90.0f, 20.0f);
    playerShip_.previousWorldPos = playerShip_.worldPos;
    playerShip_.yaw = -glm::half_pi<float>();
    playerShip_.active = true;
    playerShip_.enemy = false;

    enemyShips_.clear();
    projectiles_.clear();
    nearestPort_ = nullptr;
    currentPort_ = nullptr;
    floatingTexts_.clear();
    muzzleFlashes_.clear();
    explosionRings_.clear();
    eventLog_.clear();
    pendingPirateShipId_ = "sloop";
    toastText_.clear();
    tradeMessage_.clear();
    gold_ = 1000;
    gameDayCounter_ = 0;
    combatWins_ = 0;
    eventCheckTimerSec_ = 0.0f;
    gameDateAccumSec_ = 0.0f;
    stormDebuffMs_ = 0.0f;
    toastMs_ = 0.0f;
    tradeMessageMs_ = 0.0f;
    retreatHoldMs_ = 0.0f;
    screenShakeMs_ = 0.0f;
    screenShakeIntensity_ = 0.0f;
    pirateEncounterPending_ = false;
    eventLogOpen_ = false;
    resultReason_ = "defeat";
}

void Voyage3DGameInstance::LoadGameData()
{
    portDefs_ = Voyage3D::LoadPorts(PortsConfigPath);
    goodsDefs_ = Voyage3D::LoadGoods(GoodsConfigPath);
    shipDefs_ = Voyage3D::LoadShips(ShipsConfigPath);
    landBlocks_ = Voyage3D::LoadLandmass(LandmassConfigPath);
    eventDefs_ = Voyage3D::LoadEvents(EventsConfigPath);
    portLore_ = Voyage3D::LoadPortLore(PortLoreConfigPath);
}

void Voyage3DGameInstance::BuildPorts(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                      std::vector<Assets::Model>& models,
                                      std::vector<Assets::FMaterial>& materials)
{
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 1.6f, 0.5f)));
    const uint32_t towerModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.6f, 1.6f, -0.6f), glm::vec3(0.6f, 1.9f, 0.6f)));
    const uint32_t roofModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f, 2.4f, 0.0f), 0.25f));
    const uint32_t anchorModelId = static_cast<uint32_t>(models.size() - 1);

    const uint32_t roofMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.15f, 0.10f, 0.10f));
    visitedAnchorMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.90f, 0.70f, 0.20f));
    unvisitedAnchorMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.50f, 0.50f, 0.50f));

    for (Voyage3D::FPortRuntime& port : ports_)
    {
        port.rootNode = Assets::Node::CreateNode("Voyage3D_Port_" + port.def.id,
                                                 port.worldPos,
                                                 glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                 glm::vec3(1.0f),
                                                 static_cast<uint32_t>(nodes.size()));
        nodes.push_back(port.rootNode);

        const uint32_t towerMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, port.def.color);
        port.towerNode = Assets::SceneBuilder::CreateRenderNode("Voyage3D_PortTower_" + port.def.id,
                                                        glm::vec3(0.0f),
                                                        glm::vec3(1.0f),
                                                        static_cast<uint32_t>(nodes.size()),
                                                        towerModelId,
                                                        towerMaterialId);
        port.towerNode->SetParent(port.rootNode);
        nodes.push_back(port.towerNode);

        port.roofNode = Assets::SceneBuilder::CreateRenderNode("Voyage3D_PortRoof_" + port.def.id,
                                                       glm::vec3(0.0f),
                                                       glm::vec3(1.0f),
                                                       static_cast<uint32_t>(nodes.size()),
                                                       roofModelId,
                                                       roofMaterialId);
        port.roofNode->SetParent(port.rootNode);
        nodes.push_back(port.roofNode);

        port.anchorNode = Assets::SceneBuilder::CreateRenderNode("Voyage3D_PortAnchor_" + port.def.id,
                                                         glm::vec3(0.0f),
                                                         glm::vec3(1.0f),
                                                         static_cast<uint32_t>(nodes.size()),
                                                         anchorModelId,
                                                         port.visited ? visitedAnchorMaterialId_ : unvisitedAnchorMaterialId_);
        port.anchorNode->SetParent(port.rootNode);
        nodes.push_back(port.anchorNode);
    }
}

void Voyage3DGameInstance::BuildShipVisual(Voyage3D::FShipRuntime& ship,
                                           std::string_view namePrefix,
                                           const glm::vec3& sailColor,
                                           std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                           std::vector<Assets::Model>& models,
                                           std::vector<Assets::FMaterial>& materials,
                                           bool visible)
{
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 1.0f, 0.5f)));
    const uint32_t hullModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 1.0f, 0.5f)));
    const uint32_t mastModelId = static_cast<uint32_t>(models.size() - 1);
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.5f, 0.0f, -0.5f), glm::vec3(0.5f, 1.0f, 0.5f)));
    const uint32_t sailModelId = static_cast<uint32_t>(models.size() - 1);

    const uint32_t hullMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, ship.enemy ? glm::vec3(0.34f, 0.16f, 0.10f) : glm::vec3(0.50f, 0.30f, 0.15f));
    const uint32_t mastMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.30f, 0.20f, 0.10f));
    const uint32_t sailMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, sailColor);

    ship.rootNode = Assets::Node::CreateNode(std::string(namePrefix) + "_Root",
                                             ship.worldPos,
                                             YawRotation(ship.yaw),
                                             glm::vec3(1.0f),
                                             static_cast<uint32_t>(nodes.size()));
    nodes.push_back(ship.rootNode);

    ship.hullNode = Assets::SceneBuilder::CreateRenderNode(std::string(namePrefix) + "_Hull",
                                                   glm::vec3(0.0f),
                                                   ship.def.size,
                                                   static_cast<uint32_t>(nodes.size()),
                                                   hullModelId,
                                                   hullMaterialId,
                                                   visible);
    ship.hullNode->SetParent(ship.rootNode);
    nodes.push_back(ship.hullNode);

    ship.mastNode = Assets::SceneBuilder::CreateRenderNode(std::string(namePrefix) + "_Mast",
                                                   glm::vec3(0.0f, ship.def.size.y, 0.0f),
                                                   glm::vec3(0.12f, ship.def.sailHeight, 0.12f),
                                                   static_cast<uint32_t>(nodes.size()),
                                                   mastModelId,
                                                   mastMaterialId,
                                                   visible);
    ship.mastNode->SetParent(ship.rootNode);
    nodes.push_back(ship.mastNode);

    ship.sailNode = Assets::SceneBuilder::CreateRenderNode(std::string(namePrefix) + "_Sail",
                                                   glm::vec3(0.0f, ship.def.size.y + 0.32f, 0.0f),
                                                   glm::vec3(ship.def.size.x * 0.68f, ship.def.sailHeight * 0.72f, 0.08f),
                                                   static_cast<uint32_t>(nodes.size()),
                                                   sailModelId,
                                                   sailMaterialId,
                                                   visible);
    ship.sailNode->SetParent(ship.rootNode);
    nodes.push_back(ship.sailNode);
    SetRuntimeVisible(ship, visible);
}

void Voyage3DGameInstance::BuildProjectilePool(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials)
{
    models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 0.12f));
    const uint32_t projectileModelId = static_cast<uint32_t>(models.size() - 1);
    const uint32_t projectileMaterialId = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.15f, 0.15f, 0.15f));
    projectiles_.clear();
    projectiles_.reserve(96);
    for (int index = 0; index < 96; ++index)
    {
        auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("Voyage3D_Projectile_{}", index),
                                                   Voyage3D::HiddenPosition,
                                                   glm::vec3(1.0f),
                                                   static_cast<uint32_t>(nodes.size()),
                                                   projectileModelId,
                                                   projectileMaterialId,
                                                   false);
        nodes.push_back(node);
        Voyage3D::FProjectileRuntime projectile;
        projectile.node = node;
        projectiles_.push_back(projectile);
    }
}

void Voyage3DGameInstance::RebuildPlayerShipVisual()
{
    auto rebuild = [](Voyage3D::FShipRuntime& ship)
    {
        if (!ship.hullNode || !ship.mastNode || !ship.sailNode)
        {
            return;
        }
        ship.hullNode->SetScale(ship.def.size);
        ship.hullNode->SetTranslation(glm::vec3(0.0f));
        ship.mastNode->SetScale(glm::vec3(0.12f, ship.def.sailHeight, 0.12f));
        ship.mastNode->SetTranslation(glm::vec3(0.0f, ship.def.size.y, 0.0f));
        ship.sailNode->SetScale(glm::vec3(ship.def.size.x * 0.68f, ship.def.sailHeight * 0.72f, 0.08f));
        ship.sailNode->SetTranslation(glm::vec3(0.0f, ship.def.size.y + 0.32f, 0.0f));
    };
    rebuild(playerShip_);
    for (Voyage3D::FShipRuntime& enemy : enemyShips_)
    {
        rebuild(enemy);
    }
}

void Voyage3DGameInstance::UpdateNearestPort()
{
    nearestPort_ = nullptr;
    float nearestDist = PortEnterDistance;
    for (Voyage3D::FPortRuntime& port : ports_)
    {
        const glm::vec2 delta(playerShip_.worldPos.x - port.worldPos.x, playerShip_.worldPos.z - port.worldPos.z);
        const float distance = glm::length(delta);
        if (distance < nearestDist)
        {
            nearestDist = distance;
            nearestPort_ = &port;
        }
    }
}

void Voyage3DGameInstance::UpdateRuntimeEffects(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    stormDebuffMs_ = std::max(0.0f, stormDebuffMs_ - deltaMs);
    toastMs_ = std::max(0.0f, toastMs_ - deltaMs);
    tradeMessageMs_ = std::max(0.0f, tradeMessageMs_ - deltaMs);
    screenShakeMs_ = std::max(0.0f, screenShakeMs_ - deltaMs);
    if (screenShakeMs_ <= 0.0f)
    {
        screenShakeIntensity_ = 0.0f;
    }

    auto updateRemaining = [deltaMs](auto& item)
    {
        item.remainingMs -= deltaMs;
    };
    std::for_each(floatingTexts_.begin(), floatingTexts_.end(), updateRemaining);
    std::for_each(muzzleFlashes_.begin(), muzzleFlashes_.end(), updateRemaining);
    std::for_each(explosionRings_.begin(), explosionRings_.end(), updateRemaining);
    floatingTexts_.erase(std::remove_if(floatingTexts_.begin(), floatingTexts_.end(), [](const Voyage3D::FFloatingText& item)
    {
        return item.remainingMs <= 0.0f;
    }), floatingTexts_.end());
    muzzleFlashes_.erase(std::remove_if(muzzleFlashes_.begin(), muzzleFlashes_.end(), [](const Voyage3D::FMuzzleFlash& item)
    {
        return item.remainingMs <= 0.0f;
    }), muzzleFlashes_.end());
    explosionRings_.erase(std::remove_if(explosionRings_.begin(), explosionRings_.end(), [](const Voyage3D::FExpandingRing& item)
    {
        return item.remainingMs <= 0.0f;
    }), explosionRings_.end());
}

void Voyage3DGameInstance::UpdateVisitedAnchor(Voyage3D::FPortRuntime& port)
{
    Assets::NodeUtils::SetPrimaryMaterial(port.anchorNode, port.visited ? visitedAnchorMaterialId_ : unvisitedAnchorMaterialId_);
}

void Voyage3DGameInstance::SetRuntimeVisible(Voyage3D::FShipRuntime& ship, bool visible)
{
    Assets::NodeUtils::SetVisibleRecursive(ship.rootNode, visible);
}

void Voyage3DGameInstance::UpdateBgm()
{
    const std::string bgmId = BgmIdForState(appState_);
    if (bgmId == currentBgmId_)
    {
        return;
    }
    currentBgmId_ = bgmId;

    NextAudio* audio = GetEngine().GetAudio();
    if (!audio)
    {
        return;
    }

    static std::set<std::string> warnedMissingBgm;
    const std::string path = BgmPathForState(appState_);
    if (!std::filesystem::exists(path))
    {
        if (!warnedMissingBgm.contains(path))
        {
            SPDLOG_WARN("[Voyage3D] BGM not found: {}", path);
            warnedMissingBgm.insert(path);
        }
        return;
    }
    audio->PlayMusic(path, 0.45f);
}

void Voyage3DGameInstance::ApplyLightingSettings()
{
    auto& envSettings = GetEngine().GetScene().GetEnvSettings();
    envSettings.HasSky = true;
    envSettings.HasSun = true;
    envSettings.SkyIntensity = 7.0f;
    envSettings.SunIntensity = 350.0f;
    envSettings.SunRotation = 0.35f;
    GetEngine().GetScene().MarkEnvDirty();
}

void Voyage3DGameInstance::SetTradeMessage(std::string message)
{
    tradeMessage_ = std::move(message);
    tradeMessageMs_ = 1800.0f;
}

void Voyage3DGameInstance::DropOverflowCargo()
{
    while (playerShip_.cargoUsed > playerShip_.def.cargoMax && !playerShip_.cargo.empty())
    {
        auto dropIt = std::max_element(playerShip_.cargo.begin(), playerShip_.cargo.end(), [](const auto& lhs, const auto& rhs)
        {
            return lhs.second < rhs.second;
        });
        const int overflow = playerShip_.cargoUsed - playerShip_.def.cargoMax;
        const int dropped = std::min(overflow, dropIt->second);
        SPDLOG_WARN("[Voyage3D] Dropping overflow cargo: {} x{}", dropIt->first, dropped);
        dropIt->second -= dropped;
        playerShip_.cargoUsed -= dropped;
        if (dropIt->second <= 0)
        {
            playerShip_.cargo.erase(dropIt);
        }
    }
}

void Voyage3DGameInstance::CheckLossConditions()
{
    if (appState_ == Voyage3D::EAppState::Result)
    {
        return;
    }
    if (playerShip_.currentHp <= 0)
    {
        EndCombatDefeat();
    }
    else if (gold_ < 0 && playerShip_.cargoUsed == 0)
    {
        resultReason_ = "bankrupt";
        appState_ = Voyage3D::EAppState::Result;
    }
}
