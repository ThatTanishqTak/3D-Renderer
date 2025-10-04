#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "Renderer/CommandBufferPool.h"

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

        VkSemaphore GetImageAvailableSemaphorePerImage(size_t imageIndex) const { return m_ImageAvailableSemaphoresPerImage[imageIndex]; }
        VkSemaphore GetRenderFinishedSemaphoreForFrame(size_t frameIndex) const { return m_RenderFinishedSemaphoresPerFrame[frameIndex]; }
        VkFence GetInFlightFence(size_t index) const { return m_InFlightFences[index]; }
        VkFence& GetImageInFlight(size_t index) { return m_ImagesInFlight[index]; }

        void SetImageInFlight(size_t index, VkFence fence) { m_ImagesInFlight[index] = fence; }

        size_t GetFrameCount() const { return m_ImageAvailableSemaphoresPerImage.size(); }
        size_t& CurrentFrame() { return m_CurrentFrame; }
        size_t CurrentFrame() const { return m_CurrentFrame; }

        CommandBufferPool& GetOneTimePool() { return m_OneTimePool; }
        const CommandBufferPool& GetOneTimePool() const { return m_OneTimePool; }

        VkCommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    private:
        void CreateCommandPool();
        void CreateCommandBuffers(uint32_t commandBufferCount);
        void CreateSyncObjects(uint32_t count);

    private:
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;
        std::vector<VkSemaphore> m_ImageAvailableSemaphoresPerImage;
        // Render-finished semaphores are owned per frame-in-flight so they cannot be re-signaled until
        // presentation has consumed them. The array length matches the frame count tracked by m_CurrentFrame.
        std::vector<VkSemaphore> m_RenderFinishedSemaphoresPerFrame;
        std::vector<VkFence> m_InFlightFences;
        std::vector<VkFence> m_ImagesInFlight;
        size_t m_CurrentFrame = 0;
        CommandBufferPool m_OneTimePool;
    };
}