#include "Engine/Runtime/GameInstance.hpp"
#include "MagicaLegoGameInstance.hpp"
#include "MagicaLegoCommands.hpp"
#include "MagicaLegoConstants.hpp"
#include "MagicaLegoPlacementRules.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "MagicaLegoUserInterface.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <system_error>

const glm::i16vec3 invalidPos(0, -10, 0);
constexpr uint32_t InvalidOwnerHash = std::numeric_limits<uint32_t>::max();

enum class EHitFace
{
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
    PositiveZ,
    NegativeZ
};

EHitFace GetHitFaceFromNormal(const glm::vec3& normal)
{
    const glm::vec3 absNormal = glm::abs(normal);
    if (absNormal.y >= absNormal.x && absNormal.y >= absNormal.z)
    {
        return normal.y >= 0.0f ? EHitFace::PositiveY : EHitFace::NegativeY;
    }
    if (absNormal.x >= absNormal.z)
    {
        return normal.x >= 0.0f ? EHitFace::PositiveX : EHitFace::NegativeX;
    }
    return normal.z >= 0.0f ? EHitFace::PositiveZ : EHitFace::NegativeZ;
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    return std::make_unique<MagicaLegoGameInstance>(config, options, engine);
}

glm::vec3 GetRenderLocationFromBlockLocation(glm::i16vec3 blockLocation)
{
    using namespace MagicaLego::Grid;
    return glm::vec3(
        static_cast<float>(blockLocation.x) * UnitX,
        static_cast<float>(blockLocation.y) * UnitY,
        static_cast<float>(blockLocation.z) * UnitZ
    );
}

glm::i16vec3 GetBlockLocationFromRenderLocation(glm::vec3 renderLocation)
{
    using namespace MagicaLego::Grid;
    return glm::i16vec3(
        static_cast<int16_t>(round(renderLocation.x / UnitX)),
        static_cast<int16_t>(round((renderLocation.y - HeightOffset) / UnitY)),
        static_cast<int16_t>(round(renderLocation.z / UnitZ))
    );
}

uint32_t GetHashFromBlockLocation(const glm::i16vec3& blockLocation)
{
    uint32_t x = static_cast<uint32_t>(blockLocation.x) & 0xFFFF;
    uint32_t y = static_cast<uint32_t>(blockLocation.y) & 0xFFFF;
    uint32_t z = static_cast<uint32_t>(blockLocation.z) & 0xFFFF;

    uint32_t hash = x;
    hash = hash * 31 + y;
    hash = hash * 31 + z;

    return hash;
}

MagicaLegoGameInstance::MagicaLegoGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    NextRenderer::HideConsole();

    // windows config
    config.Title = "MagicaLego";
    config.Height = 960;
    config.Width = 1920;
    config.ForceSDR = true;
    config.HideTitleBar = true;

    // options
    // options.SceneName = "legobricks.glb";
    options.ForceSDR = true;
    options.locale = "zhCN";
    //options.SuperResolution = 0;
    // Select r.upscaler.type=1 to prefer DLSS when supported.

    // mode init
    SetBuildMode(ELegoMode::ELM_Place);

    // control init
    resetMouse_ = true;
    cameraRotX_ = 45;
    cameraRotY_ = 30;
    cameraArm_ = 5.0;
    cameraFOV_ = 12.f;

    // camera focus init
    focusTarget_ = glm::vec3(0, 0, 0);
    cameraCenter_ = focusTarget_;
    isOrbitDragging_ = false;
    mouseRightPressed_ = false;
    isTracingObject_ = false;

    // ui
    UserInterface_ = std::make_unique<MagicaLegoUserInterface>(this);

    lastSelectLocation_ = invalidPos;
    lastPlacedLocation_ = invalidPos;

    GetEngine().GetPakSystem().SetRunMode(Utilities::Package::EPM_PakFile);
    // Do not Reset() the pak system here: that would unmount optional.pak which the engine
    // mounted at startup and which still provides legobricks.glb when lego.pak hasn't been built.
    GetEngine().GetPakSystem().MountPak(Utilities::FileHelper::GetPlatformFilePath("assets/paks/lego.pak"));
    GetEngine().GetPakSystem().MountPak(Utilities::FileHelper::GetPlatformFilePath("assets/paks/thumbs.pak"));
    {
        const std::string magicalegoPakPath = Utilities::FileHelper::GetPlatformFilePath("assets/paks/magicalego.pak");
        std::error_code ec;
        if (std::filesystem::exists(magicalegoPakPath, ec))
        {
            GetEngine().GetPakSystem().MountPak(magicalegoPakPath);
        }
    }

    // Initialize cursor
    cursor_ = std::make_unique<MagicaLego::FCursor>();
}

void MagicaLegoGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "16", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "8", &error);
    cvars.SetDefaultFromString("r.rendererType", "0", &error);
    cvars.SetDefaultFromString("r.upscaler.type", "1", &error);
}

bool MagicaLegoGameInstance::ResolvePlacementLocationFromRay(const Assets::RayCastResult& rayResult, glm::i16vec3& outBlockLocation, std::string* reason)
{
    glm::vec3 hitPoint = glm::vec3(rayResult.HitPoint);
    glm::vec3 hitNormal = glm::vec3(rayResult.Normal);
    if (glm::dot(hitNormal, hitNormal) > 0.0f)
    {
        hitNormal = glm::normalize(hitNormal);
    }

    outBlockLocation = GetBlockLocationFromRenderLocation(hitPoint + hitNormal * 0.005f);

    auto* hitNode = GetEngine().GetScene().GetNodeByInstanceId(rayResult.InstanceId);
    if (hitNode == nullptr || hitNode->GetName() != "blockInst")
    {
        return true;
    }

    glm::i16vec3 hitAnchor = GetBlockLocationFromRenderLocation(
        glm::vec3(hitNode->WorldTransform() * glm::vec4(0, 0.0475f, 0, 1)));
    uint32_t ownerHash = GetOccupancyOwnerHash(hitAnchor);
    if (ownerHash == InvalidOwnerHash)
    {
        return true;
    }

    auto ownerIt = BlocksDynamics.find(ownerHash);
    if (ownerIt == BlocksDynamics.end() || ownerIt->second.modelId_ < 0 ||
        ownerIt->second.modelId_ >= static_cast<int16_t>(BasicNodes.size()))
    {
        return true;
    }

    const std::string_view ownerType(BasicNodes[ownerIt->second.modelId_].type);
    EHitFace hitFace = GetHitFaceFromNormal(hitNormal);

    if (hitFace == EHitFace::PositiveY)
    {
        glm::i16vec3 hitCell = GetBlockLocationFromRenderLocation(hitPoint);
        outBlockLocation = {
            hitCell.x,
            static_cast<int16_t>(ownerIt->second.location.y + 1),
            hitCell.z
        };
    }

    // For thin blocks, side-hit should still be allowed if it resolves to a non-overlapping cell.
    // We only force y+1 when hitting the top face.
    (void)ownerType;
    (void)reason;

    return true;
}

void MagicaLegoGameInstance::OnRayHitResponse(Assets::RayCastResult& rayResult)
{
    // 如果正在 Orbit 拖拽，不执行建造操作
    if (isOrbitDragging_)
    {
        return;
    }

    uint32_t instanceId = rayResult.InstanceId;
    lastSelectIndex_ = instanceId;
    glm::i16vec3 blockLocation = invalidPos;
    std::string placementResolveReason;
    bool hasPlacementLocation = ResolvePlacementLocationFromRay(rayResult, blockLocation, &placementResolveReason);

    if (!bMouseLeftDown_)
    {
        hasValidPlacementTarget_ = false;
        indicatorDrawRequest_ = false;
        placementConflictReason_.clear();

        if (currentMode_ == ELegoMode::ELM_Place)
        {
            if (hasPlacementLocation && currentBlockIdx_ >= 0 && currentBlockIdx_ < static_cast<int16_t>(BasicNodes.size()))
            {
                FPlacedBlock previewBlock{blockLocation, currentOrientation_, 0, currentBlockIdx_, 0, 0};
                std::string placeReason;
                bool canPlace = CanPlaceBlock(previewBlock, &placeReason);
                hasValidPlacementTarget_ = canPlace;
                placementConflictReason_ = canPlace ? std::string() : placeReason;

                glm::vec3 renderLocation = GetRenderLocationFromBlockLocation(blockLocation);
                currentBlockPosTarget_ = renderLocation;

                auto indicatorIt = BasicNodeIndicatorMap.find(BasicNodes[currentBlockIdx_].type);
                if (indicatorIt != BasicNodeIndicatorMap.end())
                {
                    const auto& indicator = indicatorIt->second;
                    glm::mat4 orientation = GetOrientationMatrix(currentOrientation_);

                    indicatorMinTarget_ = renderLocation + glm::vec3(orientation * glm::vec4(std::get<0>(indicator), 1.0f));
                    indicatorMaxTarget_ = renderLocation + glm::vec3(orientation * glm::vec4(std::get<1>(indicator), 1.0f));
                    indicatorColor_ = canPlace ? glm::vec4(0.5f, 0.65f, 1.0f, 0.75f) : glm::vec4(1.0f, 0.2f, 0.2f, 0.85f);
                    indicatorDrawRequest_ = true;
                }
            }
            else if (!hasPlacementLocation && !placementResolveReason.empty())
            {
                placementConflictReason_ = placementResolveReason;
            }
        }
        return;
    }

    auto* node = GetEngine().GetScene().GetNodeByInstanceId(instanceId);
    if (node == nullptr) return;

    switch (currentMode_)
    {
    case ELegoMode::ELM_Dig:
        if (lastDownFrameNum_ + 1 == GetEngine().GetRenderer().FrameCount())
        {
            if (node->GetName() == "blockInst")
            {
                glm::i16vec3 digBlockLocation = GetBlockLocationFromRenderLocation(glm::vec3((node->WorldTransform() * glm::vec4(0, 0.0475f, 0, 1))));
                FPlacedBlock block{digBlockLocation, EOrientation::EO_North, 0, -1, 0, 0};
                PlaceDynamicBlock(block);
            }
        }
        break;
    case ELegoMode::ELM_Place:
        if (!hasPlacementLocation || currentBlockIdx_ < 0 || currentBlockIdx_ >= static_cast<int16_t>(BasicNodes.size()))
        {
            return;
        }

        if (blockLocation == lastPlacedLocation_
            || std::find(oneLinePlacedInstance_.begin(), oneLinePlacedInstance_.end(), instanceId) != oneLinePlacedInstance_.end())
        {
            return;
        }
        if (PlaceDynamicBlock({blockLocation, currentOrientation_, 0, currentBlockIdx_, 0, 0}))
        {
            oneLinePlacedInstance_.push_back(GetHashFromBlockLocation(blockLocation) + instanceCountBeforeDynamics_);
        }
        break;
    case ELegoMode::ELM_Select:
        lastSelectLocation_ = node->GetName() == "blockInst" ? GetBlockLocationFromRenderLocation(glm::vec3((node->WorldTransform() * glm::vec4(0, 0.0475f, 0, 1)))) : invalidPos;
        GetEngine().GetScene().SetSelectedId(lastSelectIndex_);
        // Sync cursor to selection location
        if (cursor_ && lastSelectLocation_ != invalidPos)
        {
            cursor_->position = lastSelectLocation_;
        }
        break;
    }
}

bool MagicaLegoGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    float xRotation = cameraRotX_; // 例如绕X轴旋转45度
    float yRotation = cameraRotY_; // 例如上下偏转30度
    float armLength = cameraArm_;

    glm::vec3 cameraPos;
    cameraPos.x = realCameraCenter_.x + armLength * cos(glm::radians(yRotation)) * cos(glm::radians(xRotation));
    cameraPos.y = realCameraCenter_.y + armLength * sin(glm::radians(yRotation));
    cameraPos.z = realCameraCenter_.z + armLength * cos(glm::radians(yRotation)) * sin(glm::radians(xRotation));

    // calcate the view forward and left
    glm::vec3 forward = glm::normalize(realCameraCenter_ - cameraPos);
    cachedCameraPos_ = cameraPos;
    forward.y = 0.0f;
    glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    left.y = 0.0f;

    panForward_ = glm::normalize(forward);
    panLeft_ = glm::normalize(left);

    outRenderCamera.ModelView = glm::lookAtRH(cameraPos, realCameraCenter_, glm::vec3(0.0f, 1.0f, 0.0f));
    outRenderCamera.FieldOfView = cameraFOV_;
    
    
    return true;
}

void MagicaLegoGameInstance::OnInit()
{
    // MagicaLego cannot run without the brick scene asset. Bail out with a helpful prompt
    // instead of crashing later in OnSceneLoaded when GetNode("BasePlane12x12") returns null.
    if (!Utilities::FileHelper::IsAssetAvailable("assets/models/legobricks.glb"))
    {
        const char* message =
            "MagicaLego needs legobricks.glb (shipped via the optional asset pack).\n\n"
            "Run one of the following from the repo root, then relaunch:\n"
            "  ./gnb.sh paks fetch optional      (Linux / macOS / Git Bash)\n"
            "  gnb.bat paks fetch optional       (Windows)";
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MagicaLego - Missing Optional Assets",
                                 message, GetEngine().GetWindow().Handle());
        SPDLOG_ERROR("MagicaLego: legobricks.glb not found in disk or any mounted pak; aborting OnInit");
        GetEngine().RequestClose();
        return;
    }

    bgmArray_.push_back({"Salut d'Amour", "assets/sfx/bgm.mp3"});
    bgmArray_.push_back({"Liebestraum No. 3", "assets/sfx/bgm2.mp3"});
    PlayNextBGM();

    GetEngine().RequestLoadScene({.filename = "assets/models/legobricks.glb"});
    GetEngine().GetUserSettings().SceneEpsilonScale = 0.01f;
    GetEngine().GetUserSettings().AmbientCubeUnit = 0.04f;//0.0625f;
    GetEngine().GetUserSettings().AmbientCubeOffsetX = 0.02f;
    GetEngine().GetUserSettings().AmbientCubeOffsetZ = 0.02f;
    // GetEngine().GetUserSettings().AmbientCubeOffsetY = -0.04f;
    GetEngine().GetUserSettings().ShowOverlay = false;
}

void MagicaLegoGameInstance::OnTick(double deltaSeconds)
{
    // raycast request - 所有模式下都执行以实时更新鼠标状态
    CPURaycast();

    // select edge showing
    GetEngine().GetShowFlags().ShowEdge = currentMode_ == ELegoMode::ELM_Select && lastSelectLocation_ != invalidPos;

    // update mouse cursor based on current state
    UpdateMouseCursor();

    // camera center lerping
    const float speed = 0.025f;
    float t = 1.0f - glm::pow(1.0f - speed, float(deltaSeconds) * 60.0f);
    realCameraCenter_ = glm::mix(realCameraCenter_, cameraCenter_, t);

    const bool isMouseOverUI = mouseCapturedByUI_;
    const bool shouldShowPreview = (currentMode_ == ELegoMode::ELM_Place) && isTracingObject_ && !isOrbitDragging_ && hasValidPlacementTarget_ && !isMouseOverUI;

    // indicator update
    float invDelta = static_cast<float>(deltaSeconds) / 60.0f;
    if (shouldShowPreview)
    {
        if (!previewWasVisible_)
        {
            // Reappear should snap to target directly, no interpolation from hidden position.
            indicatorMinCurrent_ = indicatorMinTarget_;
            indicatorMaxCurrent_ = indicatorMaxTarget_;
            currentBlockPosCurrent_ = currentBlockPosTarget_;
        }
        else
        {
            indicatorMinCurrent_ = glm::mix(indicatorMinCurrent_, indicatorMinTarget_, invDelta * 2000.0f);
            indicatorMaxCurrent_ = glm::mix(indicatorMaxCurrent_, indicatorMaxTarget_, invDelta * 1000.0f);
            currentBlockPosCurrent_ = glm::mix(currentBlockPosCurrent_, currentBlockPosTarget_, invDelta * 1000.0f);
        }
    }
    else if (indicatorDrawRequest_)
    {
        // If preview is hidden but indicator should be drawn (e.g. conflict), snap indicator
        // to latest target to avoid a one-frame red box at stale hidden position.
        indicatorMinCurrent_ = indicatorMinTarget_;
        indicatorMaxCurrent_ = indicatorMaxTarget_;
    }
    
    // draw preview block
    if ( previewNode_.get() )
    {
        previewNode_->SetTranslation(currentBlockPosCurrent_);
        previewNode_->SetRotation(GetOrientationMatrix(currentOrientation_));

        // 只在 Place 模式、trace 到物体且非绕物拖拽时显示预览块
        Assets::NodeUtils::SetVisible(previewNode_, shouldShowPreview);
    }
    previewWasVisible_ = shouldShowPreview;
    
    // draw if no capturing
    if (indicatorDrawRequest_ && !bCapturing_)
    {
        Runtime::EngineHelper::DrawAuxBox(indicatorMinCurrent_, indicatorMaxCurrent_, indicatorColor_, 2.0);
        indicatorDrawRequest_ = false;
    }
}

bool MagicaLegoGameInstance::OnRenderUI()
{
    UserInterface_->OnRenderUI();
    return false;
}

void MagicaLegoGameInstance::OnInitUI()
{
    UserInterface_->OnInitUI();
}

void MagicaLegoGameInstance::SetBuildMode(ELegoMode mode)
{
    currentMode_ = mode;
    lastSelectLocation_ = invalidPos;
    lastPlacedLocation_ = invalidPos;
    hasValidPlacementTarget_ = false;
    previewWasVisible_ = false;
    placementConflictReason_.clear();
}

void MagicaLegoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    // BasePlane Root
    Assets::Node* base = GetEngine().GetScene().GetNode("BasePlane12x12");
    auto baseRender = base->GetComponent<Runtime::RenderComponent>();
    Assets::NodeUtils::SetVisible(base->shared_from_this(), false);
    uint32_t modelId = baseRender ? baseRender->GetModelId() : 0;
    // Copy materials
    auto matId = baseRender ? baseRender->GetMaterials() : std::array<uint32_t, 16>{};
    basementInstanceId_ = base->GetInstanceId();

    // one is 12 x 12, we support 252 x 252 (21 x 21), so duplicate and create
    for (int x = 0; x < 21; x++)
    {
        for (int z = 0; z < 21; z++)
        {
            std::string nodeName = "BigBase";
            if (x >= 7 && x <= 13 && z >= 7 && z <= 13)
            {
                nodeName = "MidBase";
            }
            if (x == 10 && z == 10)
            {
                nodeName = "SmallBase";
            }
            glm::vec3 location = glm::vec3((x - 10.25) * 0.96f, 0.0f, (z - 9.5) * 0.96f);
            auto newNode = Assets::SceneBuilder::CreateRenderNode(nodeName, location, glm::vec3(1), basementInstanceId_, modelId, matId);
            GetEngine().GetScene().AddNode(newNode);
        }
    }

    // Add the pre-defined blocks from assets
    AddBlockGroup("Block1x1");
    AddBlockGroup("Plate1x1");
    AddBlockGroup("Flat1x1");
    AddBlockGroup("Button1x1");
    AddBlockGroup("Slope1x2");
    AddBlockGroup("Cylinder1x1");

    AddBlockGroup("Plate2x2");
    AddBlockGroup("Corner2x2");

    BasicNodeIndicatorMap["Flat1x1"] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.04f, 0.032f, 0.04f)};
    BasicNodeIndicatorMap["Button1x1"] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.04f, 0.032f, 0.04f)};
    BasicNodeIndicatorMap["Plate1x1"] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.04f, 0.032f, 0.04f)};

    BasicNodeIndicatorMap["Slope1x2"] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.04f, 0.096f, 0.12f)};
    BasicNodeIndicatorMap["Plate2x2"] = {glm::vec3(-0.12f, 0.00f, -0.04f), glm::vec3(0.04f, 0.032f, 0.12f)};
    BasicNodeIndicatorMap["Corner2x2"] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.12f, 0.032f, 0.12f)};

    
    glm::mat4 orientation = GetOrientationMatrix(EOrientation::EO_North);
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size() - 1);
    previewNode_ = Assets::SceneBuilder::CreateRenderNode(
        "previewBlock",
        GetRenderLocationFromBlockLocation({0,0,0}),
        glm::vec3(1),
        instanceId,
        GetBasicBlock(currentBlockIdx_)->modelId_,
        GetBasicBlock(currentBlockIdx_)->matType,
        true,
        glm::quat(orientation),
        false);
    GetEngine().GetScene().AddNode(previewNode_);
    
    instanceCountBeforeDynamics_ = static_cast<int>(GetEngine().GetScene().Nodes().size());
    SwitchBasePlane(EBasePlane::EBP_Small);

    //GenerateThumbnail();

    UserInterface_->OnSceneLoaded();

    CleanUp();
}

void MagicaLegoGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();
    BasicNodes.clear();
    CleanUp();
}

bool MagicaLegoGameInstance::OnKey(SDL_Event& event)
{
    if (event.key.type == SDL_EVENT_KEY_DOWN)
    {
        switch (event.key.key)
        {
        case SDLK_Q: SetBuildMode(ELegoMode::ELM_Dig);
            break;
        case SDLK_W: SetBuildMode(ELegoMode::ELM_Place);
            break;
        case SDLK_E: SetBuildMode(ELegoMode::ELM_Select);
            break;
        case SDLK_R: ChangeOrientation();
            break;
        case SDLK_1: SwitchBasePlane(EBasePlane::EBP_Big);
            break;
        case SDLK_2: SwitchBasePlane(EBasePlane::EBP_Mid);
            break;
        case SDLK_3: SwitchBasePlane(EBasePlane::EBP_Small);
            break;
        case SDLK_SPACE: TestSpawnPhysicsBlock();
            break;
        default: break;
        }
    }
    else if (event.key.type == SDL_EVENT_KEY_UP)
    {
    }
    return true;
}

bool MagicaLegoGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if (resetMouse_)
    {
        mousePos_ = glm::dvec2(xpos, ypos);
        resetMouse_ = false;
    }

    glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;

    if (isOrbitDragging_ && bMouseLeftDown_)
    {
        cameraRotX_ += static_cast<float>(delta.x) * cameraMultiplier_;
        cameraRotY_ += static_cast<float>(delta.y) * cameraMultiplier_;
        cameraRotY_ = std::clamp(cameraRotY_, -89.0f, 89.0f);
    }

    if (mouseRightPressed_)
    {
        glm::vec3 panDelta = panForward_ * static_cast<float>(delta.y) * cameraMultiplier_ * 0.01f;
        panDelta += panLeft_ * static_cast<float>(delta.x) * cameraMultiplier_ * 0.01f;
        cameraCenter_ += panDelta;
        realCameraCenter_ += panDelta;
    }

    mousePos_ = glm::dvec2(xpos, ypos);

    return true;
}

bool MagicaLegoGameInstance::OnMouseButton(SDL_Event& event)
{
    if (event.button.button == SDL_BUTTON_LEFT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        bMouseLeftDown_ = true;
        lastDownFrameNum_ = GetEngine().GetRenderer().FrameCount();
        PerformLeftClickCheck();
        return true;
    }
    else if (event.button.button == SDL_BUTTON_LEFT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        bMouseLeftDown_ = false;
        isOrbitDragging_ = false;
        oneLinePlacedInstance_.clear();
        return true;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        mouseRightPressed_ = true;
        cameraMultiplier_ = 0.1f;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        mouseRightPressed_ = false;
        cameraMultiplier_ = 0.0f;
    }
    return true;
}

bool MagicaLegoGameInstance::OnScroll(double xoffset, double yoffset)
{
    const float scrollSpeed = 0.5f;
    cameraArm_ -= static_cast<float>(yoffset) * scrollSpeed;
    cameraArm_ = std::clamp(cameraArm_, 0.5f, 20.0f);
    return true;
}

void MagicaLegoGameInstance::TestSpawnPhysicsBlock()
{
    uint32_t instanceId = uint32_t(GetEngine().GetScene().Nodes().size());
    
    glm::vec3 meshPos = currentBlockPosCurrent_ + glm::vec3(0,0.2,0);
    glm::vec3 bodyExtent = glm::vec3(0.08,0.0945,0.08);
    // mesh pivot is at bottom center, physics body pivot is at center of mass
    glm::vec3 physicsOffset = glm::vec3(0, bodyExtent.y * 0.5f, 0);
    glm::vec3 bodyPos = meshPos + physicsOffset;

    std::shared_ptr<Assets::Node> newNode = Assets::SceneBuilder::CreateRenderNode(
        "phyblock",
        meshPos,
        glm::vec3(1),
        instanceId,
        GetBasicBlock(GetCurrentBrushIdx())->modelId_,
        GetBasicBlock(GetCurrentBrushIdx())->matType,
        true,
        glm::quat(),
        false);

    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = GetEngine().GetPhysicsEngine()->CreateBoxBody(bodyPos, bodyExtent, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    phys->SetPhysicsOffset(physicsOffset);
    newNode->AddComponent(phys);
    
    GetEngine().GetScene().AddNode(newNode);
    GetEngine().GetScene().MarkDirty();
    
    //GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void MagicaLegoGameInstance::TryChangeSelectionBrushIdx(int16_t idx)
{
    if (currentMode_ != ELegoMode::ELM_Select || lastSelectLocation_ == invalidPos)
    {
        return;
    }

    if (idx < 0 || idx >= static_cast<int16_t>(BasicNodes.size()))
    {
        return;
    }

    const uint32_t selectedOwnerHash = GetOccupancyOwnerHash(lastSelectLocation_);
    if (selectedOwnerHash == InvalidOwnerHash)
    {
        return;
    }

    auto selectedIt = BlocksDynamics.find(selectedOwnerHash);
    if (selectedIt == BlocksDynamics.end() || selectedIt->second.modelId_ < 0)
    {
        return;
    }

    FPlacedBlock replacedBlock = selectedIt->second;
    replacedBlock.modelId_ = idx;

    if (!CanPlaceBlockInternal(replacedBlock, selectedOwnerHash, nullptr))
    {
        return;
    }

    UnregisterOccupancy(selectedOwnerHash);
    BlocksDynamics[selectedOwnerHash] = replacedBlock;
    RegisterOccupancy(selectedOwnerHash, GetOccupiedCellsForBlock(replacedBlock));
    BlockRecords.push_back(replacedBlock);
    currentPreviewStep = static_cast<int>(BlockRecords.size());
    RebuildScene(BlocksDynamics, selectedOwnerHash);
    lastPlacedLocation_ = replacedBlock.location;

    int random = rand();
    if (random % 3 == 0)
        GetEngine().GetAudio()->PlaySound("assets/sfx/put2.wav");
    else if (random % 3 == 1)
        GetEngine().GetAudio()->PlaySound("assets/sfx/put1.wav");
    else
        GetEngine().GetAudio()->PlaySound("assets/sfx/put3.wav");
}

void MagicaLegoGameInstance::SetCurrentBrushIdx(int16_t idx)
{
    currentBlockIdx_ = idx;
    if (previewNode_.get())
    {
        if (auto render = previewNode_->GetComponent<Runtime::RenderComponent>())
        {
            render->SetModelId( GetBasicBlock(idx)->modelId_ );
            Assets::NodeUtils::SetPrimaryMaterial(previewNode_, GetBasicBlock(idx)->matType);
        }
    }
}

void MagicaLegoGameInstance::SetPlayStep(int step)
{
    if (step >= 0 && step <= static_cast<int>(BlockRecords.size()))
    {
        currentPreviewStep = step;
        RebuildFromRecord(currentPreviewStep);
    }
}

void MagicaLegoGameInstance::DumpReplayStep(int step)
{
    if (step <= static_cast<int>(BlockRecords.size()))
    {
        BlockRecords.erase(BlockRecords.begin() + step, BlockRecords.end());

        CleanDynamicBlocks();
        for (auto& block : BlockRecords)
        {
            uint32_t ownerHash = GetHashFromBlockLocation(block.location);
            if (block.modelId_ < 0)
            {
                BlocksDynamics.erase(ownerHash);
            }
            else
            {
                BlocksDynamics[ownerHash] = block;
            }
        }
        RebuildOccupancyIndex();
        RebuildScene(BlocksDynamics, -1);
    }
}

void MagicaLegoGameInstance::AddBlockGroup(std::string typeName)
{
    auto& allNodes = GetEngine().GetScene().Nodes();
    for (auto& node : allNodes)
    {
        if (node->GetName().find(typeName, 0) == 0)
        {
            AddBasicBlock(node->GetName(), typeName);
        }
    }

    BasicNodeIndicatorMap[typeName] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.04f, 0.096f, 0.04f)};
}

void MagicaLegoGameInstance::AddBasicBlock(std::string blockName, std::string typeName)
{
    auto& scene = GetEngine().GetScene();
    auto node = scene.GetNode(blockName);
    if (node)
    {
        auto render = node->GetComponent<Runtime::RenderComponent>();
        if (!render) return;

        FBasicBlock newBlock;
        std::string name = "#" + blockName.substr(strlen(typeName.c_str()) + 1);
        std::strcpy(newBlock.name, name.c_str());
        newBlock.name[127] = 0;
        std::string type = typeName;
        std::strcpy(newBlock.type, type.c_str());
        newBlock.type[127] = 0;
        newBlock.modelId_ = render->GetModelId();
        newBlock.brushId_ = static_cast<int16_t>(BasicNodes.size());
        uint32_t matId = render->GetMaterials()[0];
        auto mat = scene.GetMaterial(matId);
        if (mat)
        {
            newBlock.matType = matId;
            newBlock.color = mat->gpuMaterial_.Diffuse;
        }
        BasicNodes.push_back(newBlock);
        BasicBlockTypeMap[typeName].push_back(newBlock);
        Assets::NodeUtils::SetVisible(node->shared_from_this(), false);

#ifdef __APPLE__

#else
        std::string fileName = fmt::format("assets/textures/thumb/thumb_{}_{}.jpg", type, name);
        std::vector<uint8_t> outData;
        GetEngine().GetPakSystem().LoadFile(fileName, outData);
        Assets::GlobalTexturePool::LoadTexture(fileName, "image/jpg", outData.data(), outData.size(), false );
#endif
    }
}

FBasicBlock* MagicaLegoGameInstance::GetBasicBlock(uint32_t blockIdx)
{
    if (blockIdx < BasicNodes.size())
    {
        return &BasicNodes[blockIdx];
    }
    return nullptr;
}

std::vector<glm::i16vec3> MagicaLegoGameInstance::GetOccupiedCellsForBlock(const FPlacedBlock& block) const
{
    if (block.modelId_ < 0 || block.modelId_ >= static_cast<int16_t>(BasicNodes.size()))
    {
        return {};
    }

    const FBasicBlock& basicBlock = BasicNodes[block.modelId_];
    return MagicaLego::Placement::BuildOccupiedCells(basicBlock.type, block.location, block.orientation);
}

uint32_t MagicaLegoGameInstance::GetOccupancyOwnerHash(glm::i16vec3 location) const
{
    const uint32_t locationHash = GetHashFromBlockLocation(location);

    auto occupiedIt = OccupiedCellOwnerMap.find(locationHash);
    if (occupiedIt != OccupiedCellOwnerMap.end())
    {
        return occupiedIt->second;
    }

    auto blockIt = BlocksDynamics.find(locationHash);
    if (blockIt != BlocksDynamics.end() && blockIt->second.modelId_ >= 0)
    {
        return locationHash;
    }

    return InvalidOwnerHash;
}

bool MagicaLegoGameInstance::CanPlaceBlockInternal(const FPlacedBlock& block, uint32_t ignoredOwnerHash, std::string* reason) const
{
    if (block.modelId_ < 0)
    {
        if (reason) *reason = "Invalid place operation";
        return false;
    }

    if (block.location.y < 0)
    {
        if (reason) *reason = "Block location below base plane";
        return false;
    }

    if (block.modelId_ >= static_cast<int16_t>(BasicNodes.size()))
    {
        if (reason) *reason = "Invalid block model id";
        return false;
    }

    const std::vector<glm::i16vec3> occupiedCells = GetOccupiedCellsForBlock(block);
    if (occupiedCells.empty())
    {
        if (reason) *reason = "Invalid block footprint";
        return false;
    }

    for (const glm::i16vec3& occupiedCell : occupiedCells)
    {
        const uint32_t cellHash = GetHashFromBlockLocation(occupiedCell);
        auto occupiedIt = OccupiedCellOwnerMap.find(cellHash);
        if (occupiedIt == OccupiedCellOwnerMap.end())
        {
            continue;
        }

        if (ignoredOwnerHash != InvalidOwnerHash && occupiedIt->second == ignoredOwnerHash)
        {
            continue;
        }

        if (reason)
        {
            *reason = fmt::format("Cell ({},{},{}) already occupied", occupiedCell.x, occupiedCell.y, occupiedCell.z);
        }
        return false;
    }

    return true;
}

bool MagicaLegoGameInstance::CanPlaceBlock(const FPlacedBlock& block, std::string* reason) const
{
    return CanPlaceBlockInternal(block, InvalidOwnerHash, reason);
}

void MagicaLegoGameInstance::RegisterOccupancy(uint32_t ownerHash, const std::vector<glm::i16vec3>& occupiedCells)
{
    OwnerOccupiedCellsMap[ownerHash] = occupiedCells;
    for (const glm::i16vec3& occupiedCell : occupiedCells)
    {
        OccupiedCellOwnerMap[GetHashFromBlockLocation(occupiedCell)] = ownerHash;
    }
}

void MagicaLegoGameInstance::UnregisterOccupancy(uint32_t ownerHash)
{
    auto ownerIt = OwnerOccupiedCellsMap.find(ownerHash);
    if (ownerIt != OwnerOccupiedCellsMap.end())
    {
        for (const glm::i16vec3& occupiedCell : ownerIt->second)
        {
            OccupiedCellOwnerMap.erase(GetHashFromBlockLocation(occupiedCell));
        }
        OwnerOccupiedCellsMap.erase(ownerIt);
    }
}

void MagicaLegoGameInstance::RebuildOccupancyIndex()
{
    OccupiedCellOwnerMap.clear();
    OwnerOccupiedCellsMap.clear();

    for (const auto& [ownerHash, block] : BlocksDynamics)
    {
        if (block.modelId_ < 0)
        {
            continue;
        }

        RegisterOccupancy(ownerHash, GetOccupiedCellsForBlock(block));
    }
}

bool MagicaLegoGameInstance::PlaceDynamicBlock(FPlacedBlock block)
{
    if (block.modelId_ < 0)
    {
        const uint32_t ownerHash = GetOccupancyOwnerHash(block.location);
        if (ownerHash == InvalidOwnerHash)
        {
            return false;
        }

        auto ownerIt = BlocksDynamics.find(ownerHash);
        if (ownerIt == BlocksDynamics.end() || ownerIt->second.modelId_ < 0)
        {
            return false;
        }

        const glm::i16vec3 anchorLocation = ownerIt->second.location;
        const EOrientation anchorOrientation = ownerIt->second.orientation;

        UnregisterOccupancy(ownerHash);
        BlocksDynamics.erase(ownerIt);

        FPlacedBlock removeOp{anchorLocation, anchorOrientation, 0, -1, 0, 0};
        BlockRecords.push_back(removeOp);
        currentPreviewStep = static_cast<int>(BlockRecords.size());
        RebuildScene(BlocksDynamics, ownerHash);
        lastPlacedLocation_ = anchorLocation;

        if (cursor_)
        {
            cursor_->position = anchorLocation;
        }
        return true;
    }

    if (!CanPlaceBlock(block))
    {
        return false;
    }

    const uint32_t ownerHash = GetHashFromBlockLocation(block.location);
    const std::vector<glm::i16vec3> occupiedCells = GetOccupiedCellsForBlock(block);

    BlocksDynamics[ownerHash] = block;
    RegisterOccupancy(ownerHash, occupiedCells);
    BlockRecords.push_back(block);
    currentPreviewStep = static_cast<int>(BlockRecords.size());
    RebuildScene(BlocksDynamics, ownerHash);
    lastPlacedLocation_ = block.location;

    // Sync cursor position to placed block location
    // Note: Only sync position, not facing. Cursor facing is controlled by face/turn commands in scripts.
    if (cursor_)
    {
        cursor_->position = block.location;
    }

    // random put1 or put2
    int random = rand();
    if (random % 3 == 0)
        GetEngine().GetAudio()->PlaySound("assets/sfx/put2.wav");
    else if (random % 3 == 1)
        GetEngine().GetAudio()->PlaySound("assets/sfx/put1.wav");
    else
        GetEngine().GetAudio()->PlaySound("assets/sfx/put3.wav");

    return true;
}
void MagicaLegoGameInstance::SwitchBasePlane(EBasePlane type)
{
    currentBaseSize_ = type;
    auto& allNodes = GetEngine().GetScene().Nodes();
    for (auto& node : allNodes)
    {
        if (node->GetName() == "BigBase" || node->GetName() == "MidBase" || node->GetName() == "SmallBase")
        {
            Assets::NodeUtils::SetVisible(node, false);
        }
    }

    switch (type)
    {
    case EBasePlane::EBP_Big:
        for (auto& node : allNodes)
        {
            if (node->GetName() == "BigBase" || node->GetName() == "MidBase" || node->GetName() == "SmallBase")
            {
                Assets::NodeUtils::SetVisible(node, true);
            }
        }
        break;
    case EBasePlane::EBP_Mid:
        for (auto& node : allNodes)
        {
            if (node->GetName() == "MidBase" || node->GetName() == "SmallBase")
            {
                Assets::NodeUtils::SetVisible(node, true);
            }
        }
        break;
    case EBasePlane::EBP_Small:
        for (auto& node : allNodes)
        {
            if (node->GetName() == "SmallBase")
            {
                Assets::NodeUtils::SetVisible(node, true);
            }
        }
        break;
    }

    GetEngine().GetScene().MarkDirty();
}

void MagicaLegoGameInstance::CleanUp()
{
    BlockRecords.clear();
    CleanDynamicBlocks();
    RebuildScene(BlocksDynamics, -1);
}

void FMagicaLegoSave::Save(std::string filename)
{
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/") + filename + ".mls";

    // direct save records to file
    std::ofstream outFile(path, std::ios::binary);

    outFile.write(reinterpret_cast<const char*>(&version), sizeof(version));
    size_t size = brushs.size();
    outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));
    outFile.write(reinterpret_cast<const char*>(brushs.data()), size * sizeof(FBasicBlock));
    size = records.size();
    outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));
    outFile.write(reinterpret_cast<const char*>(records.data()), size * sizeof(FPlacedBlock));
    outFile.close();
}

void FMagicaLegoSave::Load(std::string filename)
{
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/") + filename + ".mls";

    std::ifstream inFile(path, std::ios::binary);
    if (inFile.is_open())
    {
        int ver = 0;
        inFile.read(reinterpret_cast<char*>(&ver), sizeof(ver));
        if (ver == MAGICALEGO_SAVE_VERSION)
        {
            inFile.seekg(0);
            inFile.read(reinterpret_cast<char*>(&version), sizeof(version));
            size_t size;
            inFile.read(reinterpret_cast<char*>(&size), sizeof(size));
            std::vector<FBasicBlock> tempVector(size);
            inFile.read(reinterpret_cast<char*>(tempVector.data()), size * sizeof(FBasicBlock));
            brushs = tempVector;
            inFile.read(reinterpret_cast<char*>(&size), sizeof(size));
            std::vector<FPlacedBlock> tempRecord(size);
            inFile.read(reinterpret_cast<char*>(tempRecord.data()), size * sizeof(FPlacedBlock));
            records = tempRecord;
        }
        else
        {
            // version competible code...
        }
        inFile.close();
    }
}

void MagicaLegoGameInstance::SaveRecord(std::string filename)
{
    FMagicaLegoSave save;
    save.records = BlockRecords;
    save.brushs = BasicNodes;
    save.version = MAGICALEGO_SAVE_VERSION;
    save.Save(filename);
}

void MagicaLegoGameInstance::LoadRecord(std::string filename)
{
    FMagicaLegoSave save;
    save.Load(filename);
    BlockRecords = save.records;

    if (save.version != 0)
    {
        std::map<int16_t, int16_t> brushMapping;
        for (auto& brush : save.brushs)
        {
            for (auto& newbrush : BasicNodes)
            {
                if (strcmp(brush.name, newbrush.name) == 0 && strcmp(brush.type, newbrush.type) == 0)
                {
                    brushMapping[brush.brushId_] = newbrush.brushId_;
                    break;
                }
            }
        }

        for (auto& record : BlockRecords)
        {
            if (record.modelId_ >= 0)
            {
                if (brushMapping.find(record.modelId_) != brushMapping.end())
                    record.modelId_ = brushMapping[record.modelId_];
                else
                    record.modelId_ = -1;
            }
        }
    }
    DumpReplayStep(static_cast<int>(BlockRecords.size()) - 1);
}

void MagicaLegoGameInstance::RebuildScene(std::unordered_map<uint32_t, FPlacedBlock>& source, uint32_t newhash)
{
    auto& scene = GetEngine().GetScene();
    std::vector<uint32_t> removedNodeIds;
    const auto& nodes = scene.Nodes();
    if (instanceCountBeforeDynamics_ < static_cast<int>(nodes.size()))
    {
        removedNodeIds.reserve(nodes.size() - static_cast<size_t>(instanceCountBeforeDynamics_));
        for (size_t nodeIndex = static_cast<size_t>(instanceCountBeforeDynamics_); nodeIndex < nodes.size(); ++nodeIndex)
        {
            removedNodeIds.push_back(nodes[nodeIndex]->GetInstanceId());
        }
    }
    scene.RemoveNodesByInstanceId(removedNodeIds);

    for (auto& block : source)
    {
        if (block.second.modelId_ >= 0)
        {
            auto basicBlock = GetBasicBlock(block.second.modelId_);
            if (basicBlock)
            {
                // 这里要区分一下，因为目前rebuild流程是清理后重建，因此所有node都会被认为是首次放置，都会有一个单帧的velocity
                // 所以如果没有modelid的改变的话，采用原位替换
                glm::mat4 orientation = GetOrientationMatrix(block.second.orientation);
                uint32_t instanceId = instanceCountBeforeDynamics_ + GetHashFromBlockLocation(block.second.location);
                std::shared_ptr<Assets::Node> newNode = Assets::SceneBuilder::CreateRenderNode(
                    "blockInst",
                    GetRenderLocationFromBlockLocation(block.second.location),
                    glm::vec3(1),
                    instanceId,
                    basicBlock->modelId_,
                    basicBlock->matType,
                    true,
                    glm::quat(orientation));
                scene.AddNode(newNode);
            }
        }
    }

    GetEngine().GetScene().MarkDirty();
}

void MagicaLegoGameInstance::RebuildFromRecord(int timelapse)
{
    // 从record中临时重建出一个Dynamics然后用来重建scene
    std::unordered_map<uint32_t, FPlacedBlock> tempBlocksDynamics;
    for (int i = 0; i < timelapse; i++)
    {
        auto& block = BlockRecords[i];
        uint32_t ownerHash = GetHashFromBlockLocation(block.location);
        if (block.modelId_ < 0)
        {
            tempBlocksDynamics.erase(ownerHash);
        }
        else
        {
            tempBlocksDynamics[ownerHash] = block;
        }
    }
    RebuildScene(tempBlocksDynamics, -1);
}

void MagicaLegoGameInstance::CleanDynamicBlocks()
{
    BlocksDynamics.clear();
    OccupiedCellOwnerMap.clear();
    OwnerOccupiedCellsMap.clear();
}

void MagicaLegoGameInstance::CPURaycast()
{
    if (mouseCapturedByUI_)
    {
        isTracingObject_ = false;
        hasValidPlacementTarget_ = false;
        previewWasVisible_ = false;
        placementConflictReason_.clear();
        indicatorDrawRequest_ = false;
        return;
    }

    glm::vec3 rayOrigin;
    glm::vec3 dir;
    Runtime::EngineHelper::GetScreenToWorldRay(mousePos_, rayOrigin, dir);
    isTracingObject_ = false;
    GetEngine().RayCast(rayOrigin, dir, [this](Assets::RayCastResult result)
        {
            if (result.Hit)
            {
                this->isTracingObject_ = true;
                this->OnRayHitResponse(result);
            }
            return true;
        });

    if (!isTracingObject_)
    {
        hasValidPlacementTarget_ = false;
        previewWasVisible_ = false;
        placementConflictReason_.clear();
    }
}

int16_t MagicaLegoGameInstance::ConvertBrushIdxToNextType(const std::string& prefix, int idx) const
{
    std::string subName = BasicNodes[idx].name;
    if (BasicBlockTypeMap.find(prefix) != BasicBlockTypeMap.end())
    {
        for (auto& block : BasicBlockTypeMap.at(prefix))
        {
            if (strcmp(block.name, subName.c_str()) == 0)
            {
                return block.brushId_;
            }
        }
    }
    return -1;
}

void MagicaLegoGameInstance::PlayNextBGM()
{
    GetEngine().GetAudio()->PauseSound(std::get<1>(bgmArray_[currentBGM_]), true);
    currentBGM_ = (currentBGM_ + 1) % bgmArray_.size();
    GetEngine().GetAudio()->PlaySound(std::get<1>(bgmArray_[currentBGM_]), true, 0.5f);
}

bool MagicaLegoGameInstance::IsBGMPaused()
{
    return !GetEngine().GetAudio()->IsSoundPlaying(std::get<1>(bgmArray_[currentBGM_]));
}

void MagicaLegoGameInstance::PauseBGM(bool pause)
{
    GetEngine().GetAudio()->PauseSound(std::get<1>(bgmArray_[currentBGM_]), pause);
}

std::string MagicaLegoGameInstance::GetCurrentBGMName()
{
    return std::get<0>(bgmArray_[currentBGM_]);
}

void MagicaLegoGameInstance::SetPlayReview(bool enable)
{
    playReview_ = enable;
    // add tick task to play review
    if (playReview_ == true)
    {
        if (currentPreviewStep >= static_cast<int>(BlockRecords.size()))
        {
            currentPreviewStep = 0;
        }

        GetEngine().AddTimerTask(0.1, [this]()-> bool
        {
            if (currentPreviewStep < static_cast<int>(BlockRecords.size()) && playReview_ == true)
            {
                currentPreviewStep = currentPreviewStep + 1;
                RebuildFromRecord(currentPreviewStep);
            }
            else
            {
                playReview_ = false;
                return true;
            }
            return false;
        });
    }
}


const int thumbSize = 92;

void MagicaLegoGameInstance::GenerateThumbnail()
{
    cameraArm_ = 0.7f;
    cameraCenter_ = glm::vec3(0, 0.045f, 0);
    realCameraCenter_ = cameraCenter_;
    GetEngine().GetUserSettings().TemporalFrames = 8;
    GetEngine().GetUserSettings().NumberOfSamples = 256;
    GetEngine().GetRenderer().SwapChain().UpdateRenderViewport(1920 / 2 - thumbSize / 2, 960 / 2 - thumbSize / 2, thumbSize, thumbSize);
    PlaceDynamicBlock({{0, 0, 0}, EOrientation::EO_North, 0, BasicNodes[0].brushId_, 0, 0});

    int totalTask = static_cast<int>(BasicNodes.size());
    static int currTask = 0;
    GetEngine().AddTimerTask(0.5, [this, totalTask]()-> bool
    {
        PlaceDynamicBlock({{0, 0, 0}, EOrientation::EO_North, 0, BasicNodes[currTask + 1].brushId_, 0, 0});
        std::string nodeName = BasicNodes[currTask + 1].type;
        if (nodeName.find("1x1") == std::string::npos)
        {
            cameraArm_ = 1.2f;
        }
        else
        {
            cameraArm_ = 0.7f;
        }
        GetEngine().RequestScreenShot({
            .filename = fmt::format("../../../assets/textures/thumb/thumb_{}_{}", BasicNodes[currTask].type, BasicNodes[currTask].name),
            .x = 1920 / 2 - thumbSize / 2,
            .y = 960 / 2 - thumbSize / 2,
            .width = thumbSize,
            .height = thumbSize,
            .sync = true,
        });
        currTask = currTask + 1;

        if (currTask >= totalTask)
        {
            PlaceDynamicBlock({{0, 0, 0}, EOrientation::EO_North, 0, -1, 0, 0});
            GetEngine().GetUserSettings().TemporalFrames = 16;
            GetEngine().GetUserSettings().NumberOfSamples = 8;
            cameraArm_ = 5.0f;
            cameraCenter_ = glm::vec3(0, 0.0f, 0);
            realCameraCenter_ = cameraCenter_;
            GetEngine().GetRenderer().SwapChain().UpdateRenderViewport(0, 0, 1920, 960);
            return true;
        }

        return false;
    });
}

void MagicaLegoGameInstance::PerformLeftClickCheck()
{
    glm::vec3 rayOrigin;
    glm::vec3 dir;
    Runtime::EngineHelper::GetScreenToWorldRay(mousePos_, rayOrigin, dir);

    bool hitObject = false;
    GetEngine().RayCast(rayOrigin, dir, [&hitObject](Assets::RayCastResult result) -> bool
    {
        if (result.Hit)
        {
            hitObject = true;
        }
        return true;
    });

    if (!hitObject)
    {
        isOrbitDragging_ = true;
        cameraMultiplier_ = 0.1f;
        UpdateFocusToScreenCenter();
    }
    else
    {
        isOrbitDragging_ = false;
    }
}

void MagicaLegoGameInstance::UpdateFocusToScreenCenter()
{
    auto vkextent = GetEngine().GetRenderer().SwapChain().OutputExtent();
    glm::vec2 screenCenter(vkextent.width * 0.5f, vkextent.height * 0.5f);

    glm::vec3 rayOrigin;
    glm::vec3 centerDir;
    Runtime::EngineHelper::GetScreenToWorldRay(screenCenter, rayOrigin, centerDir);

    glm::vec3 newFocus = glm::vec3(0, 0, 0);
    GetEngine().RayCast(rayOrigin, centerDir, [&newFocus](Assets::RayCastResult result) -> bool
    {
        if (result.Hit)
        {
            newFocus = glm::vec3(result.HitPoint);
        }
        return true;
    });

    focusTarget_ = newFocus;
    cameraCenter_ = focusTarget_;
}

FBasicBlock* MagicaLegoGameInstance::GetBasicBlockBySpec(const std::string& type, const std::string& color)
{
    auto it = BasicBlockTypeMap.find(type);
    if (it == BasicBlockTypeMap.end())
    {
        return nullptr;
    }

    for (auto& block : it->second)
    {
        if (block.name == color)
        {
            return &block;
        }
    }

    return nullptr;
}

bool MagicaLegoGameInstance::HasBlockAt(glm::i16vec3 location) const
{
    uint32_t ownerHash = GetOccupancyOwnerHash(location);
    if (ownerHash == InvalidOwnerHash)
    {
        return false;
    }
    auto ownerIt = BlocksDynamics.find(ownerHash);
    return ownerIt != BlocksDynamics.end() && ownerIt->second.modelId_ >= 0;
}

std::vector<std::string> MagicaLegoGameInstance::GetAllBlockTypes() const
{
    std::vector<std::string> types;
    types.reserve(BasicBlockTypeMap.size());
    for (const auto& [typeName, blocks] : BasicBlockTypeMap)
    {
        types.push_back(typeName);
    }
    return types;
}

std::vector<std::string> MagicaLegoGameInstance::GetAllBlockColors(const std::string& type) const
{
    std::vector<std::string> colors;

    if (type.empty())
    {
        // Return all colors from all types
        for (const auto& [typeName, blocks] : BasicBlockTypeMap)
        {
            for (const auto& block : blocks)
            {
                colors.push_back(block.name);
            }
        }
    }
    else
    {
        auto it = BasicBlockTypeMap.find(type);
        if (it != BasicBlockTypeMap.end())
        {
            for (const auto& block : it->second)
            {
                colors.push_back(block.name);
            }
        }
    }

    return colors;
}

MagicaLego::FCursor& MagicaLegoGameInstance::GetCursor()
{
    return *cursor_;
}

const MagicaLego::FCursor& MagicaLegoGameInstance::GetCursor() const
{
    return *cursor_;
}

std::string MagicaLegoGameInstance::GetCurrentSceneDescription() const
{
    if (BlocksDynamics.empty())
    {
        return "The scene is empty. No blocks have been placed yet.";
    }

    std::string description;
    description += fmt::format("Current scene has {} blocks:\n", BlocksDynamics.size());

    // Calculate bounding box
    glm::i16vec3 minPos{INT16_MAX, INT16_MAX, INT16_MAX};
    glm::i16vec3 maxPos{INT16_MIN, INT16_MIN, INT16_MIN};

    // Count blocks by type
    std::map<std::string, int> blockCounts;

    for (const auto& [hash, block] : BlocksDynamics)
    {
        if (block.modelId_ < 0) continue; // Skip removed blocks

        minPos = glm::min(minPos, block.location);
        maxPos = glm::max(maxPos, block.location);

        // Get block type name
        if (block.modelId_ < static_cast<int16_t>(BasicNodes.size()))
        {
            std::string typeName = BasicNodes[block.modelId_].type;
            blockCounts[typeName]++;
        }
    }

    // Describe bounding box
    description += fmt::format("- Bounding box: ({},{},{}) to ({},{},{})\n",
        minPos.x, minPos.y, minPos.z, maxPos.x, maxPos.y, maxPos.z);
    description += fmt::format("- Size: {}x{}x{} (width x height x depth)\n",
        maxPos.x - minPos.x + 1, maxPos.y - minPos.y + 1, maxPos.z - minPos.z + 1);

    // Describe block composition
    description += "- Block types used:\n";
    for (const auto& [type, count] : blockCounts)
    {
        description += fmt::format("  - {}: {} blocks\n", type, count);
    }

    // List individual blocks (limit to avoid huge prompts)
    int listedCount = 0;

    description += "\nPlaced blocks (format: Type/Color at x,y,z):\n";
    for (const auto& [hash, block] : BlocksDynamics)
    {
        if (block.modelId_ < 0) continue;
        if (listedCount >= MagicaLego::AI::MaxBlocksToList)
        {
            description += fmt::format("... and {} more blocks\n",
                static_cast<int>(BlocksDynamics.size()) - MagicaLego::AI::MaxBlocksToList);
            break;
        }

        if (block.modelId_ < static_cast<int16_t>(BasicNodes.size()))
        {
            const auto& basicBlock = BasicNodes[block.modelId_];
            description += fmt::format("- {}/{} at ({},{},{})\n",
                basicBlock.type, basicBlock.name,
                block.location.x, block.location.y, block.location.z);
        }
        listedCount++;
    }

    return description;
}

void MagicaLegoGameInstance::UpdateMouseCursor()
{
    SDL_SystemCursor cursorType;

    // 优先级 1: 右键 Pan 模式
    if (mouseRightPressed_)
    {
        cursorType = SDL_SYSTEM_CURSOR_MOVE;  // 四向箭头，表示移动
    }
    // 优先级 2: 左键拖拽绕物旋转
    else if (isOrbitDragging_ && bMouseLeftDown_)
    {
        cursorType = SDL_SYSTEM_CURSOR_POINTER;  // 手型，表示拖拽旋转
    }
    // 优先级 3: trace 到背景，显示旋转图标
    else if (!isTracingObject_)
    {
        cursorType = SDL_SYSTEM_CURSOR_POINTER;  // 手型，表示可以拖拽旋转
    }
    // 优先级 4: trace 到物体，根据当前模式显示图标
    else
    {
        switch (currentMode_)
        {
        case ELegoMode::ELM_Dig:
            cursorType = SDL_SYSTEM_CURSOR_NOT_ALLOWED;  // 禁止符号，表示删除
            break;
        case ELegoMode::ELM_Place:
            cursorType = hasValidPlacementTarget_ ? SDL_SYSTEM_CURSOR_CROSSHAIR : SDL_SYSTEM_CURSOR_NOT_ALLOWED;
            break;
        case ELegoMode::ELM_Select:
            cursorType = SDL_SYSTEM_CURSOR_POINTER;  // 手型，表示选择
            break;
        default:
            cursorType = SDL_SYSTEM_CURSOR_DEFAULT;
            break;
        }
    }

    SDL_Cursor* cursor = SDL_CreateSystemCursor(cursorType);
    if (cursor)
    {
        SDL_SetCursor(cursor);
    }
}
