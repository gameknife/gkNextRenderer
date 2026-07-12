#pragma once

// ============================================================================
// FScadTess.h - earcut-based tessellation for the SCAD loader.
//
// Triangulates/extrudes sets of 2D contours using even-odd nesting (outer +
// holes), so concave polygons and polygons with holes work. Shared by
// linear_extrude, polygon(), and the text() backend. Guarded by GK_WITH_EARCUT;
// when unavailable, falls back to per-contour fan triangulation (convex only).
// ============================================================================

#include <vector>

#include <glm/glm.hpp>

#include "Modules/ScadLoader/FScadGeometry.h" // TriSoup

namespace Assets::Scad
{
    class ScadTess
    {
    public:
        static bool Available();

        // Extrudes closed 2D contours (even-odd fill) along +Z to a triangle soup.
        static void ExtrudeEvenOdd(const std::vector<std::vector<glm::dvec2>>& contours,
                                   double height,
                                   bool center,
                                   TriSoup& out);
    };
} // namespace Assets::Scad
