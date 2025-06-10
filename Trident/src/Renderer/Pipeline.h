#pragma once

#include <vulkan/vulkan.h>
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

        VkRenderPass GetRenderPass() const { return m_RenderPass; }
        VkPipeline GetPipeline() const { return m_GraphicsPipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_DescriptorSetLayout; }
        const std::vector<VkFramebuffer>& GetFramebuffers() const { return m_SwapchainFramebuffers; }

    private:
        void CreateRenderPass(Swapchain& swapchain);
        void CreateDescriptorSetLayout();
        void CreateGraphicsPipeline(Swapchain& swapchain);

        VkShaderModule CreateShaderModule(const std::vector<char>& code);

    private:
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_GraphicsPipeline = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;
    };
}