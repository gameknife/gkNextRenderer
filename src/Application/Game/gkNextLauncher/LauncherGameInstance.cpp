#include "LauncherGameInstance.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/AgentQueries.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string_view>

using Modules::NextDotNet::EGameSessionState;
using Modules::NextDotNet::FManagedGameManifest;
using NextUI::Foundation::EColor;
using NextUI::Foundation::Color;
using NextUI::Foundation::ColorU32;

namespace
{
    /// Every native module this executable links, in the form manifests declare them. The launcher
    /// is the union of what its games need; anything outside this list cannot be acquired at
    /// runtime, which is why a manifest asking for it is refused rather than half-loaded.
    const std::vector<std::string> kLinkedModules = {
        "DevTools",      "GltfLoader",  "LDrawLoader",   "LiveCoding",     "NextAudio",
        "NextCapture",   "NextDotNet",  "NextFidelityFX", "NextPhysics",   "NextRemote",
        "NextStreamline", "NextTemporalUpscaler", "NextValidation", "NextUI",
        // Not a module under src/Modules, but the same question a manifest is asking: was this
        // host linked with it? Without it Rig.* does nothing and a game built on characters
        // renders an empty world.
        "NextGameplay",
        "ScadLoader",    "SceneContent", "SplatLoader",
    };

    const char* StateName(EGameSessionState state)
    {
        switch (state)
        {
        case EGameSessionState::Idle: return "Idle";
        case EGameSessionState::Loading: return "Loading";
        case EGameSessionState::Playing: return "Playing";
        case EGameSessionState::Unloading: return "Unloading";
        }
        return "Idle";
    }

    const char* GetGameIcon(std::string_view id)
    {
        if (id == "brotato3d")
        {
            return ICON_FA_SHIELD_HALVED;
        }
        if (id == "flappy")
        {
            return ICON_FA_FEATHER;
        }
        if (id == "sandbox")
        {
            return ICON_FA_CUBES;
        }
        return ICON_FA_GAMEPAD;
    }

    const char* GetGameSubtitle(std::string_view id)
    {
        if (id == "brotato3d")
        {
            return "Action Roguelike • High-density 3D arena with massive physics & node pooling";
        }
        if (id == "flappy")
        {
            return "Arcade Classic • Hot-reloadable C# gameplay, procedural pipes & audio";
        }
        if (id == "sandbox")
        {
            return "CoreCLR Playground • Interactive managed binding layer & minimal testbed";
        }
        return "Managed .NET 9 Game Module";
    }

    void DrawPillBadge(const char* icon, const char* label, ImVec4 bgCol, ImVec4 textCol, ImVec4 borderCol)
    {
        const std::string text = icon != nullptr && icon[0] != '\0' ? fmt::format("{} {}", icon, label) : std::string(label);
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

LauncherGameInstance::LauncherGameInstance(Vulkan::WindowConfig& config,
                                           Runtime::Config::Options& options,
                                           NextEngine* engine)
    : ManagedGameHostInstance(config,
                              options,
                              engine,
                              Modules::NextDotNet::FManagedGameHostOptions{
                                  .manifestPath = {},
                                  .window = {.title = "gkNextLauncher", .width = 860, .height = 540, .forceSDR = true},
                                  .linkedModules = kLinkedModules,
                              })
{
}

void LauncherGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    cvars.RegisterString(
        "game.select", "", &selectedGameId_, NextCVar::ECVarFlags::None,
        "Managed game to run: a game id from assets/configs/games, or empty to return to the menu",
        [this]()
        {
            pendingSelection_ = selectedGameId_;
            hasPendingSelection_ = true;
        });

    // A control channel for the console and for scripted validation, which cannot click a card.
    cvars.RegisterString("game.newProject", "", &newProjectRequest_, NextCVar::ECVarFlags::None,
                         "Set to 1 to open the new-project dialog, or 0 to close it",
                         [this]()
                         {
                             if (newProjectRequest_ == "0" || newProjectRequest_.empty())
                             {
                                 newProjectDialog_.Close();
                             }
                             else
                             {
                                 newProjectDialog_.Open();
                             }
                         });
}

void LauncherGameInstance::OnInit()
{
    ManagedGameHostInstance::OnInit();

    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetUserSettings().ShowOverlay = false;

    GetSession().SetBaselineExcludedCVars({"game.select", "game.newProject"});

    RefreshEntries();

    GetSession().SetCloseRequestHandler(
        [this]() -> bool
        {
            GetSession().RequestUnload();
            return true;
        });

    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void LauncherGameInstance::RefreshEntries()
{
    entries_.clear();

    for (FManagedGameManifest& manifest :
         Modules::NextDotNet::ScanManagedGameManifests(Modules::NextDotNet::kManagedGameManifestDirectory))
    {
        FEntry entry;
        entry.manifest = std::move(manifest);

        if (std::string missing; !GetSession().AreRequirementsMet(entry.manifest, &missing))
        {
            entry.unavailableReason = "needs " + missing;
        }
        else
        {
            const std::filesystem::path assemblyPath =
                NextRenderer::GetExecutableDirectory() / "csharp" / entry.manifest.assembly;
            std::error_code ec;
            if (!std::filesystem::exists(assemblyPath, ec))
            {
                entry.unavailableReason = "not built (" + entry.manifest.assembly + ")";
            }
        }

        entry.available = entry.unavailableReason.empty();
        entries_.push_back(std::move(entry));
    }

    highlightedIndex_ = 0;
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].available)
        {
            highlightedIndex_ = static_cast<int>(i);
            break;
        }
    }

    SPDLOG_INFO("[launcher] {} managed game(s) found", entries_.size());
}

void LauncherGameInstance::OnTick(double deltaSeconds)
{
    ManagedGameHostInstance::OnTick(deltaSeconds);
    ApplyPendingRebuild();
    ApplyPendingSelection();

    if (!hasPendingSelection_)
    {
        const auto* active = GetSession().GetActiveManifest();
        selectedGameId_ = active != nullptr ? active->id : std::string();
    }
}

void LauncherGameInstance::ApplyPendingSelection()
{
    if (!hasPendingSelection_)
    {
        return;
    }
    if (GetSession().GetState() == EGameSessionState::Loading ||
        GetSession().GetState() == EGameSessionState::Unloading)
    {
        return;
    }

    const std::string requested = pendingSelection_;
    hasPendingSelection_ = false;

    if (requested.empty())
    {
        GetSession().RequestUnload();
        return;
    }

    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&requested](const FEntry& entry) { return entry.manifest.id == requested; });
    if (it == entries_.end())
    {
        SPDLOG_ERROR("[launcher] no managed game with id '{}'", requested);
        return;
    }
    LoadEntry(static_cast<size_t>(std::distance(entries_.begin(), it)));
}

void LauncherGameInstance::ApplyPendingRebuild()
{
    if (pendingRebuildIndex_ < 0 || static_cast<size_t>(pendingRebuildIndex_) >= entries_.size())
    {
        pendingRebuildIndex_ = -1;
        return;
    }

    const FEntry& entry = entries_[static_cast<size_t>(pendingRebuildIndex_)];
    pendingRebuildIndex_ = -1;

    std::string error;
    if (GetSession().RebuildGame(entry.manifest, error))
    {
        rebuildStatus_ = "rebuilt " + entry.manifest.id;
        RefreshEntries();
    }
    else
    {
        rebuildStatus_ = "rebuild failed: " + error;
        SPDLOG_ERROR("[launcher] {}", rebuildStatus_);
    }
}

void LauncherGameInstance::LoadEntry(size_t index)
{
    if (index >= entries_.size())
    {
        return;
    }
    const FEntry& entry = entries_[index];
    if (!entry.available)
    {
        SPDLOG_ERROR("[launcher] '{}' is unavailable: {}", entry.manifest.id, entry.unavailableReason);
        return;
    }
    GetSession().RequestLoad(entry.manifest);
}

bool LauncherGameInstance::OnGameRequestedClose()
{
    GetSession().RequestUnload();
    return true;
}

bool LauncherGameInstance::OnHostKey(SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN)
    {
        return false;
    }

    // The dialog is a modal: while it is up, Enter belongs to it, not to the grid behind it.
    if (newProjectDialog_.IsOpen())
    {
        return false;
    }

    if (GetSession().IsPlaying())
    {
        if (event.key.key == SDLK_ESCAPE)
        {
            GetSession().RequestUnload();
            return true;
        }
        return false;
    }

    constexpr int kGridColumns = 3;
    // The trailing "new project" cell is navigable like any other, so the keyboard can reach it.
    const int total = static_cast<int>(entries_.size()) + 1;

    switch (event.key.key)
    {
    case SDLK_LEFT:
        highlightedIndex_ = (highlightedIndex_ + total - 1) % total;
        return true;
    case SDLK_RIGHT:
        highlightedIndex_ = (highlightedIndex_ + 1) % total;
        return true;
    case SDLK_UP:
        if (highlightedIndex_ - kGridColumns >= 0)
        {
            highlightedIndex_ -= kGridColumns;
        }
        else
        {
            int target = highlightedIndex_ + (total / kGridColumns) * kGridColumns;
            if (target >= total) target -= kGridColumns;
            if (target >= 0 && target < total) highlightedIndex_ = target;
        }
        return true;
    case SDLK_DOWN:
        if (highlightedIndex_ + kGridColumns < total)
        {
            highlightedIndex_ += kGridColumns;
        }
        else
        {
            highlightedIndex_ = highlightedIndex_ % kGridColumns;
        }
        return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (highlightedIndex_ == NewProjectCellIndex())
        {
            newProjectDialog_.Open();
        }
        else
        {
            LoadEntry(static_cast<size_t>(highlightedIndex_));
        }
        return true;
    default:
        return false;
    }
}

bool LauncherGameInstance::OnHostRenderUI()
{
    if (GetSession().IsPlaying())
    {
        return false;
    }
    DrawMenu();
    return true;
}

void LauncherGameInstance::DrawMenu()
{
    NextUI::Foundation::ApplyTheme();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 20.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Background, 0.98f));

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##gkNextLauncherMain", nullptr, windowFlags);

    // Subtle background ambient glow decoration
    ImDrawList* bgDrawList = ImGui::GetWindowDrawList();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();

    // Subtle soft glows
    bgDrawList->AddCircleFilled(ImVec2(winPos.x + 120.0f, winPos.y + 60.0f), 240.0f, ColorU32(EColor::Accent, 0.035f), 32);
    bgDrawList->AddCircleFilled(ImVec2(winPos.x + winSize.x - 120.0f, winPos.y + winSize.y - 60.0f), 220.0f, ColorU32(EColor::Brand, 0.030f), 32);

    ImFont* titleFont = NextUI::Theme::GetTitleFont(GetEngine());
    ImFont* defaultFont = NextUI::Theme::GetDefaultFont(GetEngine());
    if (defaultFont == nullptr)
    {
        defaultFont = ImGui::GetFont();
    }
    if (titleFont == nullptr)
    {
        titleFont = defaultFont;
    }

    // ==========================================
    // 1. Minimal Header Bar
    // ==========================================
    ImGui::BeginGroup();
    {
        ImGui::PushFont(titleFont);
        ImGui::TextColored(Color(EColor::Text), "gkNext Launcher");
        ImGui::PopFont();

        const std::string countStr = fmt::format("{} Games", entries_.size());
        const float refreshWidth = 28.0f;
        const float newWidth = 108.0f;
        const float countWidth = ImGui::CalcTextSize(countStr.c_str()).x;
        const float rightMargin = countWidth + refreshWidth + newWidth + 26.0f;

        if (ImGui::GetContentRegionAvail().x > rightMargin)
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f - rightMargin);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
            ImGui::TextColored(Color(EColor::TextDim), "%s", countStr.c_str());

            ImGui::SameLine(0.0f, 10.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
            if (ImGui::Button(ICON_FA_ROTATE, ImVec2(refreshWidth, 24.0f)))
            {
                RefreshEntries();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Refresh game manifests");
            }

            ImGui::SameLine(0.0f, 10.0f);
            if (ImGui::Button(ICON_FA_PLUS " New Project", ImVec2(newWidth, 24.0f)))
            {
                newProjectDialog_.Open();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Create a C# game project from a template");
            }
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();
    NextUI::Theme::DrawThinSeparator(0.40f);
    ImGui::Spacing();

    // Leak Alert Warning
    if (GetSession().IsLeaking())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Danger));
        ImGui::Text(ICON_FA_TRIANGLE_EXCLAMATION " Previous game sessions were not fully collected. Please restart gkNextLauncher.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // ==========================================
    // 2. Responsive Grid Tiles
    // ==========================================
    const float footerHeight = 36.0f;
    // Scrollable: with enough games installed the grid outgrows the window, and a clipped row is
    // indistinguishable from a game that failed to appear.
    ImGui::BeginChild("##GameCardsGrid", ImVec2(0.0f, -footerHeight), false);

    if (entries_.empty())
    {
        ImGui::Spacing();
        ImGui::TextColored(Color(EColor::TextMuted),
                           ICON_FA_TRIANGLE_EXCLAMATION " No managed games found under %s. Start one below.",
                           Modules::NextDotNet::kManagedGameManifestDirectory);
        ImGui::Spacing();
    }

    {
        constexpr int kGridCols = 3;
        constexpr float kCardRounding = 8.0f;
        constexpr float kCellPaddingY = 8.0f;

        // Sized to the rows that actually have to fit, rather than to a fixed height per count:
        // with a "new project" cell always present, a fixed 230 put the second row half below the
        // window and clipped it away.
        const int totalCells = static_cast<int>(entries_.size()) + 1;
        const int rowCount = std::max(1, (totalCells + kGridCols - 1) / kGridCols);
        const float availableHeight = ImGui::GetContentRegionAvail().y;
        const float cardHeight =
            std::clamp((availableHeight - rowCount * kCellPaddingY * 2.0f) / static_cast<float>(rowCount),
                       150.0f, 330.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 8.0f));
        if (ImGui::BeginTable("##GameGridTable", kGridCols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings))
        {
            for (size_t i = 0; i < entries_.size(); ++i)
            {
                const FEntry& entry = entries_[i];
                const bool isHighlighted = static_cast<int>(i) == highlightedIndex_;
                const bool canRebuild = !entry.manifest.project.empty();

                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));

                const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                const float cardWidth = ImGui::GetContentRegionAvail().x;
                const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);

                // Entire card is an interactive hit target
                ImGui::InvisibleButton("##TileHit", ImVec2(cardWidth, cardHeight));
                const bool isTileHovered = ImGui::IsItemHovered();
                const bool isTileClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const ImVec2 mousePos = ImGui::GetIO().MousePos;

                // Rebuild button hit region (top-right 32x32)
                const ImVec2 rbMin(cardMax.x - 34.0f, cardMin.y + 6.0f);
                const ImVec2 rbMax(cardMax.x - 6.0f, cardMin.y + 34.0f);
                const bool isRbHovered = canRebuild && (mousePos.x >= rbMin.x && mousePos.x <= rbMax.x &&
                                                        mousePos.y >= rbMin.y && mousePos.y <= rbMax.y);

                if (isTileHovered && !isRbHovered)
                {
                    highlightedIndex_ = static_cast<int>(i);
                }

                if (isTileClicked)
                {
                    if (isRbHovered)
                    {
                        pendingRebuildIndex_ = static_cast<int>(i);
                        rebuildStatus_ = "Rebuilding " + entry.manifest.id + "...";
                    }
                    else if (entry.available)
                    {
                        LoadEntry(i);
                    }
                }

                // Draw Tile Background & Border
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 bgCol = isHighlighted ? ColorU32(EColor::SurfaceHover, 0.90f)
                                                  : (isTileHovered ? ColorU32(EColor::SurfaceElevated, 0.82f)
                                                                   : ColorU32(EColor::Surface, 0.58f));
                const ImU32 borderCol = isHighlighted ? ColorU32(EColor::AccentHover, 0.85f)
                                                      : (isTileHovered ? ColorU32(EColor::BorderStrong, 0.70f)
                                                                       : ColorU32(EColor::Border, 0.35f));

                drawList->AddRectFilled(cardMin, cardMax, bgCol, kCardRounding);
                drawList->AddRect(cardMin, cardMax, borderCol, kCardRounding, 0, isHighlighted ? 1.5f : 1.0f);

                // --- 1. Rebuild icon on top-right ---
                if (canRebuild)
                {
                    if (isRbHovered)
                    {
                        drawList->AddRectFilled(rbMin, rbMax, ColorU32(EColor::SurfaceHover, 0.9f), 4.0f);
                        ImGui::SetTooltip("Rebuild C# Project");
                    }
                    const ImVec2 hammerSize = ImGui::CalcTextSize(ICON_FA_HAMMER);
                    const ImVec2 hammerPos(rbMin.x + (28.0f - hammerSize.x) * 0.5f,
                                           rbMin.y + (28.0f - hammerSize.y) * 0.5f);
                    drawList->AddText(hammerPos, isRbHovered ? ColorU32(EColor::Text) : ColorU32(EColor::TextDim),
                                      ICON_FA_HAMMER);
                }

                // --- 2. Center Icon with Circular Badge ---
                const char* iconStr = GetGameIcon(entry.manifest.id);
                const float iconBoxSize = cardHeight <= 240.0f ? 54.0f : 68.0f;
                const float iconAreaY = cardMin.y + (cardHeight <= 240.0f ? 24.0f : 40.0f);
                const ImVec2 iconBoxMin(cardMin.x + (cardWidth - iconBoxSize) * 0.5f, iconAreaY);
                const ImVec2 iconBoxMax(iconBoxMin.x + iconBoxSize, iconBoxMin.y + iconBoxSize);

                const ImU32 iconBoxBg = isHighlighted ? ColorU32(EColor::Accent, 0.18f) : ColorU32(EColor::Background, 0.45f);
                drawList->AddRectFilled(iconBoxMin, iconBoxMax, iconBoxBg, iconBoxSize * 0.5f);
                drawList->AddRect(iconBoxMin, iconBoxMax, isHighlighted ? ColorU32(EColor::AccentHover, 0.5f) : ColorU32(EColor::Border, 0.35f), iconBoxSize * 0.5f);

                ImGui::PushFont(titleFont);
                const ImVec2 iconSize = ImGui::CalcTextSize(iconStr);
                const ImVec2 iconPos(iconBoxMin.x + (iconBoxSize - iconSize.x) * 0.5f,
                                     iconBoxMin.y + (iconBoxSize - iconSize.y) * 0.5f);
                const ImU32 iconColor = entry.available ? (isHighlighted ? ColorU32(EColor::Text) : ColorU32(EColor::TextMuted))
                                                        : ColorU32(EColor::TextDim);
                drawList->AddText(iconPos, iconColor, iconStr);
                ImGui::PopFont();

                // --- 3. Title Text (Centered) ---
                const float titleY = iconBoxMax.y + (cardHeight <= 240.0f ? 16.0f : 26.0f);
                ImGui::PushFont(titleFont);
                const ImVec2 titleSize = ImGui::CalcTextSize(entry.manifest.displayName.c_str());
                const ImVec2 titlePos(cardMin.x + (cardWidth - titleSize.x) * 0.5f, titleY);
                const ImU32 titleColor = entry.available ? (isHighlighted ? ColorU32(EColor::Text) : ColorU32(EColor::Text))
                                                        : ColorU32(EColor::TextDim);
                drawList->AddText(titlePos, titleColor, entry.manifest.displayName.c_str());
                ImGui::PopFont();

                // --- 4. Subtitle / Genre (Centered) ---
                const float subY = titleY + titleSize.y + 8.0f;
                std::string subtitle = GetGameSubtitle(entry.manifest.id);
                if (const size_t dotPos = subtitle.find("•"); dotPos != std::string::npos)
                {
                    subtitle = subtitle.substr(0, dotPos - 1);
                }
                const ImVec2 subSize = ImGui::CalcTextSize(subtitle.c_str());
                const ImVec2 subPos(cardMin.x + (cardWidth - subSize.x) * 0.5f, subY);
                drawList->AddText(subPos, ColorU32(EColor::TextDim), subtitle.c_str());

                // --- 5. Launch Button Area at Tile Bottom (Focused/Hovered) ---
                if (isHighlighted || isTileHovered)
                {
                    const float btnWidth = 120.0f;
                    const float btnHeight = 34.0f;
                    const ImVec2 btnMin(cardMin.x + (cardWidth - btnWidth) * 0.5f, cardMax.y - btnHeight - 20.0f);
                    const ImVec2 btnMax(btnMin.x + btnWidth, btnMin.y + btnHeight);

                    drawList->AddRectFilled(btnMin, btnMax, isHighlighted ? ColorU32(EColor::Accent) : ColorU32(EColor::SurfaceElevated, 0.85f), 5.0f);
                    if (!isHighlighted)
                    {
                        drawList->AddRect(btnMin, btnMax, ColorU32(EColor::Border, 0.6f), 5.0f);
                    }
                    const ImVec2 launchTextSize = ImGui::CalcTextSize("Launch");
                    drawList->AddText(ImVec2(btnMin.x + (btnWidth - launchTextSize.x) * 0.5f,
                                             btnMin.y + (btnHeight - launchTextSize.y) * 0.5f),
                                      isHighlighted ? IM_COL32(255, 255, 255, 255) : ColorU32(EColor::Text), "Launch");
                }

                ImGui::PopID();
            }

            ImGui::TableNextColumn();
            ImGui::PushID("NewProject");
            if (DrawNewProjectCard(ImGui::GetContentRegionAvail().x, cardHeight,
                                   highlightedIndex_ == NewProjectCellIndex()))
            {
                newProjectDialog_.Open();
            }
            ImGui::PopID();

            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }

    ImGui::EndChild();

    // ==========================================
    // 3. Minimal Footer Bar
    // ==========================================
    NextUI::Theme::DrawThinSeparator(0.40f);
    ImGui::Spacing();

    ImGui::BeginGroup();
    {
        // Minimal keyboard shortcut guide
        ImGui::TextColored(Color(EColor::TextDim),
                           ICON_FA_ARROW_LEFT " " ICON_FA_ARROW_RIGHT " " ICON_FA_ARROW_UP " " ICON_FA_ARROW_DOWN " Navigate    Enter Launch    Esc Return");

        // Rebuild / Error Status display
        if (!rebuildStatus_.empty())
        {
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextColored(Color(EColor::Warning), ICON_FA_ROTATE " %s", rebuildStatus_.c_str());
        }
        else if (!GetSession().GetLastError().empty())
        {
            ImGui::SameLine(0.0f, 16.0f);
            ImGui::TextColored(Color(EColor::Danger), "%s", GetSession().GetLastError().c_str());
        }

        // Right side GC status
        if (GetSession().IsLeaking())
        {
            const char* leakMsg = "GC Leak Detected";
            const float leakWidth = ImGui::CalcTextSize(leakMsg).x + 10.0f;
            if (ImGui::GetContentRegionAvail().x > leakWidth)
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - 28.0f - leakWidth);
                ImGui::TextColored(Color(EColor::Danger), "%s", leakMsg);
            }
        }
    }
    ImGui::EndGroup();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    // After the menu window is closed and its style is popped: a modal drawn inside it would
    // inherit that window's padding and clipping rectangle rather than standing on its own.
    DrawNewProjectDialog();
}

bool LauncherGameInstance::DrawNewProjectCard(float cardWidth, float cardHeight, bool highlighted)
{
    // Deliberately not a copy of the game card: this one opens a dialog rather than launching
    // anything, and looking different is what says so before it is clicked.
    const ImVec2 cardMin = ImGui::GetCursorScreenPos();
    const ImVec2 cardMax(cardMin.x + cardWidth, cardMin.y + cardHeight);

    ImGui::InvisibleButton("##NewProjectHit", ImVec2(cardWidth, cardHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (hovered)
    {
        highlightedIndex_ = NewProjectCellIndex();
    }

    const bool active = highlighted || hovered;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(cardMin, cardMax,
                            active ? ColorU32(EColor::SurfaceHover, 0.55f) : ColorU32(EColor::Surface, 0.28f),
                            8.0f);
    drawList->AddRect(cardMin, cardMax,
                      active ? ColorU32(EColor::AccentHover, 0.85f) : ColorU32(EColor::Border, 0.45f), 8.0f, 0,
                      active ? 1.5f : 1.0f);

    ImFont* titleFont = NextUI::Theme::GetTitleFont(GetEngine());
    if (titleFont == nullptr)
    {
        titleFont = ImGui::GetFont();
    }

    // Laid out from the same measurements as a game card, so the plus sits where a game's icon does.
    const float iconBoxSize = cardHeight <= 240.0f ? 54.0f : 68.0f;
    const float iconAreaY = cardMin.y + (cardHeight <= 240.0f ? 24.0f : 40.0f);
    const ImVec2 iconBoxMin(cardMin.x + (cardWidth - iconBoxSize) * 0.5f, iconAreaY);
    const ImVec2 iconBoxMax(iconBoxMin.x + iconBoxSize, iconBoxMin.y + iconBoxSize);
    drawList->AddRectFilled(iconBoxMin, iconBoxMax,
                            active ? ColorU32(EColor::Accent, 0.18f) : ColorU32(EColor::Background, 0.45f),
                            iconBoxSize * 0.5f);
    drawList->AddRect(iconBoxMin, iconBoxMax,
                      active ? ColorU32(EColor::AccentHover, 0.5f) : ColorU32(EColor::Border, 0.35f),
                      iconBoxSize * 0.5f);

    ImGui::PushFont(titleFont);
    const ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_PLUS);
    drawList->AddText(ImVec2(iconBoxMin.x + (iconBoxSize - iconSize.x) * 0.5f,
                             iconBoxMin.y + (iconBoxSize - iconSize.y) * 0.5f),
                      active ? ColorU32(EColor::Text) : ColorU32(EColor::TextMuted), ICON_FA_PLUS);
    ImGui::PopFont();

    const float titleY = iconBoxMax.y + (cardHeight <= 240.0f ? 16.0f : 26.0f);
    ImGui::PushFont(titleFont);
    const char* title = "New Project";
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    drawList->AddText(ImVec2(cardMin.x + (cardWidth - titleSize.x) * 0.5f, titleY),
                      active ? ColorU32(EColor::Text) : ColorU32(EColor::TextMuted), title);
    ImGui::PopFont();

    const char* subtitle = "Start from a template";
    const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle);
    drawList->AddText(ImVec2(cardMin.x + (cardWidth - subtitleSize.x) * 0.5f,
                             titleY + titleSize.y + 8.0f),
                      ColorU32(EColor::TextDim), subtitle);

    return clicked;
}

void LauncherGameInstance::DrawNewProjectDialog()
{
    const Modules::NextDotNet::FNewGameProjectOutcome outcome = newProjectDialog_.Draw(&GetSession());
    if (!outcome.created)
    {
        return;
    }

    // The new manifest is on disk now, so the menu has to be rescanned before it can select it.
    RefreshEntries();
    for (size_t i = 0; i < entries_.size(); ++i)
    {
        if (entries_[i].manifest.id == outcome.gameId)
        {
            highlightedIndex_ = static_cast<int>(i);
            break;
        }
    }
    rebuildStatus_ = outcome.built ? "created " + outcome.gameId + " — ready to play"
                                   : "created " + outcome.gameId + " — not built yet";
}

void LauncherGameInstance::RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg)
{
    reg.Add("state", [this]() -> Runtime::Agent::FAgentQueryValue
            { return std::string(StateName(GetSession().GetState())); });
    reg.Add("active", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                const auto* manifest = GetSession().GetActiveManifest();
                return manifest != nullptr ? manifest->id : std::string();
            });
    reg.Add("count", [this]() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(entries_.size()); });
    reg.Add("availableCount", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                return static_cast<int64_t>(std::count_if(entries_.begin(), entries_.end(),
                                                          [](const FEntry& e) { return e.available; }));
            });
    reg.Add("highlighted", [this]() -> Runtime::Agent::FAgentQueryValue
            {
                if (highlightedIndex_ == NewProjectCellIndex())
                {
                    return std::string("*new-project");
                }
                return highlightedIndex_ >= 0 && highlightedIndex_ < static_cast<int>(entries_.size())
                           ? entries_[static_cast<size_t>(highlightedIndex_)].manifest.id
                           : std::string();
            });
    reg.Add("newProjectOpen", [this]() -> Runtime::Agent::FAgentQueryValue
            { return newProjectDialog_.IsOpen(); });
    reg.Add("templateCount", []() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(Modules::NextDotNet::ScanGameTemplates().size()); });
    reg.Add("lastError", [this]() -> Runtime::Agent::FAgentQueryValue { return GetSession().GetLastError(); });
    reg.Add("unloadPending", [this]() -> Runtime::Agent::FAgentQueryValue
            { return static_cast<int64_t>(GetSession().UnloadPendingStreak()); });
    reg.Add("rebuildStatus", [this]() -> Runtime::Agent::FAgentQueryValue { return rebuildStatus_; });
}
