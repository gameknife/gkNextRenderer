#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/CommandHistory.hpp"

#include <imgui.h>

#include <cstdint>
#include <limits>

namespace Assets
{
    struct FMaterial;
}

namespace Editor
{
    inline uint32_t ActiveColor = IM_COL32(64, 128, 255, 255);
    constexpr uint32_t InvalidId = std::numeric_limits<uint32_t>::max();

    struct EditorUiState
    {
        bool state = true;

        // Panels
        bool sidebar = true;
        bool properties = true;
        bool commandHistoryPanel = false;
        bool viewport = true;
        bool contentBrowser = true;
        bool materialBrowser = true;
        bool textureBrowser = true;
        bool meshBrowser = true;
        bool logPanel = true;
        bool aiPanel = true;
        bool outlinerAutoScrollToSelection = true;

        // Selection
        uint32_t selected_obj_id = InvalidId;

        // Cross-panel asset selections (avoid mixing ids from different sources)
        uint32_t selectedMaterialId = InvalidId;
        uint32_t selectedTextureId = InvalidId;
        uint32_t selectedContentItemId = InvalidId;

        // Viewport content rect (screen space)
        ImVec2 viewportContentPos{0.0f, 0.0f};
        ImVec2 viewportContentSize{0.0f, 0.0f};
        bool viewportOnMainViewport = true;

        // Material editor
        bool ed_material = false;
        Assets::FMaterial* selected_material = nullptr;

        // Tools/children
        bool child_style = false;
        bool child_demo = false;
        bool child_metrics = false;
        bool child_color = false;
        bool child_stack = false;
        bool child_resources = false;
        bool child_about = false;
        bool child_mat_editor = false;

        // Fonts
        ImFont* fontIcon = nullptr;
        ImFont* bigIcon = nullptr;
        
    };
} // namespace Editor
