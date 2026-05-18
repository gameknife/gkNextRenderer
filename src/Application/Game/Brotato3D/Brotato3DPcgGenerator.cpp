#include "Brotato3DPcgGenerator.hpp"

#include <chrono>
#include <glm/gtc/constants.hpp>
#include <random>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace
{
    constexpr float kClipEpsilon = 0.0001f;
    constexpr float kMinEdgeLength = 0.01f;
    constexpr float kQuantizeScale = 1000.0f;

    uint64_t SplitMix64(uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ull;
        value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31u);
    }

    uint32_t DeriveSeed(uint32_t rootSeed, uint32_t stageHash)
    {
        return static_cast<uint32_t>(SplitMix64((static_cast<uint64_t>(rootSeed) << 32u) | stageHash));
    }

    float Fade(float t)
    {
        return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    }

    float Lerp(float a, float b, float t)
    {
        return a + (b - a) * t;
    }

    float Grad(int ix, int iy, float x, float y, uint32_t seed)
    {
        const uint64_t hash = SplitMix64(static_cast<uint64_t>(seed) ^
                                         (static_cast<uint64_t>(static_cast<uint32_t>(ix)) << 32u) ^
                                         static_cast<uint32_t>(iy));
        const float angle = static_cast<float>(hash & 0xffffu) * (glm::two_pi<float>() / 65536.0f);
        return std::cos(angle) * x + std::sin(angle) * y;
    }

    float Perlin2D(float x, float y, uint32_t seed)
    {
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = x0 + 1;
        const int y1 = y0 + 1;
        const float sx = x - static_cast<float>(x0);
        const float sy = y - static_cast<float>(y0);

        const float n00 = Grad(x0, y0, sx, sy, seed);
        const float n10 = Grad(x1, y0, sx - 1.0f, sy, seed);
        const float n01 = Grad(x0, y1, sx, sy - 1.0f, seed);
        const float n11 = Grad(x1, y1, sx - 1.0f, sy - 1.0f, seed);
        const float ix0 = Lerp(n00, n10, Fade(sx));
        const float ix1 = Lerp(n01, n11, Fade(sx));
        return glm::clamp(Lerp(ix0, ix1, Fade(sy)) * 1.8f, -1.0f, 1.0f);
    }

    float PolygonSignedArea(const std::vector<glm::vec2>& polygon)
    {
        float twiceArea = 0.0f;
        for (size_t index = 0; index < polygon.size(); ++index)
        {
            const glm::vec2& a = polygon[index];
            const glm::vec2& b = polygon[(index + 1) % polygon.size()];
            twiceArea += a.x * b.y - b.x * a.y;
        }
        return twiceArea * 0.5f;
    }

    glm::vec2 PolygonCentroid(const std::vector<glm::vec2>& polygon)
    {
        const float signedArea = PolygonSignedArea(polygon);
        if (std::abs(signedArea) <= 0.0001f)
        {
            glm::vec2 sum(0.0f);
            for (const glm::vec2& point : polygon)
            {
                sum += point;
            }
            return polygon.empty() ? glm::vec2(0.0f) : sum / static_cast<float>(polygon.size());
        }

        glm::vec2 centroid(0.0f);
        for (size_t index = 0; index < polygon.size(); ++index)
        {
            const glm::vec2& a = polygon[index];
            const glm::vec2& b = polygon[(index + 1) % polygon.size()];
            const float cross = a.x * b.y - b.x * a.y;
            centroid += (a + b) * cross;
        }
        return centroid / (6.0f * signedArea);
    }

    void RemoveDegenerateEdges(std::vector<glm::vec2>& polygon)
    {
        if (polygon.size() < 2)
        {
            return;
        }

        std::vector<glm::vec2> cleaned;
        cleaned.reserve(polygon.size());
        for (const glm::vec2& point : polygon)
        {
            if (cleaned.empty() || glm::length(point - cleaned.back()) >= kMinEdgeLength)
            {
                cleaned.push_back(point);
            }
        }
        if (cleaned.size() > 1 && glm::length(cleaned.front() - cleaned.back()) < kMinEdgeLength)
        {
            cleaned.pop_back();
        }
        polygon = std::move(cleaned);
    }

    std::vector<glm::vec2> ClipToCloserHalfPlane(const std::vector<glm::vec2>& polygon,
                                                 const glm::vec2& site,
                                                 const glm::vec2& other)
    {
        if (polygon.empty())
        {
            return {};
        }

        const glm::vec2 normal = other - site;
        const float limit = (glm::dot(other, other) - glm::dot(site, site)) * 0.5f;
        const auto distance = [&normal, limit](const glm::vec2& point)
        {
            return glm::dot(normal, point) - limit;
        };
        const auto inside = [&distance](const glm::vec2& point)
        {
            return distance(point) <= kClipEpsilon;
        };

        std::vector<glm::vec2> clipped;
        clipped.reserve(polygon.size() + 1);
        glm::vec2 previous = polygon.back();
        bool previousInside = inside(previous);
        for (const glm::vec2& current : polygon)
        {
            const bool currentInside = inside(current);
            if (currentInside != previousInside)
            {
                const float prevDistance = distance(previous);
                const float currentDistance = distance(current);
                const float denom = prevDistance - currentDistance;
                if (std::abs(denom) > 0.000001f)
                {
                    const float t = glm::clamp(prevDistance / denom, 0.0f, 1.0f);
                    clipped.push_back(previous + (current - previous) * t);
                }
            }
            if (currentInside)
            {
                clipped.push_back(current);
            }
            previous = current;
            previousInside = currentInside;
        }

        RemoveDegenerateEdges(clipped);
        return clipped;
    }

    std::vector<Brotato3D::Pcg::FVoronoiCell> ComputeVoronoiCells(const std::vector<Brotato3D::Pcg::FVoronoiSite>& sites,
                                                                  const glm::vec2& halfExtent)
    {
        std::vector<Brotato3D::Pcg::FVoronoiCell> cells;
        cells.reserve(sites.size());
        const std::vector<glm::vec2> bounds{
            glm::vec2(-halfExtent.x, -halfExtent.y),
            glm::vec2(halfExtent.x, -halfExtent.y),
            glm::vec2(halfExtent.x, halfExtent.y),
            glm::vec2(-halfExtent.x, halfExtent.y),
        };

        for (size_t siteIndex = 0; siteIndex < sites.size(); ++siteIndex)
        {
            std::vector<glm::vec2> polygon = bounds;
            for (size_t otherIndex = 0; otherIndex < sites.size() && polygon.size() >= 3; ++otherIndex)
            {
                if (otherIndex == siteIndex)
                {
                    continue;
                }
                polygon = ClipToCloserHalfPlane(polygon, sites[siteIndex].positionXZ, sites[otherIndex].positionXZ);
            }

            RemoveDegenerateEdges(polygon);
            if (polygon.size() < 3 || std::abs(PolygonSignedArea(polygon)) < 0.01f)
            {
                continue;
            }
            if (PolygonSignedArea(polygon) < 0.0f)
            {
                std::reverse(polygon.begin(), polygon.end());
            }

            Brotato3D::Pcg::FVoronoiCell cell{};
            cell.siteIndex = static_cast<int>(siteIndex);
            cell.polygonXZ = std::move(polygon);
            cell.centroidXZ = PolygonCentroid(cell.polygonXZ);
            cells.push_back(std::move(cell));
        }
        return cells;
    }

    int PaletteCount(const Brotato3D::Pcg::FArenaPcgConfig& config)
    {
        return std::max(1, std::min(16, static_cast<int>(config.palette.size())));
    }

    std::vector<glm::vec2> SamplePoissonDisc(const glm::vec2& halfExtent,
                                             float edgeKeepout,
                                             float radius,
                                             int maxAttempts,
                                             float spawnSafeRadius,
                                             std::mt19937& rng)
    {
        const glm::vec2 minBounds = -halfExtent + glm::vec2(edgeKeepout);
        const glm::vec2 maxBounds = halfExtent - glm::vec2(edgeKeepout);
        if (minBounds.x >= maxBounds.x || minBounds.y >= maxBounds.y)
        {
            return {};
        }

        const float cellSize = radius / std::sqrt(2.0f);
        const int gridWidth = std::max(1, static_cast<int>(std::ceil((maxBounds.x - minBounds.x) / cellSize)));
        const int gridHeight = std::max(1, static_cast<int>(std::ceil((maxBounds.y - minBounds.y) / cellSize)));
        std::vector<int> grid(static_cast<size_t>(gridWidth * gridHeight), -1);
        std::vector<glm::vec2> samples;
        std::vector<glm::vec2> active;

        auto gridCoord = [&](const glm::vec2& point)
        {
            return glm::ivec2(std::clamp(static_cast<int>((point.x - minBounds.x) / cellSize), 0, gridWidth - 1),
                              std::clamp(static_cast<int>((point.y - minBounds.y) / cellSize), 0, gridHeight - 1));
        };
        auto validPoint = [&](const glm::vec2& point)
        {
            if (point.x < minBounds.x || point.x > maxBounds.x || point.y < minBounds.y || point.y > maxBounds.y)
            {
                return false;
            }
            if (glm::length(point) < spawnSafeRadius)
            {
                return false;
            }

            const glm::ivec2 coord = gridCoord(point);
            for (int y = std::max(0, coord.y - 2); y <= std::min(gridHeight - 1, coord.y + 2); ++y)
            {
                for (int x = std::max(0, coord.x - 2); x <= std::min(gridWidth - 1, coord.x + 2); ++x)
                {
                    const int sampleIndex = grid[static_cast<size_t>(y * gridWidth + x)];
                    if (sampleIndex >= 0 && glm::length(samples[static_cast<size_t>(sampleIndex)] - point) < radius)
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        auto addSample = [&](const glm::vec2& point)
        {
            const glm::ivec2 coord = gridCoord(point);
            grid[static_cast<size_t>(coord.y * gridWidth + coord.x)] = static_cast<int>(samples.size());
            samples.push_back(point);
            active.push_back(point);
        };

        std::uniform_real_distribution<float> xDist(minBounds.x, maxBounds.x);
        std::uniform_real_distribution<float> zDist(minBounds.y, maxBounds.y);
        for (int attempt = 0; attempt < maxAttempts * 4; ++attempt)
        {
            const glm::vec2 first(xDist(rng), zDist(rng));
            if (validPoint(first))
            {
                addSample(first);
                break;
            }
        }
        if (samples.empty())
        {
            return {};
        }

        std::uniform_real_distribution<float> angleDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> radiusDist(radius, radius * 2.0f);
        while (!active.empty())
        {
            std::uniform_int_distribution<size_t> activeDist(0, active.size() - 1);
            const size_t activeIndex = activeDist(rng);
            const glm::vec2 base = active[activeIndex];
            bool found = false;
            for (int attempt = 0; attempt < maxAttempts; ++attempt)
            {
                const float angle = angleDist(rng);
                const float distance = radiusDist(rng);
                const glm::vec2 candidate = base + glm::vec2(std::cos(angle), std::sin(angle)) * distance;
                if (validPoint(candidate))
                {
                    addSample(candidate);
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                active[activeIndex] = active.back();
                active.pop_back();
            }
        }

        return samples;
    }

    std::string PickWeightedPropId(const std::vector<Brotato3D::Pcg::FPropDef>& props, std::mt19937& rng)
    {
        float totalWeight = 0.0f;
        for (const Brotato3D::Pcg::FPropDef& prop : props)
        {
            totalWeight += std::max(0.0f, prop.weight);
        }
        if (totalWeight <= 0.0f)
        {
            return {};
        }

        std::uniform_real_distribution<float> pickDist(0.0f, totalWeight);
        float pick = pickDist(rng);
        for (const Brotato3D::Pcg::FPropDef& prop : props)
        {
            pick -= std::max(0.0f, prop.weight);
            if (pick <= 0.0f)
            {
                return prop.id;
            }
        }
        return props.back().id;
    }

    void BuildPropPlacements(const Brotato3D::Pcg::FArenaPcgConfig& config, Brotato3D::Pcg::FMapGraph& graph)
    {
        if (config.props.empty())
        {
            return;
        }

        std::mt19937 rng(DeriveSeed(graph.rootSeed, 0x50524f50u));
        const std::vector<glm::vec2> samples = SamplePoissonDisc(graph.arenaHalfExtent,
                                                                 config.edgeKeepout,
                                                                 config.propPoissonRadius,
                                                                 config.propPoissonMaxAttempts,
                                                                 config.spawnSafeRadius,
                                                                 rng);
        std::vector<glm::vec2> clusterCenters;
        const int clusterCount = std::clamp(static_cast<int>(std::round(static_cast<float>(samples.size()) / 24.0f)), 3, 6);
        std::uniform_real_distribution<float> xDist(-graph.arenaHalfExtent.x + config.edgeKeepout,
                                                    graph.arenaHalfExtent.x - config.edgeKeepout);
        std::uniform_real_distribution<float> zDist(-graph.arenaHalfExtent.y + config.edgeKeepout,
                                                    graph.arenaHalfExtent.y - config.edgeKeepout);
        clusterCenters.reserve(static_cast<size_t>(clusterCount));
        for (int index = 0; index < clusterCount; ++index)
        {
            for (int attempt = 0; attempt < 32; ++attempt)
            {
                const glm::vec2 center(xDist(rng), zDist(rng));
                if (glm::length(center) >= config.spawnSafeRadius + config.propPoissonRadius)
                {
                    clusterCenters.push_back(center);
                    break;
                }
            }
        }

        std::uniform_real_distribution<float> rotationDist(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> unitDist(0.0f, 1.0f);
        const float clusterRadius = std::max(config.propPoissonRadius * 2.6f,
                                             std::min(graph.arenaHalfExtent.x, graph.arenaHalfExtent.y) * 0.28f);
        graph.props.reserve(samples.size());
        for (const glm::vec2& sample : samples)
        {
            float nearestCluster = FLT_MAX;
            for (const glm::vec2& center : clusterCenters)
            {
                nearestCluster = std::min(nearestCluster, glm::length(sample - center));
            }
            const float density = clusterCenters.empty() ? 0.35f :
                                  std::clamp(1.0f - nearestCluster / clusterRadius, 0.0f, 1.0f);
            const float acceptProbability = 0.04f + density * density * 0.78f;
            if (unitDist(rng) > acceptProbability)
            {
                continue;
            }

            Brotato3D::Pcg::FPropPlacement placement{};
            placement.id = PickWeightedPropId(config.props, rng);
            if (placement.id.empty())
            {
                continue;
            }
            placement.positionXZ = sample;
            placement.rotationYRadians = rotationDist(rng);
            graph.props.push_back(placement);
        }
    }

    void BuildBorderSegments(const Brotato3D::Pcg::FArenaPcgConfig& config, Brotato3D::Pcg::FMapGraph& graph)
    {
        const int segmentCount = std::clamp(config.borderSegments, 4, 256);
        const int perSide = std::max(1, segmentCount / 4);
        const float height = std::max(0.1f, config.borderHeight);
        const float jitter = std::max(0.0f, config.borderHeightJitter);
        const float thickness = 0.2f;
        const glm::vec3 baseColor(0.45f, 0.55f, 0.35f);

        graph.borderSegments.clear();
        graph.borderSegments.reserve(static_cast<size_t>(perSide * 4));
        for (int side = 0; side < 4; ++side)
        {
            for (int index = 0; index < perSide; ++index)
            {
                const float t0 = static_cast<float>(index) / static_cast<float>(perSide);
                const float t1 = static_cast<float>(index + 1) / static_cast<float>(perSide);
                Brotato3D::Pcg::FBorderSegment segment{};
                if (side == 0)
                {
                    segment.baseStartXZ = glm::vec2(glm::mix(-graph.arenaHalfExtent.x, graph.arenaHalfExtent.x, t0), -graph.arenaHalfExtent.y);
                    segment.baseEndXZ = glm::vec2(glm::mix(-graph.arenaHalfExtent.x, graph.arenaHalfExtent.x, t1), -graph.arenaHalfExtent.y);
                }
                else if (side == 1)
                {
                    segment.baseStartXZ = glm::vec2(glm::mix(graph.arenaHalfExtent.x, -graph.arenaHalfExtent.x, t0), graph.arenaHalfExtent.y);
                    segment.baseEndXZ = glm::vec2(glm::mix(graph.arenaHalfExtent.x, -graph.arenaHalfExtent.x, t1), graph.arenaHalfExtent.y);
                }
                else if (side == 2)
                {
                    segment.baseStartXZ = glm::vec2(-graph.arenaHalfExtent.x, glm::mix(graph.arenaHalfExtent.y, -graph.arenaHalfExtent.y, t0));
                    segment.baseEndXZ = glm::vec2(-graph.arenaHalfExtent.x, glm::mix(graph.arenaHalfExtent.y, -graph.arenaHalfExtent.y, t1));
                }
                else
                {
                    segment.baseStartXZ = glm::vec2(graph.arenaHalfExtent.x, glm::mix(-graph.arenaHalfExtent.y, graph.arenaHalfExtent.y, t0));
                    segment.baseEndXZ = glm::vec2(graph.arenaHalfExtent.x, glm::mix(-graph.arenaHalfExtent.y, graph.arenaHalfExtent.y, t1));
                }

                const uint32_t segmentSeed = DeriveSeed(graph.rootSeed, 0x57414c4cu ^ static_cast<uint32_t>(side * 257 + index));
                const float signedJitter = (static_cast<float>(segmentSeed & 0xffffu) / 32767.5f - 1.0f) * jitter;
                segment.topY = std::max(0.2f, height + signedJitter);
                segment.thickness = thickness;
                const float colorJitter = static_cast<float>((segmentSeed >> 16u) & 0xffu) / 255.0f * 0.12f;
                segment.color = glm::clamp(baseColor + glm::vec3(colorJitter), glm::vec3(0.0f), glm::vec3(1.0f));
                graph.borderSegments.push_back(segment);
            }
        }
    }

    int QuantizedCoord(float value)
    {
        return static_cast<int>(std::round(value * kQuantizeScale));
    }

    uint64_t QuantizedKey(const glm::vec2& point)
    {
        const uint32_t x = static_cast<uint32_t>(QuantizedCoord(point.x));
        const uint32_t z = static_cast<uint32_t>(QuantizedCoord(point.y));
        return (static_cast<uint64_t>(x) << 32u) | static_cast<uint64_t>(z);
    }
}

namespace Brotato3D::Pcg
{
    float SampleVisualHeight(uint32_t rootSeed,
                             float vertexJitterAmplitude,
                             float vertexJitterFrequency,
                             const glm::vec2& xz)
    {
        const float amplitude = std::clamp(vertexJitterAmplitude, 0.0f, 0.60f);
        const float frequency = std::max(0.001f, vertexJitterFrequency);
        const uint32_t heightSeed = DeriveSeed(rootSeed, 0x48474954u);
        const float base = Perlin2D(xz.x * frequency, xz.y * frequency, heightSeed);
        const float detail = Perlin2D(xz.x * frequency * 2.0f + 17.0f, xz.y * frequency * 2.0f - 11.0f, heightSeed ^ 0x6a09e667u);
        return std::clamp((base + detail * 0.4f) * amplitude, -0.60f, 0.60f);
    }

    bool BuildMapGraph(const FArenaPcgConfig& config,
                       const glm::vec2& halfExtent,
                       uint32_t sessionSeed,
                       FMapGraph& outGraph)
    {
        const auto startTime = std::chrono::steady_clock::now();
        outGraph = {};
        outGraph.arenaHalfExtent = glm::max(halfExtent, glm::vec2(1.0f));
        outGraph.rootSeed = config.seedOverride != 0 ? config.seedOverride : sessionSeed;

        const int targetCells = std::clamp(config.targetCells, 1, 512);
        const int relaxationIterations = std::clamp(config.lloydRelaxIterations, 0, 4);
        std::mt19937 rng(DeriveSeed(outGraph.rootSeed, 0x51534954u));
        std::uniform_real_distribution<float> xDist(-outGraph.arenaHalfExtent.x, outGraph.arenaHalfExtent.x);
        std::uniform_real_distribution<float> zDist(-outGraph.arenaHalfExtent.y, outGraph.arenaHalfExtent.y);

        outGraph.sites.reserve(static_cast<size_t>(targetCells));
        for (int index = 0; index < targetCells; ++index)
        {
            FVoronoiSite site{};
            site.positionXZ = glm::vec2(xDist(rng), zDist(rng));
            site.paletteIndex = static_cast<int>(DeriveSeed(outGraph.rootSeed, static_cast<uint32_t>(index) ^ 0x50414c54u) %
                                                static_cast<uint32_t>(PaletteCount(config)));
            outGraph.sites.push_back(site);
        }

        for (int iteration = 0; iteration < relaxationIterations; ++iteration)
        {
            const std::vector<FVoronoiCell> cells = ComputeVoronoiCells(outGraph.sites, outGraph.arenaHalfExtent);
            for (const FVoronoiCell& cell : cells)
            {
                if (cell.siteIndex >= 0 && cell.siteIndex < static_cast<int>(outGraph.sites.size()))
                {
                    outGraph.sites[static_cast<size_t>(cell.siteIndex)].positionXZ =
                        glm::clamp(cell.centroidXZ, -outGraph.arenaHalfExtent, outGraph.arenaHalfExtent);
                }
            }
        }

        outGraph.cells = ComputeVoronoiCells(outGraph.sites, outGraph.arenaHalfExtent);
        BuildPropPlacements(config, outGraph);
        BuildBorderSegments(config, outGraph);
        const auto endTime = std::chrono::steady_clock::now();
        const double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        if (elapsedMs > 50.0)
        {
            spdlog::warn("[Brotato3DPcg] BuildMapGraph took {:.2f}ms seed=0x{:08x} cells={}",
                         elapsedMs,
                         outGraph.rootSeed,
                         outGraph.cells.size());
        }
        return !outGraph.cells.empty();
    }

    FTerrainMeshBuffers BuildTerrainMesh(const FArenaPcgConfig& config, FMapGraph& graph)
    {
        FTerrainMeshBuffers mesh{};
        graph.vertexBuffer.clear();
        graph.indexBuffer.clear();
        graph.sectionMaterialOffsets.clear();

        const float amplitude = std::clamp(config.vertexJitterAmplitude, 0.0f, 0.60f);
        const float frequency = std::max(0.001f, config.vertexJitterFrequency);
        const uint32_t heightSeed = DeriveSeed(graph.rootSeed, 0x48474954u);
        std::unordered_map<uint64_t, float> heightCache;
        heightCache.reserve(graph.cells.size() * 6);

        auto sampleVisualY = [&](const glm::vec2& xz) -> float
        {
            const uint64_t key = QuantizedKey(xz);
            if (const auto it = heightCache.find(key); it != heightCache.end())
            {
                return it->second;
            }

            const float y = SampleVisualHeight(graph.rootSeed, amplitude, frequency, xz);
            heightCache.emplace(key, y);
            graph.vertexBuffer.push_back(FCellVertexDisplacement{.xz = xz, .yVisual = y});
            return y;
        };

        auto appendTriangle = [&](const glm::vec3& a,
                                  const glm::vec3& b,
                                  const glm::vec3& c,
                                  uint32_t materialIndex,
                                  const glm::vec2& uvA,
                                  const glm::vec2& uvB,
                                  const glm::vec2& uvC)
        {
            glm::vec3 normal = glm::cross(b - a, c - a);
            if (glm::dot(normal, normal) <= 0.000001f)
            {
                return;
            }
            normal = glm::normalize(normal);

            glm::vec3 bFinal = b;
            glm::vec3 cFinal = c;
            glm::vec2 uvBFinal = uvB;
            glm::vec2 uvCFinal = uvC;
            if (normal.y < 0.0f)
            {
                std::swap(bFinal, cFinal);
                std::swap(uvBFinal, uvCFinal);
                normal = -normal;
            }

            const uint32_t first = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(Assets::Vertex{a, normal, glm::vec4(1, 0, 0, 0), uvA, materialIndex});
            mesh.vertices.push_back(Assets::Vertex{bFinal, normal, glm::vec4(1, 0, 0, 0), uvBFinal, materialIndex});
            mesh.vertices.push_back(Assets::Vertex{cFinal, normal, glm::vec4(1, 0, 0, 0), uvCFinal, materialIndex});
            mesh.indices.push_back(first);
            mesh.indices.push_back(first + 1);
            mesh.indices.push_back(first + 2);
            graph.indexBuffer.push_back(first);
            graph.indexBuffer.push_back(first + 1);
            graph.indexBuffer.push_back(first + 2);
        };

        const glm::vec2 size = glm::max(graph.arenaHalfExtent * 2.0f, glm::vec2(0.001f));
        const auto makeUv = [&graph, &size](const glm::vec2& xz)
        {
            return (xz + graph.arenaHalfExtent) / size;
        };

        for (const FVoronoiCell& cell : graph.cells)
        {
            if (cell.polygonXZ.size() < 3 || cell.siteIndex < 0 ||
                cell.siteIndex >= static_cast<int>(graph.sites.size()))
            {
                continue;
            }

            float vertexYSum = 0.0f;
            for (const glm::vec2& point : cell.polygonXZ)
            {
                vertexYSum += sampleVisualY(point);
            }
            const float averageY = vertexYSum / static_cast<float>(cell.polygonXZ.size());
            const float centerNoise = Perlin2D(cell.centroidXZ.x * frequency * 1.35f + 3.0f,
                                               cell.centroidXZ.y * frequency * 1.35f - 7.0f,
                                               heightSeed ^ 0xbb67ae85u);
            const float centerY = std::clamp(averageY + centerNoise * amplitude * 0.25f, -0.60f, 0.60f);
            const glm::vec3 center(cell.centroidXZ.x, centerY, cell.centroidXZ.y);
            const uint32_t materialIndex = static_cast<uint32_t>(std::clamp(graph.sites[static_cast<size_t>(cell.siteIndex)].paletteIndex, 0, 15));

            for (size_t edgeIndex = 0; edgeIndex < cell.polygonXZ.size(); ++edgeIndex)
            {
                const glm::vec2& aXZ = cell.polygonXZ[edgeIndex];
                const glm::vec2& bXZ = cell.polygonXZ[(edgeIndex + 1) % cell.polygonXZ.size()];
                const glm::vec3 a(aXZ.x, sampleVisualY(aXZ), aXZ.y);
                const glm::vec3 b(bXZ.x, sampleVisualY(bXZ), bXZ.y);
                appendTriangle(a, center, b, materialIndex, makeUv(aXZ), makeUv(cell.centroidXZ), makeUv(bXZ));
            }
        }

        return mesh;
    }
}
