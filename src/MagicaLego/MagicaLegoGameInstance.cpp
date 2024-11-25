#include "MagicaLegoGameInstance.hpp"

#include "Assets/Scene.hpp"
#include "Utilities/FileHelper.hpp"
#include "MagicaLegoUserInterface.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Vulkan/SwapChain.hpp"

const glm::i16vec3 INVALID_POS(0, -10, 0);

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<MagicaLegoGameInstance>(config, options, engine);
}

glm::vec3 GetRenderLocationFromBlockLocation(glm::i16vec3 BlockLocation)
{
    glm::vec3 newLocation;
    newLocation.x = static_cast<float>(BlockLocation.x) * 0.08f;
    newLocation.y = static_cast<float>(BlockLocation.y) * 0.095f;
    newLocation.z = static_cast<float>(BlockLocation.z) * 0.08f;
    return newLocation;
}

glm::i16vec3 GetBlockLocationFromRenderLocation(glm::vec3 RenderLocation)
{
    glm::i16vec3 newLocation;
    newLocation.x = static_cast<int16_t>(round(RenderLocation.x / 0.08f));
    newLocation.y = static_cast<int16_t>(round((RenderLocation.y - 0.0475f) / 0.095f));
    newLocation.z = static_cast<int16_t>(round(RenderLocation.z / 0.08f));
    return newLocation;
}

uint32_t GetHashFromBlockLocation(const glm::i16vec3& BlockLocation)
{
    uint32_t x = static_cast<uint32_t>(BlockLocation.x) & 0xFFFF;
    uint32_t y = static_cast<uint32_t>(BlockLocation.y) & 0xFFFF;
    uint32_t z = static_cast<uint32_t>(BlockLocation.z) & 0xFFFF;

    uint32_t hash = x;
    hash = hash * 31 + y;
    hash = hash * 31 + z;

    return hash;
}

MagicaLegoGameInstance::MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine): NextGameInstanceBase(config, options, engine), engine_(engine)
{
    NextRenderer::HideConsole();

    // windows config
    config.Title = "MagicaLego";
    config.Height = 960;
    config.Width = 1920;
    config.ForceSDR = true;
    config.HideTitleBar = true;

    // options
    options.SceneName = "legobricks.glb";
    options.Samples = 8;
    options.Temporal = 8;
    options.ForceSDR = true;
    options.RendererType = 0;
    options.locale = "zhCN";

    // mode init
    SetBuildMode(ELegoMode::ELM_Place);
    SetCameraMode(ECamMode::ECM_Orbit);

    // control init
    resetMouse_ = true;
    cameraRotX_ = 45;
    cameraRotY_ = 30;
    cameraArm_ = 5.0;

    // ui
    UserInterface_ = std::make_unique<MagicaLegoUserInterface>(this);

    lastSelectLocation_ = INVALID_POS;
    lastPlacedLocation_ = INVALID_POS;

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
                indicatorDrawRequest_ = true;
            }
        }
        return;
    }

    auto* Node = GetEngine().GetScene().GetNodeByInstanceId(instanceId);
    if (Node == nullptr) return;

    switch (currentMode_)
    {
    case ELegoMode::ELM_Dig:
        if (lastDownFrameNum_ + 1 == GetEngine().GetRenderer().FrameCount())
        {
            if (Node->GetName() == "blockInst")
            {
                glm::i16vec3 digBlockLocation = GetBlockLocationFromRenderLocation(glm::vec3((Node->WorldTransform() * glm::vec4(0, 0.0475f, 0, 1))));
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
        lastSelectLocation_ = Node->GetName() == "blockInst" ? GetBlockLocationFromRenderLocation(glm::vec3((Node->WorldTransform() * glm::vec4(0, 0.0475f, 0, 1)))) : INVALID_POS;
        if (currentCamMode_ == ECamMode::ECM_AutoFocus && lastSelectLocation_ != INVALID_POS) cameraCenter_ = GetRenderLocationFromBlockLocation(lastSelectLocation_);
        break;
    }
}

bool MagicaLegoGameInstance::OverrideModelView(glm::mat4& OutMatrix) const
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
    forward.y = 0.0f;
    glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    left.y = 0.0f;

    panForward_ = glm::normalize(forward);
    panLeft_ = glm::normalize(left);

    OutMatrix = glm::lookAtRH(cameraPos, realCameraCenter_, glm::vec3(0.0f, 1.0f, 0.0f));
    return true;
}

void MagicaLegoGameInstance::OnInit()
{
    bgmArray_.push_back({"Salut d'Amour", "assets/sfx/bgm.mp3"});
    bgmArray_.push_back({"Liebestraum No. 3", "assets/sfx/bgm2.mp3"});
    PlayNextBGM();
}

void MagicaLegoGameInstance::OnTick(double deltaSeconds)
{
    // raycast request
    if (GetBuildMode() == ELegoMode::ELM_Place || bMouseLeftDown_)
    {
        GetEngine().GetUserSettings().RequestRayCast = true;
    }

    // select edge showing
    GetEngine().GetUserSettings().ShowEdge = currentMode_ == ELegoMode::ELM_Select && lastSelectLocation_ != INVALID_POS;

    // camera center lerping
    realCameraCenter_ = glm::mix(realCameraCenter_, cameraCenter_, 0.1f);

    // indicator update
    float invDelta = static_cast<float>(deltaSeconds) / 60.0f;
    indicatorMinCurrent_ = glm::mix(indicatorMinCurrent_, indicatorMinTarget_, invDelta * 2000.0f);
    indicatorMaxCurrent_ = glm::mix(indicatorMaxCurrent_, indicatorMaxTarget_, invDelta * 1000.0f);

    // draw if no capturing
    if (indicatorDrawRequest_ && !bCapturing_)
    {
        GetEngine().DrawAuxBox(indicatorMinCurrent_, indicatorMaxCurrent_, glm::vec4(0.5, 0.65, 1, 0.75), 2.0);
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
    lastSelectLocation_ = INVALID_POS;
    lastPlacedLocation_ = INVALID_POS;
}

void MagicaLegoGameInstance::SetCameraMode(ECamMode mode)
{
    currentCamMode_ = mode;
}

void MagicaLegoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    // BasePlane Root
    Assets::Node* Base = GetEngine().GetScene().GetNode("BasePlane12x12");
    Base->SetVisible(false);
    uint32_t modelId = Base->GetModel();
    uint32_t instanceId = Base->GetInstanceId();

    // one is 12 x 12, we support 252 x 252 (21 x 21), so duplicate and create
    for (int x = 0; x < 21; x++)
    {
        for (int z = 0; z < 21; z++)
        {
            std::string NodeName = "BigBase";
            if (x >= 7 && x <= 13 && z >= 7 && z <= 13)
            {
                NodeName = "MidBase";
            }
            if (x == 10 && z == 10)
            {
                NodeName = "SmallBase";
            }
            glm::vec3 location = glm::vec3((x - 10.25) * 0.96f, 0.0f, (z - 9.5) * 0.96f);
            Assets::Node newNode = Assets::Node::CreateNode(NodeName, glm::translate(glm::mat4(1), location), modelId, instanceId, false);
            GetEngine().GetScene().Nodes().push_back(newNode);
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

    instanceCountBeforeDynamics_ = static_cast<int>(GetEngine().GetScene().Nodes().size());
    SwitchBasePlane(EBasePlane::EBP_Small);

    //GeneratingThmubnail();
}

void MagicaLegoGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();

    BasicNodes.clear();
    CleanUp();

    UserInterface_->OnSceneLoaded();
}

bool MagicaLegoGameInstance::OnKey(int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS)
    {
        switch (key)
        {
        case GLFW_KEY_Q: SetBuildMode(ELegoMode::ELM_Dig);
            break;
        case GLFW_KEY_W: SetBuildMode(ELegoMode::ELM_Place);
            break;
        case GLFW_KEY_E: SetBuildMode(ELegoMode::ELM_Select);
            break;
        case GLFW_KEY_A: SetCameraMode(ECamMode::ECM_Pan);
            break;
        case GLFW_KEY_S: SetCameraMode(ECamMode::ECM_Orbit);
            break;
        case GLFW_KEY_D: SetCameraMode(ECamMode::ECM_AutoFocus);
            break;
        case GLFW_KEY_R: ChangeOrientation();
            break;
        case GLFW_KEY_1: SwitchBasePlane(EBasePlane::EBP_Big);
            break;
        case GLFW_KEY_2: SwitchBasePlane(EBasePlane::EBP_Mid);
            break;
        case GLFW_KEY_3: SwitchBasePlane(EBasePlane::EBP_Small);
            break;
        default: break;
        }
    }
    else if (action == GLFW_RELEASE)
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

bool MagicaLegoGameInstance::OnMouseButton(int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        bMouseLeftDown_ = true;
        lastDownFrameNum_ = GetEngine().GetRenderer().FrameCount();
        return true;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        bMouseLeftDown_ = false;
        oneLinePlacedInstance_.clear();
        if (currentCamMode_ == ECamMode::ECM_AutoFocus && currentMode_ == ELegoMode::ELM_Place && lastPlacedLocation_ != INVALID_POS)
            cameraCenter_ = GetRenderLocationFromBlockLocation(lastPlacedLocation_);
        return true;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS)
    {
        cameraMultiplier_ = 0.1f;
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
    {
        cameraMultiplier_ = 0.0f;
    }
    return true;
}

void MagicaLegoGameInstance::TryChangeSelectionBrushIdx(int16_t idx)
{
    if (currentMode_ == ELegoMode::ELM_Select)
    {
        if (lastSelectLocation_ != INVALID_POS)
        {
            uint32_t currentHash = GetHashFromBlockLocation(lastSelectLocation_);
            FPlacedBlock currentBlock = BlocksDynamics[currentHash];
            FPlacedBlock block{lastSelectLocation_, currentBlock.orientation, 0, idx, 0, 0};
            PlaceDynamicBlock(block);
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
        for (auto& Block : BlockRecords)
        {
            BlocksDynamics[GetHashFromBlockLocation(Block.location)] = Block;
        }
        RebuildScene(BlocksDynamics, -1);
    }
}

void MagicaLegoGameInstance::AddBlockGroup(std::string typeName)
{
    auto& allNodes = GetEngine().GetScene().Nodes();
    for (auto& Node : allNodes)
    {
        if (Node.GetName().find(typeName, 0) == 0)
        {
            AddBasicBlock(Node.GetName(), typeName);
        }
    }

    BasicNodeIndicatorMap[typeName] = {glm::vec3(-0.04f, 0.00f, -0.04f), glm::vec3(0.04f, 0.096f, 0.04f)};
}

void MagicaLegoGameInstance::AddBasicBlock(std::string blockName, std::string typeName)
{
    auto& Scene = GetEngine().GetScene();
    auto Node = Scene.GetNode(blockName);
    if (Node)
    {
        FBasicBlock newBlock;
        std::string name = "#" + blockName.substr(strlen(typeName.c_str()) + 1);
        std::strcpy(newBlock.name, name.c_str());
        newBlock.name[127] = 0;
        std::string type = typeName;
        std::strcpy(newBlock.type, type.c_str());
        newBlock.type[127] = 0;
        newBlock.modelId_ = Node->GetModel();
        newBlock.brushId_ = static_cast<int16_t>(BasicNodes.size());
        uint32_t mat_id = Scene.GetModel(newBlock.modelId_)->Materials()[0];
        auto mat = Scene.GetMaterial(mat_id);
        if (mat)
        {
            newBlock.matType = static_cast<int>(mat->MaterialModel);
            newBlock.color = mat->Diffuse;
        }
        BasicNodes.push_back(newBlock);
        BasicBlockTypeMap[typeName].push_back(newBlock);
        Node->SetVisible(false);

        std::string fileName = fmt::format("assets/textures/thumb/thumb_{}_{}.jpg", type, name);
        std::vector<uint8_t> outData;
        GetEngine().GetPakSystem().LoadFile(fileName, outData);
        Assets::GlobalTexturePool::LoadTexture(fileName, outData.data(), outData.size(), Vulkan::SamplerConfig());
    }
}

FBasicBlock* MagicaLegoGameInstance::GetBasicBlock(uint32_t BlockIdx)
{
    if (BlockIdx < BasicNodes.size())
    {
        return &BasicNodes[BlockIdx];
    }
    return nullptr;
}

bool MagicaLegoGameInstance::PlaceDynamicBlock(FPlacedBlock Block)
{
    if (Block.location.y < 0)
    {
        return false;
    }

    uint32_t blockHash = GetHashFromBlockLocation(Block.location);

    // Place it
    BlocksDynamics[blockHash] = Block;
    BlockRecords.push_back(Block);
    currentPreviewStep = static_cast<int>(BlockRecords.size());
    RebuildScene(BlocksDynamics, blockHash);
    lastPlacedLocation_ = Block.location;

    // random put1 or put2
    if (Block.modelId_ >= 0)
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

void MagicaLegoGameInstance::SwitchBasePlane(EBasePlane Type)
{
    currentBaseSize_ = Type;
    auto& allNodes = GetEngine().GetScene().Nodes();
    for (auto& Node : allNodes)
    {
        if (Node.GetName() == "BigBase" || Node.GetName() == "MidBase" || Node.GetName() == "SmallBase")
        {
            Node.SetVisible(false);
        }
    }

    switch (Type)
    {
    case EBasePlane::EBP_Big:
        for (auto& Node : allNodes)
        {
            if (Node.GetName() == "BigBase" || Node.GetName() == "MidBase" || Node.GetName() == "SmallBase")
            {
                Node.SetVisible(true);
            }
        }
        break;
    case EBasePlane::EBP_Mid:
        for (auto& Node : allNodes)
        {
            if (Node.GetName() == "MidBase" || Node.GetName() == "SmallBase")
            {
                Node.SetVisible(true);
            }
        }
        break;
    case EBasePlane::EBP_Small:
        for (auto& Node : allNodes)
        {
            if (Node.GetName() == "SmallBase")
            {
                Node.SetVisible(true);
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
            std::vector<FBasicBlock> TempVector(size);
            inFile.read(reinterpret_cast<char*>(TempVector.data()), size * sizeof(FBasicBlock));
            brushs = TempVector;
            inFile.read(reinterpret_cast<char*>(&size), sizeof(size));
            std::vector<FPlacedBlock> TempRecord(size);
            inFile.read(reinterpret_cast<char*>(TempRecord.data()), size * sizeof(FPlacedBlock));
            records = TempRecord;
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
        std::map<int16_t, int16_t> BrushMapping;
        for (auto& brush : save.brushs)
        {
            for (auto& newbrush : BasicNodes)
            {
                if (strcmp(brush.name, newbrush.name) == 0 && strcmp(brush.type, newbrush.type) == 0)
                {
                    BrushMapping[brush.brushId_] = newbrush.brushId_;
                    break;
                }
            }
        }

        for (auto& Record : BlockRecords)
        {
            if (Record.modelId_ >= 0)
            {
                if (BrushMapping.find(Record.modelId_) != BrushMapping.end())
                    Record.modelId_ = BrushMapping[Record.modelId_];
                else
                    Record.modelId_ = -1;
            }
        }
    }
    DumpReplayStep(static_cast<int>(BlockRecords.size()) - 1);
}

void MagicaLegoGameInstance::RebuildScene(std::unordered_map<uint32_t, FPlacedBlock>& Source, uint32_t newhash)
{
    GetEngine().GetScene().Nodes().erase(GetEngine().GetScene().Nodes().begin() + instanceCountBeforeDynamics_, GetEngine().GetScene().Nodes().end());
    
    for (auto& Block : Source)
    {
        if (Block.second.modelId_ >= 0)
        {
            auto BasicBlock = GetBasicBlock(Block.second.modelId_);
            if (BasicBlock)
            {
                // 这里要区分一下，因为目前rebuild流程是清理后重建，因此所有node都会被认为是首次放置，都会有一个单帧的velocity
                // 所以如果没有modelid的改变的话，采用原位替换
                glm::mat4 orientation = GetOrientationMatrix(Block.second.orientation);
                Assets::Node newNode = Assets::Node::CreateNode("blockInst", glm::translate(glm::mat4(1.0f), GetRenderLocationFromBlockLocation(Block.second.location)) * orientation, BasicBlock->modelId_,
                                                                instanceCountBeforeDynamics_ + GetHashFromBlockLocation(Block.second.location), newhash != Block.first);
                GetEngine().GetScene().Nodes().push_back(newNode);
            }
        }
    }

    GetEngine().GetScene().MarkDirty();
}

void MagicaLegoGameInstance::RebuildFromRecord(int timelapse)
{
    // 从record中临时重建出一个Dynamics然后用来重建scene
    std::unordered_map<uint32_t, FPlacedBlock> TempBlocksDynamics;
    for (int i = 0; i < timelapse; i++)
    {
        auto& Block = BlockRecords[i];
        TempBlocksDynamics[GetHashFromBlockLocation(Block.location)] = Block;

        if (currentCamMode_ == ECamMode::ECM_AutoFocus)
            cameraCenter_ = GetRenderLocationFromBlockLocation(Block.location);
    }
    RebuildScene(TempBlocksDynamics, -1);
}

void MagicaLegoGameInstance::CleanDynamicBlocks()
{
    BlocksDynamics.clear();
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

void MagicaLegoGameInstance::SetPlayReview(bool Enable)
{
    playReview_ = Enable;
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


const int THUMB_SIZE = 92;

void MagicaLegoGameInstance::GeneratingThmubnail()
{
    cameraArm_ = 0.7f;
    cameraCenter_ = glm::vec3(0, 0.045f, 0);
    realCameraCenter_ = cameraCenter_;
    GetEngine().GetUserSettings().TemporalFrames = 8;
    GetEngine().GetUserSettings().NumberOfSamples = 256;
    GetEngine().GetUserSettings().Denoiser = false;
    GetEngine().GetRenderer().SwapChain().UpdateEditorViewport(1920 / 2 - THUMB_SIZE / 2, 960 / 2 - THUMB_SIZE / 2, THUMB_SIZE, THUMB_SIZE);
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
        GetEngine().SaveScreenShot(fmt::format("../../../assets/textures/thumb/thumb_{}_{}", BasicNodes[currTask].type, BasicNodes[currTask].name), 1920 / 2 - THUMB_SIZE / 2, 960 / 2 - THUMB_SIZE / 2,
                                   THUMB_SIZE, THUMB_SIZE);
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
            GetEngine().GetRenderer().SwapChain().UpdateEditorViewport(0, 0, 1920, 960);
            return true;
        }

        return false;
    });
}
