#pragma once

#include <string>

namespace NextAI
{
    class FToolRegistry;

    struct FRepoToolsOptions
    {
        std::string repoRoot;          // absolute or relative path to repository root
        bool enableRunCmd = true;      // expose run_cmd (git-only by default)
        bool enableGit = true;         // expose git_log / git_show / repoTrackedFiles helpers
        bool enableReadFile = true;
        bool enableListDir = true;
        bool enableFindFiles = true;
        bool enableSearchText = true;
        bool enableFindSymbol = true;
    };

    // Register the generic repo-exploration tools into the registry.
    // Tools added: list_dir, find_files, search_text, find_symbol, read_file,
    // git_log, git_show, run_cmd (whitelist: git status/diff/log/show/ls-files/rev-parse/branch).
    void RegisterRepoTools(FToolRegistry& registry, const FRepoToolsOptions& options);
}
