#pragma once

#include "CharacterDesigner.hpp"
#include "KitCatalog.hpp"
#include "TerrainProcessDocument.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

class NextEngine;
struct ImVec2;

namespace Assets
{
    class Node;
}

namespace ScadLibrary
{
    enum class EWorkspaceMode
    {
        SceneAssembly,
        CharacterDesigner,
        CharacterWorkbench,
    };

    // One placed instance in a scene assembly.
    struct FBenchItem
    {
        int kitIndex = -1;
        std::string moduleName;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float rotX = 0.0f;
        float rotY = 0.0f;
        float rotZ = 0.0f;
        float scale = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
        float color[4] = {0.78f, 0.78f, 0.78f, 1.0f};
        bool hasColor = false;
        char args[512] = {}; // extra call arguments, e.g. "seed = 3"
        bool evaluated = false;
        int sourceLine = 0;
        uint32_t runtimeNodeId = std::numeric_limits<uint32_t>::max();
    };

    struct FSceneAssemblyInfo
    {
        std::string relativePath;
        std::string absolutePath;
        std::vector<std::string> kitDependencies;
        bool generated = false;
    };

    // Scene assembly + character authoring around the central viewport.
    class ScadLibraryInterface
    {
    public:
        explicit ScadLibraryInterface(NextEngine& engine, std::string startupAssemblyPath = {});

        void Config(); // OnPreConfigUI: ImGui config flags
        void Init(); // OnInitUI: fonts
        void Render(); // OnRenderUI: title bar + panels + viewport mapping
        void SetWorkspaceMode(EWorkspaceMode mode);
        EWorkspaceMode WorkspaceMode() const { return workspaceMode_; }
        void SaveCurrentAssembly() { SaveAssembly(false); }
        bool SelectSceneObjectFromViewport(uint32_t hitInstanceId);
        bool TerrainFeatureConsumesMouse(double x, double y) const;
        bool IsTerrainFeatureDragging() const { return terrainFeatureDragging_ || terrainRuleDragging_; }
        bool IsTerrainProcessAssembly() const
        {
            return workspaceMode_ == EWorkspaceMode::SceneAssembly && assemblyProcedural_;
        }
        bool ConsumePreserveCameraOnNextSceneLoad();

        // Engine hooks forwarded by ScadLibraryGameInstance for the rig preview.
        FRigPreview& RigPreview() { return rigPreview_; }

    private:
        void DrawTitleBar();
        void DrawBottomBar();
        void DrawBrowserPanel(const ImVec2& pos, const ImVec2& size);
        void DrawBoneHierarchyPanel(const ImVec2& pos, const ImVec2& size);
        void DrawModePanel(const ImVec2& pos, const ImVec2& size);
        void DrawBenchContent();
        void DrawTerrainProcessContent();
        void DrawDesignerContent();
        void DrawWorkbenchContent();
        void DrawAnimationTimelinePanel(const ImVec2& pos, const ImVec2& size);
        void DrawViewportToolbar(const ImVec2& viewportPos);
        void DrawSceneGizmoToolbar(const ImVec2& viewportPos);
        void DrawSceneObjectGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize);
        void DrawTerrainFeatureToolbar(const ImVec2& viewportPos);
        void DrawTerrainFeatureOverlay(const ImVec2& viewportPos, const ImVec2& viewportSize);
        void RefreshTerrainFeatureOverlayCache();
        void CommitSceneGizmoEdit();
        Assets::Node* ResolveSceneObjectNode(FBenchItem& item, const glm::mat4& expectedWorld);
        void ApplySceneObjectTransform(FBenchItem& item, const glm::mat4& worldMatrix);
        void DrawBoneGizmo(const ImVec2& viewportPos, const ImVec2& viewportSize);
        void UpsertGizmoKey(EEditableRigChannel type, const glm::vec3& value);
        void DrawEquipmentEditor();
        void RescanKits();
        void RescanAssemblies();
        void PreviewModule(int kitIndex, const std::string& moduleName);
        void AddToBench(int kitIndex, const std::string& moduleName);
        void ReloadBench();
        void ExportBench();
        bool OpenAssembly(const std::string& path);
        void PreviewAssemblySource();
        void SaveAssembly(bool saveAs, bool reloadScene = true);
        bool ParseStructuredAssembly(const std::string& source);
        bool ImportEvaluatedAssembly(const std::string& sourcePath);
        bool ImportAssemblyTerrains(const std::string& source);
        bool ImportTerrainProcessAssembly(const std::string& sourcePath, const std::string& source);
        std::string BuildAssemblyPreviewSource() const;
        std::string BuildTerrainProcessSource() const;
        void ReloadTerrainProcess();
        void MarkTerrainProcessDirty();
        void ReloadDesigner();
        void ExportCharacter();
        void ReloadWorkbench();
        void ReloadWorkbenchStage();
        void ApplyWorkbenchRigEdit();
        std::string KitCharUsePath(bool relative) const;
        std::string BuildBenchSource(const std::filesystem::path& outputPath = {}) const;
        bool WriteWorkspaceFile(const std::string& fileName, const std::string& source, std::string& outAbsPath);
        bool WriteAndLoad(const std::string& fileName, const std::string& source);

        NextEngine& engine_;
        std::string imguiIniPath_;
        std::string startupAssemblyPath_;

        std::vector<FKitInfo> kits_;
        std::vector<FBenchItem> bench_;
        std::vector<FSceneAssemblyInfo> assemblies_;

        // Character designer state.
        FCharacterDesigner designer_;
        FRigPreview rigPreview_;
        int kitCharIndex_ = -1;
        bool designerDirty_ = false;
        bool designerEverLoaded_ = false;
        float designerTint_[3] = {0.30f, 0.52f, 0.75f};
        char characterNameBuf_[128] = "my_character";

        // Rig action/equipment workbench state.
        FCharacterWorkbench workbench_;
        bool workbenchEverLoaded_ = false;
        bool workbenchReloadRequested_ = false;
        bool workbenchEquipmentRebuildRequested_ = false;
        int workbenchClip_ = 0;
        int workbenchBone_ = 0;
        int timelineSelectedChannel_ = -1;
        int timelineSelectedKey_ = -1;
        bool timelineDraggingKey_ = false;
        float timelineVisibleDuration_ = 0.0f;
        int workbenchEditorTab_ = 0;
        int boneGizmoOperation_ = 1;
        char boneFilterBuf_[128] = {};
        EWorkspaceMode workspaceMode_ = EWorkspaceMode::SceneAssembly;
        char rigSourceBuf_[512] = "assets/scad/characters/nextdayz_survivor.scad";

        // Browser state.
        char filterBuf_[128] = {};
        char assemblyFilterBuf_[128] = {};
        char objectFilterBuf_[128] = {};
        int selectedKit_ = -1;
        std::string selectedModule_;
        int selectedAssembly_ = -1;
        int selectedBenchItem_ = -1;
        bool scrollToSelectedBenchItem_ = false;

        // Scene assembly state. Arbitrary kit-based SCAD files are editable as
        // source; files generated by ScadLibrary additionally round-trip through
        // the structured item list.
        bool autoReload_ = true;
        bool benchDirty_ = false;
        bool showFloor_ = true;
        int fnSegments_ = 12;
        char exportNameBuf_[128] = "my_scene";
        char assemblyPathBuf_[512] = "assets/scad/scenes/my_scene.scad";
        std::string openedAssemblyPath_;
        std::string assemblySource_;
        std::vector<std::string> openedAssemblyKits_;
        std::vector<std::string> assemblyTerrainSources_;
        FTerrainProcessDocument terrainProcess_;
        std::vector<std::string> terrainProcessWarnings_;
        bool assemblySourceDirty_ = false;
        bool assemblyStructured_ = false;
        bool assemblyEvaluated_ = false;
        bool assemblyProcedural_ = false;
        bool terrainProcessDirty_ = false;
        bool preserveCameraOnNextSceneLoad_ = false;
        int assemblyEditorTab_ = 0;
        int sceneGizmoOperation_ = 0;
        bool sceneGizmoWasUsing_ = false;
        bool sceneGizmoAwaitingPickRelease_ = false;
        struct FTerrainFeatureHandle
        {
            int featureIndex = -1;
            // -1: feature center, -2: radius, -3: height/depth, -4: width,
            // >= 0: polyline control point.
            int pointIndex = -1;
            glm::vec2 screen{0.0f};
            float worldHeight = 0.0f;
        };
        int selectedTerrainFeature_ = 0;
        bool terrainSelectionIsRule_ = false;
        bool scrollToSelectedTerrainItem_ = false;
        int terrainFeatureDragPoint_ = -1;
        bool showTerrainFeatureOverlay_ = true;
        bool terrainFeatureDragging_ = false;
        float terrainFeatureDragPlaneHeight_ = 0.0f;
        std::vector<FTerrainFeatureHandle> terrainFeatureHandles_;
        struct FTerrainRuleHandle
        {
            int ruleIndex = -1;
            // -1: position, -2: height sample, -3/-4: scatter region,
            // -5: scatter count, >= 0: along point.
            int pointIndex = -1;
            glm::vec2 screen{0.0f};
            float worldHeight = 0.0f;
        };
        int selectedTerrainRule_ = 0;
        int terrainRuleDragPoint_ = -1;
        bool terrainRuleDragging_ = false;
        float terrainRuleDragPlaneHeight_ = 0.0f;
        glm::vec2 terrainDragStartMouse_{0.0f};
        double terrainDragStartValue_ = 0.0;
        std::vector<FTerrainRuleHandle> terrainRuleHandles_;
        std::string terrainFeatureOverlayCacheKey_;
        std::shared_ptr<const Assets::Scad::FTerrainData> terrainFeatureOverlayData_;
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
} // namespace ScadLibrary
