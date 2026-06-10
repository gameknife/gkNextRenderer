#pragma once

// ============================================================================
// FScadCsg.h - Constructive Solid Geometry backend for the SCAD loader.
//
// Operates purely on triangle soups (groups of 3 dvec3, SCAD Z-up space) so the
// evaluator stays backend-agnostic. When GK_WITH_MANIFOLD is enabled the ops use
// the Manifold library for robust booleans; otherwise they degrade gracefully:
//   * difference   -> positive returned unchanged (subtraction skipped)
//   * intersection -> first operand returned
//   * hull         -> concatenation of all parts
// ============================================================================

#include <vector>

#include <glm/glm.hpp>

#include "Modules/ScadLoader/FScadGeometry.h" // TriSoup

namespace Assets::scad
{
    class ScadCsg
    {
    public:
        // True when a real boolean backend is compiled in.
        static bool BackendAvailable();

        // Boolean union of all parts into a single solid (concatenation fallback).
        static TriSoup Union(const std::vector<TriSoup>& parts, bool& outOk);

        // positive minus the union of all negatives.
        static TriSoup Difference(const TriSoup& positive, const std::vector<TriSoup>& negatives, bool& outOk);

        // Intersection of all operands (each operand is one combined soup).
        static TriSoup Intersection(const std::vector<TriSoup>& operands, bool& outOk);

        // Convex hull of all input vertices.
        static TriSoup Hull(const std::vector<TriSoup>& parts, bool& outOk);
    };
} // namespace Assets::scad
