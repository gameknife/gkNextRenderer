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
	
	inline uint16_t packUint8(uint16_t a, uint16_t b) {
		return a << 8 | b;
	}
	
	struct GPUVertex final
	{
		glm::detail::hdata posx;
		glm::detail::hdata posy;
		glm::detail::hdata posz;
		glm::detail::hdata texcoordx;
		
		glm::detail::hdata normalx;
		glm::detail::hdata normaly;
		glm::detail::hdata normalz;
		glm::detail::hdata texcoordy;
		
		glm::detail::hdata tangentx;
		glm::detail::hdata tangenty;
		glm::detail::hdata tangentz;
		uint16_t tangentw;
		
		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(GPUVertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDescription;
		}

		static VkVertexInputBindingDescription GetFastBindingDescription()
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(short) * 4;
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDescription;
		}
		
		static std::array<VkVertexInputAttributeDescription, 1> GetFastAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions = {};

			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R16G16B16_SFLOAT;
			attributeDescriptions[0].offset = 0;
			
			return attributeDescriptions;
		}
	};

	inline GPUVertex MakeVertex(Vertex& cpuVertex)
	{
		GPUVertex vertex;
		vertex.posx = glm::detail::toFloat16(cpuVertex.Position.x);
		vertex.posy = glm::detail::toFloat16(cpuVertex.Position.y);
		vertex.posz = glm::detail::toFloat16(cpuVertex.Position.z);
		vertex.texcoordx = glm::detail::toFloat16(cpuVertex.TexCoord.x);
		
		vertex.normalx = glm::detail::toFloat16(cpuVertex.Normal.x);
		vertex.normaly = glm::detail::toFloat16(cpuVertex.Normal.y);
		vertex.normalz = glm::detail::toFloat16(cpuVertex.Normal.z);
		vertex.texcoordy = glm::detail::toFloat16(cpuVertex.TexCoord.y);

		vertex.tangentx = glm::detail::toFloat16(cpuVertex.Tangent.x);
		vertex.tangenty = glm::detail::toFloat16(cpuVertex.Tangent.y);
		vertex.tangentz = glm::detail::toFloat16(cpuVertex.Tangent.z);

		vertex.tangentw = packUint8(cpuVertex.Tangent.w > 0 ? 2 : 0, static_cast<uint16_t>(cpuVertex.MaterialIndex));
		return vertex;
	}

}
