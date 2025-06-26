#pragma once

#include <array>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct Vertex
{
	glm::vec3 Position;
	glm::vec3 Color;
	glm::vec2 TexCoord;

	static VkVertexInputBindingDescription GetBindingDescription()
	{
		VkVertexInputBindingDescription l_Binding{};

		l_Binding.binding = 0;
		l_Binding.stride = sizeof(Vertex);
		l_Binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		
		return l_Binding;
	}

	static std::array<VkVertexInputAttributeDescription, 3> GetAttributeDescriptions()
	{
		std::array<VkVertexInputAttributeDescription, 3> l_Attributes{};

		l_Attributes[0].binding = 0;
		l_Attributes[0].location = 0;
		l_Attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		l_Attributes[0].offset = offsetof(Vertex, Position);

		l_Attributes[1].binding = 0;
		l_Attributes[1].location = 1;
		l_Attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		l_Attributes[1].offset = offsetof(Vertex, Color);

		l_Attributes[2].binding = 0;
		l_Attributes[2].location = 2;
		l_Attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		l_Attributes[2].offset = offsetof(Vertex, TexCoord);

		return l_Attributes;
	}
};