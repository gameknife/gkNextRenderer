#pragma once

// ============================================================================
// FScadText.h - text() support for the SCAD loader via FreeType.
//
// Decomposes glyph outlines (incl. CJK via the bundled DroidSansFallback font),
// flattens beziers, groups contours into filled shapes with holes (even-odd
// nesting), triangulates with earcut, and extrudes to a 3D triangle soup in the
// glyph plane (XY, extruded along +Z) matching linear_extrude semantics.
//
// When GK_WITH_FREETYPE is disabled, Available() returns false and BuildText is
// a no-op so the loader simply skips text() with a warning.
// ============================================================================

#include <string>

#include "Modules/ScadLoader/FScadGeometry.h" // TriSoup

namespace Assets::scad
{
    class ScadText
    {
    public:
        static bool Available();

        // Appends the extruded text geometry (object space, Z-up) to out.
        // size = em height in model units; height/center = linear_extrude params.
        static bool BuildText(const std::string& text,
                              double size,
                              const std::string& halign,
                              const std::string& valign,
                              double height,
                              bool center,
                              double fnSegments,
                              TriSoup& out);
    };
} // namespace Assets::scad
