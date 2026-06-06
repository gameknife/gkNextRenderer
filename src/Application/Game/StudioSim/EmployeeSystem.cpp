#include "EmployeeSystem.h"

#include "DaySchedule.h"
#include "OfficeMap.h"

#include <fstream>
#include <iterator>

#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"
#include "Engine/Utilities/FileHelper.hpp"

namespace StudioSim
{
    namespace
    {
        constexpr float kGroundY = 0.15f;     // 地板顶面高度
        constexpr float kWalkSpeed = 3.0f;    // m/s（游戏单位）

        std::vector<FEmployeeCardDef> DefaultCards()
        {
            return {
                {"alice", "Alice", ERole::Engineer,   {0.30f, 0.80f, 0.40f}, "desk_engineer_01", "话痨、乐观、爱摸鱼"},
                {"bob",   "Bob",   ERole::Engineer,   {0.30f, 0.65f, 0.85f}, "desk_engineer_02", "沉默寡言、技术控、完美主义"},
                {"carol", "Carol", ERole::Artist,     {0.95f, 0.55f, 0.25f}, "desk_artist_01",   "感性、爱拖延、追求美感"},
                {"dave",  "Dave",  ERole::Designer,   {0.55f, 0.45f, 0.90f}, "desk_designer_01", "点子多、话密、爱开会"},
                {"erin",  "Erin",  ERole::ProducerPM, {0.90f, 0.40f, 0.75f}, "desk_pm_01",       "焦虑、爱催进度、操心"},
                {"frank", "Frank", ERole::QA,         {0.90f, 0.30f, 0.30f}, "desk_qa_01",       "细致、毒舌、爱挑刺"},
            };
        }

        // 把目标点往中庭（z 趋向 0）轻推，让员工停在工位/家具的过道侧而非叠在 box 上。
        glm::vec3 CourtyardSide(glm::vec3 p)
        {
            p.z += (p.z > 0.0f) ? -1.2f : 1.2f;
            p.y = kGroundY;
            return p;
        }
    }

    void EmployeeSystem::LoadCards()
    {
        if (!cards_.empty())
        {
            return;
        }

        const std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/configs/studio_sim.json");
        std::ifstream file(path);
        if (file.is_open())
        {
            try
            {
                nlohmann::json json;
                file >> json;
                for (const auto& entry : json.at("employees"))
                {
                    FEmployeeCardDef card;
                    card.id = entry.value("id", std::string());
                    card.name = entry.value("name", card.id);
                    card.role = RoleFromString(entry.value("role", std::string("engineer")));
                    card.desk = entry.value("desk", std::string());
                    card.personality = entry.value("personality", std::string());
                    if (entry.contains("color") && entry["color"].is_array() && entry["color"].size() >= 3)
                    {
                        card.color = glm::vec3(entry["color"][0].get<float>(), entry["color"][1].get<float>(),
                                               entry["color"][2].get<float>());
                    }
                    if (!card.id.empty())
                    {
                        cards_.push_back(std::move(card));
                    }
                }
            }
            catch (const std::exception& e)
            {
                SPDLOG_WARN("StudioSim/Employees: studio_sim.json parse failed ({}), using defaults", e.what());
                cards_.clear();
            }
        }

        if (cards_.empty())
        {
            cards_ = DefaultCards();
            SPDLOG_INFO("StudioSim/Employees: using {} built-in default cards", cards_.size());
        }
        else
        {
            SPDLOG_INFO("StudioSim/Employees: loaded {} cards from studio_sim.json", cards_.size());
        }
    }

    void EmployeeSystem::InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials)
    {
        if (assetsInjected_)
        {
            return;
        }
        LoadCards();

        // 员工 = 一个 0.5 x 1.6 x 0.5 的直立 box，底面在 y=0。
        employeeModelIds_.clear();
        employeeMatIds_.clear();
        for (const auto& card : cards_)
        {
            models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.25f, 0.0f, -0.25f), glm::vec3(0.25f, 1.6f, 0.25f)));
            employeeModelIds_.push_back(static_cast<uint32_t>(models.size() - 1));
            employeeMatIds_.push_back(Assets::SceneBuilder::AddLambertianMaterial(materials, card.color));
        }

        assetsInjected_ = true;
    }

    void EmployeeSystem::BuildNavGrid(Assets::Scene& scene)
    {
        const glm::vec3 sceneMin = scene.GetSceneAABBMin();
        const glm::vec3 sceneMax = scene.GetSceneAABBMax();

        NextGameplay::FNavGridSettings settings;
        settings.cellSize = 0.5f;
        settings.agentRadius = 0.3f;
        settings.maxSlopeAngle = 50.0f;
        settings.clearanceHeight = 1.7f;
        settings.maxStepHeight = 0.35f;
        settings.worldMin = glm::vec3(sceneMin.x - 2.0f, 0.0f, sceneMin.z - 2.0f);
        settings.worldMax = glm::vec3(sceneMax.x + 2.0f, 0.0f, sceneMax.z + 2.0f);
        settings.sampleCeiling = sceneMax.y + 5.0f;
        settings.floorHeightTolerance = 1.0f;

        navGrid_.Build(scene.GetCPUAccelerationStructure(), settings);
        navReady_ = navGrid_.IsBuilt();
    }

    void EmployeeSystem::OnSceneLoaded(Assets::Scene& scene, const OfficeMap& office)
    {
        BuildNavGrid(scene);

        employees_.clear();
        for (size_t i = 0; i < cards_.size(); ++i)
        {
            const FEmployeeCardDef& card = cards_[i];

            FEmployee emp;
            emp.id = card.id;
            emp.displayName = card.name;
            emp.role = card.role;
            emp.color = card.color;
            emp.homeDeskPoi = card.desk;
            emp.personality = card.personality;

            const FPointOfInterest* desk = office.FindByName(card.desk);
            emp.position = desk ? CourtyardSide(desk->worldPos) : glm::vec3(0.0f, kGroundY, 0.0f);

            const uint32_t instanceId = scene.GenerateInstanceId();
            const uint32_t matId = employeeMatIds_.empty() ? 0 : employeeMatIds_[i % employeeMatIds_.size()];
            const uint32_t modelId = employeeModelIds_.empty() ? 0 : employeeModelIds_[i % employeeModelIds_.size()];
            emp.node = Assets::SceneBuilder::CreateRenderNode(card.id, emp.position, glm::vec3(1.0f), instanceId,
                                                              modelId, matId);

            // Dynamic mobility so the renderer refreshes the transform every frame.
            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            emp.node->AddComponent(phys);

            scene.AddNode(emp.node);
            employees_.push_back(std::move(emp));
        }

        scene.MarkDirty();
        SPDLOG_INFO("StudioSim/Employees: spawned {} employees (navReady={})", employees_.size(), navReady_);
    }

    void EmployeeSystem::RepathTo(FEmployee& emp, const FPointOfInterest& poi)
    {
        const glm::vec3 target = CourtyardSide(poi.worldPos);
        std::vector<glm::vec3> path = navGrid_.FindPath(emp.position, target, emp.position.y);
        emp.follower.SetPath(std::move(path), target);
        emp.targetPoi = poi.name;
    }

    void EmployeeSystem::Tick(float deltaSeconds, double gameMinutes, bool paused, Assets::Scene& scene,
                              const OfficeMap& office)
    {
        if (!navReady_ || employees_.empty() || paused)
        {
            return;
        }

        for (auto& emp : employees_)
        {
            // LLM 决策（override）优先；否则回退脚本日程（确定性 fallback）。
            const bool hasOverride = !emp.overrideTargetPoi.empty() && gameMinutes < emp.overrideUntilMinutes;
            std::string targetName = hasOverride ? emp.overrideTargetPoi : ScheduledPoi(emp.homeDeskPoi, gameMinutes);

            // 目标点位不可用（断电/宕机）→ 退到茶水间（休息区不受影响）。
            const FPointOfInterest* targetPoi = office.FindByName(targetName);
            if (targetPoi != nullptr && !targetPoi->workable)
            {
                targetName = "pantry_01";
                targetPoi = office.FindByName(targetName);
            }

            if (emp.targetPoi != targetName && targetPoi != nullptr)
            {
                RepathTo(emp, *targetPoi);
            }

            const glm::vec3 dir = emp.follower.GetMoveDirection(emp.position);
            if (glm::length(dir) > 0.001f)
            {
                emp.position += dir * (kWalkSpeed * deltaSeconds);
                emp.position.y = kGroundY;
                emp.yaw = std::atan2(dir.x, dir.z);

                emp.node->SetTranslation(emp.position);
                emp.node->SetRotation(glm::angleAxis(emp.yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
                emp.node->RecalcTransform();
            }
        }

        scene.MarkDirty();
    }

    void EmployeeSystem::Clear()
    {
        employees_.clear();
        navGrid_ = NextGameplay::FNavGrid{};
        navReady_ = false;
        assetsInjected_ = false;
    }
}
