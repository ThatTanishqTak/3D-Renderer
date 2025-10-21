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
        bool ReloadIfNeeded(Swapchain& swapchain, bool waitForDevice = true);

        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        VkPipeline GetPipeline() const { return m_GraphicsPipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        VkPipeline GetSkyboxPipeline() const { return m_SkyboxPipeline; }
        VkPipelineLayout GetSkyboxPipelineLayout() const { return m_SkyboxPipelineLayout; }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
        VkDescriptorSetLayout GetSkyboxDescriptorSetLayout() const { return m_SkyboxDescriptorSetLayout; }
        const std::vector<VkFramebuffer>& GetFramebuffers() const { return m_SwapchainFramebuffers; }
        const std::vector<VkImage>& GetDepthImages() const { return m_SwapchainDepthImages; }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }

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
        void CreateSkyboxDescriptorSetLayout();
        void CreateGraphicsPipeline(Swapchain& swapchain);
        void CreateSkyboxPipeline(Swapchain& swapchain);
        void DestroyGraphicsPipeline();
        void DestroySkyboxPipeline();
        void InitializeShaderStages();
        bool EnsureShaderBinaries(std::vector<ShaderStage>& shaderStages);
        bool CompileShaderStage(ShaderStage& shaderStage);
        std::string LocateShaderCompiler() const;
        VkFormat SelectDepthFormat() const;
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        VkShaderModule CreateShaderModule(const std::vector<char>& code);

    private:
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_SkyboxPipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_SkyboxPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_SkyboxDescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;
        std::vector<VkImage> m_SwapchainDepthImages;
        std::vector<VkDeviceMemory> m_SwapchainDepthMemory;
        std::vector<VkImageView> m_SwapchainDepthImageViews;
        VkFormat m_DepthFormat = VK_FORMAT_UNDEFINED;
        std::vector<ShaderStage> m_ShaderStages;
        std::vector<ShaderStage> m_SkyboxShaderStages;
    };
}