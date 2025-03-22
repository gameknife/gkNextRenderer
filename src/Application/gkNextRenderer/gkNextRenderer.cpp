#include "gkNextRenderer.hpp"

#include <imgui.h>

#include "Runtime/Engine.hpp"
#include "Utilities/Localization.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextRendererGameInstance>(config, options, engine);
}

NextRendererGameInstance::NextRendererGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine):NextGameInstanceBase(config,options,engine),engine_(engine)
{
    
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
    modelViewController_.UpdateCamera(1.0f, deltaSeconds);
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
	DrawSettings();
    return false;
}

void NextRendererGameInstance::OnInitUI()
{
    NextGameInstanceBase::OnInitUI();
}

bool NextRendererGameInstance::OverrideRenderCamera(Assets::Camera& OutRenderCamera) const
{
    OutRenderCamera.ModelView = modelViewController_.ModelView();
	OutRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}

bool NextRendererGameInstance::OnKey(int key, int scancode, int action, int mods)
{
    modelViewController_.OnKey(key, scancode, action, mods);
    return false;
}

bool NextRendererGameInstance::OnCursorPosition(double xpos, double ypos)
{
    modelViewController_.OnCursorPosition(  xpos,  ypos);
    return true;
}

bool NextRendererGameInstance::OnMouseButton(int button, int action, int mods)
{
    modelViewController_.OnMouseButton( button,  action,  mods);

#if !ANDROID
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
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
				// log the pos
				fmt::print("hit point: {}, {}\n", result.HitPoint.x, result.HitPoint.z);
			}
			return true;
		});
		return true;
	}
#endif
	
    return true;
}

bool NextRendererGameInstance::OnScroll(double xoffset, double yoffset)
{
	modelViewController_.OnScroll( xoffset,  yoffset);
	return true;
}


void NextRendererGameInstance::DrawSettings()
{
	UserSettings& UserSetting = GetEngine().GetUserSettings();
	
	if (!UserSetting.ShowSettings)
	{
		return;
	}

	const float distance = 10.0f;
	const ImVec2 pos = ImVec2(distance, distance);
	const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
#if ANDROID
	ImGui::SetNextWindowSize(ImVec2(300,-1));
#else
	ImGui::SetNextWindowSize(ImVec2(400,-1));
#endif
	ImGui::SetNextWindowBgAlpha(0.9f);
	
	const auto flags =
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("Settings", &UserSetting.ShowSettings, flags))
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
			std::vector<const char*> renderers {"PathTracing", "Hybrid", "ModernDeferred", "LegacyDeferred"};
			
			ImGui::Text("%s", LOCTEXT("Renderer"));
			
			ImGui::PushItemWidth(-1);
			ImGui::Combo("##RendererList", &UserSetting.RendererType, renderers.data(), static_cast<int>(renderers.size()));
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
			if (ImGui::Combo("##SceneList", &UserSetting.SceneIndex, scenes.data(), static_cast<int>(scenes.size())) )
			{
				// Request Scene Load
				GetEngine().RequestLoadScene(SceneList::AllScenes[UserSetting.SceneIndex]);
			}
			ImGui::PopItemWidth();

			int prevCameraIdx = UserSetting.CameraIdx;
			ImGui::Text("%s", LOCTEXT("Camera"));
			ImGui::PushItemWidth(-1);
			ImGui::Combo("##CameraList", &UserSetting.CameraIdx, camerasList.data(), static_cast<int>(camerasList.size()));
			ImGui::PopItemWidth();
			if(prevCameraIdx != UserSetting.CameraIdx)
			{
				GetEngine().GetScene().SetRenderCamera( GetEngine().GetScene().GetCameras()[UserSetting.CameraIdx] );
				modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
			}

			auto& Camera = GetEngine().GetScene().GetRenderCamera();
			ImGui::SliderFloat(LOCTEXT("Aperture"), &Camera.Aperture, 0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("Focus(cm)"), &Camera.FocalDistance, 0.001f, 1000.0f, "%.3f");
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Ray Tracing"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
			ImGui::Checkbox(LOCTEXT("AntiAlias"), &UserSetting.TAA);
			ImGui::SliderInt(LOCTEXT("Samples"), &UserSetting.NumberOfSamples, 1, 16);
			ImGui::SliderInt(LOCTEXT("TemporalSteps"), &UserSetting.AdaptiveSteps, 2, 64);
			ImGui::Checkbox(LOCTEXT("BakeWithGPU"), &UserSetting.BakeWithGPU);
			ImGui::Checkbox(LOCTEXT("FastGather"), &UserSetting.FastGather);
			ImGui::Checkbox(LOCTEXT("FastInterpole"), &UserSetting.FastInterpole);
			
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Denoiser"), ImGuiTreeNodeFlags_DefaultOpen) )
		{
#if WITH_OIDN
			ImGui::Checkbox("Use OIDN", &UserSetting.Denoiser);
#else
			ImGui::Checkbox(LOCTEXT("Use JBF"), &UserSetting.Denoiser);
			ImGui::SliderFloat(LOCTEXT("DenoiseSigma"), &UserSetting.DenoiseSigma, 0.01f, 2.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaLum"), &UserSetting.DenoiseSigmaLum, 0.01f, 50.0f, "%.2f");
			ImGui::SliderFloat(LOCTEXT("DenoiseSigmaNormal"), &UserSetting.DenoiseSigmaNormal, 0.001f, 0.2f, "%.3f");
			ImGui::SliderInt(LOCTEXT("DenoiseSize"), &UserSetting.DenoiseSize, 1, 10);
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

			ImGui::SliderFloat(LOCTEXT("PaperWhitNit"), &UserSetting.PaperWhiteNit, 100.0f, 1600.0f, "%.1f");
			ImGui::NewLine();
		}

		if( ImGui::CollapsingHeader(LOCTEXT("Misc"), ImGuiTreeNodeFlags_None) )
		{
			ImGui::Text("%s", LOCTEXT("Profiler"));
			ImGui::Separator();
			ImGui::Checkbox(LOCTEXT("ShowWireframe"), &GetEngine().GetRenderer().showWireframe_);
			ImGui::Checkbox(LOCTEXT("DebugDraw"), &UserSetting.ShowVisualDebug);
			ImGui::SliderFloat(LOCTEXT("Time Scaling"), &UserSetting.HeatmapScale, 0.10f, 2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
			ImGui::NewLine();

			ImGui::Text("%s", LOCTEXT("Performance"));
			ImGui::Separator();
			uint32_t min = 8, max = 32;
			ImGui::SliderScalar(LOCTEXT("Temporal Frames"), ImGuiDataType_U32, &UserSetting.TemporalFrames, &min, &max);		
		}
	}
	ImGui::End();
}
