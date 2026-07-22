#include "NextDayzGameInstance.hpp"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include "Application/Game/NextDayz/UI/NextDayzHUD.hpp"

namespace
{
    constexpr const char* kDefaultScene = "assets/scad/coldwar/riverland_1km.scad";
    constexpr const char* kSoldierRig = "assets/scad/characters/next_ra_soldier.scad";
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register(); // .scad scene + rig loader
    return std::make_unique<NextDayzGameInstance>(config, options, engine);
}

NextDayzGameInstance::NextDayzGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                           NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "NextDayz", 1920, 1080, false);
}

void NextDayzGameInstance::OnInit()
{
    // Runtime-added rig/loot nodes trigger scene mesh-buffer rebuilds; keep the
    // CPU mesh copies alive (same requirement as CharacterDemo/AirportSim).
    GOption->KeepCPUMeshData = true;

    weapons_.Configure(config_.Weapon);
    loot_.Configure(config_.Loot);

    if (!rig_.LoadRig(kSoldierRig))
    {
        SPDLOG_WARN("[NextDayz] soldier rig unavailable; player will have no visual");
    }

    std::string scene = kDefaultScene;
    if (!GOption->SceneName.empty())
    {
        scene = GOption->SceneName;
    }
    SPDLOG_INFO("[NextDayz] loading scene '{}'", scene);
    GetEngine().RequestLoadScene({.filename = scene});
}

void NextDayzGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    // SoftwareModern rasteriser + software GI: lightest path for a 1km POI map.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "2", &error);
}

void NextDayzGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& /*lights*/,
                                              std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    rig_.InjectAssets(models, materials);

    // FPS view-model: a dark elongated box that reads as a held rifle.
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.045f, -0.05f, -0.30f),
                                                   glm::vec3(0.045f, 0.03f, 0.32f)));
    viewModelModelId_ = static_cast<uint32_t>(models.size() - 1);
    viewModelMaterialId_ = Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.11f, 0.12f, 0.13f));
    weapons_.SetViewModelAssets(viewModelModelId_, viewModelMaterialId_);
}

glm::vec3 NextDayzGameInstance::ResolveSpawnPosition() const
{
    // Prefer spawning on the same ground as nearby supplies: a plain downward
    // raycast at the gas station hits the canopy roof, not the pad. Using a loot
    // item's world position gives real ground height and puts the player facing
    // it (loot is placed a few metres ahead along +Z, our default yaw).
    glm::vec3 lootPos;
    if (loot_.NearestLootPos(glm::vec2(config_.SpawnXZ.x, config_.SpawnXZ.z), lootPos))
    {
        const glm::vec3 spawn(lootPos.x, lootPos.y + 1.0f, lootPos.z - 3.0f);
        SPDLOG_INFO("[NextDayz] spawn near loot -> ({:.1f}, {:.1f}, {:.1f})", spawn.x, spawn.y, spawn.z);
        return spawn;
    }

    // Fallback: downward raycast (open terrain with no loot nearby).
    const glm::vec3 origin(config_.SpawnXZ.x, config_.Loot.SpawnRayHeight, config_.SpawnXZ.z);
    float groundY = 2.0f;
    GetEngine().RayCast(origin, glm::vec3(0.0f, -1.0f, 0.0f), [&](Assets::RayCastResult result) {
        if (result.Hit)
        {
            groundY = static_cast<float>(result.HitPoint.y);
        }
        return true;
    });
    return glm::vec3(config_.SpawnXZ.x, groundY + 1.0f, config_.SpawnXZ.z);
}

void NextDayzGameInstance::OnSceneLoaded()
{
    // Loot scan first so the spawn can be derived from real supply ground height.
    loot_.OnSceneLoaded(GetEngine().GetScene());

    const glm::vec3 spawn = ResolveSpawnPosition();
    player_.Create(GetEngine().GetPhysicsEngine(), spawn, config_);

    rig_.OnSceneLoaded(GetEngine().GetScene());
    weapons_.OnSceneLoaded(GetEngine());
    time_.Reset(config_.Time);
    time_.Tick(0.0, GetEngine().GetScene());

    // Start armed with an AK and a couple of magazines to test shooting before looting.
    inventory_.Clear();
    inventory_.Add("ak", "AK-74", NextDayz::EItemKind::Weapon, 1);
    inventory_.Add(std::string(NextDayz::AmmoItemId(NextDayz::EAmmoType::Rifle545)),
                   std::string(NextDayz::AmmoDisplayName(NextDayz::EAmmoType::Rifle545)), NextDayz::EItemKind::Ammo, 60);
    weapons_.Equip(0, "ak");

    GetEngine().GetScene().GetEnvSettings().SkyIdx = 2;

    showInventory_ = false;
    SetMouseCaptured(true);
    sceneReady_ = true;
    SPDLOG_INFO("[NextDayz] scene loaded ({} nodes)", GetEngine().GetScene().Nodes().size());
}

void NextDayzGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    player_.Destroy();
    rig_.OnSceneUnloaded();
    weapons_.OnSceneUnloaded();
    loot_.OnSceneUnloaded();
    inventory_.Clear();
}

void NextDayzGameInstance::OnDestroy()
{
    player_.Destroy();
}

NextDayz::EAnimState NextDayzGameInstance::CurrentAnimState() const
{
    if (fireAnimTimer_ > 0.0f)
    {
        return NextDayz::EAnimState::Fire;
    }
    switch (player_.MoveState())
    {
    case NextDayz::EMoveState::Run:  return NextDayz::EAnimState::Run;
    case NextDayz::EMoveState::Walk: return NextDayz::EAnimState::Walk;
    case NextDayz::EMoveState::Idle:
    default:                         return NextDayz::EAnimState::Idle;
    }
}

void NextDayzGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_ || !player_.IsValid())
    {
        return;
    }
    const float dt = static_cast<float>(deltaSeconds);

    player_.Update(dt);
    weapons_.Update(dt, player_, inventory_, GetEngine());
    if (weapons_.ConsumeFiredThisFrame())
    {
        fireAnimTimer_ = config_.Weapon.FireAnimSeconds;
    }

    loot_.Update(player_.EyePosition(), player_.Forward());
    time_.Tick(deltaSeconds, GetEngine().GetScene());

    rig_.SetVisible(!player_.IsFirstPerson());
    rig_.Update(player_.Position(), player_.Yaw(), CurrentAnimState(), player_.HorizontalSpeed(), dt);

    if (fireAnimTimer_ > 0.0f)
    {
        fireAnimTimer_ = std::max(0.0f, fireAnimTimer_ - dt);
    }
}

bool NextDayzGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (!player_.IsValid())
    {
        return false;
    }
    float fov = config_.Camera.BaseFov;
    player_.FillCamera(outRenderCamera.ModelView, fov);
    outRenderCamera.FieldOfView = fov;
    return true;
}

bool NextDayzGameInstance::OnRenderUI()
{
    if (!sceneReady_)
    {
        return false;
    }

    NextDayz::FHudContext ctx;
    ctx.aiming = player_.IsAiming();
    ctx.firstPerson = player_.IsFirstPerson();
    ctx.hasWeapon = weapons_.HasActiveWeapon();
    ctx.weaponName = weapons_.ActiveDisplayName();
    ctx.ammoInMag = weapons_.AmmoInMag();
    ctx.ammoReserve = weapons_.AmmoReserve(inventory_);
    ctx.reloading = weapons_.IsReloading();
    ctx.activeSlot = weapons_.ActiveSlot();
    ctx.interactionPrompt = loot_.HoveredPrompt();
    ctx.hour = time_.HourInt();
    ctx.minute = time_.MinuteInt();
    ctx.overcast = time_.Overcast();
    ctx.showInventory = showInventory_;
    ctx.inventory = &inventory_;
    ctx.weapons = &weapons_;
    ctx.equipWeapon = [this](const std::string& weaponId, int slot) {
        const_cast<NextDayzGameInstance*>(this)->EquipFromInventory(weaponId, slot);
    };
    ctx.toggleClothing = [this](const std::string& clothingId, bool on) {
        const_cast<NextDayzGameInstance*>(this)->ToggleClothing(clothingId, on);
    };
    NextDayz::NextDayzHUD::Draw(ctx);
    return true;
}

void NextDayzGameInstance::EquipFromInventory(const std::string& weaponId, int slot)
{
    weapons_.Equip(slot, weaponId);
}

void NextDayzGameInstance::ToggleClothing(const std::string& clothingId, bool on)
{
    inventory_.SetClothingEquipped(clothingId, on);
    rig_.SetClothing(clothingId, on);
}

void NextDayzGameInstance::SetMouseCaptured(bool captured)
{
    mouseCaptured_ = captured;
    resetMouse_ = true;
    if (!GOption->AgentValidation)
    {
        SDL_SetWindowRelativeMouseMode(GetEngine().GetWindow().Handle(), captured);
    }
}

void NextDayzGameInstance::SetInventoryOpen(bool open)
{
    showInventory_ = open;
    if (open)
    {
        player_.SetAiming(false);
        weapons_.SetTriggerDown(false);
        SetMouseCaptured(false);
    }
    else
    {
        SetMouseCaptured(true);
    }
}

bool NextDayzGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }
    const bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    switch (event.key.key)
    {
    case SDLK_W: player_.SetMoveKey(0, pressed); return true;
    case SDLK_S: player_.SetMoveKey(1, pressed); return true;
    case SDLK_A: player_.SetMoveKey(2, pressed); return true;
    case SDLK_D: player_.SetMoveKey(3, pressed); return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        player_.SetSprinting(pressed);
        return true;
    case SDLK_SPACE:
        if (pressed) player_.QueueJump();
        return true;
    case SDLK_R:
        if (pressed) weapons_.RequestReload(inventory_);
        return true;
    case SDLK_1:
        if (pressed) weapons_.SwitchSlot(0);
        return true;
    case SDLK_2:
        if (pressed) weapons_.SwitchSlot(1);
        return true;
    case SDLK_Q:
        if (pressed) weapons_.SwitchPrevious();
        return true;
    case SDLK_E:
        if (pressed) loot_.PickupHovered(inventory_, GetEngine());
        return true;
    case SDLK_V:
        if (pressed) player_.ToggleView();
        return true;
    case SDLK_G:
        if (pressed) time_.ToggleOvercast();
        return true;
    case SDLK_T:
        if (pressed) time_.Skip(120.0); // debug: jump 2 game hours
        return true;
    case SDLK_TAB:
    case SDLK_I:
        if (pressed) SetInventoryOpen(!showInventory_);
        return true;
    case SDLK_ESCAPE:
        if (pressed)
        {
            if (showInventory_)
            {
                SetInventoryOpen(false);
            }
            else
            {
                SetMouseCaptured(!mouseCaptured_);
            }
        }
        return true;
    default:
        return false;
    }
}

bool NextDayzGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (!mouseCaptured_ || showInventory_)
    {
        return false;
    }

    if (SDL_GetWindowRelativeMouseMode(GetEngine().GetWindow().Handle()))
    {
        player_.OnLook(static_cast<float>(xpos), static_cast<float>(ypos));
    }
    else
    {
        if (resetMouse_)
        {
            mousePos_ = glm::dvec2(xpos, ypos);
            resetMouse_ = false;
            return true;
        }
        const glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;
        mousePos_ = glm::dvec2(xpos, ypos);
        player_.OnLook(static_cast<float>(delta.x), static_cast<float>(delta.y));
    }
    return true;
}

bool NextDayzGameInstance::OnMouseButton(SDL_Event& event)
{
    if (showInventory_)
    {
        return false; // let ImGui handle panel interaction
    }

    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        if (!mouseCaptured_)
        {
            SetMouseCaptured(true);
            return true;
        }
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            weapons_.SetTriggerDown(true);
            return true;
        }
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            player_.SetAiming(true);
            return true;
        }
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            weapons_.SetTriggerDown(false);
            return true;
        }
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            player_.SetAiming(false);
            return true;
        }
    }
    return false;
}

bool NextDayzGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    if (!player_.IsFirstPerson())
    {
        player_.AdjustCameraDistance(static_cast<float>(yoffset));
        return true;
    }
    return false;
}

void NextDayzGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("ammoInMag", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(weapons_.AmmoInMag()); });
    reg.Add("ammoReserve",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(weapons_.AmmoReserve(inventory_)); });
    reg.Add("inventoryCount", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(inventory_.Items().size());
    });
    reg.Add("equippedWeapon",
            [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.ActiveWeaponId(); });
    reg.Add("isReloading", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsReloading(); });
    reg.Add("isAiming", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsAiming(); });
    reg.Add("firstPerson", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsFirstPerson(); });
    reg.Add("hoveredLoot", [this]() -> Runtime::Agent::FAgentQueryValue { return loot_.HoveredPrompt(); });
    reg.Add("lootRemaining",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.RemainingCount()); });
    reg.Add("hour", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(time_.HourInt()); });
    reg.Add("posX", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().x); });
    reg.Add("posY", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().y); });
    reg.Add("posZ", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().z); });
}
