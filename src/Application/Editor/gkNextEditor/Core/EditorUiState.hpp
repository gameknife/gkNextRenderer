#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Runtime/Command/CommandHistory.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>

namespace Editor
{
    inline uint32_t ActiveColor = IM_COL32(64, 128, 255, 255);
    constexpr uint32_t InvalidId = std::numeric_limits<uint32_t>::max();
    constexpr size_t kRecentScenesCap = 10;
    constexpr size_t kMaxCameraViewports = 3;

    enum class EEditorViewportId
    {
        Scene,
        CameraView0,
        CameraView1,
        CameraView2
    };

    struct EditorCameraViewState
    {
        bool open = false;
        ImVec2 contentPos{0.0f, 0.0f};
        ImVec2 contentSize{0.0f, 0.0f};
        bool hovered = false;
        bool focused = false;
    };

    struct EditorUiState
    {
        struct ToolbarState
        {
            int projectIndex = 0;
            int backendIndex = 0;
            int platformIndex = 0;
            int buildConfigIndex = 0;
        };

        struct OutlinerState
        {
            uint32_t renameTargetId = InvalidId;
            std::string renameBuffer;
            bool openRenamePopup = false;
            bool focusRenameInput = false;
            bool prevAutoScrollEnabled = true;
            uint32_t lastSelectionId = InvalidId;
            uint32_t pendingScrollTargetId = InvalidId;
            bool suppressNextSelectionAutoScroll = false;
            ImGuiTextFilter nodeFilter;
        };

        struct PropertiesState
        {
            uint32_t editingNodeId = InvalidId;
            std::string editingName;
            ImGuiTextFilter propertyFilter;
        };

        struct ContentBrowserState
        {
            bool initialized = false;
            std::filesystem::path currentPath;
            int currentSection = 0;
            ImGuiTextFilter contentFilter;
            ImGuiTextFilter materialFilter;
            ImGuiTextFilter textureFilter;
            ImGuiTextFilter meshFilter;
        };

        struct ViewportOverlayState
        {
            int projectionMode = 0;
            int displayMode = 0;
            int cameraIndex = 0;
            float angleSnap = 10.0f;
            float distanceSnap = 0.25f;
        };

        struct SettingsPanelState
        {
            int selectedCategory = 0;
            bool showAdvanced = false;
            bool showAllCVars = false;
            char search[128]{};
        };

        struct MaterialEditorState
        {
            struct TrackedEdit
            {
                uint32_t materialId = InvalidId;
                std::string key;
                Assets::FMaterial before;
            };

            bool shouldFocusEditor = true;
            float previewYaw = 0.0f;
            float previewPitch = 0.0f;
            float previewDistance = 4.0f;
            std::optional<TrackedEdit> trackedEdit;
        };

        struct SequencerState
        {
            enum class EChannel
            {
                Translation,
                Rotation,
                Scale,
                SunRotation,
                SunElevation,
                SkyRotation,
                SunIntensity,
                SkyIntensity,
                SunColor,
                SkyColor,
            };

            float currentTime = 0.0f;
            float pixelsPerSecond = 120.0f;
            float framesPerSecond = 30.0f;
            int selectedTrack = -1;
            int selectedKey = -1;
            EChannel selectedChannel = EChannel::Translation;
            bool snapToFrames = true;
            bool initialized = false;
            bool draggingKey = false;
            float dragOriginalTime = 0.0f;
            std::vector<Assets::AnimationTrack> editBefore;
            bool trackingValueEdit = false;
        };

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
        bool scriptConsolePanel = true;
        bool hotReloadPanel = false;
        bool settingsPanel = false;
        bool cameraViewPanel = false; // legacy alias for cameraViews[0].open
        bool sequencerPanel = true;
        uint32_t pendingExpandTargetId = InvalidId;
        uint32_t pendingCollapseTargetId = InvalidId;
        bool dockResetRequested = false;

        // Recent scenes
        std::vector<std::string> recentScenes;

        // Current scene file path (set on load, used by Ctrl+S save)
        std::string currentScenePath;
        std::mutex sceneDialogMutex;
        std::string pendingOpenScenePath;
        std::string pendingSaveScenePath;

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
        bool viewportHovered = false;
        bool viewportFocused = true;
        std::array<EditorCameraViewState, kMaxCameraViewports> cameraViews{};
        EEditorViewportId activeViewport = EEditorViewportId::Scene;

        // Material editor
        bool ed_material = false;
        Assets::FMaterial* selected_material = nullptr;
        MaterialEditorState materialEditor;
        SequencerState sequencer;

        // Per-surface UI state
        ToolbarState toolbar;
        OutlinerState outliner;
        PropertiesState propertiesState;
        ContentBrowserState contentBrowserState;
        ViewportOverlayState viewportOverlay;
        SettingsPanelState settings;

        // Tools/children
        bool child_style = false;
        bool child_demo = false;
        bool child_metrics = false;
        bool child_debug_log = false;
        bool child_color = false;
        bool child_stack = false;
        bool child_resources = false;
        bool child_about = false;
        bool child_mat_editor = false;

        // Fonts
        ImFont* bigIcon = nullptr;

        EditorUiState() = default;
        
    };
} // namespace Editor
