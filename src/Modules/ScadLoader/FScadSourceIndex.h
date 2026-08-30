#pragma once

#include "Modules/ScadLoader/FScadParser.h"
#include "Modules/ScadLoader/FScadTypes.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Assets::Scad
{
    enum class EScadDefinitionKind
    {
        Module,
        Function,
    };

    struct FScadDefinitionSpan
    {
        EScadDefinitionKind kind = EScadDefinitionKind::Module;
        std::string name;
        size_t begin = 0;
        size_t end = 0;
        size_t signatureBegin = 0;
        size_t signatureEnd = 0;
        int line = 1;
    };

    // One top-level statement of the indexed file, in source order.
    // `begin`/`end` address the caller's original bytes, so an editor can
    // rewrite, disable or delete exactly one statement and leave every other
    // byte (comments, formatting, unsupported syntax) untouched.
    struct FScadStatementSpan
    {
        StmtKind kind = StmtKind::Instance;
        std::string name;      // assign target / definition name / instance name
        std::string modifiers; // any of '*', '!', '#', '%'
        size_t begin = 0;
        size_t end = 0;
        int line = 1;
        int endLine = 1;

        // OpenSCAD's '*' disable modifier. The evaluator already skips these,
        // so it is how the editor "switches off" a structure in place instead
        // of deleting it.
        bool Disabled() const { return modifiers.find('*') != std::string::npos; }
    };

    struct FScadSourceIndex
    {
        std::vector<FScadDefinitionSpan> definitions;

        // One entry per `topLevel` statement, same order and count.
        std::vector<FScadStatementSpan> statements;

        // AST of the indexed file only (its use/include closure is not merged),
        // so statements[i] describes topLevel[i].
        Scope topLevel;

        const FScadDefinitionSpan* Find(EScadDefinitionKind kind, std::string_view name) const;
    };

    // Builds a parser-aware index over the original byte stream. use/include
    // directive bytes are masked (not removed), so all returned offsets address
    // the caller's source exactly.
    bool BuildScadSourceIndex(const std::string& source, FScadSourceIndex& outIndex, std::string& outError);

    // One byte-range replacement against an indexed source. An empty `text`
    // deletes the range; `begin == end` inserts at that point.
    struct FScadSourceEdit
    {
        size_t begin = 0;
        size_t end = 0;
        std::string text;
    };

    // Applies edits back-to-front so earlier offsets stay valid. Overlapping
    // ranges are resolved by dropping the later (lower-offset) edit, and
    // out-of-range edits are ignored, so a stale span can never corrupt the
    // file. Insertions at the same offset keep their relative order.
    std::string ApplyScadSourceEdits(const std::string& source, std::vector<FScadSourceEdit> edits);
} // namespace Assets::Scad
