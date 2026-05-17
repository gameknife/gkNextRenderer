#pragma once
#include <glm/vec2.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include "Common/CoreMinimal.hpp"
#include "Vulkan/DebugUtilities.hpp"

#include "Assets/Acceleration/CPUAccelerationStructure.h"
#include "Assets/Core/Model.hpp"
#include "Assets/Core/SceneSelectionState.hpp"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Runtime/Subsystems/NextPhysicsTypes.h"
#include "Assets/Data/Skeleton.hpp"

namespace Vulkan
{
    class Buffer;
    class CommandPool;
    class DeviceMemory;
    class Image;
    class RenderImage;
    class DescriptorSetManager;
} // namespace Vulkan

namespace Assets
{
    class Node;
    class Model;
    class Texture;
    class TextureImage;
    struct Material;
    struct LightObject;

    class Scene final
    {
    public:
        static void RegisterReflection();
        Scene(const Scene&) = delete;
        Scene(Scene&&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene& operator=(Scene&&) = delete;

        Scene(Vulkan::CommandPool& commandPool, bool supportRayTracing);
        ~Scene();

        void PostLoad(const std::vector<Skeleton>& skeletons);

        void Reload(std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
                    std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
                    std::vector<AnimationTrack>& tracks);
        std::shared_ptr<Node> Append(const std::string& sceneName, std::vector<std::shared_ptr<Node>>& nodes,
                                     std::vector<Model>& models, std::vector<FMaterial>& materials,
                                     std::vector<LightObject>& lights, std::vector<AnimationTrack>& tracks,
                                     const std::vector<Skeleton>& skeletons);
        void RebuildMeshBuffer(Vulkan::CommandPool& commandPool, bool supportRayTracing);
        void CleanUp();
        // void RebuildBVH();

        const Assets::GPUScene& FetchGPUScene(const uint32_t imageIndex) const;
        std::vector<std::shared_ptr<Node>>& Nodes() { return nodes_; }
        const std::vector<std::shared_ptr<Node>>& Nodes() const { return nodes_; }
        const std::vector<Model>& Models() const { return models_; }
        std::vector<FMaterial>& Materials() { return materials_; }
        const std::vector<FMaterial>& Materials() const { return materials_; }
        const std::vector<ModelData>& Offsets() const { return offsets_; }
        std::vector<LightObject>& Lights() { return lights_; }
        const std::vector<LightObject>& Lights() const { return lights_; }
        const Vulkan::Buffer& VertexBuffer() const { return *vertexBuffer_; }
        const Vulkan::Buffer& IndexBuffer() const { return *indexBuffer_; }
        const Vulkan::Buffer& MaterialBuffer() const { return *sceneDynamicBuffer_; }
        const Vulkan::Buffer& OffsetsBuffer() const { return *offsetBuffer_; }
        const Vulkan::Buffer& LightBuffer() const { return *lightBuffer_; }
        const Vulkan::Buffer& NodeMatrixBuffer() const { return *sceneDynamicBuffer_; }
        const Vulkan::Buffer& IndirectDrawBuffer() const { return *indirectDrawBuffer_; }
        const Vulkan::Buffer& ReorderBuffer() const { return *reorderBuffer_; }
        const Vulkan::Buffer& PrimAddressBuffer() const { return *primAddressBuffer_; }
        const glm::vec3 GetSunDir() const { return envSettings_.SunDirection(); }
        const bool HasSun() const { return envSettings_.HasSun; }

        const uint32_t GetLightCount() const { return lightCount_; }
        const uint32_t GetIndicesCount() const { return indicesCount_; }
        const uint32_t GetVerticeCount() const { return verticeCount_; }
        const uint32_t GetIndirectDrawBatchCount() const { return indirectDrawBatchCount_; }

        int32_t FindNodeIdWithComponent(const std::string& componentType) const;
        Node* GetNodeById(uint32_t nodeId);

        const Assets::GPUDrivenStat& GetGpuDrivenStat() const { return gpuDrivenStat_; }

        uint32_t GetSelectedId() const { return selectionState_.GetPrimaryId(); }
        const std::vector<uint32_t>& GetSelectedIds() const { return selectionState_.GetIds(); }
        void SetSelectedId(uint32_t id) const;
        void SetSelection(const std::vector<uint32_t>& ids) const;
        void ClearSelection() const;
        void AddToSelection(uint32_t id) const;
        void RemoveFromSelection(uint32_t id) const;
        void ToggleSelection(uint32_t id) const;
        bool IsSelected(uint32_t id) const;
        uint32_t GetHoveredId() const { return hoveredId_; }
        void SetHoveredId(uint32_t id) const;
        void ClearHoveredId() const;
        bool IsLocked(uint32_t id) const;
        void SetLocked(uint32_t id, bool locked) const;
        void ToggleLocked(uint32_t id) const;
        bool GetSelectedNodeBounds(glm::vec3& center, float& radius) const;
        bool GetNodeBounds(uint32_t nodeId, glm::vec3& center, float& radius) const;

        void Tick(float DeltaSeconds);
        void UpdateAllMaterials();
        bool UpdateNodes();
        void UpdateHDRSH();
        bool UpdateNodesGpuDriven();

        Node* GetNode(std::string name);
        Node* GetNodeByInstanceId(uint32_t id);
        const Model* GetModel(uint32_t id) const;
        const FMaterial* GetMaterial(uint32_t id) const;
        const uint32_t AddMaterial(const FMaterial& material);

        void MarkDirty();
        void MarkTransformDirty();
        void MarkSelectionDirty();

        std::vector<NodeProxy>& GetNodeProxys() { return nodeProxys; }

        void OverrideModelView(glm::mat4& OutMatrix);

        const std::vector<Assets::Camera>& GetCameras() const { return envSettings_.cameras; }
        const Assets::EnvironmentSetting& GetEnvironmentStrings() const { return envSettings_; }

        Assets::EnvironmentSetting& GetEnvSettings() { return envSettings_; }
        void SetEnvSettings(const Assets::EnvironmentSetting& envSettings) { envSettings_ = envSettings; }

        Camera& GetRenderCamera() { return renderCamera_; }
        void SetRenderCamera(const Camera& camera) { renderCamera_ = camera; }

        void PlayAllTracks();

        void MarkEnvDirty();

        void AddNode(std::shared_ptr<Node> node);
        void EnsureNodePhysicsBody(Node* node);
        std::shared_ptr<Node> RemoveNodeByInstanceId(uint32_t id);
        std::shared_ptr<Node> GetNodeSharedByInstanceId(uint32_t id) const;
        uint32_t GenerateInstanceId() const;

        struct RemovedNodeEntry
        {
            std::shared_ptr<Node> node;
            size_t index;
        };
        std::vector<RemovedNodeEntry> RemoveNodeHierarchy(uint32_t id, std::shared_ptr<Node>& outParent);
        void RestoreNodes(const std::vector<RemovedNodeEntry>& entries, const std::shared_ptr<Node>& parent,
                          const std::shared_ptr<Node>& root);

        void SetSkinningBuffers(VkDeviceAddress skinnedVertices, VkDeviceAddress jointMatrices);

        // Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);

        Vulkan::Buffer& AmbientCubeBuffer() const { return *ambientArenaBuffer_; }
        Vulkan::Buffer& AmbientCubePongBuffer() const { return *ambientArenaBuffer_; }
        Vulkan::Buffer& AmbientCubeSdfScratchBuffer() const { return *ambientArenaBuffer_; }
        Vulkan::Buffer& FarAmbientCubeBuffer() const { return *ambientArenaBuffer_; }
        Vulkan::Buffer& PageIndexBuffer() const { return *ambientArenaBuffer_; }
        size_t AmbientCubesByteOffset() const { return GPU_SCENE_AMBIENT_CUBES_OFFSET; }
        size_t AmbientVoxelsByteOffset() const { return GPU_SCENE_AMBIENT_VOXELS_OFFSET; }
        size_t AmbientPagesByteOffset() const { return GPU_SCENE_AMBIENT_PAGES_OFFSET; }
        size_t AmbientCubesPongByteOffset() const { return GPU_SCENE_AMBIENT_CUBES_PONG_OFFSET; }
        size_t AmbientSdfScratchByteOffset() const { return GPU_SCENE_AMBIENT_SDF_SCRATCH_OFFSET; }

        Vulkan::Buffer& SkinWeightBuffer() const { return *skinWeightBuffer_; }
        Vulkan::Buffer& SkinJointBuffer() const { return *skinJointBuffer_; }

        Vulkan::Buffer& HDRSHBuffer() const { return *sceneDynamicBuffer_; }

        TextureImage& ShadowMap() const { return *cpuShadowMap_; }

        FCPUAccelerationStructure& GetCPUAccelerationStructure() { return cpuAccelerationStructure_; }
        glm::vec3 GetSceneAABBMin() const { return sceneAABBMin_; }
        glm::vec3 GetSceneAABBMax() const { return sceneAABBMax_; }
        void RequestGpuDistanceFieldRebuild() { gpuSdfDirty_ = true; }
        bool ConsumeGpuDistanceFieldRebuild()
        {
            const bool rebuild = gpuSdfDirty_;
            gpuSdfDirty_ = false;
            return rebuild;
        }

        // Scene saving功能
        bool Save(const std::string& filename) const;
        bool SaveAsGLB(const std::string& filename) const;
        bool SaveAsGLTF(const std::string& filename) const;

    private:
        std::vector<FMaterial> materials_;
        std::vector<Material> gpuMaterials_;
        std::vector<Model> models_;
        std::vector<std::shared_ptr<Node>> nodes_;
        std::vector<LightObject> lights_;
        std::vector<AnimationTrack> tracks_;
        std::vector<ModelData> offsets_;

        std::unique_ptr<Vulkan::Buffer> vertexBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> vertexBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> indexBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> indexBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> reorderBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> reorderBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> primAddressBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> primAddressBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> sceneDynamicBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> sceneDynamicBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> offsetBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> offsetBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> lightBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> lightBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> indirectDrawBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> indirectDrawBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> ambientArenaBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> ambientArenaBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> skinWeightBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinWeightBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> skinJointBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinJointBufferMemory_;

        std::unique_ptr<TextureImage> cpuShadowMap_;

        uint32_t lightCount_{};
        uint32_t indicesCount_{};
        uint32_t verticeCount_{};
        uint32_t indirectDrawBatchCount_{};

        mutable SceneSelectionState selectionState_;
        mutable uint32_t hoveredId_ = SceneSelectionState::invalidNodeId;
        mutable std::unordered_set<uint32_t> lockedIds_;

        bool sceneDirtyForCpuAS_ = false;
        bool sceneDirty_ = true;
        bool materialDirty_ = true;
        bool gpuSdfDirty_ = false;

        std::vector<NodeProxy> nodeProxys;
        std::vector<VkDrawIndexedIndirectCommand> indirectDrawBufferInstanced;

        glm::mat4 overrideModelView;
        bool requestOverrideModelView = false;

        Assets::EnvironmentSetting envSettings_;
        Camera renderCamera_;

        FCPUAccelerationStructure cpuAccelerationStructure_;

        Assets::GPUDrivenStat gpuDrivenStat_;
        mutable Assets::GPUScene gpuScene_;

        glm::vec3 sceneAABBMin_{FLT_MAX, FLT_MAX, FLT_MAX};
        glm::vec3 sceneAABBMax_{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        std::vector<NextRefConst<NextMeshShapeSettings>> cachedMeshShapes_;

        VkDeviceAddress skinnedVerticesAddr_ = 0;
        VkDeviceAddress jointMatricesAddr_ = 0;
    };
} // namespace Assets
