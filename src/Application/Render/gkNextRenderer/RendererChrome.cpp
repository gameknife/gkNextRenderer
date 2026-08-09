#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <random>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Editor/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Utilities/AboutDialog.hpp"
#include "Engine/Utilities/ImGui.hpp"

#include <SDL3/SDL_misc.h>
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"


extern float TitlebarSize;
extern float TitlebarRightInfoWidth;

void NextRendererGameInstance::DrawTitleBar(const FGameUiFrameContext& context, FRendererUiState& uiState)
{
    NextUI::Theme::FAppTitleBarConfig config{};
    config.BrandWindowId = "RendererBrand";
    config.MenuWindowId = "RendererMenuBar";
    config.RightWindowId = "RendererWindowControls";
    config.AppName = "gkNextRenderer";
    config.Height = TitlebarSize;
    config.RightContentWidth = TitlebarRightInfoWidth;
    config.IsMaximized = GetEngine().IsMaximized();
    config.DrawMenuBar = [&]() -> float
    {
        float menuRight = ImGui::GetCursorScreenPos().x;

        const auto UpdateMenuRight = [&menuRight]()
        {
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
        };

        if (ImGui::BeginMenu("File"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Project Page"))
            {
                NextRenderer::OSCommand("https://github.com/gameknife/gkNextRenderer");
            }
            if (ImGui::MenuItem("Open Screenshot Folder"))
            {
                GetEngine().GetScreenShotService().EnsureDirectory();
                const std::string folderPath = GetEngine().GetScreenShotService().GetDirectory();
                NextRenderer::OSCommand(folderPath.c_str());
            }
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("View"))
        {
            UpdateMenuRight();
            auto& showFlags = GetEngine().GetShowFlags();
            Utilities::UI::DrawShowFlagsCommon(showFlags);
            ImGui::MenuItem("Profiler Overlay", nullptr, &GetEngine().GetUserSettings().ShowOverlay);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Screenshot"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Screenshot"))
            {
                RequestScreenshot(false, "");
            }
            if (ImGui::MenuItem("Screenshot and Open Folder"))
            {
                RequestScreenshot(true, "");
            }
            ImGui::Separator();
            DrawVideoCaptureMenuItems();
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Renderer"))
        {
            UpdateMenuRight();
            Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), GetEngine().GetUserSettings(),
                                                              "##RendererMenuSelector", 180.0f);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Settings"))
        {
            UpdateMenuRight();
            ImGui::MenuItem("Render Settings", nullptr, &uiState.showSettings);
            ImGui::MenuItem("Stats Overlay", nullptr, &GetEngine().GetUserSettings().ShowOverlay);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Help"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Keyboard & Mouse"))
            {
                uiState.showCheatSheet = true;
            }
            if (ImGui::MenuItem("Documentation"))
            {
                SDL_OpenURL(Utilities::UI::ProjectDocsUrl);
            }
            if (ImGui::MenuItem("Report an Issue"))
            {
                SDL_OpenURL(Utilities::UI::ProjectIssuesUrl);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About gkNextRenderer"))
            {
                uiState.showAbout = true;
            }
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        return menuRight;
    };
    config.OnMinimize = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().RequestMinimize(); })
                                                     : std::function<void()>();
    config.OnToggleMaximize = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().ToggleMaximize(); })
                                                          : std::function<void()>();
    config.OnClose = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().RequestClose(); })
                                                 : std::function<void()>();
    NextUI::Theme::DrawAppTitleBar(GetEngine(), config);
}

void NextRendererGameInstance::DrawBottomStatusBar(FRendererUiState& uiState)
{
    Runtime::DevToolsUI::DrawDeveloperStatusBar(GetEngine(), "RendererStatusBar", 30.0f,
                                         [&]()
                                         {
                                             uiState.memoryStatisticsPanelOpen = !uiState.memoryStatisticsPanelOpen;
                                         },
                                         uiState.memoryStatisticsPanelOpen,
                                         []() { Modules::LiveCoding::RequestCppReload(); },
                                         Modules::LiveCoding::IsCppLiveCodingAvailable(),
                                         [this]() { RequestScreenshot(false, ""); });
}
