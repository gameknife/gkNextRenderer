#pragma once
#include "Utilities/Glm.hpp"
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace Assets
{
	class Node;
	class Model;
	class Texture;
}

class SceneList final
{
public:

	struct CameraInitialSate
	{
		glm::mat4 ModelView;
		float FieldOfView;
		float Aperture;
		float FocusDistance;
		float ControlSpeed;
		bool GammaCorrection;
		bool HasSky;
	};

	static void CubeAndSpheres(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void RayTracingInOneWeekend(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void CornellBox(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void CornellBoxLucy(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void LivingRoom(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void Kitchen(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void LuxBall(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);
	static void Still(CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures);

	static const std::vector<std::pair<std::string, std::function<void (CameraInitialSate&, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>&, std::vector<Assets::Texture>&)>>> AllScenes;
};
