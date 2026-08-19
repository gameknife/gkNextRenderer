#include "GeoWalkGameInstance.hpp"

#include "GeoWalkConfig.hpp"

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

using namespace GeoWalk;

namespace
{
    constexpr float kPitchLimit = 1.45f; // just under 90 degrees
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<GeoWalkGameInstance>(config, options, engine);
}

GeoWalkGameInstance::GeoWalkGameInstance(Vulkan::WindowConfig& config,
                                         Runtime::Config::Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "GeoWalk", 1920, 1080, false);
}

void GeoWalkGameInstance::OnInit()
{
    // The nav grid raycasts the CPU BVH, so the scene mesh has to keep its CPU
    // copy. Without this the grid builds empty and nothing can be spawned.
    GOption->KeepCPUMeshData = true;

    tiles_ = DiscoverGeoTiles();
    if (tiles_.empty())
    {
        SPDLOG_ERROR("GeoWalk: no generated tiles found. Generate one with "
                     "`gnb geo make --name <tile> --at <lat>,<lon>`");
        return;
    }
    for (FGeoTile& tile : tiles_)
    {
        if (!LoadTilePois(tile))
        {
            SPDLOG_WARN("GeoWalk: {}", tile.loadError);
        }
    }

    // An explicit --scene wins, so `gnb shot --scene <tile>.scad` and the agent
    // scripts address a specific tile the same way every other application does.
    int index = 0;
    if (!GOption->SceneName.empty())
    {
        const int matched = FindTileByScene(tiles_, GOption->SceneName);
        if (matched >= 0)
        {
            index = matched;
        }
        else
        {
            SPDLOG_WARN("GeoWalk: '{}' is not a generated geo tile — loading '{}' instead",
                        GOption->SceneName, tiles_[0].name);
        }
    }
    RequestTile(index);
}

void GeoWalkGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    // GeoWalk is a ray-traced city walk; keep the engine's lighter default
    // available through user settings and explicit command-line overrides.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
}

void GeoWalkGameInstance::RequestTile(int index)
{
    if (index < 0 || index >= static_cast<int>(tiles_.size()))
    {
        return;
    }
    pendingTile_ = index;
    sceneReady_ = false;
    walkerSpawned_ = false;
    terrain_ = nullptr;
    focusInitialized_ = false;
    ui_.ClearSelection();
    SPDLOG_INFO("GeoWalk: loading tile '{}' ({})", tiles_[static_cast<size_t>(index)].name,
                tiles_[static_cast<size_t>(index)].scenePath);
    GetEngine().RequestLoadScene({.filename = tiles_[static_cast<size_t>(index)].scenePath});
}

void GeoWalkGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                             std::vector<Assets::Model>& models,
                                             std::vector<Assets::FMaterial>& materials,
                                             std::vector<Assets::LightObject>& /*lights*/,
                                             std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    walker_.InjectAssets(models, materials);
}

void GeoWalkGameInstance::OnSceneLoaded()
{
    if (pendingTile_ >= 0)
    {
        activeTile_ = pendingTile_;
        pendingTile_ = -1;
    }
    sceneReady_ = true;
    walkerSpawned_ = false;
    terrain_ = nullptr;
    // The terrain component arrives with the scene, but the CPU acceleration
    // structure the nav grid needs is not necessarily ready on this callback.
    // Resolution and spawning are deferred to the first ticks.
}

void GeoWalkGameInstance::TryResolveTerrain()
{
    if (terrain_ != nullptr)
    {
        return;
    }
    for (const std::shared_ptr<Assets::Node>& node : GetEngine().GetScene().Nodes())
    {
        if (auto* component = node->GetComponent<Runtime::TerrainComponent>())
        {
            if (component->HasData())
            {
                terrain_ = component;
                break;
            }
        }
    }
    if (terrain_ == nullptr)
    {
        return;
    }
    if (FGeoTile* tile = ActiveTile())
    {
        poiLayer_.OnTerrainReady(tile->pois, *terrain_);
        SPDLOG_INFO("GeoWalk: terrain {}x{} cells, {} of {} places anchored",
                    terrain_->GetCellsX(), terrain_->GetCellsY(), poiLayer_.GroundedCount(),
                    tile->pois.size());
    }
}

void GeoWalkGameInstance::SpawnWalker()
{
    if (walkerSpawned_ || terrain_ == nullptr)
    {
        return;
    }
    walkerSpawned_ = walker_.OnSceneLoaded(GetEngine().GetScene(), GetEngine(), terrain_);
    if (!walkerSpawned_)
    {
        return;
    }
    smoothedFocus_ = walker_.Position();
    focusInitialized_ = true;
    resolvedFollowDistance_ = followDistance_;
    freeCameraPosition_ = walker_.Position() + glm::vec3(0.0f, 45.0f, 45.0f);

    // Open facing the most prominent place in range, so the first frame says
    // which city this is instead of pointing at whichever compass direction yaw
    // 0 happens to be. Only the heading is taken from it: pitching up at a
    // 400 m tower would swing the boom down into the pavement.
    pitch_ = Config::kSpawnPitch;
    if (const FGeoTile* tile = ActiveTile())
    {
        for (const FGeoPoi& poi : tile->pois) // rank-descending
        {
            if (!poi.grounded)
            {
                continue;
            }
            const glm::vec2 delta = poi.position - glm::vec2(smoothedFocus_.x, smoothedFocus_.z);
            if (glm::length(delta) > Config::kLabelMaxDistance || glm::length(delta) < 1.0f)
            {
                continue;
            }
            yaw_ = std::atan2(delta.x, -delta.y);
            break;
        }
    }
}

void GeoWalkGameInstance::OnSceneUnloaded()
{
    walker_.OnSceneUnloaded();
    sceneReady_ = false;
    walkerSpawned_ = false;
    terrain_ = nullptr;
}

void GeoWalkGameInstance::OnDestroy()
{
    walker_.OnSceneUnloaded();
}

const FGeoTile* GeoWalkGameInstance::ActiveTile() const
{
    if (activeTile_ < 0 || activeTile_ >= static_cast<int>(tiles_.size()))
    {
        return nullptr;
    }
    return &tiles_[static_cast<size_t>(activeTile_)];
}

FGeoTile* GeoWalkGameInstance::ActiveTile()
{
    return const_cast<FGeoTile*>(const_cast<const GeoWalkGameInstance*>(this)->ActiveTile());
}

glm::vec3 GeoWalkGameInstance::ViewForward() const
{
    return glm::normalize(glm::vec3(std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_),
                                    -std::cos(pitch_) * std::cos(yaw_)));
}

glm::vec3 GeoWalkGameInstance::ViewRight() const
{
    return glm::normalize(glm::cross(ViewForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 GeoWalkGameInstance::CameraPosition() const
{
    if (cameraMode_ == ECameraMode::Free)
    {
        return freeCameraPosition_;
    }
    const glm::vec3 focus = smoothedFocus_ + glm::vec3(0.0f, Config::kFollowHeight, 0.0f);
    return focus - ViewForward() * resolvedFollowDistance_;
}

float GeoWalkGameInstance::ResolveFollowDistance(const glm::vec3& focus, float desired) const
{
    // Cast from the character out along the boom: anything hit is between the
    // camera and the character. The origin is pushed clear of the character's
    // own rig first — it is ordinary scene geometry, and a ray started at chest
    // height hits its own back immediately, collapsing the boom every frame.
    const glm::vec3 direction = -ViewForward();
    const glm::vec3 origin = focus + direction * Config::kCameraCollisionStart;
    const Assets::RayCastResult hit =
        GetEngine().GetScene().GetCPUAccelerationStructure().RayCastInCPU(origin, direction);
    if (hit.Hit == 0 || hit.T <= 0.0f)
    {
        return desired;
    }
    const float blocked = Config::kCameraCollisionStart + hit.T;
    if (blocked >= desired)
    {
        return desired;
    }
    return std::max(Config::kFollowMinDistance, blocked - Config::kCameraCollisionPadding);
}

glm::mat4 GeoWalkGameInstance::ViewMatrix() const
{
    const glm::vec3 eye = CameraPosition();
    return glm::lookAt(eye, eye + ViewForward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

bool GeoWalkGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = ViewMatrix();
    outRenderCamera.FieldOfView = Config::kFov;
    outRenderCamera.NearPlane = Config::kNearPlane;
    outRenderCamera.FarPlane = Config::kFarPlane;
    return true;
}

void GeoWalkGameInstance::UpdatePlayerIntent()
{
    if (walker_.Mode() != EWalkMode::Player)
    {
        return;
    }
    // Movement is camera-relative and flattened onto the ground plane, so
    // looking up does not slow the character down.
    glm::vec3 forward = ViewForward();
    forward.y = 0.0f;
    glm::vec3 right = ViewRight();
    right.y = 0.0f;
    if (glm::length(forward) > 0.001f)
    {
        forward = glm::normalize(forward);
    }
    if (glm::length(right) > 0.001f)
    {
        right = glm::normalize(right);
    }

    glm::vec3 intent(0.0f);
    intent += forward * ((keyForward_ ? 1.0f : 0.0f) - (keyBack_ ? 1.0f : 0.0f));
    intent += right * ((keyRight_ ? 1.0f : 0.0f) - (keyLeft_ ? 1.0f : 0.0f));
    if (glm::length(intent) > 0.001f)
    {
        intent = glm::normalize(intent);
    }
    walker_.SetPlayerIntent(intent, keySprint_, false);
}

void GeoWalkGameInstance::UpdateCamera(float deltaSeconds)
{
    if (cameraMode_ == ECameraMode::Free)
    {
        const float speed = Config::kFreeFlySpeed * (keySprint_ ? 4.0f : 1.0f);
        glm::vec3 move(0.0f);
        move += ViewForward() * ((keyForward_ ? 1.0f : 0.0f) - (keyBack_ ? 1.0f : 0.0f));
        move += ViewRight() * ((keyRight_ ? 1.0f : 0.0f) - (keyLeft_ ? 1.0f : 0.0f));
        move += glm::vec3(0.0f, 1.0f, 0.0f) * ((keyUp_ ? 1.0f : 0.0f) - (keyDown_ ? 1.0f : 0.0f));
        if (glm::length(move) > 0.001f)
        {
            freeCameraPosition_ += glm::normalize(move) * speed * deltaSeconds;
        }
        return;
    }

    if (!walkerSpawned_)
    {
        return;
    }
    const glm::vec3 target = walker_.Position();
    if (!focusInitialized_)
    {
        smoothedFocus_ = target;
        focusInitialized_ = true;
        return;
    }
    const float blend = 1.0f - std::exp(-Config::kCameraSharpness * deltaSeconds);
    smoothedFocus_ += (target - smoothedFocus_) * blend;

    const glm::vec3 focus = smoothedFocus_ + glm::vec3(0.0f, Config::kFollowHeight, 0.0f);
    const float allowed = ResolveFollowDistance(focus, followDistance_);
    // Snap in immediately when a wall appears (otherwise the camera spends the
    // blend inside it) and ease back out when it clears.
    resolvedFollowDistance_ = allowed < resolvedFollowDistance_
                                  ? allowed
                                  : resolvedFollowDistance_ +
                                        (allowed - resolvedFollowDistance_) * blend;
}

void GeoWalkGameInstance::OnTick(double deltaSeconds)
{
    frameMs_ = static_cast<float>(deltaSeconds * 1000.0);
    if (!sceneReady_)
    {
        return;
    }
    TryResolveTerrain();
    SpawnWalker();

    const float dt = static_cast<float>(deltaSeconds);
    UpdatePlayerIntent();
    if (walkerSpawned_)
    {
        walker_.Tick(dt, GetEngine().GetScene());
    }
    UpdateCamera(dt);

    if (const FGeoTile* tile = ActiveTile())
    {
        poiLayer_.Update(tile->pois, CameraPosition());
    }
}

void GeoWalkGameInstance::LookAtPoi(const FGeoPoi& poi)
{
    // Aim a third of the way up a building rather than at its roof: from a
    // pavement across the street, framing the roof of a 400 m tower points the
    // camera at empty sky.
    const glm::vec3 aim(poi.position.x, poi.groundY + poi.height * 0.33f, poi.position.y);
    const glm::vec3 from = cameraMode_ == ECameraMode::Free ? freeCameraPosition_ : smoothedFocus_;
    const glm::vec3 delta = aim - from;
    const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
    if (horizontal < 0.01f && std::abs(delta.y) < 0.01f)
    {
        return;
    }
    yaw_ = std::atan2(delta.x, -delta.z);
    pitch_ = std::clamp(std::atan2(delta.y, horizontal), -kPitchLimit, kPitchLimit);
}

void GeoWalkGameInstance::ApplyUIRequest(const FGeoWalkUIRequest& request)
{
    // Both of these throw the scene away, so nothing else in the request can
    // still be meaningful this frame.
    if (request.loadTileIndex >= 0)
    {
        RequestTile(request.loadTileIndex);
        return;
    }
    if (request.respawn)
    {
        // The rig's models are injected during scene rebuild, so putting the
        // character back means reloading the tile rather than re-acquiring a
        // pool slot whose geometry no longer exists.
        RequestTile(activeTile_);
        return;
    }
    if (request.toggleMode)
    {
        walker_.ToggleMode();
    }

    const FGeoTile* tile = ActiveTile();
    if (tile == nullptr)
    {
        return;
    }
    if (request.lookAtPoiIndex >= 0 && request.lookAtPoiIndex < static_cast<int>(tile->pois.size()))
    {
        LookAtPoi(tile->pois[static_cast<size_t>(request.lookAtPoiIndex)]);
    }
    if (request.walkToPoiIndex >= 0 && request.walkToPoiIndex < static_cast<int>(tile->pois.size()))
    {
        const FGeoPoi& poi = tile->pois[static_cast<size_t>(request.walkToPoiIndex)];
        walker_.WalkTo(glm::vec3(poi.position.x, poi.groundY, poi.position.y));
    }
}

bool GeoWalkGameInstance::OnRenderUI()
{
    if (!sceneReady_)
    {
        return true;
    }
    const FGeoTile* tile = ActiveTile();
    if (tile != nullptr)
    {
        poiLayer_.Draw(*this, tile->pois, CameraPosition());
    }

    const glm::vec3 cameraPosition = CameraPosition();
    FGeoWalkUIContext context;
    context.tiles = &tiles_;
    context.activeTile = activeTile_;
    context.walker = &walker_;
    context.cameraPosition = &cameraPosition;
    context.followCamera = cameraMode_ == ECameraMode::Follow;
    context.frameMs = frameMs_;
    ApplyUIRequest(ui_.Draw(context, poiLayer_));
    return true;
}

bool GeoWalkGameInstance::OnKey(SDL_Event& event)
{
    const bool down = event.type == SDL_EVENT_KEY_DOWN;
    switch (event.key.key)
    {
    case SDLK_W: keyForward_ = down; return true;
    case SDLK_S: keyBack_ = down; return true;
    case SDLK_A: keyLeft_ = down; return true;
    case SDLK_D: keyRight_ = down; return true;
    case SDLK_Q: keyDown_ = down; return true;
    case SDLK_E: keyUp_ = down; return true;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: keySprint_ = down; return true;
    case SDLK_SPACE:
        if (down && walker_.Mode() == EWalkMode::Player)
        {
            walker_.SetPlayerIntent(glm::vec3(0.0f), keySprint_, true);
        }
        return true;
    case SDLK_F:
        if (down)
        {
            walker_.ToggleMode();
            // Taking control only makes sense from behind the character.
            if (walker_.Mode() == EWalkMode::Player)
            {
                cameraMode_ = ECameraMode::Follow;
            }
        }
        return true;
    case SDLK_C:
        if (down)
        {
            cameraMode_ = cameraMode_ == ECameraMode::Follow ? ECameraMode::Free
                                                             : ECameraMode::Follow;
            if (cameraMode_ == ECameraMode::Free)
            {
                freeCameraPosition_ = CameraPosition();
            }
        }
        return true;
    case SDLK_V:
        if (down)
        {
            // Snap to a view of the whole tile. Getting here by flying is
            // hopeless inside a CBD: the free camera starts between two towers
            // and every direction is a facade.
            cameraMode_ = ECameraMode::Free;
            const glm::vec3 anchor = walkerSpawned_ ? walker_.Position() : glm::vec3(0.0f);
            freeCameraPosition_ = glm::vec3(anchor.x, anchor.y + Config::kOverviewHeight,
                                            anchor.z + Config::kOverviewSetback);
            yaw_ = 0.0f;
            pitch_ = Config::kOverviewPitch;
        }
        return true;
    case SDLK_L:
        if (down)
        {
            poiLayer_.ShowLabels() = !poiLayer_.ShowLabels();
        }
        return true;
    case SDLK_TAB:
        if (down)
        {
            ui_.Visible() = !ui_.Visible();
        }
        return true;
    default:
        break;
    }
    return false;
}

bool GeoWalkGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (!mouseLookActive_)
    {
        mousePos_ = {xpos, ypos};
        return false;
    }
    if (resetMouse_)
    {
        mousePos_ = {xpos, ypos};
        resetMouse_ = false;
        return true;
    }
    const double dx = xpos - mousePos_.x;
    const double dy = ypos - mousePos_.y;
    mousePos_ = {xpos, ypos};
    yaw_ += static_cast<float>(dx) * Config::kMouseSensitivity;
    pitch_ = std::clamp(pitch_ - static_cast<float>(dy) * Config::kMouseSensitivity,
                        -kPitchLimit, kPitchLimit);
    return true;
}

bool GeoWalkGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.button.button != SDL_BUTTON_RIGHT)
    {
        return false;
    }
    mouseLookActive_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    resetMouse_ = true;
    return true;
}

bool GeoWalkGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    if (cameraMode_ != ECameraMode::Follow)
    {
        return false;
    }
    followDistance_ = std::clamp(followDistance_ - static_cast<float>(yoffset) * 1.5f,
                                 Config::kFollowMinDistance, Config::kFollowMaxDistance);
    return true;
}

void GeoWalkGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& registry)
{
    registry.Add("geo.tile", [this]()
    {
        const FGeoTile* tile = ActiveTile();
        return tile != nullptr ? tile->name : std::string("none");
    });
    registry.Add("geo.poiCount", [this]()
    {
        const FGeoTile* tile = ActiveTile();
        return static_cast<int64_t>(tile != nullptr ? tile->pois.size() : 0);
    });
    registry.Add("geo.poiAnchored", [this]()
    {
        return static_cast<int64_t>(poiLayer_.GroundedCount());
    });
    registry.Add("geo.labelsVisible", [this]()
    {
        return static_cast<int64_t>(poiLayer_.VisibleCount());
    });
    registry.Add("geo.walkerSpawned", [this]() { return walkerSpawned_; });
    registry.Add("geo.walkerMode", [this]()
    {
        return std::string(walker_.Mode() == EWalkMode::Roam ? "roam" : "player");
    });
    registry.Add("geo.walkerSpeed", [this]() { return static_cast<double>(walker_.Speed()); });
    registry.Add("geo.walkerX", [this]() { return static_cast<double>(walker_.Position().x); });
    registry.Add("geo.walkerY", [this]() { return static_cast<double>(walker_.Position().y); });
    registry.Add("geo.walkerZ", [this]() { return static_cast<double>(walker_.Position().z); });
    registry.Add("geo.navRebuilds", [this]()
    {
        return static_cast<int64_t>(walker_.NavRebuildCount());
    });
}
