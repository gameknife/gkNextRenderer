#include "ProductionSystem.h"

#include "EmployeeSystem.h"
#include "OfficeMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace StudioSim
{
    namespace
    {
        constexpr double kWorkOutputIntervalMinutes = 15.0;
        constexpr double kRetryWhenAwayMinutes = 5.0;
        constexpr float kWorkPoiArrivalDistance = 1.8f;
        constexpr float kProductionToPolishRatio = 0.8f;
        constexpr int kMaxPolishBugs = 16;

        constexpr double kDayStartMinutes = 9.0 * 60.0;   // 每个工作日从 09:00 开工
        constexpr double kMorningRampMinutes = 60.0;      // 开工后 1 小时内产能从下限爬到满
        constexpr float kMorningRampFloor = 0.5f;         // 晨会刚散时的起始产能（避免瞬间猛加）

        float MeterTotal(const FProjectMeters& meters)
        {
            return meters.tech + meters.design + meters.art + meters.polish;
        }

        float CompletedMeterTotal(const FProjectMeters& meters, const FProjectMeters& target)
        {
            return std::min(meters.tech, target.tech) + std::min(meters.design, target.design) +
                   std::min(meters.art, target.art) + std::min(meters.polish, target.polish);
        }

        bool MeterReached(float value, float target, float ratio)
        {
            return target <= 0.0f || value >= target * ratio;
        }

        bool AllMetersReached(const FProjectMeters& meters, const FProjectMeters& target, float ratio)
        {
            return MeterReached(meters.tech, target.tech, ratio) && MeterReached(meters.design, target.design, ratio) &&
                   MeterReached(meters.art, target.art, ratio) && MeterReached(meters.polish, target.polish, ratio);
        }

        void AddMeter(FProjectMeters& meters, const std::string& meter, float amount)
        {
            if (meter == "tech")
            {
                meters.tech += amount;
            }
            else if (meter == "design")
            {
                meters.design += amount;
            }
            else if (meter == "art")
            {
                meters.art += amount;
            }
            else if (meter == "polish")
            {
                meters.polish += amount;
            }
        }

        const char* MeterLabel(const std::string& meter)
        {
            if (meter == "tech") return "tech";
            if (meter == "design") return "design";
            if (meter == "art") return "art";
            if (meter == "polish") return "polish";
            return "none";
        }

        const char* MeterLabelZh(const std::string& meter)
        {
            if (meter == "tech") return "技术";
            if (meter == "design") return "玩法";
            if (meter == "art") return "美术";
            if (meter == "polish") return "品质";
            return "";
        }

        struct FRoleOutput
        {
            std::string meter;
            float baseAmount = 0.0f;
        };

        FRoleOutput RoleOutput(ERole role)
        {
            switch (role)
            {
            case ERole::Engineer: return {"tech", 8.0f};
            case ERole::Artist:   return {"art", 7.0f};
            case ERole::Designer: return {"design", 7.0f};
            case ERole::QA:       return {"polish", 5.0f};
            default:              return {};
            }
        }

        float MoodFactor(EMood mood)
        {
            switch (mood)
            {
            case EMood::Focused:  return 1.3f;
            case EMood::Excited:  return 1.2f;
            case EMood::Bored:    return 0.7f;
            case EMood::Stressed: return 0.8f;
            case EMood::Panicked: return 0.5f;
            default:              return 1.0f;
            }
        }

        uint64_t StableHash(const std::string& text)
        {
            uint64_t hash = 1469598103934665603ull;
            for (unsigned char c : text)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        // 开工后的产能爬升系数：晨会刚散时 = kMorningRampFloor，1 小时后到满 1.0。
        float MorningRamp(double gameMinutes)
        {
            const double since = gameMinutes - kDayStartMinutes;
            if (since <= 0.0)
            {
                return kMorningRampFloor;
            }
            const float t = static_cast<float>(std::clamp(since / kMorningRampMinutes, 0.0, 1.0));
            return kMorningRampFloor + (1.0f - kMorningRampFloor) * t;
        }

        // 把每位员工当天首拍分散到开工后 [0, interval) 内，避免全员同拍齐射（台阶式跳进度）。
        double StaggeredFirstOutput(const FEmployee& emp)
        {
            const double phase = static_cast<double>(StableHash(emp.id) % 1000ull) / 1000.0;
            return kDayStartMinutes + phase * kWorkOutputIntervalMinutes;
        }

        // 给后续产出节拍加 ±15% 抖动，保持各员工错峰、不重新同步。
        double JitteredInterval(const FEmployee& emp, double gameMinutes)
        {
            const int bucket = static_cast<int>(gameMinutes / kWorkOutputIntervalMinutes);
            const uint64_t h = StableHash(emp.id + ":" + std::to_string(bucket));
            const double j = static_cast<double>(h % 1000ull) / 1000.0; // [0,1)
            return kWorkOutputIntervalMinutes * (0.85 + 0.30 * j);
        }

        bool IsAtWorkDesk(const FEmployee& emp, const OfficeMap& office)
        {
            if (emp.targetPoi.empty())
            {
                return false;
            }
            const FPointOfInterest* poi = office.FindByName(emp.targetPoi);
            if (poi == nullptr || !poi->enabled || poi->category != "desk")
            {
                return false;
            }
            return glm::length(emp.position - poi->worldPos) <= kWorkPoiArrivalDistance;
        }

        float TeamBoost(const std::vector<FEmployee>& employees, const OfficeMap& office)
        {
            float boost = 1.0f;
            for (const auto& emp : employees)
            {
                if (!IsAtWorkDesk(emp, office))
                {
                    continue;
                }
                if (emp.role == ERole::ProducerPM)
                {
                    boost += 0.10f;
                }
                else if (emp.role == ERole::Boss)
                {
                    boost += 0.08f;
                }
            }
            return boost;
        }

        bool ShouldQaFindBug(const FEmployee& emp, double gameMinutes, int currentBugCount)
        {
            if (currentBugCount >= kMaxPolishBugs)
            {
                return false;
            }
            const auto cycle = static_cast<int>(std::floor(gameMinutes / kWorkOutputIntervalMinutes));
            return ((cycle + static_cast<int>(StableHash(emp.id) % 7ull)) % 3) == 0;
        }

        void ApplyOutputToEmployee(FEmployee& emp, const FWorkOutput& output)
        {
            if (output.amount <= 0.0f || output.meter.empty())
            {
                return;
            }
            AddMeter(emp.myContribution, output.meter, output.amount);
        }
    }

    void ProductionSystem::StartProject(const FDailyGoal& goal, double gameMinutes, std::vector<FEmployee>& employees)
    {
        FGameProject project;
        project.name = goal.title;
        project.production.targetMeters = goal.targetMeters;
        StartProject(project, gameMinutes, employees);
    }

    void ProductionSystem::StartProject(const FGameProject& project, double gameMinutes, std::vector<FEmployee>& employees)
    {
        state_ = project.production;
        if (state_.stage == EProjectStage::Planning)
        {
            state_.stage = EProjectStage::Production;
        }
        if (MeterTotal(state_.targetMeters) <= 0.0f)
        {
            state_.targetMeters = {100.0f, 70.0f, 80.0f, 90.0f};
        }

        active_ = true;
        polishBugBatchGenerated_ = false;
        activeGoalTitle_ = project.name.empty() ? "未命名项目" : project.name;
        lastLoggedProgressBucket_ = -1;
        ClearFocusBoost();

        for (auto& emp : employees)
        {
            emp.myContribution = FProjectMeters{};
            emp.nextWorkOutputAt = gameMinutes + kWorkOutputIntervalMinutes;
        }

        RecalculateProgress();
        SPDLOG_INFO(
            "StudioSim/Proj: project '{}' started genre={} theme={} day {}/{} budget={} targets T/D/A/P = "
            "{:.0f}/{:.0f}/{:.0f}/{:.0f}",
            activeGoalTitle_, GameGenreName(project.genre), GameThemeName(project.theme), project.elapsedDays + 1,
            project.plannedDays, project.budget, state_.targetMeters.tech, state_.targetMeters.design,
            state_.targetMeters.art, state_.targetMeters.polish);
    }

    void ProductionSystem::Tick(double gameMinutes, bool paused, std::vector<FEmployee>& employees, const OfficeMap& office)
    {
        if (!active_ || paused || state_.stage == EProjectStage::Planning || state_.stage == EProjectStage::Done)
        {
            return;
        }

        // 跨天检测：时钟回到 09:00（gameMinutes 比上一拍小）→ 重新错开每位员工的首拍，避免新一天又齐射。
        const bool newDay = lastTickGameMinutes_ < 0.0 || gameMinutes + 1.0 < lastTickGameMinutes_;
        if (newDay)
        {
            for (auto& emp : employees)
            {
                emp.nextWorkOutputAt = StaggeredFirstOutput(emp);
            }
        }
        lastTickGameMinutes_ = gameMinutes;

        const float teamBoost = TeamBoost(employees, office);
        for (auto& emp : employees)
        {
            if (emp.gatheringId >= 0)
            {
                emp.nextWorkOutputAt = gameMinutes + kRetryWhenAwayMinutes;
                continue;
            }
            if (gameMinutes < emp.nextWorkOutputAt)
            {
                continue;
            }

            if (!IsAtWorkDesk(emp, office))
            {
                emp.nextWorkOutputAt = gameMinutes + kRetryWhenAwayMinutes;
                continue;
            }

            FWorkOutput output;
            const FRoleOutput roleOutput = RoleOutput(emp.role);
            if (state_.stage == EProjectStage::Polish && emp.role == ERole::Engineer && state_.bugCount > 0)
            {
                output.fixedBug = true;
            }
            else if (state_.stage == EProjectStage::Polish && emp.role == ERole::QA &&
                     ShouldQaFindBug(emp, gameMinutes, state_.bugCount))
            {
                output.foundBug = true;
            }
            else if (roleOutput.baseAmount > 0.0f)
            {
                output.meter = roleOutput.meter;
                output.amount = roleOutput.baseAmount * MoodFactor(emp.mood) * teamBoost * MorningRamp(gameMinutes);
                if (!focusMeter_.empty() && output.meter == focusMeter_)
                {
                    output.amount *= focusBoost_;
                }
            }

            if (output.fixedBug)
            {
                state_.bugCount = std::max(0, state_.bugCount - 1);
                state_.bugsFixed++;
                visualEvents_.push_back({emp.position + glm::vec3(0.0f, 2.0f, 0.0f), "-1 Bug", "bug_fixed"});
                SPDLOG_INFO("StudioSim/Prod: {} fixed a bug (bugs={}, fixed={})", emp.displayName, state_.bugCount,
                            state_.bugsFixed);
            }
            else if (output.foundBug)
            {
                state_.bugCount++;
                visualEvents_.push_back({emp.position + glm::vec3(0.0f, 2.0f, 0.0f), "+1 Bug", "bug_found"});
                SPDLOG_INFO("StudioSim/Prod: {} found a bug (bugs={})", emp.displayName, state_.bugCount);
            }
            else if (output.amount > 0.0f)
            {
                AddMeter(state_.meters, output.meter, output.amount);
                ApplyOutputToEmployee(emp, output);
                visualEvents_.push_back(
                    {emp.position + glm::vec3(0.0f, 2.0f, 0.0f),
                     fmt::format("+{:.0f} {}", output.amount, MeterLabelZh(output.meter)), output.meter});
                SPDLOG_INFO("StudioSim/Prod: {} +{:.1f} {} (T/D/A/P={:.0f}/{:.0f}/{:.0f}/{:.0f})",
                            emp.displayName, output.amount, MeterLabel(output.meter), state_.meters.tech,
                            state_.meters.design, state_.meters.art, state_.meters.polish);
            }

            emp.nextWorkOutputAt = gameMinutes + JitteredInterval(emp, gameMinutes);
        }

        AdvanceStage(gameMinutes);
        RecalculateProgress();

        const int progressBucket = static_cast<int>(std::floor(state_.overallProgress * 10.0f));
        if (progressBucket != lastLoggedProgressBucket_)
        {
            lastLoggedProgressBucket_ = progressBucket;
            SPDLOG_INFO("StudioSim/Prod: stage={} progress={:.0f}% bugs={} fixed={}", ProjectStageName(state_.stage),
                        state_.overallProgress * 100.0f, state_.bugCount, state_.bugsFixed);
        }
    }

    void ProductionSystem::Reset()
    {
        state_ = FProjectState{};
        active_ = false;
        polishBugBatchGenerated_ = false;
        activeGoalTitle_.clear();
        lastLoggedProgressBucket_ = -1;
        lastTickGameMinutes_ = -1.0;
        visualEvents_.clear();
        ClearFocusBoost();
    }

    void ProductionSystem::ForceShip(double gameMinutes, const std::string& reason)
    {
        if (!active_ || state_.stage == EProjectStage::Done)
        {
            return;
        }

        RecalculateProgress();
        state_.stage = EProjectStage::Done;
        state_.shipped = true;
        SPDLOG_INFO("StudioSim/Prod: project '{}' force shipped at {:.0f}min ({}) progress={:.0f}% bugs={}",
                    activeGoalTitle_, gameMinutes, reason, state_.overallProgress * 100.0f, state_.bugCount);
    }

    std::vector<FProductionVisualEvent> ProductionSystem::ConsumeVisualEvents()
    {
        std::vector<FProductionVisualEvent> events;
        events.swap(visualEvents_);
        return events;
    }

    void ProductionSystem::SetFocusBoost(const std::string& meter, float boost)
    {
        focusMeter_ = meter;
        focusBoost_ = std::max(1.0f, boost);
        SPDLOG_INFO("StudioSim/Prod: focus boost {} x{:.2f}", focusMeter_.empty() ? "none" : focusMeter_, focusBoost_);
    }

    void ProductionSystem::ClearFocusBoost()
    {
        focusMeter_.clear();
        focusBoost_ = 1.0f;
    }

    void ProductionSystem::RecalculateProgress()
    {
        const float targetTotal = MeterTotal(state_.targetMeters);
        float computedProgress = targetTotal > 0.0f ? CompletedMeterTotal(state_.meters, state_.targetMeters) / targetTotal
                                                    : 0.0f;

        if (state_.stage == EProjectStage::Polish || state_.stage == EProjectStage::Done)
        {
            const float bugDenom = static_cast<float>(state_.bugCount + state_.bugsFixed + 1);
            const float bugPenalty = bugDenom > 0.0f ? static_cast<float>(state_.bugCount) / bugDenom : 0.0f;
            computedProgress *= (1.0f - 0.3f * bugPenalty);
        }

        computedProgress = std::clamp(computedProgress, 0.0f, 1.0f);
        state_.overallProgress = std::max(state_.overallProgress, computedProgress);
    }

    void ProductionSystem::AdvanceStage(double gameMinutes)
    {
        if (state_.stage == EProjectStage::Production &&
            AllMetersReached(state_.meters, state_.targetMeters, kProductionToPolishRatio))
        {
            state_.stage = EProjectStage::Polish;
            if (!polishBugBatchGenerated_)
            {
                const int generatedBugs =
                    std::clamp(static_cast<int>(std::round(MeterTotal(state_.meters) / 500.0f)), 4, kMaxPolishBugs);
                state_.bugCount += generatedBugs;
                polishBugBatchGenerated_ = true;
                SPDLOG_INFO("StudioSim/Prod: entered Polish at {:.0f}min, generated {} bugs", gameMinutes,
                            generatedBugs);
            }
        }

        if (state_.stage == EProjectStage::Polish && state_.bugCount == 0 &&
            AllMetersReached(state_.meters, state_.targetMeters, 1.0f))
        {
            state_.stage = EProjectStage::Done;
            state_.shipped = true;
            SPDLOG_INFO("StudioSim/Prod: project '{}' shipped at {:.0f}min", activeGoalTitle_, gameMinutes);
        }
    }
}
