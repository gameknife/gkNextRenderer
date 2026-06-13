#include "StudioSimGameInstance.hpp"

#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Modules/NextAI/AIService.hpp"

#include "Modules/ScadLoader/ScadModule.hpp"
#include "Modules/NextAI/NextAIModule.hpp"

namespace
{
    // Fixed isometric-ish overhead camera shared by the render override and the
    // debug overlay so projected anchors line up with the rendered scene.
    constexpr float kOfficeFov = 50.0f;
    constexpr glm::vec3 kOfficeOverviewTarget{0.0f, 0.0f, 0.0f};
    constexpr glm::vec3 kOfficeOverviewOffset{0.0f, 18.0f, 18.0f};
    constexpr glm::vec3 kOfficeFollowOffset{0.0f, 7.0f, 7.0f};
    constexpr float kCameraTransitionSharpness = 9.0f;
    constexpr float kEmployeePickRadiusPixels = 18.0f;
    constexpr int64_t kTeamDailyWage = 1800;
    constexpr int64_t kUnitPrice = 30;
    // 一支 6 人团队在一个工作日（09:00–18:00）内大致能产出的四仪表点数总量，
    // 已扣除午休/走动/开会等非产出时间。项目目标 = 此值 × 工期天数，
    // 使项目大致需要约 plannedDays 天完成，而不是开工当天就把仪表打满（修复"晨会后猛加"）。
    constexpr float kTeamDailyMeterOutput = 760.0f;

    const char* GameGenreLabelZh(StudioSim::EGameGenre genre)
    {
        switch (genre)
        {
        case StudioSim::EGameGenre::RPG:        return "RPG";
        case StudioSim::EGameGenre::Action:     return "动作";
        case StudioSim::EGameGenre::Simulation: return "模拟";
        case StudioSim::EGameGenre::Puzzle:     return "解谜";
        case StudioSim::EGameGenre::Shooter:    return "射击";
        case StudioSim::EGameGenre::Adventure:  return "冒险";
        default:                                return "未知";
        }
    }

    const char* GameThemeLabelZh(StudioSim::EGameTheme theme)
    {
        switch (theme)
        {
        case StudioSim::EGameTheme::Fantasy: return "奇幻";
        case StudioSim::EGameTheme::SciFi:   return "科幻";
        case StudioSim::EGameTheme::Sports:  return "运动";
        case StudioSim::EGameTheme::Romance: return "恋爱";
        case StudioSim::EGameTheme::Horror:  return "恐怖";
        case StudioSim::EGameTheme::Daily:   return "日常";
        default:                             return "未知";
        }
    }

    StudioSim::FProjectMeters GenreWeights(StudioSim::EGameGenre genre)
    {
        switch (genre)
        {
        case StudioSim::EGameGenre::RPG:        return {0.25f, 0.35f, 0.30f, 0.10f};
        case StudioSim::EGameGenre::Action:     return {0.40f, 0.20f, 0.25f, 0.15f};
        case StudioSim::EGameGenre::Simulation: return {0.30f, 0.40f, 0.15f, 0.15f};
        case StudioSim::EGameGenre::Puzzle:     return {0.20f, 0.45f, 0.15f, 0.20f};
        case StudioSim::EGameGenre::Shooter:    return {0.45f, 0.15f, 0.25f, 0.15f};
        case StudioSim::EGameGenre::Adventure:  return {0.20f, 0.30f, 0.40f, 0.10f};
        default:                                return {0.25f, 0.30f, 0.25f, 0.20f};
        }
    }

    float ComboFit(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme)
    {
        if (genre == StudioSim::EGameGenre::RPG &&
            (theme == StudioSim::EGameTheme::Fantasy || theme == StudioSim::EGameTheme::SciFi))
        {
            return theme == StudioSim::EGameTheme::Fantasy ? 1.15f : 1.10f;
        }
        if (genre == StudioSim::EGameGenre::Action &&
            (theme == StudioSim::EGameTheme::SciFi || theme == StudioSim::EGameTheme::Sports))
        {
            return theme == StudioSim::EGameTheme::SciFi ? 1.15f : 1.10f;
        }
        if (genre == StudioSim::EGameGenre::Puzzle && theme == StudioSim::EGameTheme::Daily)
        {
            return 1.15f;
        }
        if ((genre == StudioSim::EGameGenre::Shooter && theme == StudioSim::EGameTheme::Romance) ||
            (genre == StudioSim::EGameGenre::Simulation && theme == StudioSim::EGameTheme::Horror))
        {
            return 0.85f;
        }
        return 1.0f;
    }

    int ProjectSizeDays(StudioSim::EProjectSizeTier tier)
    {
        switch (tier)
        {
        case StudioSim::EProjectSizeTier::Small:    return 5;
        case StudioSim::EProjectSizeTier::Standard: return 8;
        case StudioSim::EProjectSizeTier::Big:      return 12;
        default:                                    return 8;
        }
    }

    std::string DefaultProjectName(StudioSim::EGameTheme theme, int projectIndex)
    {
        static const char* kFantasy[] = {"秘境传说", "龙之纪元", "魔导之书"};
        static const char* kSciFi[] = {"星海奇谭", "曲速边境", "量子黎明"};
        static const char* kSports[] = {"极速联赛", "冠军时刻", "热血球场"};
        static const char* kRomance[] = {"心动季节", "恋爱方程式", "雨后告白"};
        static const char* kHorror[] = {"午夜回廊", "噩梦档案", "雾镇怪谈"};
        static const char* kDaily[] = {"街角日常", "便利店物语", "小城假日"};
        const char** names = kFantasy;
        int count = 3;
        switch (theme)
        {
        case StudioSim::EGameTheme::Fantasy: names = kFantasy; break;
        case StudioSim::EGameTheme::SciFi:   names = kSciFi; break;
        case StudioSim::EGameTheme::Sports:  names = kSports; break;
        case StudioSim::EGameTheme::Romance: names = kRomance; break;
        case StudioSim::EGameTheme::Horror:  names = kHorror; break;
        case StudioSim::EGameTheme::Daily:   names = kDaily; break;
        default:                             names = kFantasy; break;
        }
        return names[projectIndex % count];
    }

    std::vector<StudioSim::FHighlight> DefaultHighlights(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme)
    {
        std::vector<StudioSim::FHighlight> highlights;
        if (genre == StudioSim::EGameGenre::RPG) highlights.push_back({"角色养成", "design", false});
        else if (genre == StudioSim::EGameGenre::Action) highlights.push_back({"丝滑手感", "tech", false});
        else if (genre == StudioSim::EGameGenre::Simulation) highlights.push_back({"深度系统", "design", false});
        else if (genre == StudioSim::EGameGenre::Puzzle) highlights.push_back({"巧妙关卡", "design", false});
        else if (genre == StudioSim::EGameGenre::Shooter) highlights.push_back({"爽快射击", "tech", false});
        else highlights.push_back({"探索叙事", "art", false});

        if (theme == StudioSim::EGameTheme::Fantasy) highlights.push_back({"奇幻世界观", "art", false});
        else if (theme == StudioSim::EGameTheme::SciFi) highlights.push_back({"炫酷科技感", "art", false});
        else if (theme == StudioSim::EGameTheme::Sports) highlights.push_back({"竞技张力", "design", false});
        else if (theme == StudioSim::EGameTheme::Romance) highlights.push_back({"细腻情感", "design", false});
        else if (theme == StudioSim::EGameTheme::Horror) highlights.push_back({"压迫氛围", "art", false});
        else highlights.push_back({"生活质感", "polish", false});

        highlights.push_back({"完成度打磨", "polish", false});
        return highlights;
    }

    StudioSim::FGameProject BuildProjectFromPitch(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                                                 StudioSim::EProjectSizeTier sizeTier, int projectIndex)
    {
        StudioSim::FGameProject project;
        project.name = DefaultProjectName(theme, projectIndex);
        project.genre = genre;
        project.theme = theme;
        project.highlights = DefaultHighlights(genre, theme);
        project.comboFit = ComboFit(genre, theme);
        project.plannedDays = ProjectSizeDays(sizeTier);
        project.elapsedDays = 0;
        project.budget = kTeamDailyWage * project.plannedDays;
        project.production.stage = StudioSim::EProjectStage::Planning;

        const StudioSim::FProjectMeters weights = GenreWeights(genre);
        // 目标随工期天数放大：一天的团队产出 × 工期 ≈ 项目总目标，确保跨多天连续推进而非当天打满。
        const float total = kTeamDailyMeterOutput * static_cast<float>(std::max(1, project.plannedDays));
        project.production.targetMeters = {weights.tech * total, weights.design * total, weights.art * total,
                                           weights.polish * total};
        return project;
    }

    float ProjectMeterCompletion(float value, float target)
    {
        return target > 0.0f ? std::clamp(value / target, 0.0f, 1.0f) : 1.0f;
    }

    float ProjectMeterValue(const StudioSim::FProjectMeters& meters, const std::string& meter)
    {
        if (meter == "tech") return meters.tech;
        if (meter == "design") return meters.design;
        if (meter == "art") return meters.art;
        if (meter == "polish") return meters.polish;
        return 0.0f;
    }

    const char* ProjectMeterLabelZh(const std::string& meter)
    {
        if (meter == "tech") return "技术";
        if (meter == "design") return "玩法";
        if (meter == "art") return "美术";
        if (meter == "polish") return "品质";
        return "短板";
    }

    std::string WeakestProjectMeter(const StudioSim::FProjectState& project)
    {
        struct FMeterCandidate
        {
            std::string key;
            float value = 0.0f;
            float target = 0.0f;
        };
        FMeterCandidate candidates[] = {
            {"tech", project.meters.tech, project.targetMeters.tech},
            {"design", project.meters.design, project.targetMeters.design},
            {"art", project.meters.art, project.targetMeters.art},
            {"polish", project.meters.polish, project.targetMeters.polish},
        };
        FMeterCandidate weakest = candidates[0];
        for (const auto& candidate : candidates)
        {
            if (ProjectMeterCompletion(candidate.value, candidate.target) <
                ProjectMeterCompletion(weakest.value, weakest.target))
            {
                weakest = candidate;
            }
        }
        return weakest.key;
    }

    StudioSim::FDailyGoal BuildProjectFocusGoal(const StudioSim::FGameProject& project)
    {
        StudioSim::FDailyGoal goal;
        const int plannedDays = std::max(1, project.plannedDays);
        const int projectDay = std::clamp(project.elapsedDays + 1, 1, plannedDays);
        const int daysLeft = std::max(0, plannedDays - project.elapsedDays);
        if (project.production.bugCount > 0)
        {
            goal.title = fmt::format("修复《{}》剩余 {} 个 Bug", project.name, project.production.bugCount);
            goal.description = fmt::format("第 {}/{} 天，距上线剩余 {} 天，优先把稳定性拉回来", projectDay,
                                           plannedDays, daysLeft);
            goal.category = "fix_crash";
        }
        else
        {
            const std::string weakest = WeakestProjectMeter(project.production);
            goal.title = fmt::format("攻坚《{}》的{}", project.name, ProjectMeterLabelZh(weakest));
            goal.description = fmt::format("第 {}/{} 天，距上线剩余 {} 天，集中补齐当前最短板", projectDay,
                                           plannedDays, daysLeft);
            goal.category = "project_focus";
        }
        goal.source = "project";
        goal.set = true;
        return goal;
    }

    int AchievedHighlightCount(const StudioSim::FGameProject& project)
    {
        int count = 0;
        for (const auto& highlight : project.highlights)
        {
            if (highlight.achieved)
            {
                ++count;
            }
        }
        return count;
    }

    std::string PrimaryHighlightText(const StudioSim::FGameProject& project)
    {
        for (const auto& highlight : project.highlights)
        {
            if (highlight.achieved)
            {
                return highlight.text;
            }
        }
        if (!project.highlights.empty())
        {
            return project.highlights.front().text;
        }
        return "核心体验";
    }

    float ComputeLaunchQuality(const StudioSim::FGameProject& project, int actualDays)
    {
        const float base = project.production.overallProgress;
        const float comboBonus = project.comboFit - 1.0f;
        const float highlightBonus = 0.05f * static_cast<float>(AchievedHighlightCount(project));
        const int overdueDays = std::max(0, actualDays - std::max(1, project.plannedDays));
        const float deadlineBonus = overdueDays == 0 ? 0.05f : -0.10f * static_cast<float>(overdueDays);
        return std::clamp(base + comboBonus + highlightBonus + deadlineBonus, 0.0f, 1.0f);
    }

    int64_t GenreDemandBase(StudioSim::EGameGenre genre)
    {
        switch (genre)
        {
        case StudioSim::EGameGenre::RPG:        return 1050;
        case StudioSim::EGameGenre::Action:     return 980;
        case StudioSim::EGameGenre::Simulation: return 860;
        case StudioSim::EGameGenre::Puzzle:     return 680;
        case StudioSim::EGameGenre::Shooter:    return 940;
        case StudioSim::EGameGenre::Adventure:  return 820;
        default:                                return 760;
        }
    }

    float ThemeDemandModifier(StudioSim::EGameTheme theme)
    {
        switch (theme)
        {
        case StudioSim::EGameTheme::Fantasy: return 1.12f;
        case StudioSim::EGameTheme::SciFi:   return 1.08f;
        case StudioSim::EGameTheme::Sports:  return 0.96f;
        case StudioSim::EGameTheme::Romance: return 0.90f;
        case StudioSim::EGameTheme::Horror:  return 0.88f;
        case StudioSim::EGameTheme::Daily:   return 0.82f;
        default:                             return 1.0f;
        }
    }

    std::vector<int> BuildReviewerScores(const StudioSim::FGameProject& project, float quality)
    {
        int seed = 0;
        for (char ch : project.name)
        {
            seed += static_cast<unsigned char>(ch);
        }
        seed += static_cast<int>(project.genre) * 17 + static_cast<int>(project.theme) * 31;

        std::vector<int> scores;
        const int mean = std::clamp(static_cast<int>(std::round(quality * 10.0f)), 1, 10);
        for (int i = 0; i < 4; ++i)
        {
            const int variance = (seed + i * 11) % 3 - 1;
            scores.push_back(std::clamp(mean + variance, 1, 10));
        }
        return scores;
    }

    int ReviewScoreTotal(const std::vector<int>& scores)
    {
        int total = 0;
        for (int score : scores)
        {
            total += score;
        }
        return total;
    }

    float ReviewSalesMultiplier(int reviewScore)
    {
        if (reviewScore >= 32) return 1.35f;
        if (reviewScore >= 28) return 1.15f;
        if (reviewScore >= 20) return 1.0f;
        if (reviewScore >= 14) return 0.75f;
        return 0.55f;
    }

    int64_t EstimateUnitsSold(const StudioSim::FGameProject& project)
    {
        const float demand = static_cast<float>(GenreDemandBase(project.genre)) * ThemeDemandModifier(project.theme);
        const float qualityCurve = 0.15f + project.quality * project.quality * 1.85f;
        const float reviewMultiplier = ReviewSalesMultiplier(project.reviewScore);
        return std::max<int64_t>(0, static_cast<int64_t>(std::llround(demand * qualityCurve * reviewMultiplier)));
    }

    std::vector<std::string> BuildReviewQuotes(const StudioSim::FGameProject& project)
    {
        const std::string highlight = PrimaryHighlightText(project);
        const std::string weakest = WeakestProjectMeter(project.production);
        if (project.reviewScore >= 32)
        {
            return {
                fmt::format("{}做得很扎实", highlight),
                "完成度接近神作",
                fmt::format("{}题材吃得很透", GameThemeLabelZh(project.theme)),
                "团队节奏漂亮"};
        }
        if (project.reviewScore >= 24)
        {
            return {
                fmt::format("{}有记忆点", highlight),
                fmt::format("{}还可再补", ProjectMeterLabelZh(weakest)),
                "整体完成度不错",
                "玩家会买账"};
        }
        return {
            fmt::format("{}想法不错", highlight),
            fmt::format("{}短板明显", ProjectMeterLabelZh(weakest)),
            "Bug影响口碑",
            "需要更多打磨"};
    }

    bool ProjectWorld(const glm::mat4& viewProjection, const ImVec2& vpPos, const ImVec2& vpSize,
                      const glm::vec3& world, ImVec2& outScreen)
    {
        const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
        {
            return false;
        }
        outScreen = ImVec2(vpPos.x + (ndc.x * 0.5f + 0.5f) * vpSize.x, vpPos.y + (-ndc.y * 0.5f + 0.5f) * vpSize.y);
        return true;
    }

    float DistanceToSegment(const glm::vec2& point, const glm::vec2& start, const glm::vec2& end)
    {
        const glm::vec2 segment = end - start;
        const float lengthSquared = glm::dot(segment, segment);
        if (lengthSquared <= 0.001f)
        {
            return glm::distance(point, start);
        }
        const float t = std::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
        return glm::distance(point, start + segment * t);
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

    bool GoalNeedsMeeting(const StudioSim::FDailyGoal& goal)
    {
        const std::string text = goal.title + " " + goal.description;
        return ContainsAny(text, {"头脑风暴", "脑暴", "会议", "讨论", "评审", "规划", "计划", "重排", "协调",
                                  "决策", "复盘", "竞品", "brainstorm", "meeting", "review", "plan"});
    }

    bool EventNeedsMeeting(const std::string& eventId)
    {
        return eventId == "competitor_launch" || eventId == "power_outage" || eventId == "build_server_down";
    }

    std::string MeetingTopicForEvent(const std::string& eventId)
    {
        if (eventId == "competitor_launch") return "竞品发布后是否调整今日目标";
        if (eventId == "power_outage") return "断电期间如何继续推进目标";
        if (eventId == "build_server_down") return "版本服务器宕机后的救火分工";
        return "临时群体决策";
    }

}

// Each game executable provides this factory; DesktopMain.cpp binds it at link time.
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<StudioSimGameInstance>(config, options, engine);
}

StudioSimGameInstance::StudioSimGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                             NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "StudioSim", 1280, 720, false);
}

void StudioSimGameInstance::OnInit()
{
    // NavGrid is built from the scene CPU BVH, so keep CPU mesh data alive.
    GOption->KeepCPUMeshData = true;

    std::string initialScene = "assets/scad/office.scad";
    if (!GOption->SceneName.empty())
    {
        initialScene = GOption->SceneName;
    }

    SPDLOG_INFO("StudioSim: loading scene '{}'", initialScene);
    GetEngine().RequestLoadScene({.filename = initialScene});

    // M4 self-test: switch to the local LLM and fire one async probe to confirm the
    // engine -> llama-server link before wiring up the decision scheduler.
    // Prefer the local llama-server for employee decisions.
    if (auto* ai = NextAI::GetAIService(GetEngine()))
    {
        const bool ok = ai->SwitchProvider(NextAI::EAIProviderType::LocalLlama);
        SPDLOG_INFO("StudioSim: SwitchProvider(LocalLlama) -> {} (provider='{}')", ok, ai->GetProviderName());
    }
}

void StudioSimGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& /*nodes*/,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& /*lights*/,
                                               std::vector<Assets::AnimationTrack>& /*tracks*/)
{
    employeeSystem_.InjectAssets(models, materials);
}

void StudioSimGameInstance::OnTick(double deltaSeconds)
{
    if (!sceneReady_)
    {
        return;
    }

    if (worldState_.phase == StudioSim::EDayPhase::Briefing)
    {
        if (!HasActiveGameProject())
        {
            if (GOption->AgentValidation)
            {
                StartProjectPitch(ui_.SelectedGenre(), ui_.SelectedTheme(), ui_.SelectedSize());
            }
            else
            {
                return;
            }
        }

        // 晨会：等 LLM 给目标 → 玩家选/自定义 → 分解 → 开工（时钟暂停在 09:00）。
        goalSystem_.Tick(GOption->AgentValidation ? nullptr : NextAI::GetAIService(GetEngine()),
                         employeeSystem_.EmployeesMutable());
        if (GOption->AgentValidation && goalSystem_.State() == StudioSim::GoalSystem::EState::AwaitingChoice &&
            !goalSystem_.Options().empty())
        {
            goalSystem_.ChooseGoal(0, nullptr, employeeSystem_.EmployeesMutable());
        }
        if (goalSystem_.IsActive())
        {
            if (!productionSystem_.Active())
            {
                productionSystem_.StartProject(gameProject_, worldState_.gameClockMinutes,
                                               employeeSystem_.EmployeesMutable());
                SyncGameProjectProduction();
            }
            dayClock_.BeginWorking(worldState_);
            SPDLOG_INFO("StudioSim: goal set, entering Working");
            if (!goalMeetingStarted_ && GoalNeedsMeeting(goalSystem_.Goal()))
            {
                goalMeetingStarted_ = true;
                gatheringSystem_.RequestMeeting(
                    fmt::format("围绕《{}》今日目标「{}」做群体决策", gameProject_.name, goalSystem_.Goal().title));
            }
        }
    }
    else if (worldState_.phase == StudioSim::EDayPhase::Working && !worldState_.paused)
    {
        const bool dayEnded = dayClock_.TickWorking(worldState_, deltaSeconds,
                                                    IsPlayerDecisionFlowActive());
        if (dayEnded)
        {
            SPDLOG_INFO("StudioSim: day {} ended -> Review (goal was '{}')", worldState_.dayIndex,
                        goalSystem_.Goal().title);
            SyncGameProjectProduction();
            if (HasActiveGameProject() && !gameProject_.production.shipped &&
                gameProject_.elapsedDays + 1 >= gameProject_.plannedDays)
            {
                productionSystem_.ForceShip(worldState_.gameClockMinutes, "deadline");
                SyncGameProjectProduction();
            }
            FinalizeProjectSettlement();
            NextAI::FAIService* summaryAi =
                GOption->AgentValidation ? nullptr : NextAI::GetAIService(GetEngine());
            goalSystem_.Summarize(summaryAi, employeeSystem_.EmployeesMutable(), gameProject_);
        }

        if (worldState_.phase == StudioSim::EDayPhase::Working)
        {
            perceptionSystem_.Tick(deltaSeconds, worldState_, employeeSystem_.EmployeesMutable(),
                                   productionSystem_, gatheringSystem_);
            if (!IsAwaitingPlayerDecision())
            {
                const double gatheringDeltaSeconds = GOption->AgentValidation ? deltaSeconds * 12.0 : deltaSeconds;
                NextAI::FAIService* gatheringAi = GOption->AgentValidation ? nullptr : NextAI::GetAIService(GetEngine());
                gatheringSystem_.Tick(gatheringDeltaSeconds, worldState_, employeeSystem_.EmployeesMutable(),
                                      officeMap_, gameProject_, gatheringAi);
            }
            if (GOption->AgentValidation)
            {
                std::vector<int> autoAcceptIds;
                for (const auto& gathering : gatheringSystem_.Gatherings())
                {
                    if (gathering.awaitingConfirm && gathering.decision.valid)
                    {
                        autoAcceptIds.push_back(gathering.id);
                    }
                }
                for (int id : autoAcceptIds)
                {
                    gatheringSystem_.AcceptDecision(id, worldState_.gameClockMinutes,
                                                    employeeSystem_.EmployeesMutable(), productionSystem_);
                }
            }
            if (!IsPlayerDecisionFlowActive() && !IsAwaitingPlayerDecision() && !gatheringSystem_.HasActiveMeeting())
            {
                NextAI::FAIService* decisionAi = GOption->AgentValidation ? nullptr : NextAI::GetAIService(GetEngine());
                scheduler_.Tick(worldState_.gameClockMinutes, goalSystem_.Goal(), gameProject_,
                                StudioSim::EventSystem::BuildSummary(worldState_), decisionAi,
                                employeeSystem_.EmployeesMutable(), officeMap_);
            }
        }
    }
    else if (worldState_.phase == StudioSim::EDayPhase::Review)
    {
        // 收尾：排空 LLM 结算总结。
        goalSystem_.Tick(GOption->AgentValidation ? nullptr : NextAI::GetAIService(GetEngine()),
                         employeeSystem_.EmployeesMutable());
        if (GOption->AgentValidation && !goalSystem_.Summary().empty())
        {
            StartNextDay();
        }
    }

    const bool simulationPaused = worldState_.paused || IsAwaitingPlayerDecision();
    employeeSystem_.Tick(static_cast<float>(deltaSeconds), worldState_.gameClockMinutes, simulationPaused,
                         GetEngine().GetScene(), officeMap_);
    if (worldState_.phase == StudioSim::EDayPhase::Working)
    {
        const bool productionPaused = simulationPaused || IsPlayerDecisionFlowActive();
        productionSystem_.Tick(worldState_.gameClockMinutes, productionPaused, employeeSystem_.EmployeesMutable(),
                               officeMap_);
        SyncGameProjectProduction();
        ui_.CollectProductionVisualEvents(productionSystem_);
    }
    ui_.Tick(deltaSeconds);
    for (auto& emp : employeeSystem_.EmployeesMutable())
    {
        if (emp.bubbleClearAt > 0.0 && worldState_.gameClockMinutes >= emp.bubbleClearAt)
        {
            emp.bubbleText.clear();
            emp.bubbleClearAt = 0.0;
        }
    }
    UpdateCamera(deltaSeconds);
}

bool StudioSimGameInstance::IsAwaitingPlayerDecision() const
{
    if (GOption->AgentValidation)
    {
        return false;
    }
    if (worldState_.phase == StudioSim::EDayPhase::Briefing &&
        goalSystem_.State() == StudioSim::GoalSystem::EState::AwaitingChoice)
    {
        return true;
    }
    for (const auto& gathering : gatheringSystem_.Gatherings())
    {
        if (gathering.awaitingConfirm && gathering.decision.valid)
        {
            return true;
        }
    }
    return false;
}

bool StudioSimGameInstance::IsPlayerDecisionFlowActive() const
{
    if (worldState_.phase == StudioSim::EDayPhase::Briefing &&
        goalSystem_.State() == StudioSim::GoalSystem::EState::AwaitingChoice)
    {
        return true;
    }
    for (const auto& gathering : gatheringSystem_.Gatherings())
    {
        if (gathering.kind == StudioSim::EGatheringKind::Meeting && gathering.decision.valid &&
            gathering.state != StudioSim::EGatheringState::Dispersing)
        {
            return true;
        }
    }
    return false;
}

void StudioSimGameInstance::OnDestroy()
{
}

void StudioSimGameInstance::ResetProjectPitchSelection()
{
    gameProject_ = StudioSim::FGameProject{};
    ui_.ResetProjectPitchSelection();
}

void StudioSimGameInstance::StartProjectPitch(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                                              StudioSim::EProjectSizeTier sizeTier)
{
    gameProject_ = BuildProjectFromPitch(genre, theme, sizeTier, companyState_.projectIndex);
    productionSystem_.Reset();
    goalSystem_.Reset();
    goalSystem_.BeginDay(GOption->AgentValidation ? nullptr : NextAI::GetAIService(GetEngine()));
    goalMeetingStarted_ = false;
    ui_.ResetGoalInput();

    SPDLOG_INFO(
        "StudioSim/Proj: pitched project '{}' {} x {} size={} plannedDays={} budget={} comboFit={:.2f} funds={}",
        gameProject_.name, StudioSim::GameGenreName(gameProject_.genre), StudioSim::GameThemeName(gameProject_.theme),
        StudioSim::ProjectSizeTierName(sizeTier), gameProject_.plannedDays, gameProject_.budget,
        gameProject_.comboFit, companyState_.funds);
}

bool StudioSimGameInstance::HasActiveGameProject() const
{
    return !gameProject_.name.empty() && !gameProject_.launched;
}

void StudioSimGameInstance::SyncGameProjectProduction()
{
    if (productionSystem_.Active())
    {
        gameProject_.production = productionSystem_.State();
        for (auto& highlight : gameProject_.highlights)
        {
            if (highlight.achieved)
            {
                continue;
            }

            const float value = ProjectMeterValue(gameProject_.production.meters, highlight.meter);
            const float target = ProjectMeterValue(gameProject_.production.targetMeters, highlight.meter);
            if (target > 0.0f && ProjectMeterCompletion(value, target) >= 1.0f)
            {
                highlight.achieved = true;
                SPDLOG_INFO("StudioSim/Proj: highlight achieved '{}' for '{}'", highlight.text, gameProject_.name);
            }
        }
    }
}

void StudioSimGameInstance::FinalizeProjectSettlement()
{
    if (gameProject_.name.empty() || gameProject_.launched || !gameProject_.production.shipped)
    {
        return;
    }

    const int actualDays = std::max(1, gameProject_.elapsedDays + 1);
    gameProject_.elapsedDays = actualDays;
    gameProject_.quality = ComputeLaunchQuality(gameProject_, actualDays);
    gameProject_.reviewerScores = BuildReviewerScores(gameProject_, gameProject_.quality);
    gameProject_.reviewScore = ReviewScoreTotal(gameProject_.reviewerScores);
    gameProject_.unitsSold = EstimateUnitsSold(gameProject_);
    gameProject_.revenue = gameProject_.unitsSold * kUnitPrice;
    gameProject_.cost = kTeamDailyWage * actualDays;
    gameProject_.profit = gameProject_.revenue - gameProject_.cost;
    gameProject_.reviewQuotes = BuildReviewQuotes(gameProject_);
    gameProject_.launched = true;
    companyState_.funds += gameProject_.profit;

    SPDLOG_INFO(
        "StudioSim/Economy: launched '{}' quality={:.0f} review={} units={} revenue={} cost={} profit={} funds={}",
        gameProject_.name, gameProject_.quality * 100.0f, gameProject_.reviewScore, gameProject_.unitsSold,
        gameProject_.revenue, gameProject_.cost, gameProject_.profit, companyState_.funds);
}

void StudioSimGameInstance::OnSceneLoaded()
{
    Assets::Scene& scene = GetEngine().GetScene();
    sceneNodeCount_ = scene.Nodes().size();
    officeMap_.BuildFromScene(scene);
    employeeSystem_.OnSceneLoaded(scene, officeMap_);
    dayClock_.Reset(worldState_, GOption->AgentValidation);
    scheduler_.Reset();
    perceptionSystem_.Reset();
    goalSystem_.Reset();
    gatheringSystem_.Reset();
    productionSystem_.Reset();
    companyState_ = StudioSim::FCompanyState{};
    ResetProjectPitchSelection();
    ui_.Reset();
    goalMeetingStarted_ = false;
    followEmployeeIndex_ = -1;
    cameraTarget_ = DesiredCameraTarget();
    cameraEye_ = DesiredCameraEye();
    cameraInitialized_ = true;
    sceneReady_ = true;
    SPDLOG_INFO("StudioSim: scene loaded ({} nodes, {} POIs, {} employees)", sceneNodeCount_, officeMap_.Count(),
                employeeSystem_.Count());
}

void StudioSimGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    cameraInitialized_ = false;
    followEmployeeIndex_ = -1;
    sceneNodeCount_ = 0;
    employeeSystem_.Clear();
    officeMap_.Clear();
    productionSystem_.Reset();
    gatheringSystem_.Reset();
    ui_.Reset();
}

void StudioSimGameInstance::StartNextDay()
{
    SyncGameProjectProduction();
    if (productionSystem_.Active() && !gameProject_.launched)
    {
        ++gameProject_.elapsedDays;
        if (gameProject_.production.shipped)
        {
            gameProject_.launched = true;
        }
    }
    const bool projectLaunched = gameProject_.launched;
    const StudioSim::FGameProject finishedProject = gameProject_;

    dayClock_.BeginNextDay(worldState_);

    officeMap_.ResetWorkable();
    for (auto& emp : employeeSystem_.EmployeesMutable())
    {
        emp.todayTask.clear();
        emp.targetPoi.clear();
        emp.overrideTargetPoi.clear();
        emp.overrideUntilMinutes = 0.0;
        emp.bubbleText.clear();
        emp.bubbleClearAt = 0.0;
        emp.pendingFrom.clear();
        emp.pendingText.clear();
        emp.mood = StudioSim::EMood::Calm;
        emp.decisionPending = false;
        emp.nextDecisionAt = 0.0;
        emp.eventReactionPending = false;
        emp.gatheringId = -1;
        emp.myContribution = StudioSim::FProjectMeters{};
        emp.shortMemory.clear();
        emp.nextChatterAt = 0.0;
        emp.nextWorkOutputAt = 0.0;
    }

    scheduler_.Reset();
    perceptionSystem_.Reset();
    goalSystem_.Reset();
    gatheringSystem_.Reset();
    productionSystem_.ClearFocusBoost();
    ui_.Reset();
    if (projectLaunched)
    {
        companyState_.shipped.push_back(finishedProject);
        ++companyState_.projectIndex;
        productionSystem_.Reset();
        ResetProjectPitchSelection();
    }
    else
    {
        const StudioSim::FDailyGoal focusGoal = BuildProjectFocusGoal(gameProject_);
        goalSystem_.SetActiveGoal(focusGoal, employeeSystem_.EmployeesMutable());
    }
    ui_.ResetGoalInput();
    goalMeetingStarted_ = false;

    const auto& project = finishedProject.production;
    if (projectLaunched)
    {
        SPDLOG_INFO(
            "StudioSim/Proj: archived '{}' project day {}/{} progress={:.0f}% score={} units={} profit={} funds={} "
            "T/D/A/P={:.0f}/{:.0f}/{:.0f}/{:.0f}",
            finishedProject.name, finishedProject.elapsedDays, finishedProject.plannedDays,
            project.overallProgress * 100.0f, finishedProject.reviewScore, finishedProject.unitsSold,
            finishedProject.profit, companyState_.funds, project.meters.tech, project.meters.design,
            project.meters.art, project.meters.polish);
    }
    else
    {
        const int projectDay = std::min(finishedProject.elapsedDays + 1, finishedProject.plannedDays);
        SPDLOG_INFO(
            "StudioSim/Proj: continuing '{}' project day {}/{} progress={:.0f}% T/D/A/P={:.0f}/{:.0f}/{:.0f}/{:.0f}",
            finishedProject.name, projectDay, finishedProject.plannedDays, project.overallProgress * 100.0f,
            project.meters.tech, project.meters.design, project.meters.art, project.meters.polish);
    }
    SPDLOG_INFO("StudioSim: starting day {}", worldState_.dayIndex);
}

void StudioSimGameInstance::RaiseEventAndMaybeStartMeeting(const std::string& eventId)
{
    eventSystem_.Raise(eventId, worldState_.gameClockMinutes, worldState_, officeMap_);
    if (worldState_.phase == StudioSim::EDayPhase::Working && EventNeedsMeeting(eventId))
    {
        gatheringSystem_.RequestMeeting(MeetingTopicForEvent(eventId));
    }
}

bool StudioSimGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = ViewMatrix();
    outRenderCamera.FieldOfView = kOfficeFov;
    return true;
}

glm::vec3 StudioSimGameInstance::DesiredCameraTarget() const
{
    if (followEmployeeIndex_ >= 0 &&
        followEmployeeIndex_ < static_cast<int>(employeeSystem_.Employees().size()))
    {
        return employeeSystem_.Employees()[static_cast<size_t>(followEmployeeIndex_)].position;
    }
    return kOfficeOverviewTarget + glm::vec3(cameraPan_.x, 0.0f, cameraPan_.y);
}

glm::vec3 StudioSimGameInstance::DesiredCameraEye() const
{
    const glm::vec3 offset =
        followEmployeeIndex_ >= 0 ? kOfficeFollowOffset : kOfficeOverviewOffset * cameraZoom_;
    return DesiredCameraTarget() + offset;
}

glm::mat4 StudioSimGameInstance::ViewMatrix() const
{
    const glm::vec3 eye = cameraInitialized_ ? cameraEye_ : DesiredCameraEye();
    const glm::vec3 target = cameraInitialized_ ? cameraTarget_ : DesiredCameraTarget();
    return glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
}

void StudioSimGameInstance::UpdateCamera(double deltaSeconds)
{
    const glm::vec3 desiredTarget = DesiredCameraTarget();
    const glm::vec3 desiredEye = DesiredCameraEye();
    if (!cameraInitialized_)
    {
        cameraTarget_ = desiredTarget;
        cameraEye_ = desiredEye;
        cameraInitialized_ = true;
        return;
    }
    const float factor =
        1.0f - std::exp(-kCameraTransitionSharpness *
                        std::max(0.0f, static_cast<float>(deltaSeconds)));
    cameraTarget_ = glm::mix(cameraTarget_, desiredTarget, factor);
    cameraEye_ = glm::mix(cameraEye_, desiredEye, factor);
}

int StudioSimGameInstance::PickEmployeeAtScreen(const glm::vec2& screenPosition) const
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
    {
        return -1;
    }

    const float aspect = viewport->Size.x / viewport->Size.y;
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(kOfficeFov), aspect, 0.05f, 2000.0f) * ViewMatrix();
    int pickedIndex = -1;
    float nearestDistance = kEmployeePickRadiusPixels;
    const auto& employees = employeeSystem_.Employees();
    for (size_t index = 0; index < employees.size(); ++index)
    {
        ImVec2 feet;
        ImVec2 head;
        if (!ProjectWorld(viewProjection, viewport->Pos, viewport->Size,
                          employees[index].position + glm::vec3(0.0f, 0.1f, 0.0f), feet) ||
            !ProjectWorld(viewProjection, viewport->Pos, viewport->Size,
                          employees[index].position + glm::vec3(0.0f, 2.1f, 0.0f), head))
        {
            continue;
        }
        const float distance =
            DistanceToSegment(screenPosition, {feet.x, feet.y}, {head.x, head.y});
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            pickedIndex = static_cast<int>(index);
        }
    }
    return pickedIndex;
}

bool StudioSimGameInstance::OnRenderUI()
{
    const StudioSim::FGathering* activeMeeting = nullptr;
    for (const StudioSim::FGathering& gathering : gatheringSystem_.Gatherings())
    {
        if (gathering.kind == StudioSim::EGatheringKind::Meeting &&
            gathering.state != StudioSim::EGatheringState::Dispersing)
        {
            activeMeeting = &gathering;
            break;
        }
    }
    const std::string emptyMeetingTopic;
    const std::string& meetingTopic =
        activeMeeting != nullptr ? activeMeeting->topic : emptyMeetingTopic;

    const StudioSim::StudioSimUI::FHudContext hudContext{
        .world = worldState_,
        .gameProject = gameProject_,
        .company = companyState_,
        .goalSystem = goalSystem_,
        .productionSystem = productionSystem_,
        .employeeSystem = employeeSystem_,
        .scheduler = scheduler_,
        .eventSystem = eventSystem_,
        .gatheringSystem = gatheringSystem_,
        .officeMap = officeMap_,
        .sceneReady = sceneReady_,
        .sceneNodeCount = sceneNodeCount_,
        .meetingActive = activeMeeting != nullptr,
        .meetingTopic = meetingTopic,
        .awaitingPlayerDecision = IsAwaitingPlayerDecision(),
        .playerDecisionFlowActive = IsPlayerDecisionFlowActive(),
        .raiseEvent = [this](const std::string& eventId) { RaiseEventAndMaybeStartMeeting(eventId); },
    };
    ui_.DrawHud(hudContext);

    const StudioSim::StudioSimUI::FModalContext modalContext{
        .world = worldState_,
        .gameProject = gameProject_,
        .company = companyState_,
        .goalSystem = goalSystem_,
        .gatheringSystem = gatheringSystem_,
        .hasActiveGameProject = HasActiveGameProject(),
        .buildProjectPreview =
            [this](StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                   StudioSim::EProjectSizeTier size)
            {
                return BuildProjectFromPitch(genre, theme, size, companyState_.projectIndex);
            },
        .startProject =
            [this](StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                   StudioSim::EProjectSizeTier size)
            {
                StartProjectPitch(genre, theme, size);
            },
        .chooseGoal =
            [this](int index)
            {
                goalSystem_.ChooseGoal(index, NextAI::GetAIService(GetEngine()),
                                       employeeSystem_.EmployeesMutable());
            },
        .chooseCustomGoal =
            [this](const std::string& goal)
            {
                goalSystem_.ChooseCustom(goal, NextAI::GetAIService(GetEngine()),
                                         employeeSystem_.EmployeesMutable());
            },
        .acceptGathering =
            [this](int gatheringId)
            {
                gatheringSystem_.AcceptDecision(gatheringId, worldState_.gameClockMinutes,
                                                employeeSystem_.EmployeesMutable(), productionSystem_);
            },
        .rejectGathering =
            [this](int gatheringId)
            {
                gatheringSystem_.RejectDecision(gatheringId, worldState_.gameClockMinutes,
                                                employeeSystem_.EmployeesMutable());
            },
        .startNextDay = [this]() { StartNextDay(); },
    };
    ui_.DrawModals(modalContext);

    if (sceneReady_ && ui_.ShowOverlay())
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float aspect = viewport != nullptr && viewport->Size.y > 1.0f
                                 ? viewport->Size.x / viewport->Size.y
                                 : 16.0f / 9.0f;
        const glm::mat4 viewProjection =
            glm::perspective(glm::radians(kOfficeFov), aspect, 0.05f, 2000.0f) * ViewMatrix();
        ui_.DrawOverlay(viewProjection, officeMap_, employeeSystem_, worldState_);
    }
    return true;
}

bool StudioSimGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || !sceneReady_)
    {
        return false;
    }
    if (event.key.key == SDLK_ESCAPE && followEmployeeIndex_ >= 0)
    {
        followEmployeeIndex_ = -1;
        return true;
    }
    if (event.key.key == SDLK_LEFT)
    {
        cameraPan_.x = std::max(cameraPan_.x - 2.0f, -12.0f);
        return true;
    }
    if (event.key.key == SDLK_RIGHT)
    {
        cameraPan_.x = std::min(cameraPan_.x + 2.0f, 12.0f);
        return true;
    }
    if (event.key.key == SDLK_UP)
    {
        cameraPan_.y = std::max(cameraPan_.y - 2.0f, -10.0f);
        return true;
    }
    if (event.key.key == SDLK_DOWN)
    {
        cameraPan_.y = std::min(cameraPan_.y + 2.0f, 10.0f);
        return true;
    }

    int index = -1;
    if (event.key.key == SDLK_1) index = 0;
    else if (event.key.key == SDLK_2) index = 1;
    else if (event.key.key == SDLK_3) index = 2;
    if (index >= 0 && index < static_cast<int>(eventSystem_.Catalog().size()))
    {
        RaiseEventAndMaybeStartMeeting(eventSystem_.Catalog()[static_cast<size_t>(index)].id);
        return true;
    }
    return false;
}

bool StudioSimGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!sceneReady_ || event.type != SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.button.button != SDL_BUTTON_LEFT)
    {
        return false;
    }
    const ImVec2 mousePosition = ImGui::GetMousePos();
    followEmployeeIndex_ = PickEmployeeAtScreen({mousePosition.x, mousePosition.y});
    return followEmployeeIndex_ >= 0;
}

bool StudioSimGameInstance::OnScroll(double /*xoffset*/, double yoffset)
{
    cameraZoom_ = std::clamp(cameraZoom_ - static_cast<float>(yoffset) * 0.08f, 0.55f, 1.6f);
    return true;
}
