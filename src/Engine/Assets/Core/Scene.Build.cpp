// Scene build/reload: node graph (re)build, mesh/material GPU buffer rebuild.
// Split from Scene.cpp; same class, separate TU.
#include "Engine/Assets/Core/Scene.hpp"
#include <glm/detail/type_half.hpp>
#include <meshoptimizer.h>
#include <tiny_gltf.h>
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Savers/FSceneSaver.h"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Vulkan/BufferUtil.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/EnvironmentComponent.h"
#include "Engine/Runtime/Components/GaussianSplatComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Runtime/Profiling/FrameProfiler.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <algorithm>
#include <cmath>
#include <entt/meta/factory.hpp>
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
                const Runtime::Config::UserSettings& settings = NextEngine::GetInstance()->GetUserSettings();
                config.enable = settings.SplatProxyEnable;
                config.shadowEnable = settings.SplatShadowEnable;
                config.rayOcclusionEnable = settings.SplatRayOcclusionEnable;
                config.debugVisible = settings.SplatProxyDebugVisible;
                config.gridMax = std::clamp(settings.SplatProxyGridMax, 8u, 128u);
                config.sigma = std::clamp(settings.SplatProxySigma, 1.0f, 4.0f);
                config.isoThreshold = std::clamp(settings.SplatProxyIsoThreshold, 0.01f, 0.95f);
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

    void Scene::EnsureGaussianSplatProxyMeshes()
    {
        const FProxyBuildConfig config = ResolveProxyBuildConfig();
        if (!config.enable || gaussianSplats_.empty())
        {
            return;
        }

        uint32_t proxyMaterialId = kInvalidId;
        for (FGaussianSplatData& splat : gaussianSplats_)
        {
            if (splat.proxyModelId != kInvalidId && splat.proxyNodeInstanceId != kInvalidId &&
                splat.proxyModelId < models_.size() && GetNodeSharedByInstanceId(splat.proxyNodeInstanceId))
            {
                continue;
            }

            auto sourceNode = GetNodeSharedByInstanceId(splat.nodeInstanceId);
            auto* component = sourceNode ? sourceNode->GetComponentPtr<Runtime::GaussianSplatComponent>() : nullptr;
            if (!sourceNode || !component)
            {
                continue;
            }

            glm::uvec3 gridDim(0u);
            Model proxyModel = BuildSplatProxyMesh(splat, *component, config, gridDim);
            if (proxyModel.NumberOfIndices() == 0 || proxyModel.NumberOfVertices() == 0)
            {
                SPDLOG_WARN("[SplatProxy] skipped {}: empty proxy mesh at grid {}x{}x{}",
                            splat.name, gridDim.x, gridDim.y, gridDim.z);
                continue;
            }

            if (proxyMaterialId == kInvalidId)
            {
                proxyMaterialId = EnsureSplatProxyMaterial(materials_);
            }

            const uint32_t proxyModelId = static_cast<uint32_t>(models_.size());
            models_.push_back(std::move(proxyModel));

            const uint32_t proxyNodeId = GenerateInstanceId();
            auto proxyNode = Node::CreateNode(splat.name + "_splat_proxy",
                                              glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                              glm::vec3(1.0f), proxyNodeId);
            auto render = std::make_shared<Runtime::RenderComponent>();
            render->SetModelId(proxyModelId);
            render->SetVisible(component->GetVisible());
            render->SetMainVisible(config.debugVisible);
            render->SetCastShadows(component->GetCastShadow() && config.shadowEnable);
            render->SetRayTraceVisible(component->GetRayTraceOccluder() && config.rayOcclusionEnable);
            render->SetReceiveGI(component->GetRayTraceOccluder() && config.rayOcclusionEnable);
            render->SetRayCastVisible(false);
            std::array<uint32_t, 16> materials{};
            materials.fill(proxyMaterialId);
            render->SetMaterials(materials);
            proxyNode->AddComponent(render);
            proxyNode->SetTag("__splat_proxy");
            proxyNode->SetLayer("__internal");
            proxyNode->SetSceneReferenceOwnerProxyId(sourceNode->GetInstanceId());
            proxyNode->SetParent(sourceNode);
            CacheEnvironmentComponentFromNode(proxyNode.get());
            nodes_.push_back(proxyNode);
            RegisterNodeIndex(proxyNode);

            splat.proxyModelId = proxyModelId;
            splat.proxyNodeInstanceId = proxyNodeId;
            splat.proxyGridDim = gridDim;
            SPDLOG_INFO("[SplatProxy] {} grid {}x{}x{} -> {} vertices, {} triangles, model {}, node {}",
                        splat.name, gridDim.x, gridDim.y, gridDim.z,
                        models_[proxyModelId].NumberOfVertices(),
                        models_[proxyModelId].NumberOfIndices() / 3u, proxyModelId, proxyNodeId);
        }
    }

    bool Scene::EnsureGpuDrivenBufferCapacity(Vulkan::CommandPool& commandPool)
    {
        const uint32_t requiredCapacity = requiredGpuDrivenTriangleCapacity_;
        if (requiredCapacity <= maxSceneTriangles_)
        {
            return false;
        }

        const uint64_t doubledCapacity = static_cast<uint64_t>(maxSceneTriangles_) * 2u;
        const uint64_t grownCapacity = std::max<uint64_t>(requiredCapacity, doubledCapacity);
        if (grownCapacity > std::numeric_limits<uint32_t>::max())
        {
            throw std::overflow_error("GPU-driven scene triangle capacity exceeds uint32_t");
        }

        maxSceneTriangles_ = static_cast<uint32_t>(grownCapacity);
        const VkBufferUsageFlags flags =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderPrim", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(uint32_t) * maxSceneTriangles_, softMeshShaderPrimBuffer_, softMeshShaderPrimBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderShadowPrim", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(uint32_t) * maxSceneTriangles_ * kSunShadowCascadeCount,
            softMeshShaderShadowPrimBuffer_, softMeshShaderShadowPrimBufferMemory_);

        const std::vector<Assets::SoftMeshShaderResources> resources = {
            {
                softMeshShaderPrimBuffer_->GetDeviceAddress(),
                softMeshShaderShadowPrimBuffer_->GetDeviceAddress(),
                softMeshShaderVisibleItemBuffer_->GetDeviceAddress(),
                softMeshShaderDrawArgBuffer_->GetDeviceAddress(),
                softMeshShaderDispatchArgBuffer_->GetDeviceAddress(),
                softMeshShaderCounterBuffer_->GetDeviceAddress(),
            },
        };
        Vulkan::BufferUtil::CreateDeviceBuffer(
            commandPool, "SoftMeshShaderResources", flags, resources,
            softMeshShaderResourcesBuffer_, softMeshShaderResourcesBufferMemory_);
        SPDLOG_INFO("GPU-driven triangle capacity grown to {}", maxSceneTriangles_);
        return true;
    }

    void Scene::Reload(std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
                       std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                       std::vector<AnimationTrack>& tracks)
    {
        nodes_ = std::move(nodes);
        RebuildNodeIndex();
        RefreshEnvironmentComponentCache();
        models_ = std::move(models);
        gaussianSplats_.clear();
        materials_ = std::move(materials);
        lights_ = std::move(lights);
        tracks_ = std::move(tracks);
        selectionState_.Clear();
        hoveredId_ = SceneSelectionState::invalidNodeId;
        lockedIds_.clear();
        nodeProxys.clear();
        indirectDrawBatchCount_ = 0;
        indicesCount_ = 0;
        verticeCount_ = 0;
        lightCount_ = 0;
        gpuDrivenStat_ = {};
        shadowGpuDrivenStats_.fill({});
        sceneAABBMin_ = glm::vec3(0.0f);
        sceneAABBMax_ = glm::vec3(0.0f);
        sceneDirty_ = true;
        sceneDirtyForCpuAS_ = true;
        materialDirty_ = true;
    }

    std::shared_ptr<Node> Scene::Append(const std::string& sceneName, std::vector<std::shared_ptr<Node>>& nodes,
                                        std::vector<Model>& models, std::vector<FMaterial>& materials,
                                        std::vector<LightObject>& lights, std::vector<AnimationTrack>& tracks,
                                        const std::vector<Skeleton>& skeletons)
    {
        uint32_t modelOffset = static_cast<uint32_t>(models_.size());
        uint32_t materialOffset = static_cast<uint32_t>(materials_.size());

        // Ensure unique root node name
        std::string uniqueName = sceneName;
        int counter = 1;
        while (GetNode(uniqueName) != nullptr)
        {
            uniqueName = fmt::format("{}_{}", sceneName, counter++);
        }

        // Create a root node for the appended scene
        uint32_t currentMaxId = GenerateInstanceId();
        auto rootNode = Node::CreateNode(uniqueName, glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1), currentMaxId++);

        // Update IDs for all new nodes (assuming nodes is a flat list of all new nodes)
        for (auto& node : nodes)
        {
            if (auto* environment = node->GetComponentPtr<Runtime::EnvironmentComponent>())
            {
                if (environmentComponent_ == nullptr)
                {
                    environmentComponent_ = environment;
                }
                continue;
            }

            node->SetInstanceId(currentMaxId++);

            // Update RenderComponent
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (render)
            {
                if (render->GetModelId() != -1)
                {
                    render->SetModelId(render->GetModelId() + modelOffset);
                }
                auto mats = render->GetMaterials();
                for (auto& matId : mats)
                {
                    matId += materialOffset;
                }
                render->SetMaterials(mats);

                // Handle Skeletons
                if (render->GetSkinIndex() != -1 && render->GetSkinIndex() < skeletons.size())
                {
                    auto comp = std::make_shared<Runtime::SkinnedMeshComponent>(skeletons[render->GetSkinIndex()]);
                    comp->AddAnimations(tracks);
                    comp->PlayAnimation("Default");
                    node->AddComponent(comp);
                }
            }

            // Reparent roots (nodes that don't have a parent in the new scene hierarchy)
            if (node->GetParent() == nullptr)
            {
                node->SetParent(rootNode);
            }
        }

        // Merge vectors
        // models_.insert(models_.end(), models.begin(), models.end());
        models_.reserve(models_.size() + models.size());
        for (auto& model : models)
        {
            models_.push_back(std::move(model));
        }

        materials_.insert(materials_.end(), materials.begin(), materials.end());
        lights_.insert(lights_.end(), lights.begin(), lights.end());
        tracks_.insert(tracks_.end(), tracks.begin(), tracks.end());

        // Add new nodes to scene nodes
        for (auto& node : nodes)
        {
            if (node->GetComponentPtr<Runtime::EnvironmentComponent>() == nullptr)
            {
                nodes_.push_back(node);
                RegisterNodeIndex(node);
            }
        }

        // Add root node to scene
        CacheEnvironmentComponentFromNode(rootNode.get());
        nodes_.push_back(rootNode);
        RegisterNodeIndex(rootNode);

        // Mark dirty
        MarkDirty();

        return rootNode;
    }

    void Scene::RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        EnsureGaussianSplatProxyMeshes();

        nodeProxys.clear();
        indirectDrawBatchCount_ = 0;
        indicesCount_ = 0;
        verticeCount_ = 0;
        lightCount_ = 0;
        gpuDrivenStat_ = {};
        shadowGpuDrivenStats_.fill({});

        if (enableCpuAcceleration_)
        {
            cpuAccelerationStructure_.InitBVH(*this);
        }

        // force static flag
        std::function<void(Node*)> SetKinematicRecursive = [&](Node* node)
        {
            if (node == nullptr)
                return;
            if (auto phys = node->GetComponent<Runtime::PhysicsComponent>())
            {
                phys->SetMobility(Node::ENodeMobility::Kinematic);
            }
            else
            {
                auto newPhys = std::make_shared<Runtime::PhysicsComponent>();
                newPhys->SetMobility(Node::ENodeMobility::Kinematic);
                node->AddComponent(newPhys);
            }
            for (auto& child : node->Children())
            {
                SetKinematicRecursive(child.get());
            }
        };

        for (auto& track : tracks_)
        {
            Node* node = GetNode(track.NodeName_);
            SetKinematicRecursive(node);
        }

        // calculate the scene aabb
        sceneAABBMin_ = {FLT_MAX, FLT_MAX, FLT_MAX};
        sceneAABBMax_ = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        bool hasSceneBounds = false;
        for (auto& node : nodes_)
        {
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (render && render->GetVisible() && render->GetModelId() != -1)
            {
                glm::vec3 localaabbMin = models_[render->GetModelId()].GetLocalAABBMin();
                glm::vec3 localaabbMax = models_[render->GetModelId()].GetLocalAABBMax();

                auto& worldMtx = node->WorldTransform();

                glm::vec3 corners[8];
                corners[0] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMin.y, localaabbMin.z, 1.0f));
                corners[1] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMin.y, localaabbMin.z, 1.0f));
                corners[2] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMax.y, localaabbMin.z, 1.0f));
                corners[3] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMax.y, localaabbMin.z, 1.0f));
                corners[4] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMin.y, localaabbMax.z, 1.0f));
                corners[5] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMin.y, localaabbMax.z, 1.0f));
                corners[6] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMax.y, localaabbMax.z, 1.0f));
                corners[7] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMax.y, localaabbMax.z, 1.0f));

                // Find the new min and max from the transformed corners
                glm::vec3 worldAABBMin = corners[0];
                glm::vec3 worldAABBMax = corners[0];
                for (int i = 1; i < 8; ++i)
                {
                    worldAABBMin = glm::min(worldAABBMin, corners[i]);
                    worldAABBMax = glm::max(worldAABBMax, corners[i]);
                }

                // Update the scene's AABB
                sceneAABBMin_ = glm::min(sceneAABBMin_, worldAABBMin);
                sceneAABBMax_ = glm::max(sceneAABBMax_, worldAABBMax);
                hasSceneBounds = true;
            }
        }
        if (!hasSceneBounds)
        {
            sceneAABBMin_ = glm::vec3(0.0f);
            sceneAABBMax_ = glm::vec3(0.0f);
        }

        // static mesh to jolt mesh shape
        if (NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine())
        {
            cachedMeshShapes_.clear();
            for (auto& model : models_)
            {
                if (model.NumberOfIndices() < 65535 * 3 && model.NumberOfIndices() > 0)
                {
                    cachedMeshShapes_.push_back(
                        NextRefConst<NextMeshShapeSettings>(physicsEngine->CreateMeshShape(model)));
                }
                else
                {
                    cachedMeshShapes_.push_back(NextRefConst<NextMeshShapeSettings>(nullptr));
                }
            }

            for (auto& node : nodes_)
            {
                auto render = node->GetComponent<Runtime::RenderComponent>();
                // bind the mesh shape to the node
                if (render && render->GetRayCastVisible() &&
                    render->GetModelId() < cachedMeshShapes_.size() &&
                    cachedMeshShapes_[render->GetModelId()])
                {
                    auto phys = node->GetComponent<Runtime::PhysicsComponent>();
                    Node::ENodeMobility mobility = phys ? phys->GetMobility() : Node::ENodeMobility::Static;

                    if (mobility != Node::ENodeMobility::Dynamic)
                    {
                        NextMotionType motionType = mobility == Node::ENodeMobility::Static ? NextMotionType::Static
                                                                                            : NextMotionType::Kinematic;
                        NextObjectLayer layer =
                            mobility == Node::ENodeMobility::Static ? NextLayers::NON_MOVING : NextLayers::MOVING;

                        bool validShape = false;
                        if (cachedMeshShapes_[render->GetModelId()].GetPtr() &&
                            cachedMeshShapes_[render->GetModelId()]->mIndexedTriangles.size() > 0)
                            validShape = true;

                        if (validShape)
                        {
                            glm::vec3 worldScale = node->WorldScale();
                            if (glm::length(worldScale) > 0.01f && glm::abs(worldScale.x) > 0.001 &&
                                glm::abs(worldScale.y) > 0.001 && glm::abs(worldScale.z) > 0.001)
                            {
                                NextBodyID id = physicsEngine->CreateMeshBody(
                                    cachedMeshShapes_[render->GetModelId()], node->WorldTranslation(),
                                    node->WorldRotation(), node->WorldScale(), motionType, layer);

                                if (!phys)
                                {
                                    phys = std::make_shared<Runtime::PhysicsComponent>();
                                    phys->SetMobility(mobility);
                                    node->AddComponent(phys);
                                }
                                phys->BindPhysicsBody(id);

                                physicsEngine->SetBodyActive(id, render->GetVisible());
                            }
                        }
                    }
                }
            }

            // create 6 plane bodys, it makes negtive space, so keep the bottom plane only
            // physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(1,0,0), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMax_, glm::vec3(-1,0,0), NextMotionType::Static);
            if (hasSceneBounds)
            {
                physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(0, 1, 0), NextMotionType::Static);
            }
            // physicsEngine->CreatePlaneBody(sceneAABBMax_, glm::vec3(0,-1,0), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(0,0,1), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMax_, glm::vec3(0,0,-1), NextMotionType::Static);
        }

        // 重建universe mesh buffer, 这个可以比较静态
        std::vector<GPUVertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<glm::vec4> allWeights;
        std::vector<glm::uvec4> allJoints;
        std::vector<uint32_t> reorders;
        std::vector<uint32_t> primitiveIndices;

        offsets_.clear();
        for (auto& model : models_)
        {
            // Remember the index, vertex offsets.
            const auto indexOffset = static_cast<uint32_t>(indices.size());
            const auto vertexOffset = static_cast<uint32_t>(vertices.size());
            const auto reorderOffset = static_cast<uint32_t>(reorders.size());

            // cpu vertex to gpu vertex
            for (auto& vertex : model.CPUVertices())
            {
                vertices.push_back(MakeVertex(vertex));
            }

            const auto& weights = model.CPUWeights();
            const auto& joints = model.CPUJoints();
            if (!weights.empty() && weights.size() == model.CPUVertices().size())
            {
                allWeights.insert(allWeights.end(), weights.begin(), weights.end());
                allJoints.insert(allJoints.end(), joints.begin(), joints.end());
            }
            else
            {
                allWeights.resize(allWeights.size() + model.CPUVertices().size(), glm::vec4(0));
                allJoints.resize(allJoints.size() + model.CPUVertices().size(), glm::uvec4(0));
            }

            const std::vector<Vertex>& localVertices = model.CPUVertices();
            const std::vector<uint32_t>& localIndices = model.CPUIndices();

            std::vector<std::vector<uint32_t>> slicedIndices;
            constexpr uint32_t maxIndicesPerSlice = 65535 * 3;

            // 将localIndices分片，每片最多65535*3个索引
            for (size_t i = 0; i < localIndices.size(); i += maxIndicesPerSlice)
            {
                size_t endIndex = std::min(i + maxIndicesPerSlice, localIndices.size());
                slicedIndices.emplace_back(localIndices.begin() + i, localIndices.begin() + endIndex);
            }

            int emptySection = 10 - int(slicedIndices.size());
            int processSection = std::min(int(slicedIndices.size()), 10);

            for (int slice = 0; slice < processSection; ++slice)
            {
                const auto localIndexOffset = static_cast<uint32_t>(indices.size());
                const auto localReorderOffset = static_cast<uint32_t>(reorders.size());

                const auto& localIndiceCount = slicedIndices[slice];
                uint32_t realSize = uint32_t(localIndiceCount.size());
                offsets_.push_back({localIndexOffset, realSize, vertexOffset, model.NumberOfVertices(),
                                    vec4(model.GetLocalAABBMin(), 1), vec4(model.GetLocalAABBMax(), 1), 0, 0,
                                    localReorderOffset, 0});

                std::vector<uint32_t> provoke(localIndiceCount.size());
                std::vector<uint32_t> reorder(localVertices.size() + localIndiceCount.size() / 3);
                std::vector<uint32_t> primIndices(localIndiceCount.size());

                if (localIndiceCount.size() > 0)
                {
                    reorder.resize(meshopt_generateProvokingIndexBuffer(&provoke[0], &reorder[0], &localIndiceCount[0],
                                                                        realSize, localVertices.size()));
                }

                for (size_t i = 0; i < provoke.size(); ++i)
                {
                    primIndices[i] += reorder[provoke[i]];
                }

                // reorder is absolute vertex index
                for (size_t i = 0; i < reorder.size(); ++i)
                {
                    reorder[i] += vertexOffset;
                }

                indices.insert(indices.end(), provoke.begin(), provoke.end());
                reorders.insert(reorders.end(), reorder.begin(), reorder.end());
                primitiveIndices.insert(primitiveIndices.end(), primIndices.begin(), primIndices.end());
            }

            if (emptySection < 0)
            {
                SPDLOG_WARN("more than 10 sections in model");
            }
            for (int slice = 0; slice < emptySection; ++slice)
            {
                offsets_.push_back({indexOffset, 0, vertexOffset, model.NumberOfVertices(),
                                    vec4(model.GetLocalAABBMin(), 1), vec4(model.GetLocalAABBMax(), 1), 0, 0,
                                    reorderOffset, 0});
            }

            model.SetSectionCount(processSection);

            // 在编辑器模式下保留CPU网格数据用于场景保存功能
            if (!GOption->KeepCPUMeshData)
            {
                model.FreeMemory();
            }
        }

        int flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        // this buffer now, no support extended
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices",
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices,
                                               vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | flags,
                                               indices, indexBuffer_, indexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Reorder", flags, reorders, reorderBuffer_,
                                               reorderBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "PrimAddress",
                                               VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, primitiveIndices,
                                               primAddressBuffer_, primAddressBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_,
                                               offsetBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights_, lightBuffer_, lightBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SkinWeights", flags, allWeights, skinWeightBuffer_,
                                               skinWeightBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SkinJoints", flags, allJoints, skinJointBuffer_,
                                               skinJointBufferMemory_);

        // 一些数据
        lightCount_ = static_cast<uint32_t>(lights_.size());
        indicesCount_ = static_cast<uint32_t>(indices.size());
        verticeCount_ = static_cast<uint32_t>(vertices.size());

        // The GPU-driven primitive buffers contain expanded triangles per instance, not just
        // the unique model geometry stored in the index buffer. Sizing them from indicesCount_
        // silently dropped later instances when several nodes shared a model.
        sceneDirty_ = true;
        UpdateNodesGpuDriven();
        maxSceneTriangles_ = requiredGpuDrivenTriangleCapacity_;

        const VkBufferUsageFlags softMeshShaderFlags =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderPrim", softMeshShaderFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(uint32_t) * maxSceneTriangles_, softMeshShaderPrimBuffer_, softMeshShaderPrimBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderShadowPrim", softMeshShaderFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(uint32_t) * maxSceneTriangles_ * kSunShadowCascadeCount,
            softMeshShaderShadowPrimBuffer_, softMeshShaderShadowPrimBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderVisibleItems", softMeshShaderFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(Assets::SoftMeshShaderVisibleItem) * kMaxIndirectDrawCount * kSoftMeshShaderDrawSlotCount,
            softMeshShaderVisibleItemBuffer_, softMeshShaderVisibleItemBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderDrawArgs", softMeshShaderFlags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(VkDrawIndirectCommand) * kSoftMeshShaderDrawSlotCount,
            softMeshShaderDrawArgBuffer_, softMeshShaderDrawArgBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderDispatchArgs", softMeshShaderFlags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(VkDispatchIndirectCommand) * kSoftMeshShaderDrawSlotCount,
            softMeshShaderDispatchArgBuffer_, softMeshShaderDispatchArgBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderCounters", softMeshShaderFlags | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(uint32_t) * kSoftMeshShaderDrawSlotCount * 2u,
            softMeshShaderCounterBuffer_, softMeshShaderCounterBufferMemory_);

        const std::vector<Assets::SoftMeshShaderResources> softMeshShaderResources = {
            {
                softMeshShaderPrimBuffer_->GetDeviceAddress(),
                softMeshShaderShadowPrimBuffer_->GetDeviceAddress(),
                softMeshShaderVisibleItemBuffer_->GetDeviceAddress(),
                softMeshShaderDrawArgBuffer_->GetDeviceAddress(),
                softMeshShaderDispatchArgBuffer_->GetDeviceAddress(),
                softMeshShaderCounterBuffer_->GetDeviceAddress(),
            },
        };
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SoftMeshShaderResources", softMeshShaderFlags, softMeshShaderResources,
                                               softMeshShaderResourcesBuffer_, softMeshShaderResourcesBufferMemory_);

        UpdateAllMaterials();
        UpdateNodesGpuDriven();
        MarkDirty();

        if (enableCpuAcceleration_ && ambientArenaBufferMemory_ &&
            (!NextEngine::GetInstance() ||
             NextEngine::GetInstance()->GetRenderer().ActiveRendererRequirements().requestAmbientCube))
        {
            cpuAccelerationStructure_.AsyncProcessFull(*this, ambientArenaBufferMemory_.get(), false);
        }
    }

}
