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
#include "Geometry/Material.h"
#include "Loader/TextureLoader.h"

#include "ECS/Entity.h"
#include "Camera/Camera.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <chrono>

namespace Trident
{
    namespace UI { class ImGuiLayer; }
    namespace ECS { class Registry; }

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

    struct DirectionalLight
    {
        glm::vec3 Direction{ -0.5f, -1.0f, -0.3f }; // Default sun direction
        float Intensity{ 5.0f };                     // Scalar intensity multiplier
        glm::vec3 Color{ 1.0f, 0.98f, 0.92f };       // Slightly warm white light
        float Padding{ 0.0f };                       // Padding to keep std140 alignment stable
    };


    class Renderer
    {
    public:
        // Stores a single frame timing measurement captured from the render loop.
        struct FrameTimingSample
        {
            double FrameMilliseconds = 0.0;
            double FramesPerSecond = 0.0;
            VkExtent2D Extent{ 0, 0 };
            std::chrono::system_clock::time_point CaptureTime{};
        };

        // Aggregated statistics calculated from the ring buffer for quick heads-up display consumption.
        struct FrameTimingStats
        {
            double AverageMilliseconds = 0.0;
            double MinimumMilliseconds = 0.0;
            double MaximumMilliseconds = 0.0;
            double AverageFPS = 0.0;
        };

        ~Renderer();

        void Init();
        void Shutdown();
        void DrawFrame();

        void RecreateSwapchain();
        void UploadMesh(const std::vector<Geometry::Mesh>& meshes, const std::vector<Geometry::Material>& materials);
        void UploadTexture(const Loader::TextureData& texture);
        void SetImGuiLayer(UI::ImGuiLayer* layer);

        uint32_t GetCurrentFrame() const { return m_Commands.CurrentFrame(); }
        size_t GetLastFrameAllocationCount() const { return m_FrameAllocationCount; }
        size_t GetModelCount() const { return m_ModelCount; }
        size_t GetTriangleCount() const { return m_TriangleCount; }
        const FrameTimingStats& GetFrameTimingStats() const { return m_PerformanceStats; }
        size_t GetFrameTimingHistoryCount() const { return m_PerformanceSampleCount; }
        const std::vector<FrameTimingSample>& GetFrameTimingHistory() const { return m_PerformanceHistory; }
        size_t GetPerformanceCaptureSampleCount() const { return m_PerformanceCaptureBuffer.size(); }
        bool IsPerformanceCaptureEnabled() const { return m_PerformanceCaptureEnabled; }
        void SetPerformanceCaptureEnabled(bool enabled);

        void SetTransform(const Transform& props);
        void SetViewport(const ViewportInfo& info);

        Transform GetTransform() const;
        ViewportInfo GetViewport() const { return m_Viewport; }
        VkDescriptorSet GetViewportTexture() const;

        Camera& GetCamera() { return m_Camera; }
        const Camera& GetCamera() const { return m_Camera; }

        // Access to the CPU-side material cache so editor widgets can tweak shading values.
        std::vector<Geometry::Material>& GetMaterials() { return m_Materials; }
        const std::vector<Geometry::Material>& GetMaterials() const { return m_Materials; }

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
        std::vector<VkBuffer> m_GlobalUniformBuffers;
        std::vector<VkDeviceMemory> m_GlobalUniformBuffersMemory;
        std::vector<VkBuffer> m_MaterialUniformBuffers;
        std::vector<VkDeviceMemory> m_MaterialUniformBuffersMemory;
        VkImage m_TextureImage = VK_NULL_HANDLE;
        VkDeviceMemory m_TextureImageMemory = VK_NULL_HANDLE;
        VkImageView m_TextureImageView = VK_NULL_HANDLE;
        VkSampler m_TextureSampler = VK_NULL_HANDLE;

        // Offscreen rendering
        VkImage m_OffscreenImage = VK_NULL_HANDLE;
        VkDeviceMemory m_OffscreenMemory = VK_NULL_HANDLE;
        VkImageView m_OffscreenImageView = VK_NULL_HANDLE;
        VkFramebuffer m_OffscreenFramebuffer = VK_NULL_HANDLE;
        // Cache the ImGui descriptor set so we can reuse the image binding across frames without re-registering it.
        VkSampler m_OffscreenSampler = VK_NULL_HANDLE;
        VkExtent2D m_OffscreenExtent{ 0, 0 };

        Buffers m_Buffers;

        size_t m_MaxVertexCount = 0;
        size_t m_MaxIndexCount = 0;
        std::unique_ptr<Vertex[]> m_StagingVertices;
        std::unique_ptr<uint32_t[]> m_StagingIndices;
        std::vector<Geometry::Material> m_Materials; // CPU copy of the material table used during shading

        ECS::Entity m_Entity = 0;
        ECS::Registry* m_Registry = nullptr;
        ViewportInfo m_Viewport{};
        Camera m_Camera{};
        Skybox m_Skybox{};

        UI::ImGuiLayer* m_ImGuiLayer = nullptr;
        size_t m_FrameAllocationCount = 0;

        size_t m_ModelCount = 0;
        size_t m_TriangleCount = 0;

        DirectionalLight m_MainLight{};      // Simple directional light driving direct illumination
        glm::vec3 m_AmbientColor{ 0.03f };  // Ambient tint simulating image-based lighting
        float m_AmbientIntensity = 1.0f;     // Scalar multiplier for ambient contribution

        // Performance metrics
        static constexpr size_t s_PerformanceHistorySize = 240;
        std::vector<FrameTimingSample> m_PerformanceHistory;
        size_t m_PerformanceHistoryNextIndex = 0;
        size_t m_PerformanceSampleCount = 0;
        FrameTimingStats m_PerformanceStats{};
        bool m_PerformanceCaptureEnabled = false;
        std::vector<FrameTimingSample> m_PerformanceCaptureBuffer;
        std::chrono::system_clock::time_point m_PerformanceCaptureStartTime{};

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
        void ProcessReloadEvents();
        void AccumulateFrameTiming(double frameMilliseconds, double framesPerSecond, VkExtent2D extent, std::chrono::system_clock::time_point captureTimestamp);
        void UpdateFrameTimingStats();
        void ExportPerformanceCapture();
        void DestroyOffscreenResources();
        void CreateOrResizeOffscreenResources(VkExtent2D extent);
    };
}