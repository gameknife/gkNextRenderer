#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <functional>

class NextEngine;
struct EditorContext;

namespace NextAI
{
    class FToolRegistry;
}

namespace Editor
{
    class FEditorScriptExecutor;

    // Returns a pointer to the editor frame's transient EditorContext, or nullptr
    // when no frame is currently being processed (UI not pumping yet).
    using FEditorContextProvider = std::function<EditorContext*()>;

    struct FEditorToolsOptions
    {
        NextEngine* engine = nullptr;
        FEditorScriptExecutor* executor = nullptr;
        FEditorContextProvider contextProvider; // required for write tools

        // When the LLM invokes `scene_load_scene`, defer to the user-confirm queue
        // owned by FEditorAIService instead of executing immediately.
        bool deferHighRiskActions = true;

        // Toggle individual tools off if a downstream subsystem (e.g. MagicaLego)
        // wants a leaner surface.
        bool enableQueryTools = true;
        bool enableMutationTools = true;
        bool enableActionTools = true;
        bool enableEscapeHatches = true; // run_editor_script / run_javascript
    };

    // All tools registered here have RequiresMainThread()=true. Caller must own a
    // gnb remote-tool dispatcher invokes these handlers from the Engine main
    // thread (Scene / QuickJS are not thread-safe).
    void RegisterEditorTools(NextAI::FToolRegistry& registry, const FEditorToolsOptions& options);
}
