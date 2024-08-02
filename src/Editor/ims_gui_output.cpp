#include <filesystem>

#include "IconsFontAwesome6.h"
#include "ims_gui.h"
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"

const int ICON_SIZE = 72;

void ImStudio::GUI::ShowOutputWorkspace()
{
    ImGui::Begin("Content Browser", NULL,  ImGuiWindowFlags_NoScrollbar);
    {
        static float value = 0.5f;
        static bool open = false;
        
        if (ImGui::BeginPopupContextItem("my popup"))
        {
            ImGui::SeparatorText("Context Menu");
            if( ImGui::MenuItem("Load") )
            {
                
            }
            ImGui::EndPopup();
        }
        
        
        auto modelpath = Utilities::FileHelper::GetPlatformFilePath("assets/models");
        std::filesystem::path path = modelpath;
        std::filesystem::directory_iterator it(path);
        for (auto& entry : it)
        {
            if (entry.is_directory())
            {
                ImGui::Text("%s", entry.path().filename().string().c_str());
            }
            else
            {
                auto element = entry.path().filename().string().c_str();
                ImGui::BeginGroup();
                
                ImGui::PushFont(fontIcon_); // use the font awesome font
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
                ImGui::Button( ICON_FA_FILE, ImVec2(ICON_SIZE, ICON_SIZE) );
                ImGui::PopStyleColor();
                auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0,4);
                ImGui::GetWindowDrawList()->AddRectFilled( CursorPos, CursorPos + ImVec2(ICON_SIZE,ICON_SIZE / 5 * 3),IM_COL32(64, 64, 64, 255), 4);
                ImGui::GetWindowDrawList()->AddLine( CursorPos, CursorPos + ImVec2(ICON_SIZE,0),IM_COL32(255, 172, 0, 255), 2);
                ImGui::OpenPopupOnItemClick("my popup", ImGuiPopupFlags_MouseButtonRight);
                ImGui::PopFont();
                
                ImGui::PushItemWidth(ICON_SIZE);
                ImVec2 textSize = ImGui::CalcTextSize(entry.path().filename().string().c_str());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ICON_SIZE);
                ImGui::Text("%s", entry.path().filename().string().c_str());
                ImGui::OpenPopupOnItemClick("my popup", ImGuiPopupFlags_MouseButtonRight);
                ImGui::PopTextWrapPos();
                ImGui::PopItemWidth();
                ImGui::EndGroup();ImGui::SameLine();
            }
        }
    }
    ImGui::End();
}
