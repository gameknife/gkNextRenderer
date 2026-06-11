#pragma once

#include "AirportSimTypes.h"

#include "Engine/Assets/AssetsFwd.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/AI/PathFollower.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace AirportSim
{
    class AirportMap;

    // 视觉层接口（§3.3 换装预留）：游戏逻辑只通过此接口与外观交互。
    class IAgentVisual
    {
    public:
        virtual ~IAgentVisual() = default;
        virtual void SetWorldTransform(const glm::vec3& pos, float yaw) = 0;
        virtual void SetAnimHint(EAgentAnimHint hint) = 0;
        virtual void SetVisible(bool visible) = 0;
    };

    // MVP 几何体外观：单个职业色直立 box，坐下 = 身体压矮（第二阶段换 SkinnedVisual）。
    class GeometryVisual final : public IAgentVisual
    {
    public:
        explicit GeometryVisual(std::shared_ptr<Assets::Node> node) : node_(std::move(node)) {}
        void SetWorldTransform(const glm::vec3& pos, float yaw) override;
        void SetAnimHint(EAgentAnimHint hint) override;
        void SetVisible(bool visible) override;

    private:
        std::shared_ptr<Assets::Node> node_;
        EAgentAnimHint hint_ = EAgentAnimHint::Idle;
    };

    // 一个角色的运行态（旅客与员工共用，按 role 区分字段含义）。
    struct FAgent
    {
        int         id = -1;
        bool        active = false;
        EAgentRole  role = EAgentRole::Passenger;
        std::string name;
        std::string personality;
        glm::vec3   color{1.0f};

        // 移动（kinematic mover，§7.2）
        glm::vec3 position{0.0f};
        float     yaw = 0.0f;
        float     speed = 1.8f;
        NextGameplay::FPathFollower follower;
        bool      moving = false;
        glm::vec3 moveTarget{0.0f};
        std::vector<glm::vec3> scriptWaypoints; // PassSecurity 等脚本走点（绕过 NavGrid）
        EAgentAnimHint anim = EAgentAnimHint::Idle;
        std::unique_ptr<IAgentVisual> visual;

        // 旅客旅程（Layer 0）
        EPassengerState pstate = EPassengerState::Despawned;
        int         flightIdx = -1;
        int         groupId = -1;
        EPassengerGroupType groupType = EPassengerGroupType::Solo;
        int         groupLeaderId = -1;
        int         groupMemberIndex = 0;
        int         groupSize = 1;
        double      stateUntil = 0.0;     // 当前停留状态的结束时刻（游戏分钟）
        std::string targetPoi;            // 当前去/在用的 POI
        std::string queueId;
        int         queuedSlot = -1;      // 上次站位 slot（检测整队前移）
        std::string seatPoi;
        int         seatSlot = -1;

        // 员工日程（Layer 0）
        EStaffState sstate = EStaffState::Despawned;
        std::string postPoi;
        EShift      shift = EShift::Morning;
        int         rosterIdx = -1;
        int         patrolIdx = 0;
        std::string assignedGate;         // GateAgent 动态岗位

        // 社交 / Layer 1
        std::string bubbleText;
        double      bubbleUntil = 0.0;
        EMood       mood = EMood::Neutral;
        bool        decisionPending = false;
        double      nextDecisionAt = 0.0;
        std::string eventNote;            // 感知事件（优先触发决策）
        double      lastGreetAt = -1e9;
        double      lastGroupDiscussionAt = -1e9;
        int         chatChain = 0;        // 当前对话来回深度（say_to 链，超限不再插队）
        std::string lastChatWith;         // 上一次对话对象（喂回 prompt 防复读）
        std::vector<std::string> recentSpeech; // 自己最近说过的台词（喂回 prompt 防复读）

        bool IsGroupedPassenger() const
        {
            return role == EAgentRole::Passenger && groupId >= 0 && groupSize > 1;
        }

        bool IsGroupLeader() const
        {
            return IsGroupedPassenger() && id == groupLeaderId;
        }
    };

    // 角色池（spawn/despawn）+ NavGrid + 移动/分离力 + 视觉驱动（§6/§7.2/§7.4）。
    class AgentSystem
    {
    public:
        // BeforeSceneRebuild：注入 box 模型 + 全部池位材质。
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        // OnSceneLoaded：建 NavGrid + 创建池节点（藏在地下）。
        void OnSceneLoaded(Assets::Scene& scene);
        void Clear();

        // 员工池位 = rosterIdx 固定；旅客池位循环复用。返回 nullptr = 池满。
        FAgent* SpawnStaff(int rosterIdx, const glm::vec3& pos);
        FAgent* SpawnPassenger(const std::string& name, const std::string& personality, const glm::vec3& pos,
                               float speedScale);
        void Despawn(FAgent& agent);

        // 走 NavGrid A* 去目标；返回 false = 寻路失败（仍直线走）。
        bool MoveTo(FAgent& agent, const glm::vec3& target);
        // 脚本走点（安检南进北出等单向流，§7.2）。
        void MoveAlong(FAgent& agent, std::vector<glm::vec3> waypoints);
        bool Arrived(const FAgent& agent) const;

        // 移动积分 + 分离力 + 写节点 transform。
        void Tick(float deltaSeconds, Assets::Scene& scene);

        std::vector<FAgent>& Agents() { return agents_; }
        const std::vector<FAgent>& Agents() const { return agents_; }
        FAgent* FindById(int id);
        const FAgent* FindById(int id) const;
        int ActiveCount(EAgentRole role) const;
        int ActivePassengerCount() const;
        bool NavReady() const { return navReady_; }
        const NextGameplay::FNavGrid& NavGrid() const { return navGrid_; }

    private:
        void BuildNavGrid(Assets::Scene& scene);

        NextGameplay::FNavGrid navGrid_;
        std::vector<FAgent> agents_;       // 固定大小池
        std::vector<uint32_t> matIds_;     // 每池位材质
        std::vector<uint32_t> modelIds_;   // 每池位模型
        bool assetsInjected_ = false;
        bool navReady_ = false;
        int nextId_ = 1;
    };
}
