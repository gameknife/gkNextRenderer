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
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/Device.hpp"

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
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
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

        // Growing destroys the current primitive/resource buffers in place. They are referenced
        // by device address from command buffers that may still be executing (secondary render
        // views, upscaler and transfer submissions do not share the caller's frame fence), so
        // drain the device first. Growth is rare - a doubling step - so the stall does not show
        // up in steady-state frames, while destroying a live allocation would page-fault the GPU
        // on drivers that do not tolerate dangling buffer device addresses.
        commandPool.Device().WaitIdle();

        const VkBufferUsageFlags flags =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderPrim", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(VisibilityId) * maxSceneTriangles_, softMeshShaderPrimBuffer_, softMeshShaderPrimBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderShadowPrim", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(VisibilityId) * maxSceneTriangles_ * kSunShadowCascadeCount,
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
                lightGridBuffer_ ? lightGridBuffer_->GetDeviceAddress() : 0,
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
        for (const auto& node : nodes_)
        {
            if (node)
            {
                node->scene_ = nullptr;
            }
        }
        componentBuckets_.clear();
        componentTypeByName_.clear();
        // The physics backend is reset before Reload, so these handles no longer refer to bodies.
        staticPhysicsBodies_.clear();
        nodes_ = std::move(nodes);
        allocatedJointCount_ = 0;
        jointMatrixUploadDirty_ = false;
        skinUpdateRequests_.clear();
        for (const auto& node : nodes_)
        {
            BindNode(*node);
        }
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
        nodeProxiesBackup.clear();
        nodeProxyUpdatePending_ = false;
        needUpdateTLAS = false;
        indirectDrawBatchCount_ = 0;
        indirectDrawBatchCountBackup_ = 0;
        indicesCount_ = 0;
        vertexCount_ = 0;
        lightCount_ = 0;
        gpuDrivenStat_ = {};
        shadowGpuDrivenStats_.fill({});
        sceneAABBMin_ = glm::vec3(0.0f);
        sceneAABBMax_ = glm::vec3(0.0f);
        sceneDirty_ = true;
        cpuBvhDirty_ = true;
        levelVoxelBakePending_ = true;
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
            if (auto* environment = node->GetComponent<Runtime::EnvironmentComponent>())
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

            if (auto* lightComponent = node->GetComponent<Runtime::LightComponent>())
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
            if (node->GetComponent<Runtime::EnvironmentComponent>() == nullptr)
            {
                BindNode(*node);
                nodes_.push_back(node);
                RegisterNodeIndex(node);
            }
        }

        // Add root node to scene
        CacheEnvironmentComponentFromNode(rootNode.get());
        BindNode(*rootNode);
        nodes_.push_back(rootNode);
        RegisterNodeIndex(rootNode);

        // Mark dirty
        MarkDirty();

        return rootNode;
    }

    void Scene::RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        using Clock = std::chrono::steady_clock;
        const auto rebuildStart = Clock::now();
        lastRebuildProfile_ = {};

        nodeProxies.clear();
        nodeProxiesBackup.clear();
        nodeProxyUpdatePending_ = false;
        needUpdateTLAS = false;
        indirectDrawBatchCount_ = 0;
        indirectDrawBatchCountBackup_ = 0;
        indicesCount_ = 0;
        vertexCount_ = 0;
        lightCount_ = 0;
        gpuDrivenStat_ = {};
        shadowGpuDrivenStats_.fill({});

        if (enableCpuAcceleration_)
        {
            cpuAccelerationStructure_->InitBVH(*this);
        }

        if (enablePhysics_)
        {
            // Animated nodes must not be represented by static collision bodies. Thumbnail scenes
            // never simulate or answer collision queries, so omit both this setup and the later
            // Jolt shape/body build entirely.
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
        }

        // calculate the scene aabb
        sceneAABBMin_ = {FLT_MAX, FLT_MAX, FLT_MAX};
        sceneAABBMax_ = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        bool hasSceneBounds = false;
        for (auto* render : Components<Runtime::RenderComponent>())
        {
            Node* node = render->GetOwner();
            if (node && render->GetVisible() && render->GetModelId() != -1)
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
        NextPhysics* physicsEngine =
            enablePhysics_ ? NextEngine::GetInstance()->GetPhysicsEngine() : nullptr;
        if (physicsEngine != nullptr)
        {
            const auto shapeCookingStart = Clock::now();
            // RebuildMeshBuffer is also used after appending content. Remove the previous scene-owned
            // static bodies before recreating them for the rebuilt mesh/model tables.
            for (const auto& [node, bodyId] : staticPhysicsBodies_)
            {
                (void)node;
                physicsEngine->RemoveBody(bodyId);
            }
            staticPhysicsBodies_.clear();

            // Build and cook every mesh shape up front, in parallel. Each model owns an
            // independent shape, and neither call reaches the physics world, so the only ordering
            // this needs is the barrier below before bodies start referencing the shapes. Leaving
            // the cook to CreateMeshBody instead put the whole triangle-tree build on the main
            // thread, serialized behind body creation.
            cachedMeshShapes_.assign(models_.size(), NextMeshShapeHandle{});
            std::vector<uint32_t> shapeCookTasks;
            shapeCookTasks.reserve(models_.size());
            for (size_t modelIndex = 0; modelIndex < models_.size(); ++modelIndex)
            {
                const Model& model = models_[modelIndex];
                if (model.NumberOfIndices() >= 65535 * 3 || model.NumberOfIndices() == 0)
                {
                    continue;
                }

                shapeCookTasks.push_back(Tasks::TaskCoordinator::GetInstance()->AddParralledTask(
                    [this, physicsEngine, modelIndex](Tasks::ResTask&)
                    {
                        NextMeshShapeHandle shape = physicsEngine->CreateMeshShape(models_[modelIndex]);
                        if (shape && physicsEngine->CookMeshShape(shape))
                        {
                            cachedMeshShapes_[modelIndex] = std::move(shape);
                        }
                        else
                        {
                            // Name the model: a bare cooking failure is unactionable
                            // in a generated scene with a hundred models.
                            SPDLOG_WARN("[Physics] no collision for model '{}' ({} indices)",
                                        models_[modelIndex].Name(), models_[modelIndex].NumberOfIndices());
                        }
                    },
                    nullptr,
                    "Physics shape cooking"));
            }
            if (!shapeCookTasks.empty())
            {
                Tasks::TaskCoordinator::GetInstance()->WaitForAllParralledTask();
            }
            lastRebuildProfile_.physicsShapeCookingMs =
                std::chrono::duration<float, std::milli>(Clock::now() - shapeCookingStart).count();

            const auto bodyCreationStart = Clock::now();
            for (auto* render : Components<Runtime::RenderComponent>())
            {
                Node* node = render->GetOwner();
                // bind the mesh shape to the node
                if (node && render->GetRayCastVisible() &&
                    render->GetModelId() < cachedMeshShapes_.size() &&
                    cachedMeshShapes_[render->GetModelId()])
                {
                    auto* phys = node->GetComponent<Runtime::PhysicsComponent>();
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

                                if (mobility == Node::ENodeMobility::Static && !phys)
                                {
                                    staticPhysicsBodies_[node] = id;
                                }
                                else if (!phys)
                                {
                                    auto newPhysics = std::make_shared<Runtime::PhysicsComponent>();
                                    newPhysics->SetMobility(mobility);
                                    phys = newPhysics.get();
                                    node->AddComponent(std::move(newPhysics));
                                }
                                if (phys)
                                {
                                    phys->BindPhysicsBody(id);
                                }

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
            lastRebuildProfile_.physicsBodyCreationMs =
                std::chrono::duration<float, std::milli>(Clock::now() - bodyCreationStart).count();
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

        // Size everything from the model tables first: a multi-million vertex scene otherwise
        // spends most of this loop copying these arrays into successively larger allocations.
        // The index-derived counts are upper bounds -- models past ten sections get truncated
        // below -- which is what reserve wants anyway.
        {
            size_t totalVertices = 0;
            size_t totalIndices = 0;
            for (const auto& model : models_)
            {
                totalVertices += model.CPUVertices().size();
                totalIndices += model.CPUIndices().size();
            }
            vertices.reserve(totalVertices);
            indices.reserve(totalIndices);
            allWeights.reserve(totalVertices);
            allJoints.reserve(totalVertices);
            reorders.reserve(totalVertices + totalIndices / 3);
            primitiveIndices.reserve(totalIndices);
        }

        // Discrete LODs are built here rather than in the Model constructor: skin weights are
        // attached to a model after construction, and BuildLods has to see them to opt out.
        // Levels only ever append index data, so they cost nothing until a cull pass selects one.
        {
            const auto lodBuildStart = Clock::now();
            for (auto& model : models_)
            {
                model.BuildLods();
            }
            SPDLOG_INFO("Model LOD build took {:.1f}ms for {} models",
                        std::chrono::duration<float, std::milli>(Clock::now() - lodBuildStart).count(),
                        models_.size());
        }

        offsets_.clear();
        offsets_.reserve(models_.size() * kModelSectionStride);
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
            constexpr uint32_t maxIndicesPerSlice = kMaxTrianglesPerSection * 3;

            // Split localIndices into chunks whose zero-based triangle ID fits R16_UINT.
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
                // Trailing 1 is lodCount: every section starts as LOD0-only and the LOD table pass
                // below raises it for the sections that got simplified levels.
                offsets_.push_back({localIndexOffset, realSize, vertexOffset, model.NumberOfVertices(),
                                    vec4(model.GetLocalAABBMin(), 1), vec4(model.GetLocalAABBMax(), 1), 0, 0,
                                    localReorderOffset, 1});

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
                                    reorderOffset, 1});
            }

            model.SetSectionCount(processSection);
        }

        // LOD index data is appended only after every section's LOD0 is in place. `indices` and
        // `primitiveIndices` are parallel arrays addressed by one ModelData::indexOffset, and only
        // primitiveIndices is reachable from the shaders (GPUScene::Indices points at it), so
        // interleaving the levels would desynchronise the two for every later model.
        {
            for (ModelData& section : offsets_)
            {
                // Level 0 mirrors the section itself so a shader can index by level unconditionally.
                section.lodIndexOffset[0] = section.indexOffset;
                section.lodIndexCount[0] = section.indexCount;
            }

            uint32_t simplifiedLevelCount = 0;
            for (uint32_t modelIndex = 0; modelIndex < models_.size(); ++modelIndex)
            {
                const Model& model = models_[modelIndex];
                // A multi-section model was split because it exceeded the per-section triangle
                // limit; its simplified streams are whole-model and cannot be attributed to one
                // slice. Those keep lodCount == 1 and always draw at full detail.
                if (model.SectionCount() != 1 || model.CPULodIndices().empty())
                {
                    continue;
                }
                uint32_t encodedModelSection = 0;
                if (!TryEncodeModelSection(modelIndex, 0, encodedModelSection) ||
                    encodedModelSection >= offsets_.size())
                {
                    continue;
                }

                uint32_t lodCount = 1;
                for (const std::vector<uint32_t>& lodIndices : model.CPULodIndices())
                {
                    if (lodCount >= static_cast<uint32_t>(MAX_MODEL_LOD_COUNT) || lodIndices.empty())
                    {
                        break;
                    }
                    // Indices are already model-relative, exactly like the LOD0 stream, so they
                    // need no rebasing: the shader adds ModelData::vertexOffset either way.
                    ModelData& section = offsets_[encodedModelSection];
                    section.lodIndexOffset[lodCount] = static_cast<uint32_t>(primitiveIndices.size());
                    section.lodIndexCount[lodCount] = static_cast<uint32_t>(lodIndices.size());
                    primitiveIndices.insert(primitiveIndices.end(), lodIndices.begin(), lodIndices.end());
                    ++lodCount;
                    ++simplifiedLevelCount;
                }
                offsets_[encodedModelSection].lodCount = lodCount;
            }
            SPDLOG_INFO("Model LOD table: {} simplified levels across {} sections", simplifiedLevelCount,
                        offsets_.size());
        }

        for (auto& model : models_)
        {
            // Retain CPU mesh data in editor mode for scene saving.
            if (!GOption->KeepCPUMeshData)
            {
                model.FreeMemory();
            }
        }

        int flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        const auto gpuResourceBuildStart = Clock::now();
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
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "LightGrid", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            GPU_SCENE_LIGHT_GRID_SIZE, lightGridBuffer_, lightGridBufferMemory_);
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
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SkinnedVertices", flags | rtxFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            std::max<size_t>(sizeof(GPUVertex), vertices.size() * sizeof(GPUVertex)),
            skinnedVertexBuffer_, skinnedVertexBufferMemory_);
        EnsureJointMatrixCapacity();
        skinUpdateRequests_.clear();
        for (auto* skinnedMesh : Components<Runtime::SkinnedMeshComponent>())
        {
            if (const Node* node = skinnedMesh->GetOwner())
            {
                if (const auto* render = node->GetComponent<Runtime::RenderComponent>();
                    render && render->GetModelId() != -1)
                {
                    RequestSkinUpdate(render->GetModelId());
                }
            }
        }

        // Auxiliary scene data.
        indicesCount_ = static_cast<uint32_t>(indices.size());
        vertexCount_ = static_cast<uint32_t>(vertices.size());

        // The GPU-driven primitive buffers contain expanded triangles per instance, not just
        // the unique model geometry stored in the index buffer. Sizing them from indicesCount_
        // silently dropped later instances when several nodes shared a model.
        sceneDirty_ = true;
        SyncUpdateScene();
        maxSceneTriangles_ = requiredGpuDrivenTriangleCapacity_;

        const VkBufferUsageFlags softMeshShaderFlags =
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderPrim", softMeshShaderFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(VisibilityId) * maxSceneTriangles_, softMeshShaderPrimBuffer_, softMeshShaderPrimBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderShadowPrim", softMeshShaderFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(VisibilityId) * maxSceneTriangles_ * kSunShadowCascadeCount,
            softMeshShaderShadowPrimBuffer_, softMeshShaderShadowPrimBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "SoftMeshShaderVisibleItems", softMeshShaderFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            sizeof(Assets::SoftMeshShaderVisibleItem) * kRenderProxyCapacity * kSoftMeshShaderDrawSlotCount,
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
                lightGridBuffer_->GetDeviceAddress(),
            },
        };
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SoftMeshShaderResources", softMeshShaderFlags, softMeshShaderResources,
                                               softMeshShaderResourcesBuffer_, softMeshShaderResourcesBufferMemory_);
        
        MarkDirty();

        if (levelVoxelBakePending_)
        {
            // Consume the transition here even when the active renderer does not need ambient
            // data. A later Append()/GPU refresh must never turn that old transition into a bake.
            levelVoxelBakePending_ = false;
            if (enableCpuAcceleration_ && ambientArenaBufferMemory_ &&
                (!NextEngine::GetInstance() ||
                 NextEngine::GetInstance()->GetRenderer().ActiveRendererRequirements().requestAmbientCube))
            {
                cpuAccelerationStructure_->AsyncProcessFull(*this, ambientArenaBufferMemory_.get());
            }
        }

        const auto rebuildEnd = Clock::now();
        lastRebuildProfile_.gpuResourceBuildMs =
            std::chrono::duration<float, std::milli>(rebuildEnd - gpuResourceBuildStart).count();
        lastRebuildProfile_.totalMs =
            std::chrono::duration<float, std::milli>(rebuildEnd - rebuildStart).count();
        lastRebuildProfile_.cpuPreparationMs =
            std::max(0.0f, lastRebuildProfile_.totalMs -
                               lastRebuildProfile_.physicsShapeCookingMs -
                               lastRebuildProfile_.physicsBodyCreationMs -
                               lastRebuildProfile_.gpuResourceBuildMs);
    }

}
