#include "NextTotalwarGameInstance.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace
{
    float WrapAngle(float angle)
    {
        return std::remainder(angle, glm::two_pi<float>());
    }

    float ApproachAngle(float current, float target, float maxStep)
    {
        return current + glm::clamp(WrapAngle(target - current), -maxStep, maxStep);
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<NextTotalwar::FGameInstance>(config, options, engine);
}

namespace NextTotalwar
{
    FGameInstance::FGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
        : NextGameInstanceBase(config, options, engine)
    {
        ConfigureWindow(config, options, "NextTotalwar", 1600, 900, true);
        unitDefs_ = {{
            {EUnitType::Spearman, "spearman", "Spearmen", 8.0f, 1.45f, 4, 1.12f, 1.35f},
            {EUnitType::Swordsman, "swordsman", "Swordsmen", 8.6f, 1.45f, 4, 1.08f, 1.30f},
            {EUnitType::Archer, "archer", "Archers", 8.2f, 1.45f, 4, 1.20f, 1.42f},
        }};
    }

    void FGameInstance::OnInit()
    {
        GOption->KeepCPUMeshData = true;
        const std::string scene = GOption->SceneName.empty()
                                      ? "assets/scad/proc/nexttotalwar/greenfield_400.scad"
                                      : GOption->SceneName;
        SPDLOG_INFO("NextTotalwar: loading '{}'", scene);
        GetEngine().RequestLoadScene({.filename = scene});
    }

    void FGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
    {
        std::string error;
        // 尖刀 C：SoftwareModern 在大批动态实例的启动/帧时间上明显优于
        // SoftwareModernNoAmbient，且保留 low-poly 场景所需的光栅 + GI。
        cvars.SetDefaultFromString("r.rendererType", "2", &error);
        cvars.SetDefaultFromString("r.temporalFrames", "4", &error);
    }

    void FGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>&,
                                           std::vector<Assets::Model>& models,
                                           std::vector<Assets::FMaterial>& materials,
                                           std::vector<Assets::LightObject>&,
                                           std::vector<Assets::AnimationTrack>&)
    {
        if (sceneInjected_) return;

        // 尖刀方案：每兵种仅注入一份 mesh，所有 256 名同兵种士兵共享 modelId。
        models.push_back(Assets::FProcModel::CreateBox({-0.24f, 0.0f, -0.17f}, {0.24f, 1.72f, 0.17f}));
        soldierModelIds_[0] = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox({-0.28f, 0.0f, -0.20f}, {0.28f, 1.66f, 0.20f}));
        soldierModelIds_[1] = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox({-0.22f, 0.0f, -0.16f}, {0.22f, 1.70f, 0.16f}));
        soldierModelIds_[2] = static_cast<uint32_t>(models.size() - 1);

        const std::array<glm::vec3, 6> blue = {{
            {0.10f, 0.25f, 0.58f}, {0.12f, 0.32f, 0.70f}, {0.16f, 0.38f, 0.76f},
            {0.10f, 0.29f, 0.66f}, {0.18f, 0.34f, 0.62f}, {0.12f, 0.37f, 0.58f},
        }};
        const std::array<glm::vec3, 6> red = {{
            {0.58f, 0.12f, 0.09f}, {0.68f, 0.16f, 0.10f}, {0.74f, 0.22f, 0.12f},
            {0.62f, 0.13f, 0.16f}, {0.65f, 0.23f, 0.12f}, {0.55f, 0.18f, 0.19f},
        }};
        for (int regiment = 0; regiment < 6; ++regiment)
        {
            regimentMaterialIds_[0][regiment] = Assets::SceneBuilder::AddLambertianMaterial(materials, blue[regiment]);
            regimentMaterialIds_[1][regiment] = Assets::SceneBuilder::AddLambertianMaterial(materials, red[regiment]);
        }
        sceneInjected_ = true;
    }

    void FGameInstance::OnSceneLoaded()
    {
        NextGameInstanceBase::OnSceneLoaded();
        Assets::Scene& scene = GetEngine().GetScene();
        terrain_ = nullptr;
        for (const auto& node : scene.Nodes())
        {
            if (auto* terrain = node->GetComponentPtr<Runtime::TerrainComponent>())
            {
                terrain_ = terrain;
                break;
            }
        }

        NextGameplay::FNavGridSettings settings;
        settings.cellSize = 2.0f;
        settings.agentRadius = 0.4f;
        settings.maxSlopeAngle = 42.0f;
        settings.clearanceHeight = 2.2f;
        settings.maxStepHeight = 0.9f;
        settings.worldMin = {-200.0f, 0.0f, -200.0f};
        settings.worldMax = {200.0f, 0.0f, 200.0f};
        settings.sampleCeiling = 80.0f;
        settings.floorHeightTolerance = 2.0f;
        navGrid_.Build(scene.GetCPUAccelerationStructure(), settings);
        if (terrain_)
        {
            navGrid_.MaskUnwalkable([this](const glm::vec3& point)
            {
                return terrain_->IsWater(point.x, point.z) &&
                       point.y < terrain_->WaterSurface(point.x, point.z) + 0.05f;
            });
        }
        DeployArmies();
        CreateSoldierVisuals();
        scene.MarkDirty();
        sceneReady_ = true;
        SPDLOG_INFO("NextTotalwar: 12 regiments / 768 soldiers, shared meshes enabled");
    }

    void FGameInstance::OnSceneUnloaded()
    {
        NextGameInstanceBase::OnSceneUnloaded();
        sceneReady_ = false;
        terrain_ = nullptr;
        regiments_.clear();
        sceneInjected_ = false;
    }

    void FGameInstance::DeployArmies()
    {
        regiments_.clear();
        regiments_.reserve(12);
        int id = 0;
        for (int faction = 0; faction < 2; ++faction)
        {
            for (int index = 0; index < 6; ++index)
            {
                FRegiment regiment;
                regiment.id = id++;
                regiment.faction = faction;
                regiment.def = &unitDefs_[index % 3];
                regiment.ranks = regiment.def->defaultRanks;
                const float x = faction == 0 ? -105.0f + (index % 3) * 28.0f : 105.0f - (index % 3) * 28.0f;
                const float z = -48.0f + (index / 3) * 48.0f + (index % 2) * 5.0f;
                regiment.anchor = {x, GroundHeight(x, z), z};
                regiment.facing = faction == 0 ? glm::half_pi<float>() : -glm::half_pi<float>();
                regiment.orderFacing = regiment.facing;
                regiment.soldiers.resize(64);
                for (int soldier = 0; soldier < 64; ++soldier)
                {
                    FSoldier& item = regiment.soldiers[soldier];
                    item.slotIndex = soldier;
                    item.phaseOffset = static_cast<float>((regiment.id * 67 + soldier * 23) % 101) / 101.0f;
                    const glm::vec2 local = Formation::SlotLocalOffset(
                        soldier, 64, regiment.ranks, regiment.def->fileSpacing, regiment.def->rankSpacing);
                    item.position = Formation::SlotWorld(regiment.anchor, regiment.facing, local);
                    item.position.y = GroundHeight(item.position.x, item.position.z);
                    item.yaw = regiment.facing;
                }
                regiments_.push_back(std::move(regiment));
            }
        }
    }

    void FGameInstance::CreateSoldierVisuals()
    {
        Assets::Scene& scene = GetEngine().GetScene();
        for (FRegiment& regiment : regiments_)
        {
            const uint32_t typeIndex = static_cast<uint32_t>(regiment.def->type);
            const uint32_t material = regimentMaterialIds_[regiment.faction][regiment.id % 6];
            for (size_t index = 0; index < regiment.soldiers.size(); ++index)
            {
                FSoldier& soldier = regiment.soldiers[index];
                soldier.worldNode = Assets::Node::CreateNode(
                    fmt::format("NTW/R{}/S{}", regiment.id, index),
                    soldier.position,
                    glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f)),
                    glm::vec3(1.0f),
                    scene.GenerateInstanceId());
                auto physics = std::make_shared<Runtime::PhysicsComponent>();
                physics->SetMobility(Runtime::ENodeMobility::Dynamic);
                soldier.worldNode->AddComponent(physics);
                scene.AddNode(soldier.worldNode);

                auto render = Assets::SceneBuilder::CreateRenderNode(
                    fmt::format("NTW/R{}/S{}/Body", regiment.id, index),
                    glm::vec3(0.0f), glm::vec3(1.0f),
                    scene.GenerateInstanceId(), soldierModelIds_[typeIndex], material, true,
                    glm::quat(1.0f, 0.0f, 0.0f, 0.0f), false);
                render->SetParent(soldier.worldNode);
                scene.AddNode(render);
                soldier.renderNode = render.get();
                soldier.worldNode->RecalcTransform(true);
            }
        }
    }

    float FGameInstance::GroundHeight(float x, float z) const
    {
        if (!terrain_) return 0.0f;
        float height = terrain_->SampleHeight(x, z);
        // SCAD 桥是独立静态 mesh，TerrainComponent 只描述河床。游戏表现层在
        // 两条桥廊内使用桥面高度，确保阵型槽位不会贴回河床。
        for (const float bridgeZ : {-4.0f, -75.0f})
        {
            if (x >= -31.0f && x <= 2.0f && std::abs(z - bridgeZ) <= 3.2f)
            {
                const float westDeck = terrain_->SampleHeight(-31.0f, bridgeZ) + 0.58f;
                const float eastDeck = terrain_->SampleHeight(2.0f, bridgeZ) + 0.58f;
                const float alpha = glm::clamp((x + 31.0f) / 33.0f, 0.0f, 1.0f);
                height = std::max(height, glm::mix(westDeck, eastDeck, alpha));
            }
        }
        return height;
    }

    float FGameInstance::UiScale() const
    {
        const NextUI::UserInterface* ui = GetEngine().GetUserInterface();
        return ui ? std::max(ui->UiScale(), 0.001f) : 1.0f;
    }

    glm::dvec2 FGameInstance::ToLogicalMouse(double x, double y) const
    {
#if WIN32
        // 与 ScadLibrary / UserInterface::PreRender 保持一致：真实 SDL 指针事件
        // 位于 DPI framebuffer 像素空间，ImGui 投影和 overlay 则位于逻辑空间。
        // Agent 输入协议本身使用 viewport 逻辑坐标，不再重复缩放。
        if (!GOption->AgentValidation)
        {
            const double scale = static_cast<double>(UiScale());
            return {x / scale, y / scale};
        }
#endif
        return {x, y};
    }

    glm::vec2 FGameInstance::ToFramebufferMouse(const glm::dvec2& logicalMouse) const
    {
        // EngineHelper::GetScreenToWorldRay 使用 SwapChain OutputExtent，
        // 因而需要 framebuffer 像素；ScadLibrary 也在射线调用前做同一转换。
        return glm::vec2(logicalMouse) * UiScale();
    }

    void FGameInstance::OnTick(double deltaSeconds)
    {
        if (!sceneReady_) return;
        const float dt = std::min(static_cast<float>(deltaSeconds), 0.05f);
        camera_.Tick(dt, terrain_);
        TickRegiments(dt);
        TickSoldiers(dt);
        GetEngine().GetScene().MarkTransformDirty();
        ++frameIndex_;
    }

    void FGameInstance::TickRegiments(float deltaSeconds)
    {
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.state == ERegimentState::Idle) continue;
            if (regiment.state == ERegimentState::Marching)
            {
                if (regiment.pathCursor >= regiment.path.size())
                {
                    regiment.state = ERegimentState::Reforming;
                    continue;
                }
                glm::vec3 target = regiment.path[regiment.pathCursor];
                target.y = GroundHeight(target.x, target.z);
                glm::vec3 delta = target - regiment.anchor;
                delta.y = 0.0f;
                const float distance = glm::length(delta);
                if (distance < 0.8f)
                {
                    ++regiment.pathCursor;
                    continue;
                }
                const glm::vec3 direction = delta / distance;
                const float step = std::min(distance, regiment.def->marchSpeed * deltaSeconds);
                regiment.anchor += direction * step;
                regiment.anchor.y = GroundHeight(regiment.anchor.x, regiment.anchor.z);
                regiment.facing = ApproachAngle(
                    regiment.facing, std::atan2(direction.x, direction.z), deltaSeconds * 2.1f);
            }
            else
            {
                regiment.facing = ApproachAngle(regiment.facing, regiment.orderFacing, deltaSeconds * 2.5f);
                float maxSlotError = 0.0f;
                for (const FSoldier& soldier : regiment.soldiers)
                {
                    const glm::vec2 local = Formation::SlotLocalOffset(
                        soldier.slotIndex, static_cast<int>(regiment.soldiers.size()), regiment.ranks,
                        regiment.def->fileSpacing, regiment.def->rankSpacing);
                    const glm::vec3 slot = Formation::SlotWorld(regiment.anchor, regiment.facing, local);
                    maxSlotError = std::max(
                        maxSlotError,
                        glm::distance(glm::vec2(slot.x, slot.z), glm::vec2(soldier.position.x, soldier.position.z)));
                }
                if (std::abs(WrapAngle(regiment.orderFacing - regiment.facing)) < 0.015f && maxSlotError < 0.12f)
                {
                    regiment.facing = regiment.orderFacing;
                    regiment.state = ERegimentState::Idle;
                }
            }
        }
    }

    void FGameInstance::TickSoldiers(float deltaSeconds)
    {
        animatorUpdates_ = 0;
        for (FRegiment& regiment : regiments_)
        {
            for (FSoldier& soldier : regiment.soldiers)
            {
                const glm::vec2 local = Formation::SlotLocalOffset(
                    soldier.slotIndex, static_cast<int>(regiment.soldiers.size()), regiment.ranks,
                    regiment.def->fileSpacing, regiment.def->rankSpacing);
                glm::vec3 target = Formation::SlotWorld(regiment.anchor, regiment.facing, local);
                target.y = GroundHeight(target.x, target.z);
                glm::vec3 delta = target - soldier.position;
                const float distance = glm::length(glm::vec2(delta.x, delta.z));
                const float speed = regiment.def->marchSpeed * regiment.def->catchUpFactor;
                if (distance > 0.035f)
                {
                    const glm::vec3 direction(delta.x / distance, 0.0f, delta.z / distance);
                    const float step = std::min(distance, speed * deltaSeconds);
                    soldier.position += direction * step;
                    soldier.position.y = GroundHeight(soldier.position.x, soldier.position.z);
                    soldier.yaw = ApproachAngle(
                        soldier.yaw, std::atan2(direction.x, direction.z), deltaSeconds * 5.0f);
                }
                else
                {
                    soldier.position = target;
                    soldier.yaw = ApproachAngle(soldier.yaw, regiment.facing, deltaSeconds * 5.0f);
                }

                if (soldier.worldNode)
                {
                    const bool animate = camera_.Distance() < 120.0f ||
                                         ((frameIndex_ + static_cast<uint64_t>(soldier.slotIndex)) % 3u == 0u);
                    float bob = 0.0f;
                    if (animate && regiment.state != ERegimentState::Idle)
                    {
                        bob = std::abs(std::sin(
                                  static_cast<float>(frameIndex_) * 0.16f + soldier.phaseOffset * glm::two_pi<float>())) *
                              0.045f;
                        ++animatorUpdates_;
                    }
                    soldier.worldNode->Translation() = soldier.position + glm::vec3(0.0f, bob, 0.0f);
                    soldier.worldNode->Rotation() = glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                    soldier.worldNode->RecalcTransform(true);
                }
            }
        }
    }

    bool FGameInstance::TryGroundHit(const glm::dvec2& screen, glm::vec3& hit) const
    {
        glm::vec3 origin{};
        glm::vec3 direction{};
        Runtime::EngineHelper::GetScreenToWorldRay(ToFramebufferMouse(screen), origin, direction);
        if (direction.y >= -0.0001f) return false;
        float t = (GroundHeight(origin.x, origin.z) - origin.y) / direction.y;
        if (t < 0.0f) return false;
        for (int iteration = 0; iteration < 5; ++iteration)
        {
            hit = origin + direction * t;
            const float height = GroundHeight(hit.x, hit.z);
            t += (height - hit.y) / direction.y;
        }
        hit = origin + direction * t;
        hit.x = glm::clamp(hit.x, -198.0f, 198.0f);
        hit.z = glm::clamp(hit.z, -198.0f, 198.0f);
        hit.y = GroundHeight(hit.x, hit.z);
        return true;
    }

    void FGameInstance::ClearSelection()
    {
        for (FRegiment& regiment : regiments_) regiment.selected = false;
    }

    void FGameInstance::SelectAt(const glm::dvec2& screen, bool additive)
    {
        if (!additive) ClearSelection();
        float best = 32.0f;
        FRegiment* picked = nullptr;
        for (FRegiment& regiment : regiments_)
        {
            ImVec2 projected;
            if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, regiment.anchor, projected)) continue;
            const float distance = glm::distance(glm::vec2(projected.x, projected.y), glm::vec2(screen));
            if (distance < best)
            {
                best = distance;
                picked = &regiment;
            }
        }
        if (picked) picked->selected = additive ? !picked->selected : true;
        RefreshSelectionFeedback();
    }

    void FGameInstance::SelectAllTypeAt(const glm::dvec2& screen)
    {
        float best = 32.0f;
        const FUnitDef* pickedDef = nullptr;
        for (const FRegiment& regiment : regiments_)
        {
            ImVec2 projected;
            if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, regiment.anchor, projected)) continue;
            const float distance = glm::distance(glm::vec2(projected.x, projected.y), glm::vec2(screen));
            if (distance < best)
            {
                best = distance;
                pickedDef = regiment.def;
            }
        }
        if (!pickedDef) return;
        ClearSelection();
        for (FRegiment& regiment : regiments_)
        {
            regiment.selected = regiment.def == pickedDef;
        }
        RefreshSelectionFeedback();
    }

    void FGameInstance::SelectRect(const glm::dvec2& a, const glm::dvec2& b, bool additive)
    {
        if (!additive) ClearSelection();
        const float minX = static_cast<float>(std::min(a.x, b.x));
        const float maxX = static_cast<float>(std::max(a.x, b.x));
        const float minY = static_cast<float>(std::min(a.y, b.y));
        const float maxY = static_cast<float>(std::max(a.y, b.y));
        for (FRegiment& regiment : regiments_)
        {
            ImVec2 projected;
            if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, regiment.anchor, projected) &&
                projected.x >= minX && projected.x <= maxX && projected.y >= minY && projected.y <= maxY)
            {
                regiment.selected = true;
            }
        }
        RefreshSelectionFeedback();
    }

    void FGameInstance::RefreshSelectionFeedback()
    {
        for (FRegiment& regiment : regiments_)
        {
            for (FSoldier& soldier : regiment.soldiers)
            {
                if (!soldier.renderNode) continue;
                if (auto render = soldier.renderNode->GetComponent<Runtime::RenderComponent>())
                {
                    render->SetOutlineFlags(regiment.selected ? Runtime::RenderOutlineFlags::selected
                                                             : Runtime::RenderOutlineFlags::none);
                }
            }
        }
        GetEngine().GetScene().MarkDirty();
    }

    void FGameInstance::IssueMoveOrders(const glm::vec3& target, float facing)
    {
        std::vector<FRegiment*> selected;
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.selected) selected.push_back(&regiment);
        }
        if (selected.empty()) return;

        const glm::vec3 lateral(std::cos(facing), 0.0f, -std::sin(facing));
        const float center = static_cast<float>(selected.size() - 1) * 0.5f;
        for (size_t index = 0; index < selected.size(); ++index)
        {
            FRegiment& regiment = *selected[index];
            glm::vec3 destination = target + lateral * (static_cast<float>(index) - center) * 21.0f;
            destination.y = GroundHeight(destination.x, destination.z);
            regiment.orderTarget = destination;
            regiment.orderFacing = facing;
            regiment.path = navGrid_.IsBuilt()
                                ? navGrid_.FindPath(regiment.anchor, destination, regiment.anchor.y)
                                : std::vector<glm::vec3>{destination};
            if (regiment.path.empty())
            {
                // 河桥是独立 SCAD mesh；个别低模桥端在 2m NavGrid 上会因单格
                // 台阶离散化断连。失败时仍按战场语义走最近的桥，而不是穿河直线。
                const bool crossesRiver = (regiment.anchor.x < -15.0f && destination.x > -15.0f) ||
                                          (regiment.anchor.x > -15.0f && destination.x < -15.0f);
                if (crossesRiver)
                {
                    const float averageZ = (regiment.anchor.z + destination.z) * 0.5f;
                    const float bridgeZ = std::abs(averageZ + 4.0f) <= std::abs(averageZ + 75.0f)
                                              ? -4.0f
                                              : -75.0f;
                    const bool westToEast = regiment.anchor.x < destination.x;
                    glm::vec3 entry(westToEast ? -31.0f : 2.0f, 0.0f, bridgeZ);
                    glm::vec3 exit(westToEast ? 2.0f : -31.0f, 0.0f, bridgeZ);
                    entry.y = GroundHeight(entry.x, entry.z);
                    exit.y = GroundHeight(exit.x, exit.z);
                    regiment.path = {regiment.anchor, entry, exit, destination};
                    SPDLOG_INFO("NextTotalwar: regiment {} uses semantic bridge route", regiment.id);
                }
                else
                {
                    regiment.path = {regiment.anchor, destination};
                    SPDLOG_WARN("NextTotalwar: regiment {} path failed, using local direct fallback", regiment.id);
                }
            }
            regiment.pathCursor = regiment.path.size() > 1 ? 1 : 0;
            regiment.state = ERegimentState::Marching;
            lastOrderDistance_ = glm::distance(glm::vec2(regiment.anchor.x, regiment.anchor.z),
                                               glm::vec2(destination.x, destination.z));
        }
    }

    bool FGameInstance::OnKey(SDL_Event& event)
    {
        if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) return false;
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        switch (event.key.key)
        {
        case SDLK_W: case SDLK_UP: camera_.SetMoveForward(down); return true;
        case SDLK_S: case SDLK_DOWN: camera_.SetMoveBack(down); return true;
        case SDLK_A: case SDLK_LEFT: camera_.SetMoveLeft(down); return true;
        case SDLK_D: case SDLK_RIGHT: camera_.SetMoveRight(down); return true;
        case SDLK_Q: camera_.SetRotateLeft(down); return true;
        case SDLK_E: camera_.SetRotateRight(down); return true;
        case SDLK_F1:
            if (down) showDebug_ = !showDebug_;
            return true;
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET:
            if (down)
            {
                const int delta = event.key.key == SDLK_LEFTBRACKET ? -1 : 1;
                for (FRegiment& regiment : regiments_)
                {
                    if (regiment.selected) regiment.ranks = glm::clamp(regiment.ranks + delta, 2, 8);
                }
            }
            return true;
        default: return false;
        }
    }

    bool FGameInstance::OnCursorPosition(double x, double y)
    {
        mousePos_ = ToLogicalMouse(x, y);
        hasMouse_ = true;
        return leftDown_ || rightDown_;
    }

    bool FGameInstance::OnMouseButton(SDL_Event& event)
    {
        if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP) return false;
        mousePos_ = ToLogicalMouse(event.button.x, event.button.y);
        hasMouse_ = true;
        if (event.button.button == SDL_BUTTON_LEFT)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                leftDown_ = true;
                leftStart_ = mousePos_;
            }
            else
            {
                leftDown_ = false;
                const bool additive = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                if (glm::distance(leftStart_, mousePos_) > 6.0)
                {
                    SelectRect(leftStart_, mousePos_, additive);
                }
                else if (event.button.clicks >= 2)
                {
                    SelectAllTypeAt(mousePos_);
                }
                else
                {
                    SelectAt(mousePos_, additive);
                }
            }
            return true;
        }
        if (event.button.button == SDL_BUTTON_RIGHT)
        {
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                rightDown_ = TryGroundHit(mousePos_, rightTarget_);
                rightStart_ = mousePos_;
            }
            else if (rightDown_)
            {
                rightDown_ = false;
                glm::vec3 release{};
                if (TryGroundHit(mousePos_, release))
                {
                    glm::vec3 direction = release - rightTarget_;
                    direction.y = 0.0f;
                    float facing = 0.0f;
                    if (glm::length(direction) > 2.0f) facing = std::atan2(direction.x, direction.z);
                    else
                    {
                        for (const FRegiment& regiment : regiments_)
                        {
                            if (regiment.selected) { facing = regiment.facing; break; }
                        }
                    }
                    IssueMoveOrders(rightTarget_, facing);
                }
            }
            return true;
        }
        return false;
    }

    bool FGameInstance::OnScroll(double, double y)
    {
        camera_.AddZoom(static_cast<float>(y));
        return true;
    }

    size_t FGameInstance::SelectedCount() const
    {
        return static_cast<size_t>(std::count_if(regiments_.begin(), regiments_.end(),
                                                [](const FRegiment& regiment) { return regiment.selected; }));
    }

    size_t FGameInstance::MarchingCount() const
    {
        return static_cast<size_t>(std::count_if(regiments_.begin(), regiments_.end(),
                                                [](const FRegiment& regiment)
                                                {
                                                    return regiment.state != ERegimentState::Idle;
                                                }));
    }

    const char* FGameInstance::StateName(ERegimentState state)
    {
        switch (state)
        {
        case ERegimentState::Marching: return "Marching";
        case ERegimentState::Reforming: return "Reforming";
        default: return "Idle";
        }
    }

    bool FGameInstance::OnRenderUI()
    {
        if (!sceneReady_) return false;
        const uint32_t batches = GetEngine().GetScene().GetIndirectDrawBatchCount();
        ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_Always);
        ImGui::Begin("NextTotalwar", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Regiments: 12   Soldiers: 768");
        ImGui::Text("Selected: %zu   Moving: %zu", SelectedCount(), MarchingCount());
        ImGui::Text("Shared meshes: 3 (256 instances each)");
        const float budget = static_cast<float>(batches) / 32767.0f;
        ImGui::TextColored(batches > 28000 ? ImVec4(1, 0.2f, 0.15f, 1) : ImVec4(0.3f, 1, 0.4f, 1),
                           "Node-sections: %u / 32767", batches);
        ImGui::ProgressBar(glm::clamp(budget, 0.0f, 1.0f), {-1.0f, 0.0f});
        ImGui::Text("FPS: %.1f   Animated this frame: %d", ImGui::GetIO().Framerate, animatorUpdates_);
        ImGui::Text("NavGrid: %s (%dx%d)", navGrid_.IsBuilt() ? "ready" : "not ready",
                    navGrid_.GetWidth(), navGrid_.GetHeight());
        ImGui::Separator();
        ImGui::TextUnformatted("LMB: point/box select   Shift: add");
        ImGui::TextUnformatted("RMB drag: destination + facing");
        ImGui::TextUnformatted("WASD: pan  Q/E: rotate  Wheel: zoom");
        ImGui::TextUnformatted("[/]: selected formation ranks");
        if (showDebug_)
        {
            ImGui::Separator();
            for (const FRegiment& regiment : regiments_)
            {
                if (!regiment.selected) continue;
                ImGui::Text("#%d %s  %s  ranks=%d", regiment.id, regiment.def->displayName,
                            StateName(regiment.state), regiment.ranks);
            }
            ImGui::Text("Last order distance: %.1f m", lastOrderDistance_);
        }
        ImGui::End();

        // 底部部队条。
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({viewport->WorkPos.x + 180.0f, viewport->WorkPos.y + viewport->WorkSize.y - 78.0f},
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize({viewport->WorkSize.x - 360.0f, 66.0f}, ImGuiCond_Always);
        ImGui::Begin("Regiments", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.id > 0) ImGui::SameLine();
            if (regiment.selected) ImGui::PushStyleColor(ImGuiCol_Button, {0.15f, 0.62f, 0.85f, 1.0f});
            const std::string label = fmt::format("{}\n64##{}", regiment.def->displayName, regiment.id);
            if (ImGui::Button(label.c_str(), {76.0f, 48.0f}))
            {
                ClearSelection();
                regiment.selected = true;
                RefreshSelectionFeedback();
            }
            if (regiment.selected) ImGui::PopStyleColor();
        }
        ImGui::End();
        DrawWorldOverlay();
        return true;
    }

    void FGameInstance::DrawWorldOverlay() const
    {
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        if (!draw) return;
        if (leftDown_ && hasMouse_ && glm::distance(leftStart_, mousePos_) > 6.0)
        {
            const ImVec2 a(static_cast<float>(leftStart_.x), static_cast<float>(leftStart_.y));
            const ImVec2 b(static_cast<float>(mousePos_.x), static_cast<float>(mousePos_.y));
            draw->AddRectFilled(a, b, IM_COL32(40, 170, 255, 38));
            draw->AddRect(a, b, IM_COL32(80, 210, 255, 235), 0.0f, 0, 2.0f);
        }
        for (const FRegiment& regiment : regiments_)
        {
            if (!regiment.selected) continue;
            const glm::vec2 half = Formation::FormationHalfExtent(
                static_cast<int>(regiment.soldiers.size()), regiment.ranks,
                regiment.def->fileSpacing, regiment.def->rankSpacing);
            const std::array<glm::vec2, 4> corners = {{
                {-half.x - 0.7f, half.y + 0.7f}, {half.x + 0.7f, half.y + 0.7f},
                {half.x + 0.7f, -half.y - 0.7f}, {-half.x - 0.7f, -half.y - 0.7f},
            }};
            std::array<ImVec2, 4> projected{};
            bool valid = true;
            for (size_t index = 0; index < corners.size(); ++index)
            {
                glm::vec3 world = Formation::SlotWorld(regiment.anchor, regiment.facing, corners[index]);
                world.y = GroundHeight(world.x, world.z) + 0.08f;
                valid &= Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, projected[index]);
            }
            if (valid)
            {
                draw->AddPolyline(projected.data(), 4, IM_COL32(70, 235, 255, 240),
                                  ImDrawFlags_Closed, 2.0f);
            }
        }
        if (rightDown_)
        {
            ImVec2 target;
            if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, rightTarget_, target))
            {
                draw->AddCircle(target, 12.0f, IM_COL32(255, 225, 80, 240), 24, 2.5f);
                draw->AddLine(target, ImVec2(static_cast<float>(mousePos_.x), static_cast<float>(mousePos_.y)),
                              IM_COL32(255, 225, 80, 230), 2.0f);
            }
        }
    }

    bool FGameInstance::OverrideRenderCamera(Assets::Camera& camera) const
    {
        return camera_.OverrideRenderCamera(camera);
    }

    void FGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& registry)
    {
        registry.Add("selectedRegiments", [this]() { return static_cast<int64_t>(SelectedCount()); });
        registry.Add("regimentCount", [this]() { return static_cast<int64_t>(regiments_.size()); });
        registry.Add("marchingRegiments", [this]() { return static_cast<int64_t>(MarchingCount()); });
        registry.Add("soldierCount", [this]() { return static_cast<int64_t>(regiments_.size() * 64); });
        registry.Add("navReady", [this]() { return navGrid_.IsBuilt(); });
        registry.Add("lastOrderDistance", [this]() { return static_cast<double>(lastOrderDistance_); });
    }

    void FGameInstance::OnDestroy()
    {
    }
}
