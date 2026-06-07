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

        std::string BuildPrompt(const FEmployee& emp, const FDailyGoal& goal, const std::string& eventsSummary,
                                const std::vector<FEmployee>& allEmployees, double gameMinutes, const OfficeMap& office)
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

            return fmt::format(
                "你是一家游戏工作室的员工{}（职位：{}）。性格：{}。现在是{:02d}:{:02d}。\n"
                "{}{}{}"
                "你的同事有：{}\n"
                "办公室里你可以去的点位有：{}\n"
                "结合你的职位、性格、今日目标和当前状况，决定你接下来做一件事。需要群体决策/重排计划时用 action=MEETING "
                "并选择会议室 meet_seat_*；想找某位同事说话就用 action=TALK 并在 target_employee 填同事名。"
                "只输出一个JSON对象，不要任何解释或markdown：\n"
                "{{\"action\":\"WORK|REST|TALK|MEETING|IDLE\",\"target_poi\":\"<上面列表里的一个点位名>\","
                "\"target_employee\":\"<TALK时填一个同事名，否则空字符串>\",\"dialogue\":\"<一句不超过15字的话>\","
                "\"mood\":\"calm|focused|stressed|excited|bored|panicked\",\"duration_minutes\":<10到60的整数>}}",
                emp.displayName, RoleName(emp.role), emp.personality, hh, mm, goalLine, eventLine, incomingLine, mates,
                poiList);
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

    void DecisionScheduler::Tick(double gameMinutes, const FDailyGoal& goal, const std::string& eventsSummary,
                                 NextAI::FAIService* ai, std::vector<FEmployee>& employees, const OfficeMap& office)
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
            if (emp.decisionPending || gameMinutes < emp.nextDecisionAt)
            {
                continue;
            }

            const std::string prompt = BuildPrompt(emp, goal, eventsSummary, employees, gameMinutes, office);
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
        // 收到的搭话已在本次决策中回应过，清掉。
        emp.pendingFrom.clear();
        emp.pendingText.clear();

        if (!result.valid)
        {
            emp.bubbleText.clear();
            SPDLOG_INFO("StudioSim/LLM {} decision unparseable -> schedule fallback", emp.displayName);
            return;
        }

        emp.mood = result.mood;
        emp.bubbleText = result.dialogue;

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
