#include "AgentSystem.h"

#include "AirportSimConfig.hpp"

#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Scene.hpp"

namespace AirportSim
{
    namespace
    {
        glm::vec3 AgentPoolColor(int slot)
        {
            return slot < Config::kStaffCount
                       ? Config::kStaffRoster[slot].color
                       : Config::kPassengerPalette[(slot - Config::kStaffCount) %
                                                   Config::kPassengerPaletteCount];
        }

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

    NextGameplay::Sim::FCharacterPoolConfig AgentSystem::BuildPoolConfig() const
    {
        NextGameplay::Sim::FCharacterPoolConfig config;
        config.poolCapacity = Config::kMaxAgents;
        config.navCellSize = Config::kNavCellSize;
        config.agentRadius = Config::kAgentRadius;
        config.separationRadius = Config::kSeparationRadius;
        config.separationStrength = Config::kSeparationStrength;
        config.groundY = Config::kGroundY;
        config.parkedPosition = Config::kParkedPos;
        config.useRig = Config::kUseScadRigVisual;
        config.rigPath = Config::kAgentRigPath;
        config.rigVisual.baseWalkSpeed = Config::kBaseWalkSpeed;
        config.nodeNamePrefix = "agent";
        config.slotTints.reserve(static_cast<size_t>(Config::kMaxAgents));
        for (int slot = 0; slot < Config::kMaxAgents; ++slot)
        {
            config.slotTints.push_back(AgentPoolColor(slot));
        }
        return config;
    }

    void AgentSystem::InjectAssets(std::vector<Assets::Model>& models,
                                   std::vector<Assets::FMaterial>& materials)
    {
        characterPool_.Configure(BuildPoolConfig());
        characterPool_.InjectAssets(models, materials);
    }

    void AgentSystem::OnSceneLoaded(Assets::Scene& scene)
    {
        characterPool_.OnSceneLoaded(scene);

        agents_.clear();
        agents_.resize(static_cast<size_t>(Config::kMaxAgents));
        auto& poolCharacters = characterPool_.Characters();
        for (int slot = 0; slot < Config::kMaxAgents; ++slot)
        {
            FAgent& agent = agents_[static_cast<size_t>(slot)];
            agent.id = nextId_++;
            agent.visual = std::move(poolCharacters[static_cast<size_t>(slot)].visual);
        }

        SPDLOG_INFO("AirportSim/Agents: pool of {} agents created (staff {}, passengers {}), NavGrid {}x{}",
                    agents_.size(), Config::kStaffCount, Config::kMaxConcurrentPassengers,
                    characterPool_.NavGrid().GetWidth(), characterPool_.NavGrid().GetHeight());
    }

    void AgentSystem::Clear()
    {
        agents_.clear();
        characterPool_.Clear();
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
        for (size_t slot = static_cast<size_t>(Config::kStaffCount); slot < agents_.size(); ++slot)
        {
            FAgent& agent = agents_[slot];
            if (agent.active)
            {
                continue;
            }
            ResetAgentState(agent);
            agent.active = true;
            agent.role = EAgentRole::Passenger;
            agent.name = name;
            agent.personality = personality;
            agent.color = Config::kPassengerPalette[slot % Config::kPassengerPaletteCount];
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
        characterPool_.Release(agent);
        agent.pstate = EPassengerState::Despawned;
        agent.sstate = EStaffState::Despawned;
    }

    bool AgentSystem::MoveTo(FAgent& agent, const glm::vec3& target)
    {
        return characterPool_.MoveTo(agent, target);
    }

    void AgentSystem::MoveAlong(FAgent& agent, std::vector<glm::vec3> waypoints)
    {
        characterPool_.MoveAlong(agent, std::move(waypoints));
    }

    bool AgentSystem::Arrived(const FAgent& agent) const
    {
        return characterPool_.Arrived(agent);
    }

    void AgentSystem::Tick(float deltaSeconds, Assets::Scene& scene)
    {
        std::vector<NextGameplay::Sim::FSimCharacter*> activeCharacters;
        activeCharacters.reserve(agents_.size());
        for (FAgent& agent : agents_)
        {
            if (agent.active)
            {
                activeCharacters.push_back(&agent);
            }
        }
        characterPool_.Tick(deltaSeconds, scene, activeCharacters);
    }

    FAgent* AgentSystem::FindById(int id)
    {
        for (FAgent& agent : agents_)
        {
            if (agent.id == id && agent.active)
            {
                return &agent;
            }
        }
        return nullptr;
    }

    const FAgent* AgentSystem::FindById(int id) const
    {
        for (const FAgent& agent : agents_)
        {
            if (agent.active && agent.id == id)
            {
                return &agent;
            }
        }
        return nullptr;
    }

    int AgentSystem::ActiveCount(EAgentRole role) const
    {
        int count = 0;
        for (const FAgent& agent : agents_)
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
