#pragma once

#include <vulkan/vulkan.h>

#include <vector>
#include <cstddef>
#include <cstdint>

#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/CommandBufferPool.h"

namespace Trident
{
    class Buffers
    {
    public:
        void Cleanup();

        void CreateVertexBuffer(const std::vector<Vertex>& vertices, CommandBufferPool& pool, VkBuffer& vertexBuffer, VkDeviceMemory& vertexBufferMemory);
        void CreateVertexBuffer(const void* vertexData, size_t vertexCount, size_t vertexStride, CommandBufferPool& pool, VkBuffer& vertexBuffer, VkDeviceMemory& vertexBufferMemory);
        void CreateIndexBuffer(const std::vector<uint32_t>& indices, CommandBufferPool& pool, VkBuffer& indexBuffer, VkDeviceMemory& indexBufferMemory, uint32_t& indexCount);
        void CreateUniformBuffers(uint32_t imageCount, VkDeviceSize bufferSize, std::vector<VkBuffer>& uniformBuffers, std::vector<VkDeviceMemory>& uniformBuffersMemory);


        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, CommandBufferPool& pool);
        void DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory);

    private:
        // Utility helpers


    private:
        struct Allocation
        {
            VkBuffer Buffer = VK_NULL_HANDLE;
            VkDeviceMemory Memory = VK_NULL_HANDLE;
        };

        std::vector<Allocation> m_Allocations;
    };
}