#pragma once

#include <string>
#include <vector>

namespace ScadStudio
{
    // One node in the structure-tree outline derived from the SCAD AST.
    struct FOutlineNode
    {
        std::string label;          // e.g. "module Handle(r)" / "translate([0,0,5])" / "cube([10,2,3])"
        std::string kind;           // "module" | "function" | "assign" | "instance"
        int line = 0;               // 1-based source line
        std::vector<FOutlineNode> children;
    };

    // Result of parsing source into an outline. `ok == false` means the source has a
    // lexer/parser error (also used as the syntax validator); `error` carries line info.
    struct FOutlineResult
    {
        bool ok = false;
        std::string error;
        std::vector<FOutlineNode> roots;
    };

    // Lex + parse `source` (use/include directives are stripped first, mirroring the
    // loader) and build a read-only structure outline. Pure CPU, no GPU / scene build.
    FOutlineResult BuildScadOutline(const std::string& source);
}
