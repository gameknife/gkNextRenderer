#include "Assets/Core/Scene.hpp"
#include <glm/detail/type_half.hpp>
#include <meshoptimizer.h>
#include <tiny_gltf.h>
#include <unordered_set>
#include "Assets/GPU/TextureImage.hpp"
#include "Common/CoreMinimal.hpp"
#include "Assets/Savers/FSceneSaver.h"
#include "Assets/Core/Model.hpp"
#include "Options.hpp"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Vulkan/BufferUtil.hpp"

#include "Assets/Core/Node.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Utilities/NextEngineHelper.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <entt/meta/factory.hpp>

namespace Assets
{
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

        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "VoxelDatas", flags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            Assets::CUBE_CASCADE_MAX * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z * sizeof(Assets::VoxelData),
            farAmbientCubeBuffer_, farAmbientCubeBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "PageIndex", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            ACGI_PAGE_COUNT * ACGI_PAGE_COUNT * sizeof(Assets::PageIndex), pageIndexBuffer_, pageIndexBufferMemory_);

        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "GPUDrivenStats", flags,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(Assets::GPUDrivenStat),
            gpuDrivenStatsBuffer_, gpuDrivenStatsBuffer_Memory_);

        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "HDRSH", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(SphericalHarmonics) * 100, hdrSHBuffer_, hdrSHBufferMemory_);

        // gpu local buffers
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_,
            indirectDrawBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "AmbientCubes", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            Assets::CUBE_CASCADE_MAX * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z * sizeof(Assets::AmbientCube),
            ambientCubeBuffer_, ambientCubeBufferMemory_);

        // shadow maps
        cpuShadowMap_.reset(
            new TextureImage(commandPool, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, VK_FORMAT_R32_SFLOAT, nullptr, 0));
        cpuShadowMap_->Image().TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_GENERAL);
        cpuShadowMap_->SetDebugName("Shadowmap");

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

        indirectDrawBuffer_.reset();
        indirectDrawBufferMemory_.reset();
        nodeMatrixBuffer_.reset();
        nodeMatrixBufferMemory_.reset();
        materialBuffer_.reset();
        materialBufferMemory_.reset();

        ambientCubeBuffer_.reset();
        ambientCubeBufferMemory_.reset();

        farAmbientCubeBuffer_.reset();
        farAmbientCubeBufferMemory_.reset();

        pageIndexBuffer_.reset();
        pageIndexBufferMemory_.reset();

        hdrSHBuffer_.reset();
        hdrSHBufferMemory_.reset();

        skinWeightBuffer_.reset();
        skinWeightBufferMemory_.reset();
        skinJointBuffer_.reset();
        skinJointBufferMemory_.reset();

        cpuShadowMap_.reset();
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

    void Scene::Reload(std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
                       std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                       std::vector<AnimationTrack>& tracks)
    {
        nodes_ = std::move(nodes);
        models_ = std::move(models);
        materials_ = std::move(materials);
        lights_ = std::move(lights);
        tracks_ = std::move(tracks);
        selectionState_.Clear();
        hoveredId_ = SceneSelectionState::invalidNodeId;
        lockedIds_.clear();
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
            node->SetInstanceId(currentMaxId++);

            // Update RenderComponent
            auto render = node->GetComponent<Runtime::RenderComponent>();
            if (render)
            {
                if (render->GetModelId() != -1)
                {
                    render->SetModelId(render->GetModelId() + modelOffset);
                }
                auto& mats = render->Materials();
                for (auto& matId : mats)
                {
                    matId += materialOffset;
                }

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
        nodes_.insert(nodes_.end(), nodes.begin(), nodes.end());

        // Add root node to scene
        nodes_.push_back(rootNode);

        // Mark dirty
        MarkDirty();

        return rootNode;
    }

    void Scene::RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        // Rebuild the cpu bvh
        cpuAccelerationStructure_.InitBVH(*this);

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
            }
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
                if (render && render->GetModelId() < cachedMeshShapes_.size() &&
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
#if WITH_PHYSIC
                        if (cachedMeshShapes_[render->GetModelId()].GetPtr() &&
                            cachedMeshShapes_[render->GetModelId()]->mIndexedTriangles.size() > 0)
                            validShape = true;
#endif

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
            physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(0, 1, 0), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMax_, glm::vec3(0,-1,0), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(0,0,1), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMax_, glm::vec3(0,0,-1), NextMotionType::Static);
        }

        // 重建universe mesh buffer, 这个可以比较静态
        std::vector<GPUVertex> vertices;
        std::vector<glm::detail::hdata> simpleVertices;
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
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.x));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.y));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.z));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.x));
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

        // this two buffer may change violate, reverse to MAX_NODES and  MAX_MATERIALS
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "Nodes", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(NodeProxy) * Assets::MAX_NODES, nodeMatrixBuffer_, nodeMatrixBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(
            commandPool, "Materials", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            sizeof(Material) * Assets::MAX_MATERIALS, materialBuffer_, materialBufferMemory_);

        // this buffer now, no support extended
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices",
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices,
                                               vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SimpleVertices",
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, simpleVertices,
                                               simpleVertexBuffer_, simpleVertexBufferMemory_);
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

        UpdateAllMaterials();
        UpdateNodesGpuDriven();
        MarkDirty();

        cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), false);
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

    const Assets::GPUScene& Scene::FetchGPUScene(const uint32_t imageIndex) const
    {
        // all gpu device address
        gpuScene_.Camera =
            NextEngine::GetInstance()->GetRenderer().UniformBuffers()[imageIndex].Buffer().GetDeviceAddress();
        gpuScene_.Nodes = nodeMatrixBuffer_->GetDeviceAddress();
        gpuScene_.Materials = materialBuffer_->GetDeviceAddress();
        gpuScene_.Offsets = offsetBuffer_->GetDeviceAddress();
        gpuScene_.Indices = primAddressBuffer_->GetDeviceAddress();
        gpuScene_.Vertices = vertexBuffer_->GetDeviceAddress();
        gpuScene_.VerticesSimple = simpleVertexBuffer_->GetDeviceAddress();
        gpuScene_.Reorders = reorderBuffer_->GetDeviceAddress();
        gpuScene_.Lights = lightBuffer_->GetDeviceAddress();
        gpuScene_.Cubes = ambientCubeBuffer_->GetDeviceAddress();
        gpuScene_.Voxels = farAmbientCubeBuffer_->GetDeviceAddress();
        gpuScene_.Pages = pageIndexBuffer_->GetDeviceAddress();
        gpuScene_.HDRSHs = hdrSHBuffer_->GetDeviceAddress();
        gpuScene_.IndirectDrawCommands = indirectDrawBuffer_->GetDeviceAddress();
        gpuScene_.GPUDrivenStats = gpuDrivenStatsBuffer_->GetDeviceAddress();
        gpuScene_.TLAS = NextEngine::GetInstance()->TryGetGPUAccelerationStructureAddress();

        gpuScene_.SkinWeights = skinWeightBuffer_->GetDeviceAddress();
        gpuScene_.SkinJoints = skinJointBuffer_->GetDeviceAddress();
        gpuScene_.SkinnedVertices = skinnedVerticesAddr_;
        gpuScene_.SkinnedVerticesSimple = skinnedVerticesSimpleAddr_;
        gpuScene_.JointMatrices = jointMatricesAddr_;

        gpuScene_.SwapChainIndex = imageIndex;

        return gpuScene_;
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
        // cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), true);
        // cpuAccelerationStructure_.GenShadowMap(*this);
    }

    void Scene::Tick(float deltaSeconds)
    {
        if (NextEngine::GetInstance()->GetUserSettings().TickAnimation)
        {
            for (auto& node : nodes_)
            {
                if (auto skinnedMesh = node->GetComponent<Runtime::SkinnedMeshComponent>())
                {
                    skinnedMesh->Update(deltaSeconds);
                    if (NextEngine::GetInstance()->GetShowFlags().ShowDebugSkeleton)
                    {
                        skinnedMesh->DrawDebugSkeleton(node->WorldTransform());
                    }

                    if (skinnedMesh->IsPlaying())
                    {
                        MarkDirty();
                        if (auto renderComponent = node->GetComponent<Runtime::RenderComponent>())
                        {
                            if (renderComponent->GetModelId() != -1)
                            {
                                NextEngine::GetInstance()->GetRenderer().RequestSkinUpdate(
                                    renderComponent->GetModelId());
                            }
                        }
                    }
                }
            }

            float durationMax = 0;

            for (auto& track : tracks_)
            {
                if (!track.Playing())
                    continue;
                durationMax = glm::max(durationMax, track.Duration_);
            }

            for (auto& track : tracks_)
            {
                if (!track.Playing())
                    continue;
                track.Time_ += deltaSeconds * track.PlaySpeed_;
                if (track.Time_ > durationMax)
                {
                    track.PlaySpeed_ = -1.0f;
                }
                if (track.Time_ < 0.0f)
                {
                    track.PlaySpeed_ = 1.0f;
                }
                Node* node = GetNode(track.NodeName_);
                if (node)
                {
                    glm::vec3 translation = node->Translation();
                    glm::quat rotation = node->Rotation();
                    glm::vec3 scaling = node->Scale();

                    track.Sample(track.Time_, translation, rotation, scaling);

                    node->SetTranslation(translation);
                    node->SetRotation(rotation);
                    node->SetScale(scaling);
                    node->RecalcTransform(true);

                    MarkDirty();

                    // to physicSys
                    std::function<void(Node*)> UpdatePhysicsBodyRecursive = [&](Node* n)
                    {
                        if (!n)
                            return;
                        auto phys = n->GetComponent<Runtime::PhysicsComponent>();
                        if (phys)
                        {
                            NextBodyID bodyID = phys->GetPhysicsBody();
                            if (!bodyID.IsInvalid())
                            {
                                NextEngine::GetInstance()->GetPhysicsEngine()->MoveKinematicBody(
                                    bodyID, n->WorldTranslation(), n->WorldRotation(), 0.01f);
                            }
                        }

                        for (auto& child : n->Children())
                        {
                            UpdatePhysicsBodyRecursive(child.get());
                        }
                    };
                    UpdatePhysicsBodyRecursive(node);

                    // temporal if camera node, request override
                    if (node->GetName() == "Shot.BlueCar")
                    {
                        requestOverrideModelView = true;
                        overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0, 0, -1),
                                                          glm::vec3(0.0f, 1.0f, 0.0f));
                    }
                }
            }
        }

        if (NextEngine::GetInstance()->GetShowFlags().DebugDraw_BoundingBox)
        {
            for (auto& node : nodes_)
            {
                auto render = node->GetComponent<Runtime::RenderComponent>();
                if (!render || !render->GetVisible() || !render->IsDrawable())
                {
                    continue;
                }

                const Model* model = GetModel(render->GetModelId());
                if (!model)
                {
                    continue;
                }

                glm::vec3 localaabbMin = model->GetLocalAABBMin();
                glm::vec3 localaabbMax = model->GetLocalAABBMax();

                const auto& worldMtx = node->WorldTransform();
                glm::vec3 corners[8];
                corners[0] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMin.y, localaabbMin.z, 1.0f));
                corners[1] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMin.y, localaabbMin.z, 1.0f));
                corners[2] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMax.y, localaabbMin.z, 1.0f));
                corners[3] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMax.y, localaabbMin.z, 1.0f));
                corners[4] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMin.y, localaabbMax.z, 1.0f));
                corners[5] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMin.y, localaabbMax.z, 1.0f));
                corners[6] = glm::vec3(worldMtx * glm::vec4(localaabbMin.x, localaabbMax.y, localaabbMax.z, 1.0f));
                corners[7] = glm::vec3(worldMtx * glm::vec4(localaabbMax.x, localaabbMax.y, localaabbMax.z, 1.0f));

                glm::vec3 worldAABBMin = corners[0];
                glm::vec3 worldAABBMax = corners[0];
                for (int i = 1; i < 8; ++i)
                {
                    worldAABBMin = glm::min(worldAABBMin, corners[i]);
                    worldAABBMax = glm::max(worldAABBMax, corners[i]);
                }

                NextEngineHelper::DrawAuxBox(worldAABBMin, worldAABBMax, glm::vec4(0.2f, 0.8f, 1.0f, 1.0f), 1.5f);
            }
        }

        if (NextEngine::GetInstance()->GetShowFlags().DebugDraw_PhysicsBodies)
        {
            if (NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine())
            {
                physicsEngine->DrawDebugBodies();
            }
        }

        if (NextEngine::GetInstance()->GetTotalFrames() % 30 == 0)
        {
            cpuAccelerationStructure_.Tick(*this, ambientCubeBufferMemory_.get(), farAmbientCubeBufferMemory_.get(),
                                           pageIndexBufferMemory_.get());
            
            if (sceneDirtyForCpuAS_ && !cpuAccelerationStructure_.HasPendingWork())
            {
                if (cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), true))
                {
                    sceneDirtyForCpuAS_ = false;
                }
            }
        }
    }

    void Scene::UpdateAllMaterials()
    {
        if (materials_.empty())
            return;

        gpuMaterials_.clear();
        for (auto& material : materials_)
        {
            gpuMaterials_.push_back(material.gpuMaterial_);
        }

        Material* data =
            reinterpret_cast<Material*>(materialBufferMemory_->Map(0, sizeof(Material) * gpuMaterials_.size()));
        std::memcpy(data, gpuMaterials_.data(), gpuMaterials_.size() * sizeof(Material));
        materialBufferMemory_->Unmap();

        NextEngine::GetInstance()->SetProgressiveRendering(false, false);
    }

    bool Scene::UpdateNodes()
    {
        GPUDrivenStat zero{};
        // read back gpu driven stats
        const auto data = gpuDrivenStatsBuffer_Memory_->Map(0, sizeof(Assets::GPUDrivenStat));
        // download
        GPUDrivenStat* gpuData = static_cast<GPUDrivenStat*>(data);
        std::memcpy(&gpuDrivenStat_, gpuData, sizeof(GPUDrivenStat));
        std::memcpy(gpuData, &zero, sizeof(GPUDrivenStat)); // reset to zero
        gpuDrivenStatsBuffer_Memory_->Unmap();


        // if mat dirty, update
        if (materialDirty_)
        {
            materialDirty_ = false;
            UpdateAllMaterials();
        }

        return UpdateNodesGpuDriven();
    }

    void Scene::UpdateHDRSH()
    {
        auto& shData = GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
        if (shData.size() > 0)
        {
            SphericalHarmonics* data = reinterpret_cast<SphericalHarmonics*>(
                hdrSHBufferMemory_->Map(0, sizeof(SphericalHarmonics) * shData.size()));
            std::memcpy(data, shData.data(), shData.size() * sizeof(SphericalHarmonics));
            hdrSHBufferMemory_->Unmap();
        }
    }

    bool Scene::UpdateNodesGpuDriven()
    {
        if (nodes_.size() > 0)
        {
            // do always, no flicker now
            // if (sceneDirty_)
            {
                sceneDirty_ = false;
                {
                    PERFORMANCEAPI_INSTRUMENT_COLOR("Scene::PrepareSceneNodes",
                                                    PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
                    nodeProxys.clear();
                    indirectDrawBatchCount_ = 0;

                    uint32_t currentJointOffset = 0;
                    for (auto& node : nodes_)
                    {
                        // record all
                        auto render = node->GetComponent<Runtime::RenderComponent>();
                        if (render && render->IsDrawable())
                        {
                            glm::mat4 combined;
                            if (node->TickVelocity(combined))
                            {
                                sceneDirty_ = true;
                                // MarkDirty();
                            }

                            auto model = GetModel(render->GetModelId());
                            if (model)
                            {
                                uint32_t nodeJointOffset = 0;
                                const uint32_t instanceId = node->GetInstanceId();
                                const uint32_t selectedBit = IsSelected(instanceId) ? 1u : 0u;
                                const uint32_t hoveredBit = hoveredId_ == instanceId ? 1u : 0u;
                                const uint32_t lockedBit = IsLocked(instanceId) ? 1u : 0u;
                                const uint32_t stateBits = hoveredBit | (lockedBit << 1u);
                                if (auto skinnedMesh = node->GetComponent<Runtime::SkinnedMeshComponent>())
                                {
                                    nodeJointOffset = currentJointOffset;
                                    currentJointOffset += (uint32_t)skinnedMesh->GetJointMatrices().size();
                                }

                                for (uint32_t section = 0; section < model->SectionCount(); ++section)
                                {
                                    NodeProxy proxy = node->GetNodeProxy();
                                    proxy.combinedPrevTS = combined;
                                    proxy.modelId = render->GetModelId() * 10 + section;
                                    proxy.nort = section == 0 ? 0 : 1;
                                    proxy.reserved1 = selectedBit;
                                    proxy.reserved2 = stateBits;
                                    proxy.jointMatrixOffset = nodeJointOffset;
                                    nodeProxys.push_back(proxy);
                                    indirectDrawBatchCount_++;
                                }
                            }
                        }
                    }

                    NodeProxy* data = reinterpret_cast<NodeProxy*>(
                        nodeMatrixBufferMemory_->Map(0, sizeof(NodeProxy) * nodeProxys.size()));
                    std::memcpy(data, nodeProxys.data(), nodeProxys.size() * sizeof(NodeProxy));
                    nodeMatrixBufferMemory_->Unmap();
                }
                return true;
            }
        }
        return false;
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

    void Scene::OverrideModelView(glm::mat4& outMatrix)
    {
        if (requestOverrideModelView)
        {
            requestOverrideModelView = false;
            outMatrix = overrideModelView;
        }
    }

    void Scene::SetSkinningBuffers(VkDeviceAddress skinnedVertices, VkDeviceAddress skinnedVerticesSimple,
                                   VkDeviceAddress jointMatrices)
    {
        skinnedVerticesAddr_ = skinnedVertices;
        skinnedVerticesSimpleAddr_ = skinnedVerticesSimple;
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
