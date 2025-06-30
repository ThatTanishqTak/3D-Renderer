#pragma once

#include "Renderer/Buffers.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <vector>

namespace Trident
{
    class Skybox
    {
    public:
        void Init(Buffers& buffers, VkCommandPool commandPool);
        void Cleanup(Buffers& buffers);
        void Record(VkCommandBuffer cmdBuffer, VkPipelineLayout layout, const VkDescriptorSet* descriptorSets, uint32_t imageIndex);

    private:
        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;
        uint32_t m_IndexCount = 0;
    };
}