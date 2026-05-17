#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <array>

#include "Assets/Loaders/FProcModel.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Editor/FontLoader.h"
#include "Runtime/Editor/ProfessionalUI.hpp"
#include "Runtime/Editor/UserInterface.hpp"
#include "Runtime/Scene/SceneBuilder.h"
#include "Runtime/Utilities/NextEngineHelper.h"
#include "Runtime/Utilities/GraphicsDebugPanel.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/ImGui.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Runtime/ScreenShot.hpp"
#include "Utilities/FileHelper.hpp"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Config/CVarSystem.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Device.hpp"

extern float GAndroidMagicScale;

namespace
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

template <typename T>
bool DrawSettingSliderRow(const char* label, ImGuiDataType dataType, T* value,
                          T minValue, T maxValue, const char* format, float dragSpeed,
                          float valueWidth = 84.0f)
{
    Runtime::UiTheme::LabelOver(label);
    ImGui::PushID(label);

    const float sliderWidth = std::max(40.0f, ImGui::GetContentRegionAvail().x - valueWidth - 8.0f);
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::SetNextItemWidth(sliderWidth);
    changed |= ImGui::SliderScalar("##Slider", dataType, value, &minValue, &maxValue, format);
    ImGui::PopStyleColor();

    ImGui::SameLine(0.0f, 8.0f);
    ImGui::SetNextItemWidth(valueWidth);
    changed |= ImGui::DragScalar("##Value", dataType, value, dragSpeed, &minValue, &maxValue, format);

    ImGui::PopID();
    return changed;
}
} // namespace

// should use 1em instead of 1px
constexpr float constTitlebarSize = 44;
constexpr float constTitlebarRightInfoWidth = 296;
constexpr float constIconSize = 64;
constexpr float constPaletteSize = 46;
constexpr float constButtonSize = 36;
constexpr float constBuildBarWidth = 240;
constexpr float constSideBarWidth = 300;
constexpr float constShortcutSize = 10;
constexpr float constModeRailWidth = 56;
constexpr float constModeRailButtonSize = 40;

float TitlebarSize = constTitlebarSize;
float TitlebarRightInfoWidth = constTitlebarRightInfoWidth;
float IconSize = constIconSize;
float PaletteSize = constPaletteSize;
float ButtonSize = constButtonSize;
float BuildBarWidth = constBuildBarWidth;
float SideBarWidth = constSideBarWidth;
float ShortcutSize = constShortcutSize;
float ModeRailWidth = constModeRailWidth;
float ModeRailButtonSize = constModeRailButtonSize;

static void UpdateUiScaledMetrics()
{
    float scale = 1.0f;

    if (GAndroidMagicScale < 1.0f)
    {
        scale *= 0.75f / GAndroidMagicScale;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        const float fontSize = ImGui::GetFontSize();
        if (fontSize > 0.0f)
        {
            constexpr float referenceFontSize = 16.0f;
            scale *= fontSize / referenceFontSize;
        }
    }

    TitlebarSize = constTitlebarSize * scale;
    TitlebarRightInfoWidth = constTitlebarRightInfoWidth * scale;
    IconSize = constIconSize * scale;
    PaletteSize = constPaletteSize * scale;
    ButtonSize = constButtonSize * scale;
    BuildBarWidth = constBuildBarWidth * scale;
    SideBarWidth = constSideBarWidth * scale;
    ShortcutSize = constShortcutSize * scale;
    ModeRailWidth = constModeRailWidth * scale;
    ModeRailButtonSize = constModeRailButtonSize * scale;
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
	config.HideTitleBar = true;
}

void NextRendererGameInstance::OnInit()
{
	std::string initializedScene = SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex];
	if (!GOption->SceneName.empty())
	{
		initializedScene = GOption->SceneName;
	}
	GetEngine().RequestLoadScene({.filename = initializedScene});
    // GetEngine().GetUserSettings().SceneEpsilonScale = 0.01f;
    // GetEngine().GetUserSettings().AmbientCubeUnit = 0.02f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetX = 0.0f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetZ = 0.0f;
}

void NextRendererGameInstance::OnTick(double deltaSeconds)
{
    if (playbackPaused_ && !stepRequested_)
    {
        return;
    }
    modelViewController_.UpdateCamera(10.0f, deltaSeconds);
    stepRequested_ = false;
}

std::vector<Assets::FMaterial> MatPreparedForAdd;

void NextRendererGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
	std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
	std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
	models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0,0,0), 0.2f));
	modelId_ = static_cast<uint32_t>(models.size() - 1);
    
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.2,-0.2,-0.2), glm::vec3(0.2,0.2,0.2)));
    boxModelId_ = static_cast<uint32_t>(models.size() - 1);

	matIds_.clear();

	matIds_.push_back(SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1,1,1)));
	MatPreparedForAdd.push_back(materials.back());
	MatPreparedForAdd.push_back({Assets::Material::Metallic(glm::vec3(0.5,0.5,0.5), 0.4f)});
	materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
	MatPreparedForAdd.push_back({Assets::Material::Dielectric(1.5f, 0.0f)});
	materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
	MatPreparedForAdd.push_back({Assets::Material::Mixture(glm::vec3(1.0f, 0.3f, 0.3f), 0.01f)});
	materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
}

void NextRendererGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();
    modelViewController_.Reset( GetEngine().GetScene().GetRenderCamera() );

	GetEngine().GetScene().PlayAllTracks();
}

void NextRendererGameInstance::OnPreConfigUI()
{
    NextGameInstanceBase::OnPreConfigUI();
}

bool NextRendererGameInstance::OnRenderUI()
{
	if (GOption->AgentValidation && !agentValidationCaptured_ && GetEngine().GetTotalFrames() >= 120)
	{
		RequestScreenshot(false, "agent_validation");
		agentValidationCaptured_ = true;
	}

	if (isTakingScreenshot_)
	{
		return true;
	}

	UpdateUiScaledMetrics();

	DrawTitleBar();
	DrawModeRail();
	DrawSettings();
    DrawViewportTopBar();
    DrawViewportBottomBar();
    DrawBottomStatusBar();

	if (ImGui::GetCurrentContext() != nullptr)
	{
		auto& swapChain = GetEngine().GetRenderer().SwapChain();
		const auto offset = swapChain.OutputOffset();
		const auto extent = swapChain.OutputExtent();
		const ImVec2 viewportOrigin = ImGui::GetMainViewport()->Pos;
		gizmoController_.Draw(GetEngine(),
			glm::vec2(viewportOrigin.x + static_cast<float>(offset.x), viewportOrigin.y + static_cast<float>(offset.y)),
			glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
	}
	if (GOption->ReferenceMode)
	{
		ImGuiIO& io = ImGui::GetIO();
		const auto viewport = io.DisplaySize;
		static constexpr std::array<const char*, 4> rendererNames{"SoftModern", "SoftTracing", "VoxelTracing", "PathTracing"};
		const std::array<ImVec2, rendererNames.size()> labelPositions{
			ImVec2(viewport.x * 0.25f, viewport.y * 0.45f),
			ImVec2(viewport.x * 0.75f, viewport.y * 0.45f),
			ImVec2(viewport.x * 0.25f, viewport.y * 0.95f),
			ImVec2(viewport.x * 0.75f, viewport.y * 0.95f)
		};
		const ImGuiWindowFlags windowFlags =
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoFocusOnAppearing;

		for (size_t index = 0; index < rendererNames.size(); ++index)
		{
			ImGui::SetNextWindowPos(labelPositions[index], ImGuiCond_Always, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowBgAlpha(0.5f);

			auto windowName = fmt::format("RendererName{}", index);
			if (ImGui::Begin(windowName.c_str(), nullptr, windowFlags))
			{
				ImGui::TextUnformatted(rendererNames[index]);
			}
			ImGui::End();
		}
	}
    return false;
}

void NextRendererGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();

	if (bigFont_ == nullptr)
	{
		bigFont_ = FontLoader::Load(FontLoader::FFontRequest{
			.filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
			.pixelSize = 24.0f,
			.includeChineseFull = false,
			.extraGlyphsUtf8 = "gkNextRenderer",
		});
	}

    if (titleBarFont_ == nullptr)
    {
        titleBarFont_ = FontLoader::Load(FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-BoldCondensed.ttf",
            .pixelSize = 18.0f,
            .includeChineseFull = true,
            .extraGlyphsUtf8 = "gkNextRenderer",
        });
    }
}

void NextRendererGameInstance::RequestScreenshot(bool openFolder, const std::string& tag)
{
	if (isTakingScreenshot_)
	{
		return;
	}

	std::string folderPath = Utilities::FileHelper::GetPlatformFilePath("screenshots");
	Utilities::FileHelper::EnsureDirectoryExists(folderPath);

	auto now = std::chrono::system_clock::now();
	std::time_t in_time_t = std::chrono::system_clock::to_time_t(now);
	std::tm* tm_ptr = std::localtime(&in_time_t);
	std::string timestamp = fmt::format("{:%Y-%m-%d_%H-%M-%S}", *tm_ptr);
	std::string suffix = tag.empty() ? "" : "_" + tag;
	std::string filename = (std::filesystem::path(folderPath) / (timestamp + suffix)).string();

	isTakingScreenshot_ = true;

	GetEngine().AddTimerTask(0.2, [this, filename, folderPath, openFolder]() {
		ScreenShot::SaveSwapChainToFile(&GetEngine().GetRenderer(), filename, 0, 0, 0, 0);
		if (openFolder)
		{
			NextRenderer::OSCommand(folderPath.c_str());
		}
		isTakingScreenshot_ = false;
		return true;
	});
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = modelViewController_.ModelView();
	outRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}

float NextRendererGameInstance::GetGraphicsDebugPanelTopOffset() const
{
    return TitlebarSize;
}

bool NextRendererGameInstance::OnKey(SDL_Event& event)
{
    // WASDQE camera movement (only active when right mouse is pressed)
    modelViewController_.OnKey(event);

	if (event.key.type == SDL_EVENT_KEY_DOWN)
	{
		switch (event.key.key)
		{
		case SDLK_ESCAPE:
			GetEngine().GetScene().SetSelectedId(-1);
			GetEngine().GetShowFlags().ShowEdge = false;
			return true;
        case SDLK_F:
            {
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    modelViewController_.Focus(focusCenter, radius);
                }
            }
            break;
		case SDLK_SPACE: CreateBoxAndPush(); return true;
			break;
		default: break;
		}
	}
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
    {
        switch (event.gbutton.button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            CreateSphereAndPush(); return true;
            break;
        default: break;
        }
    }
    return false;
}

bool NextRendererGameInstance::OnCursorPosition(double xpos, double ypos)
{
    // Update Controller Context
    bool alt = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    modelViewController_.SetAltPressed(alt);
    
    glm::vec3 center;
    float radius;
    if (GetEngine().GetScene().GetSelectedNodeBounds(center, radius))
    {
        modelViewController_.SetOrbitTarget(center);
    }
    else
    {
        modelViewController_.SetOrbitTarget(std::nullopt);
    }

    if (!gizmoController_.IsInteracting())
    {
        modelViewController_.OnCursorPosition(xpos, ypos);
    }
    return true;
}

bool NextRendererGameInstance::OnMouseButton(SDL_Event& event)
{
    if (!gizmoController_.IsInteracting())
    {
        modelViewController_.OnMouseButton(event);
    }
    else
    {
        return true;
    }

	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
	{
		auto mousePos = GetEngine().GetMousePos();
		glm::vec3 org;
		glm::vec3 dir;
        NextEngineHelper::GetScreenToWorldRay(mousePos, org, dir);
		GetEngine().RayCastGPU( org, dir, [this](Assets::RayCastResult result)
		{
			if (result.Hitted)
			{
				GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
				NextEngineHelper::DrawAuxPoint( result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 60 );
				GetEngine().GetScene().SetSelectedId(result.InstanceId);
				GetEngine().GetShowFlags().ShowEdge = true;
			}
			else
			{
				GetEngine().GetScene().SetSelectedId(-1);
				GetEngine().GetShowFlags().ShowEdge = false;
			}
			return true;
		});
		return true;
	}

    return true;
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
	if (!gizmoController_.IsInteracting())
	{
		modelViewController_.OnScroll(xoffset, yoffset);
	}
	return true;
}

bool NextRendererGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
	int16_t leftTrigger, int16_t rightTrigger)
{
	return modelViewController_.OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
}

void NextRendererGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    //std::string error;
    //cvars.SetDefaultFromString("r.superResolution", "4", &error);
}


void NextRendererGameInstance::CreateSphereAndPush()
{
	glm::vec3 forward = modelViewController_.GetForward();
	glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
	glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
	glm::vec3 shotDir = normalize((farTarget - center));
	uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

	uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
	std::shared_ptr<Assets::Node> newNode = SceneBuilder::CreateRenderNode("temp", center, glm::vec3(1), instanceId, modelId_, newMatId);
	
	auto phys = std::make_shared<Runtime::PhysicsComponent>();
	phys->SetMobility(Runtime::ENodeMobility::Dynamic);
	auto id = GetEngine().GetPhysicsEngine()->CreateSphereBody(center, 0.2f, NextMotionType::Dynamic);
	phys->BindPhysicsBody(id);
	newNode->AddComponent(phys);

	GetEngine().GetScene().AddNode(newNode);
	GetEngine().GetScene().MarkDirty();

	GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void NextRendererGameInstance::CreateBoxAndPush()
{
    glm::vec3 forward = modelViewController_.GetForward();
    glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
    glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 200.f;
    glm::vec3 shotDir = normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    std::shared_ptr<Assets::Node> newNode =
        SceneBuilder::CreateRenderNode("tempBox", center, glm::vec3(1), instanceId, boxModelId_, newMatId);
    
    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateBoxBody(center, {0.4,0.4,0.4}, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    newNode->AddComponent(phys);

    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 100000.f);
}

void NextRendererGameInstance::DrawSettings()
{
    UserSettings& userSetting = GetEngine().GetUserSettings();

    if (!userSetting.ShowSettings)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float panelWidth = 360.0f;
    constexpr float panelMargin = 10.0f;
    const ImVec2 panelPos = viewport->Pos + ImVec2(ModeRailWidth + panelMargin, TitlebarSize + panelMargin);
    const ImVec2 panelSize(panelWidth,
                           viewport->Size.y - TitlebarSize - 50.0f - panelMargin);

    if (!Runtime::UiTheme::BeginFloatingPanel("##RendererSettingsPanel", ICON_FA_SLIDERS,
                                              "Renderer Settings", &userSetting.ShowSettings,
                                              panelPos, panelSize))
    {
        return;
    }

    // Scrollable body
    ImGui::BeginChild("##SettingsBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);

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

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Renderer"), true))
    {
        Runtime::UiTheme::LabelOver(LOCTEXT("Renderer"));
        ImGui::PushItemWidth(-1);
        Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), userSetting, "##RendererList");
        ImGui::PopItemWidth();
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Scene"), true))
    {
        std::vector<std::string> sceneNames;
        sceneNames.reserve(SceneList::AllScenes.size());
        for (const auto& scene : SceneList::AllScenes)
        {
            sceneNames.push_back(std::filesystem::path(scene).filename().string());
        }

        Runtime::UiTheme::LabelOver(LOCTEXT("Scene"));
        ImGui::PushItemWidth(-1);
        const char* currentScenePreview =
            (userSetting.SceneIndex >= 0 && userSetting.SceneIndex < static_cast<int>(sceneNames.size()))
                ? sceneNames[userSetting.SceneIndex].c_str()
                : "";
        if (ImGui::BeginCombo("##SceneList", currentScenePreview))
        {
            ESceneListGroup currentGroup = ESceneListGroup::Other;
            bool hasGroup = false;
            for (int sceneIdx = 0; sceneIdx < static_cast<int>(SceneList::AllScenes.size()); ++sceneIdx)
            {
                const ESceneListGroup sceneGroup = GetSceneListGroup(SceneList::AllScenes[sceneIdx]);
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
                    GetEngine().RequestLoadScene({.filename = SceneList::AllScenes[userSetting.SceneIndex]});
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Camera"), true))
    {
        std::vector<const char*> camerasList;
        for (const auto& cam : GetEngine().GetScene().GetCameras())
        {
            camerasList.emplace_back(cam.name.c_str());
        }

        Runtime::UiTheme::LabelOver(LOCTEXT("Camera"));
        ImGui::PushItemWidth(-1);
        const int prevCameraIdx = userSetting.CameraIdx;
        ImGui::Combo("##CameraList", &userSetting.CameraIdx, camerasList.data(),
                     static_cast<int>(camerasList.size()));
        ImGui::PopItemWidth();
        if (prevCameraIdx != userSetting.CameraIdx)
        {
            GetEngine().GetScene().SetRenderCamera(GetEngine().GetScene().GetCameras()[userSetting.CameraIdx]);
            modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
        }

        auto& camera = GetEngine().GetScene().GetRenderCamera();
        DrawFloatSetting(LOCTEXT("Aperture"), &camera.Aperture, 0.0f, 1.0f, "%.2f", 0.01f);
        DrawFloatSetting(LOCTEXT("Focus(cm)"), &camera.FocalDistance, 0.001f, 1000.0f, "%.3f", 0.05f);
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Ray Tracing"), true))
    {
        static bool rayTracingEnabled = true;
        ImGui::Checkbox("Enable", &rayTracingEnabled);
        ImGui::BeginDisabled(!rayTracingEnabled);
        ImGui::Checkbox(LOCTEXT("AntiAlias"), &userSetting.TAA);
        DrawIntSetting(LOCTEXT("Samples"), &userSetting.NumberOfSamples, 1, 16);
        DrawIntSetting(LOCTEXT("Temporal Steps"), &userSetting.AdaptiveSteps, 2, 64);
        ImGui::Checkbox(LOCTEXT("FastGather"), &userSetting.FastGather);
        DrawIntSetting(LOCTEXT("Ambient Speed"), &userSetting.BakeSpeedLevel, 0, 2);
        ImGui::EndDisabled();
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Denoiser"), true))
    {
        static int denoiserAlgorithm = 0;
        const char* denoiserAlgorithms[] = {"HDR JBF", "SVGF", "Atrous", "None"};
        Runtime::UiTheme::LabelOver("Algorithm");
        ImGui::PushItemWidth(-1);
        if (ImGui::Combo("##DenoiserAlgo", &denoiserAlgorithm, denoiserAlgorithms,
                         IM_ARRAYSIZE(denoiserAlgorithms)))
        {
            userSetting.Denoiser = denoiserAlgorithm != 3;
        }
        ImGui::PopItemWidth();
        DrawFloatSetting(LOCTEXT("DenoiseSigma"), &userSetting.DenoiseSigma, 0.01f, 2.0f, "%.2f", 0.01f);
        DrawFloatSetting(LOCTEXT("DenoiseSigma Lum"), &userSetting.DenoiseSigmaLum, 0.01f, 50.0f, "%.2f", 0.05f);
        DrawFloatSetting(LOCTEXT("DenoiseSigma Norm"), &userSetting.DenoiseSigmaNormal, 0.0f, 0.2f, "%.3f", 0.001f);
        DrawIntSetting(LOCTEXT("DenoiseSize"), &userSetting.DenoiseSize, 1, 10);
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Upscaling"), true))
    {
        static int upscaleMethod = userSetting.DLSS ? 1 : 0;
        const char* methods[] = {"None", "DLSS", "FSR", "TAAU"};
        Runtime::UiTheme::LabelOver("Method");
        ImGui::PushItemWidth(-1);
        if (ImGui::Combo("##UpscaleMethod", &upscaleMethod, methods, IM_ARRAYSIZE(methods)))
        {
            userSetting.DLSS = upscaleMethod == 1 && GetEngine().GetRenderer().SupportDLSS();
            GetEngine().GetRenderer().RequestRecreateSwapChain();
        }
        ImGui::PopItemWidth();

        const char* qualities[] = {"Quality", "Balanced", "Performance", "Ultra Performance", "Native"};
        Runtime::UiTheme::LabelOver("Quality");
        ImGui::PushItemWidth(-1);
        if (ImGui::Combo("##UpscaleQuality", (int*)&userSetting.SuperResolution, qualities,
                         IM_ARRAYSIZE(qualities)))
        {
            GetEngine().GetRenderer().RequestRecreateSwapChain();
        }
        ImGui::PopItemWidth();

        if (upscaleMethod == 1 && !GetEngine().GetRenderer().SupportDLSS())
        {
            ImGui::TextDisabled("DLSS not supported on this hardware.");
        }
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Lighting"), false))
    {
        ImGui::Checkbox(LOCTEXT("UseAmbientCubePropagation"), &userSetting.UseAmbientCubePropagation);
        if (ImGui::Checkbox(LOCTEXT("UseGpuAmbientCubeSdf"), &userSetting.UseGpuAmbientCubeSdf))
        {
            GetEngine().GetScene().RequestGpuDistanceFieldRebuild();
            GetEngine().GetScene().MarkDirty();
        }

        ImGui::Checkbox(LOCTEXT("HasSky"), &GetEngine().GetScene().GetEnvSettings().HasSky);
        if (GetEngine().GetScene().GetEnvSettings().HasSky)
        {
            ImGui::SliderInt(LOCTEXT("SkyIdx"), &GetEngine().GetScene().GetEnvSettings().SkyIdx, 0, 10);
            ImGui::SliderFloat(LOCTEXT("SkyRotation"), &GetEngine().GetScene().GetEnvSettings().SkyRotation, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat(LOCTEXT("SkyLum"), &GetEngine().GetScene().GetEnvSettings().SkyIntensity, 0.0f, 1000.0f, "%.0f");
        }

        ImGui::Checkbox(LOCTEXT("HasSun"), &GetEngine().GetScene().GetEnvSettings().HasSun);
        if (GetEngine().GetScene().GetEnvSettings().HasSun)
        {
            ImGui::SliderFloat(LOCTEXT("SunRotation"), &GetEngine().GetScene().GetEnvSettings().SunRotation, 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat(LOCTEXT("SunLum"), &GetEngine().GetScene().GetEnvSettings().SunIntensity, 0.0f, 2000.0f, "%.0f");
        }

        ImGui::SliderFloat(LOCTEXT("PaperWhitNit"), &userSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Animation"), false))
    {
        ImGui::Checkbox(LOCTEXT("Tick Animation"), &userSetting.TickAnimation);
        ImGui::Checkbox(LOCTEXT("Show Debug Skeleton"), &GetEngine().GetShowFlags().ShowDebugSkeleton);

        ImGui::Separator();
        for (auto& node : GetEngine().GetScene().Nodes())
        {
            if (auto skinnedMesh = node->GetComponent<Runtime::SkinnedMeshComponent>())
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
        Runtime::UiTheme::EndPanelSection();
    }

    if (Runtime::UiTheme::BeginPanelSection(LOCTEXT("Misc"), false))
    {
        ImGui::Checkbox(LOCTEXT("ShowWireframe"), &GetEngine().GetShowFlags().ShowWireframe);
        ImGui::Checkbox(LOCTEXT("TickPhysics"), &userSetting.TickPhysics);
        ImGui::Checkbox(LOCTEXT("DebugDraw"), &GetEngine().GetShowFlags().ShowVisualDebug);
        ImGui::Checkbox(LOCTEXT("DebugDraw_Lighting"), &GetEngine().GetShowFlags().DebugDraw_Lighting);
        ImGui::Checkbox(LOCTEXT("DebugDraw_BoundingBox"), &GetEngine().GetShowFlags().DebugDraw_BoundingBox);

        ImGui::SliderFloat(LOCTEXT("Time Scaling"), &userSetting.HeatmapScale, 0.10f, 2.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);

        ImGui::Spacing();
        uint32_t tmin = 8, tmax = 32;
        ImGui::SliderScalar(LOCTEXT("Temporal Frames"), ImGuiDataType_U32, &userSetting.TemporalFrames, &tmin,
                            &tmax);
        Runtime::UiTheme::EndPanelSection();
    }

    ImGui::EndChild();
    Runtime::UiTheme::EndFloatingPanel();
}

void NextRendererGameInstance::DrawModeRail()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 railPos = viewport->Pos + ImVec2(0.0f, TitlebarSize);
    const ImVec2 railSize = ImVec2(ModeRailWidth, viewport->Size.y - TitlebarSize - 30.0f);

    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled(railPos, railPos + railSize,
                              Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Background));
    background->AddLine(ImVec2(railPos.x + railSize.x - 1.0f, railPos.y),
                        ImVec2(railPos.x + railSize.x - 1.0f, railPos.y + railSize.y),
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border));

    ImGui::SetNextWindowPos(railPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(railSize, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2((ModeRailWidth - ModeRailButtonSize) * 0.5f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##ModeRail", nullptr, flags))
    {
        struct ModeEntry
        {
            EWorkMode mode;
            const char* icon;
            const char* tooltip;
        };
        const ModeEntry topEntries[] = {
            {EWorkMode::Renderer, ICON_FA_CAMERA_RETRO, "Renderer"},
            {EWorkMode::Camera,   ICON_FA_CAMERA,       "Camera"},
            {EWorkMode::World,    ICON_FA_GLOBE,        "World / Lighting"},
            {EWorkMode::Mesh,     ICON_FA_CUBE,         "Scene Outliner"},
            {EWorkMode::Profiler, ICON_FA_CHART_LINE,   "Profiler"},
        };

        for (const auto& entry : topEntries)
        {
            const bool active = (entry.mode == workMode_);
            if (Runtime::UiTheme::ModeRailButton(entry.icon, entry.tooltip, active, ModeRailButtonSize))
            {
                workMode_ = entry.mode;
            }
        }

        // Push the gear button to the bottom.
        const float gearSize = ModeRailButtonSize;
        const float spaceUntilBottom = ImGui::GetContentRegionAvail().y - gearSize - 6.0f;
        if (spaceUntilBottom > 0.0f)
        {
            ImGui::Dummy(ImVec2(0.0f, spaceUntilBottom));
        }
        const bool settingsActive = (workMode_ == EWorkMode::Settings);
        if (Runtime::UiTheme::ModeRailButton(ICON_FA_GEAR, "Settings", settingsActive, gearSize))
        {
            workMode_ = EWorkMode::Settings;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void NextRendererGameInstance::DrawViewportTopBar()
{
    UserSettings& userSetting = GetEngine().GetUserSettings();
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    const float panelMargin = 10.0f;
    const float leftEdge = viewport->Pos.x + ModeRailWidth +
        (userSetting.ShowSettings ? (360.0f + panelMargin * 2.0f) : panelMargin);
    const float topEdge = viewport->Pos.y + TitlebarSize + 10.0f;

    // Left badge: "Path Tracing | Live"
    {
        const char* rendererLabel = Runtime::GraphicsDebugPanel::GetCurrentRendererLabel(GetEngine(), userSetting);
        const std::string rendererText = rendererLabel;
        constexpr const char* liveText = "Live";
        const float badgeHeight = 30.0f;
        const float rendererWidth = ImGui::CalcTextSize(rendererText.c_str()).x + 24.0f;
        const float liveWidth = ImGui::CalcTextSize(liveText).x + 20.0f;
        const float badgeWidth = rendererWidth + liveWidth + 26.0f;

        ImGui::SetNextWindowPos(ImVec2(leftEdge, topEdge), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(badgeWidth, badgeHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

        if (ImGui::Begin("##ViewportTopLeftBadge", nullptr,
                         ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 badgeMin = ImGui::GetWindowPos();
            const ImVec2 badgeMax = badgeMin + ImGui::GetWindowSize();
            const float separatorX = badgeMin.x + rendererWidth + 12.0f;
            const float centerY = badgeMin.y + badgeHeight * 0.5f;

            dl->AddRectFilled(badgeMin, badgeMax,
                              Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::SurfaceElevated, 0.94f), 6.0f);
            dl->AddRect(badgeMin, badgeMax,
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border, 0.92f), 6.0f);
            dl->AddLine(ImVec2(separatorX, badgeMin.y + 6.0f),
                        ImVec2(separatorX, badgeMax.y - 6.0f),
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::BorderStrong, 0.8f));
            dl->AddCircleFilled(ImVec2(badgeMin.x + 14.0f, centerY), 4.0f,
                                Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Success));
            dl->AddText(ImVec2(badgeMin.x + 24.0f, badgeMin.y + 7.0f),
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Text), rendererText.c_str());
            dl->AddText(ImVec2(separatorX + 12.0f, badgeMin.y + 7.0f),
                        Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::TextMuted), liveText);
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // Right cluster: screenshot / focus / 1:1
    {
        const float clusterWidth = 138.0f;
        const float rightEdge = viewport->Pos.x + viewport->Size.x - panelMargin - clusterWidth;
        ImGui::SetNextWindowPos(ImVec2(rightEdge, topEdge), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(clusterWidth, 32.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::SurfaceElevated, 0.92f));
        if (ImGui::Begin("##ViewportTopRightCluster", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
        {
            if (Runtime::UiTheme::ToolbarButton(ICON_FA_CAMERA, "Take Screenshot", false, ImVec2(28.0f, 26.0f)))
            {
                RequestScreenshot(false, "");
            }
            ImGui::SameLine();
            if (Runtime::UiTheme::ToolbarButton(ICON_FA_EXPAND, "Focus Selected", false, ImVec2(28.0f, 26.0f)))
            {
                glm::vec3 focusCenter;
                float radius;
                if (GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, radius))
                {
                    modelViewController_.Focus(focusCenter, radius);
                }
            }
            ImGui::SameLine();
            Runtime::UiTheme::ToolbarButton("1:1 " ICON_FA_CHEVRON_DOWN, "Native Resolution", false, ImVec2(46.0f, 26.0f));
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
}

void NextRendererGameInstance::DrawViewportBottomBar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float bottomStatusBar = 30.0f;

    const auto& swapChain = GetEngine().GetRenderer().SwapChain();
    const auto extent = swapChain.OutputExtent();
    const std::string frameText = fmt::format("Frame {}", GetEngine().GetTotalFrames());
    const std::string sampleText = fmt::format("Samples {} spp", GetEngine().GetUserSettings().NumberOfSamples);
    const std::string resolutionText = fmt::format("{} x {}", extent.width, extent.height);
    const float textWidth = ImGui::CalcTextSize(frameText.c_str()).x +
        ImGui::CalcTextSize(sampleText.c_str()).x +
        ImGui::CalcTextSize(resolutionText.c_str()).x + 72.0f;
    const ImVec2 padding(16.0f, 6.0f);
    const ImVec2 windowSize(textWidth + padding.x * 2.0f + 18.0f,
                            ImGui::GetTextLineHeight() + padding.y * 2.0f);
    const ImVec2 windowPos(viewport->Pos.x + (viewport->Size.x - windowSize.x) * 0.5f,
                           viewport->Pos.y + viewport->Size.y - bottomStatusBar - windowSize.y - 8.0f);

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::SurfaceElevated, 0.92f));

    if (ImGui::Begin("##ViewportFrameInfo", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
    {
        auto DrawTokenSeparator = [&]()
        {
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextDim), "|");
            ImGui::SameLine(0.0f, 10.0f);
        };

        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%s", frameText.c_str());
        DrawTokenSeparator();
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%s", sampleText.c_str());
        DrawTokenSeparator();
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%s", resolutionText.c_str());
        ImGui::SameLine(0.0f, 10.0f);
        Runtime::UiTheme::ToolbarButton(ICON_FA_EXPAND, "Viewport Display Options", false, ImVec2(22.0f, 18.0f));
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void NextRendererGameInstance::DrawTitleBar()
{
    Runtime::UiTheme::FAppTitleBarConfig config{};
    config.BrandWindowId = "RendererBrand";
    config.MenuWindowId = "RendererMenuBar";
    config.RightWindowId = "RendererWindowControls";
    config.AppName = "gkNextRenderer";
    config.Height = TitlebarSize;
    config.RightContentWidth = TitlebarRightInfoWidth;
    config.TitleFont = titleBarFont_;
    config.IsMaximized = GetEngine().IsMaximumed();
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
                RequestScreenshot(true, "");
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

        if (ImGui::BeginMenu("Capture"))
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
            ImGui::MenuItem("Render Settings", nullptr, &GetEngine().GetUserSettings().ShowSettings);
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
            ImGui::MenuItem("Documentation", nullptr, false, false);
            ImGui::MenuItem("About gkNextRenderer", nullptr, false, false);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        return menuRight;
    };
    config.DrawRightContent = [&]()
    {
        const auto framebufferSize = GetEngine().GetWindow().FramebufferSize();
        ImGui::SetCursorPos(ImVec2(0.0f, std::floor((TitlebarSize - ImGui::GetTextLineHeight()) * 0.5f) - 1.0f));
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%ux%u",
                           framebufferSize.width, framebufferSize.height);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "Camera %d",
                           GetEngine().GetUserSettings().CameraIdx);
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success), "%.0f FPS",
                           GetEngine().GetFrameRate());
        ImGui::SameLine(0.0f, 16.0f);
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%.2f ms",
                           GetEngine().GetSmoothDeltaSeconds() * 1000.0);
    };
    config.OnMinimize = [&]() { GetEngine().RequestMinimize(); };
    config.OnToggleMaximize = [&]() { GetEngine().ToggleMaximize(); };
    config.OnClose = [&]() { GetEngine().RequestClose(); };
    Runtime::UiTheme::DrawAppTitleBar(GetEngine(), config);
}

void NextRendererGameInstance::DrawBottomStatusBar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float barHeight = 30.0f;
    ImGui::SetNextWindowPos(viewport->Pos + ImVec2(0.0f, viewport->Size.y - barHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    if (ImGui::Begin("RendererStatusBar", nullptr, flags))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(
            viewport->Pos + ImVec2(0.0f, viewport->Size.y - barHeight),
            viewport->Pos + ImVec2(viewport->Size.x, viewport->Size.y - barHeight),
            Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border), 1.0f);

        auto DrawVerticalSeparator = [&]()
        {
            ImGui::SameLine(0.0f, 12.0f);
            const ImVec2 separatorMin = ImGui::GetCursorScreenPos();
            drawList->AddLine(ImVec2(separatorMin.x, separatorMin.y + 2.0f),
                              ImVec2(separatorMin.x, separatorMin.y + 18.0f),
                              Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border, 0.9f));
            ImGui::Dummy(ImVec2(1.0f, 18.0f));
            ImGui::SameLine(0.0f, 12.0f);
        };

        if (UserInterface* ui = GetEngine().GetUserInterface())
        {
            if (Runtime::UiTheme::ToolbarButton("Console", "Toggle Console", ui->IsConsoleOpen(), ImVec2(74.0f, 22.0f)))
            {
                ui->ToggleConsole();
            }
            ImGui::SameLine();
        }
        if (Runtime::UiTheme::ToolbarButton("Stats", "Toggle Profiler", GetEngine().GetUserSettings().ShowOverlay,
                                            ImVec2(58.0f, 22.0f)))
        {
            GetEngine().GetUserSettings().ShowOverlay = !GetEngine().GetUserSettings().ShowOverlay;
        }
        ImGui::SameLine();
        if (Runtime::UiTheme::ToolbarButton("Capture", "Take Screenshot", false, ImVec2(72.0f, 22.0f)))
        {
            RequestScreenshot(false, "");
        }
        DrawVerticalSeparator();

        const float centerStart = viewport->Size.x * 0.5f - 132.0f;
        if (ImGui::GetCursorPosX() < centerStart)
        {
            ImGui::SameLine(centerStart);
        }
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "Frame %u",
                           GetEngine().GetTotalFrames());
        ImGui::SameLine(0.0f, 10.0f);
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_BACKWARD_STEP, "Previous Frame (placeholder)", false,
                                            ImVec2(28.0f, 22.0f)))
        {
            stepRequested_ = true;
        }
        ImGui::SameLine();
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_BACKWARD, "Previous Sample (placeholder)", false,
                                            ImVec2(28.0f, 22.0f)))
        {
            playbackPaused_ = true;
            stepRequested_ = true;
        }
        ImGui::SameLine();
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_PLAY, "Play / Pause", !playbackPaused_, ImVec2(30.0f, 22.0f)))
        {
            playbackPaused_ = !playbackPaused_;
        }
        ImGui::SameLine();
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_FORWARD, "Step Frame", false, ImVec2(28.0f, 22.0f)))
        {
            playbackPaused_ = true;
            stepRequested_ = true;
        }
        ImGui::SameLine();
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_FORWARD_STEP, "Advance Frame", false, ImVec2(28.0f, 22.0f)))
        {
            playbackPaused_ = true;
            stepRequested_ = true;
        }

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(GetEngine().GetRenderer().Device().PhysicalDevice(), &memoryProperties);
        uint64_t totalBytes = 0;
        for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i)
        {
            if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                totalBytes += memoryProperties.memoryHeaps[i].size;
            }
        }
        const double totalGb = static_cast<double>(totalBytes) / (1024.0 * 1024.0 * 1024.0);
        const double usedGb = 0.0;
        const float memoryFraction = totalGb > 0.0 ? static_cast<float>(usedGb / totalGb) : 0.0f;
        const std::string memoryLabel = fmt::format("{:.2f}/{:.2f} GB ({:.0f}%)", usedGb, totalGb,
                                                    memoryFraction * 100.0f);

        DrawVerticalSeparator();

        const float rightStart = viewport->Size.x - 284.0f;
        if (ImGui::GetCursorPosX() < rightStart)
        {
            ImGui::SameLine(rightStart);
        }
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%s", memoryLabel.c_str());
        ImGui::SameLine(0.0f, 8.0f);
        Runtime::UiTheme::DrawProgressBar(memoryFraction, Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success),
                                          ImVec2(92.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, 10.0f);
        Runtime::UiTheme::ToolbarButton(ICON_FA_CHART_COLUMN, "Memory Statistics", false, ImVec2(24.0f, 22.0f));
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}
