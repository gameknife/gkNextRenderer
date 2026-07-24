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
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <functional>
#include <random>
#include <cmath>
#include <unordered_map>

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

    void AttachSpherePhysics(const std::shared_ptr<Assets::Node>& node, const vec3& position,
                             float radius, NextMotionType motionType)
    {
        // DemoScenes remain loadable by core-only applications; installing NextPhysics
        // upgrades the same scene nodes with live bodies.
        NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine();
        if (!physicsEngine)
        {
            return;
        }

        auto component = std::make_shared<Runtime::PhysicsComponent>();
        component->SetMobility(motionType == NextMotionType::Dynamic
            ? Runtime::ENodeMobility::Dynamic
            : Runtime::ENodeMobility::Static);
        component->BindPhysicsBody(physicsEngine->CreateSphereBody(position, radius, motionType));
        node->AddComponent(component);
    }

    void AttachBoxPhysics(const std::shared_ptr<Assets::Node>& node, const vec3& position,
                          const quat& rotation, const vec3& extent, NextMotionType motionType)
    {
        // Keep the render-only fallback deterministic when no physics backend is installed.
        NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine();
        if (!physicsEngine)
        {
            return;
        }

        auto component = std::make_shared<Runtime::PhysicsComponent>();
        component->SetMobility(motionType == NextMotionType::Dynamic
            ? Runtime::ENodeMobility::Dynamic
            : Runtime::ENodeMobility::Static);
        component->BindPhysicsBody(physicsEngine->CreateBoxBody(position, rotation, extent, motionType));
        node->AddComponent(component);
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
                    AttachSpherePhysics(newNode, center, 0.2f, NextMotionType::Dynamic);
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
            AttachSpherePhysics(newNode, vec3(0, 1, 0), 1.0f, NextMotionType::Dynamic);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::SceneBuilder::CreateRenderNode(Utilities::NameHelper::RandomName(6), vec3(-4, 1, 0),
                                                          vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()),
                                                          modelIdx, matIdx1);
            AttachSpherePhysics(newNode, vec3(-4, 1, 0), 1.0f, NextMotionType::Dynamic);
            nodes.push_back(newNode);
        }
        
        {
            auto newNode = Assets::SceneBuilder::CreateRenderNode(Utilities::NameHelper::RandomName(6), vec3(4, 1, 0),
                                                          vec3(1, 1, 1), static_cast<uint32_t>(nodes.size()),
                                                          modelIdx, matIdx2);
            AttachSpherePhysics(newNode, vec3(4, 1, 0), 1.0f, NextMotionType::Dynamic);
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

        const size_t firstCornellLight = lights.size();
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
            auto lightComponent = std::make_shared<Runtime::LightComponent>();
            for (size_t lightIndex = firstCornellLight; lightIndex < lights.size(); ++lightIndex)
            {
                lightComponent->AddLight(lights[lightIndex]);
            }
            newNode->AddComponent(lightComponent);
            lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstCornellLight), lights.end());
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
            AttachSpherePhysics(newNode, spherePos, 1.0f, NextMotionType::Dynamic);
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
            const size_t firstLight = lights.size();
            models.push_back(Assets::FProcModel::CreateAreaLight(name, origin, right, up, lightMat, lights));
            addNode(name, vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), lightMat);
            auto lightComponent = std::make_shared<Runtime::LightComponent>(lights.back());
            nodes.back()->AddComponent(lightComponent);
            lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstLight), lights.end());
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
        cameraInit.SkyIntensity = 50.0f;
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
            const float intensity = 500.0f + roughness[i] * 300.0f;
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

        const size_t firstKeyLight = lights.size();
        models.push_back(Assets::FProcModel::CreateAreaLight(
            "KeyLight", vec3(-3.0f, 5.5f, 1.5f), vec3(6.0f, 0.0f, 0.0f),
            vec3(0.0f, 0.0f, 3.0f), matLight, lights));
        addNode("KeyLight", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matLight);
        nodes.back()->AddComponent(std::make_shared<Runtime::LightComponent>(lights.back()));
        lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstKeyLight), lights.end());
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
        cameraInit.HasSun = false;
        cameraInit.SunIntensity = 300.0f;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.45f, 0.45f, 0.46f)), "ls_ground_gray"});
        const uint32_t matSphere = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Mixture(vec3(0.72f, 0.72f, 0.72f), 0.2f), "ls_sphere_neutral"});

        const uint32_t matPointLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(1000.0f, 900.0f, 800.0f)), "ls_point_light"});
        const uint32_t matAreaLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(700.0f, 800.0f, 1200.0f)), "ls_area_light"});
        const uint32_t matFillLight = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::DiffuseLight(vec3(1050.0f, 1080.0f, 600.0f)), "ls_fill_light"});

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
        const vec3 pointLightPosition(-5.25f, 4.5f, -1.0f);
        constexpr float pointLightRadius = 0.2f;
        size_t firstLight = lights.size();
        models.push_back(Assets::FProcModel::CreatePointLight(
            "PointLight", pointLightPosition, pointLightRadius, matPointLight, lights));
        const uint32_t pointLightModel = static_cast<uint32_t>(models.size() - 1);
        addNode("PointLight", vec3(0), pointLightModel, matPointLight);
        nodes.back()->AddComponent(std::make_shared<Runtime::LightComponent>(lights.back()));
        lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstLight), lights.end());

        // Light 2: Area light (rectangle above sphere 2)
        firstLight = lights.size();
        models.push_back(Assets::FProcModel::CreateAreaLight(
            "AreaLight", vec3(-2.75f, 4.5f, -0.5f), vec3(2.0f, 0.0f, 0.0f),
            vec3(0.0f, 0.0f, 1.0f), matAreaLight, lights));
        addNode("AreaLight", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matAreaLight);
        nodes.back()->AddComponent(std::make_shared<Runtime::LightComponent>(lights.back()));
        lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstLight), lights.end());

        // Light 3: Uses global sun (directional) - no explicit light node needed
        // Light 4: Fill light (small area light near sphere 4)
        firstLight = lights.size();
        models.push_back(Assets::FProcModel::CreateAreaLight(
            "FillLight", vec3(4.75f, 2.8f, 1.0f), vec3(1.0f, 0.0f, 0.0f),
            vec3(0.0f, -0.5f, 0.5f), matFillLight, lights));
        addNode("FillLight", vec3(0, 0, 0), static_cast<uint32_t>(models.size() - 1), matFillLight);
        nodes.back()->AddComponent(std::make_shared<Runtime::LightComponent>(lights.back()));
        lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstLight), lights.end());
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
        cameraInit.SkyIntensity = 50.0f;
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
        cameraInit.SkyIntensity = 50.0f;
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
        cameraInit.SkyIntensity = 50.0f;
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

            AttachBoxPhysics(node, cube.position, quat(1, 0, 0, 0), cube.extent, NextMotionType::Dynamic);
        }
    }
}

namespace
{
    // ReSTIR stress scene: 64 colored area lights over a pillar field, no sun/sky, so every
    // surface point sees a different subset of many lights (docs/plans/pathtracing-restir-plan.md M1).
    void ManyLightsShowcase(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera defaultCam;
        defaultCam.name = "Cam";
        defaultCam.ModelView = lookAt(vec3(0, 15.0f, 24.0f), vec3(0, 0.5f, 0), vec3(0, 1, 0));
        defaultCam.FieldOfView = 50;
        defaultCam.Aperture = 0;
        defaultCam.FocalDistance = 26;

        cameraInit.cameras.push_back(defaultCam);
        cameraInit.ControlSpeed = 10.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = false;
        cameraInit.HasSun = false;

        const uint32_t matGround = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.55f, 0.55f, 0.55f)), "ml_ground"});
        const uint32_t matPillar = static_cast<uint32_t>(materials.size());
        materials.push_back({Material::Lambertian(vec3(0.70f, 0.68f, 0.62f)), "ml_pillar"});

        // Eight emitter colors with a wide intensity spread: the light CDF weights by
        // luminance * area, so this also exercises non-uniform light selection.
        const vec3 lightColors[8] = {
            vec3(2200.0f, 30.0f, 12.0f), vec3(30.0f, 2200.0f, 20.0f),
            vec3(20.0f, 35.0f, 2400.0f), vec3(2100.0f, 2000.0f, 25.0f),
            vec3(1000.0f, 25.0f, 2000.0f), vec3(25.0f, 2000.0f, 2000.0f),
            vec3(1900.0f, 1850.0f, 1780.0f), vec3(2600.0f, 50.0f, 25.0f),
        };
        const uint32_t lightMatBase = static_cast<uint32_t>(materials.size());
        for (int i = 0; i < 8; ++i)
        {
            materials.push_back({Material::DiffuseLight(lightColors[i]), "ml_light_" + std::to_string(i)});
        }

        auto addNode = [&](const std::string& name, uint32_t modelIdx, uint32_t matIdx)
        {
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(name, vec3(0), vec3(1),
                                                                   static_cast<uint32_t>(nodes.size()),
                                                                   modelIdx, matIdx));
        };

        models.push_back(Assets::FProcModel::CreateBox(vec3(-22.0f, -0.2f, -22.0f), vec3(22.0f, 0.0f, 22.0f)));
        addNode("Ground", static_cast<uint32_t>(models.size() - 1), matGround);

        // 8x8 grid of downward-facing light quads at y = 5 (normal = cross(right, up) = -y).
        const int gridN = 8;
        const float spacing = 5.0f;
        const float lightSize = 1.2f;
        for (int row = 0; row < gridN; ++row)
        {
            for (int col = 0; col < gridN; ++col)
            {
                const float x = (col - (gridN - 1) * 0.5f) * spacing - lightSize * 0.5f;
                const float z = (row - (gridN - 1) * 0.5f) * spacing - lightSize * 0.5f;
                const uint32_t matIdx = lightMatBase + (row * 3 + col) % 8;
                const size_t firstLight = lights.size();
                models.push_back(Assets::FProcModel::CreateAreaLight(
                    "ml_light_quad_" + std::to_string(row) + "_" + std::to_string(col),
                    vec3(x, 3.5f, z), vec3(lightSize, 0, 0), vec3(0, 0, lightSize),
                    matIdx, lights));
                addNode("Light_" + std::to_string(row) + "_" + std::to_string(col),
                        static_cast<uint32_t>(models.size() - 1), matIdx);
                nodes.back()->AddComponent(std::make_shared<Runtime::LightComponent>(lights.back()));
                lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstLight), lights.end());
            }
        }

        // Occluder pillars offset half a cell from the lights with varying heights: every
        // surface point sees a different subset of lights, which stresses light selection.
        for (int row = 0; row < gridN - 1; ++row)
        {
            for (int col = 0; col < gridN - 1; ++col)
            {
                const float x = (col - (gridN - 2) * 0.5f) * spacing;
                const float z = (row - (gridN - 2) * 0.5f) * spacing;
                const float h = 1.5f + float((row * 5 + col * 3) % 4);
                models.push_back(Assets::FProcModel::CreateBox(
                    vec3(x - 0.3f, 0.0f, z - 0.3f), vec3(x + 0.3f, h, z + 0.3f)));
                addNode("Pillar_" + std::to_string(row) + "_" + std::to_string(col),
                        static_cast<uint32_t>(models.size() - 1), matPillar);
            }
        }
    }

    float Hash01(uint32_t value)
    {
        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }

    float HashRange(uint32_t value, float minValue, float maxValue)
    {
        return glm::mix(minValue, maxValue, Hash01(value));
    }

    Assets::Model CreateFacetedAsteroid(uint32_t seed)
    {
        const float goldenRatio = (1.0f + std::sqrt(5.0f)) * 0.5f;
        std::vector<vec3> positions = {
            vec3(-1, goldenRatio, 0), vec3(1, goldenRatio, 0),
            vec3(-1, -goldenRatio, 0), vec3(1, -goldenRatio, 0),
            vec3(0, -1, goldenRatio), vec3(0, 1, goldenRatio),
            vec3(0, -1, -goldenRatio), vec3(0, 1, -goldenRatio),
            vec3(goldenRatio, 0, -1), vec3(goldenRatio, 0, 1),
            vec3(-goldenRatio, 0, -1), vec3(-goldenRatio, 0, 1),
        };
        for (vec3& position : positions)
        {
            position = glm::normalize(position);
        }

        std::vector<std::array<uint32_t, 3>> faces = {
            {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
            {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
            {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
            {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
        };

        std::unordered_map<uint64_t, uint32_t> midpointCache;
        auto midpoint = [&](uint32_t lhs, uint32_t rhs)
        {
            const uint32_t low = std::min(lhs, rhs);
            const uint32_t high = std::max(lhs, rhs);
            const uint64_t key = (static_cast<uint64_t>(low) << 32u) | high;
            if (const auto found = midpointCache.find(key); found != midpointCache.end())
            {
                return found->second;
            }
            const uint32_t index = static_cast<uint32_t>(positions.size());
            positions.push_back(glm::normalize(positions[lhs] + positions[rhs]));
            midpointCache.emplace(key, index);
            return index;
        };

        std::vector<std::array<uint32_t, 3>> subdividedFaces;
        subdividedFaces.reserve(faces.size() * 4);
        for (const auto& face : faces)
        {
            const uint32_t ab = midpoint(face[0], face[1]);
            const uint32_t bc = midpoint(face[1], face[2]);
            const uint32_t ca = midpoint(face[2], face[0]);
            subdividedFaces.push_back({face[0], ab, ca});
            subdividedFaces.push_back({face[1], bc, ab});
            subdividedFaces.push_back({face[2], ca, bc});
            subdividedFaces.push_back({ab, bc, ca});
        }
        faces = std::move(subdividedFaces);

        for (uint32_t index = 0; index < positions.size(); ++index)
        {
            positions[index] *= HashRange(seed * 131u + index * 17u, 0.84f, 1.16f);
        }

        std::vector<Assets::Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(faces.size() * 3);
        indices.reserve(faces.size() * 3);
        for (const auto& face : faces)
        {
            const vec3& a = positions[face[0]];
            const vec3& b = positions[face[1]];
            const vec3& c = positions[face[2]];
            const vec3 normal = glm::normalize(glm::cross(b - a, c - a));
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back({a, normal, vec4(1, 0, 0, 0), vec2(0, 0), 0});
            vertices.push_back({b, normal, vec4(1, 0, 0, 0), vec2(1, 0), 0});
            vertices.push_back({c, normal, vec4(1, 0, 0, 0), vec2(0.5f, 1), 0});
            indices.insert(indices.end(), {base, base + 1u, base + 2u});
        }
        return Assets::FProcModel::CreateFromBuffers(
            fmt::format("faceted_asteroid_{}", seed), std::move(vertices), std::move(indices), false);
    }

    void AsteroidBelt(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                      std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                      std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera camera;
        camera.name = "CinematicBelt";
        camera.ModelView = lookAt(vec3(0, 44, 182), vec3(112, -3, 258), vec3(0, 1, 0));
        camera.FieldOfView = 56;
        camera.Aperture = 0;
        camera.FocalDistance = 142;
        camera.FarPlane = 1400.0f;
        cameraInit.cameras.push_back(camera);

        Assets::Camera overviewCamera;
        overviewCamera.name = "BeltOverview";
        overviewCamera.ModelView = lookAt(vec3(420, 260, 520), vec3(0, 0, 0), vec3(0, 1, 0));
        overviewCamera.FieldOfView = 46;
        overviewCamera.Aperture = 0;
        overviewCamera.FocalDistance = 710;
        overviewCamera.FarPlane = 1600.0f;
        cameraInit.cameras.push_back(overviewCamera);
        cameraInit.ControlSpeed = 35.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SkyIntensity = 18.0f;
        cameraInit.SunIntensity = 900.0f;
        cameraInit.SunRotation = 0.72f;

        constexpr uint32_t modelCount = 24;
        constexpr uint32_t materialCount = 12000;
        constexpr uint32_t asteroidCount = 30000;
        for (uint32_t index = 0; index < modelCount; ++index)
        {
            models.push_back(CreateFacetedAsteroid(4100u + index));
        }
        for (uint32_t index = 0; index < materialCount; ++index)
        {
            const float hueBand = Hash01(index * 11u + 3u);
            const vec3 color = glm::mix(
                vec3(0.08f, 0.075f, 0.07f),
                hueBand > 0.84f ? vec3(0.34f, 0.20f, 0.10f) : vec3(0.48f, 0.46f, 0.42f),
                HashRange(index * 17u + 5u, 0.15f, 1.0f));
            const float roughness = HashRange(index * 23u + 7u, 0.08f, 0.95f);
            if ((index % 9u) == 0u)
            {
                materials.push_back({Material::Metallic(color, roughness), fmt::format("ab_metal_{:05}", index)});
            }
            else
            {
                materials.push_back({Material::Mixture(color, roughness), fmt::format("ab_rock_{:05}", index)});
            }
        }

        for (uint32_t index = 0; index < asteroidCount; ++index)
        {
            const float ringSelector = Hash01(index * 29u + 13u);
            const float ringBase = ringSelector < 0.45f ? 125.0f : (ringSelector < 0.82f ? 255.0f : 395.0f);
            const float radius = ringBase + HashRange(index * 31u + 17u, -48.0f, 48.0f);
            const float angle = HashRange(index * 37u + 19u, 0.0f, glm::two_pi<float>());
            const float vertical = HashRange(index * 41u + 23u, -1.0f, 1.0f);
            const vec3 position(
                std::cos(angle) * radius,
                vertical * vertical * vertical * 36.0f,
                std::sin(angle) * radius);
            const float size = HashRange(index * 43u + 29u, 0.55f, 3.6f);
            const vec3 scale(
                size * HashRange(index * 47u + 31u, 0.65f, 1.55f),
                size * HashRange(index * 53u + 37u, 0.65f, 1.45f),
                size * HashRange(index * 59u + 41u, 0.65f, 1.65f));
            const quat rotation = quat(vec3(
                HashRange(index * 61u + 43u, 0.0f, glm::two_pi<float>()),
                HashRange(index * 67u + 47u, 0.0f, glm::two_pi<float>()),
                HashRange(index * 71u + 53u, 0.0f, glm::two_pi<float>())));
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
                fmt::format("Asteroid_{:05}", index), position, scale, static_cast<uint32_t>(nodes.size()),
                index % modelCount, index % materialCount, true, rotation));
        }
    }

    float KilometerHeight(float x, float z)
    {
        return 13.0f * std::sin(x * 0.011f) * std::cos(z * 0.009f)
             + 7.0f * std::sin((x + z) * 0.021f)
             + 4.0f * std::cos((x - z) * 0.031f);
    }

    void KilometerWorld(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                        std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                        std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        auto addCamera = [&](const char* name, const vec3& eye, const vec3& target, float fov)
        {
            Assets::Camera camera;
            camera.name = name;
            camera.ModelView = lookAt(eye, target, vec3(0, 1, 0));
            camera.FieldOfView = fov;
            camera.Aperture = 0;
            camera.FocalDistance = glm::length(target - eye);
            camera.NearPlane = 0.2f;
            camera.FarPlane = 2000.0f;
            cameraInit.cameras.push_back(camera);
        };
        addCamera("Corner_Diagonal", vec3(-485, 22, -485), vec3(470, 45, 470), 55);
        addCamera("Ground_Center", vec3(0, 8, 440), vec3(0, 18, 0), 58);
        addCamera("Aerial_Overview", vec3(620, 620, 620), vec3(0, 0, 0), 52);
        cameraInit.ControlSpeed = 80.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SkyIntensity = 60.0f;
        cameraInit.SunIntensity = 850.0f;
        cameraInit.SunRotation = 0.34f;

        const std::array<vec3, 10> palette = {
            vec3(0.24f, 0.34f, 0.18f), vec3(0.35f, 0.43f, 0.21f), vec3(0.42f, 0.36f, 0.20f),
            vec3(0.20f, 0.24f, 0.18f), vec3(0.14f, 0.15f, 0.16f), vec3(0.56f, 0.55f, 0.50f),
            vec3(0.45f, 0.19f, 0.12f), vec3(0.18f, 0.30f, 0.48f), vec3(0.52f, 0.42f, 0.24f),
            vec3(0.75f, 0.68f, 0.42f),
        };
        const uint32_t materialBase = static_cast<uint32_t>(materials.size());
        for (uint32_t index = 0; index < palette.size(); ++index)
        {
            materials.push_back({Material::Lambertian(palette[index]), fmt::format("kw_material_{}", index)});
        }

        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f), vec3(0.5f)));
        const uint32_t cubeModel = static_cast<uint32_t>(models.size() - 1);
        auto addCube = [&](const std::string& name, const vec3& position, const vec3& scale, uint32_t material)
        {
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
                name, position, scale, static_cast<uint32_t>(nodes.size()), cubeModel, materialBase + material));
        };

        constexpr int terrainCells = 40;
        constexpr float tileSize = 25.0f;
        for (int row = 0; row < terrainCells; ++row)
        {
            for (int col = 0; col < terrainCells; ++col)
            {
                const float x = -500.0f + (static_cast<float>(col) + 0.5f) * tileSize;
                const float z = -500.0f + (static_cast<float>(row) + 0.5f) * tileSize;
                const float top = KilometerHeight(x, z);
                const float depth = top + 30.0f;
                const bool road = (row % 8 == 3) || (col % 8 == 3);
                const uint32_t material = road ? 4u : static_cast<uint32_t>((row * 3 + col * 5) % 4);
                addCube(fmt::format("Terrain_{:02}_{:02}", row, col),
                        vec3(x, top - depth * 0.5f, z), vec3(tileSize, depth, tileSize), material);
            }
        }

        for (int row = -10; row <= 10; ++row)
        {
            for (int col = -10; col <= 10; ++col)
            {
                if ((row % 4) == 0 || (col % 4) == 0)
                {
                    continue;
                }
                const float x = static_cast<float>(col) * 19.0f;
                const float z = static_cast<float>(row) * 19.0f;
                const float baseY = KilometerHeight(x, z);
                const float height = HashRange(static_cast<uint32_t>((row + 10) * 31 + col + 10), 12.0f, 72.0f);
                addCube(fmt::format("Downtown_{:+03}_{:+03}", row, col),
                        vec3(x, baseY + height * 0.5f, z),
                        vec3(HashRange(static_cast<uint32_t>(row * row + col + 200), 9.0f, 15.0f), height,
                             HashRange(static_cast<uint32_t>(col * col + row + 400), 9.0f, 15.0f)),
                        5u + static_cast<uint32_t>((row * row + col * col) % 4));
            }
        }

        for (int row = -5; row <= 5; ++row)
        {
            for (int col = -5; col <= 5; ++col)
            {
                const float x = static_cast<float>(col) * 100.0f;
                const float z = static_cast<float>(row) * 100.0f;
                const float baseY = KilometerHeight(x, z);
                const float height = ((row + col) & 1) == 0 ? 18.0f : 10.0f;
                addCube(fmt::format("DistanceMarker_{:+02}_{:+02}", row, col),
                        vec3(x, baseY + height * 0.5f, z), vec3(1.5f, height, 1.5f), 9);
            }
        }
        const float towerBase = KilometerHeight(340.0f, -330.0f);
        addCube("FarLandmarkTower", vec3(340, towerBase + 90, -330), vec3(18, 180, 18), 6);
        addCube("FarLandmarkCrown", vec3(340, towerBase + 185, -330), vec3(52, 10, 52), 9);
    }

    void TimeOfDayObservatory(Assets::EnvironmentSetting& cameraInit,
                              std::vector<std::shared_ptr<Assets::Node>>& nodes,
                              std::vector<Assets::Model>& models,
                              std::vector<Assets::FMaterial>& materials,
                              std::vector<Assets::LightObject>& lights,
                              std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera camera;
        camera.name = "ObservatoryCourtyard";
        camera.ModelView = lookAt(vec3(28, 15, 32), vec3(0, 4, 0), vec3(0, 1, 0));
        camera.FieldOfView = 52;
        camera.Aperture = 0;
        camera.FocalDistance = 42;
        cameraInit.cameras.push_back(camera);
        cameraInit.ControlSpeed = 8.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SkyIntensity = 35.0f;
        cameraInit.SunIntensity = 140.0f;
        cameraInit.SunRotation = -0.5f;
        cameraInit.SunElevation = glm::radians(6.0f);

        const std::array<Material, 8> sceneMaterials = {
            Material::Lambertian(vec3(0.56f, 0.52f, 0.43f)),
            Material::Lambertian(vec3(0.82f, 0.80f, 0.72f)),
            Material::Lambertian(vec3(0.52f, 0.13f, 0.08f)),
            Material::Lambertian(vec3(0.08f, 0.18f, 0.34f)),
            Material::Metallic(vec3(0.78f, 0.76f, 0.70f), 0.06f),
            Material::Dielectric(1.5f, 0.02f),
            Material::DiffuseLight(vec3(420.0f, 210.0f, 70.0f)),
            Material::Lambertian(vec3(0.08f, 0.08f, 0.07f)),
        };
        const uint32_t materialBase = static_cast<uint32_t>(materials.size());
        for (uint32_t index = 0; index < sceneMaterials.size(); ++index)
        {
            materials.push_back({sceneMaterials[index], fmt::format("tod_material_{}", index)});
        }
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f), vec3(0.5f)));
        const uint32_t cubeModel = static_cast<uint32_t>(models.size() - 1);
        models.push_back(Assets::FProcModel::CreateSphere(vec3(0), 1.0f));
        const uint32_t sphereModel = static_cast<uint32_t>(models.size() - 1);
        auto addNode = [&](const std::string& name, const vec3& position, const vec3& scale,
                           uint32_t model, uint32_t material, const quat& rotation = quat(1, 0, 0, 0))
        {
            nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
                name, position, scale, static_cast<uint32_t>(nodes.size()), model,
                materialBase + material, true, rotation));
        };

        addNode("Courtyard", vec3(0, -0.25f, 0), vec3(48, 0.5f, 48), cubeModel, 0);
        addNode("NorthWall", vec3(0, 3.5f, -24), vec3(48, 7, 1), cubeModel, 1);
        addNode("EastWall", vec3(24, 3.5f, 0), vec3(1, 7, 48), cubeModel, 2);
        addNode("WestWall", vec3(-24, 3.5f, 0), vec3(1, 7, 48), cubeModel, 3);
        addNode("SundialBase", vec3(0, 0.35f, 0), vec3(9, 0.7f, 9), cubeModel, 1);
        addNode("SundialGnomon", vec3(0, 5.2f, 0), vec3(0.45f, 10, 0.45f), cubeModel, 7,
                glm::angleAxis(glm::radians(-18.0f), vec3(1, 0, 0)));
        for (int index = 0; index < 12; ++index)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(index) / 12.0f;
            addNode(fmt::format("HourMarker_{:02}", index),
                    vec3(std::sin(angle) * 7.0f, 0.25f, std::cos(angle) * 7.0f),
                    vec3(0.35f, 0.5f, 1.4f), cubeModel, index % 2 == 0 ? 7u : 2u,
                    glm::angleAxis(angle, vec3(0, 1, 0)));
        }
        for (int side : {-1, 1})
        {
            for (int index = -2; index <= 2; ++index)
            {
                addNode(fmt::format("Colonnade_{}_{}", side, index),
                        vec3(static_cast<float>(side) * 14.0f, 3.0f, static_cast<float>(index) * 7.0f),
                        vec3(1.0f, 6.0f, 1.0f), cubeModel, 1);
            }
            addNode(fmt::format("ColonnadeBeam_{}", side), vec3(static_cast<float>(side) * 14.0f, 6.5f, 0),
                    vec3(1.4f, 1.0f, 36.0f), cubeModel, 1);
        }
        addNode("MetalReference", vec3(-7, 1.8f, 8), vec3(1.8f), sphereModel, 4);
        addNode("GlassReference", vec3(7, 1.8f, 8), vec3(1.8f), sphereModel, 5);

        for (int side : {-1, 1})
        {
            const float x = static_cast<float>(side) * 19.0f;
            for (int index = -2; index <= 2; ++index)
            {
                const float z = static_cast<float>(index) * 7.0f;
                const size_t firstLight = lights.size();
                models.push_back(Assets::FProcModel::CreateAreaLight(
                    fmt::format("NightLampMesh_{}_{}", side, index),
                    vec3(x - 0.4f, 4.8f, z - 0.4f), vec3(0.8f, 0, 0), vec3(0, 0, 0.8f),
                    materialBase + 6u, lights));
                addNode(fmt::format("NightLamp_{}_{}", side, index), vec3(0), vec3(1),
                        static_cast<uint32_t>(models.size() - 1), 6);
                nodes.back()->AddComponent(std::make_shared<Runtime::LightComponent>(lights.back()));
                lights.erase(lights.begin() + static_cast<std::ptrdiff_t>(firstLight), lights.end());
            }
        }

        Assets::AnimationTrack timeOfDay;
        timeOfDay.AnimationName = "TimeOfDay";
        timeOfDay.Target_ = Assets::AnimationTrack::Target::Environment;
        timeOfDay.Duration_ = 120.0f;
        timeOfDay.Play();
        const std::array<float, 5> times = {0, 30, 60, 90, 120};
        const std::array<float, 5> rotations = {-0.5f, 0.0f, 0.5f, 1.0f, 1.5f};
        const std::array<float, 5> elevations = {
            glm::radians(6.0f), glm::radians(68.0f), glm::radians(6.0f),
            glm::radians(-18.0f), glm::radians(6.0f),
        };
        const std::array<float, 5> sunIntensity = {140, 950, 120, 0, 140};
        const std::array<float, 5> skyIntensity = {35, 95, 28, 3, 35};
        for (uint32_t index = 0; index < times.size(); ++index)
        {
            timeOfDay.SunRotationChannel.Keys.push_back({times[index], rotations[index]});
            timeOfDay.SunElevationChannel.Keys.push_back({times[index], elevations[index]});
            timeOfDay.SkyRotationChannel.Keys.push_back({times[index], rotations[index] * 0.5f});
            timeOfDay.SunIntensityChannel.Keys.push_back({times[index], sunIntensity[index]});
            timeOfDay.SkyIntensityChannel.Keys.push_back({times[index], skyIntensity[index]});
        }
        tracks.push_back(std::move(timeOfDay));
    }

    void KineticWave(Assets::EnvironmentSetting& cameraInit, std::vector<std::shared_ptr<Assets::Node>>& nodes,
                     std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials,
                     std::vector<Assets::LightObject>& lights, std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera camera;
        camera.name = "WaveOverview";
        camera.ModelView = lookAt(vec3(48, 38, 58), vec3(0, 2, 0), vec3(0, 1, 0));
        camera.FieldOfView = 52;
        camera.Aperture = 0;
        camera.FocalDistance = 84;
        cameraInit.cameras.push_back(camera);
        cameraInit.ControlSpeed = 12.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SkyIntensity = 45.0f;
        cameraInit.SunIntensity = 650.0f;

        const uint32_t materialBase = static_cast<uint32_t>(materials.size());
        const std::array<vec3, 4> colors = {
            vec3(0.08f, 0.32f, 0.74f), vec3(0.12f, 0.66f, 0.48f),
            vec3(0.88f, 0.48f, 0.08f), vec3(0.70f, 0.12f, 0.22f),
        };
        for (uint32_t index = 0; index < colors.size(); ++index)
        {
            materials.push_back({Material::Mixture(colors[index], 0.24f), fmt::format("kwave_{}", index)});
        }
        materials.push_back({Material::Lambertian(vec3(0.12f)), "kwave_ground"});
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f), vec3(0.5f)));
        const uint32_t cubeModel = static_cast<uint32_t>(models.size() - 1);
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "KineticGround", vec3(0, -0.5f, 0), vec3(70, 1, 70), static_cast<uint32_t>(nodes.size()),
            cubeModel, materialBase + 4u));

        constexpr int gridSize = 32;
        constexpr float spacing = 2.0f;
        for (int row = 0; row < gridSize; ++row)
        {
            for (int col = 0; col < gridSize; ++col)
            {
                const float x = (static_cast<float>(col) - 15.5f) * spacing;
                const float z = (static_cast<float>(row) - 15.5f) * spacing;
                const float phase = glm::length(vec2(x, z)) * 0.32f + std::atan2(z, x) * 2.0f;
                const std::string name = fmt::format("Wave_{:02}_{:02}", row, col);
                nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
                    name, vec3(x, 2.5f, z), vec3(1.3f, 4.0f, 1.3f), static_cast<uint32_t>(nodes.size()),
                    cubeModel, materialBase + static_cast<uint32_t>((row + col) & 3)));

                Assets::AnimationTrack track;
                track.AnimationName = "KineticWave";
                track.NodeName_ = name;
                track.Duration_ = 8.0f;
                track.Play();
                for (int key = 0; key <= 8; ++key)
                {
                    const float time = static_cast<float>(key);
                    const float wave = std::sin(phase + glm::two_pi<float>() * time / 8.0f);
                    track.TranslationChannel.Keys.push_back({time, vec3(x, 2.5f + wave * 2.2f, z)});
                    track.RotationChannel.Keys.push_back({
                        time, glm::angleAxis(wave * 0.42f, glm::normalize(vec3(1, 0.2f, 1)))});
                }
                tracks.push_back(std::move(track));
            }
        }
    }

    void RigidBodyAvalanche(Assets::EnvironmentSetting& cameraInit,
                            std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks)
    {
        Assets::Camera camera;
        camera.name = "AvalancheOverview";
        camera.ModelView = lookAt(vec3(28, 24, 42), vec3(0, 8, 0), vec3(0, 1, 0));
        camera.FieldOfView = 55;
        camera.Aperture = 0;
        camera.FocalDistance = 50;
        cameraInit.cameras.push_back(camera);
        cameraInit.ControlSpeed = 10.0f;
        cameraInit.GammaCorrection = true;
        cameraInit.HasSky = true;
        cameraInit.HasSun = true;
        cameraInit.SkyIntensity = 48.0f;
        cameraInit.SunIntensity = 720.0f;

        const uint32_t materialBase = static_cast<uint32_t>(materials.size());
        const std::array<vec3, 6> colors = {
            vec3(0.75f, 0.12f, 0.10f), vec3(0.10f, 0.30f, 0.78f), vec3(0.12f, 0.62f, 0.24f),
            vec3(0.88f, 0.58f, 0.08f), vec3(0.58f, 0.12f, 0.68f), vec3(0.10f, 0.68f, 0.68f),
        };
        for (uint32_t index = 0; index < colors.size(); ++index)
        {
            materials.push_back({Material::Mixture(colors[index], 0.35f), fmt::format("avalanche_{}", index)});
        }
        materials.push_back({Material::Lambertian(vec3(0.35f, 0.32f, 0.27f)), "avalanche_structure"});
        models.push_back(Assets::FProcModel::CreateBox(vec3(-0.5f), vec3(0.5f)));
        const uint32_t cubeModel = static_cast<uint32_t>(models.size() - 1);
        auto addPhysicalBox = [&](const std::string& name, const vec3& position, const vec3& extent,
                                  const quat& rotation, uint32_t material, NextMotionType motionType)
        {
            auto node = Assets::SceneBuilder::CreateRenderNode(
                name, position, extent, static_cast<uint32_t>(nodes.size()), cubeModel,
                materialBase + material, true, rotation);
            AttachBoxPhysics(node, position, rotation, extent, motionType);
            nodes.push_back(std::move(node));
        };

        addPhysicalBox("AvalancheFloor", vec3(0, -0.75f, 0), vec3(42, 1.5f, 42),
                       quat(1, 0, 0, 0), 6, NextMotionType::Static);
        addPhysicalBox("AvalancheRampLeft", vec3(-8, 8, -4), vec3(18, 1, 13),
                       glm::angleAxis(glm::radians(-24.0f), vec3(0, 0, 1)), 6, NextMotionType::Static);
        addPhysicalBox("AvalancheRampRight", vec3(8, 8, 4), vec3(18, 1, 13),
                       glm::angleAxis(glm::radians(24.0f), vec3(0, 0, 1)), 6, NextMotionType::Static);
        addPhysicalBox("AvalancheDivider", vec3(0, 3, 0), vec3(1, 6, 30),
                       quat(1, 0, 0, 0), 6, NextMotionType::Static);

        constexpr int columns = 16;
        constexpr int rows = 12;
        constexpr int layers = 3;
        for (int layer = 0; layer < layers; ++layer)
        {
            for (int row = 0; row < rows; ++row)
            {
                for (int col = 0; col < columns; ++col)
                {
                    const uint32_t seed = static_cast<uint32_t>(layer * rows * columns + row * columns + col);
                    const vec3 extent(
                        HashRange(seed * 17u + 1u, 0.65f, 1.15f),
                        HashRange(seed * 19u + 3u, 0.65f, 1.15f),
                        HashRange(seed * 23u + 5u, 0.65f, 1.15f));
                    const vec3 position(
                        (static_cast<float>(col) - 7.5f) * 1.2f,
                        18.0f + static_cast<float>(layer) * 2.0f + static_cast<float>(row) * 1.15f,
                        (static_cast<float>(row) - 5.5f) * 1.15f);
                    const quat rotation = quat(vec3(
                        HashRange(seed * 29u, -0.15f, 0.15f),
                        HashRange(seed * 31u, -glm::pi<float>(), glm::pi<float>()),
                        HashRange(seed * 37u, -0.15f, 0.15f)));
                    addPhysicalBox(fmt::format("Avalanche_{:03}", seed), position, extent, rotation,
                                   seed % 6u, NextMotionType::Dynamic);
                }
            }
        }
    }
}

namespace AppCommon
{
    void RegisterDemoScenes()
    {
        auto& registry = Assets::FLoaderRegistry::Get();
        registry.RegisterProcScene("CornellBox.proc", CornellBox);
        registry.RegisterProcScene("ManyLightsShowcase.proc", ManyLightsShowcase);
        registry.RegisterProcScene("GIBootcamp.proc", GIBootcamp);
        registry.RegisterProcScene("MaterialShowcase.proc", MaterialShowcase);
        registry.RegisterProcScene("LightingShowcase.proc", LightingShowcase);
        registry.RegisterProcScene("CameraShowcase.proc", CameraShowcase);
        registry.RegisterProcScene("AnimationShowcase.proc", AnimationShowcase);
        registry.RegisterProcScene("PhysicsShowcase.proc", PhysicsShowcase);
        registry.RegisterProcScene("AsteroidBelt.proc", AsteroidBelt);
        registry.RegisterProcScene("KilometerWorld.proc", KilometerWorld);
        registry.RegisterProcScene("TimeOfDayObservatory.proc", TimeOfDayObservatory);
        registry.RegisterProcScene("KineticWave.proc", KineticWave);
        registry.RegisterProcScene("RigidBodyAvalanche.proc", RigidBodyAvalanche);
        registry.RegisterProcScene("RTIO.proc", RayTracingInOneWeekend);
    }
}
