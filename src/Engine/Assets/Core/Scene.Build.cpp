// Scene build/reload: node graph (re)build, mesh/material GPU buffer rebuild.
// Split from Scene.cpp; same class, separate TU.
#include "Engine/Assets/Core/Scene.hpp"
#include <glm/detail/type_half.hpp>
#include <meshoptimizer.h>
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/LightObject.hpp"
#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
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
                lightBuffer_ ? lightBuffer_->GetDeviceAddress() : 0,
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
        materials_ = std::move(materials);
        lights_ = std::move(lights);
        tracks_ = std::move(tracks);
        selectionState_.Clear();
        hoveredId_ = SceneSelectionState::invalidNodeId;
        lockedIds_.clear();
        nodeProxies.clear();
        indirectDrawBatchCount_ = 0;
        indicesCount_ = 0;
        vertexCount_ = 0;
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

            if (auto* lightComponent = node->GetComponentPtr<Runtime::LightComponent>())
            {
                for (LightObject& light : lightComponent->Lights())
                {
                    light.lightMatIdx += materialOffset;
                }
            }

            // Reparent roots (nodes that don't have a parent in the new scene hierarchy)
            if (node->GetParent() == nullptr)
            {
                node->SetParent(rootNode);
            }
        }

        for (LightObject& light : lights)
        {
            light.lightMatIdx += materialOffset;
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

        nodeProxies.clear();
        indirectDrawBatchCount_ = 0;
        indicesCount_ = 0;
        vertexCount_ = 0;
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
                    cachedMeshShapes_.push_back(physicsEngine->CreateMeshShape(model));
                }
                else
                {
                    cachedMeshShapes_.push_back(nullptr);
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

                        if (cachedMeshShapes_[render->GetModelId()])
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

            // create 6 plane bodies, it makes negative space, so keep the bottom plane only
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

        // Rebuild the universe mesh buffer; this data is relatively static.
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

            // Split localIndices into chunks of at most 65535 * 3 indices.
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

            // Retain CPU mesh data in editor mode for scene saving.
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
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "Lights", flags,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(LightObject) * kMaxLightCount, lightBuffer_, lightBufferMemory_);
        lightCount_ = std::min<uint32_t>(static_cast<uint32_t>(lights_.size()), kMaxLightCount);
        if (lights_.size() > kMaxLightCount)
        {
            SPDLOG_WARN("Scene contains {} lights; only the first {} are uploaded", lights_.size(), kMaxLightCount);
        }
        UpdateLights();
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SkinWeights", flags, allWeights, skinWeightBuffer_,
                                               skinWeightBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SkinJoints", flags, allJoints, skinJointBuffer_,
                                               skinJointBufferMemory_);

        // Auxiliary scene data.
        indicesCount_ = static_cast<uint32_t>(indices.size());
        vertexCount_ = static_cast<uint32_t>(vertices.size());

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
                lightBuffer_->GetDeviceAddress(),
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
