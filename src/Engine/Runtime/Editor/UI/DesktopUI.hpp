#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Editor/UI/UiTheme.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"

#include <functional>
#include <imgui.h>

namespace NextUI::Theme
{
    using EColor = Foundation::EColor;
    using Foundation::Color;
    using Foundation::ColorU32;
    ImFont* GetDefaultFont(NextEngine& engine);
    ImFont* GetTitleFont(NextEngine& engine);

    struct FAppTitleBarConfig
    {
        const char* BrandWindowId = "AppTitleBarBrand";
        const char* MenuWindowId = "AppTitleBarMenu";
        const char* RightWindowId = "AppTitleBarRight";
        const char* AppName = "";
        float Height = 48.0f;
        float RightContentWidth = 0.0f;
        float BrandHorizontalPadding = 14.0f;
        float BrandIconSize = 48.0f;
        float BrandTextSpacing = 10.0f;
        float MenuHitPadding = 28.0f;
        float MenuTrailingPadding = 8.0f;
        ImFont* TitleFont = nullptr;
        bool IsMaximized = false;
        std::function<float()> DrawMenuBar;
        std::function<void()> DrawRightContent;
        std::function<void()> OnMinimize;
        std::function<void()> OnToggleMaximize;
        std::function<void()> OnClose;
    };

    struct FBottomBarConfig
    {
        const char* WindowId = "AppBottomBar";
        float Height = 30.0f;
        float HorizontalPadding = 10.0f;
        float VerticalPadding = 4.0f;
        float CenterWidth = 0.0f;
        float RightWidth = 0.0f;
        std::function<void()> DrawLeftContent;
        std::function<void()> DrawCenterContent;
        std::function<void()> DrawRightContent;
    };

    struct FOverlayPanelConfig
    {
        const char* WindowId = "OverlayPanel";
        ImVec2 Position = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        ImVec2 Padding = ImVec2(10.0f, 5.0f);
        ImVec2 ItemSpacing = ImVec2(6.0f, 0.0f);
        float Rounding = 8.0f;
        // Overlay surfaces are flat by default. Set this explicitly for panels
        // that need an outlined treatment.
        float BorderSize = 0.0f;
        float BorderAlpha = 0.74f;
        float BackgroundAlpha = 0.82f;
        EColor BackgroundColor = EColor::Background;
        ImGuiWindowFlags ExtraFlags = 0;
    };

    // The standard detail surface used by renderer settings, diagnostics, and
    // application explorers. It owns both the floating chrome and the inset,
    // scrollable content surface so detail windows cannot drift in padding or
    // border treatment from one application to another.
    struct FDetailPanelConfig
    {
        const char* WindowId = "DetailPanel";
        const char* ContentWindowId = "##DetailPanelContent";
        const char* Icon = nullptr;
        const char* Title = nullptr;
        bool* Open = nullptr;
        ImVec2 Position = ImVec2(0.0f, 0.0f);
        ImVec2 Size = ImVec2(0.0f, 0.0f);
        ImVec2 Pivot = ImVec2(0.0f, 0.0f);
        bool DetachedViewport = false;
        ImVec2 ContentPadding = ImVec2(10.0f, 10.0f);
        bool ContentBorder = true;
        float ContentBackgroundAlpha = 0.30f;
        ImGuiWindowFlags ContentFlags = 0;
    };

    void ApplyProfessionalTheme();
    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size);
    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config);
    void DrawBottomBar(const FBottomBarConfig& config);
    void DrawTooltip(const char* text);
    // A non-positive size component is fitted to the active font and theme padding.
    bool IconButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(0.0f, 0.0f));
    bool GhostButton(const char* label, const char* tooltip = nullptr, ImVec2 size = ImVec2(0.0f, 0.0f));
    bool ToolbarButton(const char* label, const char* tooltip, bool active = false, ImVec2 size = ImVec2(34.0f, 30.0f));
    void PushViewportToolbarStyle();
    void PopViewportToolbarStyle();
    bool DrawFlatViewportButton(const char* label, const char* tooltip, bool active, ImVec2 size);
    void PushViewportPopupStyle();
    void PopViewportPopupStyle();
    bool DrawViewportComboOption(const char* label, bool selected);
    void PushToolWindowStyle();
    void PopToolWindowStyle();
    void PushToolWindowContentStyle();
    void PopToolWindowContentStyle();
    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize);
    bool BeginSection(const char* icon, const char* label, bool defaultOpen = true);
    void EndSection();
    void BeginFormRow(const char* label, float ratio = 0.40f, float minLabelWidth = 96.0f, float maxLabelWidth = 140.0f);
    bool BeginInsetPanel(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f), bool border = true,
                         ImGuiWindowFlags flags = 0, ImVec2 padding = ImVec2(10.0f, 10.0f),
                         float backgroundAlpha = 0.30f);
    void EndInsetPanel();
    bool BeginOverlayPanel(const FOverlayPanelConfig& config);
    void EndOverlayPanel();

    // Floating panel matching the new design language: rounded surface, single-line title with optional close X.
    // pOpen may be null. Returns true when the panel body is visible (matches ImGui::Begin semantics).
    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                             ImVec2 position, ImVec2 size, ImVec2 pivot = ImVec2(0.0f, 0.0f),
                             bool detachedViewport = false);
    void EndFloatingPanel();

    bool BeginDetailPanel(const FDetailPanelConfig& config);
    void EndDetailPanel();

    // Collapsible section inside a floating panel: chevron + title row, no border background.
    bool BeginPanelSection(const char* label, bool defaultOpen = true);
    void EndPanelSection();

    // Inline sparkline. width<=0 fills available width.
    // When scaleMin/scaleMax are left at FLT_MAX the range auto-fits the dataset.
    // Set baselineAtZero=true (in auto-fit mode) to pin scaleMin=0 so timing-style
    // data (frame time, pass ms) doesn't visually amplify small noise — the line
    // wiggles far less than the default min..max fit.
    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin = FLT_MAX, float scaleMax = FLT_MAX,
                   bool baselineAtZero = false);

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle = nullptr);
    void DrawThinSeparator(float alpha = 1.0f);
    void DrawVerticalSeparator(float height = 18.0f, float spacing = 12.0f, float alpha = 0.9f);
    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size);
} // namespace NextUI::Theme
