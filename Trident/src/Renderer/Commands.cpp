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
    }

    void Commands::Cleanup()
    {
        for (size_t i = 0; i < m_ImageAvailableSemaphores.size(); ++i)
        {
            if (m_RenderFinishedSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(Application::GetDevice(), m_RenderFinishedSemaphores[i], nullptr);
            }
            if (m_ImageAvailableSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(Application::GetDevice(), m_ImageAvailableSemaphores[i], nullptr);
            }

            if (m_InFlightFences[i] != VK_NULL_HANDLE)
            {
                vkDestroyFence(Application::GetDevice(), m_InFlightFences[i], nullptr);
            }
        }

        if (!m_CommandBuffers.empty())
        {
            vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
            m_CommandBuffers.clear();
        }

        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(Application::GetDevice(), m_CommandPool, nullptr);

            m_CommandPool = VK_NULL_HANDLE;
        }

        m_ImageAvailableSemaphores.clear();
        m_RenderFinishedSemaphores.clear();
        m_InFlightFences.clear();
        m_ImagesInFlight.clear();
    }

    void Commands::Recreate(uint32_t commandBufferCount)
    {
        for (size_t i = 0; i < m_ImageAvailableSemaphores.size(); ++i)
        {
            if (m_RenderFinishedSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(Application::GetDevice(), m_RenderFinishedSemaphores[i], nullptr);
            }

            if (m_ImageAvailableSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(Application::GetDevice(), m_ImageAvailableSemaphores[i], nullptr);
            }

            if (m_InFlightFences[i] != VK_NULL_HANDLE)
            {
                vkDestroyFence(Application::GetDevice(), m_InFlightFences[i], nullptr);
            }
        }

        m_ImageAvailableSemaphores.clear();
        m_RenderFinishedSemaphores.clear();
        m_InFlightFences.clear();
        m_ImagesInFlight.clear();

        m_CurrentFrame = 0;

        vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
        m_CommandBuffers.clear();

        CreateCommandBuffers(commandBufferCount);
        CreateSyncObjects(commandBufferCount);
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

    void Commands::CreateSyncObjects(uint32_t count)
    {
        TR_CORE_TRACE("Creating Sync Objects");

        m_ImageAvailableSemaphores.resize(count);
        m_RenderFinishedSemaphores.resize(count);
        m_InFlightFences.resize(count);
        m_ImagesInFlight.resize(count);
        std::fill(m_ImagesInFlight.begin(), m_ImagesInFlight.end(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo l_SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo l_FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < count; ++i)
        {
            if (vkCreateSemaphore(Application::GetDevice(), &l_SemaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(Application::GetDevice(), &l_SemaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(Application::GetDevice(), &l_FenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create sync for image {}", i);
            }
        }

        TR_CORE_TRACE("Sync Objects Created ({})", count);
    }
}