#include "DecisionScheduler.h"

#include "EmployeeSystem.h"
#include "OfficeMap.h"
#include "StructuredDecisionContract.hpp"
#include "StudioSimLabels.hpp"
#include "StudioSimProjectMetrics.hpp"

#include <algorithm>
#include <chrono>

#include <spdlog/spdlog.h>

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AIService.hpp"

namespace StudioSim
{
    namespace
    {
        // 每个员工两次 LLM 决策之间的最小游戏时间间隔（分钟）。
        constexpr double kDecisionIntervalMinutes = 60.0;
        constexpr double kLlmTimeoutSeconds = 15.0;
        constexpr size_t kShortMemoryLimit = 4;
        constexpr size_t kLogLimit = 60;
        constexpr double kDecisionBubbleDurationMinutes = 25.0;

        std::string BuildHighlightsText(const FGameProject& gameProject)
        {
            if (gameProject.highlights.empty())
            {
                return "暂无";
            }

            std::string text;
            for (size_t i = 0; i < gameProject.highlights.size(); ++i)
            {
                if (i > 0)
                {
                    text += "、";
                }
                const FHighlight& highlight = gameProject.highlights[i];
                text += highlight.text;
                text += highlight.achieved ? "（已做实）" : "（待做实）";
            }
            return text;
        }

        std::string BuildProjectIdentityLine(const FGameProject& gameProject)
        {
            if (gameProject.name.empty())
            {
                return {};
            }

            const int plannedDays = std::max(1, gameProject.plannedDays);
            const int projectDay = std::clamp(gameProject.elapsedDays + 1, 1, plannedDays);
            return fmt::format(
                "[立项设定]\n"
                "项目：《{}》。类型：{}，题材：{}，契合度 {:.0f}%。工期：第 {}/{} 天。\n"
                "体验要点：{}。\n"
                "说话和行动要贴合这个项目，不要只泛泛而谈进度。\n",
                gameProject.name, GameGenreLabelZh(gameProject.genre, ELabelTextStyle::Prompt), GameThemeLabelZh(gameProject.theme, ELabelTextStyle::Prompt),
                gameProject.comboFit * 100.0f, projectDay, plannedDays, BuildHighlightsText(gameProject));
        }

        std::string BuildProjectLine(const FGameProject& gameProject, const FEmployee& emp)
        {
            const FProjectState& project = gameProject.production;
            const FMeterSnapshot weakest = WeakestMeter(project);
            std::string line = fmt::format(
                "[项目进度]\n"
                "阶段：{}。总进度 {:.0f}%。\n"
                "仪表：技术 {:.0f}/{:.0f}，玩法 {:.0f}/{:.0f}，美术 {:.0f}/{:.0f}，品质 {:.0f}/{:.0f}。\n"
                "短板：{} {:.0f}/{:.0f}，优先补这块。\n",
                ProjectStageLabelZh(project.stage), project.overallProgress * 100.0f, project.meters.tech,
                project.targetMeters.tech, project.meters.design, project.targetMeters.design, project.meters.art,
                project.targetMeters.art, project.meters.polish, project.targetMeters.polish, weakest.label,
                weakest.value, weakest.target);

            if (project.stage == EProjectStage::Polish)
            {
                line += fmt::format("待修 Bug：{}。工程师优先修 bug，QA 优先验证/复现。\n", project.bugCount);
            }
            line += fmt::format("你今天累计产出：技术 +{:.0f}，玩法 +{:.0f}，美术 +{:.0f}，品质 +{:.0f}。\n",
                                emp.myContribution.tech, emp.myContribution.design, emp.myContribution.art,
                                emp.myContribution.polish);
            line += "决策原则：优先选择能补最短板的 WORK；不能直接补短板时，协调同事或回本职工位推进。\n";
            return line;
        }

        std::string BuildMemoryLine(const FEmployee& emp)
        {
            if (emp.shortMemory.empty())
            {
                return {};
            }

            std::string line = "最近记忆：";
            for (size_t i = 0; i < emp.shortMemory.size(); ++i)
            {
                if (i > 0)
                {
                    line += "；";
                }
                line += emp.shortMemory[i];
            }
            line += '\n';
            return line;
        }

        void PushShortMemory(FEmployee& emp, double gameMinutes, const std::string& text)
        {
            int hh = 0, mm = 0;
            MinutesToHHMM(gameMinutes, hh, mm);
            emp.shortMemory.push_back(fmt::format("{:02d}:{:02d} {}", hh, mm, text));
            while (emp.shortMemory.size() > kShortMemoryLimit)
            {
                emp.shortMemory.erase(emp.shortMemory.begin());
            }
        }

        // 取员工当日产出最高的那项，作为"进度感知"的记忆片段（让记忆/对白挂钩真实开发进度）。
        std::string DominantContribution(const FProjectMeters& c)
        {
            struct FEntry { const char* zh; float value; };
            const FEntry entries[] = {{"技术", c.tech}, {"玩法", c.design}, {"美术", c.art}, {"品质", c.polish}};
            const FEntry* best = &entries[0];
            for (const auto& entry : entries)
            {
                if (entry.value > best->value)
                {
                    best = &entry;
                }
            }
            if (best->value <= 0.0f)
            {
                return {};
            }
            return fmt::format("已产出{}+{:.0f}", best->zh, best->value);
        }

        int ChatterBudgetPerHour(const std::string& personality)
        {
            if (personality.find("沉默寡言") != std::string::npos)
            {
                return 0;
            }
            if (personality.find("话痨") != std::string::npos || personality.find("话密") != std::string::npos)
            {
                return 2;
            }
            return 1;
        }

        bool ShouldShowDialogue(FEmployee& emp, const FDecisionResult& result, double gameMinutes, bool hadIncoming)
        {
            if (result.dialogue.empty())
            {
                return false;
            }

            if (hadIncoming || emp.eventReactionPending || result.action == "TALK" || result.action == "MEETING")
            {
                emp.eventReactionPending = false;
                return true;
            }
            emp.eventReactionPending = false;

            const int budget = ChatterBudgetPerHour(emp.personality);
            if (budget <= 0 || gameMinutes < emp.nextChatterAt)
            {
                return false;
            }

            emp.nextChatterAt = gameMinutes + 60.0 / static_cast<double>(budget);
            return true;
        }

        std::string BuildPrompt(const FEmployee& emp, const FDailyGoal& goal, const FGameProject& gameProject,
                                const std::string& eventsSummary, const std::vector<FEmployee>& allEmployees,
                                double gameMinutes, const OfficeMap& office)
        {
            int hh = 0, mm = 0;
            MinutesToHHMM(gameMinutes, hh, mm);

            std::string poiList;
            for (const auto& poi : office.Points())
            {
                if (!poi.enabled)
                {
                    continue; // 断电/宕机的点位不列给 LLM
                }
                poiList += poi.name;
                poiList += ' ';
            }

            std::string mates;
            for (const auto& other : allEmployees)
            {
                if (other.id == emp.id)
                {
                    continue;
                }
                mates += fmt::format("{}（{}）", other.displayName, RoleName(other.role));
                mates += ' ';
            }

            std::string goalLine;
            if (goal.set)
            {
                goalLine = fmt::format("今天的团队目标是：{}（{}）。你的今日重点：{}。\n", goal.title, goal.description,
                                       emp.todayTask.empty() ? std::string("按本职推进目标") : emp.todayTask);
            }

            std::string eventLine;
            if (!eventsSummary.empty() && eventsSummary != "暂无")
            {
                eventLine = fmt::format("⚠ 今天发生了大事：{}。请结合这件事重新考虑你的行动。\n", eventsSummary);
            }

            std::string incomingLine;
            if (!emp.pendingFrom.empty())
            {
                incomingLine = fmt::format("刚才{}对你说：「{}」。请回应他/她。\n", emp.pendingFrom, emp.pendingText);
            }

            const std::string projectIdentityLine = BuildProjectIdentityLine(gameProject);
            const std::string projectLine = BuildProjectLine(gameProject, emp);
            const std::string memoryLine = BuildMemoryLine(emp);

            return fmt::format(
                "你是一家游戏工作室的员工{}（职位：{}）。性格：{}。现在是{:02d}:{:02d}。\n"
                "{}{}{}{}{}{}"
                "你的同事有：{}\n"
                "办公室里你可以去的点位有：{}\n"
                "结合你的职位、性格、今日目标和当前状况，决定你接下来做一件事。需要群体决策/重排计划时用 action=MEETING "
                "并选择会议室 meet_seat_*；想找某位同事说话就用 action=TALK 并在 target_employee 填同事名。"
                "dialogue 可以留空；常规 WORK 不要说话。一旦开口，必须引用上面看到的具体进度/最短板/你自己的累计产出"
                "（例如「技术补到120了」「美术还差40」「我今天写了60技术」），不要说「加油」「没问题」这类空话。"
                "只输出一个JSON对象，不要任何解释或markdown：\n"
                "{{\"action\":\"WORK|REST|TALK|MEETING|IDLE\",\"target_poi\":\"<上面列表里的一个点位名>\","
                "\"target_employee\":\"<TALK时填一个同事名，否则空字符串>\",\"dialogue\":\"<可为空；要说就带上具体进度数字，一句不超过15字>\","
                "\"mood\":\"calm|focused|stressed|excited|bored|panicked\",\"duration_minutes\":<10到60的整数>}}",
                emp.displayName, RoleName(emp.role), emp.personality, hh, mm, goalLine, projectIdentityLine,
                projectLine, memoryLine, eventLine, incomingLine, mates, poiList);
        }

    }

    double DecisionScheduler::InFlightElapsedMs() const
    {
        if (!inFlight_)
        {
            return 0.0;
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - inFlightStartedAt_)
            .count();
    }

    void DecisionScheduler::PushLog(const FEmployee& employee, std::string summary,
                                    std::string prompt, std::string response, double elapsedMs,
                                    bool success)
    {
        log_.push_back({
            .id = nextLogId_++,
            .employeeName = employee.displayName,
            .summary = std::move(summary),
            .prompt = std::move(prompt),
            .response = std::move(response),
            .elapsedMs = elapsedMs,
            .success = success,
        });
        if (log_.size() > kLogLimit)
        {
            log_.erase(log_.begin(), log_.begin() +
                                         static_cast<long long>(log_.size() - kLogLimit));
        }
    }

    void DecisionScheduler::Tick(double gameMinutes, const FDailyGoal& goal, const FGameProject& gameProject,
                                 const std::string& eventsSummary, NextAI::FAIService* ai,
                                 std::vector<FEmployee>& employees, const OfficeMap& office)
    {
        // 1. 排空已完成的决策，在主线程 apply。
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& pending : completed_)
            {
                if (pending.empIndex < employees.size())
                {
                    if (pending.result.valid)
                    {
                        ApplyResult(employees, pending.empIndex, pending.result, gameMinutes, office);
                        PushLog(employees[pending.empIndex],
                                fmt::format("LLM · {}", pending.result.action), pending.prompt,
                                pending.response, pending.elapsedMs, true);
                    }
                    else
                    {
                        const std::string reason =
                            pending.serviceSuccess ? "响应解析失败"
                                                   : (pending.errorMessage.empty()
                                                          ? "服务失败"
                                                          : fmt::format("服务失败: {}", pending.errorMessage));
                        ApplyFallback(employees, pending.empIndex, gameMinutes, office, reason,
                                      pending.elapsedMs, pending.prompt, pending.response);
                    }
                }
            }
            if (!completed_.empty())
            {
                completed_.clear();
                inFlight_ = false;
            }
        }

        if (inFlight_ && InFlightElapsedMs() >= kLlmTimeoutSeconds * 1000.0)
        {
            ++generation_;
            if (inFlightEmployeeIndex_ < employees.size())
            {
                ApplyFallback(employees, inFlightEmployeeIndex_, gameMinutes, office, "请求超时",
                              InFlightElapsedMs(), inFlightPrompt_, {});
            }
            inFlight_ = false;
            inFlightPrompt_.clear();
        }

        // 2. 空闲则发起下一个决策（在途上限 1，匹配 llama-server parallel:1）。
        if (inFlight_ || employees.empty())
        {
            return;
        }

        size_t chosenIndex = employees.size();
        size_t idleIndex = employees.size();
        for (size_t offset = 0; offset < employees.size(); ++offset)
        {
            const size_t i = (scanCursor_ + offset) % employees.size();
            FEmployee& emp = employees[i];
            if (emp.gatheringId >= 0 || emp.decisionPending || gameMinutes < emp.nextDecisionAt)
            {
                continue;
            }
            if (emp.eventReactionPending || !emp.pendingFrom.empty())
            {
                chosenIndex = i;
                break;
            }
            if (idleIndex == employees.size())
            {
                idleIndex = i;
            }
        }
        if (chosenIndex == employees.size())
        {
            chosenIndex = idleIndex;
        }
        if (chosenIndex == employees.size())
        {
            return;
        }

        scanCursor_ = (chosenIndex + 1) % employees.size();
        FEmployee& employee = employees[chosenIndex];
        employee.nextDecisionAt = gameMinutes + kDecisionIntervalMinutes;
        ++decisionsMade_;

        if (ai == nullptr)
        {
            ApplyFallback(employees, chosenIndex, gameMinutes, office, "AI 不可用");
            return;
        }

        const std::string prompt =
            BuildPrompt(employee, goal, gameProject, eventsSummary, employees, gameMinutes, office);
        employee.decisionPending = true;
        inFlight_ = true;
        inFlightEmployeeIndex_ = chosenIndex;
        inFlightStartedAt_ = std::chrono::steady_clock::now();
        inFlightPrompt_ = prompt;

        const uint64_t generation = generation_;
        ai->GenerateStructuredTextAsync(prompt, "studio_employee_decision", std::string(kStructuredDecisionSchema),
                              [this, chosenIndex, generation, prompt](NextAI::FAIResponse response)
                              {
                                  FDecisionResult result =
                                      ParseStructuredDecision(response.success ? response.text : std::string());
                                  std::lock_guard<std::mutex> lock(mutex_);
                                  if (generation != generation_)
                                  {
                                      return;
                                  }
                                  completed_.push_back({
                                      .empIndex = chosenIndex,
                                      .result = std::move(result),
                                      .elapsedMs = response.elapsedMs,
                                      .serviceSuccess = response.success,
                                      .errorMessage = response.message,
                                      .prompt = prompt,
                                      .response = response.success ? std::move(response.text)
                                                                   : std::move(response.message),
                                  });
                              });
    }

    void DecisionScheduler::ApplyResult(std::vector<FEmployee>& employees, size_t empIndex,
                                        const FDecisionResult& result, double gameMinutes, const OfficeMap& office)
    {
        FEmployee& emp = employees[empIndex];
        emp.decisionPending = false;
        const bool hadIncoming = !emp.pendingFrom.empty();
        // 收到的搭话已在本次决策中回应过，清掉。
        emp.pendingFrom.clear();
        emp.pendingText.clear();

        if (!result.valid)
        {
            emp.bubbleText.clear();
            PushShortMemory(emp, gameMinutes, "LLM 决策无效，回到脚本日程");
            SPDLOG_INFO("StudioSim/LLM {} decision unparseable -> schedule fallback", emp.displayName);
            return;
        }

        emp.mood = result.mood;
        if (ShouldShowDialogue(emp, result, gameMinutes, hadIncoming))
        {
            emp.bubbleText = result.dialogue;
            emp.bubbleClearAt = gameMinutes + kDecisionBubbleDurationMinutes;
        }
        else if (emp.bubbleClearAt <= gameMinutes)
        {
            emp.bubbleText.clear();
            emp.bubbleClearAt = 0.0;
        }

        // 校验目标点位（必须存在且可用）；非法则不覆盖日程（仍保留对话/情绪）。
        std::string targetName = result.targetPoi;
        if (result.action == "MEETING")
        {
            const auto seats = office.PointsOfCategory("meet");
            if (!seats.empty() && office.FindByName(targetName) == nullptr)
            {
                targetName = seats[empIndex % seats.size()]->name;
            }
        }

        const FPointOfInterest* targetPoi = office.FindByName(targetName);
        if (targetPoi != nullptr && targetPoi->enabled)
        {
            emp.overrideTargetPoi = targetName;
            emp.overrideUntilMinutes = gameMinutes + result.durationMinutes;
        }
        else
        {
            emp.overrideTargetPoi.clear();
        }

        std::string memory = fmt::format("{} -> {}", result.action.empty() ? "IDLE" : result.action,
                                         targetName.empty() ? "脚本日程" : targetName);
        if (!result.dialogue.empty())
        {
            memory += fmt::format("，说「{}」", result.dialogue);
        }
        const std::string contribution = DominantContribution(emp.myContribution);
        if (!contribution.empty())
        {
            memory += "，" + contribution;
        }
        PushShortMemory(emp, gameMinutes, memory);

        // M7：TALK 时给目标同事投递消息，让其插队尽快回应（形成来回 + 情绪扩散）。
        if (result.action == "TALK" && !result.targetEmployee.empty() && !result.dialogue.empty())
        {
            for (auto& other : employees)
            {
                if (other.id != emp.id && other.displayName == result.targetEmployee)
                {
                    other.pendingFrom = emp.displayName;
                    other.pendingText = result.dialogue;
                    other.nextDecisionAt = gameMinutes;
                    PushShortMemory(other, gameMinutes, fmt::format("{} 对我说「{}」", emp.displayName, result.dialogue));
                    SPDLOG_INFO("StudioSim/Talk {} -> {}: '{}'", emp.displayName, other.displayName, result.dialogue);
                    break;
                }
            }
        }

        SPDLOG_INFO("StudioSim/LLM {} -> action={} poi='{}' to='{}' mood={} say='{}'", emp.displayName, result.action,
                    result.targetPoi, result.targetEmployee, MoodName(result.mood), result.dialogue);
    }

    void DecisionScheduler::ApplyFallback(std::vector<FEmployee>& employees, size_t empIndex,
                                          double gameMinutes, const OfficeMap& office,
                                          std::string reason, double elapsedMs, std::string prompt,
                                          std::string response)
    {
        if (empIndex >= employees.size())
        {
            return;
        }

        ++fallbacksUsed_;
        FEmployee& employee = employees[empIndex];
        FDecisionResult fallback;
        fallback.valid = true;
        fallback.action = "WORK";
        fallback.targetPoi = employee.homeDeskPoi;
        fallback.mood = employee.eventReactionPending ? EMood::Focused : EMood::Calm;
        fallback.durationMinutes = 30;
        if (!employee.pendingFrom.empty())
        {
            fallback.action = "TALK";
            fallback.targetEmployee = employee.pendingFrom;
            fallback.dialogue = "收到，我先处理";
        }
        const FPointOfInterest* desk = office.FindByName(fallback.targetPoi);
        if (desk == nullptr || !desk->enabled)
        {
            fallback.action = "REST";
            fallback.targetPoi = "pantry_01";
        }

        ApplyResult(employees, empIndex, fallback, gameMinutes, office);
        PushLog(employee, fmt::format("规则 fallback · {}", reason), std::move(prompt),
                std::move(response), elapsedMs, false);
    }

    void DecisionScheduler::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        completed_.clear();
        inFlight_ = false;
        inFlightEmployeeIndex_ = 0;
        inFlightStartedAt_ = {};
        inFlightPrompt_.clear();
        decisionsMade_ = 0;
        fallbacksUsed_ = 0;
        nextLogId_ = 1;
        scanCursor_ = 0;
        log_.clear();
    }
}
