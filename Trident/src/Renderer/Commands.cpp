#include "Renderer/Commands.h"

#include "Application.h"
#include "Core/Utilities.h"

namespace Trident
{
    void Commands::Init(uint32_t commandBufferCount)
    {
        CreateCommandPool();
        CreateCommandBuffers(commandBufferCount);
        CreateSyncObjects(commandBufferCount);

        m_OneTimePool.Init(m_CommandPool, commandBufferCount);
    }

    void Commands::Cleanup()
    {
        const VkDevice l_Device = Application::GetDevice();

        // Tear down per-frame semaphores before pool destruction so presentation never observes recycled handles mid-teardown.
        for (VkSemaphore l_RenderFinished : m_RenderFinishedSemaphoresPerFrame)
        {
            if (l_RenderFinished != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(l_Device, l_RenderFinished, nullptr);
            }
        }

        for (VkSemaphore l_ImageAvailable : m_ImageAvailableSemaphoresPerImage)
        {
            if (l_ImageAvailable != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(l_Device, l_ImageAvailable, nullptr);
            }
        }

        for (VkFence l_InFlightFence : m_InFlightFences)
        {
            if (l_InFlightFence != VK_NULL_HANDLE)
            {
                vkDestroyFence(l_Device, l_InFlightFence, nullptr);
            }
        }

        if (!m_CommandBuffers.empty())
        {
            vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());

            m_CommandBuffers.clear();
        }

        m_OneTimePool.Cleanup();

        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(Application::GetDevice(), m_CommandPool, nullptr);

            m_CommandPool = VK_NULL_HANDLE;
        }

        m_ImageAvailableSemaphoresPerImage.clear();
        m_RenderFinishedSemaphoresPerFrame.clear();
        m_InFlightFences.clear();
        m_ImagesInFlight.clear();
    }

    void Commands::Recreate(uint32_t commandBufferCount)
    {
        const VkDevice l_Device = Application::GetDevice();

        for (VkSemaphore l_RenderFinished : m_RenderFinishedSemaphoresPerFrame)
        {
            if (l_RenderFinished != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(l_Device, l_RenderFinished, nullptr);
            }
        }

        for (VkSemaphore l_ImageAvailable : m_ImageAvailableSemaphoresPerImage)
        {
            if (l_ImageAvailable != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(l_Device, l_ImageAvailable, nullptr);
            }
        }

        for (VkFence l_InFlightFence : m_InFlightFences)
        {
            if (l_InFlightFence != VK_NULL_HANDLE)
            {
                vkDestroyFence(l_Device, l_InFlightFence, nullptr);
            }
        }

        m_ImageAvailableSemaphoresPerImage.clear();
        m_RenderFinishedSemaphoresPerFrame.clear();
        m_InFlightFences.clear();
        m_ImagesInFlight.clear();

        m_CurrentFrame = 0;

        vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
        m_CommandBuffers.clear();
        m_OneTimePool.Cleanup();

        CreateCommandBuffers(commandBufferCount);
        CreateSyncObjects(commandBufferCount);
        m_OneTimePool.Init(m_CommandPool, commandBufferCount);
    }

    VkCommandBuffer Commands::BeginSingleTimeCommands()
    {
        VkCommandBuffer l_CommandBuffer = m_OneTimePool.Acquire();

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        return l_CommandBuffer;
    }

    void Commands::EndSingleTimeCommands(VkCommandBuffer l_CommandBuffer)
    {
        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Application::GetGraphicsQueue());

        m_OneTimePool.Release(l_CommandBuffer);
    }

    void Commands::CreateCommandPool()
    {
        TR_CORE_TRACE("Creating Command Pool");

        VkCommandPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        l_PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        l_PoolInfo.queueFamilyIndex = Application::GetQueueFamilyIndices().GraphicsFamily.value();

        if (vkCreateCommandPool(Application::GetDevice(), &l_PoolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create command pool");
        }

        TR_CORE_TRACE("Command Pool Created");
    }

    void Commands::CreateCommandBuffers(uint32_t commandBufferCount)
    {
        TR_CORE_TRACE("Allocating Command Buffers");

        m_CommandBuffers.resize(commandBufferCount);

        VkCommandBufferAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateInfo.commandPool = m_CommandPool;
        l_AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

        if (vkAllocateCommandBuffers(Application::GetDevice(), &l_AllocateInfo, m_CommandBuffers.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate command buffers");
        }

        TR_CORE_TRACE("Command Buffers Allocated ({})", m_CommandBuffers.size());
    }

    void Commands::CreateSyncObjects(uint32_t swapchainImageCount)
    {
        TR_CORE_TRACE("Creating Sync Objects");

        const size_t l_FrameCount = swapchainImageCount; // Frames in flight currently mirror the swapchain image count.

        m_ImageAvailableSemaphoresPerImage.resize(swapchainImageCount);
        m_RenderFinishedSemaphoresPerFrame.resize(l_FrameCount);
        m_InFlightFences.resize(l_FrameCount);
        m_ImagesInFlight.resize(swapchainImageCount);

        std::fill(m_ImagesInFlight.begin(), m_ImagesInFlight.end(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < l_FrameCount; ++i)
        {
            if (vkCreateSemaphore(Application::GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphoresPerImage[i]) != VK_SUCCESS ||
                vkCreateSemaphore(Application::GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphoresPerFrame[i]) != VK_SUCCESS ||
                vkCreateFence(Application::GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create sync objects for image {}", i);
            }
        }

        TR_CORE_TRACE("Sync Objects Created ({})", swapchainImageCount);

        // Future improvement: adopt VK_KHR_timeline_semaphore or VK_EXT_swapchain_maintenance1 when driver coverage improves.
    }

}