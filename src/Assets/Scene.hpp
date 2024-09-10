#pragma once

#include "Vulkan/Vulkan.hpp"
#include <memory>
#include <vector>
#include <glm/vec2.hpp>

namespace Vulkan
{
	class Buffer;
	class CommandPool;
	class DeviceMemory;
	class Image;
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

		Scene(Vulkan::CommandPool& commandPool,
			std::vector<Node>& nodes,
			std::vector<Model>& models,
			std::vector<Material>& materials,
			std::vector<LightObject>& lights,
			bool supportRayTracing);
		~Scene();

		const std::vector<Node>& Nodes() const { return nodes_; }
		const std::vector<Model>& Models() const { return models_; }
		std::vector<Material>& Materials() { return materials_; }
		const std::vector<glm::uvec2>& Offsets() const { return offsets_; }
		
		
		bool HasProcedurals() const { return static_cast<bool>(proceduralBuffer_); }

		const Vulkan::Buffer& VertexBuffer() const { return *vertexBuffer_; }
		const Vulkan::Buffer& IndexBuffer() const { return *indexBuffer_; }
		const Vulkan::Buffer& MaterialBuffer() const { return *materialBuffer_; }
		const Vulkan::Buffer& OffsetsBuffer() const { return *offsetBuffer_; }
		const Vulkan::Buffer& AabbBuffer() const { return *aabbBuffer_; }
		const Vulkan::Buffer& ProceduralBuffer() const { return *proceduralBuffer_; }
		const Vulkan::Buffer& LightBuffer() const { return *lightBuffer_; }
		const Vulkan::Buffer& NodeMatrixBuffer() const { return *nodeMatrixBuffer_; }
		const Vulkan::Buffer& IndirectDrawBuffer() const { return *indirectDrawBuffer_; }

		const std::vector<uint32_t>& ModelInstanceCount() const { return model_instance_count_; }

		const uint32_t GetLightCount() const {return lightCount_;}
		const uint32_t GetIndicesCount() const {return indicesCount_;}
		const uint32_t GetVerticeCount() const {return verticeCount_;}
		const uint32_t GetIndirectDrawBatchCount() const {return indirectDrawBatchCount_;}
		
		uint32_t GetSelectedId() const { return selectedId_; }
		void SetSelectedId( uint32_t id ) const { selectedId_ = id; }

		void UpdateMaterial();
		
	private:
		std::vector<Material> materials_;
		const std::vector<Model> models_;
		const std::vector<Node> nodes_;
		std::vector<glm::uvec2> offsets_;
		std::vector<uint32_t> model_instance_count_;

		std::unique_ptr<Vulkan::Buffer> vertexBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> vertexBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> indexBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> indexBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> materialBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> materialBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> offsetBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> offsetBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> aabbBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> aabbBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> proceduralBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> proceduralBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> lightBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> lightBufferMemory_;

		std::unique_ptr<Vulkan::Buffer> nodeMatrixBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> nodeMatrixBufferMemory_;
		
		std::unique_ptr<Vulkan::Buffer> indirectDrawBuffer_;
		std::unique_ptr<Vulkan::DeviceMemory> indirectDrawBufferMemory_;

		uint32_t lightCount_ {};
		uint32_t indicesCount_ {};
		uint32_t verticeCount_ {};
		uint32_t indirectDrawBatchCount_ {};

		mutable uint32_t selectedId_ = -1;
	};

}
