#include "SceneList.hpp"
#include "Assets/Material.hpp"
#include "Assets/Model.hpp"
#include "Assets/Texture.hpp"
#include <functional>
#include <random>
#include <filesystem>
#include <algorithm>

#include "Application.hpp"
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

extern std::unique_ptr<NextRendererApplication> GApplication;

namespace
{
    int CreateMaterial( std::vector<Assets::Material>& materials, Material mat )
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
                            Model::CreateSphere(center, 0.2f, CreateMaterial( materials, Material::Lambertian(vec3(r, g, b)) ), isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode( Utilities::NameHelper::RandomName(6), glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
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
                            Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
                    }
                    else // Glass
                    {
                        const float fuzziness = 0.5f * random();
                        models.push_back(
                            Model::CreateSphere(center, 0.2f, CreateMaterial(materials,Material::Dielectric(1.45f, fuzziness)), isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
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
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), false));

    AddRayTracingInOneWeekendCommonScene(nodes, models, materials, isProc, random);

    models.push_back(Model::CreateSphere(vec3(0, 1, 0), 1.0f, CreateMaterial(materials,Material::Dielectric(1.5f, 0.0f)), isProc));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
    models.push_back(Model::CreateSphere(vec3(-4, 1, 0), 1.0f, CreateMaterial(materials,Material::Lambertian(vec3(0.4f, 0.2f, 0.1f))), isProc));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
    models.push_back(Model::CreateSphere(vec3(4, 1, 0), 1.0f, CreateMaterial(materials,Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.5f)),
                                         isProc));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),glm::mat4(1), static_cast<int>(models.size() - 1), static_cast<uint32_t>(nodes.size()), isProc));
}

void CornellBox(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models,
                           std::vector<Assets::Material>& materials, std::vector<Assets::LightObject>& lights)
{
    camera.ModelView = lookAt(vec3(0, 278, 1078), vec3(0, 278, 0), vec3(0, 1, 0));
    camera.FieldOfView = 40;
    camera.Aperture = 0.0f;
    camera.FocusDistance = 778.0f;
    camera.ControlSpeed = 200.0f;
    camera.GammaCorrection = true;
    camera.HasSky = false;
    camera.HasSun = false;


    int cbox_model = Model::CreateCornellBox(555, models, materials, lights);
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),mat4(1), cbox_model, static_cast<uint32_t>(nodes.size()), false));

    
    materials.push_back( Material::Lambertian(vec3(0.73f, 0.73f, 0.73f)) );
    auto box0 = Model::CreateBox(vec3(0, 0, -165), vec3(165, 165, 0), static_cast<int>(materials.size() - 1) );
    models.push_back(box0);
    
    glm::mat4 ts0 = (rotate(translate(mat4(1), vec3(278 - 130 - 165, 0, 213)), radians(-18.0f), vec3(0, 1, 0)));
    glm::mat4 ts1 = (rotate(scale(translate(mat4(1), vec3(278 - 265 - 165, 0, 17)), vec3(1, 2, 1)),
                                          radians(15.0f), vec3(0, 1, 0)));
   
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),ts0, 1, static_cast<uint32_t>(nodes.size()), false));
    nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6),ts1, 1, static_cast<uint32_t>(nodes.size()), false));
}

// procedural scene without assets
std::vector<scenes_pair> SceneList::AllScenes
    =
    {
    {"Ray Tracing In One Weekend", RayTracingInOneWeekend},
    {"Cornell Box", CornellBox},
    };

//compare func for sort scene names
bool compareSceneNames(scenes_pair i1, scenes_pair i2) { return (i1.first < i2.first); }

// scan assets
void SceneList::ScanScenes()
{
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/models/");
    for (const auto & entry : std::filesystem::directory_iterator(path))
    {
        std::filesystem::path filename = entry.path().filename();

        //looking for beauty scene name by file name
        std::string fn = filename.string();
        Assets::uo_string_string_t::const_iterator got = Assets::sceneNames.find(fn);

        //if found - change fn. if not - just filename
        if (got != Assets::sceneNames.end()) fn = got->second;
        std::string ext = entry.path().extension().string();
        if(ext != ".glb" && ext != ".obj") continue;

        AllScenes.push_back({fn, [=](Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                    std::vector<Assets::Material>& materials,
                    std::vector<Assets::LightObject>& lights)
        {
            if(ext == ".glb")
            {
                Model::LoadGLTFScene(absolute(entry.path()).string(), camera, nodes, models, materials, lights);
            }
            else if(ext == ".obj")
            {
                Model::LoadObjModel(absolute(entry.path()).string(), nodes, models, materials, lights);

                camera.FieldOfView = 38;
                camera.Aperture = 0.0f;
                camera.FocusDistance = 100.0f;
                camera.ControlSpeed = 1.0f;
                camera.GammaCorrection = true;
                camera.HasSky = true;

                Model::AutoFocusCamera(camera, models);
            }
        }
        });
    }	
	
	//sort scene names
    sort(AllScenes.begin(), AllScenes.end(), compareSceneNames); 
}

int32_t SceneList::AddExternalScene(std::string absPath)
{
    // check if absPath exists
    std::filesystem::path filename = absPath;
    std::string ext = filename.extension().string();
    if (std::filesystem::exists(absPath) && (ext == ".glb" || ext == ".obj") )
    {
        AllScenes.push_back({filename.filename().string(), [=](Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                    std::vector<Assets::Material>& materials,
                    std::vector<Assets::LightObject>& lights)
        {
            if(ext == ".glb")
            {
                Model::LoadGLTFScene(absolute(filename).string(), camera, nodes, models, materials, lights);
            }
            else if(ext == ".obj")
            {
                Model::LoadObjModel(absolute(filename).string(), nodes, models, materials, lights);

                camera.FieldOfView = 38;
                camera.Aperture = 0.0f;
                camera.FocusDistance = 100.0f;
                camera.ControlSpeed = 1.0f;
                camera.GammaCorrection = true;
                camera.HasSky = true;

                Model::AutoFocusCamera(camera, models);
            }
        }
        });
    }
    return static_cast<int32_t>(AllScenes.size()) - 1;
}
