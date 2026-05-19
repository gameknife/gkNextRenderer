#include "EditorInterface.hpp"

#include "Engine/Utilities/Exception.hpp"

#include <imgui.h>
#include <imgui_freetype.h>

#include <array>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "EditorUi.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "EditorActionDispatcher.hpp"
#include "EditorContext.hpp"
#include "EditorMain.h"
#include "Core/EditorLayoutConstants.hpp"
#include "Core/RecentScenes.hpp"
#include "EditorUtils.h"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Editor/ProfessionalUI.hpp"
#include "Engine/Runtime/Utilities/GraphicsDebugPanel.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <SDL3/SDL_dialog.h>

extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;

namespace
{
    void CheckVulkanResultCallback(const VkResult err)
    {
        if (err != VK_SUCCESS)
        {
            Throw(std::runtime_error(std::string("ImGui Vulkan error (") + Vulkan::ToString(err) + ")"));
        }
    }

    const ImWchar* GetGlyphRangesFontAwesome()
    {
        static const ImWchar ranges[] = {
            ICON_MIN_FA,
            ICON_MAX_FA, // Basic Latin + Latin Supplement
            0,
        };
        return &ranges[0];
    }
} // namespace

EditorInterface::EditorInterface(class EditorGameInstance* editor) : editor_(editor) {}

EditorInterface::~EditorInterface() = default;

void EditorInterface::Config()
{
    auto& io = ImGui::GetIO();

    io.IniFilename = "editor.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
}

void EditorInterface::Init()
{
    auto& io = ImGui::GetIO();

    // Window scaling and style.
    const auto scaleFactor = 1.0;
    // ImGui::GetStyle().ScaleAllSizes(scaleFactor);

    io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
    const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic()
        : GOption->locale == "zhCN"                     ? io.Fonts->GetGlyphRangesChineseFull()
                                                        : io.Fonts->GetGlyphRangesDefault();

    const ImWchar* iconRange = GetGlyphRangesFontAwesome();
    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphMinAdvanceX = 14.0f;
    config.GlyphOffset = ImVec2(0, 0);
    if (!io.Fonts->AddFontFromFileTTF(
            Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), 14 * scaleFactor,
            &config, iconRange))
    {
    }

    ImFont* fontIcon = io.Fonts->AddFontFromFileTTF(
        Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-BoldCondensed.ttf").c_str(), 18 * scaleFactor,
        nullptr, glyphRange);

    config.GlyphMinAdvanceX = 20.0f;
    config.GlyphOffset = ImVec2(0, 0);
    io.Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(),
                                 18 * scaleFactor, &config, iconRange);

    ImFont* fontBigIcon = io.Fonts->AddFontFromFileTTF(
        Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), 32 * scaleFactor, nullptr,
        iconRange);

    uiState_.fontIcon = fontIcon;
    uiState_.bigIcon = fontBigIcon;
    Editor::LoadRecentScenes(uiState_);
    firstRun_ = true;
}

namespace
{
    constexpr float kToolbarSize = 38.0f;
    constexpr float kToolbarIconWidth = 34.0f;
    constexpr float kToolbarIconHeight = 30.0f;
} // namespace

ImGuiID EditorInterface::DockSpaceUI()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float topOffset = kToolbarSize + Editor::kTitleBarHeight;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + topOffset));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x, viewport->Size.y - topOffset - Editor::kFooterHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);
    ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Master DockSpace", NULL, windowFlags);
    ImGuiID dockMain = ImGui::GetID("MyDockspace");

    if (firstRun_ || uiState_.dockResetRequested)
    {
        RebuildDefaultDockLayout(dockMain);
        uiState_.dockResetRequested = false;
    }

    ImGui::DockSpace(dockMain, ImVec2(0, 0),
                     ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(3);

    return dockMain;
}

void EditorInterface::RebuildDefaultDockLayout(ImGuiID id)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoDockingInCentralNode |
                                      ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(id, viewport->Size);

    ImGuiID dockMain = id;
    ImGuiID dock1 = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.155f, nullptr, &dockMain);
    ImGuiID dock2 = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.205f, nullptr, &dockMain);
    ImGuiID dock3 = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Outliner", dock1);
    ImGui::DockBuilderDockWindow("Properties", dock2);
    ImGui::DockBuilderDockWindow("Command History", dock2);
    ImGui::DockBuilderDockWindow("AI Assistant", dock2);
    ImGui::DockBuilderDockWindow("Hot Reload", dock2);
    ImGui::DockBuilderDockWindow("Content Browser", dock3);
    ImGui::DockBuilderDockWindow("Log", dock3);
    ImGui::DockBuilderDockWindow("Material Browser", dock3);
    ImGui::DockBuilderDockWindow("Texture Browser", dock3);
    ImGui::DockBuilderDockWindow("Mesh Browser", dock3);
    ImGui::DockBuilderFinish(id);
}

void EditorInterface::ToolbarUI(EditorContext& ctx)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + Editor::kTitleBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, kToolbarSize));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::Begin("TOOLBAR", NULL, windowFlags);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
    const ImVec2 toolbarMin = ImGui::GetWindowPos();
    const ImVec2 toolbarMax = toolbarMin + ImGui::GetWindowSize();
    toolbarDrawList->AddLine(toolbarMin, ImVec2(toolbarMax.x, toolbarMin.y),
                             Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border, 0.70f), 1.0f);
    toolbarDrawList->AddLine(ImVec2(toolbarMin.x, toolbarMax.y - 1.0f), toolbarMax,
                             Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border, 0.92f), 1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
    ImGui::SetCursorPosY((kToolbarSize - kToolbarIconHeight) * 0.5f);

    static int projectIndex = 0;
    static int backendIndex = 0;
    static int platformIndex = 0;
    static int buildConfigIndex = 0;

    ImGui::SetNextItemWidth(138.0f);
    ImGui::Combo("##ProjectSelector", &projectIndex, ICON_FA_CUBE " RayQuery\0" ICON_FA_CUBE " Playground\0\0");
    Runtime::UiTheme::DrawTooltip("Project");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(108.0f);
    ImGui::Combo("##BackendSelector", &backendIndex, "Vulkan\0Metal\0DirectX 12\0\0");
    Runtime::UiTheme::DrawTooltip("Backend");
    ImGui::SameLine(0.0f, 12.0f);

    ImGui::BeginGroup();
    if (uiState_.fontIcon)
    {
        ImGui::PushFont(uiState_.fontIcon);
    }
    if (Runtime::UiTheme::ToolbarButton(ICON_FA_FLOPPY_DISK, "Save Scene", false,
                                        ImVec2(kToolbarIconWidth, kToolbarIconHeight)))
    {
        if (!uiState_.currentScenePath.empty())
        {
            ctx.scene.Save(uiState_.currentScenePath);
            SPDLOG_INFO("Scene saved: {}", uiState_.currentScenePath);
        }
        else
        {
            const std::string filename = "saved_scene.glb";
            ctx.scene.Save(filename);
            uiState_.currentScenePath = filename;
            SPDLOG_INFO("Scene saved: {}", filename);
        }
    }
    ImGui::SameLine();
    if (Runtime::UiTheme::ToolbarButton(ICON_FA_FOLDER_OPEN, "Open Scene", false,
                                        ImVec2(kToolbarIconWidth, kToolbarIconHeight)))
    {
        SDL_DialogFileFilter filters[] = {
            {"Scenes", "glb;gltf;ldr;mpd"},
            {"All Files", "*"},
        };
        SDL_ShowOpenFileDialog(
            [](void* userdata, const char* const* filelist, int /*filter*/)
            {
                auto* editorCtx = static_cast<EditorContext*>(userdata);
                if (filelist && filelist[0])
                {
                    editorCtx->actions.Dispatch(*editorCtx, EEditorAction::IO_LoadScene, std::string(filelist[0]));
                }
            },
            &ctx,
            ctx.engine.GetWindow().Handle(),
            filters, 2, nullptr, false);
    }
    ImGui::SameLine();
    Runtime::UiTheme::ToolbarButton(ICON_FA_FILE_IMPORT, "Import Asset (placeholder)", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    Runtime::UiTheme::ToolbarButton(ICON_FA_CUBE, "Create Actor (placeholder)", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    if (uiState_.fontIcon)
    {
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 12.0f);

    ImGui::BeginGroup();
    if (uiState_.fontIcon)
    {
        ImGui::PushFont(uiState_.fontIcon);
    }
    Runtime::UiTheme::ToolbarButton(ICON_FA_GEAR, "Project Settings (placeholder)", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    Runtime::UiTheme::ToolbarButton(ICON_FA_CIRCLE_NODES, "Node Graph (placeholder)", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    Runtime::UiTheme::ToolbarButton(ICON_FA_ARROWS_ROTATE, "Refresh Assets (placeholder)", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    Runtime::UiTheme::ToolbarButton(ICON_FA_MAGNET, "Snap Settings (placeholder)", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    if (uiState_.fontIcon)
    {
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, 14.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success, 0.75f));
    if (ImGui::Button(ICON_FA_PLAY " Play", ImVec2(88.0f, kToolbarIconHeight)))
    {
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::string cmdline = (currentPath / "gkNextRenderer").string() + (GOption->ForceSDR ? " --forcesdr" : "");
        std::system(cmdline.c_str());
    }
    Runtime::UiTheme::DrawTooltip("Run in gkNextRenderer");
    ImGui::PopStyleColor(3);
    ImGui::SameLine(0.0f, 12.0f);

    ImGui::SetNextItemWidth(116.0f);
    ImGui::Combo("##PlatformSelector", &platformIndex, ICON_FA_DESKTOP " Desktop\0Android\0iOS\0\0");
    Runtime::UiTheme::DrawTooltip("Target Platform");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(134.0f);
    ImGui::Combo("##BuildConfigSelector", &buildConfigIndex, "Development\0Debug\0Shipping\0\0");
    Runtime::UiTheme::DrawTooltip("Build Configuration");

    const float rightStart = viewport->Size.x - 104.0f;
    if (ImGui::GetCursorPosX() < rightStart)
    {
        ImGui::SameLine(rightStart);
    }
    if (uiState_.fontIcon)
    {
        ImGui::PushFont(uiState_.fontIcon);
    }
    Runtime::UiTheme::ToolbarButton(ICON_FA_GEAR, "Editor Settings", false,
                                    ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    if (uiState_.fontIcon)
    {
        ImGui::PopFont();
    }
    ImGui::SameLine(0.0f, 8.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 avatarPos = ImGui::GetCursorScreenPos();
    const float avatarRadius = kToolbarIconHeight * 0.5f;
    drawList->AddCircleFilled(avatarPos + ImVec2(avatarRadius, avatarRadius), avatarRadius,
                              Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Accent, 0.55f), 24);
    drawList->AddCircle(avatarPos + ImVec2(avatarRadius, avatarRadius), avatarRadius,
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::BorderStrong), 24, 1.0f);
    const ImVec2 initialsSize = ImGui::CalcTextSize("GK");
    drawList->AddText(avatarPos + ImVec2(avatarRadius - initialsSize.x * 0.5f, avatarRadius - initialsSize.y * 0.5f),
                      Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Text), "GK");
    ImGui::Dummy(ImVec2(kToolbarIconHeight, kToolbarIconHeight));
    Runtime::UiTheme::DrawTooltip("User");

    ImGui::PopStyleVar();
    ImGui::End();
}

void EditorInterface::Render()
{
    UserInterface* ui = editor_->GetEngine().GetUserInterface();
    if (ui == nullptr)
    {
        return;
    }

    EditorContext ctx{editor_->GetEngine(), editor_->GetEngine().GetScene(), *ui, editor_->Actions(), &editor_->GetGizmoController()};
    void* previousUserData = ImGui::GetIO().UserData;
    ImGui::GetIO().UserData = &ctx;

    uiState_.selected_obj_id = ctx.scene.GetSelectedId();
    
    // Global keyboard shortcuts are handled by NextEngine.

    ImGuiID id = DockSpaceUI();
    ToolbarUI(ctx);

    Editor::DrawTitleBarOverlay(ctx, uiState_);

    if (uiState_.sidebar)
        Editor::DrawOutlinerPanel(ctx, uiState_);
    if (uiState_.properties)
        Editor::DrawPropertiesPanel(ctx, uiState_);
    if (uiState_.contentBrowser || uiState_.materialBrowser || uiState_.textureBrowser || uiState_.meshBrowser)
        Editor::DrawContentBrowserPanel(ctx, uiState_);
    if (uiState_.logPanel)
        Editor::DrawConsoleLogPanel(ctx, uiState_);
    if (uiState_.commandHistoryPanel)
        Editor::DrawCommandHistoryPanel(ctx, uiState_);
    if (uiState_.hotReloadPanel)
        Editor::DrawHotReloadPanel(ctx, uiState_);
    if (uiState_.aiPanel)
        Editor::DrawAIPanel(ctx, uiState_);
    if (uiState_.viewport)
        Editor::DrawViewportOverlay(ctx, uiState_);

    ctx.ui.RenderConsoleOverlay();

    if (uiState_.child_style)
        utils::ShowStyleEditorWindow(&uiState_.child_style);
    if (uiState_.child_demo)
        ImGui::ShowDemoWindow(&uiState_.child_demo);
    if (uiState_.child_metrics)
        ImGui::ShowMetricsWindow(&uiState_.child_metrics);
    if (uiState_.child_stack)
        ImGui::ShowStackToolWindow(&uiState_.child_stack);
    if (uiState_.child_color)
        utils::ShowColorExportWindow(&uiState_.child_color);
    if (uiState_.child_resources)
        utils::ShowResourcesWindow(&uiState_.child_resources);
    if (uiState_.child_about)
        utils::ShowAboutWindow(&uiState_.child_about);

    if (uiState_.ed_material)
        Editor::DrawMaterialEditorPanel(ctx, uiState_);

    // The renderer output rect is the dockspace central node.
    uiState_.viewportOnMainViewport = true;
    uiState_.viewportContentPos = ImVec2(0.0f, 0.0f);
    uiState_.viewportContentSize = ImVec2(0.0f, 0.0f);

    if (ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id))
    {
        uiState_.viewportContentPos = node->Pos;
        uiState_.viewportContentSize = node->Size;

        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        if (node->HostWindow && node->HostWindow->Viewport && node->HostWindow->Viewport != mainViewport)
        {
            uiState_.viewportOnMainViewport = false;
        }

        if (uiState_.viewportOnMainViewport && node->Size.x >= 1.0f && node->Size.y >= 1.0f)
        {
            editor_->GetEngine().GetRenderer().SwapChain().UpdateOutputViewport(
                Utilities::Math::floorToInt(node->Pos.x - mainViewport->Pos.x),
                Utilities::Math::floorToInt(node->Pos.y - mainViewport->Pos.y),
                Utilities::Math::ceilToInt(node->Size.x), Utilities::Math::ceilToInt(node->Size.y));

            editor_->DrawGizmo(glm::vec2(node->Pos.x, node->Pos.y), glm::vec2(node->Size.x, node->Size.y));
        }
    }

    firstRun_ = false;
    ImGui::GetIO().UserData = previousUserData;
}

void EditorInterface::DrawIndicator(uint32_t frameCount)
{
    ImGui::OpenPopup("Loading");
    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(100, 40));

    if (ImGui::BeginPopupModal("Loading", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::Text("Loading%s   ",
                    frameCount % 4 == 0       ? ""
                        : frameCount % 4 == 1 ? "."
                        : frameCount % 4 == 2 ? ".."
                                              : "...");
        ImGui::EndPopup();
    }
}
