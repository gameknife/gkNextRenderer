#include "NextWorldTravelGameInstance.hpp"

#include "NextWorldTravelConfig.hpp"

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
#include "Engine/Runtime/ScreenShotService.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

using namespace NextWorldTravel;

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<NextWorldTravelGameInstance>(config, options, engine);
}

NextWorldTravelGameInstance::NextWorldTravelGameInstance(Vulkan::WindowConfig& config,
                                         Runtime::Config::Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "NextWorldTravel", 1920, 1080, false);
}

void NextWorldTravelGameInstance::OnInit()
{
    // The nav grid raycasts the CPU BVH, so the scene mesh has to keep its CPU
    // copy. Without this the grid builds empty and nothing can be spawned.
    GOption->KeepCPUMeshData = true;

    tiles_ = DiscoverGeoTiles();
    if (tiles_.empty())
    {
        SPDLOG_ERROR("NextWorldTravel: no generated tiles found. Generate one with "
                     "`gnb geo make --name <tile> --at <lat>,<lon>`");
        return;
    }
    for (FGeoTile& tile : tiles_)
    {
        if (!LoadTilePois(tile))
        {
            SPDLOG_WARN("NextWorldTravel: {}", tile.loadError);
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
            SPDLOG_WARN("NextWorldTravel: '{}' is not a generated geo tile — loading '{}' instead",
                        GOption->SceneName, tiles_[0].name);
        }
    }
    RequestTile(index);
}

void NextWorldTravelGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    // NextWorldTravel is a ray-traced city walk; keep the engine's lighter default
    // available through user settings and explicit command-line overrides.
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
}

void NextWorldTravelGameInstance::RequestTile(int index)
{
    if (index < 0 || index >= static_cast<int>(tiles_.size()))
    {
        return;
    }
    pendingTile_ = index;
    sceneReady_ = false;
    walkerSpawned_ = false;
    terrain_ = nullptr;
    focusPoi_ = -1;
    tourActive_ = false;
    poiLayer_.SetHighlight(-1);
    ui_.ClearSelection();
    SPDLOG_INFO("NextWorldTravel: loading tile '{}' ({})", tiles_[static_cast<size_t>(index)].name,
                tiles_[static_cast<size_t>(index)].scenePath);
    GetEngine().RequestLoadScene({.filename = tiles_[static_cast<size_t>(index)].scenePath});
}

void NextWorldTravelGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                             std::vector<Assets::Model>& models,
                                             std::vector<Assets::FMaterial>& materials,
                                             std::vector<Assets::LightObject>& /*lights*/,
                                             std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    walker_.InjectAssets(models, materials);
}

void NextWorldTravelGameInstance::OnSceneLoaded()
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

void NextWorldTravelGameInstance::TryResolveTerrain()
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
        SPDLOG_INFO("NextWorldTravel: terrain {}x{} cells, {} of {} places anchored",
                    terrain_->GetCellsX(), terrain_->GetCellsY(), poiLayer_.GroundedCount(),
                    tile->pois.size());
    }
}

void NextWorldTravelGameInstance::SpawnWalker()
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

    // Prime the camera with the spawn position before deriving anything from
    // it: the follow rig smooths towards the character, and without this first
    // tick that smoothing starts at the tile origin.
    camera_.Tick(1.0f, MakeCameraWorld());

    // Open facing the most prominent place in range, so the first frame says
    // which city this is instead of pointing at whichever compass direction yaw
    // 0 happens to be. Only the heading is taken from it: pitching up at a
    // 400 m tower would swing the boom down into the pavement.
    if (const FGeoTile* tile = ActiveTile())
    {
        const glm::vec3 from = walker_.Position();
        for (const FGeoPoi& poi : tile->pois) // rank-descending
        {
            if (!poi.grounded)
            {
                continue;
            }
            const glm::vec2 delta = poi.position - glm::vec2(from.x, from.z);
            if (glm::length(delta) > Config::kLabelMaxDistance || glm::length(delta) < 1.0f)
            {
                continue;
            }
            camera_.SetHeading(std::atan2(delta.x, -delta.y), Config::kSpawnPitch);
            break;
        }
    }
    camera_.Tick(1.0f, MakeCameraWorld());
}

void NextWorldTravelGameInstance::OnSceneUnloaded()
{
    walker_.OnSceneUnloaded();
    sceneReady_ = false;
    walkerSpawned_ = false;
    terrain_ = nullptr;
}

void NextWorldTravelGameInstance::OnDestroy()
{
    walker_.OnSceneUnloaded();
}

const FGeoTile* NextWorldTravelGameInstance::ActiveTile() const
{
    if (activeTile_ < 0 || activeTile_ >= static_cast<int>(tiles_.size()))
    {
        return nullptr;
    }
    return &tiles_[static_cast<size_t>(activeTile_)];
}

FGeoTile* NextWorldTravelGameInstance::ActiveTile()
{
    return const_cast<FGeoTile*>(const_cast<const NextWorldTravelGameInstance*>(this)->ActiveTile());
}

FCameraWorld NextWorldTravelGameInstance::MakeCameraWorld() const
{
    FCameraWorld world;
    world.walkerValid = walkerSpawned_;
    world.walkerPosition = walkerSpawned_ ? walker_.Position() : glm::vec3(0.0f);
    world.terrain = terrain_;
    if (sceneReady_)
    {
        world.probe = [this](const glm::vec3& origin, const glm::vec3& direction) -> float
        {
            const Assets::RayCastResult hit =
                GetEngine().GetScene().GetCPUAccelerationStructure().RayCastInCPU(origin, direction);
            return (hit.Hit == 0 || hit.T <= 0.0f) ? -1.0f : hit.T;
        };
    }
    return world;
}

bool NextWorldTravelGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = camera_.ViewMatrix();
    outRenderCamera.FieldOfView = Config::kFov;
    outRenderCamera.NearPlane = Config::kNearPlane;
    outRenderCamera.FarPlane = Config::kFarPlane;
    return true;
}

void NextWorldTravelGameInstance::UpdatePlayerIntent()
{
    if (walker_.Mode() != EWalkMode::Player)
    {
        return;
    }
    // Only the walk camera drives the character: WASD belongs to the map while
    // the map is up, and to the orbit while a place is being looked at.
    if (camera_.Mode() != EViewMode::Walk)
    {
        walker_.SetPlayerIntent(glm::vec3(0.0f), false, false);
        return;
    }
    // Movement is camera-relative and flattened onto the ground plane, so
    // looking up does not slow the character down.
    glm::vec3 forward = camera_.Forward();
    forward.y = 0.0f;
    glm::vec3 right = camera_.Right();
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

std::vector<int> NextWorldTravelGameInstance::BuildBrowseOrder() const
{
    std::vector<int> order;
    const FGeoTile* tile = ActiveTile();
    if (tile == nullptr)
    {
        return order;
    }
    order.reserve(tile->pois.size());
    for (size_t i = 0; i < tile->pois.size(); ++i)
    {
        const FGeoPoi& poi = tile->pois[i];
        // The sidecar is already rank-descending, so honouring its order is all
        // it takes for a tour to open on the landmark and work down.
        if (poi.grounded && poiLayer_.IsCategoryEnabled(poi.category))
        {
            order.push_back(static_cast<int>(i));
        }
    }
    return order;
}

FFocusSubject NextWorldTravelGameInstance::MakeFocusSubject(const FGeoPoi& poi) const
{
    FFocusSubject subject;
    const float footprint = std::sqrt(std::max(poi.areaM2, 0.0f));
    // Look at the middle of the mass: aiming at the base leaves a tower in the
    // bottom third of the frame, aiming at the roof points at empty sky.
    const float lift = std::max(poi.height * Config::kFocusCenterHeightFactor,
                                Config::kFocusCenterMinLift);
    subject.center = glm::vec3(poi.position.x, poi.groundY + lift, poi.position.y);
    subject.radius = std::clamp(std::max(Config::kFocusMinRadius,
                                         poi.height * Config::kFocusRadiusFromHeight +
                                             footprint * Config::kFocusRadiusFromFootprint),
                                Config::kFocusMinRadius, Config::kFocusMaxRadius);
    subject.halfExtent = std::max(6.0f, footprint * 0.5f);
    subject.valid = true;
    return subject;
}

void NextWorldTravelGameInstance::SetViewMode(EViewMode mode)
{
    if (mode == camera_.Mode())
    {
        return;
    }
    if (mode == EViewMode::Focus && focusPoi_ < 0)
    {
        // Entering the orbit without a subject: open on whatever the tile is
        // known for, which is the first entry of the browse order.
        const std::vector<int> order = BuildBrowseOrder();
        if (order.empty())
        {
            SPDLOG_WARN("NextWorldTravel: no anchored place to focus on this tile");
            return;
        }
        FocusPoi(order.front());
        return;
    }

    camera_.SetMode(mode, MakeCameraWorld());
    // From above the tile is a map and every place gets a marker; on the street
    // and in an orbit the street rules apply, with the focused place pinned.
    poiLayer_.SetStyle(mode == EViewMode::Aerial ? ELabelStyle::Aerial : ELabelStyle::Street);
    poiLayer_.SetHighlight(mode == EViewMode::Walk ? -1 : focusPoi_);
    // A nav window rebuild costs about a second, and one landing in the middle
    // of an orbit is the most visible hitch the application has. Browsing an
    // orbit is a camera move, not a walk, so the character holds still for it.
    walker_.SetPaused(mode == EViewMode::Focus);
    if (mode != EViewMode::Focus)
    {
        tourActive_ = false;
    }
}

void NextWorldTravelGameInstance::FocusPoi(int poiIndex)
{
    const FGeoTile* tile = ActiveTile();
    if (tile == nullptr || poiIndex < 0 || poiIndex >= static_cast<int>(tile->pois.size()))
    {
        return;
    }
    const FGeoPoi& poi = tile->pois[static_cast<size_t>(poiIndex)];
    if (!poi.grounded)
    {
        SPDLOG_WARN("NextWorldTravel: '{}' is outside the heightfield and cannot be framed", poi.name);
        return;
    }

    focusPoi_ = poiIndex;
    ui_.SetSelectedPoi(poiIndex);
    poiLayer_.SetHighlight(poiIndex);
    camera_.SetFocusSubject(MakeFocusSubject(poi));
    if (camera_.Mode() != EViewMode::Focus)
    {
        camera_.SetMode(EViewMode::Focus, MakeCameraWorld());
        poiLayer_.SetStyle(ELabelStyle::Street);
        walker_.SetPaused(true);
    }
    tourTimer_ = tourDwell_;
}

void NextWorldTravelGameInstance::FocusStep(int delta)
{
    const std::vector<int> order = BuildBrowseOrder();
    if (order.empty())
    {
        return;
    }
    const auto it = std::find(order.begin(), order.end(), focusPoi_);
    // Stepping from nothing starts at the top of the order rather than at
    // whatever index happens to be adjacent to -1.
    const int current = it == order.end() ? -1 : static_cast<int>(it - order.begin());
    const int count = static_cast<int>(order.size());
    const int next = current < 0 ? (delta >= 0 ? 0 : count - 1)
                                 : ((current + delta) % count + count) % count;
    FocusPoi(order[static_cast<size_t>(next)]);
}

void NextWorldTravelGameInstance::UpdateTour(float deltaSeconds)
{
    if (!tourActive_ || camera_.Mode() != EViewMode::Focus)
    {
        return;
    }
    tourTimer_ -= deltaSeconds;
    if (tourTimer_ <= 0.0f)
    {
        FocusStep(1);
    }
}

void NextWorldTravelGameInstance::OnTick(double deltaSeconds)
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

    FCameraMoveInput move;
    move.forward = (keyForward_ ? 1.0f : 0.0f) - (keyBack_ ? 1.0f : 0.0f);
    move.right = (keyRight_ ? 1.0f : 0.0f) - (keyLeft_ ? 1.0f : 0.0f);
    move.up = (keyUp_ ? 1.0f : 0.0f) - (keyDown_ ? 1.0f : 0.0f);
    move.sprint = keySprint_;
    // In walk mode the same keys drive the character; the free camera is the
    // only walk sub-mode that flies.
    const bool cameraOwnsMovement = camera_.Mode() != EViewMode::Walk ||
                                    camera_.WalkCamera() == EWalkCamera::Free;
    camera_.SetMoveInput(cameraOwnsMovement ? move : FCameraMoveInput{});
    camera_.Tick(dt, MakeCameraWorld());

    UpdateTour(dt);

    if (const FGeoTile* tile = ActiveTile())
    {
        poiLayer_.Update(tile->pois, CameraPosition());
    }
}

void NextWorldTravelGameInstance::ApplyUIRequest(const FNextWorldTravelUIRequest& request)
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
    if (request.setViewMode)
    {
        SetViewMode(request.viewMode);
    }
    if (request.resetViewport)
    {
        camera_.ResetView(camera_.Mode(), MakeCameraWorld());
    }
    if (request.toggleTour)
    {
        tourActive_ = !tourActive_;
        tourTimer_ = tourDwell_;
        if (tourActive_)
        {
            // Starting a tour from the map is the common case, and it has to
            // put something on screen rather than only arming a timer.
            if (camera_.Mode() != EViewMode::Focus)
            {
                SetViewMode(EViewMode::Focus);
            }
            camera_.AutoOrbit() = true;
        }
    }
    if (request.focusNext)
    {
        FocusStep(1);
    }
    if (request.focusPrev)
    {
        FocusStep(-1);
    }
    if (request.takeScreenshot)
    {
        GetEngine().GetScreenShotService().Request();
    }

    const FGeoTile* tile = ActiveTile();
    if (tile == nullptr)
    {
        return;
    }
    if (request.focusPoiIndex >= 0 && request.focusPoiIndex < static_cast<int>(tile->pois.size()))
    {
        FocusPoi(request.focusPoiIndex);
    }
    if (request.lookAtPoiIndex >= 0 && request.lookAtPoiIndex < static_cast<int>(tile->pois.size()))
    {
        // Aim a third of the way up a building rather than at its roof: from a
        // pavement across the street, framing the roof of a 400 m tower points
        // the camera at empty sky.
        const FGeoPoi& poi = tile->pois[static_cast<size_t>(request.lookAtPoiIndex)];
        camera_.LookAt(glm::vec3(poi.position.x, poi.groundY + poi.height * 0.33f, poi.position.y));
    }
    if (request.walkToPoiIndex >= 0 && request.walkToPoiIndex < static_cast<int>(tile->pois.size()))
    {
        const FGeoPoi& poi = tile->pois[static_cast<size_t>(request.walkToPoiIndex)];
        // Walking there is a walk-mode action; asking for it from the map or an
        // orbit means the browser goes back to the street to do it.
        SetViewMode(EViewMode::Walk);
        walker_.WalkTo(glm::vec3(poi.position.x, poi.groundY, poi.position.y));
    }
}

void NextWorldTravelGameInstance::DrawWalkerMarker() const
{
    if (!walkerSpawned_ || camera_.Mode() != EViewMode::Aerial)
    {
        return;
    }
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (drawList == nullptr)
    {
        return;
    }
    const glm::vec3 head = walker_.Position() + glm::vec3(0.0f, Config::kCharacterHeight, 0.0f);
    ImVec2 screen;
    if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, head, screen))
    {
        return;
    }
    const ImU32 color = ImGui::GetColorU32(ImVec4(Config::kCharacterTint.r, Config::kCharacterTint.g,
                                                  Config::kCharacterTint.b, 1.0f));
    drawList->AddCircleFilled(screen, 5.0f, color);
    drawList->AddCircle(screen, 9.0f, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.85f)), 0, 1.5f);
    drawList->AddText(ImVec2(screen.x + 12.0f, screen.y - 7.0f), color, "walker");
}

bool NextWorldTravelGameInstance::OnRenderUI()
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
    DrawWalkerMarker();

    const glm::vec3 cameraPosition = CameraPosition();
    const std::vector<int> order = BuildBrowseOrder();
    const auto focusIt = std::find(order.begin(), order.end(), focusPoi_);

    FNextWorldTravelUIContext context;
    context.tiles = &tiles_;
    context.activeTile = activeTile_;
    context.walker = &walker_;
    context.cameraPosition = &cameraPosition;
    context.camera = &camera_;
    context.tourDwellSeconds = &tourDwell_;
    context.viewMode = camera_.Mode();
    context.followCamera = camera_.WalkCamera() == EWalkCamera::Follow;
    context.focusPoi = focusPoi_;
    context.focusOrdinal = focusIt == order.end() ? 0 : static_cast<int>(focusIt - order.begin()) + 1;
    context.focusTotal = static_cast<int>(order.size());
    context.tourActive = tourActive_;
    context.tourProgress = tourDwell_ > 0.0f ? std::clamp(tourTimer_ / tourDwell_, 0.0f, 1.0f) : 0.0f;
    context.frameMs = frameMs_;
    ApplyUIRequest(ui_.Draw(context, poiLayer_));
    return true;
}

bool NextWorldTravelGameInstance::OnKey(SDL_Event& event)
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
        if (down && walker_.Mode() == EWalkMode::Player && camera_.Mode() == EViewMode::Walk)
        {
            walker_.SetPlayerIntent(glm::vec3(0.0f), keySprint_, true);
        }
        return true;

    // ---- View modes ---------------------------------------------------
    case SDLK_1:
        if (down) { SetViewMode(EViewMode::Walk); }
        return true;
    case SDLK_2:
    case SDLK_V:
        // V used to snap to a whole-tile overview; that view is now a mode of
        // its own, so the key keeps its meaning and gains a map.
        if (down) { SetViewMode(EViewMode::Aerial); }
        return true;
    case SDLK_3:
    case SDLK_G:
        if (down) { SetViewMode(EViewMode::Focus); }
        return true;

    // ---- Browsing -----------------------------------------------------
    case SDLK_N:
        if (down) { FocusStep(1); }
        return true;
    case SDLK_B:
        if (down) { FocusStep(-1); }
        return true;
    case SDLK_T:
        if (down)
        {
            tourActive_ = !tourActive_;
            tourTimer_ = tourDwell_;
            if (tourActive_)
            {
                if (camera_.Mode() != EViewMode::Focus)
                {
                    SetViewMode(EViewMode::Focus);
                }
                camera_.AutoOrbit() = true;
            }
        }
        return true;
    case SDLK_O:
        if (down) { camera_.AutoOrbit() = !camera_.AutoOrbit(); }
        return true;

    // ---- Walk mode ----------------------------------------------------
    case SDLK_F:
        if (down)
        {
            walker_.ToggleMode();
            // Taking control only makes sense from behind the character.
            if (walker_.Mode() == EWalkMode::Player)
            {
                SetViewMode(EViewMode::Walk);
                camera_.SetWalkCamera(EWalkCamera::Follow);
            }
        }
        return true;
    case SDLK_C:
        if (down && camera_.Mode() == EViewMode::Walk)
        {
            camera_.ToggleWalkCamera();
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

bool NextWorldTravelGameInstance::OnCursorPosition(double xpos, double ypos)
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
    camera_.AddLook(static_cast<float>(dx) * Config::kMouseSensitivity,
                    static_cast<float>(dy) * Config::kMouseSensitivity);
    return true;
}

bool NextWorldTravelGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.button.button == SDL_BUTTON_RIGHT)
    {
        mouseLookActive_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        resetMouse_ = true;
        camera_.SetLookActive(mouseLookActive_);
        return true;
    }
    if (event.button.button != SDL_BUTTON_LEFT || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        return false;
    }
    // Picking a marker is the map's whole point, but the HUD is drawn over the
    // same pixels and gets first refusal on them.
    if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
    {
        return false;
    }
    const int picked = poiLayer_.PickAt(glm::vec2(mousePos_));
    if (picked < 0)
    {
        return false;
    }
    FocusPoi(picked);
    return true;
}

bool NextWorldTravelGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    camera_.AddZoom(static_cast<float>(yoffset));
    return true;
}

void NextWorldTravelGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& registry)
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
    // Two different questions: how many places the layer decided are worth a
    // name here, and how many of those actually reached the screen after
    // decluttering. A camera facing a wall legitimately draws almost none.
    registry.Add("geo.labelsVisible", [this]()
    {
        return static_cast<int64_t>(poiLayer_.VisibleCount());
    });
    registry.Add("geo.labelsDrawn", [this]()
    {
        return static_cast<int64_t>(poiLayer_.PlacedCount());
    });
    registry.Add("geo.markersVisible", [this]()
    {
        return static_cast<int64_t>(poiLayer_.MarkerCount());
    });
    registry.Add("geo.viewMode", [this]()
    {
        switch (camera_.Mode())
        {
        case EViewMode::Aerial: return std::string("aerial");
        case EViewMode::Focus: return std::string("focus");
        case EViewMode::Walk:
        default: return std::string("walk");
        }
    });
    registry.Add("geo.focusName", [this]()
    {
        const FGeoTile* tile = ActiveTile();
        if (tile == nullptr || focusPoi_ < 0 || focusPoi_ >= static_cast<int>(tile->pois.size()))
        {
            return std::string("none");
        }
        return tile->pois[static_cast<size_t>(focusPoi_)].name;
    });
    registry.Add("geo.focusIndex", [this]() { return static_cast<int64_t>(focusPoi_); });
    registry.Add("geo.browseCount", [this]()
    {
        return static_cast<int64_t>(BuildBrowseOrder().size());
    });
    registry.Add("geo.tourActive", [this]() { return tourActive_; });
    registry.Add("geo.cameraHeight", [this]()
    {
        return static_cast<double>(camera_.EyePosition().y);
    });
    registry.Add("geo.cameraX", [this]() { return static_cast<double>(camera_.EyePosition().x); });
    registry.Add("geo.cameraZ", [this]() { return static_cast<double>(camera_.EyePosition().z); });
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
