#include "MagicaLegoGameInstance.hpp"
#include "Assets/Scene.hpp"
#include "Assets/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Utilities/FileHelper.hpp"
#include "MagicaLegoUserInterface.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Vulkan/SwapChain.hpp"

#include <glm/gtc/quaternion.hpp>

const glm::i16vec3 invalidPos(0, -10, 0);

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<MagicaLegoGameInstance>(config, options, engine);
}

glm::vec3 GetRenderLocationFromBlockLocation(glm::i16vec3 blockLocation)
{
    glm::vec3 newLocation;
    newLocation.x = static_cast<float>(blockLocation.x) * 0.08f;
    newLocation.y = static_cast<float>(blockLocation.y) * 0.095f;
    newLocation.z = static_cast<float>(blockLocation.z) * 0.08f;
    return newLocation;
}

glm::i16vec3 GetBlockLocationFromRenderLocation(glm::vec3 renderLocation)
{
    glm::i16vec3 newLocation;
    newLocation.x = static_cast<int16_t>(round(renderLocation.x / 0.08f));
    newLocation.y = static_cast<int16_t>(round((renderLocation.y - 0.0475f) / 0.095f));
    newLocation.z = static_cast<int16_t>(round(renderLocation.z / 0.08f));
    return newLocation;
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

MagicaLegoGameInstance::MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine): NextGameInstanceBase(config, options, engine), engine_(engine)
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
    options.Samples = 16;
    options.Temporal = 8;
    options.ForceSDR = true;
    options.RendererType = 0;
    options.locale = "zhCN";
    options.SuperResolution = 4;

    // mode init
    SetBuildMode(ELegoMode::ELM_Place);
    SetCameraMode(ECamMode::ECM_Orbit);

    // control init
    resetMouse_ = true;
    cameraRotX_ = 45;
    cameraRotY_ = 30;
    cameraArm_ = 5.0;
    cameraFOV_ = 12.f;

    // ui
    UserInterface_ = std::make_unique<MagicaLegoUserInterface>(this);

    lastSelectLocation_ = invalidPos;
    lastPlacedLocation_ = invalidPos;

    GetEngine().GetPakSystem().SetRunMode(Utilities::Package::EPM_PakFile);
    GetEngine().GetPakSystem().Reset();
    GetEngine().GetPakSystem().MountPak(Utilities::FileHelper::GetPlatformFilePath("assets/paks/lego.pak"));
    GetEngine().GetPakSystem().MountPak(Utilities::FileHelper::GetPlatformFilePath("assets/paks/thumbs.pak"));
}

void MagicaLegoGameInstance::OnRayHitResponse(Assets::RayCastResult& rayResult)
{
    glm::vec3 newLocation = glm::vec3(rayResult.HitPoint) + glm::vec3(rayResult.Normal) * 0.005f;
    glm::i16vec3 blockLocation = GetBlockLocationFromRenderLocation(newLocation);
    glm::vec3 renderLocation = GetRenderLocationFromBlockLocation(blockLocation);
    uint32_t instanceId = rayResult.InstanceId;
    lastSelectIndex_ = instanceId;
    
    if (!bMouseLeftDown_)
    {
        if (currentMode_ == ELegoMode::ELM_Place)
        {
            if (BasicNodeIndicatorMap.size() > 0 && BasicNodes.size() > 0)
            {
                auto& indicator = BasicNodeIndicatorMap[BasicNodes[currentBlockIdx_].type];

                glm::mat4 orientation = GetOrientationMatrix(currentOrientation_);

                indicatorMinTarget_ = renderLocation + glm::vec3(orientation * glm::vec4(std::get<0>(indicator), 1.0f));
                indicatorMaxTarget_ = renderLocation + glm::vec3(orientation * glm::vec4(std::get<1>(indicator), 1.0f));
                currentBlockPosTarget_ = renderLocation;
                indicatorDrawRequest_ = true;
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
        if (currentCamMode_ == ECamMode::ECM_AutoFocus && lastSelectLocation_ != invalidPos) cameraCenter_ = GetRenderLocationFromBlockLocation(lastSelectLocation_);
        GetEngine().GetScene().SetSelectedId(lastSelectIndex_);
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
    bgmArray_.push_back({"Salut d'Amour", "assets/sfx/bgm.mp3"});
    bgmArray_.push_back({"Liebestraum No. 3", "assets/sfx/bgm2.mp3"});
    PlayNextBGM();

    GetEngine().RequestLoadScene("assets/models/legobricks.glb");
    GetEngine().GetUserSettings().SceneEpsilonScale = 0.01f;
}

void MagicaLegoGameInstance::OnTick(double deltaSeconds)
{
    // raycast request
    if (GetBuildMode() == ELegoMode::ELM_Place || bMouseLeftDown_)
    {
        CPURaycast();
    }

    // select edge showing
    GetEngine().GetShowFlags().ShowEdge = currentMode_ == ELegoMode::ELM_Select && lastSelectLocation_ != invalidPos;

    
    // camera center lerping
    const float speed = 0.025f;
    float t = 1.0f - glm::pow(1.0f - speed, float(deltaSeconds) * 60.0f);
    realCameraCenter_ = glm::mix(realCameraCenter_, cameraCenter_, t);

    // indicator update
    float invDelta = static_cast<float>(deltaSeconds) / 60.0f;
    indicatorMinCurrent_ = glm::mix(indicatorMinCurrent_, indicatorMinTarget_, invDelta * 2000.0f);
    indicatorMaxCurrent_ = glm::mix(indicatorMaxCurrent_, indicatorMaxTarget_, invDelta * 1000.0f);
    currentBlockPosCurrent_ = glm::mix(currentBlockPosCurrent_, currentBlockPosTarget_, invDelta * 1000.0f);
    
    // draw preview block
    if ( previewNode_.get() )
    {
        previewNode_->SetTranslation(currentBlockPosCurrent_);
        previewNode_->SetRotation(GetOrientationMatrix(currentOrientation_));
        previewNode_->RecalcTransform();
    }
    
    // draw if no capturing
    if (indicatorDrawRequest_ && !bCapturing_)
    {
        NextEngineHelper::DrawAuxBox(indicatorMinCurrent_, indicatorMaxCurrent_, glm::vec4(0.5, 0.65, 1, 0.75), 2.0);
        indicatorDrawRequest_ = false;
    }
}

bool MagicaLegoGameInstance::OnRenderUI()
{
    UserInterface_->OnRenderUI();
    return true;
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
}

void MagicaLegoGameInstance::SetCameraMode(ECamMode mode)
{
    currentCamMode_ = mode;
}

void MagicaLegoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    // BasePlane Root
    Assets::Node* base = GetEngine().GetScene().GetNode("BasePlane12x12");
    auto baseRender = base->GetComponent<Runtime::RenderComponent>();
    if (baseRender) baseRender->SetVisible(false);
    uint32_t modelId = baseRender ? baseRender->GetModelId() : 0;
    // Copy materials
    auto matId = baseRender ? baseRender->Materials() : std::array<uint32_t, 16>{};
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
            auto newNode = Assets::Node::CreateNode(nodeName, location, glm::quat(1,0,0,0), glm::vec3(1), basementInstanceId_);
            auto renderComp = std::make_shared<Runtime::RenderComponent>();
            renderComp->SetModelId(modelId);
            renderComp->SetMaterial(matId);
            newNode->AddComponent(renderComp);
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
    previewNode_ = Assets::Node::CreateNode("previewBlock", GetRenderLocationFromBlockLocation({0,0,0}), glm::quat(orientation), glm::vec3(1), instanceId);
    auto previewRender = std::make_shared<Runtime::RenderComponent>();
    previewRender->SetModelId(GetBasicBlock(currentBlockIdx_)->modelId_);
    previewRender->SetMaterial({ GetBasicBlock(currentBlockIdx_)->matType });
    previewRender->SetVisible(true);
    previewRender->SetRayCastVisible(false);
    previewNode_->AddComponent(previewRender);
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
        case SDLK_A: SetCameraMode(ECamMode::ECM_Pan);
            break;
        case SDLK_S: SetCameraMode(ECamMode::ECM_Orbit);
            break;
        case SDLK_D: SetCameraMode(ECamMode::ECM_AutoFocus);
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

    if ((currentCamMode_ == ECamMode::ECM_Orbit) || (currentCamMode_ == ECamMode::ECM_AutoFocus))
    {
        cameraRotX_ += static_cast<float>(delta.x) * cameraMultiplier_;
        cameraRotY_ += static_cast<float>(delta.y) * cameraMultiplier_;
    }

    if (currentCamMode_ == ECamMode::ECM_Pan)
    {
        cameraCenter_ += panForward_ * static_cast<float>(delta.y) * cameraMultiplier_ * 0.01f;
        cameraCenter_ += panLeft_ * static_cast<float>(delta.x) * cameraMultiplier_ * 0.01f;
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
        return true;
    }
    else if (event.button.button == SDL_BUTTON_LEFT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        bMouseLeftDown_ = false;
        oneLinePlacedInstance_.clear();
        if (currentCamMode_ == ECamMode::ECM_AutoFocus && currentMode_ == ELegoMode::ELM_Place && lastPlacedLocation_ != invalidPos)
            cameraCenter_ = GetRenderLocationFromBlockLocation(lastPlacedLocation_);
        return true;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
    {
        cameraMultiplier_ = 0.1f;
    }
    else if (event.button.button == SDL_BUTTON_RIGHT && event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        cameraMultiplier_ = 0.0f;
    }
    return true;
}

bool MagicaLegoGameInstance::OnScroll(double xoffset, double yoffset)
{
    cameraFOV_ -= static_cast<float>(yoffset);
    cameraFOV_ = std::clamp(cameraFOV_, 1.0f, 30.0f);
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

    std::shared_ptr<Assets::Node> newNode = Assets::Node::CreateNode("phyblock", meshPos, glm::quat(), glm::vec3(1), instanceId);
    
    auto renderComp = std::make_shared<Runtime::RenderComponent>();
    renderComp->SetModelId(GetBasicBlock(GetCurrentBrushIdx())->modelId_);
    renderComp->SetMaterial( { GetBasicBlock(GetCurrentBrushIdx())->matType } );
    renderComp->SetVisible(true);
    renderComp->SetRayCastVisible(false);
    newNode->AddComponent(renderComp);

    auto phys = std::make_shared<Runtime::PhysicsComponent>();
    phys->SetMobility(Runtime::ENodeMobility::Dynamic);
    auto id = NextEngine::GetInstance()->GetPhysicsEngine()->CreateBoxBody(bodyPos, bodyExtent, NextMotionType::Dynamic);
    phys->BindPhysicsBody(id);
    phys->SetPhysicsOffset(physicsOffset);
    newNode->AddComponent(phys);
    
    GetEngine().GetScene().Nodes().push_back(newNode);
    GetEngine().GetScene().MarkDirty();
    
    //GetEngine().GetPhysicsEngine()->AddForceToBody(id, shotDir * 70000.f);
}

void MagicaLegoGameInstance::TryChangeSelectionBrushIdx(int16_t idx)
{
    if (currentMode_ == ELegoMode::ELM_Select)
    {
        if (lastSelectLocation_ != invalidPos)
        {
            uint32_t currentHash = GetHashFromBlockLocation(lastSelectLocation_);
            FPlacedBlock currentBlock = BlocksDynamics[currentHash];
            FPlacedBlock block{lastSelectLocation_, currentBlock.orientation, 0, idx, 0, 0};
            PlaceDynamicBlock(block);
        }
    }
}

void MagicaLegoGameInstance::SetCurrentBrushIdx(int16_t idx)
{
    currentBlockIdx_ = idx;
    if (previewNode_.get())
    {
        if (auto render = previewNode_->GetComponent<Runtime::RenderComponent>())
        {
            render->SetModelId( GetBasicBlock(idx)->modelId_ );
            render->SetMaterial( { GetBasicBlock(idx)->matType } );
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
            BlocksDynamics[GetHashFromBlockLocation(block.location)] = block;
        }
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
        uint32_t matId = render->Materials()[0];
        auto mat = scene.GetMaterial(matId);
        if (mat)
        {
            newBlock.matType = matId;
            newBlock.color = mat->gpuMaterial_.Diffuse;
        }
        BasicNodes.push_back(newBlock);
        BasicBlockTypeMap[typeName].push_back(newBlock);
        render->SetVisible(false);

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

bool MagicaLegoGameInstance::PlaceDynamicBlock(FPlacedBlock block)
{
    if (block.location.y < 0)
    {
        return false;
    }

    uint32_t blockHash = GetHashFromBlockLocation(block.location);

    // Place it
    BlocksDynamics[blockHash] = block;
    BlockRecords.push_back(block);
    currentPreviewStep = static_cast<int>(BlockRecords.size());
    RebuildScene(BlocksDynamics, blockHash);
    lastPlacedLocation_ = block.location;

    // random put1 or put2
    if (block.modelId_ >= 0)
    {
        int random = rand();
        if (random % 3 == 0)
            GetEngine().PlaySound("assets/sfx/put2.wav");
        else if (random % 3 == 1)
            GetEngine().PlaySound("assets/sfx/put1.wav");
        else
            GetEngine().PlaySound("assets/sfx/put3.wav");
    }

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
            if (auto r = node->GetComponent<Runtime::RenderComponent>()) r->SetVisible(false);
        }
    }

    switch (type)
    {
    case EBasePlane::EBP_Big:
        for (auto& node : allNodes)
        {
            if (node->GetName() == "BigBase" || node->GetName() == "MidBase" || node->GetName() == "SmallBase")
            {
                if (auto r = node->GetComponent<Runtime::RenderComponent>()) r->SetVisible(true);
            }
        }
        break;
    case EBasePlane::EBP_Mid:
        for (auto& node : allNodes)
        {
            if (node->GetName() == "MidBase" || node->GetName() == "SmallBase")
            {
                if (auto r = node->GetComponent<Runtime::RenderComponent>()) r->SetVisible(true);
            }
        }
        break;
    case EBasePlane::EBP_Small:
        for (auto& node : allNodes)
        {
            if (node->GetName() == "SmallBase")
            {
                if (auto r = node->GetComponent<Runtime::RenderComponent>()) r->SetVisible(true);
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
    GetEngine().GetScene().Nodes().erase(GetEngine().GetScene().Nodes().begin() + instanceCountBeforeDynamics_, GetEngine().GetScene().Nodes().end());

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
                std::shared_ptr<Assets::Node> newNode = Assets::Node::CreateNode("blockInst", GetRenderLocationFromBlockLocation(block.second.location), glm::quat(orientation), glm::vec3(1), instanceId);
                auto renderComp = std::make_shared<Runtime::RenderComponent>();
                renderComp->SetModelId(basicBlock->modelId_);
                renderComp->SetMaterial( {basicBlock->matType} );
                renderComp->SetVisible(true);
                newNode->AddComponent(renderComp);
                GetEngine().GetScene().Nodes().push_back(newNode);
            }
        }
    }

    GetEngine().GetScene().MarkDirty();
    GetEngine().GetScene().GetCPUAccelerationStructure().UpdateBVH(GetEngine().GetScene());
}

void MagicaLegoGameInstance::RebuildFromRecord(int timelapse)
{
    // 从record中临时重建出一个Dynamics然后用来重建scene
    std::unordered_map<uint32_t, FPlacedBlock> tempBlocksDynamics;
    for (int i = 0; i < timelapse; i++)
    {
        auto& block = BlockRecords[i];
        tempBlocksDynamics[GetHashFromBlockLocation(block.location)] = block;

        if (currentCamMode_ == ECamMode::ECM_AutoFocus)
            cameraCenter_ = GetRenderLocationFromBlockLocation(block.location);
    }
    RebuildScene(tempBlocksDynamics, -1);
}

void MagicaLegoGameInstance::CleanDynamicBlocks()
{
    BlocksDynamics.clear();
}

void MagicaLegoGameInstance::CPURaycast()
{
    glm::vec3 dir = NextEngineHelper::ProjectScreenToWorld(mousePos_);
    GetEngine().RayCastGPU(cachedCameraPos_, dir, [this](Assets::RayCastResult result)
        {
            if (result.Hitted)
            {
                this->OnRayHitResponse(result);
            }
            return true;
        });
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
    GetEngine().PauseSound(std::get<1>(bgmArray_[currentBGM_]), true);
    currentBGM_ = (currentBGM_ + 1) % bgmArray_.size();
    GetEngine().PlaySound(std::get<1>(bgmArray_[currentBGM_]), true, 0.5f);
}

bool MagicaLegoGameInstance::IsBGMPaused()
{
    return !GetEngine().IsSoundPlaying(std::get<1>(bgmArray_[currentBGM_]));
}

void MagicaLegoGameInstance::PauseBGM(bool pause)
{
    GetEngine().PauseSound(std::get<1>(bgmArray_[currentBGM_]), pause);
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
    GetEngine().GetUserSettings().Denoiser = false;
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
        GetEngine().SaveScreenShot(fmt::format("../../../assets/textures/thumb/thumb_{}_{}", BasicNodes[currTask].type, BasicNodes[currTask].name), 1920 / 2 - thumbSize / 2, 960 / 2 - thumbSize / 2,
                                   thumbSize, thumbSize);
        currTask = currTask + 1;

        if (currTask >= totalTask)
        {
            PlaceDynamicBlock({{0, 0, 0}, EOrientation::EO_North, 0, -1, 0, 0});
            GetEngine().GetUserSettings().TemporalFrames = 16;
            GetEngine().GetUserSettings().NumberOfSamples = 8;
            GetEngine().GetUserSettings().Denoiser = true;
            cameraArm_ = 5.0f;
            cameraCenter_ = glm::vec3(0, 0.0f, 0);
            realCameraCenter_ = cameraCenter_;
            GetEngine().GetRenderer().SwapChain().UpdateRenderViewport(0, 0, 1920, 960);
            return true;
        }

        return false;
    });
}
