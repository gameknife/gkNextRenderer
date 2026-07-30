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
#include <chrono>
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

    ImU32 FactionOutlineColor(int faction, int alpha = 240)
    {
        return faction == 0
                   ? IM_COL32(45, 165, 255, alpha)
                   : IM_COL32(255, 68, 52, alpha);
    }

    ImU32 FactionFillColor(int faction, int alpha = 28)
    {
        return faction == 0
                   ? IM_COL32(30, 125, 255, alpha)
                   : IM_COL32(245, 45, 35, alpha);
    }

    bool PointInConvexQuad(const glm::vec2& point, const std::array<glm::vec2, 4>& quad)
    {
        bool hasPositive = false;
        bool hasNegative = false;
        for (size_t index = 0; index < quad.size(); ++index)
        {
            const glm::vec2 edge = quad[(index + 1) % quad.size()] - quad[index];
            const glm::vec2 relative = point - quad[index];
            const float cross = edge.x * relative.y - edge.y * relative.x;
            hasPositive |= cross > 0.0f;
            hasNegative |= cross < 0.0f;
            if (hasPositive && hasNegative) return false;
        }
        return true;
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
        using NextCVar::ECVarFlags;
        std::string error;
        // 尖刀 C：SoftwareModern 在大批动态实例的启动/帧时间上明显优于
        // SoftwareModernNoAmbient，且保留 low-poly 场景所需的光栅 + GI。
        cvars.SetDefaultFromString("r.rendererType", "0", &error);
        //cvars.SetDefaultFromString("r.temporalFrames", "4", &error);
        cvars.RegisterBool("tw.combat.enabled", true, &combatTuning_.enabled, ECVarFlags::None,
                           "Enable the NextTotalwar melee simulation.");
        cvars.RegisterFloat("tw.combat.tickRate", 20.0f, &combatTuning_.tickRate, ECVarFlags::None,
                            "Fixed combat simulation ticks per second.");
        cvars.RegisterFloat("tw.combat.engageMargin", 1.6f, &combatTuning_.engageMargin, ECVarFlags::None,
                            "Extra regiment contact margin in metres.");
        cvars.RegisterFloat("tw.combat.regimentEngageDistance",
                            combatTuning_.regimentEngageDistance,
                            &combatTuning_.regimentEngageDistance, ECVarFlags::None,
                            "Front-line distance at which two regiments enter combat.");
        cvars.RegisterFloat("tw.combat.searchRadius", 2.4f, &combatTuning_.searchRadius, ECVarFlags::None,
                            "Soldier target search radius in metres.");
        cvars.RegisterInt("tw.combat.maxAttackersPerTarget", 3,
                          &combatTuning_.maxAttackersPerTarget, ECVarFlags::None,
                          "Maximum soldiers locking one target.");
        cvars.RegisterFloat("tw.combat.targetLateralPenalty", 3.0f,
                            &combatTuning_.targetLateralPenalty, ECVarFlags::None,
                            "Target matching penalty for crossing the battle line.");
        cvars.RegisterFloat("tw.combat.engagementArcDegrees", 140.0f,
                            &combatTuning_.engagementArcDegrees, ECVarFlags::None,
                            "Arc occupied by soldiers sharing one melee target.");
        cvars.RegisterFloat("tw.combat.separationRadius", 0.85f,
                            &combatTuning_.separationRadius, ECVarFlags::None,
                            "Friendly soldier soft-separation radius in metres.");
        cvars.RegisterFloat("tw.combat.separationStrength", 1.4f,
                            &combatTuning_.separationStrength, ECVarFlags::None,
                            "Strength of lateral friendly separation while fighting.");
        cvars.RegisterFloat("tw.combat.maxBreakDistance", 2.5f,
                            &combatTuning_.maxBreakDistance, ECVarFlags::None,
                            "Maximum distance a fighting soldier may leave its slot.");
        cvars.RegisterFloat("tw.combat.hitBase", 0.45f, &combatTuning_.hitBase, ECVarFlags::None,
                            "Base melee hit probability.");
        cvars.RegisterFloat("tw.combat.hitScale", 0.03f, &combatTuning_.hitScale, ECVarFlags::None,
                            "Hit probability per attack-defense point.");
        cvars.RegisterFloat("tw.combat.chargeWindow", 4.0f, &combatTuning_.chargeWindow,
                            ECVarFlags::None, "Charge bonus duration in seconds.");
        cvars.RegisterInt("tw.combat.flankBonus", 3, &combatTuning_.flankBonus, ECVarFlags::None,
                          "Flanking attack bonus.");
        cvars.RegisterInt("tw.combat.rearBonus", 6, &combatTuning_.rearBonus, ECVarFlags::None,
                          "Rear attack bonus.");
        cvars.RegisterFloat("tw.fx.deathClipSeconds", 0.8f,
                            &combatTuning_.deathClipSeconds, ECVarFlags::None,
                            "Death animation duration before a corpse freezes.");
        cvars.RegisterInt("tw.battle.seed", 1337, &battleSeed_, ECVarFlags::StartupOnly,
                          "Deterministic battle random seed.");
        cvars.RegisterFloat("tw.battle.deployDistance", 125.0f, &battleDeployDistance_,
                            ECVarFlags::None, "Army deployment distance from the battlefield centre.");
        cvars.RegisterBool("tw.determinism", GOption->AgentValidation, &deterministicCombat_,
                           ECVarFlags::None, "Advance exactly one combat tick per rendered frame.");
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
            {0.03f, 0.18f, 0.85f}, {0.04f, 0.28f, 0.95f}, {0.08f, 0.38f, 1.00f},
            {0.02f, 0.22f, 0.72f}, {0.10f, 0.30f, 0.82f}, {0.04f, 0.35f, 0.72f},
        }};
        const std::array<glm::vec3, 6> red = {{
            {0.85f, 0.035f, 0.025f}, {0.95f, 0.05f, 0.03f}, {1.00f, 0.10f, 0.05f},
            {0.78f, 0.025f, 0.04f}, {0.90f, 0.09f, 0.03f}, {0.75f, 0.04f, 0.07f},
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
        combatSystem_.Reset(static_cast<uint64_t>(battleSeed_));
        battleState_.events.clear();
        battleState_.combatTicks = 0;
        combatAccumulator_ = 0.0f;
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
        soldierVisuals_.clear();
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
                const float x = faction == 0
                                    ? -battleDeployDistance_ + column * 30.0f
                                    : battleDeployDistance_ - column * 30.0f;
                const float z = -66.0f + row * 44.0f + (column % 2) * 3.0f;
                regiment.anchor = {x, GroundHeight(x, z), z};
                regiment.facing = faction == 0 ? glm::half_pi<float>() : -glm::half_pi<float>();
                regiment.orderFacing = regiment.facing;
                regiment.soldiers.resize(soldiersPerRegiment);
                regiment.strength = soldiersPerRegiment;
                regiment.startStrength = soldiersPerRegiment;
                regiment.morale = CombatDef(regiment.def->type).baseMorale;
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
                    item.health = static_cast<int16_t>(CombatDef(regiment.def->type).maxHealth);
                    item.attackTimer = CombatDef(regiment.def->type).attackInterval * item.phaseOffset;
                }
                regiments_.push_back(std::move(regiment));
            }
        }
    }

    void FGameInstance::CreateSoldierVisuals()
    {
        Assets::Scene& scene = GetEngine().GetScene();
        soldierVisuals_.clear();
        soldierVisuals_.resize(regiments_.size());
        for (size_t regimentIndex = 0; regimentIndex < regiments_.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments_[regimentIndex];
            std::vector<FSoldierVisual>& visuals = soldierVisuals_[regimentIndex];
            visuals.resize(regiment.soldiers.size());
            const uint32_t typeIndex = static_cast<uint32_t>(regiment.def->type);
            const Assets::FRigAsset& rig = soldierRigAssets_[typeIndex];
            const uint32_t tintMaterial = regimentMaterialIds_[regiment.faction][regiment.id % 6];
            for (size_t index = 0; index < regiment.soldiers.size(); ++index)
            {
                FSoldier& soldier = regiment.soldiers[index];
                FSoldierVisual& visual = visuals[index];
                visual.worldNode = Assets::Node::CreateNode(
                    fmt::format("NTW/R{}/S{}", regiment.id, index),
                    soldier.position,
                    glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f)),
                    glm::vec3(1.0f),
                    scene.GenerateInstanceId());
                auto physics = std::make_shared<Runtime::PhysicsComponent>();
                physics->SetMobility(Runtime::ENodeMobility::Dynamic);
                visual.worldNode->AddComponent(physics);
                scene.AddNode(visual.worldNode);

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
                rigRoot->SetParent(visual.worldNode);
                visual.renderNodes.clear();
                for (size_t nodeIndex = firstRigNode; nodeIndex < scene.Nodes().size(); ++nodeIndex)
                {
                    Assets::Node* node = scene.Nodes()[nodeIndex].get();
                    if (node->GetComponentPtr<Runtime::RenderComponent>())
                    {
                        visual.renderNodes.push_back(node);
                    }
                }
                visual.animator.Bind(&rig, std::move(boneNodes), visual.worldNode.get());
                visual.animator.SetPhaseOffset(soldier.phaseOffset);
                visual.animator.Play("idle", 0.0f);
                visual.worldNode->RecalcTransform(true);
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
        if (camera_.IsFollowing())
        {
            glm::vec3 center{};
            if (TrySelectedCenter(center)) camera_.SetFollowTarget(center, false);
            else camera_.ClearFollowTarget();
        }
        camera_.Tick(dt, terrain_);
        TickRegiments(dt);
        const auto combatStart = std::chrono::steady_clock::now();
        const uint64_t combatTicksBefore = battleState_.combatTicks;
        const float combatStep = 1.0f / std::max(combatTuning_.tickRate, 1.0f);
        if (deterministicCombat_)
        {
            combatSystem_.Tick(combatStep, regiments_, combatTuning_, battleState_);
        }
        else
        {
            combatAccumulator_ += dt;
            int substeps = 0;
            while (combatAccumulator_ >= combatStep && substeps < 3)
            {
                combatSystem_.Tick(combatStep, regiments_, combatTuning_, battleState_);
                combatAccumulator_ -= combatStep;
                ++substeps;
            }
        }
        combatTicksThisFrame_ =
            static_cast<int>(battleState_.combatTicks - combatTicksBefore);
        combatCpuMilliseconds_ = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - combatStart).count();
        TickSoldiers(dt);
        combatFx_.Tick(dt, regiments_, soldierVisuals_, battleState_.events);
        lastCombatEventCount_ = static_cast<int>(battleState_.events.size());
        battleState_.events.clear();
        GetEngine().GetScene().MarkTransformDirty();
        ++frameIndex_;
    }

    void FGameInstance::TickRegiments(float deltaSeconds)
    {
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.state == ERegimentState::Idle ||
                regiment.state == ERegimentState::Engaged ||
                regiment.state == ERegimentState::Routing ||
                regiment.state == ERegimentState::Destroyed)
            {
                continue;
            }
            if (regiment.state == ERegimentState::Marching)
            {
                if (regiment.pathCursor >= regiment.path.size())
                {
                    Formation::RepackSlots(regiment);
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
                    if (soldier.combatState == ESoldierState::Dying ||
                        soldier.combatState == ESoldierState::Dead)
                    {
                        continue;
                    }
                    const glm::vec2 local = Formation::SlotLocalOffset(
                        soldier.slotIndex, regiment.strength, regiment.ranks,
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
        movementGrid_.Build(regiments_);
        std::vector<FCombatGridEntry> nearbySoldiers;
        nearbySoldiers.reserve(24);
        const uint32_t animationStride = camera_.Distance() < 90.0f
                                             ? 1u
                                             : (camera_.Distance() < 140.0f ? 3u : 8u);
        for (size_t regimentIndex = 0; regimentIndex < regiments_.size(); ++regimentIndex)
        {
            FRegiment& regiment = regiments_[regimentIndex];
            const bool regimentMoving =
                regiment.state == ERegimentState::Marching ||
                regiment.state == ERegimentState::Charging ||
                regiment.state == ERegimentState::Reforming ||
                regiment.state == ERegimentState::Routing;
            for (size_t soldierIndex = 0; soldierIndex < regiment.soldiers.size(); ++soldierIndex)
            {
                FSoldier& soldier = regiment.soldiers[soldierIndex];
                FSoldierVisual& visual = soldierVisuals_[regimentIndex][soldierIndex];
                if (soldier.combatState == ESoldierState::Dying ||
                    soldier.combatState == ESoldierState::Dead)
                {
                    continue;
                }
                bool transformChanged = false;
                float distance = 0.0f;
                const int slotCount = regiment.state == ERegimentState::Engaged
                                          ? regiment.startStrength
                                          : regiment.strength;
                const glm::vec2 local = Formation::SlotLocalOffset(
                    soldier.slotIndex, slotCount, regiment.ranks,
                    regiment.def->fileSpacing, regiment.def->rankSpacing);
                glm::vec3 slot = Formation::SlotWorld(regiment.anchor, regiment.facing, local);
                slot.y = GroundHeight(slot.x, slot.z);

                bool validFightTarget = false;
                glm::vec3 fightTarget{};
                if (soldier.combatState == ESoldierState::Fighting &&
                    soldier.targetRegiment >= 0 && soldier.targetSoldier >= 0 &&
                    static_cast<size_t>(soldier.targetRegiment) < regiments_.size() &&
                    static_cast<size_t>(soldier.targetSoldier) <
                        regiments_[soldier.targetRegiment].soldiers.size())
                {
                    const FSoldier& target =
                        regiments_[soldier.targetRegiment].soldiers[soldier.targetSoldier];
                    validFightTarget =
                        target.combatState != ESoldierState::Dying &&
                        target.combatState != ESoldierState::Dead;
                    glm::vec3 approachDirection =
                        regiment.anchor - regiments_[soldier.targetRegiment].anchor;
                    approachDirection.y = 0.0f;
                    float approachLength =
                        glm::length(glm::vec2(approachDirection.x, approachDirection.z));
                    if (approachLength < 0.001f)
                    {
                        approachDirection = soldier.position - target.position;
                        approachDirection.y = 0.0f;
                        approachLength =
                            glm::length(glm::vec2(approachDirection.x, approachDirection.z));
                    }
                    if (approachLength < 0.001f)
                    {
                        approachDirection = {
                            std::sin(regiment.facing), 0.0f, std::cos(regiment.facing)};
                    }
                    else
                    {
                        approachDirection /= approachLength;
                    }

                    const int maxSlotRing =
                        std::max(1, combatTuning_.maxAttackersPerTarget / 2);
                    const float slotFraction =
                        static_cast<float>(soldier.engagementSlot) /
                        static_cast<float>(maxSlotRing);
                    const float slotAngle = glm::radians(
                        combatTuning_.engagementArcDegrees * 0.5f * slotFraction);
                    const float slotSine = std::sin(slotAngle);
                    const float slotCosine = std::cos(slotAngle);
                    const glm::vec3 slottedDirection{
                        slotCosine * approachDirection.x +
                            slotSine * approachDirection.z,
                        0.0f,
                        -slotSine * approachDirection.x +
                            slotCosine * approachDirection.z};
                    fightTarget = target.position +
                        slottedDirection *
                            (CombatDef(regiment.def->type).weaponReach * 0.85f);
                }

                if (validFightTarget)
                {
                    glm::vec3 delta = fightTarget - soldier.position;
                    delta.y = 0.0f;
                    distance = glm::length(glm::vec2(delta.x, delta.z));
                    glm::vec3 direction =
                        distance > 0.001f
                            ? glm::vec3(delta.x / distance, 0.0f, delta.z / distance)
                            : glm::vec3(
                                  std::sin(regiment.facing), 0.0f, std::cos(regiment.facing));
                    const glm::vec3 side{-direction.z, 0.0f, direction.x};
                    float separation = 0.0f;
                    glm::vec3 enemySeparation{};
                    movementGrid_.Query(
                        soldier.position, combatTuning_.separationRadius, nearbySoldiers);
                    for (const FCombatGridEntry& entry : nearbySoldiers)
                    {
                        if (entry.regiment < 0 ||
                            (entry.regiment == static_cast<int>(regimentIndex) &&
                             entry.soldier == static_cast<int>(soldierIndex)))
                        {
                            continue;
                        }
                        const FSoldier& neighbour =
                            regiments_[entry.regiment].soldiers[entry.soldier];
                        glm::vec3 away = soldier.position - neighbour.position;
                        away.y = 0.0f;
                        const float neighbourDistance =
                            glm::length(glm::vec2(away.x, away.z));
                        if (neighbourDistance >= combatTuning_.separationRadius) continue;
                        if (neighbourDistance > 0.001f)
                        {
                            away /= neighbourDistance;
                            const float weight =
                                1.0f - neighbourDistance / combatTuning_.separationRadius;
                            if (regiments_[entry.regiment].faction == regiment.faction)
                            {
                                separation += glm::dot(away, side) * weight;
                            }
                            else
                            {
                                enemySeparation += away * weight;
                            }
                        }
                        else
                        {
                            const float sign =
                                soldierIndex < static_cast<size_t>(entry.soldier) ? -1.0f : 1.0f;
                            if (regiments_[entry.regiment].faction == regiment.faction)
                            {
                                separation += sign;
                            }
                            else
                            {
                                enemySeparation += side * sign;
                            }
                        }
                    }
                    const float lateralSteering = glm::clamp(
                        separation * combatTuning_.separationStrength, -1.0f, 1.0f);
                    const glm::vec3 steeredDelta =
                        delta +
                        (side * lateralSteering +
                         enemySeparation * combatTuning_.separationStrength) *
                            combatTuning_.separationRadius;
                    const float steeredDistance =
                        glm::length(glm::vec2(steeredDelta.x, steeredDelta.z));
                    if (steeredDistance > 0.035f)
                    {
                        direction = steeredDelta / steeredDistance;
                        const float step =
                            std::min(steeredDistance, regiment.def->marchSpeed * deltaSeconds);
                        glm::vec3 candidate = soldier.position + direction * step;
                        glm::vec3 fromSlot = candidate - slot;
                        fromSlot.y = 0.0f;
                        const float slotDistance = glm::length(glm::vec2(fromSlot.x, fromSlot.z));
                        float combatLeash = combatTuning_.maxBreakDistance;
                        if (regiment.state == ERegimentState::Engaged)
                        {
                            const glm::vec2 formationExtent =
                                Formation::FormationHalfExtent(
                                    regiment.startStrength, regiment.ranks,
                                    regiment.def->fileSpacing, regiment.def->rankSpacing);
                            combatLeash += formationExtent.y * 2.0f +
                                           combatTuning_.regimentEngageDistance;
                        }
                        if (slotDistance > combatLeash)
                        {
                            candidate = slot +
                                fromSlot * (combatLeash / slotDistance);
                        }
                        soldier.position = candidate;
                        soldier.position.y = GroundHeight(soldier.position.x, soldier.position.z);
                        transformChanged = true;
                    }
                    if (distance > 0.001f)
                    {
                        soldier.yaw = ApproachAngle(
                            soldier.yaw, std::atan2(delta.x, delta.z), deltaSeconds * 8.0f);
                    }
                    transformChanged = true;
                }
                else if (regimentMoving)
                {
                    const glm::vec3 delta = slot - soldier.position;
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
                        soldier.position = slot;
                        soldier.yaw = ApproachAngle(soldier.yaw, regiment.facing, deltaSeconds * 5.0f);
                    }
                    transformChanged = true;
                }

                if (visual.worldNode)
                {
                    const bool animate =
                        (frameIndex_ + static_cast<uint64_t>(regiment.id * 17 + soldier.slotIndex)) %
                            animationStride ==
                        0u;
                    const bool fighting = soldier.combatState == ESoldierState::Fighting;
                    const bool moving = !fighting && (regimentMoving || distance > 0.035f);
                    const Assets::FRigAsset& rig =
                        soldierRigAssets_[static_cast<size_t>(regiment.def->type)];
                    const char* clip = fighting && rig.FindClip("attack")
                                           ? "attack"
                                           : (moving && rig.FindClip("march")
                                                  ? "march"
                                                  : (moving ? "walk" : "idle"));
                    visual.animator.Play(clip);
                    if (transformChanged)
                    {
                        visual.worldNode->Translation() = soldier.position;
                        visual.worldNode->Rotation() =
                            glm::angleAxis(soldier.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                    }
                    if (animate)
                    {
                        visual.animator.Update(deltaSeconds * static_cast<float>(animationStride));
                        ++animatorUpdates_;
                    }
                    else if (transformChanged)
                    {
                        visual.worldNode->RecalcTransform(true);
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
        camera_.ClearFollowTarget();
    }

    bool FGameInstance::TryProjectRegimentBounds(
        const FRegiment& regiment, std::array<glm::vec2, 4>& projected) const
    {
        if (regiment.strength <= 0) return false;

        glm::vec2 localMin(std::numeric_limits<float>::max());
        glm::vec2 localMax(std::numeric_limits<float>::lowest());
        bool foundAlive = false;
        for (const FSoldier& soldier : regiment.soldiers)
        {
            if (soldier.combatState == ESoldierState::Dying ||
                soldier.combatState == ESoldierState::Dead)
            {
                continue;
            }
            const glm::vec2 local =
                Formation::SlotLocal(regiment.anchor, regiment.facing, soldier.position);
            localMin = glm::min(localMin, local);
            localMax = glm::max(localMax, local);
            foundAlive = true;
        }
        if (!foundAlive) return false;
        constexpr float selectionPadding = 0.7f;
        localMin -= glm::vec2(selectionPadding);
        localMax += glm::vec2(selectionPadding);
        const std::array<glm::vec2, 4> corners = {{
            {localMin.x, localMax.y}, {localMax.x, localMax.y},
            {localMax.x, localMin.y}, {localMin.x, localMin.y},
        }};
        for (size_t index = 0; index < corners.size(); ++index)
        {
            glm::vec3 world =
                Formation::SlotWorld(regiment.anchor, regiment.facing, corners[index]);
            world.y = GroundHeight(world.x, world.z) + 0.08f;
            ImVec2 screen{};
            if (!Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, world, screen))
            {
                return false;
            }
            projected[index] = {screen.x, screen.y};
        }
        return true;
    }

    FRegiment* FGameInstance::PickRegimentAt(const glm::dvec2& screen)
    {
        const glm::vec2 point(screen);
        FRegiment* contained = nullptr;
        float containedDistance = std::numeric_limits<float>::max();
        FRegiment* nearby = nullptr;
        float nearbyDistance = 32.0f;
        for (FRegiment& regiment : regiments_)
        {
            if (!IsRegimentSelectable(regiment)) continue;
            std::array<glm::vec2, 4> bounds{};
            if (TryProjectRegimentBounds(regiment, bounds) &&
                PointInConvexQuad(point, bounds))
            {
                glm::vec2 center{};
                for (const glm::vec2& corner : bounds) center += corner;
                center /= static_cast<float>(bounds.size());
                const float distance = glm::distance(point, center);
                if (distance < containedDistance)
                {
                    containedDistance = distance;
                    contained = &regiment;
                }
            }

            ImVec2 anchor{};
            if (Runtime::EngineHelper::TryProjectWorldToScreenForGame(*this, regiment.anchor, anchor))
            {
                const float distance = glm::distance(point, glm::vec2(anchor.x, anchor.y));
                if (distance < nearbyDistance)
                {
                    nearbyDistance = distance;
                    nearby = &regiment;
                }
            }
        }
        return contained ? contained : nearby;
    }

    void FGameInstance::SelectAt(const glm::dvec2& screen, bool additive)
    {
        if (!additive) ClearSelection();
        FRegiment* picked = PickRegimentAt(screen);
        if (picked) picked->selected = additive ? !picked->selected : true;
        RefreshSelectionFeedback();
    }

    void FGameInstance::SelectAllTypeAt(const glm::dvec2& screen)
    {
        const FRegiment* picked = PickRegimentAt(screen);
        if (!picked) return;
        const FUnitDef* pickedDef = picked->def;
        ClearSelection();
        for (FRegiment& regiment : regiments_)
        {
            regiment.selected = IsRegimentSelectable(regiment) && regiment.def == pickedDef;
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
            if (!IsRegimentSelectable(regiment)) continue;
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
        for (size_t regimentIndex = 0; regimentIndex < regiments_.size(); ++regimentIndex)
        {
            if (!IsRegimentSelectable(regiments_[regimentIndex]))
            {
                regiments_[regimentIndex].selected = false;
            }
            for (FSoldierVisual& visual : soldierVisuals_[regimentIndex])
            {
                for (Assets::Node* renderNode : visual.renderNodes)
                {
                    if (auto render = renderNode->GetComponent<Runtime::RenderComponent>())
                    {
                        render->SetOutlineFlags(Runtime::RenderOutlineFlags::none);
                    }
                }
            }
        }
        if (SelectedCount() == 0) camera_.ClearFollowTarget();
        GetEngine().GetScene().MarkDirty();
    }

    bool FGameInstance::TrySelectedCenter(glm::vec3& center) const
    {
        center = {};
        size_t count = 0;
        for (const FRegiment& regiment : regiments_)
        {
            if (!regiment.selected || !IsRegimentSelectable(regiment)) continue;
            center += regiment.anchor;
            ++count;
        }
        if (count == 0) return false;
        center /= static_cast<float>(count);
        center.y = GroundHeight(center.x, center.z);
        return true;
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
            if (regiment.selected && IsRegimentSelectable(regiment)) return regiment.facing;
        }
        return 0.0f;
    }

    std::vector<glm::vec3> FGameInstance::BuildOrderPath(
        const FRegiment& regiment, const glm::vec3& destination, float facing) const
    {
        const float approachDistance =
            Formation::FormationHalfExtent(regiment.strength,
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
            if (regiment.selected && IsRegimentSelectable(regiment)) selected.push_back(&regiment);
        }
        if (selected.empty()) return;

        std::vector<glm::vec3> starts;
        std::vector<glm::vec3> destinations;
        starts.reserve(selected.size());
        destinations.reserve(selected.size());
        for (size_t index = 0; index < selected.size(); ++index)
        {
            Formation::PrepareNearestReform(*selected[index]);
            starts.push_back(selected[index]->anchor);
            glm::vec3 destination = target + RegimentOrderOffset(index, selected.size(), facing);
            destination.x = glm::clamp(destination.x, -188.0f, 188.0f);
            destination.z = glm::clamp(destination.z, -188.0f, 188.0f);
            destination.y = GroundHeight(destination.x, destination.z);
            destinations.push_back(destination);
        }
        const std::vector<size_t> assignment =
            Formation::MinimumTravelAssignment(starts, destinations);

        for (size_t index = 0; index < selected.size(); ++index)
        {
            FRegiment& regiment = *selected[index];
            const bool leavingCombat =
                regiment.state == ERegimentState::Engaged ||
                !regiment.engagedWith.empty();
            const glm::vec3& destination = destinations[assignment[index]];
            regiment.orderTarget = destination;
            regiment.orderFacing = facing;
            regiment.path = BuildOrderPath(regiment, destination, facing);
            regiment.pathCursor = regiment.path.size() > 1 ? 1 : 0;
            regiment.state = ERegimentState::Marching;
            regiment.disengaging = leavingCombat;
            if (leavingCombat)
            {
                regiment.engagedWith.clear();
                for (FSoldier& soldier : regiment.soldiers)
                {
                    soldier.targetRegiment = -1;
                    soldier.targetSoldier = -1;
                    soldier.engagementSlot = -1;
                    if (soldier.combatState == ESoldierState::Fighting)
                    {
                        soldier.combatState = ESoldierState::Formation;
                    }
                }
            }
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
        case SDLK_F:
            if (down)
            {
                if (camera_.IsFollowing())
                {
                    camera_.ClearFollowTarget();
                }
                else
                {
                    glm::vec3 center{};
                    if (TrySelectedCenter(center)) camera_.SetFollowTarget(center, true);
                }
            }
            return true;
        case SDLK_F1:
        case SDLK_F5:
            if (down) showDebug_ = !showDebug_;
            return true;
        case SDLK_LEFTBRACKET:
        case SDLK_RIGHTBRACKET:
            if (down)
            {
                const int delta = event.key.key == SDLK_LEFTBRACKET ? -1 : 1;
                for (FRegiment& regiment : regiments_)
                {
                    if (regiment.selected && IsRegimentSelectable(regiment))
                    {
                        regiment.ranks = glm::clamp(regiment.ranks + delta, 4, 32);
                        Formation::RepackSlots(regiment);
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
        const glm::dvec2 nextMouse = ToLogicalMouse(x, y);
        if (middleDown_)
        {
            camera_.PanByScreenDelta(glm::vec2(nextMouse - middleLast_));
            middleLast_ = nextMouse;
        }
        mousePos_ = nextMouse;
        hasMouse_ = true;
        return leftDown_ || rightDown_ || middleDown_;
    }

    bool FGameInstance::OnMouseButton(SDL_Event& event)
    {
        if (event.type != SDL_EVENT_MOUSE_BUTTON_DOWN && event.type != SDL_EVENT_MOUSE_BUTTON_UP) return false;
        mousePos_ = ToLogicalMouse(event.button.x, event.button.y);
        hasMouse_ = true;
        if (event.button.button == SDL_BUTTON_MIDDLE)
        {
            middleDown_ = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            middleLast_ = mousePos_;
            return true;
        }
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
                                                [](const FRegiment& regiment)
                                                {
                                                    return regiment.selected &&
                                                           IsRegimentSelectable(regiment);
                                                }));
    }

    size_t FGameInstance::MarchingCount() const
    {
        return static_cast<size_t>(std::count_if(regiments_.begin(), regiments_.end(),
                                                [](const FRegiment& regiment)
                                                {
                                                    return regiment.state == ERegimentState::Marching ||
                                                           regiment.state == ERegimentState::Reforming;
                                                }));
    }

    const char* FGameInstance::StateName(ERegimentState state)
    {
        switch (state)
        {
        case ERegimentState::Marching: return "Marching";
        case ERegimentState::Reforming: return "Reforming";
        case ERegimentState::Engaged: return "Engaged";
        case ERegimentState::Charging: return "Charging";
        case ERegimentState::Routing: return "Routing";
        case ERegimentState::Destroyed: return "Destroyed";
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
        int aliveSoldiers = 0;
        for (const FRegiment& regiment : regiments_) aliveSoldiers += regiment.strength;
        ImGui::Text("Regiments: %zu   Soldiers: %d / %d",
                    regiments_.size(), aliveSoldiers, totalSoldierCount);
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
        ImGui::TextUnformatted("WASD/MMB drag: pan  Q/E: rotate  Wheel: zoom");
        ImGui::TextUnformatted("F: follow selected regiments");
        ImGui::TextUnformatted("F5: toggle battle debug");
        ImGui::TextUnformatted("[/]: selected formation ranks");
        if (showDebug_)
        {
            ImGui::Separator();
            for (const FRegiment& regiment : regiments_)
            {
                if (!regiment.selected || !IsRegimentSelectable(regiment)) continue;
                const ImVec4 factionColor = regiment.faction == 0
                                                ? ImVec4(0.25f, 0.70f, 1.0f, 1.0f)
                                                : ImVec4(1.0f, 0.30f, 0.22f, 1.0f);
                ImGui::TextColored(
                    factionColor, "%s #%d  %s  %s  ranks=%d",
                    regiment.faction == 0 ? "BLUE" : "RED",
                    regiment.id, regiment.def->displayName,
                    StateName(regiment.state), regiment.ranks);
                ImGui::Text("Strength %d/%d  Kills %d  Morale %.0f",
                            regiment.strength, regiment.startStrength,
                            regiment.kills, regiment.morale);
            }
            ImGui::Text("Last order distance: %.1f m", lastOrderDistance_);
        }
        ImGui::End();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (showDebug_)
        {
            int engagedRegiments = 0;
            int routingRegiments = 0;
            int destroyedRegiments = 0;
            int disengagingRegiments = 0;
            int pursuingRegiments = 0;
            int fightingSoldiers = 0;
            int dyingSoldiers = 0;
            std::array<int, 2> factionStrength{};
            std::array<int, 2> factionKills{};
            for (const FRegiment& regiment : regiments_)
            {
                engagedRegiments += regiment.state == ERegimentState::Engaged ? 1 : 0;
                routingRegiments += regiment.state == ERegimentState::Routing ? 1 : 0;
                destroyedRegiments += regiment.state == ERegimentState::Destroyed ? 1 : 0;
                disengagingRegiments += regiment.disengaging ? 1 : 0;
                pursuingRegiments += std::any_of(
                    regiment.engagedWith.begin(), regiment.engagedWith.end(),
                    [this](int16_t target)
                    {
                        return target >= 0 &&
                               static_cast<size_t>(target) < regiments_.size() &&
                               regiments_[target].disengaging;
                    }) ? 1 : 0;
                if (regiment.faction >= 0 && regiment.faction < 2)
                {
                    factionStrength[regiment.faction] += regiment.strength;
                    factionKills[regiment.faction] += regiment.kills;
                }
                for (const FSoldier& soldier : regiment.soldiers)
                {
                    fightingSoldiers += soldier.combatState == ESoldierState::Fighting ? 1 : 0;
                    dyingSoldiers += soldier.combatState == ESoldierState::Dying ? 1 : 0;
                }
            }

            ImGui::SetNextWindowPos(
                {viewport->WorkPos.x + viewport->WorkSize.x - 332.0f,
                 viewport->WorkPos.y + 12.0f},
                ImGuiCond_Always);
            ImGui::SetNextWindowSize({320.0f, 0.0f}, ImGuiCond_Always);
            ImGui::Begin("Battle Debug", nullptr,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse);
            ImGui::Text("Combat: %s   Deterministic: %s",
                        combatTuning_.enabled ? "ON" : "OFF",
                        deterministicCombat_ ? "ON" : "OFF");
            ImGui::Text("Tick: %.1f Hz   frame/total: %d / %llu",
                        combatTuning_.tickRate, combatTicksThisFrame_,
                        static_cast<unsigned long long>(battleState_.combatTicks));
            ImGui::Text("Combat CPU: %.3f ms   Events: %d",
                        combatCpuMilliseconds_, lastCombatEventCount_);
            ImGui::SeparatorText("Regiments");
            ImGui::Text("Engaged: %d   Routing: %d   Destroyed: %d",
                        engagedRegiments, routingRegiments, destroyedRegiments);
            ImGui::Text("Disengaging: %d   Pursuing: %d",
                        disengagingRegiments, pursuingRegiments);
            ImGui::Text("Blue: %d alive / %d kills", factionStrength[0], factionKills[0]);
            ImGui::Text("Red:  %d alive / %d kills", factionStrength[1], factionKills[1]);
            ImGui::SeparatorText("Soldiers");
            ImGui::Text("Alive: %d / %d", aliveSoldiers, totalSoldierCount);
            ImGui::Text("Fighting: %d   Dying: %d   Corpses: %d",
                        fightingSoldiers, dyingSoldiers, combatFx_.CorpseCount());
            ImGui::SeparatorText("Ranges");
            ImGui::Text("Engage margin: %.2f m", combatTuning_.engageMargin);
            ImGui::Text("Regiment engage: %.2f m",
                        combatTuning_.regimentEngageDistance);
            ImGui::Text("Acquire: %.2f m   Break: %.2f m",
                        combatTuning_.searchRadius + combatTuning_.maxBreakDistance,
                        combatTuning_.maxBreakDistance);
            ImGui::Text("Weapon search: %.2f m   Max attackers: %d",
                        combatTuning_.searchRadius,
                        combatTuning_.maxAttackersPerTarget);
            ImGui::Text("Engagement arc: %.0f deg   Lateral penalty: %.1f",
                        combatTuning_.engagementArcDegrees,
                        combatTuning_.targetLateralPenalty);
            ImGui::Text("Separation: %.2f m x %.2f",
                        combatTuning_.separationRadius,
                        combatTuning_.separationStrength);
            ImGui::End();
        }

        // 底部部队条。
        ImGui::SetNextWindowPos({viewport->WorkPos.x + 180.0f, viewport->WorkPos.y + viewport->WorkSize.y - 78.0f},
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize({viewport->WorkSize.x - 360.0f, 66.0f}, ImGuiCond_Always);
        ImGui::Begin("Regiments", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                            ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_HorizontalScrollbar);
        for (FRegiment& regiment : regiments_)
        {
            if (regiment.id > 0) ImGui::SameLine();
            ImGui::BeginGroup();
            const bool selectable = IsRegimentSelectable(regiment);
            const bool wasSelected = selectable && regiment.selected;
            const bool blueFaction = regiment.faction == 0;
            const ImVec4 baseColor = blueFaction
                                         ? (wasSelected ? ImVec4(0.08f, 0.48f, 0.88f, 1.0f)
                                                        : ImVec4(0.055f, 0.20f, 0.48f, 1.0f))
                                         : (wasSelected ? ImVec4(0.82f, 0.16f, 0.10f, 1.0f)
                                                        : ImVec4(0.48f, 0.075f, 0.055f, 1.0f));
            const ImVec4 hoverColor = blueFaction
                                          ? ImVec4(0.10f, 0.42f, 0.78f, 1.0f)
                                          : ImVec4(0.76f, 0.13f, 0.09f, 1.0f);
            const ImVec4 activeColor = blueFaction
                                           ? ImVec4(0.14f, 0.58f, 1.0f, 1.0f)
                                           : ImVec4(0.96f, 0.20f, 0.13f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, baseColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f, 0.97f, 1.0f, 1.0f));
            ImGui::BeginDisabled(!selectable);
            const std::string label =
                fmt::format("{} {}\n{}##{}",
                            blueFaction ? "B" : "R", regiment.def->displayName,
                            regiment.strength, regiment.id);
            if (ImGui::Button(label.c_str(), {88.0f, 36.0f}) && selectable)
            {
                ClearSelection();
                regiment.selected = true;
                RefreshSelectionFeedback();
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor(4);
            const float strengthRatio = regiment.startStrength > 0
                                            ? static_cast<float>(regiment.strength) /
                                                  static_cast<float>(regiment.startStrength)
                                            : 0.0f;
            const ImVec4 healthColor = strengthRatio > 0.6f
                                           ? ImVec4(0.20f, 0.78f, 0.28f, 1.0f)
                                           : (strengthRatio > 0.3f
                                                  ? ImVec4(0.92f, 0.70f, 0.16f, 1.0f)
                                                  : ImVec4(0.86f, 0.20f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, healthColor);
            ImGui::ProgressBar(strengthRatio, {88.0f, 6.0f}, "");
            ImGui::PopStyleColor();
            ImGui::EndGroup();
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
            const ImU32 routeColor =
                FactionOutlineColor(regiment.faction, regiment.selected ? 235 : 125);

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
            if (!regiment.selected || !IsRegimentSelectable(regiment)) continue;
            glm::vec2 localMin(std::numeric_limits<float>::max());
            glm::vec2 localMax(std::numeric_limits<float>::lowest());
            bool foundAlive = false;
            for (const FSoldier& soldier : regiment.soldiers)
            {
                if (soldier.combatState == ESoldierState::Dying ||
                    soldier.combatState == ESoldierState::Dead)
                {
                    continue;
                }
                const glm::vec2 local =
                    Formation::SlotLocal(regiment.anchor, regiment.facing, soldier.position);
                localMin = glm::min(localMin, local);
                localMax = glm::max(localMax, local);
                foundAlive = true;
            }
            if (!foundAlive) continue;
            constexpr float selectionPadding = 0.7f;
            localMin -= glm::vec2(selectionPadding);
            localMax += glm::vec2(selectionPadding);
            drawFormationFrame(regiment.anchor, regiment.facing, localMin, localMax,
                               FactionOutlineColor(regiment.faction),
                               FactionFillColor(regiment.faction));
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
                if (regiment.selected && IsRegimentSelectable(regiment))
                {
                    selected.push_back(&regiment);
                }
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
                    Formation::FormationHalfExtent(regiment.strength,
                                                   regiment.ranks,
                                                   regiment.def->fileSpacing,
                                                   regiment.def->rankSpacing);
                constexpr float previewPadding = 0.7f;
                const glm::vec2 localMin(-halfExtent.x - previewPadding,
                                         -halfExtent.y * 2.0f - previewPadding);
                const glm::vec2 localMax(halfExtent.x + previewPadding, previewPadding);
                drawFormationFrame(destination, previewFacing, localMin, localMax,
                                   FactionOutlineColor(regiment.faction, 245),
                                   FactionFillColor(regiment.faction, 38));
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
        registry.Add("debugVisible", [this]() { return showDebug_; });
        registry.Add("lastOrderDistance", [this]() { return static_cast<double>(lastOrderDistance_); });
        registry.Add("cameraFollowing", [this]() { return camera_.IsFollowing(); });
        registry.Add("cameraFocusX", [this]() { return static_cast<double>(camera_.Focus().x); });
        registry.Add("cameraFocusZ", [this]() { return static_cast<double>(camera_.Focus().z); });
        registry.Add("cameraYaw", [this]() { return static_cast<double>(camera_.Yaw()); });
        registry.Add("cameraFollowError", [this]()
        {
            glm::vec3 center{};
            if (!TrySelectedCenter(center)) return -1.0;
            return static_cast<double>(
                glm::distance(glm::vec2(camera_.Focus().x, camera_.Focus().z),
                              glm::vec2(center.x, center.z)));
        });
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
        registry.Add("aliveSoldiers", [this]()
        {
            int64_t count = 0;
            for (const FRegiment& regiment : regiments_) count += regiment.strength;
            return count;
        });
        registry.Add("factionStrength0", [this]()
        {
            int64_t count = 0;
            for (const FRegiment& regiment : regiments_)
            {
                if (regiment.faction == 0) count += regiment.strength;
            }
            return count;
        });
        registry.Add("factionStrength1", [this]()
        {
            int64_t count = 0;
            for (const FRegiment& regiment : regiments_)
            {
                if (regiment.faction == 1) count += regiment.strength;
            }
            return count;
        });
        registry.Add("engagedRegiments", [this]()
        {
            return static_cast<int64_t>(std::count_if(
                regiments_.begin(), regiments_.end(), [](const FRegiment& regiment)
                {
                    return regiment.state == ERegimentState::Engaged;
                }));
        });
        registry.Add("fightingSoldiers", [this]()
        {
            int64_t count = 0;
            for (const FRegiment& regiment : regiments_)
            {
                count += std::count_if(
                    regiment.soldiers.begin(), regiment.soldiers.end(), [](const FSoldier& soldier)
                    {
                        return soldier.combatState == ESoldierState::Fighting;
                    });
            }
            return count;
        });
        registry.Add("destroyedRegiments", [this]()
        {
            return static_cast<int64_t>(std::count_if(
                regiments_.begin(), regiments_.end(), [](const FRegiment& regiment)
                {
                    return regiment.state == ERegimentState::Destroyed;
                }));
        });
        registry.Add("disengagingRegiments", [this]()
        {
            return static_cast<int64_t>(std::count_if(
                regiments_.begin(), regiments_.end(), [](const FRegiment& regiment)
                {
                    return regiment.disengaging;
                }));
        });
        registry.Add("pursuingRegiments", [this]()
        {
            return static_cast<int64_t>(std::count_if(
                regiments_.begin(), regiments_.end(), [this](const FRegiment& regiment)
                {
                    return std::any_of(
                        regiment.engagedWith.begin(), regiment.engagedWith.end(),
                        [this](int16_t target)
                        {
                            return target >= 0 &&
                                   static_cast<size_t>(target) < regiments_.size() &&
                                   regiments_[target].disengaging;
                        });
                }));
        });
        registry.Add("totalKills", [this]()
        {
            int64_t count = 0;
            for (const FRegiment& regiment : regiments_) count += regiment.kills;
            return count;
        });
        registry.Add("corpseCount", [this]()
        {
            return static_cast<int64_t>(combatFx_.CorpseCount());
        });
        registry.Add("combatTicks", [this]()
        {
            return static_cast<int64_t>(battleState_.combatTicks);
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
