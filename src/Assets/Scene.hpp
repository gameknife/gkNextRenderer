#pragma once
#include "Common/CoreMinimal.hpp"
#include "Vulkan/Vulkan.hpp"
#include <memory>
#include <string>
#include <vector>
#include <glm/vec2.hpp>

#include "CPUAccelerationStructure.h"
#include "Model.hpp"

namespace Vulkan
{
	class Buffer;
	class CommandPool;
	class DeviceMemory;
	class Image;
	class RenderImage;
	class DescriptorSetManager;
}

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

		Scene(const Scene&) = delete;
		Scene(Scene&&) = delete;
		Scene& operator = (const Scene&) = delete;
		Scene& operator = (Scene&&) = delete;

		Scene(Vulkan::CommandPool& commandPool,	bool supportRayTracing);
		~Scene();

		void Reload(std::vector<std::shared_ptr<Node>>& nodes,
			std::vector<Model>& models,
			std::vector<FMaterial>& materials,
			std::vector<LightObject>& lights,
			std::vector<AnimationTrack>& tracks);
		void RebuildMeshBuffer(Vulkan::CommandPool& commandPool,
			bool supportRayTracing);
		//void RebuildBVH();

		std::vector<std::shared_ptr<Node>>& Nodes() { return nodes_; }
		const std::vector<Model>& Models() const { return models_; }
		std::vector<FMaterial>& Materials() { return materials_; }
		const std::vector<ModelData>& Offsets() const { return offsets_; }
		const std::vector<LightObject>& Lights() const { return lights_; }
		const Vulkan::Buffer& VertexBuffer() const { return *vertexBuffer_; }
		const Vulkan::Buffer& IndexBuffer() const { return *indexBuffer_; }
		const Vulkan::Buffer& MaterialBuffer() const { return *materialBuffer_; }
		const Vulkan::Buffer& OffsetsBuffer() const { return *offsetBuffer_; }
		const Vulkan::Buffer& LightBuffer() const { return *lightBuffer_; }
		const Vulkan::Buffer& NodeMatrixBuffer() const { return *nodeMatrixBuffer_; }
		const Vulkan::Buffer& IndirectDrawBuffer() const { return *indirectDrawBuffer_; }
		const glm::vec3 GetSunDir() const { return envSettings_.SunDirection(); }
		const bool HasSun() const { return envSettings_.HasSun; }

		const uint32_t GetLightCount() const {return lightCount_;}
		const uint32_t GetIndicesCount() const {return indicesCount_;}
		const uint32_t GetVerticeCount() const {return verticeCount_;}
		const uint32_t GetIndirectDrawBatchCount() const {return indirectDrawBatchCount_;}
		
		uint32_t GetSelectedId() const { return selectedId_; }
		void SetSelectedId( uint32_t id ) const { selectedId_ = id; }

		void Tick(float DeltaSeconds);
		void UpdateMaterial();
		bool UpdateNodes();

		bool UpdateNodesLegacy();
		bool UpdateNodesGpuDriven();

		Node* GetNode(std::string name);
		Node* GetNodeByInstanceId(uint32_t id);
		const Model* GetModel(uint32_t id) const;
		const FMaterial* GetMaterial(uint32_t id) const;

		void MarkDirty() {sceneDirty_ = true;}
		
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

		//Assets::RayCastResult RayCastInCPU(glm::vec3 rayOrigin, glm::vec3 rayDir);

		Vulkan::Buffer& AmbientCubeBuffer() const { return *ambientCubeBuffer_; }
		Vulkan::Buffer& FarAmbientCubeBuffer() const { return *farAmbientCubeBuffer_; }
		Vulkan::Buffer& PageIndexBuffer() const { return *pageIndexBuffer_; }

		Vulkan::Buffer& HDRSHBuffer() const { return *hdrSHBuffer_; }

		TextureImage& ShadowMap() const { return *cpuShadowMap_; }

		FCPUAccelerationStructure& GetCPUAccelerationStructure() { return cpuAccelerationStructure_; }

		Vulkan::DescriptorSetManager& GetSceneBufferDescriptorSetManager() const
		{
			return *sceneBufferDescriptorSetManager_;
		}
		
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

		std::unique_ptr<Vulkan::Buffer> materialBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> materialBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> offsetBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> offsetBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> lightBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> lightBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> nodeMatrixBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> nodeMatrixBufferMemory_;
		
		std::unique_ptr<Vulkan::Buffer> indirectDrawBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> indirectDrawBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> ambientCubeBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> ambientCubeBufferMemory_;
		
		std::unique_ptr<Vulkan::Buffer> farAmbientCubeBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> farAmbientCubeBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> pageIndexBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> pageIndexBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> hdrSHBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> hdrSHBufferMemory_;

		std::unique_ptr<TextureImage> cpuShadowMap_;
		
		std::unique_ptr<Vulkan::DescriptorSetManager> sceneBufferDescriptorSetManager_;
		
		uint32_t lightCount_ {};
		uint32_t indicesCount_ {};
		uint32_t verticeCount_ {};
		uint32_t indirectDrawBatchCount_ {};

		mutable uint32_t selectedId_ = -1;

		bool sceneDirty_ = true;
		
		std::vector<NodeProxy> nodeProxys;
		std::vector<VkDrawIndexedIndirectCommand> indirectDrawBufferInstanced;

		glm::mat4 overrideModelView;
		bool requestOverrideModelView = false;
		
		Assets::EnvironmentSetting envSettings_;
		Camera renderCamera_;

		FCPUAccelerationStructure cpuAccelerationStructure_;
	};
}
