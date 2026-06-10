#pragma once

// ============================================================================
// FScadGeometry.h - Primitive tessellation for the SCAD loader.
//
// All builders emit a triangle soup (groups of 3 vertices) in native SCAD
// object space (right-handed, Z-up, double precision). Triangles are wound CCW
// as seen from outside so a face normal of cross(v1-v0, v2-v0) points outward.
// The loader bakes these into engine vertices (Y-up + scale + flat normals).
// ============================================================================

#include <vector>

#include <glm/glm.hpp>

namespace Assets::scad
{
    using TriSoup = std::vector<glm::dvec3>;

    class ScadGeometry
    {
    public:
        // Number of facets for a circle of radius r, replicating OpenSCAD's
        // get_fragments_from_r($fn, $fa, $fs). fn<=0 falls back to fa/fs.
        static int Fragments(double r, double fn, double fa, double fs);

        // size is the full extent on each axis; center toggles origin centering.
        static void BuildCube(const glm::dvec3& size, bool center, TriSoup& out);

        static void BuildSphere(double radius, int fragments, TriSoup& out);

        // Cylinder/cone along +Z. r1 = bottom radius, r2 = top radius.
        static void BuildCylinder(double height, double r1, double r2, bool center, int fragments, TriSoup& out);

        // points indexed by faces; each face is a polygon (OpenSCAD clockwise-from-outside).
        static void BuildPolyhedron(const std::vector<glm::dvec3>& points,
                                    const std::vector<std::vector<int>>& faces,
                                    TriSoup& out);

        // Extrudes a closed 2D outline (XY) along +Z by height. center offsets to [-h/2, h/2].
        static void ExtrudePolygon(const std::vector<glm::dvec2>& outline, double height, bool center, TriSoup& out);

        // Revolves closed 2D profiles (XY, x = radius >= 0) around the Z axis by
        // angleDeg degrees using `segments` angular divisions. Profiles must be
        // CCW. angleDeg >= ~360 produces a closed surface; partial sweeps add caps.
        static void BuildRotateExtrude(const std::vector<std::vector<glm::dvec2>>& profiles,
                                       double angleDeg,
                                       int segments,
                                       TriSoup& out);
    };
} // namespace Assets::scad
