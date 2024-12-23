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

		static VkVertexInputBindingDescription GetBindingDescription()
		{
			VkVertexInputBindingDescription bindingDescription = {};
			bindingDescription.binding = 0;
			bindingDescription.stride = sizeof(Vertex);
			bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDescription;
		}

		static std::array<VkVertexInputAttributeDescription, 5> GetAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions = {};

			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex, Position);

			attributeDescriptions[1].binding = 0;
			attributeDescriptions[1].location = 1;
			attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[1].offset = offsetof(Vertex, Normal);

			attributeDescriptions[2].binding = 0;
			attributeDescriptions[2].location = 2;
			attributeDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
			attributeDescriptions[2].offset = offsetof(Vertex, Tangent);

			attributeDescriptions[3].binding = 0;
			attributeDescriptions[3].location = 3;
			attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
			attributeDescriptions[3].offset = offsetof(Vertex, TexCoord);

			attributeDescriptions[4].binding = 0;
			attributeDescriptions[4].location = 4;
			attributeDescriptions[4].format = VK_FORMAT_R32_UINT;
			attributeDescriptions[4].offset = offsetof(Vertex, MaterialIndex);

			return attributeDescriptions;
		}

		static std::array<VkVertexInputAttributeDescription, 1> GetFastAttributeDescriptions()
		{
			std::array<VkVertexInputAttributeDescription, 1> attributeDescriptions = {};

			attributeDescriptions[0].binding = 0;
			attributeDescriptions[0].location = 0;
			attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
			attributeDescriptions[0].offset = offsetof(Vertex, Position);
			
			return attributeDescriptions;
		}
	};

}
