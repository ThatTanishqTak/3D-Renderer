#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Trident
{
    class CommandBufferPool
    {
    public:
        void Init(VkCommandPool commandPool, uint32_t count);
        void Cleanup();

        VkCommandBuffer Acquire();
        void Release(VkCommandBuffer commandBuffer);

    private:
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_AllBuffers;
        std::vector<VkCommandBuffer> m_FreeBuffers;
    };
}