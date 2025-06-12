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
        //CreateOffscreenTarget();

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
        if (!IsValidViewport())
        {
            return;
        }

        VkFence l_InFlightFence = m_Commands.GetInFlightFence(m_Commands.CurrentFrame());
        vkWaitForFences(Application::GetDevice(), 1, &l_InFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex;
        VkResult l_Result = vkAcquireNextImageKHR(Application::GetDevice(), m_Swapchain.GetSwapchain(), UINT64_MAX,
            m_Commands.GetImageAvailableSemaphore(m_Commands.CurrentFrame()), VK_NULL_HANDLE, &l_ImageIndex);

        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR || l_Result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();

            return;
        }

        else if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to acquire l_Swapchain image (code {})", static_cast<int>(l_Result));

            return;
        }

        if (m_Commands.GetImageInFlight(l_ImageIndex) != VK_NULL_HANDLE)
        {
            vkWaitForFences(Application::GetDevice(), 1, &m_Commands.GetImageInFlight(l_ImageIndex), VK_TRUE, UINT64_MAX);
        }

        m_Commands.GetImageInFlight(l_ImageIndex) = l_InFlightFence;

        UniformBufferObject l_CubeUBO{};
        glm::mat4 l_Model = glm::translate(glm::mat4(1.0f), m_CubeProperties.Position);
        l_Model = glm::rotate(l_Model, glm::radians(m_CubeProperties.Rotation.x), glm::vec3(1, 0, 0));
        l_Model = glm::rotate(l_Model, glm::radians(m_CubeProperties.Rotation.y), glm::vec3(0, 1, 0));
        l_Model = glm::rotate(l_Model, glm::radians(m_CubeProperties.Rotation.z), glm::vec3(0, 0, 1));
        l_Model = glm::scale(l_Model, m_CubeProperties.Scale);

        l_CubeUBO.Model = l_Model;
        l_CubeUBO.View = glm::lookAt(glm::vec3(2, 2, 2), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));

        float l_Aspect = m_Viewport.Size.y > 0 ? m_Viewport.Size.x / m_Viewport.Size.y : 1.0f;
        l_CubeUBO.Projection = glm::perspective(glm::radians(45.0f), l_Aspect, 0.1f, 10.0f);
        l_CubeUBO.Projection[1][1] *= -1.0f;

        void* l_Data;
        vkMapMemory(Application::GetDevice(), m_UniformBuffersMemory[l_ImageIndex], 0, sizeof(l_CubeUBO), 0, &l_Data);
        memcpy(l_Data, &l_CubeUBO, sizeof(l_CubeUBO));
        vkUnmapMemory(Application::GetDevice(), m_UniformBuffersMemory[l_ImageIndex]);

        VkCommandBuffer l_Command = m_Commands.GetCommandBuffer(l_ImageIndex);
        vkResetCommandBuffer(l_Command, 0);

        VkCommandBufferBeginInfo l_BeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(l_Command, &l_BeginInfo);

        VkClearValue l_CearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
        VkRenderPassBeginInfo l_PassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        l_PassInfo.renderPass = m_Pipeline.GetRenderPass();
        l_PassInfo.framebuffer = m_OffscreenFramebuffer;
        l_PassInfo.renderArea.offset = { 0, 0 };
        l_PassInfo.renderArea.extent = m_Swapchain.GetExtent();
        l_PassInfo.clearValueCount = 1;
        l_PassInfo.pClearValues = &l_CearColor;

        vkCmdBeginRenderPass(l_Command, &l_PassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(l_Command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipeline());

        VkViewport l_Viewport
        {
            0.0f, 0.0f,
            static_cast<float>(m_Swapchain.GetExtent().width), static_cast<float>(m_Swapchain.GetExtent().height),
            0.0f, 1.0f
        };
        vkCmdSetViewport(l_Command, 0, 1, &l_Viewport);

        VkRect2D l_Scissor
        {
            { static_cast<int32_t>(m_Viewport.Position.x), static_cast<int32_t>(m_Viewport.Position.y) },
            { static_cast<uint32_t>(m_Viewport.Size.x), static_cast<uint32_t>(m_Viewport.Size.y) }
        };
        vkCmdSetScissor(l_Command, 0, 1, &l_Scissor);

        vkCmdBindDescriptorSets(l_Command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline.GetPipelineLayout(), 0, 1, &m_DescriptorSets[l_ImageIndex], 0, nullptr);
        VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
        VkDeviceSize l_Offsets[] = { 0 };
        vkCmdBindVertexBuffers(l_Command, 0, 1, l_VertexBuffers, l_Offsets);
        vkCmdBindIndexBuffer(l_Command, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        vkCmdDrawIndexed(l_Command, m_IndexCount, 1, 0, 0, 0);

        vkCmdEndRenderPass(l_Command);

        VkImageMemoryBarrier l_Barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        l_Barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        l_Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        l_Barrier.image = m_OffscreenImage;
        l_Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        l_Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        l_Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        l_Barrier.subresourceRange.baseMipLevel = 0;
        l_Barrier.subresourceRange.levelCount = 1;
        l_Barrier.subresourceRange.baseArrayLayer = 0;
        l_Barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(l_Command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &l_Barrier);

        l_PassInfo.renderPass = m_Pipeline.GetRenderPass();
        l_PassInfo.framebuffer = m_Pipeline.GetFramebuffers()[l_ImageIndex];
        l_PassInfo.renderArea.offset = { 0, 0 };
        l_PassInfo.renderArea.extent = m_Swapchain.GetExtent();
        l_PassInfo.clearValueCount = 1;
        l_PassInfo.pClearValues = &l_CearColor;

        vkCmdBeginRenderPass(l_Command, &l_PassInfo, VK_SUBPASS_CONTENTS_INLINE);
        Application::GetImGuiLayer().End(l_Command);
        vkCmdEndRenderPass(l_Command);


        if (vkEndCommandBuffer(l_Command) != VK_SUCCESS)
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
        l_SubmitInfo.pCommandBuffers = &l_Command;
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
        VkSwapchainKHR l_Swapchain = m_Swapchain.GetSwapchain();
        l_PresentInfo.pSwapchains = &l_Swapchain;
        l_PresentInfo.pImageIndices = &l_ImageIndex;

        l_Result = vkQueuePresentKHR(Application::GetPresentQueue(), &l_PresentInfo);
        if (l_Result == VK_ERROR_OUT_OF_DATE_KHR || l_Result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }

        else if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present l_Swapchain image (code {})", static_cast<int>(l_Result));
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

        CreateOffscreenTarget();

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

    void Renderer::CreateOffscreenTarget()
    {
        VkDevice device = Application::GetDevice();
        VkExtent2D extent = m_Swapchain.GetExtent();
        VkFormat format = m_Swapchain.GetImageFormat();

        VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { extent.width, extent.height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(device, &imageInfo, nullptr, &m_OffscreenImage) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen image");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(device, m_OffscreenImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = m_Buffers.FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_OffscreenMemory) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate memory for offscreen image");
        }

        vkBindImageMemory(device, m_OffscreenImage, m_OffscreenMemory, 0);

        VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        viewInfo.image = m_OffscreenImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &m_OffscreenImageView) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen image view");
        }

        VkFramebufferCreateInfo fbInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbInfo.renderPass = m_Pipeline.GetRenderPass(); // must be compatible
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &m_OffscreenImageView;
        fbInfo.width = extent.width;
        fbInfo.height = extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_OffscreenFramebuffer) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create offscreen framebuffer");
        }

        VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(Application::GetDevice(), &samplerInfo, nullptr, &m_OffscreenSampler) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create sampler for offscreen texture");
        }

        VkCommandBuffer cmd = m_Commands.BeginSingleTimeCommands();

        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.image = m_OffscreenImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr,
            1, &barrier
        );

        m_Commands.EndSingleTimeCommands(cmd);

        m_OffscreenTextureID = Application::GetImGuiLayer().RegisterTexture(m_OffscreenSampler, m_OffscreenImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        TR_CORE_INFO("Offscreen framebuffer and ImGui texture created");
    }
}