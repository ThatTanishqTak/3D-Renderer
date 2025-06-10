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
        CreateCommandPool();
        m_Buffers.CreateVertexBuffer(Geometry::CubeVertices, m_CommandPool, m_VertexBuffer, m_VertexBufferMemory);
        m_Buffers.CreateIndexBuffer(Geometry::CubeIndices, m_CommandPool, m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
        m_Buffers.CreateUniformBuffers(m_Swapchain.GetImageCount(), m_UniformBuffers, m_UniformBuffersMemory);
        CreateDescriptorPool();
        CreateDescriptorSets();
        CreateCommandBuffer();
        CreateSyncObjects();

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");
    }

    void Renderer::Shutdown()
    {
        TR_CORE_TRACE("Shutting Down Renderer");

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
        vkWaitForFences(Application::GetDevice(), 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex;
        VkResult l_Result = vkAcquireNextImageKHR(Application::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX, 
            m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &l_ImageIndex);

        Application::GetImGuiLayer().Begin();
        Application::GetImGuiLayer().SetupDockspace();
        ImGui::ShowDemoWindow();

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR || l_Result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        
            return;
        }

        else if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to acquire swapchain image (code {})", static_cast<int>(l_Result));
        }

        if (m_ImagesInFlight[l_ImageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(Application::GetDevice(), 1, &m_ImagesInFlight[l_ImageIndex], VK_TRUE, UINT64_MAX);
        }

        m_ImagesInFlight[l_ImageIndex] = m_InFlightFences[m_CurrentFrame];

        UniformBufferObject l_UniformBufferObject{};
        l_UniformBufferObject.Model = glm::rotate(glm::mat4(1.0f), Utilities::Time::GetTime() * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        l_UniformBufferObject.View = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        l_UniformBufferObject.Projection = glm::perspective(glm::radians(45.0f), m_Swapchain.GetExtent().width / float(m_Swapchain.GetExtent().height), 0.1f, 10.0f);
        l_UniformBufferObject.Projection[1][1] *= -1.0f;

        void* l_Data;
        vkMapMemory(Application::GetDevice(), m_UniformBuffersMemory[l_ImageIndex], 0, sizeof(l_UniformBufferObject), 0, &l_Data);
        memcpy(l_Data, &l_UniformBufferObject, sizeof(l_UniformBufferObject));
        vkUnmapMemory(Application::GetDevice(), m_UniformBuffersMemory[l_ImageIndex]);

        vkResetCommandBuffer(m_CommandBuffers[l_ImageIndex], 0);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        if (vkBeginCommandBuffer(m_CommandBuffers[l_ImageIndex], &l_BeginInfo) != VK_SUCCESS)
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

        vkCmdBeginRenderPass(m_CommandBuffers[l_ImageIndex], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(m_CommandBuffers[l_ImageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());
        vkCmdBindDescriptorSets(m_CommandBuffers[l_ImageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[l_ImageIndex], 0, nullptr);

        VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };
        vkCmdBindVertexBuffers(m_CommandBuffers[l_ImageIndex], 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(m_CommandBuffers[l_ImageIndex], m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(m_CommandBuffers[l_ImageIndex], m_IndexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(m_CommandBuffers[l_ImageIndex]);

        vkCmdBeginRenderPass(m_CommandBuffers[l_ImageIndex], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        Application::GetImGuiLayer().End(m_CommandBuffers[l_ImageIndex]);
        vkCmdEndRenderPass(m_CommandBuffers[l_ImageIndex]);

        if (vkEndCommandBuffer(m_CommandBuffers[l_ImageIndex]) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to record command buffer {}", l_ImageIndex);
        }

        VkSemaphore l_WaitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrame] };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrame] };

        VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &m_CommandBuffers[l_ImageIndex];
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        vkResetFences(Application::GetDevice(), 1, &m_InFlightFences[m_CurrentFrame]);
        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, m_InFlightFences[m_CurrentFrame]) != VK_SUCCESS)
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

        m_CurrentFrame = (m_CurrentFrame + 1) % m_ImageAvailableSemaphores.size();
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

        for (size_t i = 0; i < m_ImageAvailableSemaphores.size(); ++i)
        {
            if (m_RenderFinishedSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(Application::GetDevice(), m_RenderFinishedSemaphores[i], nullptr);

                m_RenderFinishedSemaphores[i] = VK_NULL_HANDLE;
            }

            if (m_ImageAvailableSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(Application::GetDevice(), m_ImageAvailableSemaphores[i], nullptr);

                m_ImageAvailableSemaphores[i] = VK_NULL_HANDLE;
            }

            if (m_InFlightFences[i] != VK_NULL_HANDLE)
            {
                vkDestroyFence(Application::GetDevice(), m_InFlightFences[i], nullptr);

                m_InFlightFences[i] = VK_NULL_HANDLE;
            }
        }

        m_ImageAvailableSemaphores.clear();
        m_RenderFinishedSemaphores.clear();
        m_InFlightFences.clear();
        m_ImagesInFlight.clear();

        m_CurrentFrame = 0;

        m_Pipeline.CleanupFramebuffers();

        m_Swapchain.Cleanup();
        m_Swapchain.Init();
        m_Pipeline.CreateFramebuffers(m_Swapchain);

        vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
        CreateCommandBuffer();
        CreateSyncObjects();

        TR_CORE_TRACE("Swapchain Recreated");
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateCommandPool()
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

    void Renderer::CreateCommandBuffer()
    {
        TR_CORE_TRACE("Allocating Command Buffers");

        m_CommandBuffers.resize(m_Pipeline.GetFramebuffers().size());

        VkCommandBufferAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateInfo.commandPool = m_CommandPool;
        l_AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

        if (vkAllocateCommandBuffers(Application::GetDevice(), &l_AllocateInfo, m_CommandBuffers.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate command buffers");
        }

        TR_CORE_TRACE("Command Buffers Allocated ({} Buffers)", m_CommandBuffers.size());
    }

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

    void Renderer::CreateSyncObjects()
    {
        TR_CORE_TRACE("Creating Sync Objects");

        size_t l_Count = m_Swapchain.GetImageCount();

        m_ImageAvailableSemaphores.resize(l_Count);
        m_RenderFinishedSemaphores.resize(l_Count);
        m_InFlightFences.resize(l_Count);
        m_ImagesInFlight.resize(l_Count);
        std::fill(m_ImagesInFlight.begin(), m_ImagesInFlight.end(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo l_SemaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        VkFenceCreateInfo l_FenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < l_Count; ++i)
        {
            if (vkCreateSemaphore(Application::GetDevice(), &l_SemaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(Application::GetDevice(), &l_SemaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(Application::GetDevice(), &l_FenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create sync for image {}", i);
            }
        }

        TR_CORE_TRACE("Sync Objects Created ({} Frames In Flight)", l_Count);
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    VkShaderModule Renderer::CreateShaderModule(VkDevice device, const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = code.size();
        l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule l_Module;
        if (vkCreateShaderModule(device, &l_CreateInfo, nullptr, &l_Module) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create shader l_Module");
        }

        return l_Module;
    }
}