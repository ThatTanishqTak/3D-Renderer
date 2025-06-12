#pragma once

#include "Core/Utilities.h"

#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffers.h"
#include "Renderer/Commands.h"

#include "Geometry/Cube.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <functional>

namespace Trident
{
    struct CubeProperties
    {
        glm::vec3 Position{ 0.0f };
        glm::vec3 Rotation{ 0.0f };
        glm::vec3 Scale{ 1.0f };
    };

    struct ViewportInfo
    {
        glm::vec2 Position{ 0.0f };
        glm::vec2 Size{ 0.0f };
    };

    class Renderer
    {
    public:
        void Init();
        void Shutdown();
        void DrawFrame();

        void SetCubeProperties(const CubeProperties& props) { m_CubeProperties = props; }
        CubeProperties GetCubeProperties() const { return m_CubeProperties; }
        void SetViewport(const ViewportInfo& info) { m_Viewport = info; }
        ViewportInfo GetViewport() const { return m_Viewport; }

        VkRenderPass GetRenderPass() const { return m_Pipeline.GetRenderPass(); }
        uint32_t GetImageCount() const { return m_Swapchain.GetImageCount(); }

        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_Pipeline.GetDescriptorSetLayout(); }

        void RecreateSwapchain();
        VkCommandPool GetCommandPool() const { return m_Commands.GetCommandPool(); }
        std::vector<VkCommandBuffer> GetCommandBuffer() const { return m_Commands.GetCommandBuffers(); }

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

        // Command pool, buffers and sync objects
        Commands m_Commands;

        // Descriptor sets & uniform buffers
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        std::vector<VkBuffer> m_UniformBuffers;
        std::vector<VkDeviceMemory> m_UniformBuffersMemory;

        Buffers m_Buffers;

        CubeProperties m_CubeProperties{};
        ViewportInfo m_Viewport{};

    private:
        // Core setup
        void CreateDescriptorPool();
        void CreateDescriptorSets();
    };
}