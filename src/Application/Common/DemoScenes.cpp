#include "Application/Common/DemoScenes.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <functional>
#include <random>
#include <cmath>

#include <spdlog/spdlog.h>

using namespace glm;

using Assets::Material;
using Assets::Model;

// Showcase ".proc" demo scenes shared by the render programs
// (gkNextRenderer / gkNextVisualTest / benchmarks). Moved out of the engine
// core scene list; registered into Assets::FLoaderRegistry at startup.
namespace
{
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
                        newNode = Assets::SceneBuilder::CreateRenderNode(name, center, vec3(1, 1, 1),
                                                                 static_cast<uint32_t>(nodes.size()), meshIdx, matId);
                    }
                    else if (chooseMat < 0.9f) // Metal
                    {
                        const float fuzziness = 0.5f * random();
                        const float b = 0.5f * (1 + random());
                        const float g = 0.5f * (1 + random());
                        const float r = 0.5f * (1 + random());
                        uint32_t matId = CreateMaterial(materials, Material::Metallic(vec3(r,g,b), fuzziness));
                        newNode = Assets::SceneBuilder::CreateRenderNode(name, center, vec3(1, 1, 1),
                                                                 static_cast<uint32_t>(nodes.size()), meshIdx, matId);
                    }
                    else // Glass
                    {
                        const float fuzziness = 0.5f * random();
                        uint32_t matId = CreateMaterial(materials, Material::Dielectric(1.5f, fuzziness));
                        newNode = Assets::SceneBuilder::CreateRenderNode(name, center, vec3(1, 1, 1),
                                                                 static_cast<uint32_t>(nodes.size()), meshIdx, matId);
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
            auto newNode = Assets::SceneBuilder::CreateRenderNode(Utilities::NameHelper::RandomName(6), vec3(0, 0, 0),
                                                          vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()), 0,
                                                          prevMatId + 0);
            nodes.push_back(newNode);
        }
        
        AddRayTracingInOneWeekendCommonScene(nodes, models, materials, tracks, random);

        uint32_t matIdx0 = CreateMaterial(materials, Material::Dielectric(1.5f, 0.0f));
        uint32_t matIdx1 = CreateMaterial(materials, Material::Lambertian(vec3(0.4f, 0.2f, 0.1f)));
        uint32_t matIdx2 = CreateMaterial(materials, Material::Metallic(vec3(0.7f, 0.6f, 0.5f), 0.1f));
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 1.0f));
        uint32_t modelIdx = static_cast<uint32_t>(models.size() - 1);
        
        {
            auto newNode = Assets::SceneBuilder::CreateRenderNode(Utilities::NameHelper::RandomName(6), vec3(0, 1, 0),
                                                          vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()),
                                                          modelIdx, matIdx0);
            
            auto body1 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(0, 1, 0), 1.0f, NextMotionType::Dynamic);
            auto phys1 = std::make_shared<Runtime::PhysicsComponent>();
            phys1->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys1->BindPhysicsBody(body1);
            newNode->AddComponent(phys1);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::SceneBuilder::CreateRenderNode(Utilities::NameHelper::RandomName(6), vec3(-4, 1, 0),
                                                          vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()),
                                                          modelIdx, matIdx1);
            
            auto body2 = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(vec3(-4, 1, 0), 1.0f, NextMotionType::Dynamic);
            auto phys2 = std::make_shared<Runtime::PhysicsComponent>();
            phys2->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys2->BindPhysicsBody(body2);
            newNode->AddComponent(phys2);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::SceneBuilder::CreateRenderNode(Utilities::NameHelper::RandomName(6), vec3(4, 1, 0),
                                                          vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()),
                                                          modelIdx, matIdx2);

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
            auto newNode = Assets::SceneBuilder::CreateRenderNode(
                Utilities::NameHelper::RandomName(6),
                vec3(0, 0, 0),
                vec3(1, 1, 1),
                static_cast<uint32_t>(nodes.size()),
                static_cast<uint32_t>(cboxModel),
                std::array<uint32_t, 16>{prevMatId + 0, prevMatId + 1, prevMatId + 2, prevMatId + 3});
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
            auto newNode = Assets::SceneBuilder::CreateRenderNode("Sphere1",
                                                          spherePos,
                                                          vec3(1, 1, 1),
                                                          static_cast<uint32_t>(nodes.size()),
                                                          static_cast<uint32_t>(cboxModel + 2),
                                                          prevMatId + 5,
                                                          true,
                                                          quat(vec3(0, 0.5f, 0)));
            
            auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateSphereBody(spherePos, 1.0f, NextMotionType::Dynamic);
            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            phys->BindPhysicsBody(id);
            newNode->AddComponent(phys);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::SceneBuilder::CreateRenderNode("Box",
                                                          boxPos,
                                                          vec3(1, 2, 1),
                                                          static_cast<uint32_t>(nodes.size()),
                                                          static_cast<uint32_t>(cboxModel + 1),
                                                          prevMatId + 4,
                                                          true,
                                                          quat(vec3(0, 0.25f, 0)));
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
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(name,
                                                           pos,
                                                           vec3(1),
                                                           static_cast<uint32_t>(nodes.size()),
                                                           modelIdx,
                                                           matIdx,
                                                           true,
                                                           rot));
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
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(name,
                                                           pos,
                                                           vec3(1),
                                                           static_cast<uint32_t>(nodes.size()),
                                                           modelIdx,
                                                           matIdx));
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
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(name,
                                                           pos,
                                                           vec3(1),
                                                           static_cast<uint32_t>(nodes.size()),
                                                           modelIdx,
                                                           matIdx));
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
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(name,
                                                           pos,
                                                           vec3(1),
                                                           static_cast<uint32_t>(nodes.size()),
                                                           modelIdx,
                                                           matIdx));
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

    void AnimationShowcase(Assets::EnvironmentSetting& cameraInit,
                           std::vector<std::shared_ptr<Assets::Node>>& nodes,
                           std::vector<Assets::Model>& models,
                           std::vector<Assets::FMaterial>& materials,
                           std::vector<Assets::LightObject>& lights,
                           std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "AnimationShowcaseCam";
        defaultCam.ModelView = lookAt(vec3(0.0f, 4.0f, 10.0f), vec3(0.0f, 1.0f, 0.0f), vec3(0, 1, 0));
        defaultCam.FieldOfView = 48;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 10;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 4.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.SkyIdx = 0;
        cameraInit.SkyIntensity = 12.0f;
        cameraInit.HasSun = true;
        cameraInit.SunIntensity = 250.0f;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.45f, 0.45f, 0.46f)), "as_ground"});
        const uint32_t matRed = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.74f, 0.22f, 0.18f)), "as_rotation_red"});
        const uint32_t matBlue = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.18f, 0.36f, 0.78f)), "as_translation_blue"});
        const uint32_t matGreen = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.18f, 0.62f, 0.28f)), "as_scale_green"});

        auto addNode = [&](const std::string& name, const vec3& pos, uint32_t modelIdx, uint32_t matIdx)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(name, pos, vec3(1), static_cast<uint32_t>(nodes.size()),
                                                       modelIdx, matIdx);
            nodes.push_back(node);
            return node;
        };

        models.push_back(Assets::FProcModel::CreateBox(vec3(-6.5f, -0.08f, -3.0f), vec3(6.5f, 0.0f, 3.0f)));
        addNode("AnimationShowcase_Ground", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matGround);

        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 0.5f, 0.5f)));
        const uint32_t cubeModel = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0, 0, 0), 0.55f));
        const uint32_t sphereModel = static_cast<uint32_t>(models.size() - 1);

        addNode("AS_RotatingCube", vec3(-3.0f, 0.65f, 0.0f), cubeModel, matRed);
        addNode("AS_BobbingSphere", vec3(0.0f, 1.0f, 0.0f), sphereModel, matBlue);
        addNode("AS_PulsingCube", vec3(3.0f, 0.65f, 0.0f), cubeModel, matGreen);

        Assets::AnimationTrack rotationTrack;
        rotationTrack.AnimationName = "RotationY";
        rotationTrack.NodeName_ = "AS_RotatingCube";
        rotationTrack.Duration_ = 12.0f;
        rotationTrack.Play();
        for (int i = 0; i <= 12; ++i)
        {
            const float time = static_cast<float>(i);
            const float angle = glm::half_pi<float>() * static_cast<float>(i);
            rotationTrack.RotationChannel.Keys.push_back({time, glm::angleAxis(angle, vec3(0, 1, 0))});
        }
        tracks.push_back(rotationTrack);

        Assets::AnimationTrack bobTrack;
        bobTrack.AnimationName = "BobY";
        bobTrack.NodeName_ = "AS_BobbingSphere";
        bobTrack.Duration_ = 12.0f;
        bobTrack.Play();
        for (int i = 0; i <= 24; ++i)
        {
            const float time = static_cast<float>(i) * 0.5f;
            const float y = 1.0f + 0.5f * std::sin(glm::pi<float>() * time);
            bobTrack.TranslationChannel.Keys.push_back({time, vec3(0.0f, y, 0.0f)});
        }
        tracks.push_back(bobTrack);

        Assets::AnimationTrack scaleTrack;
        scaleTrack.AnimationName = "ScalePulse";
        scaleTrack.NodeName_ = "AS_PulsingCube";
        scaleTrack.Duration_ = 12.0f;
        scaleTrack.Play();
        for (int i = 0; i <= 16; ++i)
        {
            const float time = static_cast<float>(i) * 0.75f;
            const float scale = 1.0f + 0.3f * std::sin(glm::two_pi<float>() * time / 3.0f);
            scaleTrack.ScaleChannel.Keys.push_back({time, vec3(scale)});
        }
        tracks.push_back(scaleTrack);
    }

    void PhysicsShowcase(Assets::EnvironmentSetting& cameraInit,
                         std::vector<std::shared_ptr<Assets::Node>>& nodes,
                         std::vector<Assets::Model>& models,
                         std::vector<Assets::FMaterial>& materials,
                         std::vector<Assets::LightObject>& lights,
                         std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "PhysicsShowcaseCam";
        defaultCam.ModelView = lookAt(vec3(0.0f, 7.0f, 14.0f), vec3(0.0f, 3.0f, 0.0f), vec3(0, 1, 0));
        defaultCam.FieldOfView = 52;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 14;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 5.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.SkyIdx = 0;
        cameraInit.SkyIntensity = 12.0f;
        cameraInit.HasSun = true;
        cameraInit.SunIntensity = 350.0f;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.45f, 0.45f, 0.46f)), "ps_ground"});
        const uint32_t matRamp = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.55f, 0.50f, 0.42f)), "ps_ramp"});
        const uint32_t matRed = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.74f, 0.22f, 0.18f)), "ps_red"});
        const uint32_t matBlue = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.18f, 0.36f, 0.78f)), "ps_blue"});
        const uint32_t matGreen = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.18f, 0.62f, 0.28f)), "ps_green"});
        const uint32_t matYellow = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.82f, 0.68f, 0.20f)), "ps_yellow"});

        auto addRenderNode = [&](const std::string& name, const vec3& pos, const quat& rot,
                                 uint32_t modelIdx, uint32_t matIdx)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(name, pos, vec3(1), static_cast<uint32_t>(nodes.size()),
                                                       modelIdx, matIdx, true, rot);
            nodes.push_back(node);
            return node;
        };

        models.push_back(Assets::FProcModel::CreateBox(vec3(-7.0f, -0.10f, -4.5f), vec3(7.0f, 0.0f, 4.5f)));
        auto ground = addRenderNode("PhysicsShowcase_Ground", vec3(0), quat(1, 0, 0, 0),
                                    static_cast<uint32_t>(models.size() - 1), matGround);
        auto groundPhys = std::make_shared<Runtime::PhysicsComponent>();
        groundPhys->SetMobility(Runtime::ENodeMobility::Static);
        ground->AddComponent(groundPhys);

        models.push_back(Assets::FProcModel::CreateBox(vec3(-2.2f, -0.15f, -0.8f), vec3(2.2f, 0.15f, 0.8f)));
        auto ramp = addRenderNode("PhysicsShowcase_Ramp", vec3(-2.0f, 1.0f, 0.0f),
                                  glm::angleAxis(glm::radians(-16.0f), vec3(0, 0, 1)),
                                  static_cast<uint32_t>(models.size() - 1), matRamp);
        auto rampPhys = std::make_shared<Runtime::PhysicsComponent>();
        rampPhys->SetMobility(Runtime::ENodeMobility::Static);
        ramp->AddComponent(rampPhys);

        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f, -0.5f, -0.5f), vec3(0.5f, 0.5f, 0.5f)));
        const uint32_t cubeModel = static_cast<uint32_t>(models.size() - 1);

        struct CubeSpec
        {
            vec3 position;
            vec3 extent;
            uint32_t materialId;
            const char* name;
        };

        const CubeSpec cubes[] = {
            {vec3(-2.8f, 7.5f, -0.6f), vec3(0.85f, 0.85f, 0.85f), matRed, "PhysicsCube_Red"},
            {vec3(-1.4f, 9.0f, 0.5f), vec3(1.10f, 0.80f, 0.90f), matBlue, "PhysicsCube_Blue"},
            {vec3(0.2f, 6.5f, -0.4f), vec3(0.75f, 1.20f, 0.75f), matGreen, "PhysicsCube_Green"},
            {vec3(1.8f, 8.2f, 0.7f), vec3(1.25f, 0.75f, 0.85f), matYellow, "PhysicsCube_Yellow"},
            {vec3(3.0f, 10.0f, -0.5f), vec3(0.90f, 0.90f, 1.20f), matRed, "PhysicsCube_Tall"},
        };

        for (const auto& cube : cubes)
        {
            auto node = addRenderNode(cube.name, cube.position, quat(1, 0, 0, 0), cubeModel, cube.materialId);
            node->SetScale(cube.extent);
            node->RecalcTransform(true);

            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            const NextBodyID bodyId = NextEngine::GetInstance()->GetPhysicsEngine()->CreateBoxBody(
                cube.position, cube.extent, NextMotionType::Dynamic);
            phys->BindPhysicsBody(bodyId);
            node->AddComponent(phys);
        }
    }
}

namespace AppCommon
{
    void RegisterDemoScenes()
    {
        auto& registry = Assets::FLoaderRegistry::Get();
        registry.RegisterProcScene("CornellBox.proc", CornellBox);
        registry.RegisterProcScene("GIBootcamp.proc", GIBootcamp);
        registry.RegisterProcScene("MaterialShowcase.proc", MaterialShowcase);
        registry.RegisterProcScene("LightingShowcase.proc", LightingShowcase);
        registry.RegisterProcScene("CameraShowcase.proc", CameraShowcase);
        registry.RegisterProcScene("AnimationShowcase.proc", AnimationShowcase);
        registry.RegisterProcScene("PhysicsShowcase.proc", PhysicsShowcase);
        registry.RegisterProcScene("RTIO.proc", RayTracingInOneWeekend);
    }
}
