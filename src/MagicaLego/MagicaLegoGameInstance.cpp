#include "MagicaLegoGameInstance.hpp"

#include <imgui.h>

#include "Assets/Scene.hpp"
#include "Editor/IconsFontAwesome6.h"

const int ICON_SIZE = 64;
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
    Assets::Node* origin = GetEngine().GetScene().GetNode("Block1x1");
    if(origin)
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
            Assets::Node newNode = Assets::Node::CreateNode("blockInst", glm::translate(glm::mat4(1.0f), newLocation), origin->GetModel(), false);
            GetEngine().GetScene().Nodes().push_back(newNode);
        }
    }
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
        ImGui::SeparatorText("Plastic");
        if( ImGui::ColorButton("##red", ImVec4(1,1,0.2f,1)) )
        {
            
        }
        ImGui::SameLine();
        if( ImGui::ColorButton("##green", ImVec4(0.2f,1,0.2f,1)) )
        {
            
        }
       
        ImGui::SeparatorText("Metal");
        if( ImGui::ColorButton("##red", ImVec4(1,1,0.2f,1)) )
        {
            
        }
        ImGui::SameLine();
        if( ImGui::ColorButton("##green", ImVec4(0.2f,1,0.2f,1)) )
        {
            
        }
       
        ImGui::SeparatorText("Glass");
        if( ImGui::ColorButton("##red", ImVec4(1,1,0.2f,1)) )
        {
            
        }
        ImGui::SameLine();
        if( ImGui::ColorButton("##green", ImVec4(0.2f,1,0.2f,1)) )
        {
            
        }
       
    }
    ImGui::End();
}
