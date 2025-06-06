#include "Renderer/Renderer.h"

#include "Application.h"

#include <stdexcept>

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
        CreateCommandBuffer();
        CreateUniformBuffer();
        CreateSyncObjects();

        TR_CORE_INFO("-------RENDERER INITIALIZED-------");
    }

    void Renderer::Shutdown()
    {
        TR_CORE_INFO("Shutting Down Renderer");

        vkDeviceWaitIdle(Application::GetDevice());

        for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i)
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

        m_RenderFinishedSemaphores.clear();
        m_ImageAvailableSemaphores.clear();
        m_InFlightFences.clear();

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

        for (VkFramebuffer fb : m_SwapchainFramebuffers)
        {
            if (fb != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(Application::GetDevice(), fb, nullptr);
            }
        }
        
        m_SwapchainFramebuffers.clear();

        for (VkImageView view : m_SwapchainImageViews)
        {
            if (view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(Application::GetDevice(), view, nullptr);
            }
        }

        m_SwapchainImageViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(Application::GetDevice(), m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }

        m_SwapchainImages.clear();

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

        TR_CORE_INFO("Renderer Shutdown Complete");
    }

    void Renderer::DrawFrame()
    {
        static size_t s_CurrentFrame = 0;

        vkWaitForFences(Application::GetDevice(), 1, &m_InFlightFences[s_CurrentFrame], VK_TRUE, UINT64_MAX);

        uint32_t l_ImageIndex;
        VkResult l_Result = vkAcquireNextImageKHR(Application::GetDevice(), m_Swapchain, UINT64_MAX,
            m_ImageAvailableSemaphores[s_CurrentFrame], VK_NULL_HANDLE, &l_ImageIndex);

        if (l_Result != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to acquire swapchain image (code {})", static_cast<int>(l_Result));
            return;
        }

        VkSubmitInfo l_SubmitInfo{};
        l_SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore l_WaitSemaphores[] = { m_ImageAvailableSemaphores[s_CurrentFrame] };
        VkPipelineStageFlags l_WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        l_SubmitInfo.waitSemaphoreCount = 1;
        l_SubmitInfo.pWaitSemaphores = l_WaitSemaphores;
        l_SubmitInfo.pWaitDstStageMask = l_WaitStages;

        l_SubmitInfo.commandBufferCount = 1;
        l_SubmitInfo.pCommandBuffers = &m_CommandBuffers[l_ImageIndex];

        VkSemaphore l_SignalSemaphores[] = { m_RenderFinishedSemaphores[s_CurrentFrame] };
        l_SubmitInfo.signalSemaphoreCount = 1;
        l_SubmitInfo.pSignalSemaphores = l_SignalSemaphores;

        vkResetFences(Application::GetDevice(), 1, &m_InFlightFences[s_CurrentFrame]);

        if (vkQueueSubmit(Application::GetGraphicsQueue(), 1, &l_SubmitInfo, m_InFlightFences[s_CurrentFrame]) != VK_SUCCESS)
        {
            TR_CORE_ERROR("Failed to submit draw command buffer");
            return;
        }

        VkPresentInfoKHR l_PresentInfo{};
        l_PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        l_PresentInfo.waitSemaphoreCount = 1;
        l_PresentInfo.pWaitSemaphores = l_SignalSemaphores;
        l_PresentInfo.swapchainCount = 1;
        l_PresentInfo.pSwapchains = &m_Swapchain;
        l_PresentInfo.pImageIndices = &l_ImageIndex;

        vkQueuePresentKHR(Application::GetPresentQueue(), &l_PresentInfo);

        s_CurrentFrame = (s_CurrentFrame + 1) % m_InFlightFences.size();
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    void Renderer::CreateSwapchain()
    {
        TR_CORE_INFO("Creating Swapchain");

        // Query swapchain support details
        auto l_Details = QuerySwapchainSupport(Application::GetPhysicalDevice(), Application::GetSurface());
        auto l_SurfaceFormat = ChooseSwapSurfaceFormat(l_Details.Formats);
        auto l_PresentMode = ChooseSwapPresentMode(l_Details.PresentModes);
        auto l_Extent = ChooseSwapExtent(l_Details.Capabilities);

        // Determine number of images
        uint32_t l_ImageCount = l_Details.Capabilities.minImageCount + 1;
        if (l_Details.Capabilities.maxImageCount > 0 && l_ImageCount > l_Details.Capabilities.maxImageCount)
        {
            l_ImageCount = l_Details.Capabilities.maxImageCount;
        }

        // Fill create info
        VkSwapchainCreateInfoKHR l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        l_CreateInfo.surface = Application::GetSurface();
        l_CreateInfo.minImageCount = l_ImageCount;
        l_CreateInfo.imageFormat = l_SurfaceFormat.format;
        l_CreateInfo.imageColorSpace = l_SurfaceFormat.colorSpace;
        l_CreateInfo.imageExtent = l_Extent;
        l_CreateInfo.imageArrayLayers = 1;
        l_CreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        // Handle multiple queue families
        auto l_Indices = Application::GetQueueFamilyIndices();
        if (l_Indices.GraphicsFamily != l_Indices.PresentFamily)
        {
            uint32_t l_QueueFamilies[] = { l_Indices.GraphicsFamily.value(), l_Indices.PresentFamily.value() };
            l_CreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            l_CreateInfo.queueFamilyIndexCount = 2;
            l_CreateInfo.pQueueFamilyIndices = l_QueueFamilies;
        }

        else
        {
            l_CreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            l_CreateInfo.queueFamilyIndexCount = 0;
            l_CreateInfo.pQueueFamilyIndices = nullptr;
        }

        // Other settings
        l_CreateInfo.preTransform = l_Details.Capabilities.currentTransform;
        l_CreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        l_CreateInfo.presentMode = l_PresentMode;
        l_CreateInfo.clipped = VK_TRUE;
        l_CreateInfo.oldSwapchain = VK_NULL_HANDLE;

        // Create swapchain
        if (vkCreateSwapchainKHR(Application::GetDevice(), &l_CreateInfo, nullptr, &m_Swapchain) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create swap chain");
        }

        // Retrieve images
        vkGetSwapchainImagesKHR(Application::GetDevice(), m_Swapchain, &l_ImageCount, nullptr);
        m_SwapchainImages.resize(l_ImageCount);
        vkGetSwapchainImagesKHR(Application::GetDevice(), m_Swapchain, &l_ImageCount, m_SwapchainImages.data());

        m_SwapchainImageFormat = l_SurfaceFormat.format;
        m_SwapchainExtent = l_Extent;

        TR_CORE_INFO("Swapchain Created");
    }

    void Renderer::CreateImageViews()
    {
        TR_CORE_INFO("Creating Image Views");

        m_SwapchainImageViews.resize(m_SwapchainImages.size());

        for (size_t i = 0; i < m_SwapchainImages.size(); i++)
        {
            VkImageViewCreateInfo l_ViewInfo{};
            l_ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            l_ViewInfo.image = m_SwapchainImages[i];
            l_ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_ViewInfo.format = m_SwapchainImageFormat;
            l_ViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            l_ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            l_ViewInfo.subresourceRange.baseMipLevel = 0;
            l_ViewInfo.subresourceRange.levelCount = 1;
            l_ViewInfo.subresourceRange.baseArrayLayer = 0;
            l_ViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(Application::GetDevice(), &l_ViewInfo, nullptr, &m_SwapchainImageViews[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create image views!");
            }
        }

        TR_CORE_INFO("Image Views Created");
    }

    void Renderer::CreateRenderPass()
    {
        TR_CORE_INFO("Creating Renderpass");

        VkAttachmentDescription l_ColorAttachment{};
        l_ColorAttachment.format = m_SwapchainImageFormat;
        l_ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference l_ColorAttachmentRef{};
        l_ColorAttachmentRef.attachment = 0;
        l_ColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_ColorAttachmentRef;

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

        TR_CORE_INFO("Renderpass Created");
    }

    void Renderer::CreateFramebuffers()
    {
        TR_CORE_INFO("Creating Framebuffers");

        m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

        for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i)
        {
            VkImageView l_Attachments[] = { m_SwapchainImageViews[i] };

            VkFramebufferCreateInfo l_FramebufferInfo{};
            l_FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            l_FramebufferInfo.renderPass = m_RenderPass;
            l_FramebufferInfo.attachmentCount = 1;
            l_FramebufferInfo.pAttachments = l_Attachments;
            l_FramebufferInfo.width = m_SwapchainExtent.width;
            l_FramebufferInfo.height = m_SwapchainExtent.height;
            l_FramebufferInfo.layers = 1;

            if (vkCreateFramebuffer(Application::GetDevice(), &l_FramebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create framebuffer");
            }
        }

        TR_CORE_INFO("Framebuffers Created");
    }

    void Renderer::CreateDescriptorSetLayout()
    {
        VkDescriptorSetLayoutBinding l_UBOBinding{};
        l_UBOBinding.binding = 0;
        l_UBOBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_UBOBinding.descriptorCount = 1;
        l_UBOBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        l_UBOBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo l_LayoutInfo{};
        l_LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        l_LayoutInfo.bindingCount = 1;
        l_LayoutInfo.pBindings = &l_UBOBinding;

        if (vkCreateDescriptorSetLayout(Application::GetDevice(), &l_LayoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor set layout");
        }

        TR_CORE_INFO("Descriptor Set Layout Created");
    }

    void Renderer::CreateGraphicsPipeline()
    {
        TR_CORE_INFO("Creating Graphics Pipeline");

        auto a_VertexCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.vert.spv");
        auto a_FragmentCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.frag.spv");

        VkShaderModule l_VertexModule = CreateShaderModule(Application::GetDevice(), a_VertexCode);
        VkShaderModule l_FragmentModule = CreateShaderModule(Application::GetDevice(), a_FragmentCode);

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

        std::array<VkPipelineShaderStageCreateInfo, 2> l_ShaderStages = { l_VertexStage, l_FragmentStage };

        VkVertexInputBindingDescription l_BindingDescription = Vertex::GetBindingDescription();
        auto l_AttributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo l_VertexInputInfo{};
        l_VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        l_VertexInputInfo.vertexBindingDescriptionCount = 1;
        l_VertexInputInfo.pVertexBindingDescriptions = &l_BindingDescription;
        l_VertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(l_AttributeDescriptions.size());
        l_VertexInputInfo.pVertexAttributeDescriptions = l_AttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{};
        l_InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        l_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        l_InputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(m_SwapchainExtent.width);
        l_Viewport.height = static_cast<float>(m_SwapchainExtent.height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0, 0 };
        l_Scissor.extent = m_SwapchainExtent;

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

        VkPipelineMultisampleStateCreateInfo l_Multisampling{};
        l_Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        l_Multisampling.sampleShadingEnable = VK_FALSE;
        l_Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
        l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        l_ColorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo l_ColorBlending{};
        l_ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        l_ColorBlending.logicOpEnable = VK_FALSE;
        l_ColorBlending.attachmentCount = 1;
        l_ColorBlending.pAttachments = &l_ColorBlendAttachment;

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
        l_PipelineInfo.stageCount = static_cast<uint32_t>(l_ShaderStages.size());
        l_PipelineInfo.pStages = l_ShaderStages.data();
        l_PipelineInfo.pVertexInputState = &l_VertexInputInfo;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_Multisampling;
        l_PipelineInfo.pDepthStencilState = nullptr;
        l_PipelineInfo.pColorBlendState = &l_ColorBlending;
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

        TR_CORE_INFO("Graphics Pipeline Created");
    }

    void Renderer::CreateCommandPool()
    {
        TR_CORE_INFO("Creating Command Pool");

        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.queueFamilyIndex = Application::GetQueueFamilyIndices().GraphicsFamily.value();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(Application::GetDevice(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create command pool");
        }

        TR_CORE_INFO("Command Pool Created");
    }

    void Renderer::CreateCommandBuffer()
    {
        TR_CORE_INFO("Creating Command Buffer");

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

        for (size_t i = 0; i < m_CommandBuffers.size(); ++i)
        {
            VkCommandBufferBeginInfo l_BeginInfo{};
            l_BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            if (vkBeginCommandBuffer(m_CommandBuffers[i], &l_BeginInfo) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to begin recording command buffer");
            }

            VkRenderPassBeginInfo l_RenderPassInfo{};
            l_RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            l_RenderPassInfo.renderPass = m_RenderPass;
            l_RenderPassInfo.framebuffer = m_SwapchainFramebuffers[i];
            l_RenderPassInfo.renderArea.offset = { 0, 0 };
            l_RenderPassInfo.renderArea.extent = m_SwapchainExtent;

            VkClearValue l_ClearColor{};
            l_ClearColor.color = { {0.0f, 0.0f, 0.0f, 1.0f} };
            l_RenderPassInfo.clearValueCount = 1;
            l_RenderPassInfo.pClearValues = &l_ClearColor;

            vkCmdBeginRenderPass(m_CommandBuffers[i], &l_RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(m_CommandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

            VkBuffer l_VertexBuffers[] = { m_VertexBuffer };
            VkDeviceSize l_Offsets[] = { 0 };
            vkCmdBindVertexBuffers(m_CommandBuffers[i], 0, 1, l_VertexBuffers, l_Offsets);
            vkCmdBindIndexBuffer(m_CommandBuffers[i], m_IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexed(m_CommandBuffers[i], m_IndexCount, 1, 0, 0, 0);

            vkCmdEndRenderPass(m_CommandBuffers[i]);

            if (vkEndCommandBuffer(m_CommandBuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to record command buffer");
            }
        }

        TR_CORE_INFO("Command Buffer Created");
    }

    void Renderer::CreateSyncObjects()
    {
        TR_CORE_INFO("Creating Sync Objects");

        size_t l_Count = m_SwapchainImages.size();
        m_ImageAvailableSemaphores.resize(l_Count);
        m_RenderFinishedSemaphores.resize(l_Count);
        m_InFlightFences.resize(l_Count);

        VkSemaphoreCreateInfo l_SemaphoreInfo{};
        l_SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo l_FenceInfo{};
        l_FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        l_FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < l_Count; ++i)
        {
            if (vkCreateSemaphore(Application::GetDevice(), &l_SemaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(Application::GetDevice(), &l_SemaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(Application::GetDevice(), &l_FenceInfo, nullptr, &m_InFlightFences[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create synchronization objects for frame {}", i);
            }
        }

        TR_CORE_INFO("Sync Objects Created");
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

    void Renderer::CreateUniformBuffer()
    {
        VkDeviceSize l_BufferSize = sizeof(UniformBufferObject);

        m_UniformBuffers.resize(m_SwapchainImages.size());
        m_UniformBuffersMemory.resize(m_SwapchainImages.size());

        for (size_t i = 0; i < m_SwapchainImages.size(); ++i)
        {
            CreateBuffer(l_BufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_UniformBuffers[i], m_UniformBuffersMemory[i]);
        }
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
}