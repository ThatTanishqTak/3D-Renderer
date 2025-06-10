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
        for (VkFramebuffer framebuffer : m_SwapchainFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(Application::GetDevice(), framebuffer, nullptr);
            }
        }
        m_SwapchainFramebuffers.clear();
    }

    void Pipeline::CreateRenderPass(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Render Pass");

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = swapchain.GetImageFormat();
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

        VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
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

    void Pipeline::CreateDescriptorSetLayout()
    {
        TR_CORE_TRACE("Creating Descriptor Set Layout");

        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(Application::GetDevice(), &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor set layout");
        }

        TR_CORE_TRACE("Descriptor Set Layout Created");
    }

    void Pipeline::CreateGraphicsPipeline(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Graphics Pipeline");

        auto vertexCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.vert.spv");
        auto fragmentCode = Utilities::FileManagement::ReadFile("Assets/Shaders/Cube.frag.spv");

        VkShaderModule vertexModule = VK_NULL_HANDLE;
        VkShaderModule fragmentModule = VK_NULL_HANDLE;

        auto createShaderModule = [&](const std::vector<char>& code) -> VkShaderModule
            {
                VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
                createInfo.codeSize = code.size();
                createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

                VkShaderModule module;
                if (vkCreateShaderModule(Application::GetDevice(), &createInfo, nullptr, &module) != VK_SUCCESS)
                {
                    TR_CORE_CRITICAL("Failed to create shader module");
                }
                return module;
            };

        vertexModule = createShaderModule(vertexCode);
        fragmentModule = createShaderModule(fragmentCode);

        VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertexModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragmentModule;
        fragStage.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

        auto bindingDescription = Vertex::GetBindingDescription();
        auto attributeDescriptions = Vertex::GetAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(swapchain.GetExtent().width);
        viewport.height = static_cast<float>(swapchain.GetExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = { 0,0 };
        scissor.extent = swapchain.GetExtent();

        VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;

        if (vkCreatePipelineLayout(Application::GetDevice(), &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create pipeline layout");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
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

        vkDestroyShaderModule(Application::GetDevice(), fragmentModule, nullptr);
        vkDestroyShaderModule(Application::GetDevice(), vertexModule, nullptr);

        TR_CORE_TRACE("Graphics Pipeline Created");
    }

    void Pipeline::CreateFramebuffers(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Framebuffers");

        m_SwapchainFramebuffers.resize(swapchain.GetImageViews().size());

        for (size_t i = 0; i < swapchain.GetImageViews().size(); ++i)
        {
            VkImageView attachments[] = { swapchain.GetImageViews()[i] };

            VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            framebufferInfo.renderPass = m_RenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = swapchain.GetExtent().width;
            framebufferInfo.height = swapchain.GetExtent().height;
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(Application::GetDevice(), &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create framebuffer {}", i);
            }
        }

        TR_CORE_TRACE("Framebuffers Created ({} Total)", m_SwapchainFramebuffers.size());
    }
}