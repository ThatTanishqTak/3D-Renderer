#include "Renderer/Buffers.h"

#include "Application.h"
#include "Core/Utilities.h"

#include <algorithm>

namespace Trident
{
    void Buffers::Cleanup()
    {
        for (auto& it_Allocation : m_Allocations)
        {
            if (it_Allocation.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Application::GetDevice(), it_Allocation.Buffer, nullptr);
            }
        
            if (it_Allocation.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(Application::GetDevice(), it_Allocation.Memory, nullptr);
            }
        }

        m_Allocations.clear();
    }

    void Buffers::CreateVertexBuffer(const std::vector<Vertex>& vertices, CommandBufferPool& pool, VkBuffer& vertexBuffer, VkDeviceMemory& vertexBufferMemory)
    {
        TR_CORE_TRACE("Creating Vertex Buffer");

        if (vertices.empty())
        {
            TR_CORE_WARN("No vertices provided - skipping vertex buffer creation");
            vertexBuffer = VK_NULL_HANDLE;
            vertexBufferMemory = VK_NULL_HANDLE;

            return;
        }

        VkDeviceSize l_BufferSize = sizeof(Vertex) * vertices.size();

        VkBuffer l_StagingBuffer;
        VkDeviceMemory l_StagingBufferMemory;
        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingBufferMemory);

        void* l_Data;
        vkMapMemory(Application::GetDevice(), l_StagingBufferMemory, 0, l_BufferSize, 0, &l_Data);
        memcpy(l_Data, vertices.data(), static_cast<size_t>(l_BufferSize));
        vkUnmapMemory(Application::GetDevice(), l_StagingBufferMemory);

        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
        if (vertexBuffer != VK_NULL_HANDLE)
        {
            CopyBuffer(l_StagingBuffer, vertexBuffer, l_BufferSize, pool);
        }

        vkDestroyBuffer(Application::GetDevice(), l_StagingBuffer, nullptr);
        vkFreeMemory(Application::GetDevice(), l_StagingBufferMemory, nullptr);

        if (vertexBuffer != VK_NULL_HANDLE)
        {
            m_Allocations.push_back({ vertexBuffer, vertexBufferMemory });
        }
    }

    void Buffers::CreateIndexBuffer(const std::vector<uint32_t>& indices, CommandBufferPool& pool, VkBuffer& indexBuffer, VkDeviceMemory& indexBufferMemory, uint32_t& indexCount)
    {
        TR_CORE_TRACE("Creating Index Buffer");

        indexCount = static_cast<uint32_t>(indices.size());
        if (indices.empty())
        {
            TR_CORE_WARN("No indices provided - skipping index buffer creation");
            indexBuffer = VK_NULL_HANDLE;
            indexBufferMemory = VK_NULL_HANDLE;

            return;
        }

        VkDeviceSize l_BufferSize = sizeof(indices[0]) * indices.size();

        VkBuffer l_StagingBuffer;
        VkDeviceMemory l_StagingBufferMemory;
        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingBufferMemory);

        void* l_Data;
        vkMapMemory(Application::GetDevice(), l_StagingBufferMemory, 0, l_BufferSize, 0, &l_Data);
        memcpy(l_Data, indices.data(), static_cast<size_t>(l_BufferSize));
        vkUnmapMemory(Application::GetDevice(), l_StagingBufferMemory);

        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);
        if (indexBuffer != VK_NULL_HANDLE)
        {
            CopyBuffer(l_StagingBuffer, indexBuffer, l_BufferSize, pool);
        }

        vkDestroyBuffer(Application::GetDevice(), l_StagingBuffer, nullptr);
        vkFreeMemory(Application::GetDevice(), l_StagingBufferMemory, nullptr);

        if (indexBuffer != VK_NULL_HANDLE)
        {
            m_Allocations.push_back({ indexBuffer, indexBufferMemory });
        }
    }

    void Buffers::CreateUniformBuffers(uint32_t imageCount, std::vector<VkBuffer>& uniformBuffers, std::vector<VkDeviceMemory>& uniformBuffersMemory)
    {
        TR_CORE_TRACE("Creating Uniform Buffers");

        VkDeviceSize l_BufferSize = sizeof(UniformBufferObject);

        uniformBuffers.resize(imageCount);
        uniformBuffersMemory.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                uniformBuffers[i], uniformBuffersMemory[i]);
            m_Allocations.push_back({ uniformBuffers[i], uniformBuffersMemory[i] });
        }

        TR_CORE_TRACE("Uniform Buffers Created ({} Buffers)", uniformBuffers.size());
    }

    uint32_t Buffers::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties l_MemoryProperties;
        vkGetPhysicalDeviceMemoryProperties(Application::GetPhysicalDevice(), &l_MemoryProperties);

        for (uint32_t i = 0; i < l_MemoryProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (l_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        TR_CORE_CRITICAL("Failed to find suitable memory type");

        return EXIT_FAILURE;
    }

    //----------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Buffers::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
    {
        if (size == 0)
        {
            TR_CORE_WARN("Attempted to create buffer with size 0");
            buffer = VK_NULL_HANDLE;
            bufferMemory = VK_NULL_HANDLE;
            
            return;
        }
        VkBufferCreateInfo l_BufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        l_BufferInfo.size = size;
        l_BufferInfo.usage = usage;
        l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult l_Result = vkCreateBuffer(Application::GetDevice(), &l_BufferInfo, nullptr, &buffer);
        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("vkCreateBuffer failed(code {}) for size = {} usage = 0x{:x}", static_cast<int>(l_Result), static_cast<uint64_t>(size), static_cast<uint64_t>(usage));
        }

        VkMemoryRequirements l_MemoryRequirements;
        vkGetBufferMemoryRequirements(Application::GetDevice(), buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = FindMemoryType(l_MemoryRequirements.memoryTypeBits, properties);

        l_Result = vkAllocateMemory(Application::GetDevice(), &l_AllocateInfo, nullptr, &bufferMemory);
        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("vkAllocateMemory failed(code {}) for size = {}", static_cast<int>(l_Result), static_cast<uint64_t>(l_MemoryRequirements.size));
        }

        vkBindBufferMemory(Application::GetDevice(), buffer, bufferMemory, 0);
    }

    void Buffers::CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, CommandBufferPool& pool)
    {
        VkCommandBuffer l_CommandBuffer = pool.Acquire();

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);
        VkBufferCopy l_CopyRegion{};
        l_CopyRegion.srcOffset = 0;
        l_CopyRegion.dstOffset = 0;
        l_CopyRegion.size = size;
        vkCmdCopyBuffer(l_CommandBuffer, srcBuffer, dstBuffer, 1, &l_CopyRegion);
        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Application::GetGraphicsQueue());

        pool.Release(l_CommandBuffer);
    }

    void Buffers::DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory)
    {
        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Application::GetDevice(), buffer, nullptr);
        }

        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Application::GetDevice(), memory, nullptr);
        }

        auto it = std::find_if(m_Allocations.begin(), m_Allocations.end(),
            [&](const Allocation& alloc)
            {
                return alloc.Buffer == buffer && alloc.Memory == memory;
            });

        if (it != m_Allocations.end())
        {
            m_Allocations.erase(it);
        }
    }
}