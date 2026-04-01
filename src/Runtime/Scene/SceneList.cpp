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
        materials.push_back({Material::Mixture(vec3(0.5f, 0.5f, 0.5f), 0.5f), "ground"});         // 0: ground grey
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

        const float stairStepHeight = 0.15f;
        const float stairStepDepth = 1.0f;
        const float landingDepth = 1.2f;
        const float landingThickness = 0.08f;
        const float rampHalfWidth = 2.0f;
        const float rampHalfLength = 4.0f;
        const float rampThickness = 0.18f;
        const float wallThickness = 1.0f;
        const float wallHeight = 12.0f;
        const float roomWallThickness = 0.2f;
        const float surfaceSink = 0.01f;
        const float landingOverlap = 0.08f;

        // -- A thin ramp model with its walkable surface on local y = 0 --
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-rampHalfWidth, -rampThickness, -rampHalfLength),
            vec3(rampHalfWidth, 0.0f, rampHalfLength)));
        uint32_t rampBoxId = static_cast<uint32_t>(models.size() - 1);

        // -- Stair step model, half the previous step height --
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-2.0f, -stairStepHeight, -0.5f),
            vec3(2.0f, 0.0f, 0.5f)));
        uint32_t stairStepId = static_cast<uint32_t>(models.size() - 1);

        // -- Landing platform to bridge ramps / stairs with the floor --
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-2.0f, -landingThickness, -landingDepth * 0.5f),
            vec3(2.0f, 0.0f, landingDepth * 0.5f)));
        uint32_t landingId = static_cast<uint32_t>(models.size() - 1);

        // -- Perimeter wall models to keep bullets and physics objects inside the test area --
        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-(groundHalfSize + wallThickness), -0.25f, -wallThickness * 0.5f),
            vec3(groundHalfSize + wallThickness, wallHeight, wallThickness * 0.5f)));
        uint32_t wallLongId = static_cast<uint32_t>(models.size() - 1);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-wallThickness * 0.5f, -0.25f, -(groundHalfSize + wallThickness)),
            vec3(wallThickness * 0.5f, wallHeight, groundHalfSize + wallThickness)));
        uint32_t wallWideId = static_cast<uint32_t>(models.size() - 1);

        models.push_back(Assets::FProcModel::CreateBox(
            vec3(-0.5f, 0.0f, -roomWallThickness * 0.5f),
            vec3(0.5f, 1.0f, roomWallThickness * 0.5f)));
        uint32_t roomWallPanelId = static_cast<uint32_t>(models.size() - 1);

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

        std::uniform_real_distribution<float> dynamicPosXDist(-22.0f, 22.0f);
        std::uniform_real_distribution<float> dynamicPosZDist(8.0f, 28.0f);

        constexpr int numDynamicBoxes = 18;
        for (int i = 0; i < numDynamicBoxes; ++i)
        {
            const float x = dynamicPosXDist(rng);
            const float z = dynamicPosZDist(rng);
            const uint32_t matId = matBase + static_cast<uint32_t>(matDist(rng));
            const float yaw = yawDist(rng);

            auto node = Assets::Node::CreateNode(
                "DynamicObstacle_" + std::to_string(i),
                vec3(x, 0, z),
                glm::angleAxis(yaw, vec3(0, 1, 0)),
                vec3(1),
                static_cast<uint32_t>(nodes.size()));

            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(smallBoxId);
            rc->SetVisible(true);
            rc->SetMaterial({matId});
            node->AddComponent(rc);

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

            nodes.push_back(node);
        }

        auto addScaledStaticNode =
            [&](const std::string& name, const vec3& position, const quat& rotation, const vec3& scale, uint32_t modelId, uint32_t materialId)
        {
            auto node = Assets::Node::CreateNode(
                name,
                position,
                rotation,
                scale,
                static_cast<uint32_t>(nodes.size()));

            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(modelId);
            rc->SetVisible(true);
            rc->SetMaterial({materialId});
            node->AddComponent(rc);

            nodes.push_back(node);
        };

        auto addStaticNode =
            [&](const std::string& name, const vec3& position, const quat& rotation, uint32_t modelId, uint32_t materialId)
        {
            addScaledStaticNode(name, position, rotation, vec3(1), modelId, materialId);
        };

        auto addWallPanel =
            [&](const std::string& name, const vec3& position, const quat& rotation, float length, float height, uint32_t materialId)
        {
            addScaledStaticNode(name,
                                position,
                                rotation,
                                vec3(length, height, 1.0f),
                                roomWallPanelId,
                                materialId);
        };

        auto addWindowWall =
            [&](const std::string& prefix, const vec3& center, const quat& rotation, float wallLength, float localBottomY,
                float totalHeight, float windowWidth, float sillHeight, float windowHeight, uint32_t materialId)
        {
            addWallPanel(prefix + "_Lower",
                         center + rotation * vec3(0.0f, localBottomY, 0.0f),
                         rotation,
                         wallLength,
                         sillHeight,
                         materialId);

            const float upperHeight = totalHeight - sillHeight - windowHeight;
            if (upperHeight > 0.05f)
            {
                addWallPanel(prefix + "_Upper",
                             center + rotation * vec3(0.0f, localBottomY + sillHeight + windowHeight, 0.0f),
                             rotation,
                             wallLength,
                             upperHeight,
                             materialId);
            }

            const float sideLength = (wallLength - windowWidth) * 0.5f;
            if (sideLength > 0.05f)
            {
                addWallPanel(prefix + "_SideL",
                             center + rotation * vec3(-(windowWidth * 0.5f + sideLength * 0.5f), localBottomY + sillHeight, 0.0f),
                             rotation,
                             sideLength,
                             windowHeight,
                             materialId);
                addWallPanel(prefix + "_SideR",
                             center + rotation * vec3(windowWidth * 0.5f + sideLength * 0.5f, localBottomY + sillHeight, 0.0f),
                             rotation,
                             sideLength,
                             windowHeight,
                             materialId);
            }
        };

        auto addDoorWall =
            [&](const std::string& prefix, const vec3& center, const quat& rotation, float wallLength, float localBottomY,
                float totalHeight, float doorWidth, float doorHeight, uint32_t materialId)
        {
            const float sideLength = (wallLength - doorWidth) * 0.5f;
            if (sideLength > 0.05f)
            {
                addWallPanel(prefix + "_SideL",
                             center + rotation * vec3(-(doorWidth * 0.5f + sideLength * 0.5f), localBottomY, 0.0f),
                             rotation,
                             sideLength,
                             doorHeight,
                             materialId);
                addWallPanel(prefix + "_SideR",
                             center + rotation * vec3(doorWidth * 0.5f + sideLength * 0.5f, localBottomY, 0.0f),
                             rotation,
                             sideLength,
                             doorHeight,
                             materialId);
            }

            const float headerHeight = totalHeight - doorHeight;
            if (headerHeight > 0.05f)
            {
                addWallPanel(prefix + "_Header",
                             center + rotation * vec3(0.0f, localBottomY + doorHeight, 0.0f),
                             rotation,
                             doorWidth,
                             headerHeight,
                             materialId);
            }
        };

        auto addSimpleRoom =
            [&](const std::string& prefix, const vec3& center, float width, float depth, float roomHeight, uint32_t materialId)
        {
            const float halfWidth = width * 0.5f;
            const float halfDepth = depth * 0.5f;
            const float panelOverlap = roomWallThickness;
            const float doorWidth = glm::min(1.6f, width - 1.0f);
            const float doorHeight = glm::min(2.3f, roomHeight - 0.3f);
            const float windowWidth = glm::min(2.2f, width - 1.2f);
            const float sideWindowWidth = glm::min(1.8f, depth - 1.2f);
            const float sillHeight = 1.0f;
            const float windowHeight = glm::min(1.1f, roomHeight - sillHeight - 0.3f);
            const float baseY = -surfaceSink;

            addDoorWall(prefix + "_Front",
                        center + vec3(0.0f, 0.0f, -halfDepth),
                        quat(1, 0, 0, 0),
                        width + panelOverlap,
                        baseY,
                        roomHeight,
                        doorWidth,
                        doorHeight,
                        materialId);

            addWindowWall(prefix + "_Back",
                          center + vec3(0.0f, 0.0f, halfDepth),
                          quat(1, 0, 0, 0),
                          width + panelOverlap,
                          baseY,
                          roomHeight,
                          windowWidth,
                          sillHeight,
                          windowHeight,
                          materialId);

            addWindowWall(prefix + "_Left",
                          center + vec3(-halfWidth, 0.0f, 0.0f),
                          glm::angleAxis(glm::half_pi<float>(), vec3(0, 1, 0)),
                          depth + panelOverlap,
                          baseY,
                          roomHeight,
                          sideWindowWidth,
                          sillHeight,
                          windowHeight,
                          materialId);

            addWallPanel(prefix + "_Right",
                         center + vec3(halfWidth, baseY, 0.0f),
                         glm::angleAxis(glm::half_pi<float>(), vec3(0, 1, 0)),
                         depth + panelOverlap,
                         roomHeight,
                         materialId);
        };

        auto addStairFlight =
            [&](const std::string& prefix, const vec3& startPosition, float yawDegrees, int stepCount, uint32_t materialId)
        {
            const quat rotation = glm::angleAxis(glm::radians(yawDegrees), vec3(0, 1, 0));
            const float topHeight = stairStepHeight * static_cast<float>(stepCount);

            addStaticNode(prefix + "_LandingLow",
                          startPosition + rotation * vec3(0.0f, -surfaceSink, -landingDepth * 0.5f),
                          rotation,
                          landingId,
                          matBase + 0);

            for (int i = 0; i < stepCount; ++i)
            {
                const float stepTop = stairStepHeight * static_cast<float>(i + 1) - surfaceSink;
                const float stepForward = static_cast<float>(i) * stairStepDepth;
                addStaticNode(prefix + "_Stair_" + std::to_string(i),
                              startPosition + rotation * vec3(0.0f, stepTop, stepForward),
                              rotation,
                              stairStepId,
                              materialId);
            }

            addStaticNode(prefix + "_LandingHigh",
                          startPosition + rotation * vec3(0.0f, topHeight - surfaceSink, stepCount * stairStepDepth),
                          rotation,
                          landingId,
                          matBase + 0);
        };

        auto addRampSet =
            [&](const std::string& prefix, const vec3& rampCenterXZ, float angleDegrees, uint32_t materialId)
        {
            const float angleRadians = glm::radians(angleDegrees);
            const float halfHeightGain = rampHalfLength * std::sin(angleRadians);
            const float totalHeightGain = halfHeightGain * 2.0f;
            const float halfRun = rampHalfLength * std::cos(angleRadians);
            const float landingHalfDepth = landingDepth * 0.5f;
            const quat rotation = glm::angleAxis(angleRadians, vec3(1, 0, 0));

            addStaticNode(prefix + "_Ramp",
                          vec3(rampCenterXZ.x, halfHeightGain - surfaceSink, rampCenterXZ.z),
                          rotation,
                          rampBoxId,
                          materialId);

            addStaticNode(prefix + "_LandingLow",
                          vec3(rampCenterXZ.x,
                               -surfaceSink,
                               rampCenterXZ.z + halfRun + landingHalfDepth - landingOverlap),
                          quat(1, 0, 0, 0),
                          landingId,
                          matBase + 0);

            addStaticNode(prefix + "_LandingHigh",
                          vec3(rampCenterXZ.x,
                               totalHeightGain - surfaceSink,
                               rampCenterXZ.z - halfRun - landingHalfDepth + landingOverlap),
                          quat(1, 0, 0, 0),
                          landingId,
                          matBase + 0);
        };

        addStaticNode("Wall_North", vec3(0.0f, 0.0f, -(groundHalfSize + wallThickness * 0.5f)), quat(1, 0, 0, 0), wallLongId, matBase + 5);
        addStaticNode("Wall_South", vec3(0.0f, 0.0f, groundHalfSize + wallThickness * 0.5f), quat(1, 0, 0, 0), wallLongId, matBase + 5);
        addStaticNode("Wall_West", vec3(-(groundHalfSize + wallThickness * 0.5f), 0.0f, 0.0f), quat(1, 0, 0, 0), wallWideId, matBase + 5);
        addStaticNode("Wall_East", vec3(groundHalfSize + wallThickness * 0.5f, 0.0f, 0.0f), quat(1, 0, 0, 0), wallWideId, matBase + 5);

        // -- Open-top room shells for traversal and projectile testing --
        addSimpleRoom("Room_A", vec3(30.0f, 0.0f, 18.0f), 8.0f, 6.0f, 3.2f, matBase + 1);
        addSimpleRoom("Room_B", vec3(-30.0f, 0.0f, 18.0f), 7.0f, 7.0f, 3.0f, matBase + 2);
        addSimpleRoom("Room_C", vec3(0.0f, 0.0f, 32.0f), 9.0f, 6.5f, 3.4f, matBase + 3);

        // -- Multiple ramps for testing different slope angles --
        addRampSet("Ramp_Gentle", vec3(6.0f, 0.0f, -6.0f), 8.0f, matBase + 4);
        addRampSet("Ramp_Medium", vec3(12.0f, 0.0f, -6.5f), 14.0f, matBase + 5);
        addRampSet("Ramp_Steep", vec3(18.0f, 0.0f, -7.0f), 20.0f, matBase + 3);
        addRampSet("Ramp_Sharp", vec3(24.0f, 0.0f, -7.5f), 26.0f, matBase + 1);

        // -- Multiple stair runs with lower steps --
        addStairFlight("StairRun_A", vec3(-6.0f, 0.0f, -11.0f), 0.0f, 8, matBase + 2);
        addStairFlight("StairRun_B", vec3(-20.0f, 0.0f, -6.0f), 90.0f, 8, matBase + 3);
        addStairFlight("StairRun_C", vec3(-10.0f, 0.0f, -20.0f), 180.0f, 8, matBase + 1);
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
