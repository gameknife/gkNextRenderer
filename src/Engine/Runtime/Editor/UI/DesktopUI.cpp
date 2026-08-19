#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "Engine/Runtime/Editor/UI/AppChrome.hpp"
#include "Engine/Runtime/Editor/UI/UiContainers.hpp"
#include "Engine/Runtime/Editor/UI/UiWidgets.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"

#include <algorithm>
#include <imgui_internal.h>
#include <fmt/format.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

namespace NextUI::Theme
{
    namespace
    {
        constexpr float kTitleBarControlButtonWidth = 46.0f;
        constexpr float kTitleBarControlButtonCount = 3.0f;
        constexpr float kTitleBarControlsWidth = kTitleBarControlButtonWidth * kTitleBarControlButtonCount;
        constexpr const char* kBrandIconAssetPath = "assets/brand/gknext_logo_icon.png";
        constexpr const char* kBrandIconAssetPath125 = "assets/brand/gknext_logo_icon_125.png";
        constexpr const char* kBrandIconAssetPath150 = "assets/brand/gknext_logo_icon_150.png";
        constexpr const char* kBrandIconAssetPath175 = "assets/brand/gknext_logo_icon_175.png";
        constexpr const char* kBrandIconAssetPath200 = "assets/brand/gknext_logo_icon_200.png";
        constexpr float kDetailPanelBackgroundAlpha = 0.82f;
        constexpr float kDetailPanelHeaderAlpha = 0.56f;
        constexpr float kDetailPanelBorderAlpha = 0.52f;

        const char* GetBrandIconAssetPath(float uiScale)
        {
            if (uiScale >= 1.875f)
            {
                return kBrandIconAssetPath200;
            }
            if (uiScale >= 1.625f)
            {
                return kBrandIconAssetPath175;
            }
            if (uiScale >= 1.375f)
            {
                return kBrandIconAssetPath150;
            }
            if (uiScale >= 1.125f)
            {
                return kBrandIconAssetPath125;
            }
            return kBrandIconAssetPath;
        }

        float CalcFontTextWidth(ImFont* font, const char* text)
        {
            if (text == nullptr || text[0] == '\0')
            {
                return 0.0f;
            }

            ImFont* activeFont = font != nullptr ? font : ImGui::GetFont();
            return activeFont->CalcTextSizeA(activeFont->LegacySize, FLT_MAX, 0.0f, text).x;
        }

        bool DrawWindowControlButton(const char* label, const char* tooltip, ImVec2 size, ImVec4 hoverColor,
                                     ImVec4 activeColor)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hoverColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, activeColor);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            DrawTooltip(tooltip);
            return pressed;
        }

        void DrawBrandIcon(NextEngine& engine, ImDrawList* drawList, ImVec2 min, float size)
        {
            if (drawList == nullptr)
            {
                return;
            }

            UserInterface* userInterface = engine.GetUserInterface();
            if (userInterface == nullptr)
            {
                DrawBrandMark(drawList, min, size);
                return;
            }

            const UserInterface::FUiTextureHandle texture =
                userInterface->RequestUiTexture(GetBrandIconAssetPath(userInterface->UiScale()), false,
                                                 EUiTextureLifetime::Persistent);
            if (!texture.valid || texture.pixelSize.x <= 0.0f || texture.pixelSize.y <= 0.0f)
            {
                DrawBrandMark(drawList, min, size);
                return;
            }

            const float aspect = texture.pixelSize.x / texture.pixelSize.y;
            float drawWidth = size;
            float drawHeight = drawWidth / aspect;
            if (drawHeight > size)
            {
                drawHeight = size;
                drawWidth = drawHeight * aspect;
            }

            const ImVec2 drawMin(
                min.x + std::floor((size - drawWidth) * 0.5f),
                min.y + std::floor((size - drawHeight) * 0.5f));
            const ImVec2 drawMax(drawMin.x + drawWidth, drawMin.y + drawHeight);
            drawList->AddImage(texture.textureId, drawMin, drawMax, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                               ImGui::GetColorU32(ImGuiCol_Text));
        }

        float CalcFormLabelWidth(float contentWidth, float ratio, float minLabelWidth, float maxLabelWidth)
        {
            return std::clamp(contentWidth * ratio, minLabelWidth, maxLabelWidth);
        }

        float CalcBadgeWidth(const char* label)
        {
            const float textWidth = ImGui::CalcTextSize(label != nullptr ? label : "").x;
            return textWidth + 14.0f;
        }
    } // namespace

    ImFont* GetDefaultFont(NextEngine& engine)
    {
        if (UserInterface* userInterface = engine.GetUserInterface())
        {
            return userInterface->GetDefaultFont();
        }
        return nullptr;
    }

    ImFont* GetTitleFont(NextEngine& engine)
    {
        if (UserInterface* userInterface = engine.GetUserInterface())
        {
            return userInterface->GetTitleBarFont();
        }
        return nullptr;
    }

    void ApplyProfessionalTheme()
    {
        Foundation::ApplyTheme();
    }

    void DrawBrandMark(ImDrawList* drawList, ImVec2 min, float size)
    {
        if (drawList == nullptr)
        {
            return;
        }

        const ImVec2 max(min.x + size, min.y + size);
        const float rounding = std::max(3.0f, size * 0.18f);
        drawList->AddRectFilled(min, max, ColorU32(EColor::Brand, 0.16f), rounding);
        drawList->AddRect(min, max, ColorU32(EColor::Brand, 0.92f), rounding, 0, 1.5f);

        const float pad = size * 0.24f;
        const float stroke = std::max(2.0f, size * 0.10f);
        const ImU32 lineColor = ColorU32(EColor::Brand);
        drawList->AddLine(ImVec2(min.x + pad, min.y + size - pad), ImVec2(min.x + pad, min.y + pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + pad, min.y + pad), ImVec2(min.x + size * 0.52f, min.y + pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size * 0.52f, min.y + pad), ImVec2(min.x + size * 0.52f, min.y + size - pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size * 0.52f, min.y + size - pad), ImVec2(min.x + size - pad, min.y + size - pad), lineColor, stroke);
        drawList->AddLine(ImVec2(min.x + size - pad, min.y + size - pad), ImVec2(min.x + size - pad, min.y + pad), lineColor, stroke);
    }

    void DrawAppTitleBar(NextEngine& engine, const FAppTitleBarConfig& config)
    {
        Foundation::FUiContext context;
        context.imguiContext = ImGui::GetCurrentContext();
        if (NextUI::UserInterface* ui = engine.GetUserInterface())
        {
            context.defaultFont = ui->GetDefaultFont();
            context.titleFont = config.TitleFont != nullptr ? config.TitleFont : ui->GetTitleBarFont();
            context.metrics = Foundation::FUiMetrics::FromScale(ui->UiScale());
        }
        context.allowWindowCommands = static_cast<bool>(config.OnMinimize) ||
            static_cast<bool>(config.OnToggleMaximize) || static_cast<bool>(config.OnClose);

        Foundation::FAppTitleBarOptions options;
        options.windowId = config.BrandWindowId;
        options.menuWindowId = config.MenuWindowId;
        options.rightWindowId = config.RightWindowId;
        options.appName = config.AppName;
        options.height = config.Height;
        options.rightContentWidth = config.RightContentWidth;
        options.brandHorizontalPadding = config.BrandHorizontalPadding;
        options.brandIconSize = config.BrandIconSize;
        options.brandTextSpacing = config.BrandTextSpacing;
        options.menuTrailingPadding = config.MenuTrailingPadding;
        options.menuHitPadding = config.MenuHitPadding;
        options.isMaximized = config.IsMaximized;
        options.drawBrandIcon = [&engine](ImDrawList* drawList, ImVec2 min, float size)
        {
            DrawBrandIcon(engine, drawList, min, size);
        };
        options.drawMenuBar = config.DrawMenuBar;
        options.drawRightContent = config.DrawRightContent;
        const Foundation::FAppChromeResult result = Foundation::DrawAppTitleBar(context, options);

        if (context.allowWindowCommands)
        {
            engine.ConfigureCustomTitleBarDrag(true, result.dragHeight,
                                               result.dragLeftReservedWidth, result.dragRightReservedWidth);
        }
        switch (result.action)
        {
        case Foundation::EAppChromeAction::Minimize:
            if (config.OnMinimize) config.OnMinimize();
            break;
        case Foundation::EAppChromeAction::ToggleMaximize:
            if (config.OnToggleMaximize) config.OnToggleMaximize();
            break;
        case Foundation::EAppChromeAction::Close:
            if (config.OnClose) config.OnClose();
            break;
        case Foundation::EAppChromeAction::None:
            break;
        }
    }

    void DrawBottomBar(const FBottomBarConfig& config)
    {
        Foundation::FBottomBarOptions options;
        options.windowId = config.WindowId;
        options.height = config.Height;
        options.horizontalPadding = config.HorizontalPadding;
        options.verticalPadding = config.VerticalPadding;
        options.centerWidth = config.CenterWidth;
        options.rightWidth = config.RightWidth;
        options.drawLeftContent = config.DrawLeftContent;
        options.drawCenterContent = config.DrawCenterContent;
        options.drawRightContent = config.DrawRightContent;
        Foundation::DrawBottomBar(options);
    }

    void DrawTooltip(const char* text)
    {
        Foundation::Tooltip(text);
    }

    bool IconButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        return Foundation::IconButton(label, tooltip, active, size);
    }

    bool GhostButton(const char* label, const char* tooltip, ImVec2 size)
    {
        Foundation::FButtonOptions options;
        options.variant = Foundation::EButtonVariant::Ghost;
        options.size = size;
        options.tooltip = tooltip;
        return Foundation::Button(label, options);
    }

    bool ToolbarButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        Foundation::FButtonOptions options;
        options.variant = Foundation::EButtonVariant::Toolbar;
        options.size = size;
        options.tooltip = tooltip;
        options.active = active;
        return Foundation::Button(label, options);
    }

    void PushViewportToolbarStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(EColor::Surface, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(EColor::SurfaceHover, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, Color(EColor::Accent, 0.24f));
        ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Surface, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.72f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.0f));
    }

    void PopViewportToolbarStyle()
    {
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(3);
    }

    bool DrawFlatViewportButton(const char* label, const char* tooltip, bool active, ImVec2 size)
    {
        Foundation::FButtonOptions options;
        options.variant = Foundation::EButtonVariant::Toolbar;
        options.size = size;
        options.tooltip = tooltip;
        options.active = active;
        return Foundation::Button(label, options);
    }

    void PushViewportPopupStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, Color(EColor::Background, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::SurfaceHover, 0.46f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.78f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.26f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.0f));
    }

    void PopViewportPopupStyle()
    {
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(5);
    }

    bool DrawViewportComboOption(const char* label, bool selected)
    {
        return Foundation::ComboOption(label, selected);
    }

    void PushToolWindowStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, Color(EColor::SurfaceElevated));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Color(EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::SurfaceElevated, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.32f));
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, Color(EColor::Surface, 0.56f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, Color(EColor::Background, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, Color(EColor::SurfaceElevated, 0.32f));
    }

    void PopToolWindowStyle()
    {
        ImGui::PopStyleColor(8);
        ImGui::PopStyleVar(7);
    }

    void PushToolWindowContentStyle()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    }

    void PopToolWindowContentStyle()
    {
        ImGui::PopStyleVar();
    }

    void BeginFormRow(const char* label, float ratio, float minLabelWidth, float maxLabelWidth)
    {
        Foundation::LabeledRow(label, ratio, minLabelWidth, maxLabelWidth);
    }

    bool BeginInsetPanel(const char* id, ImVec2 size, bool border, ImGuiWindowFlags flags, ImVec2 padding,
                         float backgroundAlpha)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Color(EColor::Background, backgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.82f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        return ImGui::BeginChild(id, size, border, flags);
    }

    void EndInsetPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    bool BeginOverlayPanel(const FOverlayPanelConfig& config)
    {
        ImGui::SetNextWindowPos(config.Position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(config.Size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(config.BackgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, config.Padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, config.ItemSpacing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, config.Rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, config.BorderSize);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(config.BackgroundColor, config.BackgroundAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::BorderStrong, config.BorderAlpha));

        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | config.ExtraFlags;
        return ImGui::Begin(config.WindowId, nullptr, flags);
    }

    void EndOverlayPanel()
    {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
    }

    bool BeginSection(const char* icon, const char* label, bool defaultOpen)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Header, Color(EColor::Background, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::Accent, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, 0.84f));

        const std::string header = fmt::format("{} {}", icon ? icon : "", label ? label : "");
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(header.c_str(), flags);

        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        if (open)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Indent(5.0f);
        }
        return open;
    }

    void EndSection()
    {
        ImGui::Unindent(5.0f);
        ImGui::PopStyleVar();
        ImGui::Spacing();
    }

    void DrawPanelHeader(const char* icon, const char* title, const char* subtitle)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text));
        ImGui::Text("%s %s", icon ? icon : "", title ? title : "");
        ImGui::PopStyleColor();

        if (subtitle != nullptr && subtitle[0] != '\0')
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
        }

        DrawThinSeparator(0.9f);
    }

    void DrawThinSeparator(float alpha)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddLine(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y), ColorU32(EColor::Border, alpha), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void DrawVerticalSeparator(float height, float spacing, float alpha)
    {
        ImGui::SameLine(0.0f, spacing);
        const ImVec2 separatorMin = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(separatorMin.x, separatorMin.y + 1.0f),
            ImVec2(separatorMin.x, separatorMin.y + height - 1.0f),
            ColorU32(EColor::Border, alpha));
        ImGui::Dummy(ImVec2(1.0f, height));
        ImGui::SameLine(0.0f, spacing);
    }

    void DrawProgressBar(float fraction, ImVec4 color, ImVec2 size)
    {
        fraction = std::clamp(fraction, 0.0f, 1.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pos, pos + size, ColorU32(EColor::Background, 0.86f), 4.0f);
        drawList->AddRect(pos, pos + size, ColorU32(EColor::BorderStrong, 0.88f), 4.0f);
        if (fraction > 0.0f)
        {
            const float fillWidth = std::max(2.0f, size.x * fraction);
            drawList->AddRectFilled(pos + ImVec2(1.0f, 1.0f), pos + ImVec2(fillWidth - 1.0f, size.y - 1.0f),
                                    ImGui::GetColorU32(color), 3.0f);
        }
        ImGui::Dummy(size);
    }

    bool ModeRailButton(const char* icon, const char* tooltip, bool active, float buttonSize)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Color(EColor::Accent, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::Accent, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::Accent, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::AccentHover));
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.65f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
        }
        const bool pressed = ImGui::Button(icon ? icon : "?", ImVec2(buttonSize, buttonSize));
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        if (active)
        {
            // 4px accent strip on the left edge, like the mockup.
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const float stripWidth = 3.0f;
            const float stripPad = 6.0f;
            drawList->AddRectFilled(
                ImVec2(itemMin.x - 4.0f, itemMin.y + stripPad),
                ImVec2(itemMin.x - 4.0f + stripWidth, itemMax.y - stripPad),
                ColorU32(EColor::AccentHover), stripWidth * 0.5f);
        }

        DrawTooltip(tooltip);
        return pressed;
    }

    bool BeginFloatingPanel(const char* id, const char* icon, const char* title, bool* pOpen,
                             ImVec2 position, ImVec2 size, ImVec2 pivot, const bool detachedViewport)
    {
        if (detachedViewport && (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGuiWindowClass windowClass;
            windowClass.ParentViewportId = 0;
            windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&windowClass);
            ImGui::SetNextWindowPos(position, ImGuiCond_Appearing, pivot);
            ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
        }
        else
        {
            ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
            ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        }
        // Detail panels share the viewport toolbar's dark, lightly translucent
        // surface instead of reading as an opaque application window.
        ImGui::SetNextWindowBgAlpha(kDetailPanelBackgroundAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, Color(EColor::Border, kDetailPanelBorderAlpha));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, Color(EColor::Background, kDetailPanelBackgroundAlpha));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs;
        if (!detachedViewport)
        {
            flags |= ImGuiWindowFlags_NoMove;
        }

        const bool visible = ImGui::Begin(id, nullptr, flags);
        if (!visible)
        {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return false;
        }

        // Header strip
        constexpr float headerHeight = 38.0f;
        const ImVec2 winPos = ImGui::GetWindowPos();
        const ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Surface, kDetailPanelHeaderAlpha), 8.0f, ImDrawFlags_RoundCornersTop);
        drawList->AddLine(
            ImVec2(winPos.x, winPos.y + headerHeight),
            ImVec2(winPos.x + winSize.x, winPos.y + headerHeight),
            ColorU32(EColor::Border, kDetailPanelBorderAlpha));

        // Title text
        const float textY = winPos.y + (headerHeight - ImGui::GetTextLineHeight()) * 0.5f;
        const float textX = winPos.x + 14.0f;
        const ImU32 iconCol = ColorU32(EColor::AccentHover);
        const ImU32 titleCol = ColorU32(EColor::Text);
        if (icon != nullptr && icon[0] != '\0')
        {
            drawList->AddText(ImVec2(textX, textY), iconCol, icon);
            const float iconWidth = ImGui::CalcTextSize(icon).x;
            drawList->AddText(ImVec2(textX + iconWidth + 8.0f, textY), titleCol, title ? title : "");
        }
        else
        {
            drawList->AddText(ImVec2(textX, textY), titleCol, title ? title : "");
        }

        // Optional close X
        if (pOpen != nullptr)
        {
            const float closeSize = 20.0f;
            ImGui::SetCursorScreenPos(ImVec2(winPos.x + winSize.x - closeSize - 10.0f,
                                             winPos.y + (headerHeight - closeSize) * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(EColor::SurfaceHover, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::TextMuted));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            if (ImGui::Button(ICON_FA_XMARK, ImVec2(closeSize, closeSize)))
            {
                *pOpen = false;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
        }

        // Move cursor below header & start a child for the body so padding works as expected.
        ImGui::SetCursorScreenPos(ImVec2(winPos.x, winPos.y + headerHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##FloatingPanelBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);
        return true;
    }

    void EndFloatingPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    bool BeginDetailPanel(const FDetailPanelConfig& config)
    {
        if (!BeginFloatingPanel(config.WindowId, config.Icon, config.Title, config.Open,
                                config.Position, config.Size, config.Pivot, config.DetachedViewport))
        {
            return false;
        }

        // BeginChild must always be paired with EndChild, even if it returns
        // false. The wrapper therefore reports the floating shell's visibility
        // and owns the inset pair in EndDetailPanel().
        BeginInsetPanel(config.ContentWindowId, ImVec2(0.0f, 0.0f), config.ContentBorder,
                        config.ContentFlags, config.ContentPadding, config.ContentBackgroundAlpha);
        return true;
    }

    void EndDetailPanel()
    {
        EndInsetPanel();
        EndFloatingPanel();
    }

    bool BeginPanelSection(const char* label, bool defaultOpen)
    {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Color(EColor::SurfaceHover, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, Color(EColor::SurfaceHover, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_Text, Color(EColor::Text, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
        if (defaultOpen)
        {
            flags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        const bool open = ImGui::CollapsingHeader(label ? label : "", flags);

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);

        if (open)
        {
            ImGui::PushID(label);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));
            ImGui::Dummy(ImVec2(0.0f, 1.0f));
        }
        return open;
    }

    void EndPanelSection()
    {
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }

    void Sparkline(const float* values, int count, ImVec2 size, ImVec4 color,
                   float scaleMin, float scaleMax, bool baselineAtZero)
    {
        if (values == nullptr || count <= 1)
        {
            ImGui::Dummy(size);
            return;
        }

        if (size.x <= 0.0f)
        {
            size.x = ImGui::GetContentRegionAvail().x;
        }
        if (size.y <= 0.0f)
        {
            size.y = ImGui::GetTextLineHeight() * 1.6f;
        }

        if (scaleMin == FLT_MAX || scaleMax == FLT_MAX)
        {
            // Auto-fit mode. For timing-style data (always >= 0) pin the baseline
            // at 0 and only fit the top — fitting both ends amplifies noise because
            // the min keeps jittering frame to frame.
            float lo = baselineAtZero ? 0.0f : values[0];
            float hi = values[0];
            for (int i = 1; i < count; ++i)
            {
                hi = std::max(hi, values[i]);
                if (!baselineAtZero)
                {
                    lo = std::min(lo, values[i]);
                }
            }
            scaleMin = lo;
            scaleMax = hi;
        }
        const float range = std::max(0.0001f, scaleMax - scaleMin);

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, origin + size, ColorU32(EColor::Background, 0.55f), 4.0f);

        const float stepX = size.x / static_cast<float>(count - 1);
        ImU32 lineCol = ImGui::GetColorU32(color);
        ImU32 fillCol = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, color.w * 0.18f));

        // Build polyline
        std::vector<ImVec2> pts;
        pts.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            const float t = (values[i] - scaleMin) / range;
            const float x = origin.x + stepX * static_cast<float>(i);
            const float y = origin.y + size.y - 2.0f - t * (size.y - 4.0f);
            pts.emplace_back(x, y);
        }

        // Fill underneath
        const ImVec2 baseRight(pts.back().x, origin.y + size.y);
        const ImVec2 baseLeft(pts.front().x, origin.y + size.y);
        for (int i = 0; i + 1 < count; ++i)
        {
            ImVec2 quad[4] = {pts[i], pts[i + 1],
                              ImVec2(pts[i + 1].x, origin.y + size.y),
                              ImVec2(pts[i].x, origin.y + size.y)};
            drawList->AddConvexPolyFilled(quad, 4, fillCol);
        }
        (void)baseRight; (void)baseLeft;

        drawList->AddPolyline(pts.data(), count, lineCol, ImDrawFlags_None, 1.5f);
        ImGui::Dummy(size);
    }
} // namespace NextUI::Theme
