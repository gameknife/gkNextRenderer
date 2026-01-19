#include "Scene.hpp"
#include "Common/CoreMinimal.hpp"
#include "Model.hpp"
#include "Options.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Assets/TextureImage.hpp"
#include <chrono>
#include <unordered_set>
#include <meshoptimizer.h>
#include <glm/detail/type_half.hpp>
#include "Runtime/NextPhysics.h"

#include "Node.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Components/SkinnedMeshComponent.h"

#include <spdlog/spdlog.h>

namespace Assets
{
    Scene::Scene(Vulkan::CommandPool& commandPool,
                 bool supportRayTracing)
    {
        int flags =  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "VoxelDatas", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z * sizeof(Assets::VoxelData), farAmbientCubeBuffer_,
                                                    farAmbientCubeBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "PageIndex", flags,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ACGI_PAGE_COUNT * ACGI_PAGE_COUNT * sizeof(Assets::PageIndex), pageIndexBuffer_,
            pageIndexBufferMemory_);

        Vulkan::BufferUtil::CreateDeviceBufferLocal( commandPool, "GPUDrivenStats", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(Assets::GPUDrivenStat), gpuDrivenStatsBuffer_, gpuDrivenStatsBuffer_Memory_ );

        Vulkan::BufferUtil::CreateDeviceBufferLocal( commandPool, "HDRSH", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(SphericalHarmonics) * 100, hdrSHBuffer_, hdrSHBufferMemory_ );
        
        // gpu local buffers
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_,
                                            indirectDrawBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "AmbientCubes", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z * sizeof(Assets::AmbientCube), ambientCubeBuffer_,
                                            ambientCubeBufferMemory_);

        // shadow maps
        cpuShadowMap_.reset(new TextureImage(commandPool, SHADOWMAP_SIZE, SHADOWMAP_SIZE, 1, VK_FORMAT_R32_SFLOAT, nullptr, 0));
        cpuShadowMap_->Image().TransitionImageLayout( commandPool, VK_IMAGE_LAYOUT_GENERAL);
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
            if (node->GetSkin() != -1 && node->GetSkin() < skeletons.size())
            {
                auto comp = std::make_shared<Runtime::SkinnedMeshComponent>(skeletons[node->GetSkin()]);
                comp->AddAnimations(tracks_);
                comp->PlayAnimation("Default");
                node->SetSkinnedMesh(comp);
            }
        }
    }

    void Scene::Reload(std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models, std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                       std::vector<AnimationTrack>& tracks)
    {
        nodes_ = std::move(nodes);
        models_ = std::move(models);
        materials_ = std::move(materials);
        lights_ = std::move(lights);
        tracks_ = std::move(tracks);
    }

    void Scene::RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing)
    {
        // Rebuild the cpu bvh
        cpuAccelerationStructure_.InitBVH(*this);

        // force static flag
        std::function<void(Node*)> SetKinematicRecursive = [&](Node* node)
        {
            if (node == nullptr) return;
            node->SetMobility(Node::ENodeMobility::Kinematic);
            for (auto& child : node->Children())
            {
                SetKinematicRecursive(child.get());
            }
        };

        for ( auto& track : tracks_ )
        {
            Node* node = GetNode(track.NodeName_);
            SetKinematicRecursive(node);
        }
        
        // calculate the scene aabb
        sceneAABBMin_ = { FLT_MAX, FLT_MAX, FLT_MAX };
        sceneAABBMax_ = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (auto& node : nodes_)
        {
            if (node->IsVisible() && node->GetModel() != -1)
            {
                glm::vec3 localaabbMin = models_[node->GetModel()].GetLocalAABBMin();
                glm::vec3 localaabbMax = models_[node->GetModel()].GetLocalAABBMax();

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
        if ( NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine() )
        {
            cachedMeshShapes_.clear();
            for (auto& model : models_)
            {
                if (model.NumberOfIndices() < 65535 * 3 && model.NumberOfIndices() > 0)
                {
                    cachedMeshShapes_.push_back( NextRefConst<NextMeshShapeSettings>(physicsEngine->CreateMeshShape(model))  );
                }
                else
                {
                    cachedMeshShapes_.push_back( NextRefConst<NextMeshShapeSettings>(nullptr)  );
                }
            }

            for (auto& node : nodes_)
            {
                // bind the mesh shape to the node
                if (node->GetMobility() != Node::ENodeMobility::Dynamic && node->GetModel() < cachedMeshShapes_.size() && cachedMeshShapes_[node->GetModel()])// && node->GetParent() == nullptr)
                {
                    NextMotionType motionType = node->GetMobility() == Node::ENodeMobility::Static ? NextMotionType::Static : NextMotionType::Kinematic;
                    NextObjectLayer layer = node->GetMobility() == Node::ENodeMobility::Static ? NextLayers::NON_MOVING : NextLayers::MOVING;

                    bool validShape = false;
#if WITH_PHYSIC
                    if (cachedMeshShapes_[node->GetModel()].GetPtr() && cachedMeshShapes_[node->GetModel()]->mIndexedTriangles.size() > 0) validShape = true;
#endif

                    if ( validShape )
                    {
                        glm::vec3 worldScale = node->WorldScale();
                        if (glm::length(worldScale) > 0.01f && glm::abs(worldScale.x) > 0.001 && glm::abs(worldScale.y) > 0.001 && glm::abs(worldScale.z) > 0.001)
                        {
                            NextBodyID id = physicsEngine->CreateMeshBody(cachedMeshShapes_[node->GetModel()], node->WorldTranslation(), node->WorldRotation(), node->WorldScale(), motionType, layer);\
                            node->BindPhysicsBody(id);

                            physicsEngine->SetBodyActive(id, node->IsVisible());
                        }
                    }
                }
            }
            
            // create 6 plane bodys, it makes negtive space, so keep the bottom plane only
            // physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(1,0,0), NextMotionType::Static);
            // physicsEngine->CreatePlaneBody(sceneAABBMax_, glm::vec3(-1,0,0), NextMotionType::Static);
             physicsEngine->CreatePlaneBody(sceneAABBMin_, glm::vec3(0,1,0), NextMotionType::Static);
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
            for (size_t i = 0; i < localIndices.size(); i += maxIndicesPerSlice) {
                size_t endIndex = std::min(i + maxIndicesPerSlice, localIndices.size());
                slicedIndices.emplace_back(localIndices.begin() + i, localIndices.begin() + endIndex);
            }

            int emptySection = 10 - int(slicedIndices.size());
            int processSection = std::min( int(slicedIndices.size()), 10);

            for ( int slice = 0; slice < processSection; ++slice )
            {
                const auto localIndexOffset = static_cast<uint32_t>(indices.size());
                const auto localReorderOffset = static_cast<uint32_t>(reorders.size());
                
                const auto& localIndiceCount = slicedIndices[slice];
                uint32_t realSize = uint32_t(localIndiceCount.size());
                offsets_.push_back({localIndexOffset, realSize, vertexOffset, model.NumberOfVertices(), vec4(model.GetLocalAABBMin(), 1), vec4(model.GetLocalAABBMax(), 1), 0, 0, localReorderOffset, 0});

                std::vector<uint32_t> provoke(localIndiceCount.size());
                std::vector<uint32_t> reorder(localVertices.size() + localIndiceCount.size() / 3);
                std::vector<uint32_t> primIndices(localIndiceCount.size());
            
                if (localIndiceCount.size() > 0)
                {
                    reorder.resize(meshopt_generateProvokingIndexBuffer(&provoke[0], &reorder[0], &localIndiceCount[0], realSize, localVertices.size()));
                }
            
                for ( size_t i = 0; i < provoke.size(); ++i )
                {
                    primIndices[i] += reorder[provoke[i]];
                }
            
                // reorder is absolute vertex index
                for ( size_t i = 0; i < reorder.size(); ++i )
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
            for ( int slice = 0; slice < emptySection; ++slice )
            {
                offsets_.push_back({indexOffset, 0, vertexOffset, model.NumberOfVertices(), vec4(model.GetLocalAABBMin(), 1), vec4(model.GetLocalAABBMax(), 1), 0, 0, reorderOffset, 0});
            }

            model.SetSectionCount(processSection);

            model.FreeMemory();
        }
        
        int flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        // this two buffer may change violate, reverse to MAX_NODES and  MAX_MATERIALS
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "Nodes", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(NodeProxy) * Assets::MAX_NODES, nodeMatrixBuffer_, nodeMatrixBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "Materials", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,sizeof(Material) * Assets::MAX_MATERIALS, materialBuffer_, materialBufferMemory_);

        // this buffer now, no support extended
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices, vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SimpleVertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, simpleVertices, simpleVertexBuffer_, simpleVertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | flags, indices, indexBuffer_, indexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Reorder", flags, reorders, reorderBuffer_, reorderBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "PrimAddress", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, primitiveIndices, primAddressBuffer_, primAddressBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_, offsetBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights_, lightBuffer_, lightBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SkinWeights", flags, allWeights, skinWeightBuffer_, skinWeightBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SkinJoints", flags, allJoints, skinJointBuffer_, skinJointBufferMemory_);

        // 一些数据
        lightCount_ = static_cast<uint32_t>(lights_.size());
        indicesCount_ = static_cast<uint32_t>(indices.size());
        verticeCount_ = static_cast<uint32_t>(vertices.size());

        UpdateAllMaterials();
        UpdateNodesGpuDriven();
        MarkDirty();

        cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), false);
    }

    void Scene::CleanUp()
    {
        cpuAccelerationStructure_.ClearAllTasks();
    }

    void Scene::AddNode(std::shared_ptr<Node> node)
    {
        nodes_.push_back(node);
        if ( NextPhysics* physicsEngine = NextEngine::GetInstance()->GetPhysicsEngine() )
        {
             // bind the mesh shape to the node
            if (node->GetMobility() != Node::ENodeMobility::Dynamic && node->GetModel() < cachedMeshShapes_.size() && cachedMeshShapes_[node->GetModel()])// && node->GetParent() == nullptr)
            {
                NextMotionType motionType = node->GetMobility() == Node::ENodeMobility::Static ? NextMotionType::Static : NextMotionType::Kinematic;
                NextObjectLayer layer = node->GetMobility() == Node::ENodeMobility::Static ? NextLayers::NON_MOVING : NextLayers::MOVING;

                bool validShape = false;
#if WITH_PHYSIC
                if (cachedMeshShapes_[node->GetModel()].GetPtr() && cachedMeshShapes_[node->GetModel()]->mIndexedTriangles.size() > 0) validShape = true;
#endif

                if ( validShape )
                {
                    NextBodyID id = physicsEngine->CreateMeshBody(cachedMeshShapes_[node->GetModel()], node->WorldTranslation(), node->WorldRotation(), node->WorldScale(), motionType, layer);\
                    node->BindPhysicsBody(id);

                    physicsEngine->SetBodyActive(id, node->IsVisible());
                }
            }
        }
    }

    const Assets::GPUScene& Scene::FetchGPUScene(const uint32_t imageIndex) const
    {
        // all gpu device address
        gpuScene_.Camera = NextEngine::GetInstance()->GetRenderer().UniformBuffers()[imageIndex].Buffer().GetDeviceAddress();
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
        //cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), true);
        //cpuAccelerationStructure_.GenShadowMap(*this);
    }

    void Scene::Tick(float deltaSeconds)
    {
        if ( NextEngine::GetInstance()->GetUserSettings().TickAnimation)
        {
            for (auto& node : nodes_)
            {
                if (auto skinnedMesh = node->GetSkinnedMesh())
                {
                    skinnedMesh->Update(deltaSeconds);
                    if (NextEngine::GetInstance()->GetUserSettings().ShowDebugSkeleton)
                    {
                        skinnedMesh->DrawDebugSkeleton(node->WorldTransform());
                    }
                    MarkDirty();
                }
            }

            float durationMax = 0;

            for (auto& track : tracks_)
            {
                if (!track.Playing()) continue;
                durationMax = glm::max(durationMax, track.Duration_);
            }

            for (auto& track : tracks_)
            {
                if (!track.Playing()) continue;
                track.Time_ += deltaSeconds * track.PlaySpeed_;
                if (track.Time_ > durationMax)
                {
                    track.PlaySpeed_ = -1.0f;
                }
                if ( track.Time_ < 0.0f )
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
                        if (!n) return;
                        NextBodyID bodyID = n->GetPhysicsBody();
                        if (!bodyID.IsInvalid())
                        {
                            NextEngine::GetInstance()->GetPhysicsEngine()->MoveKinematicBody(bodyID, n->WorldTranslation(), n->WorldRotation(), 0.01f);
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
                        overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0, 0, -1), glm::vec3(0.0f, 1.0f, 0.0f));
                    }
                }
            }
        }

        if ( NextEngine::GetInstance()->GetTotalFrames() % 10 == 0 )
        {
            // if (sceneDirtyForCpuAS_)
            // {
            //     if ( cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), true) )
            //     {
            //         sceneDirtyForCpuAS_ = false;
            //     }
            // }
            
            cpuAccelerationStructure_.Tick(*this,  ambientCubeBufferMemory_.get(), farAmbientCubeBufferMemory_.get(), pageIndexBufferMemory_.get() );
        }
    }

    void Scene::UpdateAllMaterials()
    {
        if (materials_.empty()) return;

        gpuMaterials_.clear();
        for (auto& material : materials_)
        {
            gpuMaterials_.push_back(material.gpuMaterial_);
        }

        Material* data = reinterpret_cast<Material*>(materialBufferMemory_->Map(0, sizeof(Material) * gpuMaterials_.size()));
        std::memcpy(data, gpuMaterials_.data(), gpuMaterials_.size() * sizeof(Material));
        materialBufferMemory_->Unmap();

        NextEngine::GetInstance()->SetProgressiveRendering(false, false);
    }
        
    bool Scene::UpdateNodes()
    {
        GPUDrivenStat zero {};
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
            SphericalHarmonics* data = reinterpret_cast<SphericalHarmonics*>(hdrSHBufferMemory_->Map(0, sizeof(SphericalHarmonics) * shData.size()));
            std::memcpy(data, shData.data(), shData.size() * sizeof(SphericalHarmonics));
            hdrSHBufferMemory_->Unmap();
        }
    }

    bool Scene::UpdateNodesGpuDriven()
    {
        if (nodes_.size() > 0)
        {
            // do always, no flicker now
            //if (sceneDirty_)
            {
                sceneDirty_ = false;
                {
                    PERFORMANCEAPI_INSTRUMENT_COLOR("Scene::PrepareSceneNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
                    nodeProxys.clear();
                    indirectDrawBatchCount_ = 0;
                    
                    uint32_t currentJointOffset = 0;
                    for (auto& node : nodes_)
                    {
                        // record all
                        if (node->IsDrawable())
                        {
                            glm::mat4 combined;
                            if (node->TickVelocity(combined))
                            {
                                sceneDirty_ = true;
                                //MarkDirty();
                            }

                            auto model = GetModel(node->GetModel());
                            if (model)
                            {
                                uint32_t nodeJointOffset = 0;
                                if (auto skinnedMesh = node->GetSkinnedMesh())
                                {
                                    nodeJointOffset = currentJointOffset;
                                    currentJointOffset += (uint32_t)skinnedMesh->GetJointMatrices().size();
                                }

                                for ( uint32_t section = 0; section < model->SectionCount(); ++section)
                                {
                                    NodeProxy proxy = node->GetNodeProxy();
                                    proxy.combinedPrevTS = combined;
                                    proxy.modelId = node->GetModel() * 10 + section;
                                    proxy.nort = section == 0 ? 0 : 1;
                                    proxy.jointMatrixOffset = nodeJointOffset;
                                    nodeProxys.push_back(proxy);
                                    indirectDrawBatchCount_++;
                                }        
                            }
                        }
                    }
                    
                    NodeProxy* data = reinterpret_cast<NodeProxy*>(nodeMatrixBufferMemory_->Map(0, sizeof(NodeProxy) * nodeProxys.size()));
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

    void Scene::SetSkinningBuffers(VkDeviceAddress skinnedVertices, VkDeviceAddress skinnedVerticesSimple, VkDeviceAddress jointMatrices)
    {
        skinnedVerticesAddr_ = skinnedVertices;
        skinnedVerticesSimpleAddr_ = skinnedVerticesSimple;
        jointMatricesAddr_ = jointMatrices;
    }
}

