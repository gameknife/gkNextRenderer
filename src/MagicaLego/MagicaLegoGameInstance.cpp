#include "MagicaLegoGameInstance.hpp"

#include <imgui.h>

#include "Assets/Scene.hpp"
#include "Editor/IconsFontAwesome6.h"

const int ICON_SIZE = 64;
const int PALATE_SIZE = 48;
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, NextRendererApplication* engine)
{
    return std::make_unique<MagicaLegoGameInstance>(config,engine);
}

MagicaLegoGameInstance::MagicaLegoGameInstance(Vulkan::WindowConfig& config, NextRendererApplication* engine):NextGameInstanceBase(config,engine),engine_(engine)
{
    config.Title = "MagicaLego";
    currentMode_ = ELM_Place;
}

bool MagicaLegoGameInstance::OnRenderUI()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    DrawLeftBar();
    DrawRightBar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    return true;
}

void MagicaLegoGameInstance::OnRayHitResponse(Assets::RayCastResult& rayResult)
{
    uint32_t instanceId = rayResult.InstanceId;

    if( currentMode_ == ELM_Dig )
    {
        if( GetEngine().GetScene().Nodes()[instanceId].GetName() == "blockInst" )
        {
            GetEngine().GetScene().Nodes().erase(GetEngine().GetScene().Nodes().begin() + instanceId);
        }
        return;
    }

    if( currentMode_ == ELM_Place )
    {
        glm::vec3 newLocation = glm::vec3(rayResult.HitPoint) + glm::vec3(rayResult.Normal) * 0.001f;
        // align with x: 0.08, y 0.08, z 0.095
        newLocation.x = round(newLocation.x / 0.08f) * 0.08f;
        newLocation.z = round(newLocation.z / 0.08f) * 0.08f;
        newLocation.y = round((newLocation.y - 0.0475f) / 0.095f) * 0.095f;

        auto Block = GetBasicBlock(currentBlockIdx_);
        if(Block)
        {
            Assets::Node newNode = Assets::Node::CreateNode("blockInst", glm::translate(glm::mat4(1.0f), newLocation), Block->modelId_, false);
            GetEngine().GetScene().Nodes().push_back(newNode);
        }
    }
}

void MagicaLegoGameInstance::OnSceneLoaded()
{
    NextGameInstanceBase::OnSceneLoaded();

    AddBasicBlock("Block1x1_Yellow");
    AddBasicBlock("Block1x1_Blue");
    AddBasicBlock("Block1x1_Red");
    AddBasicBlock("Block1x1_White");
    
    AddBasicBlock("Block1x1_Metal");
    AddBasicBlock("Block1x1_Glass");
    AddBasicBlock("Block1x1_Light");
}

void MagicaLegoGameInstance::OnSceneUnloaded()
{
    NextGameInstanceBase::OnSceneUnloaded();

    BasicNodes.clear();
    BlocksFromScene.clear();
    BlocksDynamics.clear();
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

void MagicaLegoGameInstance::RebuildScene()
{
    
}

bool SelectButton(const char* label, bool selected)
{
    if(selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f,0.2f,0.8f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f,0.2f,0.8f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f,0.2f,0.8f,1));
    }
    bool result = ImGui::Button(label, ImVec2(ICON_SIZE, ICON_SIZE));
    if(selected)
    {
        ImGui::PopStyleColor(3);
    }
    return result;
}

bool MaterialButton(FBasicBlock& block, float WindowWidth)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 color = ImVec4(block.color.r, block.color.g, block.color.b, block.color.a);
    
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
    
    return result;
}

void MagicaLegoGameInstance::DrawLeftBar()
{
    const ImVec2 pos = ImVec2(0, 0);
    const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(300,viewportSize.y));
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
            currentMode_ = ELM_Dig;
            GetEngine().GetUserSettings().ShowEdge = false;
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CUBES_STACKED, currentMode_ == ELM_Place) )
        {
            currentMode_ = ELM_Place;
            GetEngine().GetUserSettings().ShowEdge = false;
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_HAND_POINTER, currentMode_ == ELM_Select) )
        {
            currentMode_ = ELM_Select;
            GetEngine().GetUserSettings().ShowEdge = true;
        }
        ImGui::SeparatorText("Pattern");
        if( ImGui::Button("Select") )
        {
            
        }
        ImGui::SeparatorText("Redo / Undo");
        if( ImGui::Button("Select") )
        {
            
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
    ImGui::SetNextWindowSize(ImVec2(300,viewportSize.y));
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
            if( MaterialButton(block, WindowWidth) )
            {
                currentBlockIdx_ = static_cast<int>(i);
            }
        }
    }
    ImGui::End();
}
