#include "SceneList.hpp"
#include "Assets/Material.hpp"
#include "Assets/Model.hpp"
#include "Assets/Texture.hpp"
#include <functional>
#include <random>
#include <filesystem>

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

extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;

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
                    if (chooseMat < 0.8f) // Diffuse
                    {
                        const float b = random() * random();
                        const float g = random() * random();
                        const float r = random() * random();

                        models.push_back(
                            Model::CreateSphere(center, 0.2f, CreateMaterial( materials, Material::Lambertian(vec3(r, g, b)) ), isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), isProc));
                    }
                    else if (chooseMat < 0.95f) // Metal
                    {
                        const float fuzziness = 0.5f * random();
                        const float b = 0.5f * (1 + random());
                        const float g = 0.5f * (1 + random());
                        const float r = 0.5f * (1 + random());

                        models.push_back(Model::CreateSphere(center, 0.2f, CreateMaterial(materials, Material::Metallic(vec3(r, g, b), fuzziness)),
                                                             isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), isProc));
                    }
                    else // Glass
                    {
                        const float fuzziness = 0.5f * random();
                        models.push_back(
                            Model::CreateSphere(center, 0.2f, CreateMaterial(materials,Material::Dielectric(1.45f, fuzziness)), isProc));
                        nodes.push_back(
                            Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), isProc));
                    }
                }
            }
        }
    }
}

void RayTracingInOneWeekend(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                                       std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
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
    
    const bool isProc = GApplication->GetRendererType() != "RayQueryRenderer";

    std::mt19937 engine(42);
    std::function<float ()> random = std::bind(std::uniform_real_distribution<float>(), engine);

    materials.push_back(Material::Lambertian(vec3(0.4f, 0.4f, 0.4f)));
    models.push_back(Model::CreateBox(vec3(-1000, -0.5, -1000), vec3(1000, 0, 1000),
                                      static_cast<int>(materials.size() - 1)));
    nodes.push_back(Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), false));

    AddRayTracingInOneWeekendCommonScene(nodes, models, materials, isProc, random);

    models.push_back(Model::CreateSphere(vec3(0, 1, 0), 1.0f, CreateMaterial(materials,Material::Dielectric(1.5f, 0.5f)), isProc));
    nodes.push_back(Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), isProc));
    models.push_back(Model::CreateSphere(vec3(-4, 1, 0), 1.0f, CreateMaterial(materials,Material::Lambertian(vec3(0.4f, 0.2f, 0.1f))), isProc));
    nodes.push_back(Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), isProc));
    models.push_back(Model::CreateSphere(vec3(4, 1, 0), 1.0f, CreateMaterial(materials,Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.3f)),
                                         isProc));
    nodes.push_back(Assets::Node::CreateNode(glm::mat4(1), static_cast<int>(models.size() - 1), isProc));
}

void CornellBox(Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes,
                           std::vector<Assets::Model>& models, std::vector<Assets::Texture>& textures,
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
    nodes.push_back(Assets::Node::CreateNode(mat4(1), cbox_model, false));

    
    materials.push_back( Material::Lambertian(vec3(0.73f, 0.73f, 0.73f)) );
    auto box0 = Model::CreateBox(vec3(0, 0, -165), vec3(165, 165, 0), static_cast<int>(materials.size() - 1) );
    models.push_back(box0);
    
    glm::mat4 ts0 = (rotate(translate(mat4(1), vec3(278 - 130 - 165, 0, 213)), radians(-18.0f), vec3(0, 1, 0)));
    glm::mat4 ts1 = (rotate(scale(translate(mat4(1), vec3(278 - 265 - 165, 0, 17)), vec3(1, 2, 1)),
                                          radians(15.0f), vec3(0, 1, 0)));
   
    nodes.push_back(Assets::Node::CreateNode(ts0, 1, false));
    nodes.push_back(Assets::Node::CreateNode(ts1, 1, false));
}

// procedural scene without assets
std::vector<scenes_pair> SceneList::AllScenes
    =
    {
    {"Ray Tracing In One Weekend", RayTracingInOneWeekend},
    {"Cornell Box", CornellBox},
    };

typedef std::unordered_map<std::string, std::string> uo_string_string_t;
uo_string_string_t sceneNames =
{
    {"qx50.glb",            "Qx50"},
    {"track.glb",           "LowpolyTrack"},
    {"simple.glb",          "Simple"},
    {"complex.glb",         "Complex"},
    {"livingroom.glb",      "LivingRoom"},
    {"kitchen.glb",         "Kitchen"},
    {"luxball.glb",         "LuxBall"},
    {"moderndepart.glb",    "ModernHouse1"}
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
        uo_string_string_t::const_iterator got = sceneNames.find(fn);

        //if found - change fn. if not - just filename
        if (got != sceneNames.end()) fn = got->second;
        AllScenes.push_back({fn, [=](Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                    std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                    std::vector<Assets::LightObject>& lights)
        {
            if(entry.path().extension().string() == ".glb")
            {
                Model::LoadGLTFScene(absolute(entry.path()).string(), camera, nodes, models, textures, materials, lights);
            }
            else if(entry.path().extension().string() == ".obj")
            {
                Model::LoadObjModel(absolute(entry.path()).string(), nodes, models, textures, materials, lights);

                camera.FieldOfView = 38;
                camera.Aperture = 0.0f;
                camera.FocusDistance = 100.0f;
                camera.ControlSpeed = 1.0f;
                camera.GammaCorrection = true;
                camera.HasSky = true;

                //auto center camera by scene bounds
                glm::vec3 boundsMin, boundsMax;
                for (int i = 0; i < models.size(); i++)
                {
                	auto& model = models[i];
                	glm::vec3 AABBMin = model.GetLocalAABBMin();
                	glm::vec3 AABBMax = model.GetLocalAABBMax();
                	if (i == 0)
                	{
                		boundsMin = AABBMin;
                		boundsMax = AABBMax;
                	}
                	else
                	{
                		boundsMin = glm::min(AABBMin, boundsMin);
                		boundsMax = glm::max(AABBMax, boundsMax);
                	}
                }

                glm::vec3 boundsCenter = (boundsMax - boundsMin) * 0.5f + boundsMin;
                camera.ModelView = lookAt(vec3(boundsCenter.x, boundsCenter.y, boundsCenter.z + glm::length(boundsMax - boundsMin)), boundsCenter, vec3(0, 1, 0));
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
    if (std::filesystem::exists(absPath) && (filename.extension().string() == ".glb" || filename.extension().string() == ".obj") )
    {
        AllScenes.push_back({filename.filename().string(), [=](Assets::CameraInitialSate& camera, std::vector<Assets::Node>& nodes, std::vector<Assets::Model>& models,
                    std::vector<Assets::Texture>& textures, std::vector<Assets::Material>& materials,
                    std::vector<Assets::LightObject>& lights)
        {
            if(filename.extension().string() == ".glb")
            {
                Model::LoadGLTFScene(absolute(filename).string(), camera, nodes, models, textures, materials, lights);
            }
            else if(filename.extension().string() == ".obj")
            {
                Model::LoadObjModel(absolute(filename).string(), nodes, models, textures, materials, lights);

                camera.FieldOfView = 38;
                camera.Aperture = 0.0f;
                camera.FocusDistance = 100.0f;
                camera.ControlSpeed = 1.0f;
                camera.GammaCorrection = true;
                camera.HasSky = true;

                //auto center camera by scene bounds
                glm::vec3 boundsMin, boundsMax;
                for (int i = 0; i < models.size(); i++)
                {
                	auto& model = models[i];
                	glm::vec3 AABBMin = model.GetLocalAABBMin();
                	glm::vec3 AABBMax = model.GetLocalAABBMax();
                	if (i == 0)
                	{
                		boundsMin = AABBMin;
                		boundsMax = AABBMax;
                	}
                	else
                	{
                		boundsMin = glm::min(AABBMin, boundsMin);
                		boundsMax = glm::max(AABBMax, boundsMax);
                	}
                }

                glm::vec3 boundsCenter = (boundsMax - boundsMin) * 0.5f + boundsMin;
                camera.ModelView = lookAt(vec3(boundsCenter.x, boundsCenter.y, boundsCenter.z + glm::length(boundsMax - boundsMin)), boundsCenter, vec3(0, 1, 0));
            }
        }
        });
    }
    return static_cast<int32_t>(AllScenes.size()) - 1;
}
