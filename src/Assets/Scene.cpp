#include "Scene.hpp"
#include "Model.hpp"
#include "Options.hpp"
#include "Sphere.hpp"
#include "Vulkan/BufferUtil.hpp"
#include "Assets/TextureImage.hpp"
#include <chrono>
#include <unordered_set>
#include <meshoptimizer.h>
#include <glm/detail/type_half.hpp>

#include "Runtime/Engine.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/SwapChain.hpp"

namespace Assets
{
    Scene::Scene(Vulkan::CommandPool& commandPool,
                 bool supportRayTracing)
    {
        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        
        // host buffers
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "Nodes", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(NodeProxy) * 65535, nodeMatrixBuffer_, nodeMatrixBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "Materials", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,sizeof(Material) * 4096, materialBuffer_, materialBufferMemory_); // support 65535 nodes

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
        sceneBufferDescriptorSetManager_.reset();
        
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

        cpuShadowMap_.reset();
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

        // tier1: simple flatten whole scene
        cpuAccelerationStructure_.InitBVH(*this);

        // tier2: top level, the aabb, bottom level, the triangles in local sapace, with 2 ray relay

        // 重建universe mesh buffer, 这个可以比较静态
        std::vector<GPUVertex> vertices;
        std::vector<glm::detail::hdata> simpleVertices;
        std::vector<uint32_t> indices;
        std::vector<uint32_t> reorders;
        std::vector<uint32_t> primitiveIndices;

        offsets_.clear();
        for (auto& model : models_)
        {
            // Remember the index, vertex offsets.
            const auto indexOffset = static_cast<uint32_t>(indices.size());
            const auto vertexOffset = static_cast<uint32_t>(vertices.size());
            const auto reorderOffset = static_cast<uint32_t>(reorders.size());
            offsets_.push_back({indexOffset, model.NumberOfIndices(), vertexOffset, model.NumberOfVertices(), vec4(model.GetLocalAABBMin(), 1), vec4(model.GetLocalAABBMax(), 1), 0, 0, reorderOffset, 0});

            // Copy model data one after the other.

            // cpu vertex to gpu vertex
            for (auto& vertex : model.CPUVertices())
            {
                vertices.push_back(MakeVertex(vertex));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.x));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.y));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.z));
                simpleVertices.push_back(glm::detail::toFloat16(vertex.Position.x));
            }
            
            const std::vector<Vertex>& localVertices = model.CPUVertices();
            const std::vector<uint32_t>& localIndices = model.CPUIndices();
            std::vector<uint32_t> provoke(localIndices.size());
            std::vector<uint32_t> reorder(localVertices.size() + localIndices.size() / 3);
            std::vector<uint32_t> primIndices(localIndices.size());
            
            if (localIndices.size() > 0)
            {
                reorder.resize(meshopt_generateProvokingIndexBuffer(&provoke[0], &reorder[0], &localIndices[0], localIndices.size(), localVertices.size()));
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
            
            model.FreeMemory();
        }
        
        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices, vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "SimpleVertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, simpleVertices, simpleVertexBuffer_, simpleVertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | flags, indices, indexBuffer_, indexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Reorder", flags, reorders, reorderBuffer_, reorderBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "PrimAddress", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, primitiveIndices, primAddressBuffer_, primAddressBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_, offsetBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights_, lightBuffer_, lightBufferMemory_);

        // 一些数据
        lightCount_ = static_cast<uint32_t>(lights_.size());
        indicesCount_ = static_cast<uint32_t>(indices.size());
        verticeCount_ = static_cast<uint32_t>(vertices.size());

        UpdateMaterial();
        MarkDirty();

#if ANDROID
        cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), false);
#else
        if ( !NextEngine::GetInstance()->GetRenderer().supportRayTracing_ )
        {
            cpuAccelerationStructure_.AsyncProcessFull(*this, farAmbientCubeBufferMemory_.get(), false);
        }
#endif
        // no need for shadow map
        //cpuAccelerationStructure_.GenShadowMap(*this);
        
        uint32_t maxSets = 2;//NextEngine::GetInstance()->GetRenderer().SwapChain().ImageViews().size();

        sceneBufferDescriptorSetManager_.reset(new Vulkan::DescriptorSetManager(commandPool.Device(), {
                // all buffer here
                {0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {1, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {2, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {3, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {4, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {5, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {6, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {7, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {8, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {9, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {10, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {11, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {12, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
            }, maxSets));
        
        auto& descriptorSets = sceneBufferDescriptorSetManager_->DescriptorSets();

        for (uint32_t i = 0; i != maxSets; ++i)
        {
            std::vector<VkWriteDescriptorSet> descriptorWrites =
            {
                descriptorSets.Bind(i, 0, { vertexBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 1, { indexBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 2, { materialBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 3, { offsetBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 4, { nodeMatrixBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 5, { ambientCubeBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 6, { farAmbientCubeBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 7, { hdrSHBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 8, { lightBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 9, { pageIndexBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 10, { gpuDrivenStatsBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 11, { reorderBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
                descriptorSets.Bind(i, 12, { primAddressBuffer_->Handle(), 0, VK_WHOLE_SIZE}),
            };

            descriptorSets.UpdateDescriptors(i, descriptorWrites);
        }
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

    void Scene::Tick(float DeltaSeconds)
    {
        float DurationMax = 0;

        for (auto& track : tracks_)
        {
            if (!track.Playing()) continue;
            DurationMax = glm::max(DurationMax, track.Duration_);
        }

        for (auto& track : tracks_)
        {
            if (!track.Playing()) continue;
            track.Time_ += DeltaSeconds;
            if (track.Time_ > DurationMax)
            {
                track.Time_ = 0;
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

                // temporal if camera node, request override
                if (node->GetName() == "Shot.BlueCar")
                {
                    requestOverrideModelView = true;
                    overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0, 0, -1), glm::vec3(0.0f, 1.0f, 0.0f));
                }
            }
        }

        if ( NextEngine::GetInstance()->GetTotalFrames() % 10 == 0 )
        {
            cpuAccelerationStructure_.Tick(*this,  ambientCubeBufferMemory_.get(), farAmbientCubeBufferMemory_.get(), pageIndexBufferMemory_.get() );
        }
    }

    void Scene::UpdateMaterial()
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
        
        return UpdateNodesGpuDriven();
    }

    void Scene::UpdateHDRSH()
    {
        auto& SHData = GlobalTexturePool::GetInstance()->GetHDRSphericalHarmonics();
        if (SHData.size() > 0)
        {
            SphericalHarmonics* data = reinterpret_cast<SphericalHarmonics*>(hdrSHBufferMemory_->Map(0, sizeof(SphericalHarmonics) * SHData.size()));
            std::memcpy(data, SHData.data(), SHData.size() * sizeof(SphericalHarmonics));
            hdrSHBufferMemory_->Unmap();
        }
    }

    bool Scene::UpdateNodesLegacy()
    {
         // this can move to thread task
        if (nodes_.size() > 0)
        {
            if (sceneDirty_)
            {
                sceneDirty_ = false;
                {
                    PERFORMANCEAPI_INSTRUMENT_COLOR("Scene::PrepareSceneNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
                    nodeProxys.clear();
                    indirectDrawBufferInstanced.clear();

                    uint32_t indexOffset = 0;
                    uint32_t vertexOffset = 0;
                    uint32_t nodeOffsetBatched = 0;

                    static std::unordered_map<uint32_t, std::vector<NodeProxy>> nodeProxysMapByModel;
                    for (auto& [key, value] : nodeProxysMapByModel)
                    {
                        value.clear();
                    }

                    for (auto& node : nodes_)
                    {
                        if (node->IsVisible())
                        {
                            auto modelId = node->GetModel();
                            glm::mat4 combined;
                            if (node->TickVelocity(combined))
                            {
                                MarkDirty();
                            }

                            NodeProxy proxy = node->GetNodeProxy();
                            proxy.combinedPrevTS = combined;
                            nodeProxysMapByModel[modelId].push_back(proxy);
                        }
                    }

                    int modelCount = static_cast<int>(models_.size());
                    for (int i = 0; i < modelCount; i++)
                    {
                        if (nodeProxysMapByModel.find(i) != nodeProxysMapByModel.end())
                        {
                            auto& nodesOfThisModel = nodeProxysMapByModel[i];

                            // draw indirect buffer, instanced, this could be generate in gpu
                            VkDrawIndexedIndirectCommand cmd{};
                            cmd.firstIndex = indexOffset;
                            cmd.indexCount = models_[i].NumberOfIndices();
                            cmd.vertexOffset = static_cast<int32_t>(vertexOffset);
                            cmd.firstInstance = nodeOffsetBatched;
                            cmd.instanceCount = static_cast<uint32_t>(nodesOfThisModel.size());

                            indirectDrawBufferInstanced.push_back(cmd);
                            nodeOffsetBatched += static_cast<uint32_t>(nodesOfThisModel.size());

                            // fill the nodeProxy
                            nodeProxys.insert(nodeProxys.end(), nodesOfThisModel.begin(), nodesOfThisModel.end());
                        }

                        indexOffset += models_[i].NumberOfIndices();
                        vertexOffset += models_[i].NumberOfVertices();
                    }

                    NodeProxy* data = reinterpret_cast<NodeProxy*>(nodeMatrixBufferMemory_->Map(0, sizeof(NodeProxy) * nodeProxys.size()));
                    std::memcpy(data, nodeProxys.data(), nodeProxys.size() * sizeof(NodeProxy));
                    nodeMatrixBufferMemory_->Unmap();

                    VkDrawIndexedIndirectCommand* diic = reinterpret_cast<VkDrawIndexedIndirectCommand*>(indirectDrawBufferMemory_->Map(
                        0, sizeof(VkDrawIndexedIndirectCommand) * indirectDrawBufferInstanced.size()));
                    std::memcpy(diic, indirectDrawBufferInstanced.data(), indirectDrawBufferInstanced.size() * sizeof(VkDrawIndexedIndirectCommand));
                    indirectDrawBufferMemory_->Unmap();

                    indirectDrawBatchCount_ = static_cast<uint32_t>(indirectDrawBufferInstanced.size());
                }
                // cpuAccelerationStructure_.RequestUpdate(glm::vec3(10,0,-2), 1.0f);
                // cpuAccelerationStructure_.RequestUpdate(glm::vec3(-9,0,9), 2.0f);
                // cpuAccelerationStructure_.RequestUpdate(glm::vec3(-9,0,5), 2.0f);
                //cpuAccelerationStructure_.RequestUpdate(glm::vec3(1,0,1), 2.0f);
                return true;
            }
        }
        return false;
    }

    bool Scene::UpdateNodesGpuDriven()
    {
        if (nodes_.size() > 0)
        {
            if (sceneDirty_)
            {
                sceneDirty_ = false;
                {
                    PERFORMANCEAPI_INSTRUMENT_COLOR("Scene::PrepareSceneNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 200));
                    nodeProxys.clear();
                    indirectDrawBatchCount_ = 0;
                    
                    uint32_t nodeOffsetBatched = 0;

                    for (auto& node : nodes_)
                    {
                        // record all
                        if (node->IsDrawable())
                        {
                            auto modelId = node->GetModel();
                            glm::mat4 combined;
                            if (node->TickVelocity(combined))
                            {
                                MarkDirty();
                            }

                            NodeProxy proxy = node->GetNodeProxy();
                            proxy.combinedPrevTS = combined;
                            nodeProxys.push_back(proxy);

                            nodeOffsetBatched++;
                            indirectDrawBatchCount_++;
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

    void Scene::OverrideModelView(glm::mat4& OutMatrix)
    {
        if (requestOverrideModelView)
        {
            requestOverrideModelView = false;
            OutMatrix = overrideModelView;
        }
    }
}
