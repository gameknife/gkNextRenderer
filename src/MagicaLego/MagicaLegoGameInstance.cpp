#include "MagicaLegoGameInstance.hpp"

#include <fstream>
#include <fmt/printf.h>

#include "Assets/Scene.hpp"
#include "Utilities/FileHelper.hpp"
#include "MagicaLegoUserInterface.hpp"

const glm::i16vec3 INVALID_POS(0,-10,0);

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<MagicaLegoGameInstance>(config,options,engine);
}

glm::vec3 GetRenderLocationFromBlockLocation( glm::i16vec3 BlockLocation )
{
    glm::vec3 newLocation;
    newLocation.x = static_cast<float>(BlockLocation.x) * 0.08f;
    newLocation.y = static_cast<float>(BlockLocation.y) * 0.095f;
    newLocation.z = static_cast<float>(BlockLocation.z) * 0.08f;
    return newLocation;
}

glm::i16vec3 GetBlockLocationFromRenderLocation( glm::vec3 RenderLocation )
{
    glm::i16vec3 newLocation;
    newLocation.x = static_cast<int16_t>(round(RenderLocation.x / 0.08f));
    newLocation.y = static_cast<int16_t>(round((RenderLocation.y - 0.0475f) / 0.095f));
    newLocation.z = static_cast<int16_t>(round(RenderLocation.z / 0.08f));
    return newLocation;
}

uint32_t GetHashFromBlockLocation(const glm::i16vec3& BlockLocation) {
    uint32_t x = static_cast<uint32_t>(BlockLocation.x) & 0xFFFF; // 取 16 位
    uint32_t y = static_cast<uint32_t>(BlockLocation.y) & 0xFFFF; // 取 16 位
    uint32_t z = static_cast<uint32_t>(BlockLocation.z) & 0xFFFF; // 取 16 位

    // 混合三个 16 位分量
    uint32_t hash = x;
    hash = hash * 31 + y; // 使用质数 31 增加分布性
    hash = hash * 31 + z; 

    return hash;
}

MagicaLegoGameInstance::MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine):NextGameInstanceBase(config,options,engine),engine_(engine)
{
    // windows config
    config.Title = "MagicaLego";
    config.Height = 960;
    config.Width = 1920;
    config.ForceSDR = true;

    // options
    options.SceneName = "legobricks.glb";
    options.Samples = 8;
    options.Temporal = 16;
    options.ForceSDR = true;
    options.RendererType = 0;

    // mode init
    SetBuildMode(ELM_Place);
    SetCameraMode(ECM_Orbit);

    // control init
    resetMouse_ = true;
    cameraRotX_ = 45;
    cameraRotY_ = 30;

    // ui
    UserInterface_ = std::make_unique<MagicaLegoUserInterface>(this);

    previewWindowTimer_ = 0.1;

    lastSelectLocation_ = INVALID_POS;
    lastPlacedLocation_ = INVALID_POS;
}

void MagicaLegoGameInstance::OnRayHitResponse(Assets::RayCastResult& rayResult)
{
    if(!bMouseLeftDown_)
    {
        return;
    }

    uint32_t instanceId = rayResult.InstanceId;
    auto* Node = GetEngine().GetScene().GetNodeByInstanceId(instanceId);
    
    if( currentMode_ == ELM_Dig )
    {
        if( lastDownFrameNum_ + 1 == GetEngine().GetRenderer().FrameCount())
        {
            if( Node && Node->GetName() == "blockInst" )
            {
                glm::i16vec3 blockLocation = GetBlockLocationFromRenderLocation( glm::vec3((Node->WorldTransform() * glm::vec4(0,0.0475f,0,1))) );
                FPlacedBlock block { blockLocation, -1 };
                PlaceDynamicBlock(block);
            }
        }
        return;
    }
    if( currentMode_ == ELM_Place )
    {
        for( auto& id : oneLinePlacedInstance_ )
        {
            if( instanceId == id)
            {
                return;
            }
        }
        
        glm::vec3 newLocation = glm::vec3(rayResult.HitPoint) + glm::vec3(rayResult.Normal) * 0.01f;
        glm::i16vec3 blockLocation = GetBlockLocationFromRenderLocation(newLocation);
        if(blockLocation == lastPlacedLocation_) return;
        PlaceDynamicBlock({ blockLocation, currentBlockIdx_ });
        oneLinePlacedInstance_.push_back( GetHashFromBlockLocation(blockLocation) + instanceCountBeforeDynamics_ );
    }
    if( currentMode_ == ELM_Select )
    {
        if( Node->GetName() == "blockInst")
        {
            lastSelectLocation_ = GetBlockLocationFromRenderLocation( glm::vec3((Node->WorldTransform() * glm::vec4(0,0.0475f,0,1))) );
        }
        else
        {
            lastSelectLocation_ = INVALID_POS;
        }
        if(currentCamMode_ == ECM_AutoFocus && lastSelectLocation_ != INVALID_POS)
            cameraCenter_ = GetRenderLocationFromBlockLocation(lastSelectLocation_);
    }
}

bool MagicaLegoGameInstance::OverrideModelView(glm::mat4& OutMatrix) const
{
    float xRotation = cameraRotX_; // 例如绕X轴旋转45度
    float yRotation = cameraRotY_; // 例如上下偏转30度
    float armLength = 5.0f;
        
    glm::vec3 cameraPos;
    cameraPos.x = realCameraCenter_.x + armLength * cos(glm::radians(yRotation)) * cos(glm::radians(xRotation));
    cameraPos.y = realCameraCenter_.y + armLength * sin(glm::radians(yRotation));
    cameraPos.z = realCameraCenter_.z + armLength * cos(glm::radians(yRotation)) * sin(glm::radians(xRotation));

    // calcate the view forward and left
    glm::vec3 forward = glm::normalize(realCameraCenter_ - cameraPos); forward.y = 0.0f;
    glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));  left.y = 0.0f;
    
    panForward_ = glm::normalize(forward);
    panLeft_ = glm::normalize(left);
    
    OutMatrix = glm::lookAtRH(cameraPos, realCameraCenter_, glm::vec3(0.0f, 1.0f, 0.0f));
    return true;
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
    for( int x = 0; x < 21; x++ )
    {
        for( int z = 0; z < 21; z++ )
        {
            std::string NodeName = "BigBase";
            if( x >= 7 && x <= 13 && z >= 7 && z <= 13 )
            {
                NodeName = "MidBase";
            }
            if( x == 10 && z == 10 )
            {
                NodeName = "SmallBase";
            }
            glm::vec3 location = glm::vec3((x - 10.25) * 0.96f, 0.0f, (z - 9.5) * 0.96f);
            Assets::Node newNode = Assets::Node::CreateNode(NodeName, glm::translate( glm::mat4(1), location), modelId, instanceId, false);
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
    
    instanceCountBeforeDynamics_ = static_cast<int>(GetEngine().GetScene().Nodes().size());
    SwitchBasePlane(EBP_Small);

    GetEngine().PlaySound("assets/sfx/bgm.mp3", true, 0.5f);
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
            case GLFW_KEY_Q: SetBuildMode(ELM_Dig); break;
            case GLFW_KEY_W: SetBuildMode(ELM_Place); break;
            case GLFW_KEY_E: SetBuildMode(ELM_Select); break;

            case GLFW_KEY_A: SetCameraMode(ECM_Pan); break;
            case GLFW_KEY_S: SetCameraMode(ECM_Orbit); break;
            case GLFW_KEY_D: SetCameraMode(ECM_AutoFocus); break;

            case GLFW_KEY_1: SwitchBasePlane(EBP_Big); break;
            case GLFW_KEY_2: SwitchBasePlane(EBP_Mid); break;
            case GLFW_KEY_3: SwitchBasePlane(EBP_Small); break;
            default: break;
        }
    }
    else if(action == GLFW_RELEASE)
    {
        
    }
    return true;
}

bool MagicaLegoGameInstance::OnCursorPosition(double xpos, double ypos)
{
    if(resetMouse_)
    {
        mousePos_ = glm::dvec2(xpos, ypos);
        resetMouse_ = false;
    }
    
    glm::dvec2 delta = glm::dvec2(xpos, ypos) - mousePos_;

    if((currentCamMode_ == ECM_Orbit) || (currentCamMode_ == ECM_AutoFocus))
    {
        cameraRotX_ += static_cast<float>(delta.x) * cameraMultiplier_;
        cameraRotY_ += static_cast<float>(delta.y) * cameraMultiplier_;
    }

    if(currentCamMode_ == ECM_Pan)
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
        if(currentCamMode_ == ECM_AutoFocus && currentMode_ == ELM_Place && lastPlacedLocation_ != INVALID_POS)
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

void MagicaLegoGameInstance::TryChangeSelectionBrushIdx(int idx)
{
    if( currentMode_ == ELM_Select )
    {
        if(lastSelectLocation_ != INVALID_POS)
        {
            FPlacedBlock block { lastSelectLocation_, idx };
            PlaceDynamicBlock(block);
        }
    }
}

void MagicaLegoGameInstance::SetPlayStep(int step)
{
    if( step >= 0 && step <= BlockRecords.size() )
    {
        currentPreviewStep = step;
        RebuildFromRecord(currentPreviewStep);
    }
}

void MagicaLegoGameInstance::DumpReplayStep(int step)
{
    if(step <= BlockRecords.size())
    {
        BlockRecords.erase( BlockRecords.begin() + step, BlockRecords.end());
    
        CleanDynamicBlocks();
        for( auto& Block : BlockRecords )
        {
            BlocksDynamics[GetHashFromBlockLocation(Block.location)] = Block;
        }
        RebuildScene(BlocksDynamics);
    }
}

void MagicaLegoGameInstance::AddBlockGroup(std::string typeName)
{
    auto& allNodes = GetEngine().GetScene().Nodes();
    for ( auto& Node : allNodes )
    {
        if(Node.GetName().find(typeName, 0) == 0)
        {
            AddBasicBlock(Node.GetName(), typeName);
        }
    }
}

void MagicaLegoGameInstance::AddBasicBlock(std::string blockName, std::string typeName)
{
    auto& Scene = GetEngine().GetScene();
    auto Node = Scene.GetNode(blockName);
    if(Node)
    {
        FBasicBlock newBlock;
        std::string name = "#" + blockName.substr(strlen(typeName.c_str()) + 1);
        std::strcpy(newBlock.name, name.c_str());
        newBlock.name[127] = 0;
        std::string type = typeName;
        std::strcpy(newBlock.type, type.c_str());
        newBlock.type[127] = 0;
        newBlock.modelId_ = Node->GetModel();
        newBlock.brushId_ = static_cast<int>(BasicNodes.size());
        uint32_t mat_id = Scene.GetModel(newBlock.modelId_)->Materials()[0];
        auto mat = Scene.GetMaterial(mat_id);
        if(mat)
        {
            newBlock.matType = static_cast<int>(mat->MaterialModel);
            newBlock.color = mat->Diffuse;
        }
        BasicNodes.push_back(newBlock);
        BasicBlockTypeMap[typeName].push_back(newBlock);
        Node->SetVisible(false);
    }
}

FBasicBlock* MagicaLegoGameInstance::GetBasicBlock(uint32_t BlockIdx)
{
    if(BlockIdx < BasicNodes.size())
    {
        return &BasicNodes[BlockIdx];
    }
    return nullptr;
}

void MagicaLegoGameInstance::PlaceDynamicBlock(FPlacedBlock Block)
{
    if(Block.location.y < 0)
    {
        return;
    }
    
    uint32_t blockHash = GetHashFromBlockLocation(Block.location);
    
    // Place it
    BlocksDynamics[blockHash] = Block;
    BlockRecords.push_back(Block);
    currentPreviewStep = static_cast<int>(BlockRecords.size());
    RebuildScene(BlocksDynamics);
    lastPlacedLocation_ = Block.location;
    
    // random put1 or put2
    if(Block.modelId_ >= 0)
    {
        int random = rand();
        if( random % 3 == 0 )
            GetEngine().PlaySound("assets/sfx/put2.wav");
        else if( random % 3 == 1 )
            GetEngine().PlaySound("assets/sfx/put1.wav");
        else
            GetEngine().PlaySound("assets/sfx/put3.wav");
    }

}

void MagicaLegoGameInstance::SwitchBasePlane(EBasePlane Type)
{
    currentBaseSize_ = Type;
    // BigBase, MidBase, SmallBase
    // first hide all
    auto& allNodes = GetEngine().GetScene().Nodes();
    for ( auto& Node : allNodes )
    {
        if(Node.GetName() == "BigBase" || Node.GetName() == "MidBase" || Node.GetName() == "SmallBase")
        {
            Node.SetVisible(false);
        }
    }

    switch (Type)
    {
        case EBP_Big:
            for ( auto& Node : allNodes )
            {
                if(Node.GetName() == "BigBase" || Node.GetName() == "MidBase" || Node.GetName() == "SmallBase")
                {
                    Node.SetVisible(true);
                }
            }
            break;
        case EBP_Mid:
            for ( auto& Node : allNodes )
            {
                if(Node.GetName() == "MidBase" || Node.GetName() == "SmallBase")
                {
                    Node.SetVisible(true);
                }
            }
            break;
        case EBP_Small:
            for ( auto& Node : allNodes )
            {
                if(Node.GetName() == "SmallBase")
                {
                    Node.SetVisible(true);
                }
            }
            break;
    }
}

void MagicaLegoGameInstance::CleanUp()
{
    BlockRecords.clear();
    CleanDynamicBlocks();
    RebuildScene(BlocksDynamics);
}

void FMagicaLegoSave::Save(std::string filename)
{
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/") + filename + ".mls";;
    
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
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/") + filename + ".mls";;
    
    std::ifstream inFile(path, std::ios::binary);
    if(inFile.is_open())
    {
        int ver = 0;
        inFile.read(reinterpret_cast<char*>(&ver), sizeof(ver));
        if(ver == MAGICALEGO_SAVE_VERSION)
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

    if(save.version != 0)
    {
        std::map<int, int> BrushMapping;
        for( auto& brush : save.brushs )
        {
            for( auto& newbrush : BasicNodes )
            {
                if( strcmp(brush.name, newbrush.name) == 0 && strcmp(brush.type, newbrush.type) == 0 )
                {
                    BrushMapping[brush.brushId_] = newbrush.brushId_;
                    break;
                }
            }
        }

        for( auto& Record : BlockRecords )
        {
            if(Record.modelId_ >= 0)
            {
                if( BrushMapping.find(Record.modelId_) != BrushMapping.end() )
                    Record.modelId_ = BrushMapping[Record.modelId_];
                else
                    Record.modelId_ = -1;  
            }
        }
    }
    DumpReplayStep(static_cast<int>(BlockRecords.size()) - 1);
}

void MagicaLegoGameInstance::RebuildScene(std::unordered_map<uint32_t, FPlacedBlock>& Source)
{
    GetEngine().GetScene().Nodes().erase(GetEngine().GetScene().Nodes().begin() + instanceCountBeforeDynamics_, GetEngine().GetScene().Nodes().end());

    uint32_t counter = 0;
    for ( auto& Block : Source )
    {
        if(Block.second.modelId_ >= 0)
        {
            auto BasicBlock = GetBasicBlock(Block.second.modelId_);
            if(BasicBlock)
            {
                // with stable instance id
                Assets::Node newNode = Assets::Node::CreateNode("blockInst", glm::translate(glm::mat4(1.0f), GetRenderLocationFromBlockLocation(Block.second.location)), BasicBlock->modelId_, instanceCountBeforeDynamics_ + GetHashFromBlockLocation(Block.second.location), false);
                GetEngine().GetScene().Nodes().push_back(newNode);
            }
        }
    }
}

void MagicaLegoGameInstance::RebuildFromRecord(int timelapse)
{
    // 从record中临时重建出一个Dynamics然后用来重建scene
    std::unordered_map<uint32_t, FPlacedBlock> TempBlocksDynamics;
    for( int i = 0; i < timelapse; i++ )
    {
        auto& Block = BlockRecords[i];
        TempBlocksDynamics[GetHashFromBlockLocation(Block.location)] = Block;

        if(currentCamMode_ == ECM_AutoFocus)
            cameraCenter_ = GetRenderLocationFromBlockLocation(Block.location);
    }
    RebuildScene(TempBlocksDynamics);
}

void MagicaLegoGameInstance::CleanDynamicBlocks()
{
    BlocksDynamics.clear();
}

void MagicaLegoGameInstance::OnTick(double deltaSeconds)
{
    realCameraCenter_ = glm::mix( realCameraCenter_, cameraCenter_, 0.1f );

    if( bMouseLeftDown_ )
    {
        GetEngine().GetUserSettings().RequestRayCast = true;
    }

    previewWindowElapsed_ += deltaSeconds;
    if(playReview_ && previewWindowElapsed_ > previewWindowTimer_)
    {
        previewWindowElapsed_ = 0.0;
        
        if(currentPreviewStep < BlockRecords.size())
        {
            currentPreviewStep = currentPreviewStep + 1;
            RebuildFromRecord(currentPreviewStep);
        }
        else
        {
            playReview_ = false;
        }
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
    GetEngine().GetUserSettings().ShowEdge = (currentMode_ == ELM_Select);
    lastSelectLocation_ = INVALID_POS;
}

void MagicaLegoGameInstance::SetCameraMode(ECamMode mode)
{
    currentCamMode_ = mode;
}

int MagicaLegoGameInstance::ConvertBrushIdxToNextType(const std::string& prefix, int idx) const
{
    std::string subName = BasicNodes[idx].name;
    if( BasicBlockTypeMap.find(prefix) != BasicBlockTypeMap.end() )
    {
        for( auto& block : BasicBlockTypeMap.at(prefix) )
        {
            if( strcmp(block.name, subName.c_str()) == 0 )
            {
                return block.brushId_;
            }
        }
    }
    return -1;
}
