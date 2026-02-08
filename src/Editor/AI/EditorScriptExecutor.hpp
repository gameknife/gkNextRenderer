#pragma once

#include "Common/CoreMinimal.hpp"

#include <functional>
#include <string>
#include <vector>

class NextEngine;

namespace Editor
{
    struct ScriptLogEntry
    {
        std::string message;
        bool isError;
    };

    class FEditorScriptExecutor
    {
    public:
        explicit FEditorScriptExecutor(NextEngine& engine);

        // Register Editor.* JS helper functions into the QuickJS context
        void RegisterEditorBindings(void* jsContext);

        // Execute editorscript text (one command per line)
        void ExecuteScriptText(const std::string& scriptText);

        // Execute JavaScript code via QuickJS eval
        void EvalJavaScript(const std::string& code);

        // Get and clear execution logs
        std::vector<ScriptLogEntry> TakeLog();

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

        // Helpers
        std::vector<std::string> Tokenize(const std::string& line);
        uint32_t ResolveNode(const std::string& nameOrId);
        void LogError(const std::string& message);

        NextEngine& engine_;
        std::vector<ScriptLogEntry> log_;
    };
} // namespace Editor
