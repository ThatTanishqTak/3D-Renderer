#include "Renderer/Buffers.h"

#include "Application/Startup.h"
#include "Core/Utilities.h"

#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace Trident
{
    void Buffers::Cleanup()
    {
        for (auto& it_Allocation : m_Allocations)
        {
            if (it_Allocation.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Startup::GetDevice(), it_Allocation.Buffer, nullptr);
            }
        
            if (it_Allocation.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(Startup::GetDevice(), it_Allocation.Memory, nullptr);
            }
        }

        m_Allocations.clear();
    }

    void Buffers::CreateVertexBuffer(const std::vector<Vertex>& vertices, CommandBufferPool& pool, VkBuffer& vertexBuffer, VkDeviceMemory& vertexBufferMemory)
    {
        CreateVertexBuffer(vertices.data(), vertices.size(), sizeof(Vertex), pool, vertexBuffer, vertexBufferMemory);
    }

    void Buffers::CreateVertexBuffer(const void* vertexData, size_t vertexCount, size_t vertexStride, CommandBufferPool& pool, VkBuffer& vertexBuffer, VkDeviceMemory& vertexBufferMemory)
    {
        TR_CORE_TRACE("Creating Vertex Buffer");

        if (vertexData == nullptr || vertexCount == 0 || vertexStride == 0)
        {
            TR_CORE_WARN("Vertex buffer creation skipped because the input stream was empty or invalid");
            vertexBuffer = VK_NULL_HANDLE;
            vertexBufferMemory = VK_NULL_HANDLE;

            return;
        }

        VkDeviceSize l_BufferSize = static_cast<VkDeviceSize>(vertexCount) * static_cast<VkDeviceSize>(vertexStride);

        VkBuffer l_StagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory l_StagingBufferMemory = VK_NULL_HANDLE;
        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingBufferMemory);

        void* l_Data = nullptr;
        vkMapMemory(Startup::GetDevice(), l_StagingBufferMemory, 0, l_BufferSize, 0, &l_Data);
        std::memcpy(l_Data, vertexData, static_cast<size_t>(l_BufferSize));
        vkUnmapMemory(Startup::GetDevice(), l_StagingBufferMemory);

        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);
        if (vertexBuffer != VK_NULL_HANDLE)
        {
            CopyBuffer(l_StagingBuffer, vertexBuffer, l_BufferSize, pool);
        }

        vkDestroyBuffer(Startup::GetDevice(), l_StagingBuffer, nullptr);
        vkFreeMemory(Startup::GetDevice(), l_StagingBufferMemory, nullptr);

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
        vkMapMemory(Startup::GetDevice(), l_StagingBufferMemory, 0, l_BufferSize, 0, &l_Data);
        memcpy(l_Data, indices.data(), static_cast<size_t>(l_BufferSize));
        vkUnmapMemory(Startup::GetDevice(), l_StagingBufferMemory);

        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);
        if (indexBuffer != VK_NULL_HANDLE)
        {
            CopyBuffer(l_StagingBuffer, indexBuffer, l_BufferSize, pool);
        }

        vkDestroyBuffer(Startup::GetDevice(), l_StagingBuffer, nullptr);
        vkFreeMemory(Startup::GetDevice(), l_StagingBufferMemory, nullptr);

        if (indexBuffer != VK_NULL_HANDLE)
        {
            m_Allocations.push_back({ indexBuffer, indexBufferMemory });
        }
    }

    void Buffers::CreateUniformBuffers(uint32_t imageCount, VkDeviceSize bufferSize, std::vector<VkBuffer>& uniformBuffers, std::vector<VkDeviceMemory>& uniformBuffersMemory)
    {
        TR_CORE_TRACE("Creating Uniform Buffers");

        if (bufferSize == 0)
        {
            TR_CORE_WARN("Requested uniform buffer of size 0; skipping allocation");
            uniformBuffers.clear();
            uniformBuffersMemory.clear();

            return;
        }

        uniformBuffers.resize(imageCount);
        uniformBuffersMemory.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                uniformBuffers[i], uniformBuffersMemory[i]);
            m_Allocations.push_back({ uniformBuffers[i], uniformBuffersMemory[i] });
        }

        TR_CORE_TRACE("Uniform Buffers Created ({} Buffers)", uniformBuffers.size());
    }

    void Buffers::CreateStorageBuffers(uint32_t imageCount, VkDeviceSize bufferSize, std::vector<VkBuffer>& storageBuffers, std::vector<VkDeviceMemory>& storageBuffersMemory)
    {
        TR_CORE_TRACE("Creating Storage Buffers");

        if (bufferSize == 0)
        {
            TR_CORE_WARN("Requested storage buffer of size 0; skipping allocation");
            storageBuffers.clear();
            storageBuffersMemory.clear();

            return;
        }

        storageBuffers.resize(imageCount);
        storageBuffersMemory.resize(imageCount);

        for (uint32_t i = 0; i < imageCount; ++i)
        {
            CreateBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, storageBuffers[i], storageBuffersMemory[i]);
            m_Allocations.push_back({ storageBuffers[i], storageBuffersMemory[i] });
        }

        TR_CORE_TRACE("Storage Buffers Created ({} Buffers)", storageBuffers.size());
    }


    uint32_t Buffers::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        // Gather the available memory types so we can select a compatible one for the requested usage.
        VkPhysicalDeviceMemoryProperties l_MemoryProperties;
        vkGetPhysicalDeviceMemoryProperties(Startup::GetPhysicalDevice(), &l_MemoryProperties);

        for (uint32_t i = 0; i < l_MemoryProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (l_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        // Log detailed diagnostics before signaling failure so future investigations know what was requested.
        TR_CORE_CRITICAL("Failed to find suitable memory type (typeFilter = 0x{:x}, properties = 0x{:x})", static_cast<uint64_t>(typeFilter), static_cast<uint64_t>(properties));

        // Signal the failure so callers can handle the situation gracefully instead of using an invalid index.
        throw std::runtime_error("Failed to find suitable memory type");
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

        VkResult l_Result = vkCreateBuffer(Startup::GetDevice(), &l_BufferInfo, nullptr, &buffer);
        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("vkCreateBuffer failed(code {}) for size = {} usage = 0x{:x}", static_cast<int>(l_Result), static_cast<uint64_t>(size), static_cast<uint64_t>(usage));
        }

        VkMemoryRequirements l_MemoryRequirements;
        vkGetBufferMemoryRequirements(Startup::GetDevice(), buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        try
        {
            // Attempt to locate a compatible memory type for the buffer allocation.
            l_AllocateInfo.memoryTypeIndex = FindMemoryType(l_MemoryRequirements.memoryTypeBits, properties);
        }
        catch (const std::runtime_error& l_Error)
        {
            TR_CORE_CRITICAL("{}", l_Error.what());

            // Clean up the partially created buffer to avoid leaking resources.
            if (buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Startup::GetDevice(), buffer, nullptr);
                buffer = VK_NULL_HANDLE;
            }

            bufferMemory = VK_NULL_HANDLE;

            return;
        }

        l_Result = vkAllocateMemory(Startup::GetDevice(), &l_AllocateInfo, nullptr, &bufferMemory);
        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("vkAllocateMemory failed(code {}) for size = {}", static_cast<int>(l_Result), static_cast<uint64_t>(l_MemoryRequirements.size));
        }

        vkBindBufferMemory(Startup::GetDevice(), buffer, bufferMemory, 0);
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

        vkQueueSubmit(Startup::GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Startup::GetGraphicsQueue());

        pool.Release(l_CommandBuffer);
    }

    void Buffers::DestroyBuffer(VkBuffer buffer, VkDeviceMemory memory)
    {
        if (buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Startup::GetDevice(), buffer, nullptr);
        }

        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Startup::GetDevice(), memory, nullptr);
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

    void Buffers::TrackAllocation(VkBuffer buffer, VkDeviceMemory memory)
    {
        if (buffer == VK_NULL_HANDLE || memory == VK_NULL_HANDLE)
        {
            return;
        }

        m_Allocations.push_back({ buffer, memory });
    }
}