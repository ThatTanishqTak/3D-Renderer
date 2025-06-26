#include "Renderer/Renderer.h"

#include "Application.h"
#include "Geometry/Mesh.h"
#include "UI/ImGuiLayer.h"

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

        // Ensure the GPU is idle before destroying resources
        vkDeviceWaitIdle(Application::GetDevice());

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

        if (m_TextureSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Application::GetDevice(), m_TextureSampler, nullptr);
            m_TextureSampler = VK_NULL_HANDLE;
        }

        if (m_TextureImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(Application::GetDevice(), m_TextureImageView, nullptr);
            m_TextureImageView = VK_NULL_HANDLE;
        }

        if (m_TextureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(Application::GetDevice(), m_TextureImage, nullptr);
            m_TextureImage = VK_NULL_HANDLE;
        }

        if (m_TextureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Application::GetDevice(), m_TextureImageMemory, nullptr);
            m_TextureImageMemory = VK_NULL_HANDLE;
        }

        if (m_OffscreenSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(Application::GetDevice(), m_OffscreenSampler, nullptr);

            m_OffscreenSampler = VK_NULL_HANDLE;
        }

        TR_CORE_TRACE("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        VkFence l_InFlightFence = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());
        vkWaitForFences(Application::GetDevice(), 1, &l_InFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex = 0;
        if (!AcquireNextImage(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to acquire the next image");

            return;
        }

        UpdateUniformBuffer(l_ImageIndex);

        vkResetFences(Application::GetDevice(), 1, &l_InFlightFence);

        if (!RecordCommandBuffer(l_ImageIndex))
        {
            TR_CORE_CRITICAL("Failed to record command buffer");
            
            return;
        }

        if (!SubmitFrame(l_ImageIndex, l_InFlightFence))
        {
            TR_CORE_CRITICAL("Failed to submit frame");

            return;
        }

        PresentFrame(l_ImageIndex);

        m_Commands.CurrentFrame() = (m_Commands.CurrentFrame() + 1) % m_Commands.GetFrameCount();
    }

    void Renderer::UploadMesh(const Geometry::Mesh& mesh)
    {
        // Ensure no GPU operations are using the old buffers
        vkDeviceWaitIdle(Application::GetDevice());

        if (m_VertexBuffer != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_VertexBuffer, m_VertexBufferMemory);
            m_VertexBuffer = VK_NULL_HANDLE;
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }

        if (m_IndexBuffer != VK_NULL_HANDLE)
        {
            m_Buffers.DestroyBuffer(m_IndexBuffer, m_IndexBufferMemory);
            m_IndexBuffer = VK_NULL_HANDLE;
            m_IndexBufferMemory = VK_NULL_HANDLE;
            m_IndexCount = 0;
        }

        m_Buffers.CreateVertexBuffer(mesh.Vertices, m_Commands.GetCommandPool(), m_VertexBuffer, m_VertexBufferMemory);
        m_Buffers.CreateIndexBuffer(mesh.Indices, m_Commands.GetCommandPool(), m_IndexBuffer, m_IndexBufferMemory, m_IndexCount);
    }

    void Renderer::UploadTexture(const Loader::TextureData& texture)
    {
        // Currently just store texture data on CPU side
        TR_CORE_TRACE("Uploading texture ({}x{})", texture.Width, texture.Height);
        // Placeholder for future GPU upload implementation
    }

    void Renderer::SetImGuiLayer(UI::ImGuiLayer* layer)
    {
        m_ImGuiLayer = layer;
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

        VkDescriptorPoolSize l_PoolSizes[2]{};
        l_PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_PoolSizes[0].descriptorCount = m_Swapchain.GetImageCount();
        l_PoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_PoolSizes[1].descriptorCount = m_Swapchain.GetImageCount();

        VkDescriptorPoolCreateInfo l_PoolInfo{};
        l_PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        l_PoolInfo.poolSizeCount = 2;
        l_PoolInfo.pPoolSizes = l_PoolSizes;
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

        std::vector<VkDescriptorSetLayout> l_Layouts(l_ImageCount, m_Pipeline.GetDescriptorSetLayout());

        VkDescriptorSetAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        l_AllocateInfo.descriptorPool = m_DescriptorPool;
        l_AllocateInfo.descriptorSetCount = l_ImageCount;
        l_AllocateInfo.pSetLayouts = l_Layouts.data();

        m_DescriptorSets.resize(l_ImageCount);
        if (vkAllocateDescriptorSets(Application::GetDevice(), &l_AllocateInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate descriptor sets");
        }

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            VkDescriptorBufferInfo l_BufferInfo{};
            l_BufferInfo.buffer = m_UniformBuffers[i];
            l_BufferInfo.offset = 0;
            l_BufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo l_ImageInfo{};
            l_ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            l_ImageInfo.imageView = m_TextureImageView;
            l_ImageInfo.sampler = m_TextureSampler;

            VkWriteDescriptorSet l_DescriptorWrite{};
            l_DescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_DescriptorWrite.dstSet = m_DescriptorSets[i];
            l_DescriptorWrite.dstBinding = 0;
            l_DescriptorWrite.dstArrayElement = 0;
            l_DescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            l_DescriptorWrite.descriptorCount = 1;
            l_DescriptorWrite.pBufferInfo = &l_BufferInfo;

            VkWriteDescriptorSet l_ImageWrite{};
            l_ImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            l_ImageWrite.dstSet = m_DescriptorSets[i];
            l_ImageWrite.dstBinding = 1;
            l_ImageWrite.dstArrayElement = 0;
            l_ImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            l_ImageWrite.descriptorCount = 1;
            l_ImageWrite.pImageInfo = &l_ImageInfo;

            VkWriteDescriptorSet l_Writes[] = { l_DescriptorWrite, l_ImageWrite };
            vkUpdateDescriptorSets(Application::GetDevice(), 2, l_Writes, 0, nullptr);
        }

        TR_CORE_TRACE("Descriptor Sets Allocated ({})", l_ImageCount);
    }

    bool Renderer::AcquireNextImage(uint32_t& imageIndex, VkFence inFlightFence)
    {
        VkResult l_Result = vkAcquireNextImageKHR(Application::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX,
            m_Commands.GetImageAvailableSemaphorePerImage(m_Commands.CurrentFrame()), VK_NULL_HANDLE, &imageIndex);

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_Result != VK_SUCCESS && l_Result != VK_SUBOPTIMAL_KHR)
        {
            TR_CORE_CRITICAL("Failed to acquire swap chain image!");

            return EXIT_FAILURE;
        }

        VkFence& l_ImageFence = m_Commands.GetImageInFlight(imageIndex);
        if (l_ImageFence != VK_NULL_HANDLE)
        {
            vkWaitForFences(Application::GetDevice(), 1, &l_ImageFence, VK_TRUE, UINT64_MAX);
        }

        m_Commands.SetImageInFlight(imageIndex, inFlightFence);

        return true;
    }

    bool Renderer::RecordCommandBuffer(uint32_t imageIndex)
    {
        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);

        VkRenderPassBeginInfo l_RenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        l_RenderPassInfo.renderPass = m_Pipeline.GetRenderPass();
        l_RenderPassInfo.framebuffer = m_Pipeline.GetFramebuffers()[imageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Swapchain.GetExtent();

        VkClearValue l_ClearColor = { 0.0f, 0.0f, 0.0f, 1.0f };
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_ClearColor;

        vkCmdBeginRenderPass(l_CommandBuffer, &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
        l_Viewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;
        vkCmdSetViewport(l_CommandBuffer, 0, 1, &l_Viewport);

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0, 0 };
        l_Scissor.extent = m_Swapchain.GetExtent();
        vkCmdSetScissor(l_CommandBuffer, 0, 1, &l_Scissor);

        vkCmdBindPipeline(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());

        VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };

        vkCmdBindVertexBuffers(l_CommandBuffer, 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(l_CommandBuffer, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(l_CommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[imageIndex], 0, nullptr);

        vkCmdDrawIndexed(l_CommandBuffer, m_IndexCount, 1, 0, 0, 0);

        if (m_ImGuiLayer)
        {
            m_ImGuiLayer->Render(l_CommandBuffer);
        }

        vkCmdEndRenderPass(l_CommandBuffer);

        if (vkEndCommandBuffer(l_CommandBuffer) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to record command buffer!");

            return EXIT_FAILURE;
        }

        return true;
    }

    bool Renderer::SubmitFrame(uint32_t imageIndex, VkFence inFlightFence)
    {
        VkCommandBuffer l_CommandBuffer = m_Commands.GetCommandBuffer(imageIndex);

        VkSemaphore l_WaitSemaphores[] = { m_Commands.GetImageAvailableSemaphorePerImage(m_Commands.CurrentFrame()) };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphorePerImage(imageIndex) };

        VkSubmitInfo l_SubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, inFlightFence) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit draw command buffer!");

            return EXIT_FAILURE;
        }

        return true;
    }

    void Renderer::PresentFrame(uint32_t imageIndex)
    {
        VkSemaphore l_SignalSemaphores[] = { m_Commands.GetRenderFinishedSemaphorePerImage(imageIndex) };

        VkPresentInfoKHR l_PresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;

        VkSwapchainKHR l_Swapchains[] = { m_Swapchain.GetSwapchain() };
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = l_Swapchains;
        l_PresentInfo.pImageIndices = &imageIndex;

        VkResult l_PresentResult = vkQueuePresentKHR(Application::GetPresentQueue(), &l_PresentInfo);

        if (l_PresentResult == VK_ERROR_OUT_OF_DATE_KHR || l_PresentResult == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_PresentResult != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present swap chain image!");
        }
    }

    void Renderer::UpdateUniformBuffer(uint32_t currentImage)
    {
        UniformBufferObject l_CubeUBO{};

        glm::mat4 l_Model = glm::mat4(1.0f);
        l_Model = glm::translate(l_Model, m_CubeProperties.Position);
        l_Model = glm::scale(l_Model, m_CubeProperties.Scale);
        l_Model = glm::rotate(l_Model, static_cast<float>(Utilities::Time::GetTime()), glm::vec3(1.0f, 1.0f, 1.0f));

        l_CubeUBO.Model = l_Model;
        l_CubeUBO.View = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

        float l_AspectRatio = static_cast<float>(m_Swapchain.GetExtent().width) / static_cast<float>(m_Swapchain.GetExtent().height);
        l_CubeUBO.Projection = glm::perspective(glm::radians(45.0f), l_AspectRatio, 0.1f, 10.0f);
        l_CubeUBO.Projection[1][1] *= -1.0f;

        void* l_Data = nullptr;
        vkMapMemory(Application::GetDevice(), m_UniformBuffersMemory[currentImage], 0, sizeof(l_CubeUBO), 0, &l_Data);
        memcpy(l_Data, &l_CubeUBO, sizeof(l_CubeUBO));
        vkUnmapMemory(Application::GetDevice(), m_UniformBuffersMemory[currentImage]);
    }
}