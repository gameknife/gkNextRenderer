#include "GoalSystem.h"

#include "EmployeeSystem.h"

#include <map>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"

namespace StudioSim
{
    namespace
    {
        const char* kGoalsPrompt =
            "你是一家游戏工作室的负责人。团队成员：2名工程师、1名美术、1名设计、1名PM、1名QA。\n"
            "为今天提出3个不同的、具体的团队目标。只输出一个JSON数组，不要任何解释：\n"
            "[{\"title\":\"<简短目标>\",\"description\":\"<一句话说明>\"},{\"title\":\"...\",\"description\":\"...\"},"
            "{\"title\":\"...\",\"description\":\"...\"}]";

        std::vector<FGoalOption> FallbackOptions()
        {
            return {
                {"赶在竞品前发布可玩demo", "集中火力把试玩版本做出来"},
                {"修复线上严重崩溃", "稳定性优先，全员救火"},
                {"新玩法头脑风暴", "放松一天，发散创意"},
            };
        }

        std::vector<FGoalOption> ParseGoalOptions(const std::string& text)
        {
            std::vector<FGoalOption> result;
            const size_t open = text.find('[');
            const size_t close = text.rfind(']');
            if (open == std::string::npos || close == std::string::npos || close <= open)
            {
                return result;
            }
            try
            {
                const nlohmann::json arr = nlohmann::json::parse(text.substr(open, close - open + 1));
                for (const auto& item : arr)
                {
                    FGoalOption option;
                    option.title = item.value("title", std::string());
                    option.description = item.value("description", std::string());
                    if (!option.title.empty())
                    {
                        result.push_back(std::move(option));
                    }
                }
            }
            catch (...)
            {
            }
            return result;
        }
    }

    void GoalSystem::BeginDay(NextAI::FAIService* ai)
    {
        Reset();
        state_ = EState::RequestingGoals;

        if (ai == nullptr)
        {
            // 无 LLM：直接用预置目标库（确定性 fallback）。
            std::lock_guard<std::mutex> lock(mutex_);
            inbox_.push_back({EMsgKind::Goals, std::string()});
            return;
        }

        const uint64_t generation = generation_;
        ai->GenerateTextAsync(kGoalsPrompt,
                              [this, generation](NextAI::FAIResponse response)
                              {
                                  std::lock_guard<std::mutex> lock(mutex_);
                                  if (generation != generation_)
                                  {
                                      return;
                                  }
                                  inbox_.push_back({EMsgKind::Goals, response.success ? response.text : std::string()});
                              });
    }

    void GoalSystem::StartDecompose(NextAI::FAIService* ai)
    {
        state_ = EState::Decomposing;

        if (ai == nullptr)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            inbox_.push_back({EMsgKind::Decompose, std::string()});
            return;
        }

        const std::string prompt = fmt::format(
            "今天团队目标是：{}（{}）。\n"
            "团队职位有：engineer, artist, designer, pm, qa。\n"
            "给每个职位分配今天的一句话重点任务。只输出一个JSON对象，不要任何解释：\n"
            "{{\"engineer\":\"...\",\"artist\":\"...\",\"designer\":\"...\",\"pm\":\"...\",\"qa\":\"...\"}}",
            goal_.title, goal_.description);

        const uint64_t generation = generation_;
        ai->GenerateTextAsync(prompt,
                              [this, generation](NextAI::FAIResponse response)
                              {
                                  std::lock_guard<std::mutex> lock(mutex_);
                                  if (generation != generation_)
                                  {
                                      return;
                                  }
                                  inbox_.push_back(
                                      {EMsgKind::Decompose, response.success ? response.text : std::string()});
                              });
    }

    void GoalSystem::ApplyDecompose(const std::string& payload, std::vector<FEmployee>& employees)
    {
        std::map<std::string, std::string> tasks; // role -> task
        const size_t open = payload.find('{');
        const size_t close = payload.rfind('}');
        if (open != std::string::npos && close != std::string::npos && close > open)
        {
            try
            {
                const nlohmann::json json = nlohmann::json::parse(payload.substr(open, close - open + 1));
                for (auto it = json.begin(); it != json.end(); ++it)
                {
                    if (it->is_string())
                    {
                        tasks[it.key()] = it->get<std::string>();
                    }
                }
            }
            catch (...)
            {
            }
        }

        for (auto& emp : employees)
        {
            const auto found = tasks.find(RoleName(emp.role));
            emp.todayTask = (found != tasks.end() && !found->second.empty())
                                ? found->second
                                : fmt::format("围绕「{}」推进本职工作", goal_.title); // 静态 fallback
        }
    }

    void GoalSystem::Tick(NextAI::FAIService* ai, std::vector<FEmployee>& employees)
    {
        std::vector<FMsg> messages;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            messages.swap(inbox_);
        }

        for (const auto& msg : messages)
        {
            if (msg.kind == EMsgKind::Goals)
            {
                options_ = ParseGoalOptions(msg.payload);
                if (options_.empty())
                {
                    options_ = FallbackOptions();
                }
                // 等玩家选择/自定义；不要自动启动当天任务。
                state_ = EState::AwaitingChoice;
                SPDLOG_INFO("StudioSim/Goal {} options ready, awaiting choice", options_.size());
            }
            else if (msg.kind == EMsgKind::Decompose)
            {
                ApplyDecompose(msg.payload, employees);
                state_ = EState::Active;
                SPDLOG_INFO("StudioSim/Goal active: '{}'", goal_.title);
                for (const auto& emp : employees)
                {
                    SPDLOG_INFO("  {} todayTask: {}", emp.displayName, emp.todayTask);
                }
            }
            else if (msg.kind == EMsgKind::Summary)
            {
                int score = -1;
                std::string text;
                const size_t open = msg.payload.find('{');
                const size_t close = msg.payload.rfind('}');
                if (open != std::string::npos && close != std::string::npos && close > open)
                {
                    try
                    {
                        const nlohmann::json json = nlohmann::json::parse(msg.payload.substr(open, close - open + 1));
                        text = json.value("summary", std::string());
                        score = json.value("score", -1);
                    }
                    catch (...)
                    {
                    }
                }
                summary_ = text.empty() ? fmt::format("今日目标「{}」结束。", goal_.title)
                           : score >= 0 ? fmt::format("{}（达成度 {}/100）", text, score)
                                        : text;
                SPDLOG_INFO("StudioSim/Goal review: {}", summary_);
            }
        }
    }

    void GoalSystem::ChooseGoal(int index, NextAI::FAIService* ai, std::vector<FEmployee>& employees)
    {
        if (index < 0 || index >= static_cast<int>(options_.size()))
        {
            return;
        }
        (void)employees;
        goal_ = {options_[static_cast<size_t>(index)].title, options_[static_cast<size_t>(index)].description, "player",
                 true};
        SPDLOG_INFO("StudioSim/Goal player chose: '{}'", goal_.title);
        StartDecompose(ai);
    }

    void GoalSystem::ChooseCustom(const std::string& title, NextAI::FAIService* ai, std::vector<FEmployee>& employees)
    {
        if (title.empty())
        {
            return;
        }
        (void)employees;
        goal_ = {title, "玩家自定义目标", "player", true};
        SPDLOG_INFO("StudioSim/Goal player custom: '{}'", goal_.title);
        StartDecompose(ai);
    }

    void GoalSystem::Summarize(NextAI::FAIService* ai, const std::vector<FEmployee>& employees)
    {
        if (summarizeRequested_)
        {
            return;
        }
        summarizeRequested_ = true;

        std::string status;
        for (const auto& emp : employees)
        {
            status += fmt::format("{}（{}，{}）；", emp.displayName, RoleName(emp.role), MoodName(emp.mood));
        }

        if (ai == nullptr)
        {
            summary_ = fmt::format("今日目标「{}」结束（无 LLM 结算）。", goal_.title);
            SPDLOG_INFO("StudioSim/Goal review: {}", summary_);
            return;
        }

        const std::string prompt = fmt::format(
            "今天团队的目标是：{}。\n员工们一天结束时的状态是：{}\n"
            "用一句话总结今天这个目标的达成情况，并给一个0到100的分数。只输出一个JSON对象，不要任何解释：\n"
            "{{\"summary\":\"<一句话总结>\",\"score\":<0到100的整数>}}",
            goal_.title, status);

        const uint64_t generation = generation_;
        ai->GenerateTextAsync(prompt,
                              [this, generation](NextAI::FAIResponse response)
                              {
                                  std::lock_guard<std::mutex> lock(mutex_);
                                  if (generation != generation_)
                                  {
                                      return;
                                  }
                                  inbox_.push_back({EMsgKind::Summary, response.success ? response.text : std::string()});
                              });
    }

    void GoalSystem::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        inbox_.clear();
        state_ = EState::Idle;
        options_.clear();
        goal_ = FDailyGoal{};
        summary_.clear();
        summarizeRequested_ = false;
    }
}
