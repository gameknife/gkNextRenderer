#pragma once

#include "ScadOutline.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ScadStudio
{
    // A single chat turn shown in the right panel. `role` is "user" or "assistant".
    struct FChatTurn
    {
        std::string role;
        std::string content;  // full text (assistant text may contain the code block)
        std::string scadSource; // extracted SCAD source for this turn (assistant only)
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
        std::vector<FChatTurn> turns;

        // Runtime-only (not persisted):
        std::string scenePath;     // absolute path of the last written .scad
        std::string statusLine;    // "✓ loaded" / "✗ parse error: ..." / ""
        bool statusError = false;  // colour the status line red
        FOutlineResult outline;    // structure-tree outline of currentSource
        bool outlineDirty = true;  // rebuild outline lazily when source changes
    };
}
