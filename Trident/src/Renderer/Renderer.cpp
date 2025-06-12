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
        m_Commands.Init(m_Pipeline.GetFramebuffers().size());
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

        TR_CORE_TRACE("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        VkFence l_InFlightFence = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());

        vkWaitForFences(Application::GetDevice(), 1, &l_InFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex;
        VkResult l_Result = vkAcquireNextImageKHR(Application::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX,
            m_Commands.GetImageAvailableSemaphore(m_Commands.CurrentFrame()), VK_NULL_HANDLE, &l_ImageIndex);

        glm::vec2 viewportPos = m_Viewport.Position;
        glm::vec2 viewportSize = m_Viewport.Size;

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR || l_Result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        
            return;
        }

        else if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to acquire swapchain image (code {})", static_cast<int>(l_Result));
        }

        if (m_Commands.GetImageInFlight(l_ImageIndex) != VK_NULL_HANDLE)
        {
            vkWaitForFences(Application::GetDevice(), 1, &m_Commands.GetImageInFlight(l_ImageIndex), VK_TRUE, UINT64_MAX);
        }

        m_Commands.GetImageInFlight(l_ImageIndex) = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());

        UniformBufferObject l_UniformBufferObject{};
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, m_CubeProperties.Position);
        model = glm::rotate(model, glm::radians(m_CubeProperties.Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, glm::radians(m_CubeProperties.Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(m_CubeProperties.Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, m_CubeProperties.Scale);
        l_UniformBufferObject.Model = model;
        l_UniformBufferObject.View = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        float aspect = m_Viewport.Size.y > 0.0f ? m_Viewport.Size.x / m_Viewport.Size.y : 1.0f;
        l_UniformBufferObject.Projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 10.0f);
        l_UniformBufferObject.Projection[1][1] *= -1.0f;

        void* l_Data;
        vkMapMemory(Application::GetDevice(), m_UniformBuffersMemory[l_ImageIndex], 0, sizeof(l_UniformBufferObject), 0, &l_Data);
        memcpy(l_Data, &l_UniformBufferObject, sizeof(l_UniformBufferObject));
        vkUnmapMemory(Application::GetDevice(), m_UniformBuffersMemory[l_ImageIndex]);

        vkResetCommandBuffer(m_Commands.GetCommandBuffer(l_ImageIndex), 0);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        if (vkBeginCommandBuffer(m_Commands.GetCommandBuffer(l_ImageIndex), &l_BeginInfo) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to begin recording command buffer {}", l_ImageIndex);
        }

        VkRenderPassBeginInfo l_RenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        l_RenderPassInfo.renderPass = m_Pipeline.GetRenderPass();
        l_RenderPassInfo.framebuffer = m_Pipeline.GetFramebuffers()[l_ImageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Swapchain.GetExtent();

        VkClearValue l_ClearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_ClearColor;

        vkCmdBeginRenderPass(m_Commands.GetCommandBuffer(l_ImageIndex), &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(m_Commands.GetCommandBuffer(l_ImageIndex), VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());
        VkViewport viewport{};
        viewport.x = viewportPos.x;
        viewport.y = viewportPos.y;
        viewport.width = viewportSize.x;
        viewport.height = viewportSize.y;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(m_Commands.GetCommandBuffer(l_ImageIndex), 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { static_cast<int32_t>(viewportPos.x), static_cast<int32_t>(viewportPos.y) };
        scissor.extent = { static_cast<uint32_t>(viewportSize.x), static_cast<uint32_t>(viewportSize.y) };
        vkCmdSetScissor(m_Commands.GetCommandBuffer(l_ImageIndex), 0, 1, &scissor);
        vkCmdBindDescriptorSets(m_Commands.GetCommandBuffer(l_ImageIndex), VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[l_ImageIndex], 0, nullptr);

        VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };
        vkCmdBindVertexBuffers(m_Commands.GetCommandBuffer(l_ImageIndex), 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(m_Commands.GetCommandBuffer(l_ImageIndex), m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(m_Commands.GetCommandBuffer(l_ImageIndex), m_IndexCount, 1, 0, 0, 0);

        //vkCmdEndRenderPass(m_Commands.GetCommandBuffer(l_ImageIndex));

        //vkCmdBeginRenderPass(m_Commands.GetCommandBuffer(l_ImageIndex), &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        Application::GetImGuiLayer().End(m_Commands.GetCommandBuffer(l_ImageIndex));
        vkCmdEndRenderPass(m_Commands.GetCommandBuffer(l_ImageIndex));

        if (vkEndCommandBuffer(m_Commands.GetCommandBuffer(l_ImageIndex)) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to record command buffer {}", l_ImageIndex);
        }

        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetImageAvailableSemaphore(m_Commands.CurrentFrame()) };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphore(m_Commands.CurrentFrame()) };

        VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &m_Commands.GetCommandBuffers()[l_ImageIndex];
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;
        
        vkResetFences(Application::GetDevice(), 1, &l_InFlightFence);
        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, l_InFlightFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit draw command buffer");
        }

        VkPresentInfoKHR l_PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;
        l_PresentInfo.swapchainCount = 1;
        VkSwapchainKHR l_SwapchainHandle = m_Swapchain.GetSwapchain();
        l_PresentInfo.pSwapchains = &l_SwapchainHandle;
        l_PresentInfo.pImageIndices = &l_ImageIndex;

        l_Result = vkQueuePresentKHR(Application::GetPresentQueue(), &l_PresentInfo);
        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR || l_Result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present swapchain image (code {})", static_cast<int>(l_Result));
        }

        m_Commands.CurrentFrame() = (m_Commands.CurrentFrame() + 1) % m_Commands.GetFrameCount();
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

        m_Commands.Recreate(m_Pipeline.GetFramebuffers().size());

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
        allocInfo.descriptorSetCount = static_cast<uint32_t>(l_ImageCount);
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