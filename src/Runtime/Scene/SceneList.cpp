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
        if (filename == "CharacterPlayground.proc")
        {
            CharacterPlayground(camera, nodes, models, materials, lights, tracks);
            return true;
        }
        return false;
    }

    return false;
}
