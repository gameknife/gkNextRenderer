#pragma once
#include <glm/vec2.hpp>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.h"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/SceneSelectionState.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.h"
#include "Engine/Assets/Data/Skeleton.hpp"

namespace Assets
{
    class Scene final
    {
    public:
        static void RegisterReflection();
        static constexpr uint32_t kSunShadowCascadeCount = 4;
        static constexpr uint32_t kSunShadowResolution = 1024;
        static constexpr uint32_t kMaxIndirectDrawCount = 65535;
        static constexpr uint32_t kSoftMeshShaderDrawSlotCount = 1 + kSunShadowCascadeCount;
        static constexpr uint32_t kSunShadowCascadeMask = (1u << kSunShadowCascadeCount) - 1u;
        static constexpr std::array<int32_t, 16> kSunShadowHighCascadeSchedule = {
            1, -1, 2, -1, 1, -1, 3, -1,
            1, -1, 2, -1, 1, -1, -1, -1,
        };
        static constexpr uint32_t BuildSunShadowCascadeUpdateMask(uint32_t frameIndex, uint32_t priorityCascadeMask)
        {
            uint32_t updateMask = 1u << 0;
            const uint32_t priorityHighMask = priorityCascadeMask & (kSunShadowCascadeMask & ~(1u << 0));
            int32_t highCascade = kSunShadowHighCascadeSchedule[frameIndex % kSunShadowHighCascadeSchedule.size()];
            if (priorityHighMask != 0u &&
                (highCascade <= 0 || (priorityHighMask & (1u << static_cast<uint32_t>(highCascade))) == 0u))
            {
                highCascade = -1;
                for (uint32_t offset = 0; offset < kSunShadowHighCascadeSchedule.size(); ++offset)
                {
                    const int32_t candidate =
                        kSunShadowHighCascadeSchedule[(frameIndex + offset) % kSunShadowHighCascadeSchedule.size()];
                    if (candidate > 0 && (priorityHighMask & (1u << static_cast<uint32_t>(candidate))) != 0u)
                    {
                        highCascade = candidate;
                        break;
                    }
                }
            }
            if (highCascade > 0)
            {
                updateMask |= (1u << static_cast<uint32_t>(highCascade));
            }
            return updateMask;
        }
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
        const Vulkan::Buffer& SoftMeshShaderPrimBuffer() const { return *softMeshShaderPrimBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderShadowPrimBuffer() const { return *softMeshShaderShadowPrimBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderVisibleItemBuffer() const { return *softMeshShaderVisibleItemBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderDrawArgBuffer() const { return *softMeshShaderDrawArgBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderDispatchArgBuffer() const { return *softMeshShaderDispatchArgBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderCounterBuffer() const { return *softMeshShaderCounterBuffer_; }
        VkDeviceSize SoftMeshShaderDrawArgByteOffset(uint32_t slot) const;
        uint32_t SoftMeshShaderDrawSlotForShadowCascade(uint32_t cascade) const;
        const Vulkan::Buffer& ReorderBuffer() const { return *reorderBuffer_; }
        const Vulkan::Buffer& PrimAddressBuffer() const { return *primAddressBuffer_; }
        const glm::vec3 GetSunDir() const { return envSettings_.SunDirection(); }
        const bool HasSun() const { return envSettings_.HasSun; }

        const uint32_t GetLightCount() const { return lightCount_; }
        const uint32_t GetIndicesCount() const { return indicesCount_; }
        const uint32_t GetVerticeCount() const { return verticeCount_; }
        const uint32_t GetIndirectDrawBatchCount() const { return indirectDrawBatchCount_; }
        const uint32_t GetMaxSceneTriangles() const { return maxSceneTriangles_; }

        int32_t FindNodeIdWithComponent(const std::string& componentType) const;
        Node* GetNodeById(uint32_t nodeId);

        const Assets::GPUDrivenStat& GetGpuDrivenStat() const { return gpuDrivenStat_; }
        const std::array<Assets::GPUDrivenStat, kSunShadowCascadeCount>& GetShadowGpuDrivenStats() const
        {
            return shadowGpuDrivenStats_;
        }

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
        Vulkan::Buffer& AmbientSdfSeedABuffer() const { return *ambientArenaBuffer_; }
        Vulkan::Buffer& FarAmbientCubeBuffer() const { return *ambientArenaBuffer_; }
        Vulkan::Buffer& PageIndexBuffer() const { return *ambientArenaBuffer_; }
        // Runtime byte offsets into the arena, sized to the actual allocated cascade capacity (Phase 2)
        // rather than the compile-time GPU_SCENE_AMBIENT_*_OFFSET constants (which assume CASCADE_MAX).
        size_t AmbientCubesByteOffset() const { return 0; }
        size_t AmbientVoxelsByteOffset() const { return ambientVoxelsOffset_; }
        size_t AmbientPagesByteOffset() const { return ambientPagesOffset_; }
        size_t AmbientCubesPongByteOffset() const { return ambientPongOffset_; }
        size_t AmbientSdfScratchByteOffset() const { return ambientScratchOffset_; }
        size_t AmbientSdfSeedAByteOffset() const { return ambientSdfSeedAOffset_; }
        // Allocated cascade capacity for this Scene. The effective cascade count used by the bake,
        // the CPU baker and the UBO is clamped to this so a runtime cascade-count change never reads
        // or writes outside the arena allocation.
        uint32_t AmbientCubeCascadeCapacity() const { return ambientCubeCascadeCapacity_; }
        // Phase 3 sparse cube pool: byte offset of the per-cascade brick table inside the arena and the
        // number of cube bricks allocated per cascade (the sparse pool cap).
        size_t AmbientBrickTableByteOffset() const { return ambientBrickTableOffset_; }
        size_t AmbientActiveBrickListByteOffset() const { return ambientActiveBrickListOffset_; }
        uint32_t AmbientPoolBricksPerCascade() const { return poolBricksPerCascade_; }
        uint32_t AmbientActiveBrickCount(uint32_t cascade) const;
        void SetAmbientActiveBrickCounts(const std::vector<uint32_t>& counts);

        Vulkan::Buffer& SkinWeightBuffer() const { return *skinWeightBuffer_; }
        Vulkan::Buffer& SkinJointBuffer() const { return *skinJointBuffer_; }

        Vulkan::Buffer& HDRSHBuffer() const { return *sceneDynamicBuffer_; }

        TextureImage& ShadowMap() const;
        TextureImage& EnsureCpuShadowMap(Vulkan::CommandPool& commandPool);

        // GPU CSM 资源（4 个 cascade，单层 D32_SFLOAT，独立 image）。
        Vulkan::Image& SunShadowImage(uint32_t cascade) const { return *sunShadowImages_[cascade]; }
        const Vulkan::ImageView& SunShadowImageView(uint32_t cascade) const { return *sunShadowViews_[cascade]; }
        const Vulkan::Sampler& SunShadowSampler() const { return *sunShadowSampler_; }

        Assets::CPU::FCPUAccelerationStructure& GetCPUAccelerationStructure() { return cpuAccelerationStructure_; }
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

        std::unique_ptr<Vulkan::Buffer> softMeshShaderPrimBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderPrimBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> softMeshShaderShadowPrimBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderShadowPrimBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> softMeshShaderVisibleItemBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderVisibleItemBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> softMeshShaderDrawArgBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderDrawArgBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> softMeshShaderDispatchArgBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderDispatchArgBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> softMeshShaderCounterBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderCounterBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> softMeshShaderResourcesBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> softMeshShaderResourcesBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> ambientArenaBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> ambientArenaBufferMemory_;

        // Small indirection table holding device addresses of each ambient region inside the arena
        // (see AmbientResources in BasicTypes.slang). GPUScene.AmbientBase points here.
        std::unique_ptr<Vulkan::Buffer> ambientResourcesBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> ambientResourcesBufferMemory_;

        // Allocated ambient cascade capacity and the runtime byte offsets into the arena (Phase 2
        // right-sizing). Set once at construction from the configured cascade count.
        uint32_t ambientCubeCascadeCapacity_ = 0;
        size_t ambientVoxelsOffset_ = 0;
        size_t ambientPagesOffset_ = 0;
        size_t ambientPongOffset_ = 0;
        size_t ambientScratchOffset_ = 0;
        size_t ambientSdfSeedAOffset_ = 0;
        size_t ambientBrickTableOffset_ = 0;
        size_t ambientActiveBrickListOffset_ = 0;
        uint32_t poolBricksPerCascade_ = 0;
        std::array<uint32_t, CUBE_CASCADE_MAX> activeBrickCounts_{};

        std::unique_ptr<Vulkan::Buffer> skinWeightBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinWeightBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> skinJointBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinJointBufferMemory_;

        std::unique_ptr<TextureImage> cpuShadowMap_;

        std::array<std::unique_ptr<Vulkan::Image>, 4> sunShadowImages_;
        std::array<std::unique_ptr<Vulkan::DeviceMemory>, 4> sunShadowMemories_;
        std::array<std::unique_ptr<Vulkan::ImageView>, 4> sunShadowViews_;
        std::unique_ptr<Vulkan::Sampler> sunShadowSampler_;

        uint32_t lightCount_{};
        uint32_t indicesCount_{};
        uint32_t verticeCount_{};
        uint32_t indirectDrawBatchCount_{};
        uint32_t maxSceneTriangles_{1};

        mutable SceneSelectionState selectionState_;
        mutable uint32_t hoveredId_ = SceneSelectionState::invalidNodeId;
        mutable std::unordered_set<uint32_t> lockedIds_;

        bool sceneDirtyForCpuAS_ = false;
        bool sceneDirty_ = true;
        bool materialDirty_ = true;
        bool gpuSdfDirty_ = false;

        std::vector<NodeProxy> nodeProxys;

        glm::mat4 overrideModelView;
        bool requestOverrideModelView = false;

        Assets::EnvironmentSetting envSettings_;
        Camera renderCamera_;

        Assets::CPU::FCPUAccelerationStructure cpuAccelerationStructure_;

        Assets::GPUDrivenStat gpuDrivenStat_;
        std::array<Assets::GPUDrivenStat, kSunShadowCascadeCount> shadowGpuDrivenStats_{};
        mutable Assets::GPUScene gpuScene_;

        glm::vec3 sceneAABBMin_{FLT_MAX, FLT_MAX, FLT_MAX};
        glm::vec3 sceneAABBMax_{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        std::vector<NextRefConst<NextMeshShapeSettings>> cachedMeshShapes_;

        VkDeviceAddress skinnedVerticesAddr_ = 0;
        VkDeviceAddress jointMatricesAddr_ = 0;

        Assets::GPUScene BuildGPUScene(uint32_t imageIndex) const;
    };
} // namespace Assets
