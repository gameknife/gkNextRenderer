#if WIN32 && !defined(__MINGW32__)
#pragma warning( disable : 4005)
#endif

#pragma once

#include <string>
#include <functional>
#include <cstdint>
#include "imgui.h"
#include "imgui_stdlib.h"
#include "EditorUtils.h"

class NextEngine;

namespace Assets
{
    struct Material;
    struct FMaterial;
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
    inline uint32_t ActiveColor = IM_COL32(64, 128, 255, 255);
    
    struct GUI
    {
        bool                    state                      = true;                 // Alive

        bool                    menubar                    = true;                 // Menubar State
        void                    ShowMenubar();         

        bool                    sidebar                    = true;                 // Sidebar State
        void                    ShowSidebar(Assets::Scene* scene);         

        bool                    properties                 = true;                 // Properties State
        void                    ShowProperties();      

        bool                    viewport                   = true;                 // Viewport State
        uint32_t                selected_obj_id               = -1;                 // Viewport Selected
        Assets::Scene*          current_scene              = nullptr;
        NextEngine*             engine                     = nullptr;               // Engine
        void                    ShowViewport               (ImGuiID id);

        bool                    contentBrowser             = true;                // Workspace "Output"
        void                    ShowContentBrowser();

        bool                    materialBrowser            = true;                // Workspace "Output"
        void                    ShowMaterialBrowser();

        bool                    textureBrowser             = true;                // Workspace "Output"
        void                    ShowTextureBrowser();

        bool                    meshBrowser                = true;
        void                    ShowMeshBrowser();
        
        void                    DrawGeneralContentBrowser(bool iconOrTex, uint32_t globalId, const std::string& name, const char* icon, ImU32 color, std::function<void ()> doubleclick_action);

        bool                    ed_material                = false;                // Material Editor
        Assets::FMaterial*       selected_material          = nullptr;              // Material Selected
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
        
        uint32_t                selectedItemId              = -1; // Selected Item in Content Browser
    };

}