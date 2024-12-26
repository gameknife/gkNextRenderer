#pragma once

#include "Utilities/Glm.hpp"
#include "Vulkan/Vulkan.hpp"
#include <array>

namespace Assets
{
	struct Vertex final
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec4 Tangent;
		glm::vec2 TexCoord;
		glm::uint MaterialIndex;

		bool operator==(const Vertex& other) const
		{
			return 
				Position == other.Position &&
				Normal == other.Normal &&
				Tangent == other.Tangent &&
				TexCoord == other.TexCoord &&
				MaterialIndex == other.MaterialIndex;
		}
	};
	
	inline uint32_t packUint16(uint16_t a, uint16_t b) {
		return (static_cast<uint32_t>(a) << 16) | static_cast<uint32_t>(b);
	}
	
	struct GPUVertex final
	{
		glm::vec3 Position;
		glm::uint TexcoordXY;
		glm::uint NormalXY;
		glm::uint NormalZTangentX;
		glm::uint TangentYZ;
		glm::uint TangentWMatIdx;

		bool operator==(const GPUVertex& other) const
		{
			return 
				Position == other.Position &&
				TexcoordXY == other.TexcoordXY &&
				NormalXY == other.NormalXY &&
				NormalZTangentX == other.NormalZTangentX &&
				TangentYZ == other.TangentYZ &&
				TangentWMatIdx == other.TangentWMatIdx;
		}

		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(GPUVertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDescription;
		}

		static std::array<VkVertexInputAttributeDescription, 6> GetAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 6> attributeDescriptions = {};

			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(GPUVertex, Position);

			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32_UINT;
			attributeDescriptions[1].offset = offsetof(GPUVertex, TexcoordXY);

			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R32_UINT;
			attributeDescriptions[2].offset = offsetof(GPUVertex, NormalXY);

			attributeDescriptions[3].binding = 0;
			attributeDescriptions[3].location = 3;
			attributeDescriptions[3].format = VK_FORMAT_R32_UINT;
			attributeDescriptions[3].offset = offsetof(GPUVertex, NormalZTangentX);

			attributeDescriptions[4].binding = 0;
			attributeDescriptions[4].location = 4;
			attributeDescriptions[4].format = VK_FORMAT_R32_UINT;
			attributeDescriptions[4].offset = offsetof(GPUVertex, TangentYZ);

			attributeDescriptions[5].binding = 0;
			attributeDescriptions[5].location = 5;
			attributeDescriptions[5].format = VK_FORMAT_R32_UINT;
			attributeDescriptions[5].offset = offsetof(GPUVertex, TangentWMatIdx);
			
			return attributeDescriptions;
		}

		static std::array<VkVertexInputAttributeDescription, 1> GetFastAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions = {};

			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(GPUVertex, Position);
			
			return attributeDescriptions;
		}
	};

	inline GPUVertex MakeVertex(Vertex& cpuVertex)
	{
		GPUVertex vertex;
		vertex.Position = cpuVertex.Position;
		vertex.TexcoordXY = glm::packHalf2x16(cpuVertex.TexCoord);
		vertex.NormalXY = glm::packHalf2x16(glm::vec2(cpuVertex.Normal));
		vertex.NormalZTangentX = glm::packHalf2x16(glm::vec2(cpuVertex.Normal.z, cpuVertex.Tangent.x));
		vertex.TangentYZ = glm::packHalf2x16(glm::vec2(cpuVertex.Tangent.y, cpuVertex.Tangent.z));
		vertex.TangentWMatIdx = packUint16(cpuVertex.Tangent.w > 0 ? 2 : 0, static_cast<uint16_t>(cpuVertex.MaterialIndex));
		return vertex;
	}

}
