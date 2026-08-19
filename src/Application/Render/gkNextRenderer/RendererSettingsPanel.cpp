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
extern float ModeRailWidth;

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
        return "FIFO";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        return "FIFO Relaxed";
    default:
        return "Unknown";
    }
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
    constexpr float panelWidth = 360.0f;
    constexpr float panelMargin = 10.0f;
    const ImVec2 panelPos = viewport->Pos + ImVec2(ModeRailWidth + panelMargin, TitlebarSize + panelMargin);
    const ImVec2 panelSize(panelWidth,
                           viewport->Size.y - TitlebarSize - 50.0f - panelMargin);

    NextUI::Theme::FDetailPanelConfig panelConfig{};
    panelConfig.WindowId = "##RendererSettingsPanel";
    panelConfig.ContentWindowId = "##RendererSettingsContent";
    panelConfig.Icon = ICON_FA_SLIDERS;
    panelConfig.Title = "Renderer Settings";
    panelConfig.Open = &uiState.showSettings;
    panelConfig.Position = panelPos;
    panelConfig.Size = panelSize;
    if (!NextUI::Theme::BeginDetailPanel(panelConfig))
    {
        return;
    }

    auto DrawFloatSetting = [&](const char* label, float* value, float minValue, float maxValue,
                                const char* format, float dragSpeed)
    {
        return DrawSettingSliderRow(label, ImGuiDataType_Float, value, minValue, maxValue, format, dragSpeed);
    };

    auto DrawIntSetting = [&](const char* label, int* value, int minValue, int maxValue,
                              const char* format = "%d")
    {
        return DrawSettingSliderRow(label, ImGuiDataType_S32, value, minValue, maxValue, format, 1.0f);
    };

    auto DrawFloat3Setting = [&](const char* label, const char* id, glm::vec3* value,
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
    };

    auto DrawColorSetting = [&](const char* label, const char* id, glm::vec3* value)
    {
        return DrawSettingRow(label,
                              [&]()
                              {
                                  ImGui::SetNextItemWidth(-FLT_MIN);
                                  return ImGui::ColorEdit3(id, &value->x);
                              });
    };

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Renderer"), true))
    {
        DrawSettingRow(LOCTEXT("Renderer"),
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), userSetting, "##RendererList");
                           return false;
                       });
        DrawSettingRow(LOCTEXT("Present Mode"),
                       [&]()
                       {
                           static constexpr VkPresentModeKHR presentModes[] = {
                               VK_PRESENT_MODE_IMMEDIATE_KHR,
                               VK_PRESENT_MODE_MAILBOX_KHR,
                               VK_PRESENT_MODE_FIFO_KHR,
                               VK_PRESENT_MODE_FIFO_RELAXED_KHR,
                           };

                           int selectedMode = 0;
                           const VkPresentModeKHR requestedPresentMode =
                               static_cast<VkPresentModeKHR>(userSetting.PresentMode);
                           for (int i = 0; i < static_cast<int>(std::size(presentModes)); ++i)
                           {
                               if (presentModes[i] == requestedPresentMode)
                               {
                                   selectedMode = i;
                                   break;
                               }
                           }

                           const char* labels[] = {"Immediate", "Mailbox", "FIFO", "FIFO Relaxed"};
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##PresentMode", &selectedMode, labels, IM_ARRAYSIZE(labels)))
                           {
                               const VkPresentModeKHR nextPresentMode = presentModes[selectedMode];
                               userSetting.PresentMode = static_cast<uint32_t>(nextPresentMode);
                               GetEngine().GetRenderer().SetRequestedPresentMode(nextPresentMode);
                               return true;
                           }
                           return false;
                       });
        if (GetEngine().GetRenderer().HasSwapChain())
        {
            const VkPresentModeKHR actualPresentMode = GetEngine().GetRenderer().SwapChain().PresentMode();
            if (actualPresentMode != static_cast<VkPresentModeKHR>(userSetting.PresentMode))
            {
                ImGui::TextDisabled("Actual present mode: %s", GetPresentModeLabel(actualPresentMode));
            }
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Scene"), true))
    {
        std::vector<std::string> sceneNames;
        sceneNames.reserve(Runtime::Scene::SceneList::AllScenes.size());
        for (const auto& scene : Runtime::Scene::SceneList::AllScenes)
        {
            sceneNames.push_back(std::filesystem::path(scene).filename().string());
        }

        const char* currentScenePreview =
            (userSetting.SceneIndex >= 0 && userSetting.SceneIndex < static_cast<int>(sceneNames.size()))
                ? sceneNames[userSetting.SceneIndex].c_str()
                : "";
        DrawSettingComboRow(LOCTEXT("Scene"), currentScenePreview,
                            [&]() -> bool
                            {
                                bool changed = false;
                                ESceneListGroup currentGroup = ESceneListGroup::Other;
                                bool hasGroup = false;
                                for (int sceneIdx = 0; sceneIdx < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size()); ++sceneIdx)
                                {
                                    const ESceneListGroup sceneGroup = GetSceneListGroup(Runtime::Scene::SceneList::AllScenes[sceneIdx]);
                                    if (!hasGroup || sceneGroup != currentGroup)
                                    {
                                        if (hasGroup)
                                        {
                                            ImGui::Separator();
                                        }
                                        currentGroup = sceneGroup;
                                        hasGroup = true;
                                        ImGui::TextDisabled("%s", GetSceneListGroupLabel(sceneGroup));
                                    }

                                    const bool selected = (sceneIdx == userSetting.SceneIndex);
                                    if (ImGui::Selectable(sceneNames[sceneIdx].c_str(), selected))
                                    {
                                        userSetting.SceneIndex = sceneIdx;
                                        GetEngine().RequestLoadScene({.filename = Runtime::Scene::SceneList::AllScenes[userSetting.SceneIndex]});
                                        changed = true;
                                    }
                                    if (selected)
                                    {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                }
                                return changed;
                            });
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Camera"), true))
    {
        std::vector<const char*> camerasList;
        for (const auto& cam : GetEngine().GetScene().GetEnvSettings().cameras)
        {
            camerasList.emplace_back(cam.name.c_str());
        }

        const int prevCameraIdx = userSetting.CameraIdx;
        DrawSettingRow(LOCTEXT("Camera"),
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           return ImGui::Combo("##CameraList", &userSetting.CameraIdx, camerasList.data(),
                                               static_cast<int>(camerasList.size()));
                       });
        if (prevCameraIdx != userSetting.CameraIdx)
        {
            GetEngine().GetScene().GetRenderCamera() =
                GetEngine().GetScene().GetEnvSettings().cameras[userSetting.CameraIdx];
            modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
        }

        auto& camera = GetEngine().GetScene().GetRenderCamera();
        DrawFloatSetting(LOCTEXT("Aperture"), &camera.Aperture, 0.0f, 1.0f, "%.2f", 0.01f);
        DrawFloatSetting(LOCTEXT("Focus(cm)"), &camera.FocalDistance, 0.001f, 1000.0f, "%.3f", 0.05f);
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Environment"), true))
    {
        auto& scene = GetEngine().GetScene();
        auto& environment = scene.GetEnvSettings();
        auto& atmosphere = environment.Atmosphere;
        bool environmentChanged = false;

        ImGui::SeparatorText(LOCTEXT("Lighting"));
        environmentChanged |= DrawSettingCheckboxRow(LOCTEXT("HasSky"), &environment.HasSky);
        if (environment.HasSky)
        {
            environmentChanged |= DrawIntSetting(LOCTEXT("SkyIdx"), &environment.SkyIdx, 0, 10);
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SkyRotation"), &environment.SkyRotation, 0.0f, 2.0f, "%.2f", 0.01f);
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SkyLum"), &environment.SkyIntensity, 0.0f, 1000.0f, "%.0f", 1.0f);
            environmentChanged |= DrawColorSetting(
                LOCTEXT("SkyColor"), "##EnvironmentSkyColor", &environment.SkyColor);
        }

        environmentChanged |= DrawSettingCheckboxRow(LOCTEXT("HasSun"), &environment.HasSun);
        if (environment.HasSun)
        {
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SunRotation"), &environment.SunRotation, 0.0f, 2.0f, "%.2f", 0.01f);
            environmentChanged |= DrawFloatSetting(
                LOCTEXT("SunLum"), &environment.SunIntensity, 0.0f, 2000.0f, "%.0f", 1.0f);
            environmentChanged |= DrawColorSetting(
                LOCTEXT("SunColor"), "##EnvironmentSunColor", &environment.SunColor);
        }
        DrawFloatSetting(
            LOCTEXT("PaperWhitNit"), &userSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f", 1.0f);

        ImGui::SeparatorText("Atmosphere");
        environmentChanged |= DrawSettingCheckboxRow(
            "Enabled", &environment.AtmosphereEnabled);

        float sunElevationDegrees = glm::degrees(environment.SunElevation);
        if (DrawFloatSetting(
                "Sun Elevation", &sunElevationDegrees, -24.0f, 90.0f, "%.1f deg", 0.25f))
        {
            environment.SunElevation = glm::radians(sunElevationDegrees);
            environmentChanged = true;
        }
        environmentChanged |= DrawFloatSetting(
            "Sky Luminance", &atmosphere.SkyLuminanceScale, 0.0f, 10.0f, "%.2f", 0.01f);
        environmentChanged |= DrawFloatSetting(
            "Sky LUT Scale", &userSetting.AtmosphereSkyViewLutScale,
            0.25f, 2.0f, "%.2fx", 0.05f);

        static constexpr const char* debugModes[] = {
            "Off", "In-Scatter", "Transmittance", "SkyView LUT"};
        environmentChanged |= DrawSettingRow(
            "Debug View",
            [&]()
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                return ImGui::Combo(
                    "##AtmosphereDebug", &userSetting.AtmosphereDebugMode,
                    debugModes, IM_ARRAYSIZE(debugModes));
            });

        ImGui::SeparatorText("Aerial Perspective");
        ImGui::PushID("AerialPerspective");
        environmentChanged |= DrawSettingCheckboxRow(
            "Enabled", &environment.AerialPerspectiveEnabled);
        ImGui::PopID();
        ImGui::BeginDisabled(!environment.AtmosphereEnabled ||
                             !environment.AerialPerspectiveEnabled);
        environmentChanged |= DrawFloatSetting(
            "Max Distance", &atmosphere.AerialPerspectiveMaxDistance,
            10.0f, 50000.0f, "%.0f", 10.0f);
        ImGui::EndDisabled();

        ImGui::SeparatorText("Height Fog");
        ImGui::PushID("HeightFog");
        environmentChanged |= DrawSettingCheckboxRow(
            "Enabled", &environment.HeightFogEnabled);
        ImGui::PopID();
        ImGui::BeginDisabled(!environment.HeightFogEnabled);
        environmentChanged |= DrawColorSetting(
            "Fog Color", "##AtmosphereFogColor", &atmosphere.FogInscatteringColor);
        environmentChanged |= DrawFloatSetting(
            "Density", &atmosphere.FogDensity, 0.0f, 0.1f, "%.4f", 0.0001f);
        environmentChanged |= DrawFloatSetting(
            "Height Falloff", &atmosphere.FogHeightFalloff, 0.0f, 2.0f, "%.3f", 0.005f);
        environmentChanged |= DrawFloatSetting(
            "Base Height", &atmosphere.FogBaseHeight, -2000.0f, 2000.0f, "%.1f", 0.5f);
        environmentChanged |= DrawFloatSetting(
            "Start Distance", &atmosphere.FogStartDistance, 0.0f, 10000.0f, "%.1f", 1.0f);
        environmentChanged |= DrawFloatSetting(
            "Max Opacity", &atmosphere.FogMaxOpacity, 0.0f, 1.0f, "%.2f", 0.01f);
        ImGui::EndDisabled();

        if (ImGui::TreeNodeEx("Advanced Atmosphere", ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            environmentChanged |= DrawFloat3Setting(
                "Rayleigh", "##AtmosphereRayleigh", &atmosphere.RayleighScattering,
                0.00001f, 0.0f, 0.1f, "%.6f");
            environmentChanged |= DrawFloatSetting(
                "Rayleigh Height", &atmosphere.RayleighDensityH,
                0.1f, 32.0f, "%.2f km", 0.05f);
            environmentChanged |= DrawFloat3Setting(
                "Mie Scatter", "##AtmosphereMieScatter", &atmosphere.MieScattering,
                0.00001f, 0.0f, 0.1f, "%.6f");
            environmentChanged |= DrawFloat3Setting(
                "Mie Absorption", "##AtmosphereMieAbsorption", &atmosphere.MieAbsorption,
                0.00001f, 0.0f, 0.1f, "%.6f");
            environmentChanged |= DrawFloatSetting(
                "Mie Height", &atmosphere.MieDensityH,
                0.1f, 16.0f, "%.2f km", 0.05f);
            environmentChanged |= DrawFloatSetting(
                "Mie Anisotropy", &atmosphere.MiePhaseG,
                0.0f, 0.99f, "%.3f", 0.005f);
            environmentChanged |= DrawFloat3Setting(
                "Ozone Absorption", "##AtmosphereOzone", &atmosphere.OzoneAbsorption,
                0.00001f, 0.0f, 0.05f, "%.6f");
            environmentChanged |= DrawFloatSetting(
                "Ozone Center", &atmosphere.OzoneCenterAltitude,
                0.0f, 60.0f, "%.1f km", 0.1f);
            environmentChanged |= DrawFloatSetting(
                "Ozone Width", &atmosphere.OzoneWidth,
                0.1f, 40.0f, "%.1f km", 0.1f);
            environmentChanged |= DrawColorSetting(
                "Ground Albedo", "##AtmosphereGroundAlbedo", &atmosphere.GroundAlbedo);
            environmentChanged |= DrawFloatSetting(
                "World Units / km", &atmosphere.WorldUnitsPerKm,
                0.001f, 10000.0f, "%.3f", 1.0f);
            environmentChanged |= DrawFloatSetting(
                "Origin Altitude", &atmosphere.WorldOriginAltitude,
                -10.0f, 100.0f, "%.2f km", 0.05f);
            environmentChanged |= DrawSettingRow(
                "Preset",
                [&]()
                {
                    if (!ImGui::Button("Earth Defaults", ImVec2(-FLT_MIN, 0.0f)))
                    {
                        return false;
                    }
                    atmosphere.Reset();
                    return true;
                });
            ImGui::TreePop();
        }

        if (userSetting.TickAnimation)
        {
            ImGui::TextDisabled("Disable Tick Animation to hold a manual sun angle.");
        }
        if (environmentChanged)
        {
            scene.MarkDirty();
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Ray Tracing"), true))
    {
        DrawSettingCheckboxRow(LOCTEXT("Progressive Render"), &userSetting.ProgressiveRender);
        ImGui::BeginDisabled(userSetting.ProgressiveRender);
        DrawIntSetting(LOCTEXT("Samples"), &userSetting.NumberOfSamples, 1, 16);
        ImGui::EndDisabled();
        if (userSetting.ProgressiveRender)
        {
            NextUI::Theme::DrawTooltip("Progressive rendering always uses 1 spp per frame");
        }
        DrawSettingCheckboxRow(LOCTEXT("Exit After First"), &userSetting.ExitAfterFirst);
        DrawFloatSetting(LOCTEXT("Indirect Intensity"), &userSetting.IndirectIntensity,
                         0.0f, 8.0f, "%.2fx", 0.05f);
        NextUI::Theme::DrawTooltip(
            "Scales bounce light only (SHARC cache hit + ambient cube terminal). "
            "Direct sun and sky are untouched, so this lifts GI without raising contrast. 1 = physical.");
        DrawFloatSetting(LOCTEXT("Multi Bounce Intensity"), &userSetting.MultiBounceIntensity,
                         0.0f, 4.0f, "%.2fx", 0.05f);
        NextUI::Theme::DrawTooltip(
            "Weights bounce orders past the first: order n scales by this^(n-1). "
            "0 keeps only once-bounced direct light, 1 = physical. PathTracing (SHARC) only.");
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Ambient Cube Grid"), true))
    {
        const Assets::Scene& scene = GetEngine().GetScene();
        // Cascade 0 voxel size. The probe count per cascade is fixed, so a smaller unit resolves
        // finer detail across a proportionally smaller volume: worth lowering for interiors, not for
        // open terrain. Editing any of these re-voxelizes the scene and re-bakes every cascade;
        // shading keeps using the previous grid until the bake has adopted the new one.
        DrawFloatSetting(LOCTEXT("Cube Unit"), &userSetting.AmbientCubeUnit,
                         0.02f, 2.0f, "%.3f m", 0.005f);
        DrawIntSetting(LOCTEXT("Cascades"), &userSetting.AmbientCubeCascadeCount,
                       1, Assets::CUBE_CASCADE_MAX);
        DrawFloatSetting(LOCTEXT("Cascade Ratio"), &userSetting.AmbientCubeCascadeRatio,
                         1.0f, 8.0f, "%.2f", 0.05f);
        int bakeTargetFps = static_cast<int>(std::clamp(userSetting.AmbientCubeBakeTargetFps, 1u, 240u));
        if (DrawIntSetting(LOCTEXT("Bake Target FPS"), &bakeTargetFps, 1, 240))
        {
            userSetting.AmbientCubeBakeTargetFps = static_cast<uint32_t>(bakeTargetFps);
        }
        NextUI::Theme::DrawTooltip(
            "Ambient cube bake scales its next dispatch from total frame time to preserve this target FPS. "
            "GPU timestamp queries are not required.");

        const float baseUnit = Assets::SanitizeAmbientCubeUnit(userSetting.AmbientCubeUnit);
        const float ratio = Assets::SanitizeAmbientCubeCascadeRatio(userSetting.AmbientCubeCascadeRatio);
        const uint32_t cascadeCount = std::min(
            Assets::SanitizeAmbientCubeCascadeCount(userSetting.AmbientCubeCascadeCount),
            std::max(1u, scene.AmbientCubeCascadeCapacity()));
        const float outerUnit = Assets::CalculateAmbientCubeCascadeUnit(baseUnit, ratio, cascadeCount - 1);
        DrawSettingRow(LOCTEXT("Coverage"),
                       [&]()
                       {
                           ImGui::TextDisabled("%.1f x %.1f x %.1f m",
                                               outerUnit * Assets::CUBE_SIZE_XY,
                                               outerUnit * Assets::CUBE_SIZE_Z,
                                               outerUnit * Assets::CUBE_SIZE_XY);
                           return false;
                       });
        if (cascadeCount < Assets::SanitizeAmbientCubeCascadeCount(userSetting.AmbientCubeCascadeCount))
        {
            ImGui::TextDisabled("Scene allocated %u cascades; reload to use more.",
                                scene.AmbientCubeCascadeCapacity());
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Upscaling"), true))
    {
        int upscaleMethod = userSetting.UpscalerType;
        const char* methods[] = {
            "None", "DLSS", "DLSS Ray Reconstruction", "FidelityFX FSR",
            "Native TAAU", "SGSR2 (2-pass CS)"};
        DrawSettingRow("Method",
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##UpscaleMethod", &upscaleMethod, methods, IM_ARRAYSIZE(methods)))
                           {
                               const auto type = Rendering::Upscaler::GetUpscalerTypeInfo(
                                   static_cast<uint32_t>(upscaleMethod)).type;
                               GetEngine().SetUpscalerConfiguration(type, userSetting.SuperResolution);
                               return true;
                           }
                           return false;
                       });

        const char* qualities[] = {"Quality", "Balanced", "Performance", "Ultra Performance", "Native", "Auto"};
        int upscaleQuality = static_cast<int>(userSetting.SuperResolution);
        DrawSettingRow("Quality",
                       [&]()
                       {
                           ImGui::SetNextItemWidth(-FLT_MIN);
                           if (ImGui::Combo("##UpscaleQuality", &upscaleQuality, qualities,
                                            IM_ARRAYSIZE(qualities)))
                           {
                               GetEngine().SetUpscalerConfiguration(
                                   static_cast<Rendering::Upscaler::EUpscalerType>(userSetting.UpscalerType),
                                   static_cast<uint32_t>(upscaleQuality));
                               return true;
                           }
                           return false;
                       });

        const auto& upscalerTypeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(upscaleMethod));
        if (upscalerTypeInfo.type != Rendering::Upscaler::EUpscalerType::None &&
            !GetEngine().GetRenderer().SupportsUpscaler(upscalerTypeInfo.type))
        {
            ImGui::TextDisabled("%s is not supported on this hardware.", upscalerTypeInfo.name);
        }
        if (upscalerTypeInfo.type == Rendering::Upscaler::EUpscalerType::NativeTAAU)
        {
            DrawFloatSetting("History Weight", &userSetting.NativeTAAUHistoryWeight,
                             0.5f, 0.98f, "%.2f", 0.01f);
            DrawFloatSetting("Sharpness", &userSetting.NativeTAAUSharpness,
                             0.0f, 1.0f, "%.2f", 0.01f);
        }

        const bool supportsTemporalNoiseFilter = upscalerTypeInfo.supportsTemporalPostFilter &&
            GetEngine().GetRenderer().SupportsUpscaler(upscalerTypeInfo.type);
        if (supportsTemporalNoiseFilter)
        {
            DrawSettingCheckboxRow("Noise Filter", &userSetting.TemporalUpscalerPostFilter);
            ImGui::BeginDisabled(!userSetting.TemporalUpscalerPostFilter);
            int filterPasses = static_cast<int>(userSetting.TemporalUpscalerPostFilterPasses);
            if (DrawIntSetting("A-Trous Passes", &filterPasses, 1, 4))
            {
                userSetting.TemporalUpscalerPostFilterPasses = static_cast<uint32_t>(filterPasses);
            }
            DrawFloatSetting("Filter Strength", &userSetting.TemporalUpscalerPostFilterStrength, 0.0f, 1.0f, "%.2f", 0.01f);
            DrawFloatSetting("Edge Sigma", &userSetting.TemporalUpscalerPostFilterLumaSigma, 0.01f, 0.5f, "%.2f", 0.01f);
            ImGui::EndDisabled();
        }
        DrawFloatSetting("Firefly Sigma", &userSetting.TemporalUpscalerFireflySigma, 0.1f, 16.0f, "%.1f", 0.1f);

        const bool canUseFrameGeneration =
            GetEngine().GetRenderer().SupportsFrameGeneration(upscalerTypeInfo.type);
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

        int frameLimitFps = static_cast<int>(std::min(userSetting.FrameGenerationFrameLimitFps, 1000u));
        if (DrawIntSetting("FG Base FPS Limit", &frameLimitFps, 0, 1000))
        {
            userSetting.FrameGenerationFrameLimitFps = static_cast<uint32_t>(std::clamp(frameLimitFps, 0, 1000));
        }

        const auto frameGenerationState = GetEngine().GetRenderer().GetFrameGenerationState();
        if (userSetting.FrameGeneration && frameGenerationState.valid)
        {
            ImGui::TextDisabled("Frame Generation presented x%u, status 0x%X",
                                frameGenerationState.numFramesActuallyPresented,
                                frameGenerationState.statusMask);
        }
        if (userSetting.FrameGeneration && !canUseFrameGeneration)
        {
            ImGui::TextDisabled("Frame generation is unavailable for the selected upscaler.");
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Animation"), false))
    {
        DrawSettingCheckboxRow(LOCTEXT("Tick Animation"), &userSetting.TickAnimation);
        DrawSettingCheckboxRow(LOCTEXT("TickPhysics"), &userSetting.TickPhysics);
        
        ImGui::Separator();
        for (auto* skinnedMesh : GetEngine().GetScene().Components<Runtime::SkinnedMeshComponent>())
        {
            if (Assets::Node* node = skinnedMesh->GetOwner())
            {
                ImGui::PushID(node->GetName().c_str());
                ImGui::Text("%s", node->GetName().c_str());
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

                    if (ImGui::Combo("##AnimList", &selectedAnim, animPtrs.data(),
                                     static_cast<int>(animPtrs.size())))
                    {
                        skinnedMesh->PlayAnimation(animNames[selectedAnim]);
                    }

                    float speed = skinnedMesh->GetPlaySpeed();
                    if (ImGui::SliderFloat("Speed", &speed, -2.0f, 2.0f, "%.2f"))
                    {
                        skinnedMesh->SetPlaySpeed(speed);
                    }
                }
                else
                {
                    ImGui::TextDisabled("No animations");
                }
                ImGui::PopID();
            }
        }
        NextUI::Theme::EndPanelSection();
    }

    if (NextUI::Theme::BeginPanelSection(LOCTEXT("Misc"), false))
    {
        ImGui::SliderFloat(LOCTEXT("Time Scaling"), &userSetting.HeatmapScale, 0.10f, 2.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::Spacing();
        uint32_t tmin = 8, tmax = 32;
        ImGui::SliderScalar(LOCTEXT("Motion History"), ImGuiDataType_U32, &userSetting.TemporalFrames, &tmin,
                            &tmax);
        NextUI::Theme::EndPanelSection();
    }

    NextUI::Theme::EndDetailPanel();
}
