#include "EditorInterface.hpp"

#include "Utilities/Exception.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include "Editor/EditorUi.hpp"

#include "Assets/Core/Scene.hpp"
#include "Assets/GPU/TextureImage.hpp"
#include "Common/CoreMinimal.hpp"
#include "Editor/EditorActionDispatcher.hpp"
#include "Editor/EditorContext.hpp"
#include "Editor/EditorMain.h"
#include "Editor/EditorUtils.h"
#include "Options.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/SwapChain.hpp"

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
    firstRun_ = true;
}

namespace
{
    constexpr float kToolbarSize = 50.0f;
    constexpr float kToolbarIconWidth = 32.0f;
    constexpr float kToolbarIconHeight = 32.0f;
    constexpr float kTitleBarHeight = 55.0f;
    constexpr float kFootBarHeight = 40.0f;
    float gMenuBarHeight = 0.0f;
} // namespace

ImGuiID EditorInterface::DockSpaceUI()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x, viewport->Pos.y + kToolbarSize + kTitleBarHeight - gMenuBarHeight));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x, viewport->Size.y - kToolbarSize - kTitleBarHeight + gMenuBarHeight - kFootBarHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);
    ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Master DockSpace", NULL, windowFlags);
    ImGuiID dockMain = ImGui::GetID("MyDockspace");

    // Save off menu bar height for later.
    gMenuBarHeight = ImGui::GetCurrentWindow()->MenuBarHeight;

    ImGui::DockSpace(dockMain, ImVec2(0, 0),
                     ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(3);

    return dockMain;
}

void EditorInterface::ToolbarUI()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + kTitleBarHeight));
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

    ImGui::BeginGroup();
    if (uiState_.fontIcon)
    {
        ImGui::PushFont(uiState_.fontIcon);
    }
    ImGui::Button(ICON_FA_FLOPPY_DISK, ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    ImGui::Button(ICON_FA_FOLDER, ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    if (uiState_.fontIcon)
    {
        ImGui::PopFont();
    }
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::SameLine(50);
    if (uiState_.fontIcon)
    {
        ImGui::PushFont(uiState_.fontIcon);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 210, 0, 255));
    if (ImGui::Button(ICON_FA_PLAY, ImVec2(kToolbarIconWidth, kToolbarIconHeight)))
    {
        std::filesystem::path currentPath = std::filesystem::current_path();
        std::string cmdline = (currentPath / "gkNextRenderer").string() + (GOption->ForceSDR ? " --forcesdr" : "");
        std::system(cmdline.c_str());
    }
    ImGui::SameLine();

    ImGui::PopStyleColor();
    if (uiState_.fontIcon)
    {
        ImGui::PopFont();
    }
    static int item = 3;
    ImGui::SetNextItemWidth(120);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 7));
    ImGui::Combo("##Render", &item, "RTPipe\0ModernDeferred\0LegacyDeferred\0RayQuery\0HybirdRender\0\0");
    ImGui::SameLine();
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::SameLine();


    ImGui::BeginGroup();
    ImGui::SameLine(50);
    if (uiState_.fontIcon)
    {
        ImGui::PushFont(uiState_.fontIcon);
    }
    ImGui::Button(ICON_FA_FILE_IMPORT, ImVec2(kToolbarIconWidth, kToolbarIconHeight));
    ImGui::SameLine();
    if (uiState_.fontIcon)
    {
        ImGui::PopFont();
    }
    ImGui::EndGroup();

    ImGui::End();
}

void EditorInterface::Render()
{
    UserInterface* ui = editor_->GetEngine().GetUserInterface();
    if (ui == nullptr)
    {
        return;
    }

    EditorContext ctx{editor_->GetEngine(), editor_->GetEngine().GetScene(), *ui, editor_->Actions()};
    void* previousUserData = ImGui::GetIO().UserData;
    ImGui::GetIO().UserData = &ctx;

    uiState_.selected_obj_id = ctx.scene.GetSelectedId();
    
    // Global keyboard shortcuts are handled by NextEngine.

    ImGuiID id = DockSpaceUI();
    ToolbarUI();

    if (firstRun_)
    {
        ImGuiID dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.1f, nullptr, &id);
        ImGuiID dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.2f, nullptr, &id);
        ImGuiID dock3 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, 0.25f, nullptr, &id);

        ImGui::DockBuilderDockWindow("Outliner", dock1);
        ImGui::DockBuilderDockWindow("Properties", dock2);
        ImGui::DockBuilderDockWindow("Command History", dock2);
        ImGui::DockBuilderDockWindow("Content Browser", dock3);
        ImGui::DockBuilderDockWindow("Log", dock3);
        ImGui::DockBuilderDockWindow("Material Browser", dock3);
        ImGui::DockBuilderDockWindow("Texture Browser", dock3);
        ImGui::DockBuilderDockWindow("Mesh Browser", dock3);
        ImGui::DockBuilderFinish(id);
    }

    Editor::DrawTitleBarOverlay(ctx, uiState_);

    if (uiState_.sidebar)
        Editor::DrawOutlinerPanel(ctx, uiState_);
    if (uiState_.properties)
        Editor::DrawPropertiesPanel(ctx, uiState_);
    if (uiState_.contentBrowser)
        Editor::DrawContentBrowserPanel(ctx, uiState_);
    if (uiState_.logPanel)
        Editor::DrawConsoleLogPanel(ctx, uiState_);
    if (uiState_.materialBrowser)
        Editor::DrawMaterialBrowserPanel(ctx, uiState_);
    if (uiState_.textureBrowser)
        Editor::DrawTextureBrowserPanel(ctx, uiState_);
    if (uiState_.meshBrowser)
        Editor::DrawMeshBrowserPanel(ctx, uiState_);
    if (uiState_.commandHistoryPanel)
        Editor::DrawCommandHistoryPanel(ctx, uiState_);
    if (uiState_.viewport)
        Editor::DrawViewportOverlay(ctx, uiState_);

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
