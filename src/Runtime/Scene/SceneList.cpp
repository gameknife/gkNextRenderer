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
#include "Assets/Loaders/KayKitPieceLoader.h"
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
    enum class ESceneCategory : uint8_t
    {
        Procedural = 0,
        Gltf = 1,
        LDraw = 2,
        Other = 3,
    };

    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    ESceneCategory GetSceneCategory(std::string_view scenePath)
    {
        const std::string extension = ToLowerCopy(std::filesystem::path(scenePath).extension().string());
        if (extension == ".proc")
        {
            return ESceneCategory::Procedural;
        }
        if (extension == ".glb" || extension == ".gltf")
        {
            return ESceneCategory::Gltf;
        }
        if (extension == ".ldr" || extension == ".mpd")
        {
            return ESceneCategory::LDraw;
        }
        return ESceneCategory::Other;
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

    void GIBootcamp(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<Assets::LightObject>& lights,
                    std::vector<Assets::AnimationTrack>& tracks)
    {
        // Camera: outside, slightly elevated, looking into the open front of the room.
        Assets::Camera defaultCam;
        defaultCam.name = "Cam";
        defaultCam.ModelView = lookAt(vec3(0, 3.5f, 14.0f), vec3(0, 2.0f, -1.0f), vec3(0, 1, 0));
        defaultCam.FieldOfView = 55;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 14;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SunIntensity = 1000.0f;
        cameraInit.SkyIntensity = 50.0f;

        const uint32_t matGround   = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.40f, 0.42f, 0.44f)), "gb_ground"});
        const uint32_t matWhite    = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.78f, 0.78f, 0.78f)), "gb_white"});
        const uint32_t matRed      = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.72f, 0.12f, 0.12f)), "gb_red"});
        const uint32_t matGreen    = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.14f, 0.55f, 0.18f)), "gb_green"});
        const uint32_t matBlue     = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.14f, 0.28f, 0.68f)), "gb_blue"});
        const uint32_t matYellow   = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.85f, 0.72f, 0.18f)), "gb_yellow"});
        const uint32_t matMetal    = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Metallic(vec3(0.90f, 0.90f, 0.92f), 0.02f), "gb_metal"});
        const uint32_t matGlossy   = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Mixture(vec3(0.92f, 0.52f, 0.22f), 0.18f), "gb_glossy"});

        const uint32_t matLightWarm  = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(800.0f, 750.0f, 680.0f)), "gb_light_warm"});
        const uint32_t matLightRed   = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(1500.0f, 150.0f, 60.0f)), "gb_light_red"});
        const uint32_t matLightBlue  = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(60.0f, 250.0f, 1500.0f)), "gb_light_blue"});
        const uint32_t matLightGreen = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(80.0f, 1100.0f, 200.0f)), "gb_light_green"});

        auto addNodeRot = [&](const std::string& name, const vec3& pos, const quat& rot,
                              uint32_t modelIdx, uint32_t matIdx)
        {
            auto node = Assets::Node::CreateNode(name, pos, rot, vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelIdx);
            rc->SetVisible(true);
            rc->SetMaterial({matIdx});
            node->AddComponent(rc);
            nodes.push_back(node);
        };
        auto addNode = [&](const std::string& name, const vec3& pos, uint32_t modelIdx, uint32_t matIdx)
        {
            addNodeRot(name, pos, quat(1, 0, 0, 0), modelIdx, matIdx);
        };

        // Outdoor ground (thin slab; sits below y=0 so room walls land on top).
        models.push_back(Assets::FProcModel::CreateBox(vec3(-50.0f, -0.1f, -50.0f), vec3(50.0f, 0.0f, 50.0f)));
        addNode("Ground", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGround);

        // Room interior: x in [-6, 6], z in [-6, 0], y in [0, 6]. Front (z=0) is open.
        const float rxMin = -6.0f, rxMax = 6.0f;
        const float rzMin = -6.0f, rzMax = 0.0f;
        const float ryTop = 6.0f;
        const float t = 0.2f;

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(rxMin - t, 0.0f, rzMin - t),
            vec3(rxMax + t, ryTop, rzMin)));
        addNode("Wall_Back", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matRed);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(rxMin - t, 0.0f, rzMin),
            vec3(rxMin,     ryTop, rzMax + t)));
        addNode("Wall_Left", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGreen);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(rxMax,     0.0f, rzMin),
            vec3(rxMax + t, ryTop, rzMax + t)));
        addNode("Wall_Right", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matBlue);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(rxMin - t, ryTop,     rzMin - t),
            vec3(rxMax + t, ryTop + t, rzMax + t)));
        addNode("Ceiling", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matWhite);

        // Front-top overhang frames the opening so interior lights don't leak straight out.
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(rxMin, 4.6f, rzMax),
            vec3(rxMax, ryTop, rzMax + t)));
        addNode("Wall_FrontTop", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matYellow);

        auto addAreaLight = [&](const std::string& name, const vec3& origin,
                                const vec3& right, const vec3& up, uint32_t lightMat)
        {
            models.push_back(Assets::FProcModel::CreateAreaLight(name, origin, right, up, lightMat, lights));
            addNode(name, vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), lightMat);
        };

        // Main ceiling light (warm white), faces -Y.
        addAreaLight("Light_CeilingWarm",
                     vec3(-2.0f, ryTop - 0.02f, -4.5f),
                     vec3(4.0f, 0.0f, 0.0f),
                     vec3(0.0f, 0.0f, 3.0f),
                     matLightWarm);

        // Red accent on left wall, faces +X into the room.
        addAreaLight("Light_AccentRed",
                     vec3(rxMin + 0.02f, 0.8f, -5.0f),
                     vec3(0.0f, 1.2f, 0.0f),
                     vec3(0.0f, 0.0f, 1.2f),
                     matLightRed);

        // Blue accent on back wall, faces +Z into the room.
        addAreaLight("Light_AccentBlue",
                     vec3(2.5f, 3.8f, rzMin + 0.02f),
                     vec3(2.0f, 0.0f, 0.0f),
                     vec3(0.0f, 1.2f, 0.0f),
                     matLightBlue);

        // Green outdoor lantern, faces +Y (up). Sits on a small pedestal.
        addAreaLight("Light_OutdoorGreen",
                     vec3(8.0f, 0.45f, 5.0f),
                     vec3(2.0f, 0.0f, 0.0f),
                     vec3(0.0f, 0.0f, -2.0f),
                     matLightGreen);

        // Shared meshes for props.
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 0.8f));
        const uint32_t mdlSphere = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.6f, 0.0f, -0.6f), vec3(0.6f, 1.2f, 0.6f)));
        const uint32_t mdlCube = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.4f, 0.0f, -0.4f), vec3(0.4f, 0.8f, 0.4f)));
        const uint32_t mdlCubeSmall = static_cast<uint32_t>(models.size() - 1);

        // Pedestal under the green lantern.
        models.push_back(Assets::FProcModel::CreateBox(vec3(8.0f, 0.0f, 3.0f), vec3(10.0f, 0.45f, 5.0f)));
        addNode("Pedestal_Green", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matWhite);

        // Indoor props (GI bounce tests: metal reflects coloured walls, glossy picks up warm ceiling).
        addNode("In_Sphere_Metal",  vec3(-3.0f, 0.8f, -2.0f), mdlSphere,    matMetal);
        addNode("In_Sphere_Glossy", vec3( 2.5f, 0.8f, -4.5f), mdlSphere,    matGlossy);
        addNode("In_Cube_White",    vec3(-4.0f, 0.0f, -5.0f), mdlCube,      matWhite);
        addNode("In_Cube_Yellow",   vec3( 3.5f, 0.0f, -1.5f), mdlCubeSmall, matYellow);

        // Outdoor props (sun/sky GI under open sky).
        addNode("Out_Sphere_Metal",  vec3( 5.0f, 1.0f,  8.0f), mdlSphere,    matMetal);
        addNode("Out_Sphere_Glossy", vec3(-4.0f, 1.0f,  8.5f), mdlSphere,    matGlossy);
        addNode("Out_Sphere_Yellow", vec3(-2.0f, 0.8f,  5.5f), mdlSphere,    matYellow);

        // --- East half-open room (floor uses ground; no front wall facing main room) ---
        // Interior: x in [12, 20], z in [-6, 2], y in [0, 4]. Opens toward -X and +Z.
        const float exMin = 12.0f, exMax = 20.0f;
        const float ezMin = -6.0f, ezMax = 2.0f;
        const float eyTop = 4.0f;

        // Back wall (x = exMax), green
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(exMax,     0.0f, ezMin - t),
            vec3(exMax + t, eyTop, ezMax + t)));
        addNode("EastRoom_Back", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGreen);

        // Side wall (z = ezMin), blue
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(exMin - t, 0.0f, ezMin - t),
            vec3(exMax,     eyTop, ezMin)));
        addNode("EastRoom_Side", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matBlue);

        // Short partial front pier (x ~ exMin) to suggest a missing wall
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(exMin - t, 0.0f, ezMin),
            vec3(exMin,     eyTop, ezMin + 1.5f)));
        addNode("EastRoom_FrontPier", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matYellow);

        // Roof
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(exMin - t, eyTop,     ezMin - t),
            vec3(exMax + t, eyTop + t, ezMax + t)));
        addNode("EastRoom_Roof", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matWhite);

        // --- West ruin wall (standalone wall with a window opening) ---
        // Freestanding piece at x ~ -14, facing east. Two vertical segments + horizontal lintel.
        const float wxCenter = -14.0f;
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(wxCenter - t, 0.0f, -5.0f),
            vec3(wxCenter + t, 4.5f, -2.5f)));
        addNode("WestRuin_LeftPier", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matRed);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(wxCenter - t, 0.0f, 1.0f),
            vec3(wxCenter + t, 4.5f, 3.5f)));
        addNode("WestRuin_RightPier", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matRed);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(wxCenter - t, 3.2f, -2.5f),
            vec3(wxCenter + t, 4.5f, 1.0f)));
        addNode("WestRuin_Lintel", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matYellow);

        // --- South courtyard with four corner pillars (open-air, no roof) ---
        const vec3 pillarCenters[4] = {
            vec3(-4.0f, 0.0f, 10.0f),
            vec3( 4.0f, 0.0f, 10.0f),
            vec3(-4.0f, 0.0f, 16.0f),
            vec3( 4.0f, 0.0f, 16.0f),
        };
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.3f, 0.0f, -0.3f), vec3(0.3f, 4.0f, 0.3f)));
        const uint32_t mdlPillar = static_cast<uint32_t>(models.size() - 1);
        for (int i = 0; i < 4; ++i)
        {
            addNode("Courtyard_Pillar_" + std::to_string(i), pillarCenters[i], mdlPillar, matWhite);
        }
        // Cross-beam connecting the two back pillars
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-4.3f, 3.7f, 15.7f),
            vec3( 4.3f, 4.0f, 16.3f)));
        addNode("Courtyard_Beam", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matYellow);

        // --- Large freestanding solid walls (full panels, no openings) ---
        // These are colourful reflectors placed out in the open, so sun/sky GI bounces
        // off them and tints nearby props — useful for validating indirect shading.
        struct SolidWall { vec3 p0; vec3 p1; uint32_t matId; const char* name; };
        const SolidWall solidWalls[] = {
            // Long red wall along north edge (z = -14), full height.
            {vec3(-10.0f, 0.0f, -14.2f), vec3( 10.0f, 5.0f, -14.0f), matRed,    "SolidWall_North_Red"},
            // Long green wall along east edge (x = 26), full height.
            {vec3( 26.0f, 0.0f, -12.0f), vec3( 26.2f, 5.0f,  12.0f), matGreen,  "SolidWall_East_Green"},
            // Long blue wall along west edge (x = -26), full height.
            {vec3(-26.2f, 0.0f, -12.0f), vec3(-26.0f, 5.0f,  12.0f), matBlue,   "SolidWall_West_Blue"},
            // Wide yellow backdrop to the south (z = 24).
            {vec3(-12.0f, 0.0f, 23.8f),  vec3( 12.0f, 4.5f, 24.0f),  matYellow, "SolidWall_South_Yellow"},
            // Tall white divider between main room and east room (x = 10, partial).
            {vec3(  9.9f, 0.0f,  4.0f),  vec3( 10.1f, 5.0f, 11.0f),  matWhite,  "SolidWall_Divider_White"},
        };
        for (const auto& w : solidWalls)
        {
            models.push_back(Assets::FProcModel::CreateBox(w.p0, w.p1));
            addNode(w.name, vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), w.matId);
        }

        // --- Scattered coloured cubes with Y-axis rotations ---
        struct CubeSpec { vec3 pos; float yawRad; uint32_t modelId; uint32_t matId; const char* name; };
        const CubeSpec cubeSpecs[] = {
            // Cluster near main-room entrance
            {vec3( 2.0f, 0.0f,  6.5f),  0.35f, mdlCube,      matRed,    "Out_Cube_Red_A"},
            {vec3(-7.0f, 0.0f,  4.0f), -0.55f, mdlCube,      matGreen,  "Out_Cube_Green_A"},
            {vec3( 8.0f, 0.0f, -2.0f),  0.80f, mdlCube,      matBlue,   "Out_Cube_Blue_A"},
            {vec3(-9.0f, 0.0f, -3.0f), -1.10f, mdlCubeSmall, matWhite,  "Out_Cube_White_A"},
            // Around east room
            {vec3(14.5f, 0.0f, -1.5f),  0.25f, mdlCube,      matYellow, "East_Cube_Yellow"},
            {vec3(17.0f, 0.0f, -4.0f), -0.70f, mdlCubeSmall, matRed,    "East_Cube_Red"},
            {vec3(13.0f, 0.0f,  0.5f),  1.20f, mdlCubeSmall, matBlue,   "East_Cube_Blue"},
            {vec3(16.0f, 0.0f,  0.0f), -0.20f, mdlCube,      matGreen,  "East_Cube_Green"},
            // Between east room and main room
            {vec3( 9.5f, 0.0f,  3.0f),  0.60f, mdlCubeSmall, matRed,    "Between_Cube_Red"},
            {vec3(10.5f, 0.0f,  1.0f), -0.45f, mdlCube,      matYellow, "Between_Cube_Yellow"},
            // Near west ruin
            {vec3(-16.0f, 0.0f, -6.0f),  0.90f, mdlCubeSmall, matGreen, "West_Cube_Green"},
            {vec3(-12.0f, 0.0f, -5.0f), -0.30f, mdlCube,      matBlue,  "West_Cube_Blue"},
            {vec3(-13.0f, 0.0f,  5.0f),  1.40f, mdlCubeSmall, matRed,   "West_Cube_Red"},
            {vec3(-17.0f, 0.0f,  0.5f), -0.75f, mdlCube,      matYellow,"West_Cube_Yellow"},
            // Scatter near south courtyard
            {vec3(-6.5f, 0.0f, 12.0f),  0.50f, mdlCubeSmall, matBlue,   "South_Cube_Blue_A"},
            {vec3( 6.5f, 0.0f, 12.5f), -0.85f, mdlCube,      matRed,    "South_Cube_Red_A"},
            {vec3( 0.0f, 0.0f, 18.5f),  0.20f, mdlCubeSmall, matYellow, "South_Cube_Yellow"},
            {vec3(-2.5f, 0.0f, 20.0f), -1.25f, mdlCube,      matGreen,  "South_Cube_Green"},
            // Far outdoor corners for open-sky GI
            {vec3( 18.0f, 0.0f,  14.0f),  0.95f, mdlCube,      matBlue,   "Far_Cube_Blue"},
            {vec3(-20.0f, 0.0f,  12.0f), -0.40f, mdlCubeSmall, matRed,    "Far_Cube_Red"},
            {vec3( 22.0f, 0.0f,  -8.0f),  1.10f, mdlCubeSmall, matYellow, "Far_Cube_Yellow"},
            {vec3(-22.0f, 0.0f,  -8.0f), -0.65f, mdlCube,      matGreen,  "Far_Cube_Green"},
            // Extra stacked / tilted cubes against the new solid walls
            {vec3(-8.0f, 0.0f, -12.5f),  0.30f, mdlCube,      matYellow, "Near_NorthWall_Yellow"},
            {vec3( 6.0f, 0.0f, -12.8f), -0.55f, mdlCubeSmall, matBlue,   "Near_NorthWall_Blue"},
            {vec3(24.0f, 0.0f,  -4.0f),  0.70f, mdlCubeSmall, matRed,    "Near_EastWall_Red"},
            {vec3(24.5f, 0.0f,   6.0f), -0.90f, mdlCube,      matWhite,  "Near_EastWall_White"},
            {vec3(-24.0f, 0.0f,  2.0f),  0.45f, mdlCube,      matYellow, "Near_WestWall_Yellow"},
            {vec3(-24.5f, 0.0f, -4.5f), -1.05f, mdlCubeSmall, matGreen,  "Near_WestWall_Green"},
            {vec3( 4.5f, 0.0f,  22.5f),  0.20f, mdlCubeSmall, matRed,    "Near_SouthWall_Red"},
            {vec3(-5.5f, 0.0f,  22.8f), -0.35f, mdlCube,      matBlue,   "Near_SouthWall_Blue"},
        };
        for (const auto& spec : cubeSpecs)
        {
            addNodeRot(spec.name, spec.pos, quat(vec3(0.0f, spec.yawRad, 0.0f)),
                       spec.modelId, spec.matId);
        }
    }

    void MaterialShowcase(Assets::EnvironmentSetting& cameraInit,
                          std::vector<std::shared_ptr<Assets::Node>>& nodes,
                          std::vector<Assets::Model>& models,
                          std::vector<Assets::FMaterial>& materials,
                          std::vector<Assets::LightObject>& lights,
                          std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "MaterialShowcaseCam";
        defaultCam.ModelView = lookAt(vec3(0.0f, 5.0f, 12.0f), vec3(0.0f, 1.2f, 0.0f), vec3(0, 1, 0));
        defaultCam.FieldOfView = 45;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 12;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 4.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.SkyIdx = 0;
        cameraInit.SkyIntensity = 15.0f;
        cameraInit.HasSun = false;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.45f, 0.45f, 0.46f)), "ms_ground_lambertian"});
        const uint32_t matLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(900.0f, 850.0f, 760.0f)), "ms_key_light"});

        struct MaterialRow
        {
            const char* name;
            uint32_t materials[3];
        };

        MaterialRow rows[] = {
            {"Lambertian",
             {static_cast<uint32_t>(materials.size()),
              static_cast<uint32_t>(materials.size() + 1),
              static_cast<uint32_t>(materials.size() + 2)}},
            {"Metallic",
             {static_cast<uint32_t>(materials.size() + 3),
              static_cast<uint32_t>(materials.size() + 4),
              static_cast<uint32_t>(materials.size() + 5)}},
            {"Mixture",
             {static_cast<uint32_t>(materials.size() + 6),
              static_cast<uint32_t>(materials.size() + 7),
              static_cast<uint32_t>(materials.size() + 8)}},
            {"Dielectric",
             {static_cast<uint32_t>(materials.size() + 9),
              static_cast<uint32_t>(materials.size() + 10),
              static_cast<uint32_t>(materials.size() + 11)}},
            {"DiffuseLight",
             {static_cast<uint32_t>(materials.size() + 12),
              static_cast<uint32_t>(materials.size() + 13),
              static_cast<uint32_t>(materials.size() + 14)}},
        };

        const float roughness[] = {0.0f, 0.3f, 0.8f};
        for (int i = 0; i < 3; ++i)
        {
            materials.push_back({Material::Lambertian(vec3(0.70f, 0.36f + roughness[i] * 0.35f, 0.24f)),
                                 fmt::format("ms_lambertian_{:.1f}", roughness[i])});
        }
        for (int i = 0; i < 3; ++i)
        {
            materials.push_back({Material::Metallic(vec3(0.86f, 0.82f, 0.74f), roughness[i]),
                                 fmt::format("ms_metallic_{:.1f}", roughness[i])});
        }
        for (int i = 0; i < 3; ++i)
        {
            materials.push_back({Material::Mixture(vec3(0.25f, 0.48f, 0.85f), roughness[i]),
                                 fmt::format("ms_mixture_{:.1f}", roughness[i])});
        }
        for (int i = 0; i < 3; ++i)
        {
            materials.push_back({Material::Dielectric(1.5f, roughness[i]),
                                 fmt::format("ms_dielectric_{:.1f}", roughness[i])});
        }
        for (int i = 0; i < 3; ++i)
        {
            const float intensity = 280.0f + roughness[i] * 520.0f;
            materials.push_back({Material::DiffuseLight(vec3(intensity, intensity * 0.85f, intensity * 0.55f)),
                                 fmt::format("ms_diffuse_light_{:.1f}", roughness[i])});
        }

        auto addNode = [&](const std::string& name, const vec3& pos, uint32_t modelIdx, uint32_t matIdx)
        {
            auto node = Assets::Node::CreateNode(name, pos, quat(1, 0, 0, 0), vec3(1),
                                                 static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelIdx);
            rc->SetVisible(true);
            rc->SetMaterial({matIdx});
            node->AddComponent(rc);
            nodes.push_back(node);
        };

        models.push_back(Assets::FProcModel::CreateBox(vec3(-7.5f, -0.08f, -5.0f), vec3(7.5f, 0.0f, 5.0f)));
        addNode("Ground", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGround);

        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 0.75f));
        const uint32_t sphereModel = static_cast<uint32_t>(models.size() - 1);

        for (int row = 0; row < 5; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                const vec3 pos(-2.8f + static_cast<float>(col) * 2.8f, 0.75f,
                               -3.2f + static_cast<float>(row) * 1.6f);
                addNode(fmt::format("MaterialShowcase_{}_R{}", rows[row].name, col),
                        pos, sphereModel, rows[row].materials[col]);
            }
        }

        models.push_back(Assets::FProcModel::CreateAreaLight(
            "KeyLight", vec3(-3.0f, 5.5f, 1.5f), vec3(6.0f, 0.0f, 0.0f),
            vec3(0.0f, 0.0f, -3.0f), matLight, lights));
        addNode("KeyLight", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matLight);
    }

    void LightingShowcase(Assets::EnvironmentSetting& cameraInit,
                         std::vector<std::shared_ptr<Assets::Node>>& nodes,
                         std::vector<Assets::Model>& models,
                         std::vector<Assets::FMaterial>& materials,
                         std::vector<Assets::LightObject>& lights,
                         std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "LightingShowcaseCam";
        defaultCam.ModelView = lookAt(vec3(0.0f, 4.5f, 14.0f), vec3(0.0f, 1.2f, 0.0f), vec3(0, 1, 0));
        defaultCam.FieldOfView = 50;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 14;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.SkyIdx = 0;
        cameraInit.SkyIntensity = 10.0f;
        cameraInit.HasSun = true;
        cameraInit.SunIntensity = 300.0f;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.45f, 0.45f, 0.46f)), "ls_ground_gray"});
        const uint32_t matSphere = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.72f, 0.72f, 0.72f)), "ls_sphere_neutral"});

        const uint32_t matPointLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(1000.0f, 900.0f, 800.0f)), "ls_point_light"});
        const uint32_t matAreaLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(700.0f, 800.0f, 900.0f)), "ls_area_light"});
        const uint32_t matFillLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(150.0f, 180.0f, 200.0f)), "ls_fill_light"});

        auto addNode = [&](const std::string& name, const vec3& pos, uint32_t modelIdx, uint32_t matIdx)
        {
            auto node = Assets::Node::CreateNode(name, pos, quat(1, 0, 0, 0), vec3(1),
                                                 static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelIdx);
            rc->SetVisible(true);
            rc->SetMaterial({matIdx});
            node->AddComponent(rc);
            nodes.push_back(node);
        };

        // Ground
        models.push_back(Assets::FProcModel::CreateBox(vec3(-10.0f, -0.08f, -6.0f), vec3(10.0f, 0.0f, 6.0f)));
        addNode("Ground", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGround);

        // Back wall
        models.push_back(Assets::FProcModel::CreateBox(vec3(-10.0f, 0.0f, -6.0f), vec3(10.0f, 6.0f, -5.8f)));
        addNode("Wall", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGround);

        // 4 neutral gray spheres as light receivers
        const float sphereRadius = 0.7f;
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), sphereRadius));
        const uint32_t sphereModel = static_cast<uint32_t>(models.size() - 1);

        const float spacing = 2.5f * sphereRadius * 2.0f;
        const vec3 spherePositions[] = {
            vec3(-5.25f, sphereRadius, -1.0f),
            vec3(-1.75f, sphereRadius, -1.0f),
            vec3( 1.75f, sphereRadius, -1.0f),
            vec3( 5.25f, sphereRadius, -1.0f),
        };
        for (int i = 0; i < 4; ++i)
        {
            addNode(fmt::format("Sphere{}", i + 1), spherePositions[i], sphereModel, matSphere);
        }

        // Light 1: Point light (small sphere emitter above sphere 1)
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 0.2f));
        const uint32_t pointLightModel = static_cast<uint32_t>(models.size() - 1);
        addNode("PointLight", vec3(-5.25f, 4.5f, -1.0f), pointLightModel, matPointLight);

        // Light 2: Area light (rectangle above sphere 2)
        models.push_back(Assets::FProcModel::CreateAreaLight(
            "AreaLight", vec3(-2.75f, 4.5f, -0.5f), vec3(2.0f, 0.0f, 0.0f),
            vec3(0.0f, 0.0f, -1.0f), matAreaLight, lights));
        addNode("AreaLight", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matAreaLight);

        // Light 3: Uses global sun (directional) - no explicit light node needed
        // Light 4: Fill light (small area light near sphere 4)
        models.push_back(Assets::FProcModel::CreateAreaLight(
            "FillLight", vec3(4.75f, 2.8f, 1.0f), vec3(1.0f, 0.0f, 0.0f),
            vec3(0.0f, 0.5f, -0.5f), matFillLight, lights));
        addNode("FillLight", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matFillLight);
    }

    void CameraShowcase(Assets::EnvironmentSetting& cameraInit,
                       std::vector<std::shared_ptr<Assets::Node>>& nodes,
                       std::vector<Assets::Model>& models,
                       std::vector<Assets::FMaterial>& materials,
                       std::vector<Assets::LightObject>& lights,
                       std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "CameraShowcaseCam";
        defaultCam.ModelView = lookAt(vec3(0.0f, 3.0f, 16.0f), vec3(0.0f, 1.5f, 0.0f), vec3(0, 1, 0));
        defaultCam.FieldOfView = 50;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 16;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.SkyIdx = 0;
        cameraInit.SkyIntensity = 10.0f;
        cameraInit.HasSun = true;
        cameraInit.SunIntensity = 400.0f;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.50f, 0.50f, 0.50f)), "cs_ground"});
        const uint32_t matWhite = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.78f, 0.78f, 0.78f)), "cs_white"});
        const uint32_t matDark = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.25f, 0.25f, 0.25f)), "cs_dark"});
        const uint32_t matRed = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.72f, 0.15f, 0.15f)), "cs_red"});
        const uint32_t matBlue = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.15f, 0.30f, 0.72f)), "cs_blue"});
        const uint32_t matGreen = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.15f, 0.55f, 0.20f)), "cs_green"});

        auto addNode = [&](const std::string& name, const vec3& pos, uint32_t modelIdx, uint32_t matIdx)
        {
            auto node = Assets::Node::CreateNode(name, pos, quat(1, 0, 0, 0), vec3(1),
                                                 static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelIdx);
            rc->SetVisible(true);
            rc->SetMaterial({matIdx});
            node->AddComponent(rc);
            nodes.push_back(node);
        };

        // Checkerboard ground: 8x8 grid of alternating white/dark squares
        const float tileSize = 1.5f;
        const float groundHalfExtent = 6.0f;
        for (int row = -4; row < 4; ++row)
        {
            for (int col = -4; col < 4; ++col)
            {
                const bool isWhite = (row + col) % 2 == 0;
                const uint32_t mat = isWhite ? matWhite : matDark;
                const float x0 = static_cast<float>(col) * tileSize;
                const float z0 = static_cast<float>(row) * tileSize;
                models.push_back(Assets::FProcModel::CreateBox(
                    vec3(x0, -0.05f, z0),
                    vec3(x0 + tileSize, 0.0f, z0 + tileSize)));
                addNode(fmt::format("Grid_{}_{}", row + 4, col + 4),
                        vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), mat);
            }
        }

        // 5 cubes in a row, receding into the distance
        struct CubeSpec { vec3 pos; vec3 size; uint32_t mat; const char* name; };
        const CubeSpec cubes[] = {
            {vec3(-4.0f, 0.5f, 5.0f),  vec3(1.0f), matRed,   "Cube_Near"},
            {vec3(-2.0f, 0.6f, 3.0f),  vec3(1.2f), matBlue,  "Cube_NearMid"},
            {vec3( 0.0f, 0.7f, 0.0f),  vec3(1.4f), matGreen, "Cube_Mid"},
            {vec3( 2.0f, 0.8f, -3.0f), vec3(1.6f), matWhite, "Cube_FarMid"},
            {vec3( 4.0f, 0.9f, -6.0f), vec3(1.8f), matRed,   "Cube_Far"},
        };
        for (const auto& cube : cubes)
        {
            const vec3 halfSize = cube.size * 0.5f;
            models.push_back(Assets::FProcModel::CreateBox(
                cube.pos - halfSize, cube.pos + halfSize));
            addNode(cube.name, vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), cube.mat);
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

        // -- Materials for procedural elements (ground, walls) --
        uint32_t matBase = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Mixture(vec3(0.5f, 0.5f, 0.5f), 0.5f), "ground"});
        materials.push_back({Material::Mixture(vec3(0.9f, 0.9f, 0.9f), 0.05f), "wall_shiny"});

        // -- Load KayKit pieces --
        Assets::KayKitPieceLoader loader;

        // Platforms (green - main color)
        int platSmall   = loader.LoadPiece("platform_1x1x1", "green", models, materials);
        int platMed     = loader.LoadPiece("platform_2x2x1", "green", models, materials);
        int platLarge   = loader.LoadPiece("platform_4x4x1", "green", models, materials);
        int platHuge    = loader.LoadPiece("platform_6x6x1", "green", models, materials);
        int platTall    = loader.LoadPiece("platform_4x4x2", "green", models, materials);
        int platMedTall = loader.LoadPiece("platform_2x2x2", "green", models, materials);
        int platWide    = loader.LoadPiece("platform_4x2x1", "green", models, materials);
        int platLong    = loader.LoadPiece("platform_6x2x1", "green", models, materials);

        // Slopes
        int slopeLarge  = loader.LoadPiece("platform_slope_4x4x4", "green", models, materials);
        int slopeSmall  = loader.LoadPiece("platform_slope_2x2x2", "green", models, materials);
        int slopeBlueLarge = loader.LoadPiece("platform_slope_4x4x4", "blue", models, materials);
        int slopeBlueSmall = loader.LoadPiece("platform_slope_2x2x2", "blue", models, materials);
        int slopeBlueMediumNarrow = loader.LoadPiece("platform_slope_4x6x4", "blue", models, materials);
        int slopeBlueNarrow = loader.LoadPiece("platform_slope_2x6x4", "blue", models, materials);
        int slopeBlueWide = loader.LoadPiece("platform_slope_6x4x4", "blue", models, materials);
        int slopeBlueHuge = loader.LoadPiece("platform_slope_6x6x4", "blue", models, materials);

        // Barriers and pillars
        int barrierTall = loader.LoadPiece("barrier_4x1x4", "green", models, materials);
        int barrierLow  = loader.LoadPiece("barrier_2x1x2", "green", models, materials);
        int pillarTall  = loader.LoadPiece("pillar_1x1x4", "neutral", models, materials);
        int pillarShort = loader.LoadPiece("pillar_2x2x2", "neutral", models, materials);

        // Accent color platforms
        int platBlueMed = loader.LoadPiece("platform_2x2x1", "blue", models, materials);
        int platBlueMedTall = loader.LoadPiece("platform_2x2x4", "blue", models, materials);
        int platBlue    = loader.LoadPiece("platform_4x4x1", "blue", models, materials);
        int platBlueTall = loader.LoadPiece("platform_4x2x4", "blue", models, materials);
        int platBlueWide = loader.LoadPiece("platform_4x2x1", "blue", models, materials);
        int platBlueLong = loader.LoadPiece("platform_6x2x1", "blue", models, materials);
        int platBlueHuge = loader.LoadPiece("platform_6x6x1", "blue", models, materials);
        int platBlueHugeTall = loader.LoadPiece("platform_6x6x2", "blue", models, materials);
        int platBlueTallLong = loader.LoadPiece("platform_6x2x4", "blue", models, materials);
        int platRed     = loader.LoadPiece("platform_4x4x1", "red", models, materials);
        int platYellow  = loader.LoadPiece("platform_2x2x1", "yellow", models, materials);
        int platHoleBlue = loader.LoadPiece("platform_hole_6x6x1", "blue", models, materials);
        int platDecoBlue = loader.LoadPiece("platform_decorative_2x2x2", "blue", models, materials);
        int platDecoYellow = loader.LoadPiece("platform_decorative_2x2x2", "yellow", models, materials);
        int woodPlatform = loader.LoadPiece("platform_wood_1x1x1", "neutral", models, materials);

        // Decorations
        int flagA       = loader.LoadPiece("flag_A", "green", models, materials);
        int flagB       = loader.LoadPiece("flag_B", "yellow", models, materials);
        int flagC       = loader.LoadPiece("flag_C", "blue", models, materials);
        int cone        = loader.LoadPiece("cone", "red", models, materials);
        int diamond     = loader.LoadPiece("diamond", "yellow", models, materials);
        int star        = loader.LoadPiece("star", "blue", models, materials);
        int heart       = loader.LoadPiece("heart", "red", models, materials);
        int power       = loader.LoadPiece("power", "blue", models, materials);
        int ballBlue    = loader.LoadPiece("ball", "blue", models, materials);
        int bombABlue   = loader.LoadPiece("bomb_A", "blue", models, materials);
        int bombBBlue   = loader.LoadPiece("bomb_B", "blue", models, materials);
        int archGreen   = loader.LoadPiece("arch", "green", models, materials);
        int archWide    = loader.LoadPiece("arch_wide", "green", models, materials);
        int spring      = loader.LoadPiece("spring", "neutral", models, materials);
        int springPad   = loader.LoadPiece("spring_pad", "green", models, materials);
        int hoop        = loader.LoadPiece("hoop", "red", models, materials);
        int hoopAngled  = loader.LoadPiece("hoop_angled", "red", models, materials);
        int buttonBase  = loader.LoadPiece("button_base", "yellow", models, materials);
        int buttonBaseBlue = loader.LoadPiece("button_base", "blue", models, materials);
        int leverFloorBlue = loader.LoadPiece("lever_floor_base", "blue", models, materials);
        int arrowStand  = loader.LoadPiece("signage_arrow_stand", "green", models, materials);
        int arrowsLeft  = loader.LoadPiece("signage_arrows_left", "green", models, materials);
        int arrowsRight = loader.LoadPiece("signage_arrows_right", "green", models, materials);
        int finishSign  = loader.LoadPiece("signage_finish_wide", "neutral", models, materials);
        int railingStraight = loader.LoadPiece("railing_straight_single", "yellow", models, materials);
        int railingCorner = loader.LoadPiece("railing_corner_single", "yellow", models, materials);
        int braceLarge  = loader.LoadPiece("bracing_large", "green", models, materials);
        int braceMedium = loader.LoadPiece("bracing_medium", "green", models, materials);
        int pillarMini  = loader.LoadPiece("pillar_1x1x2", "neutral", models, materials);
        int pillarMega  = loader.LoadPiece("pillar_1x1x8", "neutral", models, materials);
        int pillarWide  = loader.LoadPiece("pillar_2x2x4", "neutral", models, materials);
        int floorWoodSquare = loader.LoadPiece("floor_wood_2x2", "neutral", models, materials);
        int floorWoodLong   = loader.LoadPiece("floor_wood_2x6", "neutral", models, materials);
        int structureA  = loader.LoadPiece("structure_A", "neutral", models, materials);
        int structureB  = loader.LoadPiece("structure_B", "neutral", models, materials);
        int structureC  = loader.LoadPiece("structure_C", "neutral", models, materials);
        int strutHorizontal = loader.LoadPiece("strut_horizontal", "neutral", models, materials);
        int strutVertical = loader.LoadPiece("strut_vertical", "neutral", models, materials);
        int pipeTurn    = loader.LoadPiece("pipe_180_A", "red", models, materials);
        int pipeElbow   = loader.LoadPiece("pipe_90_A", "red", models, materials);
        int pipeStraightBlue = loader.LoadPiece("pipe_straight_A", "blue", models, materials);
        int pipeStraightYellow = loader.LoadPiece("pipe_straight_A", "yellow", models, materials);
        int pipeStraightGreen = loader.LoadPiece("pipe_straight_A", "green", models, materials);
        int pipeEndGreen = loader.LoadPiece("pipe_end", "green", models, materials);

        // -- Ground plane: large flat box (procedural, 100x100) --
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

        // -- Perimeter walls (procedural, mostly invisible) --
        const float wallThickness = 1.0f;
        const float wallHeight = 12.0f;
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-(groundHalfSize + wallThickness), -0.25f, -wallThickness * 0.5f),
            vec3(groundHalfSize + wallThickness, wallHeight, wallThickness * 0.5f)));
        uint32_t wallLongId = static_cast<uint32_t>(models.size() - 1);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-wallThickness * 0.5f, -0.25f, -(groundHalfSize + wallThickness)),
            vec3(wallThickness * 0.5f, wallHeight, groundHalfSize + wallThickness)));
        uint32_t wallWideId = static_cast<uint32_t>(models.size() - 1);

        auto addProcNode = [&](const std::string& name, const vec3& pos, const quat& rot,
                               uint32_t modelId, uint32_t materialId)
        {
            auto node = Assets::Node::CreateNode(name, pos, rot, vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelId);
            rc->SetVisible(true);
            rc->SetMaterial({materialId});
            node->AddComponent(rc);
            nodes.push_back(node);
        };

        addProcNode("Wall_North", vec3(0, 0, -(groundHalfSize + wallThickness * 0.5f)), quat(1, 0, 0, 0), wallLongId, matBase + 1);
        addProcNode("Wall_South", vec3(0, 0, groundHalfSize + wallThickness * 0.5f), quat(1, 0, 0, 0), wallLongId, matBase + 1);
        addProcNode("Wall_West", vec3(-(groundHalfSize + wallThickness * 0.5f), 0, 0), quat(1, 0, 0, 0), wallWideId, matBase + 1);
        addProcNode("Wall_East", vec3(groundHalfSize + wallThickness * 0.5f, 0, 0), quat(1, 0, 0, 0), wallWideId, matBase + 1);

        // -- Helper: place a KayKit piece with optional static physics --
        auto placePiece = [&](const std::string& name, int pieceIdx, const vec3& pos,
                              const quat& rot = quat(1, 0, 0, 0), bool withPhysics = true)
        {
            if (pieceIdx < 0) return;
            const auto& piece = loader.GetPiece(pieceIdx);
            auto node = Assets::Node::CreateNode(name, pos, rot, vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(piece.modelId);
            rc->SetVisible(true);
            rc->SetMaterial({piece.materialId});
            node->AddComponent(rc);

            if (withPhysics)
            {
                // Static KayKit pieces should use the scene's mesh-collider path instead of a
                // manually created AABB box. Box proxies are acceptable for cubes, but they make
                // sloped meshes behave like invisible blocks and cause the character to hover.
                auto phys = std::make_shared<Runtime::PhysicsComponent>();
                phys->SetMobility(Runtime::ENodeMobility::Static);
                node->AddComponent(phys);
            }

            nodes.push_back(node);
        };

        auto createKayKitNode = [&](const std::string& name, int pieceIdx, const vec3& pos,
                                    const quat& rot = quat(1, 0, 0, 0)) -> std::shared_ptr<Assets::Node>
        {
            if (pieceIdx < 0)
            {
                return nullptr;
            }

            const auto& piece = loader.GetPiece(pieceIdx);
            auto node = Assets::Node::CreateNode(name, pos, rot, vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(piece.modelId);
            rc->SetVisible(true);
            rc->SetMaterial({piece.materialId});
            node->AddComponent(rc);
            return node;
        };

        auto placeDynamicBoxApprox = [&](const std::string& name, int pieceIdx, const vec3& pos,
                                         const vec3& physicsOffset, const vec3& extent,
                                         const quat& rot = quat(1, 0, 0, 0))
        {
            if (pieceIdx < 0) return;
            const auto& piece = loader.GetPiece(pieceIdx);
            const vec3 fullExtent = piece.aabbMax - piece.aabbMin;
            const vec3 nodePos = pos + vec3(0.0f, -piece.aabbMin.y, 0.0f);
            const vec3 bodyExtent = fullExtent;
            const vec3 localPhysicsOffset = physicsOffset;

            auto node = createKayKitNode(name, pieceIdx, nodePos, rot);
            if (!node)
            {
                return;
            }

            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys->SetPhysicsOffset(localPhysicsOffset);
            NextBodyID bodyId = NextEngine::GetInstance()->GetPhysicsEngine()->CreateBoxBody(
                nodePos + rot * localPhysicsOffset, rot, bodyExtent, NextMotionType::Dynamic);
            phys->BindPhysicsBody(bodyId);
            node->AddComponent(phys);

            nodes.push_back(node);
        };

        auto surfaceAlignedPos = [&](int pieceIdx, const vec3& supportPos)
        {
            if (pieceIdx < 0)
            {
                return supportPos;
            }

            const auto& piece = loader.GetPiece(pieceIdx);
            return supportPos + vec3(0.0f, -piece.aabbMin.y, 0.0f);
        };

        auto placeSurfacePiece = [&](const std::string& name, int pieceIdx, const vec3& supportPos,
                                     const quat& rot = quat(1, 0, 0, 0), bool withPhysics = false)
        {
            placePiece(name, pieceIdx, surfaceAlignedPos(pieceIdx, supportPos), rot, withPhysics);
        };

        auto placeDynamicSphereApprox = [&](const std::string& name, int pieceIdx, const vec3& pos,
                                            const vec3& physicsOffset, float radius,
                                            const quat& rot = quat(1, 0, 0, 0))
        {
            if (pieceIdx < 0) return;
            const auto& piece = loader.GetPiece(pieceIdx);
            const vec3 fullExtent = piece.aabbMax - piece.aabbMin;
            const vec3 nodePos = pos + vec3(0.0f, -piece.aabbMin.y, 0.0f);
            const vec3 localPhysicsOffset = physicsOffset;
            const float bodyRadius = std::max(radius, fullExtent.y * 0.5f);

            auto node = createKayKitNode(name, pieceIdx, nodePos, rot);
            if (!node)
            {
                return;
            }

            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys->SetPhysicsOffset(localPhysicsOffset);
            NextBodyID bodyId = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(
                nodePos + rot * localPhysicsOffset, bodyRadius, NextMotionType::Dynamic);
            phys->BindPhysicsBody(bodyId);
            node->AddComponent(phys);

            nodes.push_back(node);
        };

        struct PrefabPiecePlacement
        {
            std::string suffix;
            int pieceIdx = -1;
            vec3 offset = vec3(0.0f);
            quat rotation = quat(1, 0, 0, 0);
            bool withPhysics = true;
        };

        auto placePrefab = [&](const std::string& prefix, const vec3& anchor, const quat& baseRotation,
                               const std::vector<PrefabPiecePlacement>& pieces)
        {
            for (const auto& piece : pieces)
            {
                if (piece.pieceIdx < 0)
                {
                    continue;
                }

                const vec3 worldPosition = anchor + baseRotation * piece.offset;
                const quat worldRotation = baseRotation * piece.rotation;
                placePiece(prefix + "_" + piece.suffix, piece.pieceIdx, worldPosition, worldRotation, piece.withPhysics);
            }
        };

        const quat rotY90 = glm::angleAxis(glm::half_pi<float>(), vec3(0, 1, 0));
        const quat rotY180 = glm::angleAxis(glm::pi<float>(), vec3(0, 1, 0));
        const quat rotY270 = glm::angleAxis(glm::half_pi<float>() * 3.0f, vec3(0, 1, 0));

        // ====================================================================
        // LEVEL LAYOUT - Rich 3C playground using reviewed ramp prefabs
        // ====================================================================

        const vec3 spawnAnchor(0.0f, 0.0f, 0.0f);
        const vec3 largeMainAnchor(0.0f, 0.0f, 14.0f);
        const quat largeMainRot = quat(1, 0, 0, 0);
        const vec3 steepMainAnchor(0.0f, 2.0f, 27.0f);
        const quat steepMainRot = quat(1, 0, 0, 0);
        const vec3 narrowFinishAnchor(0.0f, 5.0f, 39.0f);
        const quat narrowFinishRot = quat(1, 0, 0, 0);
        const vec3 mediumLeftAnchor(-18.0f, 0.0f, 18.0f);
        const quat mediumLeftRot = rotY90;
        const vec3 steepRightAnchor(18.0f, 0.0f, 18.0f);
        const quat steepRightRot = rotY270;
        const vec3 largeBackLeftAnchor(-24.0f, 0.0f, 34.0f);
        const quat largeBackLeftRot = rotY180;
        const vec3 mediumBackRightAnchor(24.0f, 0.0f, 34.0f);
        const quat mediumBackRightRot = rotY180;

        auto prefabPoint = [&](const vec3& anchor, const quat& rotation, const vec3& local)
        {
            return anchor + rotation * local;
        };

        const vec3 largeMainLowerTop = prefabPoint(largeMainAnchor, largeMainRot, vec3(0.0f, 1.0f, 0.0f));
        const vec3 largeMainUpperTop = prefabPoint(largeMainAnchor, largeMainRot, vec3(0.0f, 4.0f, 10.0f));
        const vec3 steepMainLowerTop = prefabPoint(steepMainAnchor, steepMainRot, vec3(0.0f, 2.0f, 0.0f));
        const vec3 steepMainUpperTop = prefabPoint(steepMainAnchor, steepMainRot, vec3(0.0f, 4.0f, 8.0f));
        const vec3 narrowFinishLowerTop = prefabPoint(narrowFinishAnchor, narrowFinishRot, vec3(0.0f, 1.0f, 0.0f));
        const vec3 narrowFinishUpperTop = prefabPoint(narrowFinishAnchor, narrowFinishRot, vec3(0.0f, 4.0f, 8.0f));
        const vec3 mediumLeftLowerTop = prefabPoint(mediumLeftAnchor, mediumLeftRot, vec3(0.0f, 1.0f, 0.0f));
        const vec3 mediumLeftUpperTop = prefabPoint(mediumLeftAnchor, mediumLeftRot, vec3(0.0f, 4.0f, 9.0f));
        const vec3 steepRightLowerTop = prefabPoint(steepRightAnchor, steepRightRot, vec3(0.0f, 2.0f, 0.0f));
        const vec3 steepRightUpperTop = prefabPoint(steepRightAnchor, steepRightRot, vec3(0.0f, 4.0f, 8.0f));
        const vec3 largeBackLeftUpperTop = prefabPoint(largeBackLeftAnchor, largeBackLeftRot, vec3(0.0f, 4.0f, 10.0f));
        const vec3 mediumBackRightUpperTop = prefabPoint(mediumBackRightAnchor, mediumBackRightRot, vec3(0.0f, 4.0f, 9.0f));
        const vec3 flagSurfaceBias(0.0f, -0.05f, 0.0f);

        placePiece("Spawn_Platform", platHuge, spawnAnchor);
        placePiece("WarmupPad_Left", platBlueMed, vec3(-4.5f, 0.0f, 6.5f));
        placePiece("WarmupPad_Right", platBlueMed, vec3(4.5f, 0.0f, 6.5f));
        placePiece("WarmupBridge", platBlueWide, vec3(0.0f, 0.0f, 9.5f));

        const std::vector<PrefabPiecePlacement> bluePlatformRampPrefab{
            {"LowerDeck", platBlueHuge, vec3(0.0f, 0.0f, 0.0f)},
            {"MainSlope", slopeBlueHuge, vec3(0.0f, 0.0f, 6.0f), rotY180},
            {"UpperDeck", platBlueTallLong, vec3(0.0f, 0.0f, 10.0f)},
        };

        placePrefab("BluePlatformRamp_Main", largeMainAnchor, largeMainRot, bluePlatformRampPrefab);

        const std::vector<PrefabPiecePlacement> bluePlatformRampSteepPrefab{
            {"LowerDeck", platBlueHugeTall, vec3(0.0f, 0.0f, 0.0f)},
            {"MainSlope", slopeBlueWide, vec3(0.0f, 0.0f, 5.0f), rotY180},
            {"UpperDeck", platBlueTallLong, vec3(0.0f, 0.0f, 8.0f)},
        };

        placePrefab("BluePlatformRampSteep_Main", steepMainAnchor, steepMainRot, bluePlatformRampSteepPrefab);
        placePrefab("BluePlatformRampSteep_Right", steepRightAnchor, steepRightRot, bluePlatformRampSteepPrefab);

        const std::vector<PrefabPiecePlacement> bluePlatformRampMediumWidthPrefab{
            {"LowerDeck", platBlue, vec3(0.0f, 0.0f, 0.0f)},
            {"MainSlope", slopeBlueMediumNarrow, vec3(0.0f, 0.0f, 5.0f), rotY180},
            {"UpperDeck", platBlueTall, vec3(0.0f, 0.0f, 9.0f)},
        };

        placePrefab("BluePlatformRampMediumWidth_Left", mediumLeftAnchor, mediumLeftRot, bluePlatformRampMediumWidthPrefab);
        placePrefab("BluePlatformRampMediumWidth_BackRight", mediumBackRightAnchor, mediumBackRightRot, bluePlatformRampMediumWidthPrefab);

        const std::vector<PrefabPiecePlacement> bluePlatformRampNarrowPrefab{
            {"LowerDeck", platBlueMed, vec3(0.0f, 0.0f, 0.0f)},
            {"MainSlope", slopeBlueNarrow, vec3(0.0f, 0.0f, 4.0f), rotY180},
            {"UpperDeck", platBlueMedTall, vec3(0.0f, 0.0f, 8.0f)},
        };

        placePrefab("BluePlatformRampNarrow_Finish", narrowFinishAnchor, narrowFinishRot, bluePlatformRampNarrowPrefab);
        placePrefab("BluePlatformRamp_BackLeft", largeBackLeftAnchor, largeBackLeftRot, bluePlatformRampPrefab);

        // Connectors expand the reviewed prefab set into a less linear route.
        placePiece("Connector_CentralHigh", platBlueLong, vec3(0.0f, 5.0f, 37.0f));
        placePiece("Connector_CentralHigh_LeftLip", platBlueMed, vec3(-4.0f, 5.0f, 37.0f));
        placePiece("Connector_CentralHigh_RightLip", platBlueMed, vec3(4.0f, 5.0f, 37.0f));
        placePiece("Connector_LeftBranch", platBlueLong, vec3(-12.0f, 0.0f, 11.5f), rotY90);
        placePiece("Connector_RightBranch", platBlueLong, vec3(12.0f, 0.0f, 11.5f), rotY90);
        placePiece("Connector_BackLeft", platBlueWide, vec3(-18.0f, 0.0f, 34.0f), quat(1, 0, 0, 0));
        placePiece("Connector_BackRight", platBlueWide, vec3(18.0f, 0.0f, 34.0f), quat(1, 0, 0, 0));

        // Route markers and set dressing. These stay non-physical so the reviewed ramps
        // remain the primary collision geometry for character traversal.
        placeSurfacePiece("StartGate", archWide, vec3(0.0f, 0.0f, 5.5f), rotY90);
        placeSurfacePiece("StartHoop_Left", hoop, vec3(-8.5f, 0.0f, 7.0f), rotY90);
        placeSurfacePiece("StartHoop_Right", hoopAngled, vec3(8.5f, 0.0f, 7.0f), rotY90);
        placeSurfacePiece("GuideArrow_LargeRamp", arrowStand, prefabPoint(largeMainAnchor, largeMainRot, vec3(-2.8f, 0.0f, -3.4f)), largeMainRot);
        placeSurfacePiece("GuideArrow_SteepRamp", arrowStand, prefabPoint(steepMainAnchor, steepMainRot, vec3(2.6f, 0.0f, -3.2f)), steepMainRot);
        placeSurfacePiece("GuideArrow_LeftBranch", arrowStand, prefabPoint(mediumLeftAnchor, mediumLeftRot, vec3(0.0f, 0.0f, -3.2f)), mediumLeftRot);
        placeSurfacePiece("GuideArrow_RightBranch", arrowStand, prefabPoint(steepRightAnchor, steepRightRot, vec3(0.0f, 0.0f, -3.2f)), steepRightRot);
        placeSurfacePiece("GuideArrow_FinishRamp", arrowStand, prefabPoint(narrowFinishAnchor, narrowFinishRot, vec3(0.0f, 1.0f, -3.0f)), narrowFinishRot);
        placeSurfacePiece("GuideSign_Left", arrowsLeft, vec3(-13.0f, 0.0f, 11.5f), rotY90);
        placeSurfacePiece("GuideSign_Right", arrowsRight, vec3(13.0f, 0.0f, 11.5f), rotY270);
        placeSurfacePiece("RampFlag_Large", flagC, largeMainUpperTop + largeMainRot * vec3(-1.6f, 0.0f, 0.4f) + flagSurfaceBias, rotY180);
        placeSurfacePiece("RampFlag_Steep", flagA, steepMainUpperTop + steepMainRot * vec3(1.3f, 0.0f, -0.4f) + flagSurfaceBias, rotY90);
        placeSurfacePiece("RampFlag_Left", flagB, mediumLeftUpperTop + mediumLeftRot * vec3(0.3f, 0.0f, -0.6f) + flagSurfaceBias, rotY180);
        placeSurfacePiece("RampFlag_Right", flagC, steepRightUpperTop + steepRightRot * vec3(-1.2f, 0.0f, 0.3f) + flagSurfaceBias, rotY270);
        placeSurfacePiece("RampFlag_Finish", flagC, narrowFinishUpperTop + narrowFinishRot * vec3(0.2f, 0.0f, 0.0f) + flagSurfaceBias, rotY90);
        placeSurfacePiece("RampFlag_BackLeft", flagA, largeBackLeftUpperTop + largeBackLeftRot * vec3(1.2f, 0.0f, -0.2f) + flagSurfaceBias, rotY270);
        placeSurfacePiece("RampFlag_BackRight", flagB, mediumBackRightUpperTop + mediumBackRightRot * vec3(-0.4f, 0.0f, 0.4f) + flagSurfaceBias, rotY180);
        placeSurfacePiece("RampSpring_Large", springPad, largeMainUpperTop + vec3(2.0f, 0.0f, -0.8f));
        placeSurfacePiece("RampSpring_Finish", springPad, narrowFinishUpperTop + vec3(0.0f, 0.0f, -0.8f));
        placeSurfacePiece("FinishSign", finishSign, vec3(0.0f, 9.0f, 48.0f));
        placeSurfacePiece("PipeShowcase_Straight", pipeStraightBlue, vec3(40.0f, 0.0f, 6.0f), rotY90);
        placeSurfacePiece("PipeShowcase_Elbow", pipeElbow, vec3(41.8f, 0.0f, 8.0f));
        placeSurfacePiece("PipeShowcase_End", pipeEndGreen, vec3(43.0f, 0.0f, 9.8f), rotY90);

        // Small decorations use approximate dynamic physics bodies so they can be pushed
        // around during traversal and shooting tests without dominating the collision scene.
        placeDynamicSphereApprox("Prop_Ball_Start", ballBlue, vec3(-9.0f, 0.0f, 2.0f),
                                 loader.GetCollisionOffset(ballBlue), 0.55f);
        placeDynamicBoxApprox("Prop_Cone_Start", cone, vec3(-5.5f, 0.0f, 3.8f),
                              loader.GetCollisionOffset(cone), vec3(0.16f, 0.22f, 0.16f));
        placeDynamicBoxApprox("Prop_Diamond_Start", diamond, vec3(-1.2f, 0.0f, 5.2f),
                              loader.GetCollisionOffset(diamond), vec3(0.24f, 0.24f, 0.24f), rotY90);
        placeDynamicBoxApprox("Prop_Heart_Start", heart, vec3(1.6f, 0.0f, 5.4f),
                              loader.GetCollisionOffset(heart), vec3(0.28f, 0.24f, 0.28f));
        placeDynamicBoxApprox("Prop_Star_Start", star, vec3(4.6f, 0.0f, 4.4f),
                              loader.GetCollisionOffset(star), vec3(0.32f, 0.18f, 0.32f));
        placeDynamicBoxApprox("Prop_Button_Warmup", buttonBaseBlue, vec3(5.0f, 1.0f, 8.5f),
                              loader.GetCollisionOffset(buttonBaseBlue), vec3(0.45f, 0.16f, 0.45f));
        placeDynamicSphereApprox("Prop_Bomb_LargeTop", bombABlue, largeMainUpperTop + vec3(-2.0f, 0.0f, -0.7f),
                                 loader.GetCollisionOffset(bombABlue), 0.24f);
        placeDynamicBoxApprox("Prop_Power_SteepTop", power, steepMainUpperTop + vec3(-2.2f, 0.0f, 0.8f),
                              loader.GetCollisionOffset(power), vec3(0.24f, 0.36f, 0.24f));
        placeDynamicBoxApprox("Prop_Lever_SteepLow", leverFloorBlue, steepMainLowerTop + vec3(2.2f, 0.0f, -1.2f),
                              loader.GetCollisionOffset(leverFloorBlue), vec3(0.32f, 0.22f, 0.32f));
        placeDynamicSphereApprox("Prop_Bomb_MediumTop", bombBBlue, mediumLeftUpperTop + mediumLeftRot * vec3(0.8f, 0.0f, 1.0f),
                                 loader.GetCollisionOffset(bombBBlue), 0.24f);
        placeDynamicBoxApprox("Prop_Diamond_MediumTop", diamond, mediumBackRightUpperTop + mediumBackRightRot * vec3(-1.0f, 0.0f, -0.8f),
                              loader.GetCollisionOffset(diamond), vec3(0.24f, 0.24f, 0.24f), rotY90);
        placeDynamicBoxApprox("Prop_Cone_NarrowLow", cone, narrowFinishLowerTop + vec3(0.9f, 0.0f, -0.8f),
                              loader.GetCollisionOffset(cone), vec3(0.16f, 0.22f, 0.16f));
        placeDynamicBoxApprox("Prop_Heart_NarrowTop", heart, narrowFinishUpperTop + vec3(-0.8f, 0.0f, 0.9f),
                              loader.GetCollisionOffset(heart), vec3(0.28f, 0.24f, 0.28f));
        placeDynamicSphereApprox("Prop_Ball_RightBranchTop", ballBlue, steepRightUpperTop + steepRightRot * vec3(-2.2f, 0.0f, -0.8f),
                                 loader.GetCollisionOffset(ballBlue), 0.48f);
        placeDynamicSphereApprox("Prop_Ball_BackLeftTop", ballBlue, largeBackLeftUpperTop + largeBackLeftRot * vec3(2.1f, 0.0f, 0.8f),
                                 loader.GetCollisionOffset(ballBlue), 0.48f);
        placeDynamicBoxApprox("Prop_Spring_Ground", spring, vec3(38.8f, 0.0f, 8.4f),
                              loader.GetCollisionOffset(spring), vec3(0.30f, 0.42f, 0.30f));
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
    AllScenes.clear();

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

    // Pull additional top-level scene entries from any mounted paks (e.g. optional.pak),
    // so files moved out of the on-disk tree still appear in the scene list.
    auto* pakSystem = Utilities::Package::FPackageFileSystem::TryGetInstance();
    if (pakSystem != nullptr)
    {
        auto mergePakPrefix = [pakSystem](const std::string& prefix)
        {
            for (const auto& entry : pakSystem->ListMountedEntries(prefix))
            {
                // Only top-level files under the prefix (skip nested directories like KayKit/...).
                if (entry.find('/', prefix.size()) != std::string::npos) continue;
                if (!IsSupportedScenePath(std::filesystem::path(entry))) continue;
                AllScenes.push_back(entry);
            }
        };
        mergePakPrefix(modelPath);
        mergePakPrefix(omrPath);
    }

    AllScenes.push_back("CornellBox.proc");
    AllScenes.push_back("GIBootcamp.proc");
    AllScenes.push_back("MaterialShowcase.proc");
    AllScenes.push_back("LightingShowcase.proc");
    AllScenes.push_back("CameraShowcase.proc");
    AllScenes.push_back("RTIO.proc");

    // Deduplicate (a file may exist on disk and in pak) before sorting.
    std::sort(AllScenes.begin(), AllScenes.end(), [](const std::string& lhs, const std::string& rhs)
    {
        const ESceneCategory lhsCategory = GetSceneCategory(lhs);
        const ESceneCategory rhsCategory = GetSceneCategory(rhs);
        if (lhsCategory != rhsCategory)
        {
            return lhsCategory < rhsCategory;
        }
        return lhs < rhs;
    });
    AllScenes.erase(std::unique(AllScenes.begin(), AllScenes.end()), AllScenes.end());

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
        if (filename == "GIBootcamp.proc")
        {
            GIBootcamp(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        if (filename == "MaterialShowcase.proc")
        {
            MaterialShowcase(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        if (filename == "LightingShowcase.proc")
        {
            LightingShowcase(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        if (filename == "CameraShowcase.proc")
        {
            CameraShowcase(camera, nodes, models, materials, lights, tracks);
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
