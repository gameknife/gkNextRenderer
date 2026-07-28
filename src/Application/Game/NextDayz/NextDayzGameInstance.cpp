#include "NextDayzGameInstance.hpp"

#include <algorithm>
#include <cmath>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <fmt/format.h>
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
    constexpr const char* kDefaultScene = "assets/scad/proc/coldwar/riverland_1km.scad";
    constexpr const char* kSoldierRig = "assets/scad/characters/nextdayz_survivor.scad";

    void AppendWeaponBox(std::vector<Assets::Vertex>& vertices, std::vector<uint32_t>& indices,
                         const glm::vec3& min, const glm::vec3& max)
    {
        Assets::Model box = Assets::FProcModel::CreateBox(min, max);
        const uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        vertices.insert(vertices.end(), box.CPUVertices().begin(), box.CPUVertices().end());
        for (uint32_t index : box.CPUIndices())
        {
            indices.push_back(vertexOffset + index);
        }
    }

    Assets::Model CreateWeaponVisual(std::string_view weaponId)
    {
        std::vector<Assets::Vertex> vertices;
        std::vector<uint32_t> indices;
        const auto box = [&](const glm::vec3& min, const glm::vec3& max) {
            AppendWeaponBox(vertices, indices, min, max);
        };

        if (weaponId == "pistol")
        {
            box({-0.040f, -0.015f, -0.08f}, {0.040f, 0.055f, 0.22f}); // slide
            box({-0.034f, -0.22f, -0.04f}, {0.034f, -0.015f, 0.07f}); // grip
            box({-0.018f, 0.005f, 0.22f}, {0.018f, 0.035f, 0.29f});   // barrel
            box({-0.035f, 0.055f, -0.06f}, {-0.016f, 0.095f, -0.035f});
            box({0.016f, 0.055f, -0.06f}, {0.035f, 0.095f, -0.035f});
            box({-0.007f, 0.055f, 0.255f}, {0.007f, 0.095f, 0.280f});
        }
        else if (weaponId == "shotgun")
        {
            box({-0.055f, -0.045f, -0.42f}, {0.055f, 0.045f, -0.10f});
            box({-0.045f, -0.050f, -0.10f}, {0.045f, 0.055f, 0.20f});
            box({-0.025f, 0.005f, 0.20f}, {0.025f, 0.050f, 0.78f});
            box({-0.040f, -0.065f, 0.22f}, {0.040f, -0.005f, 0.48f});
            box({-0.040f, 0.050f, -0.02f}, {-0.018f, 0.10f, 0.01f});
            box({0.018f, 0.050f, -0.02f}, {0.040f, 0.10f, 0.01f});
            box({-0.007f, 0.050f, 0.72f}, {0.007f, 0.10f, 0.75f});
        }
        else if (weaponId == "mosin")
        {
            box({-0.045f, -0.040f, -0.48f}, {0.045f, 0.045f, 0.12f});
            box({-0.020f, 0.005f, 0.12f}, {0.020f, 0.040f, 0.86f});
            box({-0.034f, -0.13f, -0.08f}, {0.034f, -0.04f, 0.03f});
            box({-0.038f, 0.045f, 0.00f}, {-0.017f, 0.10f, 0.03f});
            box({0.017f, 0.045f, 0.00f}, {0.038f, 0.10f, 0.03f});
            box({-0.007f, 0.040f, 0.80f}, {0.007f, 0.10f, 0.83f});
        }
        else if (weaponId == "svd")
        {
            box({-0.050f, -0.045f, -0.43f}, {0.050f, 0.050f, 0.20f});
            box({-0.021f, 0.000f, 0.20f}, {0.021f, 0.040f, 0.86f});
            // Four rails leave a visible sight channel through the scope.
            box({-0.035f, 0.055f, -0.02f}, {-0.022f, 0.115f, 0.25f});
            box({0.022f, 0.055f, -0.02f}, {0.035f, 0.115f, 0.25f});
            box({-0.022f, 0.055f, -0.02f}, {0.022f, 0.068f, 0.25f});
            box({-0.022f, 0.102f, -0.02f}, {0.022f, 0.115f, 0.25f});
            box({-0.035f, -0.18f, -0.02f}, {0.035f, -0.045f, 0.10f});
        }
        else // AK-74
        {
            box({-0.055f, -0.045f, -0.38f}, {0.055f, 0.045f, -0.12f});
            box({-0.050f, -0.050f, -0.12f}, {0.050f, 0.060f, 0.20f});
            box({-0.022f, 0.000f, 0.20f}, {0.022f, 0.045f, 0.66f});
            box({-0.038f, -0.20f, -0.02f}, {0.038f, -0.05f, 0.12f});
            box({-0.045f, 0.045f, -0.01f}, {-0.018f, 0.105f, 0.025f});
            box({0.018f, 0.045f, -0.01f}, {0.045f, 0.105f, 0.025f});
            box({-0.008f, 0.045f, 0.59f}, {0.008f, 0.105f, 0.625f});
        }

        return Assets::Model::CreateFromGeometry(fmt::format("nd_weapon_{}", weaponId),
                                                  std::move(vertices), std::move(indices));
    }

    glm::vec3 WeaponVisualColor(std::string_view weaponId)
    {
        if (weaponId == "ak") return {0.20f, 0.13f, 0.07f};
        if (weaponId == "svd") return {0.24f, 0.15f, 0.08f};
        if (weaponId == "mosin") return {0.31f, 0.20f, 0.10f};
        if (weaponId == "shotgun") return {0.14f, 0.12f, 0.10f};
        return {0.10f, 0.11f, 0.12f};
    }
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
    rig_.Configure(config_.Animation);
    actions_.Configure(config_.Action);

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

    cvars.RegisterFloat("nextdayz.player.maxStepHeight", config_.Player.MaxStepHeight,
                        &config_.Player.MaxStepHeight, NextCVar::ECVarFlags::Archive,
                        "Maximum obstacle height the player can step over; applied when the controller is created",
                        nullptr, 0.05, 0.60);
}

void NextDayzGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& /*lights*/,
                                              std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    rig_.InjectAssets(models, materials);

    for (size_t i = 0; i < NextDayz::kWeapons.size(); ++i)
    {
        const NextDayz::FWeaponDef& def = NextDayz::kWeapons[i];
        models.push_back(CreateWeaponVisual(def.id));
        weaponModelIds_[i] = static_cast<uint32_t>(models.size() - 1);
        weaponMaterialIds_[i] =
            Assets::SceneBuilder::AddLambertianMaterial(materials, WeaponVisualColor(def.id));
    }
    weapons_.SetViewModelAssets(weaponModelIds_, weaponMaterialIds_);
    rig_.SetWeaponAssets(weaponModelIds_, weaponMaterialIds_);
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
    actions_.Reset();
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
    actions_.Reset();
}

void NextDayzGameInstance::OnDestroy()
{
    player_.Destroy();
}

void NextDayzGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_ || !player_.IsValid())
    {
        return;
    }
    const float dt = static_cast<float>(deltaSeconds);

    actions_.Update(dt);
    if (const std::optional<NextDayz::FLootHandle> cancel = actions_.ConsumeCancelRequest())
    {
        loot_.Cancel(*cancel);
    }
    if (const std::optional<NextDayz::FLootHandle> commit = actions_.ConsumeCommitRequest())
    {
        loot_.Commit(*commit, inventory_, GetEngine());
    }

    const bool actionLocked = actions_.IsActive();
    const bool traversalLocked = player_.IsTraversing();
    player_.SetMovementLocked(actionLocked || traversalLocked || showInventory_);
    weapons_.SetPresentationSuppressed(actionLocked || traversalLocked);
    if (actionLocked || traversalLocked)
    {
        player_.SetAiming(false);
        weapons_.SetTriggerDown(false);
    }
    player_.Update(dt);
    weapons_.Update(dt, player_, inventory_, GetEngine());
    if (weapons_.PresentationAction() != NextDayz::EWeaponPresentationAction::None)
    {
        player_.SetAiming(false);
    }
    for (const NextDayz::FShotEvent& shot : weapons_.ConsumeShotEvents())
    {
        player_.ApplyCameraRecoil(shot.cameraImpulseRadians);
        rig_.TriggerRecoil(shot.rigRecoilScale);
    }

    loot_.Update(player_.EyePosition(), player_.Forward());
    time_.Tick(deltaSeconds, GetEngine().GetScene());

    rig_.SetVisible(!player_.IsFirstPerson());
    NextDayz::FPlayerPresentationState presentation;
    presentation.locomotion = player_.LocomotionState();
    presentation.aimWeight = player_.IsAiming() ? 1.0f : 0.0f;
    presentation.aimPitchRadians = player_.Pitch();
    presentation.hasWeapon = weapons_.HasActiveWeapon();
    presentation.weaponId = weapons_.ActiveWeaponId();
    presentation.weaponAction = weapons_.PresentationAction();
    presentation.weaponActionTime01 = weapons_.PresentationActionTime();
    presentation.activeWeaponSlot = weapons_.ActiveSlot();
    presentation.slotWeaponIds = {
        weapons_.SlotWeaponId(0),
        weapons_.SlotWeaponId(1),
    };
    presentation.action = actions_.Action();
    presentation.actionTime01 = actions_.NormalizedTime();
    rig_.Update(player_.Position(), player_.Yaw(), presentation, dt);
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
    outRenderCamera.NearPlane = 0.1f;
    outRenderCamera.FarPlane = 1500.f;
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
    ctx.interactionPrompt = actions_.IsActive() ? "Looting..." : loot_.HoveredPrompt();
    ctx.hour = time_.HourInt();
    ctx.minute = time_.MinuteInt();
    ctx.overcast = time_.Overcast();
    ctx.showInventory = showInventory_;
    ctx.showDebugPanel = showDebugPanel_;
    ctx.inventory = &inventory_;
    ctx.weapons = &weapons_;
    const NextDayz::FPlayerLocomotionState& locomotion = player_.LocomotionState();
    ctx.debug.position = player_.Position();
    ctx.debug.eyePosition = player_.EyePosition();
    ctx.debug.velocity = locomotion.worldVelocity;
    ctx.debug.localMove = locomotion.localMove;
    ctx.debug.cameraRecoilRadians = player_.CameraRecoil();
    ctx.debug.yawRadians = player_.Yaw();
    ctx.debug.pitchRadians = player_.Pitch();
    ctx.debug.fovDegrees = player_.CurrentFov();
    ctx.debug.horizontalSpeed = locomotion.horizontalSpeed;
    ctx.debug.controllerHeight = player_.ControllerHeight();
    ctx.debug.aimWeight = rig_.AimWeight();
    ctx.debug.actionTime = actions_.NormalizedTime();
    ctx.debug.stance = NextDayz::StanceName(locomotion.actualStance);
    ctx.debug.desiredStance = NextDayz::StanceName(locomotion.desiredStance);
    ctx.debug.gait = NextDayz::GaitName(locomotion.gait);
    ctx.debug.jumpPhase = NextDayz::JumpPhaseName(locomotion.jumpPhase);
    ctx.debug.jumpPhaseTime = locomotion.jumpPhaseTime01;
    ctx.debug.traversalAction = NextDayz::TraversalActionName(locomotion.traversalAction);
    ctx.debug.traversalProbeResult = player_.TraversalProbeResult();
    ctx.debug.traversalTime = locomotion.traversalTime01;
    ctx.debug.traversalHeight = locomotion.traversalHeight;
    ctx.debug.baseAnimation = rig_.CurrentBaseClipName();
    ctx.debug.action = NextDayz::ActionName(actions_.Action());
    ctx.debug.onGround = locomotion.onGround;
    ctx.debug.standBlocked = locomotion.standBlocked;
    ctx.debug.sprinting = player_.IsSprinting();
    ctx.debug.actionCommitted = actions_.IsCommitted();
    ctx.debug.cameraRecoilActive = player_.RecoilActive();
    ctx.debug.rigRecoilActive = rig_.RecoilActive();
    ctx.debug.viewModelRecoilActive = weapons_.ViewModelRecoilActive();
    ctx.debug.shotSequence = weapons_.LastShotSequence();
    ctx.debug.weaponAction = NextDayz::WeaponActionName(weapons_.PresentationAction());
    ctx.debug.weaponActionTime = weapons_.PresentationActionTime();
    ctx.debug.weaponActionClip = rig_.CurrentWeaponActionClipName();
    ctx.debug.weaponActionWeight = rig_.WeaponActionWeight();
    ctx.debug.switchingWeapon = weapons_.IsSwitching();
    ctx.debug.switchTargetSlot = weapons_.SwitchTargetSlot();
    ctx.debug.switchCommitted = weapons_.SwitchCommitted();
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
    player_.SetMovementLocked(open);
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
    if (pressed && actions_.IsActive() &&
        (event.key.key == SDLK_W || event.key.key == SDLK_S || event.key.key == SDLK_A ||
         event.key.key == SDLK_D || event.key.key == SDLK_SPACE))
    {
        actions_.RequestCancel();
    }
    switch (event.key.key)
    {
    case SDLK_W: player_.SetMoveKey(0, pressed); return true;
    case SDLK_S: player_.SetMoveKey(1, pressed); return true;
    case SDLK_A: player_.SetMoveKey(2, pressed); return true;
    case SDLK_D: player_.SetMoveKey(3, pressed); return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        player_.SetSprintModifier(pressed);
        return true;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        player_.SetWalkModifier(pressed);
        return true;
    case SDLK_C:
        if (pressed && !actions_.IsActive() && !player_.IsTraversing()) player_.QueueCrouchToggle();
        return true;
    case SDLK_SPACE:
        if (pressed && !actions_.IsActive() &&
            weapons_.PresentationAction() == NextDayz::EWeaponPresentationAction::None)
        {
            player_.QueueContextualJump(GetEngine());
        }
        return true;
    case SDLK_R:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.RequestReload(inventory_);
            if (weapons_.IsReloading()) player_.SetAiming(false);
        }
        return true;
    case SDLK_1:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(0);
            if (weapons_.IsSwitching()) player_.SetAiming(false);
        }
        return true;
    case SDLK_2:
        if (pressed && !player_.IsTraversing())
        {
            weapons_.SwitchSlot(1);
            if (weapons_.IsSwitching()) player_.SetAiming(false);
        }
        return true;
    case SDLK_Q:
        if (pressed && !player_.IsTraversing()) weapons_.SwitchPrevious();
        return true;
    case SDLK_E:
        if (pressed && !actions_.IsActive() && !player_.IsTraversing())
        {
            if (const std::optional<NextDayz::FLootHandle> handle = loot_.ReserveHovered())
            {
                if (actions_.BeginLoot(*handle))
                {
                    player_.SetAiming(false);
                    player_.SetMovementLocked(true);
                    weapons_.SetTriggerDown(false);
                    weapons_.SetPresentationSuppressed(true);
                }
                else
                {
                    loot_.Cancel(*handle);
                }
            }
        }
        return true;
    case SDLK_V:
        if (pressed) player_.ToggleView();
        return true;
    case SDLK_F5:
        if (pressed) showDebugPanel_ = !showDebugPanel_;
        return true;
    case SDLK_G:
        if (pressed) time_.ToggleOvercast();
        return true;
    case SDLK_T:
        if (pressed) time_.Skip(120.0); // debug: jump 2 game hours
        return true;
    case SDLK_TAB:
    case SDLK_I:
        if (pressed && !player_.IsTraversing()) SetInventoryOpen(!showInventory_);
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
    if (actions_.IsActive())
    {
        return true;
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
    reg.Add("isSwitching", [this]() -> Runtime::Agent::FAgentQueryValue { return weapons_.IsSwitching(); });
    reg.Add("weaponAction", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::WeaponActionName(weapons_.PresentationAction()));
    });
    reg.Add("weaponActionTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(weapons_.PresentationActionTime());
    });
    reg.Add("activeWeaponSlot", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(weapons_.ActiveSlot());
    });
    reg.Add("isAiming", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsAiming(); });
    reg.Add("firstPerson", [this]() -> Runtime::Agent::FAgentQueryValue { return player_.IsFirstPerson(); });
    reg.Add("hoveredLoot", [this]() -> Runtime::Agent::FAgentQueryValue { return loot_.HoveredPrompt(); });
    reg.Add("lootRemaining",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(loot_.RemainingCount()); });
    reg.Add("hour", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<int64_t>(time_.HourInt()); });
    reg.Add("posX", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().x); });
    reg.Add("posY", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().y); });
    reg.Add("posZ", [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.Position().z); });
    reg.Add("stance", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::StanceName(player_.LocomotionState().actualStance));
    });
    reg.Add("desiredStance", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::StanceName(player_.LocomotionState().desiredStance));
    });
    reg.Add("standBlocked",
            [this]() -> Runtime::Agent::FAgentQueryValue { return player_.LocomotionState().standBlocked; });
    reg.Add("controllerHeight",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(player_.ControllerHeight()); });
    reg.Add("gait", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::GaitName(player_.LocomotionState().gait));
    });
    reg.Add("jumpPhase", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::JumpPhaseName(player_.LocomotionState().jumpPhase));
    });
    reg.Add("jumpPhaseTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().jumpPhaseTime01);
    });
    reg.Add("traversalAction", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::TraversalActionName(
            player_.LocomotionState().traversalAction));
    });
    reg.Add("traversalTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().traversalTime01);
    });
    reg.Add("traversalHeight", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().traversalHeight);
    });
    reg.Add("traversalProbe", [this]() -> Runtime::Agent::FAgentQueryValue {
        return player_.TraversalProbeResult();
    });
    reg.Add("onGround",
            [this]() -> Runtime::Agent::FAgentQueryValue { return player_.LocomotionState().onGround; });
    reg.Add("verticalVelocity", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().worldVelocity.y);
    });
    reg.Add("velocityX", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().worldVelocity.x);
    });
    reg.Add("velocityZ", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().worldVelocity.z);
    });
    reg.Add("localMoveX", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().localMove.x);
    });
    reg.Add("localMoveY", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.LocomotionState().localMove.y);
    });
    reg.Add("baseAnim",
            [this]() -> Runtime::Agent::FAgentQueryValue { return rig_.CurrentBaseClipName(); });
    reg.Add("aimWeight",
            [this]() -> Runtime::Agent::FAgentQueryValue { return static_cast<double>(rig_.AimWeight()); });
    reg.Add("shotSequence", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<int64_t>(weapons_.LastShotSequence());
    });
    reg.Add("recoilActive", [this]() -> Runtime::Agent::FAgentQueryValue {
        return player_.RecoilActive() || weapons_.ViewModelRecoilActive() || rig_.RecoilActive();
    });
    reg.Add("cameraRecoilPitch", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(player_.CameraRecoil().y);
    });
    reg.Add("action", [this]() -> Runtime::Agent::FAgentQueryValue {
        return std::string(NextDayz::ActionName(actions_.Action()));
    });
    reg.Add("actionTime", [this]() -> Runtime::Agent::FAgentQueryValue {
        return static_cast<double>(actions_.NormalizedTime());
    });
}
