#include "MagicaLegoUserInterface.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include "Editor/IconsFontAwesome6.h"
#include "Utilities/FileHelper.hpp"
#include "MagicaLegoGameInstance.hpp"
#include "Runtime/Platform/PlatformCommon.h"
#include "Utilities/ImGui.hpp"

const int ICON_SIZE = 64;
const int PALATE_SIZE = 46;
const int BUTTON_SIZE = 36;
const int BUILD_BAR_WIDTH = 240;
const int SIDE_BAR_WIDTH = 300;

bool SelectButton(const char* label, const char* shortcut, bool selected)
{
    if(selected)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f,0.6f,0.8f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f,0.6f,0.8f,1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f,0.6f,0.8f,1));
    }
    ImGui::BeginGroup();
    bool result = ImGui::Button(label, ImVec2(ICON_SIZE, ICON_SIZE));
    Utilities::UI::TextCentered(shortcut, ICON_SIZE);
    ImGui::EndGroup();
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

    ImGui::BeginGroup();
    ImGui::PushID(block.modelId_);
    bool result = ImGui::Button("##Block", ImVec2(PALATE_SIZE, PALATE_SIZE));
    ImGui::PopID();
    Utilities::UI::TextCentered(block.name, PALATE_SIZE);
    ImGui::EndGroup();
    
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

MagicaLegoUserInterface::MagicaLegoUserInterface(MagicaLegoGameInstance* gameInstance):gameInstance_(gameInstance)
{
    showLeftBar_ = true;
    showRightBar_ = true;
    showTimeline_ = false;
}

void MagicaLegoUserInterface::OnRenderUI()
{
    // TotalSwitch
    DrawMainToolBar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    if( showLeftBar_ ) DrawLeftBar();
    if( showRightBar_ ) DrawRightBar();
    ImGui::PopStyleVar(2);

    if(showTimeline_)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18);
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 18);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
        DrawTimeline();
        ImGui::PopStyleVar(3); 
    }
}

void MagicaLegoUserInterface::DrawMainToolBar()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    
    const ImVec2 pos = ImVec2(viewportSize.x * 0.5f, 0);
    const ImVec2 posPivot = ImVec2(0.5f, 0.0f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(0,0));
    ImGui::SetNextWindowBgAlpha(0.2f);
    const int flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    if (ImGui::Begin("MainToolBar", 0, flags))
    {
        if( ImGui::Button(ICON_FA_SQUARE_CARET_LEFT, ImVec2(BUTTON_SIZE, BUTTON_SIZE)) )
        {
            showLeftBar_ = !showLeftBar_;
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_SQUARE_CARET_DOWN, ImVec2(BUTTON_SIZE, BUTTON_SIZE)) )
        {
            showTimeline_ = !showTimeline_;
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_SQUARE_CARET_RIGHT, ImVec2(BUTTON_SIZE, BUTTON_SIZE)) )
        {
            showRightBar_ = !showRightBar_;
        }
        ImGui::SameLine();
    }
    ImGui::PopStyleColor();

    auto location = GetGameInstance()->GetCurrentSeletionBlock();
    ImGui::Text( "%d, %d, %d", location.x, location.y, location.z );
    ImGui::End();
}

void MagicaLegoUserInterface::DrawLeftBar()
{
    const ImVec2 pos = ImVec2(0, 0);
    const ImVec2 posPivot = ImVec2(0.0f, 0.0f);
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(BUILD_BAR_WIDTH,viewportSize.y));
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
        if( SelectButton(ICON_FA_PERSON_DIGGING, "Q", GetGameInstance()->GetBuildMode() == ELM_Dig) )
        {
            GetGameInstance()->SetBuildMode(ELM_Dig);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CUBES_STACKED, "W", GetGameInstance()->GetBuildMode() == ELM_Place) )
        {
            GetGameInstance()->SetBuildMode(ELM_Place);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_HAND_POINTER, "E", GetGameInstance()->GetBuildMode() == ELM_Select) )
        {
            GetGameInstance()->SetBuildMode(ELM_Select);
        }
        ImGui::SeparatorText("Camera");
        if( SelectButton(ICON_FA_UP_DOWN_LEFT_RIGHT, "A", GetGameInstance()->GetCameraMode() == ECM_Pan))
        {
            GetGameInstance()->SetCameraMode(ECM_Pan);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CAMERA_ROTATE, "S", GetGameInstance()->GetCameraMode() == ECM_Orbit))
        {
            GetGameInstance()->SetCameraMode(ECM_Orbit);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_CIRCLE_DOT, "D", GetGameInstance()->GetCameraMode() == ECM_AutoFocus))
        {
            GetGameInstance()->SetCameraMode(ECM_AutoFocus);
        }

        ImGui::Dummy(ImVec2(0,50));
        ImGui::SeparatorText("Light");

        ImGui::Checkbox("Sun", &GetGameInstance()->GetEngine().GetUserSettings().HasSun);
        if(GetGameInstance()->GetEngine().GetUserSettings().HasSun)
        {
            ImGui::SliderFloat("Dir", &GetGameInstance()->GetEngine().GetUserSettings().SunRotation, 0, 2);
        }
        ImGui::SliderInt("Sky", &GetGameInstance()->GetEngine().GetUserSettings().SkyIdx, 0, 10);
        ImGui::SliderFloat("Lum", &GetGameInstance()->GetEngine().GetUserSettings().SkyIntensity, 0, 100);

        ImGui::Dummy(ImVec2(0,50));
        
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
            GetGameInstance()->CleanUp();
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_FOLDER_OPEN, ImVec2(BUTTON_SIZE, BUTTON_SIZE) ))
        {
            GetGameInstance()->LoadRecord(selected_filename);
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_FLOPPY_DISK, ImVec2(BUTTON_SIZE, BUTTON_SIZE)) )
        {
            if(selected_filename != "") GetGameInstance()->SaveRecord(selected_filename);
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
                GetGameInstance()->SaveRecord(new_filename);
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

void MagicaLegoUserInterface::DrawRightBar()
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

        static int current_type = 0;
        std::vector<const char*> types {"Block1x1", "Plate1x1"};
        ImGui::Combo("Type", &current_type, types.data(), static_cast<int>(types.size()));

        float WindowWidth = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        auto& blocks = GetGameInstance()->GetBasicNodeByType(types[current_type]);
        for( size_t i = 0; i < blocks.size(); i++ )
        {
            auto& block = blocks[i];
            if( MaterialButton(block, WindowWidth, GetGameInstance()->GetCurrentBrushIdx() == block.brushId_) )
            {
                GetGameInstance()->SetCurrentBrushIdx(static_cast<int>(block.brushId_));
                GetGameInstance()->TryChangeSelectionBrushIdx(static_cast<int>(block.brushId_));
            }
        }
    }
    ImGui::End();
}

void MagicaLegoUserInterface::DrawTimeline()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 pos = ImVec2(viewportSize.x * 0.5f, viewportSize.y - 20);
    const ImVec2 posPivot = ImVec2(0.5f, 1.0f);
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
        int step = GetGameInstance()->GetCurrentStep();
        if( ImGui::SliderInt("##Timeline", &step, 0, GetGameInstance()->GetMaxStep()) )
        {
            GetGameInstance()->SetPlayStep(step);
        }
        ImGui::PopItemWidth();
    
        ImVec2 NextLinePos = ImGui::GetCursorPos();
        ImGui::SetCursorPos( NextLinePos + ImVec2(width * 0.5f - (style.ItemSpacing.x * 4 + BUTTON_SIZE * 3) * 0.5f,0) );
        ImGui::BeginGroup();
        if( ImGui::Button(ICON_FA_BACKWARD_STEP, ImVec2(BUTTON_SIZE,BUTTON_SIZE)) )
        {
            GetGameInstance()->SetPlayStep(step - 1);
        }
        ImGui::SameLine();
        if( ImGui::Button(GetGameInstance()->IsPlayReview() ? ICON_FA_PAUSE : ICON_FA_PLAY, ImVec2(BUTTON_SIZE,BUTTON_SIZE)))
        {
            GetGameInstance()->SetPlayReview(!GetGameInstance()->IsPlayReview());
        }
        ImGui::SameLine();
        if( ImGui::Button(ICON_FA_FORWARD_STEP, ImVec2(BUTTON_SIZE,BUTTON_SIZE)))
        {
            GetGameInstance()->SetPlayStep(step + 1);
        }
        ImGui::EndGroup();
    
        ImGui::SetCursorPos( NextLinePos + ImVec2(width - (style.ItemSpacing.x * 4 + BUTTON_SIZE * 3) * 0.5f,0) );
        ImGui::BeginGroup();
        if( ImGui::Button(ICON_FA_CLOCK_ROTATE_LEFT, ImVec2(BUTTON_SIZE,BUTTON_SIZE)) )
        {
            GetGameInstance()->DumpReplayStep(step);
        }
        ImGui::EndGroup();
    }
    ImGui::End();
}

