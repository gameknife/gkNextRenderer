#include "Runtime/Scene/SceneList.hpp"
#include "Common/CoreMinimal.hpp"
#include "Utilities/FileHelper.hpp"
#include "Assets/Data/Material.hpp"
#include "Assets/Core/Model.hpp"

#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextPhysics.h"

#include <functional>
#include <random>
#include <filesystem>
#include <algorithm>
#include <array>
#include <cctype>

#include "Assets/Loaders/FProcModel.h"
#include "Assets/Loaders/FLDrawLoader.h"
#include "Assets/Loaders/FSceneLoader.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Assets/Data/Skeleton.hpp"
#include "Runtime/Components/SkinnedMeshComponent.h"

#include <spdlog/spdlog.h>
#include <SDL3/SDL_filesystem.h>

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
    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    constexpr std::array<std::string_view, 4> kSupportedSceneExtensions{
        ".glb",
        ".gltf",
        ".ldr",
        ".mpd",
    };

    int CreateMaterial(std::vector<Assets::FMaterial>& materials, Material mat)
    {
        materials.push_back({mat});
        return static_cast<int>(materials.size() - 1);
    }

    void AddRayTracingInOneWeekendCommonScene(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::AnimationTrack>& tracks, std::function<float ()>& random)
    {
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0,0,0), 0.2f));
        uint32_t meshIdx = static_cast<uint32_t>(models.size() - 1);
        for (int i = -22; i < 22; ++i)
        {
            for (int j = -22; j < 22; ++j)
            {
                const float chooseMat = random();
                const float centerY = static_cast<float>(j) + 0.9f * random();
                const float centerX = static_cast<float>(i) + 0.9f * random();
                const vec3 center(centerX, 0.2f + 2.0f * chooseMat, centerY);

                const std::string name = Utilities::NameHelper::RandomName(6);
                
                if (length(center - vec3(4, 0.2f, 0)) > 0.9f)
                {
                    auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(center, 0.2f, NextMotionType::Dynamic);
                    
                    std::shared_ptr<Assets::Node> newNode;

                    if (chooseMat < 0.7f) // Diffuse
                    {
                        const float b = random() * random();
                        const float g = random() * random();
                        const float r = random() * random();
                        uint32_t matId = CreateMaterial(materials, Material::Lambertian(vec3(r,g,b)));
                        newNode = Assets::Node::CreateNode(name,
                                                                 center,
                                                                 quat(1, 0, 0, 0),
                                                                 vec3(1, 1, 1),
                                                                 static_cast<uint32_t>(nodes.size()));
                        auto renderComp = std::make_shared<Runtime::RenderComponent>();
                        renderComp->SetModelId(meshIdx);
                        renderComp->SetVisible(true);
                        renderComp->SetMaterial({matId});
                        newNode->AddComponent(renderComp);
                    }
                    else if (chooseMat < 0.9f) // Metal
                    {
                        const float fuzziness = 0.5f * random();
                        const float b = 0.5f * (1 + random());
                        const float g = 0.5f * (1 + random());
                        const float r = 0.5f * (1 + random());
                        uint32_t matId = CreateMaterial(materials, Material::Metallic(vec3(r,g,b), fuzziness));
                        newNode = Assets::Node::CreateNode(name,
                                                                 center,
                                                                 quat(1, 0, 0, 0),
                                                                 vec3(1, 1, 1),
                                                                 static_cast<uint32_t>(nodes.size()));
                        auto renderComp = std::make_shared<Runtime::RenderComponent>();
                        renderComp->SetModelId(meshIdx);
                        renderComp->SetVisible(true);
                        renderComp->SetMaterial({matId});
                        newNode->AddComponent(renderComp);
                    }
                    else // Glass
                    {
                        const float fuzziness = 0.5f * random();
                        uint32_t matId = CreateMaterial(materials, Material::Dielectric(1.5f, fuzziness));
                        newNode = Assets::Node::CreateNode(name,
                                                                 center,
                                                                 quat(1, 0, 0, 0),
                                                                 vec3(1, 1, 1),
                                                                 static_cast<uint32_t>(nodes.size()));
                        auto renderComp = std::make_shared<Runtime::RenderComponent>();
                        renderComp->SetModelId(meshIdx);
                        renderComp->SetVisible(true);
                        renderComp->SetMaterial({matId});
                        newNode->AddComponent(renderComp);
                    }
                    
                    nodes.push_back(newNode);
                    auto phys = std::make_shared<Runtime::PhysicsComponent>();
                    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
                    phys->BindPhysicsBody(id);
                    newNode->AddComponent(phys);
                }
            }
        }
    }

    void RayTracingInOneWeekend(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                std::vector<Assets::Model>& models,
                                std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        uint32_t prevMatId = static_cast<uint32_t>(materials.size());
        
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

        materials.push_back({Material::Lambertian(vec3(0.4f, 0.4f, 0.4f))});
        models.push_back(Assets::FProcModel::CreateBox(vec3(-1000, -0.5, -1000), vec3(1000, 0, 1000)));
        {
            auto newNode = Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0, 0, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(0);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({prevMatId + 0});
            newNode->AddComponent(renderComp);
            nodes.push_back(newNode);
        }
        
        AddRayTracingInOneWeekendCommonScene(nodes, models, materials, tracks, random);

        uint32_t matIdx0 = CreateMaterial(materials, Material::Dielectric(1.5f, 0.0f));
        uint32_t matIdx1 = CreateMaterial(materials, Material::Lambertian(vec3(0.4f, 0.2f, 0.1f)));
        uint32_t matIdx2 = CreateMaterial(materials, Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.1f));
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 1.0f));
        uint32_t modelIdx = static_cast<uint32_t>(models.size() - 1);
        
        {
            auto newNode = Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0, 1, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({matIdx0});
            newNode->AddComponent(renderComp);
            
            auto body1 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(0, 1, 0), 1.0f, NextMotionType::Dynamic);
            auto phys1 = std::make_shared<Runtime::PhysicsComponent>();
            phys1->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys1->BindPhysicsBody(body1);
            newNode->AddComponent(phys1);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(-4, 1, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({matIdx1});
            newNode->AddComponent(renderComp);
            
            auto body2 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(-4, 1, 0), 1.0f, NextMotionType::Dynamic);
            auto phys2 = std::make_shared<Runtime::PhysicsComponent>();
            phys2->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys2->BindPhysicsBody(body2);
            newNode->AddComponent(phys2);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(4, 1, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelIdx);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({matIdx2});
            newNode->AddComponent(renderComp);

            auto body3 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(4, 1, 0), 1.0f, NextMotionType::Dynamic);
            auto phys3 = std::make_shared<Runtime::PhysicsComponent>();
            phys3->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys3->BindPhysicsBody(body3);
            newNode->AddComponent(phys3);
            nodes.push_back(newNode);
        }
        
    }

    void CornellBox(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        uint32_t prevMatId = static_cast<uint32_t>(materials.size());
        
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

        int cboxModel = Assets::FProcModel::CreateCornellBox(5.55f, models, materials, lights);
        {
            auto newNode = Assets::Node::CreateNode(Utilities::NameHelper::RandomName(6), vec3(0, 0, 0), quat(1, 0, 0, 0), vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(cboxModel);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({prevMatId + 0,prevMatId + 1,prevMatId + 2,prevMatId + 3});
            newNode->AddComponent(renderComp);
            nodes.push_back(newNode);
        }

        auto spherePos = vec3(1.30, 1.01 + 2.00 * 0.0, 0.80);
        auto boxPos = vec3(-1.30, 0, -0.80);
        
        materials.push_back({Material::Lambertian(vec3(0.73f, 0.73f, 0.73f)), "cbox_white"});
        materials.push_back({Material::Mixture(vec3(0.73f, 0.73f, 0.73f), 0.01f), "cball_white"});
        auto box0 = Assets::FProcModel::CreateBox(vec3(-0.80, 0, -0.80), vec3(0.80, 1.60, 0.80));
        models.push_back(box0);
        auto ball0 = Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 1.0f);
        models.push_back(ball0);
        
        {
            auto newNode = Assets::Node::CreateNode("Sphere1", spherePos, quat(vec3(0, 0.5f, 0)), vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(cboxModel + 2);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({prevMatId + 5});
            newNode->AddComponent(renderComp);
            
            auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(spherePos, 1.0f, NextMotionType::Dynamic);
            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys->BindPhysicsBody(id);
            newNode->AddComponent(phys);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::Node::CreateNode("Box", boxPos, quat(vec3(0, 0.25f, 0)), vec3(1, 2, 1), static_cast<uint32_t>(nodes.size()));
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(cboxModel + 1);
            renderComp->SetVisible(true);
            renderComp->SetMaterial({prevMatId + 4});
            newNode->AddComponent(renderComp);
            nodes.push_back(newNode);
        }
    }

    void CharacterPlayground(Assets::EnvironmentSetting& cameraInit,
                             std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             std::vector<Assets::Model>& models,
                             std::vector<Assets::FMaterial>& materials,
                             std::vector<Assets::LightObject>& lights,
                             std::vector<Assets::AnimationTrack>& tracks)
    {
        // Camera: slightly elevated, looking at origin
        Assets::Camera defaultCam;
        defaultCam.name = "PlaygroundCam";
        defaultCam.ModelView = lookAt(vec3(0, 3, 10), vec3(0, 1, 0), vec3(0, 1, 0));
        defaultCam.FieldOfView = 60;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 10;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = false;
        cameraInit.SunIntensity = 200.0f;
        cameraInit.SkyIntensity = 50.0f;

        // -- Materials --
        uint32_t matBase = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Mixture(vec3(0.5f, 0.5f, 0.5f), 0.05f), "ground"});         // 0: ground grey
        materials.push_back({Material::Lambertian(vec3(0.85f, 0.35f, 0.25f)), "box_red"});      // 1: red
        materials.push_back({Material::Lambertian(vec3(0.25f, 0.55f, 0.85f)), "box_blue"});     // 2: blue
        materials.push_back({Material::Lambertian(vec3(0.35f, 0.75f, 0.35f)), "box_green"});    // 3: green
        materials.push_back({Material::Lambertian(vec3(0.9f, 0.8f, 0.3f)), "box_yellow"});      // 4: yellow
        materials.push_back({Material::Mixture(vec3(0.9f, 0.9f, 0.9f), 0.05f), "box_shiny"});   // 5: shiny white

        // -- Ground plane: large flat box --
        const float groundHalfSize = 50.0f;
        const float groundThickness = 0.5f;
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-groundHalfSize, -groundThickness, -groundHalfSize),
            vec3(groundHalfSize, 0.0f, groundHalfSize)));
        uint32_t groundModelId = static_cast<uint32_t>(models.size() - 1);

        {
            auto node = Assets::Node::CreateNode("Ground", vec3(0, 0, 0), quat(1, 0, 0, 0), vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(groundModelId);
            rc->SetVisible(true);
            rc->SetMaterial({matBase + 0});
            node->AddComponent(rc);
            nodes.push_back(node);
        }

        // -- Obstacle box models (3 sizes) --
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f, 0, -0.5f), vec3(0.5f, 1.0f, 0.5f)));
        uint32_t smallBoxId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(vec3(-1.0f, 0, -1.0f), vec3(1.0f, 2.0f, 1.0f)));
        uint32_t medBoxId = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(vec3(-1.5f, 0, -1.5f), vec3(1.5f, 3.0f, 1.5f)));
        uint32_t largeBoxId = static_cast<uint32_t>(models.size() - 1);

        // -- A ramp model (wedge-like box) --
        models.push_back(Assets::FProcModel::CreateBox(vec3(-2.0f, 0, -4.0f), vec3(2.0f, 1.5f, 4.0f)));
        uint32_t rampBoxId = static_cast<uint32_t>(models.size() - 1);

        // -- Stair step model --
        models.push_back(Assets::FProcModel::CreateBox(vec3(-2.0f, 0, -0.5f), vec3(2.0f, 0.3f, 0.5f)));
        uint32_t stairStepId = static_cast<uint32_t>(models.size() - 1);

        // -- Random obstacle boxes --
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> posDist(-30.0f, 30.0f);
        std::uniform_int_distribution<int> sizeDist(0, 2);
        std::uniform_int_distribution<int> matDist(1, 5);
        std::uniform_real_distribution<float> yawDist(0.0f, glm::two_pi<float>());

        constexpr int numBoxes = 30;
        for (int i = 0; i < numBoxes; ++i)
        {
            float x = posDist(rng);
            float z = posDist(rng);

            // Keep a clear area around spawn
            if (std::abs(x) < 4.0f && std::abs(z) < 4.0f)
            {
                x += (x >= 0 ? 4.0f : -4.0f);
            }

            int size = sizeDist(rng);
            uint32_t modelId = (size == 0) ? smallBoxId : (size == 1) ? medBoxId : largeBoxId;
            uint32_t matId = matBase + static_cast<uint32_t>(matDist(rng));
            float yaw = yawDist(rng);

            auto node = Assets::Node::CreateNode(
                "Obstacle_" + std::to_string(i),
                vec3(x, 0, z),
                glm::angleAxis(yaw, vec3(0, 1, 0)),
                vec3(1), static_cast<uint32_t>(nodes.size()));

            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelId);
            rc->SetVisible(true);
            rc->SetMaterial({matId});
            node->AddComponent(rc);

            if (size == 0)
            {
                auto phys = std::make_shared<Runtime::PhysicsComponent>();
                phys->SetMobility(Runtime::ENodeMobility::Dynamic);
                phys->SetPhysicsOffset(vec3(0.0f, 0.5f, 0.0f));

                NextBodyID bodyId = NextEngine::GetInstance()->GetPhysicsEngine()->CreateBoxBody(
                    vec3(x, 0.5f, z),
                    glm::angleAxis(yaw, vec3(0, 1, 0)),
                    vec3(1.0f, 1.0f, 1.0f),
                    NextMotionType::Dynamic);
                phys->BindPhysicsBody(bodyId);
                node->AddComponent(phys);
            }

            nodes.push_back(node);
        }

        // -- A ramp for testing slope walking --
        {
            auto node = Assets::Node::CreateNode("Ramp", vec3(5, 0, -5),
                glm::angleAxis(glm::radians(15.0f), vec3(1, 0, 0)),
                vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(rampBoxId);
            rc->SetVisible(true);
            rc->SetMaterial({matBase + 4});
            node->AddComponent(rc);
            nodes.push_back(node);
        }

        // -- Stairs for step-up testing --
        for (int i = 0; i < 6; ++i)
        {
            float y = static_cast<float>(i) * 0.3f;
            float z = -10.0f + static_cast<float>(i) * 1.0f;
            auto node = Assets::Node::CreateNode(
                "Stair_" + std::to_string(i),
                vec3(-5, y, z), quat(1, 0, 0, 0), vec3(1),
                static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(stairStepId);
            rc->SetVisible(true);
            rc->SetMaterial({matBase + 2});
            node->AddComponent(rc);
            nodes.push_back(node);
        }
    }
}

std::vector<std::string> SceneList::AllScenes;

bool SceneList::IsSupportedSceneExtension(std::string_view extension)
{
    if (extension.empty())
    {
        return false;
    }

    const std::string normalized = ToLowerCopy(std::string(extension));
    return std::find(kSupportedSceneExtensions.begin(), kSupportedSceneExtensions.end(), normalized)
        != kSupportedSceneExtensions.end();
}

bool SceneList::IsSupportedScenePath(const std::filesystem::path& path)
{
    return path.has_extension() && IsSupportedSceneExtension(path.extension().string());
}

std::span<const std::string_view> SceneList::SupportedSceneExtensions()
{
    return kSupportedSceneExtensions;
}

void SceneList::ScanScenes()
{
    // add relative path
    std::string modelPath = "assets/models/";
    std::filesystem::path path = Utilities::FileHelper::GetPlatformFilePath(modelPath.c_str());

    // if with models, scan
    if (std::filesystem::exists(path))
    {
        SPDLOG_INFO("Scanning dir: {}", path.string());
        for (const auto& entry : std::filesystem::directory_iterator(path))
        {
            std::filesystem::path filename = entry.path().filename();
            if (!IsSupportedScenePath(entry.path())) continue;
            AllScenes.push_back((modelPath / filename).string());
        }
    }

    // Scan assets/omr/ for .ldr files
    std::string omrPath = "assets/omr/";
    std::filesystem::path omrDir = Utilities::FileHelper::GetPlatformFilePath(omrPath.c_str());
    if (std::filesystem::exists(omrDir))
    {
        for (const auto& entry : std::filesystem::directory_iterator(omrDir))
        {
            if (!IsSupportedScenePath(entry.path())) continue;
            std::filesystem::path filename = entry.path().filename();
            AllScenes.push_back((omrPath / filename).string());
        }
    }

    // sort the scene
    std::sort(AllScenes.begin(), AllScenes.end());

    AllScenes.insert(AllScenes.begin(), "CharacterPlayground.proc");
    AllScenes.insert(AllScenes.begin(), "RTIO.proc");
    AllScenes.insert(AllScenes.begin(), "CornellBox.proc");

    SPDLOG_INFO("Scene found: {}", AllScenes.size());
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
                          std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks,
                          std::vector<Assets::Skeleton>& skeletons)
{
    std::filesystem::path filepath = filename;
    std::string ext = ToLowerCopy(filepath.extension().string());
    materials.push_back({Material::Lambertian(vec3(0.73f, 0.73f, 0.73f)), "root_default"});
    if (ext == ".glb" || ext == ".gltf")
    {
        return Assets::FSceneLoader::LoadGLTFScene(filename, camera, nodes, models, materials, lights, tracks, skeletons);
    }
    if (ext == ".ldr" || ext == ".mpd")
    {
        Assets::LDrawLoadOptions ldrawOptions;
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            ldrawOptions.lduToWorldScale = engine->GetUserSettings().LDrawLduToWorldScale;
        }

        return Assets::FLDrawLoader::LoadLDrawScene(
            filename,
            camera,
            nodes,
            models,
            materials,
            lights,
            tracks,
            skeletons,
            ldrawOptions);
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
        if (filename == "CharacterPlayground.proc")
        {
            CharacterPlayground(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        return false;
    }

    return false;
}
