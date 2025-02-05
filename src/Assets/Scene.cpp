#include "Scene.hpp"
#include "Model.hpp"
#include "Options.hpp"
#include "Sphere.hpp"
#include "Vulkan/BufferUtil.hpp"


namespace Assets
{
    Scene::Scene(Vulkan::CommandPool& commandPool,
                 bool supportRayTracing)
    {
        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        RebuildMeshBuffer(commandPool, supportRayTracing);

        // 动态更新的场景结构，每帧更新
        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Nodes", flags, sizeof(NodeProxy) * 65535, nodeMatrixBuffer_, nodeMatrixBufferMemory_); // support 65535 nodes
        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_,
                                                      indirectDrawBufferMemory_); // support 65535 nodes


        Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Materials", flags, sizeof(Material) * 4096, materialBuffer_, materialBufferMemory_); // support 65535 nodes
    }

    Scene::~Scene()
    {
        offsetBuffer_.reset();
        offsetBufferMemory_.reset(); // release memory after bound buffer has been destroyed

        indexBuffer_.reset();
        indexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        vertexBuffer_.reset();
        vertexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
        lightBuffer_.reset();
        lightBufferMemory_.reset();

        indirectDrawBuffer_.reset();
        indirectDrawBufferMemory_.reset();
        nodeMatrixBuffer_.reset();
        nodeMatrixBufferMemory_.reset();
        materialBuffer_.reset();
        materialBufferMemory_.reset();
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
        // 重建universe mesh buffer, 这个可以比较静态
        std::vector<GPUVertex> vertices;
        std::vector<uint32_t> indices;

        offsets_.clear();
        for (auto& model : models_)
        {
            // Remember the index, vertex offsets.
            const auto indexOffset = static_cast<uint32_t>(indices.size());
            const auto vertexOffset = static_cast<uint32_t>(vertices.size());

            offsets_.emplace_back(indexOffset, vertexOffset);

            // Copy model data one after the other.

            // cpu vertex to gpu vertex
            for (auto& vertex : model.CPUVertices())
            {
                vertices.push_back(MakeVertex(vertex));
            }
            //vertices.insert(vertices.end(), model.Vertices().begin(), model.Vertices().end());
            indices.insert(indices.end(), model.CPUIndices().begin(), model.CPUIndices().end());

            model.FreeMemory();
        }

        int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices, vertexBuffer_, vertexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, indices, indexBuffer_, indexBufferMemory_);
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_, offsetBufferMemory_);

        // 材质和灯光也应考虑更新
        Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights_, lightBuffer_, lightBufferMemory_);

        // 一些数据
        lightCount_ = static_cast<uint32_t>(lights_.size());
        indicesCount_ = static_cast<uint32_t>(indices.size());
        verticeCount_ = static_cast<uint32_t>(vertices.size());

        UpdateMaterial();
        MarkDirty();
    }

    void Scene::PlayAllTracks()
    {
		for (auto& track : tracks_)
		{
		    track.Play();
		}
    }

    void Scene::Tick(float DeltaSeconds)
    {
        float DurationMax = 0;
        
        for ( auto& track : tracks_ )
        {
            if ( !track.Playing() ) continue;
            DurationMax = glm::max(DurationMax, track.Duration_);
        }
        
        for ( auto& track : tracks_ )
        {
            if ( !track.Playing() ) continue;
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
                    overrideModelView = glm::lookAtRH(translation, translation + rotation * glm::vec3(0,0,-1), glm::vec3(0.0f, 1.0f, 0.0f));
                }
            }
        }
    }

    void Scene::UpdateMaterial()
    {
        if (materials_.empty()) return;

        gpuMaterials_.clear();
        for ( auto& material : materials_ )
        {
            gpuMaterials_.push_back(material.gpuMaterial_);
        }
        
        Material* data = reinterpret_cast<Material*>(materialBufferMemory_->Map(0, sizeof(Material) * gpuMaterials_.size()));
        std::memcpy(data, gpuMaterials_.data(), gpuMaterials_.size() * sizeof(Material));
        materialBufferMemory_->Unmap();
    }

    bool Scene::UpdateNodes()
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

                    static std::unordered_map<uint32_t, std::vector<NodeProxy> > nodeProxysMapByModel;
                    for ( auto& [key, value] : nodeProxysMapByModel )
                    {
                        value.clear();
                    }

                    for (auto& node : nodes_)
                    {
                        if (node->IsVisible())
                        {
                            auto modelId = node->GetModel();
                            glm::mat4 combined;
                            if ( node->TickVelocity(combined) )
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
