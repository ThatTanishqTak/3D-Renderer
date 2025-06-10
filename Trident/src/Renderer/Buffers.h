#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"

namespace Trident
{
    class Buffers
    {
    public:
        void Cleanup();

        void CreateVertexBuffer(const std::vector<Vertex>& vertices, VkCommandPool commandPool, VkBuffer& vertexBuffer, VkDeviceMemory& vertexBufferMemory);
        void CreateIndexBuffer(const std::vector<uint16_t>& indices, VkCommandPool commandPool, VkBuffer& indexBuffer, VkDeviceMemory& indexBufferMemory, uint32_t& indexCount);
        void CreateUniformBuffers(uint32_t imageCount, std::vector<VkBuffer>& uniformBuffers, std::vector<VkDeviceMemory>& uniformBuffersMemory);

    private:
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, VkCommandPool commandPool);

        struct Allocation
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VkDeviceMemory Memory = VK_NULL_HANDLE;
        };

        std::vector<Allocation> m_Allocations;
    };
}