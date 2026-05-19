#include "Brotato3DArena.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Brotato3DDataLoader.hpp"
#include "Brotato3DPcgGenerator.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"

#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>
#include <spdlog/spdlog.h>

namespace
{
    constexpr float GroundBottomY = -0.05f;
    constexpr float GroundTopY = 0.0f;
    constexpr float ProceduralGroundCellSize = 4.0f;
    constexpr int ProceduralGroundGridMultiplier = 4;
    constexpr int MinGroundGridCols = 8;
    constexpr int MinGroundGridRows = 6;
    constexpr float VisualWallHeight = 4.0f;
    constexpr float VisualWallThickness = 0.2f;

    enum class EGroundBoundaryPattern : uint8_t
    {
        None,
        CornerNorthEast,
        CornerSouthEast,
        CornerSouthWest,
        CornerNorthWest,
    };

    struct FGroundGridSpec
    {
        int columns = 0;
        int rows = 0;
        float cellWidth = 0.0f;
        float cellHeight = 0.0f;
    };

    struct FGroundBoundaryDecision
    {
        EGroundBoundaryPattern pattern = EGroundBoundaryPattern::None;
        int blendMaterialIndex = -1;
    };

    struct FFloodShapeProfile
    {
        int seedX = 0;
        int seedY = 0;
        glm::ivec2 primaryDir{1, 0};
        glm::ivec2 secondaryDir{0, 1};
        glm::ivec2 lobeATarget{0, 0};
        glm::ivec2 lobeBTarget{0, 0};
        int lobeRadius = 3;
        int macroCellSize = 3;
        int microCellSize = 2;
        int variationWeight = 48;
        int selectionSlack = 0;
    };

    const Brotato3D::FArenaDef* FindArena(const std::vector<Brotato3D::FArenaDef>& arenaDefs, const std::string& arenaId)
    {
        const auto it = std::find_if(arenaDefs.begin(), arenaDefs.end(), [&arenaId](const Brotato3D::FArenaDef& arena)
        {
            return arena.id == arenaId;
        });
        return it == arenaDefs.end() ? nullptr : &(*it);
    }

    const char* ToGroundKindName(Brotato3D::EGroundMaterialKind kind)
    {
        switch (kind)
        {
        case Brotato3D::EGroundMaterialKind::Metallic:
            return "metallic";
        case Brotato3D::EGroundMaterialKind::Mixture:
            return "mixture";
        case Brotato3D::EGroundMaterialKind::Lambertian:
        default:
            return "lambertian";
        }
    }

    uint32_t AddGroundMaterial(std::vector<Assets::FMaterial>& materials,
                               const Brotato3D::FGroundTileDef& tile,
                               size_t tileIndex)
    {
        const glm::vec3 color = glm::clamp(tile.color, glm::vec3(0.0f), glm::vec3(1.0f));
        const float fuzziness = std::clamp(tile.fuzziness, 0.0f, 1.0f);
        Assets::Material material = Assets::Material::Lambertian(color);
        switch (tile.kind)
        {
        case Brotato3D::EGroundMaterialKind::Metallic:
            material = Assets::Material::Metallic(color, fuzziness);
            break;
        case Brotato3D::EGroundMaterialKind::Mixture:
            material = Assets::Material::Mixture(color, fuzziness);
            break;
        case Brotato3D::EGroundMaterialKind::Lambertian:
        default:
            break;
        }

        materials.push_back({material, fmt::format("Brotato3D_GroundTileMaterial_{}_{}", tileIndex, ToGroundKindName(tile.kind))});
        return static_cast<uint32_t>(materials.size() - 1);
    }

    std::vector<glm::vec2> BuildGroundPolygon(const Brotato3D::FGroundTileDef& tile, const glm::vec2& halfExtent)
    {
        std::vector<glm::vec2> polygon;
        if (!tile.pointsXZ.empty())
        {
            polygon.reserve(tile.pointsXZ.size());
            for (const glm::vec2& point : tile.pointsXZ)
            {
                polygon.push_back(glm::clamp(point, -halfExtent, halfExtent));
            }
            return polygon;
        }

        const glm::vec2 tileMin = glm::clamp(glm::min(tile.minXZ, tile.maxXZ), -halfExtent, halfExtent);
        const glm::vec2 tileMax = glm::clamp(glm::max(tile.minXZ, tile.maxXZ), -halfExtent, halfExtent);
        if (tileMax.x - tileMin.x <= 0.001f || tileMax.y - tileMin.y <= 0.001f)
        {
            return polygon;
        }

        polygon.push_back(glm::vec2(tileMin.x, tileMin.y));
        polygon.push_back(glm::vec2(tileMax.x, tileMin.y));
        polygon.push_back(glm::vec2(tileMax.x, tileMax.y));
        polygon.push_back(glm::vec2(tileMin.x, tileMax.y));
        return polygon;
    }

    bool HasExplicitGroundGeometry(const Brotato3D::FGroundTileDef& tile)
    {
        return !tile.pointsXZ.empty() ||
               glm::length(tile.maxXZ - tile.minXZ) > 0.001f;
    }

    bool UseProceduralGroundFlood(const Brotato3D::FArenaDef& arena)
    {
        if (arena.groundTiles.empty())
        {
            return false;
        }

        return std::none_of(arena.groundTiles.begin(), arena.groundTiles.end(), [](const Brotato3D::FGroundTileDef& tile)
        {
            return HasExplicitGroundGeometry(tile);
        });
    }

    uint32_t HashArenaSeed(const std::string& value)
    {
        uint32_t hash = 2166136261u;
        for (const unsigned char ch : value)
        {
            hash ^= ch;
            hash *= 16777619u;
        }
        return hash;
    }

    // 进程启动时抽一次随机数，混进 arena 的洪水填充种子，让每次启动游戏地形都不同。
    // 同一进程内保持稳定（character select 反复切 arena 看到的预览一致，scene reload
    // 也不会重抽）；下次启动 process 才换布局。
    uint32_t GetArenaSessionSeed()
    {
        static const uint32_t kSessionSeed = []()
        {
            const uint32_t seed = std::random_device{}();
            spdlog::info("[Brotato3D] arena session seed = 0x{:08x}", seed);
            return seed;
        }();
        return kSessionSeed;
    }

    uint32_t HashCell(uint32_t seed, int x, int y)
    {
        uint32_t hash = seed ^ 0x9e3779b9u;
        hash ^= static_cast<uint32_t>(x) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<uint32_t>(y) + 0x9e3779b9u + (hash << 6u) + (hash >> 2u);
        return hash;
    }

    FGroundGridSpec BuildGroundGridSpec(const glm::vec2& halfExtent)
    {
        const float width = halfExtent.x * 2.0f;
        const float depth = halfExtent.y * 2.0f;
        FGroundGridSpec spec{};
        spec.columns =
            std::max(MinGroundGridCols, static_cast<int>(std::round(width / ProceduralGroundCellSize))) * ProceduralGroundGridMultiplier;
        spec.rows =
            std::max(MinGroundGridRows, static_cast<int>(std::round(depth / ProceduralGroundCellSize))) * ProceduralGroundGridMultiplier;
        spec.cellWidth = width / static_cast<float>(spec.columns);
        spec.cellHeight = depth / static_cast<float>(spec.rows);
        return spec;
    }

    int CountDistinctBoundaryMaterials(const std::array<int, 4>& neighbors, int materialIndex)
    {
        std::array<int, 4> distinct{};
        int count = 0;
        for (int neighbor : neighbors)
        {
            if (neighbor == materialIndex)
            {
                continue;
            }

            bool exists = false;
            for (int index = 0; index < count; ++index)
            {
                if (distinct[index] == neighbor)
                {
                    exists = true;
                    break;
                }
            }
            if (!exists)
            {
                distinct[count++] = neighbor;
            }
        }
        return count;
    }

    int GetGridCellMaterial(const std::vector<int>& cellMaterialIndices, const FGroundGridSpec& grid, int x, int y, int fallbackMaterial)
    {
        if (x < 0 || x >= grid.columns || y < 0 || y >= grid.rows)
        {
            return fallbackMaterial;
        }
        return cellMaterialIndices[static_cast<size_t>(y * grid.columns + x)];
    }

    FFloodShapeProfile BuildFloodShapeProfile(uint32_t seed, const FGroundGridSpec& grid, int seedX, int seedY, int targetCells)
    {
        constexpr std::array<glm::ivec2, 8> directions{
            glm::ivec2(1, 0),
            glm::ivec2(1, 1),
            glm::ivec2(0, 1),
            glm::ivec2(-1, 1),
            glm::ivec2(-1, 0),
            glm::ivec2(-1, -1),
            glm::ivec2(0, -1),
            glm::ivec2(1, -1),
        };

        FFloodShapeProfile profile{};
        profile.seedX = seedX;
        profile.seedY = seedY;
        profile.primaryDir = directions[seed % directions.size()];
        profile.secondaryDir = glm::ivec2(-profile.primaryDir.y, profile.primaryDir.x);
        const int span = std::max(5, static_cast<int>(std::sqrt(static_cast<float>(targetCells)) * 0.9f));
        const int skew = 2 + static_cast<int>((seed >> 6u) % static_cast<uint32_t>(std::max(3, span + 1)));
        profile.lobeATarget =
            glm::ivec2(std::clamp(seedX + profile.primaryDir.x * span + profile.secondaryDir.x * skew, 0, grid.columns - 1),
                       std::clamp(seedY + profile.primaryDir.y * span + profile.secondaryDir.y * skew, 0, grid.rows - 1));
        profile.lobeBTarget =
            glm::ivec2(std::clamp(seedX - profile.primaryDir.x * (span * 3 / 4 + 2) - profile.secondaryDir.x * (skew + 2),
                                  0,
                                  grid.columns - 1),
                       std::clamp(seedY - profile.primaryDir.y * (span * 3 / 4 + 2) - profile.secondaryDir.y * (skew + 2),
                                  0,
                                  grid.rows - 1));
        profile.lobeRadius = std::max(5, span / 2 + 3);
        profile.macroCellSize = 2 + static_cast<int>((seed >> 11u) & 0x1u);
        profile.microCellSize = 1;
        profile.variationWeight = 88 + static_cast<int>((seed >> 15u) & 0x3fu);
        profile.selectionSlack = 180 + static_cast<int>((seed >> 21u) & 0x7fu);
        return profile;
    }

    int ScoreFloodCandidate(const std::vector<int>& cellMaterialIndices,
                            const FGroundGridSpec& grid,
                            int cell,
                            int materialIndex,
                            const FFloodShapeProfile* profile,
                            uint32_t seed)
    {
        const int x = cell % grid.columns;
        const int y = cell / grid.columns;
        const int north = GetGridCellMaterial(cellMaterialIndices, grid, x, y + 1, -1);
        const int east = GetGridCellMaterial(cellMaterialIndices, grid, x + 1, y, -1);
        const int south = GetGridCellMaterial(cellMaterialIndices, grid, x, y - 1, -1);
        const int west = GetGridCellMaterial(cellMaterialIndices, grid, x - 1, y, -1);
        const int northEast = GetGridCellMaterial(cellMaterialIndices, grid, x + 1, y + 1, -1);
        const int southEast = GetGridCellMaterial(cellMaterialIndices, grid, x + 1, y - 1, -1);
        const int southWest = GetGridCellMaterial(cellMaterialIndices, grid, x - 1, y - 1, -1);
        const int northWest = GetGridCellMaterial(cellMaterialIndices, grid, x - 1, y + 1, -1);
        const int north2 = GetGridCellMaterial(cellMaterialIndices, grid, x, y + 2, -1);
        const int east2 = GetGridCellMaterial(cellMaterialIndices, grid, x + 2, y, -1);
        const int south2 = GetGridCellMaterial(cellMaterialIndices, grid, x, y - 2, -1);
        const int west2 = GetGridCellMaterial(cellMaterialIndices, grid, x - 2, y, -1);

        const bool sameNorth = north == materialIndex;
        const bool sameEast = east == materialIndex;
        const bool sameSouth = south == materialIndex;
        const bool sameWest = west == materialIndex;
        const int sameCount = static_cast<int>(sameNorth) + static_cast<int>(sameEast) + static_cast<int>(sameSouth) + static_cast<int>(sameWest);
        const int diagonalCount = static_cast<int>(northEast == materialIndex) + static_cast<int>(southEast == materialIndex) +
                                  static_cast<int>(southWest == materialIndex) + static_cast<int>(northWest == materialIndex);
        const int foreignCount = static_cast<int>(north >= 0 && north != materialIndex) +
                                 static_cast<int>(east >= 0 && east != materialIndex) +
                                 static_cast<int>(south >= 0 && south != materialIndex) +
                                 static_cast<int>(west >= 0 && west != materialIndex);

        int score = sameCount * 110 + diagonalCount * 70 - foreignCount * 40;
        if (sameNorth && sameSouth)
        {
            score += 160;
        }
        if (sameEast && sameWest)
        {
            score += 160;
        }
        if ((sameNorth || sameSouth) && (sameEast || sameWest))
        {
            score += 240;
        }
        if (sameNorth && sameEast)
        {
            score += northEast == materialIndex ? 280 : 120;
        }
        if (sameEast && sameSouth)
        {
            score += southEast == materialIndex ? 280 : 120;
        }
        if (sameSouth && sameWest)
        {
            score += southWest == materialIndex ? 280 : 120;
        }
        if (sameWest && sameNorth)
        {
            score += northWest == materialIndex ? 280 : 120;
        }
        if (sameNorth && north2 == materialIndex)
        {
            score += 50;
        }
        if (sameEast && east2 == materialIndex)
        {
            score += 50;
        }
        if (sameSouth && south2 == materialIndex)
        {
            score += 50;
        }
        if (sameWest && west2 == materialIndex)
        {
            score += 50;
        }
        if (sameCount == 1 && diagonalCount == 0)
        {
            score -= 140;
        }
        if ((sameNorth || sameSouth) && !(sameEast || sameWest) && diagonalCount == 0)
        {
            score -= 80;
        }
        if ((sameEast || sameWest) && !(sameNorth || sameSouth) && diagonalCount == 0)
        {
            score -= 80;
        }
        if (sameCount == 0)
        {
            score -= 240;
        }
        if (profile != nullptr && materialIndex != 0)
        {
            const auto lobeBonus = [x, y](const glm::ivec2& target, int radius)
            {
                const int distance = std::abs(x - target.x) + std::abs(y - target.y);
                return std::max(0, radius * 2 - distance) * 28;
            };
            score += std::max(lobeBonus(profile->lobeATarget, profile->lobeRadius), lobeBonus(profile->lobeBTarget, profile->lobeRadius));

            const int macroNoise = static_cast<int>(HashCell(seed ^ 0x68bc21ebu,
                                                             (x + profile->seedX) / profile->macroCellSize,
                                                             (y + profile->seedY) / profile->macroCellSize) &
                                                     0xffu) -
                                   128;
            const int microNoise =
                static_cast<int>(HashCell(seed ^ 0x02e5be93u, x / profile->microCellSize, y / profile->microCellSize) & 0x7fu) - 64;
            score += macroNoise * profile->variationWeight / 12;
            score += microNoise * std::max(18, profile->variationWeight / 2) / 6;

            const int branchDistance = std::abs((x - profile->seedX) * profile->secondaryDir.x + (y - profile->seedY) * profile->secondaryDir.y);
            score += std::max(0, profile->lobeRadius + 8 - branchDistance) * 16;
        }
        score += static_cast<int>(HashCell(seed, x, y) & 0x1fu);
        return score;
    }

    size_t SelectBestFloodCandidate(const std::vector<int>& candidates,
                                    const std::vector<int>& cellMaterialIndices,
                                    const FGroundGridSpec& grid,
                                    int materialIndex,
                                    const FFloodShapeProfile* profile,
                                    uint32_t seed)
    {
        size_t bestIndex = 0;
        int bestScore = std::numeric_limits<int>::min();
        std::vector<int> scores(candidates.size(), 0);
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            const int score = ScoreFloodCandidate(cellMaterialIndices, grid, candidates[index], materialIndex, profile, seed);
            scores[index] = score;
            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = index;
            }
        }

        const int slack = profile == nullptr ? 0 : profile->selectionSlack;
        if (slack <= 0)
        {
            return bestIndex;
        }

        std::vector<size_t> nearBest;
        nearBest.reserve(candidates.size());
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            if (scores[index] >= bestScore - slack)
            {
                nearBest.push_back(index);
            }
        }
        if (nearBest.empty())
        {
            return bestIndex;
        }
        const size_t pick = static_cast<size_t>(HashCell(seed ^ 0x9e3779b9u, bestScore, static_cast<int>(nearBest.size())) % nearBest.size());
        return nearBest[pick];
    }

    // 切角触发条件只看 4 邻居（不含对角）：当两个相邻轴向（如 N+E）是同一外材质 X
    // 且另两个轴向是 self 时，沿对应对角线把 cell 切两半。这同时覆盖了凸角（cell 是 self
    // 海中突出的 X 角）和凹角（X 区的内凹处）—— 两者用同一刀几何就能平滑边界。
    FGroundBoundaryDecision BuildBoundaryDecision(int materialIndex, int north, int east, int south, int west)
    {
        const std::array<int, 4> neighbors{north, east, south, west};
        if (CountDistinctBoundaryMaterials(neighbors, materialIndex) > 1)
        {
            return {};
        }

        if (north == east && north != materialIndex && south == materialIndex && west == materialIndex)
        {
            return {.pattern = EGroundBoundaryPattern::CornerNorthEast, .blendMaterialIndex = north};
        }
        if (east == south && east != materialIndex && north == materialIndex && west == materialIndex)
        {
            return {.pattern = EGroundBoundaryPattern::CornerSouthEast, .blendMaterialIndex = east};
        }
        if (south == west && south != materialIndex && north == materialIndex && east == materialIndex)
        {
            return {.pattern = EGroundBoundaryPattern::CornerSouthWest, .blendMaterialIndex = south};
        }
        if (west == north && west != materialIndex && east == materialIndex && south == materialIndex)
        {
            return {.pattern = EGroundBoundaryPattern::CornerNorthWest, .blendMaterialIndex = west};
        }

        return {};
    }

    void AppendGroundPiece(std::vector<Assets::Model>& models,
                           std::vector<std::shared_ptr<Assets::Node>>& nodes,
                           Brotato3D::FArenaResources& outResources,
                           const std::vector<glm::vec2>& polygon,
                           uint32_t materialId,
                           std::string_view debugName)
    {
        if (polygon.size() < 3)
        {
            return;
        }

        models.push_back(Assets::FProcModel::CreateExtrudedConvexPolygon(std::string(debugName), polygon, GroundBottomY, GroundTopY));
        const uint32_t modelId = static_cast<uint32_t>(models.size() - 1);
        outResources.groundNodes.push_back(SceneBuilder::CreateRenderNode(std::string(debugName),
                                                                          glm::vec3(0.0f),
                                                                          glm::vec3(1.0f),
                                                                          static_cast<uint32_t>(nodes.size()),
                                                                          modelId,
                                                                          materialId));
        nodes.push_back(outResources.groundNodes.back());
    }

    void MarkRenderOnly(const std::shared_ptr<Assets::Node>& node)
    {
        auto physicsComponent = std::make_shared<Runtime::PhysicsComponent>();
        physicsComponent->SetMobility(Runtime::ENodeMobility::Dynamic);
        node->AddComponent(physicsComponent);
    }

    const Brotato3D::Pcg::FPropDef* FindPropDef(const Brotato3D::Pcg::FArenaPcgConfig& config, const std::string& propId)
    {
        const auto it = std::find_if(config.props.begin(), config.props.end(), [&propId](const Brotato3D::Pcg::FPropDef& prop)
        {
            return prop.id == propId;
        });
        return it != config.props.end() ? &(*it) : nullptr;
    }

    void BuildPcgProps(std::vector<Assets::Model>& models,
                       std::vector<Assets::FMaterial>& materials,
                       std::vector<std::shared_ptr<Assets::Node>>& nodes,
                       Brotato3D::FArenaResources& outResources,
                       const Brotato3D::Pcg::FArenaPcgConfig& config,
                       const Brotato3D::Pcg::FMapGraph& graph)
    {
        if (graph.props.empty())
        {
            return;
        }

        struct FPropRenderResource
        {
            uint32_t modelId = 0;
            uint32_t materialId = 0;
        };
        std::unordered_map<std::string, FPropRenderResource> resources;
        for (const Brotato3D::Pcg::FPropDef& prop : config.props)
        {
            if (prop.id.empty() || resources.contains(prop.id))
            {
                continue;
            }

            const glm::vec2 halfFootprint = glm::max(prop.footprintXZ * 0.5f, glm::vec2(0.05f));
            models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-halfFootprint.x, 0.0f, -halfFootprint.y),
                                                           glm::vec3(halfFootprint.x, prop.visualHeight, halfFootprint.y)));
            const uint32_t modelId = static_cast<uint32_t>(models.size() - 1);
            const uint32_t materialId = SceneBuilder::AddLambertianMaterial(materials, prop.baseColor);
            resources.emplace(prop.id, FPropRenderResource{.modelId = modelId, .materialId = materialId});
        }

        for (size_t index = 0; index < graph.props.size(); ++index)
        {
            const Brotato3D::Pcg::FPropPlacement& placement = graph.props[index];
            const Brotato3D::Pcg::FPropDef* prop = FindPropDef(config, placement.id);
            const auto resourceIt = resources.find(placement.id);
            if (!prop || resourceIt == resources.end())
            {
                continue;
            }

            const float groundY = Brotato3D::Pcg::SampleVisualHeight(graph.rootSeed,
                                                                     config.vertexJitterAmplitude,
                                                                     config.vertexJitterFrequency,
                                                                     placement.positionXZ);
            auto node = SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_PcgProp_{}_{}", placement.id, index),
                                                       glm::vec3(placement.positionXZ.x, groundY, placement.positionXZ.y),
                                                       glm::vec3(1.0f),
                                                       static_cast<uint32_t>(nodes.size()),
                                                       resourceIt->second.modelId,
                                                       resourceIt->second.materialId,
                                                       true,
                                                       glm::angleAxis(placement.rotationYRadians, glm::vec3(0.0f, 1.0f, 0.0f)),
                                                       false);
            MarkRenderOnly(node);
            outResources.propNodes.push_back(node);
            nodes.push_back(node);

            const glm::vec3 colliderExtent(std::max(0.05f, prop->footprintXZ.x),
                                           std::max(0.05f, prop->colliderHeight),
                                           std::max(0.05f, prop->footprintXZ.y));
            outResources.propColliders.push_back(Brotato3D::FArenaResources::FBoxCollider{
                .center = glm::vec3(placement.positionXZ.x, groundY + prop->colliderHeight * 0.5f, placement.positionXZ.y),
                .rotation = glm::angleAxis(placement.rotationYRadians, glm::vec3(0.0f, 1.0f, 0.0f)),
                .extent = colliderExtent,
            });
        }
    }

    void AppendBoxToMesh(std::vector<Assets::Vertex>& vertices,
                         std::vector<uint32_t>& indices,
                         const glm::vec3& minPos,
                         const glm::vec3& maxPos)
    {
        const std::array<glm::vec3, 8> p{
            glm::vec3(minPos.x, minPos.y, minPos.z),
            glm::vec3(maxPos.x, minPos.y, minPos.z),
            glm::vec3(maxPos.x, maxPos.y, minPos.z),
            glm::vec3(minPos.x, maxPos.y, minPos.z),
            glm::vec3(minPos.x, minPos.y, maxPos.z),
            glm::vec3(maxPos.x, minPos.y, maxPos.z),
            glm::vec3(maxPos.x, maxPos.y, maxPos.z),
            glm::vec3(minPos.x, maxPos.y, maxPos.z),
        };

        const auto addFace = [&vertices, &indices](const glm::vec3& a,
                                                   const glm::vec3& b,
                                                   const glm::vec3& c,
                                                   const glm::vec3& d,
                                                   const glm::vec3& normal)
        {
            const uint32_t offset = static_cast<uint32_t>(vertices.size());
            vertices.push_back(Assets::Vertex{a, normal, glm::vec4(1, 0, 0, 0), glm::vec2(0.0f), 0});
            vertices.push_back(Assets::Vertex{b, normal, glm::vec4(1, 0, 0, 0), glm::vec2(1.0f, 0.0f), 0});
            vertices.push_back(Assets::Vertex{c, normal, glm::vec4(1, 0, 0, 0), glm::vec2(1.0f), 0});
            vertices.push_back(Assets::Vertex{d, normal, glm::vec4(1, 0, 0, 0), glm::vec2(0.0f, 1.0f), 0});
            indices.insert(indices.end(), {offset, offset + 1, offset + 2, offset, offset + 2, offset + 3});
        };

        addFace(p[0], p[4], p[7], p[3], glm::vec3(-1.0f, 0.0f, 0.0f));
        addFace(p[1], p[2], p[6], p[5], glm::vec3(1.0f, 0.0f, 0.0f));
        addFace(p[0], p[1], p[5], p[4], glm::vec3(0.0f, -1.0f, 0.0f));
        addFace(p[3], p[7], p[6], p[2], glm::vec3(0.0f, 1.0f, 0.0f));
        addFace(p[0], p[3], p[2], p[1], glm::vec3(0.0f, 0.0f, -1.0f));
        addFace(p[4], p[5], p[6], p[7], glm::vec3(0.0f, 0.0f, 1.0f));
    }

    void BuildPcgBorderVisual(std::vector<Assets::Model>& models,
                              std::vector<std::shared_ptr<Assets::Node>>& nodes,
                              Brotato3D::FArenaResources& outResources,
                              const Brotato3D::Pcg::FMapGraph& graph,
                              uint32_t materialId)
    {
        if (graph.borderSegments.empty())
        {
            return;
        }

        std::vector<Assets::Vertex> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(graph.borderSegments.size() * 24);
        indices.reserve(graph.borderSegments.size() * 36);
        for (const Brotato3D::Pcg::FBorderSegment& segment : graph.borderSegments)
        {
            glm::vec2 minXZ = glm::min(segment.baseStartXZ, segment.baseEndXZ);
            glm::vec2 maxXZ = glm::max(segment.baseStartXZ, segment.baseEndXZ);
            const bool horizontal = std::abs(segment.baseStartXZ.y - segment.baseEndXZ.y) < 0.001f;
            if (horizontal)
            {
                const float outward = segment.baseStartXZ.y < 0.0f ? -1.0f : 1.0f;
                minXZ.y += outward < 0.0f ? -segment.thickness : 0.0f;
                maxXZ.y += outward > 0.0f ? segment.thickness : 0.0f;
            }
            else
            {
                const float outward = segment.baseStartXZ.x < 0.0f ? -1.0f : 1.0f;
                minXZ.x += outward < 0.0f ? -segment.thickness : 0.0f;
                maxXZ.x += outward > 0.0f ? segment.thickness : 0.0f;
            }
            AppendBoxToMesh(vertices,
                            indices,
                            glm::vec3(minXZ.x, 0.0f, minXZ.y),
                            glm::vec3(maxXZ.x, segment.topY, maxXZ.y));
        }

        models.push_back(Assets::FProcModel::CreateFromBuffers("Brotato3D_PcgFracturedBorder",
                                                               std::move(vertices),
                                                               std::move(indices),
                                                               false));
        const uint32_t modelId = static_cast<uint32_t>(models.size() - 1);
        auto node = SceneBuilder::CreateRenderNode("Brotato3D_PcgFracturedBorder",
                                                   glm::vec3(0.0f),
                                                   glm::vec3(1.0f),
                                                   static_cast<uint32_t>(nodes.size()),
                                                   modelId,
                                                   materialId,
                                                   true,
                                                   glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                   false);
        MarkRenderOnly(node);
        outResources.borderNodes.push_back(node);
        nodes.push_back(node);
    }

    void BuildExplicitGround(std::vector<Assets::Model>& models,
                             std::vector<Assets::FMaterial>& materials,
                             std::vector<std::shared_ptr<Assets::Node>>& nodes,
                             Brotato3D::FArenaResources& outResources,
                             const Brotato3D::FArenaDef& selectedArena,
                             const glm::vec2& halfExtent,
                             const std::string& selectedId)
    {
        if (selectedArena.groundTiles.empty())
        {
            const Brotato3D::FGroundTileDef fallbackTile{
                .minXZ = glm::vec2(-halfExtent.x, -halfExtent.y),
                .maxXZ = glm::vec2(halfExtent.x, halfExtent.y),
                .color = selectedArena.baseGroundColor,
                .kind = Brotato3D::EGroundMaterialKind::Lambertian,
            };
            AppendGroundPiece(models,
                              nodes,
                              outResources,
                              BuildGroundPolygon(fallbackTile, halfExtent),
                              outResources.groundMaterialIds[selectedId],
                              "Brotato3D_GroundBase");
            return;
        }

        for (size_t tileIndex = 0; tileIndex < selectedArena.groundTiles.size(); ++tileIndex)
        {
            const Brotato3D::FGroundTileDef& tile = selectedArena.groundTiles[tileIndex];
            const std::vector<glm::vec2> polygon = BuildGroundPolygon(tile, halfExtent);
            if (polygon.size() < 3)
            {
                spdlog::warn("[Brotato3D] skipped invalid ground tile {} for arena '{}'", tileIndex, selectedArena.id);
                continue;
            }

            const uint32_t tileMaterialId = AddGroundMaterial(materials, tile, tileIndex);
            AppendGroundPiece(models,
                              nodes,
                              outResources,
                              polygon,
                              tileMaterialId,
                              fmt::format("Brotato3D_GroundTile_{}_{}", tileIndex, ToGroundKindName(tile.kind)));
        }
    }

    Brotato3D::Pcg::FArenaPcgConfig BuildPcgConfigWithPalette(const Brotato3D::FArenaDef& selectedArena)
    {
        Brotato3D::Pcg::FArenaPcgConfig config = selectedArena.pcg;
        if (!config.palette.empty())
        {
            if (config.palette.size() > 16)
            {
                config.palette.resize(16);
            }
            return config;
        }

        config.palette.push_back(selectedArena.baseGroundColor);
        for (const Brotato3D::FGroundTileDef& tile : selectedArena.groundTiles)
        {
            if (config.palette.size() >= 16)
            {
                break;
            }
            config.palette.push_back(tile.color);
        }
        return config;
    }

    std::array<uint32_t, 16> AddPcgGroundMaterials(std::vector<Assets::FMaterial>& materials,
                                                   const Brotato3D::Pcg::FArenaPcgConfig& config,
                                                   const glm::vec3& fallbackColor)
    {
        std::array<uint32_t, 16> materialIds{};
        const std::vector<glm::vec3>& palette = config.palette;
        const size_t paletteCount = std::max<size_t>(1, std::min<size_t>(16, palette.size()));
        for (size_t index = 0; index < paletteCount; ++index)
        {
            const glm::vec3 color = palette.empty() ? fallbackColor : palette[index];
            materialIds[index] = SceneBuilder::AddLambertianMaterial(materials, color);
        }
        for (size_t index = paletteCount; index < materialIds.size(); ++index)
        {
            materialIds[index] = materialIds[0];
        }
        return materialIds;
    }

    bool BuildPcgGround(std::vector<Assets::Model>& models,
                        std::vector<Assets::FMaterial>& materials,
                        std::vector<std::shared_ptr<Assets::Node>>& nodes,
                        Brotato3D::FArenaResources& outResources,
                        const Brotato3D::FArenaDef& selectedArena,
                        const glm::vec2& halfExtent)
    {
        Brotato3D::Pcg::FArenaPcgConfig pcgConfig = BuildPcgConfigWithPalette(selectedArena);
        Brotato3D::Pcg::FMapGraph graph{};
        const uint32_t sessionSeed = HashArenaSeed(selectedArena.id) ^ GetArenaSessionSeed();
        if (!Brotato3D::Pcg::BuildMapGraph(pcgConfig, halfExtent, sessionSeed, graph))
        {
            spdlog::warn("[Brotato3DPcg] failed to build graph for arena '{}'", selectedArena.id);
            return false;
        }
        outResources.terrainHeight = Brotato3D::FArenaResources::FTerrainHeightSampler{
            .enabled = true,
            .rootSeed = graph.rootSeed,
            .vertexJitterAmplitude = pcgConfig.vertexJitterAmplitude,
            .vertexJitterFrequency = pcgConfig.vertexJitterFrequency,
        };

        Brotato3D::Pcg::FTerrainMeshBuffers mesh = Brotato3D::Pcg::BuildTerrainMesh(pcgConfig, graph);
        if (mesh.vertices.empty() || mesh.indices.empty())
        {
            spdlog::warn("[Brotato3DPcg] generated empty terrain mesh for arena '{}'", selectedArena.id);
            return false;
        }

        const std::array<uint32_t, 16> materialIds = AddPcgGroundMaterials(materials, pcgConfig, selectedArena.baseGroundColor);
        models.push_back(Assets::FProcModel::CreateFromBuffers("Brotato3D_PcgTerrain",
                                                               std::move(mesh.vertices),
                                                               std::move(mesh.indices),
                                                               false));
        const uint32_t modelId = static_cast<uint32_t>(models.size() - 1);
        outResources.groundNodes.push_back(SceneBuilder::CreateRenderNode("Brotato3D_PcgTerrain",
                                                                          glm::vec3(0.0f),
                                                                          glm::vec3(1.0f),
                                                                          static_cast<uint32_t>(nodes.size()),
                                                                          modelId,
                                                                          materialIds,
                                                                          true,
                                                                          glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                                          false));
        nodes.push_back(outResources.groundNodes.back());

        const uint32_t borderMaterialId = outResources.borderMaterialIds[selectedArena.id];
        BuildPcgProps(models, materials, nodes, outResources, pcgConfig, graph);
        BuildPcgBorderVisual(models, nodes, outResources, graph, borderMaterialId);

        spdlog::info("[Brotato3DPcg] seed=0x{:08x} arena={} cells={} props={} verts={} indices={}",
                     graph.rootSeed,
                     selectedArena.id,
                     graph.cells.size(),
                     graph.props.size(),
                     models.back().NumberOfVertices(),
                     models.back().NumberOfIndices());
        return true;
    }

    void BuildFloodFilledGround(std::vector<Assets::Model>& models,
                                std::vector<Assets::FMaterial>& materials,
                                std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                Brotato3D::FArenaResources& outResources,
                                const Brotato3D::FArenaDef& selectedArena,
                                const glm::vec2& halfExtent,
                                const std::string& selectedId)
    {
        const FGroundGridSpec grid = BuildGroundGridSpec(halfExtent);
        const int totalCells = grid.columns * grid.rows;
        std::vector<int> cellMaterialIndices(static_cast<size_t>(totalCells), 0);
        std::vector<uint32_t> materialPaletteIds{outResources.groundMaterialIds[selectedId]};
        materialPaletteIds.reserve(selectedArena.groundTiles.size() + 1);

        for (size_t tileIndex = 0; tileIndex < selectedArena.groundTiles.size(); ++tileIndex)
        {
            materialPaletteIds.push_back(AddGroundMaterial(materials, selectedArena.groundTiles[tileIndex], tileIndex));
        }

        auto cellIndex = [&grid](int x, int y)
        {
            return y * grid.columns + x;
        };

        auto collectCellsWithMaterial = [&cellMaterialIndices](int desiredMaterialIndex)
        {
            std::vector<int> result;
            for (int index = 0; index < static_cast<int>(cellMaterialIndices.size()); ++index)
            {
                if (cellMaterialIndices[static_cast<size_t>(index)] == desiredMaterialIndex)
                {
                    result.push_back(index);
                }
            }
            return result;
        };

        const uint32_t arenaSeed = HashArenaSeed(selectedArena.id) ^ GetArenaSessionSeed();
        for (size_t tileIndex = 0; tileIndex < selectedArena.groundTiles.size(); ++tileIndex)
        {
            const Brotato3D::FGroundTileDef& tile = selectedArena.groundTiles[tileIndex];
            const uint32_t tileSeed = arenaSeed ^ (static_cast<uint32_t>(tileIndex + 1) * 0x85ebca6bu);
            const int targetCells = std::clamp(static_cast<int>(std::round(tile.coverage * static_cast<float>(totalCells))),
                                               0,
                                               totalCells);
            if (targetCells <= 0)
            {
                continue;
            }

            std::vector<int> baseCells = collectCellsWithMaterial(0);
            if (baseCells.empty())
            {
                break;
            }

            const int seed = baseCells[SelectBestFloodCandidate(baseCells, cellMaterialIndices, grid, 0, nullptr, tileSeed)];
            const int materialIndex = static_cast<int>(tileIndex + 1);
            cellMaterialIndices[static_cast<size_t>(seed)] = materialIndex;
            const FFloodShapeProfile shapeProfile = BuildFloodShapeProfile(tileSeed, grid, seed % grid.columns, seed / grid.columns, targetCells);

            std::vector<int> frontier;
            frontier.reserve(static_cast<size_t>(targetCells) * 2);
            std::vector<uint8_t> frontierFlags(static_cast<size_t>(totalCells), 0u);
            auto enqueueBaseNeighbor = [&](int cell)
            {
                if (cell < 0 || cell >= totalCells)
                {
                    return;
                }
                if (cellMaterialIndices[static_cast<size_t>(cell)] != 0 || frontierFlags[static_cast<size_t>(cell)] != 0u)
                {
                    return;
                }
                frontier.push_back(cell);
                frontierFlags[static_cast<size_t>(cell)] = 1u;
            };

            const int seedX = seed % grid.columns;
            const int seedY = seed / grid.columns;
            enqueueBaseNeighbor(seedX > 0 ? cellIndex(seedX - 1, seedY) : -1);
            enqueueBaseNeighbor(seedX + 1 < grid.columns ? cellIndex(seedX + 1, seedY) : -1);
            enqueueBaseNeighbor(seedY > 0 ? cellIndex(seedX, seedY - 1) : -1);
            enqueueBaseNeighbor(seedY + 1 < grid.rows ? cellIndex(seedX, seedY + 1) : -1);

            int grownCells = 1;
            while (grownCells < targetCells && !frontier.empty())
            {
                const size_t chosenIndex =
                    SelectBestFloodCandidate(frontier,
                                             cellMaterialIndices,
                                             grid,
                                             materialIndex,
                                             &shapeProfile,
                                             tileSeed ^ (grownCells * 0x27d4eb2du));
                const int cell = frontier[chosenIndex];
                frontier[chosenIndex] = frontier.back();
                frontier.pop_back();
                frontierFlags[static_cast<size_t>(cell)] = 0u;

                if (cellMaterialIndices[static_cast<size_t>(cell)] != 0)
                {
                    continue;
                }

                cellMaterialIndices[static_cast<size_t>(cell)] = materialIndex;
                ++grownCells;

                const int x = cell % grid.columns;
                const int y = cell / grid.columns;
                enqueueBaseNeighbor(x > 0 ? cellIndex(x - 1, y) : -1);
                enqueueBaseNeighbor(x + 1 < grid.columns ? cellIndex(x + 1, y) : -1);
                enqueueBaseNeighbor(y > 0 ? cellIndex(x, y - 1) : -1);
                enqueueBaseNeighbor(y + 1 < grid.rows ? cellIndex(x, y + 1) : -1);
            }
        }

        // Cleanup pass: 4 邻居中如果 ≥3 个是同一非 self 材质（T 型尖刺 / 完全孤岛），把当前
        // cell 吞进该材质。剩下的"2 self + 2 X"L 形会走切角逻辑，"3 distinct"会被 distinct
        // count 兜住。多跑几轮直到稳定，避免连锁产生新孤岛；上限 4 轮防退化。
        constexpr int CleanupMaxPasses = 4;
        for (int pass = 0; pass < CleanupMaxPasses; ++pass)
        {
            const std::vector<int> snapshot = cellMaterialIndices;
            bool changed = false;
            for (int y = 0; y < grid.rows; ++y)
            {
                for (int x = 0; x < grid.columns; ++x)
                {
                    const int self = snapshot[static_cast<size_t>(cellIndex(x, y))];
                    const int n = y + 1 < grid.rows ? snapshot[static_cast<size_t>(cellIndex(x, y + 1))] : self;
                    const int s = y > 0 ? snapshot[static_cast<size_t>(cellIndex(x, y - 1))] : self;
                    const int e = x + 1 < grid.columns ? snapshot[static_cast<size_t>(cellIndex(x + 1, y))] : self;
                    const int w = x > 0 ? snapshot[static_cast<size_t>(cellIndex(x - 1, y))] : self;
                    const std::array<int, 4> neighbors{n, e, s, w};

                    int dominant = -1;
                    int dominantCount = 0;
                    for (int i = 0; i < 4; ++i)
                    {
                        if (neighbors[i] == self)
                        {
                            continue;
                        }
                        int count = 0;
                        for (int j = 0; j < 4; ++j)
                        {
                            if (neighbors[j] == neighbors[i])
                            {
                                ++count;
                            }
                        }
                        if (count > dominantCount)
                        {
                            dominantCount = count;
                            dominant = neighbors[i];
                        }
                    }

                    if (dominant >= 0 && dominantCount >= 3)
                    {
                        cellMaterialIndices[static_cast<size_t>(cellIndex(x, y))] = dominant;
                        changed = true;
                    }
                }
            }
            if (!changed)
            {
                break;
            }
        }

        for (int y = 0; y < grid.rows; ++y)
        {
            const float z0 = -halfExtent.y + static_cast<float>(y) * grid.cellHeight;
            const float z1 = z0 + grid.cellHeight;
            for (int x = 0; x < grid.columns; ++x)
            {
                const float x0 = -halfExtent.x + static_cast<float>(x) * grid.cellWidth;
                const float x1 = x0 + grid.cellWidth;
                const int materialIndex = cellMaterialIndices[static_cast<size_t>(cellIndex(x, y))];
                const int north = y + 1 < grid.rows ? cellMaterialIndices[static_cast<size_t>(cellIndex(x, y + 1))] : materialIndex;
                const int east = x + 1 < grid.columns ? cellMaterialIndices[static_cast<size_t>(cellIndex(x + 1, y))] : materialIndex;
                const int south = y > 0 ? cellMaterialIndices[static_cast<size_t>(cellIndex(x, y - 1))] : materialIndex;
                const int west = x > 0 ? cellMaterialIndices[static_cast<size_t>(cellIndex(x - 1, y))] : materialIndex;
                const FGroundBoundaryDecision boundary = BuildBoundaryDecision(materialIndex, north, east, south, west);

                const glm::vec2 southWest(x0, z0);
                const glm::vec2 southEast(x1, z0);
                const glm::vec2 northEast(x1, z1);
                const glm::vec2 northWest(x0, z1);
                const uint32_t primaryMaterialId = materialPaletteIds[static_cast<size_t>(materialIndex)];

                if (boundary.pattern == EGroundBoundaryPattern::None || boundary.blendMaterialIndex < 0)
                {
                    AppendGroundPiece(models,
                                      nodes,
                                      outResources,
                                      {southWest, southEast, northEast, northWest},
                                      primaryMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_full", x, y));
                    continue;
                }

                const uint32_t blendMaterialId = materialPaletteIds[static_cast<size_t>(boundary.blendMaterialIndex)];
                switch (boundary.pattern)
                {
                case EGroundBoundaryPattern::CornerNorthEast:
                    AppendGroundPiece(models, nodes, outResources, {northWest, northEast, southEast}, blendMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_blendNE", x, y));
                    AppendGroundPiece(models, nodes, outResources, {northWest, southEast, southWest}, primaryMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_baseNE", x, y));
                    break;
                case EGroundBoundaryPattern::CornerSouthEast:
                    AppendGroundPiece(models, nodes, outResources, {northEast, southEast, southWest}, blendMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_blendSE", x, y));
                    AppendGroundPiece(models, nodes, outResources, {northEast, southWest, northWest}, primaryMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_baseSE", x, y));
                    break;
                case EGroundBoundaryPattern::CornerSouthWest:
                    AppendGroundPiece(models, nodes, outResources, {northWest, southEast, southWest}, blendMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_blendSW", x, y));
                    AppendGroundPiece(models, nodes, outResources, {northWest, northEast, southEast}, primaryMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_baseSW", x, y));
                    break;
                case EGroundBoundaryPattern::CornerNorthWest:
                    AppendGroundPiece(models, nodes, outResources, {northEast, southWest, northWest}, blendMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_blendNW", x, y));
                    AppendGroundPiece(models, nodes, outResources, {northEast, southEast, southWest}, primaryMaterialId,
                                      fmt::format("Brotato3D_GroundCell_{}_{}_baseNW", x, y));
                    break;
                case EGroundBoundaryPattern::None:
                default:
                    break;
                }
            }
        }
    }
}

namespace Brotato3D
{
    void BuildArena(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    FArenaResources& outResources,
                    const std::vector<FArenaDef>& arenaDefs,
                    const std::string& selectedArenaId)
    {
        outResources = {};
        const std::vector<FArenaDef> fallbackArenas =
            arenaDefs.empty() ? std::vector<FArenaDef>{{.id = "grassland", .name = "绿野"}} : arenaDefs;
        for (const FArenaDef& arena : fallbackArenas)
        {
            outResources.groundMaterialIds[arena.id] = SceneBuilder::AddLambertianMaterial(materials, arena.baseGroundColor);
            outResources.borderMaterialIds[arena.id] = SceneBuilder::AddLambertianMaterial(materials, arena.borderColor);
        }

        const std::string selectedId = outResources.groundMaterialIds.contains(selectedArenaId) ? selectedArenaId :
                                                                                                  fallbackArenas.front().id;
        const FArenaDef& selectedArena = *FindArena(fallbackArenas, selectedId);
        const glm::vec2 halfExtent = glm::max(selectedArena.halfExtent, glm::vec2(1.0f));

        const bool builtPcgGround = selectedArena.pcg.enabled &&
                                    BuildPcgGround(models, materials, nodes, outResources, selectedArena, halfExtent);
        if (!builtPcgGround && UseProceduralGroundFlood(selectedArena))
        {
            BuildFloodFilledGround(models, materials, nodes, outResources, selectedArena, halfExtent, selectedId);
        }
        else if (!builtPcgGround)
        {
            BuildExplicitGround(models, materials, nodes, outResources, selectedArena, halfExtent, selectedId);
        }

        if (!builtPcgGround)
        {
            const uint32_t boundaryMaterialId = outResources.borderMaterialIds[selectedId];
            models.push_back(
                Assets::FProcModel::CreateBox(glm::vec3(-(halfExtent.x + VisualWallThickness * 0.5f), 0.0f, -VisualWallThickness * 0.5f),
                                              glm::vec3(halfExtent.x + VisualWallThickness * 0.5f, VisualWallHeight, VisualWallThickness * 0.5f)));
            const uint32_t horizontalBoundaryModelId = static_cast<uint32_t>(models.size() - 1);
            models.push_back(
                Assets::FProcModel::CreateBox(glm::vec3(-VisualWallThickness * 0.5f, 0.0f, -(halfExtent.y + VisualWallThickness * 0.5f)),
                                              glm::vec3(VisualWallThickness * 0.5f, VisualWallHeight, halfExtent.y + VisualWallThickness * 0.5f)));
            const uint32_t verticalBoundaryModelId = static_cast<uint32_t>(models.size() - 1);

            outResources.borderNodes.push_back(SceneBuilder::CreateRenderNode("Brotato3D_Boundary_N",
                                                                              glm::vec3(0.0f, 0.0f, -(halfExtent.y + VisualWallThickness * 0.5f)),
                                                                              glm::vec3(1.0f),
                                                                              static_cast<uint32_t>(nodes.size()),
                                                                              horizontalBoundaryModelId,
                                                                              boundaryMaterialId));
            nodes.push_back(outResources.borderNodes.back());
            outResources.borderNodes.push_back(SceneBuilder::CreateRenderNode("Brotato3D_Boundary_S",
                                                                              glm::vec3(0.0f, 0.0f, halfExtent.y + VisualWallThickness * 0.5f),
                                                                              glm::vec3(1.0f),
                                                                              static_cast<uint32_t>(nodes.size()),
                                                                              horizontalBoundaryModelId,
                                                                              boundaryMaterialId));
            nodes.push_back(outResources.borderNodes.back());
            outResources.borderNodes.push_back(SceneBuilder::CreateRenderNode("Brotato3D_Boundary_W",
                                                                              glm::vec3(-(halfExtent.x + VisualWallThickness * 0.5f), 0.0f, 0.0f),
                                                                              glm::vec3(1.0f),
                                                                              static_cast<uint32_t>(nodes.size()),
                                                                              verticalBoundaryModelId,
                                                                              boundaryMaterialId));
            nodes.push_back(outResources.borderNodes.back());
            outResources.borderNodes.push_back(SceneBuilder::CreateRenderNode("Brotato3D_Boundary_E",
                                                                              glm::vec3(halfExtent.x + VisualWallThickness * 0.5f, 0.0f, 0.0f),
                                                                              glm::vec3(1.0f),
                                                                              static_cast<uint32_t>(nodes.size()),
                                                                              verticalBoundaryModelId,
                                                                              boundaryMaterialId));
            nodes.push_back(outResources.borderNodes.back());
        }
    }
}
