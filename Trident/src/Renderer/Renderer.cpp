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
        CreateRenderPass();
        CreateDescriptorSetLayout();
        CreateGraphicsPipeline();
        CreateFramebuffers();
        CreateCommandPool();
        CreateVertexBuffer();
        CreateIndexBuffer();
        CreateUniformBuffer();
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
        
        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(Application::GetDevice(), m_DescriptorSetLayout, nullptr);
            
            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
        
        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(Application::GetDevice(), m_DescriptorPool, nullptr);

            m_DescriptorPool = VK_NULL_HANDLE;
        }
        
        m_DescriptorSets.clear();

        for (VkFramebuffer l_FrameBuffer : m_SwapchainFramebuffers)
        {
            if (l_FrameBuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(Application::GetDevice(), l_FrameBuffer, nullptr);
            }
        }
        
        m_SwapchainFramebuffers.clear();

        m_Swapchain.Cleanup();

        if (m_GraphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Application::GetDevice(), m_GraphicsPipeline, nullptr);
            
            m_GraphicsPipeline = VK_NULL_HANDLE;
        }

        if (m_PipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(Application::GetDevice(), m_PipelineLayout, nullptr);
            
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(Application::GetDevice(), m_RenderPass, nullptr);
            
            m_RenderPass = VK_NULL_HANDLE;
        }

        if (m_IndexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Application::GetDevice(), m_IndexBuffer, nullptr);
            
            m_IndexBuffer = VK_NULL_HANDLE;
        }
        
        if (m_IndexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Application::GetDevice(), m_IndexBufferMemory, nullptr);
            
            m_IndexBufferMemory = VK_NULL_HANDLE;
        }
        
        if (m_VertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(Application::GetDevice(), m_VertexBuffer, nullptr);
            
            m_VertexBuffer = VK_NULL_HANDLE;
        }
        
        if (m_VertexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(Application::GetDevice(), m_VertexBufferMemory, nullptr);
            
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
        {
            if (m_UniformBuffers[i] != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Application::GetDevice(), m_UniformBuffers[i], nullptr);

                m_UniformBuffers[i] = VK_NULL_HANDLE;
            }
        
            if (m_UniformBuffersMemory[i] != VK_NULL_HANDLE)
            {
                vkFreeMemory(Application::GetDevice(), m_UniformBuffersMemory[i], nullptr);

                m_UniformBuffersMemory[i] = VK_NULL_HANDLE;
            }
        }

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
        l_RenderPassInfo.renderPass = m_RenderPass;
        l_RenderPassInfo.framebuffer = m_SwapchainFramebuffers[l_ImageIndex];
        l_RenderPassInfo.renderArea.offset = { 0, 0 };
        l_RenderPassInfo.renderArea.extent = m_Swapchain.GetExtent();

        VkClearValue l_ClearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
        l_RenderPassInfo.clearValueCount = 1;
        l_RenderPassInfo.pClearValues = &l_ClearColor;

        vkCmdBeginRenderPass(m_CommandBuffers[l_ImageIndex], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(m_CommandBuffers[l_ImageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
        vkCmdBindDescriptorSets(m_CommandBuffers[l_ImageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSets[l_ImageIndex], 0, nullptr);

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

        for (VkFramebuffer l_FrameBuffer : m_SwapchainFramebuffers)
        {
            vkDestroyFramebuffer(Application::GetDevice(), l_FrameBuffer, nullptr);
        }

        m_SwapchainFramebuffers.clear();

        m_Swapchain.Cleanup();
        m_Swapchain.Init();
        CreateFramebuffers();

        vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
        CreateCommandBuffer();
        CreateSyncObjects();

        TR_CORE_TRACE("Swapchain Recreated");
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateRenderPass()
    {
        TR_CORE_TRACE("Creating Render Pass");

        VkAttachmentDescription l_ColorAttachment{};
        l_ColorAttachment.format = m_Swapchain.GetImageFormat();
        l_ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference l_ColorAttachmentReference{};
        l_ColorAttachmentReference.attachment = 0;
        l_ColorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_ColorAttachmentReference;

        VkSubpassDependency l_Dependency{};
        l_Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        l_Dependency.dstSubpass = 0;
        l_Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        l_Dependency.srcAccessMask = 0;
        l_Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        l_Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo l_RenderPassInfo{};
        l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        l_RenderPassInfo.attachmentCount = 1;
        l_RenderPassInfo.pAttachments = &l_ColorAttachment;
        l_RenderPassInfo.subpassCount = 1;
        l_RenderPassInfo.pSubpasses = &l_Subpass;
        l_RenderPassInfo.dependencyCount = 1;
        l_RenderPassInfo.pDependencies = &l_Dependency;

        if (vkCreateRenderPass(Application::GetDevice(), &l_RenderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create render pass");
        }

        TR_CORE_TRACE("Render Pass Created");
    }

    void Renderer::CreateDescriptorSetLayout()
    {
        TR_CORE_TRACE("Creating Descriptor Set Layout");

        VkDescriptorSetLayoutBinding l_UboLayoutBinding{};
        l_UboLayoutBinding.binding = 0;
        l_UboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_UboLayoutBinding.descriptorCount = 1;
        l_UboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        l_UboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo l_LayoutInfo{};
        l_LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        l_LayoutInfo.bindingCount = 1;
        l_LayoutInfo.pBindings = &l_UboLayoutBinding;

        if (vkCreateDescriptorSetLayout(Application::GetDevice(), &l_LayoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor set layout");
        }

        TR_CORE_TRACE("Descriptor Set Layout Created");
    }

    void Renderer::CreateGraphicsPipeline()
    {
        TR_CORE_TRACE("Creating Graphics Pipeline");

        auto a_VertexShaderCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.vert.spv");
        auto a_FragmentShaderCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.frag.spv");

        VkShaderModule l_VertexModule = CreateShaderModule(Application::GetDevice(), a_VertexShaderCode);
        VkShaderModule l_FragmentModule = CreateShaderModule(Application::GetDevice(), a_FragmentShaderCode);

        VkPipelineShaderStageCreateInfo l_VertexStage{};
        l_VertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_VertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        l_VertexStage.module = l_VertexModule;
        l_VertexStage.pName = "main";

        VkPipelineShaderStageCreateInfo l_FragmentStage{};
        l_FragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l_FragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_FragmentStage.module = l_FragmentModule;
        l_FragmentStage.pName = "main";

        VkPipelineShaderStageCreateInfo l_ShaderStages[] = { l_VertexStage, l_FragmentStage };

        auto a_BindingDescription = Vertex::GetBindingDescription();
        auto a_AttributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo l_VertexInputInfo{};
        l_VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        l_VertexInputInfo.vertexBindingDescriptionCount = 1;
        l_VertexInputInfo.pVertexBindingDescriptions = &a_BindingDescription;
        l_VertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(a_AttributeDescriptions.size());
        l_VertexInputInfo.pVertexAttributeDescriptions = a_AttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{};
        l_InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        l_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        l_InputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(m_Swapchain.GetExtent().width);
        l_Viewport.height = static_cast<float>(m_Swapchain.GetExtent().height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0, 0 };
        l_Scissor.extent = m_Swapchain.GetExtent();

        VkPipelineViewportStateCreateInfo l_ViewportState{};
        l_ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        l_ViewportState.viewportCount = 1;
        l_ViewportState.pViewports = &l_Viewport;
        l_ViewportState.scissorCount = 1;
        l_ViewportState.pScissors = &l_Scissor;

        VkPipelineRasterizationStateCreateInfo l_Rasterizer{};
        l_Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        l_Rasterizer.depthClampEnable = VK_FALSE;
        l_Rasterizer.rasterizerDiscardEnable = VK_FALSE;
        l_Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        l_Rasterizer.lineWidth = 1.0f;
        l_Rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        l_Rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        l_Rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo l_MultiSamplingInfo{};
        l_MultiSamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        l_MultiSamplingInfo.sampleShadingEnable = VK_FALSE;
        l_MultiSamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
        l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        l_ColorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo l_ColorBlendInfo{};
        l_ColorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        l_ColorBlendInfo.logicOpEnable = VK_FALSE;
        l_ColorBlendInfo.attachmentCount = 1;
        l_ColorBlendInfo.pAttachments = &l_ColorBlendAttachment;

        VkPipelineLayoutCreateInfo l_PipelineLayoutInfo{};
        l_PipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        l_PipelineLayoutInfo.setLayoutCount = 1;
        l_PipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
        l_PipelineLayoutInfo.pushConstantRangeCount = 0;
        l_PipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(Application::GetDevice(), &l_PipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create pipeline layout");
        }

        VkGraphicsPipelineCreateInfo l_PipelineInfo{};
        l_PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        l_PipelineInfo.stageCount = 2;
        l_PipelineInfo.pStages = l_ShaderStages;
        l_PipelineInfo.pVertexInputState = &l_VertexInputInfo;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_MultiSamplingInfo;
        l_PipelineInfo.pDepthStencilState = nullptr;
        l_PipelineInfo.pColorBlendState = &l_ColorBlendInfo;
        l_PipelineInfo.pDynamicState = nullptr;
        l_PipelineInfo.layout = m_PipelineLayout;
        l_PipelineInfo.renderPass = m_RenderPass;
        l_PipelineInfo.subpass = 0;
        l_PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        l_PipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(Application::GetDevice(), VK_NULL_HANDLE, 1, &l_PipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create graphics pipeline");
        }

        vkDestroyShaderModule(Application::GetDevice(), l_FragmentModule, nullptr);
        vkDestroyShaderModule(Application::GetDevice(), l_VertexModule, nullptr);

        TR_CORE_TRACE("Graphics Pipeline Created");
    }

    void Renderer::CreateFramebuffers()
    {
        TR_CORE_TRACE("Creating Framebuffers");

        m_SwapchainFramebuffers.resize(m_Swapchain.GetImageViews().size());

        for (size_t i = 0; i < m_Swapchain.GetImageViews().size(); ++i)
        {
            VkImageView l_Attachments[] = { m_Swapchain.GetImageViews()[i] };

            VkFramebufferCreateInfo l_FrameBufferInfo{};
            l_FrameBufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            l_FrameBufferInfo.renderPass = m_RenderPass;
            l_FrameBufferInfo.attachmentCount = 1;
            l_FrameBufferInfo.pAttachments = l_Attachments;
            l_FrameBufferInfo.width = m_Swapchain.GetExtent().width;
            l_FrameBufferInfo.height = m_Swapchain.GetExtent().height;
            l_FrameBufferInfo.layers = 1;

            if (vkCreateFramebuffer(Application::GetDevice(), &l_FrameBufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create framebuffer {}", i);
            }
        }

        TR_CORE_TRACE("Framebuffers Created ({} Total)", m_SwapchainFramebuffers.size());
    }

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

        m_CommandBuffers.resize(m_SwapchainFramebuffers.size());

        VkCommandBufferAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateInfo.commandPool = m_CommandPool;
        l_AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

        if (vkAllocateCommandBuffers(Application::GetDevice(), &l_AllocateInfo, m_CommandBuffers.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate command buffers");
        }

        //for (size_t i = 0; i < m_CommandBuffers.size(); ++i)
        //{
        //    VkCommandBufferBeginInfo l_BeginInfo{};
        //    l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        //    if (vkBeginCommandBuffer(m_CommandBuffers[i], &l_BeginInfo) != VK_SUCCESS)
        //    {
        //        TR_CORE_CRITICAL("Failed to begin recording command buffer {}", i);
        //    }

        //    VkRenderPassBeginInfo l_RenderPassInfo{};
        //    l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        //    l_RenderPassInfo.renderPass = m_RenderPass;
        //    l_RenderPassInfo.framebuffer = m_SwapchainFramebuffers[i];
        //    l_RenderPassInfo.renderArea.offset = { 0, 0 };
        //    l_RenderPassInfo.renderArea.extent = m_SwapchainExtent;

        //    VkClearValue l_ClearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
        //    l_RenderPassInfo.clearValueCount = 1;
        //    l_RenderPassInfo.pClearValues = &l_ClearColor;

        //    vkCmdBeginRenderPass(m_CommandBuffers[i], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        //    vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
        //    vkCmdBindDescriptorSets(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSets[i], 0, nullptr);

        //    VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
        //    VkDeviceSize l_Offsets[] = { 0 };
        //    vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, l_VertexBuffers, l_Offsets);
        //    vkCmdBindIndexBuffer(m_CommandBuffers[i], m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

        //    vkCmdDrawIndexed(m_CommandBuffers[i], m_IndexCount, 1, 0, 0, 0);
        //    vkCmdEndRenderPass(m_CommandBuffers[i]);

        //    if (vkEndCommandBuffer(m_CommandBuffers[i]) != VK_SUCCESS)
        //    {
        //        TR_CORE_CRITICAL("Failed to record command buffer {}", i);
        //    }
        //}

        //TR_CORE_TRACE("Command Buffers Recorded");
        TR_CORE_TRACE("Command Buffers Allocated ({} Buffers)", m_CommandBuffers.size());
    }

    void Renderer::CreateUniformBuffer()
    {
        TR_CORE_TRACE("Creating Uniform Buffers");

        VkDeviceSize l_BufferSize = sizeof(UniformBufferObject);

        m_UniformBuffers.resize(m_Swapchain.GetImageCount());
        m_UniformBuffersMemory.resize(m_Swapchain.GetImageCount());

        for (size_t i = 0; i < m_Swapchain.GetImageCount(); ++i)
        {
            CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_UniformBuffers[i], m_UniformBuffersMemory[i]);
        }

        TR_CORE_TRACE("Uniform Buffers Created ({} Buffers)", m_UniformBuffers.size());
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

        std::vector<VkDescriptorSetLayout> layouts(l_ImageCount, m_DescriptorSetLayout);

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

    void Renderer::CreateVertexBuffer()
    {
        auto& a_Vertices = Geometry::CubeVertices;
        VkDeviceSize l_BufferSize = sizeof(a_Vertices[0]) * a_Vertices.size();

        VkBuffer l_StagingBuffer;
        VkDeviceMemory l_StagingBufferMemory;
        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingBufferMemory);

        void* l_Data;
        vkMapMemory(Application::GetDevice(), l_StagingBufferMemory, 0, l_BufferSize, 0, &l_Data);
        memcpy(l_Data, a_Vertices.data(), (size_t)l_BufferSize);
        vkUnmapMemory(Application::GetDevice(), l_StagingBufferMemory);

        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_VertexBuffer, m_VertexBufferMemory);
        CopyBuffer(l_StagingBuffer, m_VertexBuffer, l_BufferSize);

        vkDestroyBuffer(Application::GetDevice(), l_StagingBuffer, nullptr);
        vkFreeMemory(Application::GetDevice(), l_StagingBufferMemory, nullptr);
    }

    void Renderer::CreateIndexBuffer()
    {
        auto& a_Indices = Geometry::CubeIndices;
        m_IndexCount = static_cast<uint32_t>(a_Indices.size());
        VkDeviceSize l_BufferSize = sizeof(a_Indices[0]) * a_Indices.size();

        VkBuffer l_StagingBuffer;
        VkDeviceMemory l_StagingBufferMemory;
        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, l_StagingBuffer, l_StagingBufferMemory);

        void* l_Data;
        vkMapMemory(Application::GetDevice(), l_StagingBufferMemory, 0, l_BufferSize, 0, &l_Data);
        memcpy(l_Data, a_Indices.data(), (size_t)l_BufferSize);
        vkUnmapMemory(Application::GetDevice(), l_StagingBufferMemory);

        CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_IndexBuffer, m_IndexBufferMemory);
        CopyBuffer(l_StagingBuffer, m_IndexBuffer, l_BufferSize);

        vkDestroyBuffer(Application::GetDevice(), l_StagingBuffer, nullptr);
        vkFreeMemory(Application::GetDevice(), l_StagingBufferMemory, nullptr);
    }

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

    uint32_t Renderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
    {
        VkPhysicalDeviceMemoryProperties l_MemoryProperties;
        vkGetPhysicalDeviceMemoryProperties(Application::GetPhysicalDevice(), &l_MemoryProperties);

        for (uint32_t i = 0; i < l_MemoryProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) && (l_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        TR_CORE_CRITICAL("Failed to find suitable memory type");

        return EXIT_FAILURE;
    }

    void Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
    {
        VkBufferCreateInfo l_BufferInfo{};
        l_BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        l_BufferInfo.pNext = nullptr;
        l_BufferInfo.size = size;
        l_BufferInfo.usage = usage;
        l_BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        l_BufferInfo.queueFamilyIndexCount = 0;
        l_BufferInfo.pQueueFamilyIndices = nullptr;

        VkResult l_Result = vkCreateBuffer(Application::GetDevice(), &l_BufferInfo, nullptr, &buffer);
        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("vkCreateBuffer failed(code {}) for size = {} usage = 0x{:x}", static_cast<int>(l_Result), static_cast<uint64_t>(size), static_cast<uint64_t>(usage));
        }

        VkMemoryRequirements l_MemoryRequirements;
        vkGetBufferMemoryRequirements(Application::GetDevice(), buffer, &l_MemoryRequirements);

        VkMemoryAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        l_AllocateInfo.pNext = nullptr;
        l_AllocateInfo.allocationSize = l_MemoryRequirements.size;
        l_AllocateInfo.memoryTypeIndex = FindMemoryType(l_MemoryRequirements.memoryTypeBits, properties);

        l_Result = vkAllocateMemory(Application::GetDevice(), &l_AllocateInfo, nullptr, &bufferMemory);
        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("vkAllocateMemory failed(code {}) for size = {}", static_cast<int>(l_Result), static_cast<uint64_t>(l_MemoryRequirements.size));
        }

        vkBindBufferMemory(Application::GetDevice(), buffer, bufferMemory, 0);
    }

    void Renderer::CopyBuffer(VkBuffer sourceBuffer, VkBuffer destinationBuffer, VkDeviceSize size)
    {
        VkCommandBufferAllocateInfo l_AllocateInfo{};
        l_AllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        l_AllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        l_AllocateInfo.commandPool = m_CommandPool;
        l_AllocateInfo.commandBufferCount = 1;

        VkCommandBuffer l_CommandBuffer;
        vkAllocateCommandBuffers(Application::GetDevice(), &l_AllocateInfo, &l_CommandBuffer);

        VkCommandBufferBeginInfo l_BeginInfo{};
        l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        l_BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(l_CommandBuffer, &l_BeginInfo);
        VkBufferCopy l_CopyRegion{};
        l_CopyRegion.srcOffset = 0;
        l_CopyRegion.dstOffset = 0;
        l_CopyRegion.size = size;
        
        vkCmdCopyBuffer(l_CommandBuffer, sourceBuffer, destinationBuffer, 1, &l_CopyRegion);
        vkEndCommandBuffer(l_CommandBuffer);

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &l_CommandBuffer;

        vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(Application::GetGraphicsQueue());

        vkFreeCommandBuffers(Application::GetDevice(), m_CommandPool, 1, &l_CommandBuffer);
    }
}