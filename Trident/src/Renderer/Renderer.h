#pragma once

#include "Core/Utilities.h"

#include "Renderer/RenderData.h"
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
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/SpriteComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/LightComponent.h"
#include "Camera/Camera.h"
#include "Camera/CameraComponent.h"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_map>

namespace Trident
{
    namespace UI { class ImGuiLayer; }
    namespace ECS { class Registry; }

    struct ViewportInfo
    {
        uint32_t ViewportID = 0;
        glm::vec2 Position{ 0.0f };
        glm::vec2 Size{ 0.0f };
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

        // Allow external systems to adjust the render target clear colour while keeping editor and runtime in sync.
        void SetClearColor(const glm::vec4& color);
        // Returns the colour applied when clearing render targets so UI layers can present the current state.
        glm::vec4 GetClearColor() const { return m_ClearColor; }

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
        void SetViewportCamera(ECS::Entity cameraEntity);

        Transform GetTransform() const;
        ViewportInfo GetViewport() const { return m_Viewport; }
        VkDescriptorSet GetViewportTexture() const;
        ECS::Entity GetViewportCamera() const { return m_ViewportCamera; }

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
        struct MeshDrawInfo
        {
            uint32_t m_FirstIndex = 0;            ///< First index in the shared buffer for the mesh.
            uint32_t m_IndexCount = 0;            ///< Number of indices the draw call should submit.
            int32_t m_BaseVertex = 0;             ///< Base vertex offset applied during drawing.
            int32_t m_MaterialIndex = -1;         ///< Material resolved at upload time.
        };

        struct MeshDrawCommand
        {
            glm::mat4 m_ModelMatrix{ 1.0f };      ///< Cached transform ready for the GPU.
            const MeshComponent* m_Component = nullptr; ///< Source component describing the draw parameters.
            ECS::Entity m_Entity = 0;             ///< Owning entity for debugging and picking hooks.
        };

        struct SpriteDrawCommand
        {
            glm::mat4 m_ModelMatrix{ 1.0f };        ///< Cached transform ready for GPU submission.
            const SpriteComponent* m_Component = nullptr; ///< Pointer into ECS storage for sprite properties.
            ECS::Entity m_Entity = 0;               ///< Owning entity for debugging and future sorting.
        };

        struct CameraSnapshot
        {
            glm::mat4 View{ 1.0f };
            glm::vec3 Position{ 0.0f };
            float FieldOfView = 45.0f;
            float NearClip = 0.1f;
            float FarClip = 100.0f;
        };

        CameraSnapshot ResolveViewportCamera() const;
        void GatherMeshDraws();
        void BuildSpriteGeometry();
        void DestroySpriteGeometry();
        void GatherSpriteDraws();
        void DrawSprites(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        // Swapchain
        Swapchain m_Swapchain;

        // Cache the swapchain image layouts so we can transition from the correct state each frame and keep validation quiet.
        std::vector<VkImageLayout> m_SwapchainImageLayouts;

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
        VkBuffer m_SpriteVertexBuffer = VK_NULL_HANDLE;      ///< Shared quad geometry for batched sprites.
        VkDeviceMemory m_SpriteVertexMemory = VK_NULL_HANDLE;///< Memory backing the sprite vertex buffer.
        VkBuffer m_SpriteIndexBuffer = VK_NULL_HANDLE;       ///< Index buffer referencing the shared quad.
        VkDeviceMemory m_SpriteIndexMemory = VK_NULL_HANDLE; ///< Memory backing the sprite index buffer.
        uint32_t m_SpriteIndexCount = 0;                    ///< Number of indices issued per sprite draw.
        std::vector<MeshDrawInfo> m_MeshDrawInfo;           ///< Cached draw metadata for each uploaded mesh.
        std::vector<MeshDrawCommand> m_MeshDrawCommands;    ///< Mesh draw list gathered per-frame from the ECS registry.

        struct OffscreenTarget
        {
            // Vulkan handles owned by the renderer; lifetime is managed explicitly via DestroyOffscreenResources.
            VkImage m_Image = VK_NULL_HANDLE;
            VkDeviceMemory m_Memory = VK_NULL_HANDLE;
            VkImageView m_ImageView = VK_NULL_HANDLE;
            VkImage m_DepthImage = VK_NULL_HANDLE;
            VkDeviceMemory m_DepthMemory = VK_NULL_HANDLE;
            VkImageView m_DepthView = VK_NULL_HANDLE;
            VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
            VkDescriptorSet m_TextureID = VK_NULL_HANDLE;
            VkSampler m_Sampler = VK_NULL_HANDLE;
            VkExtent2D m_Extent{ 0, 0 };
            VkImageLayout m_CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout m_DepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        };

        // Offscreen rendering resources keyed by viewport identifier so multiple panels can co-exist.
        std::unordered_map<uint32_t, OffscreenTarget> m_OffscreenTargets;
        uint32_t m_ActiveViewportId = 0;
        ECS::Entity m_ViewportCamera = std::numeric_limits<ECS::Entity>::max();

        Buffers m_Buffers;

        size_t m_MaxVertexCount = 0;
        size_t m_MaxIndexCount = 0;
        std::unique_ptr<Vertex[]> m_StagingVertices;
        std::unique_ptr<uint32_t[]> m_StagingIndices;
        std::vector<Geometry::Material> m_Materials; // CPU copy of the material table used during shading
        std::vector<SpriteDrawCommand> m_SpriteDrawList;    ///< Cached list of sprites visible for the current frame.

        ECS::Entity m_Entity = 0;
        ECS::Registry* m_Registry = nullptr;
        ViewportInfo m_Viewport{};
        Camera m_Camera{};
        Skybox m_Skybox{};

        UI::ImGuiLayer* m_ImGuiLayer = nullptr;
        size_t m_FrameAllocationCount = 0;

        size_t m_ModelCount = 0;
        size_t m_TriangleCount = 0;

        static constexpr uint32_t s_MaxPointLights = kMaxPointLights; ///< Mirror uniform buffer light budget.
        static constexpr glm::vec3 s_DefaultDirectionalDirection{ -0.5f, -1.0f, -0.3f }; ///< Fallback sun direction.
        static constexpr glm::vec3 s_DefaultDirectionalColor{ 1.0f, 0.98f, 0.92f }; ///< Warm sunlight tint.
        static constexpr float s_DefaultDirectionalIntensity = 5.0f; ///< Brightness used when no lights exist.
        glm::vec3 m_AmbientColor{ 0.03f };  // Ambient tint simulating image-based lighting
        float m_AmbientIntensity = 1.0f;     // Scalar multiplier for ambient contribution
        glm::vec4 m_ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f }; // Default background colour used for both offscreen and swapchain clears

        // Performance metrics
        static constexpr size_t s_PerformanceHistorySize = 240;
        std::vector<FrameTimingSample> m_PerformanceHistory;
        size_t m_PerformanceHistoryNextIndex = 0;
        size_t m_PerformanceSampleCount = 0;
        FrameTimingStats m_PerformanceStats{};
        bool m_PerformanceCaptureEnabled = false;
        std::vector<FrameTimingSample> m_PerformanceCaptureBuffer;
        std::chrono::system_clock::time_point m_PerformanceCaptureStartTime{};
        std::vector<VkImageLayout> m_SwapchainDepthLayouts;

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
        void DestroyOffscreenResources(uint32_t viewportId);
        void DestroyAllOffscreenResources();
        OffscreenTarget& GetOrCreateOffscreenTarget(uint32_t viewportId);
        void CreateOrResizeOffscreenResources(OffscreenTarget& target, VkExtent2D extent);
    };
}