#include "GatheringSystem.h"

#include "EmployeeSystem.h"
#include "OfficeMap.h"
#include "ProductionSystem.h"

#include <algorithm>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace StudioSim
{
    namespace
    {
        constexpr double kEvalIntervalMinutes = 30.0;
        constexpr double kMeetingDurationMinutes = 35.0;
        constexpr double kPantryDurationMinutes = 20.0;
        constexpr double kLineIntervalSeconds = 3.0;
        constexpr double kBubbleDurationMinutes = 20.0;

        const char* GatheringKindName(EGatheringKind kind)
        {
            return kind == EGatheringKind::Meeting ? "Meeting" : "Pantry";
        }

        const char* MeterLabelZh(const std::string& meter)
        {
            if (meter == "tech") return "技术";
            if (meter == "design") return "玩法";
            if (meter == "art") return "美术";
            if (meter == "polish") return "品质";
            return "短板";
        }

        const char* GameThemeLabelZh(EGameTheme theme)
        {
            switch (theme)
            {
            case EGameTheme::Fantasy: return "奇幻";
            case EGameTheme::SciFi:   return "科幻";
            case EGameTheme::Sports:  return "体育";
            case EGameTheme::Romance: return "恋爱";
            case EGameTheme::Horror:  return "恐怖";
            case EGameTheme::Daily:   return "日常";
            default:                  return "题材";
            }
        }

        struct FMeterSnapshot
        {
            std::string key;
            float value = 0.0f;
            float target = 0.0f;
        };

        float Completion(const FMeterSnapshot& meter)
        {
            return meter.target > 0.0f ? std::clamp(meter.value / meter.target, 0.0f, 1.0f) : 1.0f;
        }

        FMeterSnapshot WeakestMeter(const FProjectState& project)
        {
            FMeterSnapshot meters[] = {
                {"tech", project.meters.tech, project.targetMeters.tech},
                {"design", project.meters.design, project.targetMeters.design},
                {"art", project.meters.art, project.targetMeters.art},
                {"polish", project.meters.polish, project.targetMeters.polish},
            };
            FMeterSnapshot weakest = meters[0];
            for (const auto& meter : meters)
            {
                if (Completion(meter) < Completion(weakest))
                {
                    weakest = meter;
                }
            }
            return weakest;
        }

        std::string FirstHighlightText(const FGameProject& gameProject)
        {
            if (gameProject.highlights.empty())
            {
                return "核心卖点";
            }
            return gameProject.highlights.front().text;
        }

        bool HasKind(const std::vector<FGathering>& gatherings, EGatheringKind kind)
        {
            for (const auto& gathering : gatherings)
            {
                if (gathering.kind == kind && gathering.state != EGatheringState::Dispersing)
                {
                    return true;
                }
            }
            return false;
        }

        void PushMemory(FEmployee& emp, double gameMinutes, const std::string& text)
        {
            int hh = 0, mm = 0;
            MinutesToHHMM(gameMinutes, hh, mm);
            emp.shortMemory.push_back(fmt::format("{:02d}:{:02d} {}", hh, mm, text));
            while (emp.shortMemory.size() > 4)
            {
                emp.shortMemory.erase(emp.shortMemory.begin());
            }
        }

        std::vector<FMeetingLine> BuildFallbackLines(EGatheringKind kind, const std::vector<FEmployee>& employees,
                                                     const std::vector<size_t>& participants,
                                                     const std::string& topic, const std::string& focusMeter,
                                                     const FGameProject& gameProject)
        {
            std::vector<FMeetingLine> lines;
            const std::string projectName = gameProject.name.empty() ? std::string("项目") : gameProject.name;
            const std::string highlight = FirstHighlightText(gameProject);
            if (kind == EGatheringKind::Pantry)
            {
                const std::string kPantryLines[] = {
                    fmt::format("《{}》这题材有感觉", projectName),
                    fmt::format("{}别丢", highlight),
                    "别让Bug追着跑",
                    "喝完继续冲"};
                for (size_t i = 0; i < participants.size() && i < 4; ++i)
                {
                    lines.push_back({employees[participants[i]].displayName, kPantryLines[i]});
                }
                return lines;
            }

            for (size_t idx : participants)
            {
                const auto& emp = employees[idx];
                if (emp.role == ERole::ProducerPM)
                {
                    lines.push_back({emp.displayName,
                                     fmt::format("《{}》先补{}", projectName, MeterLabelZh(focusMeter))});
                }
                else if (emp.role == ERole::Engineer)
                {
                    lines.push_back({emp.displayName, fmt::format("我压住{}技术风险", projectName)});
                }
                else if (emp.role == ERole::QA)
                {
                    lines.push_back({emp.displayName, "我盯Bug列表"});
                }
                else if (emp.role == ERole::Artist)
                {
                    lines.push_back({emp.displayName,
                                     fmt::format("{}美术量先收敛", GameThemeLabelZh(gameProject.theme))});
                }
                else if (emp.role == ERole::Designer)
                {
                    lines.push_back({emp.displayName, fmt::format("玩法要贴住{}", highlight)});
                }
            }
            if (lines.empty())
            {
                lines.push_back({"Team", topic});
            }
            return lines;
        }
    }

    void GatheringSystem::Reset()
    {
        gatherings_.clear();
        pendingMeetingTopics_.clear();
        nextEvalGameMinutes_ = 0.0;
        nextId_ = 1;
    }

    void GatheringSystem::RequestMeeting(const std::string& topic)
    {
        pendingMeetingTopics_.push_back(topic);
    }

    bool GatheringSystem::HasActiveMeeting() const
    {
        return HasKind(gatherings_, EGatheringKind::Meeting);
    }

    void GatheringSystem::EvaluateTriggers(const FWorldState& world, std::vector<FEmployee>& employees,
                                           const OfficeMap& office, const FGameProject& gameProject)
    {
        const FProjectState& project = gameProject.production;
        const std::string projectName = gameProject.name.empty() ? std::string("项目") : gameProject.name;
        if (world.gameClockMinutes < nextEvalGameMinutes_)
        {
            return;
        }
        nextEvalGameMinutes_ = world.gameClockMinutes + kEvalIntervalMinutes;

        if (!HasKind(gatherings_, EGatheringKind::Meeting) && project.stage == EProjectStage::Polish &&
            project.bugCount >= 6)
        {
            StartGathering(EGatheringKind::Meeting, fmt::format("《{}》Bug 太多，重新分工", projectName),
                           world.gameClockMinutes, employees, office, gameProject);
        }
        else if (!HasKind(gatherings_, EGatheringKind::Meeting) && world.gameClockMinutes >= 13.0 * 60.0)
        {
            const FMeterSnapshot weakest = WeakestMeter(project);
            if (Completion(weakest) < 0.4f)
            {
                StartGathering(EGatheringKind::Meeting,
                               fmt::format("《{}》{}进度落后，临时碰头", projectName, MeterLabelZh(weakest.key)),
                               world.gameClockMinutes, employees, office, gameProject);
            }
        }

        const double hour = world.gameClockMinutes / 60.0;
        if (!HasKind(gatherings_, EGatheringKind::Pantry) &&
            ((hour >= 12.0 && hour < 12.4) || (hour >= 15.5 && hour < 15.9)))
        {
            StartGathering(EGatheringKind::Pantry,
                           fmt::format("茶水间聊《{}》{}", projectName, GameThemeLabelZh(gameProject.theme)),
                           world.gameClockMinutes, employees, office, gameProject);
        }
    }

    void GatheringSystem::StartGathering(EGatheringKind kind, const std::string& topic, double gameMinutes,
                                         std::vector<FEmployee>& employees, const OfficeMap& office,
                                         const FGameProject& gameProject)
    {
        const FProjectState& project = gameProject.production;
        if (HasKind(gatherings_, kind))
        {
            return;
        }

        const std::string anchorCategory = kind == EGatheringKind::Meeting ? "meet" : "pantry";
        const auto anchors = office.PointsOfCategory(anchorCategory);
        if (anchors.empty())
        {
            return;
        }

        std::vector<size_t> participants;
        if (kind == EGatheringKind::Meeting)
        {
            for (size_t i = 0; i < employees.size(); ++i)
            {
                if (employees[i].gatheringId < 0 && employees[i].role == ERole::ProducerPM)
                {
                    participants.push_back(i);
                    break;
                }
            }
        }
        for (size_t i = 0; i < employees.size() && participants.size() < (kind == EGatheringKind::Meeting ? 4u : 3u); ++i)
        {
            if (employees[i].gatheringId < 0 && std::find(participants.begin(), participants.end(), i) == participants.end())
            {
                participants.push_back(i);
            }
        }
        if (participants.size() < 2)
        {
            return;
        }

        const FMeterSnapshot weakest = WeakestMeter(project);
        FGathering gathering;
        gathering.id = nextId_++;
        gathering.kind = kind;
        gathering.state = EGatheringState::Talking;
        gathering.topic = topic;
        gathering.anchorCategory = anchorCategory;
        gathering.participants = participants;
        gathering.startGameMinutes = gameMinutes;
        gathering.endGameMinutes = gameMinutes + (kind == EGatheringKind::Meeting ? kMeetingDurationMinutes
                                                                                   : kPantryDurationMinutes);
        gathering.lines = BuildFallbackLines(kind, employees, participants, topic, weakest.key, gameProject);
        if (kind == EGatheringKind::Meeting)
        {
            gathering.decision.summary =
                fmt::format("为《{}》集中补{}", gameProject.name.empty() ? std::string("项目") : gameProject.name,
                            MeterLabelZh(weakest.key));
            gathering.decision.focusMeter = weakest.key;
            gathering.decision.valid = true;
        }

        for (size_t p = 0; p < participants.size(); ++p)
        {
            FEmployee& emp = employees[participants[p]];
            emp.gatheringId = gathering.id;
            emp.overrideTargetPoi = anchors[p % anchors.size()]->name;
            emp.overrideUntilMinutes = gathering.endGameMinutes;
            emp.targetPoi.clear();
            emp.decisionPending = false;
            emp.bubbleText = kind == EGatheringKind::Meeting ? "去会议室" : "去茶水间";
            emp.bubbleClearAt = gameMinutes + 8.0;
        }

        SPDLOG_INFO("StudioSim/Gathering started {} id={} topic='{}' participants={}", GatheringKindName(kind),
                    gathering.id, topic, participants.size());
        gatherings_.push_back(std::move(gathering));
    }

    void GatheringSystem::Tick(double deltaSeconds, FWorldState& world, std::vector<FEmployee>& employees,
                               const OfficeMap& office, const FGameProject& gameProject)
    {
        if (!pendingMeetingTopics_.empty() && !HasKind(gatherings_, EGatheringKind::Meeting))
        {
            const std::string topic = pendingMeetingTopics_.front();
            pendingMeetingTopics_.erase(pendingMeetingTopics_.begin());
            StartGathering(EGatheringKind::Meeting, topic, world.gameClockMinutes, employees, office, gameProject);
        }

        EvaluateTriggers(world, employees, office, gameProject);

        for (auto& gathering : gatherings_)
        {
            if (gathering.state == EGatheringState::Dispersing || gathering.awaitingConfirm)
            {
                if (gathering.awaitingConfirm)
                {
                    for (size_t idx : gathering.participants)
                    {
                        if (idx < employees.size())
                        {
                            employees[idx].overrideUntilMinutes = world.gameClockMinutes + 5.0;
                        }
                    }
                }
                continue;
            }

            gathering.elapsedRealSeconds += deltaSeconds;
            if (gathering.nextLineIndex < gathering.lines.size() &&
                gathering.elapsedRealSeconds >= gathering.nextLineRealSeconds)
            {
                const FMeetingLine& line = gathering.lines[gathering.nextLineIndex++];
                for (size_t idx : gathering.participants)
                {
                    if (idx < employees.size() && employees[idx].displayName == line.speaker)
                    {
                        employees[idx].bubbleText = line.text;
                        employees[idx].bubbleClearAt = world.gameClockMinutes + kBubbleDurationMinutes;
                        employees[idx].mood = gathering.kind == EGatheringKind::Pantry ? EMood::Calm : EMood::Focused;
                        break;
                    }
                }
                gathering.nextLineRealSeconds += kLineIntervalSeconds;
            }

            const bool linesDone = gathering.nextLineIndex >= gathering.lines.size();
            if (linesDone && gathering.kind == EGatheringKind::Meeting && gathering.decision.valid)
            {
                gathering.state = EGatheringState::Deciding;
                gathering.awaitingConfirm = true;
                SPDLOG_INFO("StudioSim/Gathering decision pending id={} summary='{}' focus={}", gathering.id,
                            gathering.decision.summary, gathering.decision.focusMeter);
            }
            else if (linesDone && gathering.kind == EGatheringKind::Pantry)
            {
                for (size_t idx : gathering.participants)
                {
                    if (idx < employees.size())
                    {
                        employees[idx].mood = EMood::Focused;
                    }
                }
                ReleaseGathering(gathering, world.gameClockMinutes, employees);
            }
            else if (gathering.kind != EGatheringKind::Meeting && world.gameClockMinutes >= gathering.endGameMinutes)
            {
                ReleaseGathering(gathering, world.gameClockMinutes, employees);
            }
        }

        gatherings_.erase(std::remove_if(gatherings_.begin(), gatherings_.end(),
                                         [](const FGathering& gathering)
                                         {
                                             return gathering.state == EGatheringState::Dispersing;
                                         }),
                          gatherings_.end());
    }

    void GatheringSystem::AcceptDecision(int gatheringId, double gameMinutes, std::vector<FEmployee>& employees,
                                         ProductionSystem& production)
    {
        for (auto& gathering : gatherings_)
        {
            if (gathering.id != gatheringId || !gathering.awaitingConfirm)
            {
                continue;
            }
            gathering.decision.accepted = true;
            production.SetFocusBoost(gathering.decision.focusMeter, 1.3f);
            for (size_t idx : gathering.participants)
            {
                if (idx < employees.size())
                {
                    employees[idx].todayTask = fmt::format("集中补{}", MeterLabelZh(gathering.decision.focusMeter));
                    PushMemory(employees[idx], gameMinutes, fmt::format("会议采纳：{}", gathering.decision.summary));
                }
            }
            ReleaseGathering(gathering, gameMinutes, employees);
            return;
        }
    }

    void GatheringSystem::RejectDecision(int gatheringId, double gameMinutes, std::vector<FEmployee>& employees)
    {
        for (auto& gathering : gatherings_)
        {
            if (gathering.id != gatheringId || !gathering.awaitingConfirm)
            {
                continue;
            }
            gathering.decision.rejected = true;
            for (size_t idx : gathering.participants)
            {
                if (idx < employees.size())
                {
                    PushMemory(employees[idx], gameMinutes, fmt::format("会议否决：{}", gathering.decision.summary));
                }
            }
            ReleaseGathering(gathering, gameMinutes, employees);
            return;
        }
    }

    void GatheringSystem::ReleaseGathering(FGathering& gathering, double gameMinutes, std::vector<FEmployee>& employees)
    {
        for (size_t idx : gathering.participants)
        {
            if (idx >= employees.size())
            {
                continue;
            }
            FEmployee& emp = employees[idx];
            emp.gatheringId = -1;
            emp.overrideTargetPoi.clear();
            emp.overrideUntilMinutes = 0.0;
            emp.nextDecisionAt = gameMinutes;
        }
        gathering.state = EGatheringState::Dispersing;
        gathering.awaitingConfirm = false;
        SPDLOG_INFO("StudioSim/Gathering ended id={} topic='{}'", gathering.id, gathering.topic);
    }
}
