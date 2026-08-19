#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

namespace NextUI::Foundation
{
    enum class EButtonVariant : uint8_t
    {
        Primary,
        Secondary,
        Ghost,
        Toolbar,
        Danger,
    };

    enum class EButtonTone : uint8_t
    {
        Default,
        Accent,
        Success,
        Warning,
    };

    struct FButtonOptions
    {
        EButtonVariant variant = EButtonVariant::Secondary;
        EButtonTone tone = EButtonTone::Default;
        ImVec2 size{};
        const char* tooltip = nullptr;
        bool active = false;
        bool disabled = false;
    };

    void Tooltip(const char* text);
    bool Button(const char* label, const FButtonOptions& options = {});
    bool IconButton(const char* icon, const char* tooltip, bool active = false,
                    ImVec2 size = ImVec2(), bool activeUnderline = false);
    bool ComboOption(const char* label, bool selected);
}
