#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Utilities/AboutDialog.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/LogFile.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <SDL3/SDL_misc.h>

namespace
{
    void DrawRow(const char* label, const std::string& value)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value.c_str());
    }

    void DrawCopyableRow(const char* label, const std::string& value)
    {
        DrawRow(label, value);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Click to copy");
        }
        if (ImGui::IsItemClicked())
        {
            ImGui::SetClipboardText(value.c_str());
        }
    }

    std::string DescribeRenderer()
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (engine == nullptr)
        {
            return "unavailable";
        }
        return Vulkan::GetRendererName(engine->GetRenderer().CurrentLogicRendererType());
    }

    std::string DescribeDevice(bool driverOnly)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (engine == nullptr)
        {
            return "unavailable";
        }
        const VkPhysicalDeviceProperties properties = engine->GetRenderer().Device().DeviceProperties();
        if (!driverOnly)
        {
            return properties.deviceName;
        }
        return fmt::format("{}.{}.{} (Vulkan {}.{}.{})",
                           VK_VERSION_MAJOR(properties.driverVersion), VK_VERSION_MINOR(properties.driverVersion),
                           VK_VERSION_PATCH(properties.driverVersion),
                           VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                           VK_VERSION_PATCH(properties.apiVersion));
    }
}

namespace Utilities::UI
{
    void ShowAboutDialog(bool& open)
    {
        if (!open)
        {
            return;
        }

        ImGui::OpenPopup("About");
        if (!BeginAnchoredPopupModal("About", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        TextCentered(NextRenderer::GetApplicationIdentity());
        TextCentered("A cross-platform Vulkan renderer and engine");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::BeginTable("##AboutInfo", 2, ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            DrawRow("Version", NextRenderer::GetBuildVersion());
            DrawRow("Built", fmt::format("{} {}", __DATE__, __TIME__));
            DrawRow("Renderer", DescribeRenderer());
            DrawRow("GPU", DescribeDevice(false));
            DrawRow("Driver", DescribeDevice(true));

            const std::string& logPath = Logging::GetLogFilePath();
            DrawCopyableRow("Log file", logPath.empty() ? std::string("not available") : logPath);
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::Separator();
        ImGui::TextDisabled("MIT License. See LICENSE and THIRD-PARTY-NOTICES.md.");
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        if (ImGui::Button("Project Page"))
        {
            SDL_OpenURL(ProjectHomeUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Documentation"))
        {
            SDL_OpenURL(ProjectDocsUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Report an Issue"))
        {
            SDL_OpenURL(ProjectIssuesUrl);
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            ImGui::CloseCurrentPopup();
            open = false;
        }

        ImGui::EndPopup();
    }
}
