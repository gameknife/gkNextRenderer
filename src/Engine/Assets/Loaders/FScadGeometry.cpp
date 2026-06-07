#include "Engine/Assets/Loaders/FScadGeometry.h"

#include "Engine/Assets/Loaders/FScadTypes.h"

#include <algorithm>
#include <cmath>

namespace Assets::scad
{
    namespace
    {
        double SignedArea2D(const std::vector<glm::dvec2>& poly)
        {
            double twice = 0.0;
            const size_t n = poly.size();
            for (size_t i = 0; i < n; ++i)
            {
                const glm::dvec2& a = poly[i];
                const glm::dvec2& b = poly[(i + 1) % n];
                twice += a.x * b.y - b.x * a.y;
            }
            return twice * 0.5;
        }
    } // namespace

    int ScadGeometry::Fragments(double r, double fn, double fa, double fs)
    {
        if (r < 1e-9)
        {
            return 3;
        }
        if (fn > 0.0)
        {
            return std::max(3, static_cast<int>(fn));
        }
        if (fa <= 0.0)
        {
            fa = 12.0;
        }
        if (fs <= 0.0)
        {
            fs = 2.0;
        }
        const double byAngle = 360.0 / fa;
        const double byLength = (r * 2.0 * kPi) / fs;
        return std::max(5, static_cast<int>(std::ceil(std::min(byAngle, byLength))));
    }

    void ScadGeometry::BuildCube(const glm::dvec3& size, bool center, TriSoup& out)
    {
        const glm::dvec3 mn = center ? -size * 0.5 : glm::dvec3(0.0);
        const glm::dvec3 mx = center ? size * 0.5 : size;

        const glm::dvec3 c[8] = {
            {mn.x, mn.y, mn.z}, // 0
            {mx.x, mn.y, mn.z}, // 1
            {mx.x, mx.y, mn.z}, // 2
            {mn.x, mx.y, mn.z}, // 3
            {mn.x, mn.y, mx.z}, // 4
            {mx.x, mn.y, mx.z}, // 5
            {mx.x, mx.y, mx.z}, // 6
            {mn.x, mx.y, mx.z}, // 7
        };

        // Each face as a quad with outward-CCW winding.
        static const int quads[6][4] = {
            {0, 3, 2, 1}, // bottom (-Z)
            {4, 5, 6, 7}, // top (+Z)
            {0, 1, 5, 4}, // front (-Y)
            {2, 3, 7, 6}, // back (+Y)
            {1, 2, 6, 5}, // right (+X)
            {0, 4, 7, 3}, // left (-X)
        };

        for (const auto& q : quads)
        {
            out.push_back(c[q[0]]);
            out.push_back(c[q[1]]);
            out.push_back(c[q[2]]);
            out.push_back(c[q[0]]);
            out.push_back(c[q[2]]);
            out.push_back(c[q[3]]);
        }
    }

    void ScadGeometry::BuildRotateExtrude(const std::vector<std::vector<glm::dvec2>>& profiles,
                                          double angleDeg,
                                          int segments,
                                          TriSoup& out)
    {
        const int segs = std::max(2, segments);
        const bool full = angleDeg >= 359.999;
        const double angleRad = angleDeg * kPi / 180.0;

        for (const std::vector<glm::dvec2>& profileIn : profiles)
        {
            if (profileIn.size() < 3) continue;
            std::vector<glm::dvec2> profile = profileIn;
            if (SignedArea2D(profile) < 0.0) std::reverse(profile.begin(), profile.end()); // force CCW

            auto vert = [&](const glm::dvec2& p, int k)
            {
                const double th = angleRad * static_cast<double>(k) / static_cast<double>(segs);
                return glm::dvec3(p.x * std::cos(th), p.x * std::sin(th), p.y);
            };

            const size_t m = profile.size();
            for (size_t i = 0; i < m; ++i)
            {
                const glm::dvec2& pi = profile[i];
                const glm::dvec2& pj = profile[(i + 1) % m];
                for (int k = 0; k < segs; ++k)
                {
                    const int k1 = (full && k == segs - 1) ? 0 : (k + 1);
                    const glm::dvec3 a = vert(pi, k);
                    const glm::dvec3 b = vert(pi, k1);
                    const glm::dvec3 c = vert(pj, k1);
                    const glm::dvec3 d = vert(pj, k);
                    out.push_back(a);
                    out.push_back(b);
                    out.push_back(c);
                    out.push_back(a);
                    out.push_back(c);
                    out.push_back(d);
                }
            }

            // End caps for partial sweeps (fan; correct for convex profiles).
            if (!full)
            {
                for (size_t k = 1; k + 1 < m; ++k)
                {
                    // Start cap at angle 0 (faces toward -sweep).
                    out.push_back(vert(profile[0], 0));
                    out.push_back(vert(profile[k + 1], 0));
                    out.push_back(vert(profile[k], 0));
                    // End cap at angle (faces toward +sweep).
                    out.push_back(vert(profile[0], segs));
                    out.push_back(vert(profile[k], segs));
                    out.push_back(vert(profile[k + 1], segs));
                }
            }
        }
    }

    void ScadGeometry::BuildSphere(double radius, int fragments, TriSoup& out)
    {
        const int sectors = std::max(3, fragments);
        const int rings = std::max(2, fragments / 2);

        auto pointAt = [&](int ring, int sector)
        {
            const double theta = kPi * static_cast<double>(ring) / static_cast<double>(rings); // 0..pi
            const double phi = 2.0 * kPi * static_cast<double>(sector) / static_cast<double>(sectors);
            const double ringR = radius * std::sin(theta);
            return glm::dvec3(ringR * std::cos(phi), ringR * std::sin(phi), radius * std::cos(theta));
        };

        for (int ring = 0; ring < rings; ++ring)
        {
            for (int sector = 0; sector < sectors; ++sector)
            {
                const glm::dvec3 a = pointAt(ring, sector);
                const glm::dvec3 b = pointAt(ring, sector + 1);
                const glm::dvec3 cc = pointAt(ring + 1, sector + 1);
                const glm::dvec3 d = pointAt(ring + 1, sector);

                if (ring != 0)
                {
                    out.push_back(a);
                    out.push_back(d);
                    out.push_back(b);
                }
                if (ring != rings - 1)
                {
                    out.push_back(b);
                    out.push_back(d);
                    out.push_back(cc);
                }
            }
        }
    }

    void ScadGeometry::BuildCylinder(double height, double r1, double r2, bool center, int fragments, TriSoup& out)
    {
        const int frag = std::max(3, fragments);
        const double z0 = center ? -height * 0.5 : 0.0;
        const double z1 = center ? height * 0.5 : height;
        r1 = std::max(0.0, r1);
        r2 = std::max(0.0, r2);

        auto ring = [&](double radius, double z, int i)
        {
            const double a = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(frag);
            return glm::dvec3(radius * std::cos(a), radius * std::sin(a), z);
        };

        for (int i = 0; i < frag; ++i)
        {
            const glm::dvec3 b0 = ring(r1, z0, i);
            const glm::dvec3 b1 = ring(r1, z0, i + 1);
            const glm::dvec3 t0 = ring(r2, z1, i);
            const glm::dvec3 t1 = ring(r2, z1, i + 1);

            // Side wall (skip degenerate slivers when a radius is zero).
            if (r1 > 1e-9 || r2 > 1e-9)
            {
                if (r1 > 1e-9 && r2 > 1e-9)
                {
                    out.push_back(b0);
                    out.push_back(b1);
                    out.push_back(t1);
                    out.push_back(b0);
                    out.push_back(t1);
                    out.push_back(t0);
                }
                else if (r2 <= 1e-9)
                {
                    // Cone narrowing to a top point.
                    out.push_back(b0);
                    out.push_back(b1);
                    out.push_back(glm::dvec3(0.0, 0.0, z1));
                }
                else
                {
                    // Cone narrowing to a bottom point.
                    out.push_back(glm::dvec3(0.0, 0.0, z0));
                    out.push_back(t1);
                    out.push_back(t0);
                }
            }

            // Bottom cap (normal -Z).
            if (r1 > 1e-9)
            {
                out.push_back(glm::dvec3(0.0, 0.0, z0));
                out.push_back(b1);
                out.push_back(b0);
            }
            // Top cap (normal +Z).
            if (r2 > 1e-9)
            {
                out.push_back(glm::dvec3(0.0, 0.0, z1));
                out.push_back(t0);
                out.push_back(t1);
            }
        }
    }

    void ScadGeometry::BuildPolyhedron(const std::vector<glm::dvec3>& points,
                                       const std::vector<std::vector<int>>& faces,
                                       TriSoup& out)
    {
        for (const auto& face : faces)
        {
            if (face.size() < 3)
            {
                continue;
            }
            // OpenSCAD winds faces clockwise as seen from outside; reverse to CCW.
            std::vector<int> idx(face.rbegin(), face.rend());
            bool valid = true;
            for (int v : idx)
            {
                if (v < 0 || v >= static_cast<int>(points.size()))
                {
                    valid = false;
                    break;
                }
            }
            if (!valid)
            {
                continue;
            }
            for (size_t k = 1; k + 1 < idx.size(); ++k)
            {
                out.push_back(points[idx[0]]);
                out.push_back(points[idx[k]]);
                out.push_back(points[idx[k + 1]]);
            }
        }
    }

    void ScadGeometry::ExtrudePolygon(const std::vector<glm::dvec2>& outlineIn, double height, bool center, TriSoup& out)
    {
        if (outlineIn.size() < 3)
        {
            return;
        }

        std::vector<glm::dvec2> outline = outlineIn;
        if (SignedArea2D(outline) < 0.0)
        {
            std::reverse(outline.begin(), outline.end()); // force CCW
        }

        const double z0 = center ? -height * 0.5 : 0.0;
        const double z1 = center ? height * 0.5 : height;
        const size_t n = outline.size();

        auto bottom = [&](size_t i) { return glm::dvec3(outline[i].x, outline[i].y, z0); };
        auto top = [&](size_t i) { return glm::dvec3(outline[i].x, outline[i].y, z1); };

        // Top cap (+Z), fan from vertex 0.
        for (size_t k = 1; k + 1 < n; ++k)
        {
            out.push_back(top(0));
            out.push_back(top(k));
            out.push_back(top(k + 1));
        }
        // Bottom cap (-Z), reversed fan.
        for (size_t k = 1; k + 1 < n; ++k)
        {
            out.push_back(bottom(0));
            out.push_back(bottom(k + 1));
            out.push_back(bottom(k));
        }
        // Side walls (outward for CCW outline).
        for (size_t i = 0; i < n; ++i)
        {
            const size_t j = (i + 1) % n;
            out.push_back(bottom(i));
            out.push_back(bottom(j));
            out.push_back(top(i));

            out.push_back(bottom(j));
            out.push_back(top(j));
            out.push_back(top(i));
        }
    }
} // namespace Assets::scad
