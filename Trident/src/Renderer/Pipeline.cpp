#include "Renderer/Pipeline.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Vertex.h"

#include "Application.h"

#include "Core/Utilities.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdlib>
#include <sstream>
#include <system_error>

namespace Trident
{
    void Pipeline::Init(Swapchain& swapchain)
    {
        InitializeShaderStages();
        CreateRenderPass(swapchain);
        CreateDescriptorSetLayout();
        CreateGraphicsPipeline(swapchain);
        CreateFramebuffers(swapchain);
    }

    void Pipeline::Cleanup()
    {
        CleanupFramebuffers();
        DestroyGraphicsPipeline();

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

        m_ShaderStages.clear();
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

        // Tear down swapchain depth attachments so future recreations can allocate fresh images.
        for (VkImageView it_View : m_SwapchainDepthImageViews)
        {
            if (it_View != VK_NULL_HANDLE)
            {
                vkDestroyImageView(Application::GetDevice(), it_View, nullptr);
            }
        }

        for (VkImage it_Image : m_SwapchainDepthImages)
        {
            if (it_Image != VK_NULL_HANDLE)
            {
                vkDestroyImage(Application::GetDevice(), it_Image, nullptr);
            }
        }

        for (VkDeviceMemory it_Memory : m_SwapchainDepthMemory)
        {
            if (it_Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(Application::GetDevice(), it_Memory, nullptr);
            }
        }

        m_SwapchainDepthImageViews.clear();
        m_SwapchainDepthImages.clear();
        m_SwapchainDepthMemory.clear();
    }

    void Pipeline::DestroyGraphicsPipeline()
    {
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
    }

    void Pipeline::InitializeShaderStages()
    {
        m_ShaderStages.clear();

        std::filesystem::path l_ShaderRoot = std::filesystem::path("Assets") / "Shaders";

        ShaderStage l_Vertex{};
        l_Vertex.Stage = VK_SHADER_STAGE_VERTEX_BIT;
        l_Vertex.SourcePath = (l_ShaderRoot / "Default.vert").generic_string();
        l_Vertex.SpirvPath = l_Vertex.SourcePath + ".spv";
        m_ShaderStages.push_back(l_Vertex);

        ShaderStage l_Fragment{};
        l_Fragment.Stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_Fragment.SourcePath = (l_ShaderRoot / "Default.frag").generic_string();
        l_Fragment.SpirvPath = l_Fragment.SourcePath + ".spv";
        m_ShaderStages.push_back(l_Fragment);

        // Cache initial timestamps so the first frame hot-reload check does not trigger unnecessarily.
        std::error_code l_Error{};
        for (auto& l_Shader : m_ShaderStages)
        {
            if (std::filesystem::exists(l_Shader.SourcePath, l_Error))
            {
                l_Shader.SourceTimestamp = std::filesystem::last_write_time(l_Shader.SourcePath, l_Error);
            }
            if (std::filesystem::exists(l_Shader.SpirvPath, l_Error))
            {
                l_Shader.SpirvTimestamp = std::filesystem::last_write_time(l_Shader.SpirvPath, l_Error);
            }
        }
    }

    bool Pipeline::EnsureShaderBinaries()
    {
        bool l_AllCompiled = true;
        std::error_code l_Error{};

        for (auto& l_Shader : m_ShaderStages)
        {
            if (!std::filesystem::exists(l_Shader.SourcePath, l_Error))
            {
                TR_CORE_CRITICAL("Missing shader source: {}", l_Shader.SourcePath);
                l_AllCompiled = false;
                continue;
            }

            bool l_ShouldCompile = false;

            if (!std::filesystem::exists(l_Shader.SpirvPath, l_Error))
            {
                l_ShouldCompile = true;
            }
            else
            {
                auto l_SourceTime = std::filesystem::last_write_time(l_Shader.SourcePath, l_Error);
                auto l_SpirvTime = std::filesystem::last_write_time(l_Shader.SpirvPath, l_Error);
                if (l_SpirvTime < l_SourceTime)
                {
                    l_ShouldCompile = true;
                }
            }

            if (l_ShouldCompile)
            {
                if (!CompileShaderStage(l_Shader))
                {
                    l_AllCompiled = false;
                }
            }
        }

        return l_AllCompiled;
    }

    bool Pipeline::CompileShaderStage(ShaderStage& shaderStage)
    {
        std::vector<std::string> l_Commands;

        auto l_BuildCommand = [&shaderStage](const std::string& compiler)
            {
                return std::string("\"") + compiler + "\" -V \"" + shaderStage.SourcePath + "\" -o \"" + shaderStage.SpirvPath + "\"";
            };

        if (std::string l_Compiler = LocateShaderCompiler(); !l_Compiler.empty())
        {
            l_Commands.push_back(l_BuildCommand(l_Compiler));
        }
        else
        {
            // Fall back to common compiler names so developers can rely on PATH resolution.
            std::array<const char*, 4> l_DefaultCompilers{ "glslc", "glslc.exe", "glslangValidator", "glslangValidator.exe" };
            for (const char* l_Name : l_DefaultCompilers)
            {
                l_Commands.push_back(l_BuildCommand(l_Name));
            }
        }

        for (const std::string& l_Command : l_Commands)
        {
            int l_Result = std::system(l_Command.c_str());
            if (l_Result == 0)
            {
                std::error_code l_Error{};
                shaderStage.SourceTimestamp = std::filesystem::last_write_time(shaderStage.SourcePath, l_Error);
                shaderStage.SpirvTimestamp = std::filesystem::last_write_time(shaderStage.SpirvPath, l_Error);
                TR_CORE_INFO("Compiled shader {}", shaderStage.SourcePath);

                return true;
            }

            TR_CORE_WARN("Shader compile command failed (code {}): {}", l_Result, l_Command);
        }

        TR_CORE_CRITICAL("Failed to compile shader {}", shaderStage.SourcePath);

        return false;
    }

    std::string Pipeline::LocateShaderCompiler() const
    {
        if (const char* l_Custom = std::getenv("TRIDENT_GLSL_COMPILER"))
        {
            std::filesystem::path l_CustomPath = l_Custom;
            if (std::filesystem::exists(l_CustomPath))
            {
                return l_CustomPath.generic_string();
            }
        }

        std::vector<std::filesystem::path> l_Candidates;

        auto l_PushCompiler = [&l_Candidates](const std::filesystem::path& directory)
            {
                if (directory.empty())
                {
                    return;
                }

                std::array<const char*, 4> l_Names{ "glslc", "glslc.exe", "glslangValidator", "glslangValidator.exe" };
                for (const char* l_Name : l_Names)
                {
                    l_Candidates.emplace_back(directory / l_Name);
                }
            };

        if (const char* l_VulkanSdk = std::getenv("VULKAN_SDK"))
        {
            std::filesystem::path l_Root = l_VulkanSdk;
            l_PushCompiler(l_Root / "Bin");
            l_PushCompiler(l_Root / "bin");
        }

        if (const char* l_PathEnv = std::getenv("PATH"))
        {
            std::string l_PathString = l_PathEnv;
#ifdef _WIN32
            char l_Delimiter = ';';
#else
            char l_Delimiter = ':';
#endif
            std::stringstream l_Stream(l_PathString);
            std::string l_Item;
            while (std::getline(l_Stream, l_Item, l_Delimiter))
            {
                l_PushCompiler(l_Item);
            }
        }

        for (const auto& l_Path : l_Candidates)
        {
            if (std::filesystem::exists(l_Path))
            {
                return l_Path.generic_string();
            }
        }

        return {};
    }

    VkFormat Pipeline::SelectDepthFormat() const
    {
        // Prefer higher precision formats so that cascaded shadow maps or SSAO integrations remain stable in the future.
        const std::array<VkFormat, 3> l_Candidates{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT };
        for (VkFormat l_Format : l_Candidates)
        {
            VkFormatProperties l_Properties{};
            vkGetPhysicalDeviceFormatProperties(Application::GetPhysicalDevice(), l_Format, &l_Properties);
            if ((l_Properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            {
                return l_Format;
            }
        }

        TR_CORE_CRITICAL("Failed to locate a supported depth format; falling back to VK_FORMAT_D32_SFLOAT");
        return VK_FORMAT_D32_SFLOAT;
    }

    uint32_t Pipeline::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties l_MemoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(Application::GetPhysicalDevice(), &l_MemoryProperties);

        for (uint32_t i = 0; i < l_MemoryProperties.memoryTypeCount; ++i)
        {
            if ((typeFilter & (1u << i)) && (l_MemoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        TR_CORE_CRITICAL("Failed to find suitable memory type (typeFilter = {}, properties = 0x{:X})", typeFilter, properties);
        return 0;
    }

    void Pipeline::CreateRenderPass(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Render Pass");

        // Locate a depth format that is compatible with the current GPU; this keeps the renderer portable across vendors.
        m_DepthFormat = SelectDepthFormat();

        VkAttachmentDescription l_ColorAttachment{};
        l_ColorAttachment.format = swapchain.GetImageFormat();
        l_ColorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        // Preserve swapchain contents because multi-panel compositing blits before the pass begins.
        l_ColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        l_ColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        l_ColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_ColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_ColorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        l_ColorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription l_DepthAttachment{};
        l_DepthAttachment.format = m_DepthFormat;
        l_DepthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        l_DepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        l_DepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_DepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        l_DepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        l_DepthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        l_DepthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference l_colorAttachmentReference{};
        l_colorAttachmentReference.attachment = 0;
        l_colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference l_DepthAttachmentReference{};
        l_DepthAttachmentReference.attachment = 1;
        l_DepthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription l_Subpass{};
        l_Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        l_Subpass.colorAttachmentCount = 1;
        l_Subpass.pColorAttachments = &l_colorAttachmentReference;
        l_Subpass.pDepthStencilAttachment = &l_DepthAttachmentReference;

        VkSubpassDependency l_Dependency{};
        l_Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        l_Dependency.dstSubpass = 0;
        l_Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        l_Dependency.srcAccessMask = 0;
        l_Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        l_Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 2> l_Attachments{ l_ColorAttachment, l_DepthAttachment };
        VkRenderPassCreateInfo l_RenderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        l_RenderPassInfo.attachmentCount = static_cast<uint32_t>(l_Attachments.size());
        l_RenderPassInfo.pAttachments = l_Attachments.data();
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

        VkDescriptorSetLayoutBinding l_GlobalLayoutBinding{};
        l_GlobalLayoutBinding.binding = 0;
        l_GlobalLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_GlobalLayoutBinding.descriptorCount = 1;
        l_GlobalLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        l_GlobalLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_MaterialLayoutBinding{};
        l_MaterialLayoutBinding.binding = 1;
        l_MaterialLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        l_MaterialLayoutBinding.descriptorCount = 1;
        l_MaterialLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_MaterialLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_SamplerLayoutBinding{};
        l_SamplerLayoutBinding.binding = 2;
        l_SamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        l_SamplerLayoutBinding.descriptorCount = 1;
        l_SamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        l_SamplerLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding l_Bindings[] = { l_GlobalLayoutBinding, l_MaterialLayoutBinding, l_SamplerLayoutBinding };

        VkDescriptorSetLayoutCreateInfo l_LayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        l_LayoutInfo.bindingCount = 3;
        l_LayoutInfo.pBindings = l_Bindings;

        if (vkCreateDescriptorSetLayout(Application::GetDevice(), &l_LayoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create descriptor set layout");
        }

        TR_CORE_TRACE("Descriptor Set Layout Created");
    }

    void Pipeline::CreateGraphicsPipeline(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Graphics Pipeline");

        DestroyGraphicsPipeline();

        if (!EnsureShaderBinaries())
        {
            TR_CORE_WARN("Shader compilation reported issues; attempting to reuse existing SPIR-V artifacts");
        }

        std::vector<VkPipelineShaderStageCreateInfo> l_ShaderStages;
        std::vector<VkShaderModule> l_ShaderModules;
        l_ShaderStages.reserve(m_ShaderStages.size());
        l_ShaderModules.reserve(m_ShaderStages.size());

        for (auto& l_Shader : m_ShaderStages)
        {
            auto a_Code = Utilities::FileManagement::ReadBinaryFile(l_Shader.SpirvPath);
            if (a_Code.empty())
            {
                TR_CORE_CRITICAL("Failed to read shader binary: {}", l_Shader.SpirvPath);
                continue;
            }

            VkShaderModule l_Module = CreateShaderModule(a_Code);
            if (l_Module == VK_NULL_HANDLE)
            {
                continue;
            }

            VkPipelineShaderStageCreateInfo l_ShaderStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            l_ShaderStage.stage = l_Shader.Stage;
            l_ShaderStage.module = l_Module;
            l_ShaderStage.pName = "main";

            l_ShaderStages.push_back(l_ShaderStage);
            l_ShaderModules.push_back(l_Module);
        }

        if (l_ShaderStages.size() != m_ShaderStages.size())
        {
            for (VkShaderModule it_Module : l_ShaderModules)
            {
                vkDestroyShaderModule(Application::GetDevice(), it_Module, nullptr);
            }

            TR_CORE_CRITICAL("Aborting pipeline creation because a shader stage failed to load");

            return;
        }

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

        // Enable depth testing so geometry renders with proper occlusion; less-or-equal supports skybox depth of 1.0f.
        VkPipelineDepthStencilStateCreateInfo l_DepthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        l_DepthStencil.depthTestEnable = VK_TRUE;
        l_DepthStencil.depthWriteEnable = VK_TRUE;
        l_DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        l_DepthStencil.depthBoundsTestEnable = VK_FALSE;
        l_DepthStencil.stencilTestEnable = VK_FALSE;

        VkDynamicState l_DynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo l_DynamicState{};
        l_DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        l_DynamicState.dynamicStateCount = 2;
        l_DynamicState.pDynamicStates = l_DynamicStates;

        VkPushConstantRange l_PushConstant{};
        l_PushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        l_PushConstant.offset = 0;
        l_PushConstant.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo l_PipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        l_PipelineLayoutInfo.setLayoutCount = 1;
        l_PipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
        l_PipelineLayoutInfo.pushConstantRangeCount = 1;
        l_PipelineLayoutInfo.pPushConstantRanges = &l_PushConstant;

        if (vkCreatePipelineLayout(Application::GetDevice(), &l_PipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS)
        {
            TR_CORE_CRITICAL("Failed to create pipeline layout");
        }

        VkGraphicsPipelineCreateInfo l_PipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        l_PipelineInfo.stageCount = static_cast<uint32_t>(l_ShaderStages.size());
        l_PipelineInfo.pStages = l_ShaderStages.data();
        l_PipelineInfo.pVertexInputState = &l_VertexInputInfo;
        l_PipelineInfo.pInputAssemblyState = &l_InputAssembly;
        l_PipelineInfo.pViewportState = &l_ViewportState;
        l_PipelineInfo.pRasterizationState = &l_Rasterizer;
        l_PipelineInfo.pMultisampleState = &l_Multisampling;
        l_PipelineInfo.pDepthStencilState = &l_DepthStencil;
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

        for (VkShaderModule it_Module : l_ShaderModules)
        {
            vkDestroyShaderModule(Application::GetDevice(), it_Module, nullptr);
        }

        std::error_code l_Error{};
        for (auto& l_Shader : m_ShaderStages)
        {
            if (std::filesystem::exists(l_Shader.SourcePath, l_Error))
            {
                l_Shader.SourceTimestamp = std::filesystem::last_write_time(l_Shader.SourcePath, l_Error);
            }
            if (std::filesystem::exists(l_Shader.SpirvPath, l_Error))
            {
                l_Shader.SpirvTimestamp = std::filesystem::last_write_time(l_Shader.SpirvPath, l_Error);
            }
        }

        TR_CORE_TRACE("Graphics Pipeline Created");
    }

    void Pipeline::CreateFramebuffers(Swapchain& swapchain)
    {
        TR_CORE_TRACE("Creating Framebuffers");

        const VkDevice l_Device = Application::GetDevice();
        const size_t l_ImageCount = swapchain.GetImageViews().size();

        m_SwapchainFramebuffers.resize(l_ImageCount);
        m_SwapchainDepthImages.assign(l_ImageCount, VK_NULL_HANDLE);
        m_SwapchainDepthMemory.assign(l_ImageCount, VK_NULL_HANDLE);
        m_SwapchainDepthImageViews.assign(l_ImageCount, VK_NULL_HANDLE);

        for (size_t i = 0; i < l_ImageCount; ++i)
        {
            // Create a dedicated depth image for each swapchain back buffer so layout transitions remain independent per frame.
            VkImageCreateInfo l_DepthInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            l_DepthInfo.imageType = VK_IMAGE_TYPE_2D;
            l_DepthInfo.extent.width = swapchain.GetExtent().width;
            l_DepthInfo.extent.height = swapchain.GetExtent().height;
            l_DepthInfo.extent.depth = 1;
            l_DepthInfo.mipLevels = 1;
            l_DepthInfo.arrayLayers = 1;
            l_DepthInfo.format = m_DepthFormat;
            l_DepthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            l_DepthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            l_DepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            l_DepthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            l_DepthInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(l_Device, &l_DepthInfo, nullptr, &m_SwapchainDepthImages[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create depth image for swapchain framebuffer {}", i);
                continue;
            }

            VkMemoryRequirements l_DepthRequirements{};
            vkGetImageMemoryRequirements(l_Device, m_SwapchainDepthImages[i], &l_DepthRequirements);

            VkMemoryAllocateInfo l_AllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            l_AllocateInfo.allocationSize = l_DepthRequirements.size;
            l_AllocateInfo.memoryTypeIndex = FindMemoryType(l_DepthRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (vkAllocateMemory(l_Device, &l_AllocateInfo, nullptr, &m_SwapchainDepthMemory[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to allocate depth memory for swapchain framebuffer {}", i);
                continue;
            }

            vkBindImageMemory(l_Device, m_SwapchainDepthImages[i], m_SwapchainDepthMemory[i], 0);

            VkImageViewCreateInfo l_DepthViewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            l_DepthViewInfo.image = m_SwapchainDepthImages[i];
            l_DepthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            l_DepthViewInfo.format = m_DepthFormat;
            l_DepthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            l_DepthViewInfo.subresourceRange.baseMipLevel = 0;
            l_DepthViewInfo.subresourceRange.levelCount = 1;
            l_DepthViewInfo.subresourceRange.baseArrayLayer = 0;
            l_DepthViewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(l_Device, &l_DepthViewInfo, nullptr, &m_SwapchainDepthImageViews[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create depth view for swapchain framebuffer {}", i);
                continue;
            }

            std::array<VkImageView, 2> l_Attachments{ swapchain.GetImageViews()[i], m_SwapchainDepthImageViews[i] };
            // Future improvement: upgrade these attachments to support MSAA or a dedicated depth pre-pass when post effects arrive.

            VkFramebufferCreateInfo l_FramebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            l_FramebufferInfo.renderPass = m_RenderPass;
            l_FramebufferInfo.attachmentCount = static_cast<uint32_t>(l_Attachments.size());
            l_FramebufferInfo.pAttachments = l_Attachments.data();
            l_FramebufferInfo.width = swapchain.GetExtent().width;
            l_FramebufferInfo.height = swapchain.GetExtent().height;
            l_FramebufferInfo.layers = 1;

            if (vkCreateFramebuffer(l_Device, &l_FramebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
            {
                TR_CORE_CRITICAL("Failed to create it_Framebuffer {}", i);
            }
        }

        TR_CORE_TRACE("Framebuffers Created ({} Total)", m_SwapchainFramebuffers.size());
    }

    bool Pipeline::ReloadIfNeeded(Swapchain& swapchain, bool waitForDevice)
    {
        std::error_code l_Error{};
        bool l_ShouldReload = false;

        for (auto& l_Shader : m_ShaderStages)
        {
            if (!std::filesystem::exists(l_Shader.SourcePath, l_Error))
            {
                continue;
            }

            auto l_WriteTime = std::filesystem::last_write_time(l_Shader.SourcePath, l_Error);
            if (l_Shader.SourceTimestamp != l_WriteTime)
            {
                l_ShouldReload = true;
            }
        }

        if (!l_ShouldReload)
        {
            return false;
        }

        if (waitForDevice)
        {
            vkDeviceWaitIdle(Application::GetDevice());
        }

        CreateGraphicsPipeline(swapchain);

        if (m_GraphicsPipeline == VK_NULL_HANDLE)
        {
            TR_CORE_ERROR("Graphics pipeline handle is null after reload attempt");
            return false;
        }

        return true;
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