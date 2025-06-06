#pragma once

#include "Core/Utilities.h"
#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"
#include "Geometry/Cube.h"

#include <vulkan/vulkan.h>

#include <vector>
#include <optional>

namespace Trident
{
    struct SwapchainSupportDetails
    {
        VkSurfaceCapabilitiesKHR Capabilities;
        std::vector<VkSurfaceFormatKHR> Formats;
        std::vector<VkPresentModeKHR> PresentModes;
    };

    class Renderer
    {
    public:
        void Init();
        void Shutdown();
        void DrawFrame();

    private:
        // Swapchain
        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> m_SwapchainImages;
        std::vector<VkImageView> m_SwapchainImageViews;
        VkFormat m_SwapchainImageFormat;
        VkExtent2D m_SwapchainExtent;

        // Buffers
        VkBuffer m_VertexBuffer;
        VkDeviceMemory m_VertexBufferMemory;
        VkBuffer m_IndexBuffer;
        VkDeviceMemory m_IndexBufferMemory;
        uint32_t m_IndexCount;

        // Pipeline objects
        VkRenderPass m_RenderPass;
        VkPipelineLayout m_PipelineLayout;
        VkPipeline m_GraphicsPipeline;

        // Framebuffers
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;

        // Command buffers
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;

        // Synchronization
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;

        VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_Descriptor = VK_NULL_HANDLE;

        std::vector<VkBuffer> m_UniformBuffers;
        std::vector<VkDeviceMemory> m_UniformBuffersMemory;

        std::vector<VkDescriptorSet> m_DescriptorSets;

    private:
        // Core setup
        void CreateSwapchain();
        void CreateImageViews();
        void CreateRenderPass();
        void CreateDescriptorSetLayout();
        void CreateGraphicsPipeline();
        void CreateFramebuffers();
        void CreateCommandPool();
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CreateUniformBuffer();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void CreateCommandBuffer();
        void CreateSyncObjects();

        // Helpers
        VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& shaderCode);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
        VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    };
}