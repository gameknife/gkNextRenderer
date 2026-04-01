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

        // Barriers and pillars
        int barrierTall = loader.LoadPiece("barrier_4x1x4", "green", models, materials);
        int barrierLow  = loader.LoadPiece("barrier_2x1x2", "green", models, materials);
        int pillarTall  = loader.LoadPiece("pillar_1x1x4", "neutral", models, materials);
        int pillarShort = loader.LoadPiece("pillar_2x2x2", "neutral", models, materials);

        // Accent color platforms
        int platBlueMed = loader.LoadPiece("platform_2x2x1", "blue", models, materials);
        int platBlue    = loader.LoadPiece("platform_4x4x1", "blue", models, materials);
        int platBlueWide = loader.LoadPiece("platform_4x2x1", "blue", models, materials);
        int platBlueLong = loader.LoadPiece("platform_6x2x1", "blue", models, materials);
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
        int archGreen   = loader.LoadPiece("arch", "green", models, materials);
        int archWide    = loader.LoadPiece("arch_wide", "green", models, materials);
        int spring      = loader.LoadPiece("spring", "neutral", models, materials);
        int springPad   = loader.LoadPiece("spring_pad", "green", models, materials);
        int hoop        = loader.LoadPiece("hoop", "red", models, materials);
        int hoopAngled  = loader.LoadPiece("hoop_angled", "red", models, materials);
        int buttonBase  = loader.LoadPiece("button_base", "yellow", models, materials);
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

        auto placeDynamic = [&](const std::string& name, int pieceIdx, const vec3& pos,
                                const quat& rot = quat(1, 0, 0, 0))
        {
            if (pieceIdx < 0) return;
            const auto& piece = loader.GetPiece(pieceIdx);
            auto node = Assets::Node::CreateNode(name, pos, rot, vec3(1), static_cast<uint32_t>(nodes.size()));
            auto rc = std::make_shared<Runtime::RenderComponent>();
            rc->SetModelId(piece.modelId);
            rc->SetVisible(true);
            rc->SetMaterial({piece.materialId});
            node->AddComponent(rc);

            auto phys = std::make_shared<Runtime::PhysicsComponent>();
            phys->SetMobility(Runtime::ENodeMobility::Dynamic);
            glm::vec3 offset = loader.GetCollisionOffset(pieceIdx);
            glm::vec3 extent = loader.GetCollisionExtent(pieceIdx);
            phys->SetPhysicsOffset(offset);
            NextBodyID bodyId = NextEngine::GetInstance()->GetPhysicsEngine()->CreateBoxBody(
                pos + rot * offset, rot, extent, NextMotionType::Dynamic);
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
        // LEVEL LAYOUT - Single reviewed prefab
        // ====================================================================

        placePiece("Spawn_Platform", platHuge, vec3(0, 0, 0));

        const std::vector<PrefabPiecePlacement> scaffoldPrefab{
            {"LeftTowerLower", structureC, vec3(-3.9f, 0.0f, 0.0f)},
            {"LeftTowerUpper", structureC, vec3(-3.9f, 1.9f, 0.0f)},
            {"RightTowerLower", structureC, vec3(3.9f, 0.0f, 0.0f)},
            {"RightTowerUpper", structureC, vec3(3.9f, 1.9f, 0.0f)},
            {"TopPadLeft", floorWoodSquare, vec3(-3.9f, 3.88f, 0.0f), quat(1, 0, 0, 0)},
            {"TopPadRight", floorWoodSquare, vec3(3.9f, 3.88f, 0.0f), quat(1, 0, 0, 0)},
            {"TopBridge", floorWoodLong, vec3(0.0f, 3.88f, 0.0f), quat(1, 0, 0, 0)},
        };

        placePrefab("Scaffold", vec3(0, 0, 14), quat(1, 0, 0, 0), scaffoldPrefab);
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
