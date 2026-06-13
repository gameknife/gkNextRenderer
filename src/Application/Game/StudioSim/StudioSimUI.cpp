#include "StudioSimUI.h"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>

namespace StudioSim
{
    namespace
    {
        constexpr float kOfficeFov = 50.0f;
        constexpr double kBubbleFadeMinutes = 5.0;
        constexpr EGameGenre kProjectPitchGenres[] = {
            EGameGenre::RPG, EGameGenre::Action, EGameGenre::Simulation,
            EGameGenre::Puzzle, EGameGenre::Shooter, EGameGenre::Adventure,
        };
        constexpr EGameTheme kProjectPitchThemes[] = {
            EGameTheme::Fantasy, EGameTheme::SciFi, EGameTheme::Sports,
            EGameTheme::Romance, EGameTheme::Horror, EGameTheme::Daily,
        };
        constexpr EProjectSizeTier kProjectPitchSizes[] = {
            EProjectSizeTier::Small,
            EProjectSizeTier::Standard,
            EProjectSizeTier::Big,
        };
        constexpr ImGuiWindowFlags kHudFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
        constexpr ImGuiWindowFlags kModalFlags =
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

        float ModalWidth(float minWidth, float maxWidth)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float viewportWidth = viewport != nullptr ? viewport->WorkSize.x : 1280.0f;
            const float viewportLimit = std::max(360.0f, viewportWidth - 64.0f);
            const float maxAllowed = std::min(maxWidth, viewportLimit);
            const float minAllowed = std::min(minWidth, maxAllowed);
            return std::clamp(viewportWidth * 0.52f, minAllowed, maxAllowed);
        }

        void PrepareModal(float minWidth, float maxWidth)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float viewportHeight = viewport != nullptr ? viewport->WorkSize.y : 720.0f;
            const float width = ModalWidth(minWidth, maxWidth);
            const float maxHeight = std::max(260.0f, viewportHeight * 0.82f);
            ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, maxHeight));
        }

        void TextDisabledWrapped(const std::string& text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::PopStyleColor();
        }

        bool BeginHudPanel(const char* name, const glm::vec2& position, const glm::vec2& size)
        {
            ImGui::SetNextWindowPos(ImVec2(position.x, position.y), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(size.x, size.y), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.88f);
            return ImGui::Begin(name, nullptr, kHudFlags);
        }

        void DrawHudTitle(const char* title)
        {
            ImGui::TextColored(ImVec4(0.78f, 0.88f, 1.00f, 1.0f), "%s", title);
            ImGui::Separator();
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

        ImVec4 ToImVec4(const glm::vec4& color)
        {
            return ImVec4(color.r, color.g, color.b, color.a);
        }

        ImU32 ColorToImU32(const glm::vec3& color)
        {
            return IM_COL32(static_cast<int>(color.r * 255.0f), static_cast<int>(color.g * 255.0f),
                            static_cast<int>(color.b * 255.0f), 255);
        }

        ImU32 ColorToImU32(const glm::vec4& color)
        {
            const auto channel = [](float value)
            {
                return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            };
            return IM_COL32(channel(color.r), channel(color.g), channel(color.b), channel(color.a));
        }

        ImU32 CategoryColor(const std::string& category)
        {
            if (category == "desk") return IM_COL32(230, 150, 60, 255);
            if (category == "meet") return IM_COL32(70, 140, 230, 255);
            if (category == "pantry") return IM_COL32(220, 200, 80, 255);
            if (category == "lounge") return IM_COL32(190, 120, 210, 255);
            return IM_COL32(200, 200, 200, 255);
        }

        bool ProjectWorld(const glm::mat4& viewProjection, const ImVec2& viewportPosition,
                          const ImVec2& viewportSize, const glm::vec3& worldPosition, ImVec2& outScreen)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
            {
                return false;
            }
            outScreen = ImVec2(viewportPosition.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                               viewportPosition.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y);
            return true;
        }

        const char* ProjectStageLabelZh(EProjectStage stage)
        {
            switch (stage)
            {
            case EProjectStage::Planning: return "企划";
            case EProjectStage::Production: return "生产";
            case EProjectStage::Polish: return "打磨";
            case EProjectStage::Done: return "完成";
            default: return "?";
            }
        }

        const char* GameGenreLabelZh(EGameGenre genre)
        {
            switch (genre)
            {
            case EGameGenre::RPG: return "RPG";
            case EGameGenre::Action: return "动作";
            case EGameGenre::Simulation: return "模拟";
            case EGameGenre::Puzzle: return "解谜";
            case EGameGenre::Shooter: return "射击";
            case EGameGenre::Adventure: return "冒险";
            default: return "未知";
            }
        }

        const char* GameThemeLabelZh(EGameTheme theme)
        {
            switch (theme)
            {
            case EGameTheme::Fantasy: return "奇幻";
            case EGameTheme::SciFi: return "科幻";
            case EGameTheme::Sports: return "运动";
            case EGameTheme::Romance: return "恋爱";
            case EGameTheme::Horror: return "恐怖";
            case EGameTheme::Daily: return "日常";
            default: return "未知";
            }
        }

        const char* ProjectSizeTierLabelZh(EProjectSizeTier tier)
        {
            switch (tier)
            {
            case EProjectSizeTier::Small: return "小品";
            case EProjectSizeTier::Standard: return "标准";
            case EProjectSizeTier::Big: return "大作";
            default: return "标准";
            }
        }

        const char* ProjectMeterLabelZh(const std::string& meter)
        {
            if (meter == "tech") return "技术";
            if (meter == "design") return "玩法";
            if (meter == "art") return "美术";
            if (meter == "polish") return "品质";
            return "短板";
        }

        glm::vec4 ProjectStageColor(EProjectStage stage)
        {
            switch (stage)
            {
            case EProjectStage::Planning: return {0.78f, 0.78f, 0.78f, 1.0f};
            case EProjectStage::Production: return {0.20f, 0.60f, 1.00f, 1.0f};
            case EProjectStage::Polish: return {0.74f, 0.52f, 1.00f, 1.0f};
            case EProjectStage::Done: return {0.28f, 0.82f, 0.44f, 1.0f};
            default: return {0.92f, 0.92f, 0.92f, 1.0f};
            }
        }

        void DrawMeterProgress(const char* label, float value, float target, const glm::vec4& color)
        {
            const float ratio = target > 0.0f ? std::clamp(value / target, 0.0f, 1.0f) : 0.0f;
            const std::string overlay = fmt::format("{} {:.0f}/{:.0f}", label, value, target);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ToImVec4(color));
            ImGui::ProgressBar(ratio, ImVec2(-1.0f, 0.0f), overlay.c_str());
            ImGui::PopStyleColor();
        }
    }

    void StudioSimUI::DrawHud(const FHudContext& context)
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

        DrawStatusHud(context, {workPos.x + margin, workPos.y + margin}, {leftWidth, statusHeight});
        DrawProgressHud(context, {workPos.x + workSize.x - rightWidth - margin, workPos.y + margin},
                        {rightWidth, progressHeight});
        DrawEmployeeHud(context, {workPos.x + margin, workPos.y + workSize.y - bottomHeight - margin},
                        {employeeWidth, bottomHeight});
        DrawEventHud(context,
                     {workPos.x + workSize.x - rightWidth - margin,
                      workPos.y + workSize.y - bottomHeight - margin},
                     {rightWidth, bottomHeight});
    }

    void StudioSimUI::DrawModals(const FModalContext& context)
    {
        DrawProjectPitchModal(context);
        DrawGoalChoiceModal(context);
        DrawGatheringDecisionModal(context);
        DrawReviewModal(context);
    }

    void StudioSimUI::DrawProjectPitchModal(const FModalContext& context)
    {
        if (context.hasActiveGameProject || context.world.phase != EDayPhase::Briefing)
        {
            return;
        }

        ImGui::OpenPopup("游戏立项");
        PrepareModal(640.0f, 860.0f);
        if (!ImGui::BeginPopupModal("游戏立项", nullptr, kModalFlags))
        {
            return;
        }

        pitchGenreIndex_ = std::clamp(pitchGenreIndex_, 0, static_cast<int>(std::size(kProjectPitchGenres)) - 1);
        pitchThemeIndex_ = std::clamp(pitchThemeIndex_, 0, static_cast<int>(std::size(kProjectPitchThemes)) - 1);
        pitchSizeIndex_ = std::clamp(pitchSizeIndex_, 0, static_cast<int>(std::size(kProjectPitchSizes)) - 1);

        const EGameGenre selectedGenre = SelectedGenre();
        const EGameTheme selectedTheme = SelectedTheme();
        const EProjectSizeTier selectedSize = SelectedSize();

        ImGui::TextUnformatted("选择下一款游戏项目");
        ImGui::Text("公司资金 %lld", static_cast<long long>(context.company.funds));
        if (!context.company.shipped.empty())
        {
            const FGameProject& lastProject = context.company.shipped.back();
            ImGui::TextWrapped("上一作：《%s》 评分 %d  销量 %lld  利润 %lld", lastProject.name.c_str(),
                               lastProject.reviewScore, static_cast<long long>(lastProject.unitsSold),
                               static_cast<long long>(lastProject.profit));
        }
        ImGui::Separator();
        if (ImGui::BeginCombo("类型", GameGenreLabelZh(selectedGenre)))
        {
            for (int i = 0; i < static_cast<int>(std::size(kProjectPitchGenres)); ++i)
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
            for (int i = 0; i < static_cast<int>(std::size(kProjectPitchThemes)); ++i)
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
            for (int i = 0; i < static_cast<int>(std::size(kProjectPitchSizes)); ++i)
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

        if (context.buildProjectPreview)
        {
            const FGameProject preview = context.buildProjectPreview(selectedGenre, selectedTheme, selectedSize);
            ImGui::Separator();
            ImGui::TextWrapped("《%s》 %s x %s", preview.name.c_str(), GameGenreName(preview.genre),
                               GameThemeName(preview.theme));
            ImGui::Text("工期 %d 天 | 预算 %lld | 契合 %.0f%%", preview.plannedDays,
                        static_cast<long long>(preview.budget), preview.comboFit * 100.0f);
            ImGui::Text("目标 T/D/A/P %.0f / %.0f / %.0f / %.0f", preview.production.targetMeters.tech,
                        preview.production.targetMeters.design, preview.production.targetMeters.art,
                        preview.production.targetMeters.polish);
            for (const FHighlight& highlight : preview.highlights)
            {
                ImGui::BulletText("%s", highlight.text.c_str());
            }
        }
        ImGui::Spacing();
        if (ImGui::Button("开始研发") && context.startProject)
        {
            context.startProject(selectedGenre, selectedTheme, selectedSize);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void StudioSimUI::DrawGoalChoiceModal(const FModalContext& context)
    {
        const bool awaitingChoice =
            context.world.phase == EDayPhase::Briefing &&
            context.goalSystem.State() == GoalSystem::EState::AwaitingChoice;
        if (awaitingChoice)
        {
            ImGui::OpenPopup("今日目标选择");
        }

        PrepareModal(640.0f, 860.0f);
        if (!ImGui::BeginPopupModal("今日目标选择", nullptr, kModalFlags))
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
        const auto& options = context.goalSystem.Options();
        for (size_t i = 0; i < options.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextWrapped("%s", options[i].title.c_str());
            TextDisabledWrapped(options[i].description);
            if (ImGui::Button("选择此目标") && context.chooseGoal)
            {
                context.chooseGoal(static_cast<int>(i));
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            ImGui::PopID();
        }

        const float buttonWidth =
            ImGui::CalcTextSize("使用自定义").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float inputWidth =
            std::max(260.0f, ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputTextWithHint("##custom_goal", "自定义目标", customGoalBuffer_.data(),
                                 customGoalBuffer_.size());
        ImGui::SameLine();
        if (ImGui::Button("使用自定义", ImVec2(buttonWidth, 0.0f)) && customGoalBuffer_[0] != '\0' &&
            context.chooseCustomGoal)
        {
            context.chooseCustomGoal(customGoalBuffer_.data());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void StudioSimUI::DrawGatheringDecisionModal(const FModalContext& context)
    {
        const FGathering* pendingGathering = nullptr;
        for (const FGathering& gathering : context.gatheringSystem.Gatherings())
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

        PrepareModal(560.0f, 780.0f);
        if (!ImGui::BeginPopupModal("会议决策", nullptr, kModalFlags))
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
        if (ImGui::Button("采纳") && context.acceptGathering)
        {
            context.acceptGathering(gatheringId);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("否决") && context.rejectGathering)
        {
            context.rejectGathering(gatheringId);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void StudioSimUI::DrawReviewModal(const FModalContext& context)
    {
        const bool inReview = context.world.phase == EDayPhase::Review;
        if (inReview)
        {
            ImGui::OpenPopup("当天复盘");
        }

        PrepareModal(600.0f, 820.0f);
        if (!ImGui::BeginPopupModal("当天复盘", nullptr, kModalFlags))
        {
            return;
        }
        if (!inReview)
        {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        const bool launchReview = context.gameProject.launched;
        ImGui::TextUnformatted(launchReview ? "上线结算" : "当天复盘");
        ImGui::Separator();
        if (launchReview)
        {
            ImGui::TextWrapped("《%s》 %s x %s", context.gameProject.name.c_str(),
                               GameGenreLabelZh(context.gameProject.genre),
                               GameThemeLabelZh(context.gameProject.theme));
            ImGui::Text("质量 %.0f/100 | 媒体评分 %d/40 | 销量 %lld", context.gameProject.quality * 100.0f,
                        context.gameProject.reviewScore, static_cast<long long>(context.gameProject.unitsSold));
            if (!context.gameProject.reviewerScores.empty())
            {
                std::string reviewerLine;
                for (size_t i = 0; i < context.gameProject.reviewerScores.size(); ++i)
                {
                    if (i > 0)
                    {
                        reviewerLine += " / ";
                    }
                    reviewerLine += fmt::format("{}", context.gameProject.reviewerScores[i]);
                }
                ImGui::TextWrapped("评委分：%s", reviewerLine.c_str());
            }
            ImGui::Text("营收 %lld | 成本 %lld | 利润 %lld",
                        static_cast<long long>(context.gameProject.revenue),
                        static_cast<long long>(context.gameProject.cost),
                        static_cast<long long>(context.gameProject.profit));
            ImGui::Text("公司资金 %lld", static_cast<long long>(context.company.funds));
            if (!context.gameProject.reviewQuotes.empty())
            {
                ImGui::Separator();
                for (const std::string& quote : context.gameProject.reviewQuotes)
                {
                    ImGui::BulletText("%s", quote.c_str());
                }
            }
            ImGui::Separator();
        }
        if (context.goalSystem.Summary().empty())
        {
            ImGui::TextUnformatted(launchReview ? "结算点评生成中..." : "复盘生成中...");
        }
        else
        {
            ImGui::TextWrapped("%s", context.goalSystem.Summary().c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button(launchReview ? "开下一个项目" : "进入下一天") && context.startNextDay)
        {
            context.startNextDay();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void StudioSimUI::DrawOverlay(const glm::mat4& viewProjection, const OfficeMap& officeMap,
                                  const EmployeeSystem& employeeSystem,
                                  const FWorldState& worldState) const
    {
        if (!showOverlay_)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 viewportPosition = viewport->Pos;
        const ImVec2 viewportSize = viewport->Size;
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        ImVec2 screen;
        for (const FPointOfInterest& point : officeMap.Points())
        {
            if (ProjectWorld(viewProjection, viewportPosition, viewportSize,
                             point.worldPos + glm::vec3(0.0f, 0.9f, 0.0f), screen))
            {
                const ImU32 color = CategoryColor(point.category);
                drawList->AddCircleFilled(screen, 4.0f, color);
                drawList->AddText(ImVec2(screen.x + 6.0f, screen.y - 6.0f), color, point.name.c_str());
            }
        }

        for (const FEmployee& employee : employeeSystem.Employees())
        {
            if (!ProjectWorld(viewProjection, viewportPosition, viewportSize,
                              employee.position + glm::vec3(0.0f, 2.0f, 0.0f), screen))
            {
                continue;
            }

            const ImU32 color = ColorToImU32(employee.color);
            drawList->AddText(ImVec2(screen.x - 12.0f, screen.y - 8.0f), color, employee.displayName.c_str());
            const char* bubble = employee.decisionPending ? "..." : employee.bubbleText.c_str();
            if (bubble == nullptr || bubble[0] == '\0')
            {
                continue;
            }

            constexpr float bubbleMaxWidth = 240.0f;
            const ImVec2 padding(8.0f, 5.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(bubble, nullptr, false, bubbleMaxWidth);
            const ImVec2 textPosition(screen.x - textSize.x * 0.5f, screen.y + 8.0f);
            const ImVec2 bubbleMin(textPosition.x - padding.x, textPosition.y - padding.y);
            const ImVec2 bubbleMax(textPosition.x + textSize.x + padding.x,
                                   textPosition.y + textSize.y + padding.y);
            float alphaScale = 1.0f;
            if (employee.bubbleClearAt > worldState.gameClockMinutes)
            {
                const double remaining = employee.bubbleClearAt - worldState.gameClockMinutes;
                alphaScale = static_cast<float>(std::clamp(remaining / kBubbleFadeMinutes, 0.0, 1.0));
            }
            const auto alpha = [alphaScale](int value)
            {
                return static_cast<int>(static_cast<float>(value) * alphaScale);
            };
            drawList->AddRectFilled(bubbleMin, bubbleMax, IM_COL32(20, 24, 28, alpha(220)), 6.0f);
            drawList->AddRect(bubbleMin, bubbleMax, IM_COL32(255, 255, 255, alpha(70)), 6.0f);
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPosition,
                              IM_COL32(255, 255, 255, alpha(245)), bubble, nullptr, bubbleMaxWidth);
        }

        for (const FFloatingTextParticle& particle : floatingText_)
        {
            const float progress =
                particle.durationSeconds > 0.0f ? particle.ageSeconds / particle.durationSeconds : 1.0f;
            const glm::vec3 position = particle.worldPos + glm::vec3(0.0f, progress * 0.9f, 0.0f);
            if (!ProjectWorld(viewProjection, viewportPosition, viewportSize, position, screen))
            {
                continue;
            }

            glm::vec4 color = particle.color;
            color.a *= std::clamp(1.0f - progress, 0.0f, 1.0f);
            const ImVec2 textSize = ImGui::CalcTextSize(particle.text.c_str());
            const ImVec2 textPosition(screen.x - textSize.x * 0.5f, screen.y - 24.0f);
            drawList->AddText(ImVec2(textPosition.x + 1.0f, textPosition.y + 1.0f),
                              IM_COL32(0, 0, 0, 180), particle.text.c_str());
            drawList->AddText(textPosition, ColorToImU32(color), particle.text.c_str());
        }
    }

    void StudioSimUI::DrawStatusHud(const FHudContext& context, const glm::vec2& position,
                                    const glm::vec2& size)
    {
        if (!BeginHudPanel("##StudioSimStatusHud", position, size))
        {
            ImGui::End();
            return;
        }

        DrawHudTitle("工作日");
        int hour = 0;
        int minute = 0;
        MinutesToHHMM(context.world.gameClockMinutes, hour, minute);
        ImGui::Text("Day %d", context.world.dayIndex);
        ImGui::SameLine();
        ImGui::TextDisabled("%s  %02d:%02d", DayPhaseName(context.world.phase), hour, minute);
        ImGui::TextWrapped("%s", context.goalSystem.Goal().set ? context.goalSystem.Goal().title.c_str()
                                                               : "晨会准备中...");
        ImGui::SliderFloat("速度", &context.world.timeScale, 1.0f, 240.0f, "%.0f min/s");
        ImGui::Checkbox("暂停", &context.world.paused);
        if (context.awaitingPlayerDecision)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "等待玩家判断，游戏进度已暂停");
        }
        else if (context.playerDecisionFlowActive)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f), "会议决策中，游戏进度已暂停");
        }

        ImGui::Separator();
        ImGui::TextDisabled("scene %s  nav %s", context.sceneReady ? "ready" : "loading",
                            context.employeeSystem.NavReady() ? "ok" : "no");
        ImGui::TextDisabled("LLM %s  decisions %d  fallback %d",
                            context.scheduler.InFlight() ? "thinking" : "idle",
                            context.scheduler.DecisionsMade(), context.scheduler.FallbacksUsed());
        ImGui::End();
    }

    void StudioSimUI::DrawProgressHud(const FHudContext& context, const glm::vec2& position,
                                      const glm::vec2& size)
    {
        if (!BeginHudPanel("##StudioSimProgressHud", position, size))
        {
            ImGui::End();
            return;
        }

        DrawHudTitle("项目进度");
        if (context.gameProject.name.empty())
        {
            ImGui::TextWrapped("等待新项目立项...");
            ImGui::Text("公司资金 %lld", static_cast<long long>(context.company.funds));
            ImGui::Text("已发行 %zu 款", context.company.shipped.size());
            ImGui::End();
            return;
        }

        const int plannedDays = std::max(1, context.gameProject.plannedDays);
        const bool launched = context.gameProject.launched || context.gameProject.production.shipped;
        const int displayDay = launched
                                   ? std::clamp(context.gameProject.elapsedDays, 1, plannedDays)
                                   : std::clamp(context.gameProject.elapsedDays + 1, 1, plannedDays);
        ImGui::TextWrapped("《%s》 %s x %s", context.gameProject.name.c_str(),
                           GameGenreName(context.gameProject.genre), GameThemeName(context.gameProject.theme));
        const int daysLeft = launched ? 0 : std::max(0, plannedDays - context.gameProject.elapsedDays);
        ImGui::Text("%s %d/%d 天 | 剩余 %d 天 | 预算 %lld", launched ? "已上线" : "工期第",
                    displayDay, plannedDays, daysLeft, static_cast<long long>(context.gameProject.budget));
        ImGui::Text("公司资金 %lld", static_cast<long long>(context.company.funds));
        if (!context.gameProject.highlights.empty())
        {
            std::string highlights;
            for (const FHighlight& highlight : context.gameProject.highlights)
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
        if (!context.productionSystem.Active())
        {
            ImGui::TextWrapped("等待今日目标...");
            ImGui::End();
            return;
        }

        const FProjectState& project = context.productionSystem.State();
        ImGui::TextUnformatted("阶段:");
        ImGui::SameLine();
        ImGui::TextColored(ToImVec4(ProjectStageColor(project.stage)), "%s",
                           ProjectStageLabelZh(project.stage));
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

    void StudioSimUI::DrawEmployeeHud(const FHudContext& context, const glm::vec2& position,
                                      const glm::vec2& size)
    {
        if (!BeginHudPanel("##StudioSimEmployeeHud", position, size))
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

            for (const FEmployee& employee : context.employeeSystem.Employees())
            {
                const std::string target = employee.decisionPending
                                               ? "thinking..."
                                               : (employee.targetPoi.empty() ? "(idle)" : employee.targetPoi);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImColor(ColorToImU32(employee.color)), "%s", employee.displayName.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(RoleName(employee.role));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(MoodName(employee.mood));
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", target.c_str());
                if (!employee.bubbleText.empty())
                {
                    ImGui::TextDisabled("%s", employee.bubbleText.c_str());
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%.0f / %.0f / %.0f / %.0f", employee.myContribution.tech,
                            employee.myContribution.design, employee.myContribution.art,
                            employee.myContribution.polish);
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }

    void StudioSimUI::DrawEventHud(const FHudContext& context, const glm::vec2& position,
                                   const glm::vec2& size)
    {
        if (!BeginHudPanel("##StudioSimEventHud", position, size))
        {
            ImGui::End();
            return;
        }

        DrawHudTitle("事件 / 会议");
        if (context.meetingActive)
        {
            ImGui::TextWrapped("Meeting: %s", context.meetingTopic.c_str());
        }
        for (const FGathering& gathering : context.gatheringSystem.Gatherings())
        {
            ImGui::TextWrapped("%s #%d: %s", gathering.kind == EGatheringKind::Meeting ? "Meeting" : "Pantry",
                               gathering.id, gathering.topic.c_str());
            if (gathering.awaitingConfirm && gathering.decision.valid)
            {
                ImGui::TextDisabled("等待会议决策弹窗");
            }
        }

        if (context.world.todaysEvents.empty())
        {
            ImGui::TextDisabled("今日暂无随机事件");
        }
        else
        {
            ImGui::Text("Mood: %s", context.world.globalMood.c_str());
            for (const FWorldEvent& event : context.world.todaysEvents)
            {
                ImGui::BulletText("%s", event.title.c_str());
            }
        }

        ImGui::Separator();
        for (const FEventDef& definition : context.eventSystem.Catalog())
        {
            ImGui::PushID(definition.id.c_str());
            if (ImGui::Button(definition.title.c_str(), ImVec2(-1.0f, 0.0f)) && context.raiseEvent)
            {
                context.raiseEvent(definition.id);
            }
            ImGui::PopID();
        }

        ImGui::Separator();
        ImGui::Checkbox("Show overlay", &showOverlay_);
        ImGui::TextDisabled("nodes %zu  POIs %zu  employees %zu", context.sceneNodeCount,
                            context.officeMap.Count(), context.employeeSystem.Count());
        ImGui::End();
    }

    void StudioSimUI::CollectProductionVisualEvents(ProductionSystem& productionSystem)
    {
        for (const FProductionVisualEvent& event : productionSystem.ConsumeVisualEvents())
        {
            FFloatingTextParticle particle;
            particle.worldPos = event.worldPos;
            particle.text = event.text;
            particle.color = MeterColor(event.meter);
            floatingText_.push_back(std::move(particle));
        }
    }

    void StudioSimUI::Tick(double deltaSeconds)
    {
        for (FFloatingTextParticle& particle : floatingText_)
        {
            particle.ageSeconds += static_cast<float>(deltaSeconds);
        }
        floatingText_.erase(
            std::remove_if(floatingText_.begin(), floatingText_.end(),
                           [](const FFloatingTextParticle& particle)
                           {
                               return particle.ageSeconds >= particle.durationSeconds;
                           }),
            floatingText_.end());
    }

    void StudioSimUI::Reset()
    {
        floatingText_.clear();
        ResetGoalInput();
        ResetProjectPitchSelection();
    }

    void StudioSimUI::ResetProjectPitchSelection()
    {
        pitchGenreIndex_ = 0;
        pitchThemeIndex_ = 0;
        pitchSizeIndex_ = 1;
    }

    void StudioSimUI::ResetGoalInput()
    {
        customGoalBuffer_.fill('\0');
    }

    EGameGenre StudioSimUI::SelectedGenre() const
    {
        return kProjectPitchGenres[std::clamp(pitchGenreIndex_, 0,
                                               static_cast<int>(std::size(kProjectPitchGenres)) - 1)];
    }

    EGameTheme StudioSimUI::SelectedTheme() const
    {
        return kProjectPitchThemes[std::clamp(pitchThemeIndex_, 0,
                                               static_cast<int>(std::size(kProjectPitchThemes)) - 1)];
    }

    EProjectSizeTier StudioSimUI::SelectedSize() const
    {
        return kProjectPitchSizes[std::clamp(pitchSizeIndex_, 0,
                                              static_cast<int>(std::size(kProjectPitchSizes)) - 1)];
    }
}
