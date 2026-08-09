#include "EditorUtils.h"

#include <fmt/core.h>
#include <fmt/printf.h>

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/AboutDialog.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <SDL3/SDL_misc.h>

namespace
{
void HelpMarker(const char* description)
{
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
} // namespace

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
    // Project resources. The previous contents were a page of Dear ImGui ecosystem
    // links carried over from an ImGui starter template and had nothing to do with
    // this engine.
    ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_Once);
    NextUI::Theme::PushToolWindowStyle();
    if (ImGui::Begin("Resources", childResources, ImGuiWindowFlags_NoCollapse))
    {
        NextUI::Theme::PushToolWindowContentStyle();
        NextUI::Theme::DrawPanelHeader(
            ICON_FA_BOOK_OPEN, "Resources", "Documentation and support for gkNextRenderer");

        const auto Link = [](const char* label, const char* url)
        {
            if (ImGui::Button(label, ImVec2(-FLT_MIN, ImGui::GetFrameHeight())))
            {
                SDL_OpenURL(url);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", url);
            }
        };

        Link("Project page", Utilities::UI::ProjectHomeUrl);
        Link("Documentation", Utilities::UI::ProjectDocsUrl);
        Link("Report an issue", Utilities::UI::ProjectIssuesUrl);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Keyboard and mouse bindings are listed in the renderer's shortcut panel "
            "and in docs/AGENT_GUIDE/quick-commands.md.");
        NextUI::Theme::PopToolWindowContentStyle();
    }
    ImGui::End();
    NextUI::Theme::PopToolWindowStyle();
}
void utils::ShowAboutWindow(bool *childAbout)
{
    // Shared with gkNextRenderer so both targets report the same information.
    NextUI::Theme::PushToolWindowStyle();
    NextUI::Theme::PushToolWindowContentStyle();
    Utilities::UI::ShowAboutDialog(*childAbout);
    NextUI::Theme::PopToolWindowContentStyle();
    NextUI::Theme::PopToolWindowStyle();
}
