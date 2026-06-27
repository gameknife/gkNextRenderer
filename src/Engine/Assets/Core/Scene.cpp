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
#include "Engine/Runtime/Components/GaussianSplatComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Utilities/Exception.hpp"

#include <algorithm>
#include <cmath>
#include <entt/meta/factory.hpp>

namespace Assets
{
    namespace
    {
        constexpr VkDeviceSize perAmbientCascadeCount =
            static_cast<VkDeviceSize>(Assets::CUBE_SIZE_XY) * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;

        uint32_t ResolveAmbientPoolBricksPerCascade(float poolBrickRatio)
        {
            const float clampedRatio = std::clamp(poolBrickRatio, 0.0f, 1.0f);
            const auto requestedBricks = static_cast<uint32_t>(std::ceil(
                static_cast<float>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) * clampedRatio));
            return std::clamp(requestedBricks, 1u,
                              static_cast<uint32_t>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE));
        }

        // Byte layout of the ambient arena for a given allocated cascade capacity. Cubes and Voxels
        // scale with the capacity; Pages is a fixed world grid; CubesPong is a single cascade.
        // With cascadeCapacity == CUBE_CASCADE_MAX this reproduces the
        // compile-time GPU_SCENE_AMBIENT_*_OFFSET constants (asserted below), so right-sizing the
        // capacity (Phase 2) only shrinks the arena without touching the GPU-visible struct layout.
        struct AmbientArenaLayout
        {
            VkDeviceSize cubesOffset;
            VkDeviceSize voxelsOffset;
            VkDeviceSize pagesOffset;
            VkDeviceSize pongOffset;
            VkDeviceSize brickTableOffset;
            VkDeviceSize activeBrickListOffset;
            VkDeviceSize residencyOffset;
            VkDeviceSize totalSize;
        };

        // poolBricksPerCascade sizes the sparse cube pool (Cubes) and its ping-pong copy (CubesPong);
        // Voxels stay dense (full per-cascade). With poolBricksPerCascade == BRICKS_PER_CASCADE the
        // cube pool equals the dense grid (Phase 3a behaviour).
        constexpr AmbientArenaLayout ComputeAmbientArenaLayout(uint32_t cascadeCapacity, uint32_t poolBricksPerCascade)
        {
            const VkDeviceSize poolCubesPerCascade =
                static_cast<VkDeviceSize>(poolBricksPerCascade) * Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME;
            AmbientArenaLayout layout{};
            layout.cubesOffset = 0;
            layout.voxelsOffset = layout.cubesOffset +
                static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_CUBE_SIZE) * poolCubesPerCascade * cascadeCapacity;
            layout.pagesOffset = layout.voxelsOffset +
                static_cast<VkDeviceSize>(Assets::GPU_SCENE_VOXEL_DATA_SIZE) * perAmbientCascadeCount * cascadeCapacity;
            layout.pongOffset = layout.pagesOffset +
                static_cast<VkDeviceSize>(Assets::GPU_SCENE_PAGE_INDEX_SIZE) * Assets::ACGI_PAGE_COUNT *
                    Assets::ACGI_PAGE_COUNT;
            layout.brickTableOffset = layout.pongOffset +
                static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_CUBE_SIZE) * poolCubesPerCascade;
            // Phase 3: per-cascade brick -> pool slot table (uint per brick), sized to the capacity.
            layout.activeBrickListOffset = layout.brickTableOffset +
                sizeof(uint32_t) * static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) *
                    cascadeCapacity;
            // Phase 3c: per-cascade active brick list, one packed brick-linear id per pool slot.
            layout.residencyOffset = layout.activeBrickListOffset +
                sizeof(uint32_t) * static_cast<VkDeviceSize>(poolBricksPerCascade) * cascadeCapacity;
            layout.totalSize = layout.residencyOffset +
                sizeof(Assets::AmbientBrickResidency) *
                    static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) * cascadeCapacity;
            return layout;
        }

        static_assert(sizeof(Assets::NodeProxy) == Assets::GPU_SCENE_NODE_PROXY_SIZE);
        static_assert(sizeof(Assets::Material) == Assets::GPU_SCENE_MATERIAL_SIZE);
        static_assert(sizeof(Assets::GPUDrivenStat) == Assets::GPU_SCENE_GPU_DRIVEN_STAT_SIZE);
        static_assert(sizeof(Assets::SoftMeshShaderVisibleItem) == 16);
        static_assert(sizeof(Assets::SoftMeshShaderResources) == 48);
        static_assert(sizeof(Assets::SphericalHarmonics) == Assets::GPU_SCENE_SPHERICAL_HARMONICS_SIZE);
        static_assert(sizeof(Assets::AmbientCube) == Assets::GPU_SCENE_AMBIENT_CUBE_SIZE);
        static_assert(sizeof(Assets::AmbientBrickResidency) == 16);
        static_assert(sizeof(Assets::AmbientResources) == 80);
        static_assert(sizeof(Assets::VoxelData) == Assets::GPU_SCENE_VOXEL_DATA_SIZE);
        static_assert(sizeof(Assets::PageIndex) == Assets::GPU_SCENE_PAGE_INDEX_SIZE);
        static_assert(Assets::GPU_SCENE_AMBIENT_PER_CASCADE_COUNT == perAmbientCascadeCount);
        static_assert(Assets::GPU_SCENE_AMBIENT_CASCADE_MAX == Assets::CUBE_CASCADE_MAX);
        static_assert(Assets::GPU_SCENE_ACGI_PAGE_COUNT == Assets::ACGI_PAGE_COUNT);
        // The runtime ComputeAmbientArenaLayout (which also covers the Phase 3 brick table) is now
        // the authoritative arena layout; the compile-time
        // GPU_SCENE_AMBIENT_* offsets remain only as a reference for the dense cube/voxel sub-regions.
        static_assert(Assets::GPU_SCENE_AMBIENT_BRICKS_X * Assets::GPU_SCENE_AMBIENT_BRICKS_Y *
                          Assets::GPU_SCENE_AMBIENT_BRICKS_Z ==
                      Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
        static_assert(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE * Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME ==
                      Assets::GPU_SCENE_AMBIENT_PER_CASCADE_COUNT);
        static_assert(sizeof(Assets::GPUScene) == 128);
    }

    void Scene::RegisterReflection()
    {
        using namespace entt::literals;

        entt::meta_factory<Assets::Scene>()
            .type("Scene"_hs)
            .func<&Assets::Scene::GetIndicesCount>("GetIndicesCount")
            .func<&Assets::Scene::FindNodeIdWithComponent>("FindNodeIdWithComponent")
            .func<&Assets::Scene::GetNodeById>("GetNodeById");
    }

    int32_t Scene::FindNodeIdWithComponent(const std::string& componentType) const
    {
        for (const auto& node : nodes_)
        {
            if (!node)
            {
                continue;
            }

            for (const auto& component : node->GetComponents())
            {
                if (component && component->GetTypeName() == componentType)
                {
                    return static_cast<int32_t>(node->GetInstanceId());
                }
            }
        }

        return -1;
    }

    Node* Scene::GetNodeById(uint32_t nodeId)
    {
        return GetNodeByInstanceId(nodeId);
    }

    Scene::Scene(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        int flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        const bool allocateAmbientCube =
            !NextEngine::GetInstance() ||
            NextEngine::GetInstance()->GetRenderer().RegisteredRendererRequirements().requestAmbientCube;
        // Phase 2 right-sizing: allocate Cubes/Voxels for the configured cascade count (default 3)
        // rather than always CUBE_CASCADE_MAX (4). The capacity is fixed for this Scene's lifetime;
        // consumers clamp the effective cascade count to it (a runtime cvar increase only takes
        // effect after a scene reload, but never reads/writes outside the allocation).
        uint32_t configuredCascadeCount = Assets::CUBE_CASCADE_MAX;
        if (NextEngine::GetInstance())
        {
            configuredCascadeCount = Assets::SanitizeAmbientCubeCascadeCount(
                NextEngine::GetInstance()->GetUserSettings().AmbientCubeCascadeCount);
        }
        const uint32_t ambientCubeCascadeCapacity = allocateAmbientCube ? configuredCascadeCount : 1u;
        ambientCubeCascadeCapacity_ = ambientCubeCascadeCapacity;
        float poolBrickRatio = 0.66f;
        if (NextEngine::GetInstance())
        {
            poolBrickRatio = NextEngine::GetInstance()->GetUserSettings().AmbientCubePoolBrickRatio;
        }
        poolBricksPerCascade_ = ResolveAmbientPoolBricksPerCascade(poolBrickRatio);
        const AmbientArenaLayout ambientLayout =
            ComputeAmbientArenaLayout(ambientCubeCascadeCapacity, poolBricksPerCascade_);
        ambientVoxelsOffset_ = static_cast<size_t>(ambientLayout.voxelsOffset);
        ambientPagesOffset_ = static_cast<size_t>(ambientLayout.pagesOffset);
        ambientPongOffset_ = static_cast<size_t>(ambientLayout.pongOffset);
        ambientBrickTableOffset_ = static_cast<size_t>(ambientLayout.brickTableOffset);
        ambientActiveBrickListOffset_ = static_cast<size_t>(ambientLayout.activeBrickListOffset);
        ambientResidencyOffset_ = static_cast<size_t>(ambientLayout.residencyOffset);

        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SceneDynamic", flags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            Assets::GPU_SCENE_DYNAMIC_SIZE, sceneDynamicBuffer_, sceneDynamicBufferMemory_);

        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "AmbientArena", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            ambientLayout.totalSize, ambientArenaBuffer_, ambientArenaBufferMemory_);

        if (allocateAmbientCube)
        {
            const double mb = 1024.0 * 1024.0;
            SPDLOG_INFO("[AmbientArena] total {:.1f} MB | cascades {} | pool {}/{} bricks/cascade | "
                        "cubes {:.1f} voxels {:.1f} pong {:.1f} MB",
                        ambientLayout.totalSize / mb, ambientCubeCascadeCapacity, poolBricksPerCascade_,
                        static_cast<uint32_t>(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE),
                        (ambientLayout.voxelsOffset - ambientLayout.cubesOffset) / mb,
                        (ambientLayout.pagesOffset - ambientLayout.voxelsOffset) / mb,
                        (ambientLayout.brickTableOffset - ambientLayout.pongOffset) / mb);
        }

        // Ambient GI resource table (see AmbientResources in BasicTypes.slang). GPUScene carries a
        // single AmbientBase pointer to this table rather than inlining every region address, which
        // keeps the push constant at 128B and decouples the GPU-visible layout from the compile-time
        // GPU_SCENE_AMBIENT_*_OFFSET constants. Region addresses are derived from the arena base once
        // here (they are stable for the arena's lifetime). When ambient cubes are not requested the
        // arena is shrunk to one cascade; the table still stores the nominal offsets, but the shaders
        // in NoAmbient paths never dereference them.
        {
            const VkDeviceAddress arenaBase = ambientArenaBuffer_->GetDeviceAddress();
            AmbientResources resources{};
            resources.Cubes = arenaBase + ambientLayout.cubesOffset;
            resources.Voxels = arenaBase + ambientLayout.voxelsOffset;
            resources.Pages = arenaBase + ambientLayout.pagesOffset;
            resources.CubesPong = arenaBase + ambientLayout.pongOffset;
            resources.BrickTable = arenaBase + ambientLayout.brickTableOffset;
            const uint64_t activeListByteOffset =
                static_cast<uint64_t>(ambientLayout.activeBrickListOffset - ambientLayout.brickTableOffset);
            resources.PoolParams = (activeListByteOffset << 32u) | poolBricksPerCascade_;
            resources.Residency = arenaBase + ambientLayout.residencyOffset;
            const std::vector<AmbientResources> resourcesData = {resources};
            Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "AmbientResources", flags, resourcesData,
                                                   ambientResourcesBuffer_, ambientResourcesBufferMemory_);
        }

        // Phase 3b/3c: initialise the brick table and active list to "all unallocated" (INVALID).
        // The CPU brick classifier fills compacted pool slots and the list once the scene is voxelized.
        if (allocateAmbientCube)
        {
            const uint32_t bricksPerCascade = static_cast<uint32_t>(GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE);
            const size_t tableCount = static_cast<size_t>(ambientCubeCascadeCapacity) * bricksPerCascade;
            std::vector<uint32_t> brickTable(tableCount, GPU_SCENE_AMBIENT_BRICK_INVALID);
            void* mapped = ambientArenaBufferMemory_->Map(static_cast<VkDeviceSize>(ambientLayout.brickTableOffset),
                                                          tableCount * sizeof(uint32_t));
            std::memcpy(mapped, brickTable.data(), tableCount * sizeof(uint32_t));
            ambientArenaBufferMemory_->Unmap();

            const size_t activeListCount =
                static_cast<size_t>(ambientCubeCascadeCapacity) * poolBricksPerCascade_;
            std::vector<uint32_t> activeList(activeListCount, GPU_SCENE_AMBIENT_BRICK_INVALID);
            mapped = ambientArenaBufferMemory_->Map(static_cast<VkDeviceSize>(ambientActiveBrickListOffset_),
                                                    activeListCount * sizeof(uint32_t));
            std::memcpy(mapped, activeList.data(), activeListCount * sizeof(uint32_t));
            ambientArenaBufferMemory_->Unmap();

            const size_t residencyCount =
                static_cast<size_t>(ambientCubeCascadeCapacity) * bricksPerCascade;
            std::vector<AmbientBrickResidency> residency(residencyCount);
            mapped = ambientArenaBufferMemory_->Map(static_cast<VkDeviceSize>(ambientResidencyOffset_),
                                                    residencyCount * sizeof(AmbientBrickResidency));
            std::memcpy(mapped, residency.data(), residencyCount * sizeof(AmbientBrickResidency));
            ambientArenaBufferMemory_->Unmap();
        }

        // 太阳方向光 CSM：4 个单层 D32_SFLOAT 阴影图，初始 layout = DEPTH_READ_ONLY。
        {
            const auto& device = commandPool.Device();
            const VkExtent2D extent{kSunShadowResolution, kSunShadowResolution};
            constexpr VkFormat shadowFormat = VK_FORMAT_D32_SFLOAT;
            constexpr VkImageUsageFlags shadowUsage =
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;

            for (uint32_t i = 0; i < kSunShadowCascadeCount; ++i)
            {
                sunShadowImages_[i].reset(
                    new Vulkan::Image(device, extent, 1, shadowFormat, VK_IMAGE_TILING_OPTIMAL, shadowUsage, false));

                sunShadowMemories_[i].reset(new Vulkan::DeviceMemory(
                    sunShadowImages_[i]->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
                sunShadowMemories_[i]->SetName(fmt::format("SunShadowCascade{} Memory", i).c_str());

                sunShadowViews_[i].reset(new Vulkan::ImageView(
                    device, sunShadowImages_[i]->Handle(), shadowFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1));
            }

            Vulkan::SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
            {
                VkImageSubresourceRange range{};
                range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                range.baseMipLevel = 0;
                range.levelCount = 1;
                range.baseArrayLayer = 0;
                range.layerCount = 1;

                for (uint32_t i = 0; i < kSunShadowCascadeCount; ++i)
                {
                    VkImageMemoryBarrier toTransfer{};
                    toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toTransfer.image = sunShadowImages_[i]->Handle();
                    toTransfer.subresourceRange = range;
                    toTransfer.srcAccessMask = 0;
                    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                         0, nullptr, 0, nullptr, 1, &toTransfer);

                    VkClearDepthStencilValue clearValue{1.0f, 0};
                    vkCmdClearDepthStencilImage(commandBuffer, sunShadowImages_[i]->Handle(),
                                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

                    VkImageMemoryBarrier toReadOnly{};
                    toReadOnly.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    toReadOnly.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    toReadOnly.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                    toReadOnly.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toReadOnly.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    toReadOnly.image = sunShadowImages_[i]->Handle();
                    toReadOnly.subresourceRange = range;
                    toReadOnly.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    toReadOnly.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         0, 0, nullptr, 0, nullptr, 1, &toReadOnly);
                }
            });

            Vulkan::SamplerConfig samplerConfig;
            samplerConfig.MagFilter = VK_FILTER_NEAREST;
            samplerConfig.MinFilter = VK_FILTER_NEAREST;
            samplerConfig.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samplerConfig.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samplerConfig.AddressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            samplerConfig.BorderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            samplerConfig.AnisotropyEnable = false;
            samplerConfig.MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sunShadowSampler_.reset(new Vulkan::Sampler(device, samplerConfig));
        }

        RebuildMeshBuffer(commandPool, supportRayTracing);
    }

    Scene::~Scene()
    {
        offsetBuffer_.reset();
        offsetBufferMemory_.reset(); // release memory after bound buffer has been destroyed

        indexBuffer_.reset();
        indexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        vertexBuffer_.reset();
        vertexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        reorderBuffer_.reset();
        reorderBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        primAddressBuffer_.reset();
        primAddressBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        lightBuffer_.reset();
        lightBufferMemory_.reset();

        softMeshShaderPrimBuffer_.reset();
        softMeshShaderPrimBufferMemory_.reset();
        softMeshShaderShadowPrimBuffer_.reset();
        softMeshShaderShadowPrimBufferMemory_.reset();
        softMeshShaderVisibleItemBuffer_.reset();
        softMeshShaderVisibleItemBufferMemory_.reset();
        softMeshShaderDrawArgBuffer_.reset();
        softMeshShaderDrawArgBufferMemory_.reset();
        softMeshShaderDispatchArgBuffer_.reset();
        softMeshShaderDispatchArgBufferMemory_.reset();
        softMeshShaderCounterBuffer_.reset();
        softMeshShaderCounterBufferMemory_.reset();
        softMeshShaderResourcesBuffer_.reset();
        softMeshShaderResourcesBufferMemory_.reset();

        sceneDynamicBuffer_.reset();
        sceneDynamicBufferMemory_.reset();
        ambientResourcesBuffer_.reset();
        ambientResourcesBufferMemory_.reset();
        ambientArenaBuffer_.reset();
        ambientArenaBufferMemory_.reset();

        skinWeightBuffer_.reset();
        skinWeightBufferMemory_.reset();
        skinJointBuffer_.reset();
        skinJointBufferMemory_.reset();

        cpuShadowMap_.reset();

        sunShadowSampler_.reset();
        for (uint32_t i = 0; i < kSunShadowCascadeCount; ++i)
        {
            sunShadowViews_[i].reset();
            sunShadowImages_[i].reset();
            sunShadowMemories_[i].reset();
        }
    }

    TextureImage& Scene::ShadowMap() const
    {
        if (!cpuShadowMap_)
        {
            Throw(std::runtime_error("CPU shadow map was requested before it was allocated"));
        }
        return *cpuShadowMap_;
    }

    TextureImage& Scene::EnsureCpuShadowMap(Vulkan::CommandPool& commandPool)
    {
        if (!cpuShadowMap_)
        {
            cpuShadowMap_.reset(
                new TextureImage(commandPool, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, VK_FORMAT_R32_SFLOAT, nullptr, 0));
            cpuShadowMap_->Image().TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_GENERAL);
            cpuShadowMap_->SetDebugName("Shadowmap");
        }
        return *cpuShadowMap_;
    }

    uint32_t Scene::AmbientActiveBrickCount(uint32_t cascade) const
    {
        return activeBrickCounts_[std::min<uint32_t>(cascade, CUBE_CASCADE_MAX - 1u)];
    }

    void Scene::SetAmbientActiveBrickCounts(const std::vector<uint32_t>& counts)
    {
        activeBrickCounts_.fill(0u);
        const size_t countToCopy = std::min(counts.size(), activeBrickCounts_.size());
        for (size_t i = 0; i < countToCopy; ++i)
        {
            activeBrickCounts_[i] = std::min(counts[i], poolBricksPerCascade_);
        }
    }

    void Scene::PostLoad(const std::vector<Skeleton>& skeletons)
    {
        for (auto& node : nodes_)
        {
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (render && render->GetSkinIndex() != -1 && render->GetSkinIndex() < skeletons.size())
            {
                auto comp = std::make_shared<Runtime::SkinnedMeshComponent>(skeletons[render->GetSkinIndex()]);
                comp->AddAnimations(tracks_);
                comp->PlayAnimation("Default");
                node->AddComponent(comp);
            }
        }
    }

    void Scene::CleanUp() { cpuAccelerationStructure_.ClearAllTasks(); }

    void Scene::AddNode(std::shared_ptr<Node> node)
    {
        nodes_.push_back(node);
        EnsureNodePhysicsBody(node.get());
    }

    void Scene::EnsureNodePhysicsBody(Node* node)
    {
        if (!node)
        {
            return;
        }

        if (NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine())
        {
            auto render = node->GetComponent<Runtime::RenderComponent>();
            // bind the mesh shape to the node
            if (render && render->GetModelId() < cachedMeshShapes_.size() && cachedMeshShapes_[render->GetModelId()])
            {
                auto phys = node->GetComponent<Runtime::PhysicsComponent>();
                Node::ENodeMobility mobility = phys ? phys->GetMobility() : Node::ENodeMobility::Static;

                if (mobility == Node::ENodeMobility::Dynamic)
                {
                    return;
                }

                if (phys)
                {
                    const NextBodyID oldBodyId = phys->GetPhysicsBody();
                    if (!oldBodyId.IsInvalid())
                    {
                        physicsEngine->RemoveBody(oldBodyId);
                    }
                }

                NextMotionType motionType =
                    mobility == Node::ENodeMobility::Static ? NextMotionType::Static : NextMotionType::Kinematic;
                NextObjectLayer layer =
                    mobility == Node::ENodeMobility::Static ? NextLayers::NON_MOVING : NextLayers::MOVING;

                bool validShape = false;
#if WITH_PHYSIC
                if (cachedMeshShapes_[render->GetModelId()].GetPtr() &&
                    cachedMeshShapes_[render->GetModelId()]->mIndexedTriangles.size() > 0)
                    validShape = true;
#endif

                if (validShape)
                {
                    NextBodyID id = physicsEngine->CreateMeshBody(cachedMeshShapes_[render->GetModelId()],
                                                                  node->WorldTranslation(), node->WorldRotation(),
                                                                  node->WorldScale(), motionType, layer);

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

    std::shared_ptr<Node> Scene::RemoveNodeByInstanceId(uint32_t id)
    {
        for (auto it = nodes_.begin(); it != nodes_.end(); ++it)
        {
            if ((*it)->GetInstanceId() == id)
            {
                auto node = *it;
                if (NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine())
                {
                    if (auto phys = node->GetComponent<Runtime::PhysicsComponent>())
                    {
                        const NextBodyID bodyId = phys->GetPhysicsBody();
                        if (!bodyId.IsInvalid())
                        {
                            physicsEngine->RemoveBody(bodyId);
                        }
                    }
                }
                selectionState_.Remove(id);
                if (hoveredId_ == id)
                {
                    hoveredId_ = SceneSelectionState::invalidNodeId;
                }
                lockedIds_.erase(id);
                node->ClearParent();
                nodes_.erase(it);
                return node;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Node> Scene::GetNodeSharedByInstanceId(uint32_t id) const
    {
        for (const auto& node : nodes_)
        {
            if (node->GetInstanceId() == id)
            {
                return node;
            }
        }
        return nullptr;
    }

    uint32_t Scene::GenerateInstanceId() const
    {
        uint32_t maxId = 0;
        for (const auto& node : nodes_)
        {
            maxId = std::max(maxId, node->GetInstanceId());
        }
        return nodes_.empty() ? 0 : maxId + 1;
    }

    namespace
    {
        void CollectNodeHierarchy(const std::shared_ptr<Node>& node, std::vector<std::shared_ptr<Node>>& outNodes)
        {
            if (!node)
            {
                return;
            }
            outNodes.push_back(node);
            for (const auto& child : node->Children())
            {
                CollectNodeHierarchy(child, outNodes);
            }
        }
    } // namespace

    std::vector<Scene::RemovedNodeEntry> Scene::RemoveNodeHierarchy(uint32_t id, std::shared_ptr<Node>& outParent)
    {
        std::vector<RemovedNodeEntry> removedEntries;
        auto root = GetNodeSharedByInstanceId(id);
        if (!root)
        {
            outParent = nullptr;
            return removedEntries;
        }

        outParent = nullptr;
        if (Node* parent = root->GetParent())
        {
            outParent = GetNodeSharedByInstanceId(parent->GetInstanceId());
            root->ClearParent();
        }

        std::vector<std::shared_ptr<Node>> nodesToRemove;
        CollectNodeHierarchy(root, nodesToRemove);

        std::unordered_set<uint32_t> removeIds;
        removeIds.reserve(nodesToRemove.size());
        for (const auto& node : nodesToRemove)
        {
            removeIds.insert(node->GetInstanceId());
        }

        for (uint32_t removeId : removeIds)
        {
            selectionState_.Remove(removeId);
            lockedIds_.erase(removeId);
            if (hoveredId_ == removeId)
            {
                hoveredId_ = SceneSelectionState::invalidNodeId;
            }
        }

        removedEntries.reserve(nodesToRemove.size());
        for (size_t index = 0; index < nodes_.size(); ++index)
        {
            const auto& node = nodes_[index];
            if (removeIds.find(node->GetInstanceId()) != removeIds.end())
            {
                removedEntries.push_back({node, index});
            }
        }

        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&removeIds](const std::shared_ptr<Node>& node)
                                    { return removeIds.find(node->GetInstanceId()) != removeIds.end(); }),
                     nodes_.end());

        return removedEntries;
    }

    void Scene::RestoreNodes(const std::vector<RemovedNodeEntry>& entries, const std::shared_ptr<Node>& parent,
                             const std::shared_ptr<Node>& root)
    {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            const size_t index = std::min(it->index, nodes_.size());
            nodes_.insert(nodes_.begin() + index, it->node);
        }

        if (parent && root)
        {
            root->SetParent(parent);
        }
    }

    Assets::GPUScene Scene::BuildGPUScene(const uint32_t imageIndex) const
    {
        Assets::GPUScene gpuScene{};
        // Active RenderView's camera UBO (primary == per-image uniform buffer; secondary views
        // supply their own camera so each viewport can use a different camera).
        gpuScene.Camera = NextEngine::GetInstance()->GetRenderer().ActiveViewCameraAddress(imageIndex);
        gpuScene.SceneDynamicBase = sceneDynamicBuffer_->GetDeviceAddress();
        gpuScene.Offsets = offsetBuffer_->GetDeviceAddress();
        gpuScene.Indices = primAddressBuffer_->GetDeviceAddress();
        gpuScene.Vertices = vertexBuffer_->GetDeviceAddress();
        gpuScene.Reorders = reorderBuffer_->GetDeviceAddress();
        gpuScene.ReservedAddress0 = 0;
        gpuScene.AmbientBase = ambientResourcesBuffer_->GetDeviceAddress();
        gpuScene.TLAS = NextEngine::GetInstance()->TryGetGPUAccelerationStructureAddress();

        gpuScene.SkinWeights = skinWeightBuffer_->GetDeviceAddress();
        gpuScene.SkinJoints = skinJointBuffer_->GetDeviceAddress();
        gpuScene.SkinnedVertices = skinnedVerticesAddr_;
        gpuScene.JointMatrices = jointMatricesAddr_;
        gpuScene.SoftMeshShaderResourcesAddress =
            softMeshShaderResourcesBuffer_ ? softMeshShaderResourcesBuffer_->GetDeviceAddress() : 0;

        gpuScene.SwapChainIndex = imageIndex;
        // Active RenderView RT bank base -> shaders resolve screen-space slots via Bindless::ViewRT.
        // Primary view == 0, so the absolute (legacy) layout is unchanged.
        gpuScene.custom_data_0 = NextEngine::GetInstance()->GetRenderer().ActiveViewBankBase();

        return gpuScene;
    }

    const Assets::GPUScene& Scene::FetchGPUScene(const uint32_t imageIndex) const
    {
        gpuScene_ = BuildGPUScene(imageIndex);

        return gpuScene_;
    }

    uint32_t Scene::SoftMeshShaderDrawSlotForShadowCascade(uint32_t cascade) const
    {
        return 1u + std::min(cascade, kSunShadowCascadeCount - 1);
    }

    VkDeviceSize Scene::SoftMeshShaderDrawArgByteOffset(uint32_t slot) const
    {
        return static_cast<VkDeviceSize>(std::min(slot, kSoftMeshShaderDrawSlotCount - 1)) * sizeof(VkDrawIndirectCommand);
    }

    void Scene::PlayAllTracks()
    {
        for (auto& track : tracks_)
        {
            track.Play();
        }
    }

    void Scene::MarkEnvDirty()
    {
        // cpuAccelerationStructure_.AsyncProcessFull(*this, ambientArenaBufferMemory_.get(), true);
    }

    Node* Scene::GetNode(std::string name)
    {
        for (auto& node : nodes_)
        {
            if (node->GetName() == name)
            {
                return node.get();
            }
        }
        return nullptr;
    }

    Node* Scene::GetNodeByInstanceId(uint32_t id)
    {
        for (auto& node : nodes_)
        {
            if (node->GetInstanceId() == id)
            {
                return node.get();
            }
        }
        return nullptr;
    }

    bool Scene::GetNodeBounds(uint32_t nodeId, glm::vec3& center, float& radius) const
    {
        if (nodeId == SceneSelectionState::invalidNodeId)
        {
            return false;
        }

        const Node* foundNode = nullptr;
        for (const auto& node : nodes_)
        {
            if (node->GetInstanceId() == nodeId)
            {
                foundNode = node.get();
                break;
            }
        }

        if (!foundNode)
            return false;

        glm::vec3 splatBoundsMin;
        glm::vec3 splatBoundsMax;
        if (GetGaussianSplatWorldBounds(nodeId, splatBoundsMin, splatBoundsMax))
        {
            center = (splatBoundsMin + splatBoundsMax) * 0.5f;
            radius = glm::length(splatBoundsMax - splatBoundsMin) * 0.5f;
            return true;
        }

        center = glm::vec3(foundNode->WorldTransform()[3]);

        auto renderComp = foundNode->GetComponent<Runtime::RenderComponent>();
        if (renderComp)
        {
            const auto* model = GetModel(renderComp->GetModelId());
            if (model)
            {
                glm::vec3 localCenter = (model->GetLocalAABBMax() + model->GetLocalAABBMin()) * 0.5f;
                center = glm::vec3(foundNode->WorldTransform() * glm::vec4(localCenter, 1.0f));

                glm::vec3 extent = (model->GetLocalAABBMax() - model->GetLocalAABBMin()) * foundNode->WorldScale();
                radius = glm::length(extent) * 0.5f;
                return true;
            }
        }

        // Fallback for non-render nodes (default small radius)
        radius = 1.0f;
        return true;
    }

    bool Scene::GetGaussianSplatWorldBounds(uint32_t nodeId, glm::vec3& boundsMin, glm::vec3& boundsMax) const
    {
        const auto splat = std::find_if(gaussianSplats_.begin(), gaussianSplats_.end(),
            [nodeId](const FGaussianSplatData& data) { return data.nodeInstanceId == nodeId; });
        if (splat == gaussianSplats_.end()) return false;

        const auto node = GetNodeSharedByInstanceId(nodeId);
        if (!node) return false;

        const glm::vec3& localMin = splat->aabbMin;
        const glm::vec3& localMax = splat->aabbMax;
        const glm::mat4& world = node->WorldTransform();
        boundsMin = glm::vec3(std::numeric_limits<float>::max());
        boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local(
                (corner & 1u) ? localMax.x : localMin.x,
                (corner & 2u) ? localMax.y : localMin.y,
                (corner & 4u) ? localMax.z : localMin.z);
            const glm::vec3 transformed = glm::vec3(world * glm::vec4(local, 1.0f));
            boundsMin = glm::min(boundsMin, transformed);
            boundsMax = glm::max(boundsMax, transformed);
        }
        return true;
    }

    void Scene::RayCastGaussianSplats(glm::vec3 rayOrigin, glm::vec3 rayDir, RayCastResult& result) const
    {
        constexpr float directionEpsilon = 1e-7f;
        for (const auto& splat : gaussianSplats_)
        {
            const auto node = GetNodeSharedByInstanceId(splat.nodeInstanceId);
            const auto* component = node ? node->GetComponentPtr<Runtime::GaussianSplatComponent>() : nullptr;
            if (component && (!component->GetVisible() || !component->GetRayCastVisible())) continue;

            glm::vec3 boundsMin;
            glm::vec3 boundsMax;
            if (!GetGaussianSplatWorldBounds(splat.nodeInstanceId, boundsMin, boundsMax)) continue;

            float nearT = 0.0f;
            float farT = std::numeric_limits<float>::max();
            bool hit = true;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (std::abs(rayDir[axis]) < directionEpsilon)
                {
                    if (rayOrigin[axis] < boundsMin[axis] || rayOrigin[axis] > boundsMax[axis]) hit = false;
                    continue;
                }
                float axisNear = (boundsMin[axis] - rayOrigin[axis]) / rayDir[axis];
                float axisFar = (boundsMax[axis] - rayOrigin[axis]) / rayDir[axis];
                if (axisNear > axisFar) std::swap(axisNear, axisFar);
                nearT = std::max(nearT, axisNear);
                farT = std::min(farT, axisFar);
                if (nearT > farT) hit = false;
            }
            if (!hit || farT < 0.0f || (result.Hitted && nearT >= result.T)) continue;

            result.Hitted = 1;
            result.T = nearT;
            result.InstanceId = splat.nodeInstanceId;
            result.MaterialId = 0;
            result.HitPoint = glm::vec4(rayOrigin + rayDir * nearT, 1.0f);
            result.Normal = glm::vec4(0.0f);
        }
    }

    const Model* Scene::GetModel(uint32_t id) const
    {
        if (id < models_.size())
        {
            return &models_[id];
        }
        return nullptr;
    }

    const FMaterial* Scene::GetMaterial(uint32_t id) const
    {
        if (id < materials_.size())
        {
            return &materials_[id];
        }
        return nullptr;
    }

    const uint32_t Scene::AddMaterial(const FMaterial& material)
    {
        materials_.push_back(material);
        materialDirty_ = true;
        return uint32_t(materials_.size() - 1);
    }

    void Scene::MarkDirty()
    {
        sceneDirty_ = true;
        sceneDirtyForCpuAS_ = true;
        NextEngine::GetInstance()->SetProgressiveRendering(false, false);
    }

    void Scene::MarkTransformDirty()
    {
        sceneDirty_ = true;
        NextEngine::GetInstance()->SetProgressiveRendering(false, false);
    }

    void Scene::MarkSelectionDirty()
    {
        sceneDirty_ = true;
        NextEngine::GetInstance()->SetProgressiveRendering(false, false);
    }

    void Scene::OverrideModelView(glm::mat4& outMatrix)
    {
        if (requestOverrideModelView)
        {
            requestOverrideModelView = false;
            outMatrix = overrideModelView;
        }
    }

    void Scene::SetSkinningBuffers(VkDeviceAddress skinnedVertices, VkDeviceAddress jointMatrices)
    {
        skinnedVerticesAddr_ = skinnedVertices;
        jointMatricesAddr_ = jointMatrices;
    }

    bool Scene::Save(const std::string& filename) const
    {
        // 根据文件扩展名选择保存格式
        if (filename.ends_with(".glb") || filename.ends_with(".GLB"))
        {
            return SaveAsGLB(filename);
        }
        else if (filename.ends_with(".gltf") || filename.ends_with(".GLTF"))
        {
            return SaveAsGLTF(filename);
        }
        else
        {
            SPDLOG_ERROR("Unsupported file extension. Use .glb or .gltf");
            return false;
        }
    }

    bool Scene::SaveAsGLB(const std::string& filename) const { return FSceneSaver::SaveGLBScene(filename, *this); }

    bool Scene::SaveAsGLTF(const std::string& filename) const { return FSceneSaver::SaveGLTFScene(filename, *this); }
} // namespace Assets
