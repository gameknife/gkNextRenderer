#pragma once

#include <string>
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

#include "ims_utils.h"

#ifdef __EMSCRIPTEN__
#include "JsClipboardTricks.h"
#endif

namespace Assets
{
    class Node;
}

namespace Assets
{
    class Scene;
}

namespace ImStudio
{

    struct GUI
    {
        bool                    state                      = true;                 // Alive
        bool                    compact                    = false;                // Compact/Spacious Switch
        bool                    wksp_create                = true;                 // Workspace "Create"

        bool                    menubar                    = true;                 // Menubar State
        void                    ShowMenubar();         

        bool                    sidebar                    = true;                 // Sidebar State
        void                    ShowSidebar(const Assets::Scene* scene);         

        bool                    properties                 = true;                 // Properties State
        void                    ShowProperties();      

        bool                    viewport                   = true;                 // Viewport State
        const Assets::Node*     selected_obj               = nullptr;              // Viewport Selected
        const Assets::Scene*    current_scene              = nullptr;
        void                    ShowViewport               (ImGuiID id);

        bool                    wksp_output                = false;                // Workspace "Output"
        ImVec2                  ot_P                       = {};                   // Output Window Pos
        ImVec2                  ot_S                       = {};                   // Output Window Size
        std::string             output                     = {};
        void                    ShowOutputWorkspace();        

        bool                    child_style                = false;                // Show Style Editor
        bool                    child_demo                 = false;                // Show Demo Window
        bool                    child_metrics              = false;                // Show Metrics Window
        bool                    child_color                = false;                // Show Color Export
        bool                    child_stack                = false;                // Show Stack Tool
        bool                    child_resources            = false;                // Show Help Resources
        bool                    child_about                = false;                // Show About Window

        ImFont*                 fontIcon_                  = nullptr;
        ImFont*                 bigIcon_                   = nullptr;
    };

}