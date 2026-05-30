#pragma once

// ============================================================================
// FScadParser.h - Recursive-descent parser: tokens -> AST scope.
//
// `use <...>` / `include <...>` directives are expected to be stripped by the
// loader before tokenizing, so the parser never sees them.
// ============================================================================

#include "Engine/Assets/Loaders/FScadLexer.h"
#include "Engine/Assets/Loaders/FScadTypes.h"

namespace Assets::scad
{
    class ScadParser
    {
    public:
        // Parses a full token stream into a top-level scope.
        // Returns false on a fatal parse error (outError filled with line info).
        static bool Parse(const std::vector<Token>& tokens, Scope& outScope, std::string& outError);
    };
} // namespace Assets::scad
