#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace Trident
{
    class Commands
    {
    public:
        void Init(uint32_t commandBufferCount);
        void Cleanup();
        void Recreate(uint32_t commandBufferCount);

        VkCommandPool GetCommandPool() const { return m_CommandPool; }
        const std::vector<VkCommandBuffer>& GetCommandBuffers() const { return m_CommandBuffers; }
        VkCommandBuffer& GetCommandBuffer(uint32_t index) { return m_CommandBuffers[index]; }

        VkSemaphore GetImageAvailableSemaphore(size_t index) const { return m_ImageAvailableSemaphores[index]; }
        VkSemaphore GetRenderFinishedSemaphore(size_t index) const { return m_RenderFinishedSemaphores[index]; }
        VkFence GetInFlightFence(size_t index) const { return m_InFlightFences[index]; }
        VkFence& GetImageInFlight(size_t index) { return m_ImagesInFlight[index]; }

        size_t GetFrameCount() const { return m_ImageAvailableSemaphores.size(); }
        size_t& CurrentFrame() { return m_CurrentFrame; }
        size_t CurrentFrame() const { return m_CurrentFrame; }

        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        void CreateCommandPool();
        void CreateCommandBuffers(uint32_t commandBufferCount);
        void CreateSyncObjects(uint32_t count);

    private:
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;
        std::vector<VkFence> m_ImagesInFlight;
        size_t m_CurrentFrame = 0;
    };
}