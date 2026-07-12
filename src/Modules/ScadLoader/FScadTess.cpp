#include "Modules/ScadLoader/FScadTess.h"

#ifndef GK_WITH_EARCUT
#define GK_WITH_EARCUT 0
#endif

#include <algorithm>

#if GK_WITH_EARCUT
#include <array>
#include <cstdint>
#include <mapbox/earcut.hpp>
#endif

namespace Assets::Scad
{
    namespace
    {
        using Pt = glm::dvec2;
        using Contour = std::vector<Pt>;

        double SignedArea(const Contour& c)
        {
            double a = 0.0;
            for (size_t i = 0; i < c.size(); ++i)
            {
                const Pt& p = c[i];
                const Pt& q = c[(i + 1) % c.size()];
                a += p.x * q.y - q.x * p.y;
            }
            return a * 0.5;
        }

#if GK_WITH_EARCUT
        bool PointInContour(const Pt& pt, const Contour& c)
        {
            bool inside = false;
            for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++)
            {
                if (((c[i].y > pt.y) != (c[j].y > pt.y)) &&
                    (pt.x < (c[j].x - c[i].x) * (pt.y - c[i].y) / (c[j].y - c[i].y) + c[i].x))
                {
                    inside = !inside;
                }
            }
            return inside;
        }

        void AppendWalls(const Contour& ring, double z0, double z1, TriSoup& out)
        {
            const size_t m = ring.size();
            for (size_t i = 0; i < m; ++i)
            {
                const Pt& pi = ring[i];
                const Pt& pj = ring[(i + 1) % m];
                out.emplace_back(pi.x, pi.y, z0);
                out.emplace_back(pj.x, pj.y, z0);
                out.emplace_back(pi.x, pi.y, z1);
                out.emplace_back(pj.x, pj.y, z0);
                out.emplace_back(pj.x, pj.y, z1);
                out.emplace_back(pi.x, pi.y, z1);
            }
        }
#endif
    } // namespace

#if GK_WITH_EARCUT

    bool ScadTess::Available() { return true; }

    void ScadTess::ExtrudeEvenOdd(const std::vector<std::vector<glm::dvec2>>& contoursIn,
                                  double height,
                                  bool center,
                                  TriSoup& out)
    {
        std::vector<Contour> contours;
        for (const Contour& c : contoursIn)
        {
            if (c.size() >= 3) contours.push_back(c);
        }
        if (contours.empty()) return;

        const size_t n = contours.size();
        std::vector<int> depth(n, 0);
        std::vector<bool> isHole(n, false);
        for (size_t i = 0; i < n; ++i)
        {
            const Pt rep = contours[i][0];
            int d = 0;
            for (size_t j = 0; j < n; ++j)
            {
                if (i == j) continue;
                if (PointInContour(rep, contours[j])) ++d;
            }
            depth[i] = d;
            isHole[i] = (d % 2) == 1;
        }

        struct ShapeBuild { int outer; std::vector<int> holes; };
        std::vector<ShapeBuild> shapes;
        std::vector<int> shapeForContour(n, -1);
        for (size_t i = 0; i < n; ++i)
        {
            if (!isHole[i])
            {
                shapeForContour[i] = static_cast<int>(shapes.size());
                shapes.push_back({static_cast<int>(i), {}});
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (!isHole[i]) continue;
            const Pt rep = contours[i][0];
            int best = -1;
            double bestArea = 1e300;
            for (size_t j = 0; j < n; ++j)
            {
                if (isHole[j] || depth[j] != depth[i] - 1) continue;
                if (!PointInContour(rep, contours[j])) continue;
                const double a = std::abs(SignedArea(contours[j]));
                if (a < bestArea) { bestArea = a; best = shapeForContour[j]; }
            }
            if (best >= 0) shapes[static_cast<size_t>(best)].holes.push_back(static_cast<int>(i));
        }

        const double z0 = center ? -height * 0.5 : 0.0;
        const double z1 = center ? height * 0.5 : height;

        for (const ShapeBuild& shape : shapes)
        {
            std::vector<Contour> rings;
            Contour outer = contours[static_cast<size_t>(shape.outer)];
            if (SignedArea(outer) < 0.0) std::reverse(outer.begin(), outer.end());
            rings.push_back(std::move(outer));
            for (int h : shape.holes)
            {
                Contour hole = contours[static_cast<size_t>(h)];
                if (SignedArea(hole) > 0.0) std::reverse(hole.begin(), hole.end());
                rings.push_back(std::move(hole));
            }

            std::vector<std::vector<std::array<double, 2>>> poly;
            std::vector<Pt> flat;
            for (const Contour& ring : rings)
            {
                std::vector<std::array<double, 2>> r;
                r.reserve(ring.size());
                for (const Pt& p : ring)
                {
                    r.push_back({p.x, p.y});
                    flat.push_back(p);
                }
                poly.push_back(std::move(r));
            }

            const std::vector<uint32_t> idx = mapbox::earcut<uint32_t>(poly);
            for (size_t t = 0; t + 2 < idx.size(); t += 3)
            {
                const Pt& a = flat[idx[t + 0]];
                const Pt& b = flat[idx[t + 1]];
                const Pt& c = flat[idx[t + 2]];
                out.emplace_back(a.x, a.y, z1);
                out.emplace_back(b.x, b.y, z1);
                out.emplace_back(c.x, c.y, z1);
                out.emplace_back(a.x, a.y, z0);
                out.emplace_back(c.x, c.y, z0);
                out.emplace_back(b.x, b.y, z0);
            }
            for (const Contour& ring : rings)
            {
                AppendWalls(ring, z0, z1, out);
            }
        }
    }

#else // GK_WITH_EARCUT

    bool ScadTess::Available() { return false; }

    void ScadTess::ExtrudeEvenOdd(const std::vector<std::vector<glm::dvec2>>& contours,
                                  double height,
                                  bool center,
                                  TriSoup& out)
    {
        // Fallback: per-contour convex fan extrude.
        for (const std::vector<glm::dvec2>& c : contours)
        {
            ScadGeometry::ExtrudePolygon(c, height, center, out);
        }
    }

#endif // GK_WITH_EARCUT
} // namespace Assets::Scad
