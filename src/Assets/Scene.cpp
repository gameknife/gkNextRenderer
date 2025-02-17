#include "Scene.hpp"
#include "Model.hpp"
#include "Options.hpp"
#include "Sphere.hpp"
#include "Vulkan/BufferUtil.hpp"

#define TINYBVH_IMPLEMENTATION
#include "ThirdParty/tinybvh/tiny_bvh.h"

static tinybvh::BVH GCpuBvh;
// represent every triangles verts one by one, triangle count is size / 3
static std::vector<tinybvh::bvhvec4> GTriangles;

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
        Vulkan::BufferUtil::CreateDeviceBufferLocal(commandPool, "AmbientCubes", flags, Assets::CUBE_SIZE * Assets::CUBE_SIZE * Assets::CUBE_SIZE * sizeof(Assets::AmbientCube), ambientCubeBuffer_,
                                                    ambientCubeBufferMemory_);
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

        ambientCubeBuffer_.reset();
        ambientCubeBufferMemory_.reset();
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
        RebuildBVH();

        // tier2: top level, the aabb, bottom level, the triangles in local sapace, with 2 ray relay

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

        GenerateAmbientCubeCPU();
    }

    void Scene::RebuildBVH()
    {
        GTriangles.clear();


        for (auto& node : nodes_)
        {
            node->RecalcTransform(true);

            uint32_t modelId = node->GetModel();
            if (modelId == -1) continue;
            Model& model = models_[modelId];

            for (auto& idx : model.CPUIndices())
            {
                Vertex& v = model.CPUVertices()[idx];
                // transform to worldpos
                glm::vec4 wpos_v = node->WorldTransform() * glm::vec4(v.Position, 1);
                wpos_v = wpos_v / wpos_v.w;
                GTriangles.push_back(tinybvh::bvhvec4(wpos_v.x, wpos_v.y, wpos_v.z, 0));
            }
        }

        if (GTriangles.size() >= 3)
        {
            GCpuBvh.Build(GTriangles.data(), static_cast<int>(GTriangles.size()) / 3);
            GCpuBvh.Convert(tinybvh::BVH::WALD_32BYTE, tinybvh::BVH::VERBOSE);
            GCpuBvh.Refit(tinybvh::BVH::VERBOSE);
        }
    }

    void Scene::PlayAllTracks()
    {
        for (auto& track : tracks_)
        {
            track.Play();
        }
    }

    Assets::RayCastResult Scene::RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir)
    {
        Assets::RayCastResult Result;

        tinybvh::Ray ray(tinybvh::bvhvec3(rayOrigin.x, rayOrigin.y, rayOrigin.z), tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z));
        GCpuBvh.Intersect(ray);

        if (ray.hit.t < 100000.f)
        {
            glm::vec3 hitPos = rayOrigin + rayDir * ray.hit.t;

            Result.HitPoint = glm::vec4(hitPos, 0);
            //Result.Normal = glm::vec4(GExtInfos[ray.hit.prim].normal, 0);
            Result.Hitted = true;
            //Result.InstanceId = GExtInfos[ray.hit.prim].instanceId;
        }

        return Result;
    }

#define CUBE_SIZE 100

    void Scene::GenerateAmbientCubeCPU()
    {
        if (GTriangles.size() < 999999999993)
        {
            return;
        }

        constexpr int cubeSize = CUBE_SIZE;
        std::vector<AmbientCube> ambientCubes(cubeSize * cubeSize * cubeSize);

        // Setup ray directions for 6 faces of the cube
        constexpr int raysPerFace = 1; // 16x16 rays per face
        const float step = 1.0f / raysPerFace;

        // Directions for cube faces:
        // PosZ, NegZ, PosY, NegY, PosX, NegX
        const glm::vec3 directions[6] = {
            glm::vec3(0, 0, 1), // PosZ
            glm::vec3(0, 0, -1), // NegZ
            glm::vec3(0, 1, 0), // PosY
            glm::vec3(0, -1, 0), // NegY
            glm::vec3(1, 0, 0), // PosX
            glm::vec3(-1, 0, 0) // NegX
        };

        const float CUBE_UNIT = 0.2f;
        const glm::vec3 CUBE_OFFSET = vec3(-50, -49.9, -50) * CUBE_UNIT;

        // Process each probe position
        //#pragma omp parallel for collapse(3)
        for (int z = 0; z < cubeSize; z++)
        {
            for (int y = 0; y < cubeSize; y++)
            {
                for (int x = 0; x < cubeSize; x++)
                {
                    glm::vec3 probePos = glm::vec3(x, y, z) * CUBE_UNIT + CUBE_OFFSET;
                    AmbientCube& cube = ambientCubes[z * cubeSize * cubeSize + y * cubeSize + x];
                    cube.Info = glm::vec4(1, 0, 0, 0); // Mark as active

                    // Cast rays for each face
                    for (int face = 0; face < 6; face++)
                    {
                         glm::vec3 baseDir = directions[face];
                        // Create orthonormal basis
                        glm::vec3 u = glm::normalize(glm::cross(
                            baseDir, glm::abs(baseDir.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0)));
                        glm::vec3 v = glm::normalize(glm::cross(baseDir, u));

                        float occlusion = 0.0f;
                        for (int i = 0; i < raysPerFace; i++)
                        {
                            for (int j = 0; j < raysPerFace; j++)
                            {
                                // Generate random angles within a hemisphere
                                float phi = (i + 0.5f) * (2.0f * glm::pi<float>() / raysPerFace);
                                float theta = (j + 0.5f) * (glm::pi<float>() / (2.0f * raysPerFace));

                                // Convert spherical to cartesian coordinates
                                float x = std::sin(theta) * std::cos(phi);
                                float y = std::sin(theta) * std::sin(phi);
                                float z = std::cos(theta);

                                // Transform the ray direction from tangent space to world space
                                glm::vec3 rayDir = glm::normalize(
                                    x * u + y * v + z * baseDir
                                );

                                tinybvh::Ray ray(
                                    tinybvh::bvhvec3(probePos.x, probePos.y, probePos.z),
                                    tinybvh::bvhvec3(rayDir.x, rayDir.y, rayDir.z)
                                );

                                GCpuBvh.Intersect(ray);

                                // Accumulate occlusion
                                if (ray.hit.t < 100.0f)
                                {
                                    occlusion += 1.0f;
                                }
                            }
                        }

                        // Store average occlusion for this face
                        occlusion /= (raysPerFace * raysPerFace);
                        float visibility = 1.0f - occlusion;

                        // Store in the correct face vector
                        switch (face)
                        {
                        case 0: cube.PosZ = glm::vec4(visibility);
                            break;
                        case 1: cube.NegZ = glm::vec4(visibility);
                            break;
                        case 2: cube.PosY = glm::vec4(visibility);
                            break;
                        case 3: cube.NegY = glm::vec4(visibility);
                            break;
                        case 4: cube.PosX = glm::vec4(visibility);
                            break;
                        case 5: cube.NegX = glm::vec4(visibility);
                            break;
                        }
                    }
                }
            }
        }

        // Upload to GPU
        AmbientCube* data = reinterpret_cast<AmbientCube*>(ambientCubeBufferMemory_->Map(0, sizeof(AmbientCube) * ambientCubes.size()));
        std::memcpy(data, ambientCubes.data(), ambientCubes.size() * sizeof(AmbientCube));
        ambientCubeBufferMemory_->Unmap();
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
