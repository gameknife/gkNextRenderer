#include "GoalSystem.h"

#include "EmployeeSystem.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
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

        bool ContainsAny(const std::string& text, std::initializer_list<const char*> keywords)
        {
            for (const char* keyword : keywords)
            {
                if (text.find(keyword) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        void ApplyGoalTemplate(FDailyGoal& goal)
        {
            const std::string text = goal.title + " " + goal.description;
            if (ContainsAny(text, {"崩溃", "救火", "修复", "稳定", "宕机", "bug", "Bug", "crash", "fix"}))
            {
                goal.category = "fix_crash";
                goal.targetMeters = {60.0f, 20.0f, 10.0f, 120.0f};
            }
            else if (ContainsAny(text, {"头脑风暴", "脑暴", "创意", "玩法", "brainstorm", "idea"}))
            {
                goal.category = "brainstorm";
                goal.targetMeters = {20.0f, 110.0f, 50.0f, 20.0f};
            }
            else
            {
                goal.category = "ship_demo";
                goal.targetMeters = {100.0f, 70.0f, 80.0f, 90.0f};
            }
        }

        float MeterRatio(float value, float target)
        {
            return target > 0.0f ? std::clamp(value / target, 0.0f, 1.0f) : 1.0f;
        }

        int ComputeLocalScore(const FProjectState& project)
        {
            int score = static_cast<int>(std::round(project.overallProgress * 100.0f));
            if (project.shipped)
            {
                score += 5;
            }
            score -= std::min(project.bugCount * 3, 25);
            return std::clamp(score, 0, 100);
        }

        int ComputeLocalScore(const FGameProject& gameProject)
        {
            if (gameProject.launched)
            {
                return std::clamp(static_cast<int>(std::round(gameProject.quality * 100.0f)), 0, 100);
            }
            return ComputeLocalScore(gameProject.production);
        }

        std::string BuildProjectFacts(const FProjectState& project)
        {
            return fmt::format("总进度 {:.0f}%，技术 {:.0f}/{:.0f}，玩法 {:.0f}/{:.0f}，美术 {:.0f}/{:.0f}，"
                               "品质 {:.0f}/{:.0f}，剩余 Bug {}，已修 Bug {}，{}。",
                               project.overallProgress * 100.0f, project.meters.tech, project.targetMeters.tech,
                               project.meters.design, project.targetMeters.design, project.meters.art,
                               project.targetMeters.art, project.meters.polish, project.targetMeters.polish,
                               project.bugCount, project.bugsFixed, project.shipped ? "已交付" : "未交付");
        }

        std::string BuildReviewerScoresText(const FGameProject& gameProject)
        {
            if (gameProject.reviewerScores.empty())
            {
                return fmt::format("总分 {}", gameProject.reviewScore);
            }

            std::string text;
            for (size_t i = 0; i < gameProject.reviewerScores.size(); ++i)
            {
                if (i > 0)
                {
                    text += "/";
                }
                text += fmt::format("{}", gameProject.reviewerScores[i]);
            }
            return text;
        }

        std::string BuildAchievedHighlightsText(const FGameProject& gameProject)
        {
            std::string text;
            for (const auto& highlight : gameProject.highlights)
            {
                if (!highlight.achieved)
                {
                    continue;
                }
                if (!text.empty())
                {
                    text += "、";
                }
                text += highlight.text;
            }
            return text.empty() ? std::string("暂无") : text;
        }

        std::string BuildLaunchFacts(const FGameProject& gameProject)
        {
            return fmt::format("《{}》上线：质量 {:.0f}/100，媒体评分 {}（{}），销量 {}，营收 {}，成本 {}，"
                               "利润 {}，做实卖点：{}。",
                               gameProject.name, gameProject.quality * 100.0f, gameProject.reviewScore,
                               BuildReviewerScoresText(gameProject), gameProject.unitsSold, gameProject.revenue,
                               gameProject.cost, gameProject.profit, BuildAchievedHighlightsText(gameProject));
        }

        std::string BuildCarryOverContext(const FDailyGoal& goal, const FGameProject& gameProject, int score)
        {
            const FProjectState& project = gameProject.production;
            std::string context;
            if (gameProject.launched)
            {
                context = fmt::format("昨日目标「{}」完成并上线，{}",
                                      goal.set ? goal.title : std::string("未定目标"), BuildLaunchFacts(gameProject));
            }
            else
            {
                context = fmt::format("昨日目标「{}」结算 {} 分，{}",
                                      goal.set ? goal.title : std::string("未定目标"), score,
                                      BuildProjectFacts(project));
            }
            if (project.bugCount > 0 || project.overallProgress < 1.0f)
            {
                context += " 次日晨会请考虑残留风险。";
            }
            return context;
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
        const std::string carryOver = nextBriefingContext_;
        Reset();
        state_ = EState::RequestingGoals;
        nextBriefingContext_.clear();

        std::string prompt = kGoalsPrompt;
        if (!carryOver.empty())
        {
            prompt += "\n昨日残留上下文：";
            prompt += carryOver;
            prompt += "\n今天的3个目标可以回应这些残留，但仍需给玩家不同选择。";
        }

        if (ai == nullptr)
        {
            // 无 LLM：直接用预置目标库（确定性 fallback）。
            std::lock_guard<std::mutex> lock(mutex_);
            inbox_.push_back({EMsgKind::Goals, std::string()});
            return;
        }

        const uint64_t generation = generation_;
        ai->GenerateTextAsync(prompt,
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
                std::string text;
                const size_t open = msg.payload.find('{');
                const size_t close = msg.payload.rfind('}');
                if (open != std::string::npos && close != std::string::npos && close > open)
                {
                    try
                    {
                        const nlohmann::json json = nlohmann::json::parse(msg.payload.substr(open, close - open + 1));
                        text = json.value("summary", std::string());
                    }
                    catch (...)
                    {
                    }
                }
                summary_ = text.empty() ? fmt::format("今日目标「{}」结束（本地达成度 {}/100）。", goal_.title, localScore_)
                                        : fmt::format("{}（本地达成度 {}/100）", text, localScore_);
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
        goal_ = FDailyGoal{};
        goal_.title = options_[static_cast<size_t>(index)].title;
        goal_.description = options_[static_cast<size_t>(index)].description;
        goal_.source = "player";
        goal_.set = true;
        ApplyGoalTemplate(goal_);
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
        goal_ = FDailyGoal{};
        goal_.title = title;
        goal_.description = "玩家自定义目标";
        goal_.source = "player";
        goal_.set = true;
        ApplyGoalTemplate(goal_);
        SPDLOG_INFO("StudioSim/Goal player custom: '{}'", goal_.title);
        StartDecompose(ai);
    }

    void GoalSystem::SetActiveGoal(const FDailyGoal& goal, std::vector<FEmployee>& employees)
    {
        Reset();
        goal_ = goal;
        goal_.set = true;
        ApplyGoalTemplate(goal_);
        ApplyDecompose(std::string(), employees);
        state_ = EState::Active;
        SPDLOG_INFO("StudioSim/Goal focus active: '{}'", goal_.title);
        for (const auto& emp : employees)
        {
            SPDLOG_INFO("  {} todayTask: {}", emp.displayName, emp.todayTask);
        }
    }

    void GoalSystem::Summarize(NextAI::FAIService* ai, const std::vector<FEmployee>& employees,
                               const FGameProject& gameProject)
    {
        if (summarizeRequested_)
        {
            return;
        }
        summarizeRequested_ = true;
        const FProjectState& project = gameProject.production;
        localScore_ = ComputeLocalScore(gameProject);
        nextBriefingContext_ = BuildCarryOverContext(goal_, gameProject, localScore_);

        std::string status;
        for (const auto& emp : employees)
        {
            status += fmt::format("{}（{}，{}）；", emp.displayName, RoleName(emp.role), MoodName(emp.mood));
        }

        if (ai == nullptr)
        {
            if (gameProject.launched)
            {
                summary_ = fmt::format("今日目标「{}」结束，{}", goal_.title, BuildLaunchFacts(gameProject));
            }
            else
            {
                summary_ = fmt::format("今日目标「{}」结束（本地达成度 {}/100）：{}", goal_.title, localScore_,
                                      BuildProjectFacts(project));
            }
            SPDLOG_INFO("StudioSim/Goal review: {}", summary_);
            return;
        }

        const std::string facts = gameProject.launched ? BuildLaunchFacts(gameProject) : BuildProjectFacts(project);
        const std::string prompt = fmt::format(
            "今天团队的目标是：{}。\n真实结算数据：{} 本地分数：{}/100。\n员工状态：{}\n"
            "只根据这些真实数据写一句有人味的点评，不要编造分数。只输出一个JSON对象，不要任何解释：\n"
            "{{\"summary\":\"<一句话点评>\"}}",
            goal_.title, facts, localScore_, status);

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
        const std::string carryOver = nextBriefingContext_;
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        inbox_.clear();
        state_ = EState::Idle;
        options_.clear();
        goal_ = FDailyGoal{};
        summary_.clear();
        summarizeRequested_ = false;
        localScore_ = -1;
        nextBriefingContext_ = carryOver;
    }
}
