#include "Engine/Common/CoreMinimal.hpp"

#include "Modules/NextUI/UI/UiContainers.hpp"

namespace NextUI::Foundation
{
    FScopedOverlayPanel::FScopedOverlayPanel(const FOverlayPanelOptions& options)
    {
        ImGui::SetNextWindowPos(options.position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(options.size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(options.backgroundAlpha);
        style_.Add(ImGuiStyleVar_WindowPadding, options.padding)
            .Add(ImGuiStyleVar_ItemSpacing, options.itemSpacing)
            .Add(ImGuiStyleVar_WindowRounding, options.rounding)
            .Add(ImGuiStyleVar_WindowBorderSize, options.borderSize)
            .Add(ImGuiCol_WindowBg, Color(options.backgroundColor, options.backgroundAlpha))
            .Add(ImGuiCol_Border, Color(EColor::BorderStrong, options.borderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | options.extraFlags;
        window_ = std::make_unique<FScopedWindow>(options.windowId, nullptr, flags);
    }

    FScopedInsetPanel::FScopedInsetPanel(const char* id,
                                         const ImVec2 size,
                                         const bool border,
                                         const ImGuiWindowFlags flags,
                                         const ImVec2 padding,
                                         const float backgroundAlpha)
    {
        style_.Add(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha))
            .Add(ImGuiCol_Border, Color(EColor::Border, 0.82f))
            .Add(ImGuiStyleVar_WindowPadding, padding);
        const ImGuiChildFlags childFlags = border ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
        child_ = std::make_unique<FScopedChild>(id, size, childFlags, flags);
    }

    FScopedSection::FScopedSection(const char* icon, const char* label, const bool defaultOpen)
    {
        FScopedStyle headerStyle;
        headerStyle.Add(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f))
            .Add(ImGuiStyleVar_FrameBorderSize, 1.0f)
            .Add(ImGuiCol_Header, Color(EColor::Background, 0.5f))
            .Add(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.5f))
            .Add(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.5f))
            .Add(ImGuiCol_Border, Color(EColor::Border, 0.84f));

        const std::string header = fmt::format("{} {}", icon ? icon : "", label ? label : "");
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        open_ = ImGui::CollapsingHeader(header.c_str(), flags);
        if (open_)
        {
            bodyStyle_ = std::make_unique<FScopedStyle>();
            bodyStyle_->Add(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Indent(5.0f);
        }
    }

    FScopedSection::~FScopedSection()
    {
        if (open_)
        {
            ImGui::Unindent(5.0f);
            ImGui::Spacing();
        }
    }

    FScopedToolbar::FScopedToolbar(const char* id,
                                   const ImVec2 size,
                                   const ImVec2 padding,
                                   const ImVec2 spacing)
    {
        style_.Add(ImGuiStyleVar_WindowPadding, padding)
            .Add(ImGuiStyleVar_ItemSpacing, spacing)
            .Add(ImGuiStyleVar_ChildRounding, 6.0f)
            .Add(ImGuiStyleVar_ChildBorderSize, 0.0f)
            .Add(ImGuiCol_ChildBg, Color(EColor::Surface, 0.92f));
        child_ = std::make_unique<FScopedChild>(id, size, ImGuiChildFlags_None,
                                                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    }

    void LabeledRow(const char* label,
                    const float ratio,
                    const float minLabelWidth,
                    const float maxLabelWidth)
    {
        ImGui::AlignTextToFramePadding();
        {
            FScopedStyle textStyle;
            textStyle.Add(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::TextUnformatted(label ? label : "");
        }
        const float availableWidth = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
        const float labelWidth = std::clamp(availableWidth * ratio, minLabelWidth, maxLabelWidth);
        ImGui::SameLine(labelWidth);
    }
}
