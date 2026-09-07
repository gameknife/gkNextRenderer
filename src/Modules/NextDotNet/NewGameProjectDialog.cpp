#include "Modules/NextDotNet/NewGameProjectDialog.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/ManagedGameSession.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiContainers.hpp"
#include "Modules/NextUI/UI/UiScopes.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "Modules/NextUI/UI/UiWidgets.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <fmt/format.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace Modules::NextDotNet
{
    using NextUI::Foundation::Color;
    using NextUI::Foundation::ColorU32;
    using NextUI::Foundation::EColor;

    namespace
    {
        constexpr const char* kPopupTitle = "New Game Project";

        template <size_t N>
        void SetBuffer(std::array<char, N>& buffer, std::string_view value)
        {
            const size_t length = std::min(value.size(), N - 1);
            std::memcpy(buffer.data(), value.data(), length);
            buffer[length] = '\0';
        }

        const char* GetTemplateIcon(std::string_view id)
        {
            if (id == "blank")
            {
                return ICON_FA_CUBES;
            }
            if (id == "arcade2d")
            {
                return ICON_FA_PERSON_RUNNING;
            }
            if (id == "topdown3d")
            {
                return ICON_FA_SHIELD_HALVED;
            }
            if (id == "firstperson")
            {
                return ICON_FA_COMPASS;
            }
            if (id == "tps")
            {
                return ICON_FA_CROSSHAIRS;
            }
            return ICON_FA_GAMEPAD;
        }

        const char* GetTemplateCategory(std::string_view id)
        {
            if (id == "blank")
            {
                return "Minimal Starter";
            }
            if (id == "arcade2d")
            {
                return "2D Arcade";
            }
            if (id == "topdown3d")
            {
                return "Action Roguelike";
            }
            if (id == "firstperson")
            {
                return "3D First-Person";
            }
            if (id == "tps")
            {
                return "Shooter & Rig";
            }
            return "Game Template";
        }

        void DrawBadge(const char* icon, const char* label, ImVec4 bgCol, ImVec4 textCol, ImVec4 borderCol)
        {
            const std::string text =
                icon != nullptr && icon[0] != '\0' ? fmt::format("{} {}", icon, label) : std::string(label);
            const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const ImVec2 padding(8.0f, 3.0f);
            const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);

            const ImVec2 pos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(bgCol), 4.0f);
            drawList->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(borderCol), 4.0f, 0, 1.0f);
            drawList->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y), ImGui::GetColorU32(textCol), text.c_str());

            ImGui::Dummy(size);
        }
    }

    std::string FNewGameProjectDialog::UnavailableReason()
    {
        if (DotNetRuntime::ManagedSourceRoot().empty())
        {
            return "This build has no C# sources to write into (an installed build never does).";
        }
        if (GameTemplateRoot().empty())
        {
            return "No project templates found under assets/templates/games.";
        }
        return {};
    }

    void FNewGameProjectDialog::Open()
    {
        templates_ = ScanGameTemplates();
        unavailableReason_ = UnavailableReason();
        ResetForm();
        open_ = true;
        requestOpen_ = true;
    }

    void FNewGameProjectDialog::Close()
    {
        open_ = false;
        requestOpen_ = false;
        phase_ = EPhase::Editing;
    }

    void FNewGameProjectDialog::ResetForm()
    {
        phase_ = EPhase::Editing;
        selectedTemplate_ = 0;
        projectName_[0] = '\0';
        displayName_[0] = '\0';
        gameId_[0] = '\0';
        displayNameEdited_ = false;
        gameIdEdited_ = false;
        buildAfterCreate_ = true;
        validationError_.clear();
        buildError_.clear();
        built_ = false;
        result_ = {};
    }

    const FGameTemplate* FNewGameProjectDialog::SelectedTemplate() const
    {
        if (selectedTemplate_ < 0 || static_cast<size_t>(selectedTemplate_) >= templates_.size())
        {
            return nullptr;
        }
        return &templates_[static_cast<size_t>(selectedTemplate_)];
    }

    void FNewGameProjectDialog::SyncDerivedNames()
    {
        const std::string projectName = projectName_.data();
        if (!displayNameEdited_)
        {
            SetBuffer(displayName_, projectName);
        }
        if (!gameIdEdited_)
        {
            SetBuffer(gameId_, DeriveGameId(projectName));
        }
    }

    void FNewGameProjectDialog::PerformWork(ManagedGameSession* session)
    {
        const FGameTemplate* gameTemplate = SelectedTemplate();
        if (gameTemplate == nullptr)
        {
            validationError_ = "Please select a template.";
            phase_ = EPhase::Editing;
            return;
        }

        FNewGameRequest request;
        request.templateId = gameTemplate->id;
        request.projectName = projectName_.data();
        request.displayName = displayName_.data();
        request.gameId = gameId_.data();

        result_ = CreateManagedGame(*gameTemplate, request);
        if (!result_.created)
        {
            validationError_ = result_.error;
            phase_ = EPhase::Editing;
            return;
        }

        built_ = false;
        buildError_.clear();
        if (buildAfterCreate_ && session != nullptr)
        {
            built_ = session->RebuildGame(result_.manifest, buildError_);
        }
        phase_ = EPhase::Done;
    }

    FNewGameProjectOutcome FNewGameProjectDialog::Draw(ManagedGameSession* session)
    {
        FNewGameProjectOutcome outcome;
        if (!open_)
        {
            return outcome;
        }

        const bool completingWork = phase_ == EPhase::Working;
        if (completingWork)
        {
            PerformWork(session);
            if (phase_ == EPhase::Done)
            {
                outcome.created = true;
                outcome.gameId = result_.manifest.id;
                outcome.built = built_;
            }
        }

        if (requestOpen_)
        {
            requestOpen_ = false;
            ImGui::OpenPopup(kPopupTitle);
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->GetCenter().y), ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));

        const float targetWidth = std::min(840.0f, viewport->WorkSize.x - 32.0f);
        const float targetHeight = std::min(560.0f, viewport->WorkSize.y - 32.0f);
        ImGui::SetNextWindowSize(ImVec2(targetWidth, targetHeight), ImGuiCond_Appearing);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Surface, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Surface, 0.98f));

        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;

        bool stayOpen = true;
        if (!ImGui::BeginPopupModal(kPopupTitle, &stayOpen, windowFlags))
        {
            open_ = false;
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
            return outcome;
        }

        if (phase_ != EPhase::Working && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
            open_ = false;
            ImGui::EndPopup();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);
            return outcome;
        }

        ImFont* titleFont = nullptr;
        ImFont* defaultFont = ImGui::GetFont();
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            titleFont = NextUI::Theme::GetTitleFont(*engine);
            if (ImFont* df = NextUI::Theme::GetDefaultFont(*engine))
            {
                defaultFont = df;
            }
        }
        if (titleFont == nullptr)
        {
            titleFont = defaultFont;
        }

        const ImVec2 dialogPos = ImGui::GetWindowPos();
        const ImVec2 dialogSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // ==========================================
        // 1. Header Bar
        // ==========================================
        constexpr float kHeaderHeight = 52.0f;
        const ImVec2 headerMin = dialogPos;
        const ImVec2 headerMax(dialogPos.x + dialogSize.x, dialogPos.y + kHeaderHeight);

        drawList->AddRectFilled(headerMin, headerMax, ColorU32(EColor::Background, 0.65f), 8.0f,
                                ImDrawFlags_RoundCornersTop);
        drawList->AddLine(ImVec2(headerMin.x, headerMax.y), headerMax, ColorU32(EColor::Border, 0.50f), 1.0f);

        // Header Title and Icon
        const float headerPadX = 18.0f;
        const float headerPadY = 10.0f;
        const ImVec2 iconBoxMin(dialogPos.x + headerPadX, dialogPos.y + headerPadY);
        const float iconBoxSize = 32.0f;
        const ImVec2 iconBoxMax(iconBoxMin.x + iconBoxSize, iconBoxMin.y + iconBoxSize);

        drawList->AddRectFilled(iconBoxMin, iconBoxMax, ColorU32(EColor::Accent, 0.16f), 6.0f);
        drawList->AddRect(iconBoxMin, iconBoxMax, ColorU32(EColor::AccentHover, 0.40f), 6.0f);

        const ImVec2 headerIconSize = ImGui::CalcTextSize(ICON_FA_FOLDER_PLUS);
        drawList->AddText(
            ImVec2(iconBoxMin.x + (iconBoxSize - headerIconSize.x) * 0.5f,
                   iconBoxMin.y + (iconBoxSize - headerIconSize.y) * 0.5f),
            ColorU32(EColor::AccentHover), ICON_FA_FOLDER_PLUS);

        // Title text
        const float titleX = iconBoxMax.x + 12.0f;
        ImGui::PushFont(titleFont);
        drawList->AddText(ImVec2(titleX, dialogPos.y + 8.0f), ColorU32(EColor::Text), "Create New Game Project");
        ImGui::PopFont();

        // Subtitle text
        drawList->AddText(ImVec2(titleX, dialogPos.y + 29.0f), ColorU32(EColor::TextDim),
                          "Initialize a new .NET 9 managed C# gameplay project from a template");

        // Close X Button
        const float closeBtnSize = 26.0f;
        const ImVec2 closeBtnPos(dialogPos.x + dialogSize.x - closeBtnSize - 14.0f,
                                 dialogPos.y + (kHeaderHeight - closeBtnSize) * 0.5f);
        ImGui::SetCursorScreenPos(closeBtnPos);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.8f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceElevated));
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (ImGui::Button(ICON_FA_XMARK "##CloseDialog", ImVec2(closeBtnSize, closeBtnSize)) &&
            phase_ != EPhase::Working)
        {
            ImGui::CloseCurrentPopup();
            open_ = false;
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);

        // Set cursor below Header
        ImGui::SetCursorScreenPos(ImVec2(dialogPos.x, dialogPos.y + kHeaderHeight));

        // ==========================================
        // 2. Body Area
        // ==========================================
        constexpr float kFooterHeight = 56.0f;
        const float bodyHeight = dialogSize.y - kHeaderHeight - kFooterHeight;

        if (!unavailableReason_.empty())
        {
            // Unavailable view
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 16.0f));
            ImGui::BeginChild("##UnavailableBody", ImVec2(0.0f, bodyHeight), false);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 28.0f);

            ImGui::BeginGroup();
            const float centerX = ImGui::GetWindowWidth() * 0.5f;

            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Danger));
            ImGui::PushFont(titleFont);
            const char* unavTitle = ICON_FA_CIRCLE_EXCLAMATION " Cannot Create Project Here";
            const ImVec2 unavTitleSize = ImGui::CalcTextSize(unavTitle);
            ImGui::SetCursorPosX(centerX - unavTitleSize.x * 0.5f);
            ImGui::TextUnformatted(unavTitle);
            ImGui::PopFont();
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Spacing();

            const float panelWidth = std::min(600.0f, ImGui::GetContentRegionAvail().x - 40.0f);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - panelWidth) * 0.5f);
            if (NextUI::Theme::BeginInsetPanel("##UnavReasonBox", ImVec2(panelWidth, 100.0f), false, 0,
                                               ImVec2(16.0f, 16.0f), 0.40f))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
                ImGui::TextWrapped("%s", unavailableReason_.c_str());
                ImGui::PopStyleColor();
                NextUI::Theme::EndInsetPanel();
            }
            ImGui::EndGroup();

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        else if (phase_ == EPhase::Done)
        {
            // Done / Success view
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 16.0f));
            ImGui::BeginChild("##DoneBody", ImVec2(0.0f, bodyHeight), false);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16.0f);

            // Success Header Banner
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Success));
            ImGui::PushFont(titleFont);
            const std::string doneTitle =
                fmt::format(ICON_FA_CIRCLE_CHECK "  Project '{}' Created Successfully!", result_.manifest.id);
            ImGui::SetCursorPosX(24.0f);
            ImGui::TextUnformatted(doneTitle.c_str());
            ImGui::PopFont();
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::SetCursorPosX(24.0f);

            const float contentWidth = dialogSize.x - 48.0f;

            // Details Inset Panel
            if (NextUI::Theme::BeginInsetPanel("##DoneDetails", ImVec2(contentWidth, 140.0f), false, 0,
                                               ImVec2(16.0f, 12.0f), 0.35f))
            {
                ImGui::TextColored(Color(EColor::TextDim), ICON_FA_FOLDER "  Project Directory:");
                ImGui::SameLine(180.0f);
                ImGui::TextColored(Color(EColor::Text), "%s", result_.projectDirectory.string().c_str());

                ImGui::Spacing();
                ImGui::TextColored(Color(EColor::TextDim), ICON_FA_FILE_CODE "  Manifest Path:");
                ImGui::SameLine(180.0f);
                ImGui::TextColored(Color(EColor::Text), "%s", result_.manifestFile.string().c_str());

                ImGui::Spacing();
                ImGui::TextColored(Color(EColor::TextDim), ICON_FA_HAMMER "  Build Status:");
                ImGui::SameLine(180.0f);
                if (built_)
                {
                    ImGui::TextColored(Color(EColor::Success), ICON_FA_CHECK " Published & ready to play immediately");
                }
                else if (!buildError_.empty())
                {
                    ImGui::TextColored(Color(EColor::Warning), ICON_FA_TRIANGLE_EXCLAMATION " Not built yet: %s",
                                       buildError_.c_str());
                }
                else
                {
                    ImGui::TextColored(Color(EColor::TextMuted),
                                       "Created on disk. Build with 'Rebuild C#' or dotnet publish.");
                }

                NextUI::Theme::EndInsetPanel();
            }

            ImGui::Spacing();
            ImGui::SetCursorPosX(24.0f);

            // Guidance Inset Panel
            if (NextUI::Theme::BeginInsetPanel("##DoneGuidance", ImVec2(contentWidth, 110.0f), false, 0,
                                               ImVec2(16.0f, 12.0f), 0.20f))
            {
                ImGui::TextColored(Color(EColor::AccentHover), ICON_FA_LIGHTBULB "  Next Steps");
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
                ImGui::TextWrapped(
                    "1. Run 'gnb dotnet sln' to include the new project into 'assets/csharp/GkNextManaged.sln'.\n"
                    "2. Open the solution in your IDE (VS / Rider / VS Code) to get full intellisense and GkNext.Engine "
                    "support.\n"
                    "3. Launch or Play-In-Editor from gkNextLauncher or gkNextEditor.");
                ImGui::PopStyleColor();
                NextUI::Theme::EndInsetPanel();
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        else
        {
            // Standard Two-Column Editing View
            const bool working = phase_ == EPhase::Working;
            ImGui::BeginDisabled(working);

            // -----------------------------------------------------------------
            // Left Column: Templates Rail
            // -----------------------------------------------------------------
            constexpr float kLeftColumnWidth = 260.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(EColor::Background, 0.45f));
            ImGui::BeginChild("##TemplatesRail", ImVec2(kLeftColumnWidth, bodyHeight), false);

            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextDim));
            ImGui::Text(ICON_FA_LIST "  TEMPLATES (%zu)", templates_.size());
            ImGui::PopStyleColor();
            ImGui::Spacing();

            for (size_t i = 0; i < templates_.size(); ++i)
            {
                const FGameTemplate& tmpl = templates_[i];
                const bool isSelected = static_cast<int>(i) == selectedTemplate_;

                ImGui::PushID(static_cast<int>(i));

                const ImVec2 cardPos = ImGui::GetCursorScreenPos();
                const float cardWidth = ImGui::GetContentRegionAvail().x;
                constexpr float kCardHeight = 48.0f;
                const ImVec2 cardMax(cardPos.x + cardWidth, cardPos.y + kCardHeight);

                ImGui::InvisibleButton("##TemplateCard", ImVec2(cardWidth, kCardHeight));
                const bool isHovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked())
                {
                    selectedTemplate_ = static_cast<int>(i);
                }

                // Card background and border
                const ImU32 cardBg = isSelected ? ColorU32(EColor::SurfaceHover, 0.90f)
                                                : (isHovered ? ColorU32(EColor::SurfaceElevated, 0.70f)
                                                             : ColorU32(EColor::Surface, 0.35f));
                const ImU32 cardBorder = isSelected ? ColorU32(EColor::AccentHover, 0.85f)
                                                    : (isHovered ? ColorU32(EColor::BorderStrong, 0.60f)
                                                                 : ColorU32(EColor::Border, 0.30f));

                drawList->AddRectFilled(cardPos, cardMax, cardBg, 6.0f);
                drawList->AddRect(cardPos, cardMax, cardBorder, 6.0f, 0, isSelected ? 1.5f : 1.0f);

                // Left Accent Indicator Strip for selected item
                if (isSelected)
                {
                    drawList->AddRectFilled(ImVec2(cardPos.x + 2.0f, cardPos.y + 8.0f),
                                            ImVec2(cardPos.x + 5.0f, cardMax.y - 8.0f),
                                            ColorU32(EColor::AccentHover), 2.0f);
                }

                // Icon Box
                const float itemIconSize = 30.0f;
                const ImVec2 itemIconMin(cardPos.x + 10.0f, cardPos.y + (kCardHeight - itemIconSize) * 0.5f);
                const ImVec2 itemIconMax(itemIconMin.x + itemIconSize, itemIconMin.y + itemIconSize);
                const char* iconStr = GetTemplateIcon(tmpl.id);

                drawList->AddRectFilled(itemIconMin, itemIconMax,
                                        isSelected ? ColorU32(EColor::Accent, 0.22f)
                                                   : ColorU32(EColor::Background, 0.40f),
                                        5.0f);
                const ImVec2 iconTextSize = ImGui::CalcTextSize(iconStr);
                drawList->AddText(
                    ImVec2(itemIconMin.x + (itemIconSize - iconTextSize.x) * 0.5f,
                           itemIconMin.y + (itemIconSize - iconTextSize.y) * 0.5f),
                    isSelected ? ColorU32(EColor::Text) : ColorU32(EColor::TextMuted), iconStr);

                // Title and Subtitle Text
                const float textLeft = itemIconMax.x + 10.0f;
                const ImVec2 titlePos(textLeft, cardPos.y + 6.0f);
                drawList->AddText(titlePos, isSelected ? ColorU32(EColor::Text) : ColorU32(EColor::Text),
                                  tmpl.displayName.c_str());

                const ImVec2 subPos(textLeft, cardPos.y + 26.0f);
                drawList->AddText(subPos, ColorU32(EColor::TextDim), GetTemplateCategory(tmpl.id));

                ImGui::PopID();
                ImGui::Spacing();
            }

            if (templates_.empty())
            {
                ImGui::TextColored(Color(EColor::TextDim), "No templates available");
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            ImGui::SameLine(0.0f, 0.0f);

            // Vertical divider line between columns
            const ImVec2 dividerPos = ImGui::GetCursorScreenPos();
            drawList->AddLine(dividerPos, ImVec2(dividerPos.x, dividerPos.y + bodyHeight),
                              ColorU32(EColor::Border, 0.50f), 1.0f);

            // -----------------------------------------------------------------
            // Right Column: Template Overview & Configuration Form
            // -----------------------------------------------------------------
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
            ImGui::BeginChild("##FormContent", ImVec2(0.0f, bodyHeight), false);

            const FGameTemplate* selectedTmpl = SelectedTemplate();
            if (selectedTmpl != nullptr)
            {
                // Top: Template Overview Inset Panel
                if (NextUI::Theme::BeginInsetPanel("##TmplOverview", ImVec2(0.0f, 152.0f), false, 0,
                                                   ImVec2(14.0f, 10.0f), 0.25f))
                {
                    ImGui::BeginGroup();
                    ImGui::PushFont(titleFont);
                    ImGui::TextColored(Color(EColor::Text), "%s", selectedTmpl->displayName.c_str());
                    ImGui::PopFont();

                    ImGui::SameLine(0.0f, 12.0f);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 1.0f);
                    for (const std::string& mod : selectedTmpl->requiredModules)
                    {
                        DrawBadge(ICON_FA_CUBE, mod.c_str(), Color(EColor::SurfaceElevated, 0.85f),
                                  Color(EColor::AccentHover), Color(EColor::BorderStrong, 0.6f));
                        ImGui::SameLine(0.0f, 6.0f);
                    }
                    if (selectedTmpl->hotReload)
                    {
                        DrawBadge(ICON_FA_BOLT, "Hot Reload", Color(EColor::SurfaceElevated, 0.85f),
                                  Color(EColor::Success), Color(EColor::BorderStrong, 0.6f));
                    }
                    ImGui::EndGroup();

                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
                    ImGui::TextWrapped("%s", selectedTmpl->description.c_str());
                    ImGui::PopStyleColor();

                    if (!selectedTmpl->highlights.empty())
                    {
                        ImGui::Spacing();
                        for (const std::string& highlight : selectedTmpl->highlights)
                        {
                            ImGui::TextColored(Color(EColor::Success), ICON_FA_CIRCLE_CHECK);
                            ImGui::SameLine(0.0f, 8.0f);
                            ImGui::TextColored(Color(EColor::Text), "%s", highlight.c_str());
                        }
                    }

                    NextUI::Theme::EndInsetPanel();
                }

                ImGui::Spacing();

                // Bottom: Project Configuration Form
                ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextDim));
                ImGui::Text(ICON_FA_SLIDERS "  PROJECT CONFIGURATION");
                ImGui::PopStyleColor();
                ImGui::Spacing();

                // 1. Project Name (C# Identifier)
                ImGui::TextColored(Color(EColor::Text), ICON_FA_SIGNATURE "  Project Name");
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::TextColored(Color(EColor::TextDim), "(PascalCase C# Identifier)");

                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputTextWithHint("##ProjectName", "e.g. SpaceShooter", projectName_.data(), projectName_.size()))
                {
                    SyncDerivedNames();
                }
                ImGui::PopStyleVar(2);
                ImGui::TextColored(Color(EColor::TextDim),
                                   "Sets directory assets/csharp/<Name>/, namespace and class name.");

                ImGui::Spacing();

                // 2. Display Name & Game ID in two columns
                if (ImGui::BeginTable("##DualNameTable", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
                {
                    ImGui::TableNextColumn();
                    ImGui::TextColored(Color(EColor::Text), ICON_FA_TAG "  Display Name");
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputTextWithHint("##DisplayName", "e.g. Space Shooter", displayName_.data(), displayName_.size()))
                    {
                        displayNameEdited_ = true;
                    }
                    ImGui::PopStyleVar(2);
                    ImGui::TextColored(Color(EColor::TextDim), "Window & menu title");

                    ImGui::TableNextColumn();
                    ImGui::TextColored(Color(EColor::Text), ICON_FA_CODE "  Game ID");
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::InputTextWithHint("##GameId", "e.g. spaceshooter", gameId_.data(), gameId_.size()))
                    {
                        gameIdEdited_ = true;
                    }
                    ImGui::PopStyleVar(2);
                    ImGui::TextColored(Color(EColor::TextDim), "Manifest and publish directory (lowercase)");

                    ImGui::EndTable();
                }

                // 3. Publish Checkbox
                if (session != nullptr)
                {
                    ImGui::Spacing();
                    ImGui::PushStyleColor(ImGuiCol_CheckMark, Color(EColor::AccentHover));
                    ImGui::Checkbox(ICON_FA_ROCKET "  Publish project immediately (ready to play instantly)",
                                    &buildAfterCreate_);
                    ImGui::PopStyleColor();
                }

                // Real-time Request Validation
                FNewGameRequest request;
                request.templateId = selectedTmpl->id;
                request.projectName = projectName_.data();
                request.displayName = displayName_.data();
                request.gameId = gameId_.data();
                std::string error;
                const bool valid = ValidateNewGameRequest(request, error);
                validationError_ = valid || projectName_[0] == '\0' ? std::string() : error;

                if (!validationError_.empty())
                {
                    ImGui::Spacing();
                    const ImVec2 errMin = ImGui::GetCursorScreenPos();
                    const float errWidth = ImGui::GetContentRegionAvail().x;
                    const ImVec2 errMax(errMin.x + errWidth, errMin.y + 32.0f);

                    drawList->AddRectFilled(errMin, errMax, ColorU32(EColor::Danger, 0.15f), 5.0f);
                    drawList->AddRect(errMin, errMax, ColorU32(EColor::Danger, 0.55f), 5.0f);

                    drawList->AddText(
                        ImVec2(errMin.x + 10.0f, errMin.y + 7.0f), ColorU32(EColor::Danger),
                        fmt::format(ICON_FA_CIRCLE_EXCLAMATION "  {}", validationError_).c_str());
                    ImGui::Dummy(ImVec2(0.0f, 32.0f));
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();

            ImGui::EndDisabled();
        }

        // Set cursor to Footer position
        ImGui::SetCursorScreenPos(ImVec2(dialogPos.x, dialogPos.y + dialogSize.y - kFooterHeight));

        // ==========================================
        // 3. Footer Action Bar
        // ==========================================
        const ImVec2 footerMin(dialogPos.x, dialogPos.y + dialogSize.y - kFooterHeight);
        const ImVec2 footerMax(dialogPos.x + dialogSize.x, dialogPos.y + dialogSize.y);

        drawList->AddRectFilled(footerMin, footerMax, ColorU32(EColor::Background, 0.70f), 8.0f,
                                ImDrawFlags_RoundCornersBottom);
        drawList->AddLine(footerMin, ImVec2(footerMax.x, footerMin.y), ColorU32(EColor::Border, 0.50f), 1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 12.0f));
        ImGui::BeginChild("##FooterContent", ImVec2(0.0f, kFooterHeight), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if (!unavailableReason_.empty())
        {
            ImGui::SetCursorPosX(dialogSize.x - 140.0f);
            if (ImGui::Button("Close", ImVec2(120.0f, 32.0f)))
            {
                ImGui::CloseCurrentPopup();
                open_ = false;
            }
        }
        else if (phase_ == EPhase::Done)
        {
            ImGui::TextColored(Color(EColor::TextDim), ICON_FA_INFO "  You can close this dialog at any time.");
            ImGui::SameLine(dialogSize.x - 158.0f);
            if (ImGui::Button(ICON_FA_CHECK "  Done", ImVec2(140.0f, 32.0f)))
            {
                ImGui::CloseCurrentPopup();
                open_ = false;
            }
        }
        else if (phase_ == EPhase::Working)
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Warning));
            ImGui::Text(ICON_FA_ROTATE "  %s",
                        buildAfterCreate_ && session != nullptr
                            ? "Creating project files and publishing with .NET SDK... Please wait."
                            : "Creating project files on disk...");
            ImGui::PopStyleColor();
        }
        else
        {
            // Left side info
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            if (const FGameTemplate* tmpl = SelectedTemplate())
            {
                ImGui::TextColored(Color(EColor::TextDim), ICON_FA_CODE_BRANCH "  Template: %s • .NET 9.0",
                                   tmpl->id.c_str());
            }

            // Right side buttons
            const float btnWidth = 140.0f;
            const float cancelWidth = 90.0f;
            ImGui::SameLine(dialogSize.x - btnWidth - cancelWidth - 28.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);

            // Cancel Button (Ghost style)
            if (ImGui::Button("Cancel", ImVec2(cancelWidth, 32.0f)))
            {
                ImGui::CloseCurrentPopup();
                open_ = false;
            }

            ImGui::SameLine(0.0f, 10.0f);

            // Create Project Button (Primary Accent)
            const bool canCreate =
                SelectedTemplate() != nullptr && projectName_[0] != '\0' && validationError_.empty();

            ImGui::BeginDisabled(!canCreate);
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::AccentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::AccentHover, 0.8f));
            if (ImGui::Button(ICON_FA_PLUS "  Create Project", ImVec2(btnWidth, 32.0f)))
            {
                phase_ = EPhase::Working;
            }
            ImGui::PopStyleColor(3);
            ImGui::EndDisabled();
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::EndPopup();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        return outcome;
    }
}
