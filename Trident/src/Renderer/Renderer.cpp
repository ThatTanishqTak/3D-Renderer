#include "Renderer/Renderer.h"

#include "Application.h"

#include <stdexcept>
#include <algorithm>
#include <limits>

namespace Trident
{
    void Renderer::Init()
    {
        TR_CORE_INFO("-------INITIALIZING RENDERER-------");

        CreateSwapchain();
        CreateImageViews();
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

        VkDevice l_Device = Application::GetDevice();
        vkDeviceWaitIdle(l_Device);

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (m_RenderFinishedSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(l_Device, m_RenderFinishedSemaphores[i], nullptr);
            }

            if (m_ImageAvailableSemaphores[i] != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(l_Device, m_ImageAvailableSemaphores[i], nullptr);
            }

            if (m_InFlightFences[i] != VK_NULL_HANDLE)
            {
                vkDestroyFence(l_Device, m_InFlightFences[i], nullptr);
            }
        }

        if (!m_CommandBuffers.empty())
        {
            vkFreeCommandBuffers(l_Device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());

            m_CommandBuffers.clear();
        }

        if (m_CommandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(l_Device, m_CommandPool, nullptr);
            
            m_CommandPool = VK_NULL_HANDLE;
        }
        
        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(l_Device, m_DescriptorSetLayout, nullptr);
            
            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
        
        if (m_DescriptorPool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(l_Device, m_DescriptorPool, nullptr);
            m_DescriptorPool = VK_NULL_HANDLE;
        }
        
        m_DescriptorSets.clear();

        for (VkFramebuffer fb : m_SwapchainFramebuffers)
        {
            if (fb != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(l_Device, fb, nullptr);
            }
        }
        
        m_SwapchainFramebuffers.clear();

        for (VkImageView view : m_SwapchainImageViews)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(l_Device, view, nullptr);
            }
        }
        
        m_SwapchainImageViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(l_Device, m_Swapchain, nullptr);

            m_Swapchain = VK_NULL_HANDLE;
        }
        
        m_SwapchainImages.clear();

        if (m_GraphicsPipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(l_Device, m_GraphicsPipeline, nullptr);
            
            m_GraphicsPipeline = VK_NULL_HANDLE;
        }

        if (m_PipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(l_Device, m_PipelineLayout, nullptr);
            
            m_PipelineLayout = VK_NULL_HANDLE;
        }

        if (m_RenderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(l_Device, m_RenderPass, nullptr);
            
            m_RenderPass = VK_NULL_HANDLE;
        }

        if (m_IndexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(l_Device, m_IndexBuffer, nullptr);
            
            m_IndexBuffer = VK_NULL_HANDLE;
        }
        
        if (m_IndexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, m_IndexBufferMemory, nullptr);
            
            m_IndexBufferMemory = VK_NULL_HANDLE;
        }
        
        if (m_VertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(l_Device, m_VertexBuffer, nullptr);
            
            m_VertexBuffer = VK_NULL_HANDLE;
        }
        
        if (m_VertexBufferMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(l_Device, m_VertexBufferMemory, nullptr);
            
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }

        for (size_t i = 0; i < m_UniformBuffers.size(); ++i)
        {
            if (m_UniformBuffers[i] != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(l_Device, m_UniformBuffers[i], nullptr);

                m_UniformBuffers[i] = VK_NULL_HANDLE;
            }
        
            if (m_UniformBuffersMemory[i] != VK_NULL_HANDLE)
            {
                vkFreeMemory(l_Device, m_UniformBuffersMemory[i], nullptr);

                m_UniformBuffersMemory[i] = VK_NULL_HANDLE;
            }
        }

        m_UniformBuffers.clear();
        m_UniformBuffersMemory.clear();

        TR_CORE_TRACE("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        VkDevice device = Application::GetDevice();

        vkWaitForFences(device, 1, &m_InFlightFences[m_CurrentFrame], VK_TRUE, UINT64_MAX);

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, m_Swapchain, UINT64_MAX, m_ImageAvailableSemaphores[m_CurrentFrame], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        
            return;
        }

        else if (result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to acquire swapchain image (code {})", static_cast<int>(result));
        }

        if (m_ImagesInFlight[imageIndex] != VK_NULL_HANDLE)
        {
            vkWaitForFences(device, 1, &m_ImagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
        }
        m_ImagesInFlight[imageIndex] = m_InFlightFences[m_CurrentFrame];

        UniformBufferObject ubo{};
        ubo.model = glm::mat4(1.0f);
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), m_SwapchainExtent.width / float(m_SwapchainExtent.height), 0.1f, 10.0f);
        ubo.proj[1][1] *= -1.0f;

        void* data;
        vkMapMemory(device, m_UniformBuffersMemory[imageIndex], 0, sizeof(ubo), 0, &data);
        memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, m_UniformBuffersMemory[imageIndex]);

        VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphores[m_CurrentFrame] };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphores[m_CurrentFrame] };

        VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_CommandBuffers[imageIndex];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(device, 1, &m_InFlightFences[m_CurrentFrame]);
        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &submitInfo, m_InFlightFences[m_CurrentFrame]) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to submit draw command buffer");
        }

        VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_Swapchain;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(Application::GetPresentQueue(), &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            RecreateSwapchain();
        }
        
        else if (result != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to present swapchain image (code {})", static_cast<int>(result));
        }

        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateSwapchain()
    {
        TR_CORE_TRACE("Creating Swapchain");

        auto details = QuerySwapchainSupport(Application::GetPhysicalDevice(), Application::GetSurface());

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.Formats);
        VkPresentModeKHR presentMode = ChooseSwapPresentMode(details.PresentModes);
        VkExtent2D extent = ChooseSwapExtent(details.Capabilities);

        uint32_t imageCount = details.Capabilities.minImageCount + 1;
        if (details.Capabilities.maxImageCount > 0 && imageCount > details.Capabilities.maxImageCount)
        {
            imageCount = details.Capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = Application::GetSurface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto indices = Application::GetQueueFamilyIndices();
        uint32_t queueFamilyIndices[] = { indices.GraphicsFamily.value(), indices.PresentFamily.value() };

        if (indices.GraphicsFamily != indices.PresentFamily)
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }

        else
        {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;
            createInfo.pQueueFamilyIndices = nullptr;
        }

        createInfo.preTransform = details.Capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;
        createInfo.oldSwapchain = VK_NULL_HANDLE;

        if (vkCreateSwapchainKHR(Application::GetDevice(), &createInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create swap chain");
        }

        vkGetSwapchainImagesKHR(Application::GetDevice(), m_Swapchain, &imageCount, nullptr);
        m_SwapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(Application::GetDevice(), m_Swapchain, &imageCount, m_SwapchainImages.data());

        m_SwapchainImageFormat = surfaceFormat.format;
        m_SwapchainExtent = extent;

        TR_CORE_TRACE("Swapchain Created: {} Images, Format {}, Extent {}x{}", imageCount, (int)surfaceFormat.format, extent.width, extent.height);
    }

    void Renderer::CreateImageViews()
    {
        TR_CORE_TRACE("Creating Image Views");

        m_SwapchainImageViews.resize(m_SwapchainImages.size());

        for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
        {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = m_SwapchainImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_SwapchainImageFormat;
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(Application::GetDevice(), &viewInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create image view for swapchain image {}", i);
            }
        }

        TR_CORE_TRACE("Image Views Created ({} Views)", m_SwapchainImageViews.size());
    }

    void Renderer::CreateRenderPass()
    {
        TR_CORE_TRACE("Creating Render Pass");

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_SwapchainImageFormat;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(Application::GetDevice(), &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create render pass");
        }

        TR_CORE_TRACE("Render Pass Created");
    }

    void Renderer::CreateDescriptorSetLayout()
    {
        TR_CORE_TRACE("Creating Descriptor Set Layout");

        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(Application::GetDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor set layout");
        }

        TR_CORE_TRACE("Descriptor Set Layout Created");
    }

    void Renderer::CreateGraphicsPipeline()
    {
        TR_CORE_TRACE("Creating Graphics Pipeline");

        auto vertShaderCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.vert.spv");
        auto fragShaderCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.frag.spv");

        VkShaderModule vertModule = CreateShaderModule(Application::GetDevice(), vertShaderCode);
        VkShaderModule fragModule = CreateShaderModule(Application::GetDevice(), fragShaderCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        auto bindingDescription = Vertex::GetBindingDescription();
        auto attributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_SwapchainExtent.width);
        viewport.height = static_cast<float>(m_SwapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_SwapchainExtent;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(Application::GetDevice(), &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create pipeline layout");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = nullptr;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = nullptr;
        pipelineInfo.layout = m_PipelineLayout;
        pipelineInfo.renderPass = m_RenderPass;
        pipelineInfo.subpass = 0;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;

        if (vkCreateGraphicsPipelines(Application::GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create graphics pipeline");
        }

        vkDestroyShaderModule(Application::GetDevice(), fragModule, nullptr);
        vkDestroyShaderModule(Application::GetDevice(), vertModule, nullptr);

        TR_CORE_TRACE("Graphics Pipeline Created");
    }

    void Renderer::CreateFramebuffers()
    {
        TR_CORE_TRACE("Creating Framebuffers");

        m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

        for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
        {
            VkImageView attachments[] = { m_SwapchainImageViews[i] };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_RenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_SwapchainExtent.width;
            framebufferInfo.height = m_SwapchainExtent.height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(Application::GetDevice(), &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create framebuffer {}", i);
            }
        }

        TR_CORE_TRACE("Framebuffers Created ({} Total)", m_SwapchainFramebuffers.size());
    }

    void Renderer::CreateCommandPool()
    {
        TR_CORE_TRACE("Creating Command Pool");

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = Application::GetQueueFamilyIndices().GraphicsFamily.value();

        if (vkCreateCommandPool(Application::GetDevice(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create command pool");
        }

        TR_CORE_TRACE("Command Pool Created");
    }

    void Renderer::CreateCommandBuffer()
    {
        TR_CORE_TRACE("Recording Command Buffers");

        m_CommandBuffers.resize(m_SwapchainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

        if (vkAllocateCommandBuffers(Application::GetDevice(), &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate command buffers");
        }

        for (size_t i = 0; i < m_CommandBuffers.size(); ++i)
        {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            if (vkBeginCommandBuffer(m_CommandBuffers[i], &beginInfo) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to begin recording command buffer {}", i);
            }

            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = m_RenderPass;
            rpInfo.framebuffer = m_SwapchainFramebuffers[i];
            rpInfo.renderArea.offset = { 0, 0 };
            rpInfo.renderArea.extent = m_SwapchainExtent;

            VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
            rpInfo.clearValueCount = 1;
            rpInfo.pClearValues = &clearColor;

            vkCmdBeginRenderPass(m_CommandBuffers[i], &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);
            vkCmdBindDescriptorSets(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1, &m_DescriptorSets[i], 0, nullptr);

            VkBuffer vertexBuffers[] = { m_VertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(m_CommandBuffers[i], m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexed(m_CommandBuffers[i], m_IndexCount, 1, 0, 0, 0);
            vkCmdEndRenderPass(m_CommandBuffers[i]);

            if (vkEndCommandBuffer(m_CommandBuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to record command buffer {}", i);
            }
        }

        TR_CORE_TRACE("Command Buffers Recorded");
    }

    void Renderer::CreateUniformBuffer()
    {
        TR_CORE_TRACE("Creating Uniform Buffers");

        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        m_UniformBuffers.resize(m_SwapchainImages.size());
        m_UniformBuffersMemory.resize(m_SwapchainImages.size());

        for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
        {
            CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_UniformBuffers[i], m_UniformBuffersMemory[i]);
        }

        TR_CORE_TRACE("Uniform Buffers Created ({} Buffers)", m_UniformBuffers.size());
    }

    void Renderer::CreateDescriptorPool()
    {
        TR_CORE_TRACE("Creating Descriptor Pool");

        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(m_SwapchainImages.size());

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = static_cast<uint32_t>(m_SwapchainImages.size());

        if (vkCreateDescriptorPool(Application::GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor pool");
        }

        TR_CORE_TRACE("Descriptor Pool Created (MaxSets = {})", poolInfo.maxSets);
    }

    void Renderer::CreateDescriptorSets()
    {
        TR_CORE_TRACE("Allocating Descriptor Sets");

        VkDevice device = Application::GetDevice();
        size_t imageCount = m_SwapchainImages.size();

        std::vector<VkDescriptorSetLayout> layouts(imageCount, m_DescriptorSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_DescriptorPool;
        allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
        allocInfo.pSetLayouts = layouts.data();

        m_DescriptorSets.resize(imageCount);
        if (vkAllocateDescriptorSets(device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to allocate descriptor sets");
        }

        for (size_t i = 0; i < imageCount; ++i)
        {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = m_UniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_DescriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
        }

        TR_CORE_TRACE("Descriptor Sets Allocated ({})", imageCount);
    }

    void Renderer::CreateSyncObjects()
    {
        TR_CORE_TRACE("Creating Sync Objects");

        m_ImagesInFlight.resize(m_SwapchainImages.size());
        std::fill(m_ImagesInFlight.begin(), m_ImagesInFlight.end(), VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (vkCreateSemaphore(Application::GetDevice(), &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(Application::GetDevice(), &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(Application::GetDevice(), &fenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create sync objects for frame {}", i);
            }
        }

        TR_CORE_TRACE("Sync Objects Created ({} Frames In Flight)", MAX_FRAMES_IN_FLIGHT);
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

    SwapchainSupportDetails Renderer::QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    {
        SwapchainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.Capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount)
        {
            details.Formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.Formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount)
        {
            details.PresentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.PresentModes.data());
        }

        return details;
    }

    VkSurfaceFormatKHR Renderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
    {
        for (const auto& it_AvailableFormat : availableFormats)
        {
            if (it_AvailableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && it_AvailableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return it_AvailableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR Renderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
    {
        for (auto a_Mode : availablePresentModes)
        {
            if (a_Mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return a_Mode;
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D Renderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities)
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        else
        {
            VkExtent2D actualExtent = { 1920, 1080 };

            actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
            actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

            return actualExtent;
        }
    }

    void Renderer::RecreateSwapchain()
    {
        TR_CORE_TRACE("Recreating Swapchain");

        VkDevice device = Application::GetDevice();

        vkDeviceWaitIdle(device);

        for (VkFramebuffer fb : m_SwapchainFramebuffers)
        {
            vkDestroyFramebuffer(device, fb, nullptr);
        }
        m_SwapchainFramebuffers.clear();

        for (VkImageView view : m_SwapchainImageViews)
        {
            vkDestroyImageView(device, view, nullptr);
        }
        m_SwapchainImageViews.clear();        

        vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
        m_SwapchainImages.clear();

        CreateSwapchain();
        CreateImageViews();
        CreateFramebuffers();

        vkFreeCommandBuffers(device, m_CommandPool, static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
        CreateCommandBuffer();

        TR_CORE_TRACE("Swapchain Recreated");
    }
}