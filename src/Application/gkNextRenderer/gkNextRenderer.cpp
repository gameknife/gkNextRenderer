#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <array>

#include "Assets/FProcModel.h"
#include "Assets/Node.h"
#include "Runtime/Engine.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/ImGui.hpp"
#include "Runtime/Platform/PlatformCommon.h"

extern float GAndroidMagicScale;

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
	GetEngine().RequestLoadScene(initializedScene);
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
    
    models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-0.1,-0.1,-0.1), glm::vec3(0.1,0.1,0.1)));
    boxModelId_ = static_cast<uint32_t>(models.size() - 1);

	matIds_.clear();
	
	MatPreparedForAdd.push_back({Assets::Material::Lambertian(glm::vec3(1,1,1))});
	materials.push_back(MatPreparedForAdd.back());matIds_.push_back(uint32_t(materials.size() - 1));
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
	UpdateUiScaledMetrics();

	DrawTitleBar();
	DrawSettings();
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
		ImFontGlyphRangesBuilder builder;
		builder.AddText("gkNextRenderer");
		const ImWchar* glyphRange = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
		bigFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-BoldCondensed.ttf").c_str(), 24, nullptr, glyphRange);
	}
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = modelViewController_.ModelView();
	outRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}

bool NextRendererGameInstance::OnKey(SDL_Event& event)
{
    modelViewController_.OnKey(event);

	if (event.key.type == SDL_EVENT_KEY_DOWN)
	{
		switch (event.key.key)
		{
		case SDLK_ESCAPE: GetEngine().GetScene().SetSelectedId(-1); return true;
			break;
		case SDLK_F1: GetEngine().GetUserSettings().ShowSettings = !GetEngine().GetUserSettings().ShowSettings; return true;
			break;
		case SDLK_F2: GetEngine().GetUserSettings().ShowOverlay = !GetEngine().GetUserSettings().ShowOverlay; return true;
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
    modelViewController_.OnCursorPosition(  xpos,  ypos);
    return true;
}

bool NextRendererGameInstance::OnMouseButton(SDL_Event& event)
{
    modelViewController_.OnMouseButton(event);

	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
	{
		auto mousePos = GetEngine().GetMousePos();
		glm::vec3 org;
		glm::vec3 dir;
		GetEngine().GetScreenToWorldRay(mousePos, org, dir);
		GetEngine().RayCastGPU( org, dir, [this](Assets::RayCastResult result)
		{
			if (result.Hitted)
			{
				GetEngine().GetScene().GetRenderCamera().FocalDistance = result.T;
				GetEngine().DrawAuxPoint( result.HitPoint, glm::vec4(0.2, 1, 0.2, 1), 2, 60 );
			}
			return true;
		});
		return true;
	}

    return true;
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
	modelViewController_.OnScroll( xoffset,  yoffset);
	return true;
}

bool NextRendererGameInstance::OnGamepadInput(int16_t leftStickX, int16_t leftStickY, int16_t rightStickX, int16_t rightStickY,
	int16_t leftTrigger, int16_t rightTrigger)
{
	return modelViewController_.OnGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
}


void NextRendererGameInstance::CreateSphereAndPush()
{
	glm::vec3 forward = modelViewController_.GetForward();
	glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
	glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
	glm::vec3 shotDir = normalize((farTarget - center));
	uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());
	std::shared_ptr<Assets::Node> newNode = Assets::Node::CreateNode("temp", center, glm::quat(), glm::vec3(1), modelId_,
															   instanceId, false);

	uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
	newNode->SetMaterial( { newMatId } );
	newNode->SetVisible(true);
	newNode->SetMobility(Assets::Node::ENodeMobility::Dynamic);
	auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(center, 0.2f, JPH::EMotionType::Dynamic);
	newNode->BindPhysicsBody(id);

	GetEngine().GetScene().Nodes().push_back(newNode);
	GetEngine().GetScene().MarkDirty();

	GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void NextRendererGameInstance::CreateBoxAndPush()
{
    glm::vec3 forward = modelViewController_.GetForward();
    glm::vec3 center = modelViewController_.GetPosition() + forward * 0.1f + modelViewController_.GetRight() * 0.5f + modelViewController_.GetUp() * -0.5f;
    glm::vec3 farTarget = modelViewController_.GetPosition() + forward * 1000.0f + modelViewController_.GetUp() * 100.f;
    glm::vec3 shotDir = normalize((farTarget - center));
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());
    std::shared_ptr<Assets::Node> newNode = Assets::Node::CreateNode("tempBox", center, glm::quat(), glm::vec3(1), boxModelId_,
                                                               instanceId, false);

    uint32_t newMatId = matIds_[std::rand() % matIds_.size()];
    newNode->SetMaterial( { newMatId } );
    newNode->SetVisible(true);
    newNode->SetMobility(Assets::Node::ENodeMobility::Dynamic);
    auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateBoxBody(center, {0.2,0.2,0.2}, JPH::EMotionType::Dynamic);
    newNode->BindPhysicsBody(id);

    GetEngine().GetScene().Nodes().push_back(newNode);
    GetEngine().GetScene().MarkDirty();

    GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
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
		// if( ImGui::CollapsingHeader(LOCTEXT("Help"), ImGuiTreeNodeFlags_None) )
		// {
		// 	ImGui::Separator();
		// 	ImGui::BulletText("%s", LOCTEXT("F1: toggle Settings."));
		// 	ImGui::BulletText("%s", LOCTEXT("F2: toggle Statistics."));
		// 	ImGui::BulletText("%s", LOCTEXT("Click: Click Object to Focus."));
		// 	ImGui::BulletText("%s", LOCTEXT("DropFile: if glb file, load it."));
		// 	ImGui::NewLine();
		// }

		if( ImGui::CollapsingHeader(LOCTEXT("Renderer"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			std::vector<const char*> renderers {"PathTracing", "SoftTracing", "SoftModern", "VoxelTracing"};
			
			ImGui::Text("%s", LOCTEXT("Renderer"));
			
			ImGui::PushItemWidth(-1);
			ImGui::Combo("##RendererList", &userSetting.RendererType, renderers.data(), static_cast<int>(renderers.size()));
			ImGui::PopItemWidth();
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

			std::vector<const char*> scenes;
			for (const auto& scene : sceneNames)
			{
				scenes.push_back(scene.c_str());
			}
			
			std::vector<const char*> camerasList;
			for (const auto& cam : GetEngine().GetScene().GetCameras())
			{
				camerasList.emplace_back(cam.name.c_str());
			}
			
			ImGui::Text("%s", LOCTEXT("Scene"));
			
			ImGui::PushItemWidth(-1);
			if (ImGui::Combo("##SceneList", &userSetting.SceneIndex, scenes.data(), static_cast<int>(scenes.size())) )
			{
				// Request Scene Load
				GetEngine().RequestLoadScene(SceneList::AllScenes[userSetting.SceneIndex]);
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
			ImGui::Checkbox("Use OIDN", &UserSetting.Denoiser);
#else
			ImGui::Checkbox(LOCTEXT("Use JBF"), &userSetting.Denoiser);
			ImGui::SliderFloat(LOCTEXT("DenoiseSigma"), &userSetting.DenoiseSigma, 0.01f, 2.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaLum"), &userSetting.DenoiseSigmaLum, 0.01f, 50.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaNormal"), &userSetting.DenoiseSigmaNormal, 0.001f, 0.2f, "%.3f");
			ImGui::SliderInt(LOCTEXT("DenoiseSize"), &userSetting.DenoiseSize, 1, 10);
#endif
			ImGui::NewLine();
		}
		
		if( ImGui::CollapsingHeader(LOCTEXT("Lighting"), ImGuiTreeNodeFlags_None) )
		{
			
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

		if( ImGui::CollapsingHeader(LOCTEXT("Misc"), ImGuiTreeNodeFlags_None) )
		{
			ImGui::Text("%s", LOCTEXT("Profiler"));
			ImGui::Separator();
			ImGui::Checkbox(LOCTEXT("ShowWireframe"), &GetEngine().GetRenderer().showWireframe_);
			ImGui::Checkbox(LOCTEXT("TickPhysics"), &userSetting.TickPhysics);
			ImGui::Checkbox(LOCTEXT("DebugDraw"), &userSetting.ShowVisualDebug);
			ImGui::Checkbox(LOCTEXT("DebugDraw_Lighting"), &userSetting.DebugDraw_Lighting);
			ImGui::Checkbox(LOCTEXT("DisableSpatialReuse"), &userSetting.DisableSpatialReuse);
			
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

    }
    BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_LIST_CHECK, ImVec2(TitlebarSize, TitlebarSize)))
	{
		GetEngine().GetUserSettings().ShowSettings = !GetEngine().GetUserSettings().ShowSettings;
	}
	BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
	if (ImGui::Button(ICON_FA_GAUGE_SIMPLE_HIGH, ImVec2(TitlebarSize, TitlebarSize)))
	{
		GetEngine().GetUserSettings().ShowOverlay = !GetEngine().GetUserSettings().ShowOverlay;
	}
	BUTTON_TOOLTIP(LOCTEXT("Take a Screenshot into the screenshots folder"))
	ImGui::SameLine();
    ImGui::GetForegroundDrawList()->AddLine(ImGui::GetCursorPos() + ImVec2(4, TitlebarSize / 2 - 5), ImGui::GetCursorPos() + ImVec2(4, TitlebarSize / 2 + 5), IM_COL32(255, 255, 255, 160), 2.0f);
    ImGui::Dummy(ImVec2(10, 10));
    ImGui::SameLine();
    float deltaSeconds = GetEngine().GetSmoothDeltaSeconds();
    ImGui::SameLine();
    ImGui::SetCursorPosY((TitlebarSize - ImGui::GetTextLineHeight()) / 2);
    ImGui::TextUnformatted(fmt::format("{:.0f}fps", 1.0f / deltaSeconds).c_str());
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
}
