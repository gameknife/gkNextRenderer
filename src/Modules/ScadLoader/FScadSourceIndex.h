#pragma once

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

    struct FScadSourceIndex
    {
        std::vector<FScadDefinitionSpan> definitions;

        const FScadDefinitionSpan* Find(EScadDefinitionKind kind, std::string_view name) const;
    };

    // Builds a parser-aware index over the original byte stream. use/include
    // directive bytes are masked (not removed), so all returned offsets address
    // the caller's source exactly.
    bool BuildScadSourceIndex(const std::string& source, FScadSourceIndex& outIndex, std::string& outError);
} // namespace Assets::Scad
