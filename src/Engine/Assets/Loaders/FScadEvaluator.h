#pragma once

// ============================================================================
// FScadEvaluator.h - Evaluates a parsed SCAD program into colored geometry.
//
// MVP semantics:
//   * union/group/hull/minkowski  -> concatenate all child geometry
//   * difference/intersection      -> keep only the first child (degraded; a
//                                     true boolean backend (Manifold) is a
//                                     later phase). Emits a one-time warning.
// Output is grouped by quantized color so the loader builds one Model+Node per
// color (each Node uses a single material slot, dodging the 16-slot limit).
// Triangles are in native SCAD world space (Z-up); the loader converts to Y-up.
// ============================================================================

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Loaders/FScadTypes.h"

namespace Assets::scad
{
    struct ColorBucket
    {
        glm::vec4 color = glm::vec4(0.78f, 0.78f, 0.78f, 1.0f);
        std::vector<glm::dvec3> tris; // triangle soup (3N), SCAD world space (Z-up)
    };

    struct EvalResult
    {
        std::map<uint32_t, ColorBucket> buckets; // ordered by color key for determinism
        int warningCount = 0;
        size_t triangleCount = 0;
    };

    class ScadEvaluator
    {
    public:
        // modules/functions are the merged definition tables from the use/include closure.
        // mainTopLevel is the executable top-level scope of the main file only.
        static bool Evaluate(const Scope& mainTopLevel,
                             const std::unordered_map<std::string, StmtPtr>& modules,
                             const std::unordered_map<std::string, StmtPtr>& functions,
                             const ScadLoadOptions& options,
                             EvalResult& outResult,
                             std::string& outError);
    };
} // namespace Assets::scad
