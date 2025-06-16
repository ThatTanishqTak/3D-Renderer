#include "Renderer/Pipeline.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Vertex.h"

#include "Application.h"

#include "Core/Utilities.h"

namespace Trident
{
    void Pipeline::Init(Swapchain& swapchain)
    {
        CreateRenderPass(swapchain);
        CreateDescriptorSetLayout();
        CreateGraphicsPipeline(swapchain);
        CreateFramebuffers(swapchain);
    }

    void Pipeline::Cleanup()
    {
        CleanupFramebuffers();

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

        if (m_DescriptorSetLayout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(Application::GetDevice(), m_DescriptorSetLayout, nullptr);

            m_DescriptorSetLayout = VK_NULL_HANDLE;
        }
    }

    void Pipeline::RecreateFramebuffers(Swapchain& swapchain)
    {
        CleanupFramebuffers();
        CreateFramebuffers(swapchain);
    }

    void Pipeline::CleanupFramebuffers()
    {
        for (VkFramebuffer it_Framebuffer : m_SwapchainFramebuffers)
        {
            if (it_Framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(Application::GetDevice(), it_Framebuffer, nullptr);
            }
        }

        m_SwapchainFramebuffers.clear();
    }

    void Pipeline::CreateRenderPass(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Render Pass");

        VkAttachmentDescription l_ColorAttachment{};
        l_ColorAttachment.format = swapchain.GetImageFormat();
        l_ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        l_ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference l_colorAttachmentReference{};
        l_colorAttachmentReference.attachment = 0;
        l_colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_colorAttachmentReference;

        VkSubpassDependency l_Dependency{};
        l_Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        l_Dependency.dstSubpass = 0;
        l_Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        l_Dependency.srcAccessMask = 0;
        l_Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        l_Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo l_RenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
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

    void Pipeline::CreateDescriptorSetLayout()
    {
        TR_CORE_TRACE("Creating Descriptor Set Layout");

        VkDescriptorSetLayoutBinding l_UboLayoutBinding{};
        l_UboLayoutBinding.binding = 0;
        l_UboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_UboLayoutBinding.descriptorCount = 1;
        l_UboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        l_UboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo l_LayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        l_LayoutInfo.bindingCount = 1;
        l_LayoutInfo.pBindings = &l_UboLayoutBinding;

        if (vkCreateDescriptorSetLayout(Application::GetDevice(), &l_LayoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor set layout");
        }

        TR_CORE_TRACE("Descriptor Set Layout Created");
    }

    void Pipeline::CreateGraphicsPipeline(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Graphics Pipeline");

        auto a_VertexCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.vert.spv");
        auto a_FragmentCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.frag.spv");

        VkShaderModule l_VertexModule = VK_NULL_HANDLE;
        VkShaderModule l_FragmentModule = VK_NULL_HANDLE;

        l_VertexModule = CreateShaderModule(a_VertexCode);
        l_FragmentModule = CreateShaderModule(a_FragmentCode);

        VkPipelineShaderStageCreateInfo l_VertexStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        l_VertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        l_VertexStage.module = l_VertexModule;
        l_VertexStage.pName = "main";

        VkPipelineShaderStageCreateInfo l_FragmentStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        l_FragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_FragmentStage.module = l_FragmentModule;
        l_FragmentStage.pName = "main";

        VkPipelineShaderStageCreateInfo l_ShaderStages[] = { l_VertexStage, l_FragmentStage };

        auto a_BindingDescription = Vertex::GetBindingDescription();
        auto a_AttributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo l_VertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        l_VertexInputInfo.vertexBindingDescriptionCount = 1;
        l_VertexInputInfo.pVertexBindingDescriptions = &a_BindingDescription;
        l_VertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(a_AttributeDescriptions.size());
        l_VertexInputInfo.pVertexAttributeDescriptions = a_AttributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo l_InputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        l_InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        l_InputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport l_Viewport{};
        l_Viewport.x = 0.0f;
        l_Viewport.y = 0.0f;
        l_Viewport.width = static_cast<float>(swapchain.GetExtent().width);
        l_Viewport.height = static_cast<float>(swapchain.GetExtent().height);
        l_Viewport.minDepth = 0.0f;
        l_Viewport.maxDepth = 1.0f;

        VkRect2D l_Scissor{};
        l_Scissor.offset = { 0,0 };
        l_Scissor.extent = swapchain.GetExtent();

        VkPipelineViewportStateCreateInfo l_ViewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        l_ViewportState.viewportCount = 1;
        l_ViewportState.pViewports = &l_Viewport;
        l_ViewportState.scissorCount = 1;
        l_ViewportState.pScissors = &l_Scissor;

        VkPipelineRasterizationStateCreateInfo l_Rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        l_Rasterizer.depthClampEnable = VK_FALSE;
        l_Rasterizer.rasterizerDiscardEnable = VK_FALSE;
        l_Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        l_Rasterizer.lineWidth = 1.0f;
        l_Rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        l_Rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        l_Rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo l_Multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        l_Multisampling.sampleShadingEnable = VK_FALSE;
        l_Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState l_ColorBlendAttachment{};
        l_ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        l_ColorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo l_ColorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        l_ColorBlending.logicOpEnable = VK_FALSE;
        l_ColorBlending.attachmentCount = 1;
        l_ColorBlending.pAttachments = &l_ColorBlendAttachment;

        VkDynamicState l_DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo l_DynamicState{};
        l_DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        l_DynamicState.dynamicStateCount = 2;
        l_DynamicState.pDynamicStates = l_DynamicStates;

        VkPipelineLayoutCreateInfo l_PipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        l_PipelineLayoutInfo.setLayoutCount = 1;
        l_PipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;

        if (vkCreatePipelineLayout(Application::GetDevice(), &l_PipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create pipeline layout");
        }

        VkGraphicsPipelineCreateInfo l_PipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        l_PipelineInfo.stageCount = 2;
        l_PipelineInfo.pStages = l_ShaderStages;
        l_PipelineInfo.pVertexInputState = &l_VertexInputInfo;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_Multisampling;
        l_PipelineInfo.pDepthStencilState = nullptr;
        l_PipelineInfo.pColorBlendState = &l_ColorBlending;
        l_PipelineInfo.pDynamicState = &l_DynamicState;
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

    void Pipeline::CreateFramebuffers(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Framebuffers");

        m_SwapchainFramebuffers.resize(swapchain.GetImageViews().size());

        for (size_t i = 0; i < swapchain.GetImageViews().size(); ++i)
        {
            VkImageView l_Attachments[] = { swapchain.GetImageViews()[i] };

            VkFramebufferCreateInfo l_FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            l_FramebufferInfo.renderPass = m_RenderPass;
            l_FramebufferInfo.attachmentCount = 1;
            l_FramebufferInfo.pAttachments = l_Attachments;
            l_FramebufferInfo.width = swapchain.GetExtent().width;
            l_FramebufferInfo.height = swapchain.GetExtent().height;
            l_FramebufferInfo.layers = 1;

            if (vkCreateFramebuffer(Application::GetDevice(), &l_FramebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create it_Framebuffer {}", i);
            }
        }

        TR_CORE_TRACE("Framebuffers Created ({} Total)", m_SwapchainFramebuffers.size());
    }

    //------------------------------------------------------------------------------------------------------------------------------------------------------//

    VkShaderModule Pipeline::CreateShaderModule(const std::vector<char>& code)
    {
        VkShaderModuleCreateInfo l_CreateInfo{};
        l_CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        l_CreateInfo.codeSize = code.size();
        l_CreateInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule l_Module;
        if (vkCreateShaderModule(Application::GetDevice(), &l_CreateInfo, nullptr, &l_Module) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create shader l_Module");
        }

        return l_Module;
    }
}