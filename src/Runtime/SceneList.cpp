#include "SceneList.hpp"
#include "Assets/Material.hpp"
#include "Assets/Model.hpp"
#include "Assets/Texture.hpp"
#include <functional>
#include <random>
#include <filesystem>
#include <algorithm>

#include "Engine.hpp"
#include "Utilities/FileHelper.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"

namespace Vulkan
{
    class VulkanBaseRenderer;
}

using namespace glm;

using Assets::Material;
using Assets::Model;
using Assets::Texture;

// 这里保留procedural的场景代码，后续再添加，先去掉functor的场景创建，换成使用loader
#if 0
namespace
{
    int CreateMaterial(std::vector<Assets::Material>& materials, Material mat)
    {
        materials.push_back(mat);
        return static_cast<int>(materials.size() - 1);
    }

    void AddRayTracingInOneWeekendCommonScene(std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::Material>& materials,
                                              const bool& isProc, std::function<float ()>& random)
    {
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
                    if (chooseMat < 0.7f) // Diffuse
                    {
                        const float b = random() * random();
                        const float g = random() * random();
                        const float r = random() * random();

                        models.push_back(
                            Model::CreateSphere(center, 0.2f, CreateMaterial(materials, Material::Lambertian(vec3(r, g, b))), isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
                    }
                    else if (chooseMat < 0.9f) // Metal
                    {
                        const float fuzziness = 0.5f * random();
                        const float b = 0.5f * (1 + random());
                        const float g = 0.5f * (1 + random());
                        const float r = 0.5f * (1 + random());

                        models.push_back(Model::CreateSphere(center, 0.2f, CreateMaterial(materials, Material::Metallic(vec3(r, g, b), fuzziness)),
                                                             isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
                    }
                    else // Glass
                    {
                        const float fuzziness = 0.5f * random();
                        models.push_back(
                            Model::CreateSphere(center, 0.2f, CreateMaterial(materials, Material::Dielectric(1.45f, fuzziness)), isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
                    }
                }
            }
        }
    }
}

void RayTracingInOneWeekend(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::Material>& materials,
                            std::vector<Assets::LightObject>& lights)
{
    // Final scene from Ray Tracing In One Weekend book.

    camera.ModelView = lookAt(vec3(13, 2, 3), vec3(0, 0, 0), vec3(0, 1, 0));
    camera.FieldOfView = 20;
    camera.Aperture = 0.1f;
    camera.FocusDistance = 8.5f;
    camera.ControlSpeed = 5.0f;
    camera.GammaCorrection = true;
    camera.HasSky = true;
    camera.HasSun = false;

    const bool isProc = false;

    std::mt19937 engine(42);
    std::function<float ()> random = std::bind(std::uniform_real_distribution<float>(), engine);

    materials.push_back(Material::Lambertian(vec3(0.4f, 0.4f, 0.4f)));
    models.push_back(Model::CreateBox(vec3(-1000, -0.5, -1000), vec3(1000, 0, 1000),
                                      static_cast<int>(materials.size() - 1)));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), false));

    AddRayTracingInOneWeekendCommonScene(nodes, models, materials, isProc, random);

    models.push_back(Model::CreateSphere(vec3(0, 1, 0), 1.0f, CreateMaterial(materials, Material::Dielectric(1.5f, 0.0f)), isProc));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
    models.push_back(Model::CreateSphere(vec3(-4, 1, 0), 1.0f, CreateMaterial(materials, Material::Lambertian(vec3(0.4f, 0.2f, 0.1f))), isProc));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
    models.push_back(Model::CreateSphere(vec3(4, 1, 0), 1.0f, CreateMaterial(materials, Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.5f)),
                                         isProc));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
}
#endif

void CornellBox(Assets::EnvironmentSetting& cameraInit, std::vector< std::shared_ptr<Assets::Node> >& nodes,
                              std::vector<Assets::Model>& models,
                              std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
    //cameraInit.ModelView = lookAt(vec3(0, 278, 1078), vec3(0, 278, 0), vec3(0, 1, 0));
    //camera.FieldOfView = 40;
    //camera.Aperture = 0.0f;
    //camera.FocusDistance = 778.0f;
    
    cameraInit.ControlSpeed = 200.0f;
    cameraInit.GammaCorrection = true;
    cameraInit.HasSky = false;
    cameraInit.HasSun = false;


    int cbox_model = Model::CreateCornellBox(555, models, materials, lights);
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0,0,0), quat(1,0,0,0), vec3(1,1,1), cbox_model, static_cast<uint32_t>(nodes.size()), false));
    nodes.back()->SetVisible(true);
    
    materials.push_back({ "cbox_white", 4, Material::Lambertian(vec3(0.73f, 0.73f, 0.73f))});
    auto box0 = Model::CreateBox(vec3(-80, 0, -80), vec3(80, 160, 80), static_cast<uint32_t>(materials.size() - 1));
    models.push_back(box0);

    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0 + 130 - 0, 0, 80), quat(vec3(0,0.5f,0)), vec3(1,1,1), cbox_model + 1, static_cast<uint32_t>(nodes.size()), false));
    nodes.back()->SetVisible(true);
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0 - 130 - 0, 0, -80), quat(vec3(0,0.25f,0)), vec3(1,2,1), cbox_model + 1, static_cast<uint32_t>(nodes.size()), false));
    nodes.back()->SetVisible(true);
    
    Assets::Camera defaultCam;
    defaultCam.ModelView = lookAt(vec3(0, 278, 1078), vec3(0, 278, 0), vec3(0, 1, 0));
    defaultCam.FieldOfView = 40;
    
    // if no camera, add default
    if (cameraInit.cameras.empty() )
    {
        cameraInit.cameras.push_back(defaultCam);
    }
}

std::vector<std::string> SceneList::AllScenes;

void SceneList::ScanScenes()
{
    // add relative path
    std::string modelPath = "assets/models/";
    std::string path = Utilities::FileHelper::GetPlatformFilePath(modelPath.c_str());
    fmt::print("Scaning dir: {}\n", path);
    for (const auto& entry : std::filesystem::directory_iterator(path))
    {
        std::filesystem::path filename = entry.path().filename();
        std::string ext = entry.path().extension().string();
        if (ext != ".glb" && ext != ".gltf") continue;
        AllScenes.push_back((modelPath / filename).string());
    }

    AllScenes.push_back("0cornellbox.proc");
    
    // sort the scene
    std::sort(AllScenes.begin(), AllScenes.end());
    
    fmt::print("Scene found: {}\n", AllScenes.size());
}

int32_t SceneList::AddExternalScene(std::string absPath)
{
    // add absolute path
    if ( std::filesystem::exists(absPath) )
    {
        AllScenes.push_back(absPath);
    }
    return static_cast<int32_t>(AllScenes.size() - 1);
}

bool SceneList::LoadScene(std::string filename, Assets::EnvironmentSetting& camera, std::vector< std::shared_ptr<Assets::Node> >& nodes, std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                          std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
{
    std::filesystem::path filepath = filename;
    std::string ext = filepath.extension().string();
    if (ext == ".glb" || ext == ".gltf")
    {
        return Model::LoadGLTFScene(filename, camera, nodes, models, materials, lights, tracks);
    }
    if (ext == ".proc")
    {
        CornellBox(camera, nodes, models, materials, lights, tracks);
        return true;
    }
    
    return false;
}
