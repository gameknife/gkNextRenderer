#pragma once

#include "Common/CoreMinimal.hpp"
#include "Application/Editor/gkNextEditor/EditorActionDispatcher.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class NextEngine;
struct EditorContext;

namespace Editor
{
    struct ScriptLogEntry
    {
        std::string message;
        bool isError;
    };

    struct FDeferredEditorAction
    {
        EEditorAction action = EEditorAction::Camera_FocusSelected;
        std::string args;
        std::string commandText;
        std::string description;
    };

    class FEditorScriptExecutor
    {
    public:
        explicit FEditorScriptExecutor(NextEngine& engine);

        // Register Editor.* JS helper functions into the QuickJS context
        void RegisterEditorBindings(void* jsContext);

        // Execute editorscript text (one command per line)
        void ExecuteScriptText(const std::string& scriptText, EditorContext* editorContext = nullptr,
                               bool deferHighRiskActions = false);

        // Execute JavaScript code via QuickJS eval
        void EvalJavaScript(const std::string& code);

        // Get and clear execution logs
        std::vector<ScriptLogEntry> TakeLog();
        std::vector<FDeferredEditorAction> TakeDeferredActions();

        bool ExecuteDeferredAction(const FDeferredEditorAction& deferredAction, EditorContext& editorContext);

        // Public for JS callback access
        void Log(const std::string& message);
        static FEditorScriptExecutor* activeInstance_;

    private:
        // EditorScript command handlers
        void ExecSelect(const std::vector<std::string>& tokens);
        void ExecRename(const std::vector<std::string>& tokens);
        void ExecDelete(const std::vector<std::string>& tokens);
        void ExecDuplicate(const std::vector<std::string>& tokens);
        void ExecMove(const std::vector<std::string>& tokens);
        void ExecRotate(const std::vector<std::string>& tokens);
        void ExecScale(const std::vector<std::string>& tokens);
        void ExecSetProperty(const std::vector<std::string>& tokens);
        void ExecRenamePattern(const std::vector<std::string>& tokens);
        void ExecListNodes(const std::vector<std::string>& tokens);
        void ExecCVar(const std::vector<std::string>& tokens);
        void ExecAction(const std::vector<std::string>& tokens);

        // Helpers
        std::vector<std::string> Tokenize(const std::string& line);
        uint32_t ResolveNode(const std::string& nameOrId);
        bool DispatchAction(EditorContext& editorContext, EEditorAction action, std::string_view args,
                            std::string_view commandText);
        bool IsHighRiskAction(EEditorAction action) const;
        std::optional<std::string> ResolveScenePath(const std::string& sceneRef, std::string& errorMessage,
                                                    std::vector<std::string>* outCandidates = nullptr) const;
        void LogError(const std::string& message);

        NextEngine& engine_;
        EditorContext* activeEditorContext_ = nullptr;
        bool deferHighRiskActions_ = false;
        std::vector<FDeferredEditorAction> deferredActions_;
        std::vector<ScriptLogEntry> log_;
    };
} // namespace Editor
