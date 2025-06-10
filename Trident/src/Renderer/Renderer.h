#pragma once

#include "Core/Utilities.h"
#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Pipeline.h"

#include "Geometry/Cube.h"

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <functional>

namespace Trident
{
    class Renderer
    {
    public:
        void Init();
        void Shutdown();
        void DrawFrame();

        VkRenderPass GetRenderPass() const { return m_Pipeline.GetRenderPass(); }
        uint32_t GetImageCount() const { return m_Swapchain.GetImageCount(); }

        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_Pipeline.GetDescriptorSetLayout(); }

        void RecreateSwapchain();
        VkCommandPool GetCommandPool() const { return m_CommandPool; }
        std::vector<VkCommandBuffer> GetCommandBuffer() const { return m_CommandBuffers; }

    private:
        // Swapchain
        Swapchain m_Swapchain;

        // Buffers
        VkBuffer m_VertexBuffer;
        VkDeviceMemory m_VertexBufferMemory;
        VkBuffer m_IndexBuffer;
        VkDeviceMemory m_IndexBufferMemory;
        uint32_t m_IndexCount;

        // Pipeline
        Pipeline m_Pipeline;

        // Command pool & buffers
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_CommandBuffers;

        // Synchronization (frames-in-flight pattern)
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
        std::vector<VkSemaphore> m_ImageAvailableSemaphores;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        std::vector<VkFence> m_InFlightFences;

        // Track which fence is using each swapchain image
        std::vector<VkFence> m_ImagesInFlight;
        size_t m_CurrentFrame = 0;

        // Descriptor sets & uniform buffers
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        std::vector<VkBuffer> m_UniformBuffers;
        std::vector<VkDeviceMemory> m_UniformBuffersMemory;

    private:
        // Core setup
        void CreateCommandPool();
        void CreateVertexBuffer();
        void CreateIndexBuffer();
        void CreateUniformBuffer();
        void CreateDescriptorPool();
        void CreateDescriptorSets();
        void CreateCommandBuffer();
        void CreateSyncObjects();

        // Utility helpers
        VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
        void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    };
}