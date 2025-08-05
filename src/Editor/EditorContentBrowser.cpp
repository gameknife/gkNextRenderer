#include <filesystem>

#include "EditorCommand.hpp"
#include "IconsFontAwesome6.h"
#include "EditorGUI.h"
#include "Utilities/FileHelper.hpp"
#include "EditorInterface.hpp"
#include "Assets/Scene.hpp"

const int ICON_SIZE = 96;
const int ICON_PADDING = 20;

void Editor::GUI::DrawGeneralContentBrowser(bool iconOrTex, uint32_t globalId, const std::string& name, const char* icon, ImU32 color, std::function<void ()> doubleclick_action)
{
    ImGui::BeginGroup();
    ImGui::PushFont(bigIcon_); // use the font awesome font
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(32, 32, 32, 255));
    ImGui::PushID(name.c_str());

    if ( iconOrTex || (VK_NULL_HANDLE == GUserInterface->RequestImTextureId(globalId)))
    {
        ImGui::Button(icon, ImVec2(ICON_SIZE, ICON_SIZE));
    }
    else
    {
        ImGui::Image((ImTextureID)(intptr_t)GUserInterface->RequestImTextureId(globalId), ImVec2(ICON_SIZE, ICON_SIZE));
    }
    
    ImGui::PopID();
    ImGui::PopStyleColor();
    ImGui::PopFont();
                
    if( ImGui::IsItemHovered(ImGuiHoveredFlags_None) )
    {
        if( ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            selectedItemId = -1;
            doubleclick_action();
        }
        if( ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            selectedItemId = globalId;
        }
    }

    
    auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, 4 + ImGui::GetScrollY());
    bool selected = selectedItemId == globalId;
    ImGui::GetWindowDrawList()->AddRectFilled(CursorPos, CursorPos + ImVec2(ICON_SIZE, ICON_SIZE / 5 * 3),selected ? IM_COL32(64, 128, 255, 255) : IM_COL32(64, 64, 64, 255), 4);
    ImGui::GetWindowDrawList()->AddLine(CursorPos, CursorPos + ImVec2(ICON_SIZE, 0), color, 2);

    ImGui::PushItemWidth(ICON_SIZE);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ICON_SIZE);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 5);
    ImGui::Text("%s", name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopItemWidth();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ICON_PADDING);
    ImGui::EndGroup();
}

void Editor::GUI::ShowMaterialBrowser()
{
    ImGui::Begin("Material Browser", NULL);
    {
        if (current_scene)
        {
            auto& AllMaterials = current_scene->Materials();
            
            // 计算每行能容纳的元素数量
            float windowWidth = ImGui::GetContentRegionAvail().x;
            int itemsPerRow = std::max(1, (int)(windowWidth / (ICON_SIZE + ImGui::GetStyle().ItemSpacing.x)));

            for ( uint32_t i = 0; i < AllMaterials.size(); ++i)
            {
                auto& mat = AllMaterials[i];
                DrawGeneralContentBrowser(true, mat.globalId_, mat.name_, ICON_FA_BOWLING_BALL, IM_COL32(132, 255, 132, 255), [this, i]()
                {
                    selected_material = &(current_scene->Materials()[i]);
                    ed_material = true;
                    OpenMaterialEditor();
                });

                // 根据位置决定是否换行
                if ((i + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
            }
        }


    }
    ImGui::End();
}

void Editor::GUI::ShowTextureBrowser()
{
    ImGui::Begin("Texture Browser", NULL );
    {
        if (current_scene)
        {
            auto& totalTextureMap = Assets::GlobalTexturePool::GetInstance()->TotalTextureMap();
            
            // 计算每行能容纳的元素数量
            float windowWidth = ImGui::GetContentRegionAvail().x;
            int itemsPerRow = std::max(1, (int)(windowWidth / (ICON_SIZE + ImGui::GetStyle().ItemSpacing.x)));
            
            int itemIndex = 0;
            for ( auto& textureGroup : totalTextureMap )
            {
                DrawGeneralContentBrowser(false, textureGroup.second.GlobalIdx_, textureGroup.first, ICON_FA_LINK_SLASH, IM_COL32(255, 72, 72, 255), [this]()
                {
                    
                });
            
                // 根据位置决定是否换行
                if ((itemIndex + 1) % itemsPerRow != 0)
                {
                    ImGui::SameLine();
                }
                itemIndex++;
            }
        }
    }
    ImGui::End();
}

void Editor::GUI::ShowContentBrowser()
{
    ImGui::Begin("Content Browser", NULL);
    {
        static auto modelpath = Utilities::FileHelper::GetPlatformFilePath("assets");
        std::filesystem::path path = modelpath;
    
        // head path router
        // split path
        std::vector<std::string> paths;
        std::string pathstr = path.string();
        std::string delimiter = std::string(1, std::filesystem::path::preferred_separator);
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
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8,4));
        if (ImGui::Button(ICON_FA_HOUSE)) modelpath = Utilities::FileHelper::GetPlatformFilePath("assets");
        for (int i = 1; i < paths.size(); i++)
        {
            ImGui::SameLine();
            ImGui::TextUnformatted(">");
            ImGui::SameLine();
            auto upperPath = paths[i];
            if (!upperPath.empty())  upperPath[0] = std::toupper(upperPath[0]);
            if (ImGui::Button(upperPath.c_str()))
            {
                auto newpath = std::filesystem::path(paths[0]);
                for (int j = 1; j <= i; j++)
                {
                    newpath = newpath.append(paths[j]);
                }
                modelpath = newpath.string();
            }
        }
        ImGui::PopStyleVar();
        ImGui::PopFont();
        
        auto CursorPos = ImGui::GetWindowPos() + ImVec2(0, ImGui::GetCursorPos().y + 2);
        ImGui::NewLine();
        ImGui::GetWindowDrawList()->AddLine(CursorPos + ImVec2(0,1), CursorPos + ImVec2(ImGui::GetWindowSize().x,1), IM_COL32(20,20,20,128), 1);
        ImGui::GetWindowDrawList()->AddLine(CursorPos, CursorPos + ImVec2(ImGui::GetWindowSize().x,0), IM_COL32(20,20,20,255), 1);

        ImGui::BeginChild("Content Items");
        // content view
        static std::string contextMenuFile;
        // if (ImGui::BeginPopupContextItem(path.c_str()))
        // {
        //     ImGui::SeparatorText("Context Menu");
        //     menu_actions();
        //     ImGui::EndPopup();
        // }
        // ImGui::OpenPopupOnItemClick(path.c_str(), ImGuiPopupFlags_MouseButtonRight);
        
        std::filesystem::directory_iterator it(path);
        
        float windowWidth = ImGui::GetContentRegionAvail().x;
        int itemsPerRow = std::max(1, (int)(windowWidth / (ICON_SIZE + ImGui::GetStyle().ItemSpacing.x)));
        
        uint32_t elementIdx = 0;
        for (auto& entry : it)
        {
            std::string abspath = absolute(entry.path()).string();
            std::string relpath = relative(entry.path()).string();
            std::string name = entry.path().filename().string();
            std::string ext = entry.path().extension().string();

            const char* icon = ICON_FA_FOLDER;
            ImU32 color = IM_COL32(0, 172, 255, 255);
            if (entry.is_regular_file())
            {
                if (ext == ".glb")
                {
                    icon = ICON_FA_CUBE;
                    color = IM_COL32(255, 172, 0, 255);
                }
                else if (ext == ".hdr")
                {
                    icon = ICON_FA_FILE_IMAGE;
                    color = IM_COL32(200, 64, 64, 255);
                }
                else
                {
                    continue;
                }
            }

            DrawGeneralContentBrowser(true, elementIdx, name, icon, color, [&]()
            {
                if (entry.is_directory())
                {
                    modelpath = relpath;
                }
                else
                {
                    if (ext == ".glb")
                    {
                        EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadScene, abspath);
                    }
                    else if (ext == ".hdr")
                    {
                        EditorCommand::ExecuteCommand(EEditorCommand::ECmdIO_LoadHDRI, abspath);
                    }
                }
            });

            if ((elementIdx + 1) % itemsPerRow != 0)
            {
                ImGui::SameLine();
            }

            elementIdx++;
        }

        ImGui::EndChild();
    }
    ImGui::End();
}
