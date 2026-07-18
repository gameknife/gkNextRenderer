#pragma once

#include "CharacterDesigner.hpp"
#include "KitCatalog.hpp"

#include <string>
#include <vector>

class NextEngine;
struct ImVec2;

namespace ScadLibrary
{
    // One placed instance on the compose bench.
    struct FBenchItem
    {
        int kitIndex = -1;
        std::string moduleName;
        float x = 0.0f;
        float y = 0.0f;
        float rotZ = 0.0f;
        float scale = 1.0f;
        char args[128] = {}; // extra call arguments, e.g. "seed = 3"
    };

    // Kit browser + compose bench around the central viewport. Left pane lists
    // the parts libraries under assets/scad/lib/ (click = isolated preview);
    // the right pane composes selected modules into a generated bench .scad
    // that reloads in the viewport.
    class ScadLibraryInterface
    {
    public:
        explicit ScadLibraryInterface(NextEngine& engine);

        void Config(); // OnPreConfigUI: ImGui config flags
        void Init();   // OnInitUI: fonts
        void Render(); // OnRenderUI: title bar + panels + viewport mapping

        // Engine hooks forwarded by ScadLibraryGameInstance for the rig preview.
        FRigPreview& RigPreview() { return rigPreview_; }

    private:
        void DrawTitleBar();
        void DrawBottomBar();
        void DrawBrowserPanel(const ImVec2& pos, const ImVec2& size);
        void DrawBenchPanel(const ImVec2& pos, const ImVec2& size);
        void DrawBenchContent();
        void DrawDesignerContent();
        void RescanKits();
        void PreviewModule(int kitIndex, const std::string& moduleName);
        void AddToBench(int kitIndex, const std::string& moduleName);
        void ReloadBench();
        void ExportBench();
        void ReloadDesigner();
        void ExportCharacter();
        std::string KitCharUsePath(bool relative) const;
        std::string BuildBenchSource(bool relativeUses) const;
        bool WriteWorkspaceFile(const std::string& fileName, const std::string& source,
                                std::string& outAbsPath);
        bool WriteAndLoad(const std::string& fileName, const std::string& source);

        NextEngine& engine_;
        std::string imguiIniPath_;

        std::vector<FKitInfo> kits_;
        std::vector<FBenchItem> bench_;

        // Character designer state.
        FCharacterDesigner designer_;
        FRigPreview rigPreview_;
        int kitCharIndex_ = -1;
        bool designerDirty_ = false;
        bool designerEverLoaded_ = false;
        float designerTint_[3] = {0.30f, 0.52f, 0.75f};
        char characterNameBuf_[128] = "my_character";

        // Browser state.
        char filterBuf_[128] = {};
        int selectedKit_ = -1;
        std::string selectedModule_;

        // Bench state.
        bool autoReload_ = true;
        bool benchDirty_ = false;
        bool showFloor_ = true;
        int fnSegments_ = 12;
        char exportNameBuf_[128] = "bench_export";
        // Placement cursor for newly added items (footprint-aware row layout).
        float benchCursorX_ = 0.0f;
        float benchCursorY_ = 0.0f;
        float benchRowDepth_ = 0.0f;
        int benchColCount_ = 0;

        std::string statusLine_;
        bool statusError_ = false;
        bool welcomeLoaded_ = false;
        bool browserCollapsed_ = false;
        bool benchCollapsed_ = false;
    };
}
