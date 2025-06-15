#include "Renderer/Renderer.h"

#include "Application.h"

#include <stdexcept>

#include <glm/gtc/matrix_transform.hpp>

namespace Trident
{
    void Renderer::Init()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        m_Swapchain.Init();
        m_Pipeline.Init(m_Swapchain);
        m_Commands.Init(m_Swapchain.GetImageCount());
        m_Buffers.CreateVertexBuffer(Geometry::CubeVertices, m_Commands.GetCommandPool(), m_VertexBuffer, m_VertexBufferMemory);
        m_Buffers.CreateIndexBuffer(Geometry::CubeIndices, m_Commands.GetCommandPool(), m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), m_UniformBuffers, m_UniformBuffersMemory);
        CreateDescriptorPool();
        CreateDescriptorSets();

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");
    }

    void Renderer::Shutdown()
    {
        TR_CORE_TRACE("Shutting Down Renderer");

        m_Commands.Cleanup();

        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Application::GetDevice(), m_DescriptorPool, nullptr);

            m_DescriptorPool = VK_NULL_HANDLE;
        }

        m_DescriptorSets.clear();

        m_Pipeline.Cleanup();
        m_Swapchain.Cleanup();
        m_Buffers.Cleanup();

        m_UniformBuffers.clear();
        m_UniformBuffersMemory.clear();

        if (m_OffscreenSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Application::GetDevice(), m_OffscreenSampler, nullptr);

            m_OffscreenSampler = VK_NULL_HANDLE;
        }

        TR_CORE_TRACE("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        const size_t currentFrame = m_Commands.CurrentFrame();

        VkFence inFlightFence = m_Commands.GetInFlightFence(currentFrame);
        vkWaitForFences(Application::GetDevice(), 1, &inFlightFence, VK_TRUE, UINT64_MAX);

        // Acquire next image
        uint32_t imageIndex;
        VkSemaphore imageAvailable = m_Commands.GetImageAvailableSemaphorePerImage(currentFrame);

        VkResult result = vkAcquireNextImageKHR(
            Application::GetDevice(),
            m_Swapchain.GetSwapchain(),
            UINT64_MAX,
            imageAvailable,
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain();
            return;
        }
        else if (result != VK_SUCCESS) {
            TR_CORE_CRITICAL("Failed to acquire swapchain image: {}", static_cast<int>(result));
            return;
        }

        // If this image is already being used, wait for it
        if (VkFence imageFence = m_Commands.GetImageInFlight(imageIndex); imageFence != VK_NULL_HANDLE) {
            vkWaitForFences(Application::GetDevice(), 1, &imageFence, VK_TRUE, UINT64_MAX);
        }

        // Mark this image as now being used by this frame
        m_Commands.SetImageInFlight(imageIndex, inFlightFence);

        // Update UBO
        UniformBufferObject ubo{};
        ubo.Model = glm::rotate(glm::mat4(1.0f), Utilities::Time::GetTime() * glm::radians(90.0f), glm::vec3(0, 0, 1));
        ubo.View = glm::lookAt(glm::vec3(2, 2, 2), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
        ubo.Projection = glm::perspective(glm::radians(45.0f), m_Swapchain.GetExtent().width / float(m_Swapchain.GetExtent().height), 0.1f, 10.0f);
        ubo.Projection[1][1] *= -1;

        void* data;
        vkMapMemory(Application::GetDevice(), m_UniformBuffersMemory[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(Application::GetDevice(), m_UniformBuffersMemory[imageIndex]);

        // Record command buffer
        VkCommandBuffer cmd = m_Commands.GetCommandBuffer(imageIndex);
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        renderPassInfo.renderPass = m_Pipeline.GetRenderPass();
        renderPassInfo.framebuffer = m_Pipeline.GetFramebuffers()[imageIndex];
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = m_Swapchain.GetExtent();

        VkClearValue clearColor = { {0.0f, 0.0f, 0.0f, 1.0f} };
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{ 0.0f, 0.0f,
                             static_cast<float>(m_Swapchain.GetExtent().width),
                             static_cast<float>(m_Swapchain.GetExtent().height),
                             0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{ {0, 0}, m_Swapchain.GetExtent() };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(),
            0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);

        VkBuffer vertexBuffers[] = { m_VertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(cmd, m_IndexCount, 1, 0, 0, 0);
        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        // Submit command buffer
        VkSemaphore renderFinished = m_Commands.GetRenderFinishedSemaphorePerImage(currentFrame);

        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &imageAvailable;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &renderFinished;

        vkResetFences(Application::GetDevice(), 1, &inFlightFence);
        vkQueueSubmit(Application::GetGraphicsQueue(), 1, &submitInfo, inFlightFence);

        // Present
        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &renderFinished;
        VkSwapchainKHR swapchains[] = { m_Swapchain.GetSwapchain() };
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(Application::GetPresentQueue(), &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain();
        }

        // Advance frame
        m_Commands.CurrentFrame() = (currentFrame + 1) % m_Commands.GetFrameCount();
    }

    void Renderer::RecreateSwapchain()
    {
        TR_CORE_TRACE("Recreating Swapchain");

        uint32_t l_Width = 0;
        uint32_t l_Height = 0;

        Application::GetWindow().GetFramebufferSize(l_Width, l_Height);

        while (l_Width == 0 || l_Height == 0)
        {
            glfwWaitEvents();
            Application::GetWindow().GetFramebufferSize(l_Width, l_Height);
        }

        vkDeviceWaitIdle(Application::GetDevice());

        m_Pipeline.CleanupFramebuffers();

        m_Swapchain.Cleanup();
        m_Swapchain.Init();
        m_Pipeline.CreateFramebuffers(m_Swapchain);

        m_Commands.Recreate(m_Swapchain.GetImageCount());

        TR_CORE_TRACE("Swapchain Recreated");
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateDescriptorPool()
    {
        TR_CORE_TRACE("Creating Descriptor Pool");

        VkDescriptorPoolSize l_PoolSize{};
        l_PoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSize.descriptorCount = m_Swapchain.GetImageCount();

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        l_PoolInfo.poolSizeCount = 1;
        l_PoolInfo.pPoolSizes = &l_PoolSize;
        l_PoolInfo.maxSets = m_Swapchain.GetImageCount();

        if (vkCreateDescriptorPool(Application::GetDevice(), &l_PoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor pool");
        }

        TR_CORE_TRACE("Descriptor Pool Created (MaxSets = {})", l_PoolInfo.maxSets);
    }

    void Renderer::CreateDescriptorSets()
    {
        TR_CORE_TRACE("Allocating Descriptor Sets");

        size_t l_ImageCount = m_Swapchain.GetImageCount();

        std::vector<VkDescriptorSetLayout> layouts(l_ImageCount, m_Pipeline.GetDescriptorSetLayout());

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = l_ImageCount;
        allocInfo.pSetLayouts = layouts.data();

        m_DescriptorSets.resize(l_ImageCount);
        if (vkAllocateDescriptorSets(Application::GetDevice(), &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate descriptor sets");
        }

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_BufferInfo{};
            l_BufferInfo.buffer = m_UniformBuffers[i];
            l_BufferInfo.offset = 0;
            l_BufferInfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet l_DescriptorWrite{};
            l_DescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrite.dstSet = m_DescriptorSets[i];
            l_DescriptorWrite.dstBinding = 0;
            l_DescriptorWrite.dstArrayElement = 0;
            l_DescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_DescriptorWrite.descriptorCount = 1;
            l_DescriptorWrite.pBufferInfo = &l_BufferInfo;

            vkUpdateDescriptorSets(Application::GetDevice(), 1, &l_DescriptorWrite, 0, nullptr);
        }

        TR_CORE_TRACE("Descriptor Sets Allocated ({})", l_ImageCount);
    }
}