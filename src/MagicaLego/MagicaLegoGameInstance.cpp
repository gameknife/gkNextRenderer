#include "MagicaLegoGameInstance.hpp"

#include <fstream>
#include <imgui.h>
#include <imgui_stdlib.h>

#include "Assets/Scene.hpp"
#include "Editor/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"

const int ICON_SIZE = 64;
const int PALATE_SIZE = 46;
const int BUTTON_SIZE = 36;
const int SIDE_BAR_WIDTH = 240;

#pragma optimize("", off)
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<MagicaLegoGameInstance>(config,options,engine);
}

glm::vec3 GetRenderLocationFromBlockLocation( glm::ivec3 BlockLocation )
{
    glm::vec3 newLocation;
    newLocation.x = static_cast<float>(BlockLocation.x) * 0.08f;
    newLocation.y = static_cast<float>(BlockLocation.y) * 0.095f;
    newLocation.z = static_cast<float>(BlockLocation.z) * 0.08f;
    return newLocation;
}

glm::ivec3 GetBlockLocationFromRenderLocation( glm::vec3 RenderLocation )
{
    glm::ivec3 newLocation;
    newLocation.x = static_cast<int>(round(RenderLocation.x / 0.08f));
    newLocation.y = static_cast<int>(round((RenderLocation.y - 0.0475f) / 0.095f));
    newLocation.z = static_cast<int>(round(RenderLocation.z / 0.08f));
    return newLocation;
}

uint64_t GetHashFromBlockLocation( glm::ivec3 BlockLocation )
{
    uint64_t hash = 17;
    hash = hash * 31 + BlockLocation.x;
    hash = hash * 31 + BlockLocation.y;
    hash = hash * 31 + BlockLocation.z;
    return hash;
}

MagicaLegoGameInstance::MagicaLegoGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine):NextGameInstanceBase(config,options,engine),engine_(engine)
{
    config.Title = "MagicaLego";
    config.Height = 1080;
    config.Width = 2160;
    
    options.SceneName = "legobricks.glb";
    options.Samples = 4;
    options.Temporal = 16;
    options.ForceSDR = true;
    SetBuildMode(ELM_Place);
}

void MagicaLegoGameInstance::OnRayHitResponse(Assets::RayCastResult& rayResult)
{
    uint32_t instanceId = rayResult.InstanceId;

    if( currentMode_ == ELM_Dig )
    {
        auto& Node = GetEngine().GetScene().Nodes()[instanceId];
        if( Node.GetName() == "blockInst" )
        {
            // 这里可以从Node上取到location，但是WorldMatrix需要Decompose，简单点，直接反向normal，找一个内部位置
            glm::vec3 inLocation = glm::vec3(rayResult.HitPoint) - glm::vec3(rayResult.Normal) * 0.001f;
            glm::ivec3 blockLocation = GetBlockLocationFromRenderLocation(inLocation);
            FPlacedBlock block { blockLocation, -1 };
            PlaceDynamicBlock(block);
        }
        return;
    }
    if( currentMode_ == ELM_Place )
    {
        glm::vec3 newLocation = glm::vec3(rayResult.HitPoint) + glm::vec3(rayResult.Normal) * 0.001f;
        glm::ivec3 blockLocation = GetBlockLocationFromRenderLocation(newLocation);
        FPlacedBlock block { blockLocation, currentBlockIdx_ };
        PlaceDynamicBlock(block);
    }
}

void MagicaLegoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    // Add the pre-defined blocks from assets
    AddBasicBlock("Block1x1_Yellow");
    AddBasicBlock("Block1x1_Blue");
    AddBasicBlock("Block1x1_Red");
    AddBasicBlock("Block1x1_White");
    
    AddBasicBlock("Block1x1_Metal");
    AddBasicBlock("Block1x1_Glass");
    AddBasicBlock("Block1x1_Light");

    instanceCountBeforeDynamics_ = static_cast<int>(GetEngine().GetScene().Nodes().size() - 1);
}

void MagicaLegoGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();

    BasicNodes.clear();
    BlocksFromScene.clear();
    BlocksDynamics.clear();
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
            default: break;
        }
    }
    else if(action == GLFW_RELEASE)
    {
        
    }
    return true;
}

bool MagicaLegoGameInstance::OnMouseButton(int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
    {
        GetEngine().GetUserSettings().RequestRayCast = true;
        return true;
    }
    else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
    {
        return true;
    }
    return false;
}

void MagicaLegoGameInstance::CleanUp()
{
    BlockRecords.clear();
    BlocksDynamics.clear();
    RebuildScene(BlocksDynamics);
}

void MagicaLegoGameInstance::SaveRecord(std::string filename)
{
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/") + filename + ".mls";;
    
    // direct save records to file
    std::ofstream outFile(path, std::ios::binary);
    size_t size = BlockRecords.size();
    outFile.write(reinterpret_cast<const char*>(&size), sizeof(size));
    outFile.write(reinterpret_cast<const char*>(BlockRecords.data()), size * sizeof(FPlacedBlock));
    outFile.close();
}

void MagicaLegoGameInstance::LoadRecord(std::string filename)
{
    std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/") + filename + ".mls";;
    
    std::ifstream inFile(path, std::ios::binary);
    if(inFile.is_open())
    {
        size_t size;
        inFile.read(reinterpret_cast<char*>(&size), sizeof(size));
        std::vector<FPlacedBlock> TempVector(size);
        inFile.read(reinterpret_cast<char*>(TempVector.data()), size * sizeof(FPlacedBlock));
        inFile.close();

        BlockRecords = TempVector;

        BlocksDynamics.clear();
        for( auto& Block : BlockRecords )
        {
            BlocksDynamics[GetHashFromBlockLocation(Block.location)] = Block;
        }
        RebuildScene(BlocksDynamics);
    }
}

void MagicaLegoGameInstance::AddBasicBlock(std::string blockName)
{
    auto& Scene = GetEngine().GetScene();
    auto Node = Scene.GetNode(blockName);
    if(Node)
    {
        FBasicBlock newBlock;
        newBlock.modelId_ = Node->GetModel();
        uint32_t mat_id = Scene.GetModel(newBlock.modelId_)->Materials()[0];
        auto mat = Scene.GetMaterial(mat_id);
        if(mat)
        {
            newBlock.matType = static_cast<int>(mat->MaterialModel);
            newBlock.color = mat->Diffuse;
        }
        BasicNodes.push_back(newBlock);
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
    uint64_t blockHash = GetHashFromBlockLocation(Block.location);
    // Place it
    BlocksDynamics[blockHash] = Block;
    BlockRecords.push_back(Block);
    currentPreviewStep = static_cast<int>(BlockRecords.size());
    RebuildScene(BlocksDynamics);
}

void MagicaLegoGameInstance::RebuildScene(std::unordered_map<uint64_t, FPlacedBlock>& Source)
{
    GetEngine().GetScene().Nodes().erase(GetEngine().GetScene().Nodes().begin() + instanceCountBeforeDynamics_, GetEngine().GetScene().Nodes().end());
    for ( auto& Block : Source )
    {
        if(Block.second.modelId_ >= 0)
        {
            auto BasicBlock = GetBasicBlock(Block.second.modelId_);
            if(BasicBlock)
            {
                Assets::Node newNode = Assets::Node::CreateNode("blockInst", glm::translate(glm::mat4(1.0f), GetRenderLocationFromBlockLocation(Block.second.location)), BasicBlock->modelId_, false);
                GetEngine().GetScene().Nodes().push_back(newNode);
            }
        }
    }
}

bool MagicaLegoGameInstance::OnRenderUI()
{
    if(playReview_ && GetEngine().GetRenderer().frameCount_ % 30 == 0)
    {
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
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    DrawLeftBar();
    DrawRightBar();
    ImGui::PopStyleVar(2);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18);
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 18);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    DrawTimeline();
    ImGui::PopStyleVar(3);
    return true;
}

void MagicaLegoGameInstance::RebuildFromRecord(int timelapse)
{
    // 从record中临时重建出一个Dynamics然后用来重建scene
    std::unordered_map<uint64_t, FPlacedBlock> TempBlocksDynamics;
    for( int i = 0; i < timelapse; i++ )
    {
        auto& Block = BlockRecords[i];
        TempBlocksDynamics[GetHashFromBlockLocation(Block.location)] = Block;
    }
    RebuildScene(TempBlocksDynamics);
}

bool SelectButton(const char* label, bool selected)
{
    if(selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.6f,0.8f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f,0.6f,0.8f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f,0.6f,0.8f,1));
    }
    bool result = ImGui::Button(label, ImVec2(ICON_SIZE, ICON_SIZE));
    if(selected)
    {
        ImGui::PopStyleColor(3);
    }
    return result;
}

bool MaterialButton(FBasicBlock& block, float WindowWidth, bool selected)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 color = ImVec4(block.color.r, block.color.g, block.color.b, block.color.a);

    if(selected)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.6f,0.85f,1.0f,1));
    }

    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);

    ImGui::PushID(block.modelId_);
    bool result = ImGui::Button("##Block", ImVec2(PALATE_SIZE, PALATE_SIZE));
    ImGui::PopID();

    float last_button_x2 = ImGui::GetItemRectMax().x;
    float next_button_x2 = last_button_x2 + style.ItemSpacing.x + PALATE_SIZE; // Expected position if next button was on same line
    if (next_button_x2 < WindowWidth)
        ImGui::SameLine();
    
    ImGui::PopStyleColor(3);

    if(selected)
    {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    
    return result;
}

void MagicaLegoGameInstance::DrawLeftBar()
{
    const ImVec2 pos = ImVec2(0, 0);
    const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(SIDE_BAR_WIDTH,viewportSize.y));
    ImGui::SetNextWindowBgAlpha(0.9f);
    const int flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;
    
    if (ImGui::Begin("Place & Dig", 0, flags))
    {
        ImGui::SeparatorText("Mode");
        if( SelectButton(ICON_FA_PERSON_DIGGING, currentMode_ == ELM_Dig) )
        {
            SetBuildMode(ELM_Dig);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CUBES_STACKED, currentMode_ == ELM_Place) )
        {
            SetBuildMode(ELM_Place);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_HAND_POINTER, currentMode_ == ELM_Select) )
        {
            SetBuildMode(ELM_Select);
        }
        ImGui::SeparatorText("Camera");
        if( SelectButton(ICON_FA_UP_DOWN_LEFT_RIGHT, true) )
        {

        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CAMERA_ROTATE, false) )
        {

        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CIRCLE_DOT, false) )
        {

        }
        
        ImGui::SeparatorText("Files");
        static std::string selected_filename = "";
        static std::string new_filename = "magicalego";
        if (ImGui::BeginListBox("##listbox 2", ImVec2(-FLT_MIN, 11 * ImGui::GetTextLineHeightWithSpacing())))
        {
            
            std::string path = Utilities::FileHelper::GetPlatformFilePath("assets/legos/");
            for (const auto & entry : std::filesystem::directory_iterator(path))
            {
                if( entry.path().extension() != ".mls" )
                {
                    break;
                }
                std::string filename = entry.path().filename().string();
                // remove extension
                filename = filename.substr(0, filename.size() - 4);

                bool is_selected = (selected_filename == filename);
                ImGuiSelectableFlags flags = (selected_filename == filename) ? ImGuiSelectableFlags_Highlight : 0;
                if (ImGui::Selectable(filename.c_str(), is_selected, flags))
                {
                    selected_filename = filename;
                    new_filename = filename;
                }
                // Set the initial focus when opening the combo (scrolling + keyboard navigation focus)
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndListBox();
        }
        
        if( ImGui::Button(ICON_FA_FILE, ImVec2(BUTTON_SIZE, BUTTON_SIZE) ))
        {
            CleanUp();
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_FOLDER_OPEN, ImVec2(BUTTON_SIZE, BUTTON_SIZE) ))
        {
            LoadRecord(selected_filename);
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_FLOPPY_DISK, ImVec2(BUTTON_SIZE, BUTTON_SIZE)) )
        {
            if(selected_filename != "") SaveRecord(selected_filename);
        }
        ImGui::SameLine();
        // 模态新保存
        if (ImGui::Button(ICON_FA_DOWNLOAD, ImVec2(BUTTON_SIZE, BUTTON_SIZE)))
            ImGui::OpenPopup(ICON_FA_DOWNLOAD);
        
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(ICON_FA_DOWNLOAD, NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Input new filename here.\nDO NOT need extension.");
            ImGui::Separator();
            
            ImGui::InputText("##newscenename", &new_filename);

            if (ImGui::Button("Save", ImVec2(120, 0)))
            {
                SaveRecord(new_filename);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

void MagicaLegoGameInstance::DrawRightBar()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 pos = ImVec2(viewportSize.x, 0);
    const ImVec2 posPivot = ImVec2(1.0f, 0.0f);
    
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(SIDE_BAR_WIDTH,viewportSize.y));
    ImGui::SetNextWindowBgAlpha(0.9f);
    const int flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Color Pallete", 0, flags))
    {
        ImGui::SeparatorText("Blocks");
        float WindowWidth = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for( size_t i = 0; i < BasicNodes.size(); i++ )
        {
            auto& block = BasicNodes[i];
            if( MaterialButton(block, WindowWidth, currentBlockIdx_ == i) )
            {
                currentBlockIdx_ = static_cast<int>(i);
            }
        }
    }
    ImGui::End();
}

void MagicaLegoGameInstance::DrawTimeline()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 pos = ImVec2(viewportSize.x * 0.5f, viewportSize.y - 100);
    const ImVec2 posPivot = ImVec2(0.5f, 0.5f);
    const float width = viewportSize.x - SIDE_BAR_WIDTH * 2 - 100;
    ImGuiStyle& style = ImGui::GetStyle();
    
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(width, 90));
    ImGui::SetNextWindowBgAlpha(0.5f);
    const int flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Timeline", 0, flags))
    {
        ImGui::PushItemWidth(width - 20);
        static int timeline = 0;
        int size = static_cast<int>(BlockRecords.size());
        if( ImGui::SliderInt("##Timeline", &currentPreviewStep, 0, size) )
        {
            RebuildFromRecord(currentPreviewStep);
        }
        ImGui::PopItemWidth();

        ImVec2 NextLinePos = ImGui::GetCursorPos();
        ImGui::SetCursorPos( NextLinePos + ImVec2(width * 0.5f - (style.ItemSpacing.x * 4 + BUTTON_SIZE * 3) * 0.5f,0) );
        ImGui::BeginGroup();
        if( ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(BUTTON_SIZE,BUTTON_SIZE)) )
        {
            currentPreviewStep = currentPreviewStep - 1;
            RebuildFromRecord(currentPreviewStep);
        }
        ImGui::SameLine();
        if( ImGui::Button(playReview_ ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(BUTTON_SIZE,BUTTON_SIZE)))
        {
            playReview_ = !playReview_;
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(BUTTON_SIZE,BUTTON_SIZE)))
        {
            if(currentPreviewStep < BlockRecords.size())
            {
                currentPreviewStep = currentPreviewStep + 1;
                RebuildFromRecord(currentPreviewStep);
            }
        }
        ImGui::EndGroup();

        ImGui::SetCursorPos( NextLinePos + ImVec2(width - (style.ItemSpacing.x * 4 + BUTTON_SIZE * 3) * 0.5f,0) );
        ImGui::BeginGroup();
        if( ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT, ImVec2(BUTTON_SIZE,BUTTON_SIZE)) )
        {
            
        }
        ImGui::EndGroup();
    }
    ImGui::End();
}

void MagicaLegoGameInstance::SetBuildMode(ELegoMode mode)
{
    currentMode_ = mode;
    GetEngine().GetUserSettings().ShowEdge = (currentMode_ == ELM_Select);
}
#pragma optimize("", on)
