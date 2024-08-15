#include <filesystem>

#include "EditorCommand.hpp"
#include "IconsFontAwesome6.h"
#include "EditorGUI.h"
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"

const int ICON_SIZE = 96;

void Editor::GUI::ShowContentBrowser()
{
    ImGui::Begin("Content Browser", NULL, ImGuiWindowFlags_NoScrollbar);
    {
        static std::string selectedpath = "";
        static auto modelpath = Utilities::FileHelper::GetPlatformFilePath("assets");
        std::filesystem::path path = modelpath;
    
        // head path router
        // split path
        std::vector<std::string> paths;
        std::string pathstr = path.string();
        std::string delimiter = "\\";
        size_t pos = 0;
        std::string token;
        while ((pos = pathstr.find(delimiter)) != std::string::npos) {
            token = pathstr.substr(0, pos);
            paths.push_back(token);
            pathstr.erase(0, pos + delimiter.length());
        }
        paths.push_back(pathstr);

        // show path
        ImGui::PushFont(fontIcon_); // use the font awesome font
        for (int i = 0; i < paths.size(); i++)
        {
            if (i == 0)
            {
                if (ImGui::Button(ICON_FA_HOUSE))
                {
                    modelpath = Utilities::FileHelper::GetPlatformFilePath("assets");
                }
            }
            else
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(">");
                ImGui::SameLine();
                auto upperPath = paths[i];
                if (!upperPath.empty()) {
                    upperPath[0] = std::toupper(upperPath[0]);
                }
                if (ImGui::Button(upperPath.c_str()))
                {
                    std::string newpath = paths[0];
                    for (int j = 1; j <= i; j++)
                    {
                        newpath += "/" + paths[j];
                    }
                    modelpath = newpath;
                }
            }
        }
        ImGui::PopFont();

        auto CursorPos = ImGui::GetWindowPos() + ImVec2(0, ImGui::GetCursorPos().y + 2);
        ImGui::NewLine();
        ImGui::GetWindowDrawList()->AddLine(CursorPos + ImVec2(0,1), CursorPos + ImVec2(ImGui::GetWindowSize().x,1), IM_COL32(20,20,20,128), 1);
        ImGui::GetWindowDrawList()->AddLine(CursorPos, CursorPos + ImVec2(ImGui::GetWindowSize().x,0), IM_COL32(20,20,20,255), 1);

        // content view

        static float value = 0.5f;
        static bool open = false;
        static std::string contextMenuFile;

        auto elementLambda = [this](const std::string& path, const std::string& name, const char* icon, ImU32 color, std::function<void ()> menu_actions, std::function<void ()> doubleclick_action)
        {
            ImGui::BeginGroup();
            ImGui::PushFont(bigIcon_); // use the font awesome font
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
            ImGui::Button(icon, ImVec2(ICON_SIZE, ICON_SIZE));
            ImGui::PopStyleColor();
            ImGui::PopFont();

            if (ImGui::BeginPopupContextItem(path.c_str()))
            {
                ImGui::SeparatorText("Context Menu");
                menu_actions();
                ImGui::EndPopup();
            }
            ImGui::OpenPopupOnItemClick(path.c_str(), ImGuiPopupFlags_MouseButtonRight);
            if( ImGui::IsItemHovered(ImGuiHoveredFlags_None) )
            {
                if( ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    doubleclick_action();
                }
                if( ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    selectedpath = path;
                }
            }

            auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, 4);
            bool selected = selectedpath == path;
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

        
        std::filesystem::directory_iterator it(path);
        
        for (auto& entry : it)
        {
            std::string abspath = absolute(entry.path()).string();
            std::string relpath = relative(entry.path()).string();
            std::string name = entry.path().filename().string();
            std::string ext = entry.path().extension().string();

            if (entry.is_directory())
            {
                elementLambda(relpath, name, ICON_FA_FOLDER, IM_COL32(0, 172, 255, 255), [&relpath]()
                {
                    if (ImGui::MenuItem("Open"))
                    {
                        modelpath = relpath;
                    }
                },
                [&relpath]()
                {
                    modelpath = relpath;
                });
            }
            else
            {
                if (ext == ".glb")
                {
                    elementLambda(abspath, name, ICON_FA_CUBE, IM_COL32(255, 172, 0, 255), [&abspath]()
                    {
                        if (ImGui::MenuItem("Load"))
                        {
                            EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadScene, abspath);
                        }
                    },
                [&abspath]()
                {
                    EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadScene, abspath);
                });
                }
                else if (ext == ".hdr")
                {
                    elementLambda(abspath, name, ICON_FA_FILE_IMAGE, IM_COL32(200, 64, 64, 255), [&abspath]()
                    {
                        if (ImGui::MenuItem("Use as HDRI"))
                        {
                            EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadHDRI, abspath);
                        }
                    },
                [&abspath]()
                {
                    EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadHDRI, abspath);
                });
                }
            }
        }
    }
    ImGui::End();
}
