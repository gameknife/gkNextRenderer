#include "Scene.hpp"
#include "Model.hpp"
#include "Sphere.hpp"
#include "Vulkan/BufferUtil.hpp"


namespace Assets {

Scene::Scene(Vulkan::CommandPool& commandPool,
	std::vector<Node>& nodes,
	std::vector<Model>& models,
	std::vector<Material>& materials,
	std::vector<LightObject>& lights,
	bool supportRayTracing) :
	materials_(std::move(materials)),
	models_(std::move(models)),
	nodes_(std::move(nodes))
{
	// Concatenate all the models
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	
	std::vector<glm::vec4> procedurals;
	std::vector<VkAabbPositionsKHR> aabbs;
	
	for (auto& model : models_)
	{
		// Remember the index, vertex offsets.
		const auto indexOffset = static_cast<uint32_t>(indices.size());
		const auto vertexOffset = static_cast<uint32_t>(vertices.size());

		offsets_.emplace_back(indexOffset, vertexOffset);

		// Copy model data one after the other.
		vertices.insert(vertices.end(), model.Vertices().begin(), model.Vertices().end());
		indices.insert(indices.end(), model.Indices().begin(), model.Indices().end());

		// Add optional procedurals.
		const auto* const sphere = dynamic_cast<const Sphere*>(model.Procedural());
		if (sphere != nullptr)
		{
			const auto aabb = sphere->BoundingBox();
			aabbs.push_back({aabb.first.x, aabb.first.y, aabb.first.z, aabb.second.x, aabb.second.y, aabb.second.z});
			procedurals.emplace_back(sphere->Center, sphere->Radius);
		}
		else
		{
			aabbs.emplace_back();
			procedurals.emplace_back();
		}
	}

	// node should sort by models, for instancing rendering
	std::vector<NodeProxy> nodeProxys;
	std::vector<VkDrawIndexedIndirectCommand> indirectDrawBuffer;
	std::vector<VkDrawIndexedIndirectCommand> indirectDrawBufferInstanced;
	
	uint32_t indexOffset = 0;
	uint32_t vertexOffset = 0;
	uint32_t nodeOffset = 0;
	uint32_t nodeOffsetBatched = 0;
	int modelCount = static_cast<int>(models_.size());
	for (int i = 0; i < modelCount; i++)
	{	
		uint32_t instanceCountOfThisModel = 0;
		for (const auto& node : nodes_)
		{
			if(node.GetModel() == i)
			{
				nodeProxys.push_back({ node.WorldTransform() });

				// draw indirect buffer, one by one
				VkDrawIndexedIndirectCommand cmd{};
				cmd.firstIndex    = indexOffset;
				cmd.indexCount    = static_cast<uint32_t>(models_[i].Indices().size());
				cmd.vertexOffset  = static_cast<int32_t>(vertexOffset);
				cmd.firstInstance = nodeOffset;
				cmd.instanceCount = 1;

				indirectDrawBuffer.push_back(cmd);
				instanceCountOfThisModel++;
				nodeOffset++;
			}
		}
		// draw indirect buffer, instanced
		VkDrawIndexedIndirectCommand cmd{};
		cmd.firstIndex    = indexOffset;
		cmd.indexCount    = static_cast<uint32_t>(models_[i].Indices().size());
		cmd.vertexOffset  = static_cast<int32_t>(vertexOffset);
		cmd.firstInstance = nodeOffsetBatched;
		cmd.instanceCount = instanceCountOfThisModel;

		indirectDrawBufferInstanced.push_back(cmd);
		
		indexOffset += static_cast<uint32_t>(models_[i].Indices().size());
		vertexOffset += static_cast<uint32_t>(models_[i].Vertices().size());
		nodeOffsetBatched += instanceCountOfThisModel;
	}
	
	int flags = supportRayTracing ? (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	int rtxFlags = supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;
	
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Vertices", VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | rtxFlags | flags, vertices, vertexBuffer_, vertexBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Indices", VK_BUFFER_USAGE_INDEX_BUFFER_BIT | rtxFlags | flags, indices, indexBuffer_, indexBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Materials", flags, materials_, materialBuffer_, materialBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Offsets", flags, offsets_, offsetBuffer_, offsetBufferMemory_);

	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "AABBs", rtxFlags | flags, aabbs, aabbBuffer_, aabbBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Procedurals", flags, procedurals, proceduralBuffer_, proceduralBufferMemory_);

	Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Lights", flags, lights, lightBuffer_, lightBufferMemory_);

	//Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "Nodes", flags, nodeProxys, nodeMatrixBuffer_, nodeMatrixBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "Nodes", flags, sizeof(NodeProxy) * 65535, nodeMatrixBuffer_, nodeMatrixBufferMemory_); // support 65535 nodes
	Vulkan::BufferUtil::CreateDeviceBufferViolate(commandPool, "SimpleNodes", flags, sizeof(NodeSimpleProxy) * 65535, nodeSimpleMatrixBuffer_, nodeSimpleMatrixBufferMemory_); // support 65535 nodes
	
	//Vulkan::BufferUtil::CreateDeviceBuffer(commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, indirectDrawBufferInstanced, indirectDrawBuffer_, indirectDrawBufferMemory_);
	Vulkan::BufferUtil::CreateDeviceBufferViolate( commandPool, "IndirectDraws", flags | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, sizeof(VkDrawIndexedIndirectCommand) * 65535, indirectDrawBuffer_, indirectDrawBufferMemory_); // support 65535 nodes
	
	lightCount_ = static_cast<uint32_t>(lights.size());
	indicesCount_ = static_cast<uint32_t>(indices.size());
	verticeCount_ = static_cast<uint32_t>(vertices.size());
}

Scene::~Scene()
{
	proceduralBuffer_.reset();
	proceduralBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	aabbBuffer_.reset();
	aabbBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	offsetBuffer_.reset();
	offsetBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	materialBuffer_.reset();
	materialBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	indexBuffer_.reset();
	indexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	vertexBuffer_.reset();
	vertexBufferMemory_.reset(); // release memory after bound buffer has been destroyed
	lightBuffer_.reset();
	lightBufferMemory_.reset();
	indirectDrawBuffer_.reset();
	indirectDrawBufferMemory_.reset();
	
	nodeSimpleMatrixBuffer_.reset();
	nodeSimpleMatrixBufferMemory_.reset();
	nodeMatrixBuffer_.reset();
	nodeMatrixBufferMemory_.reset();
}

void Scene::UpdateMaterial()
{
	// update value after binding, like the bindless textures, try
}

bool Scene::UpdateNodes()
{
	if(nodes_.size() > 0)
	{
		if(sceneDirty_)
		{
			sceneDirty_ = false;
			{
				// this is a fast node proxy for ray tracing, plain order as nodes
				nodeSimpleProxys.clear();
				nodeSimpleProxys.reserve(nodes_.size());
				for (auto& node : nodes_)
				{
					if (node.IsVisible())
					{
						
					}
				}

			}
			
			{
				nodeProxys.clear();
				indirectDrawBufferInstanced.clear();
				uint32_t indexOffset = 0;
				uint32_t vertexOffset = 0;
				uint32_t nodeOffsetBatched = 0;

				// 这里相当于是对nodes的一个排序，用这个顺序来喂给ray tracing感觉会更稳定
				int modelCount = static_cast<int>(models_.size());
				for (int i = 0; i < modelCount; i++)
				{
					// sort by model, one model one idc
					uint32_t instanceCountOfThisModel = 0;
					for (auto& node : nodes_)
					{
						if(node.GetModel() == i && node.IsVisible())
						{
							glm::vec3 delta = node.TickVelocity();
							if(glm::length(delta) > 0.01f)
							{
								sceneDirty_ = true;
							}
							nodeSimpleProxys.push_back({ node.GetInstanceId(), node.GetModel(), 0u, 0u, glm::vec4(delta, 0) });
							nodeProxys.push_back({ node.WorldTransform() });
							instanceCountOfThisModel++;
						}
					}
					// draw indirect buffer, instanced
					VkDrawIndexedIndirectCommand cmd{};
					cmd.firstIndex    = indexOffset;
					cmd.indexCount    = static_cast<uint32_t>(models_[i].Indices().size());
					cmd.vertexOffset  = static_cast<int32_t>(vertexOffset);
					cmd.firstInstance = nodeOffsetBatched;
					cmd.instanceCount = instanceCountOfThisModel;

					indirectDrawBufferInstanced.push_back(cmd);
		
					indexOffset += static_cast<uint32_t>(models_[i].Indices().size());
					vertexOffset += static_cast<uint32_t>(models_[i].Vertices().size());
					nodeOffsetBatched += instanceCountOfThisModel;
				}

				NodeSimpleProxy* simpleProxy = reinterpret_cast<NodeSimpleProxy*>(nodeSimpleMatrixBufferMemory_->Map(0, sizeof(NodeSimpleProxy) * nodeSimpleProxys.size()));
				std::memcpy(simpleProxy, nodeSimpleProxys.data(), nodeSimpleProxys.size() * sizeof(NodeSimpleProxy));
				nodeSimpleMatrixBufferMemory_->Unmap();
				
				NodeProxy* data = reinterpret_cast<NodeProxy*>(nodeMatrixBufferMemory_->Map(0, sizeof(NodeProxy) * nodeProxys.size()));
				std::memcpy(data, nodeProxys.data(), nodeProxys.size() * sizeof(NodeProxy));
				nodeMatrixBufferMemory_->Unmap();
				
				VkDrawIndexedIndirectCommand* diic = reinterpret_cast<VkDrawIndexedIndirectCommand*>(indirectDrawBufferMemory_->Map(0, sizeof(VkDrawIndexedIndirectCommand) * indirectDrawBufferInstanced.size()));
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
		if (node.GetName() == name)
		{
			return &node;
		}
	}
	return nullptr;
}

Node* Scene::GetNodeByInstanceId(uint32_t id)
{
	for (auto& node : nodes_)
	{
		if (node.GetInstanceId() == id)
		{
			return &node;
		}
	}
	return nullptr;
}

const Model* Scene::GetModel(uint32_t id) const
{
	if( id < models_.size() )
	{
		return &models_[id];
	}
	return nullptr;
}

const Material* Scene::GetMaterial(uint32_t id) const
{
	if( id < materials_.size() )
	{
		return &materials_[id];
	}
	return nullptr;
}
}
