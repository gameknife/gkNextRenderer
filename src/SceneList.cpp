#include "SceneList.hpp"
#include "Assets/Material.hpp"
#include "Assets/Model.hpp"
#include "Assets/Texture.hpp"
#include <functional>
#include <random>

using namespace glm;

using Assets::Material;
using Assets::Model;
using Assets::Texture;

namespace
{

	void AddRayTracingInOneWeekendCommonScene(std::vector<Assets::Model>& models, const bool& isProc, std::function<float ()>& random)
	{
		// Common models from the final scene from Ray Tracing In One Weekend book. Only the three central spheres are missing.
		// Calls to random() are always explicit and non-inlined to avoid C++ undefined evaluation order of function arguments,
		// this guarantees consistent and reproducible behaviour across different platforms and compilers.

		//models.push_back(Model::CreateSphere(vec3(0, -1000, 0), 1000, Material::Lambertian(vec3(0.5f, 0.5f, 0.5f)), isProc));

		for (int i = -11; i < 11; ++i)
		{
			for (int j = -11; j < 11; ++j)
			{
				const float chooseMat = random();
				const float center_y = static_cast<float>(j) + 0.9f * random();
				const float center_x = static_cast<float>(i) + 0.9f * random();
				const vec3 center(center_x, 0.2f, center_y);

				if (length(center - vec3(4, 0.2f, 0)) > 0.9f)
				{
					if (chooseMat < 0.8f) // Diffuse
					{
						const float b = random() * random();
						const float g = random() * random();
						const float r = random() * random();

						models.push_back(Model::CreateSphere(center, 0.2f, Material::Lambertian(vec3(r, g, b)), isProc));
					}
					else if (chooseMat < 0.95f) // Metal
					{
						const float fuzziness = 0.5f * random();
						const float b = 0.5f * (1 + random());
						const float g = 0.5f * (1 + random());
						const float r = 0.5f * (1 + random());

						models.push_back(Model::CreateSphere(center, 0.2f, Material::Metallic(vec3(r, g, b), fuzziness), isProc));
					}
					else // Glass
					{
						const float fuzziness = 0.5f * random();
						models.push_back(Model::CreateSphere(center, 0.2f, Material::Dielectric(1.45f, fuzziness), isProc));
					}
				}
			}
		}
	}

	void AddRayTracingInOneWeekendCommonSceneBox(std::vector<Assets::Model>& models, const bool& isProc, std::function<float ()>& random)
	{
		// Common models from the final scene from Ray Tracing In One Weekend book. Only the three central spheres are missing.
		// Calls to random() are always explicit and non-inlined to avoid C++ undefined evaluation order of function arguments,
		// this guarantees consistent and reproducible behaviour across different platforms and compilers.

		models.push_back(Model::CreateSphere(vec3(0, -1000, 0), 1000, Material::Lambertian(vec3(0.5f, 0.5f, 0.5f)), isProc));

		for (int i = -11; i < 11; ++i)
		{
			for (int j = -11; j < 11; ++j)
			{
				const float chooseMat = random();
				const float center_y = static_cast<float>(j) + 0.9f * random();
				const float center_x = static_cast<float>(i) + 0.9f * random();
				const vec3 center(center_x, 0.2f, center_y);
				const vec3 centerl = center + vec3(.4,.4,.4);

				if (length(center - vec3(4, 0.2f, 0)) > 0.9f)
				{
					if (chooseMat < 0.8f) // Diffuse
						{
						const float b = random() * random();
						const float g = random() * random();
						const float r = random() * random();

						models.push_back(Model::CreateBox(center, centerl, Material::Lambertian(vec3(r, g, b))));
						}
					else // Metal
						{
						const float fuzziness = 0.5f * random();
						const float b = 0.5f * (1 + random());
						const float g = 0.5f * (1 + random());
						const float r = 0.5f * (1 + random());

						models.push_back(Model::CreateBox(center, centerl, Material::Lambertian(vec3(r, g, b))));
						}
				}
			}
		}
	}

}

const std::vector<std::pair<std::string, std::function<void (SceneList::CameraInitialSate&, std::vector<Assets::Model>&, std::vector<Assets::Texture>&)>>> SceneList::AllScenes =
{
	{"Cube And Spheres", CubeAndSpheres},
	{"Ray Tracing In One Weekend", RayTracingInOneWeekend},
	{"Planets In One Weekend", PlanetsInOneWeekend},
	{"Lucy In One Weekend", LucyInOneWeekend},
	{"Cornell Box", CornellBox},
	{"Cornell Box & Lucy", CornellBoxLucy},
	{"LivingRoom", LivingRoom},
	{"Kitchen", Kitchen},
	{"LuxBall", LuxBall},
	{"Still", Still},
};

void SceneList::CubeAndSpheres(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(13, 2, 3), vec3(0, 0, 0), vec3(0, 1, 0));
	camera.FieldOfView = 20;
	camera.Aperture = 0.1f;
	camera.FocusDistance = 1000.0f;
	camera.ControlSpeed = 5.0f;
	camera.GammaCorrection = true;
	camera.HasSky = true;

	const bool isProc = true;

	std::mt19937 engine(42);
	std::function<float ()> random = std::bind(std::uniform_real_distribution<float>(), engine);

	AddRayTracingInOneWeekendCommonSceneBox(models, isProc, random);
}

void SceneList::RayTracingInOneWeekend(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	// Final scene from Ray Tracing In One Weekend book.
	
	camera.ModelView = lookAt(vec3(13, 2, 3), vec3(0, 0, 0), vec3(0, 1, 0));
	camera.FieldOfView = 20;
	camera.Aperture = 0.1f;
	camera.FocusDistance = 1000.0f;
	camera.ControlSpeed = 5.0f;
	camera.GammaCorrection = true;
	camera.HasSky = true;

	const bool isProc = true;

	std::mt19937 engine(42);
	std::function<float ()> random = std::bind(std::uniform_real_distribution<float>(), engine);
	models.push_back(Model::CreateBox(vec3(-1000, -0.5, -1000), vec3(1000, 0, 1000), Material::Lambertian(vec3(0.4f, 0.4f, 0.4f))));
	
	AddRayTracingInOneWeekendCommonScene(models, isProc, random);

	models.push_back(Model::CreateSphere(vec3(0, 1, 0), 1.0f, Material::Dielectric(1.5f, 0.5f), isProc));
	models.push_back(Model::CreateSphere(vec3(-4, 1, 0), 1.0f, Material::Lambertian(vec3(0.4f, 0.2f, 0.1f)), isProc));
	models.push_back(Model::CreateSphere(vec3(4, 1, 0), 1.0f, Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.3f), isProc));
}

void SceneList::PlanetsInOneWeekend(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	// Same as RayTracingInOneWeekend but using textures.
	
	camera.ModelView = lookAt(vec3(13, 2, 3), vec3(0, 0, 0), vec3(0, 1, 0));
	camera.FieldOfView = 20;
	camera.Aperture = 0.0f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 5.0f;
	camera.GammaCorrection = true;
	camera.HasSky = true;

	const bool isProc = true;

	std::mt19937 engine(42);
	std::function<float()> random = std::bind(std::uniform_real_distribution<float>(), engine);

	AddRayTracingInOneWeekendCommonScene(models, isProc, random);

	models.push_back(Model::CreateSphere(vec3(0, 1, 0), 1.0f, Material::Metallic(vec3(1.0f), 0.1f, 3), isProc));
	models.push_back(Model::CreateSphere(vec3(-4, 1, 0), 1.0f, Material::Lambertian(vec3(1.0f), 1), isProc));
	models.push_back(Model::CreateSphere(vec3(4, 1, 0), 1.0f, Material::Metallic(vec3(1.0f), 0.0f, 2), isProc));

	textures.push_back(Texture::LoadTexture("../assets/textures/2k_mars.jpg", Vulkan::SamplerConfig()));
	textures.push_back(Texture::LoadTexture("../assets/textures/2k_moon.jpg", Vulkan::SamplerConfig()));
	textures.push_back(Texture::LoadTexture("../assets/textures/land_ocean_ice_cloud_2048.png", Vulkan::SamplerConfig()));
}

void SceneList::LucyInOneWeekend(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	// Same as RayTracingInOneWeekend but using the Lucy 3D model.
	
	camera.ModelView = lookAt(vec3(13, 2, 3), vec3(0, 1.0, 0), vec3(0, 1, 0));
	camera.FieldOfView = 20;
	camera.Aperture = 0.05f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 5.0f;
	camera.GammaCorrection = true;
	camera.HasSky = true;

	const bool isProc = true;

	std::mt19937 engine(42);
	std::function<float()> random = std::bind(std::uniform_real_distribution<float>(), engine);
	
	AddRayTracingInOneWeekendCommonScene(models, isProc, random);

	auto lucy0 = Model::LoadModel("../assets/models/lucy.obj", textures);
	auto lucy1 = lucy0;
	auto lucy2 = lucy0;

	const auto i = mat4(1);
	const float scaleFactor = 0.0035f;

	lucy0.Transform(
		rotate(
			scale(
				translate(i, vec3(0, -0.08f, 0)), 
				vec3(scaleFactor)),
			radians(90.0f), vec3(0, 1, 0)));

	lucy1.Transform(
		rotate(
			scale(
				translate(i, vec3(-4, -0.08f, 0)),
				vec3(scaleFactor)),
			radians(90.0f), vec3(0, 1, 0)));

	lucy2.Transform(
		rotate(
			scale(
				translate(i, vec3(4, -0.08f, 0)),
				vec3(scaleFactor)),
			radians(90.0f), vec3(0, 1, 0)));

	lucy0.SetMaterial(Material::Dielectric(1.5f, 0.5f));
	lucy1.SetMaterial(Material::Lambertian(vec3(0.4f, 0.2f, 0.1f)));
	lucy2.SetMaterial(Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.05f));

	models.push_back(std::move(lucy0));
	models.push_back(std::move(lucy1));
	models.push_back(std::move(lucy2));
}

void SceneList::CornellBox(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(278, 278, 800), vec3(278, 278, 0), vec3(0, 1, 0));
	camera.FieldOfView = 40;
	camera.Aperture = 0.0f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 500.0f;
	camera.GammaCorrection = true;
	camera.HasSky = false;

	const auto i = mat4(1);
	const auto white = Material::Lambertian(vec3(0.73f, 0.73f, 0.73f));

	auto box0 = Model::CreateBox(vec3(0, 0, -165), vec3(165, 165, 0), white);
	auto box1 = Model::CreateBox(vec3(0, 0, -165), vec3(165, 330, 0), white);

	box0.Transform(rotate(translate(i, vec3(555 - 130 - 165, 0, -65)), radians(-18.0f), vec3(0, 1, 0)));
	box1.Transform(rotate(translate(i, vec3(555 - 265 - 165, 0, -295)), radians(15.0f), vec3(0, 1, 0)));
	
	models.push_back(Model::CreateCornellBox(555));
	models.push_back(box0);
	models.push_back(box1);
}

void SceneList::CornellBoxLucy(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(278, 278, 800), vec3(278, 278, 0), vec3(0, 1, 0));
	camera.FieldOfView = 40;
	camera.Aperture = 0.0f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 500.0f;
	camera.GammaCorrection = true;
	camera.HasSky = false;
	
	const auto i = mat4(1);
	const auto sphere = Model::CreateSphere(vec3(555 - 130, 165.0f, -165.0f / 2 - 65), 80.0f, Material::Isotropic(vec3(0.7,1.0,0.65), 1.5f, 0.5f), true);
	auto lucy0 = Model::LoadModel("../assets/models/lucy.obj", textures);

	lucy0.Transform(
		rotate(
			scale(
				translate(i, vec3(555 - 300 - 165/2, -9, -295 - 165/2)),
				vec3(0.6f)),
			radians(75.0f), vec3(0, 1, 0)));


	models.push_back(Model::CreateCornellBox(555));
	models.push_back(sphere);
	models.push_back(lucy0);
}

void SceneList::LivingRoom(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(0, 1.5, 8), vec3(0, 1.5, 4.5), vec3(0, 1, 0));
	camera.FieldOfView = 45;
	camera.Aperture = 0.0f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 1.0f;
	camera.GammaCorrection = true;
	camera.HasSky = true;
	
	const auto i = mat4(1);
	
	const auto arealight = Material::DiffuseLight(vec3(30,30,30));
	auto box0 = Model::CreateBox(vec3(-2, 0.5, -1), vec3(2, 3, -0.5), arealight);

	auto lucy0 = Model::LoadModel("../assets/models/livingroom.obj", textures);

	lucy0.Transform(
		rotate(
			scale(
				translate(i, vec3(0, 0, 0)),
				vec3(1.0)),
			radians(0.0f), vec3(0, 1, 0)));
	
	models.push_back(box0);
	models.push_back(lucy0);
}

void SceneList::Kitchen(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(2, 1.5, 3), vec3(-4, 1.5, -4), vec3(0, 1, 0));
	camera.FieldOfView = 40;
	camera.Aperture = 0.0f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 1.0f;
	camera.GammaCorrection = true;
	camera.HasSky = true;
	
	const auto i = mat4(1);
	
	const auto arealight = Material::DiffuseLight(vec3(30,30,25));
	auto box0 = Model::CreateBox(vec3(-2, 0.5, -5), vec3(2, 5, -4.5), arealight);

	auto lucy0 = Model::LoadModel("../assets/models/kitchen.obj", textures);

	lucy0.Transform(
		rotate(
			scale(
				translate(i, vec3(0, 0, 0)),
				vec3(1.0)),
			radians(0.0f), vec3(0, 1, 0)));

	
	models.push_back(box0);
	models.push_back(lucy0);
}

void SceneList::LuxBall(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(0.168, 0.375, 0.487), vec3(0, 0.05, 0.0), vec3(0, 1, 0));
	camera.FieldOfView = 20;
	camera.Aperture = 0.02f;
	camera.FocusDistance = 55.0f;
	camera.ControlSpeed = 0.2f;
	camera.GammaCorrection = true;
	camera.HasSky = false;
	
	const auto i = mat4(1);
	
	auto lucy0 = Model::LoadModel("../assets/models/luxball.obj", textures);
	lucy0.Transform(
		rotate(
			scale(
				translate(i, vec3(0, 0, 0)),
				vec3(1.0)),
			radians(0.0f), vec3(0, 1, 0)));
	
	models.push_back(lucy0);
}

void SceneList::Still(CameraInitialSate& camera, std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures)
{
	camera.ModelView = lookAt(vec3(0.031, 0.26, 2.454), vec3(0.031, 0.26, 2), vec3(0, 1, 0));
	camera.FieldOfView = 16;
	camera.Aperture = 0.0f;
	camera.FocusDistance = 100.0f;
	camera.ControlSpeed = 0.2f;
	camera.GammaCorrection = true;
	camera.HasSky = false;
	
	const auto i = mat4(1);
	
	auto lucy0 = Model::LoadModel("../assets/models/still1.obj", textures);

	lucy0.Transform(
		rotate(
			scale(
				translate(i, vec3(0, 0, 0)),
				vec3(1.0)),
			radians(0.0f), vec3(0, 1, 0)));
	
	models.push_back(lucy0);
}
