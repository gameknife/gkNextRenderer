#include "Modules/ScadLoader/FScadCsg.h"

#ifndef GK_WITH_MANIFOLD
#define GK_WITH_MANIFOLD 0
#endif

#if GK_WITH_MANIFOLD
#include <cmath>
#include <cstdint>
#include <unordered_map>

#include <manifold/manifold.h>
#endif

namespace Assets::Scad
{
#if GK_WITH_MANIFOLD
    namespace
    {
        struct VKey
        {
            int64_t x, y, z;
            bool operator==(const VKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };

        VKey QuantizedKey(const glm::dvec3& p)
        {
            constexpr double weldEpsilon = 1e-9;
            return VKey{
                static_cast<int64_t>(std::llround(p.x / weldEpsilon)),
                static_cast<int64_t>(std::llround(p.y / weldEpsilon)),
                static_cast<int64_t>(std::llround(p.z / weldEpsilon))};
        }

        struct VKeyHash
        {
            size_t operator()(const VKey& k) const
            {
                size_t h = 1469598103934665603ull;
                const int64_t values[3] = {k.x, k.y, k.z};
                for (int64_t v : values)
                {
                    h ^= static_cast<size_t>(v);
                    h *= 1099511628211ull;
                }
                return h;
            }
        };

        // Welds an unindexed triangle soup into a Manifold. Quantized keys avoid
        // leaving tiny seam gaps from repeated trig evaluation at 0 / 2pi.
        manifold::Manifold ToManifold(const TriSoup& soup, bool& ok)
        {
            ok = false;
            if (soup.size() < 3)
            {
                return manifold::Manifold();
            }

            manifold::MeshGL mesh;
            mesh.numProp = 3;
            std::unordered_map<VKey, uint32_t, VKeyHash> lookup;
            lookup.reserve(soup.size());

            mesh.triVerts.reserve(soup.size());
            for (const glm::dvec3& p : soup)
            {
                const VKey key = QuantizedKey(p);
                auto found = lookup.find(key);
                uint32_t index;
                if (found != lookup.end())
                {
                    index = found->second;
                }
                else
                {
                    index = static_cast<uint32_t>(mesh.vertProperties.size() / 3);
                    mesh.vertProperties.push_back(static_cast<float>(p.x));
                    mesh.vertProperties.push_back(static_cast<float>(p.y));
                    mesh.vertProperties.push_back(static_cast<float>(p.z));
                    lookup.emplace(key, index);
                }
                mesh.triVerts.push_back(index);
            }

            manifold::Manifold m(mesh);
            ok = (m.Status() == manifold::Manifold::Error::NoError) && m.NumTri() > 0;
            return m;
        }

        void AppendMesh(const manifold::Manifold& m, TriSoup& out)
        {
            const manifold::MeshGL mesh = m.GetMeshGL();
            const uint32_t numProp = mesh.numProp;
            auto vert = [&](uint32_t idx)
            {
                const size_t base = static_cast<size_t>(idx) * numProp;
                return glm::dvec3(mesh.vertProperties[base + 0],
                                  mesh.vertProperties[base + 1],
                                  mesh.vertProperties[base + 2]);
            };
            out.reserve(out.size() + mesh.triVerts.size());
            for (size_t i = 0; i + 2 < mesh.triVerts.size(); i += 3)
            {
                out.push_back(vert(mesh.triVerts[i + 0]));
                out.push_back(vert(mesh.triVerts[i + 1]));
                out.push_back(vert(mesh.triVerts[i + 2]));
            }
        }
    } // namespace

    bool ScadCsg::BackendAvailable() { return true; }

    TriSoup ScadCsg::Union(const std::vector<TriSoup>& parts, bool& outOk)
    {
        std::vector<manifold::Manifold> solids;
        solids.reserve(parts.size());
        for (const TriSoup& part : parts)
        {
            bool ok = false;
            manifold::Manifold m = ToManifold(part, ok);
            if (ok) solids.push_back(std::move(m));
        }
        if (solids.empty())
        {
            outOk = false;
            TriSoup out;
            for (const TriSoup& part : parts) out.insert(out.end(), part.begin(), part.end());
            return out;
        }
        manifold::Manifold acc = manifold::Manifold::BatchBoolean(solids, manifold::OpType::Add);
        outOk = true;
        TriSoup out;
        AppendMesh(acc, out);
        return out;
    }

    TriSoup ScadCsg::Difference(const TriSoup& positive, const std::vector<TriSoup>& negatives, bool& outOk)
    {
        bool posOk = false;
        manifold::Manifold acc = ToManifold(positive, posOk);
        if (!posOk)
        {
            outOk = false;
            return positive; // cannot build a solid; leave the positive untouched
        }
        for (const TriSoup& neg : negatives)
        {
            bool negOk = false;
            manifold::Manifold n = ToManifold(neg, negOk);
            if (negOk)
            {
                acc = acc - n;
            }
        }
        outOk = true;
        TriSoup out;
        AppendMesh(acc, out);
        return out;
    }

    TriSoup ScadCsg::Intersection(const std::vector<TriSoup>& operands, bool& outOk)
    {
        if (operands.empty())
        {
            outOk = false;
            return {};
        }
        bool firstOk = false;
        manifold::Manifold acc = ToManifold(operands[0], firstOk);
        if (!firstOk)
        {
            outOk = false;
            return operands[0];
        }
        for (size_t i = 1; i < operands.size(); ++i)
        {
            bool ok = false;
            manifold::Manifold m = ToManifold(operands[i], ok);
            if (ok)
            {
                acc = acc ^ m;
            }
        }
        outOk = true;
        TriSoup out;
        AppendMesh(acc, out);
        return out;
    }

    TriSoup ScadCsg::Hull(const std::vector<TriSoup>& parts, bool& outOk)
    {
        std::vector<manifold::vec3> pts;
        for (const TriSoup& part : parts)
        {
            for (const glm::dvec3& p : part)
            {
                pts.emplace_back(p.x, p.y, p.z);
            }
        }
        if (pts.size() < 4)
        {
            outOk = false;
            TriSoup out;
            for (const TriSoup& part : parts)
            {
                out.insert(out.end(), part.begin(), part.end());
            }
            return out;
        }
        manifold::Manifold m = manifold::Manifold::Hull(pts);
        outOk = m.NumTri() > 0;
        TriSoup out;
        AppendMesh(m, out);
        return out;
    }

#else // GK_WITH_MANIFOLD

    bool ScadCsg::BackendAvailable() { return false; }

    TriSoup ScadCsg::Union(const std::vector<TriSoup>& parts, bool& outOk)
    {
        outOk = false;
        TriSoup out;
        for (const TriSoup& part : parts) out.insert(out.end(), part.begin(), part.end());
        return out;
    }

    TriSoup ScadCsg::Difference(const TriSoup& positive, const std::vector<TriSoup>& /*negatives*/, bool& outOk)
    {
        outOk = false;
        return positive;
    }

    TriSoup ScadCsg::Intersection(const std::vector<TriSoup>& operands, bool& outOk)
    {
        outOk = false;
        return operands.empty() ? TriSoup{} : operands[0];
    }

    TriSoup ScadCsg::Hull(const std::vector<TriSoup>& parts, bool& outOk)
    {
        outOk = false;
        TriSoup out;
        for (const TriSoup& part : parts)
        {
            out.insert(out.end(), part.begin(), part.end());
        }
        return out;
    }

#endif // GK_WITH_MANIFOLD
} // namespace Assets::Scad
