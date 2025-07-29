#include <filesystem>

#include "EditorCommand.hpp"
#include "IconsFontAwesome6.h"
#include "EditorGUI.h"
#include "EditorInterface.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"

const int ICON_SIZE = 96;

void Editor::GUI::ShowMaterialBrowser()
{
    ImGui::Begin("Material Browser", NULL, ImGuiWindowFlags_NoScrollbar);
    {
        if (current_scene)
        {
            auto& AllMaterials = current_scene->Materials();
            ImGui::Text("All Materials %d", AllMaterials.size());

            
            auto elementLambda = [this](uint32_t materialId, const std::string& name, const char* icon, ImU32 color, std::function<void ()> doubleclick_action)
            {
                ImGui::BeginGroup();
                ImGui::PushFont(bigIcon_); // use the font awesome font
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
                ImGui::PushID(name.c_str());
                ImGui::Button(icon, ImVec2(ICON_SIZE, ICON_SIZE));
                ImGui::PopID();
                ImGui::PopStyleColor();
                ImGui::PopFont();
                
                if( ImGui::IsItemHovered(ImGuiHoveredFlags_None) )
                {
                    if( ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        doubleclick_action();
                    }
                    if( ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        selectedMaterialId = materialId;
                    }
                }

                auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, 4);
                bool selected = selectedMaterialId == materialId;
                ImGui::GetWindowDrawList()->AddRectFilled(CursorPos, CursorPos + ImVec2(ICON_SIZE, ICON_SIZE / 5 * 3),selected ? IM_COL32(64, 128, 255, 255) : IM_COL32(64, 64, 64, 255), 4);
                ImGui::GetWindowDrawList()->AddLine(CursorPos, CursorPos + ImVec2(ICON_SIZE, 0), color, 2);

                ImGui::PushItemWidth(ICON_SIZE);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ICON_SIZE);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                ImGui::Text("%s", name.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopItemWidth();
                ImGui::EndGroup();
                ImGui::SameLine();
            };

            for ( uint32_t i = 0; i < AllMaterials.size(); ++i)
            {
                auto& mat = AllMaterials[i];
                elementLambda(mat.globalId_, mat.name_, ICON_FA_BOWLING_BALL, IM_COL32(132, 255, 132, 255), [this, i]()
                {
                    selected_material = &(current_scene->Materials()[i]);
                    ed_material = true;
                    OpenMaterialEditor();
                });
            }
        }


    }
    ImGui::End();
}


void Editor::GUI::ShowTextureBrowser()
{
    ImGui::Begin("Texture Browser", NULL, ImGuiWindowFlags_NoScrollbar );
    {
        if (current_scene)
        {
            auto& totalTextureMap = Assets::GlobalTexturePool::GetInstance()->TotalTextureMap();
            ImGui::Text("All Textures %d", totalTextureMap.size());

            
            auto elementLambda = [this](uint32_t texId, const std::string& texName, Assets::TextureImage* texture, const char* icon, ImU32 color, std::function<void ()> doubleclick_action)
            {
                ImGui::BeginGroup();
                ImGui::PushFont(bigIcon_); // use the font awesome font
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
                //ImGui::Button(icon, ImVec2(ICON_SIZE, ICON_SIZE));
                //ImGui::GetWindowDrawList()->AddImage(GUserInterface->RequestImTextureId(texId), CursorPos, CursorPos + ImVec2(ICON_SIZE, ICON_SIZE / 5 * 3));
                ImGui::Image((ImTextureID)(intptr_t)GUserInterface->RequestImTextureId(texId), ImVec2(ICON_SIZE, ICON_SIZE) );
                ImGui::PopStyleColor();
                ImGui::PopFont();
                
                if( ImGui::IsItemHovered(ImGuiHoveredFlags_None) )
                {
                    if( ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        doubleclick_action();
                    }
                    if( ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        selectedTextureId = texId;
                    }
                }

                auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, 4);
                bool selected = selectedTextureId == texId;
                
                ImGui::GetWindowDrawList()->AddRectFilled(CursorPos, CursorPos + ImVec2(ICON_SIZE, ICON_SIZE / 5 * 3),selected ? IM_COL32(64, 128, 255, 255) : IM_COL32(64, 64, 64, 255), 4);
                ImGui::GetWindowDrawList()->AddLine(CursorPos, CursorPos + ImVec2(ICON_SIZE, 0), color, 2);

                ImGui::PushItemWidth(ICON_SIZE);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ICON_SIZE);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
                ImGui::Text("%s", texName.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopItemWidth();
                ImGui::EndGroup();
                ImGui::SameLine();
            };

            for ( auto& textureGroup : totalTextureMap )
            {
                Assets::TextureImage* texture = Assets::GlobalTexturePool::GetTextureImage(textureGroup.second.GlobalIdx_);
                elementLambda(textureGroup.second.GlobalIdx_, textureGroup.first, texture, ICON_FA_BOWLING_BALL, IM_COL32(255, 72, 72, 255), [this]()
                {
                    
                });
            }
        }
    }
    ImGui::End();
}