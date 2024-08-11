#include <filesystem>

#include "EditorCommand.hpp"
#include "IconsFontAwesome6.h"
#include "EditorGUI.h"
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"

const int ICON_SIZE = 72;

void Editor::GUI::ShowContentBrowser()
{
    ImGui::Begin("Content Browser", NULL,  ImGuiWindowFlags_NoScrollbar);
    {
        static float value = 0.5f;
        static bool open = false;
        static std::string contextMenuFile;

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
                auto abspath = absolute(entry.path()).string();
                                
                ImGui::BeginGroup();
                
                ImGui::PushFont(fontIcon_); // use the font awesome font
                ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
                ImGui::Button( ICON_FA_FILE, ImVec2(ICON_SIZE, ICON_SIZE) );
                ImGui::PopStyleColor();
                ImGui::PopFont();
                
                if (ImGui::BeginPopupContextItem(abspath.c_str()))
                {
                    ImGui::SeparatorText("Context Menu");
                    if( ImGui::MenuItem("Load") )
                    {
                        EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadScene, abspath);
                    }
                    ImGui::EndPopup();
                }
                ImGui::OpenPopupOnItemClick(abspath.c_str(), ImGuiPopupFlags_MouseButtonRight);
 
                auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0,4);
                ImGui::GetWindowDrawList()->AddRectFilled( CursorPos, CursorPos + ImVec2(ICON_SIZE,ICON_SIZE / 5 * 3),IM_COL32(64, 64, 64, 255), 4);
                ImGui::GetWindowDrawList()->AddLine( CursorPos, CursorPos + ImVec2(ICON_SIZE,0),IM_COL32(255, 172, 0, 255), 2);
                
              
                
                ImGui::PushItemWidth(ICON_SIZE);
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ICON_SIZE);
                ImGui::Text("%s", entry.path().filename().string().c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopItemWidth();
                ImGui::EndGroup();ImGui::SameLine();
            }
        }
    }
    ImGui::End();
}
