#pragma once

#include "ScadOutline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ScadStudio
{
    struct FScadProjectFile
    {
        std::string path;   // relative to the session project dir, e.g. "main.scad" or "parts/wall.scad"
        std::string source; // complete file content
    };

    // A single chat turn shown in the right panel. `role` is "user" or "assistant".
    struct FChatTurn
    {
        std::string role;
        std::string content;    // full text (assistant text may contain the code block)
        std::string scadSource; // extracted SCAD source for this turn (assistant only)
        std::vector<FScadProjectFile> files; // extracted multi-file project snapshot (assistant only)
        std::string targetFilePath;          // module file replaced by scadSource when files is empty
        bool isError = false;
    };

    // One conversation == one evolving SCAD model. Persisted to disk as JSON; the
    // current source is also mirrored to <workspace>/<id>.scad so the engine loader
    // can pick it up via RequestLoadScene.
    struct FScadSession
    {
        std::string id;            // filesystem-safe, e.g. "model_0001"
        std::string title;         // shown in the left list (derived from first prompt)
        std::string currentSource; // latest authoritative .scad source (rendered)
        std::vector<FScadProjectFile> files; // empty means legacy single-file session
        std::string activeFilePath;          // target file for outline/chat edits; empty => root
        std::vector<FChatTurn> turns;
        int64_t createdAt = 0; // unix seconds
        int64_t updatedAt = 0; // unix seconds, refreshed on chat/edit/archive
        bool archived = false;

        // Runtime-only (not persisted):
        std::string scenePath;     // absolute path of the last written .scad
        std::string statusLine;    // "✓ loaded" / "✗ parse error: ..." / ""
        bool statusError = false;  // colour the status line red
        FOutlineResult outline;    // structure-tree outline of currentSource
        bool outlineDirty = true;  // rebuild outline lazily when source changes
        std::string previewModuleName;
        std::string previewModuleFilePath;
    };
}
