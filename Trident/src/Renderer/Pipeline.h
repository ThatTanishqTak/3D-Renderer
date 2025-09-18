#pragma once

#include <vulkan/vulkan.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Trident
{
    class Swapchain;

    class Pipeline
    {
    public:
        void Init(Swapchain& swapchain);
        void Cleanup();
        void RecreateFramebuffers(Swapchain& swapchain);
        void CleanupFramebuffers();
        void CreateFramebuffers(Swapchain& swapchain);
        bool ReloadIfNeeded(Swapchain& swapchain);

        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        VkPipeline GetPipeline() const { return m_GraphicsPipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
        const std::vector<VkFramebuffer>& GetFramebuffers() const { return m_SwapchainFramebuffers; }

    private:
        struct ShaderStage
        {
            VkShaderStageFlagBits Stage = VK_SHADER_STAGE_VERTEX_BIT;
            std::string SourcePath;                                   // Path to the GLSL file
            std::string SpirvPath;                                    // Path to the generated SPIR-V binary
            std::filesystem::file_time_type SourceTimestamp{};        // Last edit time cached for hot reload
            std::filesystem::file_time_type SpirvTimestamp{};         // Timestamp of the SPIR-V output
        };

        void CreateRenderPass(Swapchain& swapchain);
        void CreateDescriptorSetLayout();
        void CreateGraphicsPipeline(Swapchain& swapchain);
        void DestroyGraphicsPipeline();
        void InitializeShaderStages();
        bool EnsureShaderBinaries();
        bool CompileShaderStage(ShaderStage& shaderStage);
        std::string LocateShaderCompiler() const;

        VkShaderModule CreateShaderModule(const std::vector<char>& code);

    private:
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;
        std::vector<ShaderStage> m_ShaderStages;
    };
}