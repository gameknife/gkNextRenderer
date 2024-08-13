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
        static float value = 0.5f;
        static bool open = false;
        static std::string contextMenuFile;

        auto elementLambda = [this](const std::string& path, const std::string& name, const char* icon, ImU32 color, std::function<void ()> menu_actions)
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

            auto CursorPos = ImGui::GetCursorPos() + ImGui::GetWindowPos() - ImVec2(0, 4);
            ImGui::GetWindowDrawList()->AddRectFilled(CursorPos, CursorPos + ImVec2(ICON_SIZE, ICON_SIZE / 5 * 3),IM_COL32(64, 64, 64, 255), 4);
            ImGui::GetWindowDrawList()->AddLine(CursorPos, CursorPos + ImVec2(ICON_SIZE, 0), color, 2);

            ImGui::PushItemWidth(ICON_SIZE);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ICON_SIZE);
            ImGui::Text("%s", name.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopItemWidth();
            ImGui::EndGroup();
            ImGui::SameLine();
        };

        static auto modelpath = Utilities::FileHelper::GetPlatformFilePath("assets");

        std::filesystem::path path = modelpath;
        std::filesystem::directory_iterator it(path);


        for (auto& entry : it)
        {
            std::string abspath = absolute(entry.path()).string();
            std::string name = entry.path().filename().string();
            std::string ext = entry.path().extension().string();

            if (entry.is_directory())
            {
                elementLambda(abspath, name, ICON_FA_FOLDER, IM_COL32(0, 172, 255, 255), [&abspath]()
                {
                    if (ImGui::MenuItem("Open"))
                    {
                        modelpath = abspath;
                    }
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
                    });
                }
            }
        }
    }
    ImGui::End();
}
