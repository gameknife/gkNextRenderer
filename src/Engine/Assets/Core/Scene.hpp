#pragma once
#include <glm/vec2.hpp>
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/NextPhysicsTypes.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/SceneSelectionState.hpp"
#include "Engine/Assets/Data/Skeleton.hpp"

#include <ranges>
#include <span>

namespace Runtime
{
    class EnvironmentComponent;
    class SkinnedMeshComponent;
}

namespace Assets
{
    struct SceneRebuildProfile
    {
        float totalMs = 0.0f;
        float physicsShapeCookingMs = 0.0f;
        float physicsBodyCreationMs = 0.0f;
        float gpuResourceBuildMs = 0.0f;
        float cpuPreparationMs = 0.0f;
    };

    class Scene final
    {
    public:
        static constexpr uint32_t kSunShadowCascadeCount = 4;
        static constexpr uint32_t kSunShadowResolution = 1024;
        static constexpr uint32_t kRenderProxyCapacity = MAX_RENDER_PROXIES;
        static constexpr uint32_t kMaxTrianglesPerSection = 65535;
        static constexpr uint32_t kMaxLightCount = 1024;
        static constexpr uint32_t kModelSectionStride = 10;
        static constexpr uint32_t kSoftMeshShaderDrawSlotCount = 1 + kSunShadowCascadeCount;
        static constexpr uint32_t kSunShadowCascadeMask = (1u << kSunShadowCascadeCount) - 1u;
        static constexpr uint32_t DecodeModelIndex(uint32_t encodedModelSection)
        {
            return encodedModelSection / kModelSectionStride;
        }
        static constexpr bool TryEncodeModelSection(
            uint32_t modelIndex, uint32_t sectionIndex, uint32_t& encodedModelSection)
        {
            if (sectionIndex >= kModelSectionStride ||
                modelIndex > (std::numeric_limits<uint32_t>::max() - sectionIndex) / kModelSectionStride)
            {
                return false;
            }
            encodedModelSection = modelIndex * kModelSectionStride + sectionIndex;
            return true;
        }
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

        Scene(Vulkan::CommandPool& commandPool, bool supportRayTracing,
              bool allocateAmbientResources = true, bool enableCpuAcceleration = true);
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
        const SceneRebuildProfile& LastRebuildProfile() const { return lastRebuildProfile_; }
        bool EnsureGpuDrivenBufferCapacity(Vulkan::CommandPool& commandPool);
        void CleanUp();

        const Assets::GPUScene& FetchGPUScene(uint32_t imageIndex, uint32_t viewBankBase) const;
        const std::vector<std::shared_ptr<Node>>& Nodes() const { return nodes_; }
        template <typename T>
        auto Components()
        {
            static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
            return GetComponentsByType(ComponentTypeId<T>()) |
                   std::views::transform([](Component* component) { return static_cast<T*>(component); });
        }
        template <typename T>
        auto Components() const
        {
            static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
            return GetComponentsByType(ComponentTypeId<T>()) |
                   std::views::transform([](Component* component) { return static_cast<const T*>(component); });
        }
        std::vector<Model>& Models() { return models_; }
        const std::vector<Model>& Models() const { return models_; }
        std::vector<FMaterial>& Materials() { return materials_; }
        const std::vector<FMaterial>& Materials() const { return materials_; }
        const std::vector<ModelData>& Offsets() const { return offsets_; }
        std::vector<LightObject>& Lights() { return lights_; }
        const std::vector<LightObject>& Lights() const { return lights_; }
        // Bumped by UpdateLights whenever the light set is re-ordered (count or type/material
        // sequence changed); consumers holding light indices across frames (ReSTIR reservoirs)
        // must drop history on a mismatch. Pure transforms / color edits do not bump it.
        uint64_t LightsGeneration() const { return lightsGeneration_; }
        const Vulkan::Buffer& VertexBuffer() const { return *vertexBuffer_; }
        const Vulkan::Buffer& NodeMatrixBuffer() const { return *sceneDynamicBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderPrimBuffer() const { return *softMeshShaderPrimBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderShadowPrimBuffer() const { return *softMeshShaderShadowPrimBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderVisibleItemBuffer() const { return *softMeshShaderVisibleItemBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderDrawArgBuffer() const { return *softMeshShaderDrawArgBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderDispatchArgBuffer() const { return *softMeshShaderDispatchArgBuffer_; }
        const Vulkan::Buffer& SoftMeshShaderCounterBuffer() const { return *softMeshShaderCounterBuffer_; }
        VkDeviceSize SoftMeshShaderDrawArgByteOffset(uint32_t slot) const;
        uint32_t SoftMeshShaderDrawSlotForShadowCascade(uint32_t cascade) const;
        const Vulkan::Buffer& PrimAddressBuffer() const { return *primAddressBuffer_; }
        const Vulkan::Buffer& LightGridBuffer() const { return *lightGridBuffer_; }
        bool HasLightGridBuffer() const { return lightGridBuffer_ != nullptr; }

        uint32_t GetLightCount() const { return lightCount_; }
        uint32_t GetIndicesCount() const { return indicesCount_; }
        uint32_t GetIndirectDrawBatchCount() const { return indirectDrawBatchCountBackup_; }
        uint32_t GetMaxSceneTriangles() const { return maxSceneTriangles_; }
        uint32_t GetTriangleCount() const { return requiredGpuDrivenTriangleCapacity_; }

        int32_t FindNodeIdWithComponent(const std::string& componentType) const;

        const Assets::GPUDrivenStat& GetGpuDrivenStat() const { return gpuDrivenStat_; }
        const std::array<Assets::GPUDrivenStat, kSunShadowCascadeCount>& GetShadowGpuDrivenStats() const
        {
            return shadowGpuDrivenStats_;
        }

        uint32_t GetSelectedId() const { return selectionState_.GetPrimaryId(); }
        const std::vector<uint32_t>& GetSelectedIds() const { return selectionState_.GetIds(); }
        void SetSelectedId(uint32_t id);
        void SetSelection(const std::vector<uint32_t>& ids);
        void ClearSelection();
        void AddToSelection(uint32_t id);
        void RemoveFromSelection(uint32_t id);
        void ToggleSelection(uint32_t id);
        bool IsSelected(uint32_t id) const;
        uint32_t ResolveEditableNodeId(uint32_t id) const;
        uint32_t GetHoveredId() const { return hoveredId_; }
        void SetHoveredId(uint32_t id);
        void ClearHoveredId();
        bool IsLocked(uint32_t id) const;
        void SetLocked(uint32_t id, bool locked);
        void ToggleLocked(uint32_t id);
        bool GetSelectedNodeBounds(glm::vec3& center, float& radius) const;
        bool GetNodeBounds(uint32_t nodeId, glm::vec3& center, float& radius) const;

        void Tick(float DeltaSeconds);
        void SyncPhysics();
        void UpdateAllMaterials();
        void MarkMaterialsDirty() { materialDirty_ = true; }
        void SyncUpdateScene();
        void UpdateHDRSH();
        void StartUpdateNodes();
        bool EndUpdateNodes();
        bool GPUUpdateNodes();

        Node* GetNode(const std::string& name);
        const Node* GetNode(const std::string& name) const;
        Node* GetNodeByInstanceId(uint32_t id);
        const Model* GetModel(uint32_t id) const;
        const FMaterial* GetMaterial(uint32_t id) const;
        uint32_t AddMaterial(const FMaterial& material);
        uint32_t DuplicateMaterial(uint32_t id);
        bool RemoveMaterial(uint32_t id, uint32_t* outSelectedMaterialId = nullptr);

        void MarkDirty();
        void MarkTransformDirty();
        void MarkSelectionDirty();

        std::vector<NodeProxy>& GetNodeProxies() { return nodeProxiesBackup; }

        void OverrideModelView(glm::mat4& OutMatrix);

        Assets::EnvironmentSetting& GetEnvSettings();
        const Assets::EnvironmentSetting& GetEnvSettings() const;

        std::vector<AnimationTrack>& Tracks() { return tracks_; }
        const std::vector<AnimationTrack>& Tracks() const { return tracks_; }
        void SetTracksPlaying(bool playing);
        void EvaluateTracks(float time);

        Camera& GetRenderCamera() { return renderCamera_; }
        const Camera& GetRenderCamera() const { return renderCamera_; }

        void PlayAllTracks();
        bool HasCameraAnimation() const;

        void AddNode(std::shared_ptr<Node> node);
        void AddNodes(std::span<const std::shared_ptr<Node>> nodes);
        void ClearSkinUpdateRequests();
        const std::vector<uint32_t>& SkinUpdateRequests() const { return skinUpdateRequests_; }
        void EnsureNodePhysicsBody(Node* node);
        std::shared_ptr<Node> RemoveNodeByInstanceId(uint32_t id);
        std::vector<std::shared_ptr<Node>> RemoveNodesByInstanceId(std::span<const uint32_t> ids);
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

        Vulkan::Buffer* SkinnedVertexBuffer() const { return skinnedVertexBuffer_.get(); }

        const Vulkan::Buffer& AmbientArenaBuffer() const { return *ambientArenaBuffer_; }
        // Runtime byte offsets into the arena, sized to the actual allocated cascade capacity (Phase 2)
        // rather than the compile-time GPU_SCENE_AMBIENT_*_OFFSET constants (which assume CASCADE_MAX).
        size_t AmbientCubesByteOffset() const { return 0; }
        size_t AmbientVoxelsByteOffset() const { return ambientVoxelsOffset_; }
        size_t AmbientPagesByteOffset() const { return ambientPagesOffset_; }
        size_t AmbientCubesPongByteOffset() const { return ambientPongOffset_; }
        // Allocated cascade capacity for this Scene. The effective cascade count used by the bake,
        // the CPU baker and the UBO is clamped to this so a runtime cascade-count change never reads
        // or writes outside the arena allocation.
        uint32_t AmbientCubeCascadeCapacity() const { return ambientCubeCascadeCapacity_; }
        // Phase 3 sparse cube pool: byte offset of the per-cascade brick table inside the arena and the
        // number of cube bricks allocated per cascade (the sparse pool cap).
        size_t AmbientBrickTableByteOffset() const { return ambientBrickTableOffset_; }
        size_t AmbientActiveBrickListByteOffset() const { return ambientActiveBrickListOffset_; }
        size_t AmbientResidencyByteOffset() const { return ambientResidencyOffset_; }
        uint32_t AmbientPoolBricksPerCascade() const { return poolBricksPerCascade_; }
        uint32_t AmbientActiveBrickCount(uint32_t cascade) const;
        void SetAmbientActiveBrickCounts(const std::vector<uint32_t>& counts);

        TextureImage& ShadowMap() const;
        TextureImage& EnsureCpuShadowMap(Vulkan::CommandPool& commandPool);

        // GPU CSM resources: four cascades, each backed by a separate single-layer D32_SFLOAT image.
        const Vulkan::ImageView& SunShadowImageView(uint32_t cascade) const { return *sunShadowViews_[cascade]; }
        const Vulkan::Sampler& SunShadowSampler() const { return *sunShadowSampler_; }

        Assets::CPU::FCPUAccelerationStructure& GetCPUAccelerationStructure() { return cpuAccelerationStructure_; }
        glm::vec3 GetSceneAABBMin() const { return sceneAABBMin_; }
        glm::vec3 GetSceneAABBMax() const { return sceneAABBMax_; }

    private:
        friend class Node;

        std::vector<FMaterial> materials_;
        std::vector<Material> gpuMaterials_;
        std::vector<Model> models_;
        std::vector<std::shared_ptr<Node>> nodes_;
        struct ComponentBucket
        {
            std::vector<Component*> components;
            std::unordered_map<Node*, size_t> slotByOwner;
            std::string typeName;
        };
        std::unordered_map<entt::id_type, ComponentBucket> componentBuckets_;
        std::unordered_map<std::string, entt::id_type> componentTypeByName_;
        std::vector<LightObject> lights_;
        uint64_t lightsGeneration_ = 0;
        uint64_t lightsSignature_ = 0;
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

        std::unique_ptr<Vulkan::Buffer> lightGridBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> lightGridBufferMemory_;

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
        size_t ambientBrickTableOffset_ = 0;
        size_t ambientActiveBrickListOffset_ = 0;
        size_t ambientResidencyOffset_ = 0;
        uint32_t poolBricksPerCascade_ = 0;
        std::array<uint32_t, CUBE_CASCADE_MAX> activeBrickCounts_{};

        std::unique_ptr<Vulkan::Buffer> skinWeightBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinWeightBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> skinJointBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinJointBufferMemory_;

        std::unique_ptr<Vulkan::Buffer> skinnedVertexBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> skinnedVertexBufferMemory_;
        std::unique_ptr<Vulkan::Buffer> jointMatrixBuffer_;
        std::unique_ptr<Vulkan::DeviceMemory> jointMatrixBufferMemory_;
        uint32_t jointMatrixCapacity_ = 0;
        uint32_t allocatedJointCount_ = 0;
        bool jointMatrixUploadDirty_ = false;
        std::vector<uint32_t> skinUpdateRequests_;

        std::unique_ptr<TextureImage> cpuShadowMap_;

        std::array<std::unique_ptr<Vulkan::Image>, 4> sunShadowImages_;
        std::array<std::unique_ptr<Vulkan::DeviceMemory>, 4> sunShadowMemories_;
        std::array<std::unique_ptr<Vulkan::ImageView>, 4> sunShadowViews_;
        std::unique_ptr<Vulkan::Sampler> sunShadowSampler_;

        uint32_t lightCount_{};
        uint32_t indicesCount_{};
        uint32_t vertexCount_{};
        uint32_t indirectDrawBatchCount_{};
        uint32_t indirectDrawBatchCountBackup_{};
        uint32_t maxSceneTriangles_{1};
        uint32_t requiredGpuDrivenTriangleCapacity_{1};

        SceneSelectionState selectionState_;
        uint32_t hoveredId_ = SceneSelectionState::invalidNodeId;
        std::unordered_set<uint32_t> lockedIds_;
        std::unordered_map<uint32_t, std::shared_ptr<Node>> nodeByInstanceId_;
        uint32_t nextInstanceId_ = 0;

        bool sceneDirtyForCpuAS_ = false;
        bool sceneDirty_ = true;
        bool materialDirty_ = true;
        SceneRebuildProfile lastRebuildProfile_{};

        std::vector<NodeProxy> nodeProxies;
        std::vector<NodeProxy> nodeProxiesBackup;
        struct NodeProxyUpdateWorkItem
        {
            Node* node = nullptr;
            uint32_t modelId = 0;
            uint32_t outputOffset = 0;
            uint32_t proxyCount = 0;
        };
        std::vector<NodeProxyUpdateWorkItem> nodeProxyWorkItems_;
        std::atomic<uint32_t> nodeProxyTasksRemaining_{0};
        std::atomic<uint64_t> nodeProxyExpandedTriangleCount_{0};
        std::atomic<bool> nodeProxyMovingNodeDetected_{false};
        bool nodeProxyUpdatePending_ = false;

        glm::mat4 overrideModelView;
        bool requestOverrideModelView = false;

        Camera renderCamera_;

        Assets::CPU::FCPUAccelerationStructure cpuAccelerationStructure_;
        bool allocateAmbientResources_ = true;
        bool enableCpuAcceleration_ = true;

        Assets::GPUDrivenStat gpuDrivenStat_;
        std::array<Assets::GPUDrivenStat, kSunShadowCascadeCount> shadowGpuDrivenStats_{};
        mutable Assets::GPUScene gpuScene_;

        glm::vec3 sceneAABBMin_{FLT_MAX, FLT_MAX, FLT_MAX};
        glm::vec3 sceneAABBMax_{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        std::vector<NextMeshShapeHandle> cachedMeshShapes_;
        // Static mesh bodies are scene implementation details. Keep their handles here so a
        // PhysicsComponent is only required for nodes with explicit (kinematic/dynamic) behavior.
        std::unordered_map<Node*, NextBodyID> staticPhysicsBodies_;

        Vulkan::CommandPool* commandPool_ = nullptr;

        void BindNode(Node& node);
        void UnbindNode(Node& node);
        void OnNodeComponentChanged(Node& node, entt::id_type componentTypeId, Component* component);
        void RegisterComponent(Node& node, Component& component);
        void UnregisterComponent(Node& node, entt::id_type componentTypeId);
        std::span<Component* const> GetComponentsByType(entt::id_type componentTypeId) const;
        void RegisterSkinComponent(Runtime::SkinnedMeshComponent& component);
        void RequestSkinUpdate(uint32_t modelId);
        void EnsureJointMatrixCapacity();

        Runtime::EnvironmentComponent* environmentComponent_ = nullptr;
        Runtime::EnvironmentComponent* GetEnvironmentComponent();
        const Runtime::EnvironmentComponent* GetEnvironmentComponent() const;

        void RebuildNodeIndex();
        std::vector<LightObject> ResolveActiveLights() const;
        void UpdateLights();
        void DrawAreaLights() const;
        void RegisterNodeIndex(const std::shared_ptr<Node>& node);
        void UnregisterNodeIndex(uint32_t id);

        void RefreshEnvironmentComponentCache();
        void CacheEnvironmentComponentFromNode(Node* node);

        Assets::GPUScene BuildGPUScene(uint32_t imageIndex, uint32_t viewBankBase) const;
        bool UpdateNodesGpuDriven();
        
        bool needUpdateTLAS = false;
    };
} // namespace Assets
