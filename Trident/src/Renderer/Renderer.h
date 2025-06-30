#pragma once

#include "Core/Utilities.h"

#include "Renderer/Vertex.h"
#include "Renderer/UniformBuffer.h"
#include "Renderer/Swapchain.h"
#include "Renderer/Pipeline.h"
#include "Renderer/Buffers.h"
#include "Renderer/Commands.h"
#include "Renderer/Skybox.h"

#include "Geometry/Mesh.h"
#include "Loader/TextureLoader.h"

#include "Camera/Camera.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <vector>
#include <array>
#include <functional>

namespace Trident
{
    namespace UI { class ImGuiLayer; }

    struct Transform
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
        ~Renderer();

        void Init();
        void Shutdown();
        void DrawFrame();

        void RecreateSwapchain();
        void UploadMesh(const std::vector<Geometry::Mesh>& meshes);
        void UploadTexture(const Loader::TextureData& texture);
        void SetImGuiLayer(UI::ImGuiLayer* layer);

        uint32_t GetCurrentFrame() const { return m_Commands.CurrentFrame(); }

        void SetTransform(const Transform& props) { m_Transform = props; }
        void SetViewport(const ViewportInfo& info) { m_Viewport = info; }
        Transform GetTransform() const { return m_Transform; }
        ViewportInfo GetViewport() const { return m_Viewport; }

        Camera& GetCamera() { return m_Camera; }
        const Camera& GetCamera() const { return m_Camera; }

        VkRenderPass GetRenderPass() const { return m_Pipeline.GetRenderPass(); }
        uint32_t GetImageCount() const { return m_Swapchain.GetImageCount(); }

        VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_Pipeline.GetDescriptorSetLayout(); }
        VkCommandPool GetCommandPool() const { return m_Commands.GetCommandPool(); }
        std::vector<VkCommandBuffer> GetCommandBuffer() const { return m_Commands.GetCommandBuffers(); }

        bool m_Shutdown = false;

    private:
        // Swapchain
        Swapchain m_Swapchain;

        // Buffers
        VkBuffer m_VertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer m_IndexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;
        uint32_t m_IndexCount = 0;

        // Pipeline
        Pipeline m_Pipeline;

        // Command pool, buffers and sync objects
        Commands m_Commands;
        VkFence m_ResourceFence = VK_NULL_HANDLE;

        // Descriptor sets & uniform buffers
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_DescriptorSets;
        std::vector<VkBuffer> m_UniformBuffers;
        std::vector<VkDeviceMemory> m_UniformBuffersMemory;
        VkImage m_TextureImage = VK_NULL_HANDLE;
        VkDeviceMemory m_TextureImageMemory = VK_NULL_HANDLE;
        VkImageView m_TextureImageView = VK_NULL_HANDLE;
        VkSampler m_TextureSampler = VK_NULL_HANDLE;

        // Offscreen rendering
        VkImage m_OffscreenImage = VK_NULL_HANDLE;
        VkDeviceMemory m_OffscreenMemory = VK_NULL_HANDLE;
        VkImageView m_OffscreenImageView = VK_NULL_HANDLE;
        VkFramebuffer m_OffscreenFramebuffer = VK_NULL_HANDLE;
        ImTextureID m_OffscreenTextureID;
        VkSampler m_OffscreenSampler = VK_NULL_HANDLE;

        Buffers m_Buffers;

        Transform m_Transform{};
        ViewportInfo m_Viewport{};
        Camera m_Camera{};
        Skybox m_Skybox{};

        UI::ImGuiLayer* m_ImGuiLayer = nullptr;

    private:
        // Core setup
        void CreateDescriptorPool();
        void CreateDefaultTexture();
        void CreateDefaultSkybox();
        void CreateDescriptorSets();

        void UpdateUniformBuffer(uint32_t currentImage);

        bool AcquireNextImage(uint32_t& imageIndex, VkFence inFlightFence);
        bool RecordCommandBuffer(uint32_t imageIndex);
        bool SubmitFrame(uint32_t imageIndex, VkFence inFlightFence);
        void PresentFrame(uint32_t imageIndex);

        bool IsValidViewport() const { return m_Viewport.Size.x > 0 && m_Viewport.Size.y > 0; }
    };
}