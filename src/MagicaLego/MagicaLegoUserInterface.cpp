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

const int PANELFLAGS =
    ImGuiWindowFlags_NoTitleBar |
    ImGuiWindowFlags_NoCollapse |
    ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoSavedSettings;

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

void MagicaLegoUserInterface::OnInitUI()
{
    if(bigFont_ == nullptr)
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddText("MagicaLego");
        const ImWchar* glyphRange = ImGui::GetIO().Fonts->GetGlyphRangesDefault();
        bigFont_ = ImGui::GetIO().Fonts->AddFontFromFileTTF(Utilities::FileHelper::GetPlatformFilePath("assets/fonts/Roboto-BoldCondensed.ttf").c_str(), 72, nullptr, glyphRange );
    }
}

void MagicaLegoUserInterface::DrawOpening()
{
    // 获取窗口的大小
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;

    // 设置背景颜色
    ImVec4 bgColor = ImVec4(0.1f, 0.1f, 0.1f, openingTimer_);
    ImVec4 fgColor = ImVec4(1.0f, 1.0f, 1.0f, openingTimer_);
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), windowSize, ImGui::ColorConvertFloat4ToU32(bgColor));

    ImGui::PushFont(bigFont_);
    
    // 设置文字格式
    auto TextSize = ImGui::CalcTextSize("for my son's MAGICALEGO");
    auto TextPos = windowSize * 0.5f - TextSize * 0.5f;
    ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), TextPos, ImGui::ColorConvertFloat4ToU32(fgColor), "for my son's MAGICALEGO");

    ImGui::PopFont();
}

void MagicaLegoUserInterface::OnRenderUI()
{
    if( GetGameInstance()->ShowBanner() )
    {
        openingTimer_ = 2.0f;
    }
    
    // TotalSwitch
    DrawMainToolBar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
    if( showLeftBar_ ) DrawLeftBar();
    if( showRightBar_ ) DrawRightBar();

    DrawStatusBar();
    ImGui::PopStyleVar(2);

    if(showTimeline_)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18);
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 18);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0);
        DrawTimeline();
        ImGui::PopStyleVar(3); 
    }

    DrawIndicator();
    

    // ugly opening guiding, optimze later
    if(openingTimer_ > -5)
    {
        openingTimer_ = openingTimer_ - GetGameInstance()->GetEngine().GetDeltaSeconds();
    }
    if(openingTimer_ >= 0)
    {
        DrawOpening();
    }
    if(openingTimer_ < 0 && openingTimer_ > -1)
    {
        auto screenSize = ImGui::GetMainViewport()->Size;
        auto lerpedPos = glm::mix(glm::vec2(screenSize.x * 0.75, screenSize.y * 0.75), glm::vec2(screenSize.x * 0.5, screenSize.y * 0.5), -openingTimer_);
        glfwSetCursorPos( GetGameInstance()->GetEngine().GetRenderer().Window().Handle(), lerpedPos.x, lerpedPos.y);
    }
    if(openingTimer_ < -1 && openingTimer_ > -3)
    {
        GetGameInstance()->PlaceDynamicBlock({glm::i16vec3(0,0,0), 12});
        openingTimer_ = -10;
    }
}

void MagicaLegoUserInterface::DrawIndicator()
{
    // draw aux on ray hit plane
}

void MagicaLegoUserInterface::DrawStatusBar()
{
    // Draw a bottom bar
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    const ImVec2 pos = ImVec2(BUILD_BAR_WIDTH , viewportSize.y);
    const ImVec2 posPivot = ImVec2(0.0f, 1.0f);
    const float width = viewportSize.x - BUILD_BAR_WIDTH - SIDE_BAR_WIDTH;
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(width, 20));
    ImGui::SetNextWindowBgAlpha(0.9f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0,0));
    ImGui::Begin("StatusBar", 0, PANELFLAGS);
    ImGui::Text("MagicaLego %s", NextRenderer::GetBuildVersion().c_str());
    ImGui::SameLine();
    ImGui::Text("| %d %d %d", 0, 0, 0);
    ImGui::SameLine();
    ImGui::Text("| %zu | %d | %d", GetGameInstance()->GetBasicNodeLibrary().size(), GetGameInstance()->GetCurrentStep(), 0);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void MagicaLegoUserInterface::DrawMainToolBar()
{
    const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    
    const ImVec2 pos = ImVec2(viewportSize.x * 0.5f, 0);
    const ImVec2 posPivot = ImVec2(0.5f, 0.0f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, posPivot);
    ImGui::SetNextWindowSize(ImVec2(0,0));
    ImGui::SetNextWindowBgAlpha(0.2f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    if (ImGui::Begin("MainToolBar", 0, PANELFLAGS | ImGuiWindowFlags_NoBackground))
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
    
    if (ImGui::Begin("Place & Dig", 0, PANELFLAGS))
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
        ImGui::SeparatorText("Base");
        if( SelectButton(ICON_FA_L, "1", GetGameInstance()->GetCurrentBasePlane() == EBP_Big))
        {
            GetGameInstance()->SwitchBasePlane(EBP_Big);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_M, "2", GetGameInstance()->GetCurrentBasePlane() == EBP_Mid))
        {
            GetGameInstance()->SwitchBasePlane(EBP_Mid);
        }
        ImGui::SameLine();
        if( SelectButton(ICON_FA_S, "3", GetGameInstance()->GetCurrentBasePlane() == EBP_Small))
        {
            GetGameInstance()->SwitchBasePlane(EBP_Small);
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
                    continue;
                }
                std::string filename = entry.path().filename().string();
                filename = filename.substr(0, filename.size() - 4);

                bool is_selected = (selected_filename == filename);
                ImGuiSelectableFlags flags = (selected_filename == filename) ? ImGuiSelectableFlags_Highlight : 0;
                if (ImGui::Selectable(filename.c_str(), is_selected, flags))
                {
                    selected_filename = filename;
                    new_filename = filename;
                }
                if (is_selected) ImGui::SetItemDefaultFocus();
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
    
    if (ImGui::Begin("Color Pallete", 0, PANELFLAGS))
    {
        std::vector<const char*> types;
        static int current_type = 0;
        
        auto& BasicNodeLibrary = GetGameInstance()->GetBasicNodeLibrary();
        if(BasicNodeLibrary.size() > 0)
        {
            for ( auto& Type : BasicNodeLibrary )
            {
                types.push_back(Type.first.c_str());
            }
        
            ImGui::SeparatorText("Blocks");
            ImGui::Dummy(ImVec2(0,5));

            ImGui::SetNextItemWidth( SIDE_BAR_WIDTH - 46 );
            ImGui::Combo("##Type", &current_type, types.data(), static_cast<int>(types.size()));
            ImGui::Dummy(ImVec2(0,5));
        
            float WindowWidth = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            auto& BasicBlocks = BasicNodeLibrary[types[current_type]];
        
            for( size_t i = 0; i < BasicBlocks.size(); i++ )
            {
                auto& block = BasicBlocks[i];
                if( MaterialButton(block, WindowWidth, GetGameInstance()->GetCurrentBrushIdx() == block.brushId_) )
                {
                    GetGameInstance()->SetCurrentBrushIdx(block.brushId_);
                    GetGameInstance()->TryChangeSelectionBrushIdx(block.brushId_);
                }
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
    
    if (ImGui::Begin("Timeline", 0, PANELFLAGS))
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

