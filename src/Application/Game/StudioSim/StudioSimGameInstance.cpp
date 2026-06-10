#include "StudioSimGameInstance.hpp"

#include <fmt/format.h>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"

#include <nlohmann/json.hpp>
#include "Modules/ScadLoader/ScadModule.hpp"

namespace
{
    // Fixed isometric-ish overhead camera shared by the render override and the
    // debug overlay so projected anchors line up with the rendered scene.
    constexpr float kOfficeFov = 50.0f;
    constexpr double kBubbleFadeMinutes = 5.0;
    constexpr double kMeetingBubbleDurationMinutes = 20.0;
    constexpr ImGuiWindowFlags kStudioSimModalFlags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    constexpr ImGuiWindowFlags kStudioSimHudFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;

    float StudioSimModalWidth(float minWidth, float maxWidth)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float viewportWidth = viewport != nullptr ? viewport->WorkSize.x : 1280.0f;
        const float viewportLimit = std::max(360.0f, viewportWidth - 64.0f);
        const float maxAllowed = std::min(maxWidth, viewportLimit);
        const float minAllowed = std::min(minWidth, maxAllowed);
        return std::clamp(viewportWidth * 0.52f, minAllowed, maxAllowed);
    }

    float StudioSimModalMaxHeight()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float viewportHeight = viewport != nullptr ? viewport->WorkSize.y : 720.0f;
        return std::max(260.0f, viewportHeight * 0.82f);
    }

    void PrepareStudioSimModal(float minWidth, float maxWidth)
    {
        const float width = StudioSimModalWidth(minWidth, maxWidth);
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, StudioSimModalMaxHeight()));
    }

    void TextDisabledWrapped(const std::string& text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", text.c_str());
        ImGui::PopStyleColor();
    }

    bool BeginStudioSimHudPanel(const char* name, const ImVec2& pos, const ImVec2& size)
    {
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88f);
        return ImGui::Begin(name, nullptr, kStudioSimHudFlags);
    }

    void DrawHudTitle(const char* title)
    {
        ImGui::TextColored(ImVec4(0.78f, 0.88f, 1.00f, 1.0f), "%s", title);
        ImGui::Separator();
    }

    glm::mat4 OfficeViewMatrix()
    {
        return glm::lookAt(glm::vec3(0.0f, 18.0f, 18.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    ImU32 CategoryColor(const std::string& category)
    {
        if (category == "desk")   return IM_COL32(230, 150, 60, 255);
        if (category == "meet")   return IM_COL32(70, 140, 230, 255);
        if (category == "pantry") return IM_COL32(220, 200, 80, 255);
        if (category == "lounge") return IM_COL32(190, 120, 210, 255);
        return IM_COL32(200, 200, 200, 255);
    }

    ImU32 ColorToImU32(const glm::vec3& c)
    {
        return IM_COL32(static_cast<int>(c.r * 255.0f), static_cast<int>(c.g * 255.0f), static_cast<int>(c.b * 255.0f),
                        255);
    }

    ImU32 ColorToImU32(const glm::vec4& c)
    {
        const auto channel = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        return IM_COL32(channel(c.r), channel(c.g), channel(c.b), channel(c.a));
    }

    ImVec4 ToImVec4(const glm::vec4& c)
    {
        return ImVec4(c.r, c.g, c.b, c.a);
    }

    glm::vec4 MeterColor(const std::string& meter)
    {
        if (meter == "tech") return {0.20f, 0.60f, 1.00f, 1.0f};
        if (meter == "design") return {0.28f, 0.82f, 0.44f, 1.0f};
        if (meter == "art") return {1.00f, 0.55f, 0.18f, 1.0f};
        if (meter == "polish") return {0.74f, 0.52f, 1.00f, 1.0f};
        if (meter == "bug_found") return {1.00f, 0.25f, 0.22f, 1.0f};
        if (meter == "bug_fixed") return {0.35f, 0.95f, 0.85f, 1.0f};
        return {0.92f, 0.92f, 0.92f, 1.0f};
    }

    const char* ProjectStageLabelZh(StudioSim::EProjectStage stage)
    {
        switch (stage)
        {
        case StudioSim::EProjectStage::Planning:   return "企划";
        case StudioSim::EProjectStage::Production: return "生产";
        case StudioSim::EProjectStage::Polish:     return "打磨";
        case StudioSim::EProjectStage::Done:       return "完成";
        default:                                   return "?";
        }
    }

    glm::vec4 ProjectStageColor(StudioSim::EProjectStage stage)
    {
        switch (stage)
        {
        case StudioSim::EProjectStage::Planning:   return {0.78f, 0.78f, 0.78f, 1.0f};
        case StudioSim::EProjectStage::Production: return {0.20f, 0.60f, 1.00f, 1.0f};
        case StudioSim::EProjectStage::Polish:     return {0.74f, 0.52f, 1.00f, 1.0f};
        case StudioSim::EProjectStage::Done:       return {0.28f, 0.82f, 0.44f, 1.0f};
        default:                                   return {0.92f, 0.92f, 0.92f, 1.0f};
        }
    }

    constexpr StudioSim::EGameGenre kProjectPitchGenres[] = {
        StudioSim::EGameGenre::RPG,        StudioSim::EGameGenre::Action,  StudioSim::EGameGenre::Simulation,
        StudioSim::EGameGenre::Puzzle,     StudioSim::EGameGenre::Shooter, StudioSim::EGameGenre::Adventure,
    };
    constexpr StudioSim::EGameTheme kProjectPitchThemes[] = {
        StudioSim::EGameTheme::Fantasy, StudioSim::EGameTheme::SciFi,  StudioSim::EGameTheme::Sports,
        StudioSim::EGameTheme::Romance, StudioSim::EGameTheme::Horror, StudioSim::EGameTheme::Daily,
    };
    constexpr StudioSim::EProjectSizeTier kProjectPitchSizes[] = {
        StudioSim::EProjectSizeTier::Small,
        StudioSim::EProjectSizeTier::Standard,
        StudioSim::EProjectSizeTier::Big,
    };
    constexpr int kProjectPitchGenreCount = 6;
    constexpr int kProjectPitchThemeCount = 6;
    constexpr int kProjectPitchSizeCount = 3;
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

    const char* ProjectSizeTierLabelZh(StudioSim::EProjectSizeTier tier)
    {
        switch (tier)
        {
        case StudioSim::EProjectSizeTier::Small:    return "小品";
        case StudioSim::EProjectSizeTier::Standard: return "标准";
        case StudioSim::EProjectSizeTier::Big:      return "大作";
        default:                                    return "标准";
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

    float MeterRatio(float value, float target)
    {
        return target > 0.0f ? std::clamp(value / target, 0.0f, 1.0f) : 0.0f;
    }

    void DrawMeterProgress(const char* label, float value, float target, const glm::vec4& color)
    {
        const float ratio = MeterRatio(value, target);
        const std::string overlay = fmt::format("{} {:.0f}/{:.0f}", label, value, target);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToImVec4(color));
        ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), overlay.c_str());
        ImGui::PopStyleColor();
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

    std::vector<StudioSim::FMeetingLine> BuildFallbackMeetingLines(const std::vector<StudioSim::FEmployee>& employees,
                                                                    const std::string& topic)
    {
        std::vector<StudioSim::FMeetingLine> lines;
        for (const auto& emp : employees)
        {
            if (emp.role == StudioSim::ERole::ProducerPM)
            {
                lines.push_back({emp.displayName, "先定优先级"});
            }
            else if (emp.role == StudioSim::ERole::Engineer)
            {
                lines.push_back({emp.displayName, "我评估技术风险"});
            }
            else if (emp.role == StudioSim::ERole::Designer)
            {
                lines.push_back({emp.displayName, "玩法目标要收敛"});
            }
            else if (emp.role == StudioSim::ERole::Artist)
            {
                lines.push_back({emp.displayName, "美术量要砍一刀"});
            }
            else if (emp.role == StudioSim::ERole::QA)
            {
                lines.push_back({emp.displayName, "测试范围要明确"});
            }
        }
        if (lines.empty())
        {
            lines.push_back({"Team", topic});
        }
        return lines;
    }

    std::vector<StudioSim::FMeetingLine> ParseMeetingLines(const std::string& text)
    {
        std::vector<StudioSim::FMeetingLine> lines;
        const size_t open = text.find('[');
        const size_t close = text.rfind(']');
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return lines;
        }

        try
        {
            const nlohmann::json json = nlohmann::json::parse(text.substr(open, close - open + 1));
            for (const auto& item : json)
            {
                StudioSim::FMeetingLine line;
                line.speaker = item.value("speaker", std::string());
                line.text = item.value("line", std::string());
                if (!line.speaker.empty() && !line.text.empty())
                {
                    lines.push_back(std::move(line));
                }
                if (lines.size() >= 10)
                {
                    break;
                }
            }
        }
        catch (...)
        {
            lines.clear();
        }
        return lines;
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
    options.QuickJSEntry = "assets/scripts/studiosim_entry.js";
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
    if (auto* ai = GetEngine().GetAIService())
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
                StartProjectPitch(kProjectPitchGenres[pitchGenreIndex_], kProjectPitchThemes[pitchThemeIndex_],
                                  kProjectPitchSizes[pitchSizeIndex_]);
            }
            else
            {
                return;
            }
        }

        // 晨会：等 LLM 给目标 → 玩家选/自定义 → 分解 → 开工（时钟暂停在 09:00）。
        goalSystem_.Tick(GOption->AgentValidation ? nullptr : GetEngine().GetAIService(),
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
            worldState_.phase = StudioSim::EDayPhase::Working;
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
        const bool decisionFlowActive = IsPlayerDecisionFlowActive();
        if (!decisionFlowActive)
        {
            worldState_.gameClockMinutes += deltaSeconds * worldState_.timeScale;
            if (worldState_.gameClockMinutes >= 18.0 * 60.0)
            {
                worldState_.gameClockMinutes = 18.0 * 60.0;
                worldState_.phase = StudioSim::EDayPhase::Review;
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
                NextAI::FAIService* summaryAi = GOption->AgentValidation ? nullptr : GetEngine().GetAIService();
                goalSystem_.Summarize(summaryAi, employeeSystem_.EmployeesMutable(), gameProject_);
            }
        }

        if (worldState_.phase == StudioSim::EDayPhase::Working)
        {
            if (!IsAwaitingPlayerDecision())
            {
                const double gatheringDeltaSeconds = GOption->AgentValidation ? deltaSeconds * 12.0 : deltaSeconds;
                NextAI::FAIService* gatheringAi = GOption->AgentValidation ? nullptr : GetEngine().GetAIService();
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
                NextAI::FAIService* decisionAi = GOption->AgentValidation ? nullptr : GetEngine().GetAIService();
                scheduler_.Tick(worldState_.gameClockMinutes, goalSystem_.Goal(), gameProject_,
                                StudioSim::EventSystem::BuildSummary(worldState_), decisionAi,
                                employeeSystem_.EmployeesMutable(), officeMap_);
            }
        }
    }
    else if (worldState_.phase == StudioSim::EDayPhase::Review)
    {
        // 收尾：排空 LLM 结算总结。
        goalSystem_.Tick(GOption->AgentValidation ? nullptr : GetEngine().GetAIService(),
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
        CollectProductionVisualEvents();
    }
    TickFloatingText(deltaSeconds);
    for (auto& emp : employeeSystem_.EmployeesMutable())
    {
        if (emp.bubbleClearAt > 0.0 && worldState_.gameClockMinutes >= emp.bubbleClearAt)
        {
            emp.bubbleText.clear();
            emp.bubbleClearAt = 0.0;
        }
    }
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

void StudioSimGameInstance::CollectProductionVisualEvents()
{
    for (const auto& event : productionSystem_.ConsumeVisualEvents())
    {
        FFloatingTextParticle particle;
        particle.worldPos = event.worldPos;
        particle.text = event.text;
        particle.color = MeterColor(event.meter);
        floatingText_.push_back(std::move(particle));
    }
}

void StudioSimGameInstance::TickFloatingText(double deltaSeconds)
{
    for (auto& particle : floatingText_)
    {
        particle.ageSeconds += static_cast<float>(deltaSeconds);
    }
    floatingText_.erase(std::remove_if(floatingText_.begin(), floatingText_.end(),
                                       [](const FFloatingTextParticle& particle)
                                       {
                                           return particle.ageSeconds >= particle.durationSeconds;
                                       }),
                        floatingText_.end());
}

void StudioSimGameInstance::ResetProjectPitchSelection()
{
    gameProject_ = StudioSim::FGameProject{};
    pitchGenreIndex_ = 0;
    pitchThemeIndex_ = 0;
    pitchSizeIndex_ = 1;
}

void StudioSimGameInstance::StartProjectPitch(StudioSim::EGameGenre genre, StudioSim::EGameTheme theme,
                                              StudioSim::EProjectSizeTier sizeTier)
{
    gameProject_ = BuildProjectFromPitch(genre, theme, sizeTier, companyState_.projectIndex);
    productionSystem_.Reset();
    goalSystem_.Reset();
    goalSystem_.BeginDay(GOption->AgentValidation ? nullptr : GetEngine().GetAIService());
    goalMeetingStarted_ = false;
    customGoalBuf_[0] = '\0';

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
    worldState_ = StudioSim::FWorldState{};
    worldState_.phase = StudioSim::EDayPhase::Briefing; // 晨会先定今日目标
    if (GOption->AgentValidation)
    {
        worldState_.timeScale = 240.0f;
    }
    scheduler_.Reset();
    goalSystem_.Reset();
    gatheringSystem_.Reset();
    productionSystem_.Reset();
    companyState_ = StudioSim::FCompanyState{};
    ResetProjectPitchSelection();
    floatingText_.clear();
    goalMeetingStarted_ = false;
    sceneReady_ = true;
    SPDLOG_INFO("StudioSim: scene loaded ({} nodes, {} POIs, {} employees)", sceneNodeCount_, officeMap_.Count(),
                employeeSystem_.Count());
}

void StudioSimGameInstance::OnSceneUnloaded()
{
    sceneReady_ = false;
    sceneNodeCount_ = 0;
    employeeSystem_.Clear();
    officeMap_.Clear();
    productionSystem_.Reset();
    gatheringSystem_.Reset();
    floatingText_.clear();
}

void StudioSimGameInstance::StartNextDay()
{
    const int nextDay = worldState_.dayIndex + 1;
    const float timeScale = worldState_.timeScale;
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

    worldState_ = StudioSim::FWorldState{};
    worldState_.dayIndex = nextDay;
    worldState_.timeScale = timeScale;
    worldState_.phase = StudioSim::EDayPhase::Briefing;
    worldState_.paused = false;

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
    goalSystem_.Reset();
    gatheringSystem_.Reset();
    productionSystem_.ClearFocusBoost();
    floatingText_.clear();
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
    customGoalBuf_[0] = '\0';
    goalMeetingStarted_ = false;
    meeting_ = FMeetingRuntime{};
    {
        std::lock_guard<std::mutex> lock(meetingMutex_);
        ++meetingGeneration_;
        pendingMeetingLines_.clear();
    }

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

void StudioSimGameInstance::StartMeeting(const std::string& topic, double durationMinutes)
{
    const auto seats = officeMap_.PointsOfCategory("meet");
    if (seats.empty() || employeeSystem_.EmployeesMutable().empty())
    {
        SPDLOG_WARN("StudioSim/Meeting: cannot start '{}', no meeting seats or employees", topic);
        return;
    }

    scheduler_.Reset();
    {
        std::lock_guard<std::mutex> lock(meetingMutex_);
        ++meetingGeneration_;
        pendingMeetingLines_.clear();
    }

    meeting_ = FMeetingRuntime{};
    meeting_.active = true;
    meeting_.topic = topic;
    meeting_.endGameMinutes = worldState_.gameClockMinutes + durationMinutes;
    meeting_.nextLineRealSeconds = 1.0;
    meeting_.lines = BuildFallbackMeetingLines(employeeSystem_.Employees(), topic);

    auto& employees = employeeSystem_.EmployeesMutable();
    for (size_t i = 0; i < employees.size(); ++i)
    {
        auto& emp = employees[i];
        emp.targetPoi.clear();
        emp.overrideTargetPoi = seats[i % seats.size()]->name;
        emp.overrideUntilMinutes = meeting_.endGameMinutes;
        emp.bubbleText = "去会议室";
        emp.bubbleClearAt = worldState_.gameClockMinutes + 8.0;
        emp.pendingFrom.clear();
        emp.pendingText.clear();
        emp.decisionPending = false;
        emp.nextDecisionAt = meeting_.endGameMinutes + 1.0;
    }

    if (auto* ai = GetEngine().GetAIService())
    {
        std::string attendees;
        for (const auto& emp : employeeSystem_.Employees())
        {
            attendees += fmt::format("{}({}) ", emp.displayName, StudioSim::RoleName(emp.role));
        }

        const std::string prompt = fmt::format(
            "你是游戏工作室会议编剧。会议主题：{}。\n"
            "今日目标：{}（{}）。当日事件：{}。\n"
            "参会者：{}。\n"
            "生成一段多人群聊会议记录，6到10句。每句必须由参会者之一发言，围绕是否调整计划、谁负责什么。"
            "只输出JSON数组，不要解释：[{{\"speaker\":\"Alice\",\"line\":\"一句不超过16字\"}}]",
            topic, goalSystem_.Goal().set ? goalSystem_.Goal().title : "未定",
            goalSystem_.Goal().set ? goalSystem_.Goal().description : "", StudioSim::EventSystem::BuildSummary(worldState_),
            attendees);

        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(meetingMutex_);
            generation = meetingGeneration_;
        }

        ai->GenerateTextAsync(prompt,
                              [this, generation](NextAI::FAIResponse response)
                              {
                                  auto lines = ParseMeetingLines(response.success ? response.text : std::string());
                                  if (lines.empty())
                                  {
                                      return;
                                  }

                                  std::lock_guard<std::mutex> lock(meetingMutex_);
                                  if (generation != meetingGeneration_)
                                  {
                                      return;
                                  }
                                  pendingMeetingLines_ = std::move(lines);
                              });
    }

    SPDLOG_INFO("StudioSim/Meeting started: '{}' ({} employees, {:.0f} game minutes)", topic, employees.size(),
                durationMinutes);
}

void StudioSimGameInstance::TickMeeting(double deltaSeconds)
{
    {
        std::lock_guard<std::mutex> lock(meetingMutex_);
        if (!pendingMeetingLines_.empty())
        {
            meeting_.lines = std::move(pendingMeetingLines_);
            pendingMeetingLines_.clear();
            meeting_.nextLineIndex = 0;
            meeting_.elapsedRealSeconds = 0.0;
            meeting_.nextLineRealSeconds = 1.0;
        }
    }

    if (!meeting_.active)
    {
        return;
    }

    if (worldState_.gameClockMinutes >= meeting_.endGameMinutes)
    {
        meeting_.active = false;
        for (auto& emp : employeeSystem_.EmployeesMutable())
        {
            emp.overrideTargetPoi.clear();
            emp.overrideUntilMinutes = 0.0;
            emp.bubbleText.clear();
            emp.bubbleClearAt = 0.0;
            emp.nextDecisionAt = worldState_.gameClockMinutes;
        }
        SPDLOG_INFO("StudioSim/Meeting ended: '{}'", meeting_.topic);
        return;
    }

    meeting_.elapsedRealSeconds += deltaSeconds;
    if (meeting_.lines.empty() || meeting_.elapsedRealSeconds < meeting_.nextLineRealSeconds)
    {
        return;
    }

    const StudioSim::FMeetingLine& line = meeting_.lines[meeting_.nextLineIndex % meeting_.lines.size()];
    for (auto& emp : employeeSystem_.EmployeesMutable())
    {
        if (emp.displayName == line.speaker)
        {
            emp.bubbleText = line.text;
            emp.bubbleClearAt = worldState_.gameClockMinutes + kMeetingBubbleDurationMinutes;
            emp.mood = StudioSim::EMood::Focused;
            break;
        }
    }
    meeting_.nextLineIndex++;
    meeting_.nextLineRealSeconds += 3.0;
}

void StudioSimGameInstance::RaiseEventAndMaybeStartMeeting(const std::string& eventId)
{
    eventSystem_.Raise(eventId, worldState_.gameClockMinutes, worldState_, employeeSystem_.EmployeesMutable(),
                       officeMap_);
    if (worldState_.phase == StudioSim::EDayPhase::Working && EventNeedsMeeting(eventId))
    {
        gatheringSystem_.RequestMeeting(MeetingTopicForEvent(eventId));
    }
}

bool StudioSimGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = OfficeViewMatrix();
    outRenderCamera.FieldOfView = kOfficeFov;
    return true;
}

void StudioSimGameInstance::DrawWorldOverlay() const
{
    const ImVec2 vpSize = ImGui::GetMainViewport()->Size;
    const ImVec2 vpPos = ImGui::GetMainViewport()->Pos;
    if (vpSize.x <= 1.0f || vpSize.y <= 1.0f)
    {
        return;
    }

    const float aspect = vpSize.x / vpSize.y;
    const glm::mat4 viewProjection =
        glm::perspective(glm::radians(kOfficeFov), aspect, 0.05f, 2000.0f) * OfficeViewMatrix();

    auto* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 screen;

    // POI anchors.
    for (const auto& poi : officeMap_.Points())
    {
        if (ProjectWorld(viewProjection, vpPos, vpSize, poi.worldPos + glm::vec3(0.0f, 0.9f, 0.0f), screen))
        {
            const ImU32 color = CategoryColor(poi.category);
            drawList->AddCircleFilled(screen, 4.0f, color);
            drawList->AddText(ImVec2(screen.x + 6.0f, screen.y - 6.0f), color, poi.name.c_str());
        }
    }

    // Employee name tags floating above each agent.
    for (const auto& emp : employeeSystem_.Employees())
    {
        if (ProjectWorld(viewProjection, vpPos, vpSize, emp.position + glm::vec3(0.0f, 2.0f, 0.0f), screen))
        {
            const ImU32 color = ColorToImU32(emp.color);
            drawList->AddText(ImVec2(screen.x - 12.0f, screen.y - 8.0f), color, emp.displayName.c_str());
            const char* bubble = emp.decisionPending ? "..." : emp.bubbleText.c_str();
            if (bubble != nullptr && bubble[0] != '\0')
            {
                constexpr float bubbleMaxWidth = 240.0f;
                const ImVec2 padding(8.0f, 5.0f);
                const ImVec2 textSize = ImGui::CalcTextSize(bubble, nullptr, false, bubbleMaxWidth);
                const ImVec2 textPos(screen.x - textSize.x * 0.5f, screen.y + 8.0f);
                const ImVec2 bubbleMin(textPos.x - padding.x, textPos.y - padding.y);
                const ImVec2 bubbleMax(textPos.x + textSize.x + padding.x, textPos.y + textSize.y + padding.y);
                float alphaScale = 1.0f;
                if (emp.bubbleClearAt > worldState_.gameClockMinutes)
                {
                    const double remaining = emp.bubbleClearAt - worldState_.gameClockMinutes;
                    alphaScale = static_cast<float>(std::clamp(remaining / kBubbleFadeMinutes, 0.0, 1.0));
                }
                const auto alpha = [alphaScale](int value)
                {
                    return static_cast<int>(static_cast<float>(value) * alphaScale);
                };
                drawList->AddRectFilled(bubbleMin, bubbleMax, IM_COL32(20, 24, 28, alpha(220)), 6.0f);
                drawList->AddRect(bubbleMin, bubbleMax, IM_COL32(255, 255, 255, alpha(70)), 6.0f);
                drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos, IM_COL32(255, 255, 255, alpha(245)),
                                  bubble, nullptr, bubbleMaxWidth);
            }
        }
    }

    for (const auto& particle : floatingText_)
    {
        const float t = particle.durationSeconds > 0.0f ? particle.ageSeconds / particle.durationSeconds : 1.0f;
        const glm::vec3 pos = particle.worldPos + glm::vec3(0.0f, t * 0.9f, 0.0f);
        if (!ProjectWorld(viewProjection, vpPos, vpSize, pos, screen))
        {
            continue;
        }

        glm::vec4 color = particle.color;
        color.a *= std::clamp(1.0f - t, 0.0f, 1.0f);
        const ImVec2 textSize = ImGui::CalcTextSize(particle.text.c_str());
        const ImVec2 textPos(screen.x - textSize.x * 0.5f, screen.y - 24.0f);
        drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f), IM_COL32(0, 0, 0, 180),
                          particle.text.c_str());
        drawList->AddText(textPos, ColorToImU32(color), particle.text.c_str());
    }
}

void StudioSimGameInstance::DrawStatusHud(const ImVec2& pos, const ImVec2& size)
{
    if (!BeginStudioSimHudPanel("##StudioSimStatusHud", pos, size))
    {
        ImGui::End();
        return;
    }

    DrawHudTitle("工作日");
    int hh = 0;
    int mm = 0;
    StudioSim::MinutesToHHMM(worldState_.gameClockMinutes, hh, mm);
    ImGui::Text("Day %d", worldState_.dayIndex);
    ImGui::SameLine();
    ImGui::TextDisabled("%s  %02d:%02d", StudioSim::DayPhaseName(worldState_.phase), hh, mm);
    ImGui::TextWrapped("%s", goalSystem_.Goal().set ? goalSystem_.Goal().title.c_str() : "晨会准备中...");
    ImGui::SliderFloat("速度", &worldState_.timeScale, 1.0f, 240.0f, "%.0f min/s");
    ImGui::Checkbox("暂停", &worldState_.paused);
    if (IsAwaitingPlayerDecision())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "等待玩家判断，游戏进度已暂停");
    }
    else if (IsPlayerDecisionFlowActive())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "会议决策中，游戏进度已暂停");
    }

    ImGui::Separator();
    ImGui::TextDisabled("scene %s  nav %s", sceneReady_ ? "ready" : "loading",
                        employeeSystem_.NavReady() ? "ok" : "no");
    ImGui::TextDisabled("LLM %s  decisions %d", scheduler_.InFlight() ? "thinking" : "idle",
                        scheduler_.DecisionsMade());
    ImGui::End();
}

void StudioSimGameInstance::DrawProgressHud(const ImVec2& pos, const ImVec2& size)
{
    if (!BeginStudioSimHudPanel("##StudioSimProgressHud", pos, size))
    {
        ImGui::End();
        return;
    }

    DrawHudTitle("项目进度");
    if (gameProject_.name.empty())
    {
        ImGui::TextWrapped("等待新项目立项...");
        ImGui::Text("公司资金 %lld", static_cast<long long>(companyState_.funds));
        ImGui::Text("已发行 %zu 款", companyState_.shipped.size());
        ImGui::End();
        return;
    }

    const int plannedDays = std::max(1, gameProject_.plannedDays);
    const bool projectLaunched = gameProject_.launched || gameProject_.production.shipped;
    const int displayDay =
        projectLaunched ? std::clamp(gameProject_.elapsedDays, 1, plannedDays)
                        : std::clamp(gameProject_.elapsedDays + 1, 1, plannedDays);
    ImGui::TextWrapped("《%s》 %s x %s", gameProject_.name.c_str(), StudioSim::GameGenreName(gameProject_.genre),
                       StudioSim::GameThemeName(gameProject_.theme));
    const int daysLeft = projectLaunched ? 0 : std::max(0, plannedDays - gameProject_.elapsedDays);
    ImGui::Text("%s %d/%d 天 | 剩余 %d 天 | 预算 %lld", projectLaunched ? "已上线" : "工期第", displayDay,
                plannedDays, daysLeft, static_cast<long long>(gameProject_.budget));
    ImGui::Text("公司资金 %lld", static_cast<long long>(companyState_.funds));
    if (!gameProject_.highlights.empty())
    {
        std::string highlights;
        for (const auto& highlight : gameProject_.highlights)
        {
            if (!highlights.empty())
            {
                highlights += " / ";
            }
            highlights += highlight.text;
            if (highlight.achieved)
            {
                highlights += "(已做实)";
            }
        }
        ImGui::TextWrapped("卖点: %s", highlights.c_str());
    }
    ImGui::Separator();
    if (!productionSystem_.Active())
    {
        ImGui::TextWrapped("等待今日目标...");
        ImGui::End();
        return;
    }

    const auto& project = productionSystem_.State();
    ImGui::TextUnformatted("阶段:");
    ImGui::SameLine();
    ImGui::TextColored(ToImVec4(ProjectStageColor(project.stage)), "%s", ProjectStageLabelZh(project.stage));
    ImGui::SameLine();
    ImGui::Text("| Bug %d | 已修 %d | %s", project.bugCount, project.bugsFixed,
                project.shipped ? "已交付" : "未交付");

    const std::string overallLabel = fmt::format("总进度 {:.0f}%", project.overallProgress * 100.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToImVec4({0.20f, 0.60f, 1.00f, 1.0f}));
    ImGui::ProgressBar(std::clamp(project.overallProgress, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f),
                       overallLabel.c_str());
    ImGui::PopStyleColor();

    DrawMeterProgress("技术", project.meters.tech, project.targetMeters.tech, MeterColor("tech"));
    DrawMeterProgress("玩法", project.meters.design, project.targetMeters.design, MeterColor("design"));
    DrawMeterProgress("美术", project.meters.art, project.targetMeters.art, MeterColor("art"));
    DrawMeterProgress("品质", project.meters.polish, project.targetMeters.polish, MeterColor("polish"));
    ImGui::End();
}

void StudioSimGameInstance::DrawEmployeeHud(const ImVec2& pos, const ImVec2& size)
{
    if (!BeginStudioSimHudPanel("##StudioSimEmployeeHud", pos, size))
    {
        ImGui::End();
        return;
    }

    DrawHudTitle("员工状态");
    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##StudioSimEmployees", 5, tableFlags, ImVec2(0.0f, 0.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("姓名", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("职位", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("情绪", ImGuiTableColumnFlags_WidthFixed, 76.0f);
        ImGui::TableSetupColumn("目标", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("贡献 T/D/A/P", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableHeadersRow();

        for (const auto& emp : employeeSystem_.Employees())
        {
            const std::string target =
                emp.decisionPending ? "thinking..." : (emp.targetPoi.empty() ? "(idle)" : emp.targetPoi);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImColor(ColorToImU32(emp.color)), "%s", emp.displayName.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(StudioSim::RoleName(emp.role));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(StudioSim::MoodName(emp.mood));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", target.c_str());
            if (!emp.bubbleText.empty())
            {
                ImGui::TextDisabled("%s", emp.bubbleText.c_str());
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.0f / %.0f / %.0f / %.0f", emp.myContribution.tech, emp.myContribution.design,
                        emp.myContribution.art, emp.myContribution.polish);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void StudioSimGameInstance::DrawEventHud(const ImVec2& pos, const ImVec2& size)
{
    if (!BeginStudioSimHudPanel("##StudioSimEventHud", pos, size))
    {
        ImGui::End();
        return;
    }

    DrawHudTitle("事件 / 会议");
    if (meeting_.active)
    {
        ImGui::TextWrapped("Meeting: %s", meeting_.topic.c_str());
    }
    for (const auto& gathering : gatheringSystem_.Gatherings())
    {
        ImGui::TextWrapped("%s #%d: %s", gathering.kind == StudioSim::EGatheringKind::Meeting ? "Meeting" : "Pantry",
                           gathering.id, gathering.topic.c_str());
        if (gathering.awaitingConfirm && gathering.decision.valid)
        {
            ImGui::TextDisabled("等待会议决策弹窗");
        }
    }

    if (worldState_.todaysEvents.empty())
    {
        ImGui::TextDisabled("今日暂无随机事件");
    }
    else
    {
        ImGui::Text("Mood: %s", worldState_.globalMood.c_str());
        for (const auto& ev : worldState_.todaysEvents)
        {
            ImGui::BulletText("%s", ev.title.c_str());
        }
    }

    ImGui::Separator();
    for (const auto& def : eventSystem_.Catalog())
    {
        ImGui::PushID(def.id.c_str());
        if (ImGui::Button(def.title.c_str(), ImVec2(-1.0f, 0.0f)))
        {
            RaiseEventAndMaybeStartMeeting(def.id);
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Checkbox("Show overlay", &showOverlay_);
    ImGui::TextDisabled("nodes %zu  POIs %zu  employees %zu", sceneNodeCount_, officeMap_.Count(),
                        employeeSystem_.Count());
    ImGui::End();
}

bool StudioSimGameInstance::OnRenderUI()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport != nullptr ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 workSize = viewport != nullptr ? viewport->WorkSize : ImVec2(1280.0f, 720.0f);
    constexpr float margin = 16.0f;

    const float leftWidth = std::clamp(workSize.x * 0.28f, 320.0f, 420.0f);
    const float rightWidth = std::clamp(workSize.x * 0.30f, 340.0f, 460.0f);
    const float statusHeight = std::clamp(workSize.y * 0.23f, 160.0f, 198.0f);
    const float progressHeight = std::clamp(workSize.y * 0.34f, 230.0f, 310.0f);
    const float bottomHeight = std::clamp(workSize.y * 0.31f, 220.0f, 280.0f);
    const float employeeMaxWidth = std::max(320.0f, workSize.x - rightWidth - margin * 3.0f);
    const float employeeWidth = std::min(std::clamp(workSize.x * 0.46f, 380.0f, 660.0f), employeeMaxWidth);

    DrawStatusHud(ImVec2(workPos.x + margin, workPos.y + margin), ImVec2(leftWidth, statusHeight));
    DrawProgressHud(ImVec2(workPos.x + workSize.x - rightWidth - margin, workPos.y + margin),
                    ImVec2(rightWidth, progressHeight));
    DrawEmployeeHud(ImVec2(workPos.x + margin, workPos.y + workSize.y - bottomHeight - margin),
                    ImVec2(employeeWidth, bottomHeight));
    DrawEventHud(ImVec2(workPos.x + workSize.x - rightWidth - margin,
                        workPos.y + workSize.y - bottomHeight - margin),
                 ImVec2(rightWidth, bottomHeight));

    DrawProjectPitchModal();
    DrawGoalChoiceModal();
    DrawGatheringDecisionModal();
    DrawReviewModal();

    if (sceneReady_ && showOverlay_)
    {
        DrawWorldOverlay();
    }
    return true;
}

void StudioSimGameInstance::DrawProjectPitchModal()
{
    if (HasActiveGameProject() || worldState_.phase != StudioSim::EDayPhase::Briefing)
    {
        return;
    }

    ImGui::OpenPopup("游戏立项");
    PrepareStudioSimModal(640.0f, 860.0f);
    if (!ImGui::BeginPopupModal("游戏立项", nullptr, kStudioSimModalFlags))
    {
        return;
    }

    pitchGenreIndex_ = std::clamp(pitchGenreIndex_, 0, kProjectPitchGenreCount - 1);
    pitchThemeIndex_ = std::clamp(pitchThemeIndex_, 0, kProjectPitchThemeCount - 1);
    pitchSizeIndex_ = std::clamp(pitchSizeIndex_, 0, kProjectPitchSizeCount - 1);

    const StudioSim::EGameGenre selectedGenre = kProjectPitchGenres[pitchGenreIndex_];
    const StudioSim::EGameTheme selectedTheme = kProjectPitchThemes[pitchThemeIndex_];
    const StudioSim::EProjectSizeTier selectedSize = kProjectPitchSizes[pitchSizeIndex_];

    ImGui::TextUnformatted("选择下一款游戏项目");
    ImGui::Text("公司资金 %lld", static_cast<long long>(companyState_.funds));
    if (!companyState_.shipped.empty())
    {
        const StudioSim::FGameProject& lastProject = companyState_.shipped.back();
        ImGui::TextWrapped("上一作：《%s》 评分 %d  销量 %lld  利润 %lld", lastProject.name.c_str(),
                           lastProject.reviewScore, static_cast<long long>(lastProject.unitsSold),
                           static_cast<long long>(lastProject.profit));
    }
    ImGui::Separator();
    if (ImGui::BeginCombo("类型", GameGenreLabelZh(selectedGenre)))
    {
        for (int i = 0; i < kProjectPitchGenreCount; ++i)
        {
            const bool selected = i == pitchGenreIndex_;
            if (ImGui::Selectable(GameGenreLabelZh(kProjectPitchGenres[i]), selected))
            {
                pitchGenreIndex_ = i;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("题材", GameThemeLabelZh(selectedTheme)))
    {
        for (int i = 0; i < kProjectPitchThemeCount; ++i)
        {
            const bool selected = i == pitchThemeIndex_;
            if (ImGui::Selectable(GameThemeLabelZh(kProjectPitchThemes[i]), selected))
            {
                pitchThemeIndex_ = i;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("规模", ProjectSizeTierLabelZh(selectedSize)))
    {
        for (int i = 0; i < kProjectPitchSizeCount; ++i)
        {
            const bool selected = i == pitchSizeIndex_;
            if (ImGui::Selectable(ProjectSizeTierLabelZh(kProjectPitchSizes[i]), selected))
            {
                pitchSizeIndex_ = i;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    const StudioSim::FGameProject preview =
        BuildProjectFromPitch(selectedGenre, selectedTheme, selectedSize, companyState_.projectIndex);
    ImGui::Separator();
    ImGui::TextWrapped("《%s》 %s x %s", preview.name.c_str(), StudioSim::GameGenreName(preview.genre),
                       StudioSim::GameThemeName(preview.theme));
    ImGui::Text("工期 %d 天 | 预算 %lld | 契合 %.0f%%", preview.plannedDays,
                static_cast<long long>(preview.budget), preview.comboFit * 100.0f);
    ImGui::Text("目标 T/D/A/P %.0f / %.0f / %.0f / %.0f", preview.production.targetMeters.tech,
                preview.production.targetMeters.design, preview.production.targetMeters.art,
                preview.production.targetMeters.polish);
    for (const auto& highlight : preview.highlights)
    {
        ImGui::BulletText("%s", highlight.text.c_str());
    }
    ImGui::Spacing();
    if (ImGui::Button("开始研发"))
    {
        StartProjectPitch(selectedGenre, selectedTheme, selectedSize);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void StudioSimGameInstance::DrawGoalChoiceModal()
{
    const bool awaitingChoice = worldState_.phase == StudioSim::EDayPhase::Briefing &&
                                goalSystem_.State() == StudioSim::GoalSystem::EState::AwaitingChoice;
    if (awaitingChoice)
    {
        ImGui::OpenPopup("今日目标选择");
    }

    PrepareStudioSimModal(640.0f, 860.0f);
    if (!ImGui::BeginPopupModal("今日目标选择", nullptr, kStudioSimModalFlags))
    {
        return;
    }
    if (!awaitingChoice)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted("选择今天要推进的目标");
    ImGui::Separator();
    const auto& options = goalSystem_.Options();
    for (size_t i = 0; i < options.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextWrapped("%s", options[i].title.c_str());
        TextDisabledWrapped(options[i].description);
        if (ImGui::Button("选择此目标"))
        {
            goalSystem_.ChooseGoal(static_cast<int>(i), GetEngine().GetAIService(),
                                   employeeSystem_.EmployeesMutable());
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        ImGui::PopID();
    }

    const float customButtonWidth = ImGui::CalcTextSize("使用自定义").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float customInputWidth =
        std::max(260.0f, ImGui::GetContentRegionAvail().x - customButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(customInputWidth);
    ImGui::InputTextWithHint("##custom_goal", "自定义目标", customGoalBuf_, sizeof(customGoalBuf_));
    ImGui::SameLine();
    if (ImGui::Button("使用自定义", ImVec2(customButtonWidth, 0.0f)) && customGoalBuf_[0] != '\0')
    {
        goalSystem_.ChooseCustom(customGoalBuf_, GetEngine().GetAIService(), employeeSystem_.EmployeesMutable());
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void StudioSimGameInstance::DrawGatheringDecisionModal()
{
    const StudioSim::FGathering* pendingGathering = nullptr;
    for (const auto& gathering : gatheringSystem_.Gatherings())
    {
        if (gathering.awaitingConfirm && gathering.decision.valid)
        {
            pendingGathering = &gathering;
            break;
        }
    }
    if (pendingGathering != nullptr)
    {
        ImGui::OpenPopup("会议决策");
    }

    PrepareStudioSimModal(560.0f, 780.0f);
    if (!ImGui::BeginPopupModal("会议决策", nullptr, kStudioSimModalFlags))
    {
        return;
    }
    if (pendingGathering == nullptr)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextWrapped("会议：%s", pendingGathering->topic.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("决议：%s", pendingGathering->decision.summary.c_str());
    if (!pendingGathering->decision.focusMeter.empty())
    {
        ImGui::TextDisabled("集中补：%s", ProjectMeterLabelZh(pendingGathering->decision.focusMeter));
    }
    for (const auto& reassign : pendingGathering->decision.reassign)
    {
        ImGui::BulletText("%s → %s", reassign.first.c_str(), reassign.second.c_str());
    }
    ImGui::Spacing();

    const int gatheringId = pendingGathering->id;
    if (ImGui::Button("采纳"))
    {
        gatheringSystem_.AcceptDecision(gatheringId, worldState_.gameClockMinutes, employeeSystem_.EmployeesMutable(),
                                        productionSystem_);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("否决"))
    {
        gatheringSystem_.RejectDecision(gatheringId, worldState_.gameClockMinutes, employeeSystem_.EmployeesMutable());
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void StudioSimGameInstance::DrawReviewModal()
{
    const bool inReview = worldState_.phase == StudioSim::EDayPhase::Review;
    if (inReview)
    {
        ImGui::OpenPopup("当天复盘");
    }

    PrepareStudioSimModal(600.0f, 820.0f);
    if (!ImGui::BeginPopupModal("当天复盘", nullptr, kStudioSimModalFlags))
    {
        return;
    }
    if (!inReview)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    const bool launchReview = gameProject_.launched;
    ImGui::TextUnformatted(launchReview ? "上线结算" : "当天复盘");
    ImGui::Separator();
    if (launchReview)
    {
        ImGui::TextWrapped("《%s》 %s x %s", gameProject_.name.c_str(), GameGenreLabelZh(gameProject_.genre),
                           GameThemeLabelZh(gameProject_.theme));
        ImGui::Text("质量 %.0f/100 | 媒体评分 %d/40 | 销量 %lld", gameProject_.quality * 100.0f,
                    gameProject_.reviewScore, static_cast<long long>(gameProject_.unitsSold));
        if (!gameProject_.reviewerScores.empty())
        {
            std::string reviewerLine;
            for (size_t i = 0; i < gameProject_.reviewerScores.size(); ++i)
            {
                if (i > 0)
                {
                    reviewerLine += " / ";
                }
                reviewerLine += fmt::format("{}", gameProject_.reviewerScores[i]);
            }
            ImGui::TextWrapped("评委分：%s", reviewerLine.c_str());
        }
        ImGui::Text("营收 %lld | 成本 %lld | 利润 %lld", static_cast<long long>(gameProject_.revenue),
                    static_cast<long long>(gameProject_.cost), static_cast<long long>(gameProject_.profit));
        ImGui::Text("公司资金 %lld", static_cast<long long>(companyState_.funds));
        if (!gameProject_.reviewQuotes.empty())
        {
            ImGui::Separator();
            for (const auto& quote : gameProject_.reviewQuotes)
            {
                ImGui::BulletText("%s", quote.c_str());
            }
        }
        ImGui::Separator();
    }
    if (goalSystem_.Summary().empty())
    {
        ImGui::TextUnformatted(launchReview ? "结算点评生成中..." : "复盘生成中...");
    }
    else
    {
        ImGui::TextWrapped("%s", goalSystem_.Summary().c_str());
    }
    ImGui::Spacing();
    if (ImGui::Button(launchReview ? "开下一个项目" : "进入下一天"))
    {
        StartNextDay();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

bool StudioSimGameInstance::OnKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || !sceneReady_)
    {
        return false;
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
