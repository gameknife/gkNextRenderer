#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <random>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Modules/SceneContent/SceneList.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
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
extern float ModeRailWidth;
constexpr float SettingsPanelWidth = 380.0f;

namespace RendererSettingsDetail
{
enum class ESceneListGroup : uint8_t
{
    Procedural = 0,
    Gltf = 1,
    LDraw = 2,
    Other = 3,
};

ESceneListGroup GetSceneListGroup(std::string_view scenePath)
{
    const std::string extension = std::filesystem::path(scenePath).extension().string();
    if (extension == ".proc")
    {
        return ESceneListGroup::Procedural;
    }
    if (extension == ".glb" || extension == ".gltf")
    {
        return ESceneListGroup::Gltf;
    }
    if (extension == ".ldr" || extension == ".mpd")
    {
        return ESceneListGroup::LDraw;
    }
    return ESceneListGroup::Other;
}

const char* GetSceneListGroupLabel(ESceneListGroup group)
{
    switch (group)
    {
    case ESceneListGroup::Procedural:
        return "Procedural";
    case ESceneListGroup::Gltf:
        return "glTF";
    case ESceneListGroup::LDraw:
        return "OMR/LDraw";
    case ESceneListGroup::Other:
    default:
        return "Other";
    }
}

const char* GetPresentModeLabel(VkPresentModeKHR presentMode)
{
    switch (presentMode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
        return "Immediate";
    case VK_PRESENT_MODE_MAILBOX_KHR:
        return "Mailbox";
    case VK_PRESENT_MODE_FIFO_KHR:
        return "FIFO (V-Sync)";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "FIFO Relaxed";
    default:
        return "Unknown";
    }
}

struct FCategoryMeta
{
    const char* icon;
    const char* title;
    const char* shortLabel;
};

FCategoryMeta GetCategoryMeta(NextRendererGameInstance::ESettingsCategory category)
{
    using Cat = NextRendererGameInstance::ESettingsCategory;
    switch (category)
    {
    case Cat::Renderer:
        return {ICON_FA_SLIDERS, "Renderer Settings", "Renderer"};
    case Cat::Environment:
        return {ICON_FA_SUN, "Environment & Lighting", "Environment"};
    case Cat::Camera:
        return {ICON_FA_VIDEO, "Camera & Optics", "Camera"};
    case Cat::Quality:
        return {ICON_FA_WAND_MAGIC_SPARKLES, "Quality & Ray Tracing", "Quality"};
    case Cat::VoxelGI:
        return {ICON_FA_CUBES, "Ambient Cube Voxel GI", "Voxel GI"};
    case Cat::Scene:
        return {ICON_FA_FOLDER_OPEN, "Scene & Models", "Scene"};
    case Cat::Animation:
        return {ICON_FA_PERSON_RUNNING, "Animation & Dynamics", "Animation"};
    case Cat::PostProcess:
        return {ICON_FA_PALETTE, "Post-Process & Misc", "Post-Process"};
    default:
        return {ICON_FA_SLIDERS, "Settings", "Settings"};
    }
}

void BeginCard(const char* id, float height = 0.0f, ImGuiWindowFlags extraFlags = 0)
{
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    const float cardWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.80f));
    if (height > 0.0f)
    {
        ImGui::BeginChild(id, ImVec2(cardWidth, height), ImGuiChildFlags_Borders, extraFlags);
    }
    else
    {
        ImGui::BeginChild(id, ImVec2(cardWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, extraFlags);
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

void EndCard()
{
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

void DrawCardHeader(const char* icon, const char* title)
{
    ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover), "%s  %s", icon ? icon : "", title);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

template <typename T>
bool DrawSegmentedPills(const char* id, const std::vector<std::pair<T, const char*>>& options,
                        T* currentValue, float totalWidth = -1.0f)
{
    if (options.empty() || currentValue == nullptr) return false;

    ImGui::PushID(id);
    const float availWidth = totalWidth > 0.0f ? totalWidth : ImGui::GetContentRegionAvail().x;
    constexpr float spacing = 4.0f;
    const float itemWidth = std::max(20.0f, (availWidth - spacing * static_cast<float>(options.size() - 1)) / static_cast<float>(options.size()));
    constexpr float itemHeight = 24.0f;

    bool changed = false;
    for (size_t i = 0; i < options.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine(0.0f, spacing);
        }
        const bool isSelected = (*currentValue == options[i].first);

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (isSelected)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::Accent));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.70f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted));
        }

        if (ImGui::Button(options[i].second, ImVec2(itemWidth, itemHeight)))
        {
            if (!isSelected)
            {
                *currentValue = options[i].first;
                changed = true;
            }
        }
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }
    ImGui::PopID();
    return changed;
}

template <typename T>
bool DrawSettingSliderRow(const char* label, ImGuiDataType dataType, T* value,
                          T minValue, T maxValue, const char* format, float dragSpeed,
                          float valueWidth = 84.0f)
{
    ImGui::PushID(label);
    NextUI::Theme::BeginFormRow(label);

    const float sliderWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - valueWidth - 6.0f);
    bool changed = false;

    ImGui::SetNextItemWidth(sliderWidth);
    changed |= ImGui::SliderScalar("##Slider", dataType, value, &minValue, &maxValue, format);

    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(valueWidth);
    changed |= ImGui::DragScalar("##Value", dataType, value, dragSpeed, &minValue, &maxValue, format);

    ImGui::PopID();
    return changed;
}

template <typename DrawControl>
bool DrawSettingRow(const char* label, DrawControl&& drawControl)
{
    ImGui::PushID(label);
    NextUI::Theme::BeginFormRow(label);

    const bool changed = drawControl();
    ImGui::PopID();
    return changed;
}

bool DrawSettingCheckboxRow(const char* label, bool* value)
{
    return DrawSettingRow(label,
                          [value]()
                          {
                              return ImGui::Checkbox("##Value", value);
                          });
}

template <typename DrawComboBody>
bool DrawSettingComboRow(const char* label, const char* preview, DrawComboBody&& drawComboBody)
{
    bool changed = false;
    DrawSettingRow(label,
                   [&]()
                   {
                       ImGui::SetNextItemWidth(-FLT_MIN);
                       if (ImGui::BeginCombo("##Value", preview))
                       {
                           changed = drawComboBody();
                           ImGui::EndCombo();
                       }
                       return changed;
                   });
    return changed;
}

bool DrawFloatSetting(const char* label, float* value, float minValue, float maxValue,
                     const char* format, float dragSpeed)
{
    return DrawSettingSliderRow(label, ImGuiDataType_Float, value, minValue, maxValue, format, dragSpeed);
}

bool DrawIntSetting(const char* label, int* value, int minValue, int maxValue,
                   const char* format = "%d")
{
    return DrawSettingSliderRow(label, ImGuiDataType_S32, value, minValue, maxValue, format, 1.0f);
}

bool DrawFloat3Setting(const char* label, const char* id, glm::vec3* value,
                      float dragSpeed, float minValue, float maxValue,
                      const char* format)
{
    return DrawSettingRow(label,
                          [&]()
                          {
                              ImGui::SetNextItemWidth(-FLT_MIN);
                              return ImGui::DragFloat3(
                                  id, &value->x, dragSpeed, minValue, maxValue, format);
                          });
}

bool DrawColorSetting(const char* label, const char* id, glm::vec3* value)
{
    return DrawSettingRow(label,
                          [&]()
                          {
                              ImGui::SetNextItemWidth(-FLT_MIN);
                              return ImGui::ColorEdit3(id, &value->x);
                          });
}
} // namespace RendererSettingsDetail

void NextRendererGameInstance::DrawSettings(FRendererUiState& uiState)
{
    using namespace RendererSettingsDetail;

    Runtime::Config::UserSettings& userSetting = GetEngine().GetUserSettings();

    if (!uiState.showSettings)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float panelMargin = 10.0f;
    const ImVec2 panelPos = viewport->Pos + ImVec2(ModeRailWidth + panelMargin, TitlebarSize + panelMargin);
    const ImVec2 panelSize(SettingsPanelWidth,
                           viewport->Size.y - TitlebarSize - 50.0f - panelMargin);

    const auto categoryMeta = GetCategoryMeta(uiState.settingsCategory);

    NextUI::Theme::FDetailPanelConfig panelConfig{};
    panelConfig.WindowId = "##RendererSettingsPanel";
    panelConfig.ContentWindowId = "##RendererSettingsContent";
    panelConfig.Icon = categoryMeta.icon;
    panelConfig.Title = categoryMeta.title;
    panelConfig.Open = &uiState.showSettings;
    panelConfig.Position = panelPos;
    panelConfig.Size = panelSize;
    if (!NextUI::Theme::BeginDetailPanel(panelConfig))
    {
        return;
    }

    // 1. Panel-internal top category navigation strip
    {
        using Cat = NextRendererGameInstance::ESettingsCategory;
        struct StripItem
        {
            Cat cat;
            const char* icon;
            const char* tooltip;
        };
        static constexpr StripItem items[] = {
            {Cat::Renderer,    ICON_FA_SLIDERS,              "Renderer"},
            {Cat::Environment, ICON_FA_SUN,                  "Environment"},
            {Cat::Camera,      ICON_FA_VIDEO,                "Camera"},
            {Cat::Quality,     ICON_FA_WAND_MAGIC_SPARKLES,  "Quality & Upscaling"},
            {Cat::VoxelGI,     ICON_FA_CUBES,                "Voxel GI"},
            {Cat::Scene,       ICON_FA_FOLDER_OPEN,          "Scene"},
            {Cat::Animation,   ICON_FA_PERSON_RUNNING,       "Animation"},
            {Cat::PostProcess, ICON_FA_PALETTE,              "Post-Process"},
        };

        const float avail = ImGui::GetContentRegionAvail().x;
        constexpr float spacing = 4.0f;
        const float btnWidth = std::max(20.0f, (avail - spacing * static_cast<float>(IM_ARRAYSIZE(items) - 1)) / static_cast<float>(IM_ARRAYSIZE(items)));
        constexpr float btnHeight = 24.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        for (size_t i = 0; i < IM_ARRAYSIZE(items); ++i)
        {
            if (i > 0) ImGui::SameLine(0.0f, spacing);
            const bool active = (uiState.settingsCategory == items[i].cat);
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::AccentHover));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::Accent));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.45f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
                ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted));
            }

            if (ImGui::Button(items[i].icon, ImVec2(btnWidth, btnHeight)))
            {
                uiState.settingsCategory = items[i].cat;
            }
            NextUI::Theme::DrawTooltip(items[i].tooltip);
            ImGui::PopStyleColor(4);
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    // 2. Render contents for active category
    switch (uiState.settingsCategory)
    {
    case ESettingsCategory::Renderer:
    {
        // Card 1: Pipeline
        BeginCard("##RendererPipelineCard");
        DrawCardHeader(ICON_FA_MICROCHIP, "Active Render Pipeline");

        auto currentRenderer = static_cast<Vulkan::ERendererType>(userSetting.RendererType);
        const std::vector<std::pair<Vulkan::ERendererType, const char*>> rendererOptions = {
            {Vulkan::ERT_SoftwareModern, "Raster"},
            {Vulkan::ERT_SoftwareTracing, "Tracing"},
            {Vulkan::ERT_SoftwareModernNoAmbient, "NoAmbient"},
            {Vulkan::ERT_PathTracing, "PathTrace"},
        };
        if (DrawSegmentedPills("##PipelinePills", rendererOptions, &currentRenderer))
        {
            userSetting.RendererType = static_cast<uint32_t>(currentRenderer);
            GetEngine().RequestRendererType(currentRenderer);
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        const char* desc = "";
        switch (currentRenderer)
        {
        case Vulkan::ERT_SoftwareModern:
            desc = "Modern GPU rasterization + software ambient cube GI.";
            break;
        case Vulkan::ERT_SoftwareTracing:
            desc = "Hardware/Software ray query tracing + ambient GI.";
            break;
        case Vulkan::ERT_SoftwareModernNoAmbient:
            desc = "Lightweight rasterization without ambient cube baking.";
            break;
        case Vulkan::ERT_PathTracing:
            desc = "Hardware path tracing with multi-bounce global illumination.";
            break;
        default:
            desc = "Active renderer.";
            break;
        }
        ImGui::TextDisabled("%s", desc);
        EndCard();

        // Card 2: Presentation & V-Sync
        BeginCard("##RendererPresentationCard");
        DrawCardHeader(ICON_FA_DISPLAY, "Presentation & V-Sync");

        auto currentMode = static_cast<VkPresentModeKHR>(userSetting.PresentMode);
        const std::vector<std::pair<VkPresentModeKHR, const char*>> modeOptions = {
            {VK_PRESENT_MODE_IMMEDIATE_KHR, "Immediate"},
            {VK_PRESENT_MODE_MAILBOX_KHR, "Mailbox"},
            {VK_PRESENT_MODE_FIFO_KHR, "V-Sync"},
            {VK_PRESENT_MODE_FIFO_RELAXED_KHR, "Relaxed"},
        };
        if (DrawSegmentedPills("##PresentModePills", modeOptions, &currentMode))
        {
            userSetting.PresentMode = static_cast<uint32_t>(currentMode);
            GetEngine().GetRenderer().SetRequestedPresentMode(currentMode);
        }

        if (GetEngine().GetRenderer().HasSwapChain())
        {
            const auto& swapChain = GetEngine().GetRenderer().SwapChain();
            const VkPresentModeKHR actualMode = swapChain.PresentMode();
            const auto extent = swapChain.OutputExtent();
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::TextDisabled("Actual: %s · %ux%u", GetPresentModeLabel(actualMode), extent.width, extent.height);
        }
        EndCard();

        // Card 3: Viewport Visual Overlays
        BeginCard("##RendererOverlaysCard");
        DrawCardHeader(ICON_FA_EYE, "Viewport Visual Overlays");
        DrawSettingCheckboxRow("Wireframe X-Ray", &userSetting.WireframeXRay);
        DrawSettingCheckboxRow("Borderless Fullscreen", &userSetting.BorderlessFullscreen);
        DrawSettingCheckboxRow("Viewport Overlay", &userSetting.ShowOverlay);
        DrawSettingCheckboxRow("Keyboard CheatSheet", &uiState.showCheatSheet);
        EndCard();
        break;
    }

    case ESettingsCategory::Environment:
    {
        auto& scene = GetEngine().GetScene();
        auto& environment = scene.GetEnvSettings();
        auto& atmosphere = environment.Atmosphere;
        bool environmentChanged = false;

        // Card 1: Physical Sun
        BeginCard("##EnvSunCard");
        DrawCardHeader(ICON_FA_SUN, "Physical Sun Lighting");
        environmentChanged |= DrawSettingCheckboxRow("Enable Sun", &environment.HasSun);
        if (environment.HasSun)
        {
            // Elevation
            float sunElevationDegrees = glm::degrees(environment.SunElevation);
            if (DrawFloatSetting("Sun Elevation", &sunElevationDegrees, -24.0f, 90.0f, "%.1f deg", 0.25f))
            {
                environment.SunElevation = glm::radians(sunElevationDegrees);
                environmentChanged = true;
            }

            // Quick Elevation Pills
            struct ElevPreset { float deg; const char* name; };
            const ElevPreset elevPresets[] = {
                {5.0f, "Dawn"}, {25.0f, "Morning"}, {65.0f, "Noon"}, {2.0f, "Sunset"}, {-12.0f, "Night"}
            };
            const float pillAvail = ImGui::GetContentRegionAvail().x;
            const float pillWidth = std::max(20.0f, (pillAvail - 4.0f * 4.0f) / 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            for (size_t p = 0; p < IM_ARRAYSIZE(elevPresets); ++p)
            {
                if (p > 0) ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button(elevPresets[p].name, ImVec2(pillWidth, 20.0f)))
                {
                    environment.SunElevation = glm::radians(elevPresets[p].deg);
                    environmentChanged = true;
                }
            }
            ImGui::PopStyleVar();

            // Rotation / Azimuth
            environmentChanged |= DrawFloatSetting("Sun Azimuth", &environment.SunRotation, 0.0f, 2.0f, "%.2f", 0.01f);
            // Compass Presets
            struct CompassPreset { float rot; const char* label; };
            const CompassPreset compassPresets[] = {
                {0.0f, "North"}, {0.5f, "East"}, {1.0f, "South"}, {1.5f, "West"}
            };
            const float compWidth = std::max(20.0f, (pillAvail - 4.0f * 3.0f) / 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            for (size_t c = 0; c < IM_ARRAYSIZE(compassPresets); ++c)
            {
                if (c > 0) ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button(compassPresets[c].label, ImVec2(compWidth, 20.0f)))
                {
                    environment.SunRotation = compassPresets[c].rot;
                    environmentChanged = true;
                }
            }
            ImGui::PopStyleVar();

            environmentChanged |= DrawFloatSetting("Sun Luminance", &environment.SunIntensity, 0.0f, 2000.0f, "%.0f", 1.0f);
            environmentChanged |= DrawColorSetting("Sun Color", "##EnvironmentSunColor", &environment.SunColor);

            // Color Presets
            struct ColorPreset { glm::vec3 col; const char* name; };
            const ColorPreset colorPresets[] = {
                {glm::vec3(1.0f, 0.85f, 0.65f), "Warm 3000K"},
                {glm::vec3(1.0f, 0.98f, 0.95f), "Noon 5500K"},
                {glm::vec3(1.0f, 0.65f, 0.35f), "Golden Hour"},
                {glm::vec3(0.85f, 0.92f, 1.0f), "Cool 6500K"},
            };
            const float colBtnWidth = std::max(20.0f, (pillAvail - 4.0f * 3.0f) / 4.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            for (size_t cp = 0; cp < IM_ARRAYSIZE(colorPresets); ++cp)
            {
                if (cp > 0) ImGui::SameLine(0.0f, 4.0f);
                if (ImGui::Button(colorPresets[cp].name, ImVec2(colBtnWidth, 20.0f)))
                {
                    environment.SunColor = colorPresets[cp].col;
                    environmentChanged = true;
                }
            }
            ImGui::PopStyleVar();
        }
        EndCard();

        // Card 2: Sky Dome & HDRI
        BeginCard("##EnvSkyCard");
        DrawCardHeader(ICON_FA_CLOUD, "Sky & HDRI Environment");
        auto bgMode = environment.BackgroundMode;
        const std::vector<std::pair<Assets::EBackgroundMode, const char*>> bgOptions = {
            {Assets::EBackgroundMode::Environment, "Environment"},
            {Assets::EBackgroundMode::Studio, "Studio Gray"},
        };
        if (DrawSegmentedPills("##BgModePills", bgOptions, &bgMode))
        {
            environment.BackgroundMode = bgMode;
            environmentChanged = true;
        }

        environmentChanged |= DrawSettingCheckboxRow("Enable Sky", &environment.HasSky);
        if (environment.HasSky)
        {
            environmentChanged |= DrawIntSetting("HDRI Index", &environment.SkyIdx, 0, 10);
            environmentChanged |= DrawFloatSetting("Sky Rotation", &environment.SkyRotation, 0.0f, 2.0f, "%.2f", 0.01f);
            environmentChanged |= DrawFloatSetting("Sky Luminance", &environment.SkyIntensity, 0.0f, 1000.0f, "%.0f", 1.0f);
            environmentChanged |= DrawColorSetting("Sky Color", "##EnvironmentSkyColor", &environment.SkyColor);
        }
        EndCard();

        // Card 3: Atmosphere & Height Fog
        BeginCard("##EnvAtmosphereCard");
        DrawCardHeader(ICON_FA_SMOG, "Atmosphere & Fog");
        environmentChanged |= DrawSettingCheckboxRow("Atmosphere Enabled", &environment.AtmosphereEnabled);
        if (environment.AtmosphereEnabled)
        {
            environmentChanged |= DrawFloatSetting("Sky Lum Scale", &atmosphere.SkyLuminanceScale, 0.0f, 10.0f, "%.2f", 0.01f);
            environmentChanged |= DrawFloatSetting("Sky LUT Scale", &userSetting.AtmosphereSkyViewLutScale, 0.25f, 2.0f, "%.2fx", 0.05f);

            static constexpr const char* debugModes[] = {"Off", "In-Scatter", "Transmittance", "SkyView LUT"};
            environmentChanged |= DrawSettingRow(
                "Debug View",
                [&]()
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    return ImGui::Combo("##AtmosphereDebug", &userSetting.AtmosphereDebugMode,
                                        debugModes, IM_ARRAYSIZE(debugModes));
                });

            environmentChanged |= DrawSettingCheckboxRow("Aerial Perspective", &environment.AerialPerspectiveEnabled);
            if (environment.AerialPerspectiveEnabled)
            {
                environmentChanged |= DrawFloatSetting("Max Distance", &atmosphere.AerialPerspectiveMaxDistance, 10.0f, 50000.0f, "%.0f", 10.0f);
            }

            environmentChanged |= DrawSettingCheckboxRow("Height Fog", &environment.HeightFogEnabled);
            if (environment.HeightFogEnabled)
            {
                environmentChanged |= DrawColorSetting("Fog Color", "##AtmosphereFogColor", &atmosphere.FogInscatteringColor);
                environmentChanged |= DrawFloatSetting("Density", &atmosphere.FogDensity, 0.0f, 0.1f, "%.4f", 0.0001f);
                environmentChanged |= DrawFloatSetting("Falloff", &atmosphere.FogHeightFalloff, 0.0f, 2.0f, "%.3f", 0.005f);
                environmentChanged |= DrawFloatSetting("Base Height", &atmosphere.FogBaseHeight, -2000.0f, 2000.0f, "%.1f", 0.5f);
                environmentChanged |= DrawFloatSetting("Opacity", &atmosphere.FogMaxOpacity, 0.0f, 1.0f, "%.2f", 0.01f);
            }
        }
        EndCard();

        if (environmentChanged)
        {
            scene.MarkDirty();
        }
        break;
    }

    case ESettingsCategory::Camera:
    {
        auto& scene = GetEngine().GetScene();
        auto& camera = scene.GetRenderCamera();

        // Card 1: Camera Optics & Presets
        BeginCard("##CameraOpticsCard");
        DrawCardHeader(ICON_FA_CAMERA, "Lens & Field of View");

        // Saved Scene Cameras
        std::vector<const char*> camerasList;
        for (const auto& cam : scene.GetEnvSettings().cameras)
        {
            camerasList.emplace_back(cam.name.c_str());
        }
        if (!camerasList.empty())
        {
            const int prevCameraIdx = userSetting.CameraIdx;
            DrawSettingRow("Scene Camera",
                           [&]()
                           {
                               ImGui::SetNextItemWidth(-FLT_MIN);
                               return ImGui::Combo("##CameraList", &userSetting.CameraIdx, camerasList.data(),
                                                   static_cast<int>(camerasList.size()));
                           });
            if (prevCameraIdx != userSetting.CameraIdx)
            {
                camera = scene.GetEnvSettings().cameras[userSetting.CameraIdx];
                modelViewController_.Reset(camera);
            }
        }

        float currentFov = modelViewController_.FieldOfView();
        if (DrawFloatSetting("Field of View", &currentFov, 15.0f, 120.0f, "%.1f deg", 0.5f))
        {
            modelViewController_.SetFieldOfView(currentFov);
        }

        // Lens Focal Length Presets
        struct LensPreset { float fov; const char* name; };
        const LensPreset lensPresets[] = {
            {84.0f, "24mm"}, {63.0f, "35mm"}, {46.0f, "50mm"}, {28.0f, "85mm"}, {18.0f, "135mm"}
        };
        const float pillAvail = ImGui::GetContentRegionAvail().x;
        const float pillWidth = std::max(20.0f, (pillAvail - 4.0f * 4.0f) / 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        for (size_t l = 0; l < IM_ARRAYSIZE(lensPresets); ++l)
        {
            if (l > 0) ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::Button(lensPresets[l].name, ImVec2(pillWidth, 22.0f)))
            {
                modelViewController_.SetFieldOfView(lensPresets[l].fov);
            }
            NextUI::Theme::DrawTooltip(lensPresets[l].name);
        }
        ImGui::PopStyleVar();
        EndCard();

        // Card 2: Depth of Field
        BeginCard("##CameraDofCard");
        DrawCardHeader(ICON_FA_CIRCLE_DOT, "Depth of Field (DoF)");
        DrawFloatSetting("Aperture", &camera.Aperture, 0.0f, 1.0f, "%.2f", 0.01f);

        // Aperture Presets
        struct AperturePreset { float ap; const char* name; };
        const AperturePreset apPresets[] = {
            {0.0f, "Off"}, {0.70f, "f/1.4"}, {0.35f, "f/2.8"}, {0.18f, "f/5.6"}, {0.09f, "f/11"}
        };
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        for (size_t a = 0; a < IM_ARRAYSIZE(apPresets); ++a)
        {
            if (a > 0) ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::Button(apPresets[a].name, ImVec2(pillWidth, 20.0f)))
            {
                camera.Aperture = apPresets[a].ap;
            }
        }
        ImGui::PopStyleVar();

        DrawFloatSetting("Focus (cm)", &camera.FocalDistance, 0.001f, 1000.0f, "%.3f", 0.05f);
        EndCard();

        // Card 3: Movement & Reset
        BeginCard("##CameraNavCard");
        DrawCardHeader(ICON_FA_COMPASS, "Navigation & Controls");
        if (ImGui::Button("Reset Camera View", ImVec2(-FLT_MIN, 28.0f)))
        {
            modelViewController_.Reset(camera);
        }
        EndCard();
        break;
    }

    case ESettingsCategory::Quality:
    {
        // Card 1: Super Resolution
        BeginCard("##QualityUpscalerCard");
        DrawCardHeader(ICON_FA_WAND_MAGIC_SPARKLES, "Super Resolution / Upscaling");

        auto upscaleMethod = userSetting.UpscalerType;
        const std::vector<std::pair<int, const char*>> methodOptions = {
            {0, "None"}, {1, "DLSS"}, {2, "DLSS-RR"}, {3, "FSR"}, {4, "TAAU"}, {5, "SGSR2"}
        };
        if (DrawSegmentedPills("##UpscaleMethodPills", methodOptions, &upscaleMethod))
        {
            userSetting.UpscalerType = upscaleMethod;
            const auto type = Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(upscaleMethod)).type;
            GetEngine().SetUpscalerConfiguration(type, userSetting.SuperResolution);
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        auto upscaleQuality = static_cast<int>(userSetting.SuperResolution);
        const std::vector<std::pair<int, const char*>> qualityOptions = {
            {0, "Quality"}, {1, "Balanced"}, {2, "Perform"}, {3, "Ultra"}, {4, "Native"}, {5, "Auto"}
        };
        if (DrawSegmentedPills("##UpscaleQualityPills", qualityOptions, &upscaleQuality))
        {
            userSetting.SuperResolution = static_cast<uint32_t>(upscaleQuality);
            GetEngine().SetUpscalerConfiguration(
                static_cast<Rendering::Upscaler::EUpscalerType>(userSetting.UpscalerType),
                static_cast<uint32_t>(upscaleQuality));
        }

        const auto& upscalerTypeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(upscaleMethod));
        if (upscalerTypeInfo.type != Rendering::Upscaler::EUpscalerType::None &&
            !GetEngine().GetRenderer().SupportsUpscaler(upscalerTypeInfo.type))
        {
            ImGui::TextDisabled("%s is not supported on this hardware.", upscalerTypeInfo.name);
        }
        if (upscalerTypeInfo.type == Rendering::Upscaler::EUpscalerType::NativeTAAU)
        {
            DrawFloatSetting("History Weight", &userSetting.NativeTAAUHistoryWeight, 0.5f, 0.98f, "%.2f", 0.01f);
            DrawFloatSetting("Sharpness", &userSetting.NativeTAAUSharpness, 0.0f, 1.0f, "%.2f", 0.01f);
        }
        EndCard();

        // Card 2: Frame Generation & Post Filter
        BeginCard("##QualityFgCard");
        DrawCardHeader(ICON_FA_SLIDERS, "Frame Generation & Filters");
        const bool canUseFrameGeneration = GetEngine().GetRenderer().SupportsFrameGeneration(upscalerTypeInfo.type);
        DrawSettingRow("Frame Generation",
                       [&]()
                       {
                           bool enabled = userSetting.FrameGeneration;
                           ImGui::BeginDisabled(!canUseFrameGeneration);
                           const bool changed = ImGui::Checkbox("##FrameGeneration", &enabled);
                           ImGui::EndDisabled();
                           if (changed)
                           {
                               userSetting.FrameGeneration = enabled && canUseFrameGeneration;
                               GetEngine().GetRenderer().RequestRecreateSwapChain();
                               return true;
                           }
                           return false;
                       });

        int frameMultiplier = static_cast<int>(std::clamp(userSetting.FrameGenerationMultiplier, 2u, 4u));
        ImGui::BeginDisabled(!canUseFrameGeneration);
        if (DrawIntSetting("FG Multiplier", &frameMultiplier, 2, 4))
        {
            userSetting.FrameGenerationMultiplier = static_cast<uint32_t>(std::clamp(frameMultiplier, 2, 4));
            GetEngine().GetRenderer().RequestRecreateSwapChain();
        }
        ImGui::EndDisabled();

        if (upscalerTypeInfo.supportsTemporalPostFilter &&
            GetEngine().GetRenderer().SupportsUpscaler(upscalerTypeInfo.type))
        {
            DrawSettingCheckboxRow("Noise Filter", &userSetting.TemporalUpscalerPostFilter);
            if (userSetting.TemporalUpscalerPostFilter)
            {
                int filterPasses = static_cast<int>(userSetting.TemporalUpscalerPostFilterPasses);
                if (DrawIntSetting("A-Trous Passes", &filterPasses, 1, 4))
                {
                    userSetting.TemporalUpscalerPostFilterPasses = static_cast<uint32_t>(filterPasses);
                }
                DrawFloatSetting("Filter Strength", &userSetting.TemporalUpscalerPostFilterStrength, 0.0f, 1.0f, "%.2f", 0.01f);
            }
        }
        EndCard();

        // Card 3: Ray Tracing Parameters
        BeginCard("##QualityRayTracingCard");
        DrawCardHeader(ICON_FA_BOLT, "Ray Tracing / Path Tracing");
        DrawSettingCheckboxRow("Progressive Render", &userSetting.ProgressiveRender);
        ImGui::BeginDisabled(userSetting.ProgressiveRender);
        DrawIntSetting("Samples (SPP)", &userSetting.NumberOfSamples, 1, 16);
        ImGui::EndDisabled();
        DrawSettingCheckboxRow("Exit After First Bounce", &userSetting.ExitAfterFirst);
        DrawFloatSetting("Indirect Intensity", &userSetting.IndirectIntensity, 0.0f, 8.0f, "%.2fx", 0.05f);
        DrawFloatSetting("Multi Bounce (SHARC)", &userSetting.MultiBounceIntensity, 0.0f, 4.0f, "%.2fx", 0.05f);
        EndCard();
        break;
    }

    case ESettingsCategory::VoxelGI:
    {
        auto& scene = GetEngine().GetScene();

        // Card 1: Ambient Cube Grid
        BeginCard("##VoxelGiGridCard");
        DrawCardHeader(ICON_FA_CUBES, "Ambient Cube Voxel Grid");
        DrawFloatSetting("Cube Unit", &userSetting.AmbientCubeUnit, 0.02f, 2.0f, "%.3f m", 0.005f);
        DrawIntSetting("Cascades", &userSetting.AmbientCubeCascadeCount, 1, Assets::CUBE_CASCADE_MAX);
        DrawFloatSetting("Cascade Ratio", &userSetting.AmbientCubeCascadeRatio, 1.0f, 8.0f, "%.2f", 0.05f);

        const float baseUnit = Assets::SanitizeAmbientCubeUnit(userSetting.AmbientCubeUnit);
        const float ratio = Assets::SanitizeAmbientCubeCascadeRatio(userSetting.AmbientCubeCascadeRatio);
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(userSetting.AmbientCubeCascadeCount),
            std::max(1u, scene.AmbientCubeCascadeCapacity()));
        const float outerUnit = Assets::CalculateAmbientCubeCascadeUnit(baseUnit, ratio, cascadeCount - 1);
        DrawSettingRow("Coverage",
                       [&]()
                       {
                           ImGui::TextDisabled("%.1f x %.1f x %.1f m",
                                               outerUnit * Assets::CUBE_SIZE_XY,
                                               outerUnit * Assets::CUBE_SIZE_Z,
                                               outerUnit * Assets::CUBE_SIZE_XY);
                           return false;
                       });
        EndCard();

        // Card 2: Bake Controller & Performance
        BeginCard("##VoxelGiBakeCard");
        DrawCardHeader(ICON_FA_GAUGE_HIGH, "Real-time Bake Controller");
        int bakeTargetFps = static_cast<int>(std::clamp(userSetting.AmbientCubeBakeTargetFps, 1u, 240u));
        if (DrawIntSetting("Bake Target FPS", &bakeTargetFps, 1, 240))
        {
            userSetting.AmbientCubeBakeTargetFps = static_cast<uint32_t>(bakeTargetFps);
        }

        const auto probeProgress = scene.GetCPUAccelerationStructure().GetProbeBakeProgress();
        const auto ambientProgress = GetEngine().GetRenderer().GetAmbientBakeProgress();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (ambientProgress.active)
        {
            const float frac = ambientProgress.totalDispatchGroups > 0
                ? static_cast<float>(ambientProgress.completedDispatchGroups) / static_cast<float>(ambientProgress.totalDispatchGroups)
                : 0.0f;
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 16.0f), "GPU Ambient Bake Active");
        }
        else
        {
            ImGui::TextDisabled("Voxel GI status: Steady / Cached");
        }
        EndCard();
        break;
    }

    case ESettingsCategory::Scene:
    {
        // Card 1: Scene Library
        BeginCard("##SceneLibraryCard");
        DrawCardHeader(ICON_FA_FOLDER_OPEN, "Scene Library & Loading");

        static char searchBuffer[64] = "";
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##SceneSearch", "Search scenes...", searchBuffer, IM_ARRAYSIZE(searchBuffer));

        // Group Filter Chips
        static int groupFilter = -1; // -1 = all
        const std::vector<std::pair<int, const char*>> filterOptions = {
            {-1, "All"}, {0, "Proc"}, {1, "glTF"}, {2, "LDraw"}
        };
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        DrawSegmentedPills("##SceneFilterPills", filterOptions, &groupFilter);

        std::string searchLower = searchBuffer;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::BeginChild("##SceneItemsList", ImVec2(0.0f, 260.0f), true);
        for (int i = 0; i < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size()); ++i)
        {
            const auto& scenePath = Runtime::Scene::SceneList::AllScenes[i];
            const std::string filename = std::filesystem::path(scenePath).filename().string();

            // Group match
            if (groupFilter >= 0)
            {
                const auto g = static_cast<int>(GetSceneListGroup(scenePath));
                if (g != groupFilter) continue;
            }

            // Search match
            if (!searchLower.empty())
            {
                std::string fnameLower = filename;
                std::transform(fnameLower.begin(), fnameLower.end(), fnameLower.begin(), ::tolower);
                if (fnameLower.find(searchLower) == std::string::npos) continue;
            }

            const bool isCurrent = (i == userSetting.SceneIndex);
            std::string label = fmt::format("{}  {}", isCurrent ? ICON_FA_CHECK : "  ", filename);
            if (ImGui::Selectable(label.c_str(), isCurrent))
            {
                userSetting.SceneIndex = i;
                GetEngine().RequestLoadScene({.filename = scenePath});
            }
        }
        ImGui::EndChild();
        EndCard();

        // Card 2: Model Level of Detail
        BeginCard("##SceneLodCard");
        DrawCardHeader(ICON_FA_LAYER_GROUP, "Model Level of Detail (LOD)");
        const auto& scene = GetEngine().GetScene();
        DrawSettingRow("Models Count",
                       [&]()
                       {
                           ImGui::TextDisabled("%zu models loaded", scene.Models().size());
                           return false;
                       });
        DrawSettingRow("Nodes Count",
                       [&]()
                       {
                           ImGui::TextDisabled("%zu scene nodes", scene.Nodes().size());
                           return false;
                       });
        if (ImGui::Button("Reload Active Scene", ImVec2(-FLT_MIN, 26.0f)))
        {
            if (userSetting.SceneIndex >= 0 && userSetting.SceneIndex < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size()))
            {
                GetEngine().RequestLoadScene({.filename = Runtime::Scene::SceneList::AllScenes[userSetting.SceneIndex]});
            }
        }
        EndCard();
        break;
    }

    case ESettingsCategory::Animation:
    {
        // Card 1: Simulation
        BeginCard("##AnimSimCard");
        DrawCardHeader(ICON_FA_PLAY, "Simulation State");
        DrawSettingCheckboxRow("Tick Animation", &userSetting.TickAnimation);
        DrawSettingCheckboxRow("Tick Physics", &userSetting.TickPhysics);
        EndCard();

        // Card 2: Skinned Mesh Player
        BeginCard("##AnimPlayerCard");
        DrawCardHeader(ICON_FA_PERSON_RUNNING, "Skeletal Mesh Player");
        bool hasSkinnedMeshes = false;
        for (auto* skinnedMesh : GetEngine().GetScene().Components<Runtime::SkinnedMeshComponent>())
        {
            if (Assets::Node* node = skinnedMesh->GetOwner())
            {
                hasSkinnedMeshes = true;
                ImGui::PushID(node->GetName().c_str());
                ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Text), "%s", node->GetName().c_str());
                auto animNames = skinnedMesh->GetAnimationNames();
                if (!animNames.empty())
                {
                    std::string current = skinnedMesh->GetCurrentAnimationName();
                    int selectedAnim = -1;
                    for (int i = 0; i < static_cast<int>(animNames.size()); ++i)
                    {
                        if (animNames[i] == current)
                        {
                            selectedAnim = i;
                            break;
                        }
                    }

                    std::vector<const char*> animPtrs;
                    for (const auto& name : animNames) animPtrs.push_back(name.c_str());

                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##AnimList", &selectedAnim, animPtrs.data(), static_cast<int>(animPtrs.size())))
                    {
                        skinnedMesh->PlayAnimation(animNames[selectedAnim]);
                    }

                    float speed = skinnedMesh->GetPlaySpeed();
                    if (DrawFloatSetting("Speed", &speed, -2.0f, 2.0f, "%.2f", 0.05f))
                    {
                        skinnedMesh->SetPlaySpeed(speed);
                    }

                    // Speed pills
                    struct SpeedPreset { float s; const char* label; };
                    const SpeedPreset spPresets[] = {
                        {-1.0f, "-1x"}, {0.5f, "0.5x"}, {1.0f, "1.0x"}, {2.0f, "2.0x"}
                    };
                    const float spAvail = ImGui::GetContentRegionAvail().x;
                    const float spWidth = std::max(20.0f, (spAvail - 4.0f * 3.0f) / 4.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                    for (size_t sp = 0; sp < IM_ARRAYSIZE(spPresets); ++sp)
                    {
                        if (sp > 0) ImGui::SameLine(0.0f, 4.0f);
                        if (ImGui::Button(spPresets[sp].label, ImVec2(spWidth, 20.0f)))
                        {
                            skinnedMesh->SetPlaySpeed(spPresets[sp].s);
                        }
                    }
                    ImGui::PopStyleVar();
                }
                else
                {
                    ImGui::TextDisabled("No animation tracks found.");
                }
                ImGui::PopID();
            }
        }
        if (!hasSkinnedMeshes)
        {
            ImGui::TextDisabled("No skinned meshes in the current scene.");
        }
        EndCard();

        // Card 3: Physics Spawning
        BeginCard("##AnimPhysicsCard");
        DrawCardHeader(ICON_FA_BOWLING_BALL, "Physics Spawning Playground");
        if (ImGui::Button("Spawn Physics Box", ImVec2(-FLT_MIN, 26.0f)))
        {
            CreateBoxAndPush();
        }
        if (ImGui::Button("Drop 400 Spheres Grid", ImVec2(-FLT_MIN, 26.0f)))
        {
            DropPhysicsSphereGrid();
        }
        EndCard();
        break;
    }

    case ESettingsCategory::PostProcess:
    {
        // Card 1: Color & Luminance
        BeginCard("##PostColorCard");
        DrawCardHeader(ICON_FA_PALETTE, "Color & Luminance");
        DrawFloatSetting("Paper White Nit", &userSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f Nit", 1.0f);
        DrawFloatSetting("Time Scaling", &userSetting.HeatmapScale, 0.10f, 2.0f, "%.2f", 0.05f);

        uint32_t tmin = 8, tmax = 32;
        int temporalFrames = static_cast<int>(userSetting.TemporalFrames);
        if (DrawIntSetting("Motion History", &temporalFrames, static_cast<int>(tmin), static_cast<int>(tmax)))
        {
            userSetting.TemporalFrames = static_cast<uint32_t>(temporalFrames);
        }
        EndCard();

        // Card 2: Capture Studio
        BeginCard("##PostCaptureCard");
        DrawCardHeader(ICON_FA_CAMERA, "Screen Capture & Benchmark");
        if (ImGui::Button("Take Clean Screenshot", ImVec2(-FLT_MIN, 26.0f)))
        {
            RequestScreenshot(true, "manual");
        }

        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextDisabled("Record 3s Benchmark Video:");
        const float btnAvail = ImGui::GetContentRegionAvail().x;
        const float btnW = std::max(20.0f, (btnAvail - 4.0f * 2.0f) / 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        if (ImGui::Button("100% Scale", ImVec2(btnW, 22.0f)))
        {
            RequestThreeSecondVideo(Runtime::IScreenShotService::EVideoOutputScale::Full);
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("50% Scale", ImVec2(btnW, 22.0f)))
        {
            RequestThreeSecondVideo(Runtime::IScreenShotService::EVideoOutputScale::Half);
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("25% Scale", ImVec2(btnW, 22.0f)))
        {
            RequestThreeSecondVideo(Runtime::IScreenShotService::EVideoOutputScale::Quarter);
        }
        ImGui::PopStyleVar();
        EndCard();
        break;
    }

    default:
        break;
    }

    NextUI::Theme::EndDetailPanel();
}
