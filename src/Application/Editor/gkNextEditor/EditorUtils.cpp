#include "EditorUtils.h"

#include <fmt/core.h>
#include <fmt/printf.h>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/ImGui.hpp"

ImVec2 utils::GetLocalCursor()
{
    ImGuiIO &     io         = ImGui::GetIO();
    ImGuiContext &g          = *ImGui::GetCurrentContext();
    ImGuiWindow * w          = g.CurrentWindow;
    ImVec2        cursor     = ImVec2(io.MousePos.x - w->Pos.x, io.MousePos.y - w->Pos.y);
    ImRect        itemrect   = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    float         itemwidth  = itemrect.Max.x - itemrect.Min.x;
    float         itemheight = itemrect.Max.y - itemrect.Min.y;
    cursor.x                -= itemwidth / 2;
    cursor.y                -= itemheight / 2;
    return cursor;
}

void utils::TextCentered(std::string text, int type = 0)
{
    auto windowWidth = ImGui::GetWindowSize().x;
    auto textWidth   = ImGui::CalcTextSize(text.c_str()).x;

    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    switch (type)
    {
    case 0:
        ImGui::TextUnformatted(text.c_str());
        break;
    
    case 1:
        ImGui::TextDisabled("%s", text.c_str());
        break;
    }
}

float utils::RandomFloat(float a, float b)
{
    float random = ((float) rand()) / (float) RAND_MAX;
    float diff = b - a;
    float r = random * diff;
    return a + r;
}

ImVec4 utils::RainbowCol()
{
    static ImVec4 col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    static int c = 0;
    if (c>=10)
    {
        col.x = RandomFloat(0.0f, 0.7f);
        col.y = RandomFloat(0.0f, 0.7f);
        col.z = RandomFloat(0.0f, 0.7f);
        c = 0;
    }
    c++;
    return col;
}

void utils::DrawGrid()
{
    if (!NextEngine::GetInstance()->GetShowFlags().ShowGrid)
    {
        return;
    }
    for (float i = ImGui::GetWindowPos().y; i < ImGui::GetWindowSize().x * 4.0f;)
    {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(0.0f, i),ImVec2(ImGui::GetMainViewport()->Size.x, i),IM_COL32(88, 88, 88, 50));
        i += 25.0f;
    }
    for (float i = ImGui::GetWindowPos().x; i < ImGui::GetWindowSize().y * 4.0f;)
    {
        ImGui::GetWindowDrawList()->AddLine(ImVec2(i, 0.0f),ImVec2(i, ImGui::GetMainViewport()->Size.y),IM_COL32(88, 88, 88, 50));
        i += 25.0f;
    }
}

void utils::ShowStyleEditorWindow(bool *childSty)
{
    ImGui::SetNextWindowSize(ImVec2(500,700), ImGuiCond_Once);
    if (ImGui::Begin("Style Editor", childSty, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
    
}

void utils::ShowColorExportWindow(bool *childColexp)
{
    if (ImGui::Begin("Color Export", childColexp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
    {
        static ImVec4 color = ImVec4(114.0f / 255.0f, 144.0f / 255.0f, 154.0f / 255.0f, 200.0f / 255.0f);

        static bool         alphaPreview      = true;
        static bool         alphaHalfPreview = true;
        static bool         dragAndDrop      = true;
        static bool         optionsMenu       = true;
        static bool         hdr                = false;
        ImGuiColorEditFlags miscFlags         = (hdr ? ImGuiColorEditFlags_HDR : 0) |
                                         (dragAndDrop ? 0 : ImGuiColorEditFlags_NoDragDrop) |
                                         (alphaHalfPreview ? ImGuiColorEditFlags_AlphaPreviewHalf
                                                             : (alphaPreview ? ImGuiColorEditFlags_AlphaPreview : 0)) |
                                         (optionsMenu ? 0 : ImGuiColorEditFlags_NoOptions);

        ImGui::Text("Color widget:");
        ImGui::SameLine();
        HelpMarker("Click on the color square to open a color picker.\n"
                   "CTRL+click on individual component to input value.\n");
        ImGui::ColorEdit3("MyColor##1", (float *)&color, miscFlags);
        ImGui::SameLine();
        HelpMarker("Right click me to export colors with your preferred format.");

        ImGui::Text("Color widget HSV with Alpha:");
        ImGui::ColorEdit4("MyColor##2", (float *)&color, ImGuiColorEditFlags_DisplayHSV | miscFlags);

        ImGui::Text("Color widget with Float Display:");
        ImGui::ColorEdit4("MyColor##2f", (float *)&color, ImGuiColorEditFlags_Float | miscFlags);

        ImGui::Text("Color picker:");
        static bool   alpha        = true;
        static bool   alphaBar    = true;
        static bool   sidePreview = true;
        static bool   refColor    = false;
        static ImVec4 refColorV(1.0f, 0.0f, 1.0f, 0.5f);
        static int    displayMode = 0;
        static int    pickerMode  = 2;
        ImGui::Combo("Display Mode", &displayMode, "Auto/Current\0None\0RGB Only\0HSV Only\0Hex Only\0");
        ImGui::SameLine();
        HelpMarker("ColorEdit defaults to displaying RGB inputs if you don't specify a display mode, "
                   "but the user can change it with a right-click.\n\nColorPicker defaults to displaying RGB+HSV+Hex "
                   "if you don't specify a display mode.\n\nYou can change the defaults using SetColorEditOptions().");
        ImGui::Combo("Picker Mode", &pickerMode, "Auto/Current\0Hue bar + SV rect\0Hue wheel + SV triangle\0");
        ImGui::SameLine();
        HelpMarker("User can right-click the picker to change mode.");
        ImGuiColorEditFlags flags = miscFlags;
        if (!alpha)
            flags |=
                ImGuiColorEditFlags_NoAlpha; // This is by default if you call ColorPicker3() instead of ColorPicker4()
        if (alphaBar)
            flags |= ImGuiColorEditFlags_AlphaBar;
        if (!sidePreview)
            flags |= ImGuiColorEditFlags_NoSidePreview;
        if (pickerMode == 1)
            flags |= ImGuiColorEditFlags_PickerHueBar;
        if (pickerMode == 2)
            flags |= ImGuiColorEditFlags_PickerHueWheel;
        if (displayMode == 1)
            flags |= ImGuiColorEditFlags_NoInputs; // Disable all RGB/HSV/Hex displays
        if (displayMode == 2)
            flags |= ImGuiColorEditFlags_DisplayRGB; // Override display mode
        if (displayMode == 3)
            flags |= ImGuiColorEditFlags_DisplayHSV;
        if (displayMode == 4)
            flags |= ImGuiColorEditFlags_DisplayHex;
        ImGui::ColorPicker4("MyColor##4", (float *)&color, flags, refColor ? &refColorV.x : NULL);

        ImGui::Text("Set defaults in code:");
        ImGui::SameLine();
        HelpMarker("SetColorEditOptions() is designed to allow you to set boot-time default.\n"
                   "We don't have Push/Pop functions because you can force options on a per-widget basis if needed,"
                   "and the user can change non-forced ones with the options menu.\nWe don't have a getter to avoid"
                   "encouraging you to persistently save values that aren't forward-compatible.");
        if (ImGui::Button("Default: Uint8 + HSV + Hue Bar"))
            ImGui::SetColorEditOptions(ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayHSV |
                                       ImGuiColorEditFlags_PickerHueBar);
        if (ImGui::Button("Default: Float + HDR + Hue Wheel"))
            ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR |
                                       ImGuiColorEditFlags_PickerHueWheel);

        // HSV encoded support (to avoid RGB<>HSV round trips and singularities when S==0 or V==0)
        static ImVec4 colorHsv(0.23f, 1.0f, 1.0f, 1.0f); // Stored as HSV!
        ImGui::Spacing();
        ImGui::Text("HSV encoded colors");
        ImGui::SameLine();
        HelpMarker(
            "By default, colors are given to ColorEdit and ColorPicker in RGB, but ImGuiColorEditFlags_InputHSV"
            "allows you to store colors as HSV and pass them to ColorEdit and ColorPicker as HSV. This comes with the"
            "added benefit that you can manipulate hue values with the picker even when saturation or value are zero.");
        ImGui::Text("Color widget with InputHSV:");
        ImGui::ColorEdit4("HSV shown as RGB##1", (float *)&colorHsv,
                          ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputHSV | ImGuiColorEditFlags_Float);
        ImGui::ColorEdit4("HSV shown as HSV##1", (float *)&colorHsv,
                          ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_InputHSV | ImGuiColorEditFlags_Float);
        ImGui::DragFloat4("Raw HSV values", (float *)&colorHsv, 0.01f, 0.0f, 1.0f);
        ImGui::End();
    }
}

void utils::ShowResourcesWindow(bool *childResources)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.84f, 0.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.12f, 0.12f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.00f, 0.00f, 0.00f, 0.54f));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.31f, 0.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.13f, 0.13f, 0.13f, 0.50f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.13f, 0.13f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14f, 0.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.42f, 0.42f, 0.42f, 0.31f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.31f, 0.00f, 1.00f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.17f, 0.00f, 0.55f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.00f, 2.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.00f, 3.00f));
    ImGui::SetNextWindowSize(ImVec2(500,600), ImGuiCond_Once);
#ifdef __EMSCRIPTEN__
    if (ImGui::Begin("Resources", child_resources, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextWrapped("Some useful resources for developers using Dear ImGui.");
        ImGui::TextWrapped("Keep in mind that the most helpful resource will always be the Dear ImGui Demo (Tools > Demo Window) and imgui/imgui_demo.cpp.");
        ImGui::Spacing();
        ImGui::CollapsingHeader("Dear ImGui", ImGuiTreeNodeFlags_DefaultOpen);
        {
            ImGui::Bullet(); if(ImGui::SmallButton("Dear ImGui Source: github.com/ocornut/imgui")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui");}
            ImGui::Bullet(); if(ImGui::SmallButton("Issues")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/issues");}
            ImGui::Bullet(); if(ImGui::SmallButton("Wiki")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/wiki");}
            ImGui::Bullet(); if(ImGui::SmallButton("Manual")) {HyperlinkHelper::OpenUrl("https://pthom.github.io/imgui_manual_online");}
            ImGui::Bullet(); if(ImGui::SmallButton("discourse.dearimgui.org")) {HyperlinkHelper::OpenUrl("https://discourse.dearimgui.org");}
            ImGui::Bullet(); if(ImGui::SmallButton("Discussions")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/discussions");}
            ImGui::Bullet(); if(ImGui::SmallButton("How to open an Issue or Pull Request #2261")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/issues/2261");}
            ImGui::Bullet(); if(ImGui::SmallButton("Docking")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/wiki/Docking");}
            ImGui::Bullet(); if(ImGui::SmallButton("Gallery")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/issues/4451");}
            ImGui::Bullet(); if(ImGui::SmallButton("Software using DearImGui")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/wiki/Software-using-dear-imgui");}
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, RainbowCol());
        ImGui::CollapsingHeader("Cool Addons", ImGuiTreeNodeFlags_DefaultOpen);
        {
            ImGui::Bullet(); if(ImGui::SmallButton("HankiDesign/awesome-dear-imgui")) {HyperlinkHelper::OpenUrl("https://github.com/HankiDesign/awesome-dear-imgui");}
            ImGui::Bullet(); if(ImGui::SmallButton("epezent/implot")) {HyperlinkHelper::OpenUrl("https://github.com/epezent/implot");}
            ImGui::Bullet(); if(ImGui::SmallButton("soufianekhiat/DearWidgets")) {HyperlinkHelper::OpenUrl("https://github.com/soufianekhiat/DearWidgets");}
            ImGui::Bullet(); if(ImGui::SmallButton("dfranx/ImFileDialog")) {HyperlinkHelper::OpenUrl("https://github.com/dfranx/ImFileDialog");}
            ImGui::Bullet(); if(ImGui::SmallButton("Toggle Button")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/issues/1537#issuecomment-355569554");}
            ImGui::Bullet(); if(ImGui::SmallButton("Knob")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/issues/942#issuecomment-268369298");}
            ImGui::Bullet(); if(ImGui::SmallButton("aiekick/ImGuiFontStudio")) {HyperlinkHelper::OpenUrl("https://github.com/aiekick/ImGuiFontStudio");}
            ImGui::Bullet(); if(ImGui::SmallButton("Add extra keys #2625")) {HyperlinkHelper::OpenUrl("https://github.com/ocornut/imgui/pull/2625");}
        }
        ImGui::PopStyleColor(1);
        ImGui::Spacing();
        ImGui::CollapsingHeader("Frameworks", ImGuiTreeNodeFlags_DefaultOpen);
        {
            ImGui::Bullet(); if(ImGui::SmallButton("pthom/hello_imgui")) {HyperlinkHelper::OpenUrl("https://github.com/pthom/hello_imgui");}
            ImGui::Bullet(); if(ImGui::SmallButton("JamesBoer/ImFrame")) {HyperlinkHelper::OpenUrl("https://github.com/JamesBoer/ImFrame");}
            ImGui::Bullet(); if(ImGui::SmallButton("pr0g/sdl-bgfx-imgui-starter")) {HyperlinkHelper::OpenUrl("https://github.com/pr0g/sdl-bgfx-imgui-starter");}
            ImGui::Bullet(); if(ImGui::SmallButton("mahilab/mahi-gui")) {HyperlinkHelper::OpenUrl("https://github.com/mahilab/mahi-gui");}
        }
        ImGui::End();
    }
#else
    if (ImGui::Begin("Resources", childResources, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::TextWrapped("Some useful resources for developers using Dear ImGui.");
        ImGui::TextWrapped("Keep in mind that the most helpful resource will always be the Dear ImGui Demo (Tools > Demo Window) and imgui/imgui_demo.cpp.");
        ImGui::Spacing();
        ImGui::TextWrapped("Click = Copy to clipboard");
        ImGui::Spacing();
        ImGui::CollapsingHeader("Dear ImGui", ImGuiTreeNodeFlags_DefaultOpen);
        {
            ImGui::Bullet(); if(ImGui::SmallButton("Dear ImGui Source: github.com/ocornut/imgui")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Issues")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/issues"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Wiki")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/wiki"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Manual")) {ImGui::LogToClipboard(); ImGui::LogText("https://pthom.github.io/imgui_manual_online"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("discourse.dearimgui.org")) {ImGui::LogToClipboard(); ImGui::LogText("https://discourse.dearimgui.org/"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Discussions")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/discussions"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("How to open an Issue or Pull Request #2261")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/issues/2261"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Docking")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/wiki/Docking"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Gallery")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/issues/4451"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Software using DearImGui")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/wiki/Software-using-dear-imgui"); ImGui::LogFinish();}
        }
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Header, RainbowCol());
        ImGui::CollapsingHeader("Cool Addons", ImGuiTreeNodeFlags_DefaultOpen);
        {
            ImGui::Bullet(); if(ImGui::SmallButton("HankiDesign/awesome-dear-imgui")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/HankiDesign/awesome-dear-imgui"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("issues/useful widgets")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/issues?q=label%%3A\"useful+widgets\""); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("epezent/implot")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/epezent/implot"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("soufianekhiat/DearWidgets")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/soufianekhiat/DearWidgets"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("dfranx/ImFileDialog")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/dfranx/ImFileDialog"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Toggle Button")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/issues/1537#issuecomment-355569554"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Knob")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/issues/942#issuecomment-268369298"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("aiekick/ImGuiFontStudio")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/aiekick/ImGuiFontStudio"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("Add extra keys #2625")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/ocornut/imgui/pull/2625"); ImGui::LogFinish();}
        }
        ImGui::PopStyleColor(1);
        ImGui::Spacing();
        ImGui::CollapsingHeader("Frameworks", ImGuiTreeNodeFlags_DefaultOpen);
        {
            ImGui::Bullet(); if(ImGui::SmallButton("pthom/hello_imgui")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/pthom/hello_imgui"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("JamesBoer/ImFrame")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/JamesBoer/ImFrame"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("pr0g/sdl-bgfx-imgui-starter")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/pr0g/sdl-bgfx-imgui-starter"); ImGui::LogFinish();}
            ImGui::Bullet(); if(ImGui::SmallButton("mahilab/mahi-gui")) {ImGui::LogToClipboard(); ImGui::LogText("https://github.com/mahilab/mahi-gui"); ImGui::LogFinish();}
        }
        ImGui::End();
    }
#endif
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(10);
}

void utils::ShowAboutWindow(bool *childAbout)
{
    std::string ver = NextRenderer::GetBuildVersion();
    std::string imguiver = IMGUI_VERSION;
    std::string fmtver = fmt::sprintf("%d", FMT_VERSION);

    ImGui::OpenPopup("About");
    if (Utilities::UI::BeginAnchoredPopupModal("About", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            TextCentered("gkNextRenderer");
            ImGui::Separator();
            ImGui::Text("Version: %s", ver.c_str());
            ImGui::Text("ImGui: %s", imguiver.c_str());
            ImGui::Text("Fmt: %s", fmtver.c_str());
            ImGui::Separator();

            if (ImGui::Button("Back")) { ImGui::CloseCurrentPopup(); *childAbout = false; }
            ImGui::EndPopup();
        }

}

bool utils::IsItemActiveAlt(ImVec2 pos, int id)
{
    bool active = false;
    ImGui::SetCursorPos(pos);
    ImGui::PushID(id);
    ImGui::InvisibleButton(" ", ImGui::GetItemRectSize());
    ImGui::PopID();
    active = ImGui::IsItemActive();
    return active;
}

bool utils::GrabButton(ImVec2 pos, int randomInt)
{
    bool active = false;
    ImGui::SetCursorPos(pos);
    ImGui::PushID(randomInt);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.00f, 0.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::BeginChild(randomInt + 9, ImVec2(20, 20));
    ImGui::Button("  ");
    active = ImGui::IsItemActive();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    return active;
}

void utils::HelpMarker(const char *desc)
{
    ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(0.92f, 0.92f, 0.92f, 1.00f));
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleColor(1);
}

float utils::CenterHorizontal()
{
    auto windowWidth = ImGui::GetWindowSize().x;
    auto itemWidth   = ImGui::GetItemRectSize().x;
    float posX = ((windowWidth - itemWidth) * 0.5f);
    return posX;
}
