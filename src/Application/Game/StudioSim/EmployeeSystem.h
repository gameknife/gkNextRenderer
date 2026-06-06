#pragma once

#include "StudioSimTypes.h"

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/NextGameplay/AI/NavGrid.h"
#include "Engine/NextGameplay/AI/PathFollower.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace StudioSim
{
    class OfficeMap;

    // 员工角色卡（从 assets/configs/studio_sim.json 加载，缺失/无效时用内置默认）。
    struct FEmployeeCardDef
    {
        std::string id;
        std::string name;
        ERole       role = ERole::Engineer;
        glm::vec3   color{0.5f};
        std::string desk;
        std::string personality;
    };

    // 一个员工的运行态（M2：卡片 + 可视节点 + 寻路）。后续里程碑会扩 mood/action/task。
    struct FEmployee
    {
        std::string id;
        std::string displayName;
        ERole       role = ERole::Unknown;
        glm::vec3   color{1.0f};
        std::string homeDeskPoi;
        std::string personality;   // M4：喂进 LLM prompt
        std::string todayTask;     // M5：今日目标分解出的个人重点

        std::shared_ptr<Assets::Node> node;
        glm::vec3   position{0.0f};
        float       yaw = 0.0f;
        std::string targetPoi;
        NextGameplay::FPathFollower follower;

        // M4：LLM 决策态。overrideTargetPoi 非空且未过期 → 覆盖脚本日程。
        std::string overrideTargetPoi;
        double      overrideUntilMinutes = 0.0;
        std::string bubbleText;    // 头顶气泡（LLM 对话）
        std::string pendingFrom;   // M7：谁刚对我说了话
        std::string pendingText;   // M7：对方说的内容
        EMood       mood = EMood::Calm;
        bool        decisionPending = false;
        double      nextDecisionAt = 0.0;
    };

    // 生成员工几何体、从场景 BVH 建 NavGrid、用 PathFollower 驱动移动。
    // M2 先做随机巡游验证移动；M3+ 改由日程/调度器决定目标。
    class EmployeeSystem
    {
    public:
        // BeforeSceneRebuild：注入员工 box 模型 + 每个员工的职位色材质。
        void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
        // OnSceneLoaded：建 NavGrid + 生成员工节点。
        void OnSceneLoaded(Assets::Scene& scene, const OfficeMap& office);
        // OnTick：按脚本日程（DaySchedule）推进员工移动。
        void Tick(float deltaSeconds, double gameMinutes, bool paused, Assets::Scene& scene, const OfficeMap& office);
        void Clear();

        const std::vector<FEmployee>& Employees() const { return employees_; }
        std::vector<FEmployee>& EmployeesMutable() { return employees_; }
        size_t Count() const { return employees_.size(); }
        bool NavReady() const { return navReady_; }

    private:
        void BuildNavGrid(Assets::Scene& scene);
        void RepathTo(FEmployee& emp, const FPointOfInterest& poi);
        void LoadCards();

        std::vector<FEmployeeCardDef> cards_;
        std::vector<uint32_t> employeeModelIds_;
        std::vector<uint32_t> employeeMatIds_;
        NextGameplay::FNavGrid navGrid_;
        std::vector<FEmployee> employees_;
        bool assetsInjected_ = false;
        bool navReady_ = false;
    };
}
