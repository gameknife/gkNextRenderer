#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include "Engine/Runtime/Reflection/PropertyMeta.hpp"

#include <algorithm>
#include <cmath>

namespace Runtime
{
    namespace
    {
        double Area2(const glm::dvec2& a, const glm::dvec2& b, const glm::dvec2& c)
        {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        }

        glm::dvec2 XY(const glm::dvec3& v)
        {
            return {v.x, v.y};
        }
    } // namespace

    bool TerrainComponent::WorldToLocal(float worldX, float worldZ, glm::dvec2& outLocal) const
    {
        if (!HasData())
        {
            return false;
        }
        // Local up maps to world up (see class comment), so the horizontal
        // component of the query is independent of the world Y we pick here.
        const glm::dvec4 local = data_->worldToLocal * glm::dvec4(worldX, 0.0, worldZ, 1.0);
        outLocal = {local.x, local.y};
        return true;
    }

    int TerrainComponent::CellIndexAt(const glm::dvec2& local) const
    {
        const FGridData& d = *data_;
        if (d.cellsX <= 0 || d.cellsY <= 0)
        {
            return -1;
        }
        const double dx = d.sizeX / d.cellsX;
        const double dy = d.sizeY / d.cellsY;
        const int i = std::clamp(static_cast<int>(std::floor((local.x + d.sizeX * 0.5) / dx)), 0, d.cellsX - 1);
        const int j = std::clamp(static_cast<int>(std::floor((local.y + d.sizeY * 0.5) / dy)), 0, d.cellsY - 1);
        return j * d.cellsX + i;
    }

    bool TerrainComponent::LocateTriangle(const glm::dvec2& local, glm::dvec3& a, glm::dvec3& b, glm::dvec3& c) const
    {
        const FGridData& d = *data_;
        const int vx = d.cellsX + 1;
        const double hx = d.sizeX * 0.5;
        const double hy = d.sizeY * 0.5;
        const double eps = 1e-9;
        const glm::dvec2 p(std::clamp(local.x, -hx + eps, hx - eps), std::clamp(local.y, -hy + eps, hy - eps));

        const double dx = d.sizeX / d.cellsX;
        const double dy = d.sizeY / d.cellsY;
        const int ci = std::clamp(static_cast<int>(std::floor((p.x + hx) / dx)), 0, d.cellsX - 1);
        const int cj = std::clamp(static_cast<int>(std::floor((p.y + hy) / dy)), 0, d.cellsY - 1);

        auto cellTris = [&](int i, int j, int tri, glm::dvec3& ta, glm::dvec3& tb, glm::dvec3& tc)
        {
            const glm::dvec3& va = d.verts[static_cast<size_t>(j) * vx + i];
            const glm::dvec3& vb = d.verts[static_cast<size_t>(j) * vx + i + 1];
            const glm::dvec3& vc = d.verts[(static_cast<size_t>(j) + 1) * vx + i + 1];
            const glm::dvec3& vd = d.verts[(static_cast<size_t>(j) + 1) * vx + i];
            if (d.diagFlip[static_cast<size_t>(j) * d.cellsX + i] == 0)
            {
                if (tri == 0) { ta = va; tb = vb; tc = vc; }
                else { ta = va; tb = vc; tc = vd; }
            }
            else
            {
                if (tri == 0) { ta = va; tb = vb; tc = vd; }
                else { ta = vb; tb = vc; tc = vd; }
            }
        };

        auto insideTri = [&](const glm::dvec3& ta, const glm::dvec3& tb, const glm::dvec3& tc)
        {
            const double denom = Area2(XY(ta), XY(tb), XY(tc));
            if (std::abs(denom) < 1e-12) return false;
            const double w0 = Area2(p, XY(tb), XY(tc)) / denom;
            const double w1 = Area2(XY(ta), p, XY(tc)) / denom;
            const double w2 = Area2(XY(ta), XY(tb), p) / denom;
            const double tol = -1e-9;
            return w0 >= tol && w1 >= tol && w2 >= tol;
        };

        // Vertex jitter is < half a cell: the containing triangle is in this
        // cell or one of its 8 neighbours.
        for (int ring = 0; ring <= 1; ++ring)
        {
            for (int oj = -ring; oj <= ring; ++oj)
            {
                for (int oi = -ring; oi <= ring; ++oi)
                {
                    if (ring == 1 && std::abs(oi) != 1 && std::abs(oj) != 1) continue;
                    const int i = ci + oi;
                    const int j = cj + oj;
                    if (i < 0 || j < 0 || i >= d.cellsX || j >= d.cellsY) continue;
                    for (int tri = 0; tri < 2; ++tri)
                    {
                        glm::dvec3 ta, tb, tc;
                        cellTris(i, j, tri, ta, tb, tc);
                        if (insideTri(ta, tb, tc))
                        {
                            a = ta;
                            b = tb;
                            c = tc;
                            return true;
                        }
                    }
                }
            }
        }

        cellTris(ci, cj, 0, a, b, c);
        return true;
    }

    float TerrainComponent::SampleHeight(float worldX, float worldZ) const
    {
        glm::dvec2 local;
        if (!WorldToLocal(worldX, worldZ, local))
        {
            return 0.0f;
        }
        glm::dvec3 a, b, c;
        if (!LocateTriangle(local, a, b, c))
        {
            return 0.0f;
        }
        const double hx = data_->sizeX * 0.5;
        const double hy = data_->sizeY * 0.5;
        const glm::dvec2 p(std::clamp(local.x, -hx + 1e-9, hx - 1e-9), std::clamp(local.y, -hy + 1e-9, hy - 1e-9));
        const double denom = Area2(XY(a), XY(b), XY(c));
        double height = a.z;
        if (std::abs(denom) >= 1e-12)
        {
            const double w0 = Area2(p, XY(b), XY(c)) / denom;
            const double w1 = Area2(XY(a), p, XY(c)) / denom;
            const double w2 = 1.0 - w0 - w1;
            height = w0 * a.z + w1 * b.z + w2 * c.z;
        }
        const glm::dvec4 world = data_->localToWorld * glm::dvec4(p.x, p.y, height, 1.0);
        return static_cast<float>(world.y);
    }

    glm::vec3 TerrainComponent::SampleNormal(float worldX, float worldZ) const
    {
        glm::dvec2 local;
        if (!WorldToLocal(worldX, worldZ, local))
        {
            return {0.0f, 1.0f, 0.0f};
        }
        glm::dvec3 a, b, c;
        if (!LocateTriangle(local, a, b, c))
        {
            return {0.0f, 1.0f, 0.0f};
        }
        glm::dvec3 n = glm::cross(b - a, c - a);
        const double len = glm::length(n);
        if (len < 1e-12)
        {
            return {0.0f, 1.0f, 0.0f};
        }
        n /= len;
        if (n.z < 0.0)
        {
            n = -n;
        }
        // Normals transform with the inverse-transpose; for the supported
        // rigid + uniform-scale transforms the linear part is enough.
        const glm::dvec3 world = glm::dmat3(data_->localToWorld) * n;
        const double worldLen = glm::length(world);
        return worldLen > 1e-12 ? glm::vec3(world / worldLen) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    float TerrainComponent::SampleSlopeDegrees(float worldX, float worldZ) const
    {
        const glm::vec3 n = SampleNormal(worldX, worldZ);
        return glm::degrees(std::acos(std::clamp(n.y, -1.0f, 1.0f)));
    }

    bool TerrainComponent::IsWater(float worldX, float worldZ) const
    {
        glm::dvec2 local;
        if (!WorldToLocal(worldX, worldZ, local))
        {
            return false;
        }
        const int cell = CellIndexAt(local);
        return cell >= 0 && (data_->flags[cell] & kFlagWater) != 0;
    }

    float TerrainComponent::WaterSurface(float worldX, float worldZ) const
    {
        glm::dvec2 local;
        if (!WorldToLocal(worldX, worldZ, local))
        {
            return 0.0f;
        }
        const int cell = CellIndexAt(local);
        if (cell < 0 || (data_->flags[cell] & kFlagWater) == 0)
        {
            return 0.0f;
        }
        const glm::dvec4 world = data_->localToWorld * glm::dvec4(local.x, local.y, data_->waterLevel[cell], 1.0);
        return static_cast<float>(world.y);
    }

    bool TerrainComponent::IsWalkable(float worldX, float worldZ, float maxSlopeDeg) const
    {
        if (!HasData())
        {
            return false;
        }
        if (IsWater(worldX, worldZ))
        {
            return false;
        }
        return SampleSlopeDegrees(worldX, worldZ) <= maxSlopeDeg;
    }

    uint8_t TerrainComponent::BiomeId(float worldX, float worldZ) const
    {
        glm::dvec2 local;
        if (!WorldToLocal(worldX, worldZ, local))
        {
            return 0;
        }
        const int cell = CellIndexAt(local);
        return cell >= 0 ? data_->biome[cell] : 0;
    }

    void TerrainComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<TerrainComponent>()
            .type("TerrainComponent"_hs)
            .data<nullptr, &TerrainComponent::GetCellsX>("CellsX")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Cells X", "Terrain", "Heightfield cells along local X"))
            .data<nullptr, &TerrainComponent::GetCellsY>("CellsY")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Cells Y", "Terrain", "Heightfield cells along local Y"))
            .data<nullptr, &TerrainComponent::GetSizeX>("SizeX")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Size X", "Terrain", "Local domain size along X"))
            .data<nullptr, &TerrainComponent::GetSizeY>("SizeY")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Size Y", "Terrain", "Local domain size along Y"))
            .data<nullptr, &TerrainComponent::GetSeed>("Seed")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Seed", "Terrain", "Deterministic generation seed"));
    }
} // namespace Runtime
