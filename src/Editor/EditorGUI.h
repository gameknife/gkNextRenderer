#if WIN32 && !defined(__MINGW32__)
#pragma warning( disable : 4005)
#endif

#pragma once

#include <string>
#include <cstdint>
#include "imgui.h"
#include "EditorUtils.h"

namespace Assets
{
    struct Material;
}

struct Statistics;

namespace Assets
{
    class Node;
}

namespace Assets
{
    class Scene;
}

namespace Editor
{

    struct GUI
    {
        bool                    state                      = true;                 // Alive

        bool                    menubar                    = true;                 // Menubar State
        void                    ShowMenubar();         

        bool                    sidebar                    = true;                 // Sidebar State
        void                    ShowSidebar(const Assets::Scene* scene);         

        bool                    properties                 = true;                 // Properties State
        void                    ShowProperties();      

        bool                    viewport                   = true;                 // Viewport State
        uint32_t                selected_obj_id               = -1;              // Viewport Selected
        Assets::Scene*    current_scene              = nullptr;
        void                    ShowViewport               (ImGuiID id, const Statistics& statistics);

        bool                    contentBrowser             = true;                // Workspace "Output"
        void                    ShowContentBrowser();

        bool                    ed_material                = false;                // Material Editor
        Assets::Material*       selected_material          = nullptr;              // Material Selected
        void                    ShowMaterialEditor();
        void                    OpenMaterialEditor();
        void                    ApplyMaterial();

        bool                    child_style                = false;                // Show Style Editor
        bool                    child_demo                 = false;                // Show Demo Window
        bool                    child_metrics              = false;                // Show Metrics Window
        bool                    child_color                = false;                // Show Color Export
        bool                    child_stack                = false;                // Show Stack Tool
        bool                    child_resources            = false;                // Show Help Resources
        bool                    child_about                = false;                // Show About Window
        bool                    child_mat_editor           = false;                // Show About Window
        
        ImFont*                 fontIcon_                  = nullptr;
        ImFont*                 bigIcon_                   = nullptr;
    };

}