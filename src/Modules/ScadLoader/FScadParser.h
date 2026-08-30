#pragma once

// ============================================================================
// FScadParser.h - Recursive-descent parser: tokens -> AST scope.
//
// `use <...>` / `include <...>` directives are expected to be stripped by the
// loader before tokenizing, so the parser never sees them.
// ============================================================================

#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadTypes.h"

#include <cstddef>

namespace Assets::Scad
{
    // Byte range of one top-level statement in the tokenized source. Recorded
    // by the parser itself so editors can splice a single statement without
    // re-deriving its extent from line numbers (which breaks as soon as two
    // statements share a line). Index i matches Scope entry i.
    struct FScadTopLevelSpan
    {
        size_t begin = 0; // first byte of the statement, modifiers included
        size_t end = 0;   // one past its ';' or closing '}'
        int line = 1;     // 1-based line of `begin`
        int endLine = 1;  // 1-based line of the last byte
    };

    class ScadParser
    {
    public:
        // Parses a full token stream into a top-level scope.
        // Returns false on a fatal parse error (outError filled with line info).
        // When outTopLevelSpans is non-null it receives one span per Scope entry.
        static bool Parse(const std::vector<Token>& tokens, Scope& outScope, std::string& outError,
                          std::vector<FScadTopLevelSpan>* outTopLevelSpans = nullptr);
    };
} // namespace Assets::Scad
