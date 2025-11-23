#include "Renderer/CommandBufferPool.h"
#include "Application/Startup.h"

namespace Trident
{
    void CommandBufferPool::Init(VkCommandPool commandPool, uint32_t count)
    {
        m_CommandPool = commandPool;
        if (count == 0)
        {
            return;
        }

        m_AllBuffers.resize(count);
        m_FreeBuffers.resize(count);

        VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.commandBufferCount = count;

        vkAllocateCommandBuffers(Startup::GetDevice(), &allocInfo, m_AllBuffers.data());
        m_FreeBuffers = m_AllBuffers;
    }

    void CommandBufferPool::Cleanup()
    {
        if (!m_AllBuffers.empty())
        {
            vkFreeCommandBuffers(Startup::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_AllBuffers.size()), m_AllBuffers.data());
            m_AllBuffers.clear();
            m_FreeBuffers.clear();
        }
        m_CommandPool = VK_NULL_HANDLE;
    }

    VkCommandBuffer CommandBufferPool::Acquire()
    {
        if (m_FreeBuffers.empty())
        {
            VkCommandBuffer cmd;
            VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = m_CommandPool;
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(Startup::GetDevice(), &allocInfo, &cmd);
            m_AllBuffers.push_back(cmd);

            return cmd;
        }

        VkCommandBuffer cmd = m_FreeBuffers.back();
        m_FreeBuffers.pop_back();

        return cmd;
    }

    void CommandBufferPool::Release(VkCommandBuffer commandBuffer)
    {
        if (commandBuffer == VK_NULL_HANDLE)
        {
            return;
        }
        
        vkResetCommandBuffer(commandBuffer, 0);
        m_FreeBuffers.push_back(commandBuffer);
    }
}