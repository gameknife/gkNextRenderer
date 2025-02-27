#include "SceneList.hpp"
#include "Assets/Material.hpp"
#include "Assets/Model.hpp"
#include "Assets/Texture.hpp"
#include <functional>
#include <random>
#include <filesystem>
#include <algorithm>

#include "Engine.hpp"
#include "NextPhysics.h"
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
namespace
{
    int CreateMaterial(std::vector<Assets::FMaterial>& materials, Material mat)
    {
        materials.push_back({"", static_cast<uint32_t>(materials.size()), mat});
        return static_cast<int>(materials.size() - 1);
    }

    void AddRayTracingInOneWeekendCommonScene(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::AnimationTrack>& tracks, std::function<float ()>& random)
    {
        models.push_back(Model::CreateSphere(vec3(0,0,0), 0.2f));
        uint32_t meshIdx = static_cast<uint32_t>(models.size() - 1);
        for (int i = -22; i < 22; ++i)
        {
            for (int j = -22; j < 22; ++j)
            {
                const float chooseMat = random();
                const float center_y = static_cast<float>(j) + 0.9f * random();
                const float center_x = static_cast<float>(i) + 0.9f * random();
                const vec3 center(center_x, 0.2f + 2.0f * chooseMat, center_y);

                const std::string Name = Utilities::NameHelper::RandomName(6);

                // Animation Sequence
                // const float Duration = 0.3f;
                // Assets::AnimationTrack track{};
                // track.Duration_ = Duration;
                // track.NodeName_ = Name;
                // int offset = static_cast<int>(random() * 9.0f);
                // for ( int i = 0; i < 11; ++i )
                // {
                //     float time = i / 10.0f;
                //     float shifttime = ((i + offset) % 10) / 10.0f;
                //     float mixer = glm::abs(shifttime - 0.5f) / 0.5f;
                //     track.TranslationChannel.Keys.push_back( {time * Duration, center + vec3(0,glm::sqrt(mixer) * 0.15f,0)} );
                // }
                // tracks.push_back(track);
                
                if (length(center - vec3(4, 0.2f, 0)) > 0.9f)
                {
                    auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(center, 0.2f, JPH::EMotionType::Dynamic);
                    
                    if (chooseMat < 0.7f) // Diffuse
                    {
                        const float b = random() * random();
                        const float g = random() * random();
                        const float r = random() * random();
                        uint32_t matId = CreateMaterial(materials, Material::Lambertian(vec3(r,g,b)));
                        nodes.push_back(Assets::Node::CreateNode(Name,
                                                                 center,
                                                                 quat(1, 0, 0, 0),
                                                                 vec3(1, 1, 1),
                                                                 meshIdx,
                                                                 static_cast<uint32_t>(nodes.size()),
                                                                 false));
                        nodes.back()->SetVisible(true);
                        nodes.back()->SetMaterial({matId});
                    }
                    else if (chooseMat < 0.9f) // Metal
                    {
                        const float fuzziness = 0.5f * random();
                        const float b = 0.5f * (1 + random());
                        const float g = 0.5f * (1 + random());
                        const float r = 0.5f * (1 + random());
                        uint32_t matId = CreateMaterial(materials, Material::Metallic(vec3(r,g,b), fuzziness));
                        nodes.push_back(Assets::Node::CreateNode(Name,
                                                                 center,
                                                                 quat(1, 0, 0, 0),
                                                                 vec3(1, 1, 1),
                                                                 meshIdx,
                                                                 static_cast<uint32_t>(nodes.size()),
                                                                 false));
                        nodes.back()->SetVisible(true);
                        nodes.back()->SetMaterial({matId});
                    }
                    else // Glass
                    {
                        const float fuzziness = 0.5f * random();
                        uint32_t matId = CreateMaterial(materials, Material::Dielectric(1.5f, fuzziness));
                        nodes.push_back(Assets::Node::CreateNode(Name,
                                                                 center,
                                                                 quat(1, 0, 0, 0),
                                                                 vec3(1, 1, 1),
                                                                 meshIdx,
                                                                 static_cast<uint32_t>(nodes.size()),
                                                                 false));
                        nodes.back()->SetVisible(true);
                        nodes.back()->SetMaterial({matId});
                    }
                    nodes.back()->BindPhysicsBody(id);
                }
            }
        }
    }

    void RayTracingInOneWeekend(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                std::vector<Assets::Model>& models,
                                std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "Cam";
        defaultCam.ModelView = lookAt(vec3(13, 2, 3), vec3(0, 0, 0), vec3(0, 1, 0));
        defaultCam.FieldOfView = 20;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 10;
        
        cameraInit.cameras.push_back(defaultCam);

        // Final scene from Ray Tracing In One Weekend book.
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = false;

        const bool isProc = false;

        std::mt19937 engine(42);
        std::function<float ()> random = std::bind(std::uniform_real_distribution<float>(), engine);

        materials.push_back({"", 0, Material::Lambertian(vec3(0.4f, 0.4f, 0.4f))});
        models.push_back(Model::CreateBox(vec3(-1000, -0.5, -1000), vec3(1000, 0, 1000)));
        nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0, 0, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), 0, static_cast<uint32_t>(nodes.size()), false));
        nodes.back()->SetVisible(true);
        nodes.back()->SetMaterial({0});

        NextEngine::GetInstance()->GetPhysicsEngine()->CreatePlaneBody(vec3(0,-0.5,0), vec3(2000, 0.5, 2000), JPH::EMotionType::Static);
        
        AddRayTracingInOneWeekendCommonScene(nodes, models, materials, tracks, random);

        uint32_t matIdx0 = CreateMaterial(materials, Material::Dielectric(1.5f, 0.0f));
        uint32_t matIdx1 = CreateMaterial(materials, Material::Lambertian(vec3(0.4f, 0.2f, 0.1f)));
        uint32_t matIdx2 = CreateMaterial(materials, Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.1f));
        models.push_back(Model::CreateSphere(vec3(0, 0, 0), 1.0f));
        uint32_t modelIdx = static_cast<uint32_t>(models.size() - 1);
        
        nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0, 1, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), modelIdx, static_cast<uint32_t>(nodes.size()), isProc));
        nodes.back()->SetVisible(true);
        nodes.back()->SetMaterial({matIdx0});
        auto body1 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(0, 1, 0), 1.0f, JPH::EMotionType::Dynamic);
        nodes.back()->BindPhysicsBody(body1);
        
        nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(-4, 1, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), modelIdx, static_cast<uint32_t>(nodes.size()), isProc));
        nodes.back()->SetVisible(true);
        nodes.back()->SetMaterial({matIdx1});
        auto body2 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(-4, 1, 0), 1.0f, JPH::EMotionType::Dynamic);
        nodes.back()->BindPhysicsBody(body2);
        
        nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(4, 1, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), modelIdx, static_cast<uint32_t>(nodes.size()), isProc));
        nodes.back()->SetVisible(true);
        nodes.back()->SetMaterial({matIdx2});
        auto body3 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(4, 1, 0), 1.0f, JPH::EMotionType::Dynamic);
        nodes.back()->BindPhysicsBody(body3);
        
    }

    void CornellBox(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "Cam";
        defaultCam.ModelView = lookAt(vec3(0, 2.78, 10.78), vec3(0, 2.78, 0), vec3(0, 1, 0));
        defaultCam.FieldOfView = 40;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 10;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 200.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = false;
        cameraInit.HasSun = false;

        int cbox_model = Model::CreateCornellBox(5.55f, models, materials, lights);
        nodes.push_back(Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0, 0, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), cbox_model, static_cast<uint32_t>(nodes.size()), false));
        nodes.back()->SetVisible(true);
        nodes.back()->SetMaterial({0,1,2,3});

        auto spherePos = vec3(1.30, 1.01 + 2.00, 0.80);
        auto boxPos = vec3(-1.30, 0, -0.80);
        
        materials.push_back({"cbox_white", 4, Material::Lambertian(vec3(0.73f, 0.73f, 0.73f))});
        materials.push_back({"cball_white", 5, Material::Dielectric(1.85f, 0.0f)});
        auto box0 = Model::CreateBox(vec3(-0.80, 0, -0.80), vec3(0.80, 1.60, 0.80));
        models.push_back(box0);
        auto ball0 = Model::CreateSphere(vec3(0, 0, 0), 1.0f);
        models.push_back(ball0);
        nodes.push_back(Assets::Node::CreateNode("Sphere1", spherePos, quat(vec3(0, 0.5f, 0)), vec3(1, 1, 1), cbox_model + 2, static_cast<uint32_t>(nodes.size()),
                                                 false));
        nodes.back()->SetVisible(true);
        nodes.back()->SetMaterial({5});

        auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(spherePos, 1.0f, JPH::EMotionType::Dynamic);
        nodes.back()->BindPhysicsBody(id);
        
        nodes.push_back(Assets::Node::CreateNode("Box", boxPos, quat(vec3(0, 0.25f, 0)), vec3(1, 2, 1), cbox_model + 1, static_cast<uint32_t>(nodes.size()),
                                                 false));
        nodes.back()->SetMaterial({4});
        nodes.back()->SetVisible(true);

        // create physical scene, later it will change to node components later
        NextEngine::GetInstance()->GetPhysicsEngine()->CreatePlaneBody(vec3(0, -1, 0), vec3(8, 1, 8), JPH::EMotionType::Static);
        
        // procedural animation
        // Assets::AnimationTrack track{};
        // track.Duration_ = 1.0f;
        // track.NodeName_ = "Sphere1";
        // for ( int i = 0; i < 11; ++i )
        // {
        //     float time = i / 10.0f;
        //     float mixer = glm::abs(time - 0.5f) / 0.5f;
        //     track.TranslationChannel.Keys.push_back( {time, vec3(130, glm::mix(360, 101, mixer * mixer), 80)} );
        // }
        //
        // tracks.push_back(track);
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
    
    // sort the scene
    std::sort(AllScenes.begin(), AllScenes.end());

    AllScenes.insert(AllScenes.begin(), "RTIO.proc");
    AllScenes.insert(AllScenes.begin(), "CornellBox.proc");
    
    fmt::print("Scene found: {}\n", AllScenes.size());
}

int32_t SceneList::AddExternalScene(std::string absPath)
{
    // add absolute path
    if (std::filesystem::exists(absPath))
    {
        AllScenes.push_back(absPath);
    }
    return static_cast<int32_t>(AllScenes.size() - 1);
}

bool SceneList::LoadScene(std::string filename, Assets::EnvironmentSetting& camera, std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                          std::vector<Assets::FMaterial>& materials,
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
        if (filename == "CornellBox.proc")
        {
            CornellBox(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        if (filename == "RTIO.proc")
        {
            RayTracingInOneWeekend(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        return false;
    }

    return false;
}
