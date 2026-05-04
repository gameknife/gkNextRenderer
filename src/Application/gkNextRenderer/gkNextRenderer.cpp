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
} // namespace

// should use 1em instead of 1px
constexpr float constTitlebarSize = 40;
constexpr float constTitlebarControlSize = constTitlebarSize * 3;
constexpr float constIconSize = 64;
constexpr float constPaletteSize = 46;
constexpr float constButtonSize = 36;
constexpr float constBuildBarWidth = 240;
constexpr float constSideBarWidth = 300;
constexpr float constShortcutSize = 10;

float TitlebarSize = constTitlebarSize;
float TitlebarControlSize = constTitlebarControlSize;
float IconSize = constIconSize;
float PaletteSize = constPaletteSize;
float ButtonSize = constButtonSize;
float BuildBarWidth = constBuildBarWidth;
float SideBarWidth = constSideBarWidth;
float ShortcutSize = constShortcutSize;

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
    TitlebarControlSize = constTitlebarControlSize * scale;
    IconSize = constIconSize * scale;
    PaletteSize = constPaletteSize * scale;
    ButtonSize = constButtonSize * scale;
    BuildBarWidth = constBuildBarWidth * scale;
    SideBarWidth = constSideBarWidth * scale;
    ShortcutSize = constShortcutSize * scale;
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine):NextGameInstanceBase(config,options,engine),engine_(engine)
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
    modelViewController_.UpdateCamera(10.0f, deltaSeconds);
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
	DrawSettings();

	if (ImGui::GetCurrentContext() != nullptr)
	{
		auto& swapChain = GetEngine().GetRenderer().SwapChain();
		const auto offset = swapChain.OutputOffset();
		const auto extent = swapChain.OutputExtent();
		const ImVec2 viewportOrigin = ImGui::GetMainViewport()->Pos;
		gizmoController_.Draw(*engine_,
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

	const float distance = 10.0f;
	const ImVec2 pos = ImVec2(distance, TitlebarSize + distance);
	const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
	ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30,-1));
	ImGui::SetNextWindowBgAlpha(0.9f);
	
	const auto flags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Settings", &userSetting.ShowSettings, flags))
	{
			if( ImGui::CollapsingHeader(LOCTEXT("Renderer"), ImGuiTreeNodeFlags_DefaultOpen) )
			{
				ImGui::Text("%s", LOCTEXT("Renderer"));
                Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), userSetting, "##RendererList");
				ImGui::NewLine();
			}
		
		if( ImGui::CollapsingHeader(LOCTEXT("Scene"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			std::vector<std::string> sceneNames;
			for (const auto& scene : SceneList::AllScenes)
			{
				std::filesystem::path path(scene);
				sceneNames.push_back(path.filename().string());
			}
			
			std::vector<const char*> camerasList;
			for (const auto& cam : GetEngine().GetScene().GetCameras())
			{
				camerasList.emplace_back(cam.name.c_str());
			}
			
			ImGui::Text("%s", LOCTEXT("Scene"));
			
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

			int prevCameraIdx = userSetting.CameraIdx;
			ImGui::Text("%s", LOCTEXT("Camera"));
			ImGui::PushItemWidth(-1);
			ImGui::Combo("##CameraList", &userSetting.CameraIdx, camerasList.data(), static_cast<int>(camerasList.size()));
			ImGui::PopItemWidth();
			if(prevCameraIdx != userSetting.CameraIdx)
			{
				GetEngine().GetScene().SetRenderCamera( GetEngine().GetScene().GetCameras()[userSetting.CameraIdx] );
				modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
			}

			auto& camera = GetEngine().GetScene().GetRenderCamera();
			ImGui::SliderFloat(LOCTEXT("Aperture"), &camera.Aperture, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("Focus(cm)"), &camera.FocalDistance, 0.001f, 1000.0f, "%.3f");
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Ray Tracing"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			ImGui::Checkbox(LOCTEXT("AntiAlias"), &userSetting.TAA);
			ImGui::SliderInt(LOCTEXT("Samples"), &userSetting.NumberOfSamples, 1, 16);
			ImGui::SliderInt(LOCTEXT("TemporalSteps"), &userSetting.AdaptiveSteps, 2, 64);
			ImGui::Checkbox(LOCTEXT("FastGather"), &userSetting.FastGather);
			ImGui::SliderInt(LOCTEXT("AmbientSpeed"), &userSetting.BakeSpeedLevel, 0, 2);

			
			
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Denoiser"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
#if WITH_OIDN
			ImGui::Checkbox("Use OIDN", &userSetting.Denoiser);
#else
			ImGui::Checkbox(LOCTEXT("Use JBF"), &userSetting.Denoiser);
			ImGui::SliderFloat(LOCTEXT("DenoiseSigma"), &userSetting.DenoiseSigma, 0.01f, 2.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaLum"), &userSetting.DenoiseSigmaLum, 0.01f, 50.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaNormal"), &userSetting.DenoiseSigmaNormal, 0.001f, 0.2f, "%.3f");
			ImGui::SliderInt(LOCTEXT("DenoiseSize"), &userSetting.DenoiseSize, 1, 10);
#endif
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Upscaling"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			if (GetEngine().GetRenderer().SupportDLSS())
			{
				if (ImGui::Checkbox("NVIDIA DLSS", &userSetting.DLSS))
                {
                    GetEngine().GetRenderer().RequestRecreateSwapChain();
                }
                
				if (userSetting.DLSS)
				{
					const char* dlssModes[] = { "Quality", "Balanced", "Performance", "Ultra Performance", "DLAA (Native)" };
					if (ImGui::Combo("DLSS Mode", (int*)&userSetting.SuperResolution, dlssModes, IM_ARRAYSIZE(dlssModes)))
                    {
                        GetEngine().GetRenderer().RequestRecreateSwapChain();
                    }
					
					if (GetEngine().GetRenderer().SupportDLSSRR())
					{
						ImGui::Checkbox("DLSS Ray Reconstruction", &userSetting.DLSSRR);
					}
				}
			}
			else
			{
				ImGui::TextDisabled("DLSS not supported on this hardware.");
			}
            
            if (!userSetting.DLSS)
            {
                const char* upscaleModes[] = { "Quality", "Balanced", "Performance", "Ultra Performance", "Native" };
				if (ImGui::Combo("Upscale Mode", (int*)&userSetting.SuperResolution, upscaleModes, IM_ARRAYSIZE(upscaleModes)))
                {
                    GetEngine().GetRenderer().RequestRecreateSwapChain();
                }
            }
			ImGui::NewLine();
		}
		
		if( ImGui::CollapsingHeader(LOCTEXT("Lighting"), ImGuiTreeNodeFlags_None) )
		{
		    ImGui::Checkbox(LOCTEXT("UseAmbientCubePropagation"), &userSetting.UseAmbientCubePropagation);
            if (ImGui::Checkbox(LOCTEXT("UseGpuAmbientCubeSdf"), &userSetting.UseGpuAmbientCubeSdf))
            {
                GetEngine().GetScene().RequestGpuDistanceFieldRebuild();
                GetEngine().GetScene().MarkDirty();
            }
		    
			ImGui::Checkbox(LOCTEXT("HasSky"), &GetEngine().GetScene().GetEnvSettings().HasSky);
			if(GetEngine().GetScene().GetEnvSettings().HasSky)
			{
				ImGui::SliderInt(LOCTEXT("SkyIdx"), &GetEngine().GetScene().GetEnvSettings().SkyIdx, 0, 10);
				ImGui::SliderFloat(LOCTEXT("SkyRotation"), &GetEngine().GetScene().GetEnvSettings().SkyRotation, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat(LOCTEXT("SkyLum"), &GetEngine().GetScene().GetEnvSettings().SkyIntensity, 0.0f, 1000.0f, "%.0f");
			}
			
			ImGui::Checkbox(LOCTEXT("HasSun"), &GetEngine().GetScene().GetEnvSettings().HasSun);
			if(GetEngine().GetScene().GetEnvSettings().HasSun)
			{
				ImGui::SliderFloat(LOCTEXT("SunRotation"), &GetEngine().GetScene().GetEnvSettings().SunRotation, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat(LOCTEXT("SunLum"), &GetEngine().GetScene().GetEnvSettings().SunIntensity, 0.0f, 2000.0f, "%.0f");
			}

			ImGui::SliderFloat(LOCTEXT("PaperWhitNit"), &userSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Animation"), ImGuiTreeNodeFlags_None) )
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
                        for(int i=0; i<static_cast<int>(animNames.size()); ++i) {
                            if(animNames[i] == current) {
                                selectedAnim = i;
                                break;
                            }
                        }

                        std::vector<const char*> animPtrs;
                        for (const auto& name : animNames) animPtrs.push_back(name.c_str());
                        
                        if (ImGui::Combo("##AnimList", &selectedAnim, animPtrs.data(), static_cast<int>(animPtrs.size())))
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
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Misc"), ImGuiTreeNodeFlags_None) )
		{
			ImGui::Text("%s", LOCTEXT("Profiler"));
			ImGui::Separator();
			ImGui::Checkbox(LOCTEXT("ShowWireframe"), &GetEngine().GetShowFlags().ShowWireframe);
			ImGui::Checkbox(LOCTEXT("TickPhysics"), &userSetting.TickPhysics);
			ImGui::Checkbox(LOCTEXT("DebugDraw"), &GetEngine().GetShowFlags().ShowVisualDebug);
			ImGui::Checkbox(LOCTEXT("DebugDraw_Lighting"), &GetEngine().GetShowFlags().DebugDraw_Lighting);
			ImGui::Checkbox(LOCTEXT("DebugDraw_BoundingBox"), &GetEngine().GetShowFlags().DebugDraw_BoundingBox);
			
			ImGui::SliderFloat(LOCTEXT("Time Scaling"), &userSetting.HeatmapScale, 0.10f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
			ImGui::NewLine();

			ImGui::Text("%s", LOCTEXT("Performance"));
			ImGui::Separator();
			uint32_t min = 8, max = 32;
			ImGui::SliderScalar(LOCTEXT("Temporal Frames"), ImGuiDataType_U32, &userSetting.TemporalFrames, &min, &max);		
		}
	}
	ImGui::End();
}

void NextRendererGameInstance::DrawTitleBar()
{
    // 获取窗口的大小
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;
    float titlebarLeftReservedWidth = 0.0f;
    float titlebarRightReservedWidth = TitlebarControlSize;

    auto bgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bgColor.w = 0.9f;
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ImVec2(windowSize.x, TitlebarSize), ImGui::ColorConvertFloat4ToU32(bgColor));

    ImGui::PushFont(bigFont_);

    auto textSize = ImGui::CalcTextSize("gkNextRenderer");
    ImGui::GetForegroundDrawList()->AddText(ImVec2((windowSize.x - textSize.x) * 0.5f, (TitlebarSize - textSize.y) * 0.5f), IM_COL32(255, 255, 255, 255), "gkNextRenderer");

    ImGui::PopFont();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(windowSize.x - TitlebarControlSize, 0), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(TitlebarControlSize, TitlebarSize));

    ImGui::Begin("TitleBarRight", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    titlebarRightReservedWidth = ImGui::GetWindowSize().x;

    if (ImGui::Button(ICON_FA_MINUS, ImVec2(TitlebarSize, TitlebarSize)))
    {
        GetEngine().RequestMinimize();
    }
    ImGui::SameLine();
    if (ImGui::Button(GetEngine().IsMaximumed() ? ICON_FA_WINDOW_RESTORE : ICON_FA_SQUARE, ImVec2(TitlebarSize, TitlebarSize)))
    {
        GetEngine().ToggleMaximize();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK, ImVec2(TitlebarSize, TitlebarSize)))
    {
        GetEngine().RequestClose();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always, ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(TitlebarSize * 18, TitlebarSize));

    ImGui::Begin("TitleBarLeft", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    if (ImGui::Button(ICON_FA_GITHUB, ImVec2(TitlebarSize, TitlebarSize)))
    {
        NextRenderer::OSCommand("https://github.com/gameknife/gkNextRenderer");
    }
    BUTTON_TOOLTIP(LOCTEXT("Open Project Page in OS Browser"))
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TWITTER, ImVec2(TitlebarSize, TitlebarSize)))
    {
        NextRenderer::OSCommand("https://x.com/gKNIFE_");
    }
    BUTTON_TOOLTIP(LOCTEXT("Open Twitter Page in OS Browser"))
    ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, TitlebarSize / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, TitlebarSize / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CAMERA, ImVec2(TitlebarSize, TitlebarSize)))
    {
        GetEngine().AddTickedTask([this](double deltaSeconds)-> bool
        {
            GetEngine().RequestScreenShot({});
            return true;
        });
		//GetEngine().RequestHighQualityScreenShot("", 512);
    }
    BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_EYE, ImVec2(TitlebarSize, TitlebarSize)))
	{
		ImGui::OpenPopup("RendererShowFlags");
	}
	BUTTON_TOOLTIP(LOCTEXT("Show Flags"))

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    if (ImGui::BeginPopup("RendererShowFlags"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));
        auto& showFlags = GetEngine().GetShowFlags();
        Utilities::UI::DrawShowFlagsCommon(showFlags);
        
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_LIST_CHECK, ImVec2(TitlebarSize, TitlebarSize)))
	{
		GetEngine().GetUserSettings().ShowSettings = !GetEngine().GetUserSettings().ShowSettings;
	}
	BUTTON_TOOLTIP(LOCTEXT("Toggle Settings Panel"))
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_GAUGE_SIMPLE_HIGH, ImVec2(TitlebarSize, TitlebarSize)))
	{
		GetEngine().GetUserSettings().ShowOverlay = !GetEngine().GetUserSettings().ShowOverlay;
	}
	BUTTON_TOOLTIP(LOCTEXT("Toggle Performance Overlay"))
	ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, TitlebarSize / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, TitlebarSize / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    ImGui::SetCursorPosY((TitlebarSize - ImGui::GetTextLineHeight()) / 2);
    ImGui::TextUnformatted(fmt::format("{:.0f}fps", GetEngine().GetFrameRate()).c_str());
    titlebarLeftReservedWidth = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    GetEngine().ConfigureCustomTitleBarDrag(
        true, TitlebarSize, titlebarLeftReservedWidth, titlebarRightReservedWidth);
}
