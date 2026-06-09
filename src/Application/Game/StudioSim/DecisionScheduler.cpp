#include "DecisionScheduler.h"

#include "EmployeeSystem.h"
#include "OfficeMap.h"

#include <algorithm>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"

namespace StudioSim
{
    namespace
    {
        // 每个员工两次 LLM 决策之间的最小游戏时间间隔（分钟）。
        constexpr double kDecisionIntervalMinutes = 60.0;
        constexpr size_t kShortMemoryLimit = 4;
        constexpr double kBubbleDurationMinutes = 25.0;

        const char* ProjectStageLabelZh(EProjectStage stage)
        {
            switch (stage)
            {
            case EProjectStage::Planning:   return "企划";
            case EProjectStage::Production: return "生产";
            case EProjectStage::Polish:     return "打磨";
            case EProjectStage::Done:       return "完成";
            default:                        return "?";
            }
        }

        const char* GameGenreLabelZh(EGameGenre genre)
        {
            switch (genre)
            {
            case EGameGenre::RPG:        return "RPG";
            case EGameGenre::Action:     return "动作";
            case EGameGenre::Simulation: return "模拟经营";
            case EGameGenre::Puzzle:     return "解谜";
            case EGameGenre::Shooter:    return "射击";
            case EGameGenre::Adventure:  return "冒险";
            default:                     return "未知";
            }
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
            default:                  return "未知";
            }
        }

        struct FMeterSnapshot
        {
            const char* key = "";
            const char* label = "";
            float value = 0.0f;
            float target = 0.0f;
        };

        float MeterCompletion(const FMeterSnapshot& meter)
        {
            return meter.target > 0.0f ? std::clamp(meter.value / meter.target, 0.0f, 1.0f) : 1.0f;
        }

        FMeterSnapshot WeakestMeter(const FProjectState& project)
        {
            FMeterSnapshot meters[] = {
                {"tech", "技术", project.meters.tech, project.targetMeters.tech},
                {"design", "玩法", project.meters.design, project.targetMeters.design},
                {"art", "美术", project.meters.art, project.targetMeters.art},
                {"polish", "品质", project.meters.polish, project.targetMeters.polish},
            };

            FMeterSnapshot weakest = meters[0];
            for (const auto& meter : meters)
            {
                if (MeterCompletion(meter) < MeterCompletion(weakest))
                {
                    weakest = meter;
                }
            }
            return weakest;
        }

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
                gameProject.name, GameGenreLabelZh(gameProject.genre), GameThemeLabelZh(gameProject.theme),
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
                if (!poi.workable)
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

        FDecisionResult ParseDecision(const std::string& text)
        {
            FDecisionResult result;
            const size_t open = text.find('{');
            const size_t close = text.rfind('}');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return result;
            }
            try
            {
                const nlohmann::json json = nlohmann::json::parse(text.substr(open, close - open + 1));
                result.action = json.value("action", std::string("IDLE"));
                result.targetPoi = json.value("target_poi", std::string());
                result.targetEmployee = json.value("target_employee", std::string());
                result.dialogue = json.value("dialogue", std::string());
                result.mood = MoodFromString(json.value("mood", std::string("calm")));
                result.durationMinutes = std::clamp(json.value("duration_minutes", 30), 10, 60);
                result.valid = true;
            }
            catch (...)
            {
                result.valid = false;
            }
            return result;
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
                    ApplyResult(employees, pending.empIndex, pending.result, gameMinutes, office);
                }
            }
            if (!completed_.empty())
            {
                completed_.clear();
                inFlight_ = false;
            }
        }

        // 2. 空闲则发起下一个决策（在途上限 1，匹配 llama-server parallel:1）。
        if (inFlight_ || ai == nullptr)
        {
            return;
        }
        for (size_t i = 0; i < employees.size(); ++i)
        {
            FEmployee& emp = employees[i];
            if (emp.gatheringId >= 0 || emp.decisionPending || gameMinutes < emp.nextDecisionAt)
            {
                continue;
            }

            const std::string prompt =
                BuildPrompt(emp, goal, gameProject, eventsSummary, employees, gameMinutes, office);
            emp.decisionPending = true;
            emp.nextDecisionAt = gameMinutes + kDecisionIntervalMinutes;
            inFlight_ = true;
            ++decisionsMade_;

            const size_t idx = i;
            const uint64_t generation = generation_;
            ai->GenerateTextAsync(prompt,
                                  [this, idx, generation](NextAI::FAIResponse response)
                                  {
                                      // Worker thread: only parse + enqueue, never touch Scene/employees.
                                      FDecisionResult result =
                                          ParseDecision(response.success ? response.text : std::string());
                                      std::lock_guard<std::mutex> lock(mutex_);
                                      if (generation != generation_)
                                      {
                                          return;
                                      }
                                      completed_.push_back({idx, result});
                                  });
            break;
        }
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
            emp.bubbleClearAt = gameMinutes + kBubbleDurationMinutes;
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
        if (targetPoi != nullptr && targetPoi->workable)
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

    void DecisionScheduler::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        completed_.clear();
        inFlight_ = false;
        decisionsMade_ = 0;
    }
}
