#include "NextTotalwarGameInstance.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
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
#include "Gameplay/Rig/RigInstance.h"
#include "Modules/ScadLoader/FScadRig.h"
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
#include <limits>

namespace
{
    constexpr int regimentCountPerFaction = 12;
    constexpr int soldiersPerRegiment = 100;
    constexpr int totalRegimentCount = regimentCountPerFaction * 2;
    constexpr int totalSoldierCount = totalRegimentCount * soldiersPerRegiment;
    constexpr float regimentLateralSpacing = 18.0f;
    constexpr float regimentDepthSpacing = 20.0f;

    float WrapAngle(float angle)
    {
        return std::remainder(angle, glm::two_pi<float>());
    }

    float ApproachAngle(float current, float target, float maxStep)
    {
        return current + glm::clamp(WrapAngle(target - current), -maxStep, maxStep);
    }

    glm::vec3 RegimentOrderOffset(size_t index, size_t regimentCount, float facing)
    {
        const glm::vec3 lateral(std::cos(facing), 0.0f, -std::sin(facing));
        const glm::vec3 forward(std::sin(facing), 0.0f, std::cos(facing));
        const size_t columns =
            std::max<size_t>(1, static_cast<size_t>(std::ceil(std::sqrt(regimentCount * 1.35f))));
        const size_t rows = (regimentCount + columns - 1) / columns;
        const size_t row = index / columns;
        const size_t column = index % columns;
        const size_t columnsInRow = std::min(columns, regimentCount - row * columns);
        const float columnOffset =
            static_cast<float>(column) - static_cast<float>(columnsInRow - 1) * 0.5f;
        const float rowOffset =
            static_cast<float>(row) - static_cast<float>(rows - 1) * 0.5f;
        return lateral * columnOffset * regimentLateralSpacing -
               forward * rowOffset * regimentDepthSpacing;
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    options.RenderCapacityMode = Runtime::Config::ERenderCapacityMode::Massive;
    Modules::Scad::Register();
    return std::make_unique<NextTotalwar::FGameInstance>(config, options, engine);
}

namespace NextTotalwar
{
    FGameInstance::FGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
        : NextGameInstanceBase(config, options, engine)
    {
        ConfigureWindow(config, options, "NextTotalwar", 1600, 900, true);
        options.RenderCapacityMode = Runtime::Config::ERenderCapacityMode::Massive;
        unitDefs_ = {{
            {EUnitType::Spearman, "spearman", "Spearmen", 8.0f, 1.45f, 10, 1.12f, 1.35f},
            {EUnitType::Swordsman, "swordsman", "Swordsmen", 8.6f, 1.45f, 10, 1.08f, 1.30f},
            {EUnitType::Archer, "archer", "Archers", 8.2f, 1.45f, 10, 1.20f, 1.42f},
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
        cvars.SetDefaultFromString("r.rendererType", "0", &error);
        //cvars.SetDefaultFromString("r.temporalFrames", "4", &error);
    }

    void FGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>&,
                                           std::vector<Assets::Model>& models,
                                           std::vector<Assets::FMaterial>& materials,
                                           std::vector<Assets::LightObject>&,
                                           std::vector<Assets::AnimationTrack>&)
    {
        if (sceneInjected_) return;

        // 每兵种的每个 ScadRig part 只注入一次，同兵种士兵共享
        // modelId；实例自己的骨骼树负责姿态，逐节点材质负责部队换色。
        static constexpr std::array<const char*, 3> rigPaths = {{
            "assets/scad/characters/tw_spearman.scad",
            "assets/scad/characters/tw_swordsman.scad",
            "assets/scad/characters/tw_archer.scad",
        }};
        for (size_t typeIndex = 0; typeIndex < rigPaths.size(); ++typeIndex)
        {
            std::string error;
            std::vector<std::string> warnings;
            Assets::FRigAsset& rig = soldierRigAssets_[typeIndex];
            if (!Assets::FScadRigLoader::LoadRig(rigPaths[typeIndex], {}, rig, error, &warnings))
            {
                throw std::runtime_error(fmt::format("NextTotalwar: failed to load ScadRig '{}': {}",
                                                     rigPaths[typeIndex], error));
            }
            for (const std::string& warning : warnings)
            {
                SPDLOG_WARN("NextTotalwar/ScadRig: {}", warning);
            }

            auto& modelIds = soldierPartModelIds_[typeIndex];
            auto& materialIds = soldierPartMaterialIds_[typeIndex];
            modelIds.clear();
            materialIds.clear();
            modelIds.reserve(rig.parts.size());
            materialIds.reserve(rig.parts.size());
            for (const Assets::FRigPart& part : rig.parts)
            {
                models.push_back(rig.partModels[part.modelIndex]);
                modelIds.push_back(static_cast<uint32_t>(models.size() - 1));

                std::array<uint32_t, 16> sections{};
                for (size_t section = 0;
                     section < part.sectionColors.size() && section < sections.size(); ++section)
                {
                    if (!part.sectionTintable[section])
                    {
                        sections[section] = Assets::SceneBuilder::AddLambertianMaterial(
                            materials, glm::vec3(part.sectionColors[section]));
                    }
                }
                materialIds.push_back(sections);
            }
            SPDLOG_INFO("NextTotalwar: loaded ScadRig '{}' ({} bones / {} shared parts / {} clips)",
                        rigPaths[typeIndex], rig.bones.size(), rig.parts.size(), rig.clips.size());
        }

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
        SPDLOG_INFO("NextTotalwar: {} regiments / {} ScadRig soldiers, Massive shared-part rendering enabled",
                    totalRegimentCount, totalSoldierCount);
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
        regiments_.reserve(totalRegimentCount);
        int id = 0;
        for (int faction = 0; faction < 2; ++faction)
        {
            for (int index = 0; index < regimentCountPerFaction; ++index)
            {
                FRegiment regiment;
                regiment.id = id++;
                regiment.faction = faction;
                regiment.def = &unitDefs_[index % 3];
                regiment.ranks = regiment.def->defaultRanks;
                const int column = index % 3;
                const int row = index / 3;
                const float x = faction == 0 ? -125.0f + column * 30.0f : 125.0f - column * 30.0f;
                const float z = -66.0f + row * 44.0f + (column % 2) * 3.0f;
                regiment.anchor = {x, GroundHeight(x, z), z};
                regiment.facing = faction == 0 ? glm::half_pi<float>() : -glm::half_pi<float>();
                regiment.orderFacing = regiment.facing;
                regiment.soldiers.resize(soldiersPerRegiment);
                for (int soldier = 0; soldier < soldiersPerRegiment; ++soldier)
                {
                    FSoldier& item = regiment.soldiers[soldier];
                    item.slotIndex = soldier;
                    item.phaseOffset = static_cast<float>((regiment.id * 67 + soldier * 23) % 101) / 101.0f;
                    const glm::vec2 local = Formation::SlotLocalOffset(
                        soldier, soldiersPerRegiment, regiment.ranks,
                        regiment.def->fileSpacing, regiment.def->rankSpacing);
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
            const Assets::FRigAsset& rig = soldierRigAssets_[typeIndex];
            const uint32_t tintMaterial = regimentMaterialIds_[regiment.faction][regiment.id % 6];
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

                NextGameplay::FRigInstanceDesc desc;
                desc.namePrefix = fmt::format("NTW/R{}/S{}", regiment.id, index);
                desc.partModelIds = soldierPartModelIds_[typeIndex];
                desc.partMaterialIds = soldierPartMaterialIds_[typeIndex];
                for (size_t partIndex = 0; partIndex < rig.parts.size(); ++partIndex)
                {
                    const Assets::FRigPart& part = rig.parts[partIndex];
                    for (size_t section = 0;
                         section < part.sectionTintable.size() && section < 16; ++section)
                    {
                        if (part.sectionTintable[section])
                        {
                            desc.partMaterialIds[partIndex][section] = tintMaterial;
                        }
                    }
                }

                const size_t firstRigNode = scene.Nodes().size();
                std::vector<Assets::Node*> boneNodes;
                auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, rig, desc, boneNodes);
                if (!rigRoot)
                {
                    throw std::runtime_error(fmt::format(
                        "NextTotalwar: failed to instantiate ScadRig for regiment {} soldier {}",
                        regiment.id, index));
                }
                rigRoot->SetParent(soldier.worldNode);
                soldier.renderNodes.clear();
                for (size_t nodeIndex = firstRigNode; nodeIndex < scene.Nodes().size(); ++nodeIndex)
                {
                    Assets::Node* node = scene.Nodes()[nodeIndex].get();
                    if (node->GetComponentPtr<Runtime::RenderComponent>())
                    {
                        soldier.renderNodes.push_back(node);
                    }
                }
                soldier.animator.Bind(&rig, std::move(boneNodes), soldier.worldNode.get());
                soldier.animator.SetPhaseOffset(soldier.phaseOffset);
                soldier.animator.Play("idle", 0.0f);
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
                    regiment.anchor = target;
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
        const uint32_t animationStride = camera_.Distance() < 90.0f
                                             ? 1u
                                             : (camera_.Distance() < 140.0f ? 3u : 8u);
        for (FRegiment& regiment : regiments_)
        {
            const bool regimentMoving = regiment.state != ERegimentState::Idle;
            for (FSoldier& soldier : regiment.soldiers)
            {
                bool transformChanged = false;
                float distance = 0.0f;
                if (regimentMoving)
                {
                    const glm::vec2 local = Formation::SlotLocalOffset(
                        soldier.slotIndex, static_cast<int>(regiment.soldiers.size()), regiment.ranks,
                        regiment.def->fileSpacing, regiment.def->rankSpacing);
                    glm::vec3 target = Formation::SlotWorld(regiment.anchor, regiment.facing, local);
                    target.y = GroundHeight(target.x, target.z);
                    const glm::vec3 delta = target - soldier.position;
                    distance = glm::length(glm::vec2(delta.x, delta.z));
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
                    transformChanged = true;
                }

                if (soldier.worldNode)
                {
                    const bool animate =
                        (frameIndex_ + static_cast<uint64_t>(regiment.id * 17 + soldier.slotIndex)) %
                            animationStride ==
                        0u;
                    const bool moving = regimentMoving || distance > 0.035f;
                    const Assets::FRigAsset& rig =
                        soldierRigAssets_[static_cast<size_t>(regiment.def->type)];
                    const char* clip = moving && rig.FindClip("march")
                                           ? "march"
                                           : (moving ? "walk" : "idle");
                    soldier.animator.Play(clip);
                    if (transformChanged)
                    {
                        soldier.worldNode->Translation() = soldier.position;
                        soldier.worldNode->Rotation() =
                            glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                    }
                    if (animate)
                    {
                        soldier.animator.Update(deltaSeconds * static_cast<float>(animationStride));
                        ++animatorUpdates_;
                    }
                    else if (transformChanged)
                    {
                        soldier.worldNode->RecalcTransform(true);
                    }
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
                for (Assets::Node* renderNode : soldier.renderNodes)
                {
                    if (auto render = renderNode->GetComponent<Runtime::RenderComponent>())
                    {
                        render->SetOutlineFlags(regiment.selected ? Runtime::RenderOutlineFlags::selected
                                                                 : Runtime::RenderOutlineFlags::none);
                    }
                }
            }
        }
        GetEngine().GetScene().MarkDirty();
    }

    float FGameInstance::ResolveOrderFacing(const glm::vec3& target, const glm::vec3& facingPoint) const
    {
        glm::vec3 direction = facingPoint - target;
        direction.y = 0.0f;
        if (glm::length(direction) > 2.0f)
        {
            return std::atan2(direction.x, direction.z);
        }
        for (const FRegiment& regiment : regiments_)
        {
            if (regiment.selected) return regiment.facing;
        }
        return 0.0f;
    }

    std::vector<glm::vec3> FGameInstance::BuildOrderPath(
        const FRegiment& regiment, const glm::vec3& destination, float facing) const
    {
        const float approachDistance =
            Formation::FormationHalfExtent(static_cast<int>(regiment.soldiers.size()),
                                           regiment.ranks,
                                           regiment.def->fileSpacing,
                                           regiment.def->rankSpacing).y;
        glm::vec3 approach =
            Formation::SlotWorld(destination, facing, glm::vec2(0.0f, -approachDistance));
        approach.y = GroundHeight(approach.x, approach.z);

        std::vector<glm::vec3> path = navGrid_.IsBuilt()
                                          ? navGrid_.FindPath(regiment.anchor, approach, regiment.anchor.y)
                                          : std::vector<glm::vec3>{regiment.anchor};
        if (path.empty())
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
                path = {regiment.anchor, entry, exit};
                SPDLOG_INFO("NextTotalwar: regiment {} uses semantic bridge route", regiment.id);
            }
            else
            {
                path = {regiment.anchor};
                SPDLOG_WARN("NextTotalwar: regiment {} path failed, using local direct fallback", regiment.id);
            }
        }

        const auto appendDistinct = [&path](const glm::vec3& node)
        {
            if (path.empty() ||
                glm::distance(glm::vec2(path.back().x, path.back().z), glm::vec2(node.x, node.z)) > 0.1f)
            {
                path.push_back(node);
            }
            else
            {
                path.back() = node;
            }
        };
        appendDistinct(approach);
        appendDistinct(destination);
        return path;
    }

    void FGameInstance::IssueMoveOrders(const glm::vec3& target, float facing)
    {
        std::vector<FRegiment*> selected;
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.selected) selected.push_back(&regiment);
        }
        if (selected.empty()) return;

        for (size_t index = 0; index < selected.size(); ++index)
        {
            FRegiment& regiment = *selected[index];
            glm::vec3 destination = target + RegimentOrderOffset(index, selected.size(), facing);
            destination.x = glm::clamp(destination.x, -188.0f, 188.0f);
            destination.z = glm::clamp(destination.z, -188.0f, 188.0f);
            destination.y = GroundHeight(destination.x, destination.z);
            regiment.orderTarget = destination;
            regiment.orderFacing = facing;
            regiment.path = BuildOrderPath(regiment, destination, facing);
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
                    if (regiment.selected)
                    {
                        regiment.ranks = glm::clamp(regiment.ranks + delta, 4, 32);
                        regiment.state = ERegimentState::Reforming;
                    }
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
                    IssueMoveOrders(rightTarget_, ResolveOrderFacing(rightTarget_, release));
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
        const Assets::Scene& scene = GetEngine().GetScene();
        const uint32_t batches = scene.GetIndirectDrawBatchCount();
        const auto& capacity = scene.RenderCapacityLimits();
        ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_Always);
        ImGui::Begin("NextTotalwar", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::Text("Regiments: %zu   Soldiers: %d", regiments_.size(), totalSoldierCount);
        ImGui::Text("Selected: %zu   Moving: %zu", SelectedCount(), MarchingCount());
        size_t sharedParts = 0;
        for (const auto& ids : soldierPartModelIds_) sharedParts += ids.size();
        ImGui::Text("ScadRig shared part meshes: %zu", sharedParts);
        const float budget = static_cast<float>(batches) /
                             static_cast<float>(capacity.visibilityProxyCapacity);
        ImGui::TextColored(budget > 0.90f ? ImVec4(1, 0.2f, 0.15f, 1)
                                         : ImVec4(0.3f, 1, 0.4f, 1),
                           "Render proxies: %u / %u (%s)", batches,
                           capacity.visibilityProxyCapacity,
                           capacity.IsMassive() ? "Massive" : "Default");
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
                                            ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_HorizontalScrollbar);
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.id > 0) ImGui::SameLine();
            if (regiment.selected) ImGui::PushStyleColor(ImGuiCol_Button, {0.15f, 0.62f, 0.85f, 1.0f});
            const std::string label =
                fmt::format("{}\n{}##{}", regiment.def->displayName,
                            regiment.soldiers.size(), regiment.id);
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
        const auto drawFormationFrame =
            [this, draw](const glm::vec3& anchor, float facing,
                         const glm::vec2& localMin, const glm::vec2& localMax,
                         ImU32 outlineColor, ImU32 fillColor)
            {
                const std::array<glm::vec2, 4> corners = {{
                    {localMin.x, localMax.y}, {localMax.x, localMax.y},
                    {localMax.x, localMin.y}, {localMin.x, localMin.y},
                }};
                std::array<ImVec2, 4> projected{};
                bool valid = true;
                for (size_t index = 0; index < corners.size(); ++index)
                {
                    glm::vec3 world = Formation::SlotWorld(anchor, facing, corners[index]);
                    world.y = GroundHeight(world.x, world.z) + 0.08f;
                    valid &= Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, projected[index]);
                }
                if (!valid) return;

                if ((fillColor & IM_COL32_A_MASK) != 0)
                {
                    draw->AddConvexPolyFilled(projected.data(), static_cast<int>(projected.size()), fillColor);
                }
                draw->AddPolyline(projected.data(), static_cast<int>(projected.size()), outlineColor,
                                  ImDrawFlags_Closed, 2.0f);

                const float centerX = (localMin.x + localMax.x) * 0.5f;
                const float frontY = localMax.y;
                const std::array<glm::vec2, 4> arrowLocal = {{
                    {centerX, frontY},
                    {centerX, frontY + 3.2f},
                    {centerX - 1.05f, frontY + 2.0f},
                    {centerX + 1.05f, frontY + 2.0f},
                }};
                std::array<ImVec2, 4> arrow{};
                for (size_t index = 0; index < arrowLocal.size(); ++index)
                {
                    glm::vec3 world = Formation::SlotWorld(anchor, facing, arrowLocal[index]);
                    world.y = GroundHeight(world.x, world.z) + 0.1f;
                    if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, arrow[index])) return;
                }
                draw->AddLine(arrow[0], arrow[1], outlineColor, 2.5f);
                draw->AddTriangleFilled(arrow[1], arrow[2], arrow[3], outlineColor);
            };

        if (leftDown_ && hasMouse_ && glm::distance(leftStart_, mousePos_) > 6.0)
        {
            const ImVec2 a(static_cast<float>(leftStart_.x), static_cast<float>(leftStart_.y));
            const ImVec2 b(static_cast<float>(mousePos_.x), static_cast<float>(mousePos_.y));
            draw->AddRectFilled(a, b, IM_COL32(40, 170, 255, 38));
            draw->AddRect(a, b, IM_COL32(80, 210, 255, 235), 0.0f, 0, 2.0f);
        }

        for (const FRegiment& regiment : regiments_)
        {
            if (regiment.state != ERegimentState::Marching ||
                regiment.pathCursor >= regiment.path.size())
            {
                continue;
            }

            glm::vec3 previousWorld = regiment.anchor;
            previousWorld.y = GroundHeight(previousWorld.x, previousWorld.z) + 0.14f;
            ImVec2 previous{};
            bool previousValid =
                Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, previousWorld, previous);
            const ImU32 routeColor = regiment.selected
                                         ? IM_COL32(65, 220, 255, 235)
                                         : (regiment.faction == 0
                                                ? IM_COL32(65, 170, 255, 125)
                                                : IM_COL32(255, 105, 80, 125));

            for (size_t nodeIndex = regiment.pathCursor;
                 nodeIndex < regiment.path.size(); ++nodeIndex)
            {
                glm::vec3 nodeWorld = regiment.path[nodeIndex];
                nodeWorld.y = GroundHeight(nodeWorld.x, nodeWorld.z) + 0.14f;
                ImVec2 node{};
                const bool nodeValid =
                    Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, nodeWorld, node);
                const bool finalSegment = nodeIndex + 1 == regiment.path.size();
                const ImU32 segmentColor =
                    finalSegment ? IM_COL32(255, 220, 70, 245) : routeColor;
                if (previousValid && nodeValid)
                {
                    draw->AddLine(previous, node, IM_COL32(10, 24, 34, 180), 5.0f);
                    draw->AddLine(previous, node, segmentColor, finalSegment ? 3.0f : 2.2f);

                    if (finalSegment)
                    {
                        const float dx = node.x - previous.x;
                        const float dy = node.y - previous.y;
                        const float length = std::sqrt(dx * dx + dy * dy);
                        if (length > 12.0f)
                        {
                            const float ux = dx / length;
                            const float uy = dy / length;
                            const ImVec2 tip(previous.x + dx * 0.72f,
                                             previous.y + dy * 0.72f);
                            const ImVec2 base(tip.x - ux * 11.0f, tip.y - uy * 11.0f);
                            const ImVec2 left(base.x - uy * 5.0f, base.y + ux * 5.0f);
                            const ImVec2 right(base.x + uy * 5.0f, base.y - ux * 5.0f);
                            draw->AddTriangleFilled(tip, left, right, segmentColor);
                        }
                    }
                }

                if (nodeValid)
                {
                    if (nodeIndex + 2 == regiment.path.size())
                    {
                        draw->AddCircleFilled(node, 4.5f, IM_COL32(255, 220, 70, 245), 12);
                        draw->AddCircle(node, 7.0f, IM_COL32(40, 35, 12, 210), 16, 1.5f);
                    }
                    else if (nodeIndex + 1 == regiment.path.size())
                    {
                        draw->AddCircle(node, 6.0f, IM_COL32(255, 235, 115, 245), 18, 2.0f);
                    }
                    else
                    {
                        draw->AddCircleFilled(node, 3.0f, routeColor, 10);
                    }
                }

                previous = node;
                previousValid = nodeValid;
            }
        }

        for (const FRegiment& regiment : regiments_)
        {
            if (!regiment.selected || regiment.soldiers.empty()) continue;
            glm::vec2 localMin(std::numeric_limits<float>::max());
            glm::vec2 localMax(std::numeric_limits<float>::lowest());
            for (const FSoldier& soldier : regiment.soldiers)
            {
                const glm::vec2 local =
                    Formation::SlotLocal(regiment.anchor, regiment.facing, soldier.position);
                localMin = glm::min(localMin, local);
                localMax = glm::max(localMax, local);
            }
            constexpr float selectionPadding = 0.7f;
            localMin -= glm::vec2(selectionPadding);
            localMax += glm::vec2(selectionPadding);
            drawFormationFrame(regiment.anchor, regiment.facing, localMin, localMax,
                               IM_COL32(70, 235, 255, 240), IM_COL32(40, 180, 220, 18));
        }
        if (rightDown_)
        {
            glm::vec3 facingPoint{};
            const bool hasFacingPoint = TryGroundHit(mousePos_, facingPoint);
            const float previewFacing = ResolveOrderFacing(
                rightTarget_, hasFacingPoint ? facingPoint : rightTarget_);
            std::vector<const FRegiment*> selected;
            for (const FRegiment& regiment : regiments_)
            {
                if (regiment.selected) selected.push_back(&regiment);
            }
            for (size_t index = 0; index < selected.size(); ++index)
            {
                const FRegiment& regiment = *selected[index];
                glm::vec3 destination =
                    rightTarget_ + RegimentOrderOffset(index, selected.size(), previewFacing);
                destination.x = glm::clamp(destination.x, -188.0f, 188.0f);
                destination.z = glm::clamp(destination.z, -188.0f, 188.0f);
                destination.y = GroundHeight(destination.x, destination.z);
                const glm::vec2 halfExtent =
                    Formation::FormationHalfExtent(static_cast<int>(regiment.soldiers.size()),
                                                   regiment.ranks,
                                                   regiment.def->fileSpacing,
                                                   regiment.def->rankSpacing);
                constexpr float previewPadding = 0.7f;
                const glm::vec2 localMin(-halfExtent.x - previewPadding,
                                         -halfExtent.y * 2.0f - previewPadding);
                const glm::vec2 localMax(halfExtent.x + previewPadding, previewPadding);
                drawFormationFrame(destination, previewFacing, localMin, localMax,
                                   IM_COL32(255, 225, 80, 245), IM_COL32(255, 205, 45, 34));
            }

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
        registry.Add("soldierCount", [this]()
        {
            size_t count = 0;
            for (const FRegiment& regiment : regiments_) count += regiment.soldiers.size();
            return static_cast<int64_t>(count);
        });
        registry.Add("renderProxyCount", [this]()
        {
            return static_cast<int64_t>(GetEngine().GetScene().GetRenderProxyCount());
        });
        registry.Add("massiveMode", [this]()
        {
            return GetEngine().GetScene().RenderCapacityLimits().IsMassive();
        });
        registry.Add("navReady", [this]() { return navGrid_.IsBuilt(); });
        registry.Add("lastOrderDistance", [this]() { return static_cast<double>(lastOrderDistance_); });
        registry.Add("routeNodeCount", [this]()
        {
            size_t count = 0;
            for (const FRegiment& regiment : regiments_)
            {
                if (regiment.selected && regiment.state == ERegimentState::Marching &&
                    regiment.pathCursor < regiment.path.size())
                {
                    count += regiment.path.size() - regiment.pathCursor;
                }
            }
            return static_cast<int64_t>(count);
        });
        registry.Add("finalApproachAligned", [this]()
        {
            bool foundRoute = false;
            for (const FRegiment& regiment : regiments_)
            {
                if (!regiment.selected || regiment.state != ERegimentState::Marching) continue;
                if (regiment.path.size() < 2) return false;
                const glm::vec3& approach = regiment.path[regiment.path.size() - 2];
                const glm::vec3& destination = regiment.path.back();
                const glm::vec2 delta(destination.x - approach.x, destination.z - approach.z);
                const float length = glm::length(delta);
                if (length < 0.1f) return false;
                const glm::vec2 forward(std::sin(regiment.orderFacing),
                                        std::cos(regiment.orderFacing));
                if (glm::dot(delta / length, forward) < 0.999f) return false;
                foundRoute = true;
            }
            return foundRoute;
        });
    }

    void FGameInstance::OnDestroy()
    {
    }
}
