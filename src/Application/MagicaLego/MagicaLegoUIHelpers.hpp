#pragma once
#include "Common/CoreMinimal.hpp"
#include "MagicaLegoConstants.hpp"
#include <im_anim.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstring>

namespace MagicaLego::UIHelpers
{
    // Animation ID generator
    inline ImGuiID MakeAnimId(const char* label)
    {
        return ImHashStr(label, std::strlen(label), 0);
    }

    // Calculate zoomed rect for hover effects
    inline void GetZoomedRect(ImVec2 p_start, ImVec2 standardSize, float hoverFactor, float zoomScale,
                              ImVec2& p_min, ImVec2& p_max)
    {
        ImVec2 zoomedSize = standardSize;
        zoomedSize.x *= (1.0f + hoverFactor * zoomScale);
        zoomedSize.y *= (1.0f + hoverFactor * zoomScale);
        ImVec2 offset = (zoomedSize - standardSize) * 0.5f;
        p_min = p_start - offset;
        p_max = p_min + zoomedSize;
    }

    // Common window flags for panels
    constexpr int PanelFlags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;
}
