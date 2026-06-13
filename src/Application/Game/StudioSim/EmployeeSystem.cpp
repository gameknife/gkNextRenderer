#include "EmployeeSystem.h"

#include "DaySchedule.h"
#include "OfficeMap.h"
#include "StudioSimConfig.hpp"

#include <fstream>
#include <iterator>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Utilities/FileHelper.hpp"

namespace StudioSim
{
    namespace
    {
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
            p.y = Config::kGroundY;
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
        LoadCards();

        NextGameplay::Sim::FCharacterPoolConfig config;
        config.poolCapacity = static_cast<int>(cards_.size());
        config.navCellSize = Config::kNavCellSize;
        config.agentRadius = Config::kAgentRadius;
        config.separationRadius = Config::kSeparationRadius;
        config.separationStrength = Config::kSeparationStrength;
        config.groundY = Config::kGroundY;
        config.parkedPosition = Config::kParkedPosition;
        config.useRig = Config::kUseScadRigVisual;
        config.rigPath = Config::kAgentRigPath;
        config.rigVisual.baseWalkSpeed = Config::kWalkSpeed;
        config.nodeNamePrefix = "employee";
        config.slotTints.reserve(cards_.size());
        for (const FEmployeeCardDef& card : cards_)
        {
            config.slotTints.push_back(card.color);
        }
        characterPool_.Configure(config);
        characterPool_.InjectAssets(models, materials);
    }

    void EmployeeSystem::OnSceneLoaded(Assets::Scene& scene, const OfficeMap& office)
    {
        characterPool_.OnSceneLoaded(scene);

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
            emp.active = true;
            emp.speed = Config::kWalkSpeed;

            const FPointOfInterest* desk = office.FindByName(card.desk);
            emp.position =
                desk ? CourtyardSide(desk->worldPos) : glm::vec3(0.0f, Config::kGroundY, 0.0f);
            emp.visual = std::move(characterPool_.Characters()[i].visual);
            if (emp.visual)
            {
                emp.visual->SetVisible(true);
                emp.visual->SetWorldTransform(emp.position, 0.0f);
            }
            employees_.push_back(std::move(emp));
        }

        scene.MarkDirty();
        SPDLOG_INFO("StudioSim/Employees: spawned {} employees (navReady={})", employees_.size(),
                    characterPool_.NavReady());
    }

    void EmployeeSystem::RepathTo(FEmployee& emp, const FPointOfInterest& poi)
    {
        const glm::vec3 target = CourtyardSide(poi.worldPos);
        characterPool_.MoveTo(emp, target);
        emp.targetPoi = poi.name;
    }

    void EmployeeSystem::Tick(float deltaSeconds, double gameMinutes, bool paused, Assets::Scene& scene,
                              const OfficeMap& office)
    {
        if (!characterPool_.NavReady() || employees_.empty() || paused)
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
            if (targetPoi != nullptr && !targetPoi->enabled)
            {
                targetName = "pantry_01";
                targetPoi = office.FindByName(targetName);
            }

            if (emp.targetPoi != targetName && targetPoi != nullptr)
            {
                RepathTo(emp, *targetPoi);
            }

        }

        std::vector<NextGameplay::Sim::FSimCharacter*> characters;
        characters.reserve(employees_.size());
        for (FEmployee& employee : employees_)
        {
            if (characterPool_.Arrived(employee))
            {
                const FPointOfInterest* point = office.FindByName(employee.targetPoi);
                if (point != nullptr && point->category == "desk")
                {
                    employee.anim = NextGameplay::Sim::EAnimHint::Work;
                }
                else if (point != nullptr &&
                         (point->category == "meet" || point->category == "pantry" ||
                          point->category == "lounge"))
                {
                    employee.anim = NextGameplay::Sim::EAnimHint::Sit;
                }
                else
                {
                    employee.anim = NextGameplay::Sim::EAnimHint::Idle;
                }
            }
            characters.push_back(&employee);
        }
        characterPool_.Tick(deltaSeconds, scene, characters);
    }

    void EmployeeSystem::Clear()
    {
        employees_.clear();
        characterPool_.Clear();
    }
}
