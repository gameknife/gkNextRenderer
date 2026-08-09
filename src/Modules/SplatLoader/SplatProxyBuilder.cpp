#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SplatLoader/SplatProxyBuilder.hpp"

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/SplatLoader/GaussianSplat.hpp"
#include "Modules/SplatLoader/GaussianSplatComponent.h"
#include "Modules/SplatLoader/SplatSettings.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <limits>

namespace Assets
{
    namespace
    {
        constexpr uint32_t kInvalidId = std::numeric_limits<uint32_t>::max();

        struct FProxyBuildConfig
        {
            bool enable = true;
            bool shadowEnable = true;
            bool rayOcclusionEnable = true;
            bool debugVisible = false;
            uint32_t gridMax = 64;
            float sigma = 2.5f;
            float isoThreshold = 0.35f;
        };

        FProxyBuildConfig ResolveProxyBuildConfig()
        {
            FProxyBuildConfig config{};
            if (NextEngine::GetInstance())
            {
                const auto settings = Modules::Splat::GetSettings(*NextEngine::GetInstance());
                if (!settings) return config;
                config.enable = settings->proxyEnable;
                config.shadowEnable = settings->shadowEnable;
                config.rayOcclusionEnable = settings->rayOcclusionEnable;
                config.debugVisible = settings->proxyDebugVisible;
                config.gridMax = std::clamp(settings->proxyGridMax, 8u, 128u);
                config.sigma = std::clamp(settings->proxySigma, 1.0f, 4.0f);
                config.isoThreshold = std::clamp(settings->proxyIsoThreshold, 0.01f, 0.95f);
            }
            return config;
        }

        glm::mat3 DecodeSplatCovariance(const FGaussianSplatGpu& splat)
        {
            glm::mat3 covariance(0.0f);
            covariance[0][0] = splat.covariance0.x;
            covariance[1][0] = covariance[0][1] = splat.covariance0.y;
            covariance[2][0] = covariance[0][2] = splat.covariance0.z;
            covariance[1][1] = splat.covariance0.w;
            covariance[2][1] = covariance[1][2] = splat.covariance1.x;
            covariance[2][2] = splat.covariance1.y;
            return covariance;
        }

        glm::uvec3 ResolveProxyGridDim(const glm::vec3& extent, uint32_t gridMax)
        {
            const float maxExtent = std::max({extent.x, extent.y, extent.z, 1e-3f});
            glm::uvec3 dims(8u);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const float ratio = std::max(extent[axis], 1e-3f) / maxExtent;
                dims[axis] = std::clamp(static_cast<uint32_t>(std::ceil(ratio * static_cast<float>(gridMax))), 8u, gridMax);
            }
            return dims;
        }

        size_t DensityIndex(glm::uvec3 p, glm::uvec3 dims)
        {
            return static_cast<size_t>(p.x) + static_cast<size_t>(dims.x) *
                (static_cast<size_t>(p.y) + static_cast<size_t>(dims.y) * static_cast<size_t>(p.z));
        }

        void AddProxyFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                          const glm::vec3 corners[4], const glm::vec3& normal)
        {
            const uint32_t base = static_cast<uint32_t>(vertices.size());
            const glm::vec4 tangent = glm::abs(normal.y) > 0.9f ? glm::vec4(1, 0, 0, 1) : glm::vec4(0, 1, 0, 1);
            for (uint32_t i = 0; i < 4; ++i)
            {
                Vertex vertex{};
                vertex.Position = corners[i];
                vertex.Normal = normal;
                vertex.Tangent = tangent;
                vertex.TexCoord = glm::vec2(0.0f);
                vertex.MaterialIndex = 0;
                vertices.push_back(vertex);
            }
            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }

        void AddVoxelProxyFaces(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
                                const glm::uvec3& p, const glm::vec3& minBounds, const glm::vec3& cellSize,
                                bool negX, bool posX, bool negY, bool posY, bool negZ, bool posZ)
        {
            const glm::vec3 v0 = minBounds + glm::vec3(p) * cellSize;
            const glm::vec3 v1 = v0 + cellSize;

            glm::vec3 corners[4];
            if (negX)
            {
                corners[0] = {v0.x, v0.y, v0.z}; corners[1] = {v0.x, v0.y, v1.z};
                corners[2] = {v0.x, v1.y, v1.z}; corners[3] = {v0.x, v1.y, v0.z};
                AddProxyFace(vertices, indices, corners, {-1, 0, 0});
            }

            if (posX)
            {
                corners[0] = {v1.x, v0.y, v1.z}; corners[1] = {v1.x, v0.y, v0.z};
                corners[2] = {v1.x, v1.y, v0.z}; corners[3] = {v1.x, v1.y, v1.z};
                AddProxyFace(vertices, indices, corners, {1, 0, 0});
            }

            if (negY)
            {
                corners[0] = {v1.x, v0.y, v0.z}; corners[1] = {v1.x, v0.y, v1.z};
                corners[2] = {v0.x, v0.y, v1.z}; corners[3] = {v0.x, v0.y, v0.z};
                AddProxyFace(vertices, indices, corners, {0, -1, 0});
            }

            if (posY)
            {
                corners[0] = {v0.x, v1.y, v0.z}; corners[1] = {v0.x, v1.y, v1.z};
                corners[2] = {v1.x, v1.y, v1.z}; corners[3] = {v1.x, v1.y, v0.z};
                AddProxyFace(vertices, indices, corners, {0, 1, 0});
            }

            if (negZ)
            {
                corners[0] = {v1.x, v0.y, v0.z}; corners[1] = {v0.x, v0.y, v0.z};
                corners[2] = {v0.x, v1.y, v0.z}; corners[3] = {v1.x, v1.y, v0.z};
                AddProxyFace(vertices, indices, corners, {0, 0, -1});
            }

            if (posZ)
            {
                corners[0] = {v0.x, v0.y, v1.z}; corners[1] = {v1.x, v0.y, v1.z};
                corners[2] = {v1.x, v1.y, v1.z}; corners[3] = {v0.x, v1.y, v1.z};
                AddProxyFace(vertices, indices, corners, {0, 0, 1});
            }
        }

        Model BuildSplatProxyMesh(const FGaussianSplatData& splatData,
                                  const Runtime::GaussianSplatComponent& component,
                                  const FProxyBuildConfig& config,
                                  glm::uvec3& outGridDim)
        {
            const glm::vec3 minBounds = splatData.aabbMin;
            const glm::vec3 maxBounds = splatData.aabbMax;
            const glm::vec3 extent = glm::max(maxBounds - minBounds, glm::vec3(1e-3f));
            const glm::uvec3 dims = ResolveProxyGridDim(extent, config.gridMax);
            const glm::vec3 cellSize = extent / glm::vec3(dims);
            outGridDim = dims;

            std::vector<float> density(static_cast<size_t>(dims.x) * dims.y * dims.z, 0.0f);
            const float sigmaRadius = config.sigma;
            const float sigmaRadiusSq = sigmaRadius * sigmaRadius;
            const float densityScale = component.GetProxyDensityScale() * component.GetOpacityScale();
            if (densityScale <= 0.0f)
            {
                return Model::CreateFromGeometry(splatData.name + "_splat_proxy", std::vector<Vertex>{},
                                                 std::vector<uint32_t>{}, false);
            }

            for (const FGaussianSplatGpu& splat : splatData.splats)
            {
                const glm::vec3 center = glm::vec3(splat.positionOpacity);
                const float opacity = std::clamp(splat.positionOpacity.w * densityScale, 0.0f, 1.0f);
                if (opacity <= 0.0f)
                {
                    continue;
                }

                const glm::mat3 covariance = DecodeSplatCovariance(splat);
                const float determinant = glm::determinant(covariance);
                if (std::abs(determinant) < 1e-12f || !std::isfinite(determinant))
                {
                    continue;
                }
                const glm::mat3 invCovariance = glm::inverse(covariance);
                const glm::vec3 sigma = glm::sqrt(glm::max(
                    glm::vec3(covariance[0][0], covariance[1][1], covariance[2][2]), glm::vec3(1e-8f)));
                const glm::vec3 radius = sigma * sigmaRadius;

                glm::ivec3 voxelMin = glm::ivec3(glm::floor((center - radius - minBounds) / cellSize));
                glm::ivec3 voxelMax = glm::ivec3(glm::ceil((center + radius - minBounds) / cellSize));
                voxelMin = glm::clamp(voxelMin, glm::ivec3(0), glm::ivec3(dims) - glm::ivec3(1));
                voxelMax = glm::clamp(voxelMax, glm::ivec3(0), glm::ivec3(dims) - glm::ivec3(1));

                constexpr int maxCellsPerSplatAxis = 24;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    if (voxelMax[axis] - voxelMin[axis] + 1 > maxCellsPerSplatAxis)
                    {
                        const int centerCell = std::clamp(
                            static_cast<int>(std::floor((center[axis] - minBounds[axis]) / cellSize[axis])),
                            0, static_cast<int>(dims[axis]) - 1);
                        voxelMin[axis] = std::max(0, centerCell - maxCellsPerSplatAxis / 2);
                        voxelMax[axis] = std::min(static_cast<int>(dims[axis]) - 1,
                                                  voxelMin[axis] + maxCellsPerSplatAxis - 1);
                    }
                }

                for (int z = voxelMin.z; z <= voxelMax.z; ++z)
                {
                    for (int y = voxelMin.y; y <= voxelMax.y; ++y)
                    {
                        for (int x = voxelMin.x; x <= voxelMax.x; ++x)
                        {
                            const glm::uvec3 p(x, y, z);
                            const glm::vec3 samplePos = minBounds + (glm::vec3(p) + glm::vec3(0.5f)) * cellSize;
                            const glm::vec3 d = samplePos - center;
                            const float q = glm::dot(d, invCovariance * d);
                            if (!std::isfinite(q) || q > sigmaRadiusSq)
                            {
                                continue;
                            }
                            density[DensityIndex(p, dims)] += opacity * std::exp(-0.5f * q);
                        }
                    }
                }
            }

            const float threshold = std::clamp(component.GetProxyAlphaThreshold(), 0.01f, 0.95f);
            std::vector<uint8_t> solid(density.size(), 0u);
            for (size_t i = 0; i < density.size(); ++i)
            {
                const float alpha = 1.0f - std::exp(-density[i]);
                solid[i] = alpha >= std::max(threshold, config.isoThreshold) ? 1u : 0u;
            }

            auto isSolid = [&](int x, int y, int z) -> bool
            {
                if (x < 0 || y < 0 || z < 0 ||
                    x >= static_cast<int>(dims.x) || y >= static_cast<int>(dims.y) || z >= static_cast<int>(dims.z))
                {
                    return false;
                }
                return solid[DensityIndex(glm::uvec3(x, y, z), dims)] != 0u;
            };

            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            for (uint32_t z = 0; z < dims.z; ++z)
            {
                for (uint32_t y = 0; y < dims.y; ++y)
                {
                    for (uint32_t x = 0; x < dims.x; ++x)
                    {
                        if (!isSolid(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)))
                        {
                            continue;
                        }

                        const bool negX = !isSolid(static_cast<int>(x) - 1, y, z);
                        const bool posX = !isSolid(static_cast<int>(x) + 1, y, z);
                        const bool negY = !isSolid(x, static_cast<int>(y) - 1, z);
                        const bool posY = !isSolid(x, static_cast<int>(y) + 1, z);
                        const bool negZ = !isSolid(x, y, static_cast<int>(z) - 1);
                        const bool posZ = !isSolid(x, y, static_cast<int>(z) + 1);
                        if (negX || posX || negY || posY || negZ || posZ)
                        {
                            AddVoxelProxyFaces(vertices, indices, {x, y, z}, minBounds, cellSize,
                                               negX, posX, negY, posY, negZ, posZ);
                        }
                    }
                }
            }

            return Model::CreateFromGeometry(splatData.name + "_splat_proxy", std::move(vertices), std::move(indices), false);
        }

        uint32_t EnsureSplatProxyMaterial(std::vector<FMaterial>& materials)
        {
            for (uint32_t i = 0; i < materials.size(); ++i)
            {
                if (materials[i].name_ == "__splat_proxy_material")
                {
                    return i;
                }
            }
            materials.push_back({Material::Lambertian(glm::vec3(0.65f, 0.68f, 0.72f)), "__splat_proxy_material"});
            return static_cast<uint32_t>(materials.size() - 1);
        }
    }


    void BuildGaussianSplatProxy(const std::shared_ptr<Node>& sourceNode,
                                 const std::shared_ptr<Runtime::GaussianSplatComponent>& component,
                                 std::vector<std::shared_ptr<Node>>& nodes,
                                 std::vector<Model>& models,
                                 std::vector<FMaterial>& materials)
    {
        if (!sourceNode || !component || !component->GetData())
        {
            return;
        }
        const FProxyBuildConfig config = ResolveProxyBuildConfig();
        if (!config.enable)
        {
            return;
        }

        glm::uvec3 gridDim(0u);
        Model proxyModel = BuildSplatProxyMesh(*component->GetData(), *component, config, gridDim);
        if (proxyModel.NumberOfIndices() == 0 || proxyModel.NumberOfVertices() == 0)
        {
            SPDLOG_WARN("[SplatProxy] skipped {}: empty proxy mesh at grid {}x{}x{}",
                        component->GetData()->name, gridDim.x, gridDim.y, gridDim.z);
            return;
        }

        const uint32_t proxyMaterialId = EnsureSplatProxyMaterial(materials);
        const uint32_t proxyModelId = static_cast<uint32_t>(models.size());
        models.push_back(std::move(proxyModel));
        uint32_t proxyNodeId = 0;
        for (const auto& node : nodes)
        {
            if (node) proxyNodeId = std::max(proxyNodeId, node->GetInstanceId() + 1u);
        }

        auto proxyNode = Node::CreateNode(component->GetData()->name + "_splat_proxy", glm::vec3(0.0f),
                                          glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f), proxyNodeId);
        auto render = std::make_shared<Runtime::RenderComponent>();
        render->SetModelId(proxyModelId);
        render->SetVisible(component->GetVisible());
        render->SetMainVisible(config.debugVisible);
        render->SetCastShadows(component->GetCastShadow() && config.shadowEnable);
        render->SetRayTraceVisible(component->GetRayTraceOccluder() && config.rayOcclusionEnable);
        render->SetReceiveGI(component->GetRayTraceOccluder() && config.rayOcclusionEnable);
        render->SetRayCastVisible(component->GetVisible() && component->GetRayCastVisible());
        std::array<uint32_t, 16> proxyMaterials{};
        proxyMaterials.fill(proxyMaterialId);
        render->SetMaterials(proxyMaterials);
        proxyNode->AddComponent(render);
        proxyNode->SetTag("__splat_proxy");
        proxyNode->SetLayer("__internal");
        proxyNode->SetSceneReferenceOwnerProxyId(sourceNode->GetInstanceId());
        proxyNode->SetParent(sourceNode);
        nodes.push_back(proxyNode);

        SPDLOG_INFO("[SplatProxy] {} grid {}x{}x{} -> {} vertices, {} triangles, model {}, node {}",
                    component->GetData()->name, gridDim.x, gridDim.y, gridDim.z,
                    models[proxyModelId].NumberOfVertices(), models[proxyModelId].NumberOfIndices() / 3u,
                    proxyModelId, proxyNodeId);
    }
}
