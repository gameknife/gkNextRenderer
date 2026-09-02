#include "NextAstrobotGameInstance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include "Application/Game/NextAstrobot/AstroAudio.hpp"
#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"
#include "Application/Game/NextAstrobot/UI/AstroHud.hpp"

using namespace NextAstrobot;

namespace
{
    constexpr const char* kGameplayConfig = "assets/configs/nextastrobot/gameplay.json";
    constexpr const char* kLevelsConfig = "assets/configs/nextastrobot/levels.json";
    constexpr const char* kPlayerRig = "assets/scad/characters/astro_bot.scad";
    constexpr float kGoalHoldSeconds = 2.0f;
    constexpr float kToastSeconds = 2.2f;
    constexpr float kStickDeadzone = 0.22f;

    // Modules whose nodes must not become implicit static colliders: pickups and
    // characters are pure geometry the game tests against, decorations would trip the
    // player up, and the movable pieces listed here are driven without collision.
    // (Design section 5.2.)
    constexpr std::array kNoCollisionModules = {
        std::string_view{"ab_item_coin"},        std::string_view{"ab_item_puzzle"},
        std::string_view{"ab_item_gem"},         std::string_view{"ab_item_key"},
        std::string_view{"ab_item_star"},        std::string_view{"ab_char_bot"},
        std::string_view{"ab_char_bot_lost"},    std::string_view{"ab_char_enemy_walker"},
        std::string_view{"ab_char_enemy_flyer"}, std::string_view{"ab_char_enemy_spiky"},
        std::string_view{"ab_prop_laser"},       std::string_view{"ab_prop_spike_ball_chain"},
    };
    // Every ab_part_* that is animated without a kinematic body of its own. Leaving one
    // out is not a subtle bug: the piece keeps the implicit static collider it was born
    // with, which then hangs in mid-air at the bind pose blocking the player.
    constexpr std::array kNoCollisionParts = {
        std::string_view{"ab_part_button_cap"},      std::string_view{"ab_part_spring_cap"},
        std::string_view{"ab_part_zipline_car"},     std::string_view{"ab_part_laser_beam"},
        std::string_view{"ab_part_spike_ball"},      std::string_view{"ab_part_cage_dome"},
        std::string_view{"ab_part_fan_blades"},      std::string_view{"ab_part_fountain_column"},
        std::string_view{"ab_part_windmill_blades"}, std::string_view{"ab_part_chest_lid"},
        std::string_view{"ab_part_lever_arm"},       std::string_view{"ab_part_checkpoint_flag"},
    };
    constexpr std::array kNoCollisionDecor = {
        std::string_view{"ab_nature_grass_tuft"}, std::string_view{"ab_nature_flower"},
        std::string_view{"ab_prop_balloon"},      std::string_view{"ab_prop_bubble"},
    };

    bool Contains(std::span<const std::string_view> names, const std::string& name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    float StickAxis(int16_t raw)
    {
        const float value = static_cast<float>(raw) / 32767.0f;
        if (std::abs(value) < kStickDeadzone)
        {
            return 0.0f;
        }
        // Rescale past the deadzone so the first responsive input is not a jump in speed.
        const float sign = value > 0.0f ? 1.0f : -1.0f;
        return sign * (std::abs(value) - kStickDeadzone) / (1.0f - kStickDeadzone);
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::Scad::Register(); // .scad scene + rig loader
    return std::make_unique<NextAstrobotGameInstance>(config, options, engine);
}

NextAstrobotGameInstance::NextAstrobotGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                   NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "NextAstrobot", 1920, 1080, false);
}

const FLevelDesc& NextAstrobotGameInstance::CurrentLevel() const
{
    const size_t cursor = std::min(levelCursor_, config_.Levels.size() - 1);
    return config_.Levels[cursor];
}

void NextAstrobotGameInstance::OnInit()
{
    // The player rig is added after the scene loads, which rebuilds the mesh buffer.
    GOption->KeepCPUMeshData = true;
    config_.Load(kGameplayConfig, kLevelsConfig);

    player_.Configure(config_.Move);
    camera_.Configure(config_.Camera);
    mechanisms_.Configure(config_.World);
    collectibles_.Configure(config_.World);
    hazards_.Configure(config_.World);
    enemies_.Configure(config_.World);
    interactables_.Configure(config_.World);

    if (!rig_.LoadRig(kPlayerRig))
    {
        SPDLOG_WARN("[NextAstrobot] player rig unavailable; the character will be invisible");
    }

    std::string scene = CurrentLevel().Scene;
    if (!GOption->SceneName.empty())
    {
        scene = GOption->SceneName;
    }
    SPDLOG_INFO("[NextAstrobot] loading level '{}'", scene);
    GetEngine().RequestLoadScene({.filename = scene});
}

void NextAstrobotGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    // A toy-plastic level with a lot of small props: the rasteriser plus software GI is
    // the cheapest path that still shows the kit's gloss.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);

    cvars.RegisterBool("astro.god", false, &godMode_, NextCVar::ECVarFlags::None,
                       "Ignore hazards, enemies and the kill plane",
                       [this]() { player_.SetGodMode(godMode_); });
    cvars.RegisterFloat("astro.timescale", 1.0f, &timeScale_, NextCVar::ECVarFlags::None,
                        "Multiplies gameplay delta time", nullptr, 0.05, 4.0);
    cvars.RegisterString("astro.teleport", "", &teleportRequest_, NextCVar::ECVarFlags::None,
                         "Teleport the player to \"x,y,z\" (world space)",
                         [this]()
                         {
                             if (teleportRequest_.empty() || !sceneReady_)
                             {
                                 return;
                             }
                             glm::vec3 target(0.0f);
                             if (std::sscanf(teleportRequest_.c_str(), "%f,%f,%f", &target.x, &target.y, &target.z) == 3)
                             {
                                 player_.Teleport(target);
                                 previousFoot_ = target;
                                 camera_.Snap(target, player_.Yaw());
                             }
                             teleportRequest_.clear();
                         });
    cvars.RegisterString("astro.ride", "", &rideRequest_, NextCVar::ECVarFlags::None,
                         "Teleport the player onto a named mechanism (\"moving\", \"pendulum\", \"conveyor\", ...)",
                         [this]()
                         {
                             if (rideRequest_.empty() || !sceneReady_)
                             {
                                 return;
                             }
                             glm::vec3 point(0.0f);
                             if (mechanisms_.TryGetRidePoint(rideRequest_, point))
                             {
                                 player_.Teleport(point);
                                 float yaw = player_.Yaw();
                                 if (mechanisms_.TryGetFaceYaw(rideRequest_, point, yaw))
                                 {
                                     // Dropped in front of a prop rather than on top of a
                                     // platform: face it, so the next punch lands.
                                     player_.SetYaw(yaw);
                                 }
                                 previousFoot_ = point;
                                 camera_.Snap(point, player_.Yaw());
                             }
                             else
                             {
                                 SPDLOG_WARN("[NextAstrobot] astro.ride: no mechanism named '{}'", rideRequest_);
                             }
                             rideRequest_.clear();
                         });
    cvars.RegisterString("astro.state", "", &forcedState_, NextCVar::ECVarFlags::None,
                         "Force the level flow into \"title\", \"playing\" or \"result\"",
                         [this]()
                         {
                             if (forcedState_ == "playing")
                             {
                                 while (flow_.RequestSkip()) {}
                             }
                             else if (forcedState_ == "result")
                             {
                                 while (flow_.RequestSkip()) {}
                                 flow_.NotifyGoalReached();
                             }
                             else if (forcedState_ == "title")
                             {
                                 RestartLevel();
                             }
                             forcedState_.clear();
                         });
}

void NextAstrobotGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                  std::vector<Assets::Model>& models,
                                                  std::vector<Assets::FMaterial>& materials,
                                                  std::vector<Assets::LightObject>& /*lights*/,
                                                  std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    // Nodes are not addressable yet (no world transforms, no scene): the only safe work
    // here is asset injection and turning off raycast visibility.
    for (const std::shared_ptr<Assets::Node>& node : nodes)
    {
        const std::string& name = node->GetName();
        const bool noCollision = Contains(kNoCollisionModules, name) || Contains(kNoCollisionParts, name) ||
                                 Contains(kNoCollisionDecor, name);
        if (noCollision)
        {
            Assets::NodeUtils::SetRayCastVisibleRecursive(node, false);
        }
    }
    rig_.InjectAssets(models, materials);
}

glm::vec3 NextAstrobotGameInstance::SpawnFootPosition() const
{
    if (!index_.hasSpawn)
    {
        return glm::vec3(0.0f, 2.0f, 0.0f);
    }
    // ab_bldg_startpad has its deck at SCAD z = 0.25, which is engine +Y.
    return index_.spawn.worldPos + glm::vec3(0.0f, 0.25f, 0.0f);
}

void NextAstrobotGameInstance::OnSceneLoaded()
{
    Assets::Scene& scene = GetEngine().GetScene();
    indexWarnings_.clear();
    index_ = FLevelIndex::Build(scene, &indexWarnings_);
    for (const std::string& warning : indexWarnings_)
    {
        SPDLOG_WARN("[NextAstrobot] level: {}", warning);
    }

    NextPhysics* physics = GetEngine().GetPhysicsEngine();
    mechanisms_.Bind(scene, physics, index_);
    collectibles_.Bind(scene, index_);
    // Scene::RebuildMeshBuffer installs an infinite floor plane at the scene AABB
    // minimum, so a fall never actually goes on forever: the kill plane has to sit
    // above that floor or the player just lands on nothing and stands there.
    const float sceneFloorY = scene.GetSceneAABBMin().y;
    const float killPlaneY =
        std::max(index_.lowestGroundY + CurrentLevel().KillPlaneOffset, sceneFloorY + 2.0f);
    hazards_.Bind(index_, killPlaneY);
    enemies_.Bind(index_);

    const glm::vec3 spawn = SpawnFootPosition();
    // The start pad points the player down the level, which runs west to east (+X).
    spawnYaw_ = kPi * 0.5f;
    interactables_.SetSpawn(spawn, spawnYaw_);
    interactables_.Bind(index_, mechanisms_);

    player_.Create(physics, spawn, spawnYaw_);
    player_.SetGodMode(godMode_);
    previousFoot_ = spawn;
    rig_.OnSceneLoaded(scene);
    if (rig_.HasInjectedAssets())
    {
        // One parked instance per robot the level can free, so a rescue never has to
        // build a node tree in the middle of a frame.
        rescueRigs_.Create(scene, rig_.Asset(), rig_.BaseInstanceDesc(), rig_.TintMaterialIds(),
                           static_cast<int>(index_.RescueTotal()));
    }
    camera_.Snap(spawn, spawnYaw_);

    levelCameras_.Bind(scene, CurrentLevel().TitleCamera, CurrentLevel().IntroCameraPath);
    // The fly-through runs for exactly as long as the level's own camera track.
    flow_.Reset(levelCameras_.HasIntro(), levelCameras_.IntroDuration(), config_.Move.DeathFadeSeconds,
                kGoalHoldSeconds);
    FRunStats& stats = flow_.Stats();
    stats.coinsTotal = collectibles_.CoinsTotal();
    stats.puzzlesTotal = collectibles_.PuzzlesTotal();
    stats.rescuedTotal = interactables_.RescueTotal();

    levelTime_ = 0.0f;
    toast_.clear();
    toastTimer_ = 0.0f;
    sceneReady_ = true;
    SPDLOG_INFO("[NextAstrobot] level ready: {} coins, {} puzzles, {} mechanisms, {} enemies, {} rescues",
                index_.coins.size(), index_.puzzles.size(), index_.mechanisms.size(), index_.enemies.size(),
                index_.RescueTotal());
}

void NextAstrobotGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    player_.Destroy();
    // Only runtime pointers and body ids: injected rig assets belong to the scene being built.
    mechanisms_.Unbind();
    collectibles_.Unbind();
    hazards_.Unbind();
    enemies_.Unbind();
    interactables_.Unbind();
    rescueRigs_.Destroy();
    rig_.OnSceneUnloaded();
    levelCameras_.Clear();
    index_ = FLevelIndex{};
}

void NextAstrobotGameInstance::OnDestroy()
{
    player_.Destroy();
    mechanisms_.Unbind();
}

void NextAstrobotGameInstance::RestartLevel()
{
    if (!sceneReady_)
    {
        return;
    }
    GetEngine().RequestLoadScene({.filename = CurrentLevel().Scene});
}

FPlayerInput NextAstrobotGameInstance::CollectInput() const
{
    FPlayerInput input;
    glm::vec2 move(0.0f);
    move.y += keyMove_[0] ? 1.0f : 0.0f;
    move.y -= keyMove_[1] ? 1.0f : 0.0f;
    move.x -= keyMove_[2] ? 1.0f : 0.0f;
    move.x += keyMove_[3] ? 1.0f : 0.0f;
    move += stickMove_;
    const float length = glm::length(move);
    if (length > 1.0f)
    {
        move /= length;
    }
    input.move = move;
    input.jumpHeld = jumpHeld_;
    input.jumpPressed = jumpPressed_;
    input.punchPressed = punchPressed_;
    return input;
}

void NextAstrobotGameInstance::ApplyRespawn()
{
    const glm::vec3 respawn = interactables_.RespawnPosition();
    const float yaw = interactables_.ActiveCheckpoint() >= 0 ? spawnYaw_ : interactables_.RespawnYaw();
    player_.Respawn(respawn, yaw);
    previousFoot_ = respawn;
    camera_.Snap(respawn, yaw);
    enemies_.ResetAll();
    flow_.NotifyRespawned();
}

void NextAstrobotGameInstance::TickWorld(float deltaSeconds)
{
    levelTime_ += deltaSeconds;
    NextEngine& engine = GetEngine();
    Assets::Scene& scene = engine.GetScene();
    const FPlayerInput input = CollectInput();

    // 1. Mechanisms move first, so the character steps onto this frame's platform
    //    position; they read last frame's foot position by design.
    mechanisms_.Update(levelTime_, deltaSeconds, previousFoot_, player_.IsOnGround(), input.jumpPressed);
    const FMechanismEffects& effects = mechanisms_.Effects();
    if (effects.surfaceVelocity != glm::vec3(0.0f))
    {
        player_.AddSurfaceVelocity(effects.surfaceVelocity);
    }
    if (effects.wind != glm::vec3(0.0f))
    {
        player_.AddWind(effects.wind);
    }
    if (effects.liftSpeed > 0.0f && !player_.IsZipping())
    {
        player_.ApplyLift(effects.liftSpeed);
    }
    if (effects.launch && !player_.IsZipping())
    {
        player_.Launch(effects.launchHeight);
        Audio::PlayLaunch(engine);
    }
    if (effects.zipAvailable && !player_.IsZipping())
    {
        player_.BeginZip(effects.zipFrom, effects.zipTo, effects.zipSpeed);
    }

    // 2. The character.
    const float fallSpeedBefore = player_.Velocity().y;
    const bool wasPunching = player_.PunchActive();
    player_.Update(input, camera_.Forward(), camera_.Right(), deltaSeconds);
    if (player_.PunchStarted())
    {
        Audio::PlayPunch(engine);
    }
    if (!wasPunching && player_.State() == ELocomotion::Jump && input.jumpPressed)
    {
        Audio::PlayJump(engine);
    }
    const glm::vec3 foot = player_.Position();
    if (player_.IsZipping())
    {
        // Keep the trolley over the rider.
        const glm::vec3 fromRider = foot - effects.zipFrom;
        const glm::vec3 span = effects.zipTo - effects.zipFrom;
        const float spanLengthSq = glm::dot(span, span);
        if (spanLengthSq > 0.01f)
        {
            mechanisms_.SetZipProgress(glm::dot(fromRider, span) / spanLengthSq);
        }
    }

    // 3. Pickups.
    const FPickupEvent pickup = collectibles_.Update(levelTime_, deltaSeconds, foot);
    FRunStats& stats = flow_.Stats();
    if (pickup.coins > 0 || pickup.puzzles > 0)
    {
        stats.coins += pickup.coins;
        stats.puzzles += pickup.puzzles;
        stats.gems += pickup.gems;
        Audio::PlayCoin(engine);
    }
    if (pickup.star)
    {
        flow_.NotifyGoalReached();
        Audio::PlayGoal(engine);
    }

    // 4. Hazards, then enemies, then interactables.
    if (!player_.IsDead())
    {
        const std::string hazard = hazards_.Check(foot, player_.ControllerHeight());
        if (!hazard.empty())
        {
            player_.Kill(hazard);
        }
    }

    const FEnemyOutcome enemyOutcome =
        enemies_.Update(levelTime_, deltaSeconds, foot, player_.ControllerHeight(), fallSpeedBefore,
                        player_.PunchActive(), foot + glm::vec3(0.0f, player_.ControllerHeight() * 0.5f, 0.0f),
                        player_.Facing(), config_.Move.PunchRange, config_.Move.PunchArcDegrees);
    if (enemyOutcome.stomped)
    {
        player_.Bounce(config_.Move.StompBounceSpeed);
        Audio::PlayPunch(engine);
    }
    if (enemyOutcome.killedPlayer && !player_.IsDead())
    {
        player_.Kill("enemy");
    }

    const FInteractionEvent interaction =
        interactables_.Update(deltaSeconds, foot, player_.PunchStarted(),
                              foot + glm::vec3(0.0f, player_.ControllerHeight() * 0.5f, 0.0f), player_.Facing(),
                              config_.Move.PunchRange, config_.Move.PunchArcDegrees);
    if (interaction.rescued > 0)
    {
        stats.rescued += interaction.rescued;
        Audio::PlayRescue(engine);
    }
    for (const FRescueEvent& freed : interaction.freed)
    {
        // Step the robot clear of whoever let it out and turn it to face them; a caged one
        // cheers, a stranded one waves. Without the step a cage punched from directly on
        // top of it would stand the robot inside the player. The kit geometry it replaces
        // was hidden by InteractableSystem.
        glm::vec3 away(freed.position.x - foot.x, 0.0f, freed.position.z - foot.z);
        const float distance = glm::length(away);
        away = distance > 0.3f ? away / distance : player_.Facing();
        const glm::vec3 spot = freed.position + away * 0.8f;
        const glm::vec3 toPlayer = foot - spot;
        rescueRigs_.Place(spot, std::atan2(toPlayer.x, toPlayer.z), freed.fromCage ? "win" : "wave");
    }
    rescueRigs_.Update(deltaSeconds);
    if (interaction.coinsFromSmash > 0)
    {
        stats.coins += interaction.coinsFromSmash;
        Audio::PlayCoin(engine);
    }
    if (!interaction.toast.empty())
    {
        toast_ = interaction.toast;
        toastTimer_ = kToastSeconds;
    }
    if (interaction.goalReached)
    {
        flow_.NotifyGoalReached();
        Audio::PlayGoal(engine);
    }

    if (player_.IsDead() && flow_.State() == ELevelState::Playing)
    {
        flow_.NotifyDeath();
        Audio::PlayDeath(engine);
    }

    previousFoot_ = foot;
    scene.MarkTransformDirty();
}

void NextAstrobotGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_)
    {
        jumpPressed_ = false;
        punchPressed_ = false;
        anyKeyPressed_ = false;
        return;
    }

    const auto dt = static_cast<float>(std::min(deltaSeconds, 0.1) * std::max(timeScale_, 0.01f));

    if (anyKeyPressed_ && flow_.RequestSkip())
    {
        camera_.Snap(player_.Position(), player_.Yaw());
    }
    anyKeyPressed_ = false;

    flow_.Update(dt);
    toastTimer_ = std::max(0.0f, toastTimer_ - dt);

    if (NextPhysics* physics = GetEngine().GetPhysicsEngine())
    {
        physics->SetPaused(!flow_.WorldRunning());
    }

    if (flow_.WorldRunning())
    {
        TickWorld(dt);
    }
    if (flow_.State() == ELevelState::Dead && flow_.StateElapsed() >= config_.Move.DeathFadeSeconds)
    {
        ApplyRespawn();
    }

    rig_.Update(player_.Position(), player_.Yaw(), player_.State(), player_.HorizontalSpeed(),
                config_.Move.RunSpeed, dt);
    // With the spring arm fully collapsed the lens is inside the character; drawing the
    // rig then is just a wall of white plastic.
    rig_.SetVisible(camera_.BoomDistance() > config_.Camera.SpringArmHideRigDistance);
    if (flow_.State() == ELevelState::Title || flow_.State() == ELevelState::Intro)
    {
        // Keep the chase camera parked on the character so ejecting from the opening shot
        // lands on a sane view instead of wherever the fly-through ended.
        camera_.Snap(player_.Position(), player_.Yaw());
        if (flow_.State() == ELevelState::Intro)
        {
            levelCameras_.AdvanceIntro(GetEngine().GetScene(), flow_.StateElapsed());
        }
    }
    else
    {
        camera_.AddManualYaw(stickCameraYaw_ * config_.Camera.ManualYawRate * dt);
        // The scene is what lets the spring arm shorten against a pillar or a wall.
        camera_.Update(player_.Position(), player_.HorizontalVelocity(), dt, &GetEngine().GetScene());
    }

    jumpPressed_ = false;
    punchPressed_ = false;
}

bool NextAstrobotGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (!sceneReady_)
    {
        return false;
    }
    // Title and intro are framed by the level's own gk_camera markers, so a new level
    // brings its own opening shot instead of inheriting one written in code.
    if (flow_.State() == ELevelState::Title && levelCameras_.HasTitle())
    {
        levelCameras_.FillTitle(outRenderCamera);
        return true;
    }
    if (flow_.State() == ELevelState::Intro && levelCameras_.HasIntro())
    {
        levelCameras_.FillIntro(outRenderCamera);
        return true;
    }
    camera_.Fill(outRenderCamera);
    return true;
}

bool NextAstrobotGameInstance::OnRenderUI()
{
    if (!sceneReady_)
    {
        return false;
    }
    FHudContext context;
    context.state = flow_.State();
    context.stats = &flow_.Stats();
    context.levelName = CurrentLevel().DisplayName;
    context.toast = toast_;
    context.toastAlpha = std::clamp(toastTimer_ / std::max(kToastSeconds * 0.4f, 0.01f), 0.0f, 1.0f);
    context.deathFade = flow_.DeathFade01();
    context.showDebug = showDebugPanel_;
    context.locomotion = LocomotionName(player_.State());
    context.playerX = player_.Position().x;
    context.playerY = player_.Position().y;
    context.playerZ = player_.Position().z;
    context.onGround = player_.IsOnGround();
    context.checkpoint = interactables_.ActiveCheckpoint();
    context.mechanismCount = static_cast<int>(mechanisms_.Count());
    context.enemiesAlive = enemies_.AliveCount();
    return AstroHud::Draw(context);
}

bool NextAstrobotGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP)
    {
        return false;
    }
    const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
    if (pressed && !event.key.repeat)
    {
        anyKeyPressed_ = true;
    }

    switch (event.key.key)
    {
    case SDLK_W: case SDLK_UP: keyMove_[0] = pressed; return true;
    case SDLK_S: case SDLK_DOWN: keyMove_[1] = pressed; return true;
    case SDLK_A: case SDLK_LEFT: keyMove_[2] = pressed; return true;
    case SDLK_D: case SDLK_RIGHT: keyMove_[3] = pressed; return true;
    case SDLK_SPACE:
        jumpHeld_ = pressed;
        if (pressed && !event.key.repeat)
        {
            jumpPressed_ = true;
        }
        return true;
    case SDLK_X:
        if (pressed && !event.key.repeat)
        {
            punchPressed_ = true;
        }
        return true;
    case SDLK_R:
        if (pressed && !event.key.repeat && flow_.State() == ELevelState::Result)
        {
            RestartLevel();
        }
        return true;
    case SDLK_ESCAPE:
        if (pressed && !event.key.repeat)
        {
            flow_.RequestPause(flow_.State() != ELevelState::Paused);
        }
        return true;
    case SDLK_F5:
        if (pressed && !event.key.repeat)
        {
            showDebugPanel_ = !showDebugPanel_;
        }
        return true;
    default:
        return false;
    }
}

bool NextAstrobotGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
    {
        punchPressed_ = true;
        anyKeyPressed_ = true;
        return true;
    }
    if (event.button.button == SDL_BUTTON_RIGHT)
    {
        mouseLookActive_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        mousePositionValid_ = false;
        return true;
    }
    return false;
}

bool NextAstrobotGameInstance::OnCursorPosition(double xpos, double ypos)
{
    const glm::dvec2 position(xpos, ypos);
    if (mouseLookActive_ && mousePositionValid_)
    {
        // Dragging with the right button orbits the camera; 400 px is a full radian.
        camera_.AddManualYaw(static_cast<float>((position.x - mousePosition_.x) * -0.0025));
    }
    mousePosition_ = position;
    mousePositionValid_ = true;
    return mouseLookActive_;
}

bool NextAstrobotGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX,
                                              int16_t /*rightStickY*/, int16_t /*leftTrigger*/,
                                              int16_t /*rightTrigger*/)
{
    stickMove_.x = StickAxis(leftStickX);
    stickMove_.y = -StickAxis(leftStickY); // SDL reports "up" as negative
    stickCameraYaw_ = StickAxis(rightStickX);
    return true;
}

void NextAstrobotGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    using Value = Runtime::Agent::FAgentQueryValue;
    reg.Add("state", [this]() -> Value { return std::string(LevelStateName(flow_.State())); });
    reg.Add("locomotion", [this]() -> Value { return std::string(LocomotionName(player_.State())); });
    reg.Add("clip", [this]() -> Value { return rig_.CurrentClip(); });
    reg.Add("playerX", [this]() -> Value { return static_cast<double>(player_.Position().x); });
    reg.Add("playerY", [this]() -> Value { return static_cast<double>(player_.Position().y); });
    reg.Add("playerZ", [this]() -> Value { return static_cast<double>(player_.Position().z); });
    reg.Add("onGround", [this]() -> Value { return player_.IsOnGround(); });
    // A punch has a cooldown, so a script that fires two in a row has to wait this out.
    reg.Add("punching", [this]() -> Value { return player_.PunchActive(); });
    reg.Add("yaw", [this]() -> Value { return static_cast<double>(player_.Yaw()); });
    reg.Add("coins", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().coins); });
    reg.Add("coinsTotal", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().coinsTotal); });
    reg.Add("puzzles", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().puzzles); });
    reg.Add("puzzlesTotal", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().puzzlesTotal); });
    reg.Add("rescued", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().rescued); });
    reg.Add("rescuedTotal", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().rescuedTotal); });
    reg.Add("deaths", [this]() -> Value { return static_cast<int64_t>(flow_.Stats().deaths); });
    reg.Add("checkpoint", [this]() -> Value { return static_cast<int64_t>(interactables_.ActiveCheckpoint()); });
    reg.Add("enemiesAlive", [this]() -> Value { return static_cast<int64_t>(enemies_.AliveCount()); });
    reg.Add("index.coins", [this]() -> Value { return static_cast<int64_t>(index_.coins.size()); });
    reg.Add("index.puzzles", [this]() -> Value { return static_cast<int64_t>(index_.puzzles.size()); });
    reg.Add("index.mechanisms", [this]() -> Value { return static_cast<int64_t>(index_.mechanisms.size()); });
    reg.Add("index.enemies", [this]() -> Value { return static_cast<int64_t>(index_.enemies.size()); });
    reg.Add("index.hazards", [this]() -> Value { return static_cast<int64_t>(index_.hazards.size()); });
    reg.Add("index.warnings", [this]() -> Value { return static_cast<int64_t>(indexWarnings_.size()); });
    reg.Add("killPlaneY", [this]() -> Value { return static_cast<double>(hazards_.KillPlaneY()); });

    reg.Add("springArm", [this]() -> Value { return static_cast<double>(camera_.SpringArm01()); });
    reg.Add("camX", [this]() -> Value { return static_cast<double>(camera_.Position().x); });
    reg.Add("camY", [this]() -> Value { return static_cast<double>(camera_.Position().y); });
    reg.Add("camZ", [this]() -> Value { return static_cast<double>(camera_.Position().z); });
    reg.Add("rescueRigs", [this]() -> Value { return static_cast<int64_t>(rescueRigs_.PlacedCount()); });

    // game.mech.<name>.t - a mechanism's normalized phase, so a script can assert that a
    // platform really is moving instead of eyeballing a screenshot.
    for (const char* name : {"moving", "pendulum", "seesaw", "spin", "crumble", "roller", "conveyor", "zipline",
                             "spring", "bounce", "cage", "gate_1", "gate_2", "button_1", "lever_2",
                             "spikeball", "laser", "fan", "fountain", "windmill", "chest", "flag_1", "flag_2"})
    {
        reg.Add(fmt::format("mech.{}.t", name),
                [this, key = std::string(name)]() -> Value
                {
                    float phase = 0.0f;
                    mechanisms_.QueryPhase(key, phase);
                    return static_cast<double>(phase);
                });
    }
}
