#include "AgentSystem.h"

#include "AirportMap.h"
#include "AirportSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"

namespace AirportSim
{
    namespace
    {
        constexpr glm::vec3 kParkedPos{0.0f, -100.0f, 0.0f}; // 池空位藏在地下
    }

    void GeometryVisual::SetWorldTransform(const glm::vec3& pos, float yaw)
    {
        node_->SetTranslation(pos);
        node_->SetRotation(glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
        node_->RecalcTransform();
    }

    void GeometryVisual::SetAnimHint(EAgentAnimHint hint)
    {
        if (hint == hint_)
        {
            return;
        }
        hint_ = hint;
        // 坐下 = 压矮身体；其余姿态 MVP 不区分（§3.3：换骨骼模型后由动画接管）。
        node_->SetScale(glm::vec3(1.0f, hint == EAgentAnimHint::Sit ? 0.55f : 1.0f, 1.0f));
        node_->RecalcTransform();
    }

    void GeometryVisual::SetVisible(bool visible)
    {
        if (!visible)
        {
            node_->SetTranslation(kParkedPos);
            node_->RecalcTransform();
        }
    }

    void AgentSystem::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        if (assetsInjected_)
        {
            return;
        }

        // 角色 = 0.5 x 1.6 x 0.5 直立 box，底面 y=0。每个池位独立 model+材质
        // （同 StudioSim 惯例：材质按 model 绑定，共享 model 改 per-node 材质不可靠）。
        modelIds_.clear();
        matIds_.clear();
        for (int i = 0; i < Config::kMaxAgents; ++i)
        {
            models.push_back(
                Assets::FProcModel::CreateBox(glm::vec3(-0.25f, 0.0f, -0.25f), glm::vec3(0.25f, 1.6f, 0.25f)));
            modelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
            const glm::vec3 color = i < Config::kStaffCount
                                        ? Config::kStaffRoster[i].color
                                        : Config::kPassengerPalette[(i - Config::kStaffCount) %
                                                                    Config::kPassengerPaletteCount];
            matIds_.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, color));
        }
        assetsInjected_ = true;
    }

    void AgentSystem::BuildNavGrid(Assets::Scene& scene)
    {
        const glm::vec3 sceneMin = scene.GetSceneAABBMin();
        const glm::vec3 sceneMax = scene.GetSceneAABBMax();

        NextGameplay::FNavGridSettings settings;
        settings.cellSize = Config::kNavCellSize;
        settings.agentRadius = Config::kAgentRadius;
        settings.maxSlopeAngle = 50.0f;
        settings.clearanceHeight = 1.7f;
        settings.maxStepHeight = 0.35f;
        settings.worldMin = glm::vec3(sceneMin.x - 2.0f, 0.0f, sceneMin.z - 2.0f);
        settings.worldMax = glm::vec3(sceneMax.x + 2.0f, 0.0f, sceneMax.z + 2.0f);
        settings.sampleCeiling = sceneMax.y + 5.0f;
        settings.floorHeightTolerance = 1.0f;

        navGrid_.Build(scene.GetCPUAccelerationStructure(), settings);
        navReady_ = navGrid_.IsBuilt();
        SPDLOG_INFO("AirportSim/Agents: NavGrid built={} size={}x{}", navReady_, navGrid_.GetWidth(),
                    navGrid_.GetHeight());
    }

    void AgentSystem::OnSceneLoaded(Assets::Scene& scene)
    {
        BuildNavGrid(scene);

        agents_.clear();
        agents_.resize(static_cast<size_t>(Config::kMaxAgents));
        for (int i = 0; i < Config::kMaxAgents; ++i)
        {
            FAgent& agent = agents_[static_cast<size_t>(i)];
            agent.id = nextId_++;
            const uint32_t instanceId = scene.GenerateInstanceId();
            const uint32_t matId = matIds_.empty() ? 0 : matIds_[static_cast<size_t>(i) % matIds_.size()];
            const uint32_t modelId = modelIds_.empty() ? 0 : modelIds_[static_cast<size_t>(i) % modelIds_.size()];
            auto node = Assets::SceneBuilder::CreateRenderNode(fmt::format("agent_{:02d}", i), kParkedPos,
                                                               glm::vec3(1.0f), instanceId, modelId, matId);
            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            node->AddComponent(phys);
            scene.AddNode(node);
            agent.visual = std::make_unique<GeometryVisual>(std::move(node));
        }
        scene.MarkDirty();
        SPDLOG_INFO("AirportSim/Agents: pool of {} agents created (staff {}, passengers {})", agents_.size(),
                    Config::kStaffCount, Config::kMaxConcurrentPassengers);
    }

    void AgentSystem::Clear()
    {
        agents_.clear();
        navGrid_ = NextGameplay::FNavGrid{};
        navReady_ = false;
        assetsInjected_ = false;
        matIds_.clear();
    }

    namespace
    {
        // 复位池位上一次使用残留的逻辑状态（保留 id/visual）。
        void ResetAgentState(FAgent& agent)
        {
            agent.follower.Clear();
            agent.moving = false;
            agent.scriptWaypoints.clear();
            agent.anim = EAgentAnimHint::Idle;
            agent.pstate = EPassengerState::Despawned;
            agent.flightIdx = -1;
            agent.groupId = -1;
            agent.groupType = EPassengerGroupType::Solo;
            agent.groupLeaderId = -1;
            agent.groupMemberIndex = 0;
            agent.groupSize = 1;
            agent.stateUntil = 0.0;
            agent.targetPoi.clear();
            agent.queueId.clear();
            agent.queuedSlot = -1;
            agent.seatPoi.clear();
            agent.seatSlot = -1;
            agent.sstate = EStaffState::Despawned;
            agent.assignedGate.clear();
            agent.patrolIdx = 0;
            agent.bubbleText.clear();
            agent.bubbleUntil = 0.0;
            agent.mood = EMood::Neutral;
            agent.decisionPending = false;
            agent.nextDecisionAt = 0.0;
            agent.eventNote.clear();
            agent.lastGreetAt = -1e9;
            agent.lastGroupDiscussionAt = -1e9;
            agent.chatChain = 0;
            agent.lastChatWith.clear();
            agent.recentSpeech.clear();
        }
    }

    FAgent* AgentSystem::SpawnStaff(int rosterIdx, const glm::vec3& pos)
    {
        if (rosterIdx < 0 || rosterIdx >= Config::kStaffCount ||
            rosterIdx >= static_cast<int>(agents_.size()))
        {
            return nullptr;
        }
        FAgent& agent = agents_[static_cast<size_t>(rosterIdx)];
        if (agent.active)
        {
            return nullptr;
        }
        const auto& def = Config::kStaffRoster[rosterIdx];
        ResetAgentState(agent);
        agent.active = true;
        agent.role = def.role;
        agent.name = def.name;
        agent.personality = def.personality;
        agent.color = def.color;
        agent.position = pos;
        agent.speed = Config::kBaseWalkSpeed;
        agent.rosterIdx = rosterIdx;
        agent.postPoi = def.post;
        agent.shift = def.shift;
        agent.sstate = EStaffState::Commute;
        agent.visual->SetWorldTransform(pos, 0.0f);
        return &agent;
    }

    FAgent* AgentSystem::SpawnPassenger(const std::string& name, const std::string& personality,
                                        const glm::vec3& pos, float speedScale)
    {
        for (size_t i = static_cast<size_t>(Config::kStaffCount); i < agents_.size(); ++i)
        {
            FAgent& agent = agents_[i];
            if (agent.active)
            {
                continue;
            }
            ResetAgentState(agent);
            agent.active = true;
            agent.role = EAgentRole::Passenger;
            agent.name = name;
            agent.personality = personality;
            agent.color = Config::kPassengerPalette[i % Config::kPassengerPaletteCount];
            agent.position = pos;
            agent.speed = Config::kBaseWalkSpeed * speedScale;
            agent.rosterIdx = -1;
            agent.pstate = EPassengerState::ToEntrance;
            agent.visual->SetWorldTransform(pos, 0.0f);
            return &agent;
        }
        return nullptr;
    }

    void AgentSystem::Despawn(FAgent& agent)
    {
        agent.active = false;
        agent.pstate = EPassengerState::Despawned;
        agent.sstate = EStaffState::Despawned;
        agent.visual->SetAnimHint(EAgentAnimHint::Idle);
        agent.visual->SetVisible(false);
    }

    bool AgentSystem::MoveTo(FAgent& agent, const glm::vec3& target)
    {
        agent.scriptWaypoints.clear();
        glm::vec3 goal = target;
        goal.y = Config::kGroundY;
        std::vector<glm::vec3> path;
        if (navReady_)
        {
            path = navGrid_.FindPath(agent.position, goal, Config::kGroundY);
        }
        const bool found = !path.empty();
        if (!found)
        {
            path.push_back(goal); // 兜底直线走，避免卡死
        }
        agent.follower.SetPath(std::move(path), goal);
        agent.moveTarget = goal;
        agent.moving = true;
        return found;
    }

    void AgentSystem::MoveAlong(FAgent& agent, std::vector<glm::vec3> waypoints)
    {
        if (waypoints.empty())
        {
            return;
        }
        for (auto& wp : waypoints)
        {
            wp.y = Config::kGroundY;
        }
        agent.moveTarget = waypoints.back();
        agent.follower.SetPath(std::move(waypoints), agent.moveTarget);
        agent.moving = true;
    }

    bool AgentSystem::Arrived(const FAgent& agent) const
    {
        return !agent.moving || agent.follower.IsFinished(agent.position, 0.45f);
    }

    void AgentSystem::Tick(float deltaSeconds, Assets::Scene& scene)
    {
        // 收集活跃角色做 O(n²) 分离（n≤40，§7.4）。
        std::vector<FAgent*> activeAgents;
        activeAgents.reserve(agents_.size());
        for (auto& agent : agents_)
        {
            if (agent.active)
            {
                activeAgents.push_back(&agent);
            }
        }

        for (FAgent* agentPtr : activeAgents)
        {
            FAgent& agent = *agentPtr;
            glm::vec3 velocity{0.0f};

            if (agent.moving)
            {
                if (agent.follower.IsFinished(agent.position, 0.35f))
                {
                    agent.moving = false;
                }
                else
                {
                    velocity = agent.follower.GetMoveDirection(agent.position, 0.6f) * agent.speed;
                }
            }

            // 邻居分离力（仅行走时；坐着/服务中不被推开）。
            if (agent.moving)
            {
                glm::vec3 separation{0.0f};
                for (const FAgent* other : activeAgents)
                {
                    if (other == agentPtr)
                    {
                        continue;
                    }
                    glm::vec3 away = agent.position - other->position;
                    away.y = 0.0f;
                    const float dist = glm::length(away);
                    if (dist > 0.001f && dist < Config::kSeparationRadius)
                    {
                        separation += away / dist * (1.0f - dist / Config::kSeparationRadius);
                    }
                }
                velocity += separation * Config::kSeparationStrength;
            }

            const float speed = glm::length(velocity);
            if (speed > 0.01f)
            {
                agent.position += velocity * deltaSeconds;
                agent.position.y = Config::kGroundY;
                const glm::vec3 dir = velocity / speed;
                agent.yaw = std::atan2(dir.x, dir.z);
                agent.anim = EAgentAnimHint::Walk;
            }
            else if (agent.anim == EAgentAnimHint::Walk)
            {
                agent.anim = EAgentAnimHint::Idle;
            }

            agent.visual->SetAnimHint(agent.anim);
            agent.visual->SetWorldTransform(agent.position, agent.yaw);
        }

        scene.MarkDirty();
    }

    FAgent* AgentSystem::FindById(int id)
    {
        for (auto& agent : agents_)
        {
            if (agent.id == id && agent.active)
            {
                return &agent;
            }
        }
        return nullptr;
    }

    int AgentSystem::ActiveCount(EAgentRole role) const
    {
        int count = 0;
        for (const auto& agent : agents_)
        {
            if (agent.active && agent.role == role)
            {
                ++count;
            }
        }
        return count;
    }

    int AgentSystem::ActivePassengerCount() const
    {
        return ActiveCount(EAgentRole::Passenger);
    }
}
