#include "GatheringSystem.h"

#include "EmployeeSystem.h"
#include "OfficeMap.h"
#include "ProductionSystem.h"
#include "StudioSimLabels.hpp"
#include "StudioSimProjectMetrics.hpp"

#include <algorithm>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Modules/NextAI/AIService.hpp"

namespace StudioSim
{
    namespace
    {
        constexpr double kEvalIntervalMinutes = 30.0;
        constexpr double kMeetingDurationMinutes = 35.0;
        constexpr double kPantryDurationMinutes = 20.0;
        constexpr double kLineIntervalSeconds = 3.0;
        constexpr double kGatheringBubbleDurationMinutes = 20.0;

        const char* GatheringKindName(EGatheringKind kind)
        {
            return kind == EGatheringKind::Meeting ? "Meeting" : "Pantry";
        }

        std::string FirstHighlightText(const FGameProject& gameProject)
        {
            if (gameProject.highlights.empty())
            {
                return "核心卖点";
            }
            return gameProject.highlights.front().text;
        }

        std::string AllHighlightsText(const FGameProject& gameProject)
        {
            if (gameProject.highlights.empty())
            {
                return "核心卖点";
            }
            std::string text;
            for (size_t i = 0; i < gameProject.highlights.size(); ++i)
            {
                if (i > 0)
                {
                    text += "、";
                }
                text += gameProject.highlights[i].text;
            }
            return text;
        }

        std::string ParticipantsText(const std::vector<FEmployee>& employees, const std::vector<size_t>& participants)
        {
            std::string text;
            for (size_t idx : participants)
            {
                if (idx >= employees.size())
                {
                    continue;
                }
                if (!text.empty())
                {
                    text += "、";
                }
                text += fmt::format("{}（{}）", employees[idx].displayName, RoleName(employees[idx].role));
            }
            return text;
        }

        // 会议/茶水间的 LLM 提示词：注入项目类型/题材/体验要点 + 真实进度/最短板/Bug，
        // 让对白与决策都"针对这款游戏的当前状态"，而非套模板。
        std::string BuildGatheringPrompt(const FGathering& gathering, const std::vector<FEmployee>& employees,
                                         const FGameProject& gameProject)
        {
            const FProjectState& project = gameProject.production;
            const std::string projectName = gameProject.name.empty() ? std::string("项目") : gameProject.name;
            const std::string participants = ParticipantsText(employees, gathering.participants);

            if (gathering.kind == EGatheringKind::Pantry)
            {
                return fmt::format(
                    "你是游戏工作室茶水间闲聊编剧。大家在做游戏《{}》（{}·{}）。话题：{}。\n"
                    "参与者：{}。\n"
                    "生成 4-5 句轻松的多人闲聊：每句由一名参与者发言、不超过16字，可以吐槽/打气/聊这款游戏的题材或手感，"
                    "别谈正式分工或具体数字。只输出JSON，不要解释：\n"
                    "{{\"lines\":[{{\"speaker\":\"姓名\",\"line\":\"...\"}}]}}",
                    projectName, GameGenreLabelZh(gameProject.genre, ELabelTextStyle::Prompt), GameThemeLabelZh(gameProject.theme, ELabelTextStyle::Prompt),
                    gathering.topic, participants);
            }

            const FMeterSnapshot weakest = WeakestMeter(project);
            const int plannedDays = std::max(1, gameProject.plannedDays);
            const int projectDay = std::clamp(gameProject.elapsedDays + 1, 1, plannedDays);
            return fmt::format(
                "你是游戏工作室会议编剧，围绕这款具体游戏写一段简短会议。\n"
                "项目：《{}》，类型 {}，题材 {}。体验要点：{}。工期 第 {}/{} 天。\n"
                "进度：{}期，总进度 {:.0f}%。最短板：{} {:.0f}/{:.0f}。待修 Bug：{}。\n"
                "议题：{}。参会者：{}。\n"
                "生成 6-8 句多人对话：每句由一名参会者发言、不超过16字，必须结合上面的真实数字/最短板/这款游戏的特性来说，"
                "不要喊空泛口号。最后给出一个群体决策（团队接下来集中补哪块仪表，可选地让谁改做什么）。\n"
                "只输出JSON，不要解释：\n"
                "{{\"lines\":[{{\"speaker\":\"姓名\",\"line\":\"≤16字\"}}],"
                "\"decision\":{{\"summary\":\"≤20字\",\"focus_meter\":\"tech|design|art|polish\","
                "\"reassign\":[{{\"who\":\"姓名\",\"task\":\"≤12字\"}}]}}}}",
                projectName, GameGenreLabelZh(gameProject.genre, ELabelTextStyle::Prompt), GameThemeLabelZh(gameProject.theme, ELabelTextStyle::Prompt),
                AllHighlightsText(gameProject), projectDay, plannedDays, ProjectStageLabelZh(project.stage),
                project.overallProgress * 100.0f, weakest.label, weakest.value, weakest.target,
                project.bugCount, gathering.topic, participants);
        }

        void ParseGatheringResult(const std::string& text, std::vector<FMeetingLine>& outLines,
                                  FGroupDecision& outDecision, bool& outHasDecision)
        {
            outLines.clear();
            outHasDecision = false;
            const size_t open = text.find('{');
            const size_t close = text.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return;
            }
            try
            {
                const nlohmann::json json = nlohmann::json::parse(text.substr(open, close - open + 1));
                if (json.contains("lines") && json["lines"].is_array())
                {
                    for (const auto& item : json["lines"])
                    {
                        FMeetingLine line;
                        line.speaker = item.value("speaker", std::string());
                        line.text = item.value("line", std::string());
                        if (!line.speaker.empty() && !line.text.empty())
                        {
                            outLines.push_back(std::move(line));
                        }
                        if (outLines.size() >= 10)
                        {
                            break;
                        }
                    }
                }
                if (json.contains("decision") && json["decision"].is_object())
                {
                    const auto& dec = json["decision"];
                    outDecision.summary = dec.value("summary", std::string());
                    const std::string focus = dec.value("focus_meter", std::string());
                    if (focus == "tech" || focus == "design" || focus == "art" || focus == "polish")
                    {
                        outDecision.focusMeter = focus;
                    }
                    outDecision.reassign.clear();
                    if (dec.contains("reassign") && dec["reassign"].is_array())
                    {
                        for (const auto& r : dec["reassign"])
                        {
                            const std::string who = r.value("who", std::string());
                            const std::string task = r.value("task", std::string());
                            if (!who.empty() && !task.empty())
                            {
                                outDecision.reassign.emplace_back(who, task);
                            }
                        }
                    }
                    outHasDecision = !outDecision.summary.empty() || !outDecision.focusMeter.empty();
                }
            }
            catch (...)
            {
                outLines.clear();
                outHasDecision = false;
            }
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
                                     fmt::format("《{}》先补{}", projectName, ProjectMeterLabelZh(focusMeter))});
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
                                     fmt::format("{}美术量先收敛", GameThemeLabelZh(gameProject.theme, ELabelTextStyle::Prompt))});
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
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_; // 作废所有在途 LLM 回调
        completed_.clear();
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
                                           const OfficeMap& office, const FGameProject& gameProject,
                                           NextAI::FAIService* ai)
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
                           world.gameClockMinutes, employees, office, gameProject, ai);
        }
        else if (!HasKind(gatherings_, EGatheringKind::Meeting) && world.gameClockMinutes >= 13.0 * 60.0)
        {
            const FMeterSnapshot weakest = WeakestMeter(project);
            if (MeterCompletion(weakest) < 0.4f)
            {
                StartGathering(EGatheringKind::Meeting,
                               fmt::format("《{}》{}进度落后，临时碰头", projectName, weakest.label),
                               world.gameClockMinutes, employees, office, gameProject, ai);
            }
        }

        const double hour = world.gameClockMinutes / 60.0;
        if (!HasKind(gatherings_, EGatheringKind::Pantry) &&
            ((hour >= 12.0 && hour < 12.4) || (hour >= 15.5 && hour < 15.9)))
        {
            StartGathering(EGatheringKind::Pantry,
                           fmt::format("茶水间聊《{}》{}", projectName, GameThemeLabelZh(gameProject.theme, ELabelTextStyle::Prompt)),
                           world.gameClockMinutes, employees, office, gameProject, ai);
        }
    }

    void GatheringSystem::StartGathering(EGatheringKind kind, const std::string& topic, double gameMinutes,
                                         std::vector<FEmployee>& employees, const OfficeMap& office,
                                         const FGameProject& gameProject, NextAI::FAIService* ai)
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
                            weakest.label);
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

        // 先有模板对白/决策垫底；同时异步请求 LLM 生成进度感知的对白与决策，回来后在 Tick 回灌。
        RequestGatheringContent(gathering, employees, gameProject, ai);

        SPDLOG_INFO("StudioSim/Gathering started {} id={} topic='{}' participants={}", GatheringKindName(kind),
                    gathering.id, topic, participants.size());
        gatherings_.push_back(std::move(gathering));
    }

    void GatheringSystem::Tick(double deltaSeconds, FWorldState& world, std::vector<FEmployee>& employees,
                               const OfficeMap& office, const FGameProject& gameProject, NextAI::FAIService* ai)
    {
        // 回灌 LLM 异步生成的聚集内容（主线程消费）。
        {
            std::vector<FPendingContent> ready;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                ready.swap(completed_);
            }
            for (const auto& pending : ready)
            {
                for (auto& gathering : gatherings_)
                {
                    if (gathering.id != pending.id || gathering.state == EGatheringState::Dispersing)
                    {
                        continue;
                    }
                    // 对白：仅在尚未进入决策确认、仍在播放时整体替换并从头重播（保证一致连续）。
                    if (!pending.lines.empty() && !gathering.awaitingConfirm &&
                        gathering.state == EGatheringState::Talking)
                    {
                        gathering.lines = pending.lines;
                        gathering.nextLineIndex = 0;
                        gathering.elapsedRealSeconds = 0.0;
                        gathering.nextLineRealSeconds = 1.0;
                    }
                    // 决策：会议未被采纳/否决前，用 LLM 决策覆盖模板决策。
                    if (pending.hasDecision && gathering.kind == EGatheringKind::Meeting &&
                        !gathering.decision.accepted && !gathering.decision.rejected)
                    {
                        if (!pending.decision.summary.empty())
                        {
                            gathering.decision.summary = pending.decision.summary;
                        }
                        if (!pending.decision.focusMeter.empty())
                        {
                            gathering.decision.focusMeter = pending.decision.focusMeter;
                        }
                        gathering.decision.reassign = pending.decision.reassign;
                        gathering.decision.valid = true;
                    }
                    break;
                }
            }
        }

        if (!pendingMeetingTopics_.empty() && !HasKind(gatherings_, EGatheringKind::Meeting))
        {
            const std::string topic = pendingMeetingTopics_.front();
            pendingMeetingTopics_.erase(pendingMeetingTopics_.begin());
            StartGathering(EGatheringKind::Meeting, topic, world.gameClockMinutes, employees, office, gameProject, ai);
        }

        EvaluateTriggers(world, employees, office, gameProject, ai);

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
                        employees[idx].bubbleClearAt = world.gameClockMinutes + kGatheringBubbleDurationMinutes;
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

    void GatheringSystem::RequestGatheringContent(FGathering& gathering, const std::vector<FEmployee>& employees,
                                                  const FGameProject& gameProject, NextAI::FAIService* ai)
    {
        if (ai == nullptr || gathering.participants.empty())
        {
            return; // 无 LLM：保留模板对白/决策（确定性 fallback）。
        }

        const std::string prompt = BuildGatheringPrompt(gathering, employees, gameProject);
        const int gatheringId = gathering.id;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            generation = generation_;
        }

        ai->GenerateTextAsync(prompt,
                              [this, gatheringId, generation](NextAI::FAIResponse response)
                              {
                                  // Worker thread：只解析 + 入队，绝不触碰 gatherings_/employees。
                                  std::vector<FMeetingLine> lines;
                                  FGroupDecision decision;
                                  bool hasDecision = false;
                                  ParseGatheringResult(response.success ? response.text : std::string(), lines, decision,
                                                       hasDecision);
                                  if (lines.empty() && !hasDecision)
                                  {
                                      return;
                                  }
                                  std::lock_guard<std::mutex> lock(mutex_);
                                  if (generation != generation_)
                                  {
                                      return;
                                  }
                                  FPendingContent pending;
                                  pending.id = gatheringId;
                                  pending.generation = generation;
                                  pending.lines = std::move(lines);
                                  pending.decision = std::move(decision);
                                  pending.hasDecision = hasDecision;
                                  completed_.push_back(std::move(pending));
                              });
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
                    employees[idx].todayTask =
                        fmt::format("集中补{}", ProjectMeterLabelZh(gathering.decision.focusMeter));
                    PushMemory(employees[idx], gameMinutes, fmt::format("会议采纳：{}", gathering.decision.summary));
                }
            }
            // LLM 群体决策里的改派覆盖到具体同事（让"开会→执行"接上）。
            for (const auto& r : gathering.decision.reassign)
            {
                for (auto& emp : employees)
                {
                    if (emp.displayName == r.first)
                    {
                        emp.todayTask = r.second;
                        PushMemory(emp, gameMinutes, fmt::format("会议改派：{}", r.second));
                        break;
                    }
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
