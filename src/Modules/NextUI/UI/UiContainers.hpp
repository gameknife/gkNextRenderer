#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextUI/UI/UiScopes.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"

#include <memory>

namespace NextUI::Foundation
{
    struct FOverlayPanelOptions
    {
        const char* windowId = "OverlayPanel";
        ImVec2 position{};
        ImVec2 size{};
        ImVec2 padding{10.0f, 5.0f};
        ImVec2 itemSpacing{6.0f, 0.0f};
        float rounding = 8.0f;
        float borderSize = 0.0f;
        float borderAlpha = 0.74f;
        float backgroundAlpha = 0.82f;
        EColor backgroundColor = EColor::Background;
        ImGuiWindowFlags extraFlags = 0;
    };

    class FScopedOverlayPanel final
    {
    public:
        explicit FScopedOverlayPanel(const FOverlayPanelOptions& options);
        ~FScopedOverlayPanel() = default;
        GK_NON_COPIABLE(FScopedOverlayPanel)
        explicit operator bool() const { return window_ != nullptr && static_cast<bool>(*window_); }

    private:
        FScopedStyle style_;
        std::unique_ptr<FScopedWindow> window_;
    };

    class FScopedInsetPanel final
    {
    public:
        FScopedInsetPanel(const char* id,
                          ImVec2 size = {},
                          bool border = true,
                          ImGuiWindowFlags flags = 0,
                          ImVec2 padding = ImVec2(10.0f, 10.0f),
                          float backgroundAlpha = 0.30f);
        ~FScopedInsetPanel() = default;
        GK_NON_COPIABLE(FScopedInsetPanel)
        explicit operator bool() const { return child_ != nullptr && static_cast<bool>(*child_); }

    private:
        FScopedStyle style_;
        std::unique_ptr<FScopedChild> child_;
    };

    class FScopedSection final
    {
    public:
        FScopedSection(const char* icon, const char* label, bool defaultOpen = true);
        ~FScopedSection();
        GK_NON_COPIABLE(FScopedSection)
        explicit operator bool() const { return open_; }

    private:
        bool open_ = false;
        std::unique_ptr<FScopedStyle> bodyStyle_;
    };

    class FScopedToolbar final
    {
    public:
        FScopedToolbar(const char* id,
                       ImVec2 size = {},
                       ImVec2 padding = ImVec2(4.0f, 4.0f),
                       ImVec2 spacing = ImVec2(4.0f, 0.0f));
        ~FScopedToolbar() = default;
        GK_NON_COPIABLE(FScopedToolbar)
        explicit operator bool() const { return child_ != nullptr && static_cast<bool>(*child_); }

    private:
        FScopedStyle style_;
        std::unique_ptr<FScopedChild> child_;
    };

    void LabeledRow(const char* label,
                    float ratio = 0.40f,
                    float minLabelWidth = 96.0f,
                    float maxLabelWidth = 140.0f);
}
