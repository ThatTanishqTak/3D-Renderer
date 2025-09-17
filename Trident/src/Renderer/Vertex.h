#pragma once

#include <array>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

struct Vertex
{
    glm::vec3 Position;
    glm::vec3 Normal;      // Surface normal used for lighting calculations
    glm::vec3 Tangent;     // Tangent vector required for normal mapping
    glm::vec3 Bitangent;   // Bitangent reconstructed from tangent and normal when available
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

    static std::array<VkVertexInputAttributeDescription, 6> GetAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 6> l_Attributes{};

        l_Attributes[0].binding = 0;
        l_Attributes[0].location = 0;
        l_Attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        l_Attributes[0].offset = offsetof(Vertex, Position);

        l_Attributes[1].binding = 0;
        l_Attributes[1].location = 1;
        l_Attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        l_Attributes[1].offset = offsetof(Vertex, Normal);

        l_Attributes[2].binding = 0;
        l_Attributes[2].location = 2;
        l_Attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        l_Attributes[2].offset = offsetof(Vertex, Tangent);

        l_Attributes[3].binding = 0;
        l_Attributes[3].location = 3;
        l_Attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        l_Attributes[3].offset = offsetof(Vertex, Bitangent);

        l_Attributes[4].binding = 0;
        l_Attributes[4].location = 4;
        l_Attributes[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        l_Attributes[4].offset = offsetof(Vertex, Color);

        l_Attributes[5].binding = 0;
        l_Attributes[5].location = 5;
        l_Attributes[5].format = VK_FORMAT_R32G32_SFLOAT;
        l_Attributes[5].offset = offsetof(Vertex, TexCoord);

        return l_Attributes;
    }
};